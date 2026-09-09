#include <assert.h>
#include <stdio.h>
#include <sysdolphin/baselib/sis_byteorder.h>
int main(void)
{
    /* Include odd alignment: SIS opcode operands need not be word aligned. */
    unsigned char glyph[] = {0, 0x20, 0x1d};
    unsigned char scale[] = {14, 1, 0x80, 2, 0};
    unsigned char spacing[] = {10, 0xff, 0x80, 0, 0};
    unsigned char saved[] = {0, 0x80, 0x31, 0x22, 0x10};
    assert(sis_read_u16(glyph+1) == 0x201d);
    assert(sis_read_u16(glyph+1) - 0x2000 == 29);
    assert(sis_read_u16(scale+1) / 256.0f == 1.5f);
    assert(sis_read_u16(scale+3) / 256.0f == 2.0f);
    assert(sis_read_s16(spacing+1) / 256.0f == -0.5f);
    assert((u32)sis_read_saved_pointer(saved+1) == 0x80312210u);
    /* Match the existing producer's explicit big-endian pointer serialization. */
    {
        u32 p = 0x12567890;
        unsigned char bytes[] = {(u8)(p>>24), (u8)(p>>16), (u8)(p>>8), (u8)p};
        assert((u32)sis_read_saved_pointer(bytes) == p);
    }
    puts("PASS: SIS glyph IDs, signed spacing, scale, unaligned operands, saved pointers");
    return 0;
}
