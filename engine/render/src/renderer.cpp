#include "render/renderer.h"
#include "vk/vk.h"
#include "platform/platform.h"          // platform_log / platform_fatal
#include "platform/platform_vulkan.h"   // platform_vk_get_loader / platform_vk_create_surface
#include <cstdlib>
#include <cstring>
#include <cmath>

// M2.0: bring-up to a cleared, animated window. Frames-in-flight = 2; per-frame
// command buffer + image_available semaphore + in_flight fence; per-SWAPCHAIN-IMAGE
// render_finished semaphore + images_in_flight fence pointer. Clearing is done with
// vkCmdClearColorImage + classic layout barriers (no render pass / dynamic rendering
// needed just to clear; that arrives with the triangle in M2.1).
#define FRAMES_IN_FLIGHT 2
#define MAX_SC_IMAGES    8

struct Renderer {
    Vk                       vk;
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         phys;
    uint32_t                 gfx_family, present_family;
    VkDevice                 device;
    VkQueue                  gfx_queue, present_queue;

    // swapchain (recreated on resize)
    VkSwapchainKHR swapchain;
    VkFormat       sc_format;
    VkExtent2D     sc_extent;
    uint32_t       sc_count;
    VkImage        sc_images[MAX_SC_IMAGES];
    VkSemaphore    render_finished[MAX_SC_IMAGES];   // per image
    VkFence        images_in_flight[MAX_SC_IMAGES];  // borrowed frame fence, or NULL
    bool           need_recreate;

    // per-frame
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd[FRAMES_IN_FLIGHT];
    VkSemaphore     image_available[FRAMES_IN_FLIGHT];
    VkFence         in_flight[FRAMES_IN_FLIGHT];
    uint32_t        frame;
    uint64_t        frame_count;

    PlatformWindow* window;
};

// Validation: log WARN/ERROR, never abort the call. Perf/best-practices noise is kept
// out by the messenger's severity mask.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types; (void)user;
    const char* msg = (data && data->pMessage) ? data->pMessage : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)        platform_log("[vk ERROR] %s\n", msg);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) platform_log("[vk WARN]  %s\n", msg);
    return VK_FALSE;
}

static bool has_layer(Vk* vk, const char* want) {
    uint32_t n = 0; vk->EnumerateInstanceLayerProperties(&n, nullptr);
    VkLayerProperties* props = (VkLayerProperties*)calloc(n ? n : 1, sizeof(VkLayerProperties));
    if (!props) return false;
    vk->EnumerateInstanceLayerProperties(&n, props);
    bool found = false;
    for (uint32_t i = 0; i < n; ++i) if (strcmp(props[i].layerName, want) == 0) { found = true; break; }
    free(props);
    return found;
}

static int device_type_score(VkPhysicalDeviceType t) {
    if (t == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   return 1000;
    if (t == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 100;
    return 10;
}

// Find graphics + present queue families for `pd`/`surface`. Prefers one family that
// does both. Returns false if either is missing.
static bool pick_families(Vk* vk, VkPhysicalDevice pd, VkSurfaceKHR surface, uint32_t* gfx, uint32_t* present) {
    uint32_t qn = 0; vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    VkQueueFamilyProperties* qf = (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(VkQueueFamilyProperties));
    if (!qf) return false;
    vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    const uint32_t NONE = 0xffffffffu;
    uint32_t g = NONE, p = NONE;
    for (uint32_t i = 0; i < qn; ++i) {
        bool can_gfx = (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 can_present = VK_FALSE;
        vk->GetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &can_present);
        if (can_gfx && can_present) { g = i; p = i; break; }   // both in one family — ideal
        if (can_gfx && g == NONE) g = i;
        if (can_present && p == NONE) p = i;
    }
    free(qf);
    if (g == NONE || p == NONE) return false;
    *gfx = g; *present = p;
    return true;
}

static void destroy_swapchain(Renderer* r) {
    for (uint32_t i = 0; i < r->sc_count; ++i)
        if (r->render_finished[i]) { r->vk.DestroySemaphore(r->device, r->render_finished[i], nullptr); r->render_finished[i] = VK_NULL_HANDLE; }
    if (r->swapchain) { r->vk.DestroySwapchainKHR(r->device, r->swapchain, nullptr); r->swapchain = VK_NULL_HANDLE; }
    r->sc_count = 0;
}

// (Re)create the swapchain for the given framebuffer size. Caller has made the device
// idle. Returns false on a transient 0-extent (minimized) — caller should skip drawing.
static bool create_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    VkSurfaceCapabilitiesKHR caps{};
    r->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(r->phys, r->surface, &caps);

    VkExtent2D extent;
    if (caps.currentExtent.width != 0xffffffffu) {
        extent = caps.currentExtent;
    } else {
        extent.width  = fb_w < caps.minImageExtent.width  ? caps.minImageExtent.width  : (fb_w > caps.maxImageExtent.width  ? caps.maxImageExtent.width  : fb_w);
        extent.height = fb_h < caps.minImageExtent.height ? caps.minImageExtent.height : (fb_h > caps.maxImageExtent.height ? caps.maxImageExtent.height : fb_h);
    }
    if (extent.width == 0 || extent.height == 0) return false;   // minimized

    // Format: prefer B8G8R8A8_SRGB / SRGB_NONLINEAR, else the first reported.
    uint32_t fn = 0; r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, nullptr);
    VkSurfaceFormatKHR* fmts = (VkSurfaceFormatKHR*)calloc(fn ? fn : 1, sizeof(VkSurfaceFormatKHR));
    r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fn; ++i)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = fmts[i]; break; }
    free(fmts);

    // Present mode: prefer MAILBOX (not guaranteed), else FIFO (always available).
    uint32_t pn = 0; r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, nullptr);
    VkPresentModeKHR* pmodes = (VkPresentModeKHR*)calloc(pn ? pn : 1, sizeof(VkPresentModeKHR));
    r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, pmodes);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pn; ++i) if (pmodes[i] == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode = VK_PRESENT_MODE_MAILBOX_KHR; break; }
    free(pmodes);

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && want > caps.maxImageCount) want = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = r->surface;
    ci.minImageCount    = want;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;  // TRANSFER_DST: we clear it
    uint32_t fams[2] = { r->gfx_family, r->present_family };
    if (r->gfx_family != r->present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = present_mode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    if (r->vk.CreateSwapchainKHR(r->device, &ci, nullptr, &r->swapchain) != VK_SUCCESS) {
        platform_log("renderer: vkCreateSwapchainKHR failed\n"); return false;
    }
    r->sc_format = chosen.format;
    r->sc_extent = extent;

    uint32_t cnt = 0; r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, nullptr);
    if (cnt > MAX_SC_IMAGES) platform_fatal("renderer: swapchain image count %u > MAX_SC_IMAGES\n", cnt);
    r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, r->sc_images);
    r->sc_count = cnt;

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < cnt; ++i) {
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->render_finished[i]);
        r->images_in_flight[i] = VK_NULL_HANDLE;
    }
    return true;
}

static void recreate_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    r->vk.DeviceWaitIdle(r->device);   // Phase-1 brute force (ARCHITECTURE / roadmap M2.0)
    destroy_swapchain(r);
    create_swapchain(r, fb_w, fb_h);
    r->need_recreate = false;
}

Renderer* renderer_create(PlatformWindow* window) {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_loader();
    if (!gipa) { platform_log("renderer: no Vulkan loader (vulkan-1.dll missing?)\n"); return nullptr; }

    Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));
    if (!r) return nullptr;
    r->window = window;
    if (!vk_load_global(&r->vk, gipa)) { free(r); return nullptr; }

    const bool validation = has_layer(&r->vk, "VK_LAYER_KHRONOS_validation");
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char* exts[]   = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VkApplicationInfo app{};
    app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "moba";
    app.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    if (validation) { ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers; }

    if (r->vk.CreateInstance(&ci, nullptr, &r->instance) != VK_SUCCESS) {
        platform_log("renderer: vkCreateInstance failed\n"); free(r); return nullptr;
    }
    vk_load_instance(&r->vk, r->instance);

    if (validation && r->vk.CreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debug_cb;
        r->vk.CreateDebugUtilsMessengerEXT(r->instance, &dci, nullptr, &r->debug);
    }

    // Surface (HWND stays in platform, ADR-0005).
    unsigned long long surf = 0;
    if (!platform_vk_create_surface(window, r->instance, &surf)) { platform_log("renderer: surface creation failed\n"); renderer_destroy(r); return nullptr; }
    r->surface = (VkSurfaceKHR)(uintptr_t)surf;

    // Pick the best physical device that has graphics+present and the swapchain ext.
    uint32_t pdn = 0; r->vk.EnumeratePhysicalDevices(r->instance, &pdn, nullptr);
    if (pdn == 0) { platform_log("renderer: no Vulkan physical devices\n"); renderer_destroy(r); return nullptr; }
    VkPhysicalDevice* pds = (VkPhysicalDevice*)calloc(pdn, sizeof(VkPhysicalDevice));
    if (!pds) { renderer_destroy(r); return nullptr; }
    r->vk.EnumeratePhysicalDevices(r->instance, &pdn, pds);

    int best = -1; VkPhysicalDeviceProperties bestProps{};
    for (uint32_t i = 0; i < pdn; ++i) {
        uint32_t g, p;
        if (!pick_families(&r->vk, pds[i], r->surface, &g, &p)) continue;
        VkPhysicalDeviceProperties props{};
        r->vk.GetPhysicalDeviceProperties(pds[i], &props);
        int s = device_type_score(props.deviceType);
        if (s > best) { best = s; r->phys = pds[i]; r->gfx_family = g; r->present_family = p; bestProps = props; }
    }
    free(pds);
    if (best < 0) { platform_log("renderer: no device with graphics+present\n"); renderer_destroy(r); return nullptr; }

    // Logical device + queues (VK_KHR_swapchain). Up to two distinct queue families.
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qcis[2]{}; uint32_t qc = 0;
    qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->gfx_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    if (r->present_family != r->gfx_family) {
        qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->present_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    }
    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = qc;
    dci.pQueueCreateInfos       = qcis;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    if (r->vk.CreateDevice(r->phys, &dci, nullptr, &r->device) != VK_SUCCESS) {
        platform_log("renderer: vkCreateDevice failed\n"); renderer_destroy(r); return nullptr;
    }
    vk_load_device(&r->vk, r->device);
    r->vk.GetDeviceQueue(r->device, r->gfx_family, 0, &r->gfx_queue);
    r->vk.GetDeviceQueue(r->device, r->present_family, 0, &r->present_queue);

    // Per-frame command buffers + sync.
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = r->gfx_family;
    r->vk.CreateCommandPool(r->device, &pci, nullptr, &r->cmd_pool);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = r->cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES_IN_FLIGHT;
    r->vk.AllocateCommandBuffers(r->device, &ai, r->cmd);

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->image_available[i]);
        r->vk.CreateFence(r->device, &fci, nullptr, &r->in_flight[i]);
    }

    // First swapchain.
    int32_t w = 0, h = 0; platform_window_size(window, &w, &h);
    create_swapchain(r, (uint32_t)(w > 0 ? w : 1), (uint32_t)(h > 0 ? h : 1));

    const char* kind = bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? "discrete"
                     : bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated" : "other";
    platform_log("renderer: Vulkan 1.3 up | validation=%s | GPU: %s (%s) | swapchain %ux%u x%u\n",
                 validation ? "on" : "off", bestProps.deviceName, kind, r->sc_extent.width, r->sc_extent.height, r->sc_count);
    return r;
}

void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized) {
    if (!r || minimized || fb_width <= 0 || fb_height <= 0) return;
    uint32_t fw = (uint32_t)fb_width, fh = (uint32_t)fb_height;

    if (r->need_recreate || r->swapchain == VK_NULL_HANDLE || fw != r->sc_extent.width || fh != r->sc_extent.height) {
        recreate_swapchain(r, fw, fh);
        if (r->swapchain == VK_NULL_HANDLE) return;   // still minimized
    }

    uint32_t fr = r->frame;
    r->vk.WaitForFences(r->device, 1, &r->in_flight[fr], VK_TRUE, UINT64_MAX);

    uint32_t img = 0;
    VkResult acq = r->vk.AcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->image_available[fr], VK_NULL_HANDLE, &img);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { r->need_recreate = true; return; }   // no image -> semaphore NOT signaled, safe to bail
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) { platform_log("renderer: acquire failed (%d)\n", (int)acq); return; }

    // Don't write an image a prior frame is still presenting.
    if (r->images_in_flight[img] != VK_NULL_HANDLE)
        r->vk.WaitForFences(r->device, 1, &r->images_in_flight[img], VK_TRUE, UINT64_MAX);
    r->images_in_flight[img] = r->in_flight[fr];
    r->vk.ResetFences(r->device, 1, &r->in_flight[fr]);

    // Animated clear color (presentation only — float is fine here).
    double t = (double)r->frame_count * 0.02;
    VkClearColorValue color{};
    color.float32[0] = (float)(0.5 + 0.5 * sin(t));
    color.float32[1] = (float)(0.5 + 0.5 * sin(t + 2.094));
    color.float32[2] = (float)(0.5 + 0.5 * sin(t + 4.188));
    color.float32[3] = 1.0f;

    VkCommandBuffer cb = r->cmd[fr];
    r->vk.ResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r->vk.BeginCommandBuffer(cb, &bi);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; range.levelCount = 1; range.layerCount = 1;

    VkImageMemoryBarrier toClear{};
    toClear.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toClear.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toClear.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image               = r->sc_images[img];
    toClear.subresourceRange    = range;
    toClear.srcAccessMask       = 0;
    toClear.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    r->vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toClear);

    r->vk.CmdClearColorImage(cb, r->sc_images[img], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

    VkImageMemoryBarrier toPresent = toClear;
    toPresent.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    r->vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    r->vk.EndCommandBuffer(cb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &r->image_available[fr];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &r->render_finished[img];
    r->vk.QueueSubmit(r->gfx_queue, 1, &si, r->in_flight[fr]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &r->render_finished[img];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &r->swapchain;
    pi.pImageIndices      = &img;
    VkResult pres = r->vk.QueuePresentKHR(r->present_queue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) r->need_recreate = true;
    else if (pres != VK_SUCCESS) platform_log("renderer: present failed (%d)\n", (int)pres);

    r->frame = (fr + 1) % FRAMES_IN_FLIGHT;
    ++r->frame_count;
}

void renderer_destroy(Renderer* r) {
    if (!r) return;
    if (r->device) r->vk.DeviceWaitIdle(r->device);
    destroy_swapchain(r);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->image_available[i]) r->vk.DestroySemaphore(r->device, r->image_available[i], nullptr);
        if (r->in_flight[i])       r->vk.DestroyFence(r->device, r->in_flight[i], nullptr);
    }
    if (r->cmd_pool) r->vk.DestroyCommandPool(r->device, r->cmd_pool, nullptr);
    if (r->device)   r->vk.DestroyDevice(r->device, nullptr);
    if (r->surface)  r->vk.DestroySurfaceKHR(r->instance, r->surface, nullptr);
    if (r->debug && r->vk.DestroyDebugUtilsMessengerEXT) r->vk.DestroyDebugUtilsMessengerEXT(r->instance, r->debug, nullptr);
    if (r->instance) r->vk.DestroyInstance(r->instance, nullptr);
    free(r);
}
