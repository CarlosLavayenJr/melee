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
#include "pc_hsd_endian.h"

#include <dolphin/gx.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/jobj.h>

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

static void swap_vertices(HSD_VtxDescList* v)
{
    for (; v && pc_hsd_claim(v, sizeof *v, PC_HSD_VERTEX); ++v) {
        v->attr = swap32(v->attr);
        v->attr_type = swap32(v->attr_type);
        v->comp_cnt = swap32(v->comp_cnt);
        v->comp_type = swap32(v->comp_type);
        v->stride = swap16(v->stride);
        if (v->attr == GX_VA_NULL) break;
    }
}

static void swap_pobj_desc(HSD_PObjDesc* p)
{
    for (; p && pc_hsd_claim(p, sizeof *p, PC_HSD_POBJ); p = p->next) {
        p->flags = swap16(p->flags);
        p->n_display = swap16(p->n_display);
        swap_vertices(p->verts);
        if ((p->flags & 0x3000) == POBJ_ENVELOPE && p->u.envelope_p) {
            HSD_EnvelopeDesc** list;
            for (list = p->u.envelope_p; *list; ++list) {
                HSD_EnvelopeDesc* e;
                for (e = *list; e->joint; ++e) {
                    if (pc_hsd_claim(e, sizeof *e, PC_HSD_ENVELOPE)) swap_f32(&e->weight);
                }
            }
        }
        if ((p->flags & 0x3000) == POBJ_SHAPEANIM)
            pc_sys_log("pc_hsd_swap: shape animation descriptor conversion not implemented\n");
    }
}

HSD_PObj* __real_HSD_PObjLoadDesc(HSD_PObjDesc* desc);
HSD_PObj* __wrap_HSD_PObjLoadDesc(HSD_PObjDesc* desc)
{
    swap_pobj_desc(desc);
    return __real_HSD_PObjLoadDesc(desc);
}

static void swap_vec(Vec3* v)
{
    swap_f32(&v->x); swap_f32(&v->y); swap_f32(&v->z);
}

static void swap_joint_tree(HSD_Joint* j)
{
    for (; j && pc_hsd_claim(j, sizeof *j, PC_HSD_JOINT); j = j->next) {
        unsigned r, c;
        j->flags = swap32(j->flags);
        swap_vec(&j->rotation); swap_vec(&j->scale); swap_vec(&j->position);
        if (j->mtx && pc_hsd_claim(j->mtx, sizeof(Mtx), PC_HSD_MATRIX)) {
            for (r = 0; r < 3; ++r) for (c = 0; c < 4; ++c) swap_f32(&j->mtx[r][c]);
        }
        swap_joint_tree(j->child);
    }
}

HSD_JObj* __real_HSD_JObjLoadJoint(HSD_Joint* joint);
HSD_JObj* __wrap_HSD_JObjLoadJoint(HSD_Joint* joint)
{
    /* Child loading is inlined within jobj.c, so convert the shared descriptor
       graph up front. Claiming before recursion also handles instance cycles. */
    swap_joint_tree(joint);
    return __real_HSD_JObjLoadJoint(joint);
}
