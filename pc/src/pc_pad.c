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
 * Port 0 reads a real keyboard on Windows now -- read_keyboard() below --
 * filling exactly the array this comment used to say nothing filled. Ports
 * 1-3 still report PAD_ERR_NO_CONTROLLER. Wiring a real gamepad (SDL,
 * XInput) is the next step after this one, and changes nothing outside this
 * function either, for the same reason keyboard didn't: this narrow API is
 * the whole surface.
 *
 * The GameCube has four ports. That limit lives here rather than in the game
 * -- pl/player.c iterates six player slots (GM_MAX_PLAYERS), which is why
 * six-player mods exist. A host port choosing to feed more than four
 * controllers would do it in this file.
 */
#include "pc_sys.h"

#include <dolphin/pad.h>
#include "pc_watch.h"

#define PC_PAD_PORTS 4

#ifdef _WIN32
#include <windows.h>

/* A default-with-no-config mapping, not a claim of correctness for
   everyone's hands -- swappable later without touching anything outside
   this function. Melee reads the left stick for movement far more than the
   digital d-pad, so WASD drives stickX/stickY rather than PAD_BUTTON_*. */
static int down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void read_keyboard(PADStatus* status)
{
    u16 button = 0;
    s8 stick_x = 0, stick_y = 0;

    if (down('D')) stick_x = 80;
    else if (down('A')) stick_x = -80;
    if (down('W')) stick_y = 80;
    else if (down('S')) stick_y = -80;

    if (down('J')) button |= PAD_BUTTON_A;
    if (down('K')) button |= PAD_BUTTON_B;
    if (down('L')) button |= PAD_BUTTON_X;
    if (down('I')) button |= PAD_BUTTON_Y;
    if (down('U')) button |= PAD_TRIGGER_Z;
    if (down(VK_SPACE)) button |= PAD_TRIGGER_L;
    if (down(VK_RETURN)) button |= PAD_BUTTON_START;
    if (down(VK_LEFT)) button |= PAD_BUTTON_LEFT;
    if (down(VK_RIGHT)) button |= PAD_BUTTON_RIGHT;
    if (down(VK_UP)) button |= PAD_BUTTON_UP;
    if (down(VK_DOWN)) button |= PAD_BUTTON_DOWN;

    status->button = button;
    status->stickX = stick_x;
    status->stickY = stick_y;
    status->substickX = 0;
    status->substickY = 0;
    status->triggerLeft = down(VK_SPACE) ? 150 : 0;
    status->triggerRight = 0;
    status->analogA = 0;
    status->analogB = 0;
    status->err = PAD_ERR_NONE;
}
#endif

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

    pc_watch_hit(PC_WATCH_PAD);
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
#ifdef _WIN32
    read_keyboard(&status[0]);
    return 1; /* bit 0: port 0's read completed */
#else
    return 0; /* bitmask of ports whose read completed */
#endif
}

void PADSetSamplingRate(unsigned long msec) { (void) msec; }
void PADSetSpec(u32 spec) { (void) spec; }

void PADControlMotor(s32 chan, u32 command)
{
    (void) chan;
    (void) command; /* no rumble on a host build */
}
