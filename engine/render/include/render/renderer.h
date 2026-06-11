#pragma once
#include <stdint.h>
#include "core/mem.h"   // Allocator (capture output is caller-allocated)
// The renderer seam (ADR-0004). The app/game sees ONLY this header — never
// <vulkan/vulkan.h>. M2.1: graphics pipeline + first triangle over dynamic
// rendering/synchronization2 (ADR-0012 minimum spec), offline SPIR-V (ADR-0008),
// an on-disk pipeline cache, and a readback capture for screenshots/tests.

typedef struct PlatformWindow PlatformWindow;
typedef struct Renderer Renderer;

// Bring up Vulkan for `window`. Returns NULL if unavailable — the null backend (no
// Vulkan SDK at build time), a runtime failure, or a device below the ADR-0012
// minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2). Already logged;
// caller continues without a renderer.
Renderer* renderer_create(PlatformWindow* window);
void      renderer_destroy(Renderer* r);

// Render one frame: clear to an animated color, draw the registered pipelines (M2.1:
// the triangle), present. Pass the current framebuffer size + minimized flag from the
// platform pump; swapchain recreation on resize is handled inside. No-op for the null
// backend or while minimized.
void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized);

// Render one frame AND read its pixels back (slow path — screenshots/visual tests).
// On success fills `out` with a w*h*4 RGBA8 image (rows top-down) allocated from
// `alloc`. Fails (false, logged) on the null backend, while minimized, when the
// swapchain can't be a transfer source, or on a transient acquire failure — callers
// may simply retry next frame.
typedef struct RendererCapture {
    int      width, height;
    uint8_t* rgba8;
} RendererCapture;
bool renderer_capture(Renderer* r, int fb_width, int fb_height, bool minimized,
                      Allocator alloc, RendererCapture* out);
