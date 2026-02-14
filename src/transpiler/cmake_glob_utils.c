#include "cmake_glob_utils.h"

#include "cmake_path_utils.h"
#include "sys_utils.h"

#include <string.h>

static bool cmk_glob_path_is_dot_or_dotdot(String_View p) {
    return nob_sv_eq(p, sv_from_cstr(".")) || nob_sv_eq(p, sv_from_cstr(".."));
}

bool cmk_glob_match(String_View pattern, String_View path) {
    const char *star = memchr(pattern.data, '*', pattern.count);
    if (!star) return nob_sv_eq(pattern, path);
    size_t left = (size_t)(star - pattern.data);
    size_t right = pattern.count - left - 1;
    if (path.count < left + right) return false;
    if (left > 0 && memcmp(pattern.data, path.data, left) != 0) return false;
    if (right > 0 && memcmp(pattern.data + pattern.count - right, path.data + path.count - right, right) != 0) return false;
    return true;
}

bool cmk_glob_collect_recursive(Arena *arena,
                                String_View base_dir,
                                String_View rel_prefix,
                                String_View pattern,
                                bool list_directories,
                                String_View relative_base,
                                String_Builder *list,
                                bool *first) {
    if (!arena || !list || !first) return false;

    Nob_File_Paths children = {0};
    if (!sys_read_dir(base_dir, &children)) return false;

    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (cmk_glob_path_is_dot_or_dotdot(name)) continue;

        String_View child_abs = cmk_path_join(arena, base_dir, name);
        String_View child_rel = rel_prefix.count > 0 ? cmk_path_join(arena, rel_prefix, name) : name;
        Nob_File_Type type = sys_get_file_type(child_abs);

        bool can_emit = (type == NOB_FILE_REGULAR || type == NOB_FILE_SYMLINK || type == NOB_FILE_OTHER) ||
                        (list_directories && type == NOB_FILE_DIRECTORY);
        if (can_emit && cmk_glob_match(pattern, child_rel)) {
            String_View emit = child_abs;
            if (relative_base.count > 0) {
                emit = cmk_path_relativize(arena, child_abs, relative_base);
            }
            if (!*first) sb_append(list, ';');
            sb_append_buf(list, emit.data, emit.count);
            *first = false;
        }
        if (type == NOB_FILE_DIRECTORY) {
            (void)cmk_glob_collect_recursive(arena, child_abs, child_rel, pattern, list_directories, relative_base, list, first);
        }
    }

    nob_da_free(children);
    return true;
}
