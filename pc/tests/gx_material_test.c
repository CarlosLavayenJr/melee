#include <assert.h>
#include <stdio.h>
#include "../src/pc_gx_material.c"
void pc_sys_log(const char* s) { fputs(s, stderr); }
void __real_GXSetTexCoordGen2(GXTexCoordID i, GXTexGenType t, GXTexGenSrc s, u32 m, GXBool n, u32 p) {}
#include "tev_movie_state.h"
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
    assert(m.texture_mask == 1 && m.pipeline_key == 3 && m.stages[0][2] == 0);
    assert(m.registers[1][0] == 128/255.0f && m.registers[1][3] == 1.0f);
    /* BP one-shot write mask preserves all unmasked bits. */
    pc_gx_bp_write(0xfe000001); pc_gx_bp_write(0x41000000);
    assert(pc_gx_material_get(&m) && m.pipeline_key == 2);
    pc_gx_bp_write(0x00001001); assert(!pc_gx_material_get(&m));
    pc_gx_bp_write(0x00000001);
    pc_gx_bp_write(0xc003fff4); assert(!pc_gx_material_get(&m));
    pc_gx_bp_write(0xc000fff2);
    __wrap_GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0, 0, GX_PTIDENTITY);
    assert(!pc_gx_material_get(&m));
    movie_test_state();
    assert(pc_gx_material_get(&m));
    assert(m.count == 4 && m.texture_mask == 7 && m.pipeline_key == 2);
    assert(m.stages[0][2] == 1 && m.stages[1][2] == 2 && m.stages[2][2] == 0 && m.stages[3][2] == 8);
    assert(m.registers[1][0] == -90/255.0f && m.registers[1][2] == -114/255.0f);
    assert(m.konst[0][2] == 226/255.0f && m.konst[0][3] == 88/255.0f);
    assert(m.konst[1][0] == 179/255.0f && m.konst[1][3] == 182/255.0f);
    assert(m.konst[3][0] == 1 && m.konst[3][1] == 0 && m.konst[3][2] == 1);
    pc_gx_bp_write(0x00004c02); assert(pc_gx_material_get(&m) && m.pipeline_key == 10);
    pc_gx_bp_write(0xf5000004); assert(!pc_gx_material_get(&m));
    puts("PASS: menu and four-stage movie TEV, konst separation, culling key, BP mask and rejection");
    return 0;
}
