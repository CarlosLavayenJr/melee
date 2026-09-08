#ifndef _STDARG_H_
#define _STDARG_H_

#ifdef __MWERKS__

/* The PowerPC EABI varargs layout, reached through MWCC's own intrinsics. */
typedef struct {
    char gpr;
    char fpr;
    char reserved[2];
    char* input_arg_area;
    char* reg_save_area;
} __va_list[1];
typedef __va_list va_list;

extern void __builtin_va_info(void*);

void* __va_arg(va_list v_list, unsigned char type);

#define va_start(ap, fmt) ((void) fmt, __builtin_va_info(&ap))
#define va_arg(ap, t) (*((t*) __va_arg(ap, _var_arg_typeof(t))))
#define va_end(ap) (void) 0

#else

/* __builtin_va_info and __va_arg are MWCC compiler internals -- there is
   nothing to link them against elsewhere, and a host build that reaches this
   header dies inside the first OSReport. GCC and Clang expose the same
   facility under the standard names, so use those.

   _var_arg_typeof stays defined because sources spell MWCC's idiom directly:
   see EFALT_VA_ARG in src/melee/ef/efalt.c. */
typedef __builtin_va_list va_list;

#define _var_arg_typeof(e) 0

#define va_start(ap, fmt) __builtin_va_start(ap, fmt)
#define va_arg(ap, t) __builtin_va_arg(ap, t)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(d, s) __builtin_va_copy(d, s)

#endif

#endif
