/* pc_hsd_archive.c -- HSD archive byte order, converted where the archive is
 * whole and final.
 *
 * This used to happen in pc_dvd.c, inside the DVD read itself, on the
 * assumption that a .dat arrives in one read at the address it will be used
 * from. That holds for small files and fails three ways for large ones, all
 * of which were live:
 *
 *   1. A big archive is read in chunks. Only the first chunk sees `rel == 0`,
 *      so only it was converted -- and since its length is far short of the
 *      archive's, the body-size check tripped and the relocation, public and
 *      extern tables were never converted at all. The recurring
 *      "pc_dvd: truncated HSD archive body" line was this, every time.
 *
 *   2. devcom.c does not read into final memory. It stages DVD chunks through
 *      two 16 KB relay buffers, into ARAM, and only later copies them to where
 *      the archive will live. Conversion at read time therefore ran against a
 *      relay buffer -- and pc_hsd_archive_body(), told that a body lived in
 *      static host storage outside the game's RAM window, correctly refused.
 *      That is the abort seen entering gm_Scene_Title_OnEnter. The buffer
 *      address was never the bug; the timing was.
 *
 *   3. Provenance recorded against a staging buffer does not follow the bytes
 *      to their destination, so descriptor schemas would not have applied even
 *      if the address had been accepted.
 *
 * The archive is whole, contiguous and final in exactly one place: when a
 * consumer parses it. HSD_ArchiveParse and lbArchiveRelocate are those places
 * -- every .dat consumer in the game reaches one of them (lbArchive_Initialize
 * DAT, efAsync_OnLoad and grDatFiles_801C5FC0 all funnel into the first).
 * Both receive the base address and the true file size, and both already
 * compare the archive's own file_size field against it as a byte-order check.
 *
 * That same comparison is what makes converting here safe to do exactly once,
 * with no extra bookkeeping: an archive whose stated file_size already equals
 * the caller's is in host order and is left alone. Re-parsing a converted
 * buffer, or relocating a copy of one (ftdata.c does this to fighter animation
 * data), is therefore a no-op rather than a second swap. It also means the
 * DVD-time conversion had to go rather than stay as a fast path: a partially
 * converted header passes this test while its tables are still big-endian.
 */
#include "pc_hsd_archive.h"

#include "pc_hsd_endian.h"
#include "pc_sys.h"

#include <stdlib.h>
#include <string.h>

typedef unsigned int pc_u32;

static pc_u32 be32(const unsigned char* p)
{
    return (pc_u32) p[0] << 24 | (pc_u32) p[1] << 16 | (pc_u32) p[2] << 8 |
           (pc_u32) p[3];
}

/* Read big-endian, write host order. On a big-endian host this is the
   identity, which is what it should be. */
static void swap_words(unsigned char* p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        pc_u32 v = be32(p + i * 4);
        memcpy(p + i * 4, &v, sizeof v);
    }
}

static void log_uint(pc_u32 v)
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

int pc_hsd_archive_convert(void* src, size_t file_size)
{
    unsigned char* addr = (unsigned char*) src;
    pc_u32 stated, data_size, nb_reloc, nb_public, nb_extern, i;
    unsigned char *body, *table;
    size_t room;

    if (addr == NULL || file_size < 0x20) {
        return -1;
    }

    memcpy(&stated, addr, sizeof stated);
    if (stated == (pc_u32) file_size) {
        return 0; /* already host order -- see the header comment */
    }
    if (be32(addr) != (pc_u32) file_size) {
        return -1; /* neither order: not an archive of this size */
    }

    data_size = be32(addr + 0x04);
    nb_reloc = be32(addr + 0x08);
    nb_public = be32(addr + 0x0C);
    nb_extern = be32(addr + 0x10);

    /* Validate the entire layout before modifying a single byte. A half
       converted archive is worse than an unconverted one: its file_size field
       would then satisfy the test above and every later pass would skip it. */
    room = file_size - 0x20;
    if (data_size > room) {
        return -1;
    }
    room -= data_size;
    if (nb_reloc > room / 4) {
        return -1;
    }
    room -= (size_t) nb_reloc * 4;
    if (nb_public > room / 8) {
        return -1;
    }
    room -= (size_t) nb_public * 8;
    if (nb_extern > room / 8) {
        return -1;
    }

    /* file_size, data_size, nb_reloc, nb_public, nb_extern are words 0-4.
       version[4] at 0x14 is bytes and stays. pad[2] at 0x18 is words 6-7. */
    swap_words(addr, 5);
    swap_words(addr + 0x18, 2);

    body = addr + 0x20;
    table = body + data_size;

    if (nb_reloc != 0) {
        swap_words(table, nb_reloc);
        for (i = 0; i < nb_reloc; i++) {
            pc_u32 offset;
            memcpy(&offset, table + i * 4, sizeof offset);
            /* Each entry names one body word that Locate() will add the load
               address to. Those words, and no others in the body, are known
               to be pointers -- the format itself vouches for them, which is
               why swapping them is safe and guessing at the rest is not. */
            if (data_size < 4 || offset > data_size - 4) {
                /* Locate() would write outside the body. Say where rather
                   than let it corrupt whatever follows. */
                pc_sys_log("pc_hsd_archive: relocation offset ");
                log_uint(offset);
                pc_sys_log(" outside a body of ");
                log_uint(data_size);
                pc_sys_log(" bytes\n");
                abort();
            }
            swap_words(body + offset, 1);
        }
        table += (size_t) nb_reloc * 4;
    }
    if (nb_public != 0) {
        swap_words(table, (size_t) nb_public * 2); /* {offset, symbol} pairs */
        table += (size_t) nb_public * 8;
    }
    if (nb_extern != 0) {
        swap_words(table, (size_t) nb_extern * 2);
        table += (size_t) nb_extern * 8;
    }
    /* What remains is the symbol table: NUL-terminated names reached by the
       offsets just converted. Bytes, left alone. */

    pc_hsd_archive_body(body, data_size);
    return 1;
}
