/* pc_input_script.c -- scripted controller input, for repeatable tests.
 *
 * This moves a stick and presses buttons. It does not set scene state, skip a
 * scene, or fake anything the game would otherwise compute: every transition
 * it causes happens because the game's own menu code acted on a PADStatus
 * exactly as it would for a person holding a controller. That is what
 * pc/tests/menu_smoke.gdb already does through the debugger; this moves it
 * into the process for one reason, which is speed.
 *
 * A gdb breakpoint on the pad read fires sixty times a second and each hit
 * costs a debugger round trip. That is tolerable for the twenty seconds it
 * takes to reach the main menu and useless for the minutes it takes to reach a
 * match through character and stage select -- written that way, the match
 * script did not finish inside a fifteen-minute timeout. Here the same
 * decisions are free.
 *
 * Off unless PC_INPUT_SCRIPT names a route, so an ordinary run is untouched:
 *
 *   PC_INPUT_SCRIPT=vs   title -> main menu -> VS Mode -> Melee ->
 *                        character select -> stage select -> match
 *
 * Which scene is live comes from gm_801A4D34, which gm_1A3F.c hands the active
 * scene's own on_frame function. It is called once per SCENE, not per frame --
 * the frame loop is inside it -- so it identifies the scene exactly, and its
 * call is exactly the moment a scene begins. Driving off that rather than a
 * frame schedule means a slow load cannot desynchronise the script.
 *
 * The clock is HSD_PadRenewCopyStatus, which lb_80019900 calls under the same
 * lb_80019A30(0) gate that gates on_frame in that loop, one for one. It is
 * also the call that recomputes HSD_PadCopyStatus::trigger, so one tick of
 * this clock is exactly one chance for the menus to see a button edge, and
 * exactly one cursor-think's worth of stick movement.
 */
#include "pc_sys.h"

#include <string.h>

#include <melee/mn/forward.h>
#include <melee/mn/mnmain.h>

/* PADStatus button bits, from dolphin/pad.h. */
#define BTN_DOWN 0x0004
#define BTN_A 0x0100
#define BTN_START 0x1000

/* MenuFlow::hovered_selection is the highlighted ROW of the main menu, not a
   MenuKind. The rows are 1-P Mode, VS. Mode, Trophies, Options, Data, and each
   opens the MenuKind one greater than its index -- confirming row 2 was
   observed opening MENU_KIND_TOY (3), which is how this was pinned down.
   So VS Mode is row 1, not MENU_KIND_VS. */
#define MAIN_MENU_ROW_VS 1

extern MenuFlow mn_804A04F0;

/* The scenes this route passes through. */
void gm_Scene_Title_OnFrame(void);
void mnMain_Scene_OnFrame(void);
void mnCharSel_Scene_OnFrame(void);
void mnStageSel_Scene_OnFrame(void);
void gm_Scene_Vs_OnFrame(void);

typedef void (*scene_fn)(void);
static scene_fn current_scene;

enum { ROUTE_NONE = 0, ROUTE_VS };

static int route = -1;
static unsigned frames;     /* frames since the current scene was entered */
static unsigned held;       /* buttons to report */
static signed char stick_x; /* stick to report */
static signed char stick_y;
static unsigned press_end;  /* frame at which a simple press is released */
static unsigned window_end; /* frame at which the next simple press may start */
static const char* last_reason;
static int seen_menu;
static int last_menu_kind = -1;

static void log_line(const char* reason)
{
    if (reason != NULL && reason != last_reason) {
        last_reason = reason;
        pc_sys_log("pc_input_script: ");
        pc_sys_log(reason);
        pc_sys_log("\n");
    }
}

static void log_ptr(const char* label, const void* p)
{
    static const char hex[] = "0123456789abcdef";
    char buf[11];
    unsigned long v = (unsigned long) p;
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 15];
    }
    buf[10] = '\0';
    pc_sys_log(label);
    pc_sys_log(buf);
    pc_sys_log("\n");
}

/* ------------------------------------------------------------------ *
 * The character select route.
 *
 * Character select is the one screen on this route with no highlighted
 * default: nothing happens until a hand cursor has carried a token onto a
 * portrait, and Start is ignored until two ports hold a character. So the
 * steps below steer the stick, which is all a person has there too.
 *
 * The positions are open loop, which works because the screen gives two
 * absolute references. The cursor is clamped to x in [-35, 26] and y in
 * [-22, 25] (mncharsel.c), so holding a direction long enough parks it on a
 * known edge no matter where it started -- and from there each frame of full
 * deflection moves it exactly 0.0002 * (80*80 - 200) = 1.24 units along that
 * axis, from getStickDelta(). Every move below therefore starts by driving
 * into a corner and then counts frames out of it.
 *
 * The geometry is measured, not guessed -- printed out of a running character
 * select rather than read off the initialisers, because the icon table is
 * loaded from the disc:
 *
 *   icon 13   x -3.40 .. 3.60   y 6.00 .. 13.00   state 2 (selectable)
 *   port 2 player-kind toggle   x -19.40 .. -13.40   y -4.60 .. 0.20
 *
 * The token a cursor is carrying sits at cursor + (2.7, -2.0) every frame
 * (fn_80262648), and it is the token, not the cursor, that is hit-tested
 * against the icon. So the hand is parked at (-2.76, 11.48), which puts the
 * token at (-0.06, 9.48) -- the middle of icon 13, about three units of slack
 * on every side, which is more than two frames' worth of movement.
 *
 * Port 2 needs no character chosen for it. Its toggle cycles player kind, and
 * with no controller in the port the cycle lands on CPU, at which point
 * mncharsel.c picks the CPU a character itself, exactly as it does for a
 * person clicking that box. */
struct css_step {
    signed char sx;
    signed char sy;
    unsigned short button;
    unsigned short frames;
    const char* why;
};

static const struct css_step css_route[] = {
    { -80, -80, 0, 60, "settling the hand into the bottom-left corner" },
    { 0, 0, 0, 4, NULL },
    { 80, 0, 0, 26, "sliding right to the middle column" },
    { 0, 0, 0, 4, NULL },
    { 0, 80, 0, 27, "raising the hand into the character grid" },
    { 0, 0, 0, 6, NULL },
    { 0, 0, BTN_A, 8, "A on the character under the hand" },
    { 0, 0, 0, 12, NULL },
    { 0, -80, 0, 36, "dropping back down to the player panels" },
    { -80, 0, 0, 36, "sliding left to the corner" },
    { 80, 0, 0, 15, "moving across to port 2's panel" },
    { 0, 80, 0, 16, "raising the hand onto port 2's toggle" },
    { 0, 0, 0, 4, NULL },
    { 0, 0, BTN_A, 8, "A on port 2's toggle, to make it a CPU" },
    { 0, 0, 0, 60, "letting the ready check settle" },
    { 0, 0, BTN_START, 10, "Start to begin the match" },
    { 0, 0, 0, 90, NULL },
};

#define CSS_STEPS ((int) (sizeof css_route / sizeof css_route[0]))
/* If Start did not take, retry it rather than replaying the whole route:
   replaying would press A on port 2's toggle again and cycle it back off. */
#define CSS_RETRY_FROM (CSS_STEPS - 2)

static int css_step;
static unsigned css_step_end;

static void css_enter_step(int step)
{
    const struct css_step* s = &css_route[step];
    css_step = step;
    css_step_end = frames + s->frames;
    stick_x = s->sx;
    stick_y = s->sy;
    held = s->button;
    log_line(s->why);
}

static void css_frame(void)
{
    if (frames < css_step_end) {
        return;
    }
    if (css_step + 1 < CSS_STEPS) {
        css_enter_step(css_step + 1);
    } else {
        css_enter_step(CSS_RETRY_FROM);
    }
}

/* ------------------------------------------------------------------ *
 * The simple scenes: one button, pressed long enough to be seen and released
 * long enough that the next one reads as a fresh edge rather than a hold. */
#define PRESS_FRAMES 10
#define GAP_FRAMES 26

static void press(unsigned button, const char* reason)
{
    held = button;
    press_end = frames + PRESS_FRAMES;
    window_end = frames + PRESS_FRAMES + GAP_FRAMES;
    log_line(reason);
}

static void simple_frame(void)
{
    if (frames < press_end) {
        return; /* still holding */
    }
    if (frames < window_end) {
        held = 0; /* released, so the next press reads as a new edge */
        return;
    }
    held = 0;

    if (current_scene == gm_Scene_Title_OnFrame) {
        press(BTN_START, "Start at the title");
    } else if (current_scene == mnMain_Scene_OnFrame) {
        seen_menu = 1;
        if ((int) mn_804A04F0.cur_menu != last_menu_kind) {
            last_menu_kind = (int) mn_804A04F0.cur_menu;
            log_ptr("pc_input_script: menu kind now ",
                    (const void*) (unsigned long) mn_804A04F0.cur_menu);
        }
        if (mn_804A04F0.cur_menu == MENU_KIND_MAIN) {
            if (mn_804A04F0.hovered_selection != MAIN_MENU_ROW_VS) {
                press(BTN_DOWN, "moving the main menu cursor toward VS Mode");
            } else {
                press(BTN_A, "confirming VS Mode");
            }
        } else if (mn_804A04F0.cur_menu == MENU_KIND_VS) {
            press(BTN_A, "confirming Melee in the VS submenu");
        } else {
            /* Some other submenu: confirm whatever is highlighted rather than
               stalling, and let the menu kind log say where it went. */
            press(BTN_A, "confirming in an unexpected submenu");
        }
    } else if (current_scene == mnStageSel_Scene_OnFrame) {
        /* Stage select opens with the cursor on slot 30, which is the random
           stage. mnStageSel_80259C28 takes A or Start on an ordinary stage
           but ONLY Start on slot 30, so Start is the press that works from
           where the screen starts -- A there is the sound of a rejected
           input, which is what a run of this script pressing A produced.
           Start also confirms any ordinary stage, so nothing is lost by
           always using it here. */
        press(BTN_START, "Start on the stage cursor's opening slot (random)");
    } else if (!seen_menu) {
        /* The opening movie and whatever else sits between boot and the title
           all advance on Start, and none of them is a scene this route needs
           to steer precisely -- it just needs to get through them. Once the
           main menu has been reached this stops, so Start is never pressed
           inside a match, where it would pause. */
        press(BTN_START, "Start to advance past the opening");
    }
}

/* ------------------------------------------------------------------ */

static void read_route(void)
{
    char buf[32];
    route = ROUTE_NONE;
    if (!pc_sys_env("PC_INPUT_SCRIPT", buf, sizeof buf)) {
        return;
    }
    if (!strcmp(buf, "vs")) {
        route = ROUTE_VS;
        pc_sys_log("pc_input_script: driving the VS route with synthetic "
                   "controller input; no scene is overridden\n");
        log_ptr("pc_input_script: title on_frame ",
                (void*) gm_Scene_Title_OnFrame);
        log_ptr("pc_input_script: menu  on_frame ", (void*) mnMain_Scene_OnFrame);
        log_ptr("pc_input_script: css   on_frame ",
                (void*) mnCharSel_Scene_OnFrame);
        log_ptr("pc_input_script: sss   on_frame ",
                (void*) mnStageSel_Scene_OnFrame);
    } else {
        pc_sys_log("pc_input_script: PC_INPUT_SCRIPT names no known route\n");
    }
}

/* Called once when a scene starts: the frame loop is inside it. */
void __real_gm_801A4D34(void (*on_frame)(void), void* info);
void __wrap_gm_801A4D34(void (*on_frame)(void), void* info)
{
    if (route < 0) {
        read_route();
    }
    if (route == ROUTE_VS) {
        current_scene = on_frame;
        log_ptr("pc_input_script: scene on_frame now ", (void*) on_frame);
        frames = 0;
        press_end = 0;
        window_end = 0;
        held = 0;
        stick_x = 0;
        stick_y = 0;
        last_reason = NULL;
        if (on_frame == mnCharSel_Scene_OnFrame) {
            css_enter_step(0);
        }
    }
    __real_gm_801A4D34(on_frame, info);
    if (route == ROUTE_VS) {
        current_scene = NULL;
        held = 0;
        stick_x = 0;
        stick_y = 0;
    }
}

/* Called once per game frame, immediately before the frame's on_frame. */
void __real_HSD_PadRenewCopyStatus(void);
void __wrap_HSD_PadRenewCopyStatus(void)
{
    if (route == ROUTE_VS && current_scene != NULL) {
        frames++;
        if (current_scene == mnCharSel_Scene_OnFrame) {
            css_frame();
        } else {
            simple_frame();
        }
    }
    __real_HSD_PadRenewCopyStatus();
}

/* What port 0 should report this poll. Every field is left alone when the
   route is off or has nothing to say, so the keyboard still drives the game
   alongside it. */
void pc_input_script_poll(unsigned* button, signed char* sx, signed char* sy)
{
    if (route != ROUTE_VS) {
        return;
    }
    *button |= held;
    if (stick_x != 0) {
        *sx = stick_x;
    }
    if (stick_y != 0) {
        *sy = stick_y;
    }
}
