#include "arena.h"
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h> // max_align_t

// Each block stores its metadata header immediately before the usable payload.
typedef struct Arena_Block Arena_Block;
struct Arena_Block {
    Arena_Block* next;
    size_t capacity;
    size_t used;
    max_align_t _align;
};

typedef struct Arena_Cleanup_Node Arena_Cleanup_Node;
struct Arena_Cleanup_Node {
    Arena_Cleanup_Fn fn;
    void *userdata;
    Arena_Cleanup_Node *next;
};

struct Arena {
    Arena_Block* first;
    Arena_Block* current;
    size_t min_block_size;
    Arena_Cleanup_Node *cleanup_head;
};

static Arena_Block* arena_find_block(Arena *arena, Arena_Block *target);
static Arena_Block* arena_find_block_for_ptr(Arena *arena, const void *ptr, size_t *offset_out);
static void arena_run_cleanups_until(Arena *arena, Arena_Cleanup_Node *stop_head);
static void arena_reset_blocks_only(Arena *arena);

// Match the platform's widest scalar alignment so arena_alloc() can back any type.
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ARENA_ALIGNMENT _Alignof(max_align_t)
#else
#define ARENA_ALIGNMENT 8
#endif
#define ALIGN_UP(n) (((n) + (ARENA_ALIGNMENT - 1)) & ~(ARENA_ALIGNMENT - 1))

static bool arena_add_overflow(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return true;
    *out = a + b;
    return false;
}

static bool arena_align_up(size_t n, size_t *out) {
    // Rounding is centralized here so overflow checks stay consistent.
    size_t tmp = 0;
    if (arena_add_overflow(n, ARENA_ALIGNMENT - 1, &tmp)) return false;
    *out = tmp & ~(ARENA_ALIGNMENT - 1);
    return true;
}

static Arena_Block* arena_block_create(size_t capacity) {
    // Allocate one contiguous object: header followed by payload bytes.
    size_t total_size = 0;
    if (arena_add_overflow(sizeof(Arena_Block), capacity, &total_size)) {
        return NULL;
    }
    Arena_Block* block = (Arena_Block*)malloc(total_size);
    if (!block) return NULL;
    
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    
    return block;
}

Arena* arena_create(size_t initial_capacity) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    
    // Small arenas still get a page-sized block so common workloads do not
    // immediately spill into extra heap allocations.
    arena->min_block_size = initial_capacity < 4096 ? 4096 : initial_capacity;
    
    arena->first = arena_block_create(arena->min_block_size);
    if (!arena->first) {
        free(arena);
        return NULL;
    }
    
    arena->current = arena->first;
    arena->cleanup_head = NULL;
    return arena;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;

    arena_run_cleanups_until(arena, NULL);
    
    Arena_Block* block = arena->first;
    while (block) {
        Arena_Block* next = block->next;
        free(block);
        block = next;
    }
    
    free(arena);
}

bool arena_on_destroy(Arena *arena, Arena_Cleanup_Fn fn, void *userdata) {
    if (!arena || !fn) return false;
    // Cleanup registrations live in arena storage too, so rewinding past the
    // registration point drops both the callback node and its future execution.
    Arena_Cleanup_Node *node = (Arena_Cleanup_Node*)arena_alloc(arena, sizeof(*node));
    if (!node) return false;
    node->fn = fn;
    node->userdata = userdata;
    node->next = arena->cleanup_head;
    arena->cleanup_head = node;
    return true;
}

static void* arena_alloc_from_block(Arena_Block* block, size_t size) {
    if (!arena_align_up(size, &size)) return NULL;
    
    size_t new_used = 0;
    if (arena_add_overflow(block->used, size, &new_used) || new_used > block->capacity) {
        return NULL;
    }
    
    // Payload starts right after the block header.
    void* ptr = (uint8_t*)(block + 1) + block->used;
    block->used = new_used;
    return ptr;
}

static Arena_Block* arena_find_reusable_block(Arena_Block *start, size_t size) {
    // After reset/rewind, later blocks remain linked and can satisfy future
    // allocations without allocating fresh memory.
    Arena_Block *block = start;
    while (block) {
        size_t next_used = 0;
        bool no_overflow = !arena_add_overflow(block->used, size, &next_used);
        if (no_overflow && block->capacity >= size && next_used <= block->capacity) {
            return block;
        }
        block = block->next;
    }
    return NULL;
}

static Arena_Block* arena_find_last_block(Arena_Block *first) {
    Arena_Block *block = first;
    while (block && block->next) {
        block = block->next;
    }
    return block;
}

static Arena_Block* arena_find_block(Arena *arena, Arena_Block *target) {
    if (!arena || !target) return NULL;

    Arena_Block *block = arena->first;
    while (block) {
        if (block == target) return block;
        block = block->next;
    }
    return NULL;
}

static Arena_Block* arena_find_block_for_ptr(Arena *arena, const void *ptr, size_t *offset_out) {
    if (offset_out) *offset_out = 0;
    if (!arena || !ptr) return NULL;

    // Used by the fallback realloc path to cap copies to memory proven to
    // belong to the arena and still lie inside a live block region.
    Arena_Block *block = arena->first;
    while (block) {
        uint8_t *begin = (uint8_t*)(block + 1);
        uint8_t *end = begin + block->used;
        if ((const uint8_t*)ptr >= begin && (const uint8_t*)ptr <= end) {
            if (offset_out) *offset_out = (size_t)((const uint8_t*)ptr - begin);
            return block;
        }
        block = block->next;
    }
    return NULL;
}

static void arena_run_cleanups_until(Arena *arena, Arena_Cleanup_Node *stop_head) {
    if (!arena) return;

    // Cleanups are a stack: the newest registration is the first one to run.
    while (arena->cleanup_head && arena->cleanup_head != stop_head) {
        Arena_Cleanup_Node *node = arena->cleanup_head;
        arena->cleanup_head = node->next;
        if (node->fn) node->fn(node->userdata);
    }
}

static void arena_reset_blocks_only(Arena *arena) {
    if (!arena) return;

    // Logical reset only clears usage counters; blocks stay allocated for reuse.
    Arena_Block* block = arena->first;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    arena->current = arena->first;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || size == 0) return NULL;

    size_t aligned_size = 0;
    if (!arena_align_up(size, &aligned_size)) return NULL;
    
    // Fast path: keep allocating from the current block.
    void* ptr = arena_alloc_from_block(arena->current, aligned_size);
    if (ptr) return ptr;

    // Reuse later blocks first so reset/rewind can pay off without extra mallocs.
    Arena_Block *reusable = arena_find_reusable_block(arena->current->next, aligned_size);
    if (reusable) {
        arena->current = reusable;
        ptr = arena_alloc_from_block(arena->current, aligned_size);
        if (ptr) return ptr;
    }
    
    // Otherwise grow geometrically, but never below the requested size or the
    // arena-wide minimum block size.
    size_t new_capacity = arena->current->capacity;
    if (new_capacity <= SIZE_MAX / 2) {
        new_capacity *= 2;
    } else {
        new_capacity = SIZE_MAX;
    }
    if (new_capacity < aligned_size) {
        new_capacity = aligned_size;
    }
    if (new_capacity < arena->min_block_size) {
        new_capacity = arena->min_block_size;
    }
    
    Arena_Block* new_block = arena_block_create(new_capacity);
    if (!new_block) return NULL;
    
    // Always append at the tail: blocks after `current` may still exist because
    // of previous rewinds and remain eligible for later reuse.
    Arena_Block *last = arena_find_last_block(arena->first);
    if (!last) {
        free(new_block);
        return NULL;
    }
    last->next = new_block;
    arena->current = new_block;
    
    // A freshly sized block must be able to satisfy the pending allocation.
    ptr = arena_alloc_from_block(arena->current, aligned_size);
    assert(ptr && "Failed to allocate in new block");
    return ptr;
}

void* arena_alloc_zero(Arena* arena, size_t size) {
    void* ptr = arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* arena_realloc_last(Arena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena) return NULL;
    if (!ptr) return arena_alloc(arena, new_size);
    if (new_size == 0) return NULL;
    
    size_t old_size_aligned = 0;
    size_t new_size_aligned = 0;
    if (!arena_align_up(old_size, &old_size_aligned)) return NULL;
    if (!arena_align_up(new_size, &new_size_aligned)) return NULL;
    
    // Only the most recent allocation can be resized in place without creating
    // holes inside the current block.
    uint8_t* expected_ptr = NULL;
    bool valid_old_size = old_size_aligned <= arena->current->used;
    if (valid_old_size) {
        expected_ptr = (uint8_t*)(arena->current + 1) +
                       (arena->current->used - old_size_aligned);
    }
    
    if (valid_old_size && ptr == expected_ptr) {
        // In-place growth/shrink is possible as long as the resized allocation
        // still fits inside the current block.
        size_t available = arena->current->capacity - 
                          (arena->current->used - old_size_aligned);
        
        if (new_size_aligned <= available) {
            // No copy is needed; only the block watermark moves.
            arena->current->used = arena->current->used - old_size_aligned + new_size_aligned;
            return ptr;
        }
    }
    
    // Fallback: allocate a new region and copy only the byte range that is
    // provably readable inside the block that owns `ptr`.
    void* new_ptr = arena_alloc(arena, new_size_aligned);
    if (new_ptr && old_size > 0) {
        size_t ptr_offset = 0;
        Arena_Block *owner = arena_find_block_for_ptr(arena, ptr, &ptr_offset);
        size_t readable_size = 0;
        if (owner && ptr_offset <= owner->used) {
            readable_size = owner->used - ptr_offset;
        }
        size_t copy_size = old_size < new_size ? old_size : new_size;
        if (copy_size > readable_size) copy_size = readable_size;
        if (copy_size > 0) memmove(new_ptr, ptr, copy_size);
    }
    return new_ptr;
}

void arena_reset(Arena* arena) {
    if (!arena) return;

    // Reset runs all pending cleanups because every logical allocation is gone.
    arena_run_cleanups_until(arena, NULL);
    arena_reset_blocks_only(arena);
}

size_t arena_total_allocated(const Arena* arena) {
    if (!arena) return 0;
    
    size_t total = 0;
    Arena_Block* block = arena->first;
    while (block) {
        total += block->used;
        block = block->next;
    }
    return total;
}

size_t arena_total_capacity(const Arena* arena) {
    if (!arena) return 0;
    
    size_t total = 0;
    Arena_Block* block = arena->first;
    while (block) {
        total += block->capacity;
        block = block->next;
    }
    return total;
}

Arena_Mark arena_mark(Arena* arena) {
    Arena_Mark mark = {0};
    if (arena && arena->current) {
        // A mark snapshots both allocation position and cleanup stack head.
        mark.block = arena->current;
        mark.used = arena->current->used;
        mark.cleanup_head = arena->cleanup_head;
    }
    return mark;
}

void arena_rewind(Arena* arena, Arena_Mark mark) {
    if (!arena) return;

    // Drop cleanups registered after the mark before rolling allocation state back.
    arena_run_cleanups_until(arena, (Arena_Cleanup_Node*)mark.cleanup_head);

    Arena_Block *target = arena_find_block(arena, (Arena_Block*)mark.block);
    if (!target || mark.used > target->capacity) {
        // Invalid marks degrade to a full logical reset so the arena remains consistent.
        arena_run_cleanups_until(arena, NULL);
        arena_reset_blocks_only(arena);
        return;
    }

    target->used = mark.used;
    arena->current = target;

    // Only allocations after the mark are released; later blocks stay attached
    // and become reusable for future allocations.
    Arena_Block* next = target->next;
    while (next) {
        next->used = 0;
        next = next->next;
    }
}

char* arena_strdup(Arena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = (char*)arena_alloc(arena, len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

char* arena_strndup(Arena* arena, const char* str, size_t max_len) {
    if (!str) return NULL;
    
    size_t len = 0;
    // Avoid reading past max_len while still guaranteeing NUL termination.
    while (len < max_len && str[len]) len++;
    
    char* copy = (char*)arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

void* arena_memdup(Arena* arena, const void* src, size_t size) {
    if (!src || size == 0) return NULL;
    void* copy = arena_alloc(arena, size);
    if (copy) {
        memcpy(copy, src, size);
    }
    return copy;
}
