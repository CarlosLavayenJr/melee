/* pc_vulkan.h — window and Vulkan device/swapchain, no GX knowledge.
 *
 * pc_gx_render.c calls this to get a command buffer to record into and a
 * place to present to. It does not know what a GXTevStage is. See
 * pc/GX_RENDERER.md for why this split exists.
 */
#ifndef PC_VULKAN_H
#define PC_VULKAN_H

#include <vulkan/vulkan.h>

/* Creates the window, the Vulkan instance/device, and the swapchain. Returns
   0 on success. Safe to call once, at GXInit. */
int pc_vulkan_init(unsigned width, unsigned height, const char* title);

/* Drains the Win32 message queue. Call every frame (pc_vi_tick) so the
   window stays responsive even while the game is busy between frames. */
void pc_vulkan_pump_events(void);

/* True once the user has closed the window (WM_CLOSE/WM_DESTROY seen). */
int pc_vulkan_should_close(void);

/* Acquires the next swapchain image and returns a command buffer already in
   the recording state, with a render pass already begun against that
   image -- cleared to the color GXSetCopyClear last set, or black before the
   first call. Returns VK_NULL_HANDLE if the swapchain needs to be rebuilt
   (window resized) and could not be acquired this frame; callers should
   just skip drawing and try again next frame. */
VkCommandBuffer pc_vulkan_begin_frame(void);

/* Ends the render pass and command buffer opened by pc_vulkan_begin_frame,
   submits it, and presents. No-op if begin_frame returned VK_NULL_HANDLE
   this frame. */
void pc_vulkan_end_frame(void);

/* State pc_gx_render.c needs to build pipelines against: the render pass
   frames are drawn into, and the current swapchain extent. */
VkDevice pc_vulkan_device(void);
VkPhysicalDevice pc_vulkan_physical_device(void);
/* Uploads share this queue on the renderer thread only. */
VkQueue pc_vulkan_graphics_queue(void);
unsigned pc_vulkan_graphics_family(void);
VkRenderPass pc_vulkan_render_pass(void);
void pc_vulkan_extent(unsigned* width, unsigned* height);

/* GXSetCopyClear's color, applied at the next begin_frame. */
void pc_vulkan_set_clear_color(float r, float g, float b, float a);

#endif /* PC_VULKAN_H */
