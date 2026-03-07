#include "eval_meta.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool meta_emit_diag(Evaluator_Context *ctx,
                           const Node *node,
                           Cmake_Diag_Severity severity,
                           String_View cause,
                           String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_meta"), node->as.cmd.name, eval_origin_from_node(ctx, node), cause, hint);
}

static String_View meta_current_bin_dir(Evaluator_Context *ctx) {
    return eval_current_binary_dir(ctx);
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
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE"), mode)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_FILE"), file_path)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_TARGETS"), targets)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_NAMESPACE"), ns)) return false;
    return true;
}

Eval_Result eval_handle_export(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() requires a signature keyword"),
                             nob_sv_from_cstr("Usage: export(TARGETS ...) or export(EXPORT ...)"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_sv_eq_ci_lit(a[0], "PACKAGE") || eval_sv_eq_ci_lit(a[0], "SETUP")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                             a[0]);
        return eval_result_from_ctx(ctx);
    }

    bool append = false;
    String_View file_path = nob_sv_from_cstr("");
    String_View ns = nob_sv_from_cstr("");
    String_View export_name = nob_sv_from_cstr("");
    SV_List targets = NULL;

    if (eval_sv_eq_ci_lit(a[0], "TARGETS")) {
        size_t i = 1;
        for (; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "NAMESPACE") ||
                eval_sv_eq_ci_lit(a[i], "APPEND") ||
                eval_sv_eq_ci_lit(a[i], "FILE") ||
                eval_sv_eq_ci_lit(a[i], "EXPORT_LINK_INTERFACE_LIBRARIES") ||
                eval_sv_eq_ci_lit(a[i], "CXX_MODULES_DIRECTORY")) {
                break;
            }
            if (!svu_list_push_temp(ctx, &targets, a[i])) return eval_result_from_ctx(ctx);
        }

        if (arena_arr_len(targets) == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) requires at least one target"),
                                 nob_sv_from_cstr("Usage: export(TARGETS <tgt>... FILE <file>)"));
            return eval_result_from_ctx(ctx);
        }

        for (; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "APPEND")) {
                append = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "EXPORT_LINK_INTERFACE_LIBRARIES")) continue;
            if (eval_sv_eq_ci_lit(a[i], "CXX_MODULES_DIRECTORY")) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(... CXX_MODULES_DIRECTORY ...) is not implemented yet"),
                                     nob_sv_from_cstr("This batch models only classic build-tree export metadata"));
                return eval_result_from_ctx(ctx);
            }
            if (eval_sv_eq_ci_lit(a[i], "NAMESPACE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(... NAMESPACE ...) requires a value"),
                                         nob_sv_from_cstr("Usage: ... NAMESPACE <ns>"));
                    return eval_result_from_ctx(ctx);
                }
                ns = a[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "FILE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(... FILE ...) requires a path"),
                                         nob_sv_from_cstr("Usage: ... FILE <path>"));
                    return eval_result_from_ctx(ctx);
                }
                file_path = a[++i];
                continue;
            }
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) received an unsupported argument"),
                                 a[i]);
            return eval_result_from_ctx(ctx);
        }

        for (size_t ti = 0; ti < arena_arr_len(targets); ti++) {
            if (!eval_target_known(ctx, targets[ti])) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(TARGETS ...) target was not declared"),
                                     targets[ti]);
                return eval_result_from_ctx(ctx);
            }
        }
    } else if (eval_sv_eq_ci_lit(a[0], "EXPORT")) {
        if (arena_arr_len(a) < 2) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) requires an export name"),
                                 nob_sv_from_cstr("Usage: export(EXPORT <name> FILE <file>)"));
            return eval_result_from_ctx(ctx);
        }
        export_name = a[1];
        for (size_t i = 2; i < arena_arr_len(a); i++) {
            if (eval_sv_eq_ci_lit(a[i], "EXPORT_LINK_INTERFACE_LIBRARIES")) continue;
            if (eval_sv_eq_ci_lit(a[i], "APPEND")) {
                append = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "NAMESPACE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(EXPORT ... NAMESPACE ...) requires a value"),
                                         nob_sv_from_cstr("Usage: ... NAMESPACE <ns>"));
                    return eval_result_from_ctx(ctx);
                }
                ns = a[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "FILE")) {
                if (i + 1 >= arena_arr_len(a)) {
                    (void)meta_emit_diag(ctx,
                                         node,
                                         EV_DIAG_ERROR,
                                         nob_sv_from_cstr("export(EXPORT ... FILE ...) requires a path"),
                                         nob_sv_from_cstr("Usage: ... FILE <path>"));
                    return eval_result_from_ctx(ctx);
                }
                file_path = a[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(a[i], "CXX_MODULES_DIRECTORY")) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("export(... CXX_MODULES_DIRECTORY ...) is not implemented yet"),
                                     nob_sv_from_cstr("This batch models only classic build-tree export metadata"));
                return eval_result_from_ctx(ctx);
            }
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) received an unsupported argument"),
                                 a[i]);
            return eval_result_from_ctx(ctx);
        }

        String_View map_key = meta_concat3_temp(ctx, "NOBIFY_INSTALL_EXPORT::", export_name, "::TARGETS");
        String_View targets_sv = eval_var_get_visible(ctx, map_key);
        if (targets_sv.count == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(EXPORT ...) could not resolve tracked install export targets"),
                                 export_name);
            return eval_result_from_ctx(ctx);
        }
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), targets_sv, &targets)) return eval_result_from_ctx(ctx);
    } else {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                             a[0]);
        return eval_result_from_ctx(ctx);
    }

    if (file_path.count == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() requires FILE <path> in the supported signatures"),
                             nob_sv_from_cstr("Usage: export(TARGETS ... FILE <file>) or export(EXPORT ... FILE <file>)"));
        return eval_result_from_ctx(ctx);
    }

    if (!eval_sv_is_abs_path(file_path)) {
        file_path = eval_sv_path_join(eval_temp_arena(ctx), meta_current_bin_dir(ctx), file_path);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    }

    String_View targets_joined = eval_sv_join_semi_temp(ctx, targets, arena_arr_len(targets));
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    String_View mode = export_name.count > 0 ? nob_sv_from_cstr("EXPORT") : nob_sv_from_cstr("TARGETS");
    if (!meta_export_write(ctx, file_path, append, mode, targets_joined, export_name, ns)) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() failed to write metadata file"),
                             file_path);
        return eval_result_from_ctx(ctx);
    }

    if (!meta_export_assign_last(ctx, mode, file_path, targets_joined, ns)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_include_external_msproject(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 2) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("include_external_msproject() requires project name and location"),
                             nob_sv_from_cstr("Usage: include_external_msproject(<name> <location> [TYPE <guid>] [GUID <guid>] [PLATFORM <name>] [deps...])"));
        return eval_result_from_ctx(ctx);
    }

    String_View name = a[0];
    String_View location = a[1];
    String_View type_guid = nob_sv_from_cstr("");
    String_View project_guid = nob_sv_from_cstr("");
    String_View platform = nob_sv_from_cstr("");
    SV_List deps = NULL;

    size_t i = 2;
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "TYPE")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(TYPE ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... TYPE <projectTypeGUID>"));
                return eval_result_from_ctx(ctx);
            }
            type_guid = a[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "GUID")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(GUID ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... GUID <projectGUID>"));
                return eval_result_from_ctx(ctx);
            }
            project_guid = a[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PLATFORM")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(PLATFORM ...) requires a value"),
                                     nob_sv_from_cstr("Usage: ... PLATFORM <name>"));
                return eval_result_from_ctx(ctx);
            }
            platform = a[i + 1];
            i += 2;
            continue;
        }
        break;
    }

    for (; i < arena_arr_len(a); i++) {
        if (!svu_list_push_temp(ctx, &deps, a[i])) return eval_result_from_ctx(ctx);
    }

    if (!eval_target_register(ctx, name)) return eval_result_from_ctx(ctx);

    String_View deps_joined = eval_sv_join_semi_temp(ctx, deps, arena_arr_len(deps));
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::LOCATION"), location)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::TYPE"), type_guid)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::GUID"), project_guid)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::PLATFORM"), platform)) return eval_result_from_ctx(ctx);
    if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", name, "::DEPENDENCIES"), deps_joined)) return eval_result_from_ctx(ctx);
    if (!eval_emit_target_declare(ctx,
                                  eval_origin_from_node(ctx, node),
                                  name,
                                  EV_TARGET_LIBRARY_UNKNOWN,
                                  true,
                                  false,
                                  nob_sv_from_cstr(""))) {
        return eval_result_fatal();
    }
    return eval_result_from_ctx(ctx);
}

static bool meta_file_api_kind_supported(String_View kind) {
    return eval_sv_eq_ci_lit(kind, "CODEMODEL") ||
           eval_sv_eq_ci_lit(kind, "CACHE") ||
           eval_sv_eq_ci_lit(kind, "CMAKEFILES") ||
           eval_sv_eq_ci_lit(kind, "TOOLCHAINS");
}

typedef struct {
    String_View kind_upper;
    String_View kind_json;
    String_View versions;
} Meta_File_Api_Request;

typedef Meta_File_Api_Request *Meta_File_Api_Request_List;

static String_View meta_file_api_kind_json_name(String_View kind) {
    if (eval_sv_eq_ci_lit(kind, "CODEMODEL")) return nob_sv_from_cstr("codemodel");
    if (eval_sv_eq_ci_lit(kind, "CACHE")) return nob_sv_from_cstr("cache");
    if (eval_sv_eq_ci_lit(kind, "CMAKEFILES")) return nob_sv_from_cstr("cmakeFiles");
    if (eval_sv_eq_ci_lit(kind, "TOOLCHAINS")) return nob_sv_from_cstr("toolchains");
    return nob_sv_from_cstr("");
}

static String_View meta_file_api_root_dir(Evaluator_Context *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), meta_current_bin_dir(ctx), nob_sv_from_cstr(".cmake/api/v1"));
}

static String_View meta_file_api_query_dir(Evaluator_Context *ctx) {
    String_View root = meta_file_api_root_dir(ctx);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("query/client-nobify"));
}

static String_View meta_file_api_reply_dir(Evaluator_Context *ctx) {
    String_View root = meta_file_api_root_dir(ctx);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("reply"));
}

static bool meta_file_api_append_version_fields(Nob_String_Builder *sb, String_View version) {
    static const char *const field_names[] = {"major", "minor", "patch", "tweak"};
    if (!sb || version.count == 0) return false;

    size_t field_index = 0;
    size_t part_start = 0;
    for (size_t i = 0; i <= version.count; i++) {
        if (i < version.count && version.data[i] != '.') {
            if (!isdigit((unsigned char)version.data[i])) return false;
            continue;
        }

        if (i == part_start || field_index >= sizeof(field_names)/sizeof(field_names[0])) return false;
        if (field_index > 0) nob_sb_append_cstr(sb, ", ");
        nob_sb_append_cstr(sb, "\"");
        nob_sb_append_cstr(sb, field_names[field_index]);
        nob_sb_append_cstr(sb, "\": ");
        nob_sb_append_buf(sb, version.data + part_start, i - part_start);
        field_index++;
        part_start = i + 1;
    }

    return field_index > 0;
}

static bool meta_file_api_is_version(String_View token) {
    if (token.count == 0) return false;
    bool saw_digit = false;
    bool prev_dot = true;
    for (size_t i = 0; i < token.count; i++) {
        char c = token.data[i];
        if (c == '.') {
            if (prev_dot) return false;
            prev_dot = true;
            continue;
        }
        if (c < '0' || c > '9') return false;
        prev_dot = false;
        saw_digit = true;
    }
    return saw_digit && !prev_dot;
}

static bool meta_file_api_set_path_var(Evaluator_Context *ctx, const char *key, String_View value) {
    if (!ctx || !key) return false;
    return eval_var_set_current(ctx, nob_sv_from_cstr(key), value);
}

static String_View meta_file_api_reply_filename_temp(Evaluator_Context *ctx,
                                                     String_View kind_json,
                                                     String_View version) {
    if (!ctx || kind_json.count == 0 || version.count == 0) return nob_sv_from_cstr("");
    size_t total = kind_json.count + sizeof("-v") - 1 + version.count + sizeof(".json") - 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, kind_json.data, kind_json.count);
    off += kind_json.count;
    memcpy(buf + off, "-v", sizeof("-v") - 1);
    off += sizeof("-v") - 1;
    memcpy(buf + off, version.data, version.count);
    off += version.count;
    memcpy(buf + off, ".json", sizeof(".json") - 1);
    off += sizeof(".json") - 1;
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool meta_file_api_write_query_file(Evaluator_Context *ctx,
                                           String_View query_file,
                                           const Meta_File_Api_Request_List *requests) {
    if (!ctx || !requests) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "{\n  \"client\": \"nobify\",\n  \"requests\": [\n");
    for (size_t i = 0; i < arena_arr_len(*requests); i++) {
        Meta_File_Api_Request req = (*requests)[i];
        SV_List versions = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), req.versions, &versions)) {
            nob_sb_free(sb);
            return false;
        }

        if (i > 0) nob_sb_append_cstr(&sb, ",\n");
        nob_sb_append_cstr(&sb, "    {\"kind\": \"");
        nob_sb_append_buf(&sb, req.kind_json.data, req.kind_json.count);
        nob_sb_append_cstr(&sb, "\", \"version\": [");
        for (size_t vi = 0; vi < arena_arr_len(versions); vi++) {
            if (vi > 0) nob_sb_append_cstr(&sb, ", ");
            nob_sb_append_cstr(&sb, "{");
            if (!meta_file_api_append_version_fields(&sb, versions[vi])) {
                nob_sb_free(sb);
                return false;
            }
            nob_sb_append_cstr(&sb, "}");
        }
        nob_sb_append_cstr(&sb, "]}");
    }
    nob_sb_append_cstr(&sb, "\n  ]\n}\n");

    bool ok = eval_write_text_file(ctx, query_file, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), false);
    nob_sb_free(sb);
    return ok;
}

static bool meta_file_api_write_reply_file(Evaluator_Context *ctx,
                                           String_View reply_dir,
                                           String_View build_dir,
                                           Meta_File_Api_Request req,
                                           String_View version,
                                           String_View *out_reply_file_name) {
    if (!ctx || !out_reply_file_name) return false;
    *out_reply_file_name = nob_sv_from_cstr("");

    String_View reply_name = meta_file_api_reply_filename_temp(ctx, req.kind_json, version);
    if (eval_should_stop(ctx)) return false;
    String_View reply_path = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, reply_name);
    if (eval_should_stop(ctx)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "{\n  \"kind\": \"");
    nob_sb_append_buf(&sb, req.kind_json.data, req.kind_json.count);
    nob_sb_append_cstr(&sb, "\",\n  \"version\": {");
    if (!meta_file_api_append_version_fields(&sb, version)) {
        nob_sb_free(sb);
        return false;
    }
    nob_sb_append_cstr(&sb, "},\n  \"generator\": \"Nobify Evaluator\",\n  \"paths\": {\n    \"build\": \"");
    nob_sb_append_buf(&sb, build_dir.data ? build_dir.data : "", build_dir.count);
    nob_sb_append_cstr(&sb, "\"\n  }\n}\n");

    bool ok = eval_write_text_file(ctx, reply_path, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), false);
    nob_sb_free(sb);
    if (!ok) return false;

    *out_reply_file_name = reply_name;
    return true;
}

static bool meta_file_api_write_index_file(Evaluator_Context *ctx,
                                           String_View index_file,
                                           String_View build_dir,
                                           String_View query_file,
                                           const Meta_File_Api_Request_List *requests) {
    if (!ctx || !requests) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "{\n  \"cmake\": {\n    \"generator\": {\"name\": \"Nobify Evaluator\"},\n    \"paths\": {\n      \"build\": \"");
    nob_sb_append_buf(&sb, build_dir.data ? build_dir.data : "", build_dir.count);
    nob_sb_append_cstr(&sb, "\"\n    }\n  },\n  \"query\": {\n    \"path\": \"");
    nob_sb_append_buf(&sb, query_file.data ? query_file.data : "", query_file.count);
    nob_sb_append_cstr(&sb, "\"\n  },\n  \"objects\": [\n");

    bool first_object = true;
    String_View reply_dir = meta_file_api_reply_dir(ctx);
    if (eval_should_stop(ctx)) {
        nob_sb_free(sb);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(*requests); i++) {
        Meta_File_Api_Request req = (*requests)[i];
        SV_List versions = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), req.versions, &versions)) {
            nob_sb_free(sb);
            return false;
        }

        for (size_t vi = 0; vi < arena_arr_len(versions); vi++) {
            String_View reply_name = nob_sv_from_cstr("");
            if (!meta_file_api_write_reply_file(ctx, reply_dir, build_dir, req, versions[vi], &reply_name)) {
                nob_sb_free(sb);
                return false;
            }

            if (!first_object) nob_sb_append_cstr(&sb, ",\n");
            first_object = false;
            nob_sb_append_cstr(&sb, "    {\"kind\": \"");
            nob_sb_append_buf(&sb, req.kind_json.data, req.kind_json.count);
            nob_sb_append_cstr(&sb, "\", \"version\": {");
            if (!meta_file_api_append_version_fields(&sb, versions[vi])) {
                nob_sb_free(sb);
                return false;
            }
            nob_sb_append_cstr(&sb, "}, \"jsonFile\": \"");
            nob_sb_append_buf(&sb, reply_name.data, reply_name.count);
            nob_sb_append_cstr(&sb, "\"}");

            String_View reply_var_key = meta_concat3_temp(ctx, "NOBIFY_CMAKE_FILE_API_REPLY::", req.kind_upper, "::FILE");
            String_View reply_path = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, reply_name);
            if (eval_should_stop(ctx)) {
                nob_sb_free(sb);
                return false;
            }
            if (!eval_var_set_current(ctx, reply_var_key, reply_path)) {
                nob_sb_free(sb);
                return false;
            }
        }
    }

    nob_sb_append_cstr(&sb, "\n  ]\n}\n");
    bool ok = eval_write_text_file(ctx, index_file, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), false);
    nob_sb_free(sb);
    return ok;
}

static bool meta_file_api_stage_query(Evaluator_Context *ctx, const Meta_File_Api_Request_List *requests) {
    if (!ctx || !requests) return false;

    String_View build_dir = meta_current_bin_dir(ctx);
    String_View root_dir = meta_file_api_root_dir(ctx);
    String_View query_dir = meta_file_api_query_dir(ctx);
    String_View reply_dir = meta_file_api_reply_dir(ctx);
    if (eval_should_stop(ctx)) return false;

    String_View query_file = eval_sv_path_join(eval_temp_arena(ctx), query_dir, nob_sv_from_cstr("query.json"));
    String_View index_file = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, nob_sv_from_cstr("index-nobify-v1.json"));
    if (eval_should_stop(ctx)) return false;

    if (!meta_file_api_write_query_file(ctx, query_file, requests)) return false;
    if (!meta_file_api_write_index_file(ctx, index_file, build_dir, query_file, requests)) return false;

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_FILE_API"), nob_sv_from_cstr("1"))) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::ROOT", root_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::QUERY_DIR", query_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::QUERY_FILE", query_file)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::REPLY_DIR", reply_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::INDEX_FILE", index_file)) return false;
    return true;
}

Eval_Result eval_handle_cmake_file_api(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    if (arena_arr_len(a) < 3) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() requires QUERY API_VERSION <major>"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 [CODEMODEL <v>...])"));
        return eval_result_from_ctx(ctx);
    }
    if (!eval_sv_eq_ci_lit(a[0], "QUERY")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports the QUERY form in evaluator v2"),
                             a[0]);
        return eval_result_from_ctx(ctx);
    }
    if (!eval_sv_eq_ci_lit(a[1], "API_VERSION")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api(QUERY ...) requires API_VERSION"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 ...)"));
        return eval_result_from_ctx(ctx);
    }
    if (!nob_sv_eq(a[2], nob_sv_from_cstr("1"))) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports API_VERSION 1 in this batch"),
                             a[2]);
        return eval_result_from_ctx(ctx);
    }

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::API_VERSION"), nob_sv_from_cstr("1"))) {
        return eval_result_from_ctx(ctx);
    }

    Meta_File_Api_Request_List requests = NULL;
    size_t i = 3;
    while (i < arena_arr_len(a)) {
        String_View kind = a[i++];
        if (!meta_file_api_kind_supported(kind)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() object kind is not implemented in evaluator v2"),
                                 kind);
            return eval_result_from_ctx(ctx);
        }

        SV_List versions = NULL;
        while (i < arena_arr_len(a) && !meta_file_api_kind_supported(a[i])) {
            if (!meta_file_api_is_version(a[i])) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("cmake_file_api() received an invalid object version token"),
                                     a[i]);
                return eval_result_from_ctx(ctx);
            }
            if (!svu_list_push_temp(ctx, &versions, a[i])) return eval_result_from_ctx(ctx);
            i++;
        }

        if (arena_arr_len(versions) == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() requires at least one version for each object kind"),
                                 kind);
            return eval_result_from_ctx(ctx);
        }

        String_View joined = eval_sv_join_semi_temp(ctx, versions, arena_arr_len(versions));
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        String_View kind_upper = eval_property_upper_name_temp(ctx, kind);
        String_View kind_json = meta_file_api_kind_json_name(kind);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (!eval_var_set_current(ctx, meta_concat3_temp(ctx, "NOBIFY_CMAKE_FILE_API_QUERY::", kind_upper, ""), joined)) {
            return eval_result_from_ctx(ctx);
        }
        Meta_File_Api_Request req = {
            .kind_upper = kind_upper,
            .kind_json = kind_json,
            .versions = joined,
        };
        if (!EVAL_ARR_PUSH(ctx, ctx->arena, requests, req)) return eval_result_fatal();
    }

    if (!meta_file_api_stage_query(ctx, &requests)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
