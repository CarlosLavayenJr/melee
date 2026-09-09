/* Replace the absent pixel-engine completion interrupt, retaining the SDK
   DrawDone flag, FinishQueue and the game's registered draw-done callback. */
#include "pc_sys.h"
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/os/OSInterrupt.h>

static int pending, delivering;
void pc_gx_render_finish(void) __attribute__((weak));

void pc_gx_finish_poll(void)
{
    __OSInterruptHandler handler;
    if (!pending || delivering) return;
    handler = __OSGetInterruptHandler(19); /* __GXPEInit: PE_FINISH */
    if (!handler) { pc_sys_log("pc_gx_finish: missing PE finish handler\n"); return; }
    delivering = 1;
    pending = 0;
    if (pc_gx_render_finish) pc_gx_render_finish();
    handler(19, OSGetCurrentContext());
    delivering = 0;
}

void __real_GXSetDrawDone(void);
void __wrap_GXSetDrawDone(void)
{
    BOOL enabled = OSDisableInterrupts();
    __real_GXSetDrawDone();
    pending = 1;
    OSRestoreInterrupts(enabled);
}

/* Calls within GXMisc.c bypass ld --wrap; intercept the composite as well. */
void __wrap_GXDrawDone(void)
{
    __wrap_GXSetDrawDone();
    GXWaitDrawDone();
}
