#include "eval_try_compile_internal.h"

#include "eval_opt_parser.h"

typedef enum {
    TRY_COMPILE_SRC_OPT_OUTPUT_VARIABLE = 0,
    TRY_COMPILE_SRC_OPT_COPY_FILE,
    TRY_COMPILE_SRC_OPT_COPY_FILE_ERROR,
    TRY_COMPILE_SRC_OPT_CMAKE_FLAGS,
    TRY_COMPILE_SRC_OPT_COMPILE_DEFINITIONS,
    TRY_COMPILE_SRC_OPT_LINK_OPTIONS,
    TRY_COMPILE_SRC_OPT_LINK_LIBRARIES,
    TRY_COMPILE_SRC_OPT_LINKER_LANGUAGE,
    TRY_COMPILE_SRC_OPT_NO_CACHE,
    TRY_COMPILE_SRC_OPT_LOG_DESCRIPTION,
    TRY_COMPILE_SRC_OPT_SOURCES,
    TRY_COMPILE_SRC_OPT_SOURCES_TYPE,
    TRY_COMPILE_SRC_OPT_SOURCE_FROM_CONTENT,
    TRY_COMPILE_SRC_OPT_SOURCE_FROM_VAR,
    TRY_COMPILE_SRC_OPT_SOURCE_FROM_FILE,
    TRY_COMPILE_SRC_OPT_C_STANDARD,
    TRY_COMPILE_SRC_OPT_C_STANDARD_REQUIRED,
    TRY_COMPILE_SRC_OPT_C_EXTENSIONS,
    TRY_COMPILE_SRC_OPT_CXX_STANDARD,
    TRY_COMPILE_SRC_OPT_CXX_STANDARD_REQUIRED,
    TRY_COMPILE_SRC_OPT_CXX_EXTENSIONS,
    TRY_COMPILE_SRC_OPT_INVALID_PROJECT,
    TRY_COMPILE_SRC_OPT_INVALID_SOURCE_DIR,
    TRY_COMPILE_SRC_OPT_INVALID_BINARY_DIR,
    TRY_COMPILE_SRC_OPT_INVALID_TARGET,
} Try_Compile_Source_Opt_Id;

typedef enum {
    TRY_COMPILE_PROJECT_OPT_PROJECT = 0,
    TRY_COMPILE_PROJECT_OPT_SOURCE_DIR,
    TRY_COMPILE_PROJECT_OPT_BINARY_DIR,
    TRY_COMPILE_PROJECT_OPT_TARGET,
    TRY_COMPILE_PROJECT_OPT_OUTPUT_VARIABLE,
    TRY_COMPILE_PROJECT_OPT_LOG_DESCRIPTION,
    TRY_COMPILE_PROJECT_OPT_NO_CACHE,
    TRY_COMPILE_PROJECT_OPT_CMAKE_FLAGS,
} Try_Compile_Project_Opt_Id;

typedef enum {
    TRY_RUN_OPT_COMPILE_OUTPUT_VARIABLE = 0,
    TRY_RUN_OPT_RUN_OUTPUT_VARIABLE,
    TRY_RUN_OPT_RUN_OUTPUT_STDOUT_VARIABLE,
    TRY_RUN_OPT_RUN_OUTPUT_STDERR_VARIABLE,
    TRY_RUN_OPT_WORKING_DIRECTORY,
    TRY_RUN_OPT_ARGS,
    TRY_RUN_OPT_PROJECT,
} Try_Run_Opt_Id;

typedef struct {
    const Node *node;
    Try_Compile_Request *req;
    Cmake_Event_Origin origin;
    Try_Compile_Language current_type;
} Try_Compile_Source_Parse_State;

typedef struct {
    Try_Compile_Request *req;
} Try_Compile_Project_Parse_State;

typedef struct {
    const Node *node;
    Try_Run_Request *req;
    Cmake_Event_Origin origin;
    SV_List *compile_args;
} Try_Run_Parse_State;

static const Eval_Opt_Spec k_try_compile_source_specs[] = {
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_OUTPUT_VARIABLE, "OUTPUT_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_compile(OUTPUT_VARIABLE) requires an output variable", "Usage: try_compile(... OUTPUT_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_COPY_FILE, "COPY_FILE", EVAL_OPT_SINGLE, 1, true, "try_compile(COPY_FILE) requires a destination path", "Usage: try_compile(... COPY_FILE <path>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_COPY_FILE_ERROR, "COPY_FILE_ERROR", EVAL_OPT_SINGLE, 1, true, "try_compile(COPY_FILE_ERROR) requires an output variable", "Usage: try_compile(... COPY_FILE_ERROR <var>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_CMAKE_FLAGS, "CMAKE_FLAGS", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_COMPILE_DEFINITIONS, "COMPILE_DEFINITIONS", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_LINK_OPTIONS, "LINK_OPTIONS", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_LINK_LIBRARIES, "LINK_LIBRARIES", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_LINKER_LANGUAGE, "LINKER_LANGUAGE", EVAL_OPT_SINGLE, 1, true, "try_compile(LINKER_LANGUAGE) requires a language", "Usage: try_compile(... LINKER_LANGUAGE <lang>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_NO_CACHE, "NO_CACHE", EVAL_OPT_FLAG),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_LOG_DESCRIPTION, "LOG_DESCRIPTION", EVAL_OPT_SINGLE, 1, true, "try_compile(LOG_DESCRIPTION) requires a description", "Usage: try_compile(... LOG_DESCRIPTION <text>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_SOURCES, "SOURCES", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_SOURCES_TYPE, "SOURCES_TYPE", EVAL_OPT_SINGLE, 1, true, "try_compile(SOURCES_TYPE) requires a language selector", "Usage: try_compile(... SOURCES_TYPE <C|CXX|HEADERS>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_SOURCE_FROM_CONTENT, "SOURCE_FROM_CONTENT", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_SOURCE_FROM_VAR, "SOURCE_FROM_VAR", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_SOURCE_FROM_FILE, "SOURCE_FROM_FILE", EVAL_OPT_MULTI),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_C_STANDARD, "C_STANDARD", EVAL_OPT_SINGLE, 1, true, "try_compile(C_STANDARD) requires a standard value", "Usage: try_compile(... C_STANDARD <value>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_C_STANDARD_REQUIRED, "C_STANDARD_REQUIRED", EVAL_OPT_SINGLE, 1, true, "try_compile(C_STANDARD_REQUIRED) requires a boolean value", "Usage: try_compile(... C_STANDARD_REQUIRED <ON|OFF>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_C_EXTENSIONS, "C_EXTENSIONS", EVAL_OPT_SINGLE, 1, true, "try_compile(C_EXTENSIONS) requires a boolean value", "Usage: try_compile(... C_EXTENSIONS <ON|OFF>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_CXX_STANDARD, "CXX_STANDARD", EVAL_OPT_SINGLE, 1, true, "try_compile(CXX_STANDARD) requires a standard value", "Usage: try_compile(... CXX_STANDARD <value>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_CXX_STANDARD_REQUIRED, "CXX_STANDARD_REQUIRED", EVAL_OPT_SINGLE, 1, true, "try_compile(CXX_STANDARD_REQUIRED) requires a boolean value", "Usage: try_compile(... CXX_STANDARD_REQUIRED <ON|OFF>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_SRC_OPT_CXX_EXTENSIONS, "CXX_EXTENSIONS", EVAL_OPT_SINGLE, 1, true, "try_compile(CXX_EXTENSIONS) requires a boolean value", "Usage: try_compile(... CXX_EXTENSIONS <ON|OFF>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_INVALID_PROJECT, "PROJECT", EVAL_OPT_FLAG),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_INVALID_SOURCE_DIR, "SOURCE_DIR", EVAL_OPT_FLAG),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_INVALID_BINARY_DIR, "BINARY_DIR", EVAL_OPT_FLAG),
    EVAL_OPT_SPEC(TRY_COMPILE_SRC_OPT_INVALID_TARGET, "TARGET", EVAL_OPT_FLAG),
};

static const Eval_Opt_Spec k_try_compile_project_specs[] = {
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_PROJECT, "PROJECT", EVAL_OPT_SINGLE, 1, true, "try_compile(PROJECT) requires a project name", "Usage: try_compile(<out-var> PROJECT <name> SOURCE_DIR <dir> ...)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_SOURCE_DIR, "SOURCE_DIR", EVAL_OPT_SINGLE, 1, true, "try_compile(PROJECT ...) requires SOURCE_DIR", "Usage: try_compile(<out-var> PROJECT <name> SOURCE_DIR <dir> ...)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_BINARY_DIR, "BINARY_DIR", EVAL_OPT_SINGLE, 1, true, "try_compile(BINARY_DIR) requires a directory", "Usage: try_compile(... BINARY_DIR <dir>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_TARGET, "TARGET", EVAL_OPT_SINGLE, 1, true, "try_compile(TARGET) requires a target name", "Usage: try_compile(... TARGET <name>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_OUTPUT_VARIABLE, "OUTPUT_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_compile(OUTPUT_VARIABLE) requires an output variable", "Usage: try_compile(... OUTPUT_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_COMPILE_PROJECT_OPT_LOG_DESCRIPTION, "LOG_DESCRIPTION", EVAL_OPT_SINGLE, 1, true, "try_compile(LOG_DESCRIPTION) requires a description", "Usage: try_compile(... LOG_DESCRIPTION <text>)"),
    EVAL_OPT_SPEC(TRY_COMPILE_PROJECT_OPT_NO_CACHE, "NO_CACHE", EVAL_OPT_FLAG),
    EVAL_OPT_SPEC(TRY_COMPILE_PROJECT_OPT_CMAKE_FLAGS, "CMAKE_FLAGS", EVAL_OPT_MULTI),
};

static const Eval_Opt_Spec k_try_run_specs[] = {
    EVAL_OPT_SPEC_REQ(TRY_RUN_OPT_COMPILE_OUTPUT_VARIABLE, "COMPILE_OUTPUT_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_run(COMPILE_OUTPUT_VARIABLE) requires an output variable", "Usage: try_run(... COMPILE_OUTPUT_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_RUN_OPT_RUN_OUTPUT_VARIABLE, "RUN_OUTPUT_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_run(RUN_OUTPUT_VARIABLE) requires an output variable", "Usage: try_run(... RUN_OUTPUT_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_RUN_OPT_RUN_OUTPUT_STDOUT_VARIABLE, "RUN_OUTPUT_STDOUT_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_run(RUN_OUTPUT_STDOUT_VARIABLE) requires an output variable", "Usage: try_run(... RUN_OUTPUT_STDOUT_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_RUN_OPT_RUN_OUTPUT_STDERR_VARIABLE, "RUN_OUTPUT_STDERR_VARIABLE", EVAL_OPT_SINGLE, 1, true, "try_run(RUN_OUTPUT_STDERR_VARIABLE) requires an output variable", "Usage: try_run(... RUN_OUTPUT_STDERR_VARIABLE <var>)"),
    EVAL_OPT_SPEC_REQ(TRY_RUN_OPT_WORKING_DIRECTORY, "WORKING_DIRECTORY", EVAL_OPT_SINGLE, 1, true, "try_run(WORKING_DIRECTORY) requires a path", "Usage: try_run(... WORKING_DIRECTORY <dir>)"),
    EVAL_OPT_SPEC(TRY_RUN_OPT_ARGS, "ARGS", EVAL_OPT_TAIL),
    EVAL_OPT_SPEC(TRY_RUN_OPT_PROJECT, "PROJECT", EVAL_OPT_FLAG),
};

static Eval_Opt_Parse_Config try_compile_opt_cfg(const Node *node,
                                                 Cmake_Event_Origin origin,
                                                 bool unknown_as_positional) {
    Eval_Opt_Parse_Config cfg = {0};
    cfg.origin = origin;
    cfg.component = nob_sv_from_cstr("dispatcher");
    cfg.command = node->as.cmd.name;
    cfg.unknown_as_positional = unknown_as_positional;
    cfg.warn_unknown = false;
    return cfg;
}

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

static bool try_compile_sv_list_append_all(Evaluator_Context *ctx,
                                           SV_List *dst,
                                           SV_List src) {
    if (!ctx || !dst) return false;
    for (size_t i = 0; i < arena_arr_len(src); i++) {
        if (!eval_sv_arr_push_temp(ctx, dst, src[i])) return false;
    }
    return true;
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

static bool try_compile_emit_invalid_source_project_keyword(Evaluator_Context *ctx,
                                                            const Node *node,
                                                            Cmake_Event_Origin origin) {
    return EVAL_DIAG_BOOL_SEV(ctx,
                              EV_DIAG_ERROR,
                              EVAL_DIAG_INVALID_VALUE,
                              nob_sv_from_cstr("dispatcher"),
                              node->as.cmd.name,
                              origin,
                              nob_sv_from_cstr("PROJECT-signature keywords are invalid in try_compile(SOURCE ...)"),
                              nob_sv_from_cstr("Use try_compile(<out-var> PROJECT ... SOURCE_DIR ...)"));
}

static bool try_compile_parse_source_option(Evaluator_Context *ctx,
                                            void *userdata,
                                            int id,
                                            SV_List values,
                                            size_t token_index) {
    (void)token_index;
    Try_Compile_Source_Parse_State *state = (Try_Compile_Source_Parse_State *)userdata;
    if (!state || !state->node || !state->req) return false;

    switch ((Try_Compile_Source_Opt_Id)id) {
        case TRY_COMPILE_SRC_OPT_OUTPUT_VARIABLE:
            state->req->output_var = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_COPY_FILE:
            state->req->copy_file_path = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_COPY_FILE_ERROR:
            state->req->copy_file_error_var = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_CMAKE_FLAGS:
            return try_compile_sv_list_append_all(ctx, &state->req->cmake_flags, values);
        case TRY_COMPILE_SRC_OPT_COMPILE_DEFINITIONS:
            return try_compile_sv_list_append_all(ctx, &state->req->compile_definitions, values);
        case TRY_COMPILE_SRC_OPT_LINK_OPTIONS:
            return try_compile_sv_list_append_all(ctx, &state->req->link_options, values);
        case TRY_COMPILE_SRC_OPT_LINK_LIBRARIES:
            return try_compile_sv_list_append_all(ctx, &state->req->link_libraries, values);
        case TRY_COMPILE_SRC_OPT_LINKER_LANGUAGE:
            state->req->linker_language = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_NO_CACHE:
            state->req->no_cache = true;
            return true;
        case TRY_COMPILE_SRC_OPT_LOG_DESCRIPTION:
            state->req->log_description = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_SOURCES:
            for (size_t i = 0; i < arena_arr_len(values); i++) {
                if (!try_compile_add_source_item(ctx, state->req, values[i], state->current_type)) return false;
            }
            return true;
        case TRY_COMPILE_SRC_OPT_SOURCES_TYPE:
            state->current_type = try_compile_language_from_sources_type(values[0]);
            return true;
        case TRY_COMPILE_SRC_OPT_SOURCE_FROM_CONTENT: {
            if (arena_arr_len(values) == 0) return true;
            String_View content = arena_arr_len(values) > 1
                ? svu_join_space_temp(ctx, &values[1], arena_arr_len(values) - 1)
                : nob_sv_from_cstr("");
            return try_compile_write_generated_source(ctx,
                                                      state->req,
                                                      values[0],
                                                      content,
                                                      state->current_type);
        }
        case TRY_COMPILE_SRC_OPT_SOURCE_FROM_VAR:
            if (arena_arr_len(values) < 2) return true;
            return try_compile_write_generated_source(ctx,
                                                      state->req,
                                                      values[0],
                                                      eval_var_get_visible(ctx, values[1]),
                                                      state->current_type);
        case TRY_COMPILE_SRC_OPT_SOURCE_FROM_FILE: {
            if (arena_arr_len(values) < 2) return true;

            String_View src_file = try_compile_resolve_in_dir(ctx, values[1], state->req->current_src_dir);
            char *src_c = eval_sv_to_cstr_temp(ctx, src_file);
            EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);

            Nob_String_Builder sb = {0};
            if (!nob_read_entire_file(src_c, &sb)) return true;
            String_View content = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
            bool ok = try_compile_write_generated_source(ctx,
                                                         state->req,
                                                         values[0],
                                                         content,
                                                         state->current_type);
            nob_sb_free(sb);
            return ok;
        }
        case TRY_COMPILE_SRC_OPT_C_STANDARD:
            state->req->c_lang.has_value = true;
            state->req->c_lang.standard = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_C_STANDARD_REQUIRED:
            state->req->c_lang.standard_required = !try_compile_is_false(values[0]);
            return true;
        case TRY_COMPILE_SRC_OPT_C_EXTENSIONS:
            state->req->c_lang.extensions_set = true;
            state->req->c_lang.extensions = !try_compile_is_false(values[0]);
            return true;
        case TRY_COMPILE_SRC_OPT_CXX_STANDARD:
            state->req->cxx_lang.has_value = true;
            state->req->cxx_lang.standard = values[0];
            return true;
        case TRY_COMPILE_SRC_OPT_CXX_STANDARD_REQUIRED:
            state->req->cxx_lang.standard_required = !try_compile_is_false(values[0]);
            return true;
        case TRY_COMPILE_SRC_OPT_CXX_EXTENSIONS:
            state->req->cxx_lang.extensions_set = true;
            state->req->cxx_lang.extensions = !try_compile_is_false(values[0]);
            return true;
        case TRY_COMPILE_SRC_OPT_INVALID_PROJECT:
        case TRY_COMPILE_SRC_OPT_INVALID_SOURCE_DIR:
        case TRY_COMPILE_SRC_OPT_INVALID_BINARY_DIR:
        case TRY_COMPILE_SRC_OPT_INVALID_TARGET:
            return try_compile_emit_invalid_source_project_keyword(ctx, state->node, state->origin);
        default:
            return false;
    }
}

static bool try_compile_parse_source_positional(Evaluator_Context *ctx,
                                                void *userdata,
                                                String_View value,
                                                size_t token_index) {
    (void)token_index;
    Try_Compile_Source_Parse_State *state = (Try_Compile_Source_Parse_State *)userdata;
    if (!state || !state->req) return false;
    return try_compile_add_source_item(ctx, state->req, value, state->current_type);
}

static bool try_compile_parse_project_option(Evaluator_Context *ctx,
                                             void *userdata,
                                             int id,
                                             SV_List values,
                                             size_t token_index) {
    (void)token_index;
    Try_Compile_Project_Parse_State *state = (Try_Compile_Project_Parse_State *)userdata;
    if (!state || !state->req) return false;

    switch ((Try_Compile_Project_Opt_Id)id) {
        case TRY_COMPILE_PROJECT_OPT_PROJECT:
            state->req->project_name = values[0];
            return true;
        case TRY_COMPILE_PROJECT_OPT_SOURCE_DIR:
            state->req->source_dir = try_compile_resolve_in_dir(ctx,
                                                                values[0],
                                                                state->req->current_src_dir);
            return true;
        case TRY_COMPILE_PROJECT_OPT_BINARY_DIR:
            state->req->binary_dir = try_compile_resolve_in_dir(ctx,
                                                                values[0],
                                                                state->req->current_bin_dir);
            return true;
        case TRY_COMPILE_PROJECT_OPT_TARGET:
            state->req->target_name = values[0];
            return true;
        case TRY_COMPILE_PROJECT_OPT_OUTPUT_VARIABLE:
            state->req->output_var = values[0];
            return true;
        case TRY_COMPILE_PROJECT_OPT_LOG_DESCRIPTION:
            state->req->log_description = values[0];
            return true;
        case TRY_COMPILE_PROJECT_OPT_NO_CACHE:
            state->req->no_cache = true;
            return true;
        case TRY_COMPILE_PROJECT_OPT_CMAKE_FLAGS:
            return try_compile_sv_list_append_all(ctx, &state->req->cmake_flags, values);
        default:
            return false;
    }
}

static bool try_run_parse_option(Evaluator_Context *ctx,
                                 void *userdata,
                                 int id,
                                 SV_List values,
                                 size_t token_index) {
    (void)token_index;
    Try_Run_Parse_State *state = (Try_Run_Parse_State *)userdata;
    if (!state || !state->node || !state->req) return false;

    switch ((Try_Run_Opt_Id)id) {
        case TRY_RUN_OPT_COMPILE_OUTPUT_VARIABLE:
            state->req->compile_output_var = values[0];
            return true;
        case TRY_RUN_OPT_RUN_OUTPUT_VARIABLE:
            state->req->run_output_var = values[0];
            return true;
        case TRY_RUN_OPT_RUN_OUTPUT_STDOUT_VARIABLE:
            state->req->run_stdout_var = values[0];
            return true;
        case TRY_RUN_OPT_RUN_OUTPUT_STDERR_VARIABLE:
            state->req->run_stderr_var = values[0];
            return true;
        case TRY_RUN_OPT_WORKING_DIRECTORY:
            state->req->working_directory = values[0];
            return true;
        case TRY_RUN_OPT_ARGS:
            return try_compile_sv_list_append_all(ctx, &state->req->run_args, values);
        case TRY_RUN_OPT_PROJECT:
            return EVAL_DIAG_BOOL_SEV(ctx,
                                      EV_DIAG_ERROR,
                                      EVAL_DIAG_INVALID_STATE,
                                      nob_sv_from_cstr("dispatcher"),
                                      state->node->as.cmd.name,
                                      state->origin,
                                      nob_sv_from_cstr("try_run() does not support the PROJECT signature in this batch"),
                                      nob_sv_from_cstr("Use source-file forms only"));
        default:
            return false;
    }
}

static bool try_run_parse_positional(Evaluator_Context *ctx,
                                     void *userdata,
                                     String_View value,
                                     size_t token_index) {
    (void)token_index;
    Try_Run_Parse_State *state = (Try_Run_Parse_State *)userdata;
    if (!state || !state->compile_args) return false;
    return eval_sv_arr_push_temp(ctx, state->compile_args, value);
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
    if (!eval_opt_token_is_keyword((*args)[1],
                                   k_try_compile_source_specs,
                                   NOB_ARRAY_LEN(k_try_compile_source_specs))) {
        out_req->binary_dir = try_compile_resolve_in_dir(ctx, (*args)[1], out_req->current_bin_dir);
        opt_start = 2;
    } else {
        out_req->binary_dir = try_compile_make_scratch_dir(ctx, out_req->current_bin_dir);
    }

    char *bindir_c = eval_sv_to_cstr_temp(ctx, out_req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
    (void)try_compile_mkdir_p_local(ctx, bindir_c);

    Try_Compile_Source_Parse_State state = {
        .node = node,
        .req = out_req,
        .origin = eval_origin_from_node(ctx, node),
        .current_type = TRY_COMPILE_LANG_AUTO,
    };
    if (!eval_opt_parse_walk(ctx,
                             *args,
                             opt_start,
                             k_try_compile_source_specs,
                             NOB_ARRAY_LEN(k_try_compile_source_specs),
                             try_compile_opt_cfg(node, state.origin, true),
                             try_compile_parse_source_option,
                             try_compile_parse_source_positional,
                             &state)) {
        return false;
    }

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

        Try_Compile_Project_Parse_State state = {
            .req = out_req,
        };
        if (!eval_opt_parse_walk(ctx,
                                 *args,
                                 1,
                                 k_try_compile_project_specs,
                                 NOB_ARRAY_LEN(k_try_compile_project_specs),
                                 try_compile_opt_cfg(node, eval_origin_from_node(ctx, node), false),
                                 try_compile_parse_project_option,
                                 NULL,
                                 &state)) {
            return false;
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

    if (try_compile_is_keyword((*args)[2]) ||
        eval_opt_token_is_keyword((*args)[2], k_try_run_specs, NOB_ARRAY_LEN(k_try_run_specs))) {
        return EVAL_DIAG_BOOL_SEV(ctx, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, nob_sv_from_cstr("dispatcher"), node->as.cmd.name, eval_origin_from_node(ctx, node), nob_sv_from_cstr("try_run() requires an explicit binary directory"), nob_sv_from_cstr("Usage: try_run(<run-var> <compile-var> <bindir> <src> ...)"));
    }

    SV_List compile_args = NULL;
    if (!eval_sv_arr_push_temp(ctx, &compile_args, (*args)[1])) return false;

    Try_Run_Parse_State state = {
        .node = node,
        .req = out_req,
        .origin = eval_origin_from_node(ctx, node),
        .compile_args = &compile_args,
    };
    if (!eval_opt_parse_walk(ctx,
                             *args,
                             2,
                             k_try_run_specs,
                             NOB_ARRAY_LEN(k_try_run_specs),
                             try_compile_opt_cfg(node, state.origin, true),
                             try_run_parse_option,
                             try_run_parse_positional,
                             &state)) {
        return false;
    }

    return try_compile_parse_source_request_core(ctx, node, &compile_args, &out_req->compile_req);
}
