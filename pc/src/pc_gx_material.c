/* Native BP shadow: register layouts are defined by the retained GX SDK's
   GXTev.c/GXPixel.c. This is a deliberately validated single-stage subset,
   not a claim of complete TEV emulation. */
#ifdef PC_GX_RENDERER
#include "pc_gx_material.h"
#include "pc_sys.h"
#include <dolphin/gx.h>
#include <string.h>
static uint32_t bp[256], mask = 0xffffff;
static uint32_t tev_registers[8];
static int identity_texgen;
void pc_gx_bp_write(unsigned int value)
{
    unsigned reg = value >> 24;
    if (reg == 0xfe) { mask = value & 0xffffff; return; }
    bp[reg] = (bp[reg] & ~mask) | (value & mask);
    /* The same BP addresses upload either TEV registers or konst registers. */
    if (reg >= 0xe0 && reg <= 0xe7 && !(bp[reg] & 0x800000)) tev_registers[reg-0xe0] = bp[reg];
    mask = 0xffffff;
}
void __real_GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32);
void __wrap_GXSetTexCoordGen2(GXTexCoordID id, GXTexGenType type, GXTexGenSrc src,
                             u32 matrix, GXBool normalize, u32 post)
{
    if (id == GX_TEXCOORD0) identity_texgen = type == GX_TG_MTX2x4 &&
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
int pc_gx_material_get(pc_gx_material* out)
{
    unsigned i, blend = bp[0x41], tc = bp[0xc0], ta = bp[0xc1], tex = 0;
    memset(out, 0, sizeof *out);
    if ((bp[0] >> 10) & 15) return unsupported(0, "multiple TEV stages");
    if ((bp[0] >> 14) & 3) return unsupported(9, "face culling");
    if ((bp[0] >> 16) & 7) return unsupported(10, "indirect texturing");
    if (((tc >> 16) & 3) == 3 || ((ta >> 16) & 3) == 3 ||
        (tc >> 22) || (ta >> 22)) return unsupported(1, "TEV compare op or non-PREV output");
    for (i = 0; i < 4; ++i) {
        unsigned c = (tc >> (i * 4)) & 15, a = (ta >> (4 + i * 3)) & 7;
        if (c == 8 || c == 9 || a == 4) tex = 1;
        if (c < 2 || (c >= 4 && c <= 7) || c == 14 || a == 0 || a == 2 || a == 3 || a == 6)
            return unsupported(2, "TEV PREV/REG1/REG2/konst input");
    }
    if (tex && (!(bp[0x28] & 64) || ((bp[0x28] >> 3) & 7) || !identity_texgen))
        return unsupported(3, "texture order or non-identity texgen");
    if (tex && (ta & 15)) return unsupported(4, "TEV swap table");
    if (tex && ((bp[0xf6] & 15) != 4 || (bp[0xf7] & 15) != 14))
        return unsupported(4, "non-identity TEV texture swap table");
    if (blend & (2 | 2048)) return unsupported(5, "logic/subtract blending");
    if ((blend & 1) && (((blend >> 8) & 7) != 4 || ((blend >> 5) & 7) != 5))
        return unsupported(6, "blend factors other than source-alpha/inverse-source-alpha");
    if ((bp[0x40] & 1) && ((bp[0x40] >> 1) & 7) != 7)
        return unsupported(7, "depth comparison needs depth attachment");
    if ((bp[0xf1] >> 21) & 7) return unsupported(8, "fog");
    if ((bp[0xf5] >> 2) & 3) return unsupported(11, "depth-texture operation");
    out->reg0[0] = signed11(tev_registers[2]); out->reg0[3] = signed11(tev_registers[2] >> 12);
    out->reg0[2] = signed11(tev_registers[3]); out->reg0[1] = signed11(tev_registers[3] >> 12);
    out->color = tc; out->alpha = ta; out->compare = bp[0xf3]; out->textured = tex;
    out->slot = bp[0x28] & 7;
    out->pipeline_key = (blend & 1) | ((blend >> 2) & 6);
    return 1;
}
#endif
