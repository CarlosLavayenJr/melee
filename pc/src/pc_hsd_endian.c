#include "pc_hsd_endian.h"
#include "pc_sys.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RAM_BASE ((uintptr_t)0x80000000u)
#define RAM_BYTES (24u << 20)
/* One byte per aligned word. 0=not archive, 0x80=unclaimed archive word,
   1..127=schema at this address. CPU vertex/texture payload stays untouched. */
static unsigned char words[RAM_BYTES / 4];
static void log_hex(unsigned value)
{
    char out[11] = "0x00000000";
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; ++i) out[9-i] = digits[(value >> (4*i)) & 15];
    pc_sys_log(out);
}

void pc_hsd_forget_range(void* data, size_t size)
{
    uintptr_t p = (uintptr_t)data, start, end;
    if (!size || p >= RAM_BASE + RAM_BYTES || size > UINTPTR_MAX - p || p + size <= RAM_BASE) return;
    start = p < RAM_BASE ? 0 : p - RAM_BASE;
    end = p + size > RAM_BASE + RAM_BYTES ? RAM_BYTES : p + size - RAM_BASE;
    memset(words + start / 4, 0, (end + 3) / 4 - start / 4);
}

void pc_hsd_archive_body(void* data, size_t size)
{
    uintptr_t p = (uintptr_t)data;
    size_t i, first, n, claimed = 0;
    if (!size) return;
    if (p < RAM_BASE || p >= RAM_BASE + RAM_BYTES || (p & 3) || size > RAM_BASE + RAM_BYTES - p) {
        pc_sys_log("pc_hsd_endian: archive body outside mapped RAM\n"); abort();
    }
    /* Marking a range fresh forgets that its objects were converted, which is
       right when a read has just refilled it with big-endian bytes and wrong
       when nothing has. In the second case every schema converts a second
       time and swaps its own work back -- which is exactly the shape of the
       two open stops: one word that reads as though it were never converted,
       inside an archive whose other words are fine. Say when a range that
       still holds claimed words is being marked fresh, so the two cases can
       be told apart in a log. */
    first = (p - RAM_BASE) / 4;
    n = size / 4;
    for (i = 0; i < n; i++) {
        unsigned char w = words[first + i];
        if (w != 0 && w != 0x80) {
            claimed++;
        }
    }
    if (claimed != 0) {
        static int warned;
        if (warned < 4) {
            warned++;
            pc_sys_log("pc_hsd_endian: archive body at ");
            log_hex((unsigned) p);
            pc_sys_log(" re-marked fresh over ");
            log_hex((unsigned) claimed);
            pc_sys_log(" already-claimed words\n");
        }
    }
    memset(words + first, 0x80, n);
}

int pc_hsd_in_archive(const void* data, size_t size)
{
    uintptr_t p = (uintptr_t)data;
    size_t i, first, last;
    if (!data || !size || p < RAM_BASE || p >= RAM_BASE + RAM_BYTES) return 0;
    if (size > RAM_BASE + RAM_BYTES - p) return 0;
    first = (p - RAM_BASE) / 4;
    last = (p - RAM_BASE + size - 1) / 4;
    for (i = first; i <= last; i++) if (!words[i]) return 0;
    return 1;
}

unsigned pc_hsd_kind_at(const void* p)
{
    uintptr_t a = (uintptr_t) p;
    if (!p || a < RAM_BASE || a >= RAM_BASE + RAM_BYTES) return 0;
    return words[(a - RAM_BASE) / 4];
}

int pc_hsd_claim(void* data, size_t size, unsigned kind)
{
    uintptr_t p = (uintptr_t)data;
    size_t first, last;
    if (!data || p < RAM_BASE || p >= RAM_BASE + RAM_BYTES) return 0;
    first = (p - RAM_BASE) / 4;
    if (!words[first]) return 0; /* Descriptor constructed natively. */
    if (!size || (p & 3) || kind == 0 || kind >= 128 || size > RAM_BASE + RAM_BYTES - p) goto invalid;
    last = (p - RAM_BASE + size - 1) / 4;
    if (!words[last]) goto invalid;
    if (words[first] == kind) return 0;
    if (words[first] != 0x80) goto invalid;
    words[first] = (unsigned char)kind;
    return 1;
invalid:
    pc_sys_log("pc_hsd_endian: descriptor outside archive or conflicting schema\n");
    pc_sys_log("  address="); log_hex((unsigned)p);
    pc_sys_log(" requested="); log_hex(kind);
    pc_sys_log(" previous="); log_hex(words[first]); pc_sys_log("\n");
    abort();
}

void pc_hsd_mobj_flags_to_native(void* flags)
{
    uint32_t value;
    if (!pc_hsd_claim(flags, sizeof value, PC_HSD_MOBJ_FLAGS)) return;
    memcpy(&value, flags, sizeof value);
    value = (value >> 24) | ((value >> 8) & 0xff00u) |
            ((value << 8) & 0xff0000u) | (value << 24);
    memcpy(flags, &value, sizeof value);
}
