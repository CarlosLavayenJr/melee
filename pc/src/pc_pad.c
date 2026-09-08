/* pc_pad.c — controller input.
 *
 * extern/dolphin/src/dolphin/pad/pad.c talks to the serial interface through
 * __SIRegs and cannot run off the console, so it is excluded from host builds
 * and this replaces it. Melee calls exactly eight PAD entry points; all eight
 * are here.
 *
 * PADClamp is not here: PadClamp.c is pure arithmetic over PADStatus, with no
 * hardware in it, and compiles unmodified. It shapes the analog stick into the
 * octagonal gate and will start mattering the moment PADRead reports real
 * sticks.
 *
 * No real input yet. Every port reports PAD_ERR_NO_CONTROLLER, which is the
 * honest answer while nothing is reading a keyboard or gamepad, and it is what
 * the game already handles gracefully on a console with empty ports. Wiring
 * this to SDL is a self-contained next step: fill PADRead's array and the rest
 * of the game needs no changes, because this narrow API is the whole surface.
 *
 * The GameCube has four ports. That limit lives here rather than in the game
 * -- pl/player.c iterates six player slots (GM_MAX_PLAYERS), which is why
 * six-player mods exist. A host port choosing to feed more than four
 * controllers would do it in this file.
 */
#include "pc_sys.h"

#include <dolphin/pad.h>

#define PC_PAD_PORTS 4

/* From pad.h's error codes: no controller present in the port. */
#define PC_PAD_ERR_NO_CONTROLLER -1

BOOL PADInit(void) { return TRUE; }

int PADReset(unsigned long mask)
{
    (void) mask;
    return TRUE;
}

BOOL PADRecalibrate(u32 mask)
{
    (void) mask;
    return TRUE;
}

u32 PADRead(PADStatus* status)
{
    int i;
    for (i = 0; i < PC_PAD_PORTS; i++) {
        status[i].button = 0;
        status[i].stickX = 0;
        status[i].stickY = 0;
        status[i].substickX = 0;
        status[i].substickY = 0;
        status[i].triggerLeft = 0;
        status[i].triggerRight = 0;
        status[i].analogA = 0;
        status[i].analogB = 0;
        status[i].err = PC_PAD_ERR_NO_CONTROLLER;
    }
    return 0; /* bitmask of ports whose read completed */
}

void PADSetSamplingRate(unsigned long msec) { (void) msec; }
void PADSetSpec(u32 spec) { (void) spec; }

void PADControlMotor(s32 chan, u32 command)
{
    (void) chan;
    (void) command; /* no rumble on a host build */
}
