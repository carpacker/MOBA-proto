#pragma once
// Platform's Vulkan seam (ADR-0004/0005, Phase 2). Deliberately Vulkan-HEADER-FREE so
// non-render code can include it: the loader is handed back as a void* (the renderer
// casts it to PFN_vkGetInstanceProcAddr), and surfaces use opaque handles. Only the
// renderer translates these to real Vulkan types.

typedef struct PlatformWindow PlatformWindow;

// A generic Vulkan entry-point pointer. Function-pointer typedef (not void*) so casting
// to/from PFN_vkGetInstanceProcAddr is fn-ptr -> fn-ptr — avoids the object<->function
// pointer cast that trips /W4 C4152 under /WX.
typedef void (*PlatformVkProc)(void);

// Hand-load vulkan-1.dll and return vkGetInstanceProcAddr (ADR-0004). NULL on failure
// (no Vulkan runtime). Cached: safe to call repeatedly.
PlatformVkProc platform_vk_get_loader(void);

// Create a VkSurfaceKHR for the window (HWND stays in platform; ADR-0005). `instance`
// is a VkInstance, `out_surface` receives a VkSurfaceKHR — both as opaque 64-bit
// handles so this header needs no Vulkan headers. Returns false on failure.
// (Arrives with the swapchain rung; declared here so the seam is settled.)
bool platform_vk_create_surface(PlatformWindow* window, void* instance, unsigned long long* out_surface);
