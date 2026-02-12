#ifndef ARENA_DYN_H_
#define ARENA_DYN_H_

#include "arena.h"
#include <stdbool.h>
#include <stdint.h>

static inline bool arena_da_reserve(
    Arena *arena,
    void **items,
    size_t *capacity,
    size_t item_size,
    size_t min_capacity
) {
    if (!arena || !items || !capacity || item_size == 0) return false;
    if (min_capacity <= *capacity) return true;

    size_t new_cap = *capacity == 0 ? 8 : *capacity;
    while (new_cap < min_capacity) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < min_capacity) return false;
    if (new_cap > SIZE_MAX / item_size) return false;
    if (*capacity > SIZE_MAX / item_size) return false;

    size_t old_size = (*capacity) * item_size;
    size_t new_size = new_cap * item_size;
    void *new_items = arena_realloc_last(arena, *items, old_size, new_size);
    if (!new_items) return false;

    *items = new_items;
    *capacity = new_cap;
    return true;
}

static inline bool arena_da_reserve_pair(
    Arena *arena,
    void **items_a,
    size_t item_a_size,
    void **items_b,
    size_t item_b_size,
    size_t *capacity,
    size_t min_capacity
) {
    if (!arena || !items_a || !items_b || !capacity || item_a_size == 0 || item_b_size == 0) return false;
    if (min_capacity <= *capacity) return true;

    size_t new_cap = *capacity == 0 ? 8 : *capacity;
    while (new_cap < min_capacity) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < min_capacity) return false;
    if (new_cap > SIZE_MAX / item_a_size) return false;
    if (new_cap > SIZE_MAX / item_b_size) return false;
    if (*capacity > SIZE_MAX / item_a_size) return false;
    if (*capacity > SIZE_MAX / item_b_size) return false;

    size_t old_size_a = (*capacity) * item_a_size;
    size_t new_size_a = new_cap * item_a_size;
    size_t old_size_b = (*capacity) * item_b_size;
    size_t new_size_b = new_cap * item_b_size;

    void *new_a = arena_realloc_last(arena, *items_a, old_size_a, new_size_a);
    if (!new_a) return false;
    void *new_b = arena_realloc_last(arena, *items_b, old_size_b, new_size_b);
    if (!new_b) return false;

    *items_a = new_a;
    *items_b = new_b;
    *capacity = new_cap;
    return true;
}

#endif // ARENA_DYN_H_
