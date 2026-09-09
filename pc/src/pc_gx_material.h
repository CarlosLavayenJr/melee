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
#endif
