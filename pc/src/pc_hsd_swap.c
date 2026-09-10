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
#include "pc_hsd_archive.h"
#include "pc_hsd_endian.h"

#include <stdint.h>
#include <stdlib.h>

#include <dolphin/gx.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>

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

/* One vertex descriptor, not a list. swap_vertices above walks until
   GX_VA_NULL, which is right for a pobj's own descriptor array but wrong for
   a shape set: get_shape_vertex_xyz dereferences vertex_desc as a single
   entry, and walking past it would claim whatever happens to follow. */
static void swap_vertex_desc_single(HSD_VtxDescList* v)
{
    if (!v || !pc_hsd_claim(v, sizeof *v, PC_HSD_VERTEX)) {
        return;
    }
    v->attr = swap32(v->attr);
    v->attr_type = swap32(v->attr_type);
    v->comp_cnt = swap32(v->comp_cnt);
    v->comp_type = swap32(v->comp_type);
    v->stride = swap16(v->stride);
}

/* Shape animation, which fighters use and the menu never did -- so this stayed
   logged as unimplemented until a VS match asserted on it at pobj.c:842,
   `vertex_buffer_size >= shape_set->nb_vertex_index`, with the count still
   big-endian.

   loadShapeSetDesc (pobj.c:253) copies these fields straight across, so
   converting the descriptor here, before HSD_PObjLoadDesc runs, is enough.
   The index lists are deliberately untouched: they are arrays of relocated
   pointers, and the indices behind them are read a byte at a time in
   big-endian order by get_shape_vertex_xyz (pobj.c:572), which is already
   correct on either host. */
static void swap_shape_set_desc(HSD_ShapeSetDesc* d)
{
    if (!d || !pc_hsd_claim(d, sizeof *d, PC_HSD_SHAPESET)) {
        return;
    }
    d->flags = swap16(d->flags);
    d->nb_shape = swap16(d->nb_shape);
    d->nb_vertex_index = (s32) swap32((u32) d->nb_vertex_index);
    d->nb_normal_index = (s32) swap32((u32) d->nb_normal_index);
    swap_vertex_desc_single(d->vertex_desc);
    swap_vertex_desc_single(d->normal_desc);
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
            swap_shape_set_desc(p->u.shape_set);
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

static void swap_wobj_desc(HSD_WObjDesc* desc)
{
    if (desc && pc_hsd_claim(desc, sizeof *desc, PC_HSD_WOBJ)) swap_vec(&desc->pos);
}

void __real_HSD_WObjInit(HSD_WObj* wobj, HSD_WObjDesc* desc);
void __wrap_HSD_WObjInit(HSD_WObj* wobj, HSD_WObjDesc* desc)
{
    swap_wobj_desc(desc);
    __real_HSD_WObjInit(wobj, desc);
}

HSD_WObj* __real_HSD_WObjLoadDesc(HSD_WObjDesc* desc);
HSD_WObj* __wrap_HSD_WObjLoadDesc(HSD_WObjDesc* desc)
{
    /* Load uses the class method directly, not HSD_WObjInit. */
    swap_wobj_desc(desc);
    return __real_HSD_WObjLoadDesc(desc);
}

static void swap_light_desc(HSD_LightDesc* desc)
{
    for (; desc && pc_hsd_claim(desc, sizeof *desc, PC_HSD_LIGHT); desc = desc->next) {
        unsigned type;
        desc->flags = swap16(desc->flags);
        desc->attnflags = swap16(desc->attnflags);
        type = desc->flags & LOBJ_TYPE_MASK;
        /* GXColor and relocated pointers have no scalar byte-order work. */
        if (type != LOBJ_AMBIENT) swap_wobj_desc(desc->position);
        if (type == LOBJ_SPOT) swap_wobj_desc(desc->interest);
        if (type != LOBJ_POINT && type != LOBJ_SPOT) continue;
        if (!desc->u.p) {
            pc_sys_log("pc_hsd_swap: point/spot light has no attenuation descriptor\n");
            abort();
        }
        /* Mirror LObjLoad's selection: point tests a bit, spot tests nonzero. */
        if ((type == LOBJ_POINT && (desc->attnflags & LOBJ_LIGHT_ATTN)) ||
            (type == LOBJ_SPOT && desc->attnflags != 0)) {
            HSD_LightAttn* a = desc->u.attn;
            if (pc_hsd_claim(a, sizeof *a, PC_HSD_LIGHT_ATTN)) {
                swap_f32(&a->a0); swap_f32(&a->a1); swap_f32(&a->a2);
                swap_f32(&a->k0); swap_f32(&a->k1); swap_f32(&a->k2);
            }
        } else if (type == LOBJ_POINT) {
            HSD_LightPointDesc* p = desc->u.point;
            if (pc_hsd_claim(p, sizeof *p, PC_HSD_LIGHT_POINT)) {
                swap_f32(&p->ref_br); swap_f32(&p->ref_dist);
                p->dist_func = swap32(p->dist_func);
            }
        } else {
            HSD_LightSpotDesc* s = desc->u.spot;
            if (pc_hsd_claim(s, sizeof *s, PC_HSD_LIGHT_SPOT)) {
                swap_f32(&s->cutoff); s->spot_func = swap32(s->spot_func);
                swap_f32(&s->ref_br); swap_f32(&s->ref_dist);
                s->dist_func = swap32(s->dist_func);
            }
        }
    }
}

HSD_LObj* __real_HSD_LObjLoadDesc(HSD_LightDesc* desc);
HSD_LObj* __wrap_HSD_LObjLoadDesc(HSD_LightDesc* desc)
{
    /* Menu point lights arrived as 0x0e00: LObjLoad classified them as ambient,
       leaving the menu's point-light lookup to walk beyond the final node. */
    swap_light_desc(desc);
    return __real_HSD_LObjLoadDesc(desc);
}

/* --- materials and textures ---------------------------------------------
 *
 * Found the same way as the schemas above: DObjLoad's switch on
 * `mobj->rendermode & 0x60000000` hit `default` and panicked (dobj.c:312)
 * with rendermode 0x31001060, whose byte-swapped form 0x60100031 selects a
 * real case. Everything reachable from HSD_MObjDesc is converted here in one
 * pass rather than one field at a time, because the material graph is loaded
 * as a unit and a half-converted one is harder to diagnose than an
 * unconverted one.
 *
 * Byte fields -- HSD_PEDesc, the repeat flags, every GXColor, the whole of
 * HSD_TObjTevDesc except `active` -- are deliberately absent: they have no
 * byte order to get wrong. So are pointer fields, which pc_dvd.c's archive
 * relocation pass already converted.
 */
static void log_uint(unsigned v)
{
    char buf[11];
    int i = (int) sizeof buf - 1;
    buf[i] = 0;
    do {
        buf[--i] = (char) (48 + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    pc_sys_log(buf + i);
}

static void swap_image_desc(struct HSD_ImageDesc* img)
{
    if (!img || !pc_hsd_claim(img, sizeof *img, PC_HSD_IMAGE)) return;
    img->width = swap16(img->width);
    img->height = swap16(img->height);
    img->format = (GXTexFmt) swap32((unsigned) img->format);
    img->mipmap = swap32(img->mipmap);
    swap_f32(&img->minLOD);
    swap_f32(&img->maxLOD);
}

static void swap_tlut_desc(HSD_TlutDesc* t)
{
    if (!t || !pc_hsd_claim(t, sizeof *t, PC_HSD_TLUT)) return;
    t->fmt = (GXTlutFmt) swap32((unsigned) t->fmt);
    t->tlut_name = swap32(t->tlut_name);
    t->n_entries = swap16(t->n_entries);
}

static void swap_texanim(HSD_TexAnim* anim)
{
    for (; anim && pc_hsd_claim(anim, sizeof *anim, PC_HSD_TEXANIM); anim = anim->next) {
        unsigned i;
        anim->id = (GXTexMapID) swap32((unsigned)anim->id);
        anim->n_imagetbl = swap16(anim->n_imagetbl);
        anim->n_tluttbl = swap16(anim->n_tluttbl);
        if ((unsigned)anim->id > GX_TEXMAP7 ||
            (anim->n_imagetbl && !pc_hsd_in_archive(anim->imagetbl,
                (size_t)anim->n_imagetbl * sizeof *anim->imagetbl)) ||
            (anim->n_tluttbl && !pc_hsd_in_archive(anim->tluttbl,
                (size_t)anim->n_tluttbl * sizeof *anim->tluttbl))) {
            pc_sys_log("pc_hsd_swap: invalid texture-animation id or table extent\n");
            abort();
        }
        /* Pointer tables are already relocated. Convert their descriptors before
           the animation selects them, even when the initial texture was valid. */
        for (i = 0; i < anim->n_imagetbl; ++i) swap_image_desc(anim->imagetbl[i]);
        for (i = 0; i < anim->n_tluttbl; ++i) swap_tlut_desc(anim->tluttbl[i]);
    }
}

void __real_HSD_TObjAddAnim(HSD_TObj* tobj, HSD_TexAnim* anim);
void __wrap_HSD_TObjAddAnim(HSD_TObj* tobj, HSD_TexAnim* anim)
{
    swap_texanim(anim);
    __real_HSD_TObjAddAnim(tobj, anim);
}

void __real_HSD_TObjAddAnimAll(HSD_TObj* tobj, HSD_TexAnim* anim);
void __wrap_HSD_TObjAddAnimAll(HSD_TObj* tobj, HSD_TexAnim* anim)
{
    /* All calls AddAnim inside tobj.c, where linker wrapping does not intercept. */
    swap_texanim(anim);
    __real_HSD_TObjAddAnimAll(tobj, anim);
}

static void swap_lod_desc(HSD_TexLODDesc* l)
{
    if (!l || !pc_hsd_claim(l, sizeof *l, PC_HSD_LOD)) return;
    l->minFilt = (GXTexFilter) swap32((unsigned) l->minFilt);
    swap_f32(&l->LODBias);
    l->max_anisotropy = (GXAnisotropy) swap32((unsigned) l->max_anisotropy);
}

static void swap_tobj_tev_desc(HSD_TObjTevDesc* t)
{
    if (!t || !pc_hsd_claim(t, sizeof *t, PC_HSD_TEV)) return;
    t->active = swap32(t->active);
}

static void swap_tobj_desc(HSD_TObjDesc* t)
{
    for (; t && pc_hsd_claim(t, sizeof *t, PC_HSD_TOBJ); t = t->next) {
        t->id = (GXTexMapID) swap32((unsigned) t->id);
        t->src = (GXTexGenSrc) swap32((unsigned) t->src);
        swap_vec(&t->rotate);
        swap_vec(&t->scale);
        swap_vec(&t->translate);
        t->wrap_s = (GXTexWrapMode) swap32((unsigned) t->wrap_s);
        t->wrap_t = (GXTexWrapMode) swap32((unsigned) t->wrap_t);
        t->blend_flags = swap32(t->blend_flags);
        swap_f32(&t->blending);
        t->magFilt = (GXTexFilter) swap32((unsigned) t->magFilt);

        /* The texmap a converted descriptor names has to be one that exists.
           Anything else means this was not a big-endian descriptor and the
           conversion has just corrupted it -- say so rather than draw with
           it. */
        if ((unsigned) t->id > GX_TEXMAP7 && t->id != GX_TEXMAP_NULL &&
            t->id != GX_TEX_DISABLE) {
            pc_sys_log("pc_hsd_swap: tobj descriptor names no valid texmap\n");
            abort();
        }

        swap_image_desc(t->imagedesc);
        swap_tlut_desc(t->tlutdesc);
        swap_lod_desc(t->lod);
        swap_tobj_tev_desc(t->tev);
    }
}

HSD_TObj* __real_HSD_TObjLoadDesc(HSD_TObjDesc* td);
HSD_TObj* __wrap_HSD_TObjLoadDesc(HSD_TObjDesc* td)
{
    /* Wrapped separately from the mobj below because texture animation
       reaches HSD_TObjLoadDesc without going through a material. Whichever
       arrives first converts; pc_hsd_claim makes the other a no-op. */
    swap_tobj_desc(td);
    return __real_HSD_TObjLoadDesc(td);
}

HSD_MObj* __real_HSD_MObjLoadDesc(HSD_MObjDesc* desc);
HSD_MObj* __wrap_HSD_MObjLoadDesc(HSD_MObjDesc* desc)
{
    if (desc && pc_hsd_claim(desc, sizeof *desc, PC_HSD_MOBJ)) {
        pc_hsd_mobj_flags_to_native(&desc->rendermode);
        /* DObjLoad switches on exactly these three; a fourth value is what
           the unconverted descriptor produced, so a fourth value here means
           the conversion did not fix it. */
        switch (desc->rendermode & 0x60000000) {
        case 0:
        case 0x40000000:
        case 0x60000000:
            break;
        default:
            pc_sys_log("pc_hsd_swap: mobj rendermode invalid after "
                       "conversion\n");
            abort();
        }
    }
    if (desc && desc->mat && pc_hsd_claim(desc->mat, sizeof(HSD_Material),
                                          PC_HSD_MATERIAL)) {
        /* ambient/diffuse/specular are GXColor -- four bytes each, no order
           to fix. Only the two floats need it. */
        swap_f32(&desc->mat->alpha);
        swap_f32(&desc->mat->shininess);
    }
    if (desc) {
        swap_tobj_desc(desc->texdesc);
    }
    return __real_HSD_MObjLoadDesc(desc);
}

/* --- whole archives ------------------------------------------------------
 *
 * The two places a .dat is complete, contiguous and at its final address.
 * pc_hsd_archive.c explains why conversion has to happen here rather than
 * inside the DVD read; both of these already take the archive base and its
 * true size, which is exactly what the conversion needs and what a staged
 * read cannot supply.
 *
 * A -1 is reported but not fatal here: both callees perform the same
 * file_size comparison and raise the game's own byte-order diagnostic, which
 * names the archive and both sizes. Aborting first would lose that.
 */
static void convert_archive(void* src, size_t file_size, const char* who)
{
    if (pc_hsd_archive_convert(src, file_size) == -1) {
        pc_sys_log("pc_hsd_archive: ");
        pc_sys_log(who);
        pc_sys_log(": size matches neither byte order; left unconverted\n");
    }
}

s32 __real_HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size);
s32 __wrap_HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size)
{
    convert_archive(src, file_size, "HSD_ArchiveParse");
    return __real_HSD_ArchiveParse(archive, src, file_size);
}

int __real_lbArchiveRelocate(HSD_Archive* archive, u8* src, size_t file_size,
                             intptr_t base_addr);
int __wrap_lbArchiveRelocate(HSD_Archive* archive, u8* src, size_t file_size,
                             intptr_t base_addr)
{
    /* Reached with a memcpy'd copy of an archive that was already converted
       and located once (ftdata.c relocates fighter animation data by a
       delta). pc_hsd_archive_convert recognises that and does nothing. */
    convert_archive(src, file_size, "lbArchiveRelocate");
    return __real_lbArchiveRelocate(archive, src, file_size, base_addr);
}

/* HSD_TObjSetup asserts at tobj.c:1246 -- the `default:` of a switch on
 * `imagedesc->format` -- three frames into a VS match, from the fighter
 * display path. The assert says only "0"; it does not say what the format
 * was, whose image it was, or whether this port ever saw the descriptor.
 *
 * This says all three, at the door, in the run that fails. The mark is the
 * PC_HSD_* value from pc_hsd_endian.h: 0 for memory this port never tracked,
 * 0x80 for a fresh archive body nothing claimed -- which would mean the
 * schema never reached this descriptor -- and PC_HSD_IMAGE if swap_image_desc
 * did convert it, in which case the format is wrong for some other reason and
 * byte order is not the answer.
 *
 * Reading a format straight back out with swap32 tells the two apart at a
 * glance: if the reversed value is a legal GXTexFmt and the mark is 0x80, the
 * descriptor simply was not converted.
 */
void __real_HSD_TObjSetup(HSD_TObj* tobj);
void __wrap_HSD_TObjSetup(HSD_TObj* tobj)
{
    if (tobj != NULL && tobj->imagedesc != NULL) {
        unsigned f = (unsigned) tobj->imagedesc->format;
        int known = (f == GX_TF_I4 || f == GX_TF_I8 || f == GX_TF_IA4 ||
                     f == GX_TF_IA8 || f == GX_TF_RGB565 ||
                     f == GX_TF_RGB5A3 || f == GX_TF_RGBA8 ||
                     f == GX_TF_CMPR || f == GX_TF_C4 || f == GX_TF_C8 ||
                     f == GX_TF_C14X2);
        if (!known) {
            static int said;
            if (said < 4) {
                said++;
                pc_sys_log("pc_hsd_swap: HSD_TObjSetup was handed an image "
                           "descriptor at ");
                log_uint((unsigned) (uintptr_t) tobj->imagedesc);
                pc_sys_log(" whose format reads ");
                log_uint(f);
                pc_sys_log("; this port's mark on it is ");
                log_uint(pc_hsd_kind_at(tobj->imagedesc));
                pc_sys_log(" and byte-reversing the format would give ");
                log_uint(swap32(f));
                pc_sys_log(", with width ");
                log_uint(tobj->imagedesc->width);
                pc_sys_log(" and height ");
                log_uint(tobj->imagedesc->height);
                pc_sys_log("\n");
            }
        }
    }
    __real_HSD_TObjSetup(tobj);
}
