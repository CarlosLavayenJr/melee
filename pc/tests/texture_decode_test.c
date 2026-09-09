#include "pc_texture_decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pixel(const unsigned char* p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

static void test_formats(void)
{
    unsigned char src[256] = {0}, out[8 * 8 * 4];
    src[0] = 0x1e;
    assert(!pc_texture_decode(PC_TF_I4, 8, 8, src, 32, out, sizeof out));
    pixel(out, 17, 17, 17, 17); pixel(out + 4, 238, 238, 238, 238);
    src[0] = 0x93;
    assert(!pc_texture_decode(PC_TF_I8, 8, 4, src, 32, out, sizeof out));
    pixel(out, 147, 147, 147, 147);
    assert(!pc_texture_decode(PC_TF_IA4, 8, 4, src, 32, out, sizeof out));
    pixel(out, 51, 51, 51, 153);
    src[0] = 0x12; src[1] = 0xa7;
    assert(!pc_texture_decode(PC_TF_IA8, 4, 4, src, 32, out, sizeof out));
    pixel(out, 167, 167, 167, 18);
    src[0] = 0xf8; src[1] = 0; src[2] = 7; src[3] = 0xe0;
    src[4] = 0; src[5] = 0x1f; src[6] = 0x8c; src[7] = 0x51;
    assert(!pc_texture_decode(PC_TF_RGB565, 4, 4, src, 32, out, sizeof out));
    pixel(out, 255, 0, 0, 255); pixel(out + 4, 0, 255, 0, 255);
    pixel(out + 8, 0, 0, 255, 255); pixel(out + 12, 140, 138, 140, 255);
    src[0] = 0xfc; src[1] = 0; src[2] = 0x31; src[3] = 0x2f;
    assert(!pc_texture_decode(PC_TF_RGB5A3, 4, 4, src, 32, out, sizeof out));
    pixel(out, 255, 0, 0, 255); pixel(out + 4, 17, 34, 255, 109);
    memset(src, 0, sizeof src);
    src[0] = 7; src[1] = 11; src[32] = 13; src[33] = 17;
    src[30] = 19; src[31] = 23; src[62] = 29; src[63] = 31;
    assert(!pc_texture_decode(PC_TF_RGBA8, 4, 4, src, 64, out, sizeof out));
    pixel(out, 11, 13, 17, 7); pixel(out + 15 * 4, 23, 29, 31, 19);
}

static void test_cmpr(void)
{
    unsigned char src[32] = {0}, out[8 * 8 * 4];
    unsigned b, y;
    /* Each 4x4 quadrant has a different endpoint0; each row selects 0,1,2,3. */
    const unsigned endpoints[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
    for (b = 0; b < 4; ++b) {
        src[b*8] = (unsigned char)(endpoints[b] >> 8);
        src[b*8+1] = (unsigned char)endpoints[b];
        for (y = 0; y < 4; ++y) src[b*8+4+y] = 0x1b;
    }
    assert(!pc_texture_decode(PC_TF_CMPR, 8, 8, src, 32, out, sizeof out));
    pixel(out, 255, 0, 0, 255); pixel(out + 4, 0, 0, 0, 255);
    pixel(out + 8, 159, 0, 0, 255); pixel(out + 12, 95, 0, 0, 255);
    pixel(out + 4*4, 0, 255, 0, 255);
    pixel(out + 4*8*4, 0, 0, 255, 255);
    pixel(out + (4*8+4)*4, 255, 255, 255, 255);
    /* Three-color mode: transparent RGB is the midpoint, not black. */
    src[0] = 0; src[1] = 0x1f; src[2] = 0xf8; src[3] = 0;
    assert(!pc_texture_decode(PC_TF_CMPR, 3, 2, src, 32, out, 3*2*4));
    pixel(out, 0, 0, 255, 255); pixel(out + 8, 127, 0, 127, 255);
    assert(!pc_texture_decode(PC_TF_CMPR, 4, 4, src, 32, out, 64));
    pixel(out + 12, 127, 0, 127, 0);
    src[0] = src[2]; src[1] = src[3];
    assert(!pc_texture_decode(PC_TF_CMPR, 4, 4, src, 32, out, 64));
    pixel(out + 12, 255, 0, 0, 0); /* equal endpoints also transparent */
}

static void test_tile_boundaries(void)
{
    const unsigned formats[] = {0, 1, 2, 3, 4, 5, 6, 14};
    unsigned k, bx, by, x, y;
    for (k = 0; k < sizeof formats / sizeof *formats; ++k) {
        unsigned f = formats[k], tw = (f <= 2 || f == 14) ? 8 : 4;
        unsigned th = (f == 0 || f == 14) ? 8 : 4;
        unsigned w = tw + 1, h = th + 1, bytes = f == 6 ? 64 : 32;
        unsigned char src[256], output[9*9*4+8], full[8*8*4];
        /* Different complete tiles; compare cropped image to independent tile
           decodes, testing horizontal/vertical strides and partial tiles. */
        for (x = 0; x < sizeof src; ++x) src[x] = (unsigned char)(x * 19 + x / bytes);
        memset(output, 0xcd, sizeof output);
        assert(pc_texture_source_size(f, w, h) == 4*bytes);
        assert(!pc_texture_decode(f, w, h, src, 4*bytes, output+4, w*h*4));
        for (by = 0; by < 2; ++by) for (bx = 0; bx < 2; ++bx) {
            assert(!pc_texture_decode(f, tw, th, src+(by*2+bx)*bytes, bytes, full, sizeof full));
            for (y = 0; y < th && by*th+y < h; ++y)
                for (x = 0; x < tw && bx*tw+x < w; ++x)
                    assert(!memcmp(output+4+((by*th+y)*w+bx*tw+x)*4, full+(y*tw+x)*4, 4));
        }
        for (x = 0; x < 4; ++x) assert(output[x] == 0xcd && output[4+w*h*4+x] == 0xcd);
    }
}

static void test_invalid(void)
{
    unsigned char src[64] = {0}, out[64], before[64];
    memset(out, 0xda, sizeof out); memcpy(before, out, sizeof out);
    assert(!pc_texture_source_size(0, 0, 1));
    assert(!pc_texture_source_size(0, 1, 1025));
    assert(!pc_texture_source_size(0, ~0u, ~0u));
    assert(!pc_texture_source_size(8, 1, 1));
    assert(pc_texture_source_size(6, 1, 1) == 64);
    assert(pc_texture_decode(6, 1, 1, src, 63, out, sizeof out) == -1);
    assert(pc_texture_decode(6, 1, 1, src, 64, out, 3) == -1);
    assert(pc_texture_decode(8, 1, 1, src, 64, out, sizeof out) == -1);
    assert(pc_texture_decode(6, 1, 1, NULL, 64, out, sizeof out) == -1);
    assert(!memcmp(out, before, sizeof out));
}

int main(void)
{
    test_formats(); test_cmpr(); test_tile_boundaries(); test_invalid();
    puts("PASS: eight formats, GX CMPR rules, tile edges, bounds and guards");
    return 0;
}
