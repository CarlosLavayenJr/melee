/* pc_sys_windows.c — host services on Win32.
 *
 * The freestanding backend is Linux i386 syscalls and the hosted one is POSIX;
 * Windows is neither, so it gets its own. All three implement the same four
 * functions from pc_sys.h, which is the whole point of that header.
 *
 * ---------------------------------------------------------------------------
 * The address space, which is the part that bites
 *
 * pc_memory.c maps the console's own addresses -- 0x80000000, 0xC0000000 and
 * 0xCC000000 -- because the game hardcodes them. A 32-bit Windows process is
 * normally given user address space of 0x00000000 to 0x7FFFFFFF only, and
 * everything above that belongs to the kernel. All three of our addresses are
 * above it, so every mapping fails and nothing runs.
 *
 * A 32-bit process on 64-bit Windows gets the full 4 GB instead, but only if
 * the image is marked large-address-aware. That is a link-time flag, not
 * something this file can ask for:
 *
 *     -Wl,--large-address-aware
 *
 * tools/phase0/linkexe.sh passes it. If a Windows build fails on its first
 * mapping, that flag is the first thing to check.
 *
 * VirtualAlloc is also coarser than mmap: reservations are rounded to the
 * allocation granularity, 64 KB, rather than the 4 KB page size. Our regions
 * are megabytes and start at aligned addresses, so this costs nothing here.
 */
#ifdef _WIN32

#include "pc_sys.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

int pc_sys_map_fixed(unsigned long at, unsigned long size)
{
    void* p = VirtualAlloc((LPVOID) (uintptr_t) at, (SIZE_T) size,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p == NULL) {
        return 0;
    }
    /* MEM_COMMIT pages arrive zeroed, matching MAP_ANONYMOUS. */
    return (unsigned long) (uintptr_t) p == at;
}

unsigned long long pc_sys_mono_ns(void)
{
    LARGE_INTEGER freq, now;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        return (unsigned long long) GetTickCount64() * 1000000ull;
    }
    QueryPerformanceCounter(&now);
    /* Scale before dividing so the nanosecond result keeps its resolution;
       splitting the count avoids overflowing the multiply on long uptimes. */
    return (unsigned long long) (now.QuadPart / freq.QuadPart) * 1000000000ull +
           ((unsigned long long) (now.QuadPart % freq.QuadPart) * 1000000000ull) /
               (unsigned long long) freq.QuadPart;
}

/* Consecutive repeats of the same message are collapsed and counted.
 *
 * stderr is unbuffered and every write to a Windows console is a syscall the
 * console then has to render, so a diagnostic printed once per draw call
 * costs far more than the drawing does. A user running a match reported
 * about 15 fps, with pc_gx_fifo's "incomplete immediate primitive" and
 * pc_gx_material's "draw skipped" lines repeating every frame -- hundreds of
 * console writes per frame, for two messages that say the same thing each
 * time.
 *
 * Nothing is dropped: when the message changes, the count of what was
 * suppressed is printed first, so a log still says exactly how many times
 * each thing happened. That matters because these lines are how this port
 * reports what it cannot yet draw, and quietly losing them would be the kind
 * of silent wrong output the rest of the port goes out of its way to avoid.
 *
 * The callers that build one line from several calls are safe by
 * construction: "pc_stage_data: ", a number and "\n" are three different
 * messages in sequence, so none of them ever collapses into its neighbour.
 * See the comparison itself below for why it reads the strings rather than
 * comparing their addresses -- that shortcut was tried and lost data.
 */
static char log_last[256];
static unsigned long log_repeats;

static void log_flush_repeats(void)
{
    if (log_repeats != 0) {
        fprintf(stderr, "  (previous line repeated %lu more times)\n",
                log_repeats);
        log_repeats = 0;
    }
}

void pc_sys_log(const char* s)
{
    size_t n;

    if (s == NULL) {
        return;
    }
    n = strlen(s);

    /* Only whole lines are ever collapsed, and this restriction is the whole
       correctness argument.
     *
       Plenty of callers build one line from several calls -- "pc_stage_data: ",
       then a number, then "\n" -- and two earlier attempts here both broke on
       that. Comparing pointers collapsed different numbers into each other,
       because log_uint hands out an interior pointer into a local buffer that
       is the same address every call. Comparing contents fixed that but still
       collapsed fragments like "# " across unrelated lines, and printed the
       repeat notice into the middle of a half-built line. The output was
       worse than the spam: "Super Smash Bros. Mele  (previous line repeated 1
       more times)".
     *
       A message that ends in a newline is a complete line on its own, which
       is exactly the shape of the per-draw diagnostics this exists for --
       pc_gx_fifo and pc_gx_material each emit theirs in a single call.
       Fragments are passed straight through and never counted, so a line
       assembled from pieces comes out byte for byte as before. */
    if (n == 0 || s[n - 1] != '\n' || n >= sizeof log_last) {
        log_flush_repeats();
        log_last[0] = '\0';
        fputs(s, stderr);
        return;
    }
    if (strcmp(s, log_last) == 0) {
        log_repeats++;
        return;
    }
    log_flush_repeats();
    memcpy(log_last, s, n + 1);
    fputs(s, stderr);
}

/* Flushed here as well as on the next differing message, because a run that
   ends while a repeat is still counting -- which is every crash and every
   debugger-terminated test -- would otherwise lose the tail of the count, and
   the count is the whole reason collapsing them is honest rather than a
   suppression. */
void pc_sys_exit(int code)
{
    log_flush_repeats();
    exit(code);
}

/* Handles are returned as ints so the interface stays the same across
   backends. Win32 HANDLEs fit: a 32-bit build has 32-bit handles, and a 64-bit
   one still keeps them within 32 bits by documented guarantee. */
int pc_sys_open_ro(const char* path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return (int) (intptr_t) h;
}

long pc_sys_pread(int fd, void* buf, unsigned long len,
                  unsigned long long offset)
{
    HANDLE h = (HANDLE) (intptr_t) fd;
    OVERLAPPED ov;
    DWORD got = 0;

    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }

    /* OVERLAPPED carries the offset per call, so this reads positionally
       without a shared file pointer -- the same property pread has, and what
       DVDLowRead needs since every request names an absolute disc offset. */
    ov.Internal = 0;
    ov.InternalHigh = 0;
    ov.Offset = (DWORD) (offset & 0xFFFFFFFFull);
    ov.OffsetHigh = (DWORD) (offset >> 32);
    ov.hEvent = NULL;

    if (!ReadFile(h, buf, (DWORD) len, &got, &ov)) {
        /* A synchronous handle reports the end of file this way rather than
           as an error. */
        if (GetLastError() != ERROR_HANDLE_EOF) {
            return -1;
        }
    }
    return (long) got;
}

void pc_sys_close(int fd)
{
    HANDLE h = (HANDLE) (intptr_t) fd;
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

#endif /* _WIN32 */

int pc_sys_env(const char* name, char* buf, unsigned long size)
{
    DWORD n = GetEnvironmentVariableA(name, buf, (DWORD) size);
    return n != 0 && n < size;
}
