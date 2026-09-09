#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include "pc_clock.h"

extern void pc_bootinfo_init(void);
extern void pc_alarm_poll(void);
static unsigned long long now_ns;
static unsigned callbacks;

unsigned long long pc_sys_mono_ns(void) { return now_ns; }
void pc_watch_hit(int site) { (void) site; }
static void fired(OSAlarm* alarm, OSContext* context)
{
    (void) alarm; (void) context;
    callbacks++;
}

int main(void)
{
    void* memory = VirtualAlloc((void*) 0x80000000u, 0x20000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    OSAlarm alarm;
    OSTime period;
    assert(memory == (void*) 0x80000000u);
    pc_bootinfo_init();
    assert(OS_BUS_CLOCK == PC_BUS_CLOCK && OS_CORE_CLOCK == PC_CORE_CLOCK);
    assert(OS_TIMER_CLOCK == 40500000);
    assert(OSSecondsToTicks(1) == 40500000);
    assert(OSMillisecondsToTicks(1) == 40500);
    assert(OSMicrosecondsToTicks(1000) == 40500);
    assert(OSNanosecondsToTicks(1000000) == 40500);
    assert(OSTicksToSeconds((OSTime) 81000000) == 2);
    assert(OSTicksToMilliseconds((OSTime) 40500) == 1);
    assert(OSTicksToMicroseconds((OSTime) 40500) == 1000);
    assert(OSGetTime() == 0);
    now_ns = 1000000000ull;
    assert(OSGetTime() == OSSecondsToTicks(1));
    now_ns = 600123456789ull;
    assert(OSGetTime() == 24304999999ll);
    now_ns = 86400000000000ull;
    assert(OSGetTime() == (OSTime) 86400 * PC_TIMER_CLOCK);
    assert(OSGetTick() == (OSTick) OSGetTime());

    OSInitAlarm();
    OSCreateAlarm(&alarm);
    period = OSSecondsToTicks(1.0f / 60);
    assert(period == 675000);
    OSSetPeriodicAlarm(&alarm, period, period, fired);
    pc_alarm_poll(); assert(callbacks == 0);
    now_ns += 16666667;
    pc_alarm_poll(); assert(callbacks == 1 && alarm.handler == fired);
    pc_alarm_poll(); assert(callbacks == 1);
    now_ns += 16666667;
    pc_alarm_poll(); assert(callbacks == 2 && alarm.handler == fired);
    now_ns += 1000000000;
    pc_alarm_poll(); assert(callbacks == 3 && alarm.fire > OSGetTime());
    OSCancelAlarm(&alarm);
    now_ns += 1000000000;
    pc_alarm_poll(); assert(callbacks == 3);
    OSSetAlarm(&alarm, OSMillisecondsToTicks(1), fired);
    now_ns += 1000000;
    pc_alarm_poll(); assert(callbacks == 4 && alarm.handler == NULL);
    pc_alarm_poll(); assert(callbacks == 4);
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    puts("PASS: IPL clocks, SDK conversions, long-uptime ticks and periodic/one-shot alarms");
    return 0;
}
