/* pc_hsd_particle.c -- byte order for the particle data banks.
 *
 * Found the way the schemas in pc_hsd_swap.c were: a real crash at a real
 * line. psInitDataBankLocate (particle.c:214) segfaulted entering
 * gm_Scene_Vs_OnEnter, dereferencing a command pointer of 0x88ccb500, with
 * version reading 16896 -- 0x4200, which is 0x42 with its bytes reversed, and
 * 0x42 is one of the four versions the function branches on.
 *
 * The archive around it is already converted. map_ptcl and map_texg are
 * reached through HSD_ArchiveGetPublicAddress, so the header, the tables and
 * every pointer the relocation table names are in host order by the time
 * anything here runs. What is left is the banks' own interior: counts,
 * offsets, and the fields of the structures they point at. The relocation
 * table does not name any of it -- the format has no way to say "this word is
 * a u16 and that one is a float" -- so it needs a schema written against what
 * particle.c actually reads.
 *
 * The one entry point is psInitDataBankLocate, and it has to run first,
 * because that function turns every offset in these banks into a pointer by
 * adding the bank's own address. Converting afterwards would swap the bytes
 * of a host pointer.
 *
 * Bounds come from the data itself, which is the whole risk: a count read out
 * of a bank decides how far the walk goes. So every derived address is
 * checked against pc_hsd_in_archive before it is followed. A bank that sends
 * the walk outside the archive it came from is reported and stops the process
 * rather than converting whatever happens to be next in memory.
 */
#include "pc_hsd_endian.h"
#include "pc_sys.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/psstructs.h>

static u16 swap16(u16 v)
{
    return (u16) ((v >> 8) | (v << 8));
}

static u32 swap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) |
           ((v << 24) & 0xFF000000u);
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

static void bank_failed(const char* what, u32 value)
{
    pc_sys_log("pc_hsd_particle: ");
    pc_sys_log(what);
    pc_sys_log(" (");
    log_uint(value);
    pc_sys_log(") leaves the archive; refusing to walk it\n");
    abort();
}

/* Follow one offset out of a bank, checking that what it names is really
   inside the archive the bank came from before anything dereferences it. */
static void* bank_at(void* base, u32 offset, size_t size, const char* what)
{
    unsigned char* p = (unsigned char*) base + offset;
    if (!pc_hsd_in_archive(p, size)) {
        bank_failed(what, offset);
    }
    return p;
}

/* One command list. particle.c reads all of this: the header fields here, and
   the byte program at cmdList[] which has no byte order to fix. */
static void swap_cmd_list(HSD_PSCmdList* c)
{
    if (!pc_hsd_claim(c, sizeof *c, PC_HSD_PSCMDLIST)) {
        return;
    }
    c->type = swap16(c->type);
    c->texGroup = swap16(c->texGroup);
    c->genLife = swap16(c->genLife);
    c->life = swap16(c->life);
    c->kind = swap32(c->kind);
    swap_f32(&c->grav);
    swap_f32(&c->fric);
    swap_f32(&c->vx);
    swap_f32(&c->vy);
    swap_f32(&c->vz);
    swap_f32(&c->radius);
    swap_f32(&c->angle);
    swap_f32(&c->random);
    swap_f32(&c->size);
    swap_f32(&c->param1);
    swap_f32(&c->param2);
    swap_f32(&c->param3);
}

/* How many texTable entries a group has. Textures first, then palettes, and
   only for the palette formats -- this mirrors psInitDataBankLocate's own
   `fmt != 8 && (fmt - 9) > 1` test, which with fmt unsigned admits exactly
   8, 9 and 10 (C4, C8, C14X2). */
static u32 tex_table_entries(const HSD_PSTexGroup* tg)
{
    u32 fmt = tg->fmt;
    if (fmt != 8 && (fmt - 9) > 1) {
        return tg->num;
    }
    if (tg->palflag & 1) {
        return tg->num + 1;
    }
    if (tg->palnum != 0) {
        return tg->num + tg->palnum;
    }
    return tg->num * 2;
}

static void swap_tex_group(HSD_PSTexGroup* tg)
{
    u32 entries, i;

    if (!pc_hsd_claim(tg, sizeof *tg, PC_HSD_PSTEXGROUP)) {
        return;
    }
    tg->num = swap32(tg->num);
    tg->fmt = swap32(tg->fmt);
    tg->tlutfmt = swap32(tg->tlutfmt);
    tg->width = swap32(tg->width);
    tg->height = swap32(tg->height);
    tg->palnum = swap16(tg->palnum);
    tg->palflag = swap16(tg->palflag);

    entries = tex_table_entries(tg);
    if (!pc_hsd_in_archive(tg->texTable, entries * sizeof tg->texTable[0])) {
        bank_failed("texture group entry count", entries);
    }
    for (i = 0; i < entries; i++) {
        u32 v;
        memcpy(&v, &tg->texTable[i], sizeof v);
        v = swap32(v);
        memcpy(&tg->texTable[i], &v, sizeof v);
    }
}

static void swap_form_group(HSD_PSFormGroup* fg)
{
    u32 i;

    if (!pc_hsd_claim(fg, sizeof *fg, PC_HSD_PSFORMGROUP)) {
        return;
    }
    fg->num = swap32(fg->num);
    if (!pc_hsd_in_archive(fg->formTable, fg->num * sizeof fg->formTable[0])) {
        bank_failed("form group entry count", fg->num);
    }
    for (i = 0; i < fg->num; i++) {
        u32 v;
        memcpy(&v, &fg->formTable[i], sizeof v);
        v = swap32(v);
        memcpy(&fg->formTable[i], &v, sizeof v);
    }
}

/* The command bank: a u16 version, then a layout that depends on it. Version 0
   keeps its count at word 1 and its pointer array at word 2; versions 0x40 to
   0x43 keep a second count at word 2 and start the array at word 3. Both are
   read back by psInitDataBankLoad, so the words have to end up host order
   whichever branch applies. */
static void swap_cmd_bank(void* cmd)
{
    u32* w = (u32*) cmd;
    u32 version, count, first, i;

    if (!pc_hsd_claim(cmd, 12, PC_HSD_PSCMDBANK)) {
        /* Either already converted, or not archive memory at all. The second
           case means something handed us a bank this never saw loaded, and
           psInitDataBankLoad will panic on its byte-reversed version -- so say
           so here, where the address is still known, rather than leaving the
           panic to be traced back by hand. */
        if (!pc_hsd_in_archive(cmd, 12)) {
            static int warned;
            if (!warned) {
                warned = 1;
                pc_sys_log("pc_hsd_particle: command bank at ");
                log_uint((u32) (uintptr_t) cmd);
                pc_sys_log(" carries no archive provenance; version reads ");
                log_uint(*(u16*) cmd);
                pc_sys_log("\n");
            }
        } else if (pc_hsd_kind_at(cmd) == PC_HSD_PSCMDBANK) {
            /* This port converted this bank already and is being handed it
               again -- which is normal, and normally means the version word
               reads 0 or 0x40..0x43 by now. When it does not, the bytes have
               changed since: something wrote over them without the DVD path
               marking the range as a fresh archive body, so the claim still
               says "converted" while the contents say otherwise.
               psInitDataBankLoad then panics on the version. That is the open
               "psInitDataBanks: unknown version" stop; say so here, where the
               address and the value are both still in hand. */
            u16 v = *(u16*) cmd;
            if (v != 0 && (v < 0x40 || v > 0x43)) {
                static int warned3;
                if (!warned3) {
                    warned3 = 1;
                    pc_sys_log("pc_hsd_particle: command bank at ");
                    log_uint((u32) (uintptr_t) cmd);
                    pc_sys_log(" is marked converted but its version now reads ");
                    log_uint(v);
                    pc_sys_log("; its bytes changed without the range being "
                               "re-marked\n");
                }
            }
        } else {
            /* Archive memory, but some other schema got here first -- which
               means this bank's version word has already been rewritten by a
               converter that thought it was something else, and
               psInitDataBankLoad will panic on it. Name that schema; the same
               shape shows up on stage param row 0 (see pc_stage_data.c) and
               the two are probably one bug. */
            static int warned2;
            if (!warned2) {
                warned2 = 1;
                pc_sys_log("pc_hsd_particle: command bank at ");
                log_uint((u32) (uintptr_t) cmd);
                pc_sys_log(" was already claimed by schema ");
                log_uint(pc_hsd_kind_at(cmd));
                pc_sys_log("; version reads ");
                log_uint(*(u16*) cmd);
                pc_sys_log("\n");
            }
        }
        return;
    }

    version = swap16(*(u16*) cmd);
    *(u16*) cmd = (u16) version;
    /* The u16 at offset 2 is not read by anything in particle.c and its width
       is therefore unknown; left exactly as it arrived. */

    if (version == 0) {
        w[1] = swap32(w[1]);
        count = w[1];
        first = 2;
    } else if (version >= 0x40 && version <= 0x43) {
        w[1] = swap32(w[1]);
        w[2] = swap32(w[2]);
        count = w[2];
        first = 3;
    } else {
        /* psInitDataBankLoad panics on anything else, and would do so with a
           value this function had already rewritten. Say it here instead,
           where the original bytes are still recoverable from the message. */
        pc_sys_log("pc_hsd_particle: command bank version ");
        log_uint(version);
        pc_sys_log(" is not one particle.c knows\n");
        abort();
    }

    if (!pc_hsd_in_archive(&w[first], (size_t) count * 4)) {
        bank_failed("command bank list count", count);
    }
    for (i = 0; i < count; i++) {
        w[first + i] = swap32(w[first + i]);
        if (w[first + i] != 0) {
            swap_cmd_list((HSD_PSCmdList*) bank_at(cmd, w[first + i],
                                                   sizeof(HSD_PSCmdList),
                                                   "command list offset"));
        }
    }
}

/* The texture bank: a group count, then that many offsets. */
static u32 swap_tex_bank(void* tex)
{
    u32* t = (u32*) tex;
    u32 groups, k;

    if (!pc_hsd_claim(tex, 4, PC_HSD_PSTEXBANK)) {
        /* Converted already, or built natively rather than loaded: either way
           the count is readable as it stands, and the form bank below still
           needs it. */
        return t[0];
    }
    t[0] = swap32(t[0]);
    groups = t[0];
    if (!pc_hsd_in_archive(&t[1], (size_t) groups * 4)) {
        bank_failed("texture bank group count", groups);
    }
    for (k = 1; k <= groups; k++) {
        t[k] = swap32(t[k]);
        if (t[k] != 0) {
            swap_tex_group((HSD_PSTexGroup*) bank_at(tex, t[k],
                                                     sizeof(HSD_PSTexGroup),
                                                     "texture group offset"));
        }
    }
    return groups;
}

/* The form bank, when there is one: the same shape, and psInitDataBankLoad
   requires its count to equal the texture bank's. */
static void swap_form_bank(void* form, u32 groups)
{
    u32* f = (u32*) form;
    u32 i;

    if (!pc_hsd_claim(form, 4, PC_HSD_PSFORMBANK)) {
        return;
    }
    f[0] = swap32(f[0]);
    if (!pc_hsd_in_archive(&f[1], (size_t) groups * 4)) {
        bank_failed("form bank group count", groups);
    }
    for (i = 1; i <= groups; i++) {
        f[i] = swap32(f[i]);
        if (f[i] != 0) {
            swap_form_group((HSD_PSFormGroup*) bank_at(form, f[i],
                                                       sizeof(HSD_PSFormGroup),
                                                       "form group offset"));
        }
    }
}

void pc_hsd_particle_banks_to_native(void* cmd, void* tex, void* form)
{
    u32 groups = 0;
    if (tex != NULL) {
        groups = swap_tex_bank(tex);
    }
    if (cmd != NULL) {
        swap_cmd_bank(cmd);
    }
    if (form != NULL) {
        swap_form_bank(form, groups);
    }
}

/* psInitDataBankLoad is the other door into the same banks, and the one the
 * open "psInitDataBanks: unknown version" stop comes through. It is reached
 * from grdatfiles.c's preloaded-archive branch, which calls Load WITHOUT
 * Locate, so the wrapper above never runs for it.
 *
 * The port already converts these banks where the public symbol is handed out
 * (pc_stage_data.c's dispatch on "map_ptcl" and "map_texg"), which should
 * cover this branch. Two runs in three still panic here, and the diagnostic
 * that was supposed to explain it turned out to be about a different bank
 * entirely -- it fires once, globally, and the address it named was hundreds
 * of bytes away from the one that panicked. So ask at the panic's own door
 * instead: what does this port know about the exact bank about to be read?
 *
 * The mark distinguishes the three possibilities that need completely
 * different fixes, and nothing so far has established which is happening:
 *
 *   0     -- not tracked memory at all, so no schema could have run
 *   0x80  -- a fresh archive body nothing claimed, so conversion never
 *            reached it and the version is simply still big-endian
 *   other -- claimed, and by which schema
 */
void __real_psInitDataBankLoad(int bank, const int* cmdBank,
                               const int* texBank, const u32* ref,
                               const int* formBank);
void __wrap_psInitDataBankLoad(int bank, const int* cmdBank,
                               const int* texBank, const u32* ref,
                               const int* formBank)
{
    if (cmdBank != NULL) {
        u16 v = *(const u16*) cmdBank;
        if (v != 0 && (v < 0x40 || v > 0x43)) {
            pc_sys_log("pc_hsd_particle: psInitDataBankLoad was handed a "
                       "command bank at ");
            log_uint((u32) (uintptr_t) cmdBank);
            pc_sys_log(" whose version reads ");
            log_uint(v);
            pc_sys_log("; this port's mark on it is ");
            log_uint(pc_hsd_kind_at(cmdBank));
            pc_sys_log(" and byte-reversing the version would give ");
            log_uint(swap16(v));
            pc_sys_log("\n");
        }
    }
    __real_psInitDataBankLoad(bank, cmdBank, texBank, ref, formBank);
}

void __real_psInitDataBankLocate(HSD_Archive* cmdBank, HSD_Archive* texBank,
                                 int* formBank);
void __wrap_psInitDataBankLocate(HSD_Archive* cmdBank, HSD_Archive* texBank,
                                 int* formBank)
{
    /* Before, not after: __real_ replaces every offset in these banks with a
       pointer by adding the bank's own address to it. */
    pc_hsd_particle_banks_to_native(cmdBank, texBank, formBank);
    __real_psInitDataBankLocate(cmdBank, texBank, formBank);
}
