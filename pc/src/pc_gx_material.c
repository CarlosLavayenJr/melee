/* Native BP shadow: register layouts are defined by the retained GX SDK's
   GXTev.c/GXPixel.c. This is a deliberately validated four-stage subset,
   not a claim of complete TEV emulation. */
#ifdef PC_GX_RENDERER
#include "pc_gx_material.h"
#include "pc_sys.h"
#include <dolphin/gx.h>
#include <string.h>
static uint32_t bp[256], mask = 0xffffff;
static uint32_t tev_registers[8];
static uint32_t konst_registers[8];
static int identity_texgen[8];
void pc_gx_bp_write(unsigned int value)
{
    unsigned reg = value >> 24;
    if (reg == 0xfe) { mask = value & 0xffffff; return; }
    bp[reg] = (bp[reg] & ~mask) | (value & mask);
    /* The same BP addresses upload either TEV registers or konst registers. */
    if (reg >= 0xe0 && reg <= 0xe7) {
        if (bp[reg] & 0x800000) konst_registers[reg-0xe0] = bp[reg];
        else tev_registers[reg-0xe0] = bp[reg];
    }
    mask = 0xffffff;
}
void __real_GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32);
void __wrap_GXSetTexCoordGen2(GXTexCoordID id, GXTexGenType type, GXTexGenSrc src,
                             u32 matrix, GXBool normalize, u32 post)
{
    if ((unsigned)id < 8) identity_texgen[id] = type == GX_TG_MTX2x4 &&
        src == GX_TG_TEX0 && matrix == GX_IDENTITY && !normalize && post == GX_PTIDENTITY;
    __real_GXSetTexCoordGen2(id, type, src, matrix, normalize, post);
}
static int unsupported(unsigned bit, const char* reason)
{
    static unsigned reported;
    if (!(reported & (1u << bit))) {
        reported |= 1u << bit;
        pc_sys_log("pc_gx_material: draw skipped: "); pc_sys_log(reason); pc_sys_log("\n");
    }
    return 0;
}
static float signed11(unsigned v)
{
    int n = v & 2047; if (n & 1024) n -= 2048; return n / 255.0f;
}
static float konst_component(unsigned reg, unsigned channel)
{
    /* RA/BG register packing, channels indexed RGBA. */
    unsigned word = konst_registers[2 * reg + (channel == 1 || channel == 2)];
    return ((word >> ((channel == 1 || channel == 3) ? 12 : 0)) & 255) / 255.0f;
}
static int resolve_konst(float out[4], unsigned kc, unsigned ka, int color, int alpha)
{
    unsigned c;
    if (color) {
        if (kc >= 8 && kc < 12) return 0;
        for (c = 0; c < 3; ++c)
            out[c] = kc < 8 ? (8 - kc) / 8.0f :
                konst_component(kc & 3, kc < 16 ? c : (kc - 16) / 4);
    }
    if (alpha) {
        if (ka >= 8 && ka < 16) return 0;
        out[3] = ka < 8 ? (8 - ka) / 8.0f : konst_component(ka & 3, (ka - 16) / 4);
    }
    return 1;
}
int pc_gx_material_get(pc_gx_material* out)
{
    unsigned i, j, blend = bp[0x41];
    memset(out, 0, sizeof *out);
    out->count = ((bp[0] >> 10) & 15) + 1;
    if (out->count > 4) return unsupported(0, "more than four TEV stages");
    if ((bp[0] >> 16) & 7) return unsupported(10, "indirect texturing");
    for (j = 0; j < out->count; ++j) {
        unsigned tc = bp[0xc0 + j*2], ta = bp[0xc1 + j*2];
        unsigned order = (bp[0x28 + j/2] >> ((j&1)*12)) & 1023;
        unsigned ksel = bp[0xf6 + j/2] >> (4 + (j&1)*10);
        unsigned coord = (order >> 3) & 7, raster = (order >> 7) & 7;
        int tex = 0, ras = 0, kc = 0, ka = 0;
        if (((tc >> 16) & 3) == 3 || ((ta >> 16) & 3) == 3)
            return unsupported(1, "TEV compare operation");
        if (bp[0x10 + j] & 0x1fffff) return unsupported(10, "indirect TEV stage");
        for (i = 0; i < 4; ++i) {
            unsigned c = (tc >> (i*4)) & 15, a = (ta >> (4+i*3)) & 7;
            tex |= c == 8 || c == 9 || a == 4;
            ras |= c == 10 || c == 11 || a == 5;
            kc |= c == 14; ka |= a == 6;
        }
        if (tex && (!(order & 64) || coord >= (bp[0] & 15) || !identity_texgen[coord]))
            return unsupported(3, "texture order or non-identity texgen");
        if ((tex && (ta & 12)) || (ras && (ta & 3))) return unsupported(4, "TEV swap table");
        if ((tex || ras) && ((bp[0xf6] & 15) != 4 || (bp[0xf7] & 15) != 14))
            return unsupported(4, "non-identity TEV swap table");
        if (ras && raster != 0 && raster != 7)
            return unsupported(9, "raster channel other than COLOR0 or ZERO");
        if (!resolve_konst(out->konst[j], ksel & 31, (ksel >> 5) & 31, kc, ka))
            return unsupported(2, "reserved konst selector");
        out->stages[j][0] = tc; out->stages[j][1] = ta;
        out->stages[j][2] = tex ? order & 7 : 8;
        out->stages[j][3] = raster;
        if (tex) out->texture_mask |= 1u << (order & 7);
    }
    if (blend & (2 | 2048)) return unsupported(5, "logic/subtract blending");
    if ((blend & 1) && (((blend >> 8) & 7) != 4 || ((blend >> 5) & 7) != 5))
        return unsupported(6, "blend factors other than source-alpha/inverse-source-alpha");
    if ((bp[0x40] & 1) && ((bp[0x40] >> 1) & 7) != 7)
        return unsupported(7, "depth comparison needs depth attachment");
    if ((bp[0xf1] >> 21) & 7) return unsupported(8, "fog");
    if ((bp[0xf5] >> 2) & 3) return unsupported(11, "depth-texture operation");
    for (i = 0; i < 4; ++i) {
        out->registers[i][0] = signed11(tev_registers[2*i]);
        out->registers[i][3] = signed11(tev_registers[2*i] >> 12);
        out->registers[i][2] = signed11(tev_registers[2*i+1]);
        out->registers[i][1] = signed11(tev_registers[2*i+1] >> 12);
    }
    out->compare = bp[0xf3];
    out->pipeline_key = (blend & 1) | ((blend >> 2) & 6) | (((bp[0] >> 14) & 3) << 3);
    return 1;
}
#endif
