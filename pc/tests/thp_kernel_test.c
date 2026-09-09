/* thp_kernel_test.c -- known-answer tests for the native THP kernels.
 *
 * Build (32-bit, matching the port):
 *   gcc -m32 -std=c11 -Wall -Wextra -Werror -O2 -I pc/src \
 *       pc/tests/thp_kernel_test.c -o build/thp_kernel_test.exe
 *
 * What is worth testing here is not that the code links. It is that the
 * entropy decoder consumes the SAME bits the console consumes -- which means
 * big-endian 32-bit words, MSB first, across word boundaries -- and that the
 * IDCT produces the SAME samples in the SAME tiled positions. So every check
 * below compares against an answer derived independently: a bit-by-bit
 * reference reader, a double-precision textbook IDCT, or a value worked out by
 * hand from the JPEG spec.
 */
#include "pc_thp_kernels.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ util - */

/* A Huffman table built exactly the way THPDec.c builds one:
   __THPHuffGenerateSizeTable / CodeTable / DecoderTables, then the quick[]
   fill from __THPPrepBitStream. Reproduced here so the test drives
   pc_thp_huff_decode through the same table shape the game hands it. */
typedef struct Table {
    unsigned char quick[32];
    unsigned char increment[32];
    unsigned char Vij[256];
    int maxCode[18];
    int valPtr[18];
    signed char sizeTab[257];
    unsigned codeTab[256];
    unsigned char bits[16];
    int numCodes;
} Table;

static void table_build(Table* t, const unsigned char bits[16],
                        const unsigned char* vals, int nvals)
{
    int p, l, i, si;
    unsigned code;

    memset(t, 0, sizeof *t);
    memcpy(t->bits, bits, 16);
    memcpy(t->Vij, vals, (size_t) nvals);

    p = 0;
    for (l = 1; l <= 16; l++) {
        i = bits[l - 1];
        while (i--) {
            t->sizeTab[p++] = (signed char) l;
        }
    }
    t->sizeTab[p] = 0;
    t->numCodes = p;
    assert(p == nvals);

    p = 0;
    code = 0;
    si = t->sizeTab[0];
    while (t->sizeTab[p]) {
        while (t->sizeTab[p] == si) {
            t->codeTab[p++] = code++;
        }
        code <<= 1;
        si++;
    }

    p = 0;
    for (l = 1; l <= 16; l++) {
        if (bits[l - 1]) {
            t->valPtr[l] = p - (int) t->codeTab[p];
            p += bits[l - 1];
            t->maxCode[l] = (int) t->codeTab[p - 1];
        } else {
            t->maxCode[l] = -1;
            t->valPtr[l] = -1;
        }
    }
    t->maxCode[17] = 0xfffff;

    /* __THPPrepBitStream's quick-table fill, verbatim in shape. */
    for (i = 0; i < 32; i++) {
        int k;
        t->quick[i] = 0xFF;
        for (k = 0; k < 5; k++) {
            int c = i >> (5 - k - 1);
            if (c <= t->maxCode[k + 1]) {
                t->quick[i] = t->Vij[c + t->valPtr[k + 1]];
                t->increment[i] = (unsigned char) (k + 1);
                break;
            }
        }
    }
}

static void table_view(PcThpHuffView* v, const Table* t)
{
    v->quick = t->quick;
    v->increment = t->increment;
    v->Vij = t->Vij;
    v->maxCode = t->maxCode;
    v->valPtr = t->valPtr;
}

/* Look up a symbol's canonical code, so the test can emit it. */
static void table_code_of(const Table* t, unsigned char sym, unsigned* code,
                          int* len)
{
    int p = 0, l;
    for (l = 0; l < t->numCodes; l++) {
        if (t->Vij[l] == sym) {
            p = l;
            break;
        }
    }
    assert(l < t->numCodes);
    *code = t->codeTab[p];
    *len = t->sizeTab[p];
}

/* An MSB-first bit writer, so the encoder side is independent of the reader
   under test. Emits into a plain byte array; the decoder must then read it as
   big-endian 32-bit words and arrive at the same bits. */
typedef struct Writer {
    unsigned char buf[512];
    int bit;
} Writer;

static void w_bits(Writer* w, unsigned v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) {
        unsigned b = (v >> i) & 1u;
        if (b) {
            w->buf[w->bit >> 3] |= (unsigned char) (0x80u >> (w->bit & 7));
        }
        w->bit++;
        assert((size_t) (w->bit >> 3) < sizeof w->buf);
    }
}

static void state_at(PcThpBitState* s, const unsigned char* buf)
{
    s->file = buf;
    s->word = pc_thp_load_word(buf);
    s->cnt = 1;
}

/* ---------------------------------------------------- byte order and bits - */

static void test_word_order(void)
{
    /* The single most consequential fact about this decoder: it caches a
       big-endian word. A host-order load would answer 0x78563412 here. */
    static const unsigned char be[4] = { 0x12, 0x34, 0x56, 0x78 };
    assert(pc_thp_load_word(be) == 0x12345678u);

    assert(pc_thp_mask(0) == 0u);
    assert(pc_thp_mask(1) == 1u);
    assert(pc_thp_mask(31) == 0x7FFFFFFFu);
    assert(pc_thp_mask(32) == 0xFFFFFFFFu);

    puts("PASS: bitstream words load big-endian");
}

static void test_receive(void)
{
    /* Two words: 0x12345678 0x9ABCDEF0. Reading MSB-first from the first
       word must cross into the second exactly at the boundary. */
    static const unsigned char buf[8] = { 0x12, 0x34, 0x56, 0x78,
                                          0x9A, 0xBC, 0xDE, 0xF0 };
    PcThpBitState s;
    unsigned i, acc;

    state_at(&s, buf);
    assert(pc_thp_receive(&s, 4) == 0x1u); /* 0001 */
    assert(s.cnt == 5);
    assert(pc_thp_receive(&s, 12) == 0x234u);
    assert(s.cnt == 17);
    assert(pc_thp_receive(&s, 16) == 0x5678u);
    assert(s.cnt == 33); /* exactly exhausted, no refill yet */
    assert(s.file == buf);
    assert(pc_thp_receive(&s, 8) == 0x9Au); /* refills here */
    assert(s.file == buf + 4);
    assert(s.cnt == 9);

    /* A read that straddles the boundary. Restart and take 30 then 8: the
       8 must be the last two bits of word 0 followed by the top six of
       word 1 -- 0b00 followed by 0b100110 = 0x26. */
    state_at(&s, buf);
    assert(pc_thp_receive(&s, 30) == (0x12345678u >> 2));
    assert(pc_thp_receive(&s, 8) == 0x26u);
    assert(s.cnt == 7);

    /* Bit-at-a-time must agree with the block reads, boundary included. */
    state_at(&s, buf);
    acc = 0;
    for (i = 0; i < 40; i++) {
        acc = (acc << 1) | pc_thp_receive(&s, 1);
        if (i == 31) {
            assert(acc == 0x12345678u);
            acc = 0;
        }
    }
    assert(acc == 0x9Au);

    puts("PASS: receive is MSB-first and refills across word boundaries");
}

static void test_extend(void)
{
    /* JPEG HUFF_EXTEND, from Table F.2: with n bits, values in the lower
       half are negative and start at -(2^n - 1). */
    assert(pc_thp_extend(1u, 1) == 1);
    assert(pc_thp_extend(0u, 1) == -1);
    assert(pc_thp_extend(3u, 2) == 3);
    assert(pc_thp_extend(2u, 2) == 2);
    assert(pc_thp_extend(1u, 2) == -2);
    assert(pc_thp_extend(0u, 2) == -3);
    assert(pc_thp_extend(0x7FFFu, 15) == 32767);
    assert(pc_thp_extend(0u, 15) == -32767);
    puts("PASS: magnitude-category sign extension matches JPEG Table F.2");
}

/* -------------------------------------------------------------- huffman --- */

/* Codes of one to seven bits, so the five-bit quick table answers some and
   the canonical fallback has to answer the rest. */
static const unsigned char kBits[16] = { 1, 1, 1, 1, 1, 1, 1, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned char kVals[7] = { 0x00, 0x11, 0x22, 0x33,
                                        0x44, 0x55, 0x66 };

static void test_huffman_lengths(void)
{
    Table t;
    PcThpHuffView v;
    Writer w;
    PcThpBitState s;
    int i, len;
    unsigned code;

    table_build(&t, kBits, kVals, 7);
    table_view(&v, &t);

    /* Canonical assignment for one code per length: 0, 10, 110, ... */
    for (i = 0; i < 7; i++) {
        table_code_of(&t, kVals[i], &code, &len);
        assert(len == i + 1);
        assert(code == (((1u << i) - 1u) << 1));
    }

    /* Every symbol, in order, then again -- 2 * (1+..+7) = 56 bits, which
       crosses a word boundary partway through the second pass. */
    memset(&w, 0, sizeof w);
    for (i = 0; i < 14; i++) {
        table_code_of(&t, kVals[i % 7], &code, &len);
        w_bits(&w, code, len);
    }
    state_at(&s, w.buf);
    for (i = 0; i < 14; i++) {
        int sym = pc_thp_huff_decode(&s, &v);
        assert(sym == kVals[i % 7]);
    }

    puts("PASS: Huffman codes of 1..7 bits decode through quick table and "
         "canonical fallback");
}

static void test_huffman_word_boundary(void)
{
    Table t;
    PcThpHuffView v;
    Writer w;
    PcThpBitState s;
    unsigned code;
    int len, pad, sym;

    table_build(&t, kBits, kVals, 7);
    table_view(&v, &t);

    /* Place a seven-bit code so it starts at every bit position from 26 to
       32 -- the cases where the quick lookup has fewer than five real bits,
       and where the code spans the word boundary. Each is a distinct branch
       of the console's decoder. */
    for (pad = 26; pad <= 32; pad++) {
        int i;
        memset(&w, 0, sizeof w);
        for (i = 0; i < pad; i++) {
            w_bits(&w, 0, 1); /* a run of the one-bit code, symbol 0x00 */
        }
        table_code_of(&t, 0x66, &code, &len);
        assert(len == 7);
        w_bits(&w, code, len);

        state_at(&s, w.buf);
        for (i = 0; i < pad; i++) {
            sym = pc_thp_huff_decode(&s, &v);
            assert(sym == 0x00);
        }
        sym = pc_thp_huff_decode(&s, &v);
        assert(sym == 0x66);
    }

    puts("PASS: codes straddling the 32-bit word boundary decode at every "
         "start offset");
}

static void test_huffman_rejects_garbage(void)
{
    /* A table with no code longer than four bits, fed all ones: no code can
       match, and the decoder must say so rather than return a symbol. */
    static const unsigned char bits[16] = { 0, 0, 0, 1, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 0 };
    static const unsigned char vals[1] = { 0x5A };
    unsigned char stream[16];
    Table t;
    PcThpHuffView v;
    PcThpBitState s;

    table_build(&t, bits, vals, 1);
    table_view(&v, &t);
    memset(stream, 0xFF, sizeof stream);
    state_at(&s, stream);
    assert(pc_thp_huff_decode(&s, &v) == -1);

    /* The one legal code, 0b0000, still decodes. */
    memset(stream, 0x00, sizeof stream);
    state_at(&s, stream);
    assert(pc_thp_huff_decode(&s, &v) == 0x5A);

    puts("PASS: an unmatchable code is reported, not guessed");
}

/* ---------------------------------------------------------------- block --- */

static const unsigned char kOrder[80] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63
};

/* Derive the zig-zag independently, so kOrder above is checked rather than
   trusted -- it has to be the same table THPDec.c passes in. */
static void test_zigzag(void)
{
    unsigned char z[64];
    int x = 0, y = 0, i;
    for (i = 0; i < 64; i++) {
        z[i] = (unsigned char) (y * 8 + x);
        if (((x + y) & 1) == 0) { /* moving up-right */
            if (x == 7) {
                y++;
            } else if (y == 0) {
                x++;
            } else {
                x++;
                y--;
            }
        } else { /* moving down-left */
            if (y == 7) {
                x++;
            } else if (x == 0) {
                y++;
            } else {
                x--;
                y++;
            }
        }
    }
    assert(!memcmp(z, kOrder, 64));
    for (i = 64; i < 80; i++) {
        assert(kOrder[i] == 63);
    }
    puts("PASS: natural-order table is the JPEG zig-zag, with a clamped tail");
}

/* DC and AC tables large enough to encode arbitrary categories. */
static void build_dc_ac(Table* dc, Table* ac)
{
    static const unsigned char dbits[16] = { 0, 12, 0, 0, 0, 0, 0, 0,
                                             0, 0,  0, 0, 0, 0, 0, 0 };
    static unsigned char dvals[12];
    static const unsigned char abits[16] = { 0, 0, 0, 0, 0, 0, 0, 0,
                                             16, 0, 0, 0, 0, 0, 0, 0 };
    static unsigned char avals[16];
    int i;

    for (i = 0; i < 12; i++) {
        dvals[i] = (unsigned char) i; /* symbol i is category i */
    }
    /* 16 AC symbols: EOB (0x00), ZRL (0xF0), and run 0 with sizes 1..14. */
    avals[0] = 0x00;
    avals[1] = 0xF0;
    for (i = 2; i < 16; i++) {
        avals[i] = (unsigned char) (i - 1); /* run 0, size 1..14 */
    }
    table_build(dc, dbits, dvals, 12);
    table_build(ac, abits, avals, 16);
}

static void emit_value(Writer* w, const Table* t, unsigned char sym, int value,
                       int size)
{
    unsigned code;
    int len;
    table_code_of(t, sym, &code, &len);
    w_bits(w, code, len);
    if (size > 0) {
        unsigned v = (value >= 0) ? (unsigned) value
                                  : (unsigned) (value + (1 << size) - 1);
        w_bits(w, v & pc_thp_mask((unsigned) size), size);
    }
}

static int category_of(int v)
{
    int n = 0, a = v < 0 ? -v : v;
    while (a) {
        a >>= 1;
        n++;
    }
    return n;
}

static void test_decode_block(void)
{
    Table dc, ac;
    PcThpHuffView dcv, acv;
    Writer w;
    PcThpBitState s;
    short block[64], pred = 0;
    int i;

    build_dc_ac(&dc, &ac);
    table_view(&dcv, &dc);
    table_view(&acv, &ac);

    /* Block 1: DC diff -5, then AC coefficients at zig-zag 1 and 3, EOB.
       Block 2: DC diff +2 (so the predictor must carry -5 forward), AC 0. */
    memset(&w, 0, sizeof w);
    emit_value(&w, &dc, (unsigned char) category_of(-5), -5, category_of(-5));
    emit_value(&w, &ac, (unsigned char) category_of(7), 7, category_of(7));
    emit_value(&w, &ac, 0xF0, 0, 0);   /* ZRL: skip 16 */
    emit_value(&w, &ac, (unsigned char) category_of(-1), -1, category_of(-1));
    emit_value(&w, &ac, 0x00, 0, 0);   /* EOB */
    emit_value(&w, &dc, (unsigned char) category_of(2), 2, category_of(2));
    emit_value(&w, &ac, 0x00, 0, 0);   /* EOB */

    state_at(&s, w.buf);
    assert(!pc_thp_decode_block(&s, &dcv, &acv, kOrder, &pred, block));
    assert(pred == -5);
    assert(block[0] == -5);
    assert(block[kOrder[1]] == 7);   /* zig-zag index 1 -> natural 1 */
    assert(block[kOrder[18]] == -1); /* 1 + 16 skipped + this one */
    for (i = 0; i < 64; i++) {
        if (i != kOrder[0] && i != kOrder[1] && i != kOrder[18]) {
            assert(block[i] == 0);
        }
    }

    assert(!pc_thp_decode_block(&s, &dcv, &acv, kOrder, &pred, block));
    assert(pred == -3); /* -5 + 2: the DC predictor is differential */
    assert(block[0] == -3);
    for (i = 1; i < 64; i++) {
        assert(block[i] == 0);
    }

    puts("PASS: block decode places zig-zag runs and carries the DC "
         "predictor");
}

/* ----------------------------------------------------------------- idct --- */

static const double kAan[8] = { 1.0,         1.387039845, 1.306562965,
                                1.175875602, 1.0,         0.785694958,
                                0.541196100, 0.275899379 };

/* The textbook 8x8 inverse DCT, in double precision, with the JPEG level
   shift. Nothing in common with the AAN factorisation under test beyond the
   definition itself. */
static double ref_idct(const short* coef, const double* quant, int x, int y)
{
    double sum = 0.0;
    int u, v;
    for (v = 0; v < 8; v++) {
        for (u = 0; u < 8; u++) {
            double cu = (u == 0) ? sqrt(0.5) : 1.0;
            double cv = (v == 0) ? sqrt(0.5) : 1.0;
            sum += cu * cv * coef[v * 8 + u] * quant[v * 8 + u] *
                   cos((2 * x + 1) * u * M_PI / 16.0) *
                   cos((2 * y + 1) * v * M_PI / 16.0);
        }
    }
    return sum / 4.0 + 128.0;
}

/* Scale a plain quantisation table the way __THPReadQuantizationTable does. */
static void prescale(const double* quant, float* q)
{
    int row, col;
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            q[row * 8 + col] =
                (float) (quant[row * 8 + col] * kAan[row] * kAan[col]);
        }
    }
}

static void test_tile_offset(void)
{
    /* GX I8: 8x4 tiles. Row 0 columns 0..7 are bytes 0..7; row 1 starts at 8;
       row 4 begins the next tile band, wid*4 bytes in. */
    assert(pc_thp_tile_offset(640, 0, 0) == 0);
    assert(pc_thp_tile_offset(640, 7, 0) == 7);
    assert(pc_thp_tile_offset(640, 0, 1) == 8);
    assert(pc_thp_tile_offset(640, 0, 3) == 24);
    assert(pc_thp_tile_offset(640, 8, 0) == 32);
    assert(pc_thp_tile_offset(640, 0, 4) == 640 * 4);
    assert(pc_thp_tile_offset(640, 0, 8) == 640 * 8);
    assert(pc_thp_tile_offset(640, 639, 15) == 640u * 12u + 79u * 32u + 3u * 8u + 7u);
    /* One MCU row of 640x16 fills exactly the 0x2800 LCStoreData moves. */
    assert(pc_thp_tile_offset(640, 639, 15) < 0x2800u);
    assert(pc_thp_tile_offset(320, 319, 7) < 0xA00u);
    puts("PASS: IDCT output addresses are GX_TF_I8 tiles");
}

static void test_idct_dc_only(void)
{
    /* A DC-only block is flat, and its level is DC*q/8 + 128 -- the 1024 bias
       the column pass adds, divided by the quantised store's eight. */
    double quant[64];
    float q[64];
    short in[64];
    unsigned char plane[0x2800];
    int i, x, y;

    for (i = 0; i < 64; i++) {
        quant[i] = 1.0;
    }
    prescale(quant, q);

    memset(in, 0, sizeof in);
    memset(plane, 0xCD, sizeof plane);
    pc_thp_idct(in, q, plane, 640, 16, 0);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            assert(plane[pc_thp_tile_offset(640, 16u + x, (unsigned) y)] == 128);
        }
    }
    /* and nothing outside the block was touched */
    assert(plane[pc_thp_tile_offset(640, 15, 0)] == 0xCD);
    assert(plane[pc_thp_tile_offset(640, 24, 0)] == 0xCD);
    assert(plane[pc_thp_tile_offset(640, 16, 8)] == 0xCD);

    in[0] = 400; /* 400/8 = 50 above mid grey */
    pc_thp_idct(in, q, plane, 640, 0, 0);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            assert(plane[pc_thp_tile_offset(640, (unsigned) x, (unsigned) y)] ==
                   178);
        }
    }

    /* Clamping, both ends. */
    in[0] = 20000;
    pc_thp_idct(in, q, plane, 640, 0, 0);
    assert(plane[0] == 255);
    in[0] = -20000;
    pc_thp_idct(in, q, plane, 640, 0, 0);
    assert(plane[0] == 0);

    puts("PASS: DC-only blocks carry the +128 level shift and clamp");
}

static void test_idct_against_reference(void)
{
    /* Real-looking coefficients against the textbook transform. The AAN
       factorisation is exact in real arithmetic, so any difference here is
       float rounding plus the store's truncation: at most one count. */
    static const double quant[64] = {
        16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
        14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
        18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
    };
    float q[64];
    short in[64];
    unsigned char plane[0x2800];
    unsigned seed = 12345u;
    int trial, i, x, y, worst = 0;

    prescale(quant, q);

    for (trial = 0; trial < 64; trial++) {
        memset(in, 0, sizeof in);
        in[0] = (short) ((int) (seed % 200u) - 100);
        seed = seed * 1103515245u + 12345u;
        for (i = 1; i < 20; i++) { /* low-frequency terms, as a photo has */
            unsigned idx = kOrder[1 + (seed >> 8) % 20u];
            seed = seed * 1103515245u + 12345u;
            in[idx] = (short) ((int) ((seed >> 8) % 61u) - 30);
            seed = seed * 1103515245u + 12345u;
        }

        memset(plane, 0, sizeof plane);
        pc_thp_idct(in, q, plane, 640, 0, 0);

        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                double want = ref_idct(in, quant, x, y);
                int got = plane[pc_thp_tile_offset(640, (unsigned) x,
                                                   (unsigned) y)];
                int lo, hi, d;
                if (want < 0.0) {
                    want = 0.0;
                }
                if (want > 255.0) {
                    want = 255.0;
                }
                /* The store truncates, so the exact answer is floor(want)
                   give or take one count of accumulated float error. */
                lo = (int) floor(want) - 1;
                hi = (int) floor(want) + 1;
                assert(got >= lo && got <= hi);
                d = got - (int) floor(want);
                if (d < 0) {
                    d = -d;
                }
                if (d > worst) {
                    worst = d;
                }
            }
        }
    }

    printf("PASS: IDCT matches a double-precision reference over 64 blocks "
           "(worst deviation %d count)\n", worst);
}

static void test_idct_y8_offset(void)
{
    /* THPDec.c calls the same kernel twice per MCU column with y_pos 0 and 8:
       the two halves must not overlap and must land eight rows apart. */
    double quant[64];
    float q[64];
    short in[64];
    unsigned char plane[0x2800];
    int x, y;

    for (x = 0; x < 64; x++) {
        quant[x] = 1.0;
    }
    prescale(quant, q);
    memset(in, 0, sizeof in);
    memset(plane, 0, sizeof plane);

    in[0] = 800; /* 100 above mid grey */
    pc_thp_idct(in, q, plane, 640, 8, 8);

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            unsigned o = pc_thp_tile_offset(640, (unsigned) x, (unsigned) y);
            int inside = (x >= 8 && y >= 8);
            assert(plane[o] == (inside ? 228 : 0));
        }
    }
    puts("PASS: the Y8 entry point writes the lower half of the MCU");
}

int main(void)
{
    test_word_order();
    test_receive();
    test_extend();
    test_zigzag();
    test_huffman_lengths();
    test_huffman_word_boundary();
    test_huffman_rejects_garbage();
    test_decode_block();
    test_tile_offset();
    test_idct_dc_only();
    test_idct_against_reference();
    test_idct_y8_offset();
    puts("all THP kernel tests passed");
    return 0;
}
