#include "cmake_path_utils.h"

#include <ctype.h>
#include <stddef.h>

static bool cmk_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

static size_t cmk_path_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        char c = path.data[i - 1];
        if (c == '/' || c == '\\') return i - 1;
    }
    return SIZE_MAX;
}

static String_View cmk_path_root_name(String_View path) {
    bool has_drive = path.count >= 2 &&
                     isalpha((unsigned char)path.data[0]) &&
                     path.data[1] == ':';
    if (has_drive) return nob_sv_from_parts(path.data, 2);
    return sv_from_cstr("");
}

static String_View cmk_path_root_directory(String_View path) {
    if (path.count == 0) return sv_from_cstr("");
    if ((path.count >= 1 && (path.data[0] == '/' || path.data[0] == '\\')) ||
        (path.count >= 3 && isalpha((unsigned char)path.data[0]) && path.data[1] == ':' &&
         (path.data[2] == '/' || path.data[2] == '\\'))) {
        return sv_from_cstr("/");
    }
    return sv_from_cstr("");
}

static String_View cmk_path_root_path(Arena *arena, String_View path) {
    String_View root_name = cmk_path_root_name(path);
    String_View root_dir = cmk_path_root_directory(path);
    if (root_name.count == 0) return root_dir;
    if (root_dir.count == 0) return root_name;
    String_Builder sb = {0};
    sb_append_buf(&sb, root_name.data, root_name.count);
    sb_append_buf(&sb, root_dir.data, root_dir.count);
    String_View out = sv_from_cstr(arena_strndup(arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View cmk_path_relative_part(Arena *arena, String_View path) {
    String_View root = cmk_path_root_path(arena, path);
    if (root.count == 0) return path;
    if (path.count <= root.count) return sv_from_cstr("");
    String_View rel = nob_sv_from_parts(path.data + root.count, path.count - root.count);
    while (rel.count > 0 && (rel.data[0] == '/' || rel.data[0] == '\\')) {
        rel = nob_sv_from_parts(rel.data + 1, rel.count - 1);
    }
    return rel;
}

static String_View cmk_path_parent_part(String_View path) {
    size_t sep = cmk_path_last_separator_index(path);
    if (sep == SIZE_MAX) return sv_from_cstr("");
    if (sep == 0) return sv_from_cstr("/");
    if (sep == 2 && path.count >= 3 && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 3);
    }
    return nob_sv_from_parts(path.data, sep);
}

bool cmk_path_is_absolute(String_View path) {
    return build_path_is_absolute(path);
}

String_View cmk_path_make_absolute(Arena *arena, String_View path) {
    return build_path_make_absolute(arena, path);
}

String_View cmk_path_join(Arena *arena, String_View base, String_View rel) {
    return build_path_join(arena, base, rel);
}

String_View cmk_path_parent(Arena *arena, String_View full_path) {
    return build_path_parent_dir(arena, full_path);
}

String_View cmk_path_basename(String_View path) {
    size_t sep = cmk_path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - (sep + 1));
}

String_View cmk_path_extension(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - dot);
}

String_View cmk_path_stem(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return name;
    return nob_sv_from_parts(name.data, dot);
}

String_View cmk_path_normalize(Arena *arena, String_View input) {
    if (!arena) return sv_from_cstr("");
    if (input.count == 0) return sv_from_cstr(".");

    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;
    if (has_drive) {
        pos = 2;
        if (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) {
            absolute = true;
            while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
        }
    } else if (input.data[0] == '/' || input.data[0] == '\\') {
        absolute = true;
        while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
    }

    String_List segments = {0};
    string_list_init(&segments);
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && input.data[pos] != '/' && input.data[pos] != '\\') pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
        if (seg.count == 0 || nob_sv_eq(seg, sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, sv_from_cstr(".."))) {
            if (segments.count > 0 && !nob_sv_eq(segments.items[segments.count - 1], sv_from_cstr(".."))) {
                segments.count--;
            } else if (!absolute) {
                string_list_add(&segments, arena, seg);
            }
            continue;
        }
        string_list_add(&segments, arena, seg);
    }

    String_Builder sb = {0};
    if (has_drive) {
        sb_append(&sb, input.data[0]);
        sb_append(&sb, ':');
    }
    if (absolute) sb_append(&sb, '/');

    for (size_t i = 0; i < segments.count; i++) {
        if ((absolute || has_drive) && sb.count > 0 && sb.items[sb.count - 1] != '/') sb_append(&sb, '/');
        if (!absolute && !has_drive && i > 0) sb_append(&sb, '/');
        sb_append_buf(&sb, segments.items[i].data, segments.items[i].count);
    }

    if (sb.count == 0) {
        if (has_drive && absolute) {
            sb_append(&sb, input.data[0]);
            sb_append_cstr(&sb, ":/");
        } else if (has_drive) {
            sb_append(&sb, input.data[0]);
            sb_append(&sb, ':');
        } else if (absolute) {
            sb_append(&sb, '/');
        } else {
            sb_append(&sb, '.');
        }
    }

    String_View out = sv_from_cstr(arena_strndup(arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

String_View cmk_path_relativize(Arena *arena, String_View path, String_View base_dir) {
    if (!arena) return sv_from_cstr("");

    String_View a = cmk_path_normalize(arena, path);
    String_View b = cmk_path_normalize(arena, base_dir);

    String_List seg_a = {0};
    String_List seg_b = {0};
    string_list_init(&seg_a);
    string_list_init(&seg_b);

    size_t start_a = 0;
    size_t start_b = 0;
    String_View root_a = cmk_path_root_path(arena, a);
    String_View root_b = cmk_path_root_path(arena, b);
    if (!nob_sv_eq(root_a, root_b)) return a;
    if (root_a.count > 0) {
        start_a = root_a.count;
        start_b = root_b.count;
    }

    for (size_t i = start_a; i <= a.count; i++) {
        bool sep = (i == a.count) || a.data[i] == '/' || a.data[i] == '\\';
        if (!sep) continue;
        if (i > start_a) string_list_add(&seg_a, arena, nob_sv_from_parts(a.data + start_a, i - start_a));
        start_a = i + 1;
    }
    for (size_t i = start_b; i <= b.count; i++) {
        bool sep = (i == b.count) || b.data[i] == '/' || b.data[i] == '\\';
        if (!sep) continue;
        if (i > start_b) string_list_add(&seg_b, arena, nob_sv_from_parts(b.data + start_b, i - start_b));
        start_b = i + 1;
    }

    size_t common = 0;
    while (common < seg_a.count && common < seg_b.count && nob_sv_eq(seg_a.items[common], seg_b.items[common])) {
        common++;
    }

    String_Builder sb = {0};
    for (size_t i = common; i < seg_b.count; i++) {
        if (sb.count > 0) sb_append(&sb, '/');
        sb_append_cstr(&sb, "..");
    }
    for (size_t i = common; i < seg_a.count; i++) {
        if (sb.count > 0) sb_append(&sb, '/');
        sb_append_buf(&sb, seg_a.items[i].data, seg_a.items[i].count);
    }
    if (sb.count == 0) sb_append(&sb, '.');
    String_View out = sv_from_cstr(arena_strndup(arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

String_View cmk_path_get_component(Arena *arena, String_View input, String_View component, bool *supported) {
    if (supported) *supported = true;
    if (cmk_sv_eq_ci(component, sv_from_cstr("ROOT_NAME"))) {
        return cmk_path_root_name(input);
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("ROOT_DIRECTORY"))) {
        return cmk_path_root_directory(input);
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("ROOT_PATH"))) {
        return cmk_path_root_path(arena, input);
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("FILENAME"))) {
        return cmk_path_basename(input);
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("STEM"))) {
        return cmk_path_stem(cmk_path_basename(input));
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("EXTENSION"))) {
        return cmk_path_extension(cmk_path_basename(input));
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("RELATIVE_PART"))) {
        return cmk_path_relative_part(arena, input);
    }
    if (cmk_sv_eq_ci(component, sv_from_cstr("PARENT_PATH"))) {
        return cmk_path_parent_part(input);
    }
    if (supported) *supported = false;
    return sv_from_cstr("");
}
