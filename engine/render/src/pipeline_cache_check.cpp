#include "render/pipeline_cache_check.h"
#include <string.h>

// memcpy-read: the blob comes from disk with no alignment guarantee.
static uint32_t read_u32le(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);   // x64 is little-endian; revisit if a BE port ever lands
    return v;
}

bool pipeline_cache_blob_ok(const void* bytes, size_t size,
                            uint32_t vendor_id, uint32_t device_id,
                            const uint8_t uuid[16]) {
    if (!bytes || !uuid || size < PIPELINE_CACHE_HEADER_SIZE) return false;
    const uint8_t* p = (const uint8_t*)bytes;
    if (read_u32le(p + 0) != PIPELINE_CACHE_HEADER_SIZE) return false;  // headerSize
    if (read_u32le(p + 4) != 1u) return false;   // VK_PIPELINE_CACHE_HEADER_VERSION_ONE
    if (read_u32le(p + 8) != vendor_id) return false;
    if (read_u32le(p + 12) != device_id) return false;
    return memcmp(p + 16, uuid, 16) == 0;        // driver build changed -> UUID differs
}
