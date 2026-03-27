#define g_evaluator_captured_nob_logs evaluator_support_impl_g_evaluator_captured_nob_logs
#define evaluator_begin_nob_log_capture evaluator_support_impl_evaluator_begin_nob_log_capture
#define evaluator_end_nob_log_capture evaluator_support_impl_evaluator_end_nob_log_capture
#define evaluator_set_source_date_epoch_value evaluator_support_impl_evaluator_set_source_date_epoch_value
#define evaluator_prepare_symlink_escape_fixture evaluator_support_impl_evaluator_prepare_symlink_escape_fixture
#define evaluator_create_directory_link_like evaluator_support_impl_evaluator_create_directory_link_like
#define evaluator_prepare_site_name_command evaluator_support_impl_evaluator_prepare_site_name_command
#define evaluator_sv_is_decimal evaluator_support_impl_evaluator_sv_is_decimal
#define evaluator_sv_is_bool01 evaluator_support_impl_evaluator_sv_is_bool01
#define evaluator_sv_list_contains evaluator_support_impl_evaluator_sv_list_contains
#define evaluator_create_tar_archive evaluator_support_impl_evaluator_create_tar_archive
#define evaluator_create_fetchcontent_git_repo evaluator_support_impl_evaluator_create_fetchcontent_git_repo
#define parse_cmake evaluator_support_impl_parse_cmake
#define eval_test_create evaluator_support_impl_eval_test_create
#define eval_test_destroy evaluator_support_impl_eval_test_destroy
#define eval_test_run evaluator_support_impl_eval_test_run
#define eval_test_report evaluator_support_impl_eval_test_report
#define eval_test_report_snapshot evaluator_support_impl_eval_test_report_snapshot
#define eval_test_set_compat_profile evaluator_support_impl_eval_test_set_compat_profile
#define eval_test_register_native_command evaluator_support_impl_eval_test_register_native_command
#define eval_test_unregister_native_command evaluator_support_impl_eval_test_unregister_native_command
#define eval_test_get_command_capability evaluator_support_impl_eval_test_get_command_capability
#define eval_test_var_get evaluator_support_impl_eval_test_var_get
#define eval_test_session_var_get evaluator_support_impl_eval_test_session_var_get
#define eval_test_var_defined evaluator_support_impl_eval_test_var_defined
#define eval_test_cache_defined evaluator_support_impl_eval_test_cache_defined
#define eval_test_target_known evaluator_support_impl_eval_test_target_known
#define eval_test_var_event_seen evaluator_support_impl_eval_test_var_event_seen
#define eval_test_canonical_artifact_count evaluator_support_impl_eval_test_canonical_artifact_count
#define eval_test_canonical_artifact_find evaluator_support_impl_eval_test_canonical_artifact_find
#define eval_test_ctest_step_count evaluator_support_impl_eval_test_ctest_step_count
#define eval_test_ctest_step_find evaluator_support_impl_eval_test_ctest_step_find
#define eval_test_current_file evaluator_support_impl_eval_test_current_file
#define native_test_handler_set_hit evaluator_support_impl_native_test_handler_set_hit
#define native_test_handler_runtime_mutation evaluator_support_impl_native_test_handler_runtime_mutation
#define native_test_handler_snapshot_set_strict_and_warn evaluator_support_impl_native_test_handler_snapshot_set_strict_and_warn
#define native_test_handler_snapshot_warn_only evaluator_support_impl_native_test_handler_snapshot_warn_only
#define evaluator_find_last_diag_for_command evaluator_support_impl_evaluator_find_last_diag_for_command
#define native_test_handler_snapshot_set_unsupported_error evaluator_support_impl_native_test_handler_snapshot_set_unsupported_error
#define evaluator_test_env_service_get evaluator_support_impl_evaluator_test_env_service_get
#define native_test_handler_tx_rollback_target evaluator_support_impl_native_test_handler_tx_rollback_target
#define evaluator_load_text_file_to_arena evaluator_support_impl_evaluator_load_text_file_to_arena
#define evaluator_read_entire_file_cstr evaluator_support_impl_evaluator_read_entire_file_cstr
#define eval_test_append_file_to_log_if_nonempty evaluator_support_impl_eval_test_append_file_to_log_if_nonempty
#define evaluator_normalize_newlines_to_arena evaluator_support_impl_evaluator_normalize_newlines_to_arena
#define sv_contains_sv evaluator_support_impl_sv_contains_sv
#define semicolon_list_count evaluator_support_impl_semicolon_list_count
#define semicolon_list_item_at evaluator_support_impl_semicolon_list_item_at
#define assert_evaluator_golden_casepack evaluator_support_impl_assert_evaluator_golden_casepack
#define evaluator_find_command_begin_index evaluator_support_impl_evaluator_find_command_begin_index
#define evaluator_find_command_end_index evaluator_support_impl_evaluator_find_command_end_index
#define evaluator_find_diag_index evaluator_support_impl_evaluator_find_diag_index
#define evaluator_stream_has_monotonic_sequence evaluator_support_impl_evaluator_stream_has_monotonic_sequence

#include "test_evaluator_v2_common.h"

#undef g_evaluator_captured_nob_logs
#undef evaluator_begin_nob_log_capture
#undef evaluator_end_nob_log_capture
#undef evaluator_set_source_date_epoch_value
#undef evaluator_prepare_symlink_escape_fixture
#undef evaluator_create_directory_link_like
#undef evaluator_prepare_site_name_command
#undef evaluator_sv_is_decimal
#undef evaluator_sv_is_bool01
#undef evaluator_sv_list_contains
#undef evaluator_create_tar_archive
#undef evaluator_create_fetchcontent_git_repo
#undef parse_cmake
#undef eval_test_create
#undef eval_test_destroy
#undef eval_test_run
#undef eval_test_report
#undef eval_test_report_snapshot
#undef eval_test_set_compat_profile
#undef eval_test_register_native_command
#undef eval_test_unregister_native_command
#undef eval_test_get_command_capability
#undef eval_test_var_get
#undef eval_test_session_var_get
#undef eval_test_var_defined
#undef eval_test_cache_defined
#undef eval_test_target_known
#undef eval_test_var_event_seen
#undef eval_test_canonical_artifact_count
#undef eval_test_canonical_artifact_find
#undef eval_test_ctest_step_count
#undef eval_test_ctest_step_find
#undef eval_test_current_file
#undef native_test_handler_set_hit
#undef native_test_handler_runtime_mutation
#undef native_test_handler_snapshot_set_strict_and_warn
#undef native_test_handler_snapshot_warn_only
#undef evaluator_find_last_diag_for_command
#undef native_test_handler_snapshot_set_unsupported_error
#undef evaluator_test_env_service_get
#undef native_test_handler_tx_rollback_target
#undef evaluator_load_text_file_to_arena
#undef evaluator_read_entire_file_cstr
#undef eval_test_append_file_to_log_if_nonempty
#undef evaluator_normalize_newlines_to_arena
#undef sv_contains_sv
#undef semicolon_list_count
#undef semicolon_list_item_at
#undef assert_evaluator_golden_casepack
#undef evaluator_find_command_begin_index
#undef evaluator_find_command_end_index
#undef evaluator_find_diag_index
#undef evaluator_stream_has_monotonic_sequence

Nob_String_Builder *evaluator_captured_nob_logs_ptr(void) {
    return &evaluator_support_impl_g_evaluator_captured_nob_logs;
}

Ast_Root parse_cmake(Arena *arena, const char *script) {
    return evaluator_support_impl_parse_cmake(arena, script);
}

void evaluator_begin_nob_log_capture(void) {
    evaluator_support_impl_evaluator_begin_nob_log_capture();
}

void evaluator_end_nob_log_capture(void) {
    evaluator_support_impl_evaluator_end_nob_log_capture();
}

void evaluator_set_source_date_epoch_value(const char *value) {
    evaluator_support_impl_evaluator_set_source_date_epoch_value(value);
}

bool evaluator_prepare_symlink_escape_fixture(void) {
    return evaluator_support_impl_evaluator_prepare_symlink_escape_fixture();
}

bool evaluator_create_directory_link_like(const char *link_path, const char *target_path) {
    return evaluator_support_impl_evaluator_create_directory_link_like(link_path, target_path);
}

bool evaluator_prepare_site_name_command(char *out_path, size_t out_path_size) {
    return evaluator_support_impl_evaluator_prepare_site_name_command(out_path, out_path_size);
}

bool evaluator_sv_is_decimal(String_View value) {
    return evaluator_support_impl_evaluator_sv_is_decimal(value);
}

bool evaluator_sv_is_bool01(String_View value) {
    return evaluator_support_impl_evaluator_sv_is_bool01(value);
}

bool evaluator_sv_list_contains(String_View list, String_View wanted) {
    return evaluator_support_impl_evaluator_sv_list_contains(list, wanted);
}

bool evaluator_create_tar_archive(const char *archive_path,
                                  const char *parent_dir,
                                  const char *entry_name) {
    return evaluator_support_impl_evaluator_create_tar_archive(archive_path, parent_dir, entry_name);
}

bool evaluator_create_fetchcontent_git_repo(const char *repo_dir,
                                            const char *cmakelists_text,
                                            const char *version_text,
                                            const char *tag_name) {
    return evaluator_support_impl_evaluator_create_fetchcontent_git_repo(
        repo_dir, cmakelists_text, version_text, tag_name);
}

typedef struct {
    Arena *temp_arena;
    Arena *event_arena;
    Cmake_Event_Stream *stream;
    Eval_Test_Init init;
    Eval_Test_Runtime *ctx;
} Eval_Test_Fixture;

Eval_Test_Runtime *eval_test_create(const Eval_Test_Init *init);
void eval_test_destroy(Eval_Test_Runtime *ctx);

static void evaluator_test_log_capture_cleanup(void *ctx) {
    (void)ctx;
    evaluator_end_nob_log_capture();
}

void eval_test_fixture_destroy(void *ctx) {
    Eval_Test_Fixture *fixture = (Eval_Test_Fixture*)ctx;
    if (!fixture) return;
    if (fixture->ctx) eval_test_destroy(fixture->ctx);
    if (fixture->temp_arena) arena_destroy(fixture->temp_arena);
    if (fixture->event_arena) arena_destroy(fixture->event_arena);
    free(fixture);
}

Eval_Test_Fixture *eval_test_fixture_create(size_t temp_arena_size,
                                            size_t event_arena_size,
                                            const Eval_Test_Init *overrides) {
    Eval_Test_Fixture *fixture = (Eval_Test_Fixture*)calloc(1, sizeof(*fixture));
    if (!fixture) return NULL;

    if (temp_arena_size == 0) temp_arena_size = 2 * 1024 * 1024;
    if (event_arena_size == 0) event_arena_size = 2 * 1024 * 1024;

    fixture->temp_arena = arena_create(temp_arena_size);
    fixture->event_arena = arena_create(event_arena_size);
    if (!fixture->temp_arena || !fixture->event_arena) {
        eval_test_fixture_destroy(fixture);
        return NULL;
    }

    fixture->stream = event_stream_create(fixture->event_arena);
    if (!fixture->stream) {
        eval_test_fixture_destroy(fixture);
        return NULL;
    }

    fixture->init = (Eval_Test_Init){
        .arena = fixture->temp_arena,
        .event_arena = fixture->event_arena,
        .stream = fixture->stream,
        .source_dir = nob_sv_from_cstr("."),
        .binary_dir = nob_sv_from_cstr("."),
        .current_file = "CMakeLists.txt",
        .exec_mode = EVAL_EXEC_MODE_PROJECT,
        .services = NULL,
        .compat_profile = EVAL_PROFILE_PERMISSIVE,
        .registry = NULL,
    };

    if (overrides) {
        if (overrides->source_dir.count > 0) fixture->init.source_dir = overrides->source_dir;
        if (overrides->binary_dir.count > 0) fixture->init.binary_dir = overrides->binary_dir;
        if (overrides->current_file) fixture->init.current_file = overrides->current_file;
        if (overrides->exec_mode != EVAL_EXEC_MODE_PROJECT) fixture->init.exec_mode = overrides->exec_mode;
        if (overrides->services) fixture->init.services = overrides->services;
        if (overrides->compat_profile != EVAL_PROFILE_PERMISSIVE) {
            fixture->init.compat_profile = overrides->compat_profile;
        }
        if (overrides->registry) fixture->init.registry = overrides->registry;
    }

    fixture->ctx = eval_test_create(&fixture->init);
    if (!fixture->ctx) {
        eval_test_fixture_destroy(fixture);
        return NULL;
    }

    if (!test_v2_cleanup_push(eval_test_fixture_destroy, fixture)) {
        eval_test_fixture_destroy(fixture);
        return NULL;
    }

    return fixture;
}

bool evaluator_test_begin_nob_log_capture_guarded(void) {
    evaluator_begin_nob_log_capture();
    if (!test_v2_cleanup_push(evaluator_test_log_capture_cleanup, NULL)) {
        evaluator_end_nob_log_capture();
        return false;
    }
    return true;
}

bool evaluator_test_guard_env(const char *name, const char *value) {
    Test_Host_Env_Guard *guard = NULL;

    if (!name || name[0] == '\0') return false;

    guard = (Test_Host_Env_Guard*)calloc(1, sizeof(*guard));
    if (!guard) return false;

    if (!test_host_env_guard_begin(guard, name, value)) {
        free(guard);
        return false;
    }

    if (!test_v2_cleanup_push(test_host_env_guard_cleanup, guard)) {
        test_host_env_guard_cleanup(guard);
        return false;
    }

    return true;
}

bool evaluator_test_guard_source_date_epoch(const char *value) {
    return evaluator_test_guard_env("SOURCE_DATE_EPOCH", value);
}

Eval_Test_Runtime *eval_test_create(const Eval_Test_Init *init) {
    return evaluator_support_impl_eval_test_create(init);
}

void eval_test_destroy(Eval_Test_Runtime *ctx) {
    evaluator_support_impl_eval_test_destroy(ctx);
}

Eval_Result eval_test_run(Eval_Test_Runtime *ctx, Ast_Root ast) {
    return evaluator_support_impl_eval_test_run(ctx, ast);
}

const Eval_Run_Report *eval_test_report(const Eval_Test_Runtime *ctx) {
    return evaluator_support_impl_eval_test_report(ctx);
}

const Eval_Run_Report *eval_test_report_snapshot(const Eval_Test_Runtime *ctx) {
    return evaluator_support_impl_eval_test_report_snapshot(ctx);
}

bool eval_test_set_compat_profile(Eval_Test_Runtime *ctx, Eval_Compat_Profile profile) {
    return evaluator_support_impl_eval_test_set_compat_profile(ctx, profile);
}

bool eval_test_register_native_command(Eval_Test_Runtime *ctx, const EvalNativeCommandDef *def) {
    return evaluator_support_impl_eval_test_register_native_command(ctx, def);
}

bool eval_test_unregister_native_command(Eval_Test_Runtime *ctx, String_View command_name) {
    return evaluator_support_impl_eval_test_unregister_native_command(ctx, command_name);
}

bool eval_test_get_command_capability(const Eval_Test_Runtime *ctx,
                                      String_View command_name,
                                      Command_Capability *out_capability) {
    return evaluator_support_impl_eval_test_get_command_capability(
        ctx, command_name, out_capability);
}

String_View eval_test_var_get(const Eval_Test_Runtime *ctx, String_View key) {
    return evaluator_support_impl_eval_test_var_get(ctx, key);
}

String_View eval_test_session_var_get(const EvalSession *session, String_View key) {
    return evaluator_support_impl_eval_test_session_var_get(session, key);
}

bool eval_test_var_defined(const Eval_Test_Runtime *ctx, String_View key) {
    return evaluator_support_impl_eval_test_var_defined(ctx, key);
}

bool eval_test_cache_defined(const Eval_Test_Runtime *ctx, String_View key) {
    return evaluator_support_impl_eval_test_cache_defined(ctx, key);
}

bool eval_test_target_known(const Eval_Test_Runtime *ctx, String_View target_name) {
    return evaluator_support_impl_eval_test_target_known(ctx, target_name);
}

bool eval_test_var_event_seen(const Cmake_Event_Stream *stream, String_View key) {
    return evaluator_support_impl_eval_test_var_event_seen(stream, key);
}

size_t eval_test_canonical_artifact_count(const Eval_Test_Runtime *ctx) {
    return evaluator_support_impl_eval_test_canonical_artifact_count(ctx);
}

size_t eval_test_ctest_step_count(const Eval_Test_Runtime *ctx) {
    return evaluator_support_impl_eval_test_ctest_step_count(ctx);
}

bool eval_test_canonical_artifact_find(const Eval_Test_Runtime *ctx,
                                       String_View producer,
                                       String_View kind,
                                       String_View *out_primary_path) {
    return evaluator_support_impl_eval_test_canonical_artifact_find(
        ctx, producer, kind, out_primary_path);
}

bool eval_test_ctest_step_find(const Eval_Test_Runtime *ctx,
                               String_View command_name,
                               String_View *out_status,
                               String_View *out_submit_part) {
    return evaluator_support_impl_eval_test_ctest_step_find(ctx,
                                                            command_name,
                                                            out_status,
                                                            out_submit_part);
}

const char *eval_test_current_file(const Eval_Test_Runtime *ctx) {
    return evaluator_support_impl_eval_test_current_file(ctx);
}

Eval_Result native_test_handler_set_hit(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_set_hit(ctx, node);
}

Eval_Result native_test_handler_runtime_mutation(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_runtime_mutation(ctx, node);
}

Eval_Result native_test_handler_snapshot_set_strict_and_warn(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_snapshot_set_strict_and_warn(ctx, node);
}

Eval_Result native_test_handler_snapshot_warn_only(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_snapshot_warn_only(ctx, node);
}

bool evaluator_find_last_diag_for_command(const Cmake_Event_Stream *stream,
                                          String_View command,
                                          Event_Diag_Severity *out_severity) {
    return evaluator_support_impl_evaluator_find_last_diag_for_command(
        stream, command, out_severity);
}

Eval_Result native_test_handler_snapshot_set_unsupported_error(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_snapshot_set_unsupported_error(ctx, node);
}

const char *evaluator_test_env_service_get(void *user_data,
                                           Arena *scratch_arena,
                                           const char *name) {
    return evaluator_support_impl_evaluator_test_env_service_get(user_data, scratch_arena, name);
}

Eval_Result native_test_handler_tx_rollback_target(EvalExecContext *ctx, const Node *node) {
    return evaluator_support_impl_native_test_handler_tx_rollback_target(ctx, node);
}

bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    return evaluator_support_impl_evaluator_load_text_file_to_arena(arena, path, out);
}

bool evaluator_read_entire_file_cstr(const char *path, Nob_String_Builder *out) {
    return evaluator_support_impl_evaluator_read_entire_file_cstr(path, out);
}

bool eval_test_append_file_to_log_if_nonempty(Arena *arena,
                                              const char *path,
                                              Nob_String_Builder *log) {
    return evaluator_support_impl_eval_test_append_file_to_log_if_nonempty(arena, path, log);
}

String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in) {
    return evaluator_support_impl_evaluator_normalize_newlines_to_arena(arena, in);
}

bool sv_contains_sv(String_View haystack, String_View needle) {
    return evaluator_support_impl_sv_contains_sv(haystack, needle);
}

size_t semicolon_list_count(String_View list) {
    return evaluator_support_impl_semicolon_list_count(list);
}

String_View semicolon_list_item_at(String_View list, size_t index) {
    return evaluator_support_impl_semicolon_list_item_at(list, index);
}

bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path) {
    return evaluator_support_impl_assert_evaluator_golden_casepack(input_path, expected_path);
}

size_t evaluator_find_command_begin_index(const Cmake_Event_Stream *stream,
                                          String_View command_name,
                                          size_t origin_line,
                                          Event_Command_Dispatch_Kind dispatch_kind) {
    return evaluator_support_impl_evaluator_find_command_begin_index(
        stream, command_name, origin_line, dispatch_kind);
}

size_t evaluator_find_command_end_index(const Cmake_Event_Stream *stream,
                                        String_View command_name,
                                        size_t origin_line,
                                        Event_Command_Dispatch_Kind dispatch_kind,
                                        Event_Command_Status status) {
    return evaluator_support_impl_evaluator_find_command_end_index(
        stream, command_name, origin_line, dispatch_kind, status);
}

size_t evaluator_find_diag_index(const Cmake_Event_Stream *stream,
                                 String_View command_name,
                                 size_t origin_line,
                                 String_View cause) {
    return evaluator_support_impl_evaluator_find_diag_index(
        stream, command_name, origin_line, cause);
}

bool evaluator_stream_has_monotonic_sequence(const Cmake_Event_Stream *stream) {
    return evaluator_support_impl_evaluator_stream_has_monotonic_sequence(stream);
}
