// M2.2 TGA decoder tests — crafted byte blobs, no files. The decoder is app-layer
// (tools/sandbox/src/tga_direct.cpp, compiled straight into this exe) and provisional
// until M4.0's asset-module parser; these tests pin the contract it must keep.
#include "test.h"
#include "core/mem.h"
#include "tga_direct.h"
#include <cstdint>
#include <cstring>

static Allocator test_alloc(Arena* a, uint8_t* buf, size_t n) {
    arena_init_fixed(a, buf, n);
    return arena_allocator(a);
}

// Build a minimal TGA into `out` (caller-sized): type-2, no id/colormap.
// Returns total size. `pixels` are file-order bytes (BGR or BGRA per bpp).
static size_t make_tga(uint8_t* out, uint16_t w, uint16_t h, uint8_t bpp,
                       bool top_left, const uint8_t* pixels) {
    std::memset(out, 0, 18);
    out[2]  = 2;                                   // uncompressed true-color
    std::memcpy(out + 12, &w, 2);
    std::memcpy(out + 14, &h, 2);
    out[16] = bpp;
    out[17] = (uint8_t)((bpp == 32 ? 0x08 : 0x00) | (top_left ? 0x20 : 0x00));
    size_t n = (size_t)w * h * (bpp / 8);
    std::memcpy(out + 18, pixels, n);
    return 18 + n;
}

TEST(tga, decode_32bpp_top_left_bgra_to_rgba) {
    // 2x1: left = pure blue (BGRA ff,00,00,80), right = pure red (00,00,ff,ff)
    const uint8_t px[] = { 0xFF,0x00,0x00,0x80,  0x00,0x00,0xFF,0xFF };
    uint8_t blob[64];
    size_t n = make_tga(blob, 2, 1, 32, true, px);

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    CHECK(img.width == 2 && img.height == 1);
    const uint8_t want[] = { 0x00,0x00,0xFF,0x80,  0xFF,0x00,0x00,0xFF };   // RGBA
    CHECK(img.rgba8 && std::memcmp(img.rgba8, want, 8) == 0);
}

TEST(tga, decode_24bpp_gets_opaque_alpha) {
    const uint8_t px[] = { 0x10,0x20,0x30 };       // BGR -> RGB 30,20,10
    uint8_t blob[64];
    size_t n = make_tga(blob, 1, 1, 24, true, px);

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    const uint8_t want[] = { 0x30,0x20,0x10,0xFF };
    CHECK(img.rgba8 && std::memcmp(img.rgba8, want, 4) == 0);
}

TEST(tga, bottom_left_origin_is_row_flipped) {
    // 1x2, bottom-left origin: file row 0 is the BOTTOM row. White then black in the
    // file must come out black-then-white top-down.
    const uint8_t px[] = { 0xFF,0xFF,0xFF,  0x00,0x00,0x00 };
    uint8_t blob[64];
    size_t n = make_tga(blob, 1, 2, 24, /*top_left=*/false, px);

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    CHECK(img.rgba8 && img.rgba8[0] == 0x00);      // top row = file's second row
    CHECK(img.rgba8 && img.rgba8[4] == 0xFF);
}

TEST(tga, id_field_is_skipped) {
    uint8_t blob[64];
    const uint8_t px[] = { 0xFF,0x00,0x00,0xFF };  // blue
    size_t n = make_tga(blob, 1, 1, 32, true, px);
    // Rebuild with a 3-byte id field: shift pixel data right, set idLength.
    std::memmove(blob + 21, blob + 18, 4);
    blob[18] = 'i'; blob[19] = 'd'; blob[20] = '!';
    blob[0] = 3;
    n += 3;

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    CHECK(img.rgba8 && img.rgba8[2] == 0xFF);      // still decodes the blue pixel
}

TEST(tga, decode_3x2_24bpp_bottom_left_flip) {
    // 24bpp multi-row AND multi-column: source row stride is width*3 = 9, NOT a
    // multiple of 4. A regression computing the stride as width*4 (BMP-style padding)
    // would pass every 32bpp test (where width*px_bytes == width*4) but fail here.
    // File row 0 is the BOTTOM row (bottom-left origin). Pixels are BGR.
    const uint8_t px[] = {
        0x00,0x00,0xFF,  0x00,0xFF,0x00,  0xFF,0x00,0x00,   // file row 0 (BOTTOM): R,G,B
        0x10,0x20,0x30,  0x40,0x50,0x60,  0x70,0x80,0x90,   // file row 1 (TOP)
    };
    uint8_t blob[64];
    size_t n = make_tga(blob, 3, 2, 24, /*top_left=*/false, px);

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    CHECK(img.width == 3 && img.height == 2);
    const uint8_t want[] = {                            // RGBA, rows top-down, BGR->RGB
        0x30,0x20,0x10,0xFF,  0x60,0x50,0x40,0xFF,  0x90,0x80,0x70,0xFF,   // top row
        0xFF,0x00,0x00,0xFF,  0x00,0xFF,0x00,0xFF,  0x00,0x00,0xFF,0xFF,   // bottom row
    };
    CHECK(img.rgba8 && std::memcmp(img.rgba8, want, sizeof(want)) == 0);
}

TEST(tga, decode_2x2_32bpp_bottom_left_flip) {
    // Multi-row AND multi-column with the default bottom-left origin: file row 0 is
    // the bottom. File: bottom row = [red, green], top row = [blue, white] (BGRA).
    const uint8_t px[] = {
        0x00,0x00,0xFF,0xFF,  0x00,0xFF,0x00,0xFF,   // file row 0 (BOTTOM): red, green
        0xFF,0x00,0x00,0xFF,  0xFF,0xFF,0xFF,0xFF,   // file row 1 (TOP):    blue, white
    };
    uint8_t blob[64];
    size_t n = make_tga(blob, 2, 2, 32, /*top_left=*/false, px);

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(tga_decode(blob, n, al, &img));
    CHECK(img.width == 2 && img.height == 2);
    const uint8_t want[] = {                        // RGBA, rows top-down
        0x00,0x00,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF,   // top:    blue, white
        0xFF,0x00,0x00,0xFF,  0x00,0xFF,0x00,0xFF,   // bottom: red, green
    };
    CHECK(img.rgba8 && std::memcmp(img.rgba8, want, sizeof(want)) == 0);
}

TEST(tga, rejects_unsupported_and_malformed) {
    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    const uint8_t px[] = { 0,0,0,0 };
    uint8_t blob[64];
    size_t n = make_tga(blob, 1, 1, 32, true, px);
    TgaImage img = {}; img.width = -7; img.height = -8; img.rgba8 = (uint8_t*)(uintptr_t)0xBEEF;

    CHECK(!tga_decode(nullptr, n, al, &img));
    CHECK(!tga_decode(blob, 17, al, &img));        // shorter than the header
    CHECK(!tga_decode(blob, n - 1, al, &img));     // truncated pixel data

    blob[2] = 10;                                  // RLE true-color: M4.0, reject now
    CHECK(!tga_decode(blob, n, al, &img));
    blob[2] = 2;

    blob[16] = 16;                                 // 16 bpp unsupported
    CHECK(!tga_decode(blob, n, al, &img));
    blob[16] = 32;

    blob[1] = 1;                                   // color-mapped
    CHECK(!tga_decode(blob, n, al, &img));
    blob[1] = 0;

    blob[17] |= 0x10;                              // right-to-left origin
    CHECK(!tga_decode(blob, n, al, &img));
    blob[17] &= (uint8_t)~0x10;

    blob[17] |= 0x40;                              // two-way interleave (deprecated)
    CHECK(!tga_decode(blob, n, al, &img));
    blob[17] &= (uint8_t)~0x40;

    // out untouched across ALL failures — every field, not just width.
    CHECK(img.width == -7 && img.height == -8 && img.rgba8 == (uint8_t*)(uintptr_t)0xBEEF);
    CHECK(tga_decode(blob, n, al, &img));          // restored blob decodes again
}

TEST(tga, zero_dimensions_rejected) {
    uint8_t blob[64];
    const uint8_t px[] = { 0,0,0,0 };
    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};

    size_t n = make_tga(blob, 1, 1, 32, true, px);
    blob[12] = 0; blob[13] = 0;                    // width 0
    CHECK(!tga_decode(blob, n, al, &img));

    n = make_tga(blob, 1, 1, 32, true, px);
    blob[14] = 0; blob[15] = 0;                    // height 0
    CHECK(!tga_decode(blob, n, al, &img));
}

TEST(tga, id_field_truncation_rejected) {
    // idLength pushes data_off past the file end: the `size < data_off` clause.
    uint8_t blob[64];
    const uint8_t px[] = { 0,0,0,0 };
    size_t n = make_tga(blob, 1, 1, 32, true, px);   // 22 bytes total
    blob[0] = 200;                                   // claims a 200-byte id field

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(!tga_decode(blob, n, al, &img));
}

TEST(tga, huge_claimed_dims_reject_before_allocating) {
    // A 22-byte file claiming 0xFFFF x 0xFFFF (a ~17 GB decode) must be rejected by
    // the u64 truncation check BEFORE any allocation — pushing that into a fixed
    // arena would hard-abort by the M1.0 OOM policy. The untouched arena offset
    // proves no allocation happened.
    uint8_t blob[64];
    const uint8_t px[] = { 0,0,0,0 };
    size_t n = make_tga(blob, 1, 1, 32, true, px);
    blob[12] = 0xFF; blob[13] = 0xFF;              // width  65535
    blob[14] = 0xFF; blob[15] = 0xFF;              // height 65535

    alignas(16) uint8_t mem[256];
    Arena a; Allocator al = test_alloc(&a, mem, sizeof(mem));
    TgaImage img = {};
    CHECK(!tga_decode(blob, n, al, &img));
    CHECK(a.offset == 0);                          // rejected before mem_alloc
}
