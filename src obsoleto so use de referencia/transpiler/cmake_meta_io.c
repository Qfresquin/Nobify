#include "cmake_meta_io.h"

#include "cmake_path_utils.h"
#include "sys_utils.h"

#include <stdlib.h>

static void cmk_meta_json_append_escaped(String_Builder *sb, String_View s) {
    if (!sb) return;
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '\\') sb_append_cstr(sb, "\\\\");
        else if (c == '"') sb_append_cstr(sb, "\\\"");
        else if (c == '\n') sb_append_cstr(sb, "\\n");
        else if (c == '\r') sb_append_cstr(sb, "\\r");
        else if (c == '\t') sb_append_cstr(sb, "\\t");
        else if (c < 0x20) sb_append_cstr(sb, "?");
        else sb_append(sb, (char)c);
    }
}

int cmk_meta_parse_version_major(String_View tok, int fallback) {
    if (tok.count == 0) return fallback;
    const char *text = nob_temp_sv_to_cstr(tok);
    if ((text[0] == 'v' || text[0] == 'V') && text[1] != '\0') text++;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (!end || end == text || parsed <= 0) return fallback;
    return (int)parsed;
}

bool cmk_meta_emit_file_api_query(Arena *arena, String_View query_root, String_View kind, String_View version_token) {
    if (!arena || kind.count == 0) return false;
    int major = cmk_meta_parse_version_major(version_token, 1);
    String_View file_name = sv_from_cstr(nob_temp_sprintf("%s-v%d.json", nob_temp_sv_to_cstr(kind), major));
    String_View out_path = cmk_path_join(arena, query_root, file_name);
    if (!sys_ensure_parent_dirs(arena, out_path)) return false;
    return sys_write_file_bytes(out_path, "", 0);
}

bool cmk_meta_emit_empty_file_api_query(Arena *arena, String_View query_root) {
    if (!arena || query_root.count == 0) return false;
    String_View out_path = cmk_path_join(arena, query_root, sv_from_cstr("query.json"));
    if (!sys_ensure_parent_dirs(arena, out_path)) return false;
    return sys_write_file_bytes(out_path, "", 0);
}

bool cmk_meta_emit_instrumentation_query(Arena *arena,
                                         String_View root,
                                         const String_List *hooks,
                                         const String_List *queries,
                                         const String_List *callbacks,
                                         size_t query_counter,
                                         String_View *out_path) {
    if (!arena || root.count == 0) return false;

    if (!sys_ensure_parent_dirs(arena, cmk_path_join(arena, root, sv_from_cstr(".keep")))) return false;
    if (!sys_mkdir(root)) return false;

    String_View file_name = sv_from_cstr(nob_temp_sprintf("query_%zu.json", query_counter));
    String_View path = cmk_path_join(arena, root, file_name);

    String_Builder json = {0};
    sb_append_cstr(&json, "{\n");
    sb_append_cstr(&json, "  \"version\": 1");

    if (hooks && hooks->count > 0) {
        sb_append_cstr(&json, ",\n  \"hooks\": [");
        for (size_t i = 0; i < hooks->count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            cmk_meta_json_append_escaped(&json, hooks->items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }

    if (queries && queries->count > 0) {
        sb_append_cstr(&json, ",\n  \"queries\": [");
        for (size_t i = 0; i < queries->count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            cmk_meta_json_append_escaped(&json, queries->items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }

    if (callbacks && callbacks->count > 0) {
        sb_append_cstr(&json, ",\n  \"callbacks\": [");
        for (size_t i = 0; i < callbacks->count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            cmk_meta_json_append_escaped(&json, callbacks->items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }

    sb_append_cstr(&json, "\n}\n");
    bool ok = sys_write_file_bytes(path, json.items ? json.items : "", json.count);
    nob_sb_free(json);
    if (!ok) return false;

    if (out_path) *out_path = path;
    return true;
}

bool cmk_meta_export_write_targets_file(Arena *arena,
                                        String_View out_path,
                                        String_View ns,
                                        String_View signature,
                                        String_View export_set_name,
                                        const String_List *targets,
                                        bool append_mode) {
    if (!arena || out_path.count == 0 || !targets) return false;
    if (!sys_ensure_parent_dirs(arena, out_path)) return false;

    Nob_String_Builder existing = {0};
    if (append_mode && sys_file_exists(out_path)) {
        (void)sys_read_file_builder(out_path, &existing);
    }

    String_Builder sb = {0};
    if (existing.items && existing.count > 0) {
        sb_append_buf(&sb, existing.items, existing.count);
        if (existing.items[existing.count - 1] != '\n') sb_append(&sb, '\n');
    }
    sb_append_cstr(&sb, "# cmk2nob export support\n");
    sb_append_cstr(&sb, "# signature: ");
    sb_append_buf(&sb, signature.data, signature.count);
    sb_append(&sb, '\n');
    if (export_set_name.count > 0) {
        sb_append_cstr(&sb, "# export-set: ");
        sb_append_buf(&sb, export_set_name.data, export_set_name.count);
        sb_append(&sb, '\n');
    }
    if (ns.count > 0) {
        sb_append_cstr(&sb, "# namespace: ");
        sb_append_buf(&sb, ns.data, ns.count);
        sb_append(&sb, '\n');
    }
    sb_append_cstr(&sb, "set(_CMK2NOB_EXPORTED_TARGETS ");
    for (size_t i = 0; i < targets->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, targets->items[i].data, targets->items[i].count);
    }
    sb_append_cstr(&sb, ")\n");
    if (ns.count > 0) {
        sb_append_cstr(&sb, "set(_CMK2NOB_EXPORTED_NAMESPACE \"");
        sb_append_buf(&sb, ns.data, ns.count);
        sb_append_cstr(&sb, "\")\n");
    }

    bool ok = sys_write_file_bytes(out_path, sb.items ? sb.items : "", sb.count);
    nob_sb_free(existing);
    nob_sb_free(sb);
    return ok;
}

bool cmk_meta_export_register_package(Arena *arena,
                                      String_View registry_dir,
                                      String_View package_name,
                                      String_View dir_key,
                                      String_View package_dir) {
    if (!arena || registry_dir.count == 0 || package_name.count == 0 || dir_key.count == 0) return false;
    if (!sys_mkdir(registry_dir)) return false;

    String_Builder reg_name_sb = {0};
    sb_append_buf(&reg_name_sb, package_name.data, package_name.count);
    sb_append_cstr(&reg_name_sb, ".cmake");
    String_View reg_name = sv_from_cstr(arena_strndup(arena, reg_name_sb.items, reg_name_sb.count));
    nob_sb_free(reg_name_sb);

    String_View reg_file = cmk_path_join(arena, registry_dir, reg_name);

    String_Builder sb = {0};
    sb_append_cstr(&sb, "# cmk2nob package registry entry\n");
    sb_append_cstr(&sb, "set(");
    sb_append_buf(&sb, dir_key.data, dir_key.count);
    sb_append_cstr(&sb, " \"");
    sb_append_buf(&sb, package_dir.data, package_dir.count);
    sb_append_cstr(&sb, "\")\n");

    bool ok = sys_write_file_bytes(reg_file, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return ok;
}
