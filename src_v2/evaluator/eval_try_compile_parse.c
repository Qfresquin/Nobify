#include "eval_try_compile_internal.h"

static bool try_compile_add_source_item(Evaluator_Context *ctx,
                                        Try_Compile_Request *req,
                                        String_View path,
                                        Try_Compile_Language forced_lang) {
    if (!ctx || !req || path.count == 0) return false;
    Try_Compile_Source_Item item = {0};
    item.path = path;
    item.language = forced_lang != TRY_COMPILE_LANG_AUTO ? forced_lang : try_compile_detect_language(path);
    return try_compile_source_push(ctx, &req->source_items, item);
}

static bool try_compile_write_generated_source(Evaluator_Context *ctx,
                                               Try_Compile_Request *req,
                                               String_View name,
                                               String_View content,
                                               Try_Compile_Language forced_lang) {
    if (!ctx || !req) return false;
    if (name.count == 0) {
        if (forced_lang == TRY_COMPILE_LANG_CXX) name = nob_sv_from_cstr("try_compile_source.cpp");
        else name = nob_sv_from_cstr("try_compile_source.c");
    }

    String_View src_path = eval_sv_path_join(eval_temp_arena(ctx), req->binary_dir, name);
    char *src_c = eval_sv_to_cstr_temp(ctx, src_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    if (!nob_write_entire_file(src_c, content.data ? content.data : "", content.count)) {
        return false;
    }
    return try_compile_add_source_item(ctx, req, src_path, forced_lang);
}

static bool try_compile_collect_until_keyword(Evaluator_Context *ctx,
                                              const SV_List *args,
                                              size_t *io_index,
                                              SV_List *out) {
    if (!ctx || !args || !io_index || !out) return false;
    while (*io_index + 1 < arena_arr_len(*args) && !try_compile_is_keyword((*args)[*io_index + 1])) {
        (*io_index)++;
        if (!eval_sv_arr_push_temp(ctx, out, (*args)[*io_index])) return false;
    }
    return true;
}

static bool try_compile_parse_source_options(Evaluator_Context *ctx,
                                             const Node *node,
                                             const SV_List *args,
                                             size_t start,
                                             Try_Compile_Request *req) {
    if (!ctx || !node || !args || !req) return false;
    Try_Compile_Language current_type = TRY_COMPILE_LANG_AUTO;

    for (size_t i = start; i < arena_arr_len(*args); i++) {
        String_View tok = (*args)[i];

        if (eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE")) {
            if (i + 1 < arena_arr_len(*args)) req->output_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COPY_FILE")) {
            if (i + 1 < arena_arr_len(*args)) req->copy_file_path = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COPY_FILE_ERROR")) {
            if (i + 1 < arena_arr_len(*args)) req->copy_file_error_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION")) {
            if (i + 1 < arena_arr_len(*args)) req->log_description = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINKER_LANGUAGE")) {
            if (i + 1 < arena_arr_len(*args)) req->linker_language = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "NO_CACHE")) {
            req->no_cache = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCES_TYPE")) {
            if (i + 1 < arena_arr_len(*args)) current_type = try_compile_language_from_sources_type((*args)[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_STANDARD")) {
            if (i + 1 < arena_arr_len(*args)) {
                req->c_lang.has_value = true;
                req->c_lang.standard = (*args)[++i];
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_STANDARD_REQUIRED")) {
            if (i + 1 < arena_arr_len(*args)) req->c_lang.standard_required = !try_compile_is_false((*args)[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "C_EXTENSIONS")) {
            if (i + 1 < arena_arr_len(*args)) {
                req->c_lang.extensions_set = true;
                req->c_lang.extensions = !try_compile_is_false((*args)[++i]);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_STANDARD")) {
            if (i + 1 < arena_arr_len(*args)) {
                req->cxx_lang.has_value = true;
                req->cxx_lang.standard = (*args)[++i];
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_STANDARD_REQUIRED")) {
            if (i + 1 < arena_arr_len(*args)) req->cxx_lang.standard_required = !try_compile_is_false((*args)[++i]);
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CXX_EXTENSIONS")) {
            if (i + 1 < arena_arr_len(*args)) {
                req->cxx_lang.extensions_set = true;
                req->cxx_lang.extensions = !try_compile_is_false((*args)[++i]);
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "COMPILE_DEFINITIONS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->compile_definitions)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINK_OPTIONS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->link_options)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "LINK_LIBRARIES")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->link_libraries)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS")) {
            if (!try_compile_collect_until_keyword(ctx, args, &i, &req->cmake_flags)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCES")) {
            SV_List values = NULL;
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            for (size_t vi = 0; vi < arena_arr_len(values); vi++) {
                if (!try_compile_add_source_item(ctx, req, values[vi], current_type)) return false;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_CONTENT")) {
            SV_List values = NULL;
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (arena_arr_len(values) == 0) continue;
            String_View content = arena_arr_len(values) > 1
                ? svu_join_space_temp(ctx, &values[1], arena_arr_len(values) - 1)
                : nob_sv_from_cstr("");
            if (!try_compile_write_generated_source(ctx, req, values[0], content, current_type)) return false;
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_VAR")) {
            SV_List values = NULL;
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (arena_arr_len(values) < 2) continue;
            if (!try_compile_write_generated_source(ctx,
                                                    req,
                                                    values[0],
                                                    eval_var_get_visible(ctx, values[1]),
                                                    current_type)) {
                return false;
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "SOURCE_FROM_FILE")) {
            SV_List values = NULL;
            if (!try_compile_collect_until_keyword(ctx, args, &i, &values)) return false;
            if (arena_arr_len(values) < 2) continue;

            String_View src_file = try_compile_resolve_in_dir(ctx, values[1], req->current_src_dir);
            char *src_c = eval_sv_to_cstr_temp(ctx, src_file);
            EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);

            Nob_String_Builder sb = {0};
            if (!nob_read_entire_file(src_c, &sb)) continue;
            String_View content = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
            bool ok = try_compile_write_generated_source(ctx, req, values[0], content, current_type);
            nob_sb_free(sb);
            if (!ok) return false;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "PROJECT") ||
            eval_sv_eq_ci_lit(tok, "SOURCE_DIR") ||
            eval_sv_eq_ci_lit(tok, "BINARY_DIR") ||
            eval_sv_eq_ci_lit(tok, "TARGET")) {
            return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("PROJECT-signature keywords are invalid in try_compile(SOURCE ...)"), nob_sv_from_cstr("Use try_compile(<out-var> PROJECT ... SOURCE_DIR ...)"));
        }

        if (!try_compile_add_source_item(ctx, req, tok, current_type)) return false;
    }

    return true;
}

static void try_compile_init_request(Evaluator_Context *ctx,
                                     String_View result_var,
                                     Try_Compile_Request *out_req) {
    if (!ctx || !out_req) return;
    *out_req = (Try_Compile_Request){
        .signature = TRY_COMPILE_SIGNATURE_SOURCE,
        .result_var = result_var,
        .current_src_dir = try_compile_current_src_dir(ctx),
        .current_bin_dir = try_compile_current_bin_dir(ctx),
        .binary_dir = nob_sv_from_cstr(""),
    };
}

static bool try_compile_parse_source_request_core(Evaluator_Context *ctx,
                                                  const Node *node,
                                                  const SV_List *args,
                                                  Try_Compile_Request *out_req) {
    if (!ctx || !node || !args || !out_req) return false;
    if (arena_arr_len(*args) < 2) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_compile() requires at least a result variable"), nob_sv_from_cstr("Usage: try_compile(<out-var> <bindir> <src> ...)"));
    }

    try_compile_init_request(ctx, (*args)[0], out_req);

    size_t opt_start = 1;
    if (!try_compile_is_keyword((*args)[1])) {
        out_req->binary_dir = try_compile_resolve_in_dir(ctx, (*args)[1], out_req->current_bin_dir);
        opt_start = 2;
    } else {
        out_req->binary_dir = try_compile_make_scratch_dir(ctx, out_req->current_bin_dir);
    }

    char *bindir_c = eval_sv_to_cstr_temp(ctx, out_req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
    (void)try_compile_mkdir_p_local(ctx, bindir_c);

    if (!try_compile_parse_source_options(ctx, node, args, opt_start, out_req)) return false;
    if (out_req->source_items.count == 0) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_compile(SOURCE ...) requires at least one source input"), nob_sv_from_cstr("Use SOURCES, SOURCE_FROM_CONTENT, SOURCE_FROM_VAR or SOURCE_FROM_FILE"));
    }

    return true;
}

bool try_compile_parse_request(Evaluator_Context *ctx,
                               const Node *node,
                               const SV_List *args,
                               Try_Compile_Request *out_req) {
    if (!ctx || !node || !args || !out_req) return false;
    if (arena_arr_len(*args) < 2) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_compile() requires at least a result variable"), nob_sv_from_cstr("Usage: try_compile(<out-var> <bindir> <src> ...)"));
    }

    try_compile_init_request(ctx, (*args)[0], out_req);

    if (eval_sv_eq_ci_lit((*args)[1], "PROJECT")) {
        out_req->signature = TRY_COMPILE_SIGNATURE_PROJECT;
        size_t i = 1;
        for (; i < arena_arr_len(*args); i++) {
            String_View tok = (*args)[i];
            if (eval_sv_eq_ci_lit(tok, "PROJECT")) {
                if (i + 1 < arena_arr_len(*args)) out_req->project_name = (*args)[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "SOURCE_DIR")) {
                if (i + 1 < arena_arr_len(*args)) out_req->source_dir = try_compile_resolve_in_dir(ctx, (*args)[++i], out_req->current_src_dir);
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "BINARY_DIR")) {
                if (i + 1 < arena_arr_len(*args)) out_req->binary_dir = try_compile_resolve_in_dir(ctx, (*args)[++i], out_req->current_bin_dir);
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "TARGET")) {
                if (i + 1 < arena_arr_len(*args)) out_req->target_name = (*args)[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "OUTPUT_VARIABLE")) {
                if (i + 1 < arena_arr_len(*args)) out_req->output_var = (*args)[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "LOG_DESCRIPTION")) {
                if (i + 1 < arena_arr_len(*args)) out_req->log_description = (*args)[++i];
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "NO_CACHE")) {
                out_req->no_cache = true;
                continue;
            }
            if (eval_sv_eq_ci_lit(tok, "CMAKE_FLAGS")) {
                if (!try_compile_collect_until_keyword(ctx, args, &i, &out_req->cmake_flags)) return false;
                continue;
            }
        }

        if (out_req->source_dir.count == 0) {
            return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_compile(PROJECT ...) requires SOURCE_DIR"), nob_sv_from_cstr("Usage: try_compile(<out-var> PROJECT <name> SOURCE_DIR <dir> ...)"));
        }

        if (out_req->binary_dir.count == 0) {
            out_req->binary_dir = try_compile_make_scratch_dir(ctx, out_req->current_bin_dir);
        }
        char *bindir_c = eval_sv_to_cstr_temp(ctx, out_req->binary_dir);
        EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
        (void)try_compile_mkdir_p_local(ctx, bindir_c);
        return true;
    }
    return try_compile_parse_source_request_core(ctx, node, args, out_req);
}

bool try_run_parse_request(Evaluator_Context *ctx,
                           const Node *node,
                           const SV_List *args,
                           Try_Run_Request *out_req) {
    if (!ctx || !node || !args || !out_req) return false;
    if (arena_arr_len(*args) < 4) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() requires run result, compile result, binary directory and sources"), nob_sv_from_cstr("Usage: try_run(<run-var> <compile-var> <bindir> <src> ...)"));
    }

    *out_req = (Try_Run_Request){
        .run_result_var = (*args)[0],
    };

    if (eval_sv_eq_ci_lit((*args)[2], "PROJECT")) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() does not support the PROJECT signature in this batch"), nob_sv_from_cstr("Use source-file forms only"));
    }

    if (try_compile_is_keyword((*args)[2])) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() requires an explicit binary directory"), nob_sv_from_cstr("Usage: try_run(<run-var> <compile-var> <bindir> <src> ...)"));
    }

    SV_List compile_args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &compile_args, (*args)[1])) return false;

    for (size_t i = 2; i < arena_arr_len(*args); i++) {
        String_View tok = (*args)[i];

        if (eval_sv_eq_ci_lit(tok, "PROJECT")) {
            return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() does not support the PROJECT signature in this batch"), nob_sv_from_cstr("Use source-file forms only"));
        }

        if (eval_sv_eq_ci_lit(tok, "COMPILE_OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(*args)) {
                return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run(COMPILE_OUTPUT_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: try_run(... COMPILE_OUTPUT_VARIABLE <var>)"));
            }
            out_req->compile_output_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "RUN_OUTPUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(*args)) {
                return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run(RUN_OUTPUT_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: try_run(... RUN_OUTPUT_VARIABLE <var>)"));
            }
            out_req->run_output_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "RUN_OUTPUT_STDOUT_VARIABLE")) {
            if (i + 1 >= arena_arr_len(*args)) {
                return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run(RUN_OUTPUT_STDOUT_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: try_run(... RUN_OUTPUT_STDOUT_VARIABLE <var>)"));
            }
            out_req->run_stdout_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "RUN_OUTPUT_STDERR_VARIABLE")) {
            if (i + 1 >= arena_arr_len(*args)) {
                return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run(RUN_OUTPUT_STDERR_VARIABLE) requires an output variable"), nob_sv_from_cstr("Usage: try_run(... RUN_OUTPUT_STDERR_VARIABLE <var>)"));
            }
            out_req->run_stderr_var = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "WORKING_DIRECTORY")) {
            if (i + 1 >= arena_arr_len(*args)) {
                return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run(WORKING_DIRECTORY) requires a path"), nob_sv_from_cstr("Usage: try_run(... WORKING_DIRECTORY <dir>)"));
            }
            out_req->working_directory = (*args)[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(tok, "ARGS")) {
            for (size_t j = i + 1; j < arena_arr_len(*args); j++) {
                if (!eval_sv_arr_push_temp(ctx, &out_req->run_args, (*args)[j])) return false;
            }
            break;
        }

        if (!eval_sv_arr_push_temp(ctx, &compile_args, tok)) return false;
    }

    return try_compile_parse_source_request_core(ctx, node, &compile_args, &out_req->compile_req);
}
