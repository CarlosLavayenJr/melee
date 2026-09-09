/* pc_hsd_swap.c — byte-order fixups for HSD object descriptors.
 *
 * pc_dvd.c's swap_hsd_archive() converts an archive's container -- header,
 * three tables -- in full, and the specific body words its own relocation
 * table names as pointers, and stops there deliberately: the rest of the
 * body is whatever object the archive holds, and nothing at that layer
 * knows its shape. This is where a shape gets named, one struct at a time,
 * as each one turns out to be needed -- confirmed the same way the archive
 * schema itself was, by a real assertion naming a real file and line
 * (HSD_ASSERT(0x7D0, 0), cobj.c:1308, an unswapped GXProjectionType hitting
 * `default` in CObjLoad's switch) rather than guessed from the struct
 * definition alone.
 *
 * Mechanism: `ld --wrap`, same as pc_gx_render.c -- the SDK's own entry
 * points into baselib are the interception point, not a filename, since
 * this data has already left the disc-read layer by the time its shape is
 * knowable. Pointer fields (class_name, eyepos, interest, up_vector) are
 * not touched here: those offsets are already in the archive's own
 * relocation table, which pc_dvd.c's schema already swapped before
 * HSD_ArchiveParse's Locate() added the load address to them, so they
 * arrive here as valid host pointers already.
 */
#include "pc_sys.h"

#include <dolphin/gx.h>
#include <sysdolphin/baselib/cobj.h>

static unsigned short swap16(unsigned short v)
{
    return (unsigned short) ((v >> 8) | (v << 8));
}

static unsigned int swap32(unsigned int v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
           ((v << 24) & 0xFF000000u);
}

static void swap_f32(f32* p)
{
    unsigned int v;
    __builtin_memcpy(&v, p, 4);
    v = swap32(v);
    __builtin_memcpy(p, &v, 4);
}

static void swap_cobj_desc(HSD_CObjDesc* desc)
{
    if (desc == NULL) {
        return;
    }

    /* HSD_CObjInit/HSD_CObjLoadDesc are reached with two different kinds of
       desc: real archive body data (still big-endian, needs this) and
       descriptors the decomp declares as plain C struct literals -- e.g.
       devtext_CObjDesc (textdraw.c) -- which this compiler already built in
       host order, and which swapping here would corrupt instead of fix.
       There is no pointer-range test available at this layer to tell them
       apart, but projection_type doesn't need one: it is one of exactly
       PROJ_PERSPECTIVE/FRUSTUM/ORTHO (1/2/3), and its byte-swapped 16-bit
       form (256/512/768) cannot collide with any of them. Already-valid
       means already-host-order; leave it alone. */
    switch (desc->common.projection_type) {
    case PROJ_PERSPECTIVE:
    case PROJ_FRUSTUM:
    case PROJ_ORTHO:
        return;
    default:
        break;
    }

    desc->common.flags = swap16(desc->common.flags);
    desc->common.projection_type = swap16(desc->common.projection_type);

    desc->common.viewport.xmin = (s16) swap16((u16) desc->common.viewport.xmin);
    desc->common.viewport.xmax = (s16) swap16((u16) desc->common.viewport.xmax);
    desc->common.viewport.ymin = (s16) swap16((u16) desc->common.viewport.ymin);
    desc->common.viewport.ymax = (s16) swap16((u16) desc->common.viewport.ymax);

    desc->common.scissor.left = swap16(desc->common.scissor.left);
    desc->common.scissor.right = swap16(desc->common.scissor.right);
    desc->common.scissor.top = swap16(desc->common.scissor.top);
    desc->common.scissor.bottom = swap16(desc->common.scissor.bottom);

    swap_f32(&desc->common.roll);
    swap_f32(&desc->common.nnear);
    swap_f32(&desc->common.ffar);

    /* Read after the swap above -- it is the same field, common.projection_type
       and frustum/perspective.projection_type alias in the union. */
    switch (desc->common.projection_type) {
    case PROJ_PERSPECTIVE:
        swap_f32(&desc->perspective.fov);
        swap_f32(&desc->perspective.aspect);
        break;
    case PROJ_FRUSTUM:
    case PROJ_ORTHO:
        swap_f32(&desc->frustum.top);
        swap_f32(&desc->frustum.bottom);
        swap_f32(&desc->frustum.left);
        swap_f32(&desc->frustum.right);
        break;
    default:
        /* Still unswapped, or a shape this function doesn't know either --
           either way, __real_ below will hit the same diagnostic assert
           CObjLoad already has, rather than this silently doing nothing. */
        break;
    }
}

void __real_HSD_CObjInit(HSD_CObj* cobj, HSD_CObjDesc* desc);
void __wrap_HSD_CObjInit(HSD_CObj* cobj, HSD_CObjDesc* desc)
{
    swap_cobj_desc(desc);
    __real_HSD_CObjInit(cobj, desc);
}

HSD_CObj* __real_HSD_CObjLoadDesc(HSD_CObjDesc* desc);
HSD_CObj* __wrap_HSD_CObjLoadDesc(HSD_CObjDesc* desc)
{
    swap_cobj_desc(desc);
    return __real_HSD_CObjLoadDesc(desc);
}
