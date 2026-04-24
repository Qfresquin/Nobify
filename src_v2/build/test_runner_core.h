#ifndef TEST_RUNNER_CORE_H_
#define TEST_RUNNER_CORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_RUNNER_PATH_CAPACITY 4096
#define TEST_RUNNER_CASE_NAME_CAPACITY 256
#define TEST_RUNNER_SUMMARY_CAPACITY 512
#define TEST_RUNNER_FAILURE_SUMMARY_CAPACITY 2048

typedef enum {
    TEST_RUNNER_ACTION_RUN_MODULE = 0,
    TEST_RUNNER_ACTION_RUN_AGGREGATE,
    TEST_RUNNER_ACTION_RUN_TIDY_MODULE,
    TEST_RUNNER_ACTION_RUN_TIDY_AGGREGATE,
    TEST_RUNNER_ACTION_RUN_TIDY_SEMANTIC,
    TEST_RUNNER_ACTION_CLEAN,
} Test_Runner_Action;

typedef enum {
    TEST_RUNNER_MODULE_ARENA = 0,
    TEST_RUNNER_MODULE_LEXER,
    TEST_RUNNER_MODULE_PARSER,
    TEST_RUNNER_MODULE_BUILD_MODEL,
    TEST_RUNNER_MODULE_EVALUATOR,
    TEST_RUNNER_MODULE_EVALUATOR_DIFF,
    TEST_RUNNER_MODULE_EVALUATOR_CODEGEN_DIFF,
    TEST_RUNNER_MODULE_EVALUATOR_INTEGRATION,
    TEST_RUNNER_MODULE_PIPELINE,
    TEST_RUNNER_MODULE_CODEGEN,
    TEST_RUNNER_MODULE_ARTIFACT_PARITY,
    TEST_RUNNER_MODULE_ARTIFACT_PARITY_CORPUS,
    TEST_RUNNER_MODULE_CODEGEN_RENDER,
    TEST_RUNNER_MODULE_CODEGEN_BUILD,
    TEST_RUNNER_MODULE_CODEGEN_REJECT,
    TEST_RUNNER_MODULE_COUNT,
} Test_Runner_Module_Id;

typedef enum {
    TEST_RUNNER_PROFILE_DEFAULT = 0,
    TEST_RUNNER_PROFILE_FAST,
    TEST_RUNNER_PROFILE_ASAN_UBSAN,
    TEST_RUNNER_PROFILE_ASAN,
    TEST_RUNNER_PROFILE_UBSAN,
    TEST_RUNNER_PROFILE_MSAN,
    TEST_RUNNER_PROFILE_COVERAGE,
    TEST_RUNNER_PROFILE_COUNT,
} Test_Runner_Profile_Id;

typedef enum {
    TEST_RUNNER_LAUNCHER_NONE = 0,
    TEST_RUNNER_LAUNCHER_CCACHE,
    TEST_RUNNER_LAUNCHER_SCCACHE,
    TEST_RUNNER_LAUNCHER_CUSTOM,
} Test_Runner_Launcher_Kind;

typedef struct {
    Test_Runner_Module_Id id;
    const char *name;
    bool include_in_aggregate;
    bool explicit_heavy;
    bool case_filter_supported;
    Test_Runner_Profile_Id default_local_profile;
    bool watch_auto_eligible;
    const char *const *watch_roots;
    size_t watch_root_count;
} Test_Runner_Module_Def;

typedef struct {
    Test_Runner_Profile_Id id;
    const char *name;
    const char *legacy_suffix;
    const char *front_door_flag;
} Test_Runner_Profile_Def;

typedef enum {
    TEST_RUNNER_WATCH_MODE_MODULE = 0,
    TEST_RUNNER_WATCH_MODE_AUTO,
} Test_Runner_Watch_Mode;

typedef struct {
    Test_Runner_Action action;
    Test_Runner_Module_Id module_id;
    Test_Runner_Profile_Id profile_id;
    char case_name[TEST_RUNNER_CASE_NAME_CAPACITY];
    bool verbose;
    bool force;
    bool skip_preflight;
} Test_Runner_Request;

typedef struct {
    Test_Runner_Watch_Mode mode;
    Test_Runner_Module_Id module_id;
    Test_Runner_Profile_Id profile_id;
    bool profile_explicit;
    bool verbose;
} Test_Runner_Watch_Request;

typedef struct {
    Test_Runner_Launcher_Kind launcher_kind;
    uint32_t compile_jobs;
    uint32_t objects_total;
    uint32_t objects_rebuilt;
    uint32_t objects_reused;
    uint32_t link_performed;
} Test_Runner_Build_Stats;

typedef struct {
    bool ok;
    int exit_code;
    char case_name[TEST_RUNNER_CASE_NAME_CAPACITY];
    char preserved_workspace_path[TEST_RUNNER_PATH_CAPACITY];
    char stdout_log_path[TEST_RUNNER_PATH_CAPACITY];
    char stderr_log_path[TEST_RUNNER_PATH_CAPACITY];
    char summary[TEST_RUNNER_SUMMARY_CAPACITY];
    char failure_summary[TEST_RUNNER_FAILURE_SUMMARY_CAPACITY];
    Test_Runner_Build_Stats build_stats;
} Test_Runner_Result;

size_t test_runner_module_count(void);
size_t test_runner_profile_count(void);
const Test_Runner_Module_Def *test_runner_get_module_def(Test_Runner_Module_Id id);
const Test_Runner_Profile_Def *test_runner_get_profile_def(Test_Runner_Profile_Id id);
const Test_Runner_Module_Def *test_runner_find_module_def_by_name(const char *name);
Test_Runner_Launcher_Kind test_runner_classify_launcher_kind(const char *launcher_path);
const char *test_runner_launcher_kind_name(Test_Runner_Launcher_Kind kind);
const char *test_runner_select_launcher_candidate(const char *override_value,
                                                  bool have_ccache,
                                                  bool have_sccache);

bool test_runner_parse_front_door(const char *argv0,
                                  int argc,
                                  char **argv,
                                  Test_Runner_Request *out_request);
bool test_runner_parse_watch_front_door(const char *argv0,
                                        int argc,
                                        char **argv,
                                        Test_Runner_Watch_Request *out_request);
bool test_runner_execute(const Test_Runner_Request *request,
                         Test_Runner_Result *out_result);
bool test_runner_run_preflight_for_profile(Test_Runner_Profile_Id profile_id, bool verbose);
bool test_runner_preflight_fingerprint(uint64_t *out_fingerprint);
bool test_runner_resolve_coverage_tools(char out_llvm_cov[TEST_RUNNER_PATH_CAPACITY],
                                        char out_llvm_profdata[TEST_RUNNER_PATH_CAPACITY]);

#endif // TEST_RUNNER_CORE_H_
