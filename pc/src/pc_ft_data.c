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

#include <melee/lb/types.h>
#include <melee/ft/kinds/ftKirby/types.h>
#include <melee/ft/kinds/ftDonkey/types.h>
#include <melee/ft/kinds/ftDrMario/types.h>
#include <melee/ft/kinds/ftKoopa/types.h>
#include <melee/ft/kinds/ftLuigi/types.h>
#include <melee/ft/kinds/ftPichu/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <melee/ft/kinds/ftSeak/types.h>
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <melee/ft/kinds/ftZelda/types.h>
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

static void swap_s16(s16* p)
{
    u16 v = (u16) *p;
    *p = (s16) ((v >> 8) | (v << 8));
}

static void swap_f32(f32* p)
{
    u32 v;
    memcpy(&v, p, sizeof v);
    v = swap32(v);
    memcpy(p, &v, sizeof v);
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

/* ext_attr is per character and the decomp states only some of the
   layouts. Saying which characters are running on reversed attributes is
   the whole point: nothing downstream will, because floats do not fault. */
static void no_layout(FighterKind kind)
{
    static u8 said[FTKIND_MAX];
    if (kind >= FTKIND_MAX || said[kind]) {
        return;
    }
    said[kind] = 1;
    pc_sys_log("pc_ft_data: fighter ");
    log_uint((u32) kind);
    pc_sys_log(" has no attribute-block layout here, so its character-"
               "specific attributes stay byte-reversed\n");
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

/* ftData::x40 and ::x50 -- the item-pickup offsets and the Vec2 at x2C4,
 * both copied wholesale into the Fighter by ftCo_800D105C when a fighter's
 * attributes are initialized:
 *
 *     fp->x294_itPickup = *fp->ft_data->x40;
 *     fp->x2C4          = *fp->ft_data->x50;
 *
 * These are converted for the same reason grGroundParam is, and it is the
 * opposite of the reason for everything else in this file: they would never
 * announce themselves. Every field is a float, floats do not fault, and a
 * byte-reversed one is a denormal or an astronomically large number that
 * makes a fighter hold items in the wrong place rather than crash. itPickup
 * is three Vec4s and x50 is a single Vec2, so both runs are uniform.
 */
static void pickup_to_native(itPickup* p)
{
    if (p == NULL || !pc_hsd_claim(p, sizeof *p, PC_HSD_FTPICKUP)) {
        return;
    }
    swap_words(p, sizeof *p);
}

static void ft_vec2_to_native(Vec2* v)
{
    if (v == NULL || !pc_hsd_claim(v, sizeof *v, PC_HSD_FTVEC2)) {
        return;
    }
    swap_words(v, sizeof *v);
}

/* Three more small blocks hanging off ftData, all of them four-byte fields
 * throughout and all of them reached on the way into a match.
 *
 * x34 is `{ Fighter_Part x0; float scale; }` and it announced itself the same
 * way x44 did -- ft_07C1.c:43 segfaulted on
 * `hit->jobj = fp->parts[x34->x0].joint`, a byte-reversed bone index used to
 * subscript the part table.
 *
 * x38 is an array of `{ int x0; Vec3 x4; float x10; }` that ft_8007C630 walks
 * and subscripts `fp->parts` with in exactly the same way, so it is the same
 * stop one function over. Its length is not in the file, but it does not need
 * to be: the loop is bounded by `ARRAY_SIZE(fp->x1614)`, a compile-time 2.
 *
 * x3C is two Vec3s of camera bounds, read every frame by ftCamera_80076018.
 * That one is the grGroundParam case again -- all floats, so it would have
 * framed the match wrongly rather than crashed.
 */
static void thrown_hitbox_to_native(struct ftData_x34* x)
{
    if (x == NULL || !pc_hsd_claim(x, sizeof *x, PC_HSD_FTX34)) {
        return;
    }
    swap_words(x, sizeof *x);
}

#define FTDATA_X38_COUNT 2

static void ft_x38_to_native(FighterKind kind, struct ftData_x38* x)
{
    size_t bytes = FTDATA_X38_COUNT * sizeof *x;

    if (x == NULL) {
        return;
    }
    if (!pc_hsd_in_archive(x, bytes)) {
        not_archive(kind, "x38 bone table");
        return;
    }
    if (!pc_hsd_claim(x, bytes, PC_HSD_FTX38)) {
        return;
    }
    swap_words(x, bytes);
}

static void ft_camera_to_native(struct UnkFloat6_Camera* c)
{
    if (c == NULL || !pc_hsd_claim(c, sizeof *c, PC_HSD_FTCAMERA)) {
        return;
    }
    swap_words(c, sizeof *c);
}

/* ftData::x44 -- the six bones the ECB is built from, and the four floats
 * that size it and the ledge snap.
 *
 * Found at `lb_00B0.c:102`, `return jobj->parent` with `jobj = 0x1a1a1a1a`,
 * reached from `Fighter_Create` through `mpColl_LoadECB_JObj`. `ft_80081B38`
 * passes `bones[x44->unk0].joint` and five more like it straight into
 * `mpColl_SetECBSource_JObj`, so a byte-reversed s16 index reads a joint
 * pointer out of whatever lies past the end of the bone table.
 *
 * Worth noting where this sits: unlike almost every stop before it, this one
 * is not stage-specific. It is on the path every fighter takes into every
 * match, so nothing else in a match could have run until it was fixed.
 */
static void ecb_source_to_native(ftData_x44_t* e)
{
    if (e == NULL || !pc_hsd_claim(e, sizeof *e, PC_HSD_FTECBSOURCE)) {
        return;
    }
    swap_s16(&e->unk0);
    swap_s16(&e->unk2);
    swap_s16(&e->unk4);
    swap_s16(&e->unk6);
    swap_s16(&e->unk8);
    swap_s16(&e->unkA);
    swap_f32(&e->unkC);
    swap_f32(&e->ledge_snap_x);
    swap_f32(&e->ledge_snap_y);
    swap_f32(&e->ledge_snap_height);
}

static void dynamics_to_native(FighterKind kind, struct ftDynamics* d)
{
    if (d == NULL || !pc_hsd_claim(d, sizeof *d, PC_HSD_FTDYNAMICS)) {
        return;
    }
    swap_s32((s32*) &d->dynamicsNum);
    swap_s32((s32*) &d->x4);

    /* The bone table this points at was left alone until ftdynamics.c:96
       segfaulted in ftCo_8009CF84 on
       `lb_8000FD48(fp->parts[bones->bone_id].joint, ..., bones->dyn_desc.count)`.
       Both of those are big-endian in the file: bone_id indexes the fighter's
       part table and count drives the same jobj-chain walk that the stage
       flags tripped over, so this is the fighter-side instance of exactly the
       stop dynamicsdata_* produced on Hyrule Castle. The descriptor itself is
       shared code -- see pc_dynamics_desc_to_native in pc_stage_data.c. */
    if (d->ftDynamicBones != NULL && d->dynamicsNum > 0) {
        size_t bytes = (size_t) d->dynamicsNum *
                       sizeof d->ftDynamicBones->array[0];
        if (!pc_hsd_in_archive(d->ftDynamicBones, bytes)) {
            not_archive(kind, "bone dynamics table");
        } else {
            int i;
            for (i = 0; i < d->dynamicsNum; i++) {
                BoneDynamicsDesc* b = &d->ftDynamicBones->array[i];
                if (pc_hsd_claim(b, sizeof *b, PC_HSD_FTBONEDYN)) {
                    swap_s32((s32*) &b->bone_id);
                }
                pc_dynamics_desc_to_native(&b->dyn_desc);
            }
        }
    }

    if (d->x8 == NULL || d->x4 <= 0) {
        return;
    }
    if (!pc_hsd_in_archive(d->x8, (size_t) d->x4 * sizeof *d->x8)) {
        not_archive(kind, "dynamics table");
        return;
    }
    swap_words(d->x8, (size_t) d->x4 * sizeof *d->x8);
}

/* ftData+0x04, the character-specific attribute block. There is one layout per
   character and the file does not say how big it is, so this cannot be done
   generically: each one needs its struct, and the decomp only states ten of
   them. Converting a block whose layout is a guess would be worse than leaving
   it -- these are floats a fighter runs on, and a wrong one is a match that
   plays like nothing rather than a crash -- so anything not listed here is
   reported and left alone.
 *
   Kirby is done because it is what stopped the route: ftkirby.c sets
   fp->x2D0 = fp->dat_attrs, and ftCo_800D0CBC (ftchangeparam.c:138) then loops
   over x14[] with x28 as the count. Byte-reversed that count is enormous and
   the loop segfaults. Jigglypuff does the same thing and has no struct in the
   decomp, so it will stop there until one exists.
 *
   Two members are not four bytes wide and are stepped around: the s16 at
   jumpaerial_unk, and the u8 at the end of the trailing ReflectDesc. */
/* An attribute block, described as a size and a list of the members inside it
 * that are NOT four-byte fields and so have to be stepped over.
 *
 * The size cannot come from the file -- nothing records it -- so it comes from
 * the decomp's own struct for that character, which is the only place it is
 * written down. Everything outside a hole is reversed as words, which is
 * right because every remaining member is a float, an int, an ItemKind, a
 * Vec2/3/4 or an ftCollisionBox, and all of those reverse the same way.
 *
 * Two nested descriptors account for most of the holes in practice.
 * AbsorbDesc is an int, a Vec3 and a float -- uniformly four-byte, so it
 * needs no hole at all. ReflectDesc is 0x20 bytes of words followed by the u8
 * x20_behavior, so its hole is the single word that byte sits in: reversing
 * that word would move the byte somewhere else in it.
 */
struct attr_hole {
    size_t at;
    size_t len;
};

/* The word a ReflectDesc's u8 behaviour field sits in, relative to the block
   that contains the descriptor. */
#define REFLECT_HOLE(at_offset)                                               \
    { (at_offset) + offsetof(struct ReflectDesc, x20_behavior), 4 }

static void ext_attr_words(void* e, size_t size, const struct attr_hole* holes,
                           int nholes)
{
    unsigned char* p = (unsigned char*) e;
    size_t pos = 0;
    int i;

    if (!pc_hsd_claim(e, size, PC_HSD_FTEXTATTR)) {
        return;
    }
    for (i = 0; i < nholes; i++) {
        swap_words(p + pos, holes[i].at - pos);
        pos = holes[i].at + holes[i].len;
    }
    swap_words(p + pos, size - pos);
}

static void uniform_ext_attr(void* e, size_t size)
{
    ext_attr_words(e, size, NULL, 0);
}

static void ext_attr_to_native(FighterKind kind, void* e)
{
    if (e == NULL) {
        return;
    }
    switch (kind) {
    case FTKIND_KIRBY: {
        struct ftKb_DatAttrs* a = e;
        size_t s16_at = offsetof(struct ftKb_DatAttrs, jumpaerial_unk);
        size_t after_s16 =
            offsetof(struct ftKb_DatAttrs, specialn_x_offset_inhaled);
        size_t u8_at = offsetof(struct ftKb_DatAttrs, specialn_zd_reflectdesc) +
                       offsetof(struct ReflectDesc, x20_behavior);
        if (!pc_hsd_claim(a, sizeof *a, PC_HSD_FTEXTATTR)) {
            return;
        }
        swap_words(a, s16_at);
        {
            u16 v;
            memcpy(&v, (unsigned char*) a + s16_at, sizeof v);
            v = (u16) ((v >> 8) | (v << 8));
            memcpy((unsigned char*) a + s16_at, &v, sizeof v);
        }
        swap_words((unsigned char*) a + after_s16, u8_at - after_s16);
        break;
    }
    /* Characters whose whole attribute block is four-byte fields. Each of
       these was checked member by member against its own `ft<Name>Attributes`
       in src/melee/ft/kinds -- floats, ints, ItemKinds, Vec2/3/4 and
       ftCollisionBox, and nothing narrower -- so the block reverses
       uniformly and there is no offset to get wrong.

       Dr Mario is in this list although its struct is not literally uniform:
       its two odd members are `u8 pad_x0[4]` and `u8 pad_x8[4]`, four-byte
       padding that nothing reads, so reversing them with everything else is
       inert. That is the same call ItemCommonData's "filler" words got.

       This list is deliberately short. Ice Climbers, Jigglypuff and Yoshi
       have unnamed byte runs, Ness and Mr Game & Watch embed an AbsorbDesc or
       ReflectDesc with a u8 behaviour field in it, and Mewtwo has both nested
       structs and a bitfield -- which on this target is a second hazard on
       top of byte order. Those need reading one at a time, and until then
       no_layout says so by name rather than converting them wrongly. */
    case FTKIND_DONKEY:
        uniform_ext_attr(e, sizeof(ftDonkeyAttributes));
        break;
    case FTKIND_KOOPA:
        uniform_ext_attr(e, sizeof(ftKoopaAttributes));
        break;
    case FTKIND_SEAK:
        uniform_ext_attr(e, sizeof(ftSeakAttributes));
        break;
    case FTKIND_PIKACHU:
        uniform_ext_attr(e, sizeof(ftPikachuAttributes));
        break;
    case FTKIND_LUIGI:
        uniform_ext_attr(e, sizeof(ftLuigiAttributes));
        break;
    case FTKIND_DRMARIO:
        uniform_ext_attr(e, sizeof(ftDrMarioAttributes));
        break;
    case FTKIND_PICHU:
        uniform_ext_attr(e, sizeof(ftPichuAttributes));
        break;
    case FTKIND_PEACH:
        /* Its one nested descriptor is an AbsorbDesc, which is uniform. */
        uniform_ext_attr(e, sizeof(ftPe_DatAttrs));
        break;
    case FTKIND_GAMEWATCH:
        uniform_ext_attr(e, sizeof(ftGameWatchAttributes));
        break;
    case FTKIND_CAPTAIN:
    case FTKIND_GANON:
        /* Ganondorf is Captain Falcon's clone and shares the layout. */
        uniform_ext_attr(e, sizeof(ftCaptain_DatAttrs));
        break;

    /* One ReflectDesc each, so one hole each. */
    case FTKIND_MARIO: {
        static const struct attr_hole h[] = {
            REFLECT_HOLE(offsetof(ftMario_DatAttrs, cape_reflection))
        };
        ext_attr_words(e, sizeof(ftMario_DatAttrs), h, 1);
        break;
    }
    case FTKIND_FOX:
    case FTKIND_FALCO: {
        static const struct attr_hole h[] = { REFLECT_HOLE(
            offsetof(ftFox_DatAttrs, xB0_FOX_REFLECTOR_REFLECTION)) };
        ext_attr_words(e, sizeof(ftFox_DatAttrs), h, 1);
        break;
    }
    case FTKIND_ZELDA: {
        static const struct attr_hole h[] = {
            REFLECT_HOLE(offsetof(ftZelda_DatAttrs, x84))
        };
        ext_attr_words(e, sizeof(ftZelda_DatAttrs), h, 1);
        break;
    }
    case FTKIND_NESS: {
        /* An AbsorbDesc as well, but that one is uniform and needs no hole. */
        static const struct attr_hole h[] = {
            REFLECT_HOLE(offsetof(ftNessAttributes, xB8_BASEBALL_BAT))
        };
        ext_attr_words(e, sizeof(ftNessAttributes), h, 1);
        break;
    }
    default:
        no_layout(kind);
        break;
    }
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
    thrown_hitbox_to_native(d->x34);
    ft_x38_to_native(kind, d->x38);
    ft_camera_to_native(d->x3C);
    pickup_to_native(d->x40);
    ft_vec2_to_native(d->x50);
    ecb_source_to_native(d->x44);
    ext_attr_to_native(kind, d->ext_attr);
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
