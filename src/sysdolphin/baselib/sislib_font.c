#include "sislib_font.h"

TextGlyphTexture HSD_SisLib_FontAtlas[] ATTRIBUTE_ALIGN(32) = {
#ifdef PC_GX_RENDERER
    /* Native renderer fills the declared 287-glyph atlas from user disc data. */
    { { 0 } }
#else
#include <sysdolphin/baselib/sislib_font.inc>
#endif
};
