#ifndef PC_CLOCK_H
#define PC_CLOCK_H

/* Retail clock values published by the IPL and used by the native timebase. */
#define PC_BUS_CLOCK 162000000u
#define PC_CORE_CLOCK 486000000u
#define PC_TIMER_CLOCK (PC_BUS_CLOCK / 4u)

#endif
