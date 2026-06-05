#pragma once
#include <stdint.h>
#include <stddef.h>
#include "platform/platform_input.h"
// THE OS seam (ADR-0005, ARCHITECTURE §4). Engine modules include only this
// (+ platform_input.h, and later platform_vulkan.h). Win32 impl in src/win32/.
//
// M0.3 surface: window/loop, high-res clock, OS page allocator, log/fatal.
// Deferred to their phases (declared canonically in ARCHITECTURE §4.1):
//   - file I/O into a caller arena (needs core's Allocator)  -> M1.0
//   - dynamic-library load (hand-loads vulkan-1.dll)         -> Phase 2
//   - Vulkan surface creation / loader bootstrap             -> Phase 2
//   - UDP sockets, dir-watch                                 -> Phase 6 / later

typedef struct PlatformWindow PlatformWindow;     // opaque, per-backend
typedef struct PlatformWindowDesc {
    const char* title;
    int32_t     width, height;
    bool        resizable;
    bool        fullscreen;
} PlatformWindowDesc;

// ---- Window / loop ----
PlatformWindow* platform_window_open(const PlatformWindowDesc*);
void            platform_window_close(PlatformWindow*);
bool            platform_pump_events(PlatformWindow*, PlatformFrameInput* out); // false => quit
void            platform_window_size(PlatformWindow*, int32_t* w, int32_t* h);

// ---- High-res monotonic clock (raw ticks; seconds computed engine-side) ----
uint64_t platform_time_ticks(void);          // QueryPerformanceCounter
uint64_t platform_time_frequency(void);
double   platform_time_seconds(uint64_t ticks);
void     platform_sleep_ms(uint32_t ms);

// ---- OS page allocator (the ONLY thing the memory subsystem depends on, §6) ----
typedef struct PlatformMemoryBlock { void* base; size_t committed; size_t reserved; } PlatformMemoryBlock;
PlatformMemoryBlock plat_mem_reserve(size_t reserve_bytes);   // MEM_RESERVE
bool                plat_mem_commit (PlatformMemoryBlock*, size_t new_committed);
void                plat_mem_release(PlatformMemoryBlock*);
size_t              plat_mem_page_size(void);

// ---- Diagnostics ----
void platform_log  (const char* fmt, ...);
void platform_fatal(const char* fmt, ...);     // logs, then aborts the process
