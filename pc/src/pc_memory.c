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
 * game already uses. The console's 24MB of main RAM appears twice in its
 * address space: cached at 0x80000000 and uncached at 0xC0000000. Both are
 * mapped here so OSCachedToUncached() arithmetic stays valid.
 *
 * The two views are genuinely the same RAM on hardware. This maps them as two
 * independent regions, which is fine while nothing relies on a write through
 * one view being visible through the other. Sharing them properly needs a
 * single memfd mapped twice; do that when something depends on it.
 */
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>

#define GC_RAM_CACHED   0x80000000UL
#define GC_RAM_UNCACHED 0xC0000000UL
#define GC_RAM_SIZE     (24u << 20) /* retail GameCube main RAM */

static int map_fixed(unsigned long at, unsigned long size, const char* what)
{
    void* p = mmap((void*) at, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || (unsigned long) p != at) {
        fprintf(stderr, "pc_memory: could not map %s at 0x%lx\n", what, at);
        return 0;
    }
    /* No memset here: MAP_ANONYMOUS pages arrive zero-filled from the kernel,
       and calling memset would bind to the decomp's own MSL implementation
       (__fill_mem), which is linked into this binary and shadows the host's. */
    return 1;
}

/* Runs before main(). The game's own main() is the entry point, so there is
   no earlier hook to use. */
__attribute__((constructor(101))) static void pc_memory_init(void)
{
    if (!map_fixed(GC_RAM_CACHED, GC_RAM_SIZE, "cached RAM")) return;
    if (!map_fixed(GC_RAM_UNCACHED, GC_RAM_SIZE, "uncached RAM")) return;
    fprintf(stderr, "pc_memory: mapped %u MB at 0x%lx and 0x%lx\n",
            GC_RAM_SIZE >> 20, GC_RAM_CACHED, GC_RAM_UNCACHED);
}
