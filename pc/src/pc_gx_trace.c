/* pc_gx_trace.c — a recorder for what the game tries to draw.
 *
 * There is no renderer yet. GXInit returns, the game configures the GPU and
 * issues geometry, and all of it lands in a write-gather FIFO that nothing
 * reads. Building a renderer blind means guessing which of GX's ~114 entry
 * points actually matter for a first frame.
 *
 * This answers that empirically instead. A curated set of GX calls is
 * intercepted at link time -- `ld --wrap=GXBegin` sends callers here and
 * leaves the original reachable as __real_GXBegin -- so nothing in the SDK or
 * the game is modified, and the recorder can be dropped by removing the link
 * flags.
 *
 * Output is deliberately not a flood. Each call site logs its first few
 * occurrences, and a per-frame tally is printed at GXCopyDisp, which is the
 * frame boundary. What comes out is the shape of a frame: how many primitives
 * of which kind, how many display lists and how large, how many texture binds
 * and TEV configurations. That is the specification for the renderer.
 *
 * Enable with tools/phase0/linkexe.sh --trace-gx.
 */
#include "pc_sys.h"

#include <dolphin/gx.h>

#define TRACE_FIRST_N 4 /* how many of each call to log verbatim */

/* Counted per frame, reported at GXCopyDisp. */
static struct {
    unsigned begin, display_lists, dl_bytes, tex_binds, tev_orders;
    unsigned pos_mtx, projections, verts;
} frame;

static struct {
    unsigned begin, display_lists, tex_binds, tev_orders, projections;
} seen;

static void put(const char* s) { pc_sys_log(s); }

static void put_u(unsigned v)
{
    char buf[12];
    int n = 0;
    if (v == 0) {
        put("0");
        return;
    }
    while (v > 0 && n < 12) {
        buf[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    {
        char out[13];
        int i = 0;
        while (n > 0) {
            out[i++] = buf[--n];
        }
        out[i] = 0;
        put(out);
    }
}

static const char* prim_name(GXPrimitive p)
{
    switch ((int) p) {
    case 0x80: return "quads";
    case 0x90: return "triangles";
    case 0x98: return "tristrip";
    case 0xA0: return "trifan";
    case 0xA8: return "lines";
    case 0xB0: return "linestrip";
    case 0xB8: return "points";
    default:   return "prim?";
    }
}

/* --- intercepted calls --- */

void __real_GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
void __wrap_GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts)
{
    frame.begin++;
    frame.verts += nverts;
    if (seen.begin++ < TRACE_FIRST_N) {
        put("gx: GXBegin ");
        put(prim_name(type));
        put(" fmt=");
        put_u((unsigned) vtxfmt);
        put(" verts=");
        put_u(nverts);
        put("\n");
    }
    __real_GXBegin(type, vtxfmt, nverts);
}

void __real_GXCallDisplayList(void* list, u32 nbytes);
void __wrap_GXCallDisplayList(void* list, u32 nbytes)
{
    frame.display_lists++;
    frame.dl_bytes += nbytes;
    if (seen.display_lists++ < TRACE_FIRST_N) {
        /* This is how Melee draws nearly everything: geometry is precompiled
           into GX command streams inside the .dat files and replayed whole. */
        put("gx: GXCallDisplayList ");
        put_u(nbytes);
        put(" bytes\n");
    }
    __real_GXCallDisplayList(list, nbytes);
}

void __real_GXLoadTexObj(GXTexObj* obj, GXTexMapID id);
void __wrap_GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    frame.tex_binds++;
    if (seen.tex_binds++ < TRACE_FIRST_N) {
        put("gx: GXLoadTexObj map=");
        put_u((unsigned) id);
        put("\n");
    }
    __real_GXLoadTexObj(obj, id);
}

void __real_GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord,
                          GXTexMapID map, GXChannelID color);
void __wrap_GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord,
                          GXTexMapID map, GXChannelID color)
{
    frame.tev_orders++;
    if (seen.tev_orders++ < TRACE_FIRST_N) {
        put("gx: GXSetTevOrder stage=");
        put_u((unsigned) stage);
        put(" map=");
        put_u((unsigned) map);
        put("\n");
    }
    __real_GXSetTevOrder(stage, coord, map, color);
}

void __real_GXSetProjection(f32 mtx[4][4], GXProjectionType type);
void __wrap_GXSetProjection(f32 mtx[4][4], GXProjectionType type)
{
    frame.projections++;
    if (seen.projections++ < TRACE_FIRST_N) {
        put("gx: GXSetProjection type=");
        put_u((unsigned) type);
        put("\n");
    }
    __real_GXSetProjection(mtx, type);
}

void __real_GXLoadPosMtxImm(f32 mtx[3][4], u32 id);
void __wrap_GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    frame.pos_mtx++;
    __real_GXLoadPosMtxImm(mtx, id);
}

/* The frame boundary: everything above is what the game drew into it. */
void __real_GXCopyDisp(void* dest, GXBool clear);
void __wrap_GXCopyDisp(void* dest, GXBool clear)
{
    static unsigned frame_no;

    put("gx: frame ");
    put_u(frame_no++);
    put(" -- ");
    put_u(frame.begin);
    put(" prims (");
    put_u(frame.verts);
    put(" verts), ");
    put_u(frame.display_lists);
    put(" display lists (");
    put_u(frame.dl_bytes);
    put(" bytes), ");
    put_u(frame.tex_binds);
    put(" textures, ");
    put_u(frame.tev_orders);
    put(" tev stages, ");
    put_u(frame.pos_mtx);
    put(" matrices\n");

    frame.begin = frame.verts = 0;
    frame.display_lists = frame.dl_bytes = 0;
    frame.tex_binds = frame.tev_orders = 0;
    frame.pos_mtx = frame.projections = 0;

    __real_GXCopyDisp(dest, clear);
}
