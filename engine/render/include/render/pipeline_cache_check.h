#pragma once
#include <stdint.h>
#include <stddef.h>
// Validates an on-disk VkPipelineCache blob before it is handed to
// vkCreatePipelineCache as pInitialData (the spec leaves consuming a stale/foreign
// blob to the implementation — drivers usually reject it, but a corrupt file is OUR
// bug to catch, with a log line, not theirs). Deliberately Vulkan-header-free: it
// parses the spec-defined little-endian header layout from raw bytes, so it compiles
// in the null backend and is unit-tested headlessly (tests/render).
//
// VkPipelineCacheHeaderVersionOne (all little-endian):
//   u32 headerSize (== 32)   u32 headerVersion (== 1 / ONE)
//   u32 vendorID             u32 deviceID         u8 pipelineCacheUUID[16]

#define PIPELINE_CACHE_HEADER_SIZE 32u

// True iff `bytes` carries a well-formed v1 header that matches this exact device
// (vendor + device id + the driver's pipelineCacheUUID). A false means: create the
// cache empty and let it be rewritten at shutdown.
bool pipeline_cache_blob_ok(const void* bytes, size_t size,
                            uint32_t vendor_id, uint32_t device_id,
                            const uint8_t uuid[16]);
