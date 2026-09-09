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

/* Deferred completions (pc_ar.c's ARAM DMA, so far) get delivered wherever
   interrupts become enabled again -- real hardware would fire a pending one
   at exactly this point. This is what actually reaches a caller that spins
   without ever calling OSSleepThread or OSYieldThread: those two are what
   the frame tick runs from, but OSDisableInterrupts/OSRestoreInterrupts
   brackets nearly every critical section in the whole tree, so almost any
   loop crosses one somewhere.
   The state genuinely has to be tracked, not stubbed to "always enabled":
   ARQPostRequest disables and restores around its own queue bookkeeping, and
   its callers -- HSD_DevComARAMWakeUp among them -- already hold their own
   outer disable across that whole call. Firing on every restore regardless
   of nesting drains mid-bookkeeping inside the *outer* function too, one
   level further out than the ARQ-internal case this design started from,
   and corrupts it exactly the same way (confirmed: a real SIGSEGV in
   HSD_DevComARAMWakeUp with that tried). Tracking the actual enabled/
   disabled level and only firing on a genuine disabled->enabled transition
   is what keeps the inner ARQPostRequest restore from firing early: it
   restores to the level *it* observed on entry, which was already disabled,
   so nothing transitions until the outer function's own restore does.
   pc_dvd.c's DVD reads defer their completion callbacks the same way and for
   the same reason: DVDReadAsyncPrio callers write `DVDReadAsyncPrio(...);
   busy = 1;`, trusting that the transfer -- and its callback -- genuinely
   happens later. Here it happens synchronously inside the call, so that
   `busy = 1` runs *after* the callback already correctly reset it, stomping
   it back to a stale "busy" nothing ever clears (confirmed: a hardware
   watchpoint on HSD_DevCom_804D77F5 caught exactly this in
   HSD_DevComDVDWakeUp). */
extern void pc_ar_poll(void);
extern void pc_dvd_poll(void);
extern void pc_gx_finish_poll(void);

static BOOL interrupts_enabled = 1;

/* The game only brackets critical sections with these and this build is
   single-threaded, so nothing else can run in between -- but a deferred
   completion drains at exactly the disabled->enabled edge, matching what a
   real pending interrupt would do. */
BOOL OSDisableInterrupts(void)
{
    BOOL prev = interrupts_enabled;
    interrupts_enabled = 0;
    return prev;
}

/* Guards the pair below as a unit, not just each one against itself.
   pc_ar_drain() and pc_dvd_poll() already stop *themselves* from nesting,
   but that leaves them exposed to *each other*: a DVD completion, running
   inside pc_dvd_poll()'s loop, can post an ARQ request whose own
   OSRestoreInterrupts() call is a genuine 0->1 edge from this function's
   point of view -- pc_ar_drain()'s own guard doesn't cover it, because we
   are inside pc_dvd_poll()'s frame, not pc_ar_drain()'s. Letting it through
   ran an entire ARAM completion -- including devcom's linked-list unlink
   and freelist recycling -- to completion before the DVD callback that
   triggered it had reached its own unlink, corrupting the list (confirmed:
   main.ssm's own devcom entry got recycled and reread, loading its header
   into an already-full bank 0 a second time). One shared guard defers the
   nested edge instead of firing it: whatever it would have delivered stays
   pending until a later, genuinely non-nested edge picks it up -- which
   arrives quickly, since disable/restore brackets nearly every critical
   section in the tree. */
static int delivering_completions;

static void pc_deliver_completions(void)
{
    if (delivering_completions) {
        return;
    }
    delivering_completions = 1;
    pc_ar_poll();
    pc_dvd_poll();
    pc_gx_finish_poll();
    delivering_completions = 0;
}

BOOL OSEnableInterrupts(void)
{
    BOOL prev = interrupts_enabled;
    interrupts_enabled = 1;
    if (!prev) {
        pc_deliver_completions();
    }
    return prev;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    BOOL prev = interrupts_enabled;
    interrupts_enabled = level;
    if (!prev && level) {
        pc_deliver_completions();
    }
    return prev;
}

/* dvdfs.c defines it; the real OSInit publishes it. See the assignment below. */
extern unsigned long __DVDLongFileNameFlag;

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

    /* Except for this one word. dvdfs.c enforces 8.3 filenames unless it is
       set, and the real OSInit sets it unconditionally (OS.c:168) -- so on
       hardware the restriction never applies. Melee ships names that do not
       fit 8.3 at all, PlKbNrCpDk.dat among them, and leaving the flag zero
       turns DVDConvertPathToEntrynum's own diagnostic panic into the game's
       stopping point the first time Kirby's copy-ability data is opened. */
    __DVDLongFileNameFlag = 1;

    /* OSReport routes through MSL's printf, which is not wired to an output
       device yet, so announce this over the host log instead. */
    pc_sys_log("pc_os: arena set, 23 MB\n");
}
