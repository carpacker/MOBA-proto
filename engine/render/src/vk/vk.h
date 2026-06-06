#pragma once
// The hand-loaded Vulkan dispatch table (ADR-0004). <vulkan/vulkan.h> is confined to
// this file (+ vk.cpp / renderer.cpp). Every Vulkan call goes through `Vk` (vk.CreateX)
// — never a presumed-linked vkCreateX — and each pointer is null-checked at load time
// so a mis-resolved entry fails loudly, not mid-frame.

// VK_NO_PROTOTYPES: do not declare the vk* prototypes (they'd want vulkan-1.lib at link
// time; we resolve every pointer ourselves). WIN32 surface types come in via the define.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4255)   // () -> (void) in Windows headers pulled by vulkan_win32.h
#endif
#include <vulkan/vulkan.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

// The dispatch table, filled in tiers. Names drop the vk prefix: vk.CreateInstance(...).
struct Vk {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;

    // --- global tier (resolved against a NULL instance) ---
    PFN_vkCreateInstance                       CreateInstance;
    PFN_vkEnumerateInstanceLayerProperties     EnumerateInstanceLayerProperties;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;

    // --- instance tier (resolved after vkCreateInstance) ---
    PFN_vkDestroyInstance                         DestroyInstance;
    PFN_vkEnumeratePhysicalDevices                EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties             GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties  GetPhysicalDeviceQueueFamilyProperties;
    // VK_EXT_debug_utils (optional — only if the layer/extension is present)
    PFN_vkCreateDebugUtilsMessengerEXT  CreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT;
};

// Resolve the global tier from the platform loader. Fatal on a missing core proc.
bool vk_load_global(Vk* vk, PFN_vkGetInstanceProcAddr gipa);
// Resolve the instance tier. Fatal on a missing core proc; debug-utils stay nullable.
bool vk_load_instance(Vk* vk, VkInstance instance);
