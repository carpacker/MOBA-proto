#pragma once
#include <stdint.h>
#include <stddef.h>
#include "core/mem.h"   // Allocator
// Direct TGA decode for M2.2's textured quad — deliberately the SIMPLEST thing that
// proves the GPU upload path. Lives in the app layer (NOT the engine): file-format
// parsing is the asset module's job, and M4.0 replaces this with the asset module's
// direct RUNTIME TGA parser behind the asset registry (the cooker/.mba path lands in
// M4.1). Roadmap: "bypass the asset manager, which doesn't exist yet". Compiled
// straight into the test exe too (tests/tga).
//
// Accepts exactly: image type 2 (uncompressed true-color), 24 or 32 bpp, top-left or
// bottom-left origin, no color map, no id field beyond skipping it. Everything else
// (RLE, palettes, 15/16 bpp, right-origin, interleaved) hard-rejects — by design,
// not omission. Truncation is rejected BEFORE any allocation (huge claimed dims on a
// tiny file can never reach the caller's arena).

typedef struct TgaImage {
    int      width, height;
    uint8_t* rgba8;          // w*h*4, rows top-down, from the caller's Allocator
} TgaImage;

// Decode `bytes` into RGBA8 (alpha = 255 for 24 bpp). Returns false (out untouched)
// on any unsupported/malformed input.
bool tga_decode(const void* bytes, size_t size, Allocator alloc, TgaImage* out);
