/* Minimal <stdio.h> for the -m32 survey. See string.h for why. */
#ifndef PHASE0_STDIO_H
#define PHASE0_STDIO_H
#include <stdarg.h>
#include <stddef.h>

/* Metrowerks' FILE is not opaque, and sysdolphin/baselib/debug.c reaches into
   it -- HSD_LogInit swaps stdout->write_proc and clears stdout->state.error to
   redirect logging. The real definition is src/MSL/stdio.h, which cannot win
   the include race against this header without dragging in the rest of MSL,
   so its shape is mirrored here. Field order matters only if something links;
   the survey is syntax-only, so this exists to typecheck against. */
typedef int __file_handle;
typedef void (*__idle_proc)(void);
typedef int (*__pos_proc)(__file_handle, long*, int, __idle_proc);
typedef int (*__io_proc)(__file_handle, unsigned char*, size_t*, __idle_proc);
typedef int (*__close_proc)(__file_handle);

typedef struct _PHASE0_FILE_STATE {
    unsigned int io_state : 3;
    unsigned int free_buffer : 1;
    unsigned char eof;
    unsigned char error;
} __phase0_file_state;

typedef struct _PHASE0_FILE {
    __file_handle handle;
    int mode;
    __phase0_file_state state;
    unsigned long position;
    unsigned char* buffer;
    unsigned long buffer_size;
    unsigned char* buffer_ptr;
    unsigned long buffer_len;
    __pos_proc position_proc;
    __io_proc read_proc;
    __io_proc write_proc;
    __close_proc close_proc;
    __idle_proc idle_proc;
} FILE;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;
int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* s, const char* fmt, ...);
int snprintf(char* s, size_t n, const char* fmt, ...);
int vprintf(const char* fmt, va_list ap);
int vfprintf(FILE* f, const char* fmt, va_list ap);
int vsprintf(char* s, const char* fmt, va_list ap);
int vsnprintf(char* s, size_t n, const char* fmt, va_list ap);
int puts(const char* s);
int putchar(int c);

#endif
