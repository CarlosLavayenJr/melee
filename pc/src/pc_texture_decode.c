/* GX tile/texel rules cross-checked against Aurora's texture_convert.cpp.
   CMPR palette semantics follow its MIT-licensed implementation; see
   pc/licenses/aurora-textures.txt. No game data or CPU emulation is used. */
#include "pc_texture_decode.h"
#include <string.h>

static int tile_shape(unsigned f, unsigned* w, unsigned* h, unsigned* bytes)
{
    *bytes = 32;
    switch (f) {
    case PC_TF_I4: case PC_TF_CMPR: *w = 8; *h = 8; break;
    case PC_TF_I8: case PC_TF_IA4: *w = 8; *h = 4; break;
    case PC_TF_RGB565: case PC_TF_RGB5A3: case PC_TF_IA8:
        *w = 4; *h = 4; break;
    case PC_TF_RGBA8: *w = 4; *h = 4; *bytes = 64; break;
    default: return -1;
    }
    return 0;
}

size_t pc_texture_source_size(unsigned f, unsigned w, unsigned h)
{
    unsigned tw, th, bytes;
    if (!w || !h || w > 1024 || h > 1024 || tile_shape(f, &tw, &th, &bytes))
        return 0;
    return (size_t)((w + tw - 1) / tw) * ((h + th - 1) / th) * bytes;
}

static unsigned be16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
static unsigned char e5(unsigned v) { return (unsigned char)((v << 3) | (v >> 2)); }
static unsigned char e6(unsigned v) { return (unsigned char)((v << 2) | (v >> 4)); }
static void rgb565(unsigned v, unsigned char* c)
{
    c[0] = e5(v >> 11); c[1] = e6((v >> 5) & 63);
    c[2] = e5(v & 31); c[3] = 255;
}

static void cmpr_pixel(const unsigned char* tile, unsigned x, unsigned y,
                       unsigned char* out)
{
    const unsigned char* b = tile + ((y / 4) * 2 + x / 4) * 8;
    unsigned a = be16(b), d = be16(b + 2), i, selector;
    unsigned char c[4][4];
    rgb565(a, c[0]); rgb565(d, c[1]);
    for (i = 0; i < 3; ++i) {
        if (a > d) {
            /* GX uses 5/8 and 3/8, not desktop BC1's 2/3 and 1/3. */
            c[2][i] = (unsigned char)((5 * c[0][i] + 3 * c[1][i]) >> 3);
            c[3][i] = (unsigned char)((3 * c[0][i] + 5 * c[1][i]) >> 3);
        } else {
            c[2][i] = c[3][i] = (unsigned char)((c[0][i] + c[1][i]) / 2);
        }
    }
    c[2][3] = 255; c[3][3] = a > d ? 255 : 0;
    selector = (b[4 + y % 4] >> (6 - 2 * (x % 4))) & 3;
    memcpy(out, c[selector], 4);
}

int pc_texture_decode(unsigned f, unsigned w, unsigned h,
                      const void* source, size_t source_size,
                      void* rgba, size_t rgba_size)
{
    unsigned tw, th, bytes, bx, by, x, y;
    size_t needed = pc_texture_source_size(f, w, h);
    const unsigned char* tile = source;
    unsigned char* dst = rgba;
    if (!needed || !source || !rgba || source_size < needed ||
        rgba_size < (size_t)w * h * 4) return -1;
    if (tile_shape(f, &tw, &th, &bytes)) return -1;
    for (by = 0; by < h; by += th) {
        for (bx = 0; bx < w; bx += tw, tile += bytes) {
            for (y = 0; y < th && by + y < h; ++y) {
                for (x = 0; x < tw && bx + x < w; ++x) {
                    unsigned n = y * tw + x, v;
                    unsigned char* c = dst + ((size_t)(by + y) * w + bx + x) * 4;
                    switch (f) {
                    case PC_TF_I4:
                        v = ((tile[n / 2] >> ((n & 1) ? 0 : 4)) & 15) * 17;
                        c[0] = c[1] = c[2] = c[3] = (unsigned char)v; break;
                    case PC_TF_I8:
                        c[0] = c[1] = c[2] = c[3] = tile[n]; break;
                    case PC_TF_IA4:
                        c[0] = c[1] = c[2] = (tile[n] & 15) * 17;
                        c[3] = (tile[n] >> 4) * 17; break;
                    case PC_TF_IA8:
                        c[0] = c[1] = c[2] = tile[n * 2 + 1];
                        c[3] = tile[n * 2]; break;
                    case PC_TF_RGB565: rgb565(be16(tile + n * 2), c); break;
                    case PC_TF_RGB5A3:
                        v = be16(tile + n * 2);
                        if (v & 0x8000) {
                            c[0] = e5((v >> 10) & 31); c[1] = e5((v >> 5) & 31);
                            c[2] = e5(v & 31); c[3] = 255;
                        } else {
                            unsigned a = v >> 12;
                            c[0] = ((v >> 8) & 15) * 17;
                            c[1] = ((v >> 4) & 15) * 17; c[2] = (v & 15) * 17;
                            c[3] = (unsigned char)((a << 5) | (a << 2) | (a >> 1));
                        }
                        break;
                    case PC_TF_RGBA8:
                        c[0] = tile[n * 2 + 1]; c[3] = tile[n * 2];
                        c[1] = tile[32 + n * 2]; c[2] = tile[33 + n * 2]; break;
                    case PC_TF_CMPR: cmpr_pixel(tile, x, y, c); break;
                    }
                }
            }
        }
    }
    return 0;
}
