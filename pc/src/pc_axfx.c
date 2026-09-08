/* pc_axfx.c — the AX effects chain, present and inert.
 *
 * chorus.c, reverb_hi.c and reverb_std.c are the auxiliary-bus effects: the
 * mixer routes voices through them before the final output. They do not build
 * off PowerPC -- each one is written with paired-single arithmetic and, in
 * places, MWCC's `@` relocation syntax in inline assembly.
 *
 * Initialisation succeeds and the per-buffer callbacks return without touching
 * the audio they were handed. Since nothing is mixing yet (see pc_dsp.c), the
 * buffers passed in are silence, and an effect over silence is silence -- so
 * this is inert rather than wrong.
 *
 * Init returning success matters: a failure here makes the AX driver tear down
 * its aux bus during setup, which changes the paths every later call takes.
 * Better to accept the effect and do nothing with it than to have the mixer
 * configure itself differently from how it would on hardware.
 */
#include "pc_sys.h"

#include <dolphin/axfx.h>

int AXFXChorusInit(struct AXFX_CHORUS* c)         { (void) c; return 0; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c)     { (void) c; return 0; }

int AXFXReverbHiInit(struct AXFX_REVERBHI* rev)     { (void) rev; return 0; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev) { (void) rev; return 0; }

int AXFXReverbStdInit(struct AXFX_REVERBSTD* rev)     { (void) rev; return 0; }
int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* rev) { (void) rev; return 0; }

/* The per-buffer callbacks. Each is handed the aux bus's current buffer and
   would filter it in place; leaving it untouched passes the audio through
   unchanged, which for silence is the correct result. */
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                        struct AXFX_CHORUS* chorus)
{
    (void) bufferUpdate;
    (void) chorus;
}

void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                          struct AXFX_REVERBHI* reverb)
{
    (void) bufferUpdate;
    (void) reverb;
}

void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                           struct AXFX_REVERBSTD* reverb)
{
    (void) bufferUpdate;
    (void) reverb;
}
