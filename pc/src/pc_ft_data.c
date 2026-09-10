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
