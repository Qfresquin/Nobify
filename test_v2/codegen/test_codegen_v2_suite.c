#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_query.h"
#include "build_model_validate.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "nob_codegen.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nob__cmd_append(Nob_Cmd *cmd, size_t n, ...);

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
    if (!parse_arena || !eval_arena || !event_arena || !builder_arena || !validate_arena || !model_arena) return false;

    Ast_Root root = parse_cmake(parse_arena, script);
    Event_Stream *stream = event_stream_create(event_arena);
    if (!root || !stream) return false;

    Evaluator_Init eval_init = {0};
    Event_Stream *build_stream = NULL;
    eval_init.arena = eval_arena;
    eval_init.event_arena = event_arena;
    eval_init.stream = stream;
    eval_init.source_dir = nob_sv_from_cstr(".");
    eval_init.binary_dir = nob_sv_from_cstr(".");
    eval_init.current_file = input_path ? input_path : "CMakeLists.txt";

    Evaluator_Context *eval_ctx = evaluator_create(&eval_init);
    if (!eval_ctx) return false;
    Eval_Result run_result = evaluator_run(eval_ctx, root);
    evaluator_destroy(eval_ctx);
    if (eval_result_is_fatal(run_result) || diag_has_errors()) return false;

    Diag_Sink *sink = bm_diag_sink_create_default(builder_arena);
    BM_Builder *builder = bm_builder_create(builder_arena, sink);
    if (!sink || !builder) return false;
    build_stream = codegen_wrap_stream_with_root(event_arena,
                                                 stream,
                                                 input_path,
                                                 eval_init.source_dir,
                                                 eval_init.binary_dir);
    if (!build_stream) return false;
    if (!bm_builder_apply_stream(builder, build_stream)) return false;

    const Build_Model_Draft *draft = bm_builder_finalize(builder);
    if (!draft) return false;
    if (!bm_validate_draft(draft, validate_arena, sink)) return false;

    const Build_Model *model = bm_freeze_draft(draft, model_arena, sink);
    if (!model) return false;

    *out_model = model;
    *out_parse_arena = parse_arena;
    *out_eval_arena = eval_arena;
    *out_event_arena = event_arena;
    *out_builder_arena = builder_arena;
    *out_validate_arena = validate_arena;
    *out_model_arena = model_arena;
    return true;
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

static bool codegen_render_script(const char *script,
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

static bool codegen_write_script(const char *script,
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

static bool codegen_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
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

static bool codegen_sv_contains(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool codegen_compile_generated_nob(const char *repo_root,
                                          const char *generated_path,
                                          const char *output_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd,
                   "-D_GNU_SOURCE",
                   "-std=c11",
                   "-Wall",
                   "-Wextra",
                   nob_temp_sprintf("-I%s/vendor", repo_root),
                   "-o",
                   output_path,
                   generated_path);
    return nob_cmd_run_sync(cmd);
}

TEST(codegen_simple_executable_generates_compilable_nob) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_executable(app main.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "#define NOB_IMPLEMENTATION") != NULL);
    ASSERT(strstr(output, "#include \"nob.h\"") != NULL);
    ASSERT(strstr(output, "int main(int argc, char **argv)") != NULL);
    ASSERT(strstr(output, "build/app") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_static_interface_alias_usage_propagates_flags) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE inc)\n"
        "target_compile_definitions(iface INTERFACE IFACE=1)\n"
        "target_compile_options(iface INTERFACE -Wshadow)\n"
        "target_link_options(iface INTERFACE -Wl,--as-needed)\n"
        "target_link_directories(iface INTERFACE libs)\n"
        "target_link_libraries(iface INTERFACE m)\n"
        "add_library(core STATIC core.c)\n"
        "target_link_libraries(core PUBLIC iface)\n"
        "add_library(core_alias ALIAS core)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core_alias pthread)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "-Iinc") != NULL);
    ASSERT(strstr(output, "-DIFACE=1") != NULL);
    ASSERT(strstr(output, "-Wshadow") != NULL);
    ASSERT(strstr(output, "-Wl,--as-needed") != NULL);
    ASSERT(strstr(output, "-Llibs") != NULL);
    ASSERT(strstr(output, "-lm") != NULL);
    ASSERT(strstr(output, "-lpthread") != NULL);
    ASSERT(strstr(output, "build/libcore.a") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_output_properties_shape_artifact_paths) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME fancy PREFIX pre_ SUFFIX .pkg ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "artifacts/lib/pre_fancy.pkg") != NULL);
    ASSERT(strstr(output, "artifacts/bin/runner") != NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_write_file_rebases_paths_and_generated_file_compiles) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    ASSERT(arena != NULL);

    ASSERT(codegen_write_script(
        "project(Test C)\n"
        "add_executable(app src/main.c)\n",
        "CMakeLists.txt",
        "generated/out/nob.c"));

    ASSERT(codegen_load_text_file_to_arena(arena, "generated/out/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "../../src/main.c"));
    ASSERT(codegen_compile_generated_nob(s_codegen_repo_root, "generated/out/nob.c", "generated/out/nob_gen"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_rejects_shared_target) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(plugin SHARED plugin.c)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_rejects_imported_target_reference) {
    Nob_String_Builder sb = {0};
    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();
    ASSERT(!codegen_render_script(
        "project(Test C)\n"
        "add_library(ext STATIC IMPORTED)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ext)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));
    nob_sb_free(sb);
    TEST_PASS();
}

void run_codegen_v2_tests(int *passed, int *failed) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "codegen");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "codegen suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "codegen suite: failed to enter isolated workspace");
        (void)test_ws_cleanup(&ws);
        if (failed) (*failed)++;
        return;
    }

    snprintf(s_codegen_repo_root, sizeof(s_codegen_repo_root), "%s/../..", prev_cwd);

    test_codegen_simple_executable_generates_compilable_nob(passed, failed);
    test_codegen_static_interface_alias_usage_propagates_flags(passed, failed);
    test_codegen_output_properties_shape_artifact_paths(passed, failed);
    test_codegen_write_file_rebases_paths_and_generated_file_compiles(passed, failed);
    test_codegen_rejects_shared_target(passed, failed);
    test_codegen_rejects_imported_target_reference(passed, failed);

    if (!test_ws_leave(prev_cwd)) {
        nob_log(NOB_ERROR, "codegen suite: failed to restore cwd");
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "codegen suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
