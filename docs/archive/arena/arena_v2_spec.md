# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Arena v2 Specification

Status: Canonical allocator contract for `src_v2/arena/arena.c`, `src_v2/arena/arena.h`, and `src_v2/arena/arena_dyn.h`.

## 1. Role

`Arena` is the project's monotonic allocator for short-lived and batch-owned memory.

It is designed for:
- Fast append-only allocation.
- Bulk lifetime control (`reset`, `rewind`, `destroy`).
- Reuse of previously allocated backing blocks without returning them to the OS until final destroy.
- Lightweight dynamic arrays built on top of arena memory.

It is not a general-purpose free-list allocator and does not support freeing individual allocations.

## 2. Core Model

### 2.1 Block Chain

An arena owns a singly linked list of memory blocks.

Each block tracks:
- `capacity`: total usable payload bytes in that block.
- `used`: currently consumed bytes in that block.

The arena tracks:
- `first`: first block in the chain.
- `current`: block used for the next allocation attempt.
- `min_block_size`: lower bound for newly created blocks.
- `cleanup_head`: LIFO stack of registered cleanup callbacks.

### 2.2 Minimum Block Size

`arena_create(initial_capacity)` creates the first block immediately.

Current behavior:
- If `initial_capacity < 4096`, the first block is promoted to `4096` bytes.
- Otherwise the first block uses `initial_capacity`.

This same `min_block_size` is also enforced when the arena later grows.

### 2.3 Alignment

All allocations are aligned to:
- `_Alignof(max_align_t)` on C11-or-newer builds.
- `8` bytes on older builds.

`arena_total_allocated()` reports aligned usage, not the raw byte counts originally requested by callers.

## 3. Public API Contract

### 3.1 Creation and Destruction

- `Arena *arena_create(size_t initial_capacity)`
Creates an arena and its first block. Returns `NULL` on allocation failure.

- `void arena_destroy(Arena *arena)`
Runs all still-pending cleanup callbacks, frees every arena block, then frees the arena itself. `NULL` is accepted as a no-op.

### 3.2 Allocation

- `void *arena_alloc(Arena *arena, size_t size)`
Allocates `size` bytes, aligned as described above.

Contract:
- Returns `NULL` if `arena == NULL`.
- Returns `NULL` if `size == 0`.
- Returns `NULL` on arithmetic overflow or backing allocation failure.
- Never frees older allocations.

Growth strategy:
- Try current block first.
- If that fails, try to reuse a later existing block that already has enough remaining space.
- If no reusable block fits, append a new block at the end of the chain.

New block capacity is at least:
- the requested aligned size,
- `min_block_size`,
- or double the current block capacity (unless that would overflow, in which case it saturates and then clamps to the requested size).

- `void *arena_alloc_zero(Arena *arena, size_t size)`
Same as `arena_alloc`, then zeroes exactly `size` bytes. Returns `NULL` on the same failure cases.

- `void *arena_realloc_last(Arena *arena, void *ptr, size_t old_size, size_t new_size)`
Specialized resize helper for the most recent allocation pattern.

Contract:
- If `arena == NULL`, returns `NULL`.
- If `ptr == NULL`, behaves like `arena_alloc(arena, new_size)`.
- If `new_size == 0`, returns `NULL`.

Behavior:
- If `ptr` is exactly the most recent allocation in `arena->current` and the resized block still fits, resize happens in place by adjusting `used`.
- Otherwise a new allocation is made and `min(old_size, new_size)` bytes are copied.

Important consequence:
- Passing a non-last pointer is safe, but the old storage remains occupied until `arena_reset()` or `arena_destroy()`.

### 3.3 Bulk Lifetime Control

- `void arena_reset(Arena *arena)`
Runs all pending cleanup callbacks, then sets `used = 0` for every block and resets `current` back to `first`.

`arena_reset()` keeps the block chain allocated, so future allocations can reuse the same capacity without re-mallocing.

- `Arena_Mark arena_mark(Arena *arena)`
Captures:
- the current block pointer,
- the current block `used`,
- the current cleanup stack head.

If `arena == NULL`, returns a zero-initialized mark.

- `void arena_rewind(Arena *arena, Arena_Mark mark)`
Rolls the arena back to the captured mark.

Implemented behavior:
- Runs cleanup callbacks that were registered after the mark.
- Restores `used` on the marked block.
- Sets `current` back to the marked block.
- Sets `used = 0` on all later blocks, keeping their capacity for reuse.

Invalid mark fallback:
- If the marked block does not belong to this arena, or `mark.used` exceeds the block capacity, the function treats the mark as invalid.
- In that case it runs all remaining cleanups and performs the same block reset as `arena_reset()`.

### 3.4 Cleanup Callbacks

- `bool arena_on_destroy(Arena *arena, Arena_Cleanup_Fn fn, void *userdata)`
Registers a cleanup callback node inside the arena itself.

Contract:
- Returns `false` if `arena == NULL`.
- Returns `false` if `fn == NULL`.
- Returns `false` if the cleanup node cannot be allocated.
- Otherwise pushes the callback in LIFO order and returns `true`.

Execution points:
- `arena_rewind()` runs callbacks registered after the target mark.
- `arena_reset()` runs all pending callbacks.
- `arena_destroy()` runs all still-pending callbacks that were not already consumed by rewind/reset.

Each registered callback runs at most once.

## 4. Utility Helpers

### 4.1 Capacity/Usage Introspection

- `size_t arena_total_allocated(const Arena *arena)`
Returns the sum of `used` across all blocks. Returns `0` for `NULL`.

- `size_t arena_total_capacity(const Arena *arena)`
Returns the sum of `capacity` across all blocks. Returns `0` for `NULL`.

### 4.2 Duplication Helpers

- `char *arena_strdup(Arena *arena, const char *str)`
Duplicates a NUL-terminated string including the terminator.

- `char *arena_strndup(Arena *arena, const char *str, size_t max_len)`
Duplicates up to `max_len` bytes or the source terminator, then appends a new terminator.

- `void *arena_memdup(Arena *arena, const void *src, size_t size)`
Duplicates `size` bytes of raw memory.

All three helpers:
- return `NULL` if the source pointer is `NULL`,
- and `arena_memdup` also returns `NULL` if `size == 0`.

## 5. Dynamic Array Layer (`arena_dyn.h`)

`arena_dyn.h` provides stretchy-buffer style arrays whose backing storage lives inside an arena.

The array memory layout is:
- `Arena_Arr_Header`
- followed immediately by the user-visible element buffer

The header stores:
- `capacity`
- `count`

### 5.1 Ownership and Lifetime

- The array lifetime is the arena lifetime.
- `arena_arr_free(arr)` is intentionally a no-op.
- Rewind/reset can invalidate arrays allocated after the rollback point, just like any other arena allocation.

### 5.2 Main Macros and Helpers

- `arena_arr_len(arr)`: current element count, or `0` for `NULL`.
- `arena_arr_cap(arr)`: current capacity, or `0` for `NULL`.
- `arena_arr_empty(arr)`: true when length is zero.
- `arena_arr_last(arr)`: last element. Caller must guarantee the array is non-empty.
- `arena_arr_set_len(arr, n)`: mutates `count` directly. Caller must guarantee `n <= capacity`.

- `arena_arr_reserve(arena, arr, min_capacity)`
Ensures capacity for at least `min_capacity` elements.

- `arena_arr_push_n(arena, arr, n)`
Appends `n` uninitialized slots and returns a pointer to the first new slot.

- `arena_arr_push(arena, arr, value)`
Appends one element and stores `value`. Returns `true` on success.

- `arena_arr_fit(arena, arr)`
Attempts to shrink capacity to exactly `count`.

### 5.3 Growth Rules

Current growth policy:
- Initial capacity is `8`.
- Capacity doubles until it reaches the required minimum.
- If doubling would overflow, the helper falls back to the requested minimum.

All dynamic-array growth and fit operations use `arena_realloc_last()`.

Practical implication:
- If the array header is the last allocation, resize may happen in place.
- If it is not the last allocation, resize may allocate a new backing buffer and copy the data.
- The abandoned old buffer remains part of the arena until the next reset/rewind/destroy.

## 6. Invalid Input and Safety Rules

The allocator is intentionally defensive for common misuse:

- `arena_alloc(NULL, ...)` returns `NULL`.
- `arena_alloc(..., 0)` returns `NULL`.
- `arena_alloc_zero(NULL, ...)` returns `NULL`.
- `arena_strdup(NULL, ...)` returns `NULL`.
- `arena_strdup(..., NULL)` returns `NULL`.
- `arena_strndup(..., NULL, ...)` returns `NULL`.
- `arena_memdup(..., NULL, ...)` returns `NULL`.
- `arena_memdup(..., ..., 0)` returns `NULL`.
- `arena_total_allocated(NULL)` returns `0`.
- `arena_total_capacity(NULL)` returns `0`.
- `arena_reset(NULL)`, `arena_rewind(NULL, ...)`, and `arena_destroy(NULL)` are no-ops.

Overflow checks are implemented for:
- size alignment math,
- block allocation sizing,
- dynamic-array capacity sizing.

An overflow failure returns `NULL`/`false` without corrupting the arena state.

## 7. Behavioral Notes

- The allocator is monotonic between rewind/reset operations.
- Individual allocations are never released early.
- Blocks are recycled aggressively after rewind/reset to avoid repeated heap churn.
- Cleanup callbacks are stored inside arena memory, so their registration itself participates in arena lifetime.
- The implementation is not thread-safe.

## 8. Example Usage

```c
Arena *arena = arena_create(1024);
if (!arena) return false;

Arena_Mark mark = arena_mark(arena);

char *name = arena_strdup(arena, "nobify");
int *values = arena_alloc_array_zero(arena, int, 16);
if (!name || !values) {
    arena_destroy(arena);
    return false;
}

arena_rewind(arena, mark); // drops `name` and `values`
arena_destroy(arena);
```

## 9. Current Non-Goals

The current arena does not provide:
- per-allocation free,
- block compaction,
- thread synchronization,
- destructors tied to arbitrary individual allocations,
- ownership transfer of arena allocations outside the arena lifetime.
