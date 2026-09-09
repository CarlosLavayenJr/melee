/* stage_data_test.c -- known-answer tests for the stage archive schemas.
 *
 *   gcc -m32 -w -O0 -g -std=gnu17 -Wno-error=implicit-function-declaration \
 *       -include tools/phase0/compat.h -I pc/src -I src \
 *       -I extern/dolphin/include -DVERSION_GALE01 -DBUILD_VERSION=0 \
 *       pc/tests/stage_data_test.c pc/src/pc_stage_data.c \
 *       pc/src/pc_hsd_endian.c -Wl,--large-address-aware -o t.exe
 *
 * (-I extern/dolphin/include/libc must be left out: its assert.h shadows the
 * host's and defines no assert.)
 */
#include <windows.h>

#include "pc_hsd_endian.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <melee/gr/types.h>
#include <melee/mp/types.h>

void pc_map_coll_to_native(MapCollData* d);
void pc_stage_head_to_native(UnkStageDat* d);
void pc_ground_param_to_native(GroundParam* p);
void pc_stage_itemdata_to_native(struct GroundItemData** t);
void pc_sys_log(const char* s) { fputs(s, stderr); }
void* __real_HSD_ArchiveGetPublicAddress(HSD_Archive* a, const char* s)
{ (void) a; (void) s; return NULL; }

static void be32(void* p, unsigned v)
{
    unsigned char* b = p;
    b[0] = (unsigned char) (v >> 24);
    b[1] = (unsigned char) (v >> 16);
    b[2] = (unsigned char) (v >> 8);
    b[3] = (unsigned char) v;
}

static void be16(void* p, unsigned v)
{
    unsigned char* b = p;
    b[0] = (unsigned char) (v >> 8);
    b[1] = (unsigned char) v;
}

#define DATA_OFF 0x0000
#define VERT_OFF 0x0100
#define LINE_OFF 0x0200
#define JOINT_OFF 0x0300
#define BLOCK 0x1000

/* Two verts, one line, one joint, laid out big-endian as a stage archive
   holds them. The three array pointers are already host order, because the
   archive relocation pass converted and based them before mpLibLoad runs. */
static void build(unsigned char* b)
{
    unsigned char* d = b + DATA_OFF;
    unsigned char* v = b + VERT_OFF;
    unsigned char* l = b + LINE_OFF;
    unsigned char* j = b + JOINT_OFF;
    MapCollData* md = (MapCollData*) d;

    memset(b, 0, BLOCK);

    md->verts = (Vec2*) v;   /* host-order pointers, as relocated */
    md->lines = (MapLine*) l;
    md->joints = (MapJoint*) j;
    be32(d + 0x04, 2);  /* vert_count */
    be32(d + 0x0C, 1);  /* line_count */
    be16(d + 0x10, 3);  /* floor_start */
    be16(d + 0x12, 4);  /* floor_count */
    be16(d + 0x14, 5);
    be16(d + 0x16, 6);
    be16(d + 0x18, 7);
    be16(d + 0x1A, 8);
    be16(d + 0x1C, 9);
    be16(d + 0x1E, 10);
    be16(d + 0x20, 11);
    be16(d + 0x22, 12);
    be32(d + 0x28, 1);        /* joint_count */
    be32(d + 0x2C, 0x1234);   /* x2C */

    be32(v + 0x00, 0x3F800000); /* 1.0f */
    be32(v + 0x04, 0xC0000000); /* -2.0f */
    be32(v + 0x08, 0x40400000); /* 3.0f */
    be32(v + 0x0C, 0x40800000); /* 4.0f */

    be16(l + 0x00, 0x0102);
    be16(l + 0x02, 0x0304);
    be16(l + 0x04, 0xFFFF); /* prev_id0 = -1, a real sentinel in this data */
    be16(l + 0x06, 0x0007);
    be16(l + 0x08, 0x0008);
    be16(l + 0x0A, 0x0009);
    be16(l + 0x0C, 0xABCD);
    be16(l + 0x0E, 0x00EF);

    be16(j + 0x00, 1);
    be16(j + 0x02, 2);
    be16(j + 0x04, 3);
    be16(j + 0x06, 4);
    be16(j + 0x08, 5);
    be16(j + 0x0A, 6);
    be16(j + 0x0C, 7);
    be16(j + 0x0E, 8);
    be16(j + 0x10, 9);
    be16(j + 0x12, 10);
    be32(j + 0x14, 0xC1200000); /* left_bound   = -10.0f */
    be32(j + 0x18, 0xC1A00000); /* bottom_bound = -20.0f */
    be32(j + 0x1C, 0x41200000); /* right_bound  =  10.0f */
    be32(j + 0x20, 0x41A00000); /* top_bound    =  20.0f */
    be16(j + 0x24, 11);
    be16(j + 0x26, 12);
}

static void test_converts(unsigned char* b)
{
    MapCollData* d = (MapCollData*) (b + DATA_OFF);
    Vec2* v;
    MapLine* l;
    MapJoint* j;

    build(b);
    pc_hsd_forget_range(b, BLOCK);
    pc_hsd_archive_body(b, BLOCK);
    pc_map_coll_to_native(d);

    assert(d->vert_count == 2 && d->line_count == 1 && d->joint_count == 1);
    assert(d->x2C == 0x1234);
    assert(d->floor_start == 3 && d->floor_count == 4);
    assert(d->ceiling_start == 5 && d->ceiling_count == 6);
    assert(d->right_wall_start == 7 && d->right_wall_count == 8);
    assert(d->left_wall_start == 9 && d->left_wall_count == 10);
    assert(d->dynamic_start == 11 && d->dynamic_count == 12);
    /* The three pointers were already host order and must not be swapped. */
    assert((unsigned char*) d->verts == b + VERT_OFF);
    assert((unsigned char*) d->lines == b + LINE_OFF);
    assert((unsigned char*) d->joints == b + JOINT_OFF);

    v = d->verts;
    assert(v[0].x == 1.0f && v[0].y == -2.0f);
    assert(v[1].x == 3.0f && v[1].y == 4.0f);

    l = d->lines;
    assert(l->v0_idx == 0x0102 && l->v1_idx == 0x0304);
    assert(l->prev_id0 == -1); /* signed fields keep their sign */
    assert(l->next_id0 == 7 && l->prev_id1 == 8 && l->next_id1 == 9);
    assert(l->hi_flags == 0xABCD && l->lo_flags == 0x00EF);

    j = d->joints;
    assert(j->floor_start == 1 && j->floor_count == 2);
    assert(j->ceiling_start == 3 && j->ceiling_count == 4);
    assert(j->right_wall_start == 5 && j->right_wall_count == 6);
    assert(j->left_wall_start == 7 && j->left_wall_count == 8);
    assert(j->dynamic_start == 9 && j->dynamic_count == 10);
    assert(j->left_bound == -10.0f && j->bottom_bound == -20.0f);
    assert(j->right_bound == 10.0f && j->top_bound == 20.0f);
    assert(j->vtx_start == 11 && j->vtx_count == 12);

    puts("PASS: counts, indices, bounds and vertices convert; array pointers "
         "are left as relocated");
}

static void test_converts_once(unsigned char* b)
{
    unsigned char snapshot[BLOCK];

    build(b);
    pc_hsd_forget_range(b, BLOCK);
    pc_hsd_archive_body(b, BLOCK);
    pc_map_coll_to_native((MapCollData*) (b + DATA_OFF));
    memcpy(snapshot, b, sizeof snapshot);

    pc_map_coll_to_native((MapCollData*) (b + DATA_OFF));
    assert(!memcmp(b, snapshot, sizeof snapshot));

    puts("PASS: a second pass changes nothing");
}

static void test_native_data_is_left_alone(void)
{
    /* mpLibLoad substitutes a natively declared mpLib_803BF760 when handed
       NULL, and that one is already host order. */
    static MapCollData native;
    static MapCollData before;

    native.vert_count = 5;
    native.joint_count = 7;
    memcpy(&before, &native, sizeof before);
    pc_map_coll_to_native(&native);
    assert(!memcmp(&native, &before, sizeof before));
    pc_map_coll_to_native(NULL);

    puts("PASS: natively declared collision data is not converted");
}

/* "map_head": six counts interleaved with six already-based pointers, and the
   one array grDatFiles_801C6228 walks. */
#define HEAD_OFF 0x0800
#define ENTRY_TABLE 0x0900
#define ENTRY_A 0x0A00
#define ENTRY_B 0x0A20
#define PAIR_ENTRIES 0x0B00
#define PAIR_INDICES 0x0B40

static void build_head(unsigned char* b)
{
    unsigned char* h = b + HEAD_OFF;
    UnkStageDat* d = (UnkStageDat*) h;
    UnkStageDatInternal** table = (UnkStageDatInternal**) (b + ENTRY_TABLE);

    memset(b + HEAD_OFF, 0, 0x400);
    be32(h + 0x04, 1); /* unk4: one joint-pair entry at unk0 */
    be32(h + 0x0C, 2);
    be32(h + 0x14, 3);
    be32(h + 0x1C, 4);
    be32(h + 0x24, 5);
    be32(h + 0x2C, 2); /* unk2C: the entry count that crashed the game */
    d->unk28 = table;  /* already based, as relocated */
    table[0] = (UnkStageDatInternal*) (b + ENTRY_A);
    table[1] = (UnkStageDatInternal*) (b + ENTRY_B);
    be32(b + ENTRY_A + 4, 0x00000012);
    be32(b + ENTRY_B + 4, 0x00000034);

    /* unk0: { void* joint; s16* pairs; s32 pair_count; }, as
       Ground_801C34AC declares it. */
    d->unk0 = b + PAIR_ENTRIES;
    *(s16**) (b + PAIR_ENTRIES + 4) = (s16*) (b + PAIR_INDICES);
    be32(b + PAIR_ENTRIES + 8, 3);
    be16(b + PAIR_INDICES + 0, 0x0007);
    be16(b + PAIR_INDICES + 2, 0xFFFF); /* -1, a real sentinel here */
    be16(b + PAIR_INDICES + 4, 0x0102);
}

static void test_head_converts(unsigned char* b)
{
    UnkStageDat* d = (UnkStageDat*) (b + HEAD_OFF);
    unsigned char snapshot[BLOCK];

    build_head(b);
    pc_hsd_forget_range(b, BLOCK);
    pc_hsd_archive_body(b, BLOCK);
    pc_stage_head_to_native(d);

    assert(d->unk4 == 1 && d->unkC == 2 && d->unk14 == 3);
    assert(d->unk1C == 4 && d->unk24 == 5 && d->unk2C == 2);
    assert((unsigned char*) d->unk28 == b + ENTRY_TABLE); /* pointer kept */
    assert(d->unk28[0]->unk4 == 0x12);
    assert(d->unk28[1]->unk4 == 0x34);
    {
        struct { void* joint; s16* pairs; s32 pair_count; }* e = d->unk0;
        assert(e->pair_count == 3);
        assert((unsigned char*) e->pairs == b + PAIR_INDICES);
        assert(e->pairs[0] == 7 && e->pairs[1] == -1 && e->pairs[2] == 0x102);
    }

    memcpy(snapshot, b, sizeof snapshot);
    pc_stage_head_to_native(d);
    assert(!memcmp(b, snapshot, sizeof snapshot));

    puts("PASS: stage head counts and entry flags convert, once, with "
         "pointers kept");
}

/* "grGroundParam" and "itemdata". The first is the one schema here that no
   crash pointed at: ground.c feeds its fields straight into stage setup, so
   getting it wrong is silent. */
#define PARAM_OFF 0x0C00
#define ROWS_OFF 0x0D00
#define ITEM_TABLE 0x0E00
#define ITEM_A 0x0E40
#define ITEM_B 0x0E60

static void test_ground_param(unsigned char* b)
{
    GroundParam* p = (GroundParam*) (b + PARAM_OFF);
    StageParam* rows = (StageParam*) (b + ROWS_OFF);
    unsigned char* q = b + PARAM_OFF;

    memset(b + PARAM_OFF, 0, 0x300);
    be32(q + 0x00, 0x41200000); /* y = 10.0f */
    be16(q + 0x04, 0x1234);
    be16(q + 0x08, 0x0005);
    be32(q + 0x0C, 7);
    be32(q + 0x18, 0x40000000); /* 2.0f */
    be32(q + 0x4C, 1);          /* bool, a whole word here */
    be32(q + 0x50, 0xBF800000); /* -1.0f */
    be16(q + 0x68, 0x00AB);
    be16(q + 0x6A, 0x00CD);     /* first element of the s16 array */
    be32(q + 0xB4, 1);          /* stage_param_count */
    q[0xB8] = 0x11; q[0xB9] = 0x22; q[0xBA] = 0x33; q[0xBB] = 0x44; /* GXColor */
    p->stage_params = rows;     /* already based */
    be32(b + ROWS_OFF + 0x00, 3);      /* stkind */
    be16(b + ROWS_OFF + 0x14, 0x0009); /* x14 */
    be16(b + ROWS_OFF + 0x1A, 0x000E); /* first of the s16 array */

    pc_hsd_forget_range(b, BLOCK);
    pc_hsd_archive_body(b, BLOCK);
    pc_ground_param_to_native(p);

    assert(p->y == 10.0f);
    assert(p->x4 == 0x1234 && p->x8 == 5 && p->xC == 7);
    assert(p->x18 == 2.0f);
    assert(p->x4C_fixed_cam == 1);
    assert(p->x50 == -1.0f);
    assert(p->x68 == 0xAB && p->x6A[0] == 0xCD);
    assert(p->stage_param_count == 1);
    assert((unsigned char*) p->stage_params == b + ROWS_OFF);
    /* GXColor is four bytes and must come through untouched. */
    assert(q[0xB8] == 0x11 && q[0xBB] == 0x44);
    assert(p->stage_params[0].stkind == 3);
    assert(p->stage_params[0].x14 == 9);
    assert(p->stage_params[0].x1A[0] == 0x0E);

    puts("PASS: ground param scalars, s16 arrays and stage param rows convert; "
         "GXColor bytes do not");
}

static void test_itemdata(unsigned char* b)
{
    struct GroundItemData** table = (struct GroundItemData**) (b + ITEM_TABLE);

    memset(b + ITEM_TABLE, 0, 0x200);
    table[0] = (struct GroundItemData*) (b + ITEM_A);
    table[1] = (struct GroundItemData*) (b + ITEM_B);
    table[2] = NULL; /* the terminator ground.c stops on */
    be32(b + ITEM_A, 0x0000010F);
    be32(b + ITEM_B, 0x00000110);

    pc_hsd_forget_range(b, BLOCK);
    pc_hsd_archive_body(b, BLOCK);
    pc_stage_itemdata_to_native(table);

    assert(table[0]->unk0 == 0x10F);
    assert(table[1]->unk0 == 0x110);
    assert(table[2] == NULL);

    puts("PASS: item kinds convert up to the null terminator");
}

int main(void)
{
    unsigned char* block = VirtualAlloc((void*) 0x81000000u, 0x4000,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    assert(block == (void*) 0x81000000u);

    test_converts(block);
    test_converts_once(block);
    test_native_data_is_left_alone();
    test_head_converts(block);
    test_ground_param(block);
    test_itemdata(block);

    pc_hsd_forget_range(block, BLOCK);
    VirtualFree(block, 0, MEM_RELEASE);
    puts("all stage data tests passed");
    return 0;
}
