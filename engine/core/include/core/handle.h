#pragma once
#include <stdint.h>
#include "core/assert.h"
// THE handle convention (ADR-0003, ARCHITECTURE §2.3): a 32-bit generational handle,
// 18-bit index + 14-bit generation (churn-weighted for the MOBA/RTS hybrid). Every
// typed handle derives from this so index/generation extraction is identical.
//   - generation 0 is NEVER valid; a freshly allocated slot starts at generation 1.
//   - HANDLE_NULL (all zero) is the universal "none" sentinel.
// A stale handle (slot recycled) fails validation deterministically — no allocation
// address ever leaks into sim state. C-style, global (matches eng_core).

typedef uint32_t Handle;

#define HANDLE_INDEX_BITS  18u                      // 262,144 live indices
#define HANDLE_GEN_BITS    14u                      // 16,383 usable generations (gen 0 reserved as null)
#define HANDLE_INDEX_MASK  ((1u << HANDLE_INDEX_BITS) - 1u)   // 0x3FFFF
#define HANDLE_GEN_MASK    ((1u << HANDLE_GEN_BITS)  - 1u)    // 0x3FFF
#define HANDLE_NULL        ((Handle)0)

static inline uint32_t handle_index(Handle h) { return h & HANDLE_INDEX_MASK; }
static inline uint32_t handle_gen  (Handle h) { return (h >> HANDLE_INDEX_BITS) & HANDLE_GEN_MASK; }
static inline Handle   handle_make(uint32_t idx, uint32_t gen) {
    return ((gen & HANDLE_GEN_MASK) << HANDLE_INDEX_BITS) | (idx & HANDLE_INDEX_MASK);
}
static inline bool     handle_is_null(Handle h) { return h == HANDLE_NULL; }

// Typed handles: distinct struct wrappers over the same ABI (compile-time safety, same bits).
#define HANDLE_TYPE(Name) typedef struct Name { Handle h; } Name
HANDLE_TYPE(EntityId);
HANDLE_TYPE(MeshHandle);
HANDLE_TYPE(TextureHandle);
HANDLE_TYPE(MaterialHandle);
HANDLE_TYPE(AssetHandle);
