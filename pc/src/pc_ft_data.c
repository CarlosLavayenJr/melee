/* pc_ft_data.c -- byte order for the structures in a fighter's data archive.
 *
 * Same family as pc_stage_data.c: Melee's own data rather than sysdolphin's,
 * pulled straight out of an archive, with every pointer field already host
 * order because the relocation table named it and every count, index and
 * float behind it still big-endian.
 *
 * Found the way every schema in this port has been, at a real stop on a real
 * line rather than by reading a struct and guessing:
 *
 *   ftData_80085A14 (ftdata.c:1674) reported "fighter figatree over! c8190000"
 *   and asserted. 0xc8190000 byte-reversed is 0x000019c8 = 6600, a plausible
 *   animation size; the check it failed is `> 0x8000`.
 *
 * Hooked at ftData_8008572C rather than at HSD_ArchiveGetPublicAddress, which
 * is where the stage schemas hook. The symbol name there would identify the
 * character ("ftDataKirby"), but the two move tables' lengths are not in the
 * archive at all -- they are compiled into ftData_Table_Unk0 and
 * ftData_UnkIntPairs, indexed by FighterKind. ftData_8008572C is handed that
 * kind, is called only from other translation units so --wrap takes, and is
 * the one function that fills gFtDataList. So it has everything, and there is
 * no name-to-kind table to keep in step.
 *
 * Because those counts come from the game's own compiled-in tables rather
 * than from the file, the walks below cannot be steered off the end by bad
 * data, which is what pc_stage_data.c has to guard against. pc_hsd_in_archive
 * is still consulted, but as a provenance check: data that did not arrive as
 * an archive body will not be converted at all by pc_hsd_claim, and that has
 * to be loud rather than silent, because the fighter would then run on
 * byte-reversed attributes.
 */
#include "pc_hsd_endian.h"
#include "pc_sys.h"

#include <stddef.h>
#include <string.h>

#include <melee/ft/dobjlist.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>

static u32 swap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
           ((v << 24) & 0xFF000000u);
}

/* s32 is `long` in this decomp, not `int`; same width on the 32-bit target,
   but the pointer types are distinct and worth matching. */
static void swap_s32(s32* p)
{
    *p = (s32) swap32((u32) *p);
}

/* A run of 4-byte fields whose individual types do not matter, because every
   one of them is a float or an int and both reverse the same way. */
static void swap_words(void* base, size_t bytes)
{
    unsigned char* p = (unsigned char*) base;
    size_t i;
    for (i = 0; i + 4 <= bytes; i += 4) {
        u32 v;
        memcpy(&v, p + i, sizeof v);
        v = swap32(v);
        memcpy(p + i, &v, sizeof v);
    }
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

/* A fighter whose data did not arrive as an archive body would keep every
   attribute and every animation offset byte-reversed, and nothing downstream
   would say so -- the assert that found this schema only fires on one field.
   Say it here instead. Once per character is enough to see it. */
static void not_archive(FighterKind kind, const char* what)
{
    static u8 said[FTKIND_MAX];
    if (kind < FTKIND_MAX) {
        if (said[kind]) {
            return;
        }
        said[kind] = 1;
    }
    pc_sys_log("pc_ft_data: fighter ");
    log_uint((u32) kind);
    pc_sys_log("'s ");
    pc_sys_log(what);
    pc_sys_log(" is not archive memory, so its byte order is unconverted\n");
}

/* The common attributes, ftData+0x00. Every field from +0x000 to +0x17C is a
   four-byte float or int -- see ftCo_DatAttrs in ft/types.h, where the Vec3s
   and the nested xBC block are floats too. The last member,
   weight_independent_throws_mask, is a single byte, so the run stops at its
   offset rather than at sizeof. */
static void attrs_to_native(ftCo_DatAttrs* a)
{
    if (a == NULL || !pc_hsd_claim(a, sizeof *a, PC_HSD_FTATTRS)) {
        return;
    }
    swap_words(a, offsetof(ftCo_DatAttrs, weight_independent_throws_mask));
}

/* One of the two move tables. Fighter_WaitAnimData is
     char* x0                 animation name, a relocated pointer
     s32   x4                 offset of this move's animation in the AJ file
     s32   x8                 its length
     union CmdUnion* xC       subaction script, a relocated pointer
     s32   x10_animCurrFlags
     u32   x14                filled in at run time from x4, so not in the file
   which leaves three words to reverse per entry. */
static void moves_to_native(struct Fighter_WaitAnimData* a, int count,
                            unsigned kind_tag)
{
    int i;
    if (a == NULL || count <= 0) {
        return;
    }
    if (!pc_hsd_claim(a, (size_t) count * sizeof *a, kind_tag)) {
        return;
    }
    for (i = 0; i < count; i++) {
        swap_s32(&a[i].x4);
        swap_s32(&a[i].x8);
        swap_s32(&a[i].x10_animCurrFlags);
    }
}

/* One part-visibility table: model_num entries, each a count and a run of
   {count, u8 indices} records. Only the two counts are words; the index runs
   are bytes and stay as they are. Byte-reversed, the outer count sent
   ftParts_80074D7C indexing dobj_list->data with whatever bytes followed, and
   HSD_DObjSetFlags segfaulted on a dobj of 0x8000000 (ftparts.c:665).

   The same table is shared between costumes -- vis_table[c][s] falls back to
   vis_table[0][s] -- so pc_hsd_claim is what keeps it from being converted
   twice. */
static void vis_lookup_to_native(FighterKind kind, FtPartsVisLookup* lk,
                                 u32 model_num)
{
    u32 i;
    int j;

    if (lk == NULL || model_num == 0) {
        return;
    }
    if (!pc_hsd_in_archive(lk, (size_t) model_num * sizeof *lk)) {
        not_archive(kind, "part visibility table");
        return;
    }
    if (!pc_hsd_claim(lk, (size_t) model_num * sizeof *lk, PC_HSD_FTVIS)) {
        return;
    }
    for (i = 0; i < model_num; i++) {
        swap_s32((s32*) &lk[i].x0);
        if (lk[i].x4 == NULL || lk[i].x0 <= 0) {
            continue;
        }
        if (!pc_hsd_in_archive(lk[i].x4,
                               (size_t) lk[i].x0 * sizeof *lk[i].x4)) {
            not_archive(kind, "part visibility run");
            continue;
        }
        if (!pc_hsd_claim(lk[i].x4, (size_t) lk[i].x0 * sizeof *lk[i].x4,
                          PC_HSD_FTVISRUN)) {
            continue;
        }
        for (j = 0; j < lk[i].x0; j++) {
            swap_s32((s32*) &lk[i].x4[j].x0);
        }
    }
}

/* ftData+0x08: the model description. Two words, and behind the second a
   per-costume list of the texture objects a costume swaps.

   ftAnim_80070200 (ftanim.c:1008) reported "fighter tobj num over!" and
   asserted on the count, which is x8.x8 -- byte-reversed it is enormous
   against an array of 32. The ids that follow it are u16 and are what
   ftParts_80075240 is handed, so they reverse too. There is one list per
   costume, and the number of costumes is compiled in
   (CostumeListsForeachCharacter), not in the file. */
static void models_to_native(FighterKind kind, struct ftData_x8* m)
{
    int costumes = (int) CostumeListsForeachCharacter[kind].numCostumes;
    int c, i, count;

    if (m == NULL || !pc_hsd_claim(m, sizeof *m, PC_HSD_FTMODELS)) {
        return;
    }
    m->x0.model_num = swap32(m->x0.model_num);
    m->x8.x8 = swap32(m->x8.x8);
    count = (int) m->x8.x8;

    /* vis_table is one row of four pointers per costume; each names a
       part-visibility table with model_num entries. */
    if (m->x0.vis_table != NULL && costumes > 0 &&
        pc_hsd_in_archive(m->x0.vis_table,
                          (size_t) costumes * 4 * sizeof(void*)))
    {
        for (c = 0; c < costumes; c++) {
            for (i = 0; i < 4; i++) {
                vis_lookup_to_native(kind,
                                     (FtPartsVisLookup*) m->x0.vis_table[c][i],
                                     m->x0.model_num);
            }
        }
    }

    if (m->x8.xC == NULL || count <= 0 || costumes <= 0) {
        return;
    }
    if (!pc_hsd_in_archive(m->x8.xC, (size_t) costumes * sizeof *m->x8.xC)) {
        not_archive(kind, "costume texture-object lists");
        return;
    }
    for (c = 0; c < costumes; c++) {
        u16* ids = m->x8.xC[c];
        if (ids == NULL) {
            continue;
        }
        if (!pc_hsd_claim(ids, (size_t) count * sizeof *ids,
                          PC_HSD_FTTOBJIDS)) {
            continue;
        }
        for (i = 0; i < count; i++) {
            ids[i] = (u16) ((ids[i] >> 8) | (ids[i] << 8));
        }
    }
}

/* ftData+0x30: the hurtboxes, a count and that many ftHurtboxInit. Every
   member of one is four bytes -- two enums, a u32, two Vec3s and a float --
   so the whole run reverses uniformly. ftColl_8007B320 (ftcoll.c:3224)
   reported "fighter hit num over!" on the count, which it checks against 15.

   ftData+0x2C is the dynamics, reached in the same function: a second count
   (x4) and that many ftData_x38, which is an int and four floats. The bone
   list at +0 is left alone -- ArticleDynamicBones is not described well enough
   here to convert, and nothing has faulted in it yet. */
static void hurtboxes_to_native(FighterKind kind, struct ftData_x30* h)
{
    if (h == NULL || !pc_hsd_claim(h, sizeof *h, PC_HSD_FTHURT)) {
        return;
    }
    swap_s32((s32*) &h->count);
    if (h->inits == NULL || h->count <= 0) {
        return;
    }
    if (!pc_hsd_in_archive(h->inits, (size_t) h->count * sizeof *h->inits)) {
        not_archive(kind, "hurtbox table");
        return;
    }
    swap_words(h->inits, (size_t) h->count * sizeof *h->inits);
}

static void dynamics_to_native(FighterKind kind, struct ftDynamics* d)
{
    if (d == NULL || !pc_hsd_claim(d, sizeof *d, PC_HSD_FTDYNAMICS)) {
        return;
    }
    swap_s32((s32*) &d->dynamicsNum);
    swap_s32((s32*) &d->x4);
    if (d->x8 == NULL || d->x4 <= 0) {
        return;
    }
    if (!pc_hsd_in_archive(d->x8, (size_t) d->x4 * sizeof *d->x8)) {
        not_archive(kind, "dynamics table");
        return;
    }
    swap_words(d->x8, (size_t) d->x4 * sizeof *d->x8);
}

/* PlCo.dat's "ftLoadCommonData" block: 23 relocated pointers to the tables
   every fighter shares (Fighter_LoadCommonData copies them out one by one).
   Three of them are converted here, and the other twenty are NOT -- they will
   announce themselves the way these did, at a line that faults.

   pData[4] is ftPartsTable, one FighterPartsTable per FighterKind, whose
   parts_num bounds the loop in ftParts_80074E58. Byte-reversed it ran off
   the end of fp->parts and segfaulted ftparts.c:681 the moment a fighter was
   created.

   pData[0] is ftCommonData, which would not have faulted at all: it is the
   deadzones, thresholds and knockback constants every fighter reads, so a
   reversed copy is a match that runs and behaves like nothing. Its members
   are four bytes each except x6DC_colorsByPlayer (four GXColors) and the
   four bytes after them, so the run stops and restarts around that gap. */
void pc_ft_common_data_to_native(void* p)
{
    void** table = (void**) p;
    struct ftCommonData* common;
    struct FighterPartsTable** parts;
    struct Fighter_804D6540_t** hidden;
    int i;

    if (table == NULL) {
        return;
    }
    if (!pc_hsd_in_archive(table, 23 * sizeof *table)) {
        pc_sys_log("pc_ft_data: PlCo.dat common data is not archive memory, "
                   "so its byte order is unconverted\n");
        return;
    }

    common = table[0];
    if (common != NULL &&
        pc_hsd_claim(common, sizeof *common, PC_HSD_FTCOMMON))
    {
        size_t gap = offsetof(struct ftCommonData, x6DC_colorsByPlayer);
        size_t after = offsetof(struct ftCommonData, metal_armor);
        swap_words(common, gap);
        swap_words((unsigned char*) common + after, sizeof *common - after);
    }

    parts = table[4];
    if (parts != NULL && pc_hsd_in_archive(parts, FTKIND_MAX * sizeof *parts))
    {
        for (i = 0; i < FTKIND_MAX; i++) {
            struct FighterPartsTable* t = parts[i];
            if (t != NULL && pc_hsd_claim(t, sizeof *t, PC_HSD_FTPARTS)) {
                swap_s32((s32*) &t->parts_num);
            }
        }
    }

    /* pData[5] is Fighter_804D6540, one entry per FighterKind, each a pointer
       to four-byte records and a count of them. Only the count is a word; the
       records are four u8 fields. Byte-reversed, the count sent
       ftParts_8007506C scanning memory for a match and it segfaulted at
       ftparts.c:722 once no early match turned up -- at part 256, having found
       spurious matches in zeroed memory for every smaller part number. */
    hidden = table[5];
    if (hidden != NULL && pc_hsd_in_archive(hidden, FTKIND_MAX * sizeof *hidden))
    {
        for (i = 0; i < FTKIND_MAX; i++) {
            struct Fighter_804D6540_t* h = hidden[i];
            if (h != NULL && pc_hsd_claim(h, sizeof *h, PC_HSD_FTHIDDENPARTS)) {
                swap_s32((s32*) &h->x4);
            }
        }
    }
}

static void ft_data_to_native(FighterKind kind)
{
    ftData* d;

    if (kind < 0 || kind >= FTKIND_MAX) {
        return;
    }
    d = gFtDataList[kind];
    if (d == NULL) {
        return;
    }
    if (!pc_hsd_in_archive(d, sizeof *d)) {
        not_archive(kind, "ftData");
        return;
    }

    if (d->x0 != NULL && !pc_hsd_in_archive(d->x0, sizeof *d->x0)) {
        not_archive(kind, "attribute block");
    } else {
        attrs_to_native(d->x0);
    }

    moves_to_native(d->xC, ftData_Table_Unk0[kind].count, PC_HSD_FTMOVES);
    moves_to_native(d->x14, ftData_UnkIntPairs[kind].count, PC_HSD_FTDEMOMOVES);
    models_to_native(kind, d->x8);
    hurtboxes_to_native(kind, d->x30);
    dynamics_to_native(kind, d->x2C);
}

/* ftdata.c loads the archive and fills gFtDataList[kind] here, and does it
   once -- the body is guarded on gFtDataList[kind] == NULL. Converting after
   it returns means the relocation table has already turned every offset that
   is a pointer into a pointer, so what is left is exactly the fields that are
   not. */
void __real_ftData_8008572C(FighterKind kind);
void __wrap_ftData_8008572C(FighterKind kind)
{
    __real_ftData_8008572C(kind);
    ft_data_to_native(kind);
}
