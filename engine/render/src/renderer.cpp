#include "render/renderer.h"
#include "vk/vk.h"
#include "platform/platform.h"          // platform_log
#include "platform/platform_vulkan.h"   // platform_vk_get_loader
#include <cstdlib>
#include <cstring>

struct Renderer {
    Vk                       vk;
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug;
    VkPhysicalDevice         phys;
};

// Validation messages: log WARN/ERROR, never abort the triggering call (return FALSE).
// Best-practices/performance noise is filtered out by the messenger's severity mask.
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

// Higher is better; -1 means "no graphics queue" (unusable). Discrete >> integrated.
static int score_device(Vk* vk, VkPhysicalDevice pd, VkPhysicalDeviceProperties* out) {
    vk->GetPhysicalDeviceProperties(pd, out);
    uint32_t qn = 0; vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    VkQueueFamilyProperties* qf = (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(VkQueueFamilyProperties));
    if (!qf) return -1;
    vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    bool gfx = false;
    for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfx = true; break; }
    free(qf);
    if (!gfx) return -1;
    if (out->deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)        return 1000;
    if (out->deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)      return 100;
    return 10;
}

Renderer* renderer_create(PlatformWindow* window) {
    (void)window;   // surface/swapchain arrive in the next rung
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_loader();
    if (!gipa) { platform_log("renderer: no Vulkan loader (vulkan-1.dll missing?)\n"); return nullptr; }

    Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));
    if (!r) return nullptr;
    if (!vk_load_global(&r->vk, gipa)) { free(r); return nullptr; }

    const bool validation = has_layer(&r->vk, "VK_LAYER_KHRONOS_validation");
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char* exts[]   = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VkApplicationInfo app{};
    app.sType        = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "moba";
    app.apiVersion   = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    if (validation) { ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers; }

    VkResult res = r->vk.CreateInstance(&ci, nullptr, &r->instance);
    if (res != VK_SUCCESS) { platform_log("renderer: vkCreateInstance failed (%d)\n", (int)res); free(r); return nullptr; }
    vk_load_instance(&r->vk, r->instance);

    if (validation && r->vk.CreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debug_cb;
        r->vk.CreateDebugUtilsMessengerEXT(r->instance, &dci, nullptr, &r->debug);
    }

    // Pick the best physical device with a graphics queue.
    uint32_t pdn = 0; r->vk.EnumeratePhysicalDevices(r->instance, &pdn, nullptr);
    if (pdn == 0) { platform_log("renderer: no Vulkan physical devices\n"); renderer_destroy(r); return nullptr; }
    VkPhysicalDevice* pds = (VkPhysicalDevice*)calloc(pdn, sizeof(VkPhysicalDevice));
    if (!pds) { renderer_destroy(r); return nullptr; }
    r->vk.EnumeratePhysicalDevices(r->instance, &pdn, pds);

    int best = -1; VkPhysicalDeviceProperties bestProps{};
    for (uint32_t i = 0; i < pdn; ++i) {
        VkPhysicalDeviceProperties p{};
        int s = score_device(&r->vk, pds[i], &p);
        if (s > best) { best = s; r->phys = pds[i]; bestProps = p; }
    }
    free(pds);
    if (best < 0) { platform_log("renderer: no device with a graphics queue\n"); renderer_destroy(r); return nullptr; }

    const char* kind = bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? "discrete"
                     : bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated" : "other";
    platform_log("renderer: Vulkan %u.%u up | validation=%s | GPU: %s (%s, driver API %u.%u.%u)\n",
                 VK_API_VERSION_MAJOR(app.apiVersion), VK_API_VERSION_MINOR(app.apiVersion),
                 validation ? "on" : "off", bestProps.deviceName, kind,
                 VK_API_VERSION_MAJOR(bestProps.apiVersion), VK_API_VERSION_MINOR(bestProps.apiVersion), VK_API_VERSION_PATCH(bestProps.apiVersion));
    return r;
}

void renderer_destroy(Renderer* r) {
    if (!r) return;
    if (r->debug && r->vk.DestroyDebugUtilsMessengerEXT) r->vk.DestroyDebugUtilsMessengerEXT(r->instance, r->debug, nullptr);
    if (r->instance) r->vk.DestroyInstance(r->instance, nullptr);
    free(r);
}
