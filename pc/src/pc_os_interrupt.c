/* pc_os_interrupt.c — host stand-ins for the OS files that cannot leave PowerPC.
 *
 * OSInterrupt.c, OSAlarm.c, OSTime.c, OSCache.c and OSContext.c are all written
 * as MWCC `asm` function bodies: exception vectors, cache line operations,
 * register-window save and restore, decrementer reads. There is no host
 * equivalent to translate, so those files are excluded from host builds and
 * the entry points the game calls are provided here.
 *
 * Interrupts are the interesting case. On hardware VIInit registers a handler
 * for the vertical retrace and the PPC exception path invokes it. Here nothing
 * generates interrupts, so handlers are stored and never called. That is
 * deliberate rather than lazy: the port drives frames from its own loop, and
 * the registered handler is what that loop will eventually call directly.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSInterrupt.h>

/* __OSInterrupt is an index; the table is sized by the enum's upper bound. */
#define PC_NUM_INTERRUPTS 32

static __OSInterruptHandler handlers[PC_NUM_INTERRUPTS];
static OSInterruptMask interrupt_mask;

volatile __OSInterrupt __OSLastInterrupt;
volatile u32 __OSLastInterruptSrr0;
volatile OSTime __OSLastInterruptTime;

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt interrupt,
                                             __OSInterruptHandler handler)
{
    __OSInterruptHandler prev;
    if ((u32) interrupt >= PC_NUM_INTERRUPTS) {
        return NULL;
    }
    prev = handlers[interrupt];
    handlers[interrupt] = handler;
    return prev;
}

__OSInterruptHandler __OSGetInterruptHandler(__OSInterrupt interrupt)
{
    if ((u32) interrupt >= PC_NUM_INTERRUPTS) {
        return NULL;
    }
    return handlers[interrupt];
}

/* Masking is bookkeeping only while nothing raises interrupts. Each returns
   the previous mask, which is what callers restore. */
OSInterruptMask OSGetInterruptMask(void) { return interrupt_mask; }

OSInterruptMask OSSetInterruptMask(OSInterruptMask mask)
{
    OSInterruptMask prev = interrupt_mask;
    interrupt_mask = mask;
    return prev;
}

OSInterruptMask __OSMaskInterrupts(OSInterruptMask mask)
{
    OSInterruptMask prev = interrupt_mask;
    interrupt_mask |= mask;
    return prev;
}

OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask)
{
    OSInterruptMask prev = interrupt_mask;
    interrupt_mask &= ~mask;
    return prev;
}

void __OSDispatchInterrupt(__OSException exception, OSContext* context)
{
    (void) exception;
    (void) context;
}
