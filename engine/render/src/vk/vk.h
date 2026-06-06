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
    PFN_vkGetDeviceProcAddr                          GetDeviceProcAddr;
    PFN_vkDestroyInstance                            DestroyInstance;
    PFN_vkEnumeratePhysicalDevices                   EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties                GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties     GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkEnumerateDeviceExtensionProperties         EnumerateDeviceExtensionProperties;
    PFN_vkCreateDevice                               CreateDevice;
    PFN_vkDestroySurfaceKHR                          DestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR         GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR    GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR         GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR    GetPhysicalDeviceSurfacePresentModesKHR;
    // VK_EXT_debug_utils (optional — only if the extension was enabled)
    PFN_vkCreateDebugUtilsMessengerEXT  CreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT;

    // --- device tier (resolved via vkGetDeviceProcAddr — skips the loader trampoline) ---
    PFN_vkDestroyDevice          DestroyDevice;
    PFN_vkGetDeviceQueue         GetDeviceQueue;
    PFN_vkDeviceWaitIdle         DeviceWaitIdle;
    PFN_vkCreateSwapchainKHR     CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR    DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR  GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR    AcquireNextImageKHR;
    PFN_vkQueuePresentKHR        QueuePresentKHR;
    PFN_vkCreateSemaphore        CreateSemaphore;
    PFN_vkDestroySemaphore       DestroySemaphore;
    PFN_vkCreateFence            CreateFence;
    PFN_vkDestroyFence           DestroyFence;
    PFN_vkWaitForFences          WaitForFences;
    PFN_vkResetFences            ResetFences;
    PFN_vkCreateCommandPool      CreateCommandPool;
    PFN_vkDestroyCommandPool     DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer     BeginCommandBuffer;
    PFN_vkEndCommandBuffer       EndCommandBuffer;
    PFN_vkResetCommandBuffer     ResetCommandBuffer;
    PFN_vkQueueSubmit            QueueSubmit;
    PFN_vkCmdPipelineBarrier     CmdPipelineBarrier;
    PFN_vkCmdClearColorImage     CmdClearColorImage;
};

// Resolve the global tier from the platform loader. Fatal on a missing core proc.
bool vk_load_global(Vk* vk, PFN_vkGetInstanceProcAddr gipa);
// Resolve the instance tier. Fatal on a missing core proc; debug-utils stay nullable.
bool vk_load_instance(Vk* vk, VkInstance instance);
// Resolve the device tier (after vkCreateDevice). Fatal on a missing proc.
bool vk_load_device(Vk* vk, VkDevice device);
