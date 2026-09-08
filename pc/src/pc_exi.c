/* pc_exi.c — the external interface and serial bus, reported as idle.
 *
 * EXI is the console's peripheral bus: memory cards, the modem and broadband
 * adapters, the real-time clock and SRAM all hang off it. SI is the serial
 * interface the controllers speak over. Both are register-level drivers over
 * hardware a host does not have, so neither builds.
 *
 * Everything here reports "nothing attached, nothing in flight". Probing finds
 * no device, transfers succeed having moved nothing, and locks are always
 * available. That keeps callers on the paths a console with empty slots takes,
 * rather than sending them into transfer state machines with no hardware to
 * complete them -- which would hang rather than fail.
 *
 * pc_card.c already answers the memory card at the CARD layer, above this one.
 * A port that wants real saves has a choice of altitude: back CARD with a file,
 * or emulate a card down here at the bus. The former is far less work and does
 * not require being bug-compatible with a serial protocol.
 */
#include "pc_sys.h"

#include <dolphin/exi.h>

/* Controllers are polled through SI; the sampling rate follows the video mode,
   and __VIRetraceHandler refreshes it every frame. With no serial hardware
   there is no rate to program. */
void SIRefreshSamplingRate(void) { }

EXICallback EXISetExiCallback(s32 channel, EXICallback callback)
{
    (void) channel; (void) callback;
    return NULL;
}

BOOL EXILock(s32 channel, u32 device, EXICallback callback)
{
    (void) channel; (void) device; (void) callback;
    return TRUE; /* the bus is never contended here */
}

BOOL EXIUnlock(s32 channel) { (void) channel; return TRUE; }

BOOL EXISelect(s32 channel, u32 device, u32 frequency)
{
    (void) channel; (void) device; (void) frequency;
    return TRUE;
}

BOOL EXIDeselect(s32 channel) { (void) channel; return TRUE; }

/* Immediate and DMA transfers "complete" without moving data. The buffer is
   left untouched, so a caller reading a device gets whatever it started with
   -- zeros, from the mapped page -- which reads as an absent or blank device
   rather than as garbage. */
BOOL EXIImm(s32 channel, void* buffer, s32 length, u32 type,
            EXICallback callback)
{
    (void) channel; (void) buffer; (void) length; (void) type; (void) callback;
    return TRUE;
}

BOOL EXIImmEx(s32 channel, void* buffer, s32 length, u32 type)
{
    (void) channel; (void) buffer; (void) length; (void) type;
    return TRUE;
}

BOOL EXIDma(s32 channel, void* buffer, s32 length, u32 type,
            EXICallback callback)
{
    (void) channel; (void) buffer; (void) length; (void) type; (void) callback;
    return TRUE;
}

BOOL EXISync(s32 channel) { (void) channel; return TRUE; }

BOOL EXIProbe(s32 channel) { (void) channel; return FALSE; }

s32 EXIProbeEx(s32 channel) { (void) channel; return -1; }

BOOL EXIAttach(s32 channel, EXICallback callback)
{
    (void) channel; (void) callback;
    return FALSE;
}

BOOL EXIDetach(s32 channel) { (void) channel; return TRUE; }

s32 EXIGetID(s32 channel, u32 device, u32* id)
{
    (void) channel; (void) device;
    if (id != NULL) {
        *id = 0;
    }
    return 0; /* no device id to report */
}
