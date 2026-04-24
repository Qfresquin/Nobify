#include "nob_codegen_internal.h"

static bool cg_sb_append_install_join_call(Nob_String_Builder *sb, String_View relative_path) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, "join_install_prefix(install_prefix, ");
    if (!cg_sb_append_c_string(sb, relative_path)) return false;
    nob_sb_append_cstr(sb, ")");
    return true;
}

static bool cg_resolve_single_install_string_for_config(CG_Context *ctx,
                                                        BM_Target_Id current_target_id,
                                                        String_View config,
                                                        String_View raw,
                                                        String_View *out);
static bool cg_split_top_level_list(Arena *arena, String_View value, String_View **out_items);

static String_View cg_install_prefix_genex_token(void) {
    return nob_sv_from_cstr("__NOB_INSTALL_PREFIX__");
}

static bool cg_emit_install_component_guard_open(Nob_String_Builder *sb, String_View component) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, "    if (install_component_matches(install_component, ");
    if (!cg_sb_append_c_string(sb, component)) return false;
    nob_sb_append_cstr(sb, ")) {\n");
    return true;
}

static String_View cg_install_effective_component(String_View specific, String_View fallback) {
    return specific.count > 0 ? specific : fallback;
}

static String_View cg_install_rule_target_component(CG_Context *ctx,
                                                    BM_Install_Rule_Id rule_id,
                                                    BM_Target_Kind kind,
                                                    bool linker_artifact) {
    String_View fallback = {0};
    if (!ctx) return (String_View){0};
    fallback = bm_query_install_rule_component(ctx->model, rule_id);
    switch (kind) {
        case BM_TARGET_EXECUTABLE:
            return cg_install_effective_component(bm_query_install_rule_runtime_component(ctx->model, rule_id),
                                                  fallback);
        case BM_TARGET_STATIC_LIBRARY:
            return cg_install_effective_component(bm_query_install_rule_archive_component(ctx->model, rule_id),
                                                  fallback);
        case BM_TARGET_SHARED_LIBRARY:
        case BM_TARGET_MODULE_LIBRARY:
            if (cg_policy_is_windows(ctx) && linker_artifact) {
                return cg_install_effective_component(bm_query_install_rule_archive_component(ctx->model, rule_id),
                                                      fallback);
            }
            if (cg_policy_is_windows(ctx)) {
                return cg_install_effective_component(bm_query_install_rule_runtime_component(ctx->model, rule_id),
                                                      fallback);
            }
            return cg_install_effective_component(bm_query_install_rule_library_component(ctx->model, rule_id),
                                                  fallback);
        case BM_TARGET_INTERFACE_LIBRARY:
        case BM_TARGET_OBJECT_LIBRARY:
        case BM_TARGET_UTILITY:
        case BM_TARGET_UNKNOWN_LIBRARY:
            return fallback;
    }
    return fallback;
}

static String_View cg_install_target_destination(String_View specific, String_View generic) {
    return specific.count > 0 ? specific : generic;
}

static String_View cg_install_trim_current_dir_prefixes(String_View path) {
    while (path.count >= 2 &&
           path.data[0] == '.' &&
           (path.data[1] == '/' || path.data[1] == '\\')) {
        path.data += 2;
        path.count -= 2;
    }
    return path;
}

static bool cg_install_path_has_prefix(String_View path, String_View prefix) {
    path = cg_install_trim_current_dir_prefixes(path);
    prefix = cg_install_trim_current_dir_prefixes(prefix);
    if (prefix.count == 0 || path.count < prefix.count) return false;
    if (!nob_sv_starts_with(path, prefix)) return false;
    if (path.count == prefix.count) return true;
    return path.data[prefix.count] == '/' || path.data[prefix.count] == '\\';
}

static bool cg_resolve_install_item_from_owner_dirs(CG_Context *ctx,
                                                    BM_Directory_Id owner_dir,
                                                    String_View item,
                                                    String_View *out) {
    String_View source_dir = {0};
    String_View binary_dir = {0};
    String_View source_candidate = {0};
    String_View binary_candidate = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    if (cg_path_is_abs(item)) return cg_rebase_path_from_cwd(ctx, item, out);

    source_dir = bm_query_directory_source_dir(ctx->model, owner_dir);
    binary_dir = bm_query_directory_binary_dir(ctx->model, owner_dir);
    if ((source_dir.count > 0 && cg_install_path_has_prefix(item, source_dir)) ||
        (binary_dir.count > 0 && cg_install_path_has_prefix(item, binary_dir))) {
        return cg_rebase_path_from_cwd(ctx, item, out);
    }
    if (!cg_rebase_from_base(ctx, item, source_dir, &source_candidate)) return false;
    if (nob_file_exists(nob_temp_sv_to_cstr(source_candidate))) {
        *out = source_candidate;
        return true;
    }

    if (!cg_rebase_from_base(ctx, item, binary_dir, &binary_candidate)) return false;
    if (nob_file_exists(nob_temp_sv_to_cstr(binary_candidate))) {
        *out = binary_candidate;
        return true;
    }

    *out = source_candidate.count > 0 ? source_candidate : binary_candidate;
    return true;
}

static bool cg_install_rule_target_destination_for_kind(CG_Context *ctx,
                                                        BM_Install_Rule_Id rule_id,
                                                        BM_Target_Kind kind,
                                                        bool linker_artifact,
                                                        String_View *out) {
    String_View general = bm_query_install_rule_destination(ctx->model, rule_id);
    String_View archive = bm_query_install_rule_archive_destination(ctx->model, rule_id);
    String_View library = bm_query_install_rule_library_destination(ctx->model, rule_id);
    String_View runtime = bm_query_install_rule_runtime_destination(ctx->model, rule_id);
    if (!ctx || !out) return false;
    *out = general;

    switch (kind) {
        case BM_TARGET_EXECUTABLE:
            *out = cg_install_target_destination(runtime, general);
            return true;

        case BM_TARGET_STATIC_LIBRARY:
            *out = cg_install_target_destination(archive, general);
            return true;

        case BM_TARGET_SHARED_LIBRARY:
        case BM_TARGET_MODULE_LIBRARY:
            if (cg_policy_is_windows(ctx) && linker_artifact) {
                *out = cg_install_target_destination(archive, general);
            } else if (cg_policy_is_windows(ctx)) {
                *out = cg_install_target_destination(runtime, general);
            } else {
                *out = cg_install_target_destination(library, general);
            }
            return true;

        case BM_TARGET_INTERFACE_LIBRARY:
        case BM_TARGET_OBJECT_LIBRARY:
        case BM_TARGET_UTILITY:
        case BM_TARGET_UNKNOWN_LIBRARY:
            return true;
    }

    return true;
}

static bool cg_resolve_install_rule_target_destination_for_kind(CG_Context *ctx,
                                                                BM_Install_Rule_Id rule_id,
                                                                BM_Target_Id target_id,
                                                                BM_Target_Kind kind,
                                                                bool linker_artifact,
                                                                String_View config,
                                                                String_View *out) {
    String_View raw_destination = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_install_rule_target_destination_for_kind(ctx, rule_id, kind, linker_artifact, &raw_destination)) {
        return false;
    }
    return cg_resolve_single_install_string_for_config(ctx, target_id, config, raw_destination, out);
}

static bool cg_target_installed_artifact_relpath(CG_Context *ctx,
                                                 BM_Install_Rule_Id rule_id,
                                                 const CG_Target_Info *info,
                                                 bool linker_artifact,
                                                 String_View config,
                                                 String_View *out) {
    String_View destination = {0};
    String_View basename = {0};
    BM_Target_Artifact_View artifact = {0};
    if (!ctx || !info || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_resolve_install_rule_target_destination_for_kind(ctx,
                                                             rule_id,
                                                             info->id,
                                                             info->kind,
                                                             linker_artifact,
                                                             config,
                                                             &destination)) {
        return false;
    }
    if (!cg_target_artifact_for_config_or_empty(info,
                                                linker_artifact ? BM_TARGET_ARTIFACT_LINKER : BM_TARGET_ARTIFACT_RUNTIME,
                                                config,
                                                &artifact)) {
        return false;
    }
    if (artifact.path.count == 0) return true;
    basename = cg_basename_to_arena(ctx->scratch, artifact.path);
    if (destination.count == 0) {
        *out = basename;
        return true;
    }
    return cg_join_paths_to_arena(ctx->scratch, destination, basename, out);
}

static bool cg_install_import_prefix_expr(CG_Context *ctx,
                                          String_View relpath,
                                          String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (relpath.count == 0) return true;
    if (nob_sv_starts_with(relpath, nob_sv_from_cstr("${_IMPORT_PREFIX}"))) {
        *out = relpath;
        return true;
    }
    if (cg_path_is_abs(relpath)) {
        return cg_normalize_path_to_arena(ctx->scratch, relpath, out);
    }
    nob_sb_append_cstr(&sb, "${_IMPORT_PREFIX}/");
    nob_sb_append_buf(&sb, relpath.data ? relpath.data : "", relpath.count);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

static BM_Query_Eval_Context cg_make_install_eval_ctx(CG_Context *ctx,
                                                      BM_Target_Id current_target_id,
                                                      String_View config) {
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   current_target_id,
                                                   BM_QUERY_USAGE_COMPILE,
                                                   config,
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = false;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = true;
    qctx.install_prefix = cg_install_prefix_genex_token();
    return qctx;
}

static bool cg_resolve_install_string_for_config(CG_Context *ctx,
                                                 BM_Target_Id current_target_id,
                                                 String_View config,
                                                 String_View raw,
                                                 String_View *out) {
    BM_Query_Eval_Context qctx = cg_make_install_eval_ctx(ctx, current_target_id, config);
    return cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, out);
}

static bool cg_resolve_single_install_string_for_config(CG_Context *ctx,
                                                        BM_Target_Id current_target_id,
                                                        String_View config,
                                                        String_View raw,
                                                        String_View *out) {
    String_View resolved = {0};
    String_View *pieces = NULL;
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !out) return false;
    if (!cg_resolve_install_string_for_config(ctx, current_target_id, config, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &pieces)) {
        return false;
    }
    if (arena_arr_len(pieces) > 1) return false;
    *out = arena_arr_len(pieces) == 1 ? pieces[0] : nob_sv_from_cstr("");
    return true;
}

static bool cg_split_top_level_list(Arena *arena, String_View value, String_View **out_items) {
    Genex_Context gx = {0};
    Gx_Sv_List pieces = {0};
    if (out_items) *out_items = NULL;
    if (!arena || !out_items) return false;
    if (value.count == 0) return true;
    gx.arena = arena;
    pieces = gx_split_top_level_alloc(&gx, value, ';');
    if (value.count > 0 && pieces.count == 0) return false;
    for (size_t i = 0; i < pieces.count; ++i) {
        String_View piece = nob_sv_trim(pieces.items[i]);
        char *copy = NULL;
        if (piece.count == 0) continue;
        copy = arena_strndup(arena, piece.data ? piece.data : "", piece.count);
        if (!copy || !arena_arr_push(arena, *out_items, nob_sv_from_parts(copy, piece.count))) return false;
    }
    return true;
}

static bool cg_install_path_is_runtime_prefixed(String_View value, String_View *out_suffix) {
    String_View token = cg_install_prefix_genex_token();
    String_View trimmed = nob_sv_trim(value);
    if (out_suffix) *out_suffix = nob_sv_from_cstr("");
    if (!nob_sv_starts_with(trimmed, token)) return false;
    trimmed = nob_sv_from_parts(trimmed.data + token.count, trimmed.count - token.count);
    if (trimmed.count > 0 && (trimmed.data[0] == '/' || trimmed.data[0] == '\\')) {
        trimmed = nob_sv_from_parts(trimmed.data + 1, trimmed.count - 1);
    }
    if (out_suffix) *out_suffix = trimmed;
    return true;
}

static bool cg_sb_append_runtime_install_path_expr(Nob_String_Builder *sb, String_View resolved_path) {
    String_View suffix = {0};
    if (!sb) return false;
    if (cg_install_path_is_runtime_prefixed(resolved_path, &suffix)) {
        return cg_sb_append_install_join_call(sb, suffix);
    }
    if (cg_path_is_abs(resolved_path)) {
        return cg_sb_append_c_string(sb, resolved_path);
    }
    return cg_sb_append_install_join_call(sb, resolved_path);
}

typedef enum {
    CG_INSTALL_EXPORT_EMIT_MAIN = 0,
    CG_INSTALL_EXPORT_EMIT_NOCONFIG,
} CG_Install_Export_Emit_Mode;

static bool cg_export_has_non_interface_targets(CG_Context *ctx, BM_Export_Id export_id) {
    BM_Target_Id_Span targets = {0};
    if (!ctx) return false;
    targets = bm_query_export_targets(ctx->model, export_id);
    for (size_t i = 0; i < targets.count; ++i) {
        if (bm_query_target_kind(ctx->model, targets.items[i]) != BM_TARGET_INTERFACE_LIBRARY) {
            return true;
        }
    }
    return false;
}

static bool cg_export_collect_direct_property_values(CG_Context *ctx,
                                                     BM_Target_Id target_id,
                                                     BM_Query_Usage_Mode usage_mode,
                                                     String_View property_name,
                                                     bool rewrite_import_prefix,
                                                     String_View **out) {
    BM_Query_Eval_Context qctx = {0};
    String_View raw = {0};
    String_View resolved = {0};
    String_View *pieces = NULL;
    if (!ctx) return false;

    qctx = cg_make_query_ctx(ctx, target_id, usage_mode, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
    qctx.build_interface_active = false;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = true;
    qctx.install_prefix = nob_sv_from_cstr("${_IMPORT_PREFIX}");

    if (!bm_query_target_modeled_property_value(ctx->model, target_id, property_name, ctx->scratch, &raw)) {
        return false;
    }
    if (raw.count == 0) return true;
    if (!cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &pieces)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(pieces); ++i) {
        String_View value = pieces[i];
        if (value.count == 0) continue;
        if (rewrite_import_prefix) {
            if (!cg_install_import_prefix_expr(ctx, value, &value)) return false;
        }
        if (!cg_collect_unique_path(ctx->scratch, out, value)) return false;
    }
    return true;
}

static bool cg_export_collect_direct_link_libraries(CG_Context *ctx,
                                                    BM_Export_Id export_id,
                                                    BM_Target_Id target_id,
                                                    String_View export_namespace,
                                                    String_View **out) {
    BM_Query_Eval_Context qctx = {0};
    BM_Target_Id_Span exported_targets = {0};
    String_View raw = {0};
    String_View resolved = {0};
    String_View *pieces = NULL;
    if (!ctx) return false;

    qctx = cg_make_query_ctx(ctx, target_id, BM_QUERY_USAGE_LINK, nob_sv_from_cstr(""), nob_sv_from_cstr(""));
    qctx.build_interface_active = false;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = true;
    qctx.install_prefix = nob_sv_from_cstr("${_IMPORT_PREFIX}");
    exported_targets = bm_query_export_targets(ctx->model, export_id);

    if (!bm_query_target_modeled_property_value(ctx->model,
                                                target_id,
                                                nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"),
                                                ctx->scratch,
                                                &raw)) {
        return false;
    }
    if (raw.count == 0) return true;
    if (!cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &pieces)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(pieces); ++i) {
        CG_Resolved_Target_Ref dep = {0};
        String_View value = pieces[i];
        if (value.count == 0) continue;
        if (cg_resolve_target_ref(ctx, &qctx, value, &dep)) {
            String_View exported_name = {0};
            if (cg_export_target_in_span(exported_targets, dep.target_id)) {
                if (!cg_target_exported_name(ctx, dep.target_id, export_namespace, &exported_name) ||
                    !cg_collect_unique_path(ctx->scratch, out, exported_name)) {
                    return false;
                }
                continue;
            }
        }
        if (!cg_collect_unique_path(ctx->scratch, out, value)) return false;
    }
    return true;
}

static bool cg_export_collect_interface_includes(CG_Context *ctx,
                                                 BM_Export_Id export_id,
                                                 BM_Install_Rule_Id rule_id,
                                                 BM_Target_Id target_id,
                                                 bool want_system,
                                                 String_View **out) {
    String_View install_dest = bm_query_install_rule_includes_destination(ctx->model, rule_id);
    String_View property_name = want_system
        ? nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES")
        : nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES");
    (void)export_id;

    if (!cg_export_collect_direct_property_values(ctx,
                                                  target_id,
                                                  BM_QUERY_USAGE_COMPILE,
                                                  property_name,
                                                  true,
                                                  out)) {
        return false;
    }
    if (!want_system && install_dest.count > 0) {
        String_View expr = {0};
        if (!cg_install_import_prefix_expr(ctx, install_dest, &expr)) return false;
        if (!arena_arr_push(ctx->scratch, *out, expr)) return false;
    }
    return true;
}

static bool cg_export_collect_effective_values(CG_Context *ctx,
                                               BM_Target_Id target_id,
                                               BM_Query_Usage_Mode usage_mode,
                                               String_View property_name,
                                               bool rewrite_import_prefix,
                                               String_View **out) {
    return cg_export_collect_direct_property_values(ctx,
                                                    target_id,
                                                    usage_mode,
                                                    property_name,
                                                    rewrite_import_prefix,
                                                    out);
}

static bool cg_export_emit_target_properties(CG_Context *ctx,
                                             BM_Export_Id export_id,
                                             BM_Target_Id target_id,
                                             String_View exported_name,
                                             String_View export_namespace,
                                             String_View config,
                                             void *userdata,
                                             Nob_String_Builder *sb) {
    const CG_Target_Info *info = cg_target_info(ctx, target_id);
    BM_Install_Rule_Id rule_id = BM_INSTALL_RULE_ID_INVALID;
    String_View runtime_rel = {0};
    String_View runtime_expr = {0};
    String_View link_languages_joined = {0};
    String_View includes_joined = {0};
    String_View system_includes_joined = {0};
    String_View compile_defs_joined = {0};
    String_View compile_opts_joined = {0};
    String_View compile_features_joined = {0};
    String_View link_opts_joined = {0};
    String_View link_dirs_joined = {0};
    String_View link_libs_joined = {0};
    String_View *include_items = NULL;
    String_View *system_include_items = NULL;
    String_View *compile_defs = NULL;
    String_View *compile_opts = NULL;
    String_View *compile_features = NULL;
    String_View *link_opts = NULL;
    String_View *link_dirs = NULL;
    String_View *link_libs = NULL;
    CG_Install_Export_Emit_Mode mode = userdata
        ? *(const CG_Install_Export_Emit_Mode*)userdata
        : CG_INSTALL_EXPORT_EMIT_MAIN;
    (void)config;
    if (!ctx || !info || !sb) return false;
    rule_id = bm_query_install_rule_for_export_target(ctx->model, export_id, target_id);

    if (!cg_export_collect_interface_includes(ctx, export_id, rule_id, target_id, false, &include_items) ||
        !cg_export_collect_interface_includes(ctx, export_id, rule_id, target_id, true, &system_include_items) ||
        !cg_export_collect_effective_values(ctx,
                                            target_id,
                                            BM_QUERY_USAGE_COMPILE,
                                            nob_sv_from_cstr("INTERFACE_COMPILE_DEFINITIONS"),
                                            false,
                                            &compile_defs) ||
        !cg_export_collect_effective_values(ctx,
                                            target_id,
                                            BM_QUERY_USAGE_COMPILE,
                                            nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS"),
                                            false,
                                            &compile_opts) ||
        !cg_export_collect_effective_values(ctx,
                                            target_id,
                                            BM_QUERY_USAGE_COMPILE,
                                            nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                            false,
                                            &compile_features) ||
        !cg_export_collect_effective_values(ctx,
                                            target_id,
                                            BM_QUERY_USAGE_LINK,
                                            nob_sv_from_cstr("INTERFACE_LINK_OPTIONS"),
                                            false,
                                            &link_opts) ||
        !cg_export_collect_effective_values(ctx,
                                            target_id,
                                            BM_QUERY_USAGE_LINK,
                                            nob_sv_from_cstr("INTERFACE_LINK_DIRECTORIES"),
                                            true,
                                            &link_dirs) ||
        !cg_export_collect_direct_link_libraries(ctx, export_id, target_id, export_namespace, &link_libs) ||
        !cg_collect_target_link_languages(ctx, target_id, &link_languages_joined) ||
        !cg_join_sv_list(ctx->scratch, include_items, &includes_joined) ||
        !cg_join_sv_list(ctx->scratch, system_include_items, &system_includes_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_defs, &compile_defs_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_opts, &compile_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_features, &compile_features_joined) ||
        !cg_join_sv_list(ctx->scratch, link_opts, &link_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, link_dirs, &link_dirs_joined) ||
        !cg_join_sv_list(ctx->scratch, link_libs, &link_libs_joined)) {
        return false;
    }

    if (info->emits_artifact &&
        !cg_target_installed_artifact_relpath(ctx, rule_id, info, false, config, &runtime_rel)) {
        return false;
    }
    if (mode == CG_INSTALL_EXPORT_EMIT_MAIN) {
        bool has_interface_props = includes_joined.count > 0 ||
                                   system_includes_joined.count > 0 ||
                                   compile_defs_joined.count > 0 ||
                                   compile_opts_joined.count > 0 ||
                                   compile_features_joined.count > 0 ||
                                   link_opts_joined.count > 0 ||
                                   link_dirs_joined.count > 0 ||
                                   link_libs_joined.count > 0;
        if (has_interface_props) {
            nob_sb_append_cstr(sb, "set_target_properties(");
            if (!cg_cmake_append_escaped(sb, exported_name)) return false;
            nob_sb_append_cstr(sb, " PROPERTIES\n");
            if (includes_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_INCLUDE_DIRECTORIES \"");
                if (!cg_cmake_append_escaped(sb, includes_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (system_includes_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES \"");
                if (!cg_cmake_append_escaped(sb, system_includes_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (compile_defs_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_DEFINITIONS \"");
                if (!cg_cmake_append_escaped(sb, compile_defs_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (compile_opts_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_OPTIONS \"");
                if (!cg_cmake_append_escaped(sb, compile_opts_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (compile_features_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_FEATURES \"");
                if (!cg_cmake_append_escaped(sb, compile_features_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (link_opts_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_LINK_OPTIONS \"");
                if (!cg_cmake_append_escaped(sb, link_opts_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (link_dirs_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_LINK_DIRECTORIES \"");
                if (!cg_cmake_append_escaped(sb, link_dirs_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            if (link_libs_joined.count > 0) {
                nob_sb_append_cstr(sb, "  INTERFACE_LINK_LIBRARIES \"");
                if (!cg_cmake_append_escaped(sb, link_libs_joined)) return false;
                nob_sb_append_cstr(sb, "\"\n");
            }
            nob_sb_append_cstr(sb, ")\n\n");
        }
        if (info->kind == BM_TARGET_INTERFACE_LIBRARY) {
            nob_sb_append_cstr(sb,
                "if(CMAKE_VERSION VERSION_LESS 3.0.0)\n"
                "  message(FATAL_ERROR \"This file relies on consumers using CMake 3.0.0 or greater.\")\n"
                "endif()\n\n");
        }
        return true;
    }

    if (info->kind == BM_TARGET_INTERFACE_LIBRARY) return true;

    nob_sb_append_cstr(sb, "# Import target \"");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, "\" for configuration \"\"\n");
    nob_sb_append_cstr(sb, "set_property(TARGET ");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)\n");
    nob_sb_append_cstr(sb, "set_target_properties(");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " PROPERTIES\n");
    if (info->kind == BM_TARGET_STATIC_LIBRARY && link_languages_joined.count > 0) {
        nob_sb_append_cstr(sb, "  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG \"");
        if (!cg_cmake_append_escaped(sb, link_languages_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (info->kind == BM_TARGET_MODULE_LIBRARY) {
        nob_sb_append_cstr(sb, "  IMPORTED_COMMON_LANGUAGE_RUNTIME_NOCONFIG \"\"\n");
    }
    if (runtime_rel.count > 0) {
        if (!cg_install_import_prefix_expr(ctx, runtime_rel, &runtime_expr)) return false;
        nob_sb_append_cstr(sb, "  IMPORTED_LOCATION_NOCONFIG \"");
        if (!cg_cmake_append_escaped(sb, runtime_expr)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (info->kind == BM_TARGET_SHARED_LIBRARY && runtime_rel.count > 0 && !cg_policy_is_windows(ctx)) {
        String_View soname = cg_basename_to_arena(ctx->scratch, runtime_rel);
        nob_sb_append_cstr(sb, "  IMPORTED_SONAME_NOCONFIG \"");
        if (!cg_cmake_append_escaped(sb, soname)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (info->kind == BM_TARGET_MODULE_LIBRARY && !cg_policy_is_windows(ctx)) {
        nob_sb_append_cstr(sb, "  IMPORTED_NO_SONAME_NOCONFIG \"TRUE\"\n");
    }
    nob_sb_append_cstr(sb, "  )\n\n");
    nob_sb_append_cstr(sb, "list(APPEND _cmake_import_check_targets ");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " )\n");
    nob_sb_append_cstr(sb, "list(APPEND _cmake_import_check_files_for_");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " \"");
    if (!cg_cmake_append_escaped(sb, runtime_expr)) return false;
    nob_sb_append_cstr(sb, "\" )\n\n");
    return true;
}

static bool cg_build_export_noconfig_file_contents(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   String_View *out) {
    if (!ctx || !out) return false;
    CG_Install_Export_Emit_Mode mode = CG_INSTALL_EXPORT_EMIT_NOCONFIG;
    return cg_build_cmake_targets_noconfig_file_contents(ctx,
                                                         export_id,
                                                         nob_sv_from_cstr(""),
                                                         cg_export_emit_target_properties,
                                                         &mode,
                                                         out);
}

static bool cg_build_export_file_contents(CG_Context *ctx,
                                          BM_Export_Id export_id,
                                          String_View config,
                                          String_View *out) {
    if (!ctx || !out) return false;

    CG_Install_Export_Emit_Mode mode = CG_INSTALL_EXPORT_EMIT_MAIN;
    return cg_build_cmake_targets_file_contents(ctx,
                                                export_id,
                                                config,
                                                true,
                                                true,
                                                cg_export_emit_target_properties,
                                                &mode,
                                                out);
}

bool cg_emit_install_function(CG_Context *ctx, Nob_String_Builder *out) {
    size_t rule_count = 0;
    size_t install_export_count = 0;
    String_View install_cwd_rebased = {0};
    if (!ctx || !out) return false;

    rule_count = bm_query_install_rule_count(ctx->model);
    for (size_t export_index = 0; export_index < bm_query_export_count(ctx->model); ++export_index) {
        if (bm_query_export_kind(ctx->model, (BM_Export_Id)export_index) == BM_EXPORT_INSTALL) {
            install_export_count++;
        }
    }
    if (!cg_relative_path_to_arena(ctx->scratch, ctx->emit_dir_abs, ctx->cwd_abs, &install_cwd_rebased)) {
        return false;
    }
    if (rule_count > 0 || install_export_count > 0) {
        nob_sb_append_cstr(out,
            "static bool install_prefix_is_abs(const char *path) {\n"
            "    return path && path[0] == '/';\n"
            "}\n\n"
            "static const char *resolve_install_prefix(const char *install_prefix) {\n"
            "    if (!install_prefix || install_prefix[0] == '\\0') install_prefix = \"install\";\n"
            "    if (install_prefix_is_abs(install_prefix)) return install_prefix;\n"
            "    if (strcmp(");
        if (!cg_sb_append_c_string(out, install_cwd_rebased)) return false;
        nob_sb_append_cstr(out,
            ", \".\") == 0) return install_prefix;\n"
            "    return nob_temp_sprintf(\"%s/%s\", ");
        if (!cg_sb_append_c_string(out, install_cwd_rebased)) return false;
        nob_sb_append_cstr(out,
            ", install_prefix);\n"
            "}\n\n"
            "static const char *join_install_prefix(const char *install_prefix, const char *relative_path) {\n"
            "    if (!relative_path || relative_path[0] == '\\0') return install_prefix;\n"
            "    return nob_temp_sprintf(\"%s/%s\", install_prefix, relative_path);\n"
            "}\n\n"
            "static bool install_component_matches(const char *requested_component, const char *rule_component) {\n"
            "    if (!requested_component || requested_component[0] == '\\0') return true;\n"
            "    if (!rule_component || rule_component[0] == '\\0') rule_component = \"Unspecified\";\n"
            "    return strcmp(requested_component, rule_component) == 0;\n"
            "}\n\n");
    }
    nob_sb_append_cstr(out, "static bool install_all(const char *install_prefix, const char *install_component) {\n");
    if (rule_count == 0 && install_export_count == 0) {
        nob_sb_append_cstr(out, "    (void)install_prefix;\n");
        nob_sb_append_cstr(out, "    (void)install_component;\n");
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    nob_sb_append_cstr(out,
        "    install_prefix = resolve_install_prefix(install_prefix);\n"
        "    if (!ensure_dir(install_prefix)) return false;\n");
    for (size_t i = 0; i < rule_count; ++i) {
        BM_Install_Rule_Id id = (BM_Install_Rule_Id)i;
        BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, id);
        String_View destination = bm_query_install_rule_destination(ctx->model, id);

        if (kind == BM_INSTALL_RULE_TARGET) {
            BM_Target_Id target_id = bm_query_install_rule_target(ctx->model, id);
            const CG_Target_Info *info = cg_target_info(ctx, target_id);
            if (!info) {
                nob_log(NOB_ERROR, "codegen: install rule references an unknown target");
                return false;
            }
            if (info->kind != BM_TARGET_INTERFACE_LIBRARY && info->emits_artifact) {
                String_View runtime_component = {0};
                runtime_component = cg_install_rule_target_component(ctx, id, info->kind, false);
                if (!cg_emit_install_component_guard_open(out, runtime_component)) {
                    return false;
                }
                nob_sb_append_cstr(out, "    if (!build_");
                nob_sb_append_cstr(out, info->ident);
                nob_sb_append_cstr(out, "()) return false;\n");
                for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                    String_View config = branch < arena_arr_len(ctx->known_configs)
                        ? ctx->known_configs[branch]
                        : nob_sv_from_cstr("");
                    String_View runtime_rel = {0};
                    BM_Target_Artifact_View artifact = {0};
                    if (!cg_target_installed_artifact_relpath(ctx, id, info, false, config, &runtime_rel) ||
                        !cg_target_artifact_for_config_or_empty(info, BM_TARGET_ARTIFACT_RUNTIME, config, &artifact) ||
                        !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        const char *install_path = ");
                    if (!cg_sb_append_runtime_install_path_expr(out, runtime_rel)) return false;
                    nob_sb_append_cstr(out, ";\n");
                    nob_sb_append_cstr(out, "        if (!ensure_parent_dir(install_path)) return false;\n");
                    nob_sb_append_cstr(out, "        if (!install_copy_file(");
                    if (!cg_sb_append_c_string(out, artifact.path)) return false;
                    nob_sb_append_cstr(out, ", install_path)) return false;\n");
                }
                if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
                nob_sb_append_cstr(out, "    }\n");
            }

            if (bm_query_install_rule_public_header_destination(ctx->model, id).count > 0) {
                BM_String_Span headers = bm_query_target_raw_property_items(ctx->model, target_id, nob_sv_from_cstr("PUBLIC_HEADER"));
                BM_Directory_Id owner_dir = bm_query_target_owner_directory(ctx->model, target_id);
                String_View header_component =
                    cg_install_effective_component(bm_query_install_rule_public_header_component(ctx->model, id),
                                                   bm_query_install_rule_component(ctx->model, id));
                if (!cg_emit_install_component_guard_open(out, header_component)) {
                    return false;
                }
                for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                    String_View config = branch < arena_arr_len(ctx->known_configs)
                        ? ctx->known_configs[branch]
                        : nob_sv_from_cstr("");
                    String_View resolved_destination = {0};
                    if (!cg_resolve_single_install_string_for_config(ctx,
                                                                     target_id,
                                                                     config,
                                                                     bm_query_install_rule_public_header_destination(ctx->model, id),
                                                                     &resolved_destination) ||
                        !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                        return false;
                    }
                    for (size_t header_index = 0; header_index < headers.count; ++header_index) {
                        String_View src_path = {0};
                        String_View basename = {0};
                        String_View dest_rel = {0};
                        if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, headers.items[header_index], &src_path)) {
                            return false;
                        }
                        basename = cg_basename_to_arena(ctx->scratch, src_path);
                        if (!cg_join_paths_to_arena(ctx->scratch, resolved_destination, basename, &dest_rel)) {
                            return false;
                        }
                        nob_sb_append_cstr(out, "        const char *dest_path = ");
                        if (!cg_sb_append_runtime_install_path_expr(out, dest_rel)) return false;
                        nob_sb_append_cstr(out, ";\n");
                        nob_sb_append_cstr(out, "        if (!install_copy_file(");
                        if (!cg_sb_append_c_string(out, src_path)) return false;
                        nob_sb_append_cstr(out, ", dest_path)) return false;\n");
                    }
                }
                if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
                nob_sb_append_cstr(out, "    }\n");
            }
            continue;
        }

        if (!cg_emit_install_component_guard_open(out, bm_query_install_rule_component(ctx->model, id))) {
            return false;
        }
        if (kind == BM_INSTALL_RULE_FILE || kind == BM_INSTALL_RULE_PROGRAM) {
            BM_Directory_Id owner_dir = bm_query_install_rule_owner_directory(ctx->model, id);
            String_View item = bm_query_install_rule_item_raw(ctx->model, id);
            String_View rename = bm_query_install_rule_rename(ctx->model, id);
            String_View trimmed_item = nob_sv_trim(item);

            if (cg_sv_has_prefix(item, "SCRIPT::") ||
                cg_sv_has_prefix(item, "CODE::") ||
                cg_sv_has_prefix(item, "EXPORT_ANDROID_MK::")) {
                nob_log(NOB_ERROR,
                        "codegen: unsupported install(FILES) pseudo-item: %.*s",
                        (int)item.count,
                        item.data ? item.data : "");
                return false;
            }
            for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                String_View config = branch < arena_arr_len(ctx->known_configs)
                    ? ctx->known_configs[branch]
                    : nob_sv_from_cstr("");
                String_View resolved_items_joined = {0};
                String_View resolved_destination = {0};
                String_View resolved_rename = {0};
                String_View *resolved_items = NULL;
                if (!cg_resolve_install_string_for_config(ctx, BM_TARGET_ID_INVALID, config, item, &resolved_items_joined) ||
                    !cg_resolve_single_install_string_for_config(ctx,
                                                                 BM_TARGET_ID_INVALID,
                                                                 config,
                                                                 destination,
                                                                 &resolved_destination) ||
                    !cg_resolve_single_install_string_for_config(ctx,
                                                                 BM_TARGET_ID_INVALID,
                                                                 config,
                                                                 rename,
                                                                 &resolved_rename) ||
                    !cg_split_top_level_list(ctx->scratch, resolved_items_joined, &resolved_items) ||
                    !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                    return false;
                }
                if (resolved_rename.count > 0 && arena_arr_len(resolved_items) > 1) {
                    nob_log(NOB_ERROR, "codegen: install(RENAME) cannot target multiple resolved items");
                    return false;
                }
                for (size_t item_index = 0; item_index < arena_arr_len(resolved_items); ++item_index) {
                    String_View resolved_item = resolved_items[item_index];
                    String_View src_path = {0};
                    String_View basename = {0};
                    String_View install_rel = {0};
                    if (trimmed_item.count >= 2 &&
                        trimmed_item.data[0] == '$' &&
                        trimmed_item.data[1] == '<' &&
                        !cg_path_is_abs(resolved_item)) {
                        nob_log(NOB_ERROR,
                                "codegen: install(FILES/PROGRAMS) item starting with genex must resolve to a full path: %.*s",
                                (int)resolved_item.count,
                                resolved_item.data ? resolved_item.data : "");
                        return false;
                    }
                    if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, resolved_item, &src_path)) return false;
                    basename = resolved_rename.count > 0 ? resolved_rename : cg_basename_to_arena(ctx->scratch, src_path);
                    if (resolved_destination.count > 0) {
                        if (!cg_join_paths_to_arena(ctx->scratch, resolved_destination, basename, &install_rel)) return false;
                    } else {
                        install_rel = basename;
                    }

                    nob_sb_append_cstr(out, "        const char *install_path = ");
                    if (!cg_sb_append_runtime_install_path_expr(out, install_rel)) return false;
                    nob_sb_append_cstr(out, ";\n");
                    nob_sb_append_cstr(out, "        if (!install_copy_file(");
                    if (!cg_sb_append_c_string(out, src_path)) return false;
                    nob_sb_append_cstr(out, ", install_path)) return false;\n");
                }
            }
            if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        if (kind == BM_INSTALL_RULE_DIRECTORY) {
            BM_Directory_Id owner_dir = bm_query_install_rule_owner_directory(ctx->model, id);
            String_View item = bm_query_install_rule_item_raw(ctx->model, id);
            for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                String_View config = branch < arena_arr_len(ctx->known_configs)
                    ? ctx->known_configs[branch]
                    : nob_sv_from_cstr("");
                String_View resolved_items_joined = {0};
                String_View resolved_destination = {0};
                String_View *resolved_items = NULL;
                if (!cg_resolve_install_string_for_config(ctx, BM_TARGET_ID_INVALID, config, item, &resolved_items_joined) ||
                    !cg_resolve_single_install_string_for_config(ctx,
                                                                 BM_TARGET_ID_INVALID,
                                                                 config,
                                                                 destination,
                                                                 &resolved_destination) ||
                    !cg_split_top_level_list(ctx->scratch, resolved_items_joined, &resolved_items) ||
                    !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                    return false;
                }
                for (size_t item_index = 0; item_index < arena_arr_len(resolved_items); ++item_index) {
                    String_View resolved_item = resolved_items[item_index];
                    String_View src_path = {0};
                    String_View install_rel = resolved_destination;
                    bool copy_contents = resolved_item.count > 0 &&
                                         (resolved_item.data[resolved_item.count - 1] == '/' ||
                                          resolved_item.data[resolved_item.count - 1] == '\\');
                    if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, resolved_item, &src_path)) return false;
                    if (!copy_contents) {
                        String_View basename = cg_basename_to_arena(ctx->scratch, src_path);
                        if (!cg_join_paths_to_arena(ctx->scratch, resolved_destination, basename, &install_rel)) return false;
                    }
                    nob_sb_append_cstr(out, "        const char *install_path = ");
                    if (!cg_sb_append_runtime_install_path_expr(out, install_rel)) return false;
                    nob_sb_append_cstr(out, ";\n");
                    nob_sb_append_cstr(out, "        if (!ensure_parent_dir(install_path)) return false;\n");
                    nob_sb_append_cstr(out, "        if (!install_copy_directory(");
                    if (!cg_sb_append_c_string(out, src_path)) return false;
                    nob_sb_append_cstr(out, ", install_path)) return false;\n");
                }
            }
            if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        nob_log(NOB_ERROR,
                "codegen: unsupported install rule kind in install backend: %d",
                (int)kind);
        return false;
    }

    for (size_t export_index = 0; export_index < bm_query_export_count(ctx->model); ++export_index) {
        BM_Export_Id export_id = (BM_Export_Id)export_index;
        bool use_noconfig = cg_export_has_non_interface_targets(ctx, export_id);
        if (bm_query_export_kind(ctx->model, export_id) != BM_EXPORT_INSTALL) continue;
        if (!cg_emit_install_component_guard_open(out, bm_query_export_component(ctx->model, export_id))) {
            return false;
        }
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs)
                ? ctx->known_configs[branch]
                : nob_sv_from_cstr("");
            String_View export_text = {0};
            String_View output_rel = {0};
            if (!cg_build_export_file_contents(ctx, export_id, config, &export_text) ||
                !cg_resolve_install_string_for_config(ctx,
                                                      BM_TARGET_ID_INVALID,
                                                      config,
                                                      bm_query_export_output_file_path(ctx->model, export_id, ctx->scratch),
                                                      &output_rel) ||
                !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                return false;
            }
            nob_sb_append_cstr(out, "        const char *output_path = ");
            if (!cg_sb_append_runtime_install_path_expr(out, output_rel)) return false;
            nob_sb_append_cstr(out, ";\n");
            nob_sb_append_cstr(out, "        if (!ensure_parent_dir(output_path)) return false;\n");
            nob_sb_append_cstr(out, "        if (!nob_write_entire_file(output_path, ");
            if (!cg_sb_append_c_string(out, export_text)) return false;
            nob_sb_append_cstr(out, ", strlen(");
            if (!cg_sb_append_c_string(out, export_text)) return false;
            nob_sb_append_cstr(out, "))) return false;\n");
        }
        if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;

        if (use_noconfig) {
            String_View export_noconfig_text = {0};
            String_View raw_noconfig_output_rel = {0};
            String_View noconfig_output_rel = {0};
            if (!cg_build_export_noconfig_file_contents(ctx, export_id, &export_noconfig_text) ||
                !cg_export_noconfig_output_file_path(ctx, export_id, &raw_noconfig_output_rel) ||
                !cg_resolve_install_string_for_config(ctx,
                                                      BM_TARGET_ID_INVALID,
                                                      nob_sv_from_cstr(""),
                                                      raw_noconfig_output_rel,
                                                      &noconfig_output_rel)) {
                return false;
            }
            nob_sb_append_cstr(out, "        const char *noconfig_output_path = ");
            if (!cg_sb_append_runtime_install_path_expr(out, noconfig_output_rel)) return false;
            nob_sb_append_cstr(out, ";\n");
            nob_sb_append_cstr(out, "        if (!ensure_parent_dir(noconfig_output_path)) return false;\n");
            nob_sb_append_cstr(out, "        if (!nob_write_entire_file(noconfig_output_path, ");
            if (!cg_sb_append_c_string(out, export_noconfig_text)) return false;
            nob_sb_append_cstr(out, ", strlen(");
            if (!cg_sb_append_c_string(out, export_noconfig_text)) return false;
            nob_sb_append_cstr(out, "))) return false;\n");
        }
        nob_sb_append_cstr(out, "    }\n");
    }

    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}
