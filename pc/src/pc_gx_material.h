#ifndef PC_GX_MATERIAL_H
#define PC_GX_MATERIAL_H
#include <stdint.h>
typedef struct pc_gx_material {
    float reg0[4];
    uint32_t color, alpha, compare, textured;
    unsigned slot, pipeline_key;
} pc_gx_material;
void pc_gx_bp_write(unsigned int value);
int pc_gx_material_get(pc_gx_material* out);
#endif
