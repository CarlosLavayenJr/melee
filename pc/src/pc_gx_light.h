/* pc_gx_light.h -- GX per-vertex lighting, as a testable function.
 *
 * GX computes lighting PER VERTEX, not per fragment. That is what lets this
 * be ordinary C next to the vertex expansion rather than SPIR-V: the result
 * is a colour per vertex, which the shader then reads like any other vertex
 * attribute. It also means a standalone test can check the arithmetic, which
 * SPIR-V would not allow without a GPU.
 *
 * The equation is a port of encounter/aurora's lighting_func
 * (lib/gx/shader.cpp, MIT licensed), which this project already uses as its
 * reference for how GX state maps to a modern API -- see pc/GX_RENDERER.md
 * and the header of pc_gx_fifo.c. The specular attenuation branch in
 * particular is close enough to Aurora's to call this a port of that specific
 * logic rather than independent work, and is credited here for that reason.
 * Aurora emits WGSL; this evaluates the same expressions in C.
 *
 * Why it exists: the main menu issues 5920+ draws whose TEV stages read
 * raster channel 1, and every one of them was measured to supply no vertex
 * colour for it -- the channel is produced by the lighting path instead. All
 * of them use one configuration:
 *
 *     lit=1  amb=GX_SRC_REG  mat=GX_SRC_REG  lights=12
 *     diff=GX_DF_CLAMP  attn=GX_AF_SPEC
 *
 * so that is the path implemented and tested. Anything else is reported by
 * the caller rather than approximated.
 *
 * Positions, normals and light vectors are all in model-view space, which is
 * the space GX lights in and the space pc_gx_fifo.c already has a vertex's
 * position in before projection.
 */
#ifndef PC_GX_LIGHT_H
#define PC_GX_LIGHT_H

/* GXDiffuseFn and GXAttnFn, restated so this header stands alone. */
enum { PC_GX_DF_NONE = 0, PC_GX_DF_SIGN = 1, PC_GX_DF_CLAMP = 2 };
enum { PC_GX_AF_SPEC = 0, PC_GX_AF_SPOT = 1, PC_GX_AF_NONE = 2 };

typedef struct pc_gx_light_src {
    float color[4];   /* 0..1 */
    float pos[3];     /* model-view space; the half-angle vector for SPEC */
    float dir[3];     /* model-view space */
    float cos_att[3]; /* GXInitLightAttn a0, a1, a2 */
    float dist_att[3];/* GXInitLightAttn k0, k1, k2 */
} pc_gx_light_src;

typedef struct pc_gx_light_channel {
    float amb[4];       /* ambient register, 0..1 */
    float mat[4];       /* material register, 0..1 */
    unsigned light_mask;/* bit per light */
    unsigned diff_fn;   /* PC_GX_DF_* */
    unsigned attn_fn;   /* PC_GX_AF_* */
} pc_gx_light_channel;

static float pc_gx_dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float pc_gx_poly3(const float c[3], float x)
{
    /* dot(coefficients, vec3(1, x, x*x)) */
    return c[0] + c[1] * x + c[2] * x * x;
}

static float pc_gx_clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* One channel's colour for one vertex. `out` receives RGBA in 0..1.
   `mv_pos` and `mv_nrm` are model-view space; `mv_nrm` must be normalised. */
static void pc_gx_light_vertex(const pc_gx_light_channel* ch,
                               const pc_gx_light_src* lights, unsigned count,
                               const float mv_pos[3], const float mv_nrm[3],
                               float out[4])
{
    float lighting[4];
    unsigned i, c;

    for (c = 0; c < 4; ++c) lighting[c] = ch->amb[c];

    for (i = 0; i < count; ++i) {
        const pc_gx_light_src* l = &lights[i];
        float ldir[3], dist2, dist, attn, diff, cos_attn, dist_attn;

        if (!(ch->light_mask & (1u << i))) continue;

        ldir[0] = l->pos[0] - mv_pos[0];
        ldir[1] = l->pos[1] - mv_pos[1];
        ldir[2] = l->pos[2] - mv_pos[2];
        dist2 = pc_gx_dot3(ldir, ldir);
        if (dist2 <= 0.0f) continue;
        dist = (float) __builtin_sqrtf(dist2);
        ldir[0] /= dist; ldir[1] /= dist; ldir[2] /= dist;

        switch (ch->attn_fn) {
        case PC_GX_AF_NONE:
            attn = 1.0f;
            break;
        case PC_GX_AF_SPOT: {
            float cosine = pc_gx_dot3(ldir, l->dir);
            if (cosine < 0.0f) cosine = 0.0f;
            cos_attn = pc_gx_poly3(l->cos_att, cosine);
            /* The spot form attenuates by DISTANCE, unlike the specular one
               below, which reuses the same cosine for both polynomials. */
            dist_attn = l->dist_att[0] + l->dist_att[1] * dist +
                        l->dist_att[2] * dist2;
            attn = cos_attn / dist_attn;
            if (attn < 0.0f) attn = 0.0f;
            break;
        }
        default: { /* PC_GX_AF_SPEC */
            float a = pc_gx_dot3(mv_nrm, l->dir);
            if (a < 0.0f) a = 0.0f;
            /* The half-angle term only contributes on the lit side. */
            if (pc_gx_dot3(mv_nrm, ldir) < 0.0f) a = 0.0f;
            cos_attn = pc_gx_poly3(l->cos_att, a);
            if (ch->diff_fn != PC_GX_DF_NONE) {
                /* Aurora normalises dist_att here; the coefficients act as a
                   direction rather than a distance falloff in this branch. */
                float n = (float) __builtin_sqrtf(pc_gx_dot3(l->dist_att,
                                                             l->dist_att));
                float k[3];
                if (n <= 0.0f) { attn = 0.0f; break; }
                k[0] = l->dist_att[0] / n;
                k[1] = l->dist_att[1] / n;
                k[2] = l->dist_att[2] / n;
                dist_attn = pc_gx_poly3(k, a);
            } else {
                dist_attn = pc_gx_poly3(l->dist_att, a);
            }
            if (dist_attn < 0.0f) dist_attn = 0.0f;
            attn = dist_attn > 0.0f ? cos_attn / dist_attn : 0.0f;
            if (attn < 0.0f) attn = 0.0f;
            break;
        }
        }

        switch (ch->diff_fn) {
        case PC_GX_DF_SIGN: diff = pc_gx_dot3(ldir, mv_nrm); break;
        case PC_GX_DF_CLAMP:
            diff = pc_gx_dot3(ldir, mv_nrm);
            if (diff < 0.0f) diff = 0.0f;
            break;
        default: diff = 1.0f; break;
        }

        for (c = 0; c < 4; ++c) lighting[c] += attn * diff * l->color[c];
    }

    for (c = 0; c < 4; ++c) out[c] = ch->mat[c] * pc_gx_clamp01(lighting[c]);
}

#endif
