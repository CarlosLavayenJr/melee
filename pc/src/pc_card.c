/* pc_card.c — memory card, reported as absent.
 *
 * CARDMount.c, CARDRdwr.c and CARDStatEx.c drive the EXI bus directly and do
 * not build off PowerPC. The other fourteen CARD sources are pure logic over
 * the card's directory and block structures and compile unmodified, so only
 * the handful of entry points those three provided are needed here.
 *
 * Every one reports CARD_RESULT_NOCARD. That is a real state the game already
 * copes with -- a console with an empty slot -- so it needs no special
 * handling anywhere else, and it is honest: there is no card, and pretending
 * otherwise would send the save code down paths with nothing behind them.
 *
 * Backing saves with a file on disk is a self-contained later step. The
 * fourteen compiled sources already implement the card's on-disk format, so
 * that work is mostly giving __CARDRead and __CARDWrite somewhere to land.
 */
#include "pc_sys.h"

#include <dolphin/card.h>

int CARDProbe(long chan)
{
    (void) chan;
    return FALSE; /* no card detected in the slot */
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    (void) chan;
    if (memSize != NULL) {
        *memSize = 0;
    }
    if (sectorSize != NULL) {
        *sectorSize = 0;
    }
    return CARD_RESULT_NOCARD;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback)
{
    (void) chan; (void) workArea; (void) detachCallback; (void) attachCallback;
    return CARD_RESULT_NOCARD;
}

s32 CARDUnmount(s32 chan)
{
    (void) chan;
    return CARD_RESULT_NOCARD;
}

void __CARDMountCallback(s32 chan, s32 result)
{
    (void) chan;
    (void) result;
}

long CARDGetXferredBytes(long chan)
{
    (void) chan;
    return 0;
}

long __CARDRead(long chan, unsigned long addr, long length, void* dst,
                void (*callback)(long, long))
{
    (void) chan; (void) addr; (void) length; (void) dst; (void) callback;
    return CARD_RESULT_NOCARD;
}

long __CARDWrite(long chan, unsigned long addr, long length, void* src,
                 void (*callback)(long, long))
{
    (void) chan; (void) addr; (void) length; (void) src; (void) callback;
    return CARD_RESULT_NOCARD;
}
