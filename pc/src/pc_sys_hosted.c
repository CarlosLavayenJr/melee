/* pc_sys_hosted.c — host services via the platform C library.
   Used for any build that is not the 32-bit freestanding one. */
/* Not on Windows: pc_sys_windows.c covers that, and none of the POSIX
   calls below exist there. */
#if !(defined(__i386__) && defined(PC_FREESTANDING)) && !defined(_WIN32)

#include "pc_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

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

int pc_sys_open_ro(const char* path) { return open(path, O_RDONLY); }

long pc_sys_pread(int fd, void* buf, unsigned long len,
                  unsigned long long offset)
{
    return (long) pread(fd, buf, (size_t) len, (off_t) offset);
}

void pc_sys_close(int fd) { close(fd); }

int pc_sys_env(const char* name, char* buf, unsigned long size)
{
    const char* v = getenv(name);
    unsigned long n;
    if (!v) return 0;
    for (n = 0; v[n] && n + 1 < size; n++) buf[n] = v[n];
    if (v[n]) return 0; /* did not fit */
    buf[n] = '\0';
    return 1;
}

#endif

