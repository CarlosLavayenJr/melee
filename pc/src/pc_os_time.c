/* pc_os_time.c — host stand-ins for OSTime.c and OSAlarm.c.
 *
 * Both are PowerPC-only on hardware. OSTime.c reads the Gekko time-base
 * registers with `mftb`; OSAlarm.c programs the decrementer and runs its queue
 * from an interrupt. Neither survives the move off the console.
 *
 * Time here comes from the monotonic clock, scaled to the bus clock the game
 * expects, so OSGetTick keeps advancing at the rate the SDK documents.
 *
 * Alarms are recorded but never fire. On hardware the decrementer interrupt
 * walks the queue; nothing raises interrupts here (see pc_os_interrupt.c), so
 * the queue is bookkeeping the port's frame loop will eventually drive. Code
 * that *waits* on an alarm will therefore spin rather than proceed -- when
 * boot next stalls instead of crashing, this is the first place to look.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>

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

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                        OSAlarmHandler handler)
{
    alarm->handler = handler;
    alarm->start = start;
    alarm->period = period;
    alarm->fire = start;
    alarm_link(alarm);
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
