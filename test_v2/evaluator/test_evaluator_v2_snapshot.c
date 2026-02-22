#include "test_evaluator_v2_snapshot.h"

#include "diagnostics.h"

#include <string.h>

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

static const char *event_kind_name(Cmake_Event_Kind kind) {
    switch (kind) {
        case EV_DIAGNOSTIC: return "EV_DIAGNOSTIC";
        case EV_PROJECT_DECLARE: return "EV_PROJECT_DECLARE";
        case EV_VAR_SET: return "EV_VAR_SET";
        case EV_SET_CACHE_ENTRY: return "EV_SET_CACHE_ENTRY";
        case EV_TARGET_DECLARE: return "EV_TARGET_DECLARE";
        case EV_TARGET_ADD_SOURCE: return "EV_TARGET_ADD_SOURCE";
        case EV_TARGET_PROP_SET: return "EV_TARGET_PROP_SET";
        case EV_TARGET_INCLUDE_DIRECTORIES: return "EV_TARGET_INCLUDE_DIRECTORIES";
        case EV_TARGET_COMPILE_DEFINITIONS: return "EV_TARGET_COMPILE_DEFINITIONS";
        case EV_TARGET_COMPILE_OPTIONS: return "EV_TARGET_COMPILE_OPTIONS";
        case EV_TARGET_LINK_LIBRARIES: return "EV_TARGET_LINK_LIBRARIES";
        case EV_GLOBAL_COMPILE_DEFINITIONS: return "EV_GLOBAL_COMPILE_DEFINITIONS";
        case EV_GLOBAL_COMPILE_OPTIONS: return "EV_GLOBAL_COMPILE_OPTIONS";
        case EV_FIND_PACKAGE: return "EV_FIND_PACKAGE";
    }
    return "EV_UNKNOWN";
}

static const char *diag_sev_name(Cmake_Diag_Severity sev) {
    switch (sev) {
        case EV_DIAG_WARNING: return "WARNING";
        case EV_DIAG_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const char *target_type_name(Cmake_Target_Type type) {
    switch (type) {
        case EV_TARGET_EXECUTABLE: return "EXECUTABLE";
        case EV_TARGET_LIBRARY_STATIC: return "LIB_STATIC";
        case EV_TARGET_LIBRARY_SHARED: return "LIB_SHARED";
        case EV_TARGET_LIBRARY_MODULE: return "LIB_MODULE";
        case EV_TARGET_LIBRARY_INTERFACE: return "LIB_INTERFACE";
        case EV_TARGET_LIBRARY_OBJECT: return "LIB_OBJECT";
        case EV_TARGET_LIBRARY_UNKNOWN: return "LIB_UNKNOWN";
    }
    return "UNKNOWN";
}

static const char *visibility_name(Cmake_Visibility visibility) {
    switch (visibility) {
        case EV_VISIBILITY_UNSPECIFIED: return "UNSPECIFIED";
        case EV_VISIBILITY_PRIVATE: return "PRIVATE";
        case EV_VISIBILITY_PUBLIC: return "PUBLIC";
        case EV_VISIBILITY_INTERFACE: return "INTERFACE";
    }
    return "UNKNOWN";
}

static const char *prop_op_name(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return "SET";
        case EV_PROP_APPEND_LIST: return "APPEND_LIST";
        case EV_PROP_APPEND_STRING: return "APPEND_STRING";
    }
    return "UNKNOWN";
}

bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
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

String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in) {
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

static String_View normalize_path_to_arena(Arena *arena, String_View path, String_View root_norm) {
    if (!arena || path.count == 0) return path;

    char *tmp = (char*)arena_alloc(arena, path.count + 1);
    if (!tmp) return path;
    for (size_t i = 0; i < path.count; i++) {
        char c = path.data[i];
        tmp[i] = (c == '\\') ? '/' : c;
    }
    tmp[path.count] = '\0';
    String_View norm = nob_sv_from_parts(tmp, path.count);

    if (root_norm.count > 0 && norm.count >= root_norm.count &&
        memcmp(norm.data, root_norm.data, root_norm.count) == 0 &&
        (norm.count == root_norm.count || norm.data[root_norm.count] == '/')) {
        size_t suffix_count = norm.count - root_norm.count;
        char *out = (char*)arena_alloc(arena, 6 + suffix_count + 1);
        if (!out) return norm;
        memcpy(out, "<ROOT>", 6);
        if (suffix_count > 0) memcpy(out + 6, norm.data + root_norm.count, suffix_count);
        out[6 + suffix_count] = '\0';
        return nob_sv_from_parts(out, 6 + suffix_count);
    }

    return norm;
}

static String_View normalize_root_to_arena(Arena *arena, const char *workspace_root) {
    if (!arena || !workspace_root) return nob_sv_from_cstr("");
    String_View in = nob_sv_from_cstr(workspace_root);
    if (in.count == 0) return in;

    char *tmp = (char*)arena_alloc(arena, in.count + 1);
    if (!tmp) return nob_sv_from_cstr("");
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        tmp[i] = (c == '\\') ? '/' : c;
    }
    tmp[in.count] = '\0';

    size_t n = in.count;
    while (n > 0 && tmp[n - 1] == '/') n--;
    tmp[n] = '\0';
    return nob_sv_from_parts(tmp, n);
}

static void append_kv_sv(Nob_String_Builder *sb, const char *key, String_View value) {
    nob_sb_append_cstr(sb, " ");
    nob_sb_append_cstr(sb, key);
    nob_sb_append_cstr(sb, "=");
    snapshot_append_escaped_sv(sb, value);
}

bool evaluator_render_snapshot_to_arena(Arena *arena,
                                        const Cmake_Event_Stream *stream,
                                        const char *workspace_root,
                                        String_View *out) {
    if (!arena || !stream || !out) return false;

    String_View root_norm = normalize_root_to_arena(arena, workspace_root);

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("EVENTS count=%zu\n", stream->count));

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        String_View file = normalize_path_to_arena(arena, ev->origin.file_path, root_norm);

        nob_sb_append_cstr(&sb, nob_temp_sprintf("EV[%zu] kind=%s", i, event_kind_name(ev->kind)));
        append_kv_sv(&sb, "file", file);
        nob_sb_append_cstr(&sb, nob_temp_sprintf(" line=%zu col=%zu", ev->origin.line, ev->origin.col));

        switch (ev->kind) {
            case EV_DIAGNOSTIC:
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" sev=%s", diag_sev_name(ev->as.diag.severity)));
                append_kv_sv(&sb, "component", ev->as.diag.component);
                append_kv_sv(&sb, "command", ev->as.diag.command);
                append_kv_sv(&sb, "cause", ev->as.diag.cause);
                append_kv_sv(&sb, "hint", ev->as.diag.hint);
                break;

            case EV_PROJECT_DECLARE:
                append_kv_sv(&sb, "name", ev->as.project_declare.name);
                append_kv_sv(&sb, "version", ev->as.project_declare.version);
                append_kv_sv(&sb, "description", ev->as.project_declare.description);
                append_kv_sv(&sb, "languages", ev->as.project_declare.languages);
                break;

            case EV_VAR_SET:
                append_kv_sv(&sb, "key", ev->as.var_set.key);
                append_kv_sv(&sb, "value", ev->as.var_set.value);
                break;

            case EV_SET_CACHE_ENTRY:
                append_kv_sv(&sb, "key", ev->as.cache_entry.key);
                append_kv_sv(&sb, "value", ev->as.cache_entry.value);
                break;

            case EV_TARGET_DECLARE:
                append_kv_sv(&sb, "name", ev->as.target_declare.name);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" type=%s", target_type_name(ev->as.target_declare.type)));
                break;

            case EV_TARGET_ADD_SOURCE: {
                String_View path = normalize_path_to_arena(arena, ev->as.target_add_source.path, root_norm);
                append_kv_sv(&sb, "target", ev->as.target_add_source.target_name);
                append_kv_sv(&sb, "path", path);
            } break;

            case EV_TARGET_PROP_SET:
                append_kv_sv(&sb, "target", ev->as.target_prop_set.target_name);
                append_kv_sv(&sb, "key", ev->as.target_prop_set.key);
                append_kv_sv(&sb, "value", ev->as.target_prop_set.value);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" op=%s", prop_op_name(ev->as.target_prop_set.op)));
                break;

            case EV_TARGET_INCLUDE_DIRECTORIES: {
                String_View path = normalize_path_to_arena(arena, ev->as.target_include_directories.path, root_norm);
                append_kv_sv(&sb, "target", ev->as.target_include_directories.target_name);
                append_kv_sv(&sb, "path", path);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" vis=%s is_system=%d is_before=%d",
                    visibility_name(ev->as.target_include_directories.visibility),
                    ev->as.target_include_directories.is_system ? 1 : 0,
                    ev->as.target_include_directories.is_before ? 1 : 0));
            } break;

            case EV_TARGET_COMPILE_DEFINITIONS:
                append_kv_sv(&sb, "target", ev->as.target_compile_definitions.target_name);
                append_kv_sv(&sb, "item", ev->as.target_compile_definitions.item);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_compile_definitions.visibility)));
                break;

            case EV_TARGET_COMPILE_OPTIONS:
                append_kv_sv(&sb, "target", ev->as.target_compile_options.target_name);
                append_kv_sv(&sb, "item", ev->as.target_compile_options.item);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_compile_options.visibility)));
                break;

            case EV_TARGET_LINK_LIBRARIES:
                append_kv_sv(&sb, "target", ev->as.target_link_libraries.target_name);
                append_kv_sv(&sb, "item", ev->as.target_link_libraries.item);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_libraries.visibility)));
                break;

            case EV_GLOBAL_COMPILE_DEFINITIONS:
                append_kv_sv(&sb, "item", ev->as.global_compile_definitions.item);
                break;

            case EV_GLOBAL_COMPILE_OPTIONS:
                append_kv_sv(&sb, "item", ev->as.global_compile_options.item);
                break;

            case EV_FIND_PACKAGE: {
                String_View location = normalize_path_to_arena(arena, ev->as.find_package.location, root_norm);
                append_kv_sv(&sb, "package", ev->as.find_package.package_name);
                append_kv_sv(&sb, "mode", ev->as.find_package.mode);
                nob_sb_append_cstr(&sb, nob_temp_sprintf(" required=%d found=%d",
                    ev->as.find_package.required ? 1 : 0,
                    ev->as.find_package.found ? 1 : 0));
                append_kv_sv(&sb, "location", location);
            } break;
        }

        nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}