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
#include <sysdolphin/baselib/lobj.h>

void pc_sys_log(const char* s) { fputs(s, stderr); }
int __real_HSD_ArchiveParse(void* a, void* s, size_t n)
{ (void)a; (void)s; (void)n; return 0; }
int __real_lbArchiveRelocate(void* a, void* s, size_t n, intptr_t base)
{ (void)a; (void)s; (void)n; (void)base; return 0; }
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
HSD_WObj* __real_HSD_WObjLoadDesc(HSD_WObjDesc* d) { return (HSD_WObj*)d; }
HSD_WObj* __wrap_HSD_WObjLoadDesc(HSD_WObjDesc* d);
HSD_LObj* __real_HSD_LObjLoadDesc(HSD_LightDesc* d) { return (HSD_LObj*)d; }
HSD_LObj* __wrap_HSD_LObjLoadDesc(HSD_LightDesc* d);
void __real_HSD_TObjAddAnim(HSD_TObj* t, HSD_TexAnim* a) { (void)t; (void)a; }
void __real_HSD_TObjAddAnimAll(HSD_TObj* t, HSD_TexAnim* a) { (void)t; (void)a; }
void __wrap_HSD_TObjAddAnim(HSD_TObj* t, HSD_TexAnim* a);
void __wrap_HSD_TObjAddAnimAll(HSD_TObj* t, HSD_TexAnim* a);

static void be_float(float* p, float value)
{
    unsigned word;
    memcpy(&word, &value, 4);
    word = (word >> 24) | ((word >> 8) & 0xff00u) |
           ((word << 8) & 0xff0000u) | (word << 24);
    memcpy(p, &word, 4);
}

static void test_lights(unsigned char* block)
{
    HSD_LightDesc* lights = (void*)block;
    HSD_LightPointDesc* point = (void*)(block + 512);
    HSD_LightSpotDesc* spot = (void*)(block + 544);
    HSD_LightAttn* raw = (void*)(block + 576);
    HSD_WObjDesc* position = (void*)(block + 640);
    HSD_WObjDesc* interest = (void*)(block + 704);
    HSD_LightDesc native = {0};
    unsigned char before[768];
    memset(block, 0, 4096);
    pc_hsd_archive_body(block, 4096);
    for (int i = 0; i < 7; ++i) {
        if (i < 6) lights[i].next = &lights[i+1];
        lights[i].color = (GXColor){1, 64, 179, 255};
        lights[i].position = position;
    }
    lights[0].flags = lights[1].flags = 0x0e00;
    lights[0].u.point = lights[1].u.point = point;
    lights[2].flags = 0x0f00; lights[2].u.spot = spot;
    lights[2].interest = interest;
    lights[3].flags = 0x0e00; lights[3].attnflags = 0x0100;
    lights[3].u.attn = raw;
    lights[4].flags = 0x0f00; lights[4].attnflags = 0x0200;
    lights[4].u.attn = raw; lights[4].interest = interest;
    lights[5].flags = 0x0d00; /* infinite, union unused */
    lights[6].flags = 0x2400; /* ambient */
    be_float(&point->ref_br, 0.5f); be_float(&point->ref_dist, 100.0f);
    point->dist_func = 0x02000000;
    be_float(&spot->cutoff, 45.0f); spot->spot_func = 0x03000000;
    be_float(&spot->ref_br, 0.25f); be_float(&spot->ref_dist, 50.0f);
    spot->dist_func = 0x01000000;
    be_float(&raw->a0, 1); be_float(&raw->a1, 2); be_float(&raw->a2, 3);
    be_float(&raw->k0, 4); be_float(&raw->k1, 5); be_float(&raw->k2, 6);
    be_float(&position->pos.x, 10); be_float(&position->pos.y, -20);
    be_float(&position->pos.z, 30); be_float(&interest->pos.z, -1);
    __wrap_HSD_LObjLoadDesc(lights);
    assert(lights[0].flags == 0x0e && lights[6].flags == 0x24);
    assert(lights[0].next == &lights[1] && lights[0].position == position);
    assert(lights[0].color.r == 1 && lights[0].color.b == 179);
    assert(lights[3].attnflags == 1 && lights[4].attnflags == 2);
    assert(point->ref_br == 0.5f && point->ref_dist == 100 && point->dist_func == 2);
    assert(spot->cutoff == 45 && spot->spot_func == 3 && spot->ref_br == 0.25f);
    assert(spot->ref_dist == 50 && spot->dist_func == 1);
    assert(raw->a0 == 1 && raw->a1 == 2 && raw->a2 == 3);
    assert(raw->k0 == 4 && raw->k1 == 5 && raw->k2 == 6);
    assert(position->pos.x == 10 && position->pos.y == -20 && position->pos.z == 30);
    assert(interest->pos.z == -1);
    memcpy(before, block, sizeof before);
    __wrap_HSD_LObjLoadDesc(lights);
    __wrap_HSD_WObjInit(NULL, position);
    __wrap_HSD_WObjLoadDesc(position);
    assert(memcmp(before, block, sizeof before) == 0);
    native.flags = 0x0e; native.u.point = point; native.position = position;
    __wrap_HSD_LObjLoadDesc(&native);
    assert(native.flags == 0x0e && point->ref_dist == 100);
    assert(__wrap_HSD_LObjLoadDesc(NULL) == NULL);
    pc_hsd_forget_range(block, 4096);
    puts("PASS: light types, shared attenuation and WObj positions convert once; native data untouched");
}

static void test_texanim(unsigned char* block)
{
    HSD_TexAnim* anim = (void*)block;
    HSD_ImageDesc** images = (void*)(block+128);
    HSD_TlutDesc** tluts = (void*)(block+144);
    HSD_ImageDesc* image = (void*)(block+192);
    HSD_TlutDesc* tlut = (void*)(block+224);
    HSD_TexAnim native = {0};
    unsigned char before[256];
    memset(block, 0, 4096);
    pc_hsd_archive_body(block, 4096);
    anim->next = &anim[1]; anim->id = 0x03000000;
    anim->imagetbl = images; anim->tluttbl = tluts;
    anim->n_imagetbl = anim->n_tluttbl = 0x0200;
    anim[1].id = 0x04000000; anim[1].imagetbl = images;
    anim[1].n_imagetbl = 0x0200;
    images[0] = images[1] = image; tluts[0] = tluts[1] = tlut;
    image->image_ptr = block+512; image->width = 0x4000; image->height = 0x3000;
    image->format = 0x02000000; be_float(&image->maxLOD, 2);
    tlut->lut = block+1024; tlut->fmt = 0x02000000; tlut->n_entries = 0x0001;
    __wrap_HSD_TObjAddAnimAll(NULL, anim);
    assert(anim->id == GX_TEXMAP3 && anim[1].id == GX_TEXMAP4);
    assert(anim->n_imagetbl == 2 && anim->n_tluttbl == 2);
    assert(image->width == 64 && image->height == 48 && image->format == GX_TF_IA4);
    assert(image->maxLOD == 2 && image->image_ptr == block+512);
    assert(tlut->fmt == GX_TL_RGB5A3 && tlut->n_entries == 256 && tlut->lut == block+1024);
    memcpy(before, block, sizeof before);
    __wrap_HSD_TObjAddAnim(NULL, anim);
    __wrap_HSD_TObjAddAnimAll(NULL, anim);
    assert(memcmp(before, block, sizeof before) == 0);
    native.id = GX_TEXMAP2;
    __wrap_HSD_TObjAddAnim(NULL, &native);
    assert(native.id == GX_TEXMAP2);
    __wrap_HSD_TObjAddAnim(NULL, NULL);
    pc_hsd_forget_range(block, 4096);
    puts("PASS: animation image/TLUT counts, shared descriptors and texmap ids convert once");
}

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
    test_lights(block+4096);
    test_texanim(block+8192);
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
    {
        HSD_MObjDesc* stage_first = (void*)(block+3520);
        HSD_MObjDesc* material_first = (void*)(block+3552);
        memset(stage_first, 0, sizeof *stage_first);
        memset(material_first, 0, sizeof *material_first);
        stage_first->rendermode = material_first->rendermode = 0x31001060;
        pc_hsd_mobj_flags_to_native(&stage_first->rendermode);
        stage_first->rendermode |= 0x04000000; /* grDatFiles_801C6228 mutation */
        __wrap_HSD_MObjLoadDesc(stage_first);
        assert(stage_first->rendermode == 0x64100031);
        __wrap_HSD_MObjLoadDesc(material_first);
        material_first->rendermode |= 0x04000000;
        pc_hsd_mobj_flags_to_native(&material_first->rendermode);
        assert(material_first->rendermode == 0x64100031);
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
