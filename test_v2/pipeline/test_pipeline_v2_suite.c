#include "test_v2_assert.h"
#include "test_case_pack.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_query.h"
#include "build_model_validate.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    String_View name;
    String_View script;
} Pipeline_Case;

typedef Pipeline_Case *Pipeline_Case_List;

static void pipeline_init_event(Event *ev, Event_Kind kind, size_t line);

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    return arena_arr_push(arena, *list, token);
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

static bool pipeline_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;

    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View pipeline_normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Pipeline_Case_List *out) {
    return test_case_pack_parse(arena, content, (Test_Case_Pack_Entry**)out);
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

static const char *pipeline_target_type_name(BM_Target_Kind type) {
    switch (type) {
        case BM_TARGET_EXECUTABLE: return "EXECUTABLE";
        case BM_TARGET_STATIC_LIBRARY: return "STATIC_LIB";
        case BM_TARGET_SHARED_LIBRARY: return "SHARED_LIB";
        case BM_TARGET_MODULE_LIBRARY: return "MODULE_LIB";
        case BM_TARGET_INTERFACE_LIBRARY: return "INTERFACE_LIB";
        case BM_TARGET_OBJECT_LIBRARY: return "OBJECT_LIB";
        case BM_TARGET_UTILITY: return "UTILITY";
    }
    return "UNKNOWN";
}

static size_t pipeline_count_non_private_items(BM_String_Item_Span items) {
    size_t count = 0;
    for (size_t i = 0; i < items.count; ++i) {
        if (items.items[i].visibility != BM_VISIBILITY_PRIVATE) count++;
    }
    return count;
}

static BM_Directory_Id pipeline_find_directory_id(const Build_Model *model,
                                                  String_View source_dir,
                                                  String_View binary_dir) {
    size_t count = bm_query_directory_count(model);
    for (size_t i = 0; i < count; ++i) {
        BM_Directory_Id id = (BM_Directory_Id)i;
        if (nob_sv_eq(bm_query_directory_source_dir(model, id), source_dir) &&
            nob_sv_eq(bm_query_directory_binary_dir(model, id), binary_dir)) {
            return id;
        }
    }
    return BM_DIRECTORY_ID_INVALID;
}

static void append_model_snapshot(Nob_String_Builder *sb, const Build_Model *model) {
    size_t target_count = bm_query_target_count(model);
    size_t package_count = bm_query_package_count(model);
    size_t test_count = bm_query_test_count(model);
    size_t install_rule_count = bm_query_install_rule_count(model);
    size_t cpack_group_count = bm_query_cpack_component_group_count(model);
    size_t cpack_type_count = bm_query_cpack_install_type_count(model);
    size_t cpack_component_count = bm_query_cpack_component_count(model);
    BM_Directory_Id root_directory = bm_query_root_directory(model);
    BM_String_Item_Span root_include_dirs = bm_query_directory_include_directories_raw(model, root_directory);
    BM_String_Item_Span root_system_include_dirs = bm_query_directory_system_include_directories_raw(model, root_directory);
    BM_String_Item_Span root_link_dirs = bm_query_directory_link_directories_raw(model, root_directory);
    BM_String_Item_Span global_compile_defs = bm_query_global_compile_definitions_raw(model);
    BM_String_Item_Span global_compile_opts = bm_query_global_compile_options_raw(model);
    BM_String_Item_Span global_link_opts = bm_query_global_link_options_raw(model);
    BM_String_Span global_link_libs = bm_query_global_raw_property_items(model, nob_sv_from_cstr("LINK_LIBRARIES"));

    nob_sb_append_cstr(sb, "MODEL project=");
    snapshot_append_escaped_sv(sb, bm_query_project_name(model));
    nob_sb_append_cstr(sb, nob_temp_sprintf(
        " targets=%zu packages=%zu tests=%zu install_enabled=%d testing_enabled=%d cpack_groups=%zu cpack_types=%zu cpack_components=%zu\n",
        target_count,
        package_count,
        test_count,
        install_rule_count > 0 ? 1 : 0,
        bm_query_testing_enabled(model) ? 1 : 0,
        cpack_group_count,
        cpack_type_count,
        cpack_component_count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "DIR include=%zu system_include=%zu link=%zu\n",
        root_include_dirs.count,
        root_system_include_dirs.count,
        root_link_dirs.count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "GLOBAL compile_defs=%zu compile_opts=%zu link_opts=%zu link_libs=%zu\n",
        global_compile_defs.count,
        global_compile_opts.count,
        global_link_opts.count,
        global_link_libs.count));

    if (target_count > 0) {
        BM_Target_Id target_id = 0;
        BM_String_Span sources = bm_query_target_sources_raw(model, target_id);
        BM_Target_Id_Span deps = bm_query_target_dependencies_explicit(model, target_id);
        BM_String_Item_Span link_libs = bm_query_target_link_libraries_raw(model, target_id);
        BM_String_Item_Span link_opts = bm_query_target_link_options_raw(model, target_id);
        BM_String_Item_Span link_dirs = bm_query_target_link_directories_raw(model, target_id);

        nob_sb_append_cstr(sb, "TARGET0 name=");
        snapshot_append_escaped_sv(sb, bm_query_target_name(model, target_id));
        nob_sb_append_cstr(sb, nob_temp_sprintf(
            " type=%s sources=%zu deps=%zu link_libs=%zu interface_libs=%zu link_opts=%zu link_dirs=%zu\n",
            pipeline_target_type_name(bm_query_target_kind(model, target_id)),
            sources.count,
            deps.count,
            link_libs.count,
            pipeline_count_non_private_items(link_libs),
            link_opts.count,
            link_dirs.count));
    }
}

static Event_Stream *pipeline_wrap_stream_with_root(Arena *arena,
                                                    const Event_Stream *stream,
                                                    const char *current_file,
                                                    String_View source_dir,
                                                    String_View binary_dir) {
    Event_Stream *wrapped = NULL;
    Event ev = {0};
    if (!arena || !stream) return NULL;

    wrapped = event_stream_create(arena);
    if (!wrapped) return NULL;

    pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, 0);
    ev.h.origin.file_path = nob_sv_from_cstr(current_file ? current_file : "CMakeLists.txt");
    ev.as.directory_enter.source_dir = source_dir;
    ev.as.directory_enter.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    for (size_t i = 0; i < stream->count; ++i) {
        if (!event_stream_push(wrapped, &stream->items[i])) return NULL;
    }

    pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, 0);
    ev.h.origin.file_path = nob_sv_from_cstr(current_file ? current_file : "CMakeLists.txt");
    ev.as.directory_leave.source_dir = source_dir;
    ev.as.directory_leave.binary_dir = binary_dir;
    if (!event_stream_push(wrapped, &ev)) return NULL;

    return wrapped;
}

static bool pipeline_snapshot_from_ast(Ast_Root root, const char *current_file, Nob_String_Builder *out_sb) {
    if (!out_sb) return false;

    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(8 * 1024 * 1024);
    Arena *validate_arena = arena_create(2 * 1024 * 1024);
    Arena *model_arena = arena_create(8 * 1024 * 1024);
    if (!temp_arena || !event_arena || !validate_arena || !model_arena) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        arena_destroy(validate_arena);
        arena_destroy(model_arena);
        return false;
    }

    Event_Stream *stream = event_stream_create(event_arena);
    Diag_Sink *sink = bm_diag_sink_create_default(temp_arena);
    Event_Stream *build_stream = NULL;
    if (!stream) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        arena_destroy(validate_arena);
        arena_destroy(model_arena);
        return false;
    }

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = current_file;

    Evaluator_Context *ctx = evaluator_create(&init);
    if (!ctx) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        arena_destroy(validate_arena);
        arena_destroy(model_arena);
        return false;
    }

    Eval_Result eval_result = evaluator_run(ctx, root);
    bool eval_ok = !eval_result_is_fatal(eval_result);
    bool builder_ok = false;
    bool freeze_ok = false;
    const Build_Model *model = NULL;

    if (eval_ok) {
        build_stream = pipeline_wrap_stream_with_root(event_arena,
                                                      stream,
                                                      current_file,
                                                      init.source_dir,
                                                      init.binary_dir);
        Build_Model_Builder *builder = builder_create(temp_arena, sink);
        const Build_Model_Draft *draft = NULL;
        if (builder && build_stream) {
            builder_ok = builder_apply_stream(builder, build_stream);
            if (builder_ok) {
                draft = builder_finalize(builder);
                builder_ok = (draft != NULL);
            }
            if (builder_ok && bm_validate_draft(draft, validate_arena, sink)) {
                model = bm_freeze_draft(draft, model_arena, sink);
                freeze_ok = (model != NULL);
            }
        }
    }

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVAL_OK %d\n", eval_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("BUILDER_OK %d\n", builder_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("FREEZE_OK %d\n", freeze_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", stream->count));
    if (freeze_ok && model) {
        append_model_snapshot(out_sb, model);
    }

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    return true;
}

static bool render_pipeline_case_snapshot_to_sb(Arena *arena,
                                                Pipeline_Case pipeline_case,
                                                Nob_String_Builder *out_sb) {
    diag_reset();
    Ast_Root root = parse_cmake(arena, pipeline_case.script.data);

    Nob_String_Builder case_sb = {0};
    bool ok = pipeline_snapshot_from_ast(root, "CMakeLists.txt", &case_sb);
    if (!ok || case_sb.count == 0) {
        nob_sb_free(case_sb);
        return false;
    }

    nob_sb_append_buf(out_sb, case_sb.items, case_sb.count);
    nob_sb_free(case_sb);
    return true;
}

static bool render_pipeline_casepack_snapshot_to_arena(Arena *arena,
                                                       Pipeline_Case_List cases,
                                                       String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE pipeline\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", arena_arr_len(cases)));

    for (size_t i = 0; i < arena_arr_len(cases); i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases[i].name.data, cases[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_pipeline_case_snapshot_to_sb(arena, cases[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < arena_arr_len(cases)) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_pipeline_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(8 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;

    if (!pipeline_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Pipeline_Case_List cases = NULL;
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (arena_arr_len(cases) != 7) {
        nob_log(NOB_ERROR, "golden: unexpected pipeline case count: got=%zu expected=7", arena_arr_len(cases));
        ok = false;
        goto done;
    }

    if (!render_pipeline_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render pipeline snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = pipeline_normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!pipeline_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = pipeline_normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(actual_norm, expected_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static void pipeline_init_event(Event *ev, Event_Kind kind, size_t line) {
    *ev = (Event){0};
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static const Build_Model *pipeline_freeze_stream(Arena *arena,
                                                 Event_Stream *stream,
                                                 Diag_Sink *sink,
                                                 Arena *validate_arena,
                                                 Arena *model_arena) {
    Build_Model_Builder *builder = builder_create(arena, sink);
    const Build_Model_Draft *draft = NULL;
    if (!builder) return NULL;
    if (!builder_apply_stream(builder, stream)) return NULL;
    draft = builder_finalize(builder);
    if (!draft) return NULL;
    if (!bm_validate_draft(draft, validate_arena, sink)) return NULL;
    return bm_freeze_draft(draft, model_arena, sink);
}

static const char *PIPELINE_GOLDEN_DIR = "test_v2/pipeline/golden";

TEST(pipeline_golden_all_cases) {
    ASSERT(assert_pipeline_golden_casepack(
        nob_temp_sprintf("%s/pipeline_all.cmake", PIPELINE_GOLDEN_DIR),
        nob_temp_sprintf("%s/pipeline_all.txt", PIPELINE_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(pipeline_builder_directory_scope_events) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    Event_Stream *stream = event_stream_create(arena);
    Diag_Sink *sink = bm_diag_sink_create_default(arena);
    ASSERT(stream != NULL);

    Event ev = {0};
    pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, 2);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_PROPERTY_MUTATE, 3);
    ev.as.directory_property_mutate.property_name = nob_sv_from_cstr("INCLUDE_DIRECTORIES");
    ev.as.directory_property_mutate.op = EVENT_PROPERTY_MUTATE_APPEND_LIST;
    ev.as.directory_property_mutate.modifier_flags = EVENT_PROPERTY_MODIFIER_NONE;
    String_View include_items[] = {nob_sv_from_cstr("sub/include")};
    ev.as.directory_property_mutate.items = include_items;
    ev.as.directory_property_mutate.item_count = NOB_ARRAY_LEN(include_items);
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_TARGET_DECLARE, 4);
    ev.as.target_declare.name = nob_sv_from_cstr("sub_lib");
    ev.as.target_declare.target_type = EV_TARGET_LIBRARY_STATIC;
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, 5);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, 6);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    const Build_Model *model = pipeline_freeze_stream(arena, stream, sink, validate_arena, model_arena);
    ASSERT(model != NULL);
    ASSERT(bm_query_directory_count(model) == 2);

    BM_Directory_Id sub_dir = pipeline_find_directory_id(model,
                                                         nob_sv_from_cstr("sub"),
                                                         nob_sv_from_cstr("sub-build"));
    ASSERT(sub_dir != BM_DIRECTORY_ID_INVALID);

    BM_String_Item_Span sub_includes = bm_query_directory_include_directories_raw(model, sub_dir);
    ASSERT(sub_includes.count == 1);
    ASSERT(nob_sv_eq(sub_includes.items[0].value, nob_sv_from_cstr("sub/include")));

    BM_Target_Id target_id = bm_query_target_by_name(model, nob_sv_from_cstr("sub_lib"));
    ASSERT(target_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_owner_directory(model, target_id) == sub_dir);
    ASSERT(bm_query_target_kind(model, target_id) == BM_TARGET_STATIC_LIBRARY);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(pipeline_validate_does_not_infer_link_library_targets) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    Event_Stream *stream = event_stream_create(arena);
    Diag_Sink *sink = bm_diag_sink_create_default(arena);
    ASSERT(stream != NULL);

    Event ev = {0};
    pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_TARGET_DECLARE, 2);
    ev.as.target_declare.name = nob_sv_from_cstr("app");
    ev.as.target_declare.target_type = EV_TARGET_EXECUTABLE;
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_TARGET_LINK_LIBRARIES, 3);
    ev.as.target_link_libraries.target_name = nob_sv_from_cstr("app");
    ev.as.target_link_libraries.visibility = EV_VISIBILITY_PRIVATE;
    ev.as.target_link_libraries.item = nob_sv_from_cstr("MissingTargetLikeName");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, 4);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    const Build_Model *model = pipeline_freeze_stream(arena, stream, sink, validate_arena, model_arena);
    ASSERT(model != NULL);

    BM_Target_Id app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    BM_Target_Id_Span explicit_deps = bm_query_target_dependencies_explicit(model, app_id);
    BM_String_Item_Span raw_link_libs = bm_query_target_link_libraries_raw(model, app_id);
    ASSERT(explicit_deps.count == 0);
    ASSERT(raw_link_libs.count == 1);
    ASSERT(nob_sv_eq(raw_link_libs.items[0].value, nob_sv_from_cstr("MissingTargetLikeName")));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

void run_pipeline_v2_tests(int *passed, int *failed) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "pipeline");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "pipeline suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "pipeline suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_pipeline_golden_all_cases(passed, failed);
    test_pipeline_builder_directory_scope_events(passed, failed);
    test_pipeline_validate_does_not_infer_link_library_targets(passed, failed);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "pipeline suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
