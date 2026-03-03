#include "eval_meta.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <stdio.h>
#include <string.h>

static bool meta_emit_diag(Evaluator_Context *ctx,
                           const Node *node,
                           Cmake_Diag_Severity severity,
                           String_View cause,
                           String_View hint) {
    return eval_emit_diag(ctx,
                          severity,
                          nob_sv_from_cstr("eval_meta"),
                          node->as.cmd.name,
                          eval_origin_from_node(ctx, node),
                          cause,
                          hint);
}

static String_View meta_current_bin_dir(Evaluator_Context *ctx) {
    String_View v = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static String_View meta_concat3_temp(Evaluator_Context *ctx,
                                     const char *prefix,
                                     String_View middle,
                                     const char *suffix) {
    if (!ctx || !prefix || !suffix) return nob_sv_from_cstr("");
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + middle.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    if (middle.count > 0) memcpy(buf + prefix_len, middle.data, middle.count);
    memcpy(buf + prefix_len + middle.count, suffix, suffix_len);
    buf[prefix_len + middle.count + suffix_len] = '\0';
    return nob_sv_from_parts(buf, prefix_len + middle.count + suffix_len);
}

static bool meta_export_write(Evaluator_Context *ctx,
                              String_View out_path,
                              bool append,
                              String_View mode,
                              String_View targets,
                              String_View export_name,
                              String_View ns) {
    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "# evaluator-generated export metadata\n");
    nob_sb_append_cstr(&sb, "set(NOBIFY_EXPORT_MODE \"");
    nob_sb_append_buf(&sb, mode.data, mode.count);
    nob_sb_append_cstr(&sb, "\")\n");
    if (export_name.count > 0) {
        nob_sb_append_cstr(&sb, "set(NOBIFY_EXPORT_NAME \"");
        nob_sb_append_buf(&sb, export_name.data, export_name.count);
        nob_sb_append_cstr(&sb, "\")\n");
    }
    nob_sb_append_cstr(&sb, "set(NOBIFY_EXPORT_TARGETS \"");
    nob_sb_append_buf(&sb, targets.data, targets.count);
    nob_sb_append_cstr(&sb, "\")\n");
    nob_sb_append_cstr(&sb, "set(NOBIFY_EXPORT_NAMESPACE \"");
    nob_sb_append_buf(&sb, ns.data, ns.count);
    nob_sb_append_cstr(&sb, "\")\n");
    return eval_write_text_file(ctx, out_path, nob_sv_from_parts(sb.items, sb.count), append);
}

static bool meta_export_assign_last(Evaluator_Context *ctx,
                                    String_View mode,
                                    String_View file_path,
                                    String_View targets,
                                    String_View ns) {
    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE"), mode)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_FILE"), file_path)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_TARGETS"), targets)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_NAMESPACE"), ns)) return false;
    return true;
}

bool eval_handle_export(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() requires a signature keyword"),
                             nob_sv_from_cstr("Usage: export(TARGETS ...) or export(EXPORT ...)"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(a.items[0], "PACKAGE") || eval_sv_eq_ci_lit(a.items[0], "SETUP")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                             a.items[0]);
        return !eval_should_stop(ctx);
    }

    bool append = false;
    String_View file_path = nob_sv_from_cstr("");
    String_View ns = nob_sv_from_cstr("");
    String_View export_name = nob_sv_from_cstr("");
    SV_List targets = {0};

    if (eval_sv_eq_ci_lit(a.items[0], "TARGETS")) {
        size_t i = 1;
        for (; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "NAMESPACE") ||
                eval_sv_eq_ci_lit(a.items[i], "APPEND") ||
                eval_sv_eq_ci_lit(a.items[i], "FILE") ||
                eval_sv_eq_ci_lit(a.items[i], "EXPORT_LINK_INTERFACE_LIBRARIES") ||
                eval_sv_eq_ci_lit(a.items[i], "CXX_MODULES_DIRECTORY")) {
                break;
            }
            if (!svu_list_push_temp(ctx, &targets, a.items[i])) return !eval_should_stop(ctx);
        }

        if (targets.count == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) requires at least one target"),
                                 nob_sv_from_cstr("Usage: export(TARGETS <tgt>... FILE <file>)"));
            return !eval_should_stop(ctx);
        }

        for (; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "APPEND")) {
                append = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "EXPORT_LINK_INTERFACE_LIBRARIES")) continue;
            if (eval_sv_eq_ci_lit(a.items[i], "CXX_MODULES_DIRECTORY")) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(... CXX_MODULES_DIRECTORY ...) is not implemented yet"),
                                     nob_sv_from_cstr("This batch models only classic build-tree export metadata"));
                return !eval_should_stop(ctx);
            }
            if (eval_sv_eq_ci_lit(a.items[i], "NAMESPACE")) {
                if (i + 1 >= a.count) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(... NAMESPACE ...) requires a value"),
                                         nob_sv_from_cstr("Usage: ... NAMESPACE <ns>"));
                    return !eval_should_stop(ctx);
                }
                ns = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "FILE")) {
                if (i + 1 >= a.count) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(... FILE ...) requires a path"),
                                         nob_sv_from_cstr("Usage: ... FILE <path>"));
                    return !eval_should_stop(ctx);
                }
                file_path = a.items[++i];
                continue;
            }
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) received an unsupported argument"),
                                 a.items[i]);
            return !eval_should_stop(ctx);
        }

        for (size_t ti = 0; ti < targets.count; ti++) {
            if (!eval_target_known(ctx, targets.items[ti])) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(TARGETS ...) target was not declared"),
                                     targets.items[ti]);
                return !eval_should_stop(ctx);
            }
        }
    } else if (eval_sv_eq_ci_lit(a.items[0], "EXPORT")) {
        if (a.count < 2) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) requires an export name"),
                                 nob_sv_from_cstr("Usage: export(EXPORT <name> FILE <file>)"));
            return !eval_should_stop(ctx);
        }
        export_name = a.items[1];
        for (size_t i = 2; i < a.count; i++) {
            if (eval_sv_eq_ci_lit(a.items[i], "EXPORT_LINK_INTERFACE_LIBRARIES")) continue;
            if (eval_sv_eq_ci_lit(a.items[i], "APPEND")) {
                append = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "NAMESPACE")) {
                if (i + 1 >= a.count) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(EXPORT ... NAMESPACE ...) requires a value"),
                                         nob_sv_from_cstr("Usage: ... NAMESPACE <ns>"));
                    return !eval_should_stop(ctx);
                }
                ns = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "FILE")) {
                if (i + 1 >= a.count) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(EXPORT ... FILE ...) requires a path"),
                                         nob_sv_from_cstr("Usage: ... FILE <path>"));
                    return !eval_should_stop(ctx);
                }
                file_path = a.items[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a.items[i], "CXX_MODULES_DIRECTORY")) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(... CXX_MODULES_DIRECTORY ...) is not implemented yet"),
                                     nob_sv_from_cstr("This batch models only classic build-tree export metadata"));
                return !eval_should_stop(ctx);
            }
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) received an unsupported argument"),
                                 a.items[i]);
            return !eval_should_stop(ctx);
        }

        String_View map_key = meta_concat3_temp(ctx, "NOBIFY_INSTALL_EXPORT::", export_name, "::TARGETS");
        String_View targets_sv = eval_var_get(ctx, map_key);
        if (targets_sv.count == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) could not resolve tracked install export targets"),
                                 export_name);
            return !eval_should_stop(ctx);
        }
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), targets_sv, &targets)) return !eval_should_stop(ctx);
    } else {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                             a.items[0]);
        return !eval_should_stop(ctx);
    }

    if (file_path.count == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() requires FILE <path> in the supported signatures"),
                             nob_sv_from_cstr("Usage: export(TARGETS ... FILE <file>) or export(EXPORT ... FILE <file>)"));
        return !eval_should_stop(ctx);
    }

    if (!eval_sv_is_abs_path(file_path)) {
        file_path = eval_sv_path_join(eval_temp_arena(ctx), meta_current_bin_dir(ctx), file_path);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    }

    String_View targets_joined = eval_sv_join_semi_temp(ctx, targets.items, targets.count);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    String_View mode = export_name.count > 0 ? nob_sv_from_cstr("EXPORT") : nob_sv_from_cstr("TARGETS");
    if (!meta_export_write(ctx, file_path, append, mode, targets_joined, export_name, ns)) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() failed to write metadata file"),
                             file_path);
        return !eval_should_stop(ctx);
    }

    if (!meta_export_assign_last(ctx, mode, file_path, targets_joined, ns)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

bool eval_handle_include_external_msproject(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 2) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("include_external_msproject() requires project name and location"),
                             nob_sv_from_cstr("Usage: include_external_msproject(<name> <location> [TYPE <guid>] [GUID <guid>] [PLATFORM <name>] [deps...])"));
        return !eval_should_stop(ctx);
    }

    String_View name = a.items[0];
    String_View location = a.items[1];
    String_View type_guid = nob_sv_from_cstr("");
    String_View project_guid = nob_sv_from_cstr("");
    String_View platform = nob_sv_from_cstr("");
    SV_List deps = {0};

    size_t i = 2;
    while (i < a.count) {
        if (eval_sv_eq_ci_lit(a.items[i], "TYPE")) {
            if (i + 1 >= a.count) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(TYPE ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... TYPE <projectTypeGUID>"));
                return !eval_should_stop(ctx);
            }
            type_guid = a.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "GUID")) {
            if (i + 1 >= a.count) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(GUID ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... GUID <projectGUID>"));
                return !eval_should_stop(ctx);
            }
            project_guid = a.items[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a.items[i], "PLATFORM")) {
            if (i + 1 >= a.count) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(PLATFORM ...) requires a value"),
                                     nob_sv_from_cstr("Usage: ... PLATFORM <name>"));
                return !eval_should_stop(ctx);
            }
            platform = a.items[i + 1];
            i += 2;
            continue;
        }
        break;
    }

    for (; i < a.count; i++) {
        if (!svu_list_push_temp(ctx, &deps, a.items[i])) return !eval_should_stop(ctx);
    }

    if (!eval_target_register(ctx, name)) return !eval_should_stop(ctx);
    Cmake_Event ev = {0};
    ev.kind = EV_TARGET_DECLARE;
    ev.origin = eval_origin_from_node(ctx, node);
    ev.as.target_declare.name = sv_copy_to_event_arena(ctx, name);
    ev.as.target_declare.type = EV_TARGET_LIBRARY_UNKNOWN;
    if (!emit_event(ctx, ev)) return !eval_should_stop(ctx);

    String_View deps_joined = eval_sv_join_semi_temp(ctx, deps.items, deps.count);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::LOCATION"), location)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::TYPE"), type_guid)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::GUID"), project_guid)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::PLATFORM"), platform)) return !eval_should_stop(ctx);
    if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::DEPENDENCIES"), deps_joined)) return !eval_should_stop(ctx);
    return !eval_should_stop(ctx);
}

static bool meta_file_api_kind_supported(String_View kind) {
    return eval_sv_eq_ci_lit(kind, "CODEMODEL") ||
           eval_sv_eq_ci_lit(kind, "CACHE") ||
           eval_sv_eq_ci_lit(kind, "CMAKEFILES") ||
           eval_sv_eq_ci_lit(kind, "TOOLCHAINS");
}

static bool meta_file_api_is_version(String_View token) {
    if (token.count == 0) return false;
    bool saw_digit = false;
    for (size_t i = 0; i < token.count; i++) {
        char c = token.data[i];
        if (c == '.') continue;
        if (c < '0' || c > '9') return false;
        saw_digit = true;
    }
    return saw_digit;
}

bool eval_handle_cmake_file_api(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (a.count < 3) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() requires QUERY API_VERSION <major>"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 [CODEMODEL <v>...])"));
        return !eval_should_stop(ctx);
    }
    if (!eval_sv_eq_ci_lit(a.items[0], "QUERY")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports the QUERY form in evaluator v2"),
                             a.items[0]);
        return !eval_should_stop(ctx);
    }
    if (!eval_sv_eq_ci_lit(a.items[1], "API_VERSION")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api(QUERY ...) requires API_VERSION"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 ...)"));
        return !eval_should_stop(ctx);
    }
    if (!nob_sv_eq(a.items[2], nob_sv_from_cstr("1"))) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports API_VERSION 1 in this batch"),
                             a.items[2]);
        return !eval_should_stop(ctx);
    }

    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::API_VERSION"), nob_sv_from_cstr("1"))) {
        return !eval_should_stop(ctx);
    }

    size_t i = 3;
    while (i < a.count) {
        String_View kind = a.items[i++];
        if (!meta_file_api_kind_supported(kind)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() object kind is not implemented in evaluator v2"),
                                 kind);
            return !eval_should_stop(ctx);
        }

        SV_List versions = {0};
        while (i < a.count && !meta_file_api_kind_supported(a.items[i])) {
            if (!meta_file_api_is_version(a.items[i])) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("cmake_file_api() received an invalid object version token"),
                                     a.items[i]);
                return !eval_should_stop(ctx);
            }
            if (!svu_list_push_temp(ctx, &versions, a.items[i])) return !eval_should_stop(ctx);
            i++;
        }

        if (versions.count == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() requires at least one version for each object kind"),
                                 kind);
            return !eval_should_stop(ctx);
        }

        String_View joined = eval_sv_join_semi_temp(ctx, versions.items, versions.count);
        if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
        if (!eval_var_set(ctx, meta_concat3_temp(ctx, "NOBIFY_CMAKE_FILE_API_QUERY::", kind, ""), joined)) {
            return !eval_should_stop(ctx);
        }
    }

    return !eval_should_stop(ctx);
}
