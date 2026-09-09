/* pc_pad_alarm.c — unblocks gm_801A4D34's pad-queue wait loop.
 *
 * That loop (src/melee/gm/gm_1A45.c) reads:
 *
 *     while ((pad_queue_count = lb_80019894()) == 0) {
 *         lb_800195D0();
 *     }
 *
 * lb_80019894() is HSD_PadGetRawQueueCount(), which reads HSD_PadLibData's
 * qcount -- and qcount has exactly one writer in the whole tree,
 * HSD_PadRenewRawStatus() (controller.c). Two things are supposed to drive
 * that writer, and this port supplies neither:
 *
 *   - lb_80019628()'s periodic OSAlarm (lb_0195.c), which never actually
 *     arms: confirmed live via gdb, lb_804329F0.x38 and x0[0].x0 are both 0
 *     at every call, so its very first guard (`if (new_val ==
 *     lb_804329F0.x0[0].x0) return;`) fires every time. x38 is set by
 *     lb_80019880(), whose only caller on this boot path (gm_801A4BD4) runs
 *     during match setup, well after the disc/memcard boot screen this loop
 *     belongs to -- so on hardware too, this path is not what unblocks early
 *     boot.
 *   - the SI poll-complete hardware interrupt, which is what actually keeps
 *     qcount moving on real hardware during boot. dolphin/pad/pad.c is
 *     excluded from this build (see linkexe.sh) and pc_pad.c's PADRead is
 *     synchronous, so there is no interrupt here to replace it with either.
 *
 * Given PADRead is already synchronous on this host, there is no reason to
 * wait for either driver: renewing raw status at the one point the game
 * polls for it is equivalent and simpler than reconstructing either
 * hardware's timing. pc_alarm_poll() still runs first for whatever other
 * OSAlarms are live elsewhere in the game; it is not what fixes this spin.
 */
#include "pc_sys.h"

#include <sysdolphin/baselib/controller.h>

extern void pc_alarm_poll(void);

u8 __real_HSD_PadGetRawQueueCount(void);
u8 __wrap_HSD_PadGetRawQueueCount(void)
{
    pc_alarm_poll();
    HSD_PadRenewRawStatus(0);
    return __real_HSD_PadGetRawQueueCount();
}
