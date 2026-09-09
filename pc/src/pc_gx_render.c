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
#include "pc_gx_texture.h"
#include "pc_texture_decode.h"

#include <dolphin/gx.h>
#include <stdint.h>
#include <string.h>

/* Preserve native image pointers before GX packs them into 21 bits. Only
   known bounded sources (mapped RAM or the runtime-loaded font) are accepted. */
static struct { GXTexObj* obj; const void* source; size_t size; u32 image; } native_sources[1024];
void __real_GXInitTexObj(GXTexObj*, void*, u16, u16, GXTexFmt, GXTexWrapMode, GXTexWrapMode, GXBool);
void __wrap_GXInitTexObj(GXTexObj* obj, void* source, u16 w, u16 h, GXTexFmt fmt,
                        GXTexWrapMode s, GXTexWrapMode t, GXBool mip)
{
    extern unsigned char HSD_SisLib_FontAtlas[0x23e00];
    uintptr_t p = (uintptr_t)source, font = (uintptr_t)HSD_SisLib_FontAtlas;
    unsigned i, empty = 1024;
    size_t size = 0;
    __real_GXInitTexObj(obj, source, w, h, fmt, s, t, mip);
    if (p >= font && p - font < 0x23e00) size = 0x23e00 - (p - font);
    else if (p >= 0x80000000u && p < 0x81800000u) size = 0x81800000u - p;
    for (i = 0; i < 1024; ++i) {
        if (native_sources[i].obj == obj) break;
        if (!native_sources[i].obj && empty == 1024) empty = i;
    }
    if (i == 1024) i = empty;
    if (i == 1024) { pc_sys_log("pc_gx_render: native texture registry exhausted\n"); pc_sys_exit(1); }
    native_sources[i].obj = obj; native_sources[i].source = source;
    native_sources[i].size = size;
    memcpy(&native_sources[i].image, (char*)obj + 12, 4);
}

static int window_ready;
static VkCommandBuffer frame_cmd = VK_NULL_HANDLE;
extern void pc_gx_fifo_begin_frame(void);
extern void pc_gx_immediate_begin(VkCommandBuffer cmd, unsigned type,
                                  unsigned fmt, unsigned count);

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
        if (frame_cmd != VK_NULL_HANDLE) {
            /* begin_frame has waited for the previous frame's fence. */
            pc_gx_textures_begin_frame();
            pc_gx_fifo_begin_frame();
        }
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
    VkCommandBuffer cmd = ensure_frame();
    __real_GXBegin(type, vtxfmt, nverts);
    pc_gx_immediate_begin(cmd, (unsigned)type, (unsigned)vtxfmt, nverts);
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
    _Static_assert(sizeof(GXTexObj) == 32, "texture bridge requires 32-bit GX ABI");
    if (ensure_frame() != VK_NULL_HANDLE) {
        unsigned i; u32 image;
        memcpy(&image, (char*)obj + 12, 4);
        for (i = 0; i < 1024; ++i) if (native_sources[i].obj == obj &&
            (native_sources[i].image & 0x1fffff) == (image & 0x1fffff)) break;
        if (i < 1024) {
            if (!native_sources[i].size) {
                u32 words[8];
                memcpy(words, obj, sizeof words);
                if (pc_texture_source_size(words[5], (words[2]&1023)+1, ((words[2]>>10)&1023)+1)) {
                    pc_sys_log("pc_gx_render: unknown native texture source\n"); pc_sys_exit(1);
                }
                /* Unsupported formats are rejected before any source read;
                   preserve that precise diagnostic (e.g. native GX_TF_Z8). */
            }
            pc_gx_texture_load_obj_source(obj, (unsigned)id, native_sources[i].source, native_sources[i].size);
        } else pc_gx_texture_load_obj(obj, (unsigned)id);
    }
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

/* The PE finish interrupt may be delivered only after submitted GPU work
   completes. A mid-frame GXDrawDone also flushes the current command buffer. */
void pc_gx_render_finish(void)
{
    if (!window_ready) return;
    if (frame_cmd != VK_NULL_HANDLE) {
        pc_vulkan_end_frame();
        frame_cmd = VK_NULL_HANDLE;
    }
    if (vkQueueWaitIdle(pc_vulkan_graphics_queue()) != VK_SUCCESS) {
        pc_sys_log("pc_gx_render: GPU completion failed\n");
        pc_sys_exit(1);
    }
}
