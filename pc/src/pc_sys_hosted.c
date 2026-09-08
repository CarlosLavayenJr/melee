/* pc_sys_hosted.c — host services via the platform C library.
   Used for any build that is not the 32-bit freestanding one. */
#if !(defined(__i386__) && defined(PC_FREESTANDING))

#include "pc_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

int pc_sys_map_fixed(unsigned long at, unsigned long size)
{
    void* p = mmap((void*) at, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p != MAP_FAILED && (unsigned long) p == at;
}

unsigned long long pc_sys_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long) ts.tv_sec * 1000000000ull +
           (unsigned long long) ts.tv_nsec;
}

void pc_sys_log(const char* s) { fputs(s, stderr); }
void pc_sys_exit(int code)     { exit(code); }

#endif
