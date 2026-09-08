/* pc_watch.h — a spin detector for the port layer.
 *
 * The hardware this port replaces signals through interrupts on console. Where
 * a replacement never signals, the SDK does not fault: its poll loops spin, so
 * the process sits burning one core with nothing more on the diagnostic
 * stream. A debugger names the loop in seconds, but that needs a machine with
 * both the disc and a debugger on it, which is not always the machine running
 * the build.
 *
 * So the build says it itself. Boot touches the disc constantly, and a spin
 * touches it not at all -- that difference is the whole trick. Count calls into
 * the port entry points a poll loop would go through, reset the counts on every
 * disc read, and a count that runs away means the boot has stopped moving and
 * names the subsystem it stopped in.
 *
 * This is weaker than a backtrace: it reports the subsystem, not the loop. It
 * needs no debugger, no symbols and no second machine.
 */
#ifndef PC_WATCH_H
#define PC_WATCH_H

enum {
    PC_WATCH_OS_TIME, /* OSGetTime -- timeout and deadline loops */
    PC_WATCH_YIELD,   /* OSSleepThread/OSYieldThread -- also the DMA pump */
    PC_WATCH_DSP,     /* DSP mailbox and DMA status */
    PC_WATCH_AR,      /* ARAM DMA status */
    PC_WATCH_PAD,     /* controller reads */
    PC_WATCH_EXI,     /* the peripheral bus */
    PC_WATCH_SITES
};

/* Count one call at a site. */
void pc_watch_hit(int site);

/* A disc read: the boot is still moving, so the counts start over. */
void pc_watch_progress(void);

#endif
