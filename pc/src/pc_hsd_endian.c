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
    if (!size) return;
    if (p < RAM_BASE || p >= RAM_BASE + RAM_BYTES || (p & 3) || size > RAM_BASE + RAM_BYTES - p) {
        pc_sys_log("pc_hsd_endian: archive body outside mapped RAM\n"); abort();
    }
    memset(words + (p - RAM_BASE) / 4, 0x80, size / 4);
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
    abort();
}
