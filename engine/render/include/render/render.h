#pragma once
// eng_render — raw Vulkan behind a thin renderer seam (ADR-0004). All Vulkan calls
// go through a hand-loaded dispatch table; <vulkan/vulkan.h> is confined to
// src/vk/vk.h (Phase 2). Placeholder for the M0.2 spine.

const char* eng_render_version(void);
