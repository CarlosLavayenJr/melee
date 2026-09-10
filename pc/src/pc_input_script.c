/* pc_input_script.c -- scripted controller input, for repeatable tests.
 *
 * This presses buttons. It does not set scene state, skip a scene, or fake
 * anything the game would otherwise compute: every transition it causes
 * happens because the game's own menu code acted on a PADStatus exactly as it
 * would for a person holding a controller. That is what
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
 * scene's own on_frame function every frame. Comparing that pointer identifies
 * the scene exactly and needs no access to gm_1A3F.c's file-static state
 * machine. Driving off that rather than a frame schedule means a slow load
 * cannot desynchronise the script.
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
static unsigned polls;
static unsigned held;
static unsigned press_end;  /* poll at which the button is released */
static unsigned window_end; /* poll at which the next press may start */
static const char* last_reason;
static int seen_menu;
static int last_menu_kind = -1;

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

void __real_gm_801A4D34(void (*on_frame)(void), void* info);
void __wrap_gm_801A4D34(void (*on_frame)(void), void* info)
{
    if (on_frame != current_scene) {
        current_scene = on_frame;
        if (route == ROUTE_VS) {
            log_ptr("pc_input_script: scene on_frame now ", (void*) on_frame);
        }
    }
    __real_gm_801A4D34(on_frame, info);
}

/* A press has to last long enough for a menu to see an edge, then be released
   long enough that the next one is a fresh edge rather than a hold. */
#define PRESS_POLLS 10
#define GAP_POLLS 26

static void press(unsigned button, const char* reason)
{
    held = button;
    press_end = polls + PRESS_POLLS;
    window_end = polls + PRESS_POLLS + GAP_POLLS;
    if (reason != last_reason) {
        last_reason = reason;
        pc_sys_log("pc_input_script: ");
        pc_sys_log(reason);
        pc_sys_log("\n");
    }
}

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
        log_ptr("pc_input_script: title on_frame ", (void*) gm_Scene_Title_OnFrame);
        log_ptr("pc_input_script: menu  on_frame ", (void*) mnMain_Scene_OnFrame);
        log_ptr("pc_input_script: css   on_frame ", (void*) mnCharSel_Scene_OnFrame);
        log_ptr("pc_input_script: sss   on_frame ", (void*) mnStageSel_Scene_OnFrame);
    } else {
        pc_sys_log("pc_input_script: PC_INPUT_SCRIPT names no known route\n");
    }
}

/* The button mask port 0 should report this poll, or 0 for none. */
unsigned pc_input_script_buttons(void)
{
    if (route < 0) {
        read_route();
    }
    if (route != ROUTE_VS) {
        return 0;
    }

    polls++;
    if (polls < press_end) {
        return held; /* still holding */
    }
    if (polls < window_end) {
        return 0; /* released, so the next press reads as a new edge */
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
    } else if (current_scene == mnCharSel_Scene_OnFrame) {
        press(BTN_START, "Start at character select");
    } else if (current_scene == mnStageSel_Scene_OnFrame) {
        press(BTN_A, "choosing a stage");
    } else if (!seen_menu) {
        /* The opening movie and whatever else sits between boot and the title
           all advance on Start, and none of them is a scene this route needs
           to steer precisely -- it just needs to get through them. Once the
           main menu has been reached this stops, so Start is never pressed
           inside a match, where it would pause. */
        press(BTN_START, "Start to advance past the opening");
    }
    /* In a match, or any scene this route does not steer, nothing is pressed. */
    return held;
}
