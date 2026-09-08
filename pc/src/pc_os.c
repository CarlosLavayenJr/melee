/* pc_os.c — host implementations of the Dolphin OS entry points.
 *
 * extern/dolphin/src/dolphin/os/OS.c does not build off PowerPC: its exception
 * vectors, FPR setup and context switching are MWCC `asm void` function bodies
 * and its prototypes carry `__declspec(section ".init")`. None of that has a
 * host equivalent, so the file is excluded from host builds and the few entry
 * points the game actually calls are provided here.
 *
 * Only what OS.c itself defined belongs in this file. OSArena.c, OSAlarm.c and
 * their neighbours compile cleanly and are used as-is -- OSInit below drives
 * the real arena rather than keeping its own.
 */
#include "pc_sys.h"

typedef unsigned int u32;
typedef int BOOL;

/* Provided by OSArena.c, which builds unmodified. */
extern void  OSSetArenaLo(void* lo);
extern void  OSSetArenaHi(void* hi);
extern void* OSGetArenaLo(void);
extern void* OSGetArenaHi(void);

/* Provided by OSError.c, which builds unmodified. */
extern void OSReport(const char* fmt, ...);

#define GC_RAM_CACHED 0x80000000UL
#define GC_RAM_SIZE   (24u << 20) /* retail GameCube main RAM */

/* Low memory holds OSBootInfo and the globals the OS keeps at fixed physical
   addresses, so the arena starts above it rather than at zero. */
#define ARENA_LO (GC_RAM_CACHED + 0x00100000UL)
#define ARENA_HI (GC_RAM_CACHED + GC_RAM_SIZE)

static int os_initialized = 0;

u32 OSGetPhysicalMemSize(void)         { return GC_RAM_SIZE; }
u32 OSGetConsoleSimulatedMemSize(void) { return GC_RAM_SIZE; }

/* The game only brackets critical sections with these and this build is
   single-threaded, so there is nothing to disable. */
BOOL OSDisableInterrupts(void)       { return 0; }
BOOL OSEnableInterrupts(void)        { return 0; }
BOOL OSRestoreInterrupts(BOOL level) { (void) level; return 0; }

void OSInit(void)
{
    if (os_initialized) {
        return;
    }
    os_initialized = 1;

    /* Hardware OSInit programs interrupt controllers, caches, EXI, SI and the
       SRAM mirror before handing the arena over. Here pc_memory.c has already
       mapped the address range, so setting the water marks is the whole job. */
    OSSetArenaLo((void*) ARENA_LO);
    OSSetArenaHi((void*) ARENA_HI);

    /* OSReport routes through MSL's printf, which is not wired to an output
       device yet, so announce this over the host log instead. */
    pc_sys_log("pc_os: arena set, 23 MB\n");
}
