#include "test_v2_assert.h"
#include "test_case_pack.h"
#include "test_semantic_pipeline.h"
#include "test_snapshot_support.h"
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

typedef Test_Case_Pack_Entry Pipeline_Case;
typedef Pipeline_Case *Pipeline_Case_List;

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
    test_snapshot_append_escaped_sv(sb, bm_query_project_name(model));
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
        test_snapshot_append_escaped_sv(sb, bm_query_target_name(model, target_id));
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

static bool pipeline_snapshot_from_script(const char *script,
                                          const char *current_file,
                                          Nob_String_Builder *out_sb) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    bool ok = false;

    if (!out_sb) return false;

    test_semantic_pipeline_config_init(&config);
    config.current_file = current_file ? current_file : "CMakeLists.txt";

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &config);
    if (!ok) return false;

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVAL_OK %d\n", fixture.eval_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("BUILDER_OK %d\n", fixture.build.builder_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("FREEZE_OK %d\n", fixture.build.freeze_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", fixture.stream ? fixture.stream->count : 0));
    if (fixture.build.freeze_ok && fixture.build.model) {
        append_model_snapshot(out_sb, fixture.build.model);
    }

    test_semantic_pipeline_fixture_destroy(&fixture);
    return true;
}

static bool render_pipeline_case_snapshot_to_sb(Arena *arena,
                                                Pipeline_Case pipeline_case,
                                                Nob_String_Builder *out_sb) {
    diag_reset();
    (void)arena;

    Nob_String_Builder case_sb = {0};
    bool ok = pipeline_snapshot_from_script(pipeline_case.script.data, "CMakeLists.txt", &case_sb);
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
    char *text = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_pipeline_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(8 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View actual = {0};
    bool ok = true;

    if (!test_snapshot_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Pipeline_Case_List cases = NULL;
    if (!test_snapshot_parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }

    if (!render_pipeline_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render pipeline snapshot");
        ok = false;
        goto done;
    }

    if (!test_snapshot_assert_golden_output(arena, input_path, expected_path, actual, NULL, NULL)) {
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

void run_pipeline_v2_tests(int *passed, int *failed, int *skipped) {
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

    test_pipeline_golden_all_cases(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "pipeline suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
