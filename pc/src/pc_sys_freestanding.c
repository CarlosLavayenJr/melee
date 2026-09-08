/* pc_sys_freestanding.c — host services via raw Linux syscalls, for -nostdlib.
 *
 * Built only for 32-bit x86 freestanding links, where no libc is available.
 * The hosted build uses pc_sys_hosted.c instead.
 */
#if defined(__i386__) && defined(PC_FREESTANDING)

#include "pc_sys.h"

#define SYS_exit   1
#define SYS_write  4
#define SYS_mmap2 192

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

static long sys(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    __asm__ volatile("push %%ebp\n\t"
                     "mov %7, %%ebp\n\t"
                     "int $0x80\n\t"
                     "pop %%ebp"
                     : "=a"(r)
                     : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), "m"(f)
                     : "memory");
    return r;
}

int pc_sys_map_fixed(unsigned long at, unsigned long size)
{
    /* mmap2 takes the offset in pages, not bytes; anonymous mappings ignore
       it. Kernel errors come back as -1..-4095 in the return register. */
    long r = sys(SYS_mmap2, (long) at, (long) size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (r < 0 && r > -4096) {
        return 0;
    }
    return (unsigned long) r == at;
}

void pc_sys_log(const char* s)
{
    long n = 0;
    while (s[n]) {
        n++;
    }
    sys(SYS_write, 2, (long) s, n, 0, 0, 0);
}

void pc_sys_exit(int code)
{
    sys(SYS_exit, code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

/* The game owns main(); this is only the ELF entry the kernel jumps to.
 *
 * A hosted build reaches main() through the C runtime, which walks
 * .init_array and runs every __attribute__((constructor)) on the way. There is
 * no C runtime here, so that has to happen by hand -- otherwise pc_memory_init
 * never runs, nothing is mapped at 0x80000000, and main() faults on its first
 * dereference. */
extern int main(void);

/* pc_memory_init is a constructor, which a hosted build reaches through
   .init_array. Nothing walks that array here, and the section bounds the
   linker would normally supply are absent from a -nostdlib -static link, so it
   is called by name. Any future initializer belongs in this list too. */
extern void pc_memory_init(void);

void pc_start_c(void)
{
    pc_memory_init();
    pc_sys_exit(main());
}

/* The kernel enters _start with %esp pointing at argc, which is only 4-byte
 * aligned. The System V i386 ABI promises 16-byte alignment at a call
 * boundary, and GCC relies on it -- it will happily emit an aligned SSE store
 * into a local, which faults in the very first prologue it runs. A normal
 * function cannot fix this, because its own prologue is already running on the
 * bad stack, so the entry point is naked and realigns before calling anything.
 */
__asm__(".globl _start\n"
        "_start:\n"
        "    and  $-16, %esp\n"
        "    call pc_start_c\n");

#endif
