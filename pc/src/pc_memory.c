/* pc_memory.c — put host memory where the game expects to find it.
 *
 * Melee addresses memory by absolute GameCube addresses. os.h defines
 *
 *     #define OSPhysicalToCached(paddr) ((void*)(OS_BASE_CACHED + (u32)(paddr)))
 *
 * with OS_BASE_CACHED == 0x80000000, so `OSPhysicalToCached(0)` is not an
 * offset into anything we allocate -- it is the literal pointer 0x80000000.
 * OSInit dereferences it on its second line to find OSBootInfo.
 *
 * Rather than rewrite every such site, we map real pages at the addresses the
 * game already uses. Main RAM appears twice in the console's address space,
 * cached at 0x80000000 and uncached at 0xC0000000, and both are mapped so
 * OSCachedToUncached() arithmetic stays valid. The two views are the same RAM
 * on hardware; here they are independent regions, which holds while nothing
 * depends on a write through one being visible through the other. Sharing them
 * properly needs one memfd mapped twice.
 *
 * The hardware register range at 0xCC000000 is mapped too. hw_regs.h places
 * the video, processor interface, memory controller, DSP, disc, serial, EXI
 * and audio blocks between 0xCC002000 and 0xCC006C00, and VIInit reads
 * __VIRegs[1] almost immediately. Ordinary pages only stop the fault: reads
 * return the last value written rather than device state, so code polling a
 * status bit will spin rather than crash. Devices get intercepted individually
 * as boot reaches them.
 */
#include "pc_sys.h"

#define GC_RAM_CACHED   0x80000000UL
#define GC_RAM_UNCACHED 0xC0000000UL
#define GC_RAM_SIZE     (24u << 20) /* retail GameCube main RAM */

#define GC_MMIO_BASE 0xCC000000UL
#define GC_MMIO_SIZE (64u << 10)

static int map_or_report(unsigned long at, unsigned long size, const char* what)
{
    if (pc_sys_map_fixed(at, size)) {
        return 1;
    }
    pc_sys_log("pc_memory: could not map ");
    pc_sys_log(what);
    pc_sys_log("\n");
    return 0;
}

/* Runs before main(). The game owns main(), so there is no earlier hook. */
__attribute__((constructor(101))) void pc_memory_init(void)
{
    if (!map_or_report(GC_RAM_CACHED, GC_RAM_SIZE, "cached RAM")) return;
    if (!map_or_report(GC_RAM_UNCACHED, GC_RAM_SIZE, "uncached RAM")) return;
    if (!map_or_report(GC_MMIO_BASE, GC_MMIO_SIZE, "hardware registers")) return;
    pc_sys_log("pc_memory: mapped 24 MB RAM and 64 KB MMIO\n");
}
