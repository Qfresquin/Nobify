#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    String_View name;
    String_View script;
} Arena_Case;

typedef struct {
    Arena_Case *items;
    size_t count;
    size_t capacity;
} Arena_Case_List;

typedef struct {
    int *value_ptr;
    int calls;
    int observed_sum;
} Cleanup_Int_State;

typedef struct {
    int call_count;
    int order[8];
    int order_count;
} Cleanup_Order_State;

typedef struct {
    Cleanup_Order_State *state;
    int id;
} Cleanup_Order_Event;

static void cleanup_read_int_and_count(void *userdata) {
    Cleanup_Int_State *state = (Cleanup_Int_State*)userdata;
    if (!state) return;
    state->calls++;
    if (state->value_ptr) state->observed_sum += *state->value_ptr;
}

static void cleanup_record_order(void *userdata) {
    Cleanup_Order_Event *event = (Cleanup_Order_Event*)userdata;
    if (!event || !event->state) return;

    Cleanup_Order_State *state = event->state;
    state->call_count++;
    if (state->order_count < (int)(sizeof(state->order) / sizeof(state->order[0]))) {
        state->order[state->order_count++] = event->id;
    }
}

static bool load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;

    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static bool arena_case_list_append(Arena *arena, Arena_Case_List *list, Arena_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Arena_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Arena_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!arena_case_list_append(arena, out, (Arena_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) {
                return false;
            }
        }
    }

    return out->count > 0;
}

#define CASE_CHECK(cond) do { \
    if (!(cond)) { \
        nob_sb_append_cstr(sb, nob_temp_sprintf("CHECK_FAIL: %s\n", #cond)); \
        return false; \
    } \
} while (0)

static bool case_create_and_destroy(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);
    arena_destroy(arena);
    return true;
}

static bool case_basic_allocation(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *ptr1 = arena_alloc(arena, 100);
    CASE_CHECK(ptr1 != NULL);

    void *ptr2 = arena_alloc(arena, 200);
    CASE_CHECK(ptr2 != NULL);
    CASE_CHECK(ptr1 != ptr2);

    arena_destroy(arena);
    return true;
}

static bool case_alloc_zero(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    char *buffer = (char*)arena_alloc_zero(arena, 100);
    CASE_CHECK(buffer != NULL);
    for (size_t i = 0; i < 100; i++) {
        CASE_CHECK(buffer[i] == 0);
    }

    arena_destroy(arena);
    return true;
}

static bool case_alloc_array(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    int *numbers = (int*)arena_alloc(arena, sizeof(int) * 10);
    CASE_CHECK(numbers != NULL);

    for (int i = 0; i < 10; i++) {
        numbers[i] = i * 2;
    }
    CASE_CHECK(numbers[5] == 10);

    arena_destroy(arena);
    return true;
}

static bool case_multiple_blocks(Nob_String_Builder *sb) {
    Arena *arena = arena_create(128);
    CASE_CHECK(arena != NULL);

    void *ptr1 = arena_alloc(arena, 100);
    void *ptr2 = arena_alloc(arena, 100);
    void *ptr3 = arena_alloc(arena, 100);
    CASE_CHECK(ptr1 != NULL);
    CASE_CHECK(ptr2 != NULL);
    CASE_CHECK(ptr3 != NULL);

    size_t total = arena_total_allocated(arena);
    CASE_CHECK(total >= 300);

    arena_destroy(arena);
    return true;
}

static bool case_reset(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *ptr1 = arena_alloc(arena, 100);
    CASE_CHECK(ptr1 != NULL);

    size_t before = arena_total_allocated(arena);
    CASE_CHECK(before > 0);

    arena_reset(arena);

    size_t after = arena_total_allocated(arena);
    CASE_CHECK(after == 0);

    void *ptr2 = arena_alloc(arena, 100);
    CASE_CHECK(ptr2 != NULL);

    arena_destroy(arena);
    return true;
}

static bool case_mark_and_rewind(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *ptr1 = arena_alloc(arena, 100);
    CASE_CHECK(ptr1 != NULL);

    Arena_Mark mark = arena_mark(arena);

    void *ptr2 = arena_alloc(arena, 200);
    CASE_CHECK(ptr2 != NULL);

    size_t before_rewind = arena_total_allocated(arena);
    arena_rewind(arena, mark);
    size_t after_rewind = arena_total_allocated(arena);

    CASE_CHECK(after_rewind < before_rewind);
    CASE_CHECK(after_rewind >= 100);

    arena_destroy(arena);
    return true;
}

static bool case_reset_runs_cleanups_once_and_not_on_destroy_again(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    int *value = arena_alloc_array(arena, int, 1);
    CASE_CHECK(value != NULL);
    *value = 42;

    Cleanup_Int_State cleanup_state = {0};
    cleanup_state.value_ptr = value;

    CASE_CHECK(arena_on_destroy(arena, cleanup_read_int_and_count, &cleanup_state));

    arena_reset(arena);
    CASE_CHECK(cleanup_state.calls == 1);
    CASE_CHECK(cleanup_state.observed_sum == 42);

    int *reused = arena_alloc_array(arena, int, 1);
    CASE_CHECK(reused != NULL);
    *reused = 777;

    arena_destroy(arena);
    CASE_CHECK(cleanup_state.calls == 1);
    CASE_CHECK(cleanup_state.observed_sum == 42);
    return true;
}

static bool case_rewind_runs_only_cleanups_after_mark(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    Cleanup_Order_State state = {0};
    Cleanup_Order_Event first = {&state, 1};
    Cleanup_Order_Event second = {&state, 2};

    CASE_CHECK(arena_on_destroy(arena, cleanup_record_order, &first));
    Arena_Mark mark = arena_mark(arena);
    CASE_CHECK(arena_on_destroy(arena, cleanup_record_order, &second));

    arena_rewind(arena, mark);
    CASE_CHECK(state.call_count == 1);
    CASE_CHECK(state.order_count == 1);
    CASE_CHECK(state.order[0] == 2);

    arena_destroy(arena);
    CASE_CHECK(state.call_count == 2);
    CASE_CHECK(state.order_count == 2);
    CASE_CHECK(state.order[1] == 1);
    return true;
}

static bool case_rewind_restores_exact_mark_usage_across_blocks(Nob_String_Builder *sb) {
    Arena *arena = arena_create(128);
    CASE_CHECK(arena != NULL);

    void *first = arena_alloc(arena, 3000);
    void *second = arena_alloc(arena, 3000);
    CASE_CHECK(first != NULL);
    CASE_CHECK(second != NULL);

    size_t allocated_at_mark = arena_total_allocated(arena);
    Arena_Mark mark = arena_mark(arena);

    void *third = arena_alloc(arena, 5000);
    CASE_CHECK(third != NULL);
    CASE_CHECK(arena_total_allocated(arena) > allocated_at_mark);

    arena_rewind(arena, mark);
    CASE_CHECK(arena_total_allocated(arena) == allocated_at_mark);

    arena_destroy(arena);
    return true;
}

static bool case_strdup(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    const char *original = "Hello, World!";
    char *copy = arena_strdup(arena, original);

    CASE_CHECK(copy != NULL);
    CASE_CHECK(copy != original);
    CASE_CHECK(strcmp(copy, original) == 0);

    arena_destroy(arena);
    return true;
}

static bool case_strndup(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    const char *original = "Hello, World!";
    char *copy = arena_strndup(arena, original, 5);

    CASE_CHECK(copy != NULL);
    CASE_CHECK(strlen(copy) == 5);
    CASE_CHECK(strncmp(copy, "Hello", 5) == 0);

    arena_destroy(arena);
    return true;
}

static bool case_memdup(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    int original[] = {1, 2, 3, 4, 5};
    int *copy = (int*)arena_memdup(arena, original, sizeof(original));

    CASE_CHECK(copy != NULL);
    CASE_CHECK(copy != original);

    for (int i = 0; i < 5; i++) {
        CASE_CHECK(copy[i] == original[i]);
    }

    arena_destroy(arena);
    return true;
}

static bool case_alignment(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *ptr1 = arena_alloc(arena, 1);
    void *ptr2 = arena_alloc(arena, 1);
    void *ptr3 = arena_alloc(arena, 1);

    CASE_CHECK(ptr1 != NULL);
    CASE_CHECK(ptr2 != NULL);
    CASE_CHECK(ptr3 != NULL);

    size_t align = 8;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    align = _Alignof(max_align_t);
#endif
    CASE_CHECK(align != 0);
    CASE_CHECK(((uintptr_t)ptr1 % align) == 0);
    CASE_CHECK(((uintptr_t)ptr2 % align) == 0);
    CASE_CHECK(((uintptr_t)ptr3 % align) == 0);

    arena_destroy(arena);
    return true;
}

static bool case_large_allocation(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *ptr = arena_alloc(arena, 10 * 1024);
    CASE_CHECK(ptr != NULL);

    size_t capacity = arena_total_capacity(arena);
    CASE_CHECK(capacity >= 10 * 1024);

    arena_destroy(arena);
    return true;
}

static bool case_realloc_last_grow_and_shrink(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    uint8_t *ptr = (uint8_t*)arena_alloc(arena, 16);
    CASE_CHECK(ptr != NULL);
    for (size_t i = 0; i < 16; i++) ptr[i] = (uint8_t)(i + 1);

    uint8_t *grown = (uint8_t*)arena_realloc_last(arena, ptr, 16, 64);
    CASE_CHECK(grown != NULL);
    for (size_t i = 0; i < 16; i++) CASE_CHECK(grown[i] == (uint8_t)(i + 1));

    uint8_t *shrunk = (uint8_t*)arena_realloc_last(arena, grown, 64, 8);
    CASE_CHECK(shrunk != NULL);
    for (size_t i = 0; i < 8; i++) CASE_CHECK(shrunk[i] == (uint8_t)(i + 1));

    arena_destroy(arena);
    return true;
}

static bool case_realloc_last_non_last_pointer(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    uint8_t *ptr1 = (uint8_t*)arena_alloc(arena, 16);
    uint8_t *ptr2 = (uint8_t*)arena_alloc(arena, 16);
    CASE_CHECK(ptr1 != NULL && ptr2 != NULL);
    for (size_t i = 0; i < 16; i++) ptr1[i] = (uint8_t)(100 + i);

    uint8_t *new_ptr1 = (uint8_t*)arena_realloc_last(arena, ptr1, 16, 32);
    CASE_CHECK(new_ptr1 != NULL);
    CASE_CHECK(new_ptr1 != ptr1);
    for (size_t i = 0; i < 16; i++) CASE_CHECK(new_ptr1[i] == (uint8_t)(100 + i));

    arena_destroy(arena);
    return true;
}

static bool case_realloc_last_invalid_old_size_is_safe(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    uint8_t *ptr = (uint8_t*)arena_alloc(arena, 16);
    CASE_CHECK(ptr != NULL);
    for (size_t i = 0; i < 16; i++) ptr[i] = (uint8_t)(200 + i);

    uint8_t *realloced = (uint8_t*)arena_realloc_last(arena, ptr, 1024, 32);
    CASE_CHECK(realloced != NULL);
    for (size_t i = 0; i < 16; i++) CASE_CHECK(realloced[i] == (uint8_t)(200 + i));

    arena_destroy(arena);
    return true;
}

static bool case_reset_reuses_existing_blocks(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *big = arena_alloc(arena, 5000);
    CASE_CHECK(big != NULL);
    size_t cap_before = arena_total_capacity(arena);
    CASE_CHECK(cap_before >= 5000);

    arena_reset(arena);

    void *big_again = arena_alloc(arena, 5000);
    CASE_CHECK(big_again != NULL);
    size_t cap_after = arena_total_capacity(arena);
    CASE_CHECK(cap_after == cap_before);

    arena_destroy(arena);
    return true;
}

static bool case_rewind_reuses_existing_blocks(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    void *head = arena_alloc(arena, 128);
    CASE_CHECK(head != NULL);
    Arena_Mark mark = arena_mark(arena);

    void *big = arena_alloc(arena, 5000);
    CASE_CHECK(big != NULL);
    size_t cap_before = arena_total_capacity(arena);

    arena_rewind(arena, mark);

    void *big_again = arena_alloc(arena, 5000);
    CASE_CHECK(big_again != NULL);
    size_t cap_after = arena_total_capacity(arena);
    CASE_CHECK(cap_after == cap_before);

    arena_destroy(arena);
    return true;
}

static bool case_invalid_inputs_are_safe(Nob_String_Builder *sb) {
    CASE_CHECK(arena_alloc(NULL, 16) == NULL);
    CASE_CHECK(arena_alloc_zero(NULL, 16) == NULL);
    CASE_CHECK(arena_strdup(NULL, "x") == NULL);
    CASE_CHECK(arena_strndup(NULL, "x", 1) == NULL);
    CASE_CHECK(arena_memdup(NULL, "x", 1) == NULL);

    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    CASE_CHECK(arena_alloc(arena, 0) == NULL);
    CASE_CHECK(arena_alloc_zero(arena, 0) == NULL);

    void *p = arena_realloc_last(arena, NULL, 0, 32);
    CASE_CHECK(p != NULL);

    CASE_CHECK(arena_realloc_last(arena, p, 32, 0) == NULL);

    CASE_CHECK(arena_strdup(arena, NULL) == NULL);
    CASE_CHECK(arena_strndup(arena, NULL, 10) == NULL);
    CASE_CHECK(arena_memdup(arena, NULL, 10) == NULL);
    CASE_CHECK(arena_memdup(arena, "abc", 0) == NULL);

    arena_destroy(arena);
    return true;
}

static bool case_overflow_alloc_returns_null_and_does_not_break_arena(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    size_t before = arena_total_allocated(arena);
    void *p = arena_alloc(arena, SIZE_MAX);
    CASE_CHECK(p == NULL);

    void *q = arena_alloc(arena, 16);
    CASE_CHECK(q != NULL);
    CASE_CHECK(arena_total_allocated(arena) > before);

    arena_destroy(arena);
    return true;
}

static bool case_dyn_reserve_basic_and_preserves_data(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    int *items = NULL;
    size_t cap = 0;

    CASE_CHECK(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 1));
    CASE_CHECK(items != NULL);
    CASE_CHECK(cap >= 1);
    CASE_CHECK(cap == 8);

    for (int i = 0; i < 8; i++) items[i] = i * 10;

    CASE_CHECK(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 9));
    CASE_CHECK(items != NULL);
    CASE_CHECK(cap >= 9);
    CASE_CHECK(cap == 16);
    for (int i = 0; i < 8; i++) CASE_CHECK(items[i] == i * 10);

    int *before_items = items;
    size_t before_cap = cap;
    CASE_CHECK(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 8));
    CASE_CHECK(items == before_items);
    CASE_CHECK(cap == before_cap);

    arena_destroy(arena);
    return true;
}

static bool case_dyn_reserve_invalid_and_overflow(Nob_String_Builder *sb) {
    Arena *arena = arena_create(1024);
    CASE_CHECK(arena != NULL);

    int *items = NULL;
    size_t cap = 0;

    CASE_CHECK(!arena_da_reserve(NULL, (void**)&items, &cap, sizeof(int), 1));
    CASE_CHECK(!arena_da_reserve(arena, NULL, &cap, sizeof(int), 1));
    CASE_CHECK(!arena_da_reserve(arena, (void**)&items, NULL, sizeof(int), 1));
    CASE_CHECK(!arena_da_reserve(arena, (void**)&items, &cap, 0, 1));

    cap = (SIZE_MAX / sizeof(int));
    CASE_CHECK(!arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), cap + 1));

    arena_destroy(arena);
    return true;
}

static bool run_arena_named_case(String_View name, Nob_String_Builder *sb) {
    if (nob_sv_eq(name, nob_sv_from_cstr("create_and_destroy"))) return case_create_and_destroy(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("basic_allocation"))) return case_basic_allocation(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("alloc_zero"))) return case_alloc_zero(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("alloc_array"))) return case_alloc_array(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("multiple_blocks"))) return case_multiple_blocks(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("reset"))) return case_reset(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("mark_and_rewind"))) return case_mark_and_rewind(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("reset_runs_cleanups_once_and_not_on_destroy_again"))) return case_reset_runs_cleanups_once_and_not_on_destroy_again(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("rewind_runs_only_cleanups_after_mark"))) return case_rewind_runs_only_cleanups_after_mark(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("rewind_restores_exact_mark_usage_across_blocks"))) return case_rewind_restores_exact_mark_usage_across_blocks(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("strdup"))) return case_strdup(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("strndup"))) return case_strndup(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("memdup"))) return case_memdup(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("alignment"))) return case_alignment(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("large_allocation"))) return case_large_allocation(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("realloc_last_grow_and_shrink"))) return case_realloc_last_grow_and_shrink(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("realloc_last_non_last_pointer"))) return case_realloc_last_non_last_pointer(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("realloc_last_invalid_old_size_is_safe"))) return case_realloc_last_invalid_old_size_is_safe(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("reset_reuses_existing_blocks"))) return case_reset_reuses_existing_blocks(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("rewind_reuses_existing_blocks"))) return case_rewind_reuses_existing_blocks(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("invalid_inputs_are_safe"))) return case_invalid_inputs_are_safe(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("overflow_alloc_returns_null_and_does_not_break_arena"))) return case_overflow_alloc_returns_null_and_does_not_break_arena(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("dyn_reserve_basic_and_preserves_data"))) return case_dyn_reserve_basic_and_preserves_data(sb);
    if (nob_sv_eq(name, nob_sv_from_cstr("dyn_reserve_invalid_and_overflow"))) return case_dyn_reserve_invalid_and_overflow(sb);

    nob_sb_append_cstr(sb, "CHECK_FAIL: unknown case\n");
    return false;
}

static bool render_arena_case_snapshot_to_sb(Arena_Case arena_case, Nob_String_Builder *sb) {
    Nob_String_Builder case_sb = {0};
    bool ok = run_arena_named_case(arena_case.name, &case_sb);

    nob_sb_append_cstr(sb, "RESULT=");
    nob_sb_append_cstr(sb, ok ? "PASS\n" : "FAIL\n");

    if (case_sb.count > 0) {
        nob_sb_append_buf(sb, case_sb.items, case_sb.count);
    }

    nob_sb_free(case_sb);
    return true;
}

static bool render_arena_casepack_snapshot_to_arena(Arena *arena, Arena_Case_List cases, String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE arena\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_arena_case_snapshot_to_sb(cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_arena_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;

    if (!load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Arena_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 24) {
        nob_log(NOB_ERROR, "golden: unexpected arena case count: got=%zu expected=24", cases.count);
        ok = false;
        goto done;
    }

    if (!render_arena_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(expected_norm, actual_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *ARENA_GOLDEN_DIR = "test_v2/arena/golden";

TEST(arena_golden_all_cases) {
    ASSERT(assert_arena_golden_casepack(
        nob_temp_sprintf("%s/arena_all.cmake", ARENA_GOLDEN_DIR),
        nob_temp_sprintf("%s/arena_all.txt", ARENA_GOLDEN_DIR)));
    TEST_PASS();
}

void run_arena_v2_tests(int *passed, int *failed) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "arena");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "arena suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "arena suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_arena_golden_all_cases(passed, failed);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "arena suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
