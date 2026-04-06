#include "test_semantic_pipeline.h"

#include "arena_dyn.h"

#include <string.h>

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
}

static void test_semantic_pipeline_init_event(Event *ev,
                                              Event_Kind kind,
                                              const char *current_file,
                                              size_t line) {
    if (!ev) return;
    memset(ev, 0, sizeof(*ev));
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr(current_file ? current_file : "CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static Test_Semantic_Pipeline_Config test_semantic_pipeline_normalize_config(
    const Test_Semantic_Pipeline_Config *config) {
    Test_Semantic_Pipeline_Config effective = {0};
    test_semantic_pipeline_config_init(&effective);
    if (!config) return effective;

    if (config->current_file) effective.current_file = config->current_file;
    if (config->source_dir.data || config->source_dir.count > 0) effective.source_dir = config->source_dir;
    if (config->binary_dir.data || config->binary_dir.count > 0) effective.binary_dir = config->binary_dir;
    if (config->override_enable_export_host_effects) {
        effective.enable_export_host_effects = config->enable_export_host_effects;
        effective.override_enable_export_host_effects = true;
    }
    if (config->parse_arena_size) effective.parse_arena_size = config->parse_arena_size;
    if (config->scratch_arena_size) effective.scratch_arena_size = config->scratch_arena_size;
    if (config->event_arena_size) effective.event_arena_size = config->event_arena_size;
    if (config->builder_arena_size) effective.builder_arena_size = config->builder_arena_size;
    if (config->validate_arena_size) effective.validate_arena_size = config->validate_arena_size;
    if (config->model_arena_size) effective.model_arena_size = config->model_arena_size;
    return effective;
}

void test_semantic_pipeline_config_init(Test_Semantic_Pipeline_Config *config) {
    if (!config) return;
    *config = (Test_Semantic_Pipeline_Config){
        .current_file = "CMakeLists.txt",
        .source_dir = { .data = ".", .count = 1 },
        .binary_dir = { .data = ".", .count = 1 },
        .enable_export_host_effects = true,
        .override_enable_export_host_effects = false,
        .parse_arena_size = 2 * 1024 * 1024,
        .scratch_arena_size = 8 * 1024 * 1024,
        .event_arena_size = 8 * 1024 * 1024,
        .builder_arena_size = 8 * 1024 * 1024,
        .validate_arena_size = 2 * 1024 * 1024,
        .model_arena_size = 8 * 1024 * 1024,
    };
}

Ast_Root test_semantic_pipeline_parse_cmake(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script ? script : ""));
    Token_List toks = NULL;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return NULL;
    }
    return parse_tokens(arena, toks);
}

Event_Stream *test_semantic_pipeline_wrap_stream_with_root(Arena *arena,
                                                           const Event_Stream *stream,
                                                           const char *current_file,
                                                           String_View source_dir,
                                                           String_View binary_dir) {
    Event_Stream *wrapped = NULL;
    Event ev = {0};
    if (!arena || !stream) return NULL;

    wrapped = event_stream_create(arena);
    if (!wrapped) return NULL;

    test_semantic_pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, current_file, 0);
    ev.as.directory_enter.source_dir = source_dir;
    ev.as.directory_enter.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    for (size_t i = 0; i < stream->count; ++i) {
        if (!event_stream_push(wrapped, &stream->items[i])) return NULL;
    }

    test_semantic_pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, current_file, 0);
    ev.as.directory_leave.source_dir = source_dir;
    ev.as.directory_leave.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    return wrapped;
}

bool test_semantic_pipeline_build_model_from_stream(
    Arena *builder_arena,
    Arena *validate_arena,
    Arena *model_arena,
    const Event_Stream *stream,
    Test_Semantic_Pipeline_Build_Result *out) {
    BM_Builder *builder = NULL;

    if (!builder_arena || !validate_arena || !model_arena || !stream || !out) return false;
    *out = (Test_Semantic_Pipeline_Build_Result){0};

    out->sink = bm_diag_sink_create_default(builder_arena);
    if (!out->sink) return false;

    builder = bm_builder_create(builder_arena, out->sink);
    if (!builder) return false;

    out->builder_ok = bm_builder_apply_stream(builder, stream);
    if (!out->builder_ok) return true;

    out->draft = bm_builder_finalize(builder);
    out->builder_ok = (out->draft != NULL);
    if (!out->builder_ok) return true;

    out->validate_ok = bm_validate_draft(out->draft, validate_arena, out->sink);
    if (!out->validate_ok) return true;

    out->model = bm_freeze_draft(out->draft, model_arena, out->sink);
    out->freeze_ok = (out->model != NULL);
    return true;
}

bool test_semantic_pipeline_fixture_from_script(Test_Semantic_Pipeline_Fixture *out,
                                                const char *script,
                                                const Test_Semantic_Pipeline_Config *config) {
    Test_Semantic_Pipeline_Config effective = test_semantic_pipeline_normalize_config(config);
    EvalSession *session = NULL;
    EvalSession_Config eval_cfg = {0};
    EvalExec_Request eval_request = {0};

    if (!out) return false;
    *out = (Test_Semantic_Pipeline_Fixture){0};

    out->current_file = effective.current_file;
    out->source_dir = effective.source_dir;
    out->binary_dir = effective.binary_dir;

    out->parse_arena = arena_create(effective.parse_arena_size);
    out->scratch_arena = arena_create(effective.scratch_arena_size);
    out->event_arena = arena_create(effective.event_arena_size);
    out->builder_arena = arena_create(effective.builder_arena_size);
    out->validate_arena = arena_create(effective.validate_arena_size);
    out->model_arena = arena_create(effective.model_arena_size);
    if (!out->parse_arena || !out->scratch_arena || !out->event_arena ||
        !out->builder_arena || !out->validate_arena || !out->model_arena) {
        goto fail;
    }

    out->ast = test_semantic_pipeline_parse_cmake(out->parse_arena, script);
    out->stream = event_stream_create(out->event_arena);
    if (!out->stream) goto fail;

    eval_cfg.persistent_arena = out->event_arena;
    eval_cfg.source_root = out->source_dir;
    eval_cfg.binary_root = out->binary_dir;
    eval_cfg.enable_export_host_effects = effective.enable_export_host_effects;

    eval_request.scratch_arena = out->scratch_arena;
    eval_request.source_dir = out->source_dir;
    eval_request.binary_dir = out->binary_dir;
    eval_request.list_file = out->current_file;
    eval_request.stream = out->stream;

    session = eval_session_create(&eval_cfg);
    if (!session) goto fail;

    out->eval_run = eval_session_run(session, &eval_request, out->ast);
    out->eval_ok = !eval_result_is_fatal(out->eval_run.result);
    eval_session_destroy(session);
    session = NULL;

    if (!out->eval_ok) return true;

    out->build_stream = test_semantic_pipeline_wrap_stream_with_root(out->event_arena,
                                                                     out->stream,
                                                                     out->current_file,
                                                                     out->source_dir,
                                                                     out->binary_dir);
    if (!out->build_stream) goto fail;

    if (!test_semantic_pipeline_build_model_from_stream(out->builder_arena,
                                                        out->validate_arena,
                                                        out->model_arena,
                                                        out->build_stream,
                                                        &out->build)) {
        goto fail;
    }

    return true;

fail:
    if (session) eval_session_destroy(session);
    test_semantic_pipeline_fixture_destroy(out);
    return false;
}

void test_semantic_pipeline_fixture_destroy(Test_Semantic_Pipeline_Fixture *fixture) {
    if (!fixture) return;
    arena_destroy(fixture->model_arena);
    arena_destroy(fixture->validate_arena);
    arena_destroy(fixture->builder_arena);
    arena_destroy(fixture->event_arena);
    arena_destroy(fixture->scratch_arena);
    arena_destroy(fixture->parse_arena);
    *fixture = (Test_Semantic_Pipeline_Fixture){0};
}
