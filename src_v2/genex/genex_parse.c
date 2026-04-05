#include "genex_internal.h"

#include <ctype.h>
#include <string.h>

String_View gx_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

String_View gx_copy_cstr_to_arena(Arena *arena, const char *s) {
    if (!arena || !s) return nob_sv_from_cstr("");
    size_t n = strlen(s);
    char *dup = arena_strndup(arena, s, n);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

String_View gx_trim(String_View sv) {
    return nob_sv_trim(sv);
}

bool gx_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

bool gx_sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    return gx_sv_eq_ci(nob_sv_from_parts(sv.data + sv.count - suffix.count, suffix.count), suffix);
}

bool gx_is_escaped(String_View input, size_t idx) {
    if (idx == 0 || idx > input.count) return false;
    size_t bs = 0;
    for (size_t i = idx; i-- > 0;) {
        if (input.data[i] != '\\') break;
        bs++;
    }
    return (bs % 2) != 0;
}

bool gx_is_genex_open_at(String_View input, size_t i) {
    if (i + 1 >= input.count) return false;
    if (input.data[i] != '$' || input.data[i + 1] != '<') return false;
    return !gx_is_escaped(input, i);
}

bool gx_is_unescaped_char_at(String_View input, size_t i, char ch) {
    if (i >= input.count || input.data[i] != ch) return false;
    return !gx_is_escaped(input, i);
}

bool gx_sv_contains_genex_unescaped(String_View input) {
    for (size_t i = 0; i + 1 < input.count; i++) {
        if (gx_is_genex_open_at(input, i)) return true;
    }
    return false;
}

bool gx_cmake_string_is_false(String_View value) {
    String_View v = gx_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, nob_sv_from_cstr("0"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("FALSE"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("OFF"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("NO"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("N"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("IGNORE"))) return true;
    if (gx_sv_eq_ci(v, nob_sv_from_cstr("NOTFOUND"))) return true;
    if (gx_sv_ends_with_ci(v, nob_sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

bool gx_list_matches_value_ci(String_View entry_list, String_View needle) {
    size_t start = 0;
    for (size_t i = 0; i <= entry_list.count; i++) {
        bool sep = (i == entry_list.count) || (entry_list.data[i] == ';');
        if (!sep) continue;
        String_View candidate = gx_trim(nob_sv_from_parts(entry_list.data + start, i - start));
        if (candidate.count > 0 && gx_sv_eq_ci(candidate, needle)) return true;
        start = i + 1;
    }
    return false;
}

String_View gx_path_dirname(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            if (i == 0) return nob_sv_from_parts(path.data, 1);
            return nob_sv_from_parts(path.data, i);
        }
    }
    return nob_sv_from_cstr("");
}

String_View gx_path_basename(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    size_t off = 0;
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            off = i + 1;
            break;
        }
    }
    return nob_sv_from_parts(path.data + off, path.count - off);
}

Gx_Sv_List gx_split_top_level_alloc(const Genex_Context *ctx, String_View input, char delimiter) {
    Gx_Sv_List out = {0};
    if (!ctx || !ctx->arena) return out;

    size_t count = 0;
    size_t start = 0;
    size_t genex_depth = 0;

    for (size_t i = 0; i <= input.count; i++) {
        bool at_end = (i == input.count);
        if (!at_end) {
            if (gx_is_genex_open_at(input, i)) {
                genex_depth++;
                i++;
                continue;
            }
            if (gx_is_unescaped_char_at(input, i, '>') && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!at_end && !(gx_is_unescaped_char_at(input, i, delimiter) && genex_depth == 0)) continue;
        count++;
        start = i + 1;
    }
    (void)start;

    if (count == 0) return out;
    String_View *items = (String_View*)arena_alloc(ctx->arena, sizeof(String_View) * count);
    if (!items) return out;

    size_t idx = 0;
    start = 0;
    genex_depth = 0;
    for (size_t i = 0; i <= input.count; i++) {
        bool at_end = (i == input.count);
        if (!at_end) {
            if (gx_is_genex_open_at(input, i)) {
                genex_depth++;
                i++;
                continue;
            }
            if (gx_is_unescaped_char_at(input, i, '>') && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!at_end && !(gx_is_unescaped_char_at(input, i, delimiter) && genex_depth == 0)) continue;
        if (idx < count) {
            items[idx++] = gx_trim(nob_sv_from_parts(input.data + start, i - start));
        }
        start = i + 1;
    }

    out.items = items;
    out.count = idx;
    return out;
}

bool gx_find_top_level_colon(String_View body, size_t *out_colon) {
    if (!out_colon) return false;
    size_t genex_depth = 0;
    for (size_t i = 0; i < body.count; i++) {
        if (gx_is_genex_open_at(body, i)) {
            genex_depth++;
            i++;
            continue;
        }
        if (gx_is_unescaped_char_at(body, i, '>') && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (gx_is_unescaped_char_at(body, i, ':') && genex_depth == 0) {
            *out_colon = i;
            return true;
        }
    }
    return false;
}

bool gx_find_matching_genex_end(String_View input, size_t start_dollar, size_t *out_end) {
    if (!out_end || start_dollar + 1 >= input.count) return false;
    if (!gx_is_genex_open_at(input, start_dollar)) return false;

    size_t depth = 1;
    for (size_t i = start_dollar + 2; i < input.count; i++) {
        if (gx_is_genex_open_at(input, i)) {
            depth++;
            i++;
            continue;
        }
        if (gx_is_unescaped_char_at(input, i, '>')) {
            if (depth == 0) return false;
            depth--;
            if (depth == 0) {
                *out_end = i;
                return true;
            }
        }
    }
    return false;
}

bool gx_target_name_eq(const Genex_Context *ctx, String_View a, String_View b) {
    if (ctx && ctx->target_name_case_insensitive) return gx_sv_eq_ci(a, b);
    return nob_sv_eq(a, b);
}

bool gx_tp_stack_contains(const Genex_Context *ctx,
                          const Genex_Target_Property_Stack *stack,
                          String_View target_name,
                          String_View property_name) {
    if (!stack) return false;
    for (size_t i = 0; i < stack->count; i++) {
        if (gx_target_name_eq(ctx, stack->entries[i].target_name, target_name) &&
            gx_sv_eq_ci(stack->entries[i].property_name, property_name)) {
            return true;
        }
    }
    return false;
}

bool gx_tp_stack_push(Genex_Target_Property_Stack *stack,
                      String_View target_name,
                      String_View property_name) {
    if (!stack || stack->count >= (sizeof(stack->entries) / sizeof(stack->entries[0]))) return false;
    stack->entries[stack->count].target_name = target_name;
    stack->entries[stack->count].property_name = property_name;
    stack->count++;
    return true;
}

void gx_tp_stack_pop(Genex_Target_Property_Stack *stack) {
    if (!stack || stack->count == 0) return;
    stack->count--;
}
