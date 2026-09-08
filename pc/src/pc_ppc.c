/* pc_ppc.c — the PowerPC machine operations, answered for a host.
 *
 * These come from the Gekko intrinsics header and from mtx/cache sources that
 * are pure assembly. They fall into three groups, and all three collapse on
 * x86 for reasons worth stating rather than assuming.
 *
 * Cache maintenance (DC*, LC*). The GameCube's CPU and GPU do not share a
 * coherent view of memory, so the SDK flushes a range before the GPU reads it
 * and invalidates one after a DMA writes it. x86 hardware keeps caches
 * coherent, so every one of these is correctly empty here -- not "stubbed out
 * for now" but genuinely unnecessary.
 *
 * The locked cache (LC*) is different in kind: it is 16 KB of L1 the console
 * can address directly as fast scratch. Nothing on x86 corresponds to it. The
 * calls succeed and the memory the game uses is ordinary RAM, which is slower
 * than the hardware intended and otherwise behaves the same.
 *
 * Special-purpose registers, the PPCMf and PPCMt families, read and write
 * machine state --
 * the machine state register, the performance monitor counters, the write-gather
 * pipe address. A host has none of it. Reads return zero and writes are
 * discarded, which is safe because the SDK uses these to configure hardware
 * the port has already replaced.
 */
#include "pc_sys.h"

typedef unsigned int u32;

/* --- cache maintenance: no-ops on a coherent architecture --- */
void DCFlushRange(void* addr, u32 nBytes)        { (void) addr; (void) nBytes; }
void DCFlushRangeNoSync(void* addr, u32 nBytes)  { (void) addr; (void) nBytes; }
void DCInvalidateRange(void* addr, u32 nBytes)   { (void) addr; (void) nBytes; }
void DCStoreRange(void* addr, u32 nBytes)        { (void) addr; (void) nBytes; }
void DCStoreRangeNoSync(void* addr, u32 nBytes)  { (void) addr; (void) nBytes; }
void DCTouchRange(void* addr, u32 nBytes)        { (void) addr; (void) nBytes; }
void ICInvalidateRange(void* addr, u32 nBytes)   { (void) addr; (void) nBytes; }
void DCEnable(void)  { }
void DCDisable(void) { }
void ICEnable(void)  { }
void ICDisable(void) { }

/* DCZeroRange must actually zero: callers rely on it to clear a buffer, not
   merely to hint at the cache. */
void DCZeroRange(void* addr, u32 nBytes)
{
    unsigned char* p = (unsigned char*) addr;
    u32 i;
    for (i = 0; i < nBytes; i++) {
        p[i] = 0;
    }
}

/* --- locked cache: no host equivalent --- */
void LCEnable(void)  { }
void LCDisable(void) { }
void LCLoadBlocks(void* dest, void* src, u32 numBlocks)
{
    (void) dest; (void) src; (void) numBlocks;
}
void LCStoreBlocks(void* dest, void* src, u32 numBlocks)
{
    (void) dest; (void) src; (void) numBlocks;
}
u32 LCLoadData(void* dest, void* src, u32 nBytes)
{
    (void) dest; (void) src;
    return nBytes;
}
u32 LCStoreData(void* dest, void* src, u32 nBytes)
{
    (void) dest; (void) src;
    return nBytes;
}
void LCQueueWait(u32 len) { (void) len; }
void LCFlushQueue(void)   { }

/* --- special-purpose registers --- */
u32 PPCMfmsr(void)          { return 0; }
void PPCMtmsr(u32 newMSR)   { (void) newMSR; }
u32 PPCMfhid0(void)         { return 0; }
void PPCMthid0(u32 newHID0) { (void) newHID0; }
u32 PPCMfhid2(void)         { return 0; }
void PPCMthid2(u32 newHID2) { (void) newHID2; }
u32 PPCMfwpar(void)         { return 0; }
void PPCMtwpar(u32 newWPAR) { (void) newWPAR; }
u32 PPCMfl2cr(void)         { return 0; }
void PPCMtl2cr(u32 newL2cr) { (void) newL2cr; }

/* Performance monitor: counters that do not exist read as zero. */
u32 PPCMfmmcr0(void)          { return 0; }
void PPCMtmmcr0(u32 newMMCR0) { (void) newMMCR0; }
u32 PPCMfmmcr1(void)          { return 0; }
void PPCMtmmcr1(u32 newMMCR1) { (void) newMMCR1; }
u32 PPCMfpmc1(void)           { return 0; }
void PPCMtpmc1(u32 newPMC1)   { (void) newPMC1; }
u32 PPCMfpmc2(void)           { return 0; }
void PPCMtpmc2(u32 newPMC2)   { (void) newPMC2; }
u32 PPCMfpmc3(void)           { return 0; }
void PPCMtpmc3(u32 newPMC3)   { (void) newPMC3; }
u32 PPCMfpmc4(void)           { return 0; }
void PPCMtpmc4(u32 newPMC4)   { (void) newPMC4; }

void PPCSync(void)  { __sync_synchronize(); }
void PPCHalt(void)
{
    pc_sys_log("pc_ppc: PPCHalt\n");
    pc_sys_exit(1);
}

/* --- MWCC intrinsics ---
 *
 * MWCC exposes a few PowerPC instructions as functions. GCC has equivalents
 * for the useful ones and no need for the rest.
 */

/* Count leading zeros of a 32-bit word. GCC's builtin is undefined at zero,
   which the instruction is not -- it answers 32 -- so guard it. */
int __cntlzw(u32 val) { return val == 0 ? 32 : __builtin_clz(val); }

/* Zero a 32-byte cache line without first reading it from memory. The saving
   is the avoided read; the observable effect is just the zeroing. */
void __dcbz(void* addr, int offset)
{
    unsigned char* p = (unsigned char*) addr + offset;
    int i;
    for (i = 0; i < 32; i++) {
        p[i] = 0;
    }
}

/* A full memory barrier. x86's store ordering makes this nearly free, but the
   compiler barrier still matters: GX writes into the FIFO must not be
   reordered around it. */
void __sync(void) { __sync_synchronize(); }
