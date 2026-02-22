#include "evaluator_internal.h"
#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

String_View sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return nob_sv_from_cstr("");
    if (sv.count == 0 || sv.data == NULL) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
}

char *eval_sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

bool eval_sv_key_eq(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool eval_sv_eq_ci_lit(String_View a, const char *lit) {
    String_View b = nob_sv_from_cstr(lit);
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

String_View eval_sv_join_semi_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (!ctx || count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    total += (count - 1);

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (i) buf[off++] = ';';
        if (items[i].count) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out) {
    if (!arena || !out) return false;
    if (input.count == 0) return true;

    size_t start = 0;
    size_t genex_depth = 0;
    for (size_t i = 0; i < input.count; i++) {
        if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
            genex_depth++;
            i++;
            continue;
        }
        if (input.data[i] == '>' && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (input.data[i] == ';' && genex_depth == 0) {
            String_View item = nob_sv_from_parts(input.data + start, i - start);
            if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
                return false;
            }
            out->items[out->count++] = item;
            start = i + 1;
        }
    }

    if (start < input.count) {
        String_View item = nob_sv_from_parts(input.data + start, input.count - start);
        if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
            return false;
        }
        out->items[out->count++] = item;
    }
    return true;
}

static bool ch_is_sep(char c) { return c == '/' || c == '\\'; }

bool eval_sv_is_abs_path(String_View p) {
    if (p.count == 0) return false;
    if (p.data[0] == '/' || p.data[0] == '\\') return true;
    if (p.count > 1 && p.data[1] == ':') return true;
    return false;
}

String_View eval_sv_path_join(Arena *arena, String_View a, String_View b) {
    if (!arena) return nob_sv_from_cstr("");
    if (a.count == 0) return sv_copy_to_arena(arena, b);
    if (b.count == 0) return sv_copy_to_arena(arena, a);

    bool need_slash = !ch_is_sep(a.data[a.count - 1]);
    size_t total = a.count + (need_slash ? 1 : 0) + b.count;

    char *buf = (char*)arena_alloc(arena, total + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t off = 0;
    memcpy(buf + off, a.data, a.count);
    off += a.count;
    if (need_slash) buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';

    return nob_sv_from_cstr(buf);
}
