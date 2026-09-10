#ifndef PC_TEXTURE_DECODE_H
#define PC_TEXTURE_DECODE_H
#include <stddef.h>

/* GX format numbers; independent of the console ABI and Vulkan headers. */
enum {
    PC_TF_I4 = 0, PC_TF_I8 = 1, PC_TF_IA4 = 2, PC_TF_IA8 = 3,
    PC_TF_RGB565 = 4, PC_TF_RGB5A3 = 5, PC_TF_RGBA8 = 6,
    PC_TF_CI4 = 8, PC_TF_CI8 = 9, PC_TF_CI14X2 = 10, PC_TF_CMPR = 14
};

/* GXTlutFmt. Palette entries are 16-bit and big-endian, like texel data. */
enum { PC_TL_IA8 = 0, PC_TL_RGB565 = 1, PC_TL_RGB5A3 = 2 };

/* Whether a format needs a palette to decode. */
int pc_texture_is_paletted(unsigned format);

/* One level, including GX's complete padded tiles. Zero means invalid.
   Dimensions are restricted to the GX hardware limit, 1..1024. */
size_t pc_texture_source_size(unsigned format, unsigned width, unsigned height);

/* Big-endian tiled bytes -> tightly packed row-major RGBA8, no Y flip.
   Returns 0 on success, -1 on invalid/unsupported input. Validates both
   buffer lengths before writing anything. Source/destination must not overlap. */
int pc_texture_decode(unsigned format, unsigned width, unsigned height,
                      const void* source, size_t source_size,
                      void* rgba, size_t rgba_size);

/* As above, for the CI formats. `tlut` is tlut_entries 16-bit big-endian
   entries in `tlut_format`. An index past the end of the palette fails the
   whole decode rather than guessing a colour. Passing a palette for a
   non-paletted format, or none for a paletted one, is an error. */
int pc_texture_decode_tlut(unsigned format, unsigned width, unsigned height,
                           const void* source, size_t source_size,
                           const void* tlut, unsigned tlut_entries,
                           unsigned tlut_format,
                           void* rgba, size_t rgba_size);
#endif
