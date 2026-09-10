#ifndef PC_GX_MATERIAL_H
#define PC_GX_MATERIAL_H
#include <stdint.h>
typedef struct pc_gx_material {
    /* std140 uniform block; one immutable copy per draw. */
    float registers[4][4]; /* PREV, REG0, REG1, REG2 */
    float konst[4][4]; /* resolved color/alpha konst for each stage */
    uint32_t stages[4][4]; /* color op, alpha op, texture slot (8=none), raster */
    uint32_t count, compare, reserved[2];
    unsigned texture_mask, pipeline_key; /* CPU-only fields */
} pc_gx_material;
#define PC_GX_MATERIAL_UNIFORM_SIZE 208u
void pc_gx_bp_write(unsigned int value);
int pc_gx_material_get(pc_gx_material* out);
/* Accepted/skipped draw tallies, by reason. Diagnostic: call it from a test
   or a debugger to see which unsupported state is actually costing pixels. */
void pc_gx_material_report(void);
/* Apply the active texgen's post-transform texture matrix to one uv, in
   place. Returns 1 when a matrix was applied. sysdolphin puts every ordinary
   texture's scale/rotate/translate here, so this is not optional. */
int pc_gx_texcoord_transform(float uv[2]);
/* Raster channel 1, which the menu produces through GX lighting rather than a
   vertex colour. Per vertex, because GX lights per vertex. */
int pc_gx_channel1_color(const float mv_pos[3], const float mv_nrm[3],
                         float out[4]);
int pc_gx_channel1_supported(void);
#endif
