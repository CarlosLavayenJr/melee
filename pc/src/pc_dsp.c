/* pc_dsp.c — the DSP, answered without one.
 *
 * The GameCube's DSP is a separate 16-bit processor that does the actual audio
 * mixing. The CPU hands it microcode plus a task descriptor and the two talk
 * over a pair of hardware mailboxes. AX builds voice parameter blocks in
 * memory; the DSP reads them and produces the finished stereo stream; AI moves
 * that to the DACs.
 *
 * dsp.c and dsp_task.c drive those mailboxes through __DSPRegs, so they do not
 * build off PowerPC -- and like ar.c they hang rather than fault: the boot
 * handshake spins in DSPCheckMailFromDSP waiting for microcode that was never
 * loaded, because there is no processor to load it into.
 *
 * The mailboxes here always report "ready, nothing waiting", so no handshake
 * blocks. Tasks are accepted and their lifecycle callbacks -- init, then done
 * -- are invoked immediately, so the SDK's task queue drains instead of
 * stalling behind a task that never completes.
 *
 * Nothing mixes. Melee's audio microcode lives in
 * extern/dolphin/src/dolphin/ax/DSPCode.c as 375 lines of raw DSP opcodes, and
 * running it means either emulating that processor or reimplementing what it
 * does. The second is the usual choice: intercept above this layer at AX,
 * where the voice parameter blocks are still readable structs -- sample
 * address, ADPCM format, pitch, volume, routing -- and mix them on the CPU.
 * gcrecomp's dsp_decoder.cpp is a documented reference for the ADPCM half.
 */
#include "pc_sys.h"

#include <dolphin/dsp.h>

/* Task states, mirroring dsp_task.c. */
#define PC_DSP_TASK_INIT 1
#define PC_DSP_TASK_RUN  4
#define PC_DSP_TASK_DONE 8

static DSPTaskInfo* current_task;
static int dsp_initialized;
static u32 last_mail;

void DSPInit(void)
{
    dsp_initialized = 1;
    pc_sys_log("pc_dsp: no DSP; audio tasks complete immediately\n");
}

BOOL DSPCheckInit(void) { return dsp_initialized; }

void DSPReset(void)  { current_task = NULL; }
void DSPHalt(void)   { }
void DSPUnhalt(void) { }

/* "The outbound mailbox is empty" -- the CPU may always send. */
u32 DSPCheckMailToDSP(void) { return 0; }

/* "The inbound mailbox is empty" -- there is never a reply pending. Returning
   non-zero here would make callers read a message that does not exist; the
   handshake instead falls through on its own timeout paths. */
u32 DSPCheckMailFromDSP(void) { return 0; }

u32 DSPReadMailFromDSP(void)  { return 0; }
u32 DSPReadCPUToDSPMbox(void) { return last_mail; }

void DSPSendMailToDSP(u32 mail) { last_mail = mail; }

void DSPAssertInt(void) { }

u32 DSPGetDMAStatus(void) { return 0; }

/* __DSPGetCurrentTask is left to dsp_debug.c, which builds unmodified. It
   reads the same variable the SDK exports, so it is not duplicated here. */

/* A task on hardware is: load its microcode, run it, report completion. Here
   the middle step is absent, so the task is walked straight from init to done
   and its callbacks fire in the order the SDK expects. */
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task)
{
    if (task == NULL) {
        return NULL;
    }
    current_task = task;

    task->state = PC_DSP_TASK_INIT;
    if (task->init_cb != NULL) {
        task->init_cb(task);
    }

    task->state = PC_DSP_TASK_RUN;
    task->state = PC_DSP_TASK_DONE;
    if (task->done_cb != NULL) {
        task->done_cb(task);
    }

    current_task = NULL;
    return task;
}

DSPTaskInfo* DSPAssertTask(DSPTaskInfo* task) { return DSPAddTask(task); }

DSPTaskInfo* DSPCancelTask(DSPTaskInfo* task)
{
    if (task != NULL) {
        task->state = PC_DSP_TASK_DONE;
    }
    if (current_task == task) {
        current_task = NULL;
    }
    return task;
}
