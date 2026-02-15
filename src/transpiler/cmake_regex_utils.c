#include "cmake_regex_utils.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static String_View cmk_regex_unescape_cmake(Arena *arena, String_View text) {
    if (!arena || text.count == 0) return text;

    bool has_escape = false;
    for (size_t i = 0; i < text.count; i++) {
        if (text.data[i] == '\\') {
            has_escape = true;
            break;
        }
    }
    if (!has_escape) return text;

    String_Builder sb = {0};
    for (size_t i = 0; i < text.count; i++) {
        char ch = text.data[i];
        if (ch == '\\' && i + 1 < text.count) {
            char next = text.data[i + 1];
            switch (next) {
                case '\\': sb_append(&sb, '\\'); break;
                case 'n': sb_append(&sb, '\n'); break;
                case 'r': sb_append(&sb, '\r'); break;
                case 't': sb_append(&sb, '\t'); break;
                case ';': sb_append(&sb, ';'); break;
                case '"': sb_append(&sb, '"'); break;
                default:
                    sb_append(&sb, '\\');
                    sb_append(&sb, next);
                    break;
            }
            i++;
            continue;
        }
        sb_append(&sb, ch);
    }

    String_View out = sv_from_cstr(arena_strndup(arena, sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return out;
}

#if defined(_MSC_VER) && !defined(__MINGW32__)

bool cmk_regex_match_first(Arena *arena, String_View pattern, String_View input, String_View *out_match) {
    if (!out_match) return false;
    *out_match = sv_from_cstr("");
    if (!arena) return false;
    pattern = cmk_regex_unescape_cmake(arena, pattern);
    if (pattern.count == 0 || input.count == 0 || pattern.count > input.count) return false;
    const char *hit = strstr(nob_temp_sv_to_cstr(input), nob_temp_sv_to_cstr(pattern));
    if (!hit) return false;
    *out_match = sv_from_cstr(arena_strndup(arena, hit, pattern.count));
    return true;
}

bool cmk_regex_match_groups(Arena *arena, String_View pattern, String_View input, String_List *out_groups) {
    if (!out_groups) return false;
    string_list_init(out_groups);
    String_View first = {0};
    if (!cmk_regex_match_first(arena, pattern, input, &first)) return false;
    string_list_add(out_groups, arena, first);
    return true;
}

String_View cmk_regex_replace_backrefs(Arena *arena, String_View pattern, String_View input, String_View replacement) {
    if (!arena) return input;
    pattern = cmk_regex_unescape_cmake(arena, pattern);
    replacement = cmk_regex_unescape_cmake(arena, replacement);
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

#else

#include <regex.h>

static bool cmk_regex_compile(regex_t *re, Arena *arena, String_View pattern) {
    if (!re || pattern.count == 0) return false;
    String_View decoded_pattern = cmk_regex_unescape_cmake(arena, pattern);
    const char *pat = nob_temp_sv_to_cstr(decoded_pattern);
    return regcomp(re, pat, REG_EXTENDED) == 0;
}

static void cmk_regex_append_group(String_Builder *sb, String_View haystack, regmatch_t m) {
    if (!sb) return;
    if (m.rm_so < 0 || m.rm_eo < m.rm_so) return;
    size_t start = (size_t)m.rm_so;
    size_t end = (size_t)m.rm_eo;
    if (end > haystack.count || start > end) return;
    sb_append_buf(sb, haystack.data + start, end - start);
}

bool cmk_regex_match_groups(Arena *arena, String_View pattern, String_View input, String_List *out_groups) {
    if (!arena || !out_groups) return false;
    string_list_init(out_groups);
    if (pattern.count == 0) return false;

    regex_t re = {0};
    if (!cmk_regex_compile(&re, arena, pattern)) return false;

    size_t max_groups = re.re_nsub + 1;
    if (max_groups < 1) max_groups = 1;
    if (max_groups > 64) max_groups = 64;

    regmatch_t matches[64] = {0};
    int rc = regexec(&re, nob_temp_sv_to_cstr(input), max_groups, matches, 0);
    if (rc != 0) {
        regfree(&re);
        return false;
    }

    for (size_t i = 0; i < max_groups; i++) {
        regmatch_t m = matches[i];
        if (m.rm_so < 0 || m.rm_eo < m.rm_so) {
            string_list_add(out_groups, arena, sv_from_cstr(""));
            continue;
        }
        size_t start = (size_t)m.rm_so;
        size_t end = (size_t)m.rm_eo;
        if (end > input.count || start > end) {
            string_list_add(out_groups, arena, sv_from_cstr(""));
            continue;
        }
        String_View item = sv_from_cstr(arena_strndup(arena, input.data + start, end - start));
        string_list_add(out_groups, arena, item);
    }

    regfree(&re);
    return true;
}

bool cmk_regex_match_first(Arena *arena, String_View pattern, String_View input, String_View *out_match) {
    if (!out_match) return false;
    *out_match = sv_from_cstr("");
    String_List groups = {0};
    if (!cmk_regex_match_groups(arena, pattern, input, &groups)) return false;
    if (groups.count == 0) return false;
    *out_match = groups.items[0];
    return true;
}

String_View cmk_regex_replace_backrefs(Arena *arena, String_View pattern, String_View input, String_View replacement) {
    if (!arena) return input;
    if (pattern.count == 0) return input;

    regex_t re = {0};
    if (!cmk_regex_compile(&re, arena, pattern)) return input;
    replacement = cmk_regex_unescape_cmake(arena, replacement);

    size_t max_groups = re.re_nsub + 1;
    if (max_groups < 1) max_groups = 1;
    if (max_groups > 64) max_groups = 64;

    regmatch_t matches[64] = {0};
    String_Builder out = {0};
    size_t cursor = 0;

    while (cursor <= input.count) {
        String_View rest = nob_sv_from_parts(input.data + cursor, input.count - cursor);
        int rc = regexec(&re, nob_temp_sv_to_cstr(rest), max_groups, matches, 0);
        if (rc != 0) {
            if (rest.count > 0) sb_append_buf(&out, rest.data, rest.count);
            break;
        }

        regmatch_t full = matches[0];
        if (full.rm_so < 0 || full.rm_eo < full.rm_so) {
            if (rest.count > 0) sb_append_buf(&out, rest.data, rest.count);
            break;
        }

        size_t pre = (size_t)full.rm_so;
        size_t match_len = (size_t)(full.rm_eo - full.rm_so);
        if (pre > rest.count) pre = rest.count;
        sb_append_buf(&out, rest.data, pre);

        for (size_t i = 0; i < replacement.count; i++) {
            char ch = replacement.data[i];
            if (ch == '\\' && i + 1 < replacement.count) {
                char nxt = replacement.data[i + 1];
                if (isdigit((unsigned char)nxt)) {
                    size_t gi = (size_t)(nxt - '0');
                    if (gi < max_groups) {
                        cmk_regex_append_group(&out, rest, matches[gi]);
                    }
                    i++;
                    continue;
                }
                if (nxt == '\\') {
                    sb_append(&out, '\\');
                    i++;
                    continue;
                }
            }
            sb_append(&out, ch);
        }

        if (match_len == 0) {
            if (pre < rest.count) {
                sb_append(&out, rest.data[pre]);
                cursor += pre + 1;
            } else {
                break;
            }
        } else {
            cursor += pre + match_len;
        }
    }

    String_View result = sv_from_cstr(arena_strndup(arena, out.items ? out.items : "", out.count));
    nob_sb_free(out);
    regfree(&re);
    return result;
}

#endif
