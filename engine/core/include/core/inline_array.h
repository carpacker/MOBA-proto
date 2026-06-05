#pragma once
#include <stdint.h>
#include "core/assert.h"
// InlineArray<T,N> — fixed-capacity array with NO heap allocation (storage inline).
// For small, bounded collections on the stack / inside structs (ARCHITECTURE §6.4).

template<class T, uint32_t N>
struct InlineArray {
    T        data[N];
    uint32_t len;
};

template<class T, uint32_t N> inline void inline_array_init(InlineArray<T,N>* a) { a->len = 0; }
template<class T, uint32_t N> inline uint32_t inline_array_cap(const InlineArray<T,N>*) { return N; }
template<class T, uint32_t N> inline bool inline_array_full(const InlineArray<T,N>* a) { return a->len >= N; }
template<class T, uint32_t N> inline T* inline_array_push(InlineArray<T,N>* a, T v) {
    ENSURE_MSG(a->len < N, "InlineArray overflow");
    a->data[a->len] = v;
    return &a->data[a->len++];
}
template<class T, uint32_t N> inline T inline_array_pop(InlineArray<T,N>* a) {
    ASSERT(a->len > 0);
    return a->data[--a->len];
}
template<class T, uint32_t N> inline void inline_array_remove_swap(InlineArray<T,N>* a, uint32_t i) {
    ASSERT(i < a->len);
    a->data[i] = a->data[--a->len];
}
template<class T, uint32_t N> inline void inline_array_clear(InlineArray<T,N>* a) { a->len = 0; }
