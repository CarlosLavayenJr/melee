#include <assert.h>
#include <stdio.h>
#include "../src/pc_gx_material.c"
void pc_sys_log(const char* s) { fputs(s, stderr); }
void __real_GXSetTexCoordGen2(GXTexCoordID i, GXTexGenType t, GXTexGenSrc s, u32 m, GXBool n, u32 p) {}
int main(void)
{
    pc_gx_material m;
    pc_gx_bp_write(0x00000001); /* one stage, one texgen */
    pc_gx_bp_write(0x280003c0); /* texture0, coord0, no raster channel */
    pc_gx_bp_write(0xc000fff2); /* RGB = reg0 */
    pc_gx_bp_write(0xc100f0f0); /* alpha = texture alpha * reg0 alpha */
    pc_gx_bp_write(0x410004a9); /* alpha blending, RGB writes only */
    pc_gx_bp_write(0xf3440000); /* alpha > 0 OR alpha > 0 */
    pc_gx_bp_write(0xe20ff080); pc_gx_bp_write(0xe3080040);
    pc_gx_bp_write(0xf6000004); pc_gx_bp_write(0xf700000e);
    pc_gx_bp_write(0xe2800000); /* konst upload must not overwrite REG0 */
    __wrap_GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, 0, GX_PTIDENTITY);
    assert(pc_gx_material_get(&m));
    assert(m.textured && m.pipeline_key == 3 && m.slot == 0);
    assert(m.reg0[0] == 128/255.0f && m.reg0[3] == 1.0f);
    /* BP one-shot write mask preserves all unmasked bits. */
    pc_gx_bp_write(0xfe000001); pc_gx_bp_write(0x41000000);
    assert(pc_gx_material_get(&m) && m.pipeline_key == 2);
    pc_gx_bp_write(0x00000401); assert(!pc_gx_material_get(&m));
    pc_gx_bp_write(0x00000001);
    pc_gx_bp_write(0xc000fff4); assert(!pc_gx_material_get(&m));
    pc_gx_bp_write(0xc000fff2);
    __wrap_GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0, 0, GX_PTIDENTITY);
    assert(!pc_gx_material_get(&m));
    puts("PASS: menu TEV inputs/registers, blending/write mask, BP mask, unsupported-state rejection");
    return 0;
}
