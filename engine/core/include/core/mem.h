#pragma once
#include <stdint.h>
#include <stddef.h>
#include <new>            // placement new (ARENA_NEW)
#include "core/assert.h"
// Memory foundation (ARCHITECTURE §6): "arenas everywhere, heap rarely".
// eng_core is a pure leaf; the OS page backend lives in eng_platform (ADR-0005) and
// is injected into virtual arenas via a commit callback, so core never calls the OS
// and stays headless-testable.

// ---- Allocator: tagged function-pointer struct, not a vtable (§6.1) ----
typedef enum AllocKind { ALLOC_ARENA, ALLOC_STACK, ALLOC_POOL, ALLOC_HEAP } AllocKind;
// One fn handles alloc (ptr==null), free (new_size==0), realloc (both set).
typedef void* (*AllocFn)(void* state, void* ptr, size_t old_size, size_t new_size, size_t align);
typedef struct Allocator { AllocFn fn; void* state; AllocKind kind; } Allocator;

#define MEM_DEFAULT_ALIGN ((size_t)16)   // any scalar / SSE

static inline void* mem_alloc  (Allocator a, size_t n, size_t al)            { return a.fn(a.state, nullptr, 0, n, al); }
static inline void* mem_realloc(Allocator a, void* p, size_t os, size_t ns, size_t al) { return a.fn(a.state, p, os, ns, al); }
static inline void  mem_free   (Allocator a, void* p, size_t n)              { a.fn(a.state, p, n, 0, 0); }

// ---- Arena: linear bump; O(1) reset; commits pages on growth via injected fn ----
// commit(base, new_committed) grows the committed region; returns false on failure.
// A null commit fn => the block is already fully committed (a fixed buffer).
typedef bool (*ArenaCommitFn)(void* base, size_t new_committed);

typedef struct Arena {
    uint8_t*      base;
    size_t        offset;
    size_t        committed;
    size_t        reserved;
    size_t        high_water;          // always-on (even release): catches under-budgeting
    ArenaCommitFn commit;              // nullable
    size_t        commit_granularity;  // page size for virtual arenas; 0 for fixed
} Arena;

void  arena_init      (Arena*, void* base, size_t reserved, size_t committed, ArenaCommitFn, size_t commit_granularity);
void  arena_init_fixed(Arena*, void* buffer, size_t size);    // fully-committed block
void* arena_push      (Arena*, size_t size, size_t align);
void* arena_push_zero (Arena*, size_t size, size_t align);
void  arena_reset     (Arena*);                               // O(1): offset=0 (keeps committed)
Allocator arena_allocator(Arena*);

#define ARENA_NEW(a, T)            (new (arena_push((a), sizeof(T), alignof(T))) T)
#define ARENA_PUSH_ARRAY(a, T, n)  ((T*)arena_push_zero((a), sizeof(T) * (size_t)(n), alignof(T)))

// ---- Nested temporary lifetimes: save/restore the arena offset ----
typedef struct TempMemory { Arena* arena; size_t saved_offset; } TempMemory;
TempMemory temp_begin(Arena*);
void       temp_end(TempMemory);

// ---- Double-buffered per-frame scratch (swap each frame; reset the new current) ----
typedef struct ScratchPad { Arena a[2]; int cur; } ScratchPad;
Arena* scratch_current   (ScratchPad*);
void   scratch_next_frame(ScratchPad*);   // swap, then reset the now-current arena
