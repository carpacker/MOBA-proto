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

#define VK_DEVICE(field, name) do {                                              \
        vk->field = (PFN_##name)vk->GetDeviceProcAddr(device, #name);            \
        if (!vk->field) { platform_fatal("vk: device proc %s not found\n", #name); return false; } \
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
    VK_INSTANCE(GetDeviceProcAddr,                       vkGetDeviceProcAddr);
    VK_INSTANCE(DestroyInstance,                         vkDestroyInstance);
    VK_INSTANCE(EnumeratePhysicalDevices,                vkEnumeratePhysicalDevices);
    VK_INSTANCE(GetPhysicalDeviceProperties,             vkGetPhysicalDeviceProperties);
    VK_INSTANCE(GetPhysicalDeviceQueueFamilyProperties,  vkGetPhysicalDeviceQueueFamilyProperties);
    VK_INSTANCE(EnumerateDeviceExtensionProperties,      vkEnumerateDeviceExtensionProperties);
    VK_INSTANCE(CreateDevice,                            vkCreateDevice);
    VK_INSTANCE(DestroySurfaceKHR,                       vkDestroySurfaceKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceSupportKHR,      vkGetPhysicalDeviceSurfaceSupportKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR,      vkGetPhysicalDeviceSurfaceFormatsKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfacePresentModesKHR, vkGetPhysicalDeviceSurfacePresentModesKHR);
    // VK_EXT_debug_utils is present only when the extension was enabled — nullable.
    vk->CreateDebugUtilsMessengerEXT  = (PFN_vkCreateDebugUtilsMessengerEXT) vk->GetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    vk->DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vk->GetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    return true;
}

bool vk_load_device(Vk* vk, VkDevice device) {
    VK_DEVICE(DestroyDevice,          vkDestroyDevice);
    VK_DEVICE(GetDeviceQueue,         vkGetDeviceQueue);
    VK_DEVICE(DeviceWaitIdle,         vkDeviceWaitIdle);
    VK_DEVICE(CreateSwapchainKHR,     vkCreateSwapchainKHR);
    VK_DEVICE(DestroySwapchainKHR,    vkDestroySwapchainKHR);
    VK_DEVICE(GetSwapchainImagesKHR,  vkGetSwapchainImagesKHR);
    VK_DEVICE(AcquireNextImageKHR,    vkAcquireNextImageKHR);
    VK_DEVICE(QueuePresentKHR,        vkQueuePresentKHR);
    VK_DEVICE(CreateSemaphore,        vkCreateSemaphore);
    VK_DEVICE(DestroySemaphore,       vkDestroySemaphore);
    VK_DEVICE(CreateFence,            vkCreateFence);
    VK_DEVICE(DestroyFence,           vkDestroyFence);
    VK_DEVICE(WaitForFences,          vkWaitForFences);
    VK_DEVICE(ResetFences,            vkResetFences);
    VK_DEVICE(CreateCommandPool,      vkCreateCommandPool);
    VK_DEVICE(DestroyCommandPool,     vkDestroyCommandPool);
    VK_DEVICE(AllocateCommandBuffers, vkAllocateCommandBuffers);
    VK_DEVICE(BeginCommandBuffer,     vkBeginCommandBuffer);
    VK_DEVICE(EndCommandBuffer,       vkEndCommandBuffer);
    VK_DEVICE(ResetCommandBuffer,     vkResetCommandBuffer);
    VK_DEVICE(QueueSubmit,            vkQueueSubmit);
    VK_DEVICE(CmdPipelineBarrier,     vkCmdPipelineBarrier);
    VK_DEVICE(CmdClearColorImage,     vkCmdClearColorImage);
    return true;
}
