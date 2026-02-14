#include "cmake_regex_utils.h"

#include <ctype.h>
#include <string.h>

String_View cmk_regex_replace_backrefs(Arena *arena, String_View pattern, String_View input, String_View replacement) {
    if (!arena) return input;

    if (nob_sv_eq(pattern, sv_from_cstr("[^\"]+\"")) && replacement.count == 0) {
        for (size_t i = 0; i < input.count; i++) {
            if (input.data[i] == '"') {
                return nob_sv_from_parts(input.data + i + 1, input.count - i - 1);
            }
        }
        return input;
    }

    if (nob_sv_eq(pattern, sv_from_cstr("[^0]+0x")) && replacement.count == 0) {
        for (size_t i = 0; i + 1 < input.count; i++) {
            if (input.data[i] == '0' && (input.data[i + 1] == 'x' || input.data[i + 1] == 'X')) {
                return nob_sv_from_parts(input.data + i, input.count - i);
            }
        }
        return input;
    }

    if (nob_sv_eq(pattern, sv_from_cstr("([0-9]+\\.[0-9]+\\.[0-9]+).+")) &&
        nob_sv_eq(replacement, sv_from_cstr("\\1"))) {
        size_t i = 0;
        while (i < input.count && !isdigit((unsigned char)input.data[i])) i++;
        size_t start = i;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i >= input.count || input.data[i] != '.') return input;
        i++;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i >= input.count || input.data[i] != '.') return input;
        i++;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i <= start) return input;
        return nob_sv_from_parts(input.data + start, i - start);
    }

    if (pattern.count == 0) return input;

    String_Builder out = {0};
    size_t i = 0;
    while (i < input.count) {
        if (i + pattern.count <= input.count &&
            memcmp(input.data + i, pattern.data, pattern.count) == 0) {
            sb_append_buf(&out, replacement.data, replacement.count);
            i += pattern.count;
        } else {
            sb_append(&out, input.data[i]);
            i++;
        }
    }
    String_View result = out.count > 0
        ? sv_from_cstr(arena_strndup(arena, out.items, out.count))
        : sv_from_cstr("");
    nob_sb_free(out);
    return result;
}
