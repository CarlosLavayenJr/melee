/* Native BP shadow: register layouts are defined by the retained GX SDK's
   GXTev.c/GXPixel.c. This is a deliberately validated four-stage subset,
   not a claim of complete TEV emulation. */
#ifdef PC_GX_RENDERER
#include "pc_gx_material.h"
#include "pc_sys.h"
#include <dolphin/gx.h>
#include "pc_gx_light.h"
#include <string.h>
static uint32_t bp[256], mask = 0xffffff;
static uint32_t tev_registers[8];
static uint32_t konst_registers[8];
static int identity_texgen[8];
/* A texgen this renderer can reproduce: an identity 2x4 generator reading the
   vertex's own TEX0. The post-transform matrix is allowed to be anything,
   because that is where sysdolphin puts the actual work -- see below. */
static int simple_texgen[8];
static unsigned texgen_post[8];
/* GX_PTTEXMTX0..19, three apart. Only the first two rows matter for a 2x4
   generator, whose third input component is 1. */
static float pt_matrix[20][2][4];
static int pt_loaded[20];
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
/* Channel 1, which no vertex in the menu supplies a colour for: all 7280
   draws rejected for raster channel 1 were measured to have no GX_VA_CLR1 in
   the vertex descriptor, so the channel is produced by the lighting/channel
   control path rather than read from the vertex. Record what that path is
   actually configured as before implementing any of it. */
static struct chan_config {
    unsigned chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn;
    unsigned count;
} chan_seen[16];
static unsigned chan_seen_count;
static unsigned char chan_mat_color[4][4], chan_amb_color[4][4];

/* The live control state per channel, so a rejected draw can be attributed to
   the configuration actually in force rather than to a call count. GX's
   combined ids set two channels at once: GX_COLOR0A0 is colour 0 and alpha 0,
   GX_COLOR1A1 is colour 1 and alpha 1. */
static struct chan_config chan_state[4];

/* Rejected draws, tallied by the channel-1 configuration in force. */
static struct draw_chan_tally {
    struct chan_config cfg;
    unsigned draws, with_nrm;
} chan1_draws[8];
static unsigned chan1_draw_count;

static unsigned split_channels(unsigned chan, unsigned out[2]);

void __real_GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src,
                          GXColorSrc mat_src, u32 light_mask,
                          GXDiffuseFn diff_fn, GXAttnFn attn_fn);
void __wrap_GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src,
                          GXColorSrc mat_src, u32 light_mask,
                          GXDiffuseFn diff_fn, GXAttnFn attn_fn)
{
    unsigned i;
    for (i = 0; i < chan_seen_count; ++i) {
        struct chan_config* c = &chan_seen[i];
        if (c->chan == (unsigned) chan && c->enable == (unsigned) enable &&
            c->amb_src == (unsigned) amb_src && c->mat_src == (unsigned) mat_src &&
            c->light_mask == light_mask && c->diff_fn == (unsigned) diff_fn &&
            c->attn_fn == (unsigned) attn_fn) { c->count++; goto done; }
    }
    if (chan_seen_count < 16) {
        struct chan_config* c = &chan_seen[chan_seen_count++];
        c->chan = chan; c->enable = enable; c->amb_src = amb_src;
        c->mat_src = mat_src; c->light_mask = light_mask;
        c->diff_fn = diff_fn; c->attn_fn = attn_fn; c->count = 1;
    }
done:
    {
        unsigned targets[2], n = split_channels((unsigned) chan, targets), k;
        for (k = 0; k < n; ++k) {
            struct chan_config* c = &chan_state[targets[k]];
            c->chan = targets[k]; c->enable = enable; c->amb_src = amb_src;
            c->mat_src = mat_src; c->light_mask = light_mask;
            c->diff_fn = diff_fn; c->attn_fn = attn_fn;
        }
    }
    __real_GXSetChanCtrl(chan, enable, amb_src, mat_src, light_mask, diff_fn,
                         attn_fn);
}

/* GX_COLOR0A0 and GX_COLOR1A1 address two channels at once, and they are how
   sysdolphin usually sets these -- an earlier version of this only recorded
   ids below 4 and so reported channel 1's colours as permanently black, which
   was an artefact of the instrumentation rather than a fact about the game. */
static unsigned split_channels(unsigned chan, unsigned out[2])
{
    switch (chan) {
    case 0: case 1: case 2: case 3: out[0] = chan; return 1;
    case 4: out[0] = 0; out[1] = 2; return 2; /* GX_COLOR0A0 */
    case 5: out[0] = 1; out[1] = 3; return 2; /* GX_COLOR1A1 */
    default: return 0;
    }
}

static void store_chan_color(unsigned char dst[4][4], GXChannelID chan,
                             GXColor c)
{
    unsigned targets[2], n = split_channels((unsigned) chan, targets), k;
    for (k = 0; k < n; ++k) {
        dst[targets[k]][0] = c.r; dst[targets[k]][1] = c.g;
        dst[targets[k]][2] = c.b; dst[targets[k]][3] = c.a;
    }
}

void __real_GXSetChanMatColor(GXChannelID chan, GXColor c);
void __wrap_GXSetChanMatColor(GXChannelID chan, GXColor c)
{
    store_chan_color(chan_mat_color, chan, c);
    __real_GXSetChanMatColor(chan, c);
}

void __real_GXSetChanAmbColor(GXChannelID chan, GXColor c);
void __wrap_GXSetChanAmbColor(GXChannelID chan, GXColor c)
{
    store_chan_color(chan_amb_color, chan, c);
    __real_GXSetChanAmbColor(chan, c);
}

/* Light objects, as loaded. __GXLightObjInt (GXLight.c:9) is
   reserved[3], Color, a[3], k[3], lpos[3], ldir[3] -- 64 bytes. Channel 1 is
   lit by lights 2 and 3 (mask 12), so what those two actually contain decides
   how much of the GX lighting equation has to exist. */
typedef struct pc_gx_light {
    unsigned char color[4];
    float a[3], k[3], pos[3], dir[3];
    int loaded;
} pc_gx_light;
static pc_gx_light lights[8];

void __real_GXLoadLightObjImm(GXLightObj* obj, GXLightID id);
void __wrap_GXLoadLightObjImm(GXLightObj* obj, GXLightID id)
{
    unsigned mask = (unsigned) id, index = 0;
    __real_GXLoadLightObjImm(obj, id);
    if (!obj || !mask) return;
    while (!(mask & 1) && index < 7) { mask >>= 1; ++index; }
    {
        const unsigned char* raw = (const unsigned char*) obj;
        pc_gx_light* l = &lights[index];
        unsigned c;
        /* GXInitLightColor packs (r<<24)|(g<<16)|(b<<8)|a into a host u32
           (GXLight.c:291), so this is a word to unpack, not four bytes to
           copy -- reading it as bytes reverses the channels. */
        {
            unsigned packed;
            memcpy(&packed, raw + 12, 4);
            l->color[0] = (unsigned char) (packed >> 24);
            l->color[1] = (unsigned char) (packed >> 16);
            l->color[2] = (unsigned char) (packed >> 8);
            l->color[3] = (unsigned char) packed;
        }
        for (c = 0; c < 3; ++c) {
            memcpy(&l->a[c], raw + 16 + c * 4, 4);
            memcpy(&l->k[c], raw + 28 + c * 4, 4);
            memcpy(&l->pos[c], raw + 40 + c * 4, 4);
            memcpy(&l->dir[c], raw + 52 + c * 4, 4);
        }
        l->loaded = 1;
    }
}

static void report_uint(unsigned v);

static void report_float(float v)
{
    /* Two decimals, no libc: enough to read a light's shape from a log. */
    int whole, frac;
    if (v < 0) { pc_sys_log("-"); v = -v; }
    if (v > 1.0e9f) { pc_sys_log("big"); return; }
    whole = (int) v;
    frac = (int) ((v - (float) whole) * 100.0f + 0.5f);
    if (frac >= 100) { whole += 1; frac -= 100; }
    report_uint((unsigned) whole);
    pc_sys_log(".");
    if (frac < 10) pc_sys_log("0");
    report_uint((unsigned) frac);
}

/* Distinct texgen configurations the game actually asks for, with counts.
   Non-identity texgen is 87% of every skipped menu draw, and "non-identity"
   covers a large space -- 2x4 vs 3x4, eight sources, a texture matrix, a
   post-transform matrix, normalisation. Knowing which handful of those the
   menu really uses is the difference between implementing a feature and
   implementing GX. */
static struct texgen_config {
    unsigned type, src, matrix, normalize, post, count;
} texgen_seen[24];
static unsigned texgen_seen_count;

static void note_texgen(unsigned type, unsigned src, unsigned matrix,
                        unsigned normalize, unsigned post)
{
    unsigned i;
    for (i = 0; i < texgen_seen_count; ++i) {
        struct texgen_config* c = &texgen_seen[i];
        if (c->type == type && c->src == src && c->matrix == matrix &&
            c->normalize == normalize && c->post == post) {
            c->count++;
            return;
        }
    }
    if (texgen_seen_count < 24) {
        struct texgen_config* c = &texgen_seen[texgen_seen_count++];
        c->type = type; c->src = src; c->matrix = matrix;
        c->normalize = normalize; c->post = post; c->count = 1;
    }
}

/* sysdolphin loads a texture's whole scale/rotate/translate matrix as the
   POST-transform matrix and leaves the generator itself identity
   (tobj.c:492 setupTextureCoordGen, tobj.c:488 GXLoadTexMtxImm with
   tobj->mtxid, which HSD_TexMapID2PTTexMtx maps to GX_PTTEXMTX0..7). So
   ignoring the post-transform is not a small approximation: it discards the
   entire UV transform of every ordinary textured draw, which measured as
   20743 of 23713 skipped menu draws. */
void __real_GXLoadTexMtxImm(f32 mtx[][4], u32 id, GXTexMtxType type);
void __wrap_GXLoadTexMtxImm(f32 mtx[][4], u32 id, GXTexMtxType type)
{
    if (id >= GX_PTTEXMTX0 && id < GX_PTIDENTITY &&
        (id - GX_PTTEXMTX0) % 3 == 0) {
        unsigned n = (id - GX_PTTEXMTX0) / 3;
        if (n < 20) {
            unsigned r, c;
            for (r = 0; r < 2; ++r)
                for (c = 0; c < 4; ++c) pt_matrix[n][r][c] = mtx[r][c];
            pt_loaded[n] = 1;
            (void) type; /* rows 0 and 1 are identical for 2x4 and 3x4 */
        }
    }
    __real_GXLoadTexMtxImm(mtx, id, type);
}

/* The texcoord the current TEV stages sample through, or -1 when none does.
   Stages that disagree are rejected in pc_gx_material_get; this is only
   asked once that has passed. */
static int active_texcoord(void)
{
    unsigned count = ((bp[0] >> 10) & 15) + 1, i, j;
    for (j = 0; j < count && j < 4; ++j) {
        unsigned tc = bp[0xc0 + j*2], ta = bp[0xc1 + j*2];
        unsigned order = (bp[0x28 + j/2] >> ((j&1)*12)) & 1023;
        for (i = 0; i < 4; ++i) {
            unsigned c = (tc >> (i*4)) & 15, a = (ta >> (4+i*3)) & 7;
            if (c == 8 || c == 9 || a == 4) return (int) ((order >> 3) & 7);
        }
    }
    return -1;
}

int pc_gx_texcoord_transform(float uv[2])
{
    int coord = active_texcoord();
    unsigned n;
    float s, t;
    if (coord < 0) return 0;
    if (texgen_post[coord] == GX_PTIDENTITY) return 0;
    n = (texgen_post[coord] - GX_PTTEXMTX0) / 3;
    if (n >= 20 || !pt_loaded[n]) return 0;
    /* A 2x4 generator's output is (s, t) with a third component of 1, and the
       3x4 post-transform is applied to (s, t, 1, 1). */
    s = uv[0]; t = uv[1];
    uv[0] = pt_matrix[n][0][0] * s + pt_matrix[n][0][1] * t +
            pt_matrix[n][0][2] + pt_matrix[n][0][3];
    uv[1] = pt_matrix[n][1][0] * s + pt_matrix[n][1][1] * t +
            pt_matrix[n][1][2] + pt_matrix[n][1][3];
    return 1;
}

void __real_GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32);
void __wrap_GXSetTexCoordGen2(GXTexCoordID id, GXTexGenType type, GXTexGenSrc src,
                             u32 matrix, GXBool normalize, u32 post)
{
    if ((unsigned)id < 8) {
        identity_texgen[id] = type == GX_TG_MTX2x4 && src == GX_TG_TEX0 &&
            matrix == GX_IDENTITY && !normalize && post == GX_PTIDENTITY;
        simple_texgen[id] = type == GX_TG_MTX2x4 && src == GX_TG_TEX0 &&
            matrix == GX_IDENTITY && !normalize;
        texgen_post[id] = post;
    }
    note_texgen(type, src, matrix, normalize, post);
    __real_GXSetTexCoordGen2(id, type, src, matrix, normalize, post);
}
/* One log line per reason is enough to notice a gap, but not to prioritise
   one: a reason that kills every draw on screen and one that kills a single
   stray draw look identical. Count them, and count the draws that got
   through, so "which unsupported feature is costing the picture" is a
   measurement rather than a guess. pc_gx_material_report prints it. */
static unsigned skip_counts[16];
static const char* skip_reason[16];
static unsigned draws_accepted;
static unsigned raster_ch1_with_clr1, raster_ch1_no_clr1;

static int unsupported(unsigned bit, const char* reason)
{
    static unsigned reported;
    skip_counts[bit]++;
    skip_reason[bit] = reason;
    if (!(reported & (1u << bit))) {
        reported |= 1u << bit;
        pc_sys_log("pc_gx_material: draw skipped: "); pc_sys_log(reason); pc_sys_log("\n");
    }
    return 0;
}
static void report_uint(unsigned v)
{
    char buf[11];
    int i = (int) sizeof buf - 1;
    buf[i] = '\0';
    do { buf[--i] = (char) ('0' + v % 10); v /= 10; } while (v && i > 0);
    pc_sys_log(buf + i);
}
void pc_gx_material_report(void)
{
    unsigned i, total = 0;
    for (i = 0; i < 16; ++i) total += skip_counts[i];
    pc_sys_log("pc_gx_material: draws accepted ");
    report_uint(draws_accepted);
    pc_sys_log(", skipped ");
    report_uint(total);
    pc_sys_log("\n");
    for (i = 0; i < 16; ++i) {
        if (!skip_counts[i]) continue;
        pc_sys_log("  ");
        report_uint(skip_counts[i]);
        pc_sys_log(" x ");
        pc_sys_log(skip_reason[i]);
        pc_sys_log("\n");
    }
    if (raster_ch1_with_clr1 || raster_ch1_no_clr1) {
        pc_sys_log("  of those, channel 1 with a vertex CLR1: ");
        report_uint(raster_ch1_with_clr1);
        pc_sys_log(", without: ");
        report_uint(raster_ch1_no_clr1);
        pc_sys_log("\n");
    }
    if (raster_ch1_with_clr1 || raster_ch1_no_clr1) {
        pc_sys_log("  of those, channel 1 with a vertex CLR1: ");
        report_uint(raster_ch1_with_clr1);
        pc_sys_log(", without: ");
        report_uint(raster_ch1_no_clr1);
        pc_sys_log("\n");
    }
    for (i = 0; i < 8; ++i) {
        unsigned c;
        if (!lights[i].loaded) continue;
        pc_sys_log("  light ");  report_uint(i);
        pc_sys_log(" rgba ");  /* r,g,b,a */
        for (c = 0; c < 4; ++c) { report_uint(lights[i].color[c]); pc_sys_log(c < 3 ? "," : ""); }
        pc_sys_log(" pos ");
        for (c = 0; c < 3; ++c) { report_float(lights[i].pos[c]); pc_sys_log(c < 2 ? "," : ""); }
        pc_sys_log(" dir ");
        for (c = 0; c < 3; ++c) { report_float(lights[i].dir[c]); pc_sys_log(c < 2 ? "," : ""); }
        pc_sys_log(" a ");
        for (c = 0; c < 3; ++c) { report_float(lights[i].a[c]); pc_sys_log(c < 2 ? "," : ""); }
        pc_sys_log(" k ");
        for (c = 0; c < 3; ++c) { report_float(lights[i].k[c]); pc_sys_log(c < 2 ? "," : ""); }
        pc_sys_log("\n");
    }
    for (i = 0; i < chan1_draw_count; ++i) {
        struct draw_chan_tally* d = &chan1_draws[i];
        pc_sys_log("  ch1 draws ");   report_uint(d->draws);
        pc_sys_log(" (with normals "); report_uint(d->with_nrm);
        pc_sys_log(") lit=");         report_uint(d->cfg.enable);
        pc_sys_log(" amb=");          report_uint(d->cfg.amb_src);
        pc_sys_log(" mat=");          report_uint(d->cfg.mat_src);
        pc_sys_log(" lights=");       report_uint(d->cfg.light_mask);
        pc_sys_log(" diff=");         report_uint(d->cfg.diff_fn);
        pc_sys_log(" attn=");         report_uint(d->cfg.attn_fn);
        pc_sys_log("\n");
    }
    for (i = 0; i < chan_seen_count; ++i) {
        struct chan_config* c = &chan_seen[i];
        pc_sys_log("  chan ");        report_uint(c->chan);
        pc_sys_log(" lit=");          report_uint(c->enable);
        pc_sys_log(" amb=");          report_uint(c->amb_src);
        pc_sys_log(" mat=");          report_uint(c->mat_src);
        pc_sys_log(" lights=");       report_uint(c->light_mask);
        pc_sys_log(" diff=");         report_uint(c->diff_fn);
        pc_sys_log(" attn=");         report_uint(c->attn_fn);
        pc_sys_log(" x");             report_uint(c->count);
        pc_sys_log("\n");
    }
    for (i = 1; i < 4; i += 2) {
        pc_sys_log("  chan ");        report_uint(i);
        pc_sys_log(" mat rgba ");
        report_uint(chan_mat_color[i][0]); pc_sys_log(",");
        report_uint(chan_mat_color[i][1]); pc_sys_log(",");
        report_uint(chan_mat_color[i][2]); pc_sys_log(",");
        report_uint(chan_mat_color[i][3]);
        pc_sys_log(" amb rgba ");
        report_uint(chan_amb_color[i][0]); pc_sys_log(",");
        report_uint(chan_amb_color[i][1]); pc_sys_log(",");
        report_uint(chan_amb_color[i][2]); pc_sys_log(",");
        report_uint(chan_amb_color[i][3]);
        pc_sys_log("\n");
    }
    for (i = 0; i < texgen_seen_count; ++i) {
        struct texgen_config* c = &texgen_seen[i];
        pc_sys_log("  texgen type=");   report_uint(c->type);
        pc_sys_log(" src=");            report_uint(c->src);
        pc_sys_log(" mtx=");            report_uint(c->matrix);
        pc_sys_log(" norm=");           report_uint(c->normalize);
        pc_sys_log(" post=");           report_uint(c->post);
        pc_sys_log(" x");               report_uint(c->count);
        pc_sys_log("\n");
    }
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
    int seen_coord = -1;
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
        /* Split three ways deliberately: these have completely different
           fixes, and lumped together they were 90% of every skipped menu
           draw with no way to tell which one mattered. */
        if (tex && !(order & 64))
            return unsupported(12, "TEV stage samples a disabled texture");
        if (tex && coord >= (bp[0] & 15))
            return unsupported(13, "texcoord index beyond enabled texgen count");
        if (tex && !simple_texgen[coord])
            return unsupported(3, "non-identity texgen");
        /* One uv per vertex reaches the shader, so two stages sampling
           through texgens with different post-transforms cannot both be
           right. Say so rather than silently using one of them. */
        if (tex) {
            if (seen_coord >= 0 && (unsigned) seen_coord != coord &&
                texgen_post[seen_coord] != texgen_post[coord])
                return unsupported(14, "stages need different post-transform "
                                       "texture matrices");
            seen_coord = (int) coord;
        }
        if ((tex && (ta & 12)) || (ras && (ta & 3))) return unsupported(4, "TEV swap table");
        if ((tex || ras) && ((bp[0xf6] & 15) != 4 || (bp[0xf7] & 15) != 14))
            return unsupported(4, "non-identity TEV swap table");
        /* Channel 1 is produced by GX lighting, per vertex, and carried in a
           second vertex colour -- see pc_gx_light.h. Accepted only when the
           channel is in a configuration that can actually be reproduced;
           otherwise it falls through to the diagnostic below rather than
           being lit approximately. */
        if (ras && raster == 1 && pc_gx_channel1_supported()) {
            /* fall through to the stage record below */
        } else if (ras && raster != 0 && raster != 7) {
            extern int pc_gx_fifo_vtx_has_clr1(void);
            if (raster == 1) {
                extern int pc_gx_fifo_vtx_has_nrm(void);
                const struct chan_config* live = &chan_state[1];
                unsigned t;
                if (pc_gx_fifo_vtx_has_clr1()) raster_ch1_with_clr1++;
                else raster_ch1_no_clr1++;
                for (t = 0; t < chan1_draw_count; ++t) {
                    struct chan_config* c = &chan1_draws[t].cfg;
                    if (c->enable == live->enable && c->amb_src == live->amb_src &&
                        c->mat_src == live->mat_src &&
                        c->light_mask == live->light_mask &&
                        c->diff_fn == live->diff_fn &&
                        c->attn_fn == live->attn_fn) break;
                }
                if (t == chan1_draw_count && chan1_draw_count < 8)
                    chan1_draws[chan1_draw_count++].cfg = *live;
                if (t < 8) {
                    chan1_draws[t].draws++;
                    if (pc_gx_fifo_vtx_has_nrm()) chan1_draws[t].with_nrm++;
                }
            }
            return unsupported(9, "raster channel other than COLOR0 or ZERO");
        }
        if (!resolve_konst(out->konst[j], ksel & 31, (ksel >> 5) & 31, kc, ka))
            return unsupported(2, "reserved konst selector");
        out->stages[j][0] = tc; out->stages[j][1] = ta;
        out->stages[j][2] = tex ? order & 7 : 8;
        out->stages[j][3] = raster;
        if (tex) out->texture_mask |= 1u << (order & 7);
    }
    if (blend & (2 | 2048)) return unsupported(5, "logic/subtract blending");
    /* Blend factors and the depth comparison are pipeline state, not shader
       code: pc_gx_fifo.c builds a pipeline per distinct combination now, so
       both are carried in the key rather than rejected. */
    if ((bp[0xf1] >> 21) & 7) return unsupported(8, "fog");
    if ((bp[0xf5] >> 2) & 3) return unsupported(11, "depth-texture operation");
    for (i = 0; i < 4; ++i) {
        out->registers[i][0] = signed11(tev_registers[2*i]);
        out->registers[i][3] = signed11(tev_registers[2*i] >> 12);
        out->registers[i][2] = signed11(tev_registers[2*i+1]);
        out->registers[i][1] = signed11(tev_registers[2*i+1] >> 12);
    }
    out->compare = bp[0xf3];
    /* key: blend enable, colour/alpha write masks, cull, source factor,
       destination factor, depth test/write/compare. pc_gx_fifo.c's
       pipeline_for decodes exactly these fields. */
    out->pipeline_key = (blend & 1) | ((blend >> 2) & 6) |
                        (((bp[0] >> 14) & 3) << 3) |
                        ((((blend >> 8) & 7)) << 5) |
                        ((((blend >> 5) & 7)) << 8) |
                        ((bp[0x40] & 1) << 11) |
                        ((((bp[0x40] >> 4) & 1)) << 12) |
                        ((((bp[0x40] >> 1) & 7)) << 13);
    draws_accepted++;
    return 1;
}
#endif

/* Bridge to pc_gx_fifo.c: channel 1's colour for one vertex, or 0 when the
   channel is not in a configuration this can reproduce. Everything it needs --
   the light objects, the channel control state and the two colour registers --
   is already tracked above for diagnostics; this just assembles it.

   Deliberately narrow: the menu was measured to use exactly one configuration
   (lit, both sources from registers, GX_DF_CLAMP, GX_AF_SPEC), so anything
   else returns 0 and the draw is rejected with the existing diagnostic rather
   than lit approximately. */
int pc_gx_channel1_color(const float mv_pos[3], const float mv_nrm[3],
                         float out[4])
{
    const struct chan_config* cfg = &chan_state[1];
    pc_gx_light_channel ch;
    pc_gx_light_src src[8];
    unsigned i, c;

    if (!cfg->enable) return 0;
    if (cfg->amb_src != 0 || cfg->mat_src != 0) return 0; /* GX_SRC_REG only */

    for (c = 0; c < 4; ++c) {
        ch.amb[c] = chan_amb_color[1][c] / 255.0f;
        ch.mat[c] = chan_mat_color[1][c] / 255.0f;
    }
    ch.light_mask = cfg->light_mask;
    ch.diff_fn = cfg->diff_fn;
    ch.attn_fn = cfg->attn_fn;

    for (i = 0; i < 8; ++i) {
        for (c = 0; c < 4; ++c) src[i].color[c] = lights[i].color[c] / 255.0f;
        for (c = 0; c < 3; ++c) {
            src[i].pos[c] = lights[i].pos[c];
            src[i].dir[c] = lights[i].dir[c];
            src[i].cos_att[c] = lights[i].a[c];
            src[i].dist_att[c] = lights[i].k[c];
        }
    }

    pc_gx_light_vertex(&ch, src, 8, mv_pos, mv_nrm, out);
    return 1;
}

/* Whether channel 1 can be produced at all, asked once per draw rather than
   once per vertex. */
int pc_gx_channel1_supported(void)
{
    const struct chan_config* cfg = &chan_state[1];
    return cfg->enable && cfg->amb_src == 0 && cfg->mat_src == 0;
}
