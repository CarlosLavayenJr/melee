/* pc_os_time.c — host stand-ins for OSTime.c and OSAlarm.c.
 *
 * Both are PowerPC-only on hardware. OSTime.c reads the Gekko time-base
 * registers with `mftb`; OSAlarm.c programs the decrementer and runs its queue
 * from an interrupt. Neither survives the move off the console.
 *
 * Time here comes from the monotonic clock, scaled to the bus clock the game
 * expects, so OSGetTick keeps advancing at the rate the SDK documents.
 *
 * Alarms are recorded and delivered from pc_alarm_poll() below rather than
 * from a decrementer interrupt: nothing raises interrupts here (see
 * pc_os_interrupt.c), so a host call site has to stand in for the exception
 * path. pc_os_thread.c's pc_vi_tick() calls pc_alarm_poll() on every sleep or
 * yield, the same place it already delivers ARAM completions -- but not
 * every wait loop in the game sleeps or yields (gm_801A4D34's pad-queue wait
 * just polls HSD_PadGetRawQueueCount() in a tight loop), so callers that
 * gate on an alarm-driven counter without ever yielding need pc_alarm_poll()
 * wrapped in directly at their own poll point; see pc_pad_alarm.c.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include "pc_watch.h"

/* The SDK derives its tick rate from the bus clock; retail hardware runs a
   162 MHz bus, and OS_TIMER_CLOCK is a quarter of it. */
#define PC_TIMER_CLOCK (162000000u / 4u)
#define NS_PER_SEC     1000000000ull

static OSAlarm* alarm_head;
static int alarm_initialized;

/* Nanoseconds since first call, from the host monotonic clock. */
static unsigned long long pc_mono_ns(void)
{
    static unsigned long long base;
    unsigned long long now = pc_sys_mono_ns();
    if (base == 0) {
        base = now;
    }
    return now - base;
}

OSTime OSGetTime(void)
{
    pc_watch_hit(PC_WATCH_OS_TIME);
    return (OSTime) ((pc_mono_ns() * PC_TIMER_CLOCK) / NS_PER_SEC);
}

OSTick OSGetTick(void) { return (OSTick) OSGetTime(); }

OSTime __OSGetSystemTime(void) { return OSGetTime(); }

/* Retail console, no development hardware attached. */
unsigned long OSGetConsoleType(void) { return 0x00000001u; }

void OSInitAlarm(void)
{
    if (alarm_initialized) {
        return;
    }
    alarm_initialized = 1;
    alarm_head = NULL;
}

void OSCreateAlarm(OSAlarm* alarm)
{
    alarm->handler = NULL;
    alarm->tag = 0;
    alarm->fire = 0;
    alarm->prev = NULL;
    alarm->next = NULL;
    alarm->period = 0;
    alarm->start = 0;
}

static void alarm_link(OSAlarm* alarm)
{
    OSAlarm* p;
    for (p = alarm_head; p != NULL; p = p->next) {
        if (p == alarm) {
            return; /* already queued */
        }
    }
    alarm->prev = NULL;
    alarm->next = alarm_head;
    if (alarm_head != NULL) {
        alarm_head->prev = alarm;
    }
    alarm_head = alarm;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    alarm->handler = handler;
    alarm->period = 0;
    alarm->start = 0;
    alarm->fire = OSGetTime() + tick;
    alarm_link(alarm);
}

void OSSetAbsAlarm(OSAlarm* alarm, OSTime time, OSAlarmHandler handler)
{
    alarm->handler = handler;
    alarm->period = 0;
    alarm->start = 0;
    alarm->fire = time;
    alarm_link(alarm);
}

/* Matches the real InsertAlarm's catch-up: start is a base time, not
   necessarily in the future (lb_80019628 passes the same small tick count as
   both start and period), so the first fire has to be walked forward past
   now by whole periods rather than left in the past where pc_alarm_poll()
   would fire it once and then -- since a period this short is otherwise
   always due -- fire it again on every single poll. */
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                        OSAlarmHandler handler)
{
    OSTime now = OSGetTime();
    OSTime fire = start;
    alarm->handler = handler;
    alarm->start = start;
    alarm->period = period;
    if (fire <= now && period > 0) {
        fire += period * ((now - fire) / period + 1);
    }
    alarm->fire = fire;
    alarm_link(alarm);
}

/* Stand-in for the decrementer exception path: called from pc_vi_tick() and
   from any poll point that gates on an alarm-driven counter without ever
   sleeping or yielding. Restarts the scan after each handler call since a
   handler is free to add, cancel, or reschedule alarms (OSCancelAlarm,
   OSSetPeriodicAlarm re-arming from inside its own callback, etc), same as
   the real dispatcher processing one alarm per interrupt. */
void pc_alarm_poll(void)
{
    OSAlarm* a;
    OSTime now;
    int fired;

again:
    now = OSGetTime();
    fired = 0;
    for (a = alarm_head; a != NULL; a = a->next) {
        OSAlarmHandler handler;
        if (a->handler == NULL || a->fire > now) {
            continue;
        }
        handler = a->handler;
        if (a->period > 0) {
            OSTime fire = a->fire + a->period;
            if (fire <= now) {
                fire += a->period * ((now - fire) / a->period + 1);
            }
            a->fire = fire;
        } else {
            OSCancelAlarm(a);
        }
        handler(a, NULL);
        fired = 1;
        break;
    }
    if (fired) {
        goto again;
    }
}

void OSCancelAlarm(OSAlarm* alarm)
{
    if (alarm->prev != NULL) {
        alarm->prev->next = alarm->next;
    } else if (alarm_head == alarm) {
        alarm_head = alarm->next;
    }
    if (alarm->next != NULL) {
        alarm->next->prev = alarm->prev;
    }
    alarm->prev = NULL;
    alarm->next = NULL;
    alarm->handler = NULL;
}

BOOL OSCheckAlarmQueue(void) { return alarm_head != NULL; }

/* Enough of a calendar for logging and the file-timestamp paths. Epoch is
   2000-01-01, which is what the SDK counts from. */
void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td)
{
    unsigned long long secs = (unsigned long long) ticks / PC_TIMER_CLOCK;
    td->usec = 0;
    td->msec = 0;
    td->sec = (int) (secs % 60);
    td->min = (int) ((secs / 60) % 60);
    td->hour = (int) ((secs / 3600) % 24);
    td->mday = (int) (secs / 86400) % 28 + 1;
    td->mon = 0;
    td->year = 2000;
    td->wday = (int) ((secs / 86400) % 7);
    td->yday = (int) ((secs / 86400) % 365);
}
