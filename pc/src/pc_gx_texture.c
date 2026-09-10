/* Synchronous uploads on the existing graphics queue, using a separate
   command pool/buffer. GXLoadTexObj may occur INSIDE an active render pass:
   transfer commands must never be recorded into that pass. Uploaded images
   are immutable; rebinding never destroys images referenced by earlier draws.
   This first implementation favours correctness over upload throughput. */
#include "pc_gx_texture.h"
#include "pc_texture_decode.h"
#include "pc_vulkan.h"
#include "pc_sys.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_TEXTURES 1024
#define MAX_TEXTURE_BYTES (64u * 1024u * 1024u)
#define MAX_MIPS 11

typedef struct texture {
    pc_gx_texture_binding binding;
    VkDeviceMemory memory;
    VkSamplerCreateInfo sampler_key;
    unsigned char* pixels;
    size_t bytes;
    VkDeviceSize allocation_bytes;
    unsigned long long last_used;
    int used_this_frame;
} texture;
static texture* cache[MAX_TEXTURES];
static texture* slots[8];
static size_t cached_bytes;
static VkDeviceSize allocated_bytes;
static VkCommandPool upload_pool;
static unsigned long long texture_loads, texture_uploads, texture_hits;
static unsigned long long use_serial, texture_evictions;

static void report(const char* reason);

/* Palettes, recorded as the game loads them. A CI texture names a TLUT by
   number in its GXTexObj; the palette itself lives wherever GXInitTlutObj was
   pointed.
 *
 * The two SDK prototypes are restated here in plain types rather than pulled
 * in from <dolphin/gx.h>, and the wrapper is guarded, because this file is
 * also compiled standalone by pc/tests/run_texture_tests.ps1 with only
 * -I pc/src and no game headers. GXTlutFmt is an enum, hence unsigned*.
 * Decoding the TLUT object through GXGetTlutObjAll rather than reaching into
 * its bitfields keeps the layout knowledge in the SDK where it belongs. */
#define MAX_TLUTS 32
static struct tlut_entry {
    unsigned name, entries, format;
    const unsigned char* data;
    int used;
} tluts[MAX_TLUTS];

static const struct tlut_entry* find_tlut(unsigned name)
{
    unsigned i;
    for (i = 0; i < MAX_TLUTS; ++i)
        if (tluts[i].used && tluts[i].name == name) return &tluts[i];
    return NULL;
}

#ifdef PC_GX_RENDERER
void GXGetTlutObjAll(const void* tlut_obj, void** data, unsigned* format,
                     unsigned short* numEntries);
void __real_GXLoadTlut(void* obj, unsigned name);
void __wrap_GXLoadTlut(void* obj, unsigned name)
{
    void* data = NULL;
    unsigned fmt = 0;
    unsigned short entries = 0;
    unsigned i, slot = MAX_TLUTS;

    __real_GXLoadTlut(obj, name);
    if (!obj) return;
    GXGetTlutObjAll(obj, &data, &fmt, &entries);
    if (!entries) return;

    for (i = 0; i < MAX_TLUTS; ++i) {
        if (tluts[i].used && tluts[i].name == name) { slot = i; break; }
        if (!tluts[i].used && slot == MAX_TLUTS) slot = i;
    }
    if (slot == MAX_TLUTS) { report("more distinct TLUTs than expected"); return; }
    tluts[slot].used = 1;
    tluts[slot].name = name;
    tluts[slot].entries = entries;
    tluts[slot].format = fmt;
    /* GXGetTlutObjAll returns the physical address the register holds. */
    tluts[slot].data = (const unsigned char*)
        (uintptr_t) (((unsigned) (uintptr_t) data & 0x1FFFFFFFu) | 0x80000000u);
}
#endif

static unsigned unsupported_format_counts[16];

/* GX texture format codes, for the tally below. */
static const char* const format_names[16] = {
    "I4", "I8", "IA4", "IA8", "RGB565", "RGB5A3", "RGBA8", "fmt7",
    "CI4", "CI8", "CI14X2", "fmt11", "fmt12", "fmt13", "CMPR", "fmt15"
};

void pc_gx_texture_report(void)
{
    unsigned i;
    char buf[192];
    snprintf(buf, sizeof buf,
             "pc_gx_texture: loads=%llu hits=%llu uploads=%llu evictions=%llu host_bytes=%lu gpu_bytes=%llu\n",
             texture_loads, texture_hits, texture_uploads, texture_evictions,
             (unsigned long)cached_bytes, (unsigned long long)allocated_bytes);
    pc_sys_log(buf);
    for (i = 0; i < 16; ++i) {
        if (!unsupported_format_counts[i]) continue;
        {
            char buf[11];
            unsigned v = unsupported_format_counts[i];
            int n = (int) sizeof buf - 1;
            buf[n] = '\0';
            do { buf[--n] = (char) ('0' + v % 10); v /= 10; } while (v && n > 0);
            pc_sys_log("pc_gx_texture: ");
            pc_sys_log(buf + n);
            pc_sys_log(" x unsupported format ");
            pc_sys_log(format_names[i]);
            pc_sys_log("\n");
        }
    }
}

static void report(const char* reason)
{
    /* Distinct diagnostics once each; a missing font can hit this thousands
       of times a second while the menu continues to run. */
    static const char* seen[64];
    static unsigned count;
    unsigned i;
    for (i = 0; i < count; ++i) if (!strcmp(seen[i], reason)) return;
    if (count < 64) seen[count++] = reason;
    pc_sys_log("pc_gx_texture: "); pc_sys_log(reason); pc_sys_log("\n");
}

static int memory_type(unsigned bits, VkMemoryPropertyFlags flags, unsigned* out)
{
    VkPhysicalDeviceMemoryProperties p;
    unsigned i;
    vkGetPhysicalDeviceMemoryProperties(pc_vulkan_physical_device(), &p);
    for (i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) {
            *out = i; return 0;
        }
    return -1;
}

static void destroy(texture* t)
{
    VkDevice dev = pc_vulkan_device();
    if (!t) return;
    if (t->binding.descriptor.sampler) vkDestroySampler(dev, t->binding.descriptor.sampler, NULL);
    if (t->binding.descriptor.imageView) vkDestroyImageView(dev, t->binding.descriptor.imageView, NULL);
    if (t->binding.image) vkDestroyImage(dev, t->binding.image, NULL);
    if (t->memory) vkFreeMemory(dev, t->memory, NULL);
    free(t->pixels); free(t);
}

static void touch(texture* t)
{
    t->used_this_frame = 1;
    t->last_used = ++use_serial;
}

/* Only completed, unbound resources may be retired. A texture can have been
   unbound since a previous draw in this frame; that draw still needs it. */
static int evict_one(void)
{
    unsigned i, s, oldest = MAX_TEXTURES;
    for (i = 0; i < MAX_TEXTURES; ++i) {
        if (!cache[i] || cache[i]->used_this_frame) continue;
        for (s = 0; s < 8 && slots[s] != cache[i]; ++s) {}
        if (s != 8) continue;
        if (oldest == MAX_TEXTURES || cache[i]->last_used < cache[oldest]->last_used)
            oldest = i;
    }
    if (oldest == MAX_TEXTURES) return 0;
    cached_bytes -= cache[oldest]->bytes;
    allocated_bytes -= cache[oldest]->allocation_bytes;
    destroy(cache[oldest]); cache[oldest] = NULL;
    ++texture_evictions;
    return 1;
}

void pc_gx_textures_begin_frame(void)
{
    unsigned i;
    /* The caller has waited for the previous frame's fence. Keep immutable
       images cached until memory/entry pressure, instead of uploading the
       same menu textures again on every frame. */
    for (i = 0; i < MAX_TEXTURES; ++i)
        if (cache[i]) cache[i]->used_this_frame = 0;
}

void pc_gx_textures_shutdown(void)
{
    unsigned i;
    memset(slots, 0, sizeof slots);
    for (i = 0; i < MAX_TEXTURES; ++i) {
        destroy(cache[i]); cache[i] = NULL;
    }
    cached_bytes = 0; allocated_bytes = 0;
    if (upload_pool) vkDestroyCommandPool(pc_vulkan_device(), upload_pool, NULL);
    upload_pool = VK_NULL_HANDLE;
}

int pc_gx_texture_get(unsigned slot, pc_gx_texture_binding* binding)
{
    if (!binding) return 0;
    memset(binding, 0, sizeof *binding);
    if (slot >= 8 || !slots[slot]) return 0;
    touch(slots[slot]);
    *binding = slots[slot]->binding;
    return 1;
}

/* All temporaries are owned here and released on both success and failure. */
static int upload(texture* t, VkBufferImageCopy* regions)
{
    VkDevice dev = pc_vulkan_device();
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo alloc = {0};
    VkBufferCreateInfo bci = {0};
    VkImageCreateInfo ici = {0};
    VkImageViewCreateInfo vci = {0};
    VkCommandBufferAllocateInfo cai = {0};
    VkCommandBufferBeginInfo begin = {0};
    VkImageMemoryBarrier barrier = {0};
    VkFenceCreateInfo fci = {0};
    VkSubmitInfo submit = {0};
    void* mapped = NULL;
    int result = -1;
    VkResult vr;
#define TRY(call) do { vr = (call); if (vr != VK_SUCCESS) { report(#call); goto done; } } while (0)
    if (!upload_pool) {
        VkCommandPoolCreateInfo pci = {0};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = pc_vulkan_graphics_family();
        TRY(vkCreateCommandPool(dev, &pci, NULL, &upload_pool));
    }
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = t->bytes; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    TRY(vkCreateBuffer(dev, &bci, NULL, &staging));
    vkGetBufferMemoryRequirements(dev, staging, &req);
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    if (memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &alloc.memoryTypeIndex)) {
        report("no coherent staging memory"); goto done;
    }
    TRY(vkAllocateMemory(dev, &alloc, NULL, &staging_mem));
    TRY(vkBindBufferMemory(dev, staging, staging_mem, 0));
    TRY(vkMapMemory(dev, staging_mem, 0, t->bytes, 0, &mapped));
    memcpy(mapped, t->pixels, t->bytes);
    vkUnmapMemory(dev, staging_mem); mapped = NULL;

    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D; ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = t->binding.width; ici.extent.height = t->binding.height;
    ici.extent.depth = 1; ici.mipLevels = t->binding.levels; ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT; ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT; /* supports diagnostic readback */
    TRY(vkCreateImage(dev, &ici, NULL, &t->binding.image));
    vkGetImageMemoryRequirements(dev, t->binding.image, &req);
    while (req.size > MAX_TEXTURE_BYTES - allocated_bytes) {
        if (!evict_one()) {
            report("64 MiB image allocation budget exhausted by active textures"); goto done;
        }
    }
    t->allocation_bytes = req.size;
    alloc.allocationSize = req.size;
    if (memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex) &&
        memory_type(req.memoryTypeBits, 0, &alloc.memoryTypeIndex)) {
        report("no image memory type"); goto done;
    }
    TRY(vkAllocateMemory(dev, &alloc, NULL, &t->memory));
    TRY(vkBindImageMemory(dev, t->binding.image, t->memory, 0));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = upload_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    TRY(vkAllocateCommandBuffers(dev, &cai, &cmd));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    TRY(vkBeginCommandBuffer(cmd, &begin));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t->binding.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = t->binding.levels;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    vkCmdCopyBufferToImage(cmd, staging, t->binding.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           t->binding.levels, regions);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    TRY(vkEndCommandBuffer(cmd));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    TRY(vkCreateFence(dev, &fci, NULL, &fence));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    TRY(vkQueueSubmit(pc_vulkan_graphics_queue(), 1, &submit, fence));
    vr = vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vr != VK_SUCCESS) {
        vkDeviceWaitIdle(dev); report("upload fence failed"); goto done;
    }
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = t->binding.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format; vci.subresourceRange = barrier.subresourceRange;
    TRY(vkCreateImageView(dev, &vci, NULL, &t->binding.descriptor.imageView));
    TRY(vkCreateSampler(dev, &t->sampler_key, NULL, &t->binding.descriptor.sampler));
    t->binding.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    result = 0;
done:
    if (mapped) vkUnmapMemory(dev, staging_mem);
    if (fence) vkDestroyFence(dev, fence, NULL);
    if (cmd) vkFreeCommandBuffers(dev, upload_pool, 1, &cmd);
    if (staging) vkDestroyBuffer(dev, staging, NULL);
    if (staging_mem) vkFreeMemory(dev, staging_mem, NULL);
#undef TRY
    return result;
}

int pc_gx_texture_load_tlut(unsigned slot, unsigned format, unsigned width,
                            unsigned height, unsigned levels,
                            const void* source, size_t source_size,
                            const void* tlut, unsigned tlut_entries,
                            unsigned tlut_format,
                            const VkSamplerCreateInfo* sampler)
{
    VkBufferImageCopy regions[MAX_MIPS] = {{0}};
    size_t src_bytes = 0, dst_bytes = 0, offset = 0;
    unsigned w = width, h = height, m, i, empty = MAX_TEXTURES;
    texture* t;
    ++texture_loads;
    if (slot >= 8) { report("invalid texture slot"); return -1; }
    slots[slot] = NULL; /* Never leave a previous texture bound after failure. */
    if (!source || !sampler || sampler->pNext || sampler->anisotropyEnable ||
        !levels || levels > MAX_MIPS || !pc_vulkan_device()) {
        report("invalid upload arguments/device"); return -1;
    }
    for (m = 0; m < levels; ++m) {
        size_t n = pc_texture_source_size(format, w, h);
        if (!n || (m && w == 1 && h == 1 && regions[m-1].imageExtent.width == 1 &&
                   regions[m-1].imageExtent.height == 1)) {
            report("unsupported format, dimensions or mip count"); return -1;
        }
        regions[m].bufferOffset = dst_bytes;
        regions[m].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[m].imageSubresource.mipLevel = m;
        regions[m].imageSubresource.layerCount = 1;
        regions[m].imageExtent.width = w; regions[m].imageExtent.height = h;
        regions[m].imageExtent.depth = 1;
        src_bytes += n; dst_bytes += (size_t)w * h * 4;
        w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
    }
    if (source_size < src_bytes) { report("truncated texture/mip data"); return -1; }
    t = calloc(1, sizeof *t);
    if (!t) { report("texture allocation failed"); return -1; }
    t->pixels = malloc(dst_bytes);
    if (!t->pixels) { destroy(t); report("decode allocation failed"); return -1; }
    t->bytes = dst_bytes; t->sampler_key = *sampler;
    t->binding.width = width; t->binding.height = height; t->binding.levels = levels;
    for (m = 0; m < levels; ++m) {
        size_t n;
        w = regions[m].imageExtent.width; h = regions[m].imageExtent.height;
        n = pc_texture_source_size(format, w, h);
        if (pc_texture_decode_tlut(format, w, h,
                                   (const unsigned char*)source + offset, n,
                                   tlut, tlut_entries, tlut_format,
                                   t->pixels + regions[m].bufferOffset,
                                   (size_t)w * h * 4)) {
            destroy(t);
            report(tlut ? "paletted decode failed (index past palette?)"
                        : "texture decode failed");
            return -1;
        }
        offset += n;
    }
    /* Compare actual decoded bytes, not source pointers or GXTexObj identity:
       archives reuse addresses, and dynamic textures can change in place. */
    for (i = 0; i < MAX_TEXTURES; ++i) {
        texture* c = cache[i];
        if (!c) { if (empty == MAX_TEXTURES) empty = i; continue; }
        if (c->binding.width == width && c->binding.height == height &&
            c->binding.levels == levels && c->bytes == dst_bytes &&
            !memcmp(&c->sampler_key, sampler, sizeof *sampler) &&
            !memcmp(c->pixels, t->pixels, dst_bytes)) {
            ++texture_hits;
            touch(c);
            slots[slot] = c; destroy(t); return 0;
        }
    }
    while (empty == MAX_TEXTURES || dst_bytes > MAX_TEXTURE_BYTES - cached_bytes) {
        if (!evict_one()) {
            report("texture cache full with active textures"); destroy(t); return -1;
        }
        if (empty == MAX_TEXTURES)
            for (i = 0; i < MAX_TEXTURES; ++i)
                if (!cache[i]) { empty = i; break; }
    }
    if (upload(t, regions)) { destroy(t); return -1; }
    ++texture_uploads;
    touch(t);
    cache[empty] = slots[slot] = t;
    cached_bytes += dst_bytes; allocated_bytes += t->allocation_bytes;
    return 0;
}

int pc_gx_texture_load(unsigned slot, unsigned format, unsigned width,
                       unsigned height, unsigned levels, const void* source,
                       size_t source_size, const VkSamplerCreateInfo* sampler)
{
    return pc_gx_texture_load_tlut(slot, format, width, height, levels, source,
                                   source_size, NULL, 0, 0, sampler);
}

int pc_gx_texture_load_obj_source(const void* obj, unsigned slot,
                                 const void* source, size_t source_size)
{
    /* SDK layout from extern/dolphin/src/dolphin/gx/GXTexture.c.
       Native words are NOT swapped; only the texture payload is big-endian.
       memcpy avoids aliasing GXTexObj's opaque storage. */
    uint32_t words[8];
    unsigned w, h, f, levels = 1, dimension, lod, hw_filter;
    const struct tlut_entry* palette = NULL;
    uint32_t physical;
    VkSamplerCreateInfo sampler = {0};
    static const VkSamplerAddressMode wraps[3] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
    };
    if (slot >= 8) { report("GXLoadTexObj: slot out of range"); return -1; }
    slots[slot] = NULL;
    if (!obj) { report("GXLoadTexObj: null object"); return -1; }
    memcpy(words, obj, sizeof words);
    w = (words[2] & 1023) + 1; h = ((words[2] >> 10) & 1023) + 1;
    f = words[5]; physical = (words[3] & 0x1fffff) << 5;
    if (!pc_texture_source_size(f, w, h)) {
        /* Depth and copy formats still need a separate implementation.
           Tallied by format code so it is possible to tell which one the
           caller is actually asking for. */
        if (f < 16) unsupported_format_counts[f]++;
        report("GXLoadTexObj: unsupported format (depth/copy)"); return -1;
    }
    if (pc_texture_is_paletted(f)) {
        /* words[6] is __GXTexObjInt::tlutName, set by GXInitTexObjTlut. */
        palette = find_tlut(words[6]);
        if (!palette) {
            if (f < 16) unsupported_format_counts[f]++;
            report("GXLoadTexObj: paletted texture names an unloaded TLUT");
            return -1;
        }
    }
    if ((words[0] & 3) > 2 || ((words[0] >> 2) & 3) > 2) {
        report("GXLoadTexObj: invalid wrap mode"); return -1;
    }
    if (((const unsigned char*)obj)[31] & 1) {
        lod = (((words[1] >> 8) & 255) + 15) / 16;
        dimension = w > h ? w : h;
        while (levels <= lod && dimension > 1) { ++levels; dimension /= 2; }
    }
    if (!source) {
        if (physical >= (24u << 20)) { report("GXLoadTexObj: source outside RAM"); return -1; }
        source = (const void*)(uintptr_t)(physical | 0x80000000u);
        source_size = (24u << 20) - physical;
    }
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.addressModeU = wraps[words[0] & 3];
    sampler.addressModeV = wraps[(words[0] >> 2) & 3];
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    hw_filter = (words[0] >> 5) & 7;
    if ((hw_filter & 3) == 3) { report("GXLoadTexObj: invalid filter"); return -1; }
    sampler.magFilter = (words[0] & 16) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.minFilter = (hw_filter & 4) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.mipmapMode = (hw_filter & 2) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.minLod = (words[1] & 255) / 16.0f;
    sampler.maxLod = ((words[1] >> 8) & 255) / 16.0f;
    if (!(hw_filter & 3)) sampler.minLod = sampler.maxLod = 0;
    if (sampler.maxLod > levels - 1) sampler.maxLod = (float)(levels - 1);
    if (sampler.minLod > sampler.maxLod) sampler.minLod = sampler.maxLod;
    /* GX's signed 8-bit bias is stored in 1/32 steps. */
    sampler.mipLodBias = (int8_t)((words[0] >> 9) & 255) / 32.0f;
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pc_vulkan_physical_device(), &props);
        if (sampler.mipLodBias > props.limits.maxSamplerLodBias)
            sampler.mipLodBias = props.limits.maxSamplerLodBias;
        if (sampler.mipLodBias < -props.limits.maxSamplerLodBias)
            sampler.mipLodBias = -props.limits.maxSamplerLodBias;
    }
    if (words[0] & ((3u << 19) | (1u << 21))) {
        static int warned;
        if (!warned++) report("anisotropy/bias clamp not implemented; using isotropic sampling");
    }
    return pc_gx_texture_load_tlut(slot, f, w, h, levels, source, source_size,
        palette ? palette->data : NULL, palette ? palette->entries : 0,
        palette ? palette->format : 0, &sampler);
}

int pc_gx_texture_load_obj(const void* obj, unsigned slot)
{
    return pc_gx_texture_load_obj_source(obj, slot, NULL, 0);
}
