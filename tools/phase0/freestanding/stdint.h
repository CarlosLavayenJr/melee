/* Minimal <stdint.h> for the -m32 survey. GCC's own stdint.h defers to the
   system copy via include_next, which -nostdinc removes, so this builds the
   types from GCC's arch-aware predefined macros instead. Those track -m32
   correctly, which is the entire point of this directory. */
#ifndef PHASE0_STDINT_H
#define PHASE0_STDINT_H
typedef __INT8_TYPE__   int8_t;
typedef __UINT8_TYPE__  uint8_t;
typedef __INT16_TYPE__  int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__  int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__  int64_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#endif
