#include "test_v2_assert.h"
#include "test_v2_suite.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model.h"
#include "build_model_validate.h"

#include <string.h>
#include <stdlib.h>

typedef struct {
    String_View name;
    String_View script;
} Pipeline_Case;

typedef struct {
    Pipeline_Case *items;
    size_t count;
    size_t capacity;
} Pipeline_Case_List;

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_cmake(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script ? script : ""));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
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

static bool pipeline_case_list_append(Arena *arena, Pipeline_Case_List *list, Pipeline_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Pipeline_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Pipeline_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!pipeline_case_list_append(arena, out, (Pipeline_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) return false;
        }
    }

    return out->count > 0;
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

static const char *target_type_name(Target_Type type) {
    switch (type) {
        case TARGET_EXECUTABLE: return "EXECUTABLE";
        case TARGET_STATIC_LIB: return "STATIC_LIB";
        case TARGET_SHARED_LIB: return "SHARED_LIB";
        case TARGET_OBJECT_LIB: return "OBJECT_LIB";
        case TARGET_INTERFACE_LIB: return "INTERFACE_LIB";
        case TARGET_UTILITY: return "UTILITY";
        case TARGET_IMPORTED: return "IMPORTED";
        case TARGET_ALIAS: return "ALIAS";
    }
    return "UNKNOWN";
}

static void append_model_snapshot(Nob_String_Builder *sb, const Build_Model *model) {
    nob_sb_append_cstr(sb, "MODEL project=");
    snapshot_append_escaped_sv(sb, model->project_name);
    nob_sb_append_cstr(sb, nob_temp_sprintf(
        " targets=%zu packages=%zu tests=%zu install_enabled=%d testing_enabled=%d cpack_groups=%zu cpack_types=%zu cpack_components=%zu\n",
        model->target_count,
        model->package_count,
        model->test_count,
        model->enable_install ? 1 : 0,
        model->enable_testing ? 1 : 0,
        model->cpack_component_group_count,
        model->cpack_install_type_count,
        model->cpack_component_count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "DIR include=%zu system_include=%zu link=%zu\n",
        model->directories.include_dirs.count,
        model->directories.system_include_dirs.count,
        model->directories.link_dirs.count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "GLOBAL compile_defs=%zu compile_opts=%zu link_opts=%zu link_libs=%zu\n",
        model->global_definitions.count,
        model->global_compile_options.count,
        model->global_link_options.count,
        model->global_link_libraries.count));

    if (model->target_count > 0 && model->targets[0]) {
        const Build_Target *t = model->targets[0];
        nob_sb_append_cstr(sb, "TARGET0 name=");
        snapshot_append_escaped_sv(sb, t->name);
        nob_sb_append_cstr(sb, nob_temp_sprintf(
            " type=%s sources=%zu deps=%zu link_libs=%zu interface_libs=%zu link_opts=%zu link_dirs=%zu\n",
            target_type_name(t->type),
            t->sources.count,
            t->dependencies.count,
            t->link_libraries.count,
            t->interface_libs.count,
            t->conditional_link_options.count,
            t->conditional_link_directories.count));
    }
}

static bool pipeline_snapshot_from_ast(Ast_Root root, const char *current_file, Nob_String_Builder *out_sb) {
    if (!out_sb) return false;

    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(8 * 1024 * 1024);
    Arena *model_arena = arena_create(8 * 1024 * 1024);
    if (!temp_arena || !event_arena || !model_arena) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        arena_destroy(model_arena);
        return false;
    }

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    if (!stream) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
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
        arena_destroy(model_arena);
        return false;
    }

    bool eval_ok = evaluator_run(ctx, root);
    bool builder_ok = false;
    bool freeze_ok = false;

    const Build_Model *model = NULL;
    if (eval_ok) {
        Build_Model_Builder *builder = builder_create(event_arena, NULL);
        if (builder) {
            builder_ok = builder_apply_stream(builder, stream);
            if (builder_ok) {
                model = build_model_freeze(builder, model_arena);
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
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_pipeline_case_snapshot_to_sb(arena, cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
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

    Pipeline_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 7) {
        nob_log(NOB_ERROR, "golden: unexpected pipeline case count: got=%zu expected=7", cases.count);
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

static const char *PIPELINE_GOLDEN_DIR = "test_v2/pipeline/golden";

TEST(pipeline_golden_all_cases) {
    ASSERT(assert_pipeline_golden_casepack(
        nob_temp_sprintf("%s/pipeline_all.cmake", PIPELINE_GOLDEN_DIR),
        nob_temp_sprintf("%s/pipeline_all.txt", PIPELINE_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(pipeline_builder_directory_scope_events) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);

    Cmake_Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    Cmake_Event ev = {0};
    ev.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.origin.line = 1;
    ev.origin.col = 1;

    ev.kind = EV_DIR_PUSH;
    ev.as.dir_push.source_dir = nob_sv_from_cstr("sub");
    ev.as.dir_push.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(arena, stream, ev));

    ev.kind = EV_DIRECTORY_INCLUDE_DIRECTORIES;
    ev.as.directory_include_directories.path = nob_sv_from_cstr("sub/include");
    ev.as.directory_include_directories.is_system = false;
    ev.as.directory_include_directories.is_before = false;
    ASSERT(event_stream_push(arena, stream, ev));

    ev.kind = EV_TARGET_DECLARE;
    ev.as.target_declare.name = nob_sv_from_cstr("sub_lib");
    ev.as.target_declare.type = EV_TARGET_LIBRARY_STATIC;
    ASSERT(event_stream_push(arena, stream, ev));

    ev.kind = EV_DIR_POP;
    ASSERT(event_stream_push(arena, stream, ev));

    Build_Model_Builder *builder = builder_create(arena, NULL);
    ASSERT(builder != NULL);
    ASSERT(builder_apply_stream(builder, stream));

    Build_Model *model = builder_finish(builder);
    ASSERT(model != NULL);
    ASSERT(model->directory_node_count >= 2);

    const Build_Directory_Node *sub_node = NULL;
    for (size_t i = 0; i < model->directory_node_count; i++) {
        const Build_Directory_Node *node = &model->directory_nodes[i];
        if (nob_sv_eq(node->source_dir, nob_sv_from_cstr("sub")) &&
            nob_sv_eq(node->binary_dir, nob_sv_from_cstr("sub-build"))) {
            sub_node = node;
            break;
        }
    }
    ASSERT(sub_node != NULL);
    ASSERT(sub_node->include_dirs.count == 1);
    ASSERT(nob_sv_eq(sub_node->include_dirs.items[0], nob_sv_from_cstr("sub/include")));

    Build_Target *target = build_model_find_target(model, nob_sv_from_cstr("sub_lib"));
    ASSERT(target != NULL);
    ASSERT(target->owner_directory_index == sub_node->index);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(pipeline_validate_does_not_infer_link_library_targets) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Build_Model *model = build_model_create(arena);
    ASSERT(model != NULL);
    Build_Target *app = build_model_add_target(model, nob_sv_from_cstr("app"), TARGET_EXECUTABLE);
    ASSERT(app != NULL);

    build_target_add_library(app, arena, nob_sv_from_cstr("MissingTargetLikeName"), VISIBILITY_PRIVATE);
    ASSERT(app->dependencies.count == 0);
    ASSERT(app->link_libraries.count == 1);
    ASSERT(build_model_validate(model, NULL));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(pipeline_builder_custom_command_events) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);

    Cmake_Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    Cmake_Event ev = {0};
    ev.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.origin.line = 1;
    ev.origin.col = 1;

    ev.kind = EV_TARGET_DECLARE;
    ev.as.target_declare.name = nob_sv_from_cstr("gen");
    ev.as.target_declare.type = EV_TARGET_LIBRARY_UNKNOWN;
    ASSERT(event_stream_push(arena, stream, ev));

    ev.kind = EV_CUSTOM_COMMAND_TARGET;
    ev.as.custom_command_target.target_name = nob_sv_from_cstr("gen");
    ev.as.custom_command_target.pre_build = true;
    ev.as.custom_command_target.command = nob_sv_from_cstr("echo gen");
    ev.as.custom_command_target.working_dir = nob_sv_from_cstr("tools");
    ev.as.custom_command_target.comment = nob_sv_from_cstr("gen step");
    ev.as.custom_command_target.outputs = nob_sv_from_cstr("");
    ev.as.custom_command_target.byproducts = nob_sv_from_cstr("out.txt");
    ev.as.custom_command_target.depends = nob_sv_from_cstr("seed.txt");
    ev.as.custom_command_target.main_dependency = nob_sv_from_cstr("");
    ev.as.custom_command_target.depfile = nob_sv_from_cstr("");
    ev.as.custom_command_target.append = false;
    ev.as.custom_command_target.verbatim = true;
    ev.as.custom_command_target.uses_terminal = false;
    ev.as.custom_command_target.command_expand_lists = false;
    ev.as.custom_command_target.depends_explicit_only = false;
    ev.as.custom_command_target.codegen = false;
    ASSERT(event_stream_push(arena, stream, ev));

    ev.kind = EV_CUSTOM_COMMAND_OUTPUT;
    ev.as.custom_command_output.command = nob_sv_from_cstr("python gen.py");
    ev.as.custom_command_output.working_dir = nob_sv_from_cstr("scripts");
    ev.as.custom_command_output.comment = nob_sv_from_cstr("codegen");
    ev.as.custom_command_output.outputs = nob_sv_from_cstr("generated.c;generated.h");
    ev.as.custom_command_output.byproducts = nob_sv_from_cstr("gen.log");
    ev.as.custom_command_output.depends = nob_sv_from_cstr("schema.idl");
    ev.as.custom_command_output.main_dependency = nob_sv_from_cstr("schema.idl");
    ev.as.custom_command_output.depfile = nob_sv_from_cstr("gen.d");
    ev.as.custom_command_output.append = false;
    ev.as.custom_command_output.verbatim = true;
    ev.as.custom_command_output.uses_terminal = false;
    ev.as.custom_command_output.command_expand_lists = false;
    ev.as.custom_command_output.depends_explicit_only = false;
    ev.as.custom_command_output.codegen = true;
    ASSERT(event_stream_push(arena, stream, ev));

    Build_Model_Builder *builder = builder_create(arena, NULL);
    ASSERT(builder != NULL);
    ASSERT(builder_apply_stream(builder, stream));

    Build_Model *model = builder_finish(builder);
    ASSERT(model != NULL);

    Build_Target *target = build_model_find_target(model, nob_sv_from_cstr("gen"));
    ASSERT(target != NULL);
    ASSERT(target->type == TARGET_UTILITY);

    size_t pre_count = 0;
    const Custom_Command *pre_cmds = build_target_get_custom_commands(target, true, &pre_count);
    ASSERT(pre_cmds != NULL);
    ASSERT(pre_count == 1);
    ASSERT(nob_sv_eq(pre_cmds[0].command, nob_sv_from_cstr("echo gen")));
    ASSERT(pre_cmds[0].byproducts.count == 1);

    size_t out_count = 0;
    const Custom_Command *out_cmds = build_model_get_output_custom_commands(model, &out_count);
    ASSERT(out_cmds != NULL);
    ASSERT(out_count == 1);
    ASSERT(out_cmds[0].outputs.count == 2);
    ASSERT(out_cmds[0].depends.count == 1);

    arena_destroy(arena);
    TEST_PASS();
}

void run_pipeline_v2_tests(int *passed, int *failed) {
    test_pipeline_golden_all_cases(passed, failed);
    test_pipeline_builder_directory_scope_events(passed, failed);
    test_pipeline_validate_does_not_infer_link_library_targets(passed, failed);
    test_pipeline_builder_custom_command_events(passed, failed);
}
