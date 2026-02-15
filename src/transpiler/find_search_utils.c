#include "find_search_utils.h"
#include "build_model.h"

#include "cmake_path_utils.h"
#include "sys_utils.h"

#include <ctype.h>
#include <stdlib.h>

static String_View find_trim_ws(String_View sv) {
    size_t begin = 0;
    while (begin < sv.count && isspace((unsigned char)sv.data[begin])) begin++;
    size_t end = sv.count;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;
    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static bool find_path_has_extension(String_View path) {
    String_View base = cmk_path_basename(path);
    for (size_t i = 0; i < base.count; i++) {
        if (base.data[i] == '.') return true;
    }
    return false;
}

void find_search_split_env_path(Arena *arena, String_View value, String_List *out_dirs) {
    if (!arena || !out_dirs || value.count == 0) return;
    bool semicolon_sep = false;
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == ';') {
            semicolon_sep = true;
            break;
        }
    }
    char sep = semicolon_sep ? ';' : ':';
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool at_sep = (i == value.count) || value.data[i] == sep;
        if (!at_sep) continue;
        if (i > start) {
            String_View d = find_trim_ws(nob_sv_from_parts(value.data + start, i - start));
            if (d.count > 0) string_list_add_unique(out_dirs, arena, d);
        }
        start = i + 1;
    }
}

void find_search_collect_program_name_variants(Arena *arena, String_View name, String_List *out_names) {
    if (!arena || !out_names || name.count == 0) return;
    string_list_add_unique(out_names, arena, name);
#if defined(_WIN32)
    if (!find_path_has_extension(name)) {
        string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".exe", SV_Arg(name))));
        string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".cmd", SV_Arg(name))));
        string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".bat", SV_Arg(name))));
        string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".com", SV_Arg(name))));
    }
#endif
}

void find_search_collect_library_name_variants(Arena *arena, String_View name, String_List *out_names) {
    if (!arena || !out_names || name.count == 0) return;
    string_list_add_unique(out_names, arena, name);
    if (find_path_has_extension(name)) return;
#if defined(_WIN32)
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".lib", SV_Arg(name))));
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".lib", SV_Arg(name))));
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".dll", SV_Arg(name))));
#elif defined(__APPLE__)
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".dylib", SV_Arg(name))));
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".a", SV_Arg(name))));
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".so", SV_Arg(name))));
#else
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".so", SV_Arg(name))));
    string_list_add_unique(out_names, arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".a", SV_Arg(name))));
#endif
}

bool find_search_candidates(Arena *arena,
                            const String_List *dirs,
                            const String_List *suffixes,
                            const String_List *names,
                            String_View *out_path) {
    if (!arena || !dirs || !names || !out_path) return false;
    for (size_t n = 0; n < names->count; n++) {
        String_View name = names->items[n];
        if (cmk_path_is_absolute(name) && sys_file_exists(name)) {
            *out_path = cmk_path_make_absolute(arena, name);
            return true;
        }
    }

    bool no_suffixes = !suffixes || suffixes->count == 0;
    for (size_t d = 0; d < dirs->count; d++) {
        String_View dir = dirs->items[d];
        if (no_suffixes) {
            for (size_t n = 0; n < names->count; n++) {
                String_View candidate = cmk_path_join(arena, dir, names->items[n]);
                if (sys_file_exists(candidate)) {
                    *out_path = cmk_path_make_absolute(arena, candidate);
                    return true;
                }
            }
        } else {
            for (size_t s = 0; s < suffixes->count; s++) {
                String_View suffix = suffixes->items[s];
                String_View base = suffix.count == 0 ? dir : cmk_path_join(arena, dir, suffix);
                for (size_t n = 0; n < names->count; n++) {
                    String_View candidate = cmk_path_join(arena, base, names->items[n]);
                    if (sys_file_exists(candidate)) {
                        *out_path = cmk_path_make_absolute(arena, candidate);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool find_search_path_candidates(Arena *arena,
                                 const String_List *dirs,
                                 const String_List *suffixes,
                                 const String_List *names,
                                 String_View *out_dir) {
    if (!arena || !dirs || !names || !out_dir) return false;

    bool no_suffixes = !suffixes || suffixes->count == 0;
    for (size_t d = 0; d < dirs->count; d++) {
        String_View dir = dirs->items[d];
        if (no_suffixes) {
            for (size_t n = 0; n < names->count; n++) {
                String_View candidate = cmk_path_join(arena, dir, names->items[n]);
                if (sys_file_exists(candidate)) {
                    *out_dir = cmk_path_make_absolute(arena, dir);
                    return true;
                }
            }
        } else {
            for (size_t s = 0; s < suffixes->count; s++) {
                String_View suffix = suffixes->items[s];
                String_View base = suffix.count == 0 ? dir : cmk_path_join(arena, dir, suffix);
                for (size_t n = 0; n < names->count; n++) {
                    String_View candidate = cmk_path_join(arena, base, names->items[n]);
                    if (sys_file_exists(candidate)) {
                        *out_dir = cmk_path_make_absolute(arena, base);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
