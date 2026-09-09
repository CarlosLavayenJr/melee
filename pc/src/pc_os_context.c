/* pc_os_context.c — host stand-in for OSContext.c.
 *
 * OSContext.c is entirely MWCC assembly: it saves and restores the PowerPC
 * register file, the floating-point registers and the paired-single state
 * across a context switch or an exception. None of that describes an x86 host,
 * and the machine state it names does not exist here.
 *
 * pc_os_thread.c runs the game on a single thread and never switches, so these
 * are bookkeeping. OSSetCurrentContext and OSGetCurrentContext are kept honest
 * because callers do round-trip a pointer through them -- __VIRetraceHandler
 * clears the context it was handed on the way out of an interrupt.
 *
 * If a real port ever reintroduces threads, this file is where the host's
 * equivalent belongs -- ucontext, fibers, or genuine OS threads -- not the
 * PowerPC original.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSContext.h>

static OSContext* current_context;

/* On hardware a current context always exists, set up before any game code
   runs. Here OSSetCurrentContext isn't called until the first VI retrace
   handler exits, and db_ClearFPUExceptions (dberror.c) reads and dereferences
   the current context during early boot, well before that first retrace --
   confirmed as a real NULL dereference there. Since OSSaveFPUContext and
   OSLoadFPUContext already ignore whatever context they are handed, a static
   fallback costs nothing and keeps the "always valid" invariant real
   hardware provides. */
static OSContext fallback_context;

OSContext* OSGetCurrentContext(void)
{
    return current_context != NULL ? current_context : &fallback_context;
}

void OSSetCurrentContext(OSContext* context) { current_context = context; }

/* Marks a context as holding no live floating-point state. With one thread
   there is nothing to invalidate, but the call has to succeed: it runs on the
   way out of every interrupt. */
void OSClearContext(OSContext* context) { (void) context; }

void OSInitContext(OSContext* context, u32 pc, u32 newsp)
{
    (void) context; (void) pc; (void) newsp;
}

/* On hardware these switch execution. Saving reports "returned normally"
   rather than "resumed from a restore", which is the only answer that keeps a
   single-threaded caller moving forward. */
u32 OSSaveContext(OSContext* context) { (void) context; return 0; }

void OSLoadContext(OSContext* context) { (void) context; }

void OSSaveFPUContext(OSContext* context) { (void) context; }
void OSLoadFPUContext(OSContext* context) { (void) context; }
void OSFillFPUContext(OSContext* context) { (void) context; }

u32 OSGetStackPointer(void)
{
    /* The caller's frame is close enough for the diagnostics that ask. */
    return (u32) (unsigned long) __builtin_frame_address(0);
}

void OSDumpContext(OSContext* context)
{
    (void) context;
    pc_sys_log("pc_os: OSDumpContext (no PowerPC context to dump)\n");
}
