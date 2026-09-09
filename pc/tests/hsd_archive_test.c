/* hsd_archive_test.c -- known-answer tests for whole-archive conversion.
 *
 * Build (32-bit, and note that -I extern/dolphin/include/libc must be left
 * out: its assert.h shadows the host's and defines no assert):
 *
 *   gcc -m32 -w -O0 -g -std=gnu17 -include tools/phase0/compat.h \
 *       -I pc/src -I src -I extern/dolphin/include \
 *       pc/tests/hsd_archive_test.c pc/src/pc_hsd_archive.c \
 *       pc/src/pc_hsd_endian.c -Wl,--large-address-aware -o t.exe
 *
 * What matters here is not that a header swaps. It is that converting is
 * decided correctly: exactly once per archive, never on one already in host
 * order, and never at all on something whose size does not agree.
 */
#include <windows.h>

#include "pc_hsd_archive.h"
#include "pc_hsd_endian.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void pc_sys_log(const char* s) { fputs(s, stderr); }

static void be(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

static unsigned host(const unsigned char* p)
{
    unsigned v;
    memcpy(&v, p, sizeof v);
    return v;
}

/* One archive: header, a body of `body_words` words, one relocation entry
   pointing at body word `reloc_word`, one public and one extern entry, then a
   symbol string. Laid out exactly as HSD_ArchiveParse walks it. */
#define BODY_WORDS 6
#define RELOC_WORD 2
#define ARCHIVE_BYTES (0x20 + BODY_WORDS * 4 + 4 + 8 + 8 + 8)

static void build(unsigned char* a)
{
    unsigned char* body = a + 0x20;
    unsigned char* table = body + BODY_WORDS * 4;
    int i;

    memset(a, 0, ARCHIVE_BYTES);
    be(a + 0x00, ARCHIVE_BYTES);   /* file_size */
    be(a + 0x04, BODY_WORDS * 4);  /* data_size */
    be(a + 0x08, 1);               /* nb_reloc  */
    be(a + 0x0C, 1);               /* nb_public */
    be(a + 0x10, 1);               /* nb_extern */
    memcpy(a + 0x14, "\x01\x02\x03\x04", 4); /* version[4]: bytes */
    be(a + 0x18, 0x11223344);
    be(a + 0x1C, 0x55667788);

    for (i = 0; i < BODY_WORDS; i++) {
        be(body + i * 4, 0xAA00 + (unsigned) i);
    }
    be(body + RELOC_WORD * 4, 0x00000010); /* the one pointer word */

    be(table, RELOC_WORD * 4); /* relocation entry: body offset */
    be(table + 4, 4);          /* public  {offset, symbol} */
    be(table + 8, 0);
    be(table + 12, 8); /* extern  {offset, symbol} */
    be(table + 16, 5);
    memcpy(table + 20, "abc\0xy\0", 7); /* symbol strings */
}

static void test_converts_once(unsigned char* a)
{
    unsigned char before[ARCHIVE_BYTES];
    unsigned char* body = a + 0x20;
    unsigned char* table = body + BODY_WORDS * 4;
    int i;

    build(a);
    memcpy(before, a, ARCHIVE_BYTES);
    pc_hsd_forget_range(a, ARCHIVE_BYTES);

    assert(pc_hsd_archive_convert(a, ARCHIVE_BYTES) == 1);

    assert(host(a + 0x00) == ARCHIVE_BYTES);
    assert(host(a + 0x04) == BODY_WORDS * 4);
    assert(host(a + 0x08) == 1 && host(a + 0x0C) == 1 && host(a + 0x10) == 1);
    /* version[4] is bytes and must survive untouched */
    assert(!memcmp(a + 0x14, "\x01\x02\x03\x04", 4));
    assert(host(a + 0x18) == 0x11223344 && host(a + 0x1C) == 0x55667788);

    /* Exactly the relocation-named body word is converted. Every other body
       word is untyped as far as anything here knows, and stays as it was. */
    assert(host(body + RELOC_WORD * 4) == 0x00000010);
    for (i = 0; i < BODY_WORDS; i++) {
        if (i != RELOC_WORD) {
            assert(!memcmp(body + i * 4, before + 0x20 + i * 4, 4));
        }
    }

    assert(host(table) == RELOC_WORD * 4);
    assert(host(table + 4) == 4 && host(table + 8) == 0);
    assert(host(table + 12) == 8 && host(table + 16) == 5);
    assert(!memcmp(table + 20, "abc\0xy\0", 7)); /* symbol bytes */

    /* Converting again must be a no-op, not a second swap. This is what lets
       a re-parsed buffer, or a relocated copy of one, pass through safely. */
    memcpy(before, a, ARCHIVE_BYTES);
    assert(pc_hsd_archive_convert(a, ARCHIVE_BYTES) == 0);
    assert(!memcmp(a, before, ARCHIVE_BYTES));

    puts("PASS: header, tables and only relocation-named body words convert, "
         "once");
}

static void test_rejects_without_touching(unsigned char* a)
{
    unsigned char before[ARCHIVE_BYTES];

    /* Wrong size in either order: not this archive. Nothing may be modified,
       because a partial conversion would then look converted. */
    build(a);
    memcpy(before, a, ARCHIVE_BYTES);
    assert(pc_hsd_archive_convert(a, ARCHIVE_BYTES + 4) == -1);
    assert(!memcmp(a, before, ARCHIVE_BYTES));

    /* A body larger than the file. Caught before the header is written. */
    build(a);
    be(a + 0x04, ARCHIVE_BYTES);
    memcpy(before, a, ARCHIVE_BYTES);
    assert(pc_hsd_archive_convert(a, ARCHIVE_BYTES) == -1);
    assert(!memcmp(a, before, ARCHIVE_BYTES));

    /* A relocation count that cannot fit after the body. */
    build(a);
    be(a + 0x08, 0x40000000);
    memcpy(before, a, ARCHIVE_BYTES);
    assert(pc_hsd_archive_convert(a, ARCHIVE_BYTES) == -1);
    assert(!memcmp(a, before, ARCHIVE_BYTES));

    /* Too short to hold a header at all. */
    assert(pc_hsd_archive_convert(a, 0x1F) == -1);
    assert(pc_hsd_archive_convert(NULL, ARCHIVE_BYTES) == -1);

    puts("PASS: a size that matches neither byte order leaves the archive "
         "untouched");
}

static void test_partial_header_is_not_mistaken_for_converted(unsigned char* a)
{
    /* The failure mode that forced conversion out of the DVD read. A read of
       the first chunk converted the header and stopped, because the body was
       not all there. The header then satisfies the "already host order" test
       while every table behind it is still big-endian -- so a later pass
       would skip it and the archive would be silently half-swapped.

       Here that state is built directly: header in host order, tables still
       big-endian. The test is not that the converter repairs it -- it cannot
       tell -- but that this state is unreachable now, because nothing
       converts a header on its own. Guard the property that makes it
       unreachable: a short buffer is refused outright. */
    unsigned char before[ARCHIVE_BYTES];

    build(a);
    memcpy(before, a, ARCHIVE_BYTES);
    /* The first 16 KB of a larger archive would arrive as a length far below
       the stated file_size. */
    assert(pc_hsd_archive_convert(a, 0x40) == -1);
    assert(!memcmp(a, before, ARCHIVE_BYTES));

    puts("PASS: a partial read is refused rather than half-converted");
}

static void test_marks_body_provenance(unsigned char* block)
{
    /* Conversion records where the body is, so the descriptor schemas in
       pc_hsd_swap.c can tell archive data from natively built structs. That
       must land on the body, not the header, and at the address the archive
       is actually parsed from. */
    unsigned char* body = block + 0x20;

    build(block);
    pc_hsd_forget_range(block, ARCHIVE_BYTES);
    assert(pc_hsd_archive_convert(block, ARCHIVE_BYTES) == 1);

    assert(pc_hsd_claim(body, 8, PC_HSD_POBJ));       /* body is archive data */
    assert(!pc_hsd_claim(body, 8, PC_HSD_POBJ));      /* and claimed once */
    assert(!pc_hsd_claim(block, 8, PC_HSD_POBJ));     /* header is not body */

    pc_hsd_forget_range(block, ARCHIVE_BYTES);
    assert(!pc_hsd_claim(body, 8, PC_HSD_POBJ));      /* a re-read forgets */

    puts("PASS: provenance is recorded on the body at its parsed address");
}

int main(void)
{
    /* Real archives are parsed out of the game's own RAM, and provenance is
       only tracked there, so the tests work in that window too. */
    unsigned char* block = VirtualAlloc((void*) 0x81000000u, 65536,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    assert(block == (void*) 0x81000000u);

    test_converts_once(block);
    test_rejects_without_touching(block);
    test_partial_header_is_not_mistaken_for_converted(block);
    test_marks_body_provenance(block);

    VirtualFree(block, 0, MEM_RELEASE);
    puts("all HSD archive tests passed");
    return 0;
}
