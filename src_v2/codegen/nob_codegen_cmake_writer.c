#include "nob_codegen_internal.h"

bool cg_target_export_name(CG_Context *ctx, BM_Target_Id id, String_View *out) {
    String_View export_name = {0};
    if (!ctx || !out) return false;
    *out = bm_query_target_name(ctx->model, id);
    if (!bm_query_target_property_value(ctx->model, id, nob_sv_from_cstr("EXPORT_NAME"), ctx->scratch, &export_name)) {
        return false;
    }
    if (export_name.count > 0) *out = export_name;
    return true;
}

bool cg_target_exported_name(CG_Context *ctx,
                             BM_Target_Id id,
                             String_View export_namespace,
                             String_View *out) {
    Nob_String_Builder sb = {0};
    String_View export_name = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_target_export_name(ctx, id, &export_name)) return false;
    nob_sb_append_buf(&sb, export_namespace.data ? export_namespace.data : "", export_namespace.count);
    nob_sb_append_buf(&sb, export_name.data ? export_name.data : "", export_name.count);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_cmake_append_escaped(Nob_String_Builder *sb, String_View value) {
    if (!sb) return false;
    for (size_t i = 0; i < value.count; ++i) {
        char c = value.data ? value.data[i] : '\0';
        if (c == '\\') {
            nob_sb_append_cstr(sb, "/");
        } else if (c == '"') {
            nob_sb_append_cstr(sb, "\\\"");
        } else {
            nob_sb_append_buf(sb, &c, 1);
        }
    }
    return true;
}

bool cg_join_sv_list(Arena *scratch, String_View *items, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (items[i].count == 0) continue;
        if (sb.count > 0) nob_sb_append_cstr(&sb, ";");
        nob_sb_append_buf(&sb, items[i].data ? items[i].data : "", items[i].count);
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_export_noconfig_file_name(CG_Context *ctx,
                                  BM_Export_Id export_id,
                                  String_View *out) {
    String_View file_name = {0};
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    size_t stem_len = 0;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    file_name = bm_query_export_file_name(ctx->model, export_id);
    if (file_name.count == 0) return true;

    stem_len = file_name.count;
    if (file_name.count >= strlen(".cmake") &&
        strncmp(file_name.data + file_name.count - strlen(".cmake"), ".cmake", strlen(".cmake")) == 0) {
        stem_len -= strlen(".cmake");
    }

    nob_sb_append_buf(&sb, file_name.data ? file_name.data : "", stem_len);
    nob_sb_append_cstr(&sb, "-noconfig");
    nob_sb_append_buf(&sb,
                      file_name.data ? file_name.data + stem_len : "",
                      file_name.count > stem_len ? file_name.count - stem_len : 0);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

bool cg_export_noconfig_output_file_path(CG_Context *ctx,
                                         BM_Export_Id export_id,
                                         String_View *out) {
    String_View destination = {0};
    String_View file_name = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    destination = bm_query_export_destination(ctx->model, export_id);
    if (!cg_export_noconfig_file_name(ctx, export_id, &file_name)) return false;
    if (destination.count == 0) {
        *out = file_name;
        return true;
    }
    return cg_join_paths_to_arena(ctx->scratch, destination, file_name, out);
}

bool cg_export_target_in_span(BM_Target_Id_Span span, BM_Target_Id id) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i] == id) return true;
    }
    return false;
}

bool cg_emit_cmake_imported_target_declaration(BM_Target_Kind kind,
                                               String_View exported_name,
                                               Nob_String_Builder *sb) {
    const char *cmake_kind = "UNKNOWN";
    if (!sb) return false;

    switch (kind) {
        case BM_TARGET_EXECUTABLE: cmake_kind = "EXECUTABLE"; break;
        case BM_TARGET_STATIC_LIBRARY: cmake_kind = "STATIC"; break;
        case BM_TARGET_SHARED_LIBRARY: cmake_kind = "SHARED"; break;
        case BM_TARGET_MODULE_LIBRARY: cmake_kind = "MODULE"; break;
        case BM_TARGET_INTERFACE_LIBRARY: cmake_kind = "INTERFACE"; break;
        default:
            nob_log(NOB_ERROR,
                    "codegen: unsupported exported target kind for '%.*s'",
                    (int)exported_name.count,
                    exported_name.data ? exported_name.data : "");
            return false;
    }

    nob_sb_append_cstr(sb, "if(NOT TARGET ");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, ")\n  ");
    if (kind == BM_TARGET_EXECUTABLE) {
        nob_sb_append_cstr(sb, "add_executable(");
    } else {
        nob_sb_append_cstr(sb, "add_library(");
    }
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " ");
    nob_sb_append_cstr(sb, cmake_kind);
    nob_sb_append_cstr(sb, " IMPORTED)\nendif()\n");
    return true;
}

bool cg_build_cmake_targets_noconfig_file_contents(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   String_View config,
                                                   CG_CMake_Target_Properties_Emitter emit_properties,
                                                   void *userdata,
                                                   String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!ctx || !emit_properties || !out) return false;
    *out = nob_sv_from_cstr("");

    nob_sb_append_cstr(&sb, "# Generated by Nobify\n\n");
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        String_View exported_name = {0};
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name) ||
            !emit_properties(ctx,
                             export_id,
                             target_id,
                             exported_name,
                             export_namespace,
                             config,
                             userdata,
                             &sb)) {
            nob_sb_free(sb);
            return false;
        }
    }

    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_build_cmake_targets_file_contents(CG_Context *ctx,
                                          BM_Export_Id export_id,
                                          String_View config,
                                          bool include_noconfig,
                                          CG_CMake_Target_Properties_Emitter emit_properties,
                                          void *userdata,
                                          String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    String_View noconfig_name = {0};
    if (!ctx || !emit_properties || !out) return false;
    *out = nob_sv_from_cstr("");

    if (include_noconfig && !cg_export_noconfig_file_name(ctx, export_id, &noconfig_name)) return false;

    nob_sb_append_cstr(&sb, "# Generated by Nobify\n\n");
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        BM_Target_Kind kind = bm_query_target_kind(ctx->model, target_id);
        String_View exported_name = {0};
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name) ||
            !cg_emit_cmake_imported_target_declaration(kind, exported_name, &sb)) {
            nob_sb_free(sb);
            return false;
        }
        if (!include_noconfig &&
            !emit_properties(ctx,
                             export_id,
                             target_id,
                             exported_name,
                             export_namespace,
                             config,
                             userdata,
                             &sb)) {
            nob_sb_free(sb);
            return false;
        }
    }

    if (include_noconfig) {
        nob_sb_append_cstr(&sb, "include(\"${CMAKE_CURRENT_LIST_DIR}/");
        if (!cg_cmake_append_escaped(&sb, noconfig_name)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "\")\n");
    }

    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}
