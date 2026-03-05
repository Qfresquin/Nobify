#ifndef ARENA_DYN_H_
#define ARENA_DYN_H_

#include "arena.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARENA_ARR_INIT_CAPACITY 8

typedef struct {
    size_t capacity;
    size_t count;
    max_align_t _align;
} Arena_Arr_Header;

static inline Arena_Arr_Header *arena_arr__hdr_mut(void *arr) {
    if (!arr) return NULL;
    return &((Arena_Arr_Header*)arr)[-1];
}

static inline const Arena_Arr_Header *arena_arr__hdr_const(const void *arr) {
    if (!arr) return NULL;
    return &((const Arena_Arr_Header*)arr)[-1];
}

#define arena_arr__hdr(a) \
    _Generic((a), \
        const void *: arena_arr__hdr_const((const void*)(a)), \
        const char *: arena_arr__hdr_const((const void*)(a)), \
        const unsigned char *: arena_arr__hdr_const((const void*)(a)), \
        default: arena_arr__hdr_mut((void*)(a)))

#define arena_arr_len(a) ((a) ? arena_arr__hdr_const((const void*)(a))->count : 0u)
#define arena_arr_cap(a) ((a) ? arena_arr__hdr_const((const void*)(a))->capacity : 0u)
#define arena_arr_empty(a) (arena_arr_len(a) == 0u)
#define arena_arr_last(a) ((a)[arena_arr_len(a) - 1u])
#define arena_arr_set_len(a, n) ((void)(arena_arr__hdr_mut((void*)(a))->count = (n)))
#define arena_arr_free(a) ((void)(a))

// Legacy compatibility helper used by v2 tests that still rely on the
// pre-header dynamic array contract.
static inline bool arena_da_reserve(
    Arena *arena,
    void **items,
    size_t *capacity,
    size_t item_size,
    size_t min_count
) {
    if (!arena || !items || !capacity || item_size == 0) return false;
    if (min_count <= *capacity) return true;

    size_t new_cap = *capacity ? *capacity : ARENA_ARR_INIT_CAPACITY;
    while (new_cap < min_count) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_count;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < min_count) return false;
    if (new_cap > (SIZE_MAX / item_size)) return false;

    void *new_items = arena_alloc(arena, new_cap * item_size);
    if (!new_items) return false;

    if (*items && *capacity > 0) {
        memcpy(new_items, *items, (*capacity) * item_size);
    }

    *items = new_items;
    *capacity = new_cap;
    return true;
}

static inline bool arena_arr__grow(
    Arena *arena,
    void **arr,
    size_t item_size,
    size_t min_capacity
) {
    if (!arena || !arr || item_size == 0) return false;

    size_t current_cap = arena_arr_cap(*arr);
    if (min_capacity <= current_cap) return true;

    size_t new_cap = current_cap == 0 ? ARENA_ARR_INIT_CAPACITY : current_cap;
    while (new_cap < min_capacity) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < min_capacity) return false;
    if (new_cap > (SIZE_MAX - sizeof(Arena_Arr_Header)) / item_size) return false;

    size_t old_count = arena_arr_len(*arr);
    size_t old_cap = current_cap;
    size_t old_bytes = old_cap == 0 ? 0 : sizeof(Arena_Arr_Header) + (old_cap * item_size);
    size_t new_bytes = sizeof(Arena_Arr_Header) + (new_cap * item_size);
    Arena_Arr_Header *old_hdr = arena_arr__hdr_mut(*arr);
    Arena_Arr_Header *new_hdr = arena_realloc_last(arena, old_hdr, old_bytes, new_bytes);
    if (!new_hdr) return false;

    new_hdr->capacity = new_cap;
    new_hdr->count = old_count;
    *arr = (void*)(new_hdr + 1);
    return true;
}

static inline void *arena_arr__append_n(
    Arena *arena,
    void **arr,
    size_t item_size,
    size_t count
) {
    if (!arr || item_size == 0) return NULL;
    if (count == 0) return *arr;

    size_t old_count = arena_arr_len(*arr);
    if (count > SIZE_MAX - old_count) return NULL;
    size_t new_count = old_count + count;
    if (!arena_arr__grow(arena, arr, item_size, new_count)) return NULL;

    Arena_Arr_Header *hdr = arena_arr__hdr_mut(*arr);
    void *slot = (unsigned char*)(*arr) + (old_count * item_size);
    hdr->count = new_count;
    return slot;
}

static inline bool arena_arr__fit_impl(Arena *arena, void **arr, size_t item_size) {
    if (!arena || !arr || !*arr || item_size == 0) return false;

    size_t count = arena_arr_len(*arr);
    size_t cap = arena_arr_cap(*arr);
    if (count == cap) return true;

    size_t old_bytes = sizeof(Arena_Arr_Header) + (cap * item_size);
    size_t new_bytes = sizeof(Arena_Arr_Header) + (count * item_size);
    Arena_Arr_Header *old_hdr = arena_arr__hdr_mut(*arr);
    Arena_Arr_Header *new_hdr = arena_realloc_last(arena, old_hdr, old_bytes, new_bytes);
    if (!new_hdr) return false;
    new_hdr->capacity = count;
    *arr = (void*)(new_hdr + 1);
    return true;
}

#define arena_arr_reserve(arena, arr, min_capacity) \
    arena_arr__grow((arena), (void**)&(arr), sizeof(*(arr)), (min_capacity))

#define arena_arr_push_n(arena, arr, n) \
    ((__typeof__(arr))arena_arr__append_n((arena), (void**)&(arr), sizeof(*(arr)), (n)))

#define arena_arr_push(arena, arr, value) \
    ({ \
        __typeof__(arr) arena_arr__slot = arena_arr_push_n((arena), (arr), 1u); \
        bool arena_arr__ok = (arena_arr__slot != NULL); \
        if (arena_arr__ok) arena_arr__slot[0] = (value); \
        arena_arr__ok; \
    })

#define arena_arr_fit(arena, arr) \
    arena_arr__fit_impl((arena), (void**)&(arr), sizeof(*(arr)))

#endif // ARENA_DYN_H_
