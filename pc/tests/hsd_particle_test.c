/* hsd_particle_test.c -- known-answer tests for the particle bank schema.
 *
 *   gcc -m32 -w -O0 -g -std=gnu17 -Wno-error=implicit-function-declaration \
 *       -include tools/phase0/compat.h -I pc/src -I src \
 *       -I extern/dolphin/include -DVERSION_GALE01 -DBUILD_VERSION=0 \
 *       pc/tests/hsd_particle_test.c pc/src/pc_hsd_particle.c \
 *       pc/src/pc_hsd_endian.c -Wl,--large-address-aware -o t.exe
 *
 * (-I extern/dolphin/include/libc must be left out: its assert.h shadows the
 * host's and defines no assert.)
 *
 * The banks are built here in big-endian exactly as they sit in a .dat, in
 * mapped game RAM with archive provenance set, and the expected values are
 * written out by hand rather than derived from the code under test.
 */
#include <windows.h>

#include "pc_hsd_endian.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <sysdolphin/baselib/psstructs.h>

void pc_hsd_particle_banks_to_native(void* cmd, void* tex, void* form);
void pc_sys_log(const char* s) { fputs(s, stderr); }
void __real_psInitDataBankLocate(HSD_Archive* c, HSD_Archive* t, int* f)
{ (void) c; (void) t; (void) f; }

static void be32(void* p, unsigned v)
{
    unsigned char* b = p;
    b[0] = (unsigned char) (v >> 24);
    b[1] = (unsigned char) (v >> 16);
    b[2] = (unsigned char) (v >> 8);
    b[3] = (unsigned char) v;
}

static void be16(void* p, unsigned v)
{
    unsigned char* b = p;
    b[0] = (unsigned char) (v >> 8);
    b[1] = (unsigned char) v;
}

static float f32_at(const void* p)
{
    float f;
    memcpy(&f, p, sizeof f);
    return f;
}

static unsigned u32_at(const void* p)
{
    unsigned v;
    memcpy(&v, p, sizeof v);
    return v;
}

/* Layout inside the block, all offsets from its base. */
#define CMD_OFF 0x0000
#define CMD_LIST_A 0x0100
#define CMD_LIST_B 0x0200
#define TEX_OFF 0x1000
#define TEX_GROUP 0x1100
#define FORM_OFF 0x2000
#define FORM_GROUP 0x2100

static void build(unsigned char* b)
{
    unsigned char* cmd = b + CMD_OFF;
    unsigned char* tex = b + TEX_OFF;
    unsigned char* form = b + FORM_OFF;
    unsigned char* tg = b + TEX_GROUP;
    unsigned char* fg = b + FORM_GROUP;
    unsigned char* a = b + CMD_LIST_A;

    memset(b, 0, 0x4000);

    /* Command bank, version 0x42: word 1 a count particle.c carries around,
       word 2 the number of list pointers, then the pointers at word 3. */
    be16(cmd + 0, 0x42);
    be16(cmd + 2, 0xBEEF); /* unread by particle.c; must survive as-is */
    be32(cmd + 4, 3);
    be32(cmd + 8, 2);
    be32(cmd + 12, CMD_LIST_A - CMD_OFF);
    be32(cmd + 16, CMD_LIST_B - CMD_OFF);

    /* One command list, filled so every field width is distinguishable. */
    be16(a + 0x00, 0x0102); /* type */
    be16(a + 0x02, 0x0304); /* texGroup */
    be16(a + 0x04, 0x0506); /* genLife */
    be16(a + 0x06, 0x0708); /* life */
    be32(a + 0x08, 0x08000000); /* kind */
    be32(a + 0x0C, 0x3F800000); /* grav = 1.0f */
    be32(a + 0x10, 0x40000000); /* fric = 2.0f */
    be32(a + 0x14, 0xC0400000); /* vx   = -3.0f */
    be32(a + 0x38, 0x41200000); /* param3 = 10.0f */
    a[0x3C] = 0x11; /* the byte program: no byte order to fix */
    a[0x3D] = 0x22;

    /* Texture bank: one group, in a palette format so palettes relocate. */
    be32(tex + 0, 1);
    be32(tex + 4, TEX_GROUP - TEX_OFF);
    be32(tg + 0x00, 2);  /* num */
    be32(tg + 0x04, 9);  /* fmt = C8, a palette format */
    be32(tg + 0x08, 1);  /* tlutfmt */
    be32(tg + 0x0C, 64); /* width */
    be32(tg + 0x10, 32); /* height */
    be16(tg + 0x14, 0);  /* palnum */
    be16(tg + 0x16, 1);  /* palflag: one palette pointer at index num */
    be32(tg + 0x18, 0x111);
    be32(tg + 0x1C, 0x222);
    be32(tg + 0x20, 0x333); /* the palette entry */

    /* Form bank: matching group count, one group of two entries. */
    be32(form + 0, 1);
    be32(form + 4, FORM_GROUP - FORM_OFF);
    be32(fg + 0, 2);
    be32(fg + 4, 0x444);
    be32(fg + 8, 0x555);
}

static void test_converts(unsigned char* b)
{
    unsigned char* cmd = b + CMD_OFF;
    unsigned char* a = b + CMD_LIST_A;
    unsigned char* tg = b + TEX_GROUP;
    unsigned char* fg = b + FORM_GROUP;
    HSD_PSTexGroup* g = (HSD_PSTexGroup*) tg;

    build(b);
    pc_hsd_forget_range(b, 0x4000);
    pc_hsd_archive_body(b, 0x4000);
    pc_hsd_particle_banks_to_native(b + CMD_OFF, b + TEX_OFF, b + FORM_OFF);

    /* Command bank header. The version is a u16 and must not be swapped as
       part of a word, or the unread u16 beside it would move too. */
    assert(*(unsigned short*) cmd == 0x42);
    assert(memcmp(cmd + 2, "\xBE\xEF", 2) == 0);
    assert(u32_at(cmd + 4) == 3);
    assert(u32_at(cmd + 8) == 2);
    assert(u32_at(cmd + 12) == CMD_LIST_A - CMD_OFF);
    assert(u32_at(cmd + 16) == CMD_LIST_B - CMD_OFF);

    /* Command list: two-byte fields stay two bytes, floats become floats. */
    assert(*(unsigned short*) (a + 0x00) == 0x0102);
    assert(*(unsigned short*) (a + 0x02) == 0x0304);
    assert(*(unsigned short*) (a + 0x04) == 0x0506);
    assert(*(unsigned short*) (a + 0x06) == 0x0708);
    assert(u32_at(a + 0x08) == 0x08000000);
    assert(f32_at(a + 0x0C) == 1.0f);
    assert(f32_at(a + 0x10) == 2.0f);
    assert(f32_at(a + 0x14) == -3.0f);
    assert(f32_at(a + 0x38) == 10.0f);
    assert(a[0x3C] == 0x11 && a[0x3D] == 0x22); /* byte program untouched */

    /* Texture bank and its group, including the palette entry that only a
       palette format has. */
    assert(u32_at(b + TEX_OFF) == 1);
    assert(u32_at(b + TEX_OFF + 4) == TEX_GROUP - TEX_OFF);
    assert(g->num == 2 && g->fmt == 9 && g->tlutfmt == 1);
    assert(g->width == 64 && g->height == 32);
    assert(g->palnum == 0 && g->palflag == 1);
    assert(u32_at(tg + 0x18) == 0x111);
    assert(u32_at(tg + 0x1C) == 0x222);
    assert(u32_at(tg + 0x20) == 0x333);

    assert(u32_at(b + FORM_OFF) == 1);
    assert(u32_at(b + FORM_OFF + 4) == FORM_GROUP - FORM_OFF);
    assert(u32_at(fg + 0) == 2);
    assert(u32_at(fg + 4) == 0x444);
    assert(u32_at(fg + 8) == 0x555);

    puts("PASS: command bank, command lists, texture group with palette, and "
         "form group convert");
}

static void test_converts_once(unsigned char* b)
{
    unsigned char snapshot[0x4000];

    build(b);
    pc_hsd_forget_range(b, 0x4000);
    pc_hsd_archive_body(b, 0x4000);
    pc_hsd_particle_banks_to_native(b + CMD_OFF, b + TEX_OFF, b + FORM_OFF);
    memcpy(snapshot, b, sizeof snapshot);

    /* psInitDataBankLocate rewrites these offsets into pointers afterwards, so
       a second conversion would corrupt real pointers. It must do nothing. */
    pc_hsd_particle_banks_to_native(b + CMD_OFF, b + TEX_OFF, b + FORM_OFF);
    assert(!memcmp(b, snapshot, sizeof snapshot));

    puts("PASS: a second pass over an already-converted bank changes nothing");
}

static void test_native_banks_are_left_alone(void)
{
    /* Banks the game builds itself are already host order and carry no
       archive provenance. Converting them would corrupt them. */
    static unsigned tex[4] = { 1, 0, 0, 0 };
    static unsigned before[4];

    memcpy(before, tex, sizeof before);
    pc_hsd_particle_banks_to_native(NULL, tex, NULL);
    assert(!memcmp(tex, before, sizeof before));

    puts("PASS: a natively built bank is not converted");
}

static void test_counts_are_bounded_by_the_archive(unsigned char* b)
{
    /* The counts that drive every walk come out of the data. One that reaches
       past the archive must be refused, not followed. pc_hsd_in_archive is
       what makes that checkable at all. */
    build(b);
    pc_hsd_forget_range(b, 0x4000);
    pc_hsd_archive_body(b, 0x1000); /* only the command bank is archive */

    assert(pc_hsd_in_archive(b, 0x1000));
    assert(!pc_hsd_in_archive(b, 0x1004));
    assert(!pc_hsd_in_archive(b + TEX_OFF, 4));
    /* A list count of 2 needs words 3 and 4, which are inside. */
    assert(pc_hsd_in_archive(b + 12, 8));
    /* 0x4000 of them would not be. */
    assert(!pc_hsd_in_archive(b + 12, 0x4000u * 4));

    puts("PASS: derived ranges are checked against the archive extent");
}

int main(void)
{
    unsigned char* block = VirtualAlloc((void*) 0x81000000u, 0x8000,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    assert(block == (void*) 0x81000000u);

    test_converts(block);
    test_converts_once(block);
    test_native_banks_are_left_alone();
    test_counts_are_bounded_by_the_archive(block);

    pc_hsd_forget_range(block, 0x4000);
    VirtualFree(block, 0, MEM_RELEASE);
    puts("all particle bank tests passed");
    return 0;
}
