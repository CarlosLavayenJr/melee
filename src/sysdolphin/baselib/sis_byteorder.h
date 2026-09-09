#ifndef HSD_SIS_BYTEORDER_H
#define HSD_SIS_BYTEORDER_H
#include <dolphin/types.h>
/* SIS opcodes, glyph IDs, fixed-point arguments and the saved-state stack
   are serialized big-endian, including bytes emitted by native producers.
   Archive-relocated pointer operands are a separate schema: do not swap them. */
#ifdef PC_GX_RENDERER
static inline u16 sis_read_u16(const void* data)
{
    const u8* p = data;
    return (u16)(((u16)p[0] << 8) | p[1]);
}
static inline s16 sis_read_s16(const void* data) { return (s16)sis_read_u16(data); }
static inline s32 sis_read_saved_pointer(const void* data)
{
    const u8* p = data;
    return (s32)(((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]);
}
#else
#define sis_read_u16(p) (*(u16*)(p))
#define sis_read_s16(p) (*(s16*)(p))
#define sis_read_saved_pointer(p) (*(s32*)(p))
#endif
#endif
