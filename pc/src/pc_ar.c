/* pc_ar.c — auxiliary RAM, backed by ordinary host memory.
 *
 * ARAM is 16 MB of slow memory beside main RAM, reached only by DMA. The game
 * parks audio samples and streamed data there. ar.c drives that DMA engine
 * through the DSP registers, so it does not build off PowerPC -- and worse, it
 * hangs: __ARChecksize probes the ARAM size by DMAing patterns to candidate
 * addresses, and it opens with
 *
 *     do { } while (!(__DSPRegs[11] & 1));
 *
 * waiting on a status bit that a mapped page never sets. That was the first
 * stall in this port that was not a crash.
 *
 * Here ARAM is just a buffer, so there is nothing to size and nothing to wait
 * for: the size is declared and DMA is a memcpy. Transfers complete
 * synchronously, which the callers tolerate because the SDK's own interface is
 * already callback-shaped -- ARQ queues requests and is told when each
 * finishes, and it is told immediately.
 *
 * arq.c above this layer builds unmodified and does the queueing, so only what
 * ar.c provided is here.
 */
#include "pc_sys.h"

#include <dolphin/ar.h>
#include "pc_watch.h"

#define PC_ARAM_SIZE (16u << 20) /* retail ARAM */

/* Static rather than arena-allocated: ARInit runs before the heap is set up,
   and the arena is main RAM, which this deliberately is not. */
static unsigned char pc_aram[PC_ARAM_SIZE];

static u32 aram_base;      /* first address the allocator may hand out */
static u32 aram_next;      /* bump pointer */
static u32* stack_index;   /* caller's allocation stack, per ARInit */
static u32 stack_entries;
static int initialized;
static ARQCallback dma_callback;

/* Completing a transfer inline is what recurses. ARQ's service routine reacts
   to a completion by starting the next queued transfer, so calling back from
   inside ARStartDMA re-enters it and the stack grows until it runs out. On
   hardware the callback arrives on an interrupt, after ARStartDMA has already
   returned, so the recursion never forms.
   The outermost ARStartDMA therefore owns the callbacks: nested calls record
   that one is due and return, and the outer frame keeps firing until the queue
   stops producing work. */
static int dma_depth;
static int dma_pending;

u32 ARGetSize(void) { return PC_ARAM_SIZE; }

/* The SDK reserves the low 16 KB for the DSP's own use. */
u32 ARGetBaseAddress(void) { return 0x4000; }

int ARCheckInit(void) { return initialized; }

u32 ARInit(u32* stack_index_addr, u32 num_entries)
{
    if (initialized) {
        return aram_base;
    }
    initialized = 1;
    stack_index = stack_index_addr;
    stack_entries = num_entries;
    aram_base = ARGetBaseAddress();
    aram_next = aram_base;
    pc_sys_log("pc_ar: 16 MB ARAM (host memory)\n");
    return aram_base;
}

void ARReset(void) { initialized = 0; }

void ARSetSize(void) { }

/* A bump allocator, matching how the SDK's own is used: the game takes its
   blocks during startup and does not give them back. ARFree unwinds to a
   recorded point rather than freeing individually, which is the same shape as
   the stack the real implementation keeps. */
u32 ARAlloc(u32 length)
{
    u32 addr = (aram_next + 31u) & ~31u; /* DMA wants 32-byte alignment */
    if (addr + length > PC_ARAM_SIZE) {
        pc_sys_log("pc_ar: out of ARAM\n");
        return 0;
    }
    aram_next = addr + length;
    return addr;
}

u32 ARFree(u32* length)
{
    if (length != NULL) {
        *length = 0;
    }
    aram_next = aram_base;
    return aram_base;
}

ARQCallback ARRegisterDMACallback(ARQCallback callback)
{
    ARQCallback prev = dma_callback;
    dma_callback = callback;
    return prev;
}

/* Always idle: transfers finish inside ARStartDMA. */
u32 ARGetDMAStatus(void)
{
    pc_watch_hit(PC_WATCH_AR);
    return 0;
}

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length)
{
    unsigned char* mram = (unsigned char*) mainmem_addr;
    unsigned char* aram;
    u32 i;

    dma_depth++;
    if (aram_addr + length > PC_ARAM_SIZE) {
        pc_sys_log("pc_ar: DMA past the end of ARAM\n");
        dma_depth--;
        return;
    }
    aram = pc_aram + aram_addr;

    if (type == ARAM_DIR_MRAM_TO_ARAM) {
        for (i = 0; i < length; i++) {
            aram[i] = mram[i];
        }
    } else {
        for (i = 0; i < length; i++) {
            mram[i] = aram[i];
        }
    }

    /* Only record the completion. Calling back from here is what broke: ARQ
       reacts to a completion by starting the next transfer, so an inline call
       re-enters ARStartDMA, and even guarded against recursion it runs while
       ARQPostRequest is still building the queue it is about to walk.
       On hardware the callback arrives on an interrupt, well after ARStartDMA
       returned and its caller finished. pc_ar_poll() below reproduces that
       timing: the frame tick drains completions once the stack is quiet. */
    dma_pending = 1;
    dma_depth--;
}

/* Called from the frame tick, standing in for the DMA completion interrupt.
   Loops because each callback may queue another transfer, which completes
   immediately and needs draining too. */
void pc_ar_poll(void)
{
    int guard = 0;
    while (dma_pending && guard++ < 1024) {
        dma_pending = 0;
        if (dma_callback != NULL) {
            dma_callback(NULL);
        }
    }
}
