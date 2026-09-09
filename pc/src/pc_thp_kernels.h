/* pc_thp_kernels.h -- native C equivalents of THPDec.c's MWCC-only kernels.
 *
 * THPDec.c decodes THP/MTH video with two pieces written only as MWCC inline
 * PowerPC assembly: the Huffman/receive entropy decoder, and a paired-single
 * AAN inverse DCT. Neither has a C fallback, so a host build reaches
 * __THPHuffDecodeDCTCompY with an uninitialised lookup result. These are the
 * replacements, called from THPDec.c's `#else` branches so the MWCC paths
 * stay exactly as they were.
 *
 * They live in a header of their own, like pc_mth.h and pc_hps.h, so that
 * pc/tests/thp_kernel_test.c can exercise them directly with known answers.
 * A build that links is not evidence that an entropy coder is correct.
 *
 * Two facts about the console code drive the whole design:
 *
 * 1. THE BITSTREAM IS BIG-ENDIAN, IN 32-BIT WORDS. The decoder does not read
 *    bytes. It caches one 32-bit word in info->currByte, consumes it MSB
 *    first, and refills with a `lwz` -- a big-endian load. info->cnt is
 *    1 + (bits consumed from that word), so 1 means untouched and 33 means
 *    exhausted. On x86 the equivalent `*(u32*)p` reads the opposite order, so
 *    every code would be decoded from reversed bytes. pc_thp_load_word is the
 *    fix, and it is why a correct Huffman decoder alone would not have been
 *    enough. This is the only endian exposure in the THP path: markers,
 *    quantisation tables and Huffman bit counts are all read a byte at a time
 *    by code that already compiles for either byte order.
 *
 * 2. THE IDCT WRITES GX-TILED I8 DIRECTLY. The paired-single store kernel is
 *    libjpeg-6b's jidctflt.c -- identical AAN constants, identical scale
 *    factor table -- with the output addressed so that consecutive rows land
 *    8 bytes apart and every eight columns start a new 32-byte group. That is
 *    the GX_TF_I8 tile layout, which is what lbMthp_8001F67C then hands to
 *    GXInitTexObj as GX_TF_I8. So the decoder's output plane is already a
 *    texture; nothing re-tiles it afterwards.
 *
 * The quantisation table reaching pc_thp_idct is pre-scaled by
 * __THPAANScaleFactor[row] * __THPAANScaleFactor[col] but NOT by libjpeg's
 * 1/8. The console recovers that 1/8 in the quantised store (GQR6 = 0x3D04,
 * type u8, scale -3), which is also where the level shift lands: the column
 * pass adds a bias of 1024, and 1024/8 is JPEG's +128. pc_thp_store_u8 does
 * both.
 */
#ifndef PC_THP_KERNELS_H
#define PC_THP_KERNELS_H

/* ---------------------------------------------------------------- bits --- */

/* `file` addresses the four bytes `word` was loaded from; `cnt` is
   1 + the number of bits already consumed from `word`, so 33 means empty. */
typedef struct PcThpBitState {
    const unsigned char* file;
    unsigned word;
    unsigned cnt;
} PcThpBitState;

/* The fields THPHuffmanTab exposes to the decoder. quick[] answers any code
   of five bits or fewer and holds 0xFF otherwise; increment[] is that code's
   length. maxCode/valPtr/Vij are the canonical-Huffman fallback, with
   maxCode[17] a sentinel large enough that the search always terminates. */
typedef struct PcThpHuffView {
    const unsigned char* quick;
    const unsigned char* increment;
    const unsigned char* Vij;
    const int* maxCode;
    const int* valPtr;
} PcThpHuffView;

static unsigned pc_thp_load_word(const unsigned char* p)
{
    return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 |
           (unsigned) p[2] << 8 | (unsigned) p[3];
}

static unsigned pc_thp_mask(unsigned n)
{
    return n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u;
}

static void pc_thp_next_word(PcThpBitState* s)
{
    s->file += 4;
    s->word = pc_thp_load_word(s->file);
}

/* Read n bits MSB first. n is at most 16 here (a JPEG magnitude category), so
   a read spans at most one word boundary. Mirrors the `slw`/`srw` pair the
   MWCC receive kernels use, including the case where the word is already
   exhausted (cnt == 33, so nothing is kept from it). */
static unsigned pc_thp_receive(PcThpBitState* s, unsigned n)
{
    unsigned avail = 33 - s->cnt;
    unsigned kept, take, v;

    if (n <= avail) {
        v = (s->word >> (avail - n)) & pc_thp_mask(n);
        s->cnt += n;
        return v;
    }
    kept = s->word & pc_thp_mask(avail);
    take = n - avail;
    pc_thp_next_word(s);
    v = (kept << take) | (s->word >> (32 - take));
    s->cnt = take + 1;
    return v;
}

/* JPEG's HUFF_EXTEND. The console spells the same test as
   __cntlzw(v) > 32 - n, which is true exactly when v < 2^(n-1). */
static int pc_thp_extend(unsigned v, unsigned n)
{
    if (v < (1u << (n - 1))) {
        return (int) (v + ((~0u << n) + 1u));
    }
    return (int) v;
}

/* Decode one Huffman symbol, or -1 if the code is longer than any in the
   table -- which means the bitstream or the table is wrong, and the caller
   reports it rather than continuing on a guess.

   The five-bit quick lookup needs five bits to index with. When fewer remain
   in the current word the console peeks past the end anyway and accepts the
   answer only if the code turned out to fit; zero-filling instead is
   equivalent, because quick[] returns the SHORTEST matching prefix, so a code
   that fits the real bits is found whatever the filler is, and one that does
   not is rejected by the same length test. */
static int pc_thp_huff_decode(PcThpBitState* s, const PcThpHuffView* h)
{
    unsigned avail, peek, len, code, sym, inc;

    if (s->cnt == 33) {
        pc_thp_next_word(s);
        s->cnt = 1;
    }
    avail = 33 - s->cnt;

    if (avail >= 5) {
        peek = (s->word >> (avail - 5)) & 0x1Fu;
        sym = h->quick[peek];
        if (sym != 0xFFu) {
            s->cnt += h->increment[peek];
            return (int) sym;
        }
        code = peek;
        len = 5;
        s->cnt += 5;
    } else {
        peek = (s->word & pc_thp_mask(avail)) << (5 - avail);
        sym = h->quick[peek];
        inc = h->increment[peek];
        if (sym != 0xFFu && inc <= avail) {
            s->cnt += inc;
            return (int) sym;
        }
        code = s->word & pc_thp_mask(avail);
        len = avail;
        s->cnt = 33;
    }

    /* Canonical search. maxCode[l] is -1 where no code has length l, and a
       code of 17 bits does not exist, so falling out at len == 17 means
       nothing matched. The console's own loop leans on maxCode[17] being a
       large sentinel and would then index Vij with a 17-bit code; stop
       instead, because a wrong symbol here corrupts the rest of the scan. */
    while (len <= 16 && (int) code > h->maxCode[len]) {
        code = (code << 1) | pc_thp_receive(s, 1);
        len++;
    }
    if (len > 16) {
        return -1;
    }
    return h->Vij[(int) code + h->valPtr[len]];
}

/* One 8x8 block: DC difference against the running predictor, then
   run/size-coded AC coefficients placed through the zig-zag order.

   `order` is THPDec.c's own __THPJpegNaturalOrder, passed in rather than
   copied here so there is only one such table in the build. It has 80 entries
   whose last sixteen are all 63, which is what makes the unchecked `k += run`
   below safe for the largest run a corrupt stream can express. */
static int pc_thp_decode_block(PcThpBitState* s, const PcThpHuffView* dc_tab,
                               const PcThpHuffView* ac_tab,
                               const unsigned char* order, short* pred,
                               short* block)
{
    int t, k, sym, run, size, diff = 0;

    for (k = 0; k < 64; k++) {
        block[k] = 0;
    }

    t = pc_thp_huff_decode(s, dc_tab);
    if (t < 0 || t > 16) {
        return -1;
    }
    if (t != 0) {
        diff = pc_thp_extend(pc_thp_receive(s, (unsigned) t), (unsigned) t);
    }
    *pred = (short) (*pred + diff);
    block[0] = *pred;

    for (k = 1; k < 64; k++) {
        sym = pc_thp_huff_decode(s, ac_tab);
        if (sym < 0) {
            return -1;
        }
        size = sym & 15;
        run = sym >> 4;
        if (size != 0) {
            k += run;
            block[order[k]] = (short) pc_thp_extend(
                pc_thp_receive(s, (unsigned) size), (unsigned) size);
        } else {
            if (run != 15) {
                break;
            }
            k += 15;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- idct --- */

/* Byte offset of pixel (x, y) in a GX_TF_I8 image `wid` pixels wide: 8x4
   tiles, tiles left to right, four-row tile bands top to bottom. This is what
   the paired-single kernel's out0/out1 pointers and its 0/8/16/24 store
   offsets spell out one register at a time. */
static unsigned pc_thp_tile_offset(unsigned wid, unsigned x, unsigned y)
{
    return (y >> 2) * (wid << 2) + (x >> 3) * 32u + (y & 3u) * 8u + (x & 7u);
}

/* The quantised store: divide by eight (GQR scale -3), clamp, truncate toward
   zero. The bias the column pass already added becomes JPEG's +128 here. */
static unsigned char pc_thp_store_u8(float v)
{
    float s = v * 0.125f;
    if (!(s > 0.0f)) {
        return 0; /* also catches NaN, which must not reach a cast to int */
    }
    if (s >= 255.0f) {
        return 255;
    }
    return (unsigned char) (int) s;
}

/* Dequantise and inverse-transform one 8x8 block into the tiled plane at
   (x_pos, y_pos). `q` is the AAN-prescaled quantisation table and `in` is a
   64-entry coefficient block in natural (de-zig-zagged) order.

   Both passes are libjpeg-6b's jidctflt.c. The console kernel special-cases
   rows whose AC terms are all zero; those shortcuts are exact -- adding and
   multiplying by zero changes nothing -- so the general form here reproduces
   them, rounding included. */
static void pc_thp_idct(const short* in, const float* q, unsigned char* base,
                        unsigned wid, unsigned x_pos, unsigned y_pos)
{
    const float c4 = 1.414213562f;     /* 2*c4 */
    const float c2 = 1.847759065f;     /* 2*c2 */
    const float c2c6s = 1.082392200f;  /* 2*(c2-c6) */
    const float c2c6a = -2.613125930f; /* -2*(c2+c6) */
    float ws[64];
    int i;

    for (i = 0; i < 8; i++) {
        const short* ip = in + i * 8;
        const float* qp = q + i * 8;
        float* out = ws + i * 8;
        float v0 = ip[0] * qp[0], v1 = ip[1] * qp[1];
        float v2 = ip[2] * qp[2], v3 = ip[3] * qp[3];
        float v4 = ip[4] * qp[4], v5 = ip[5] * qp[5];
        float v6 = ip[6] * qp[6], v7 = ip[7] * qp[7];
        float e0 = v0 + v4, e1 = v0 - v4;
        float e2 = v2 + v6, e3 = (v2 - v6) * c4 - e2;
        float t10 = e0 + e2, t13 = e0 - e2;
        float t11 = e1 + e3, t12 = e1 - e3;
        float z13 = v5 + v3, z10 = v5 - v3;
        float z11 = v1 + v7, z12 = v1 - v7;
        float o7 = z11 + z13;
        float o11 = (z11 - z13) * c4;
        float z5 = (z10 + z12) * c2;
        float o10 = c2c6s * z12 - z5;
        float o12 = c2c6a * z10 + z5;
        float o6 = o12 - o7;
        float o5 = o11 - o6;
        float o4 = o10 + o5;

        out[0] = t10 + o7;
        out[1] = t11 + o6;
        out[2] = t12 + o5;
        out[3] = t13 - o4;
        out[4] = t13 + o4;
        out[5] = t12 - o5;
        out[6] = t11 - o6;
        out[7] = t10 - o7;
    }

    for (i = 0; i < 8; i++) {
        const float* cp = ws + i;
        unsigned x = x_pos + (unsigned) i;
        float v0 = cp[0], v1 = cp[8], v2 = cp[16], v3 = cp[24];
        float v4 = cp[32], v5 = cp[40], v6 = cp[48], v7 = cp[56];
        /* The 1024 rides on both even sums, so each of the four even terms
           carries it exactly once -- the console adds it in the same place. */
        float e0 = v0 + v4 + 1024.0f, e1 = v0 - v4 + 1024.0f;
        float e2 = v2 + v6, e3 = (v2 - v6) * c4 - e2;
        float t10 = e0 + e2, t13 = e0 - e2;
        float t11 = e1 + e3, t12 = e1 - e3;
        float z13 = v5 + v3, z10 = v5 - v3;
        float z11 = v1 + v7, z12 = v1 - v7;
        float o7 = z11 + z13;
        float o11 = (z11 - z13) * c4;
        float z5 = (z10 + z12) * c2;
        float o10 = c2c6s * z12 - z5;
        float o12 = c2c6a * z10 + z5;
        float o6 = o12 - o7;
        float o5 = o11 - o6;
        float o4 = o10 + o5;

        base[pc_thp_tile_offset(wid, x, y_pos + 0)] = pc_thp_store_u8(t10 + o7);
        base[pc_thp_tile_offset(wid, x, y_pos + 1)] = pc_thp_store_u8(t11 + o6);
        base[pc_thp_tile_offset(wid, x, y_pos + 2)] = pc_thp_store_u8(t12 + o5);
        base[pc_thp_tile_offset(wid, x, y_pos + 3)] = pc_thp_store_u8(t13 - o4);
        base[pc_thp_tile_offset(wid, x, y_pos + 4)] = pc_thp_store_u8(t13 + o4);
        base[pc_thp_tile_offset(wid, x, y_pos + 5)] = pc_thp_store_u8(t12 - o5);
        base[pc_thp_tile_offset(wid, x, y_pos + 6)] = pc_thp_store_u8(t11 - o6);
        base[pc_thp_tile_offset(wid, x, y_pos + 7)] = pc_thp_store_u8(t10 - o7);
    }
}

#endif
