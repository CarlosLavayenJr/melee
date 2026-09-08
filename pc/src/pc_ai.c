/* pc_ai.c — the audio interface, accepted and discarded.
 *
 * AI is the hardware that streams finished samples to the DACs: the game hands
 * it a buffer and it raises an interrupt when that buffer has been consumed,
 * which is the clock the whole audio pipeline runs on. ai.c programs it
 * through __AIRegs, so it does not build off PowerPC.
 *
 * Nothing here plays sound. Every call is accepted and the DMA callback is
 * never invoked, which is the important half: a caller that queued a buffer
 * simply never hears back, rather than blocking on a completion that cannot
 * arrive. That keeps boot moving where a spin would not -- the same failure
 * ar.c had.
 *
 * Real audio is the largest remaining piece of this port and does not start
 * here. AX builds the voice parameter blocks, the DSP microcode mixes them,
 * and AI only moves the result. A host port intercepts at AX, mixes on the
 * CPU, and hands the output to whatever the platform uses for playback -- at
 * which point this file becomes the ring buffer feeding it, and the DMA
 * callback becomes the thing that paces the mixer.
 */
#include "pc_sys.h"

#include <dolphin/ai.h>

static AIDCallback dma_callback;
static AISCallback stream_callback;
static u32 dsp_sample_rate;
static u32 stream_play_state;
static u32 stream_trigger;
static u8 stream_vol_left;
static u8 stream_vol_right;

void AIInit(u8* stack)
{
    (void) stack;
    pc_sys_log("pc_ai: audio interface present, playback disabled\n");
}

AIDCallback AIRegisterDMACallback(AIDCallback callback)
{
    AIDCallback prev = dma_callback;
    dma_callback = callback;
    return prev;
}

/* The buffer is accepted and never consumed. Deliberately no callback: firing
   it here would re-enter the mixer synchronously and recurse. */
void AIInitDMA(u32 start_addr, u32 length)
{
    (void) start_addr;
    (void) length;
}

void AIStartDMA(void) { }
void AIStopDMA(void)  { }

AISCallback AIRegisterStreamCallback(AISCallback callback)
{
    AISCallback prev = stream_callback;
    stream_callback = callback;
    return prev;
}

void AIResetStreamSampleCount(void)     { }
void AISetStreamTrigger(u32 trigger)    { stream_trigger = trigger; }
void AISetStreamPlayState(u32 state)    { stream_play_state = state; }
void AISetDSPSampleRate(u32 rate)       { dsp_sample_rate = rate; }
void AISetStreamVolLeft(u8 vol)         { stream_vol_left = vol; }
void AISetStreamVolRight(u8 vol)        { stream_vol_right = vol; }
