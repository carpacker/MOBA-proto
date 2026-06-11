// tools/sandbox — engine bring-up shell: open a Win32 window, run a clean
// non-blocking message loop, render via the renderer seam, exit cleanly.
//   sandbox                       exits when you close the window (or press Esc)
//   sandbox --frames N            auto-quits after N frames (smoke test)
//   sandbox --screenshot out.bmp  captures the LAST frame to a 24-bit BMP (M2.1
//                                 readback — session-independent visual proof)
#include "platform/platform.h"
#include "render/renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Minimal 24-bit bottom-up BMP, same shape tools/visualize writes. `rgba8` rows are
// top-down (renderer_capture contract); BMP wants bottom-up BGR with 4-byte row pad.
static bool write_bmp24(const char* path, const uint8_t* rgba8, int w, int h) {
    if (!rgba8 || w <= 0 || h <= 0) return false;
    const uint32_t row_bytes = ((uint32_t)w * 3u + 3u) & ~3u;
    const uint32_t pixel_bytes = row_bytes * (uint32_t)h;
    const uint32_t off = 14 + 40;

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    uint8_t hdr[14 + 40] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t file_size = off + pixel_bytes;
    std::memcpy(hdr + 2, &file_size, 4);
    std::memcpy(hdr + 10, &off, 4);
    uint32_t info_size = 40; std::memcpy(hdr + 14, &info_size, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixel_bytes, 4);
    bool ok = std::fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);

    uint8_t* row = (uint8_t*)std::calloc(row_bytes, 1);
    if (!row) { std::fclose(f); return false; }
    for (int y = h - 1; ok && y >= 0; --y) {            // BMP rows are bottom-up
        const uint8_t* src = rgba8 + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2];            // B
            row[x * 3 + 1] = src[x * 4 + 1];            // G
            row[x * 3 + 2] = src[x * 4 + 0];            // R
        }
        ok = std::fwrite(row, 1, row_bytes, f) == row_bytes;
    }
    std::free(row);
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    int max_frames = -1;
    const char* screenshot_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshot_path = argv[++i];
    }

    PlatformWindowDesc desc;
    desc.title = "MOBA - sandbox (M2.1)";
    desc.width = 1280; desc.height = 720;
    desc.resizable = true; desc.fullscreen = false;

    PlatformWindow* win = platform_window_open(&desc);
    if (!win) { std::printf("sandbox: failed to open window\n"); return 1; }

    Renderer* rnd = renderer_create(win);
    if (!rnd) std::printf("sandbox: renderer unavailable (null backend / no Vulkan)\n");

    const uint64_t freq = platform_time_frequency();
    uint64_t prev  = platform_time_ticks();
    uint64_t start = prev;
    PlatformFrameInput in;
    long frame = 0;
    double since_print = 0.0;
    bool quit_requested = false;

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

        if (in.keyboard.down[KEY_ESCAPE]) { std::printf("  Esc -> quit\n"); quit_requested = true; }
        if (max_frames >= 0 && frame >= max_frames) {
            std::printf("  reached %d frames -> quit\n", max_frames);
            quit_requested = true;
        }

        if (quit_requested && screenshot_path && rnd) {
            // Last frame: render once more WITH readback and dump it. A transient
            // capture failure (acquire OUT_OF_DATE etc.) just exits without a BMP.
            Arena shot_arena;
            if (platform_arena_reserve(&shot_arena, 64u * 1024 * 1024)) {
                RendererCapture cap;
                if (renderer_capture(rnd, in.fb_width, in.fb_height, in.window_minimized,
                                     arena_allocator(&shot_arena), &cap)) {
                    if (write_bmp24(screenshot_path, cap.rgba8, cap.width, cap.height))
                        std::printf("  screenshot: %s (%dx%d)\n", screenshot_path, cap.width, cap.height);
                    else
                        std::printf("  screenshot: BMP write FAILED (%s)\n", screenshot_path);
                } else {
                    std::printf("  screenshot: capture FAILED\n");
                }
                platform_arena_release(&shot_arena);
            }
        }
        if (quit_requested) break;

        if (rnd) renderer_draw(rnd, in.fb_width, in.fb_height, in.window_minimized);  // vsync paces us
        else     platform_sleep_ms(6);   // no renderer: don't busy-spin
    }

    double elapsed = (double)(platform_time_ticks() - start) / (double)freq;
    renderer_destroy(rnd);
    platform_window_close(win);
    std::printf("sandbox: clean exit after %ld frames (%.2fs)\n", frame, elapsed);
    return 0;
}
