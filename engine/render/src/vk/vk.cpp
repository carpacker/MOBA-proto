#include "vk/vk.h"
#include "platform/platform.h"   // platform_fatal
#include <cstring>

// Resolve a proc or die loudly at load time (ADR-0004): a mis-resolved pointer should
// fail here with a clear message, never crash opaquely mid-frame.
#define VK_GLOBAL(field, name) do {                                              \
        vk->field = (PFN_##name)vk->GetInstanceProcAddr(nullptr, #name);         \
        if (!vk->field) { platform_fatal("vk: global proc %s not found\n", #name); return false; } \
    } while (0)

#define VK_INSTANCE(field, name) do {                                            \
        vk->field = (PFN_##name)vk->GetInstanceProcAddr(instance, #name);        \
        if (!vk->field) { platform_fatal("vk: instance proc %s not found\n", #name); return false; } \
    } while (0)

bool vk_load_global(Vk* vk, PFN_vkGetInstanceProcAddr gipa) {
    memset(vk, 0, sizeof(*vk));
    vk->GetInstanceProcAddr = gipa;
    VK_GLOBAL(CreateInstance,                       vkCreateInstance);
    VK_GLOBAL(EnumerateInstanceLayerProperties,     vkEnumerateInstanceLayerProperties);
    VK_GLOBAL(EnumerateInstanceExtensionProperties, vkEnumerateInstanceExtensionProperties);
    return true;
}

bool vk_load_instance(Vk* vk, VkInstance instance) {
    VK_INSTANCE(DestroyInstance,                        vkDestroyInstance);
    VK_INSTANCE(EnumeratePhysicalDevices,               vkEnumeratePhysicalDevices);
    VK_INSTANCE(GetPhysicalDeviceProperties,            vkGetPhysicalDeviceProperties);
    VK_INSTANCE(GetPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
    // VK_EXT_debug_utils is present only when the extension was enabled — nullable.
    vk->CreateDebugUtilsMessengerEXT  = (PFN_vkCreateDebugUtilsMessengerEXT) vk->GetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    vk->DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vk->GetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    return true;
}
