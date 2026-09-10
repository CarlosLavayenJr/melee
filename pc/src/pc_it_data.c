/* pc_it_data.c -- byte order for ItCo.dat, the tables every item shares.
 *
 * Same family as pc_ft_data.c one file over: Melee's own data, pulled out of
 * an archive by name, with the pointers already host order because the
 * relocation table named them and everything behind them still big-endian.
 *
 * Found where the others were, at a line that failed. The scripted route
 * reached a stage that loads completely -- Onett -- and stopped in Onett's own
 * setup, at gronett.c:480, passing a NULL item to grMaterial_801C8E08. The
 * cars are stage-hazard items, and their spawn had returned NULL. Breaking on
 * each of the four ways Item_8026862C can do that named the one:
 *
 *   ITEMNULL refused by Item_8026784C: hold=3 kind=160
 *
 * hold_kind 3 is the It_PKind_Random branch, which refuses when
 * Item_804A0C64.x58 >= .x5C -- a live count against a limit. The count is
 * zeroed at init; the limit is copied straight out of ItemCommonData, which
 * nothing had converted. A byte-reversed limit is usually enormous and would
 * wave every spawn through, but a real value with bit 7 set reverses into a
 * negative s32, and then a live count of zero is already "at the limit" and
 * every spawn is refused. Which is what a stage with no hazards looks like,
 * followed by a crash on the first one the stage code expects to exist.
 *
 * Only ItemCommonData is converted here. itPublicData also names three
 * Article tables and two more blocks, and those are NOT converted -- they will
 * announce themselves the same way.
 */
#include "pc_hsd_endian.h"
#include "pc_sys.h"

#include <stddef.h>
#include <string.h>

#include <melee/it/types.h>
#include <melee/it/it_3F14.h>

static u32 swap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
           ((v << 24) & 0xFF000000u);
}

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

/* ItemCommonData is four-byte members from +0x000 to +0x160 with one
   exception: x48_byte is a single byte in a four-byte slot, so the run stops
   before it and restarts at the float after it. The two four-byte runs the
   decomp calls "filler" are swapped with everything else -- nothing reads
   them, so either choice is inert, and leaving a hole in the middle of an
   otherwise uniform block is the more surprising of the two. */
void pc_it_common_data_to_native(void* p)
{
    it_804D6D20_t* t = (it_804D6D20_t*) p;
    struct ItemCommonData* c;
    size_t byte_at, after;

    if (t == NULL) {
        return;
    }
    if (!pc_hsd_in_archive(t, sizeof *t)) {
        pc_sys_log("pc_it_data: itPublicData is not archive memory, so the "
                   "shared item tables stay byte-reversed\n");
        return;
    }
    c = t->x0;
    if (c == NULL || !pc_hsd_claim(c, sizeof *c, PC_HSD_ITCOMMON)) {
        return;
    }
    byte_at = offsetof(struct ItemCommonData, x48_byte);
    after = offsetof(struct ItemCommonData, x4C_float);
    swap_words(c, byte_at);
    swap_words((unsigned char*) c + after, sizeof *c - after);
}
