#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc_hsd_endian.h"
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/wobj.h>

void pc_sys_log(const char* s) { fputs(s, stderr); }
void __real_HSD_CObjInit(HSD_CObj* c, HSD_CObjDesc* d) { (void)c; (void)d; }
HSD_CObj* __real_HSD_CObjLoadDesc(HSD_CObjDesc* d) { (void)d; return NULL; }
HSD_PObj* __real_HSD_PObjLoadDesc(HSD_PObjDesc* d) { return (HSD_PObj*)d; }
HSD_PObj* __wrap_HSD_PObjLoadDesc(HSD_PObjDesc* d);
HSD_JObj* __real_HSD_JObjLoadJoint(HSD_Joint* j) { return (HSD_JObj*)j; }
HSD_JObj* __wrap_HSD_JObjLoadJoint(HSD_Joint* j);
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
    puts("PASS: archive provenance, shared descriptors, envelope flags/weights, vertex metadata, reload reset");
    return 0;
}
