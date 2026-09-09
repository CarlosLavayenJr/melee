#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc_hsd_endian.h"
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

void pc_sys_log(const char* s) { fputs(s, stderr); }
void __real_HSD_CObjInit(HSD_CObj* c, HSD_CObjDesc* d) { (void)c; (void)d; }
HSD_CObj* __real_HSD_CObjLoadDesc(HSD_CObjDesc* d) { (void)d; return NULL; }
HSD_PObj* __real_HSD_PObjLoadDesc(HSD_PObjDesc* d) { return (HSD_PObj*)d; }
HSD_PObj* __wrap_HSD_PObjLoadDesc(HSD_PObjDesc* d);
HSD_JObj* __real_HSD_JObjLoadJoint(HSD_Joint* j) { return (HSD_JObj*)j; }
HSD_JObj* __wrap_HSD_JObjLoadJoint(HSD_Joint* j);
HSD_MObj* __real_HSD_MObjLoadDesc(HSD_MObjDesc* d) { return (HSD_MObj*)d; }
HSD_MObj* __wrap_HSD_MObjLoadDesc(HSD_MObjDesc* d);
HSD_TObj* __real_HSD_TObjLoadDesc(HSD_TObjDesc* d) { return (HSD_TObj*)d; }
HSD_TObj* __wrap_HSD_TObjLoadDesc(HSD_TObjDesc* d);
void __real_HSD_WObjInit(HSD_WObj* w, HSD_WObjDesc* d) { (void)w; (void)d; }
void __wrap_HSD_WObjInit(HSD_WObj* w, HSD_WObjDesc* d);

int main(void)
{
    unsigned char* block = VirtualAlloc((void*)0x81000000u, 65536,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    HSD_PObjDesc *p, *next;
    HSD_VtxDescList* v;
    HSD_EnvelopeDesc** list;
    HSD_EnvelopeDesc* env;
    HSD_PObjDesc native = {0};
    unsigned weight = 0x0000803f;
    assert(block == (void*)0x81000000u);
    p = (void*)block; next = (void*)(block+64); v = (void*)(block+128);
    list = (void*)(block+256); env = (void*)(block+320);
    p->next = next; p->verts = next->verts = v;
    p->flags = next->flags = 0x01a0; p->n_display = next->n_display = 0x0400;
    p->u.envelope_p = next->u.envelope_p = list;
    list[0] = env; list[1] = NULL;
    env->joint = (HSD_Joint*)(block+1024); memcpy(&env->weight, &weight, 4);
    v[0].attr = 0x09000000; v[0].attr_type = 0x03000000;
    v[0].comp_cnt = 0x01000000; v[0].comp_type = 0x04000000;
    v[0].frac = 7; v[0].stride = 0x0c00; v[0].vertex = block+2048;
    v[1].attr = 0xff000000;
    pc_hsd_archive_body(block, 4096);
    __wrap_HSD_PObjLoadDesc(p);
    assert(p->flags == 0xa001 && p->n_display == 4);
    assert(next->flags == 0xa001 && next->n_display == 4);
    assert(p->u.envelope_p == list && env->joint == (HSD_Joint*)(block+1024));
    assert(env->weight == 1.0f);
    assert(v[0].attr == GX_VA_POS && v[0].attr_type == GX_INDEX16);
    assert(v[0].comp_type == GX_F32 && v[0].stride == 12 && v[0].frac == 7);
    assert(v[0].vertex == block+2048 && v[1].attr == GX_VA_NULL);
    {
        HSD_Joint* j = (void*)(block+1024);
        j->flags = 0x08000020;
        memcpy(&j->scale.x, &weight, 4); memcpy(&j->scale.y, &weight, 4);
        memcpy(&j->scale.z, &weight, 4);
        j->child = j; /* shared/cyclic graph must be visited once */
        __wrap_HSD_JObjLoadJoint(j);
        assert(j->flags == 0x20000008 && j->scale.x == 1.0f && j->scale.y == 1.0f);
        __wrap_HSD_JObjLoadJoint(j); assert(j->scale.x == 1.0f);
    }
    {
        HSD_WObjDesc* w = (void*)(block+1536);
        memcpy(&w->pos.x, &weight, 4);
        __wrap_HSD_WObjInit(NULL, w);
        assert(w->pos.x == 1.0f);
        __wrap_HSD_WObjInit(NULL, w);
        assert(w->pos.x == 1.0f);
    }
    {
        /* Material graph. rendermode 0x31001060 is the value DObjLoad
           actually panicked on: byte-swapped it selects a real blending
           case, which is what made this schema necessary. */
        HSD_MObjDesc* m = (void*)(block+2560);
        HSD_Material* mat = (void*)(block+2624);
        HSD_TObjDesc* t = (void*)(block+2688);
        struct HSD_ImageDesc* img = (void*)(block+2816);
        HSD_TlutDesc* tl = (void*)(block+2880);
        HSD_TexLODDesc* lod = (void*)(block+2944);
        HSD_TObjTevDesc* tev = (void*)(block+3008);

        m->rendermode = 0x31001060; m->mat = mat; m->texdesc = t;
        memcpy(&mat->alpha, &weight, 4); memcpy(&mat->shininess, &weight, 4);
        mat->ambient.r = 1; mat->ambient.a = 4;
        t->id = 0x02000000;               /* GX_TEXMAP2 */
        t->src = 0x04000000; t->wrap_s = 0x01000000; t->wrap_t = 0x01000000;
        t->blend_flags = 0x000000ff; t->magFilt = 0x01000000;
        memcpy(&t->scale.x, &weight, 4); memcpy(&t->blending, &weight, 4);
        t->repeat_s = 3; t->repeat_t = 5;
        t->imagedesc = img; t->tlutdesc = tl; t->lod = lod; t->tev = tev;
        img->width = 0x8002; img->height = 0xe001;   /* 640, 480 */
        img->format = 0x06000000; img->mipmap = 0x01000000;
        memcpy(&img->minLOD, &weight, 4);
        img->image_ptr = block+3072;
        tl->fmt = 0x01000000; tl->tlut_name = 0x02000000; tl->n_entries = 0x0001;
        tl->lut = block+3200;
        lod->minFilt = 0x01000000; memcpy(&lod->LODBias, &weight, 4);
        lod->max_anisotropy = 0x01000000; lod->bias_clamp = 1;
        tev->active = 0x00000080; tev->color_op = 9; tev->konst.g = 7;

        __wrap_HSD_MObjLoadDesc(m);
        assert(m->rendermode == 0x60100031);
        assert((m->rendermode & 0x60000000) == 0x60000000);
        assert(mat->alpha == 1.0f && mat->shininess == 1.0f);
        assert(mat->ambient.r == 1 && mat->ambient.a == 4); /* GXColor kept */
        assert(t->id == GX_TEXMAP2 && t->src == 4);
        assert(t->wrap_s == 1 && t->wrap_t == 1 && t->magFilt == 1);
        assert(t->blend_flags == 0xff000000 && t->blending == 1.0f);
        assert(t->scale.x == 1.0f);
        assert(t->repeat_s == 3 && t->repeat_t == 5); /* bytes untouched */
        assert(img->width == 640 && img->height == 480);
        assert(img->format == 6 && img->mipmap == 1 && img->minLOD == 1.0f);
        assert(img->image_ptr == block+3072);        /* pointer untouched */
        assert(tl->fmt == 1 && tl->tlut_name == 2 && tl->n_entries == 256);
        assert(tl->lut == block+3200);
        assert(lod->minFilt == 1 && lod->LODBias == 1.0f);
        assert(lod->max_anisotropy == 1 && lod->bias_clamp == 1);
        assert(tev->active == 0x80000000);
        assert(tev->color_op == 9 && tev->konst.g == 7); /* bytes untouched */

        /* Converting twice must not undo it, whichever entry point runs. */
        __wrap_HSD_MObjLoadDesc(m);
        __wrap_HSD_TObjLoadDesc(t);
        assert(m->rendermode == 0x60100031 && img->width == 640);
        assert(t->id == GX_TEXMAP2 && tev->active == 0x80000000);
    }
    __wrap_HSD_PObjLoadDesc(p); __wrap_HSD_PObjLoadDesc(next);
    assert(p->flags == 0xa001 && env->weight == 1.0f && v[0].stride == 12);
    native.flags = 0x1234; __wrap_HSD_PObjLoadDesc(&native); assert(native.flags == 0x1234);
    pc_hsd_forget_range(block, 4096);
    assert(!pc_hsd_claim(block, 24, PC_HSD_POBJ));
    pc_hsd_archive_body(block, 4096);
    assert(pc_hsd_claim(block, 24, PC_HSD_POBJ));
    assert(!pc_hsd_claim(block, 24, PC_HSD_POBJ));
    pc_hsd_forget_range(block, 4096);
    VirtualFree(block, 0, MEM_RELEASE);
    puts("PASS: archive provenance, shared descriptors, envelope flags/weights, "
         "vertex metadata, material/texture/image/tlut/lod/tev schemas, "
         "reload reset");
    return 0;
}
