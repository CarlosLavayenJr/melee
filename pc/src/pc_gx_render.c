/* pc_gx_render.c — the __wrap_GX* implementations that actually draw.
 *
 * Same interception mechanism as pc_gx_trace.c (`ld --wrap`, __real_ still
 * reachable, neither the SDK nor the game modified), and the two are
 * mutually exclusive at link time -- see tools/phase0/linkexe.sh --renderer.
 * pc_vulkan.c owns the window/device/swapchain and knows nothing about GX;
 * this file is the other half, and knows nothing about Win32 or VkDevice
 * beyond the handful of calls pc_vulkan.h exposes. See pc/GX_RENDERER.md for
 * why the split sits here and not lower (the write-gather FIFO) or higher
 * (parsing display lists into GX* calls, which they are not).
 *
 * Milestone 1 (this commit): a window opens, and every real GXCopyDisp
 * clears it to the color the game last set and presents. Nothing the game
 * draws is visible yet -- GXBegin/GXCallDisplayList/GXLoadTexObj/
 * GXSetTevOrder/GXLoadPosMtxImm/GXSetProjection are wrapped already (so
 * later milestones don't need a relink to reach them) but only forward to
 * __real_ for now, same as pc_gx_trace.c did before this file existed.
 */
#include "pc_sys.h"
#include "pc_vulkan.h"

#include <dolphin/gx.h>

static int window_ready;
static VkCommandBuffer frame_cmd = VK_NULL_HANDLE;

/* Lazily opens the Vulkan frame the first draw-shaped call needs one for,
   and reused by every wrapper below rather than each managing its own
   begin/end -- there is exactly one frame in flight, GXInit through
   GXCopyDisp, no matter how many GX* calls happen inside it. */
static VkCommandBuffer ensure_frame(void)
{
    if (!window_ready) {
        return VK_NULL_HANDLE;
    }
    if (frame_cmd == VK_NULL_HANDLE) {
        pc_vulkan_pump_events();
        frame_cmd = pc_vulkan_begin_frame();
    }
    return frame_cmd;
}

GXFifoObj* __real_GXInit(void* base, u32 size);
GXFifoObj* __wrap_GXInit(void* base, u32 size)
{
    if (pc_vulkan_init(640, 528, "Super Smash Bros. Melee (native)") == 0) {
        window_ready = 1;
    } else {
        pc_sys_log("pc_gx_render: window/Vulkan init failed; "
                   "continuing headless\n");
    }
    /* GXInit's real return value is the FIFO object the game holds onto and
       passes back into GXSetCPUFifo etc; nothing here replaces that
       bookkeeping, so the real call still has to run and its return value
       still has to reach the caller. */
    return __real_GXInit(base, size);
}

void __real_GXSetCopyClear(GXColor clear_clr, u32 clear_z);
void __wrap_GXSetCopyClear(GXColor clear_clr, u32 clear_z)
{
    pc_vulkan_set_clear_color((float) clear_clr.r / 255.0f,
                              (float) clear_clr.g / 255.0f,
                              (float) clear_clr.b / 255.0f,
                              (float) clear_clr.a / 255.0f);
    __real_GXSetCopyClear(clear_clr, clear_z);
}

void __real_GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
void __wrap_GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts)
{
    ensure_frame();
    /* Geometry submission is milestone 3 (pc/GX_RENDERER.md); for now the
       frame this call belongs to still opens, clears, and presents, same as
       every other frame, just without this primitive drawn into it. */
    __real_GXBegin(type, vtxfmt, nverts);
}

extern void pc_gx_fifo_exec(VkCommandBuffer cmd, const void* data,
                            u32 nbytes);

void __real_GXCallDisplayList(void* list, u32 nbytes);
void __wrap_GXCallDisplayList(void* list, u32 nbytes)
{
    VkCommandBuffer cmd = ensure_frame();
    pc_gx_fifo_exec(cmd, list, nbytes);
    __real_GXCallDisplayList(list, nbytes);
}

void __real_GXLoadTexObj(GXTexObj* obj, GXTexMapID id);
void __wrap_GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    ensure_frame();
    __real_GXLoadTexObj(obj, id);
}

void __real_GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord,
                          GXTexMapID map, GXChannelID color);
void __wrap_GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord,
                          GXTexMapID map, GXChannelID color)
{
    __real_GXSetTevOrder(stage, coord, map, color);
}

extern void pc_gx_fifo_set_proj_mtx(float m[4][4]);
extern void pc_gx_fifo_set_pos_mtx(float m[3][4]);

void __real_GXSetProjection(f32 mtx[4][4], GXProjectionType type);
void __wrap_GXSetProjection(f32 mtx[4][4], GXProjectionType type)
{
    pc_gx_fifo_set_proj_mtx(mtx);
    __real_GXSetProjection(mtx, type);
}

void __real_GXLoadPosMtxImm(f32 mtx[3][4], u32 id);
void __wrap_GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    pc_gx_fifo_set_pos_mtx(mtx);
    __real_GXLoadPosMtxImm(mtx, id);
}

void __real_GXCopyDisp(void* dest, GXBool clear);
void __wrap_GXCopyDisp(void* dest, GXBool clear)
{
    VkCommandBuffer cmd = ensure_frame();
    if (cmd != VK_NULL_HANDLE) {
        pc_vulkan_end_frame();
    }
    frame_cmd = VK_NULL_HANDLE;
    __real_GXCopyDisp(dest, clear);
}
