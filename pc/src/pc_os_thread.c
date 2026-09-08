/* pc_os_thread.c — a cooperative, single-threaded stand-in for OSThread.c.
 *
 * The console's scheduler is preemptive and switches register context through
 * OSContext.c, which is MWCC assembly and cannot follow us here. Rather than
 * emulate that, this build runs the game on one thread and turns the two
 * blocking primitives into cooperative points.
 *
 * That is a fair trade because Melee barely threads. Game code calls only
 * OSCreateThread and OSResumeThread; the rest of the API is reached through
 * the SDK, mostly to block on a device.
 *
 * OSSleepThread is where it gets interesting. On hardware a sleeper is woken
 * by an interrupt -- VIWaitForRetrace parks until the vertical retrace fires:
 *
 *     count = retraceCount;
 *     do { OSSleepThread(&retraceQueue); } while (count == retraceCount);
 *
 * Nothing here raises interrupts, so a sleep that merely returned would spin
 * that loop forever. Instead sleeping *advances the machine*: it calls the VI
 * retrace handler the game itself registered during VIInit, which bumps
 * retraceCount and runs the frame callbacks. The wait then finds its condition
 * satisfied and proceeds.
 *
 * So the frame loop is inverted relative to a normal port. Rather than the
 * host driving frames and the game following, the game asks to wait and the
 * host advances one frame inside that request. It is enough to boot, and it
 * keeps every caller's control flow intact. A port that wants real pacing --
 * vsync, a fixed timestep -- eventually takes the loop back, and this function
 * is where that inversion gets undone.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSInterrupt.h>
#include <dolphin/os/OSThread.h>

/* Registered by VIInit as __OSSetInterruptHandler(0x18, __VIRetraceHandler).
   The handler is static inside vi.c, so it is reached through the table in
   pc_os_interrupt.c rather than by name. */
#define PC_INTERRUPT_PI_VI 24

static OSThread pc_main_thread;
static int scheduler_disabled;
static void* idle_function;

void __OSThreadInit(void) { }

OSThread* OSGetCurrentThread(void) { return &pc_main_thread; }

long OSCheckActiveThreads(void) { return 1; }

void OSInitThreadQueue(OSThreadQueue* queue)
{
    queue->head = NULL;
    queue->tail = NULL;
}

/* Advance one video frame by invoking the handler the game registered. */
static void pc_vi_tick(void)
{
    __OSInterruptHandler handler = __OSGetInterruptHandler(PC_INTERRUPT_PI_VI);
    if (handler != NULL) {
        handler(PC_INTERRUPT_PI_VI, NULL);
    }
}

void OSSleepThread(OSThreadQueue* queue)
{
    (void) queue;
    pc_vi_tick();
}

void OSWakeupThread(OSThreadQueue* queue) { (void) queue; }

void OSYieldThread(void) { pc_vi_tick(); }

/* Threads are accepted and never started. Anything the game spawns would need
   the frame loop to pump it; nothing on the boot path depends on one running,
   and a thread that silently does not run is easier to notice than a crash. */
int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                   void* stack, unsigned long stackSize, long priority,
                   unsigned short attr)
{
    (void) thread; (void) func; (void) param;
    (void) stack; (void) stackSize; (void) priority; (void) attr;
    return 1;
}

s32 OSResumeThread(OSThread* thread)  { (void) thread; return 0; }
s32 OSSuspendThread(OSThread* thread) { (void) thread; return 0; }
void OSCancelThread(OSThread* thread) { (void) thread; }
void OSDetachThread(OSThread* thread) { (void) thread; }
BOOL OSJoinThread(OSThread* thread, void** val)
{
    (void) thread; (void) val;
    return FALSE;
}
void OSExitThread(void* val) { (void) val; }

BOOL OSIsThreadSuspended(OSThread* thread)  { (void) thread; return FALSE; }
BOOL OSIsThreadTerminated(OSThread* thread) { (void) thread; return FALSE; }

s32 OSGetThreadPriority(OSThread* thread) { (void) thread; return 16; }
BOOL OSSetThreadPriority(OSThread* thread, s32 priority)
{
    (void) thread; (void) priority;
    return TRUE;
}
s32 __OSGetEffectivePriority(OSThread* thread) { (void) thread; return 16; }
void __OSPromoteThread(OSThread* thread, s32 priority)
{
    (void) thread; (void) priority;
}

/* One runnable thread means there is never anything to reschedule to. */
void __OSReschedule(void) { }

s32 OSDisableScheduler(void) { return scheduler_disabled++; }
s32 OSEnableScheduler(void)  { return scheduler_disabled--; }

void  OSSetIdleFunction(void* func, void* param, void* stack, u32 stackSize)
{
    (void) param; (void) stack; (void) stackSize;
    idle_function = func;
}
void* OSGetIdleFunction(void) { return idle_function; }
