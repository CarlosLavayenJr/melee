#ifndef PC_GX_TEXTURE_H
#define PC_GX_TEXTURE_H
#include <stddef.h>
#include <vulkan/vulkan.h>

typedef struct pc_gx_texture_binding {
    VkDescriptorImageInfo descriptor;
    VkImage image;
    unsigned width, height, levels;
} pc_gx_texture_binding;

/* Bind a GXTexObj (the current 32-byte console ABI) to one of eight slots.
   Texture bytes remain big-endian in mapped cached GameCube RAM. */
int pc_gx_texture_load_obj(const void* obj, unsigned slot);

/* Lower-level entry for synthetic tests and future texture producers.
   Mips are consecutively stored complete GX tiles, even for 1x1 levels.
   Sampler configuration must have pNext=NULL and anisotropy disabled. */
int pc_gx_texture_load(unsigned slot, unsigned format, unsigned width,
                       unsigned height, unsigned levels, const void* source,
                       size_t source_size, const VkSamplerCreateInfo* sampler);

/* Copy into a NEW/immutable descriptor set for each draw (do not overwrite
   sets used by earlier recorded draws). Returns 0 for an unbound slot.
   Returned resources survive the current frame. */
int pc_gx_texture_get(unsigned slot, pc_gx_texture_binding* binding);

/* Call only AFTER the previous frame fence completed. Preserves bound
   slots across frames; frees resources no longer bound. */
void pc_gx_textures_begin_frame(void);
/* Call while device still exists, after all frame work has completed. */
void pc_gx_textures_shutdown(void);
#endif
