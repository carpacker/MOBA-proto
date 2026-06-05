#pragma once
// eng_platform — the single OS seam (ADR-0005): windowing/input/timing, page
// allocator, arena-owned file I/O, DLL load, UDP sockets, Vulkan surface + loader
// bootstrap, and the headless server run-loop. Win32 impl in src/win32/ (M0.3+).
// Placeholder for the M0.2 spine.

const char* eng_platform_version(void);
