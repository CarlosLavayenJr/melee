/* No window or game data. Uses the real upload backend and reads every mip
   back from a Vulkan image, then exercises rebinding/resource retirement. */
#include "pc_gx_texture.h"
#include "pc_texture_decode.h"
#include "pc_vulkan.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VkInstance instance;
static VkDevice device;
static VkPhysicalDevice physical;
static VkQueue queue;
static unsigned family;
static VkCommandPool pool;
static unsigned validation_errors;
VkDevice pc_vulkan_device(void) { return device; }
VkPhysicalDevice pc_vulkan_physical_device(void) { return physical; }
VkQueue pc_vulkan_graphics_queue(void) { return queue; }
unsigned pc_vulkan_graphics_family(void) { return family; }
void pc_sys_log(const char* s) { fputs(s, stderr); }
#define VK_OK(c) do { VkResult r_ = (c); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #c, r_); abort(); } } while (0)

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* context)
{
    (void)type; (void)context;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++validation_errors; fprintf(stderr, "%s\n", data->pMessage);
    }
    return VK_FALSE;
}

static VkDebugUtilsMessengerEXT setup(void)
{
    VkInstanceCreateInfo ici = {0};
    VkApplicationInfo app = {0};
    VkDeviceQueueCreateInfo qci = {0};
    VkDeviceCreateInfo dci = {0};
    VkCommandPoolCreateInfo pci = {0};
    VkDebugUtilsMessengerCreateInfoEXT debug = {0};
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkLayerProperties layers[64];
    VkPhysicalDevice devices[16];
    VkQueueFamilyProperties families[32];
    unsigned n = 64, i, count = 16, qcount = 32, validation = 0;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    float priority = 1;
    VK_OK(vkEnumerateInstanceLayerProperties(&n, layers));
    for (i = 0; i < n; ++i) if (!strcmp(layers[i].layerName, layer)) validation = 1;
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_0;
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo = &app;
    if (validation) {
        ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer;
        ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &extension;
    }
    VK_OK(vkCreateInstance(&ici, NULL, &instance));
    if (validation) {
        PFN_vkCreateDebugUtilsMessengerEXT create_debug =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback = debug_message;
        assert(create_debug); VK_OK(create_debug(instance, &debug, NULL, &messenger));
    }
    printf("Vulkan validation layer: %s\n", validation ? "enabled" : "unavailable");
    VK_OK(vkEnumeratePhysicalDevices(instance, &count, devices)); assert(count);
    physical = devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qcount, families);
    for (family = 0; family < qcount; ++family)
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    assert(family < qcount);
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VK_OK(vkCreateDevice(physical, &dci, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pci.queueFamilyIndex = family;
    VK_OK(vkCreateCommandPool(device, &pci, NULL, &pool));
    return messenger;
}

static void readback(pc_gx_texture_binding t, const unsigned char* expected, size_t bytes)
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkMemoryRequirements req;
    VkPhysicalDeviceMemoryProperties props;
    VkBufferCreateInfo bci = {0};
    VkMemoryAllocateInfo mai = {0};
    VkCommandBufferAllocateInfo cai = {0};
    VkCommandBufferBeginInfo bi = {0};
    VkCommandBuffer cmd;
    VkImageMemoryBarrier barrier = {0};
    VkBufferMemoryBarrier host = {0};
    VkBufferImageCopy regions[11] = {{0}};
    VkSubmitInfo submit = {0};
    unsigned i, w = t.width, h = t.height;
    size_t offset = 0;
    void* mapped;
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_OK(vkCreateBuffer(device, &bci, NULL, &buffer));
    vkGetBufferMemoryRequirements(device, buffer, &req);
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; mai.allocationSize = req.size;
    for (i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
    assert(i < props.memoryTypeCount); mai.memoryTypeIndex = i;
    VK_OK(vkAllocateMemory(device, &mai, NULL, &memory));
    VK_OK(vkBindBufferMemory(device, buffer, memory, 0));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VK_OK(vkAllocateCommandBuffers(device, &cai, &cmd));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_OK(vkBeginCommandBuffer(cmd, &bi));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image; barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = t.levels; barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    for (i = 0; i < t.levels; ++i) {
        regions[i].bufferOffset = offset;
        regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[i].imageSubresource.mipLevel = i; regions[i].imageSubresource.layerCount = 1;
        regions[i].imageExtent.width = w; regions[i].imageExtent.height = h;
        regions[i].imageExtent.depth = 1; offset += w*h*4;
        w = w > 1 ? w/2 : 1; h = h > 1 ? h/2 : 1;
    }
    assert(offset == bytes);
    vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, t.levels, regions);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = buffer; host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, NULL, 1, &host, 0, NULL);
    VK_OK(vkEndCommandBuffer(cmd));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    VK_OK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); VK_OK(vkQueueWaitIdle(queue));
    VK_OK(vkMapMemory(device, memory, 0, bytes, 0, &mapped));
    assert(!memcmp(mapped, expected, bytes));
    vkUnmapMemory(device, memory); vkFreeCommandBuffers(device, pool, 1, &cmd);
    vkDestroyBuffer(device, buffer, NULL); vkFreeMemory(device, memory, NULL);
}

int main(void)
{
    unsigned char src[512], expected[9*5*4+4*2*4+2*1*4+4];
    const unsigned formats[] = {0,1,2,3,4,5,6,14};
    VkSamplerCreateInfo sampler = {0};
    pc_gx_texture_binding first, same, changed, absent;
    VkDebugUtilsMessengerEXT messenger = setup();
    unsigned f, i, m, w, h;
    size_t source_bytes, rgba_bytes;
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 3;
    for (i = 0; i < sizeof src; ++i) src[i] = (unsigned char)(i*13 + 7);
    for (f = 0; f < sizeof formats / sizeof *formats; ++f) {
        w = 9; h = 5; source_bytes = rgba_bytes = 0;
        for (m = 0; m < 4; ++m) {
            size_t n = pc_texture_source_size(formats[f], w, h);
            assert(!pc_texture_decode(formats[f], w, h, src+source_bytes, n,
                                      expected+rgba_bytes, w*h*4));
            source_bytes += n; rgba_bytes += w*h*4;
            w = w > 1 ? w/2 : 1; h = h > 1 ? h/2 : 1;
        }
        assert(!pc_gx_texture_load(0, formats[f], 9, 5, 4, src, source_bytes, &sampler));
        assert(pc_gx_texture_get(0, &first)); readback(first, expected, rgba_bytes);
        assert(!pc_gx_texture_load(1, formats[f], 9, 5, 4, src, source_bytes, &sampler));
        assert(pc_gx_texture_get(1, &same)); assert(first.image == same.image);
        src[0] ^= 0xff;
        assert(!pc_gx_texture_load(0, formats[f], 9, 5, 4, src, source_bytes, &sampler));
        assert(pc_gx_texture_get(0, &changed)); assert(changed.image != first.image);
        /* Rebinding leaves the old image intact, including across a fence while
           another GX slot still binds it. */
        pc_gx_textures_begin_frame(); readback(first, expected, rgba_bytes);
        src[0] ^= 0xff;
        assert(pc_gx_texture_load(0, formats[f], 9, 5, 4, src, source_bytes-1, &sampler) == -1);
        assert(!pc_gx_texture_get(0, &absent));
        assert(pc_gx_texture_load(1, formats[f], 1, 1, 2, src, sizeof src, &sampler) == -1);
        pc_gx_textures_begin_frame();
    }
    pc_gx_textures_shutdown();
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    if (messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy_debug(instance, messenger, NULL);
    }
    vkDestroyInstance(instance, NULL);
    assert(!validation_errors);
    puts("PASS: Vulkan readback of all eight formats and four mip levels, cache and rebinding lifetime");
    return 0;
}
