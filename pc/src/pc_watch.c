/* pc_watch.c — see pc_watch.h. */
#include "pc_watch.h"

#include "pc_sys.h"

/* Every registered site is a place the game polls some port stand-in while
   waiting on it, which makes each one exactly as safe as the frame tick to
   deliver a deferred ARAM DMA completion from: see the comment on pc_ar_poll()
   in pc_ar.c for why ARStartDMA itself cannot do this, and why some poll site
   has to. A caller that busy-waits without ever reaching OSSleepThread or
   OSYieldThread -- so never reaching pc_vi_tick() either -- still reaches
   here if it polls anything at all. */
extern void pc_ar_poll(void);

/* Calls at each site since the last disc read. */
static unsigned long counts[PC_WATCH_SITES];
static int reported;

/* High enough that the ordinary work between two disc reads never reaches it,
   low enough that a loop spinning on a poll crosses it in well under a second
   on any host that can run this at all. */
#define PC_WATCH_LIMIT 5000000UL

static const char* const site_names[PC_WATCH_SITES] = {
    "OSGetTime",
    "OSSleepThread/OSYieldThread",
    "DSP mailbox or DMA status",
    "ARAM DMA status",
    "PADRead",
    "EXI transfers"
};

/* Neither build can assume printf: the freestanding one links no libc. */
static void log_ulong(unsigned long n)
{
    char digits[24];
    char out[25];
    int d = 0, j = 0;

    if (n == 0) {
        pc_sys_log("0");
        return;
    }
    while (n > 0 && d < 24) {
        digits[d++] = (char) ('0' + (n % 10));
        n /= 10;
    }
    while (d > 0) {
        out[j++] = digits[--d];
    }
    out[j] = 0;
    pc_sys_log(out);
}

void pc_watch_progress(void)
{
    int i;

    for (i = 0; i < PC_WATCH_SITES; i++) {
        counts[i] = 0;
    }
    reported = 0;
}

void pc_watch_hit(int site)
{
    int i;

    if (site < 0 || site >= PC_WATCH_SITES) {
        return;
    }
    counts[site]++;
    pc_ar_poll();
    /* Report once per quiet stretch. The flag is set before anything is
       written, so logging from a counted site cannot recurse into here. */
    if (counts[site] < PC_WATCH_LIMIT || reported) {
        return;
    }
    reported = 1;

    pc_sys_log("\npc_watch: the disc has gone quiet and a poll site is "
               "running away.\n");
    for (i = 0; i < PC_WATCH_SITES; i++) {
        if (counts[i] == 0) {
            continue;
        }
        pc_sys_log("pc_watch:   ");
        pc_sys_log(site_names[i]);
        pc_sys_log(" ");
        log_ulong(counts[i]);
        pc_sys_log("\n");
    }
    pc_sys_log("pc_watch: the largest count is the subsystem the boot is "
               "stuck in.\n");
    pc_sys_log("pc_watch: no counts at all would mean the loop never enters "
               "the port layer.\n");
}
