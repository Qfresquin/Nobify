#ifndef TEST_EVALUATOR_V2_SUPPORT_H_
#define TEST_EVALUATOR_V2_SUPPORT_H_

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    String_View name;
    String_View script;
} Evaluator_Case;

typedef struct {
    Evaluator_Case *items;
    size_t count;
    size_t capacity;
} Evaluator_Case_List;

typedef struct {
    Arena *arena;
    Arena *event_arena;
    Cmake_Event_Stream *stream;
    String_View source_dir;
    String_View binary_dir;
    const char *current_file;
    Eval_Exec_Mode exec_mode;
    const EvalServices *services;
    Eval_Compat_Profile compat_profile;
    EvalRegistry *registry;
} Eval_Test_Init;

typedef struct {
    EvalSession *session;
    EvalRegistry *registry;
    EvalExec_Request request;
    EvalRunResult last_run;
    Cmake_Event_Stream *stream;
    const char *current_file;
    bool owns_registry;
} Eval_Test_Runtime;

typedef struct {
    Arena *temp_arena;
    Arena *event_arena;
    Cmake_Event_Stream *stream;
    Eval_Test_Init init;
    Eval_Test_Runtime *ctx;
} Eval_Test_Fixture;

#define EVALUATOR_GOLDEN_DIR "test_v2/evaluator/golden"

typedef struct {
    const char *name;
    const char *value;
} Eval_Test_Env_Service_Data;

Nob_String_Builder *evaluator_captured_nob_logs_ptr(void);
#define g_evaluator_captured_nob_logs (*evaluator_captured_nob_logs_ptr())

Ast_Root parse_cmake(Arena *arena, const char *script);
void evaluator_begin_nob_log_capture(void);
void evaluator_end_nob_log_capture(void);
void evaluator_set_source_date_epoch_value(const char *value);
bool evaluator_prepare_symlink_escape_fixture(void);
bool evaluator_create_directory_link_like(const char *link_path, const char *target_path);
bool evaluator_prepare_site_name_command(char *out_path, size_t out_path_size);
bool evaluator_sv_is_decimal(String_View value);
bool evaluator_sv_is_bool01(String_View value);
bool evaluator_sv_list_contains(String_View list, String_View wanted);
bool evaluator_create_tar_archive(const char *archive_path,
                                  const char *parent_dir,
                                  const char *entry_name);
bool evaluator_create_fetchcontent_git_repo(const char *repo_dir,
                                            const char *cmakelists_text,
                                            const char *version_text,
                                            const char *tag_name);
Eval_Test_Fixture *eval_test_fixture_create(size_t temp_arena_size,
                                            size_t event_arena_size,
                                            const Eval_Test_Init *overrides);
void eval_test_fixture_destroy(void *ctx);
bool evaluator_test_begin_nob_log_capture_guarded(void);
bool evaluator_test_guard_env(const char *name, const char *value);
bool evaluator_test_guard_source_date_epoch(const char *value);
Eval_Test_Runtime *eval_test_create(const Eval_Test_Init *init);
void eval_test_destroy(Eval_Test_Runtime *ctx);
Eval_Result eval_test_run(Eval_Test_Runtime *ctx, Ast_Root ast);
const Eval_Run_Report *eval_test_report(const Eval_Test_Runtime *ctx);
const Eval_Run_Report *eval_test_report_snapshot(const Eval_Test_Runtime *ctx);
bool eval_test_set_compat_profile(Eval_Test_Runtime *ctx, Eval_Compat_Profile profile);
bool eval_test_register_native_command(Eval_Test_Runtime *ctx, const EvalNativeCommandDef *def);
bool eval_test_unregister_native_command(Eval_Test_Runtime *ctx, String_View command_name);
bool eval_test_get_command_capability(const Eval_Test_Runtime *ctx,
                                      String_View command_name,
                                      Command_Capability *out_capability);
String_View eval_test_var_get(const Eval_Test_Runtime *ctx, String_View key);
String_View eval_test_session_var_get(const EvalSession *session, String_View key);
bool eval_test_var_defined(const Eval_Test_Runtime *ctx, String_View key);
bool eval_test_cache_defined(const Eval_Test_Runtime *ctx, String_View key);
bool eval_test_target_known(const Eval_Test_Runtime *ctx, String_View target_name);
bool eval_test_var_event_seen(const Cmake_Event_Stream *stream, String_View key);
Eval_Result native_test_handler_set_hit(EvalExecContext *ctx, const Node *node);
Eval_Result native_test_handler_runtime_mutation(EvalExecContext *ctx, const Node *node);
Eval_Result native_test_handler_snapshot_set_strict_and_warn(EvalExecContext *ctx, const Node *node);
Eval_Result native_test_handler_snapshot_warn_only(EvalExecContext *ctx, const Node *node);
bool evaluator_find_last_diag_for_command(const Cmake_Event_Stream *stream,
                                          String_View command,
                                          Event_Diag_Severity *out_severity);
Eval_Result native_test_handler_snapshot_set_unsupported_error(EvalExecContext *ctx, const Node *node);
const char *evaluator_test_env_service_get(void *user_data,
                                           Arena *scratch_arena,
                                           const char *name);
Eval_Result native_test_handler_tx_rollback_target(EvalExecContext *ctx, const Node *node);
bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out);
bool evaluator_read_entire_file_cstr(const char *path, Nob_String_Builder *out);
bool eval_test_append_file_to_log_if_nonempty(Arena *arena,
                                              const char *path,
                                              Nob_String_Builder *log);
String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in);
bool sv_contains_sv(String_View haystack, String_View needle);
size_t semicolon_list_count(String_View list);
String_View semicolon_list_item_at(String_View list, size_t index);
bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path);
size_t evaluator_find_command_begin_index(const Cmake_Event_Stream *stream,
                                          String_View command_name,
                                          size_t origin_line,
                                          Event_Command_Dispatch_Kind dispatch_kind);
size_t evaluator_find_command_end_index(const Cmake_Event_Stream *stream,
                                        String_View command_name,
                                        size_t origin_line,
                                        Event_Command_Dispatch_Kind dispatch_kind,
                                        Event_Command_Status status);
size_t evaluator_find_diag_index(const Cmake_Event_Stream *stream,
                                 String_View command_name,
                                 size_t origin_line,
                                 String_View cause);
bool evaluator_stream_has_monotonic_sequence(const Cmake_Event_Stream *stream);

#endif // TEST_EVALUATOR_V2_SUPPORT_H_
