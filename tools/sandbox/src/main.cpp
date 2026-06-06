// tools/sandbox — M0.3 bring-up: open a Win32 window, run a clean non-blocking
// message loop, print frame stats, exit cleanly. No engine, no Vulkan yet.
//   sandbox                exits when you close the window (or press Esc)
//   sandbox --frames N     auto-quits after N frames (smoke test)
#include "platform/platform.h"
#include "render/renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    int max_frames = -1;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = std::atoi(argv[++i]);

    PlatformWindowDesc desc;
    desc.title = "MOBA - sandbox (M0.3)";
    desc.width = 1280; desc.height = 720;
    desc.resizable = true; desc.fullscreen = false;

    PlatformWindow* win = platform_window_open(&desc);
    if (!win) { std::printf("sandbox: failed to open window\n"); return 1; }

    // M2.0: bring up the renderer (logs the GPU; null backend when built without Vulkan).
    Renderer* rnd = renderer_create(win);
    if (!rnd) std::printf("sandbox: renderer unavailable (null backend / no Vulkan)\n");

    const uint64_t freq = platform_time_frequency();
    uint64_t prev  = platform_time_ticks();
    uint64_t start = prev;
    PlatformFrameInput in;
    long frame = 0;
    double since_print = 0.0;

    std::printf("sandbox: window open (%dx%d). %s\n", desc.width, desc.height,
                max_frames >= 0 ? "auto-quit mode" : "close the window or press Esc to exit");

    while (platform_pump_events(win, &in)) {
        uint64_t now = platform_time_ticks();
        double dt = (double)(now - prev) / (double)freq;
        prev = now;
        ++frame;

        since_print += dt;
        if (since_print >= 0.25) {   // ~4 status lines/sec
            std::printf("  frame %ld  dt=%.2fms  size=%dx%d  focus=%d  min=%d\n",
                        frame, dt * 1000.0, in.fb_width, in.fb_height,
                        (int)in.window_focused, (int)in.window_minimized);
            since_print = 0.0;
        }

        if (in.keyboard.down[KEY_ESCAPE]) { std::printf("  Esc -> quit\n"); break; }
        if (max_frames >= 0 && frame >= max_frames) {
            std::printf("  reached %d frames -> quit\n", max_frames);
            break;
        }
        platform_sleep_ms(6);        // crude pacing for M0.3 (no vsync until Vulkan)
    }

    double elapsed = (double)(platform_time_ticks() - start) / (double)freq;
    renderer_destroy(rnd);
    platform_window_close(win);
    std::printf("sandbox: clean exit after %ld frames (%.2fs)\n", frame, elapsed);
    return 0;
}
