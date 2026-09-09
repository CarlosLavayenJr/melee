/* pc_init.c — one startup sequence, shared by both link modes.
 *
 * The port has to do three things before main() runs, in this order:
 *
 *   1. map memory at the console's addresses, since everything else writes
 *      through those pointers;
 *   2. publish OSBootInfo into the low memory that just became addressable;
 *   3. mount a disc image over the empty file system that step 2 left.
 *
 * Both link modes have to run all three, and they reach this point
 * differently. A hosted build enters through the C runtime, which walks
 * .init_array; a freestanding one enters at _start with no runtime at all.
 * Keeping the sequence in one function and giving each mode a way to call it
 * is what stops the two from drifting -- which they had, with bootinfo and the
 * disc mount reachable only from the freestanding path.
 */
#include "pc_sys.h"

extern void pc_memory_init(void);
extern void pc_bootinfo_init(void);
extern int pc_dvd_mount(void);
extern void __sinit_trigf_c(void);

void pc_init_all(void)
{
    static int done;
    if (done) {
        return;
    }
    done = 1;

    pc_memory_init();
    pc_bootinfo_init();
    pc_dvd_mount();
    /* MWCC .ctors is empty on the host (SECTION_CTORS expands to nothing).
       The retained MSL trig implementation needs its range-reduction data. */
    __sinit_trigf_c();
}

#if !(defined(__i386__) && defined(PC_FREESTANDING))
/* Hosted: the C runtime runs this on the way to main(). The freestanding
   entry point calls pc_init_all directly instead, because nothing walks
   .init_array there and the link does not even emit its bounds. */
__attribute__((constructor(101))) static void pc_init_ctor(void)
{
    pc_init_all();
}
#endif
