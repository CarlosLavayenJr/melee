/* gx_light_test.c -- known-answer tests for GX per-vertex lighting.
 *
 *   gcc -m32 -std=c11 -Wall -Wextra -Werror -O2 -I pc/src \
 *       pc/tests/gx_light_test.c -o t.exe
 *
 * Answers are worked out by hand from the equation, not read back out of it.
 */
#include "pc_gx_light.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a, float b) { return fabsf(a - b) < 1.0e-4f; }

static void set3(float v[3], float x, float y, float z)
{
    v[0] = x; v[1] = y; v[2] = z;
}

static void test_ambient_and_material(void)
{
    /* No lights enabled: the result is material * clamp(ambient). */
    pc_gx_light_channel ch;
    float out[4];
    memset(&ch, 0, sizeof ch);
    ch.amb[0] = 0.5f; ch.amb[1] = 0.25f; ch.amb[2] = 2.0f; ch.amb[3] = 1.0f;
    ch.mat[0] = 1.0f; ch.mat[1] = 0.5f;  ch.mat[2] = 1.0f; ch.mat[3] = 1.0f;
    ch.light_mask = 0;
    ch.diff_fn = PC_GX_DF_CLAMP;
    ch.attn_fn = PC_GX_AF_SPEC;

    pc_gx_light_vertex(&ch, NULL, 0, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       out);
    assert(near(out[0], 0.5f));
    assert(near(out[1], 0.125f));
    assert(near(out[2], 1.0f)); /* ambient above 1 is clamped before scaling */
    assert(near(out[3], 1.0f));
    puts("PASS: ambient is clamped then scaled by the material register");
}

static void test_attn_none_diffuse_clamp(void)
{
    /* attn = 1, diff = max(0, dot(ldir, n)). A light straight above a surface
       facing up gives full contribution; behind it gives none. */
    pc_gx_light_channel ch;
    pc_gx_light_src l;
    float out[4];

    memset(&ch, 0, sizeof ch);
    memset(&l, 0, sizeof l);
    ch.mat[0] = ch.mat[1] = ch.mat[2] = ch.mat[3] = 1.0f;
    ch.light_mask = 1;
    ch.diff_fn = PC_GX_DF_CLAMP;
    ch.attn_fn = PC_GX_AF_NONE;
    l.color[0] = 1.0f; l.color[3] = 1.0f;
    set3(l.pos, 0, 0, 10);

    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       out);
    assert(near(out[0], 1.0f));

    /* Facing away: clamped to zero, and the light contributes nothing. */
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, -1},
                       out);
    assert(near(out[0], 0.0f));

    /* 60 degrees off: dot = 0.5. */
    {
        float n[3] = {0.8660254f, 0, 0.5f};
        pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, n, out);
        assert(near(out[0], 0.5f));
    }

    /* A masked-out light contributes nothing however good the geometry. */
    ch.light_mask = 0;
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       out);
    assert(near(out[0], 0.0f));
    puts("PASS: GX_AF_NONE with GX_DF_CLAMP, including the unlit side and the "
         "light mask");
}

static void test_diffuse_sign_vs_clamp(void)
{
    pc_gx_light_channel ch;
    pc_gx_light_src l;
    float out[4];

    memset(&ch, 0, sizeof ch);
    memset(&l, 0, sizeof l);
    ch.mat[0] = ch.mat[1] = ch.mat[2] = ch.mat[3] = 1.0f;
    ch.light_mask = 1;
    ch.attn_fn = PC_GX_AF_NONE;
    l.color[0] = 1.0f;
    set3(l.pos, 0, 0, 10);

    /* SIGN keeps the negative lobe, which then clamps to zero only at the
       very end -- so a surface facing away still reads as zero here, but for
       a different reason than CLAMP, and would subtract from another light. */
    ch.diff_fn = PC_GX_DF_SIGN;
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, -1},
                       out);
    assert(near(out[0], 0.0f));

    /* Two lights: a negative SIGN lobe genuinely cancels a positive one. */
    {
        pc_gx_light_src two[2];
        memset(two, 0, sizeof two);
        two[0] = l;
        two[1] = l;
        set3(two[1].pos, 0, 0, -10);
        two[0].color[0] = 1.0f;
        two[1].color[0] = 1.0f;
        ch.light_mask = 3;
        pc_gx_light_vertex(&ch, two, 2, (float[3]){0, 0, 0},
                           (float[3]){0, 0, 1}, out);
        assert(near(out[0], 0.0f)); /* +1 from one, -1 from the other */

        ch.diff_fn = PC_GX_DF_CLAMP;
        pc_gx_light_vertex(&ch, two, 2, (float[3]){0, 0, 0},
                           (float[3]){0, 0, 1}, out);
        assert(near(out[0], 1.0f)); /* the facing-away one clamps to zero */
    }
    puts("PASS: GX_DF_SIGN keeps a negative lobe where GX_DF_CLAMP drops it");
}

static void test_spec_attenuation(void)
{
    /* The configuration the menu actually uses: GX_AF_SPEC with GX_DF_CLAMP,
       cos_att a=(0,0,1) and dist_att k=(25,0,-24) -- the values measured from
       lights 2 and 3.
     *
     * With the normal along +z and light.dir along +z, a = dot(n, dir) = 1.
     *   cos_attn  = 0 + 0*1 + 1*1  = 1
     *   |k|       = sqrt(625 + 576) = sqrt(1201)
     *   k_norm    = (25, 0, -24)/sqrt(1201)
     *   dist_attn = (25 - 24)/sqrt(1201) = 1/sqrt(1201)
     *   attn      = 1 / (1/sqrt(1201)) = sqrt(1201)
     * and diff = dot(ldir, n) = 1, so lighting = sqrt(1201) before clamping,
     * which the final clamp pins to 1. */
    pc_gx_light_channel ch;
    pc_gx_light_src l;
    float out[4];

    memset(&ch, 0, sizeof ch);
    memset(&l, 0, sizeof l);
    ch.mat[0] = ch.mat[1] = ch.mat[2] = ch.mat[3] = 1.0f;
    ch.light_mask = 1;
    ch.diff_fn = PC_GX_DF_CLAMP;
    ch.attn_fn = PC_GX_AF_SPEC;
    l.color[0] = l.color[1] = l.color[2] = l.color[3] = 1.0f;
    set3(l.pos, 0, 0, 10);
    set3(l.dir, 0, 0, 1);
    set3(l.cos_att, 0, 0, 1);
    set3(l.dist_att, 25, 0, -24);

    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       out);
    assert(near(out[0], 1.0f));

    /* Rotate the normal so a = dot(n, dir) = 0.5. Then cos_attn = 0.25 and
       dist_attn = (25 - 24*0.25)/sqrt(1201) = 19/sqrt(1201), so
       attn = 0.25*sqrt(1201)/19, and diff = dot(ldir, n) = 0.5. */
    {
        float n[3] = {0.8660254f, 0, 0.5f};
        float expect = (0.25f * sqrtf(1201.0f) / 19.0f) * 0.5f;
        pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, n, out);
        assert(near(out[0], expect));
    }

    /* Facing away from the light: the half-angle term is gated off, so the
       specular contribution is zero even though light.dir still faces it. */
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, -1},
                       out);
    assert(near(out[0], 0.0f));

    puts("PASS: GX_AF_SPEC with the menu's measured a=(0,0,1) k=(25,0,-24)");
}

static void test_spot_uses_distance(void)
{
    /* GX_AF_SPOT attenuates by real distance, unlike SPEC. Doubling the
       distance with k=(0,0,1) quarters the attenuation. */
    pc_gx_light_channel ch;
    pc_gx_light_src l;
    float near_out[4], far_out[4];

    memset(&ch, 0, sizeof ch);
    memset(&l, 0, sizeof l);
    ch.mat[0] = ch.mat[1] = ch.mat[2] = ch.mat[3] = 1.0f;
    ch.light_mask = 1;
    ch.diff_fn = PC_GX_DF_NONE;
    ch.attn_fn = PC_GX_AF_SPOT;
    l.color[0] = 1.0f;
    set3(l.dir, 0, 0, -1);
    set3(l.cos_att, 1, 0, 0);
    set3(l.dist_att, 0, 0, 1);

    set3(l.pos, 0, 0, 1);
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       near_out);
    set3(l.pos, 0, 0, 2);
    pc_gx_light_vertex(&ch, &l, 1, (float[3]){0, 0, 0}, (float[3]){0, 0, 1},
                       far_out);
    assert(near(near_out[0], 1.0f));   /* clamped: 1/1 */
    assert(near(far_out[0], 0.25f));   /* 1/4 */
    puts("PASS: GX_AF_SPOT falls off with distance");
}

int main(void)
{
    test_ambient_and_material();
    test_attn_none_diffuse_clamp();
    test_diffuse_sign_vs_clamp();
    test_spec_attenuation();
    test_spot_uses_distance();
    puts("all GX lighting tests passed");
    return 0;
}
