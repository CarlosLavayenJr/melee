/* Minimal <stdio.h> for the -m32 survey. See string.h for why. */
#ifndef PHASE0_STDIO_H
#define PHASE0_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct _PHASE0_FILE FILE;
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
