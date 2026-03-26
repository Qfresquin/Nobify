#ifndef ARENA_H_
#define ARENA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct Arena Arena;
typedef void (*Arena_Cleanup_Fn)(void *userdata);

// Create an arena with an initial block capacity in bytes.
Arena* arena_create(size_t initial_capacity);

// Destroy the arena and release all backing blocks.
void arena_destroy(Arena* arena);

// Register a cleanup callback in LIFO order.
// Callbacks may run during arena_rewind(), arena_reset() or arena_destroy().
bool arena_on_destroy(Arena *arena, Arena_Cleanup_Fn fn, void *userdata);

// Allocate memory from the arena. Returned memory is max_align_t-aligned.
void* arena_alloc(Arena* arena, size_t size);

// Allocate memory and zero the requested byte range.
void* arena_alloc_zero(Arena* arena, size_t size);

// Allocate storage for an array of items.
#define arena_alloc_array(arena, type, count) \
    ((type*)arena_alloc((arena), sizeof(type) * (count)))

// Allocate zeroed storage for an array of items.
#define arena_alloc_array_zero(arena, type, count) \
    ((type*)arena_alloc_zero((arena), sizeof(type) * (count)))

// Reallocate the most recent allocation when possible.
// Falls back to allocate+copy when `ptr` is not the last live allocation.
void* arena_realloc_last(Arena* arena, void* ptr, size_t old_size, size_t new_size);

// Reset the arena, running pending cleanups and releasing logical allocations
// while keeping the backing blocks for reuse.
void arena_reset(Arena* arena);

// Return the total number of bytes currently marked as used.
size_t arena_total_allocated(const Arena* arena);

// Return the total capacity across all allocated blocks.
size_t arena_total_capacity(const Arena* arena);

// Save the current arena position so it can be restored later.
typedef struct {
    void *block;
    size_t used;
    void *cleanup_head;
} Arena_Mark;

// Capture a rewind mark at the current allocation point.
Arena_Mark arena_mark(Arena* arena);

// Restore a previous mark, discarding allocations and cleanups created after it.
void arena_rewind(Arena* arena, Arena_Mark mark);

// Duplicate a NUL-terminated string into arena storage.
char* arena_strdup(Arena* arena, const char* str);

// Duplicate up to max_len bytes from a string and always append a trailing NUL.
char* arena_strndup(Arena* arena, const char* str, size_t max_len);

// Duplicate an arbitrary byte range into arena storage.
void* arena_memdup(Arena* arena, const void* src, size_t size);

#endif // ARENA_H_
