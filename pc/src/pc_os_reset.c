/* pc_os_reset.c — host stand-in for OSReset.c and OSResetSW.c.
 *
 * Both read the processor interface registers directly and drive the console's
 * reset line, so neither builds off PowerPC.
 *
 * Subsystems register a callback to run before a reset; CARDInit is the first
 * to do so during boot. The list is kept faithfully -- ordered by priority,
 * the way the SDK runs it -- so that a host build which later wants an orderly
 * shutdown has the callbacks in the right sequence. Nothing invokes them yet
 * because nothing here resets.
 */
#include "pc_sys.h"

#include <dolphin/os.h>
#include <dolphin/os/OSReset.h>
#include <dolphin/os/OSResetSW.h>
#include <dolphin/db.h>

static OSResetFunctionInfo* reset_head;

void OSRegisterResetFunction(OSResetFunctionInfo* info)
{
    OSResetFunctionInfo* p;
    OSResetFunctionInfo* prev = NULL;

    for (p = reset_head; p != NULL; p = p->next) {
        if (p == info) {
            return; /* already registered */
        }
        if (p->priority > info->priority) {
            break;
        }
        prev = p;
    }

    info->prev = prev;
    info->next = p;
    if (prev != NULL) {
        prev->next = info;
    } else {
        reset_head = info;
    }
    if (p != NULL) {
        p->prev = info;
    }
}

void OSUnregisterResetFunction(OSResetFunctionInfo* info)
{
    if (info->prev != NULL) {
        info->prev->next = info->next;
    } else if (reset_head == info) {
        reset_head = info->next;
    }
    if (info->next != NULL) {
        info->next->prev = info->prev;
    }
    info->prev = NULL;
    info->next = NULL;
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu)
{
    (void) reset;
    (void) resetCode;
    (void) forceMenu;
    pc_sys_log("pc_os: OSResetSystem — exiting\n");
    pc_sys_exit(0);
}

/* The physical reset button, which a host build does not have. */
BOOL OSGetResetSwitchState(void) { return FALSE; }

/* db.c reads the debugger's presence flag out of low memory, which the IPL
   writes and nothing here does. There is no PowerPC debugger attached, so the
   answer is fixed. */
BOOL DBIsDebuggerPresent(void) { return FALSE; }
