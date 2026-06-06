#pragma once
// The renderer seam (ADR-0004). The app/game sees ONLY this header — never
// <vulkan/vulkan.h>. M2.0 bring-up; this rung does instance + debug messenger +
// physical-device selection. Surface/swapchain/clear arrive in the next rungs.

typedef struct PlatformWindow PlatformWindow;
typedef struct Renderer Renderer;

// Bring up Vulkan for `window`. Returns NULL if unavailable — the null backend (no
// Vulkan SDK at build time) or a runtime failure (already logged). Caller continues.
Renderer* renderer_create(PlatformWindow* window);
void      renderer_destroy(Renderer* r);

// Render one frame: clear the acquired swapchain image to an animated color and present.
// Pass the current framebuffer size + minimized flag from the platform pump; it handles
// swapchain recreation on resize. No-op for the null backend or while minimized.
void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized);
