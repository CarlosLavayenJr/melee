/* pc_stage_data.c -- byte order for the structures in a stage archive.
 *
 * Same family as pc_hsd_swap.c and pc_hsd_particle.c, one layer up: this is
 * Melee's own data rather than sysdolphin's, but it reaches memory the same
 * way and needs the same treatment. grdatfiles.c pulls both of these straight
 * out of a stage archive with HSD_ArchiveGetPublicAddress, so their pointer
 * fields are relocation-named and already host order by the time anything here
 * runs, while every count, index and float behind them is still big-endian.
 *
 * Both were found the way every schema in this port has been -- at a real
 * crash on a real line, rather than by reading a struct definition and
 * guessing which fields matter:
 *
 *   "coll_data": mpLibLoad (mplib.c:920) segfaulted under
 *   gm_Scene_Vs_OnEnter on `f31 * coll_data->joints[i].left_bound`, with
 *   joint_count driving a loop off the end of the joint array.
 *
 *   "map_head": grDatFiles_801C6228 (grdatfiles.c:106) segfaulted on
 *   `temp_r4->unk4 |= 0x4000000`, with unk2C -- the length of the array it
 *   walks -- still big-endian.
 *
 * Each is converted at its first consumer, and because every bound comes out
 * of the data itself, each array is checked against pc_hsd_in_archive before
 * the walk follows it.
 */
#include "pc_hsd_endian.h"
#include "pc_sys.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sysdolphin/baselib/archive.h>

#include <melee/gr/types.h>
#include <melee/mp/types.h>

static u16 swap16(u16 v)
{
    return (u16) ((v >> 8) | (v << 8));
}

static u32 swap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
           ((v << 24) & 0xFF000000u);
}

static void swap_s16(s16* p)
{
    *p = (s16) swap16((u16) *p);
}

/* s32 is `long` in this decomp, not `int`; same width on the 32-bit target,
   but the pointer types are distinct and worth matching. */
static void swap_s32(s32* p)
{
    *p = (s32) swap32((u32) *p);
}

static void swap_f32(f32* p)
{
    u32 v;
    memcpy(&v, p, sizeof v);
    v = swap32(v);
    memcpy(p, &v, sizeof v);
}

static void log_uint(u32 v)
{
    char buf[11];
    int i = (int) sizeof buf - 1;
    buf[i] = '\0';
    do {
        buf[--i] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && i > 0);
    pc_sys_log(buf + i);
}

static void coll_failed(const char* what, int count)
{
    pc_sys_log("pc_map_coll: ");
    pc_sys_log(what);
    pc_sys_log(" (");
    log_uint((u32) count);
    pc_sys_log(") reaches outside the stage archive\n");
    abort();
}

/* Checks a count against the archive the data came from before the walk uses
   it. A negative count is refused for the same reason an oversized one is. */
static void check_array(const void* base, int count, size_t stride,
                        const char* what)
{
    if (count < 0 ||
        !pc_hsd_in_archive(base, (size_t) count * stride)) {
        coll_failed(what, count);
    }
}

void pc_map_coll_to_native(MapCollData* d)
{
    int i;

    if (d == NULL || !pc_hsd_claim(d, sizeof *d, PC_HSD_MAPCOLL)) {
        return;
    }

    /* verts, lines and joints are pointers the archive's relocation table
       already converted and Locate() already based. Only the counts and the
       inline index fields are still big-endian. */
    swap_s32(&d->vert_count);
    swap_s32(&d->line_count);
    swap_s16(&d->floor_start);
    swap_s16(&d->floor_count);
    swap_s16(&d->ceiling_start);
    swap_s16(&d->ceiling_count);
    swap_s16(&d->right_wall_start);
    swap_s16(&d->right_wall_count);
    swap_s16(&d->left_wall_start);
    swap_s16(&d->left_wall_count);
    swap_s16(&d->dynamic_start);
    swap_s16(&d->dynamic_count);
    swap_s32(&d->joint_count);
    swap_s32(&d->x2C);

    if (d->verts != NULL) {
        check_array(d->verts, d->vert_count, sizeof d->verts[0],
                    "vertex count");
        for (i = 0; i < d->vert_count; i++) {
            swap_f32(&d->verts[i].x);
            swap_f32(&d->verts[i].y);
        }
    }

    if (d->lines != NULL) {
        check_array(d->lines, d->line_count, sizeof d->lines[0], "line count");
        for (i = 0; i < d->line_count; i++) {
            MapLine* l = &d->lines[i];
            l->v0_idx = swap16(l->v0_idx);
            l->v1_idx = swap16(l->v1_idx);
            swap_s16(&l->prev_id0);
            swap_s16(&l->next_id0);
            swap_s16(&l->prev_id1);
            swap_s16(&l->next_id1);
            l->hi_flags = swap16(l->hi_flags);
            l->lo_flags = swap16(l->lo_flags);
        }
    }

    if (d->joints != NULL) {
        check_array(d->joints, d->joint_count, sizeof d->joints[0],
                    "joint count");
        for (i = 0; i < d->joint_count; i++) {
            MapJoint* j = &d->joints[i];
            swap_s16(&j->floor_start);
            swap_s16(&j->floor_count);
            swap_s16(&j->ceiling_start);
            swap_s16(&j->ceiling_count);
            swap_s16(&j->right_wall_start);
            swap_s16(&j->right_wall_count);
            swap_s16(&j->left_wall_start);
            swap_s16(&j->left_wall_count);
            swap_s16(&j->dynamic_start);
            swap_s16(&j->dynamic_count);
            swap_f32(&j->left_bound);
            swap_f32(&j->bottom_bound);
            swap_f32(&j->right_bound);
            swap_f32(&j->top_bound);
            swap_s16(&j->vtx_start);
            swap_s16(&j->vtx_count);
        }
    }
}

/* "map_head": six (pointer, count) pairs. The pointers are relocation-named
   and already based; the counts are not. Only the array grDatFiles_801C6228
   walks is followed here -- the others are converted when something is
   actually seen to read them, which is the same rule the rest of this port
   has followed rather than guessing at layouts from a header. */
void pc_stage_head_to_native(UnkStageDat* d)
{
    s32 i;

    if (d == NULL || !pc_hsd_claim(d, sizeof *d, PC_HSD_STAGEHEAD)) {
        return;
    }
    swap_s32(&d->unk4);
    swap_s32(&d->unkC);
    swap_s32(&d->unk14);
    swap_s32(&d->unk1C);
    swap_s32(&d->unk24);
    swap_s32(&d->unk2C);

    /* unk0 is `void*` in the header, but Ground_801C34AC declares the real
       shape inline: { void* joint; s16* pairs; s32 pair_count; }, unk4 of
       them. The two pointers are relocation-named and already based; the
       count and the index array behind it are not, which is what sent
       ground.c:1951 walking from 0x81800000 with a count of 0x14000000 --
       twenty, byte-reversed. */
    if (d->unk0 != NULL && d->unk4 != 0) {
        struct StageJointPairs {
            void* joint;
            s16* pairs;
            s32 pair_count;
        }* e = d->unk0;

        check_array(e, d->unk4, sizeof *e, "joint pair entry count");
        for (i = 0; i < d->unk4; i++) {
            if (!pc_hsd_claim(&e[i], sizeof e[i], PC_HSD_STAGEJOINTS)) {
                continue;
            }
            swap_s32(&e[i].pair_count);
            if (e[i].pairs != NULL && e[i].pair_count != 0) {
                int k;
                check_array(e[i].pairs, e[i].pair_count, sizeof e[i].pairs[0],
                            "joint pair count");
                for (k = 0; k < e[i].pair_count; k++) {
                    swap_s16(&e[i].pairs[k]);
                }
            }
        }
    }

    /* unk8 is an array of UnkStageDat_x8_t, one per map id, counted by unkC.
       Its pointers are relocation-named, but three things behind them are
       not: the joint table Ground_801C2ED0 walks (unk20/unk24), and the
       s16 list ground.c:906 walks (x2C/x30). Missing the first is what sent
       mpJointUpdateDynamics a joint id of 256 -- 1, byte-reversed -- and
       segfaulted mplib.c:4800 indexing groundCollJoint with it. x28 is read
       as u8 flags (granime.c:1038) and has no byte order to fix. */
    if (d->unk8 != NULL && d->unkC != 0) {
        check_array(d->unk8, d->unkC, sizeof d->unk8[0], "map entry count");
        for (i = 0; i < d->unkC; i++) {
            struct UnkStageDat_x8_t* e = &d->unk8[i];
            if (!pc_hsd_claim(e, sizeof *e, PC_HSD_STAGEMAPENTRY)) {
                continue;
            }
            swap_s32(&e->unk24);
            swap_s32((s32*) &e->x30);
            /* check_array only asks whether a walk stays inside the archive,
               and the archive is megabytes -- so a count that is wrong but
               not absurd walks over other objects in it and corrupts them
               quietly. These two are small tables in every stage seen so far;
               anything near a thousand entries means the field is not what
               this schema thinks it is, and the walk has to stop and say so
               rather than rewrite whatever follows. */
            if (e->unk24 < 0 || e->unk24 > 1024 || e->x30 < 0 ||
                e->x30 > 1024) {
                pc_sys_log("pc_stage_data: map entry ");
                log_uint((u32) i);
                pc_sys_log(" has implausible counts unk24=");
                log_uint((u32) e->unk24);
                pc_sys_log(" x30=");
                log_uint((u32) e->x30);
                pc_sys_log("; not walking them\n");
                continue;
            }
            if (e->unk20 != NULL && e->unk24 != 0) {
                s32 k;
                check_array(e->unk20, e->unk24, sizeof e->unk20[0],
                            "map entry joint count");
                for (k = 0; k < e->unk24; k++) {
                    swap_s16(&e->unk20[k].x);
                    swap_s16(&e->unk20[k].y);
                    swap_s16(&e->unk20[k].z);
                }
            }
            if (e->x2C != NULL && e->x30 != 0) {
                int k;
                check_array(e->x2C, e->x30, sizeof e->x2C[0],
                            "map entry index count");
                for (k = 0; k < e->x30; k++) {
                    swap_s16(&e->x2C[k]);
                }
            }
        }
    }

    if (d->unk28 == NULL || d->unk2C == 0) {
        return;
    }
    check_array(d->unk28, d->unk2C, sizeof d->unk28[0], "stage entry count");
    for (i = 0; i < d->unk2C; i++) {
        UnkStageDatInternal* e = d->unk28[i];
        /* grDatFiles_801C6228 read-modify-writes this word, so it has to be
           in host order before that happens. */
        /* This is a shallow view of an MObjDesc, not a distinct object.
           Claim only the shared rendermode field; full MObj loading follows. */
        if (e != NULL) pc_hsd_mobj_flags_to_native(&e->unk4);
    }
}

/* "grGroundParam": ground.c reads three dozen of these fields straight into
   Ground_801C38D0 and its neighbours before anything dereferences them, so
   unlike the crashes above this one would not have announced itself -- it
   would just have configured the stage with nonsense. Converted here for that
   reason rather than because something faulted. */
static void swap_stage_param(StageParam* r)
{
    int i;
    swap_s32((int*) &r->stkind);
    swap_s32(&r->x4);
    swap_s32(&r->x8);
    r->xC = swap32(r->xC);
    r->x10 = swap32(r->x10);
    swap_s16(&r->x14);
    swap_s16(&r->x16);
    swap_s16(&r->x18);
    for (i = 0; i < (int) (sizeof r->x1A / sizeof r->x1A[0]); i++) {
        swap_s16(&r->x1A[i]);
    }
}

void pc_ground_param_to_native(GroundParam* p)
{
    int i;

    if (p == NULL) {
        return;
    }
    if (!pc_hsd_claim(p, sizeof *p, PC_HSD_GROUNDPARAM)) {
        /* Either already converted -- right, when the same archive is handed
           out twice -- or memory the DVD path never marked as an archive
           body. The second case leaves Ground_801C28CC searching a
           byte-reversed stkind table, and it ends in
           panicMissingStageParam's `while (true) {}` rather than a crash, so
           nothing downstream would ever say what went wrong. Say it here. */
        if (!pc_hsd_in_archive(p, sizeof *p)) {
            static int warned;
            if (!warned) {
                warned = 1;
                pc_sys_log("pc_stage_data: grGroundParam at ");
                log_uint((u32) (uintptr_t) p);
                pc_sys_log(" carries no archive provenance; its stage table "
                           "is unconverted\n");
            }
        }
        return;
    }
    swap_f32(&p->y);
    swap_s16(&p->x4);
    swap_s16(&p->x8);
    swap_s16(&p->xA);
    swap_s32(&p->xC);
    swap_s32(&p->x10);
    swap_s32(&p->x14);
    swap_f32(&p->x18);
    swap_f32(&p->x1C);
    swap_f32(&p->x20);
    swap_f32(&p->x24);
    swap_f32(&p->x28);
    swap_s16(&p->x2E);
    swap_s32(&p->x30);
    swap_s32(&p->x34);
    swap_s32(&p->x38);
    swap_f32(&p->x3C);
    swap_f32(&p->x40);
    swap_f32(&p->x44);
    swap_f32(&p->x48);
    /* bool is `typedef int bool` here, so it is a whole word. Both byte orders
       of a set flag test true, which is exactly why this one is easy to miss
       and worth doing anyway. */
    swap_s32((int*) &p->x4C_fixed_cam);
    swap_f32(&p->x50);
    swap_f32(&p->x54);
    swap_f32(&p->x58);
    swap_f32(&p->x5C);
    swap_f32(&p->x60);
    swap_f32(&p->x64);
    swap_s16(&p->x68);
    for (i = 0; i < (int) (sizeof p->x6A / sizeof p->x6A[0]); i++) {
        swap_s16(&p->x6A[i]);
    }
    /* stage_params is relocation-named and already based. The GXColor fields
       from xB8 on are four bytes each and have no byte order. */
    swap_s32(&p->stage_param_count);
    if (p->stage_params != NULL) {
        /* On about half the stages Ground_801C28CC then fails to find the row
           for the stage being loaded, and the reason is always the same shape:
           row 0's stkind is already host order before this runs, so converting
           the array leaves that one row reversed while every other row comes
           out right. Something wrote that word first. The mark says which
           schema, by name, instead of leaving it to be guessed -- 0x80 means
           nothing had claimed it, which is the healthy case. */
        unsigned mark = pc_hsd_kind_at(p->stage_params);
        if (mark != 0x80) {
            pc_sys_log("pc_stage_data: stage param row 0 at ");
            log_uint((u32) (uintptr_t) p->stage_params);
            pc_sys_log(" was already claimed by schema ");
            log_uint(mark);
            pc_sys_log("\n");
        }
        check_array(p->stage_params, p->stage_param_count,
                    sizeof p->stage_params[0], "stage param count");
        /* Claim the ARRAY, not just the GroundParam that points at it. Every
           other schema here claims the object it converts, and this one did
           not -- so two GroundParams whose stage_params pointers resolve to
           the same array would each convert it, and the second swap would put
           it back exactly as it came off the disc. That is precisely what the
           failing stages show: row 0 reading as the original file bytes while
           the rest of the archive is fine. Two copies of one archive, one of
           them relocated against the other's base, would do it, and so would
           anything else that hands the same array out under two owners. */
        if (pc_hsd_claim(p->stage_params,
                         (size_t) p->stage_param_count *
                             sizeof p->stage_params[0],
                         PC_HSD_STAGEPARAMS)) {
            for (i = 0; i < p->stage_param_count; i++) {
                swap_stage_param(&p->stage_params[i]);
            }
        }
    }
}

/* "itemdata": a null-terminated array of already-based pointers, each to a
   kind and an article pointer. it_8026B40C indexes a table with the kind, so
   a big-endian one indexes wildly out of bounds -- which is the crash at
   it_26B1.c:213, with kind reading -637534208. */
void pc_stage_itemdata_to_native(struct GroundItemData** table)
{
    int i;

    if (table == NULL) {
        return;
    }
    for (i = 0; pc_hsd_in_archive(&table[i], sizeof table[i]) &&
                table[i] != NULL; i++) {
        struct GroundItemData* e = table[i];
        if (pc_hsd_claim(e, sizeof *e, PC_HSD_STAGEITEM)) {
            swap_s32(&e->unk0);
        }
    }
}

/* Hooked at HSD_ArchiveGetPublicAddress rather than at each consumer, for two
   reasons.
 *
 * The practical one: ld --wrap only redirects references that cross a
 * translation unit. grDatFiles_801C6228 is called from grDatFiles_801C6038 in
 * the same file, so wrapping it did nothing and the crash came back
 * unchanged. Every caller of HSD_ArchiveGetPublicAddress is in some other
 * file, so this one actually takes.
 *
 * The better one: the symbol name IS the type. An archive body is untyped
 * bytes, which is why nothing generic can convert it -- but the public symbol
 * table names the objects in it, and a name tells us exactly which schema
 * applies. That is also the safest place to do it: the data has been parsed
 * and located, and it is being handed out for the first time. */
/* pc_hsd_particle.c. Reached from here as well as from its own wrapper on
   psInitDataBankLocate, because grdatfiles.c's preloaded-archive branch calls
   psInitDataBankLoad WITHOUT Locate, so nothing else converts those banks and
   Load panics on a byte-reversed version (particle.c:207). Converting when
   the symbol is handed out covers both branches; whichever runs second finds
   the work already claimed. */
void pc_hsd_particle_banks_to_native(void* cmd, void* tex, void* form);
/* pc_ft_data.c. */
void pc_ft_common_data_to_native(void* p);
/* pc_it_data.c. */
void pc_it_common_data_to_native(void* p);

void* __real_HSD_ArchiveGetPublicAddress(HSD_Archive* archive,
                                         const char* symbols);
void* __wrap_HSD_ArchiveGetPublicAddress(HSD_Archive* archive,
                                         const char* symbols)
{
    void* p = __real_HSD_ArchiveGetPublicAddress(archive, symbols);

    if (p != NULL && symbols != NULL) {
        if (strcmp(symbols, "coll_data") == 0) {
            pc_map_coll_to_native((MapCollData*) p);
        } else if (strcmp(symbols, "map_head") == 0) {
            pc_stage_head_to_native((UnkStageDat*) p);
        } else if (strcmp(symbols, "grGroundParam") == 0) {
            pc_ground_param_to_native((GroundParam*) p);
        } else if (strcmp(symbols, "itemdata") == 0) {
            pc_stage_itemdata_to_native((struct GroundItemData**) p);
        } else if (strcmp(symbols, "map_ptcl") == 0) {
            pc_hsd_particle_banks_to_native(p, NULL, NULL);
        } else if (strcmp(symbols, "map_texg") == 0) {
            pc_hsd_particle_banks_to_native(NULL, p, NULL);
        } else if (strcmp(symbols, "itPublicData") == 0) {
            /* ItCo.dat's shared item tables. pc_it_data.c. */
            pc_it_common_data_to_native(p);
        } else if (strcmp(symbols, "ftLoadCommonData") == 0) {
            /* Not a stage, but this is the dispatch point: PlCo.dat's shared
               fighter tables arrive through the same door. pc_ft_data.c. */
            pc_ft_common_data_to_native(p);
        }
    }
    return p;
}
