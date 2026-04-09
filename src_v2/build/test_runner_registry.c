#define TEST_RUNNER_WATCH_COMMON \
    "src_v2/build", \
    "test_v2/test_v2_assert.c", \
    "test_v2/test_v2_assert.h", \
    "test_v2/test_v2_suite.h"

#define TEST_RUNNER_WATCH_WORKSPACE \
    "test_v2/test_workspace.c", \
    "test_v2/test_workspace.h", \
    "test_v2/test_fs.h"

#define TEST_RUNNER_WATCH_SNAPSHOT \
    "test_v2/test_snapshot_support.c", \
    "test_v2/test_snapshot_support.h", \
    "test_v2/test_case_pack.h"

#define TEST_RUNNER_WATCH_HOST_FIXTURE \
    "test_v2/test_host_fixture_support.c", \
    "test_v2/test_host_fixture_support.h"

#define TEST_RUNNER_WATCH_MANIFEST \
    "test_v2/test_manifest_support.c", \
    "test_v2/test_manifest_support.h"

#define TEST_RUNNER_WATCH_SEMANTIC_PIPELINE \
    "test_v2/test_semantic_pipeline.c", \
    "test_v2/test_semantic_pipeline.h"

#define TEST_RUNNER_WATCH_CASE_DSL \
    "test_v2/test_case_dsl.c", \
    "test_v2/test_case_dsl.h"

static void append_v2_pcre_sources(Nob_Cmd *cmd) {
#ifdef _WIN32
    nob_cmd_append(cmd,
                   "vendor/pcre/pcre2_auto_possess.c",
                   "vendor/pcre/pcre2_chkdint.c",
                   "vendor/pcre/pcre2_chartables.c",
                   "vendor/pcre/pcre2_compile.c",
                   "vendor/pcre/pcre2_compile_cgroup.c",
                   "vendor/pcre/pcre2_compile_class.c",
                   "vendor/pcre/pcre2_config.c",
                   "vendor/pcre/pcre2_context.c",
                   "vendor/pcre/pcre2_convert.c",
                   "vendor/pcre/pcre2_dfa_match.c",
                   "vendor/pcre/pcre2_error.c",
                   "vendor/pcre/pcre2_extuni.c",
                   "vendor/pcre/pcre2_find_bracket.c",
                   "vendor/pcre/pcre2_maketables.c",
                   "vendor/pcre/pcre2_match.c",
                   "vendor/pcre/pcre2_match_data.c",
                   "vendor/pcre/pcre2_match_next.c",
                   "vendor/pcre/pcre2_newline.c",
                   "vendor/pcre/pcre2_ord2utf.c",
                   "vendor/pcre/pcre2_pattern_info.c",
                   "vendor/pcre/pcre2_script_run.c",
                   "vendor/pcre/pcre2_serialize.c",
                   "vendor/pcre/pcre2_string_utils.c",
                   "vendor/pcre/pcre2_study.c",
                   "vendor/pcre/pcre2_substitute.c",
                   "vendor/pcre/pcre2_substring.c",
                   "vendor/pcre/pcre2_tables.c",
                   "vendor/pcre/pcre2_ucd.c",
                   "vendor/pcre/pcre2_valid_utf.c",
                   "vendor/pcre/pcre2_xclass.c",
                   "vendor/pcre/pcre2posix.c");
#else
    (void)cmd;
#endif
}

static void append_v2_evaluator_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "src_v2/arena/arena.c",
                   "src_v2/lexer/lexer.c",
                   "src_v2/parser/parser.c",
                   "src_v2/diagnostics/diagnostics.c",
                   "src_v2/transpiler/event_ir.c",
                   "src_v2/build_model/bm_compile_features.c",
                   "src_v2/genex/genex.c",
                   "src_v2/evaluator/stb_ds_impl.c",
                   "src_v2/evaluator/eval_exec_core.c",
                   "src_v2/evaluator/eval_nested_exec.c",
                   "src_v2/evaluator/eval_user_command.c",
                   "src_v2/evaluator/evaluator.c",
                   "src_v2/evaluator/eval_cpack.c",
                   "src_v2/evaluator/eval_cmake_path.c",
                   "src_v2/evaluator/eval_cmake_path_utils.c",
                   "src_v2/evaluator/eval_custom.c",
                   "src_v2/evaluator/eval_ctest.c",
                   "src_v2/evaluator/eval_directory.c",
                   "src_v2/evaluator/eval_diag.c",
                   "src_v2/evaluator/eval_diag_classify.c",
                   "src_v2/evaluator/eval_dispatcher.c",
                   "src_v2/evaluator/eval_command_caps.c",
                   "src_v2/evaluator/eval_expr.c",
                   "src_v2/evaluator/eval_fetchcontent.c",
                   "src_v2/evaluator/eval_hash.c",
                   "src_v2/evaluator/eval_file.c",
                   "src_v2/evaluator/eval_file_path.c",
                   "src_v2/evaluator/eval_file_glob.c",
                   "src_v2/evaluator/eval_file_rw.c",
                   "src_v2/evaluator/eval_file_copy.c",
                   "src_v2/evaluator/eval_file_extra.c",
                   "src_v2/evaluator/eval_file_runtime_deps.c",
                   "src_v2/evaluator/eval_file_fsops.c",
                   "src_v2/evaluator/eval_file_backend_curl.c",
                   "src_v2/evaluator/eval_file_backend_archive.c",
                   "src_v2/evaluator/eval_file_transfer.c",
                   "src_v2/evaluator/eval_file_generate_lock_archive.c",
                   "src_v2/evaluator/eval_flow.c",
                   "src_v2/evaluator/eval_flow_block.c",
                   "src_v2/evaluator/eval_flow_cmake_language.c",
                   "src_v2/evaluator/eval_flow_process.c",
                   "src_v2/evaluator/eval_host.c",
                   "src_v2/evaluator/eval_include.c",
                   "src_v2/evaluator/eval_install.c",
                   "src_v2/evaluator/eval_legacy.c",
                   "src_v2/evaluator/eval_meta.c",
                   "src_v2/evaluator/eval_opt_parser.c",
                   "src_v2/evaluator/eval_package_find_item.c",
                   "src_v2/evaluator/eval_package.c",
                   "src_v2/evaluator/eval_property.c",
                   "src_v2/evaluator/eval_project.c",
                   "src_v2/evaluator/eval_list.c",
                   "src_v2/evaluator/eval_list_helpers.c",
                   "src_v2/evaluator/eval_math.c",
                   "src_v2/evaluator/eval_compat.c",
                   "src_v2/evaluator/eval_policy_engine.c",
                   "src_v2/evaluator/eval_report.c",
                   "src_v2/evaluator/eval_runtime_process.c",
                   "src_v2/evaluator/eval_string_text.c",
                   "src_v2/evaluator/eval_string_regex.c",
                   "src_v2/evaluator/eval_string_json.c",
                   "src_v2/evaluator/eval_string_misc.c",
                   "src_v2/evaluator/eval_string.c",
                   "src_v2/evaluator/eval_target_property_query.c",
                   "src_v2/evaluator/eval_target_usage.c",
                   "src_v2/evaluator/eval_target_source_group.c",
                   "src_v2/evaluator/eval_target.c",
                   "src_v2/evaluator/eval_test.c",
                   "src_v2/evaluator/eval_try_compile.c",
                   "src_v2/evaluator/eval_try_compile_parse.c",
                   "src_v2/evaluator/eval_try_compile_exec.c",
                   "src_v2/evaluator/eval_try_run.c",
                   "src_v2/evaluator/eval_utils.c",
                   "src_v2/evaluator/eval_utils_path.c",
                   "src_v2/evaluator/eval_vars.c",
                   "src_v2/evaluator/eval_vars_parse.c");
}

static void append_v2_build_model_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "src_v2/build_model/build_model_builder.c",
                   "src_v2/build_model/build_model_builder_directory.c",
                   "src_v2/build_model/build_model_builder_install.c",
                   "src_v2/build_model/build_model_builder_export.c",
                   "src_v2/build_model/build_model_builder_package.c",
                   "src_v2/build_model/build_model_builder_project.c",
                   "src_v2/build_model/build_model_builder_target.c",
                   "src_v2/build_model/build_model_builder_test.c",
                   "src_v2/build_model/build_model_freeze.c",
                   "src_v2/build_model/build_model_query.c",
                   "src_v2/build_model/build_model_validate.c",
                   "src_v2/build_model/build_model_validate_cycles.c");
}

static void append_v2_codegen_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "src_v2/codegen/nob_codegen.c",
                   "src_v2/codegen/nob_codegen_steps.c");
}

static void append_v2_parser_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "src_v2/arena/arena.c",
                   "src_v2/lexer/lexer.c",
                   "src_v2/parser/parser.c",
                   "src_v2/diagnostics/diagnostics.c");
}

static void append_v2_lexer_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "src_v2/arena/arena.c",
                   "src_v2/lexer/lexer.c");
}

static void append_v2_arena_runtime_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "src_v2/arena/arena.c");
}

static void append_v2_arena_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/arena/test_arena_v2_main.c",
                   "test_v2/arena/test_arena_v2_suite.c");
}

static void append_v2_lexer_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/lexer/test_lexer_v2_main.c",
                   "test_v2/lexer/test_lexer_v2_suite.c");
}

static void append_v2_parser_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/parser/test_parser_v2_main.c",
                   "test_v2/parser/test_parser_v2_suite.c");
}

static void append_v2_build_model_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_semantic_pipeline.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_workspace.c",
                   "test_v2/build_model/test_build_model_v2_main.c",
                   "test_v2/build_model/test_build_model_v2_suite.c");
}

static void append_v2_nobify_app_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "src_v2/app/nobify.c");
}

static void append_v2_evaluator_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/evaluator/test_evaluator_v2_support.c",
                   "test_v2/evaluator/test_evaluator_v2_main.c",
                   "test_v2/evaluator/test_evaluator_v2_suite.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch1.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch2.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch3.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch4.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch5.c");
}

static void append_v2_evaluator_diff_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_case_dsl.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/evaluator/test_evaluator_v2_support.c",
                   "test_v2/evaluator_diff/test_evaluator_diff_v2_main.c",
                   "test_v2/evaluator_diff/test_evaluator_diff_v2_suite.c");
}

static void append_v2_evaluator_codegen_diff_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_case_dsl.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_semantic_pipeline.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/evaluator/test_evaluator_v2_support.c",
                   "test_v2/codegen/test_codegen_v2_support.c",
                   "test_v2/evaluator_codegen_diff/test_evaluator_codegen_diff_v2_main.c",
                   "test_v2/evaluator_codegen_diff/test_evaluator_codegen_diff_v2_suite.c");
}

static void append_v2_evaluator_integration_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/evaluator/test_evaluator_v2_support.c",
                   "test_v2/evaluator/test_evaluator_v2_integration_main.c",
                   "test_v2/evaluator/test_evaluator_v2_suite.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch1.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch2.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch3.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch4.c",
                   "test_v2/evaluator/test_evaluator_v2_suite_batch5.c");
}

static void append_v2_pipeline_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_semantic_pipeline.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/pipeline/test_pipeline_v2_main.c",
                   "test_v2/pipeline/test_pipeline_v2_suite.c");
}

static void append_v2_codegen_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_semantic_pipeline.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_workspace.c",
                   "test_v2/codegen/test_codegen_v2_support.c",
                   "test_v2/codegen/test_codegen_v2_main.c",
                   "test_v2/codegen/test_codegen_v2_suite.c",
                   "test_v2/codegen/test_codegen_v2_suite_render.c",
                   "test_v2/codegen/test_codegen_v2_suite_build.c",
                   "test_v2/codegen/test_codegen_v2_suite_reject.c");
}

static void append_v2_artifact_parity_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_semantic_pipeline.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/artifact_parity/test_artifact_parity_v2_support.c",
                   "test_v2/artifact_parity/test_artifact_parity_v2_main.c",
                   "test_v2/artifact_parity/test_artifact_parity_v2_suite.c");
}

static void append_v2_artifact_parity_corpus_test_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
                   "test_v2/test_host_fixture_support.c",
                   "test_v2/test_manifest_support.c",
                   "test_v2/test_v2_assert.c",
                   "test_v2/test_snapshot_support.c",
                   "test_v2/test_workspace.c",
                   "test_v2/artifact_parity/test_artifact_parity_v2_support.c",
                   "test_v2/artifact_parity/test_artifact_parity_corpus_v2_main.c",
                   "test_v2/artifact_parity/test_artifact_parity_corpus_v2_suite.c");
}

static void append_test_arena_all_sources(Nob_Cmd *cmd) {
    append_v2_arena_test_sources(cmd);
    append_v2_arena_runtime_sources(cmd);
}

static void append_test_lexer_all_sources(Nob_Cmd *cmd) {
    append_v2_lexer_test_sources(cmd);
    append_v2_lexer_runtime_sources(cmd);
}

static void append_test_parser_all_sources(Nob_Cmd *cmd) {
    append_v2_parser_test_sources(cmd);
    append_v2_parser_runtime_sources(cmd);
}

static void append_test_build_model_all_sources(Nob_Cmd *cmd) {
    append_v2_build_model_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_evaluator_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_evaluator_diff_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_diff_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_evaluator_codegen_diff_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_codegen_diff_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_evaluator_integration_all_sources(Nob_Cmd *cmd) {
    append_v2_evaluator_integration_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_pipeline_all_sources(Nob_Cmd *cmd) {
    append_v2_pipeline_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_codegen_all_sources(Nob_Cmd *cmd) {
    append_v2_codegen_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_artifact_parity_all_sources(Nob_Cmd *cmd) {
    append_v2_artifact_parity_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_artifact_parity_corpus_all_sources(Nob_Cmd *cmd) {
    append_v2_artifact_parity_corpus_test_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static void append_test_nobify_all_sources(Nob_Cmd *cmd) {
    append_v2_nobify_app_sources(cmd);
    append_v2_evaluator_runtime_sources(cmd);
    append_v2_build_model_runtime_sources(cmd);
    append_v2_codegen_runtime_sources(cmd);
    append_v2_pcre_sources(cmd);
}

static const char *const TEST_RUNNER_ARENA_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    "src_v2/arena",
    "test_v2/arena",
};

static const char *const TEST_RUNNER_LEXER_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    "src_v2/arena",
    "src_v2/lexer",
    "test_v2/lexer",
};

static const char *const TEST_RUNNER_PARSER_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "test_v2/parser",
};

static const char *const TEST_RUNNER_BUILD_MODEL_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SEMANTIC_PIPELINE,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/genex",
    "test_v2/build_model",
};

static const char *const TEST_RUNNER_EVALUATOR_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/genex",
    "test_v2/evaluator",
};

static const char *const TEST_RUNNER_EVALUATOR_DIFF_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    TEST_RUNNER_WATCH_CASE_DSL,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/genex",
    "test_v2/evaluator",
    "test_v2/evaluator_diff",
};

static const char *const TEST_RUNNER_EVALUATOR_CODEGEN_DIFF_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    TEST_RUNNER_WATCH_CASE_DSL,
    TEST_RUNNER_WATCH_SEMANTIC_PIPELINE,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/codegen",
    "src_v2/genex",
    "test_v2/evaluator",
    "test_v2/codegen",
    "test_v2/evaluator_codegen_diff",
};

static const char *const TEST_RUNNER_EVALUATOR_INTEGRATION_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/genex",
    "test_v2/evaluator",
};

static const char *const TEST_RUNNER_PIPELINE_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_SEMANTIC_PIPELINE,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/genex",
    "test_v2/pipeline",
};

static const char *const TEST_RUNNER_CODEGEN_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    TEST_RUNNER_WATCH_SEMANTIC_PIPELINE,
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/codegen",
    "src_v2/genex",
    "test_v2/codegen",
};

static const char *const TEST_RUNNER_ARTIFACT_PARITY_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    TEST_RUNNER_WATCH_SEMANTIC_PIPELINE,
    "src_v2/app",
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/codegen",
    "src_v2/genex",
    "test_v2/artifact_parity",
};

static const char *const TEST_RUNNER_ARTIFACT_PARITY_CORPUS_WATCH_ROOTS[] = {
    TEST_RUNNER_WATCH_COMMON,
    TEST_RUNNER_WATCH_WORKSPACE,
    TEST_RUNNER_WATCH_SNAPSHOT,
    TEST_RUNNER_WATCH_HOST_FIXTURE,
    TEST_RUNNER_WATCH_MANIFEST,
    "src_v2/app",
    "src_v2/arena",
    "src_v2/lexer",
    "src_v2/parser",
    "src_v2/diagnostics",
    "src_v2/transpiler",
    "src_v2/build_model",
    "src_v2/evaluator",
    "src_v2/codegen",
    "src_v2/genex",
    "test_v2/artifact_parity",
};

static const Test_Runner_Profile_Internal TEST_RUNNER_PROFILES[] = {
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_DEFAULT,
            .name = "default",
            .legacy_suffix = "",
            .front_door_flag = NULL,
        },
        .use_asan = false,
        .use_ubsan = false,
        .use_msan = false,
        .use_coverage = false,
        .asan_options_default = NULL,
        .ubsan_options_default = NULL,
        .msan_options_default = NULL,
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_FAST,
            .name = "fast",
            .legacy_suffix = NULL,
            .front_door_flag = NULL,
        },
        .use_asan = false,
        .use_ubsan = false,
        .use_msan = false,
        .use_coverage = false,
        .asan_options_default = NULL,
        .ubsan_options_default = NULL,
        .msan_options_default = NULL,
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_ASAN_UBSAN,
            .name = "asan_ubsan",
            .legacy_suffix = "-san",
            .front_door_flag = "--san",
        },
        .use_asan = true,
        .use_ubsan = true,
        .use_msan = false,
        .use_coverage = false,
        .asan_options_default = "detect_leaks=1:detect_stack_use_after_return=1:abort_on_error=1:symbolize=1",
        .ubsan_options_default = "print_stacktrace=1:halt_on_error=1",
        .msan_options_default = NULL,
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_ASAN,
            .name = "asan",
            .legacy_suffix = "-asan",
            .front_door_flag = "--asan",
        },
        .use_asan = true,
        .use_ubsan = false,
        .use_msan = false,
        .use_coverage = false,
        .asan_options_default = "detect_leaks=1:detect_stack_use_after_return=1:abort_on_error=1:symbolize=1",
        .ubsan_options_default = NULL,
        .msan_options_default = NULL,
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_UBSAN,
            .name = "ubsan",
            .legacy_suffix = "-ubsan",
            .front_door_flag = "--ubsan",
        },
        .use_asan = false,
        .use_ubsan = true,
        .use_msan = false,
        .use_coverage = false,
        .asan_options_default = NULL,
        .ubsan_options_default = "print_stacktrace=1:halt_on_error=1",
        .msan_options_default = NULL,
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_MSAN,
            .name = "msan",
            .legacy_suffix = "-msan",
            .front_door_flag = "--msan",
        },
        .use_asan = false,
        .use_ubsan = false,
        .use_msan = true,
        .use_coverage = false,
        .asan_options_default = NULL,
        .ubsan_options_default = NULL,
        .msan_options_default = "abort_on_error=1:symbolize=1:track_origins=2:poison_in_dtor=1",
    },
    {
        .def = {
            .id = TEST_RUNNER_PROFILE_COVERAGE,
            .name = "coverage",
            .legacy_suffix = "-cov",
            .front_door_flag = "--cov",
        },
        .use_asan = false,
        .use_ubsan = false,
        .use_msan = false,
        .use_coverage = true,
        .asan_options_default = NULL,
        .ubsan_options_default = NULL,
        .msan_options_default = NULL,
    },
};

static const Test_Runner_Module_Internal TEST_RUNNER_MODULES[] = {
    {
        .def = {
            .id = TEST_RUNNER_MODULE_ARENA,
            .name = "arena",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_ARENA_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_ARENA_WATCH_ROOTS),
        },
        .append_sources = append_test_arena_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_LEXER,
            .name = "lexer",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_LEXER_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_LEXER_WATCH_ROOTS),
        },
        .append_sources = append_test_lexer_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_PARSER,
            .name = "parser",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_PARSER_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_PARSER_WATCH_ROOTS),
        },
        .append_sources = append_test_parser_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_BUILD_MODEL,
            .name = "build-model",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_BUILD_MODEL_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_BUILD_MODEL_WATCH_ROOTS),
        },
        .append_sources = append_test_build_model_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_EVALUATOR,
            .name = "evaluator",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_EVALUATOR_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_EVALUATOR_WATCH_ROOTS),
        },
        .append_sources = append_test_evaluator_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_EVALUATOR_DIFF,
            .name = "evaluator-diff",
            .include_in_aggregate = false,
            .explicit_heavy = true,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_EVALUATOR_DIFF_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_EVALUATOR_DIFF_WATCH_ROOTS),
        },
        .append_sources = append_test_evaluator_diff_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_EVALUATOR_CODEGEN_DIFF,
            .name = "evaluator-codegen-diff",
            .include_in_aggregate = false,
            .explicit_heavy = true,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_EVALUATOR_CODEGEN_DIFF_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_EVALUATOR_CODEGEN_DIFF_WATCH_ROOTS),
        },
        .append_sources = append_test_evaluator_codegen_diff_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_EVALUATOR_INTEGRATION,
            .name = "evaluator-integration",
            .include_in_aggregate = false,
            .explicit_heavy = true,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_EVALUATOR_INTEGRATION_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_EVALUATOR_INTEGRATION_WATCH_ROOTS),
        },
        .append_sources = append_test_evaluator_integration_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_PIPELINE,
            .name = "pipeline",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_PIPELINE_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_PIPELINE_WATCH_ROOTS),
        },
        .append_sources = append_test_pipeline_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_CODEGEN,
            .name = "codegen",
            .include_in_aggregate = true,
            .explicit_heavy = false,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_CODEGEN_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_CODEGEN_WATCH_ROOTS),
        },
        .append_sources = append_test_codegen_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_ARTIFACT_PARITY,
            .name = "artifact-parity",
            .include_in_aggregate = false,
            .explicit_heavy = true,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_ARTIFACT_PARITY_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_ARTIFACT_PARITY_WATCH_ROOTS),
        },
        .append_sources = append_test_artifact_parity_all_sources,
    },
    {
        .def = {
            .id = TEST_RUNNER_MODULE_ARTIFACT_PARITY_CORPUS,
            .name = "artifact-parity-corpus",
            .include_in_aggregate = false,
            .explicit_heavy = true,
            .default_local_profile = TEST_RUNNER_PROFILE_FAST,
            .watch_auto_eligible = true,
            .watch_roots = TEST_RUNNER_ARTIFACT_PARITY_CORPUS_WATCH_ROOTS,
            .watch_root_count = NOB_ARRAY_LEN(TEST_RUNNER_ARTIFACT_PARITY_CORPUS_WATCH_ROOTS),
        },
        .append_sources = append_test_artifact_parity_corpus_all_sources,
    },
};

size_t test_runner_module_count(void) {
    return NOB_ARRAY_LEN(TEST_RUNNER_MODULES);
}

size_t test_runner_profile_count(void) {
    return NOB_ARRAY_LEN(TEST_RUNNER_PROFILES);
}

const Test_Runner_Module_Def *test_runner_get_module_def(Test_Runner_Module_Id id) {
    if ((size_t)id >= NOB_ARRAY_LEN(TEST_RUNNER_MODULES)) return NULL;
    return &TEST_RUNNER_MODULES[id].def;
}

const Test_Runner_Profile_Def *test_runner_get_profile_def(Test_Runner_Profile_Id id) {
    if ((size_t)id >= NOB_ARRAY_LEN(TEST_RUNNER_PROFILES)) return NULL;
    return &TEST_RUNNER_PROFILES[id].def;
}

static const Test_Runner_Module_Internal *find_test_module_internal(const char *name) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(TEST_RUNNER_MODULES); ++i) {
        if (cstr_equals(TEST_RUNNER_MODULES[i].def.name, name)) return &TEST_RUNNER_MODULES[i];
    }
    return NULL;
}

const Test_Runner_Module_Def *test_runner_find_module_def_by_name(const char *name) {
    const Test_Runner_Module_Internal *module = find_test_module_internal(name);
    return module ? &module->def : NULL;
}

static const Test_Runner_Profile_Internal *find_test_profile_by_front_door_flag(const char *flag) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(TEST_RUNNER_PROFILES); ++i) {
        if (cstr_equals(TEST_RUNNER_PROFILES[i].def.front_door_flag, flag)) return &TEST_RUNNER_PROFILES[i];
    }
    return NULL;
}
