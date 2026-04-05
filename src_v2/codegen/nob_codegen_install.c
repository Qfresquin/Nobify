#include "nob_codegen_internal.h"

static bool cg_sb_append_install_join_call(Nob_String_Builder *sb, String_View relative_path) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, "join_install_prefix(install_prefix, ");
    if (!cg_sb_append_c_string(sb, relative_path)) return false;
    nob_sb_append_cstr(sb, ")");
    return true;
}

static bool cg_emit_install_component_guard_open(Nob_String_Builder *sb, String_View component) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, "    if (install_component_matches(install_component, ");
    if (!cg_sb_append_c_string(sb, component)) return false;
    nob_sb_append_cstr(sb, ")) {\n");
    return true;
}

static String_View cg_install_target_destination(String_View specific, String_View generic) {
    return specific.count > 0 ? specific : generic;
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

static bool cg_target_export_name(CG_Context *ctx, BM_Target_Id id, String_View *out) {
    String_View export_name = {0};
    if (!ctx || !out) return false;
    *out = bm_query_target_name(ctx->model, id);
    if (!bm_query_target_property_value(ctx->model, id, nob_sv_from_cstr("EXPORT_NAME"), ctx->scratch, &export_name)) {
        return false;
    }
    if (export_name.count > 0) *out = export_name;
    return true;
}

static bool cg_target_exported_name(CG_Context *ctx,
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
            return true;
    }

    return true;
}

static bool cg_target_installed_artifact_relpath(CG_Context *ctx,
                                                 BM_Install_Rule_Id rule_id,
                                                 const CG_Target_Info *info,
                                                 bool linker_artifact,
                                                 String_View *out) {
    String_View destination = {0};
    String_View basename = {0};
    String_View source_path = {0};
    if (!ctx || !info || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_install_rule_target_destination_for_kind(ctx, rule_id, info->kind, linker_artifact, &destination)) {
        return false;
    }
    source_path = linker_artifact ? info->linker_artifact_path : info->artifact_path;
    if (source_path.count == 0) return true;
    basename = cg_basename_to_arena(ctx->scratch, source_path);
    if (destination.count == 0) {
        *out = basename;
        return true;
    }
    return cg_join_paths_to_arena(ctx->scratch, destination, basename, out);
}

static bool cg_cmake_append_escaped(Nob_String_Builder *sb, String_View value) {
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

static bool cg_relative_install_expr(CG_Context *ctx,
                                     String_View export_dir_rel,
                                     String_View target_rel,
                                     String_View *out) {
    String_View fake_root = nob_sv_from_cstr("/__nob_install_prefix__");
    String_View export_dir_abs = {0};
    String_View target_abs = {0};
    String_View relative = {0};
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (target_rel.count == 0) return true;

    if (!cg_join_paths_to_arena(ctx->scratch, fake_root, export_dir_rel.count > 0 ? export_dir_rel : nob_sv_from_cstr("."), &export_dir_abs) ||
        !cg_join_paths_to_arena(ctx->scratch, fake_root, target_rel, &target_abs) ||
        !cg_relative_path_to_arena(ctx->scratch, export_dir_abs, target_abs, &relative)) {
        return false;
    }

    if (cg_sv_eq_lit(relative, ".")) {
        copy = arena_strdup(ctx->scratch, "${CMAKE_CURRENT_LIST_DIR}");
        if (!copy) return false;
        *out = nob_sv_from_cstr(copy);
        return true;
    }

    nob_sb_append_cstr(&sb, "${CMAKE_CURRENT_LIST_DIR}/");
    nob_sb_append_buf(&sb, relative.data ? relative.data : "", relative.count);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

static bool cg_join_sv_list(Arena *scratch, String_View *items, String_View *out) {
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

static bool cg_export_noconfig_file_name(CG_Context *ctx,
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

static bool cg_export_noconfig_output_file_path(CG_Context *ctx,
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

static bool cg_export_target_in_span(BM_Target_Id_Span span, BM_Target_Id id) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i] == id) return true;
    }
    return false;
}

static bool cg_export_collect_interface_includes(CG_Context *ctx,
                                                 BM_Export_Id export_id,
                                                 BM_Install_Rule_Id rule_id,
                                                 BM_Target_Id target_id,
                                                 String_View **out) {
    BM_String_Span includes = {0};
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   BM_QUERY_USAGE_COMPILE,
                                                   nob_sv_from_cstr(""),
                                                   nob_sv_from_cstr(""));
    String_View install_dest = bm_query_install_rule_includes_destination(ctx->model, rule_id);
    String_View export_dir = bm_query_export_destination(ctx->model, export_id);
    qctx.build_interface_active = false;
    qctx.install_interface_active = true;
    if (!bm_query_target_effective_include_directories_with_context(ctx->model, target_id, &qctx, ctx->scratch, &includes)) {
        return false;
    }
    for (size_t i = 0; i < includes.count; ++i) {
        String_View item = includes.items[i];
        String_View expr = {0};
        if (item.count == 0) continue;
        if (!cg_path_is_abs(item)) {
            if (!cg_relative_install_expr(ctx, export_dir, item, &expr)) return false;
            item = expr;
        }
        if (!cg_collect_unique_path(ctx->scratch, out, item)) return false;
    }
    if (install_dest.count > 0) {
        String_View expr = {0};
        if (!cg_relative_install_expr(ctx, export_dir, install_dest, &expr) ||
            !cg_collect_unique_path(ctx->scratch, out, expr)) {
            return false;
        }
    }
    return true;
}

static bool cg_export_collect_effective_values(CG_Context *ctx,
                                               BM_Target_Id target_id,
                                               BM_Query_Usage_Mode usage_mode,
                                               CG_Effective_Query_Family family,
                                               String_View **out) {
    BM_String_Span values = {0};
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   usage_mode,
                                                   nob_sv_from_cstr(""),
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = false;
    qctx.install_interface_active = true;
    if (!cg_query_effective_values_cached(ctx, target_id, &qctx, family, &values)) return false;
    for (size_t i = 0; i < values.count; ++i) {
        if (values.items[i].count == 0) continue;
        if (!cg_collect_unique_path(ctx->scratch, out, values.items[i])) return false;
    }
    return true;
}

static bool cg_export_collect_link_libraries(CG_Context *ctx,
                                             BM_Export_Id export_id,
                                             BM_Target_Id target_id,
                                             String_View export_namespace,
                                             String_View **out) {
    BM_String_Item_Span libs = {0};
    BM_Target_Id_Span exported_targets = bm_query_export_targets(ctx->model, export_id);
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   BM_QUERY_USAGE_LINK,
                                                   nob_sv_from_cstr(""),
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = false;
    qctx.install_interface_active = true;
    if (!cg_query_effective_items_cached(ctx, target_id, &qctx, CG_EFFECTIVE_LINK_LIBRARIES, &libs)) return false;

    for (size_t i = 0; i < libs.count; ++i) {
        CG_Resolved_Target_Ref dep = {0};
        String_View value = libs.items[i].value;
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

static bool cg_export_emit_target_properties(CG_Context *ctx,
                                             BM_Export_Id export_id,
                                             BM_Target_Id target_id,
                                             BM_Install_Rule_Id rule_id,
                                             String_View exported_name,
                                             String_View export_namespace,
                                             Nob_String_Builder *sb) {
    const CG_Target_Info *info = cg_target_info(ctx, target_id);
    String_View runtime_rel = {0};
    String_View runtime_expr = {0};
    String_View includes_joined = {0};
    String_View compile_defs_joined = {0};
    String_View compile_opts_joined = {0};
    String_View compile_features_joined = {0};
    String_View link_opts_joined = {0};
    String_View link_dirs_joined = {0};
    String_View link_libs_joined = {0};
    String_View *include_items = NULL;
    String_View *compile_defs = NULL;
    String_View *compile_opts = NULL;
    String_View *compile_features = NULL;
    String_View *link_opts = NULL;
    String_View *link_dirs = NULL;
    String_View *link_libs = NULL;
    if (!ctx || !info || !sb) return false;

    if (!cg_export_collect_interface_includes(ctx, export_id, rule_id, target_id, &include_items) ||
        !cg_export_collect_effective_values(ctx, target_id, BM_QUERY_USAGE_COMPILE, CG_EFFECTIVE_COMPILE_DEFINITIONS, &compile_defs) ||
        !cg_export_collect_effective_values(ctx, target_id, BM_QUERY_USAGE_COMPILE, CG_EFFECTIVE_COMPILE_OPTIONS, &compile_opts) ||
        !cg_export_collect_effective_values(ctx, target_id, BM_QUERY_USAGE_COMPILE, CG_EFFECTIVE_COMPILE_FEATURES, &compile_features) ||
        !cg_export_collect_effective_values(ctx, target_id, BM_QUERY_USAGE_LINK, CG_EFFECTIVE_LINK_OPTIONS, &link_opts) ||
        !cg_export_collect_effective_values(ctx, target_id, BM_QUERY_USAGE_LINK, CG_EFFECTIVE_LINK_DIRECTORIES, &link_dirs) ||
        !cg_export_collect_link_libraries(ctx, export_id, target_id, export_namespace, &link_libs) ||
        !cg_join_sv_list(ctx->scratch, include_items, &includes_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_defs, &compile_defs_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_opts, &compile_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_features, &compile_features_joined) ||
        !cg_join_sv_list(ctx->scratch, link_opts, &link_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, link_dirs, &link_dirs_joined) ||
        !cg_join_sv_list(ctx->scratch, link_libs, &link_libs_joined)) {
        return false;
    }

    nob_sb_append_cstr(sb, "set_target_properties(");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " PROPERTIES\n");

    if (info->emits_artifact &&
        !cg_target_installed_artifact_relpath(ctx, rule_id, info, false, &runtime_rel)) {
        return false;
    }
    if (runtime_rel.count > 0) {
        if (!cg_relative_install_expr(ctx,
                                      bm_query_export_destination(ctx->model, export_id),
                                      runtime_rel,
                                      &runtime_expr)) {
            return false;
        }
        nob_sb_append_cstr(sb, "  IMPORTED_LOCATION \"");
        if (!cg_cmake_append_escaped(sb, runtime_expr)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (includes_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_INCLUDE_DIRECTORIES \"");
        if (!cg_cmake_append_escaped(sb, includes_joined)) return false;
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
    return true;
}

static bool cg_build_export_noconfig_file_contents(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    nob_sb_append_cstr(&sb, "# Generated by Nobify\n\n");
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        String_View exported_name = {0};
        BM_Install_Rule_Id matching_rule = BM_INSTALL_RULE_ID_INVALID;
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name)) {
            nob_sb_free(sb);
            return false;
        }
        for (size_t rule_index = 0; rule_index < bm_query_install_rule_count(ctx->model); ++rule_index) {
            BM_Install_Rule_Id rule_id = (BM_Install_Rule_Id)rule_index;
            if (bm_query_install_rule_kind(ctx->model, rule_id) != BM_INSTALL_RULE_TARGET) continue;
            if (bm_query_install_rule_target(ctx->model, rule_id) != target_id) continue;
            if (!nob_sv_eq(bm_query_install_rule_export_name(ctx->model, rule_id), bm_query_export_name(ctx->model, export_id))) continue;
            matching_rule = rule_id;
            break;
        }
        if (!cg_export_emit_target_properties(ctx,
                                              export_id,
                                              target_id,
                                              matching_rule,
                                              exported_name,
                                              export_namespace,
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

static bool cg_build_export_file_contents(CG_Context *ctx,
                                          BM_Export_Id export_id,
                                          String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    bool use_noconfig = false;
    String_View noconfig_name = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    use_noconfig = cg_export_has_non_interface_targets(ctx, export_id);
    if (use_noconfig && !cg_export_noconfig_file_name(ctx, export_id, &noconfig_name)) return false;

    nob_sb_append_cstr(&sb, "# Generated by Nobify\n\n");
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        BM_Target_Kind kind = bm_query_target_kind(ctx->model, target_id);
        String_View exported_name = {0};
        const char *cmake_kind = "UNKNOWN";
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name)) {
            nob_sb_free(sb);
            return false;
        }

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
                nob_sb_free(sb);
                return false;
        }

        nob_sb_append_cstr(&sb, "if(NOT TARGET ");
        if (!cg_cmake_append_escaped(&sb, exported_name)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, ")\n  ");
        if (kind == BM_TARGET_EXECUTABLE) {
            nob_sb_append_cstr(&sb, "add_executable(");
        } else {
            nob_sb_append_cstr(&sb, "add_library(");
        }
        if (!cg_cmake_append_escaped(&sb, exported_name)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, " ");
        nob_sb_append_cstr(&sb, cmake_kind);
        nob_sb_append_cstr(&sb, " IMPORTED)\nendif()\n");
        if (!use_noconfig) {
            BM_Install_Rule_Id matching_rule = BM_INSTALL_RULE_ID_INVALID;
            for (size_t rule_index = 0; rule_index < bm_query_install_rule_count(ctx->model); ++rule_index) {
                BM_Install_Rule_Id rule_id = (BM_Install_Rule_Id)rule_index;
                if (bm_query_install_rule_kind(ctx->model, rule_id) != BM_INSTALL_RULE_TARGET) continue;
                if (bm_query_install_rule_target(ctx->model, rule_id) != target_id) continue;
                if (!nob_sv_eq(bm_query_install_rule_export_name(ctx->model, rule_id),
                               bm_query_export_name(ctx->model, export_id))) {
                    continue;
                }
                matching_rule = rule_id;
                break;
            }
            if (!cg_export_emit_target_properties(ctx,
                                                  export_id,
                                                  target_id,
                                                  matching_rule,
                                                  exported_name,
                                                  export_namespace,
                                                  &sb)) {
                nob_sb_free(sb);
                return false;
            }
        }
    }

    if (use_noconfig) {
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

bool cg_emit_install_function(CG_Context *ctx, Nob_String_Builder *out) {
    size_t rule_count = 0;
    size_t export_count = 0;
    if (!ctx || !out) return false;

    rule_count = bm_query_install_rule_count(ctx->model);
    export_count = bm_query_export_count(ctx->model);
    if (rule_count > 0 || export_count > 0) {
        nob_sb_append_cstr(out,
            "static const char *join_install_prefix(const char *install_prefix, const char *relative_path) {\n"
            "    if (!install_prefix || install_prefix[0] == '\\0') install_prefix = \"install\";\n"
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
    if (rule_count == 0 && export_count == 0) {
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    nob_sb_append_cstr(out,
        "    if (!install_prefix || install_prefix[0] == '\\0') install_prefix = \"install\";\n"
        "    if (!ensure_dir(install_prefix)) return false;\n");
    for (size_t i = 0; i < rule_count; ++i) {
        BM_Install_Rule_Id id = (BM_Install_Rule_Id)i;
        BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, id);
        String_View destination = bm_query_install_rule_destination(ctx->model, id);
        if (!cg_emit_install_component_guard_open(out, bm_query_install_rule_component(ctx->model, id))) {
            return false;
        }

        if (kind == BM_INSTALL_RULE_TARGET) {
            BM_Target_Id target_id = bm_query_install_rule_target(ctx->model, id);
            const CG_Target_Info *info = cg_target_info(ctx, target_id);
            if (!info) {
                nob_log(NOB_ERROR, "codegen: install rule references an unknown target");
                return false;
            }
            if (info->kind != BM_TARGET_INTERFACE_LIBRARY && info->emits_artifact) {
                String_View runtime_rel = {0};
                if (!cg_target_installed_artifact_relpath(ctx, id, info, false, &runtime_rel)) {
                    return false;
                }
                nob_sb_append_cstr(out, "    if (!build_");
                nob_sb_append_cstr(out, info->ident);
                nob_sb_append_cstr(out, "()) return false;\n");
                nob_sb_append_cstr(out, "        const char *install_path = ");
                if (!cg_sb_append_install_join_call(out, runtime_rel)) return false;
                nob_sb_append_cstr(out, ";\n");
                nob_sb_append_cstr(out, "        if (!ensure_parent_dir(install_path)) return false;\n");
                nob_sb_append_cstr(out, "        if (!install_copy_file(");
                if (!cg_sb_append_c_string(out, info->artifact_path)) return false;
                nob_sb_append_cstr(out, ", install_path)) return false;\n");
            }

            if (bm_query_install_rule_public_header_destination(ctx->model, id).count > 0) {
                BM_String_Span headers = bm_query_target_raw_property_items(ctx->model, target_id, nob_sv_from_cstr("PUBLIC_HEADER"));
                BM_Directory_Id owner_dir = bm_query_target_owner_directory(ctx->model, target_id);
                for (size_t header_index = 0; header_index < headers.count; ++header_index) {
                    String_View src_path = {0};
                    String_View basename = {0};
                    String_View dest_rel = {0};
                    if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, headers.items[header_index], &src_path)) {
                        return false;
                    }
                    basename = cg_basename_to_arena(ctx->scratch, src_path);
                    if (!cg_join_paths_to_arena(ctx->scratch,
                                                bm_query_install_rule_public_header_destination(ctx->model, id),
                                                basename,
                                                &dest_rel)) {
                        return false;
                    }
                    nob_sb_append_cstr(out, "        const char *dest_path = ");
                    if (!cg_sb_append_install_join_call(out, dest_rel)) return false;
                    nob_sb_append_cstr(out, ";\n");
                    nob_sb_append_cstr(out, "        if (!install_copy_file(");
                    if (!cg_sb_append_c_string(out, src_path)) return false;
                    nob_sb_append_cstr(out, ", dest_path)) return false;\n");
                }
            }
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        if (kind == BM_INSTALL_RULE_FILE || kind == BM_INSTALL_RULE_PROGRAM) {
            BM_Directory_Id owner_dir = bm_query_install_rule_owner_directory(ctx->model, id);
            String_View item = bm_query_install_rule_item_raw(ctx->model, id);
            String_View src_path = {0};
            String_View basename = {0};
            String_View install_rel = {0};

            if (!cg_check_no_genex("install(FILES)", item)) return false;
            if (cg_sv_has_prefix(item, "SCRIPT::") ||
                cg_sv_has_prefix(item, "CODE::") ||
                cg_sv_has_prefix(item, "EXPORT_ANDROID_MK::")) {
                nob_log(NOB_ERROR,
                        "codegen: unsupported install(FILES) pseudo-item: %.*s",
                        (int)item.count,
                        item.data ? item.data : "");
                return false;
            }

            if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, item, &src_path)) return false;
            basename = cg_basename_to_arena(ctx->scratch, src_path);
            if (destination.count > 0) {
                if (!cg_join_paths_to_arena(ctx->scratch, destination, basename, &install_rel)) return false;
            } else {
                install_rel = basename;
            }

            nob_sb_append_cstr(out, "        const char *install_path = ");
            if (!cg_sb_append_install_join_call(out, install_rel)) return false;
            nob_sb_append_cstr(out, ";\n");
            nob_sb_append_cstr(out, "        if (!install_copy_file(");
            if (!cg_sb_append_c_string(out, src_path)) return false;
            nob_sb_append_cstr(out, ", install_path)) return false;\n");
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        if (kind == BM_INSTALL_RULE_DIRECTORY) {
            BM_Directory_Id owner_dir = bm_query_install_rule_owner_directory(ctx->model, id);
            String_View item = bm_query_install_rule_item_raw(ctx->model, id);
            String_View src_path = {0};
            String_View install_rel = destination;
            bool copy_contents = item.count > 0 &&
                                 (item.data[item.count - 1] == '/' || item.data[item.count - 1] == '\\');
            if (!cg_check_no_genex("install(DIRECTORY)", item)) return false;
            if (!cg_resolve_install_item_from_owner_dirs(ctx, owner_dir, item, &src_path)) return false;
            if (!copy_contents) {
                String_View basename = cg_basename_to_arena(ctx->scratch, src_path);
                if (!cg_join_paths_to_arena(ctx->scratch, destination, basename, &install_rel)) return false;
            }
            nob_sb_append_cstr(out, "        const char *install_path = ");
            if (!cg_sb_append_install_join_call(out, install_rel)) return false;
            nob_sb_append_cstr(out, ";\n");
            nob_sb_append_cstr(out, "        if (!ensure_parent_dir(install_path)) return false;\n");
            nob_sb_append_cstr(out, "        if (!install_copy_directory(");
            if (!cg_sb_append_c_string(out, src_path)) return false;
            nob_sb_append_cstr(out, ", install_path)) return false;\n");
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        nob_log(NOB_ERROR,
                "codegen: unsupported install rule kind in install backend: %d",
                (int)kind);
        return false;
    }

    for (size_t export_index = 0; export_index < export_count; ++export_index) {
        BM_Export_Id export_id = (BM_Export_Id)export_index;
        String_View export_text = {0};
        String_View export_noconfig_text = {0};
        String_View output_rel = bm_query_export_output_file_path(ctx->model, export_id, ctx->scratch);
        String_View noconfig_output_rel = {0};
        bool use_noconfig = cg_export_has_non_interface_targets(ctx, export_id);
        if (bm_query_export_kind(ctx->model, export_id) != BM_EXPORT_INSTALL) continue;
        if (!cg_build_export_file_contents(ctx, export_id, &export_text)) {
            return false;
        }
        if (!cg_emit_install_component_guard_open(out, bm_query_export_component(ctx->model, export_id))) {
            return false;
        }
        nob_sb_append_cstr(out, "        const char *output_path = ");
        if (!cg_sb_append_install_join_call(out, output_rel)) return false;
        nob_sb_append_cstr(out, ";\n");
        nob_sb_append_cstr(out, "        if (!ensure_parent_dir(output_path)) return false;\n");
        nob_sb_append_cstr(out, "        if (!nob_write_entire_file(output_path, ");
        if (!cg_sb_append_c_string(out, export_text)) return false;
        nob_sb_append_cstr(out, ", strlen(");
        if (!cg_sb_append_c_string(out, export_text)) return false;
        nob_sb_append_cstr(out, "))) return false;\n");

        if (use_noconfig) {
            if (!cg_build_export_noconfig_file_contents(ctx, export_id, &export_noconfig_text) ||
                !cg_export_noconfig_output_file_path(ctx, export_id, &noconfig_output_rel)) {
                return false;
            }
            nob_sb_append_cstr(out, "        const char *noconfig_output_path = ");
            if (!cg_sb_append_install_join_call(out, noconfig_output_rel)) return false;
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
