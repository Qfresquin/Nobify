#include "test_codegen_v2_support.h"

static char s_codegen_repo_root[_TINYDIR_PATH_MAX] = {0};

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

static void codegen_init_event(Event *ev, Event_Kind kind, size_t line) {
    if (!ev) return;
    memset(ev, 0, sizeof(*ev));
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static Event_Stream *codegen_wrap_stream_with_root(Arena *arena,
                                                   const Event_Stream *stream,
                                                   const char *current_file,
                                                   String_View source_dir,
                                                   String_View binary_dir) {
    Event_Stream *wrapped = NULL;
    Event ev = {0};
    if (!arena || !stream) return NULL;

    wrapped = event_stream_create(arena);
    if (!wrapped) return NULL;

    codegen_init_event(&ev, EVENT_DIRECTORY_ENTER, 0);
    ev.h.origin.file_path = nob_sv_from_cstr(current_file ? current_file : "CMakeLists.txt");
    ev.as.directory_enter.source_dir = source_dir;
    ev.as.directory_enter.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    for (size_t i = 0; i < stream->count; ++i) {
        if (!event_stream_push(wrapped, &stream->items[i])) return NULL;
    }

    codegen_init_event(&ev, EVENT_DIRECTORY_LEAVE, 0);
    ev.h.origin.file_path = nob_sv_from_cstr(current_file ? current_file : "CMakeLists.txt");
    ev.as.directory_leave.source_dir = source_dir;
    ev.as.directory_leave.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    return wrapped;
}

static Ast_Root parse_cmake(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script ? script : ""));
    Token_List toks = NULL;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return NULL;
    }
    return parse_tokens(arena, toks);
}

static bool codegen_build_model_from_script(const char *script,
                                            const char *input_path,
                                            const Build_Model **out_model,
                                            Arena **out_parse_arena,
                                            Arena **out_eval_arena,
                                            Arena **out_event_arena,
                                            Arena **out_builder_arena,
                                            Arena **out_validate_arena,
                                            Arena **out_model_arena) {
    Arena *parse_arena = arena_create(2 * 1024 * 1024);
    Arena *eval_arena = arena_create(8 * 1024 * 1024);
    Arena *event_arena = arena_create(8 * 1024 * 1024);
    Arena *builder_arena = arena_create(8 * 1024 * 1024);
    Arena *validate_arena = arena_create(2 * 1024 * 1024);
    Arena *model_arena = arena_create(8 * 1024 * 1024);
    if (out_model) *out_model = NULL;
    if (out_parse_arena) *out_parse_arena = NULL;
    if (out_eval_arena) *out_eval_arena = NULL;
    if (out_event_arena) *out_event_arena = NULL;
    if (out_builder_arena) *out_builder_arena = NULL;
    if (out_validate_arena) *out_validate_arena = NULL;
    if (out_model_arena) *out_model_arena = NULL;

    if (!parse_arena || !eval_arena || !event_arena || !builder_arena || !validate_arena || !model_arena) {
        goto fail;
    }

    Ast_Root root = parse_cmake(parse_arena, script);
    Event_Stream *stream = event_stream_create(event_arena);
    if (!root || !stream) goto fail;

    EvalSession_Config eval_cfg = {0};
    EvalExec_Request eval_request = {0};
    Event_Stream *build_stream = NULL;
    eval_cfg.persistent_arena = event_arena;
    eval_cfg.source_root = nob_sv_from_cstr(".");
    eval_cfg.binary_root = nob_sv_from_cstr(".");

    eval_request.scratch_arena = eval_arena;
    eval_request.source_dir = nob_sv_from_cstr(".");
    eval_request.binary_dir = nob_sv_from_cstr(".");
    eval_request.list_file = input_path ? input_path : "CMakeLists.txt";
    eval_request.stream = stream;

    EvalSession *eval_session = eval_session_create(&eval_cfg);
    if (!eval_session) goto fail;
    EvalRunResult eval_run = eval_session_run(eval_session, &eval_request, root);
    Eval_Result run_result = eval_run.result;
    eval_session_destroy(eval_session);
    if (eval_result_is_fatal(run_result) || diag_has_errors()) goto fail;

    Diag_Sink *sink = bm_diag_sink_create_default(builder_arena);
    BM_Builder *builder = bm_builder_create(builder_arena, sink);
    if (!sink || !builder) goto fail;
    build_stream = codegen_wrap_stream_with_root(event_arena,
                                                 stream,
                                                 input_path,
                                                 eval_request.source_dir,
                                                 eval_request.binary_dir);
    if (!build_stream) goto fail;
    if (!bm_builder_apply_stream(builder, build_stream)) goto fail;

    const Build_Model_Draft *draft = bm_builder_finalize(builder);
    if (!draft) goto fail;
    if (!bm_validate_draft(draft, validate_arena, sink)) goto fail;

    const Build_Model *model = bm_freeze_draft(draft, model_arena, sink);
    if (!model) goto fail;

    *out_model = model;
    *out_parse_arena = parse_arena;
    *out_eval_arena = eval_arena;
    *out_event_arena = event_arena;
    *out_builder_arena = builder_arena;
    *out_validate_arena = validate_arena;
    *out_model_arena = model_arena;
    return true;

fail:
    arena_destroy(model_arena);
    arena_destroy(validate_arena);
    arena_destroy(builder_arena);
    arena_destroy(event_arena);
    arena_destroy(eval_arena);
    arena_destroy(parse_arena);
    return false;
}

static void codegen_destroy_model_arenas(Arena *parse_arena,
                                         Arena *eval_arena,
                                         Arena *event_arena,
                                         Arena *builder_arena,
                                         Arena *validate_arena,
                                         Arena *model_arena) {
    arena_destroy(model_arena);
    arena_destroy(validate_arena);
    arena_destroy(builder_arena);
    arena_destroy(event_arena);
    arena_destroy(eval_arena);
    arena_destroy(parse_arena);
}

static bool codegen_run_binary(const char *binary_path, const char *arg1, const char *arg2) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!binary_path) return false;
    nob_cmd_append(&cmd, binary_path);
    if (arg1) nob_cmd_append(&cmd, arg1);
    if (arg2) nob_cmd_append(&cmd, arg2);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

void codegen_test_set_repo_root(const char *repo_root) {
    snprintf(s_codegen_repo_root, sizeof(s_codegen_repo_root), "%s", repo_root ? repo_root : "");
}

bool codegen_render_script(const char *script,
                           const char *input_path,
                           const char *output_path,
                           Nob_String_Builder *out) {
    const Build_Model *model = NULL;
    Arena *parse_arena = NULL;
    Arena *eval_arena = NULL;
    Arena *event_arena = NULL;
    Arena *builder_arena = NULL;
    Arena *validate_arena = NULL;
    Arena *model_arena = NULL;
    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    bool ok = false;
    if (!codegen_arena) return false;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    ok = codegen_build_model_from_script(script,
                                         input_path,
                                         &model,
                                         &parse_arena,
                                         &eval_arena,
                                         &event_arena,
                                         &builder_arena,
                                         &validate_arena,
                                         &model_arena);
    if (!ok) {
        arena_destroy(codegen_arena);
        return false;
    }

    Nob_Codegen_Options opts = {
        .input_path = nob_sv_from_cstr(input_path),
        .output_path = nob_sv_from_cstr(output_path),
    };
    ok = nob_codegen_render(model, codegen_arena, &opts, out);

    arena_destroy(codegen_arena);
    codegen_destroy_model_arenas(parse_arena, eval_arena, event_arena, builder_arena, validate_arena, model_arena);
    return ok;
}

bool codegen_write_script(const char *script,
                          const char *input_path,
                          const char *output_path) {
    const Build_Model *model = NULL;
    Arena *parse_arena = NULL;
    Arena *eval_arena = NULL;
    Arena *event_arena = NULL;
    Arena *builder_arena = NULL;
    Arena *validate_arena = NULL;
    Arena *model_arena = NULL;
    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    bool ok = false;
    if (!codegen_arena) return false;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    ok = codegen_build_model_from_script(script,
                                         input_path,
                                         &model,
                                         &parse_arena,
                                         &eval_arena,
                                         &event_arena,
                                         &builder_arena,
                                         &validate_arena,
                                         &model_arena);
    if (!ok) {
        arena_destroy(codegen_arena);
        return false;
    }

    Nob_Codegen_Options opts = {
        .input_path = nob_sv_from_cstr(input_path),
        .output_path = nob_sv_from_cstr(output_path),
    };
    ok = nob_codegen_write_file(model, codegen_arena, &opts);

    arena_destroy(codegen_arena);
    codegen_destroy_model_arenas(parse_arena, eval_arena, event_arena, builder_arena, validate_arena, model_arena);
    return ok;
}

bool codegen_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !path || !out) return false;
    if (!nob_read_entire_file(path, &sb)) return false;
    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool codegen_sv_contains(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

bool codegen_compile_generated_nob(const char *generated_path, const char *output_path) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (s_codegen_repo_root[0] == '\0' || !generated_path || !output_path) return false;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd,
                   "-D_GNU_SOURCE",
                   "-std=c11",
                   "-Wall",
                   "-Wextra",
                   nob_temp_sprintf("-I%s/vendor", s_codegen_repo_root),
                   "-o",
                   output_path,
                   generated_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool codegen_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !nob_mkdir_if_not_exists(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

bool codegen_run_binary_in_dir(const char *dir,
                               const char *binary_path,
                               const char *arg1,
                               const char *arg2) {
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    ok = codegen_run_binary(binary_path, arg1, arg2);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}
