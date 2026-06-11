// M2.1 render tests — the Vulkan-free pipeline-cache blob checker. Crafted byte
// blobs stand in for vkGetPipelineCacheData output; the real header layout is the
// spec's VkPipelineCacheHeaderVersionOne (see pipeline_cache_check.h).
#include "test.h"
#include "render/pipeline_cache_check.h"
#include <cstdint>
#include <cstring>

static const uint32_t VENDOR = 0x10DEu;       // NVIDIA, as on the dev box
static const uint32_t DEVICE = 0x1B81u;       // GTX 1070
static const uint8_t  UUID[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static void put_u32le(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// A valid 32-byte v1 header for VENDOR/DEVICE/UUID, plus `extra` payload bytes.
static size_t make_blob(uint8_t* out, size_t extra) {
    put_u32le(out + 0, PIPELINE_CACHE_HEADER_SIZE);
    put_u32le(out + 4, 1u);                   // VK_PIPELINE_CACHE_HEADER_VERSION_ONE
    put_u32le(out + 8, VENDOR);
    put_u32le(out + 12, DEVICE);
    std::memcpy(out + 16, UUID, 16);
    for (size_t i = 0; i < extra; ++i) out[PIPELINE_CACHE_HEADER_SIZE + i] = (uint8_t)i;
    return PIPELINE_CACHE_HEADER_SIZE + extra;
}

TEST(render, cache_blob_valid_header_accepted) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 64);
    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    // Header-only (no payload) is still structurally valid.
    CHECK(pipeline_cache_blob_ok(blob, PIPELINE_CACHE_HEADER_SIZE, VENDOR, DEVICE, UUID));
}

TEST(render, cache_blob_too_short_rejected) {
    uint8_t blob[256];
    make_blob(blob, 0);
    CHECK(!pipeline_cache_blob_ok(blob, PIPELINE_CACHE_HEADER_SIZE - 1, VENDOR, DEVICE, UUID));
    CHECK(!pipeline_cache_blob_ok(blob, 0, VENDOR, DEVICE, UUID));
    CHECK(!pipeline_cache_blob_ok(nullptr, 64, VENDOR, DEVICE, UUID));
}

TEST(render, cache_blob_bad_header_fields_rejected) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 16);

    put_u32le(blob + 0, 28u);                              // wrong headerSize
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    put_u32le(blob + 0, PIPELINE_CACHE_HEADER_SIZE);

    put_u32le(blob + 4, 2u);                               // unknown headerVersion
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
    put_u32le(blob + 4, 1u);

    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));   // restored -> valid again
}

TEST(render, cache_blob_foreign_device_rejected) {
    uint8_t blob[256];
    size_t n = make_blob(blob, 16);
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR + 1, DEVICE, UUID));   // other vendor
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE + 1, UUID));   // other GPU

    uint8_t other_uuid[16];
    std::memcpy(other_uuid, UUID, 16);
    other_uuid[15] ^= 0xFF;                                // driver update -> new UUID
    CHECK(!pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, other_uuid));
}

TEST(render, cache_blob_unaligned_source_ok) {
    // Disk bytes arrive with no alignment guarantee — the checker must memcpy-read.
    uint8_t backing[256 + 1];
    uint8_t* blob = backing + 1;                           // deliberately misaligned
    size_t n = make_blob(blob, 8);
    CHECK(pipeline_cache_blob_ok(blob, n, VENDOR, DEVICE, UUID));
}
