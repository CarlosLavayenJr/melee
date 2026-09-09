/* pc_vulkan.c — Win32 window and Vulkan device/swapchain/present.
 *
 * No GX knowledge lives here on purpose (see pc/GX_RENDERER.md): this is the
 * same "replace the hardware, not the game" split the rest of pc/ already
 * uses, just for a piece of hardware (a GPU with a real driver) that has no
 * console-side equivalent to fall back on when something is unsupported.
 *
 * Single frame in flight, one render pass, one subpass. That is simpler than
 * this needs to stay forever, but it is the version worth having first: the
 * milestone is a frame on screen, not maximum throughput.
 */
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <windows.h>

#include "pc_sys.h"
#include "pc_vulkan.h"

#define PC_SWAPCHAIN_IMAGE_MAX 8

static HWND window;
static int window_closed;

static VkInstance instance;
static VkPhysicalDevice phys_device;
static VkDevice device;
static unsigned int graphics_family;
static VkQueue graphics_queue;
static VkSurfaceKHR surface;

static VkSwapchainKHR swapchain;
static VkFormat swapchain_format;
static VkExtent2D swapchain_extent;
static VkImage swapchain_images[PC_SWAPCHAIN_IMAGE_MAX];
static VkImageView swapchain_views[PC_SWAPCHAIN_IMAGE_MAX];
static VkFramebuffer swapchain_framebuffers[PC_SWAPCHAIN_IMAGE_MAX];
static unsigned int swapchain_image_count;
static unsigned int current_image_index;

static VkRenderPass render_pass;
static VkCommandPool command_pool;
static VkCommandBuffer command_buffer;

static VkSemaphore sem_image_available;
static VkSemaphore sem_render_finished;
static VkFence fence_in_flight;

static VkClearValue clear_value;
static int frame_active;
static int swapchain_dirty;
static int can_capture;
static unsigned presented_frames;
static int frame_has_draws, capture_attempted;
static VkBuffer capture_buffer;
static VkDeviceMemory capture_memory;
static char capture_path[1024];

/* Optional framebuffer readback for headless visual QA. Enabled only by
   PC_CAPTURE_FRAME=<path.bmp>; captures the first frame containing a draw.
   This reads the application's own Vulkan image, not the desktop screen. */
static void capture_record(void)
{
    VkBufferCreateInfo b = {0};
    VkMemoryAllocateInfo a = {0};
    VkMemoryRequirements req;
    VkPhysicalDeviceMemoryProperties props;
    VkImageMemoryBarrier barrier = {0};
    VkBufferImageCopy region = {0};
    VkBufferMemoryBarrier host = {0};
    unsigned i;
    if (!can_capture || !frame_has_draws || capture_attempted ||
        !GetEnvironmentVariableA("PC_CAPTURE_FRAME", capture_path, sizeof capture_path)) return;
    capture_attempted = 1;
    if (swapchain_format != VK_FORMAT_B8G8R8A8_UNORM && swapchain_format != VK_FORMAT_R8G8B8A8_UNORM) return;
    b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    b.size = (VkDeviceSize)swapchain_extent.width * swapchain_extent.height * 4;
    b.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(device, &b, NULL, &capture_buffer) != VK_SUCCESS) return;
    vkGetBufferMemoryRequirements(device, capture_buffer, &req);
    vkGetPhysicalDeviceMemoryProperties(phys_device, &props);
    for (i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
    if (i == props.memoryTypeCount) goto failed;
    a.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    a.allocationSize = req.size; a.memoryTypeIndex = i;
    if (vkAllocateMemory(device, &a, NULL, &capture_memory) != VK_SUCCESS) goto failed;
    if (vkBindBufferMemory(device, capture_buffer, capture_memory, 0) != VK_SUCCESS) goto failed;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_images[current_image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = swapchain_extent.width;
    region.imageExtent.height = swapchain_extent.height; region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(command_buffer, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           capture_buffer, 1, &region);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = capture_buffer; host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host, 0, NULL);
    return;
failed:
    if (capture_buffer) vkDestroyBuffer(device, capture_buffer, NULL);
    if (capture_memory) vkFreeMemory(device, capture_memory, NULL);
    capture_buffer = VK_NULL_HANDLE; capture_memory = VK_NULL_HANDLE;
    pc_sys_log("pc_vulkan: capture allocation failed\n");
}

static void capture_finish(void)
{
    unsigned char* pixels;
    unsigned size = swapchain_extent.width * swapchain_extent.height * 4, i;
    BITMAPFILEHEADER file_header = {0};
    BITMAPINFOHEADER info = {0};
    HANDLE file;
    DWORD written;
    if (!capture_buffer) return;
    if (vkQueueWaitIdle(graphics_queue) == VK_SUCCESS &&
        vkMapMemory(device, capture_memory, 0, size, 0, (void**)&pixels) == VK_SUCCESS) {
        if (swapchain_format == VK_FORMAT_R8G8B8A8_UNORM)
            for (i = 0; i < size; i += 4) { unsigned char c = pixels[i]; pixels[i] = pixels[i+2]; pixels[i+2] = c; }
        file_header.bfType = 0x4d42; file_header.bfOffBits = sizeof file_header + sizeof info;
        file_header.bfSize = file_header.bfOffBits + size;
        info.biSize = sizeof info; info.biWidth = swapchain_extent.width;
        info.biHeight = -(LONG)swapchain_extent.height; info.biPlanes = 1; info.biBitCount = 32;
        info.biSizeImage = size;
        /* Game MSL stdio symbols coexist with the host CRT: use Win32 I/O. */
        file = CreateFileA(capture_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            int ok = WriteFile(file, &file_header, sizeof file_header, &written, NULL) && written == sizeof file_header &&
                     WriteFile(file, &info, sizeof info, &written, NULL) && written == sizeof info &&
                     WriteFile(file, pixels, size, &written, NULL) && written == size;
            if (!CloseHandle(file)) ok = 0;
            pc_sys_log(ok ? "pc_vulkan: frame capture saved\n" : "pc_vulkan: frame capture write failed\n");
        } else pc_sys_log("pc_vulkan: cannot open capture path\n");
        vkUnmapMemory(device, capture_memory);
    }
    vkDestroyBuffer(device, capture_buffer, NULL); vkFreeMemory(device, capture_memory, NULL);
    capture_buffer = VK_NULL_HANDLE; capture_memory = VK_NULL_HANDLE;
}

/* ---- failure reporting -----------------------------------------------
 * A GX call the renderer does not understand yet fails loud (pc/GX_RENDERER.md);
 * a Vulkan call that fails is the same rule one layer down. */
static void fail(const char* what, VkResult r)
{
    char buf[16];
    int n = 0, neg = r < 0;
    unsigned int v = (unsigned int) (neg ? -r : r);
    pc_sys_log("pc_vulkan: ");
    pc_sys_log(what);
    pc_sys_log(" failed, VkResult=");
    if (neg) {
        pc_sys_log("-");
    }
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0 && n < 16) {
        buf[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        char c = buf[--n];
        char s[2];
        s[0] = c;
        s[1] = 0;
        pc_sys_log(s);
    }
    pc_sys_log("\n");
}

#define VK_CHECK(call, what)                                                \
    do {                                                                     \
        VkResult _r = (call);                                                \
        if (_r != VK_SUCCESS) {                                              \
            fail(what, _r);                                                  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* ---- window ------------------------------------------------------------ */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        window_closed = 1;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        /* Rebuild the swapchain lazily, at the next begin_frame, rather than
           here: resizing mid-frame would tear the frame in progress apart. */
        swapchain_dirty = 1;
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static int create_window(unsigned width, unsigned height, const char* title)
{
    WNDCLASSEXA wc;
    RECT rect;
    HINSTANCE hinst = GetModuleHandleA(NULL);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR) IDC_ARROW);
    wc.lpszClassName = "pc_melee_window";
    if (!RegisterClassExA(&wc)) {
        pc_sys_log("pc_vulkan: RegisterClassEx failed\n");
        return 1;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG) width;
    rect.bottom = (LONG) height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    window = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             NULL, NULL, hinst, NULL);
    if (window == NULL) {
        pc_sys_log("pc_vulkan: CreateWindowEx failed\n");
        return 1;
    }
    ShowWindow(window, SW_SHOW);
    return 0;
}

/* ---- frame rate ---------------------------------------------------------
 *
 * Presented frames per second, in the title bar next to the app name. This
 * counts real vkQueuePresentKHR calls, so it measures what the window
 * actually shows -- not the game's internal frame counter, which advances
 * inside OSSleepThread whether or not anything was drawn.
 *
 * Uses the Win32 clock directly rather than pc_sys_mono_ns, to stay
 * independent of the host time layer the game's own pacing runs on.
 */
static char title_base[128];
static LARGE_INTEGER fps_freq, fps_mark;
static unsigned fps_frames;

static void fps_init(const char* title)
{
    size_t i;
    for (i = 0; i + 1 < sizeof title_base && title[i]; i++) {
        title_base[i] = title[i];
    }
    title_base[i] = '\0';
    QueryPerformanceFrequency(&fps_freq);
    QueryPerformanceCounter(&fps_mark);
    fps_frames = 0;
}

static void fps_tick(void)
{
    LARGE_INTEGER now;
    double elapsed;

    if (window == NULL || fps_freq.QuadPart == 0) {
        return;
    }
    fps_frames++;
    QueryPerformanceCounter(&now);
    elapsed = (double) (now.QuadPart - fps_mark.QuadPart) /
              (double) fps_freq.QuadPart;
    if (elapsed >= 0.5) {
        char buf[160];
        double fps = fps_frames / elapsed;
        wsprintfA(buf, "%s - %d.%d fps", title_base, (int) fps,
                  (int) ((fps - (int) fps) * 10.0));
        SetWindowTextA(window, buf);
        fps_mark = now;
        fps_frames = 0;
    }
}

void pc_vulkan_pump_events(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

int pc_vulkan_should_close(void) { return window_closed; }

/* ---- instance / device -------------------------------------------------- */

static int create_instance(const char* app_name)
{
    VkApplicationInfo app_info;
    VkInstanceCreateInfo ci;
    const char* extensions[2];

    ZeroMemory(&app_info, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_name;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "melee-pc";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
    extensions[1] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ci, NULL, &instance), "vkCreateInstance");
    return 0;
}

static int pick_physical_device(void)
{
    VkPhysicalDevice devices[16];
    unsigned int count = 16, i;

    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices),
             "vkEnumeratePhysicalDevices");
    if (count == 0) {
        pc_sys_log("pc_vulkan: no Vulkan-capable device found\n");
        return 1;
    }
    /* First device with a graphics queue family wins. Melee does not need
       discrete-vs-integrated preference to get a frame on screen. */
    for (i = 0; i < count; i++) {
        VkQueueFamilyProperties families[16];
        unsigned int fcount = 16, f;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &fcount, families);
        for (f = 0; f < fcount; f++) {
            if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                phys_device = devices[i];
                graphics_family = f;
                return 0;
            }
        }
    }
    pc_sys_log("pc_vulkan: no device with a graphics queue family\n");
    return 1;
}

static int create_device(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo ci;
    const char* extensions[1];

    ZeroMemory(&qci, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = graphics_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pQueueCreateInfos = &qci;
    ci.queueCreateInfoCount = 1;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateDevice(phys_device, &ci, NULL, &device),
             "vkCreateDevice");
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
    return 0;
}

static int create_surface(void)
{
    VkWin32SurfaceCreateInfoKHR ci;
    VkBool32 supported = VK_FALSE;

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleA(NULL);
    ci.hwnd = window;
    VK_CHECK(vkCreateWin32SurfaceKHR(instance, &ci, NULL, &surface),
             "vkCreateWin32SurfaceKHR");

    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
                 phys_device, graphics_family, surface, &supported),
             "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (!supported) {
        pc_sys_log("pc_vulkan: graphics queue family cannot present to this "
                   "surface\n");
        return 1;
    }
    return 0;
}

/* ---- swapchain ----------------------------------------------------------
 * Rebuildable independently of instance/device/surface, since a window
 * resize needs exactly this and nothing above it. */

static void destroy_swapchain_views(void)
{
    unsigned int i;
    for (i = 0; i < swapchain_image_count; i++) {
        if (swapchain_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, swapchain_framebuffers[i], NULL);
            swapchain_framebuffers[i] = VK_NULL_HANDLE;
        }
        if (swapchain_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, swapchain_views[i], NULL);
            swapchain_views[i] = VK_NULL_HANDLE;
        }
    }
}

static int create_render_pass(void)
{
    VkAttachmentDescription color;
    VkAttachmentReference color_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo ci;

    ZeroMemory(&color, sizeof(color));
    color.format = swapchain_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    ZeroMemory(&subpass, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    ZeroMemory(&dependency, sizeof(dependency));
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &color;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device, &ci, NULL, &render_pass),
             "vkCreateRenderPass");
    return 0;
}

static int create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    unsigned int format_count = 32, i;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR; /* always
        supported, and gives us vsync-paced presentation for free */
    VkSwapchainCreateInfoKHR ci;
    VkSwapchainKHR old_swapchain = swapchain;

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface,
                                                       &caps),
             "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface,
                                                  &format_count, formats),
             "vkGetPhysicalDeviceSurfaceFormatsKHR");

    swapchain_format = formats[0].format;
    for (i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            swapchain_format = formats[i].format;
            break;
        }
    }

    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        swapchain_extent = caps.currentExtent;
    } else {
        RECT rect;
        GetClientRect(window, &rect);
        swapchain_extent.width = (unsigned int) (rect.right - rect.left);
        swapchain_extent.height = (unsigned int) (rect.bottom - rect.top);
    }
    if (swapchain_extent.width == 0) swapchain_extent.width = 1;
    if (swapchain_extent.height == 0) swapchain_extent.height = 1;

    ZeroMemory(&ci, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface;
    ci.minImageCount = caps.minImageCount + 1 > PC_SWAPCHAIN_IMAGE_MAX
                            ? PC_SWAPCHAIN_IMAGE_MAX
                            : caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && ci.minImageCount > caps.maxImageCount) {
        ci.minImageCount = caps.maxImageCount;
    }
    ci.imageFormat = swapchain_format;
    ci.imageColorSpace = formats[0].colorSpace;
    ci.imageExtent = swapchain_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    can_capture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (can_capture && GetEnvironmentVariableA("PC_CAPTURE_FRAME", capture_path, sizeof capture_path))
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    else can_capture = 0;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present_mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old_swapchain;

    VK_CHECK(vkCreateSwapchainKHR(device, &ci, NULL, &swapchain),
             "vkCreateSwapchainKHR");

    if (old_swapchain != VK_NULL_HANDLE) {
        destroy_swapchain_views();
        vkDestroySwapchainKHR(device, old_swapchain, NULL);
    }

    swapchain_image_count = PC_SWAPCHAIN_IMAGE_MAX;
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count,
                                     swapchain_images),
             "vkGetSwapchainImagesKHR");

    for (i = 0; i < swapchain_image_count; i++) {
        VkImageViewCreateInfo vci;
        VkFramebufferCreateInfo fci;

        ZeroMemory(&vci, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapchain_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapchain_format;
        vci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &vci, NULL, &swapchain_views[i]),
                 "vkCreateImageView");

        ZeroMemory(&fci, sizeof(fci));
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &swapchain_views[i];
        fci.width = swapchain_extent.width;
        fci.height = swapchain_extent.height;
        fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fci, NULL,
                                     &swapchain_framebuffers[i]),
                 "vkCreateFramebuffer");
    }

    swapchain_dirty = 0;
    return 0;
}

static int rebuild_swapchain(void)
{
    vkDeviceWaitIdle(device);
    return create_swapchain();
}

/* ---- commands / sync ----------------------------------------------------- */

static int create_commands_and_sync(void)
{
    VkCommandPoolCreateInfo pci;
    VkCommandBufferAllocateInfo ai;
    VkSemaphoreCreateInfo sci;
    VkFenceCreateInfo fci;

    ZeroMemory(&pci, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = graphics_family;
    VK_CHECK(vkCreateCommandPool(device, &pci, NULL, &command_pool),
             "vkCreateCommandPool");

    ZeroMemory(&ai, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &command_buffer),
             "vkAllocateCommandBuffers");

    ZeroMemory(&sci, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &sem_image_available),
             "vkCreateSemaphore");
    VK_CHECK(vkCreateSemaphore(device, &sci, NULL, &sem_render_finished),
             "vkCreateSemaphore");

    ZeroMemory(&fci, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(device, &fci, NULL, &fence_in_flight),
             "vkCreateFence");
    return 0;
}

/* ---- public API ----------------------------------------------------------- */

int pc_vulkan_init(unsigned width, unsigned height, const char* title)
{
    if (create_window(width, height, title)) return 1;
    fps_init(title);
    if (create_instance(title)) return 1;
    if (pick_physical_device()) return 1;
    if (create_device()) return 1;
    if (create_surface()) return 1;
    /* Order matters here only because create_render_pass reads
       swapchain_format, which create_swapchain would normally set -- so
       query just the format up front, then build the pass before the
       swapchain that needs it for its framebuffers. */
    {
        VkSurfaceFormatKHR formats[32];
        unsigned int format_count = 32, i;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface,
                                                       &format_count, formats),
                 "vkGetPhysicalDeviceSurfaceFormatsKHR");
        swapchain_format = formats[0].format;
        for (i = 0; i < format_count; i++) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
                swapchain_format = formats[i].format;
                break;
            }
        }
    }
    if (create_render_pass()) return 1;
    if (create_swapchain()) return 1;
    if (create_commands_and_sync()) return 1;

    clear_value.color.float32[0] = 0.0f;
    clear_value.color.float32[1] = 0.0f;
    clear_value.color.float32[2] = 0.0f;
    clear_value.color.float32[3] = 1.0f;

    pc_sys_log("pc_vulkan: window and swapchain ready\n");
    return 0;
}

void pc_vulkan_set_clear_color(float r, float g, float b, float a)
{
    clear_value.color.float32[0] = r;
    clear_value.color.float32[1] = g;
    clear_value.color.float32[2] = b;
    clear_value.color.float32[3] = a;
}

VkCommandBuffer pc_vulkan_begin_frame(void)
{
    VkResult r;
    VkCommandBufferBeginInfo bi;
    VkRenderPassBeginInfo rpbi;

    if (window == NULL || window_closed) {
        return VK_NULL_HANDLE;
    }

    vkWaitForFences(device, 1, &fence_in_flight, VK_TRUE, ~0ull);

    if (swapchain_dirty) {
        if (rebuild_swapchain()) {
            return VK_NULL_HANDLE;
        }
    }

    r = vkAcquireNextImageKHR(device, swapchain, ~0ull, sem_image_available,
                              VK_NULL_HANDLE, &current_image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty = 1;
        return VK_NULL_HANDLE;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        fail("vkAcquireNextImageKHR", r);
        return VK_NULL_HANDLE;
    }

    vkResetFences(device, 1, &fence_in_flight);
    vkResetCommandBuffer(command_buffer, 0);

    ZeroMemory(&bi, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command_buffer, &bi);

    ZeroMemory(&rpbi, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = render_pass;
    rpbi.framebuffer = swapchain_framebuffers[current_image_index];
    rpbi.renderArea.extent = swapchain_extent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear_value;
    vkCmdBeginRenderPass(command_buffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    frame_active = 1;
    frame_has_draws = 0;
    return command_buffer;
}

void pc_vulkan_mark_draw(void) { frame_has_draws = 1; }

void pc_vulkan_end_frame(void)
{
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkResult r;

    if (!frame_active) {
        return;
    }
    frame_active = 0;

    vkCmdEndRenderPass(command_buffer);
    capture_record();
    vkEndCommandBuffer(command_buffer);

    ZeroMemory(&si, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &sem_image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command_buffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &sem_render_finished;
    vkQueueSubmit(graphics_queue, 1, &si, fence_in_flight);
    capture_finish();
    ++presented_frames;

    ZeroMemory(&pi, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &sem_render_finished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &current_image_index;
    r = vkQueuePresentKHR(graphics_queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty = 1;
    } else if (r != VK_SUCCESS) {
        fail("vkQueuePresentKHR", r);
    }
    fps_tick();
}

VkDevice pc_vulkan_device(void) { return device; }
VkPhysicalDevice pc_vulkan_physical_device(void) { return phys_device; }
VkQueue pc_vulkan_graphics_queue(void) { return graphics_queue; }
unsigned pc_vulkan_graphics_family(void) { return graphics_family; }
VkRenderPass pc_vulkan_render_pass(void) { return render_pass; }

void pc_vulkan_extent(unsigned* width, unsigned* height)
{
    *width = swapchain_extent.width;
    *height = swapchain_extent.height;
}
