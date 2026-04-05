#include "eval_ctest.h"

#include "eval_hash.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    String_View key;
    String_View value;
} Ctest_Parsed_Field;

typedef struct {
    SV_List positionals;
    Ctest_Parsed_Field *fields;
} Ctest_Parse_Result;

typedef struct {
    const char *const *single_keywords;
    size_t single_count;
    const char *const *multi_keywords;
    size_t multi_count;
    const char *const *flag_keywords;
    size_t flag_count;
    size_t min_positionals;
    size_t max_positionals;
} Ctest_Parse_Spec;

typedef struct {
    size_t message_check_count;
    size_t user_commands_count;
    size_t known_targets_count;
    size_t alias_targets_count;
    String_View dependency_provider_command_name;
    bool dependency_provider_supports_find_package;
    bool dependency_provider_supports_fetchcontent_makeavailable_serial;
    size_t dependency_provider_active_find_package_depth;
    size_t dependency_provider_active_fetchcontent_makeavailable_depth;
    size_t active_find_packages_count;
    size_t fetchcontent_declarations_count;
    size_t fetchcontent_states_count;
    size_t active_fetchcontent_makeavailable_count;
    size_t watched_variables_count;
    size_t watched_variable_commands_count;
    bool cpack_module_loaded;
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
    bool scope_pushed;
} Ctest_New_Process_State;

typedef struct {
    Eval_Ctest_Step_Kind step_kind;
    String_View command_name;
    String_View submit_part;
    const Ctest_Parse_Spec *spec;
    bool needs_source;
    bool needs_build;
} Ctest_Step_Runtime_Def;

typedef struct {
    Ctest_Step_Runtime_Def def;
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View resolved_source;
    String_View resolved_build;
    String_View model;
    String_View track;
} Ctest_Step_Runtime_Request;

typedef struct {
    SV_List argv;
    String_View root;
    String_View target;
} Ctest_Empty_Binary_Directory_Request;

typedef struct {
    SV_List argv;
    SV_List loaded_files;
} Ctest_Read_Custom_Files_Request;

typedef struct {
    SV_List argv;
    bool new_process;
    String_View return_var;
    SV_List scripts;
    SV_List resolved_scripts;
} Ctest_Run_Script_Request;

typedef struct {
    SV_List argv;
    String_View duration;
} Ctest_Sleep_Request;

typedef struct {
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View model;
    String_View group;
    bool append_mode;
    bool quiet;
    String_View resolved_source;
    String_View resolved_build;
} Ctest_Start_Request;

typedef struct {
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View model;
    String_View build_dir;
    String_View parts;
    String_View files;
    String_View submit_url;
    String_View build_id_var;
    String_View return_var;
    String_View capture_cmake_error_var;
    String_View http_headers;
    SV_List http_header_items;
    String_View cdash_upload_file;
    String_View cdash_upload_type;
    size_t retry_count;
    size_t retry_delay_sec;
    size_t inactivity_timeout_sec;
    bool has_inactivity_timeout;
    bool has_tls_verify;
    bool tls_verify;
    bool quiet;
    bool is_cdash_upload;
} Ctest_Submit_Request;

typedef struct {
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View model;
    String_View resolved_source;
    String_View build_dir;
    String_View configure_command;
    String_View configure_options;
    String_View labels_for_subprojects;
    String_View return_var;
    String_View capture_cmake_error_var;
    bool append_mode;
    bool quiet;
} Ctest_Configure_Request;

typedef struct {
    Ctest_Step_Runtime_Request core;
    String_View configuration;
    String_View parallel_level;
    String_View flags;
    String_View project_name;
    String_View target;
    String_View build_command;
    String_View number_errors_var;
    String_View number_warnings_var;
    String_View return_var;
    String_View capture_cmake_error_var;
    bool append_mode;
    bool quiet;
} Ctest_Build_Request;

typedef struct {
    String_View name;
    String_View command;
    String_View working_dir;
    String_View declared_source_dir;
    String_View declared_build_dir;
    bool command_expand_lists;
} Ctest_Test_Plan_Entry;

typedef Ctest_Test_Plan_Entry *Ctest_Test_Plan_Entry_List;

typedef struct {
    String_View name;
    String_View command;
    String_View backend_command;
    String_View working_dir;
    String_View stdout_text;
    String_View stderr_text;
    String_View result_text;
    size_t defect_count;
    int exit_code;
    bool passed;
    bool started;
    bool timed_out;
} Ctest_Test_Result_Entry;

typedef Ctest_Test_Result_Entry *Ctest_Test_Result_Entry_List;

typedef struct {
    Ctest_Step_Runtime_Request core;
    String_View parallel_level;
    String_View stop_time;
    String_View test_load;
    String_View output_junit;
    String_View return_var;
    String_View capture_cmake_error_var;
    int stop_time_seconds;
    bool has_stop_time;
    bool append_mode;
    bool quiet;
    bool schedule_random;
} Ctest_Test_Request;

typedef struct {
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View model;
    String_View build_dir;
    String_View coverage_command;
    String_View coverage_extra_flags;
    String_View labels;
    String_View return_var;
    String_View capture_cmake_error_var;
    bool append_mode;
    bool quiet;
} Ctest_Coverage_Request;

typedef struct {
    Ctest_Step_Runtime_Request core;
    String_View update_type;
    String_View update_command;
    String_View update_options;
    String_View return_var;
    String_View capture_cmake_error_var;
    String_View version_override;
    bool version_only;
    bool quiet;
    bool custom_command;
} Ctest_Update_Request;

typedef struct {
    String_View path;
    String_View labels;
} Ctest_Coverage_Source_Entry;

typedef Ctest_Coverage_Source_Entry *Ctest_Coverage_Source_Entry_List;

typedef struct {
    Ctest_Step_Runtime_Request core;
    String_View start;
    String_View end;
    String_View stride;
    String_View parallel_level;
    String_View stop_time;
    String_View resource_spec_file;
    String_View test_load;
    String_View schedule_random;
    String_View repeat;
    String_View output_junit;
    String_View backend_type;
    String_View backend_command;
    String_View backend_options;
    String_View backend_sanitizer_options;
    String_View suppression_file;
    String_View return_var;
    String_View capture_cmake_error_var;
    String_View defect_count_var;
    int stop_time_seconds;
    bool has_stop_time;
    bool append_mode;
    bool stop_on_failure;
    bool quiet;
} Ctest_Memcheck_Request;

typedef struct {
    SV_List argv;
    Ctest_Parse_Result parsed;
    String_View model;
    String_View build_dir;
    String_View files;
    String_View capture_cmake_error_var;
    bool quiet;
} Ctest_Upload_Request;

static const char *const s_ctest_build_single_keywords[] = {
    "BUILD", "CONFIGURATION", "PARALLEL_LEVEL", "FLAGS", "PROJECT_NAME", "TARGET",
    "NUMBER_ERRORS", "NUMBER_WARNINGS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
};
static const char *const s_ctest_build_flag_keywords[] = {"APPEND", "QUIET"};
static const Ctest_Parse_Spec s_ctest_build_parse_spec = {
    s_ctest_build_single_keywords,
    sizeof(s_ctest_build_single_keywords) / sizeof(s_ctest_build_single_keywords[0]),
    NULL,
    0,
    s_ctest_build_flag_keywords,
    sizeof(s_ctest_build_flag_keywords) / sizeof(s_ctest_build_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_configure_single_keywords[] = {
    "BUILD", "SOURCE", "OPTIONS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
};
static const char *const s_ctest_configure_flag_keywords[] = {"APPEND", "QUIET"};
static const Ctest_Parse_Spec s_ctest_configure_parse_spec = {
    s_ctest_configure_single_keywords,
    sizeof(s_ctest_configure_single_keywords) / sizeof(s_ctest_configure_single_keywords[0]),
    NULL,
    0,
    s_ctest_configure_flag_keywords,
    sizeof(s_ctest_configure_flag_keywords) / sizeof(s_ctest_configure_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_coverage_single_keywords[] = {
    "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
};
static const char *const s_ctest_coverage_multi_keywords[] = {"LABELS"};
static const char *const s_ctest_coverage_flag_keywords[] = {"APPEND", "QUIET"};
static const Ctest_Parse_Spec s_ctest_coverage_parse_spec = {
    s_ctest_coverage_single_keywords,
    sizeof(s_ctest_coverage_single_keywords) / sizeof(s_ctest_coverage_single_keywords[0]),
    s_ctest_coverage_multi_keywords,
    sizeof(s_ctest_coverage_multi_keywords) / sizeof(s_ctest_coverage_multi_keywords[0]),
    s_ctest_coverage_flag_keywords,
    sizeof(s_ctest_coverage_flag_keywords) / sizeof(s_ctest_coverage_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_memcheck_single_keywords[] = {
    "BUILD", "START", "END", "STRIDE", "EXCLUDE", "INCLUDE",
    "EXCLUDE_LABEL", "INCLUDE_LABEL", "EXCLUDE_FIXTURE",
    "EXCLUDE_FIXTURE_SETUP", "EXCLUDE_FIXTURE_CLEANUP",
    "PARALLEL_LEVEL", "RESOURCE_SPEC_FILE", "TEST_LOAD",
    "SCHEDULE_RANDOM", "STOP_TIME", "RETURN_VALUE",
    "CAPTURE_CMAKE_ERROR", "REPEAT", "OUTPUT_JUNIT",
    "DEFECT_COUNT"
};
static const char *const s_ctest_memcheck_flag_keywords[] = {
    "APPEND", "STOP_ON_FAILURE", "QUIET"
};
static const Ctest_Parse_Spec s_ctest_memcheck_parse_spec = {
    s_ctest_memcheck_single_keywords,
    sizeof(s_ctest_memcheck_single_keywords) / sizeof(s_ctest_memcheck_single_keywords[0]),
    NULL,
    0,
    s_ctest_memcheck_flag_keywords,
    sizeof(s_ctest_memcheck_flag_keywords) / sizeof(s_ctest_memcheck_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_start_single_keywords[] = {"GROUP", "TRACK"};
static const char *const s_ctest_start_flag_keywords[] = {"APPEND", "QUIET"};
static const Ctest_Parse_Spec s_ctest_start_parse_spec = {
    s_ctest_start_single_keywords,
    sizeof(s_ctest_start_single_keywords) / sizeof(s_ctest_start_single_keywords[0]),
    NULL,
    0,
    s_ctest_start_flag_keywords,
    sizeof(s_ctest_start_flag_keywords) / sizeof(s_ctest_start_flag_keywords[0]),
    0,
    3,
};

static const char *const s_ctest_submit_single_keywords[] = {
    "SUBMIT_URL", "BUILD_ID", "HTTPHEADER", "RETRY_COUNT", "RETRY_DELAY",
    "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "CDASH_UPLOAD", "CDASH_UPLOAD_TYPE"
};
static const char *const s_ctest_submit_multi_keywords[] = {"PARTS", "FILES"};
static const char *const s_ctest_submit_flag_keywords[] = {"QUIET"};
static const Ctest_Parse_Spec s_ctest_submit_parse_spec = {
    s_ctest_submit_single_keywords,
    sizeof(s_ctest_submit_single_keywords) / sizeof(s_ctest_submit_single_keywords[0]),
    s_ctest_submit_multi_keywords,
    sizeof(s_ctest_submit_multi_keywords) / sizeof(s_ctest_submit_multi_keywords[0]),
    s_ctest_submit_flag_keywords,
    sizeof(s_ctest_submit_flag_keywords) / sizeof(s_ctest_submit_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_test_single_keywords[] = {
    "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "PARALLEL_LEVEL",
    "STOP_TIME", "TEST_LOAD", "OUTPUT_JUNIT"
};
static const char *const s_ctest_test_flag_keywords[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
static const Ctest_Parse_Spec s_ctest_test_parse_spec = {
    s_ctest_test_single_keywords,
    sizeof(s_ctest_test_single_keywords) / sizeof(s_ctest_test_single_keywords[0]),
    NULL,
    0,
    s_ctest_test_flag_keywords,
    sizeof(s_ctest_test_flag_keywords) / sizeof(s_ctest_test_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_update_single_keywords[] = {"SOURCE", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"};
static const char *const s_ctest_update_flag_keywords[] = {"QUIET"};
static const Ctest_Parse_Spec s_ctest_update_parse_spec = {
    s_ctest_update_single_keywords,
    sizeof(s_ctest_update_single_keywords) / sizeof(s_ctest_update_single_keywords[0]),
    NULL,
    0,
    s_ctest_update_flag_keywords,
    sizeof(s_ctest_update_flag_keywords) / sizeof(s_ctest_update_flag_keywords[0]),
    0,
    0,
};

static const char *const s_ctest_upload_single_keywords[] = {"CAPTURE_CMAKE_ERROR"};
static const char *const s_ctest_upload_multi_keywords[] = {"FILES"};
static const char *const s_ctest_upload_flag_keywords[] = {"QUIET"};
static const Ctest_Parse_Spec s_ctest_upload_parse_spec = {
    s_ctest_upload_single_keywords,
    sizeof(s_ctest_upload_single_keywords) / sizeof(s_ctest_upload_single_keywords[0]),
    s_ctest_upload_multi_keywords,
    sizeof(s_ctest_upload_multi_keywords) / sizeof(s_ctest_upload_multi_keywords[0]),
    s_ctest_upload_flag_keywords,
    sizeof(s_ctest_upload_flag_keywords) / sizeof(s_ctest_upload_flag_keywords[0]),
    0,
    0,
};

static String_View ctest_current_binary_dir(EvalExecContext *ctx);
static String_View ctest_binary_root(EvalExecContext *ctx);
static String_View ctest_get_session_field(EvalExecContext *ctx, const char *field_name);
static bool ctest_append_command_tokens_temp(EvalExecContext *ctx, String_View raw, SV_List *out_argv);
static bool ctest_path_exists(EvalExecContext *ctx, String_View path);
static bool ctest_resolve_files(EvalExecContext *ctx,
                                const Node *node,
                                String_View command_name,
                                String_View raw_files,
                                String_View base_dir,
                                bool required,
                                bool validate_exists,
                                String_View *out_resolved);
static size_t s_ctest_session_tag_counter = 0;

static bool ctest_emit_diag(EvalExecContext *ctx,
                            const Node *node,
                            Cmake_Diag_Severity severity,
                            String_View cause,
                            String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_ctest"), node->as.cmd.name, eval_origin_from_node(ctx, node), cause, hint);
}

static bool ctest_set_field(EvalExecContext *ctx,
                            String_View command_name,
                            const char *field_name,
                            String_View value) {
    if (!ctx || !field_name) return false;
    size_t total = sizeof("NOBIFY_CTEST::") - 1 + command_name.count + sizeof("::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    int n = snprintf(buf,
                     total + 1,
                     "NOBIFY_CTEST::%.*s::%s",
                     (int)command_name.count,
                     command_name.data ? command_name.data : "",
                     field_name);
    if (n < 0) return ctx_oom(ctx);
    return eval_var_set_current(ctx, nob_sv_from_cstr(buf), value);
}

static bool ctest_step_kind_requires_submit_files(Eval_Ctest_Step_Kind kind) {
    return kind == EVAL_CTEST_STEP_BUILD ||
           kind == EVAL_CTEST_STEP_CONFIGURE ||
           kind == EVAL_CTEST_STEP_TEST ||
           kind == EVAL_CTEST_STEP_COVERAGE ||
           kind == EVAL_CTEST_STEP_UPDATE ||
           kind == EVAL_CTEST_STEP_MEMCHECK ||
           kind == EVAL_CTEST_STEP_UPLOAD;
}

static const char *ctest_step_primary_artifact_field(Eval_Ctest_Step_Kind kind) {
    switch (kind) {
    case EVAL_CTEST_STEP_BUILD: return "BUILD_XML";
    case EVAL_CTEST_STEP_CONFIGURE: return "CONFIGURE_XML";
    case EVAL_CTEST_STEP_TEST: return "TEST_XML";
    case EVAL_CTEST_STEP_COVERAGE: return "COVERAGE_XML";
    case EVAL_CTEST_STEP_UPDATE: return "UPDATE_XML";
    case EVAL_CTEST_STEP_MEMCHECK: return "MEMCHECK_XML";
    case EVAL_CTEST_STEP_UPLOAD: return "UPLOAD_XML";
    default: return NULL;
    }
}

static bool ctest_project_committed_step(EvalExecContext *ctx,
                                         const Eval_Ctest_Step_Record *step,
                                         const SV_List *argv) {
    if (!ctx || !step || !argv || step->command_name.count == 0) return false;

    if (step->source_dir.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "RESOLVED_SOURCE", step->source_dir)) {
        return false;
    }
    if (step->build_dir.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "RESOLVED_BUILD", step->build_dir)) {
        return false;
    }
    if (step->track.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "TRACK", step->track)) {
        return false;
    }
    if (step->tag.count > 0 && !ctest_set_field(ctx, step->command_name, "TAG", step->tag)) return false;
    if (step->testing_dir.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "TESTING_DIR", step->testing_dir)) {
        return false;
    }
    if (step->tag_file.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "TAG_FILE", step->tag_file)) {
        return false;
    }
    if (step->tag_dir.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "TAG_DIR", step->tag_dir)) {
        return false;
    }
    if (step->manifest.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "MANIFEST", step->manifest)) {
        return false;
    }
    if (step->output_junit.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "RESOLVED_OUTPUT_JUNIT", step->output_junit)) {
        return false;
    }
    if (step->submit_files.count > 0 &&
        !ctest_set_field(ctx, step->command_name, "RESOLVED_FILES", step->submit_files)) {
        return false;
    }
    if (step->has_primary_artifact) {
        const Eval_Canonical_Artifact *artifact = eval_canonical_artifact_at(ctx, step->primary_artifact_index);
        const char *artifact_field = ctest_step_primary_artifact_field(step->kind);
        if (artifact_field && artifact && artifact->primary_path.count > 0 &&
            !ctest_set_field(ctx, step->command_name, artifact_field, artifact->primary_path)) {
            return false;
        }
    }

    return eval_ctest_publish_metadata(ctx, step->command_name, argv, step->status);
}

static void ctest_step_fill_session_context(EvalExecContext *ctx, Eval_Ctest_Step_Record *step) {
    if (!ctx || !step) return;
    if (step->model.count == 0) step->model = ctest_get_session_field(ctx, "MODEL");
    if (step->track.count == 0) step->track = ctest_get_session_field(ctx, "TRACK");
    if (step->source_dir.count == 0) step->source_dir = ctest_get_session_field(ctx, "SOURCE");
    if (step->build_dir.count == 0) step->build_dir = ctest_get_session_field(ctx, "BUILD");
    if (step->tag.count == 0) step->tag = ctest_get_session_field(ctx, "TAG");
    if (step->testing_dir.count == 0) step->testing_dir = ctest_get_session_field(ctx, "TESTING_DIR");
    if (step->tag_file.count == 0) step->tag_file = ctest_get_session_field(ctx, "TAG_FILE");
    if (step->tag_dir.count == 0) step->tag_dir = ctest_get_session_field(ctx, "TAG_DIR");
}

static bool ctest_commit_step_record(EvalExecContext *ctx,
                                     const SV_List *argv,
                                     Eval_Ctest_Step_Record step,
                                     const Eval_Canonical_Artifact *primary_artifact) {
    if (!ctx || !argv || step.command_name.count == 0) return false;

    ctest_step_fill_session_context(ctx, &step);

    Eval_Canonical_Draft draft = {0};
    eval_canonical_draft_init(&draft);

    if (primary_artifact) {
        size_t artifact_base = arena_arr_len(ctx->canonical_state.artifacts);
        size_t local_index = 0;
        if (!eval_canonical_draft_add_artifact(ctx, &draft, primary_artifact, &local_index)) return false;
        step.has_primary_artifact = true;
        step.primary_artifact_index = artifact_base + local_index;
    }

    if (!eval_canonical_draft_add_ctest_step(ctx, &draft, &step)) return false;
    if (!eval_canonical_draft_commit(ctx, &draft)) return false;

    const Eval_Ctest_Step_Record *stored = eval_ctest_find_last_step_by_command(ctx, step.command_name);
    if (!stored) return false;
    return ctest_project_committed_step(ctx, stored, argv);
}

static bool ctest_commit_failed_step_record(EvalExecContext *ctx,
                                            const SV_List *argv,
                                            Eval_Ctest_Step_Kind kind,
                                            String_View command_name,
                                            String_View submit_part,
                                            String_View model,
                                            String_View source_dir,
                                            String_View build_dir) {
    if (!ctx || !argv || command_name.count == 0) return false;

    Eval_Ctest_Step_Record step = {
        .kind = kind,
        .command_name = command_name,
        .submit_part = submit_part,
        .status = nob_sv_from_cstr("FAILED"),
        .model = model,
        .source_dir = source_dir,
        .build_dir = build_dir,
    };
    return ctest_commit_step_record(ctx, argv, step, NULL);
}

static bool ctest_record_parsed_field(Arena *arena, Ctest_Parse_Result *out_res, String_View key, String_View value) {
    if (!arena || !out_res || key.count == 0) return false;
    Ctest_Parsed_Field field = {
        .key = key,
        .value = value,
    };
    return arena_arr_push(arena, out_res->fields, field);
}

static String_View ctest_parsed_field_value(const Ctest_Parse_Result *parsed, const char *field_name) {
    if (!parsed || !field_name) return nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(parsed->fields); i++) {
        if (eval_sv_eq_ci_lit(parsed->fields[i].key, field_name)) return parsed->fields[i].value;
    }
    return nob_sv_from_cstr("");
}

static bool ctest_list_push_unique_temp(EvalExecContext *ctx, SV_List *out_list, String_View value) {
    if (!ctx || !out_list || value.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(*out_list); i++) {
        if (nob_sv_eq((*out_list)[i], value)) return true;
    }
    return svu_list_push_temp(ctx, out_list, value);
}

static bool ctest_collect_parsed_field_values(EvalExecContext *ctx,
                                              const Ctest_Parse_Result *parsed,
                                              const char *field_name,
                                              SV_List *out_values) {
    if (!ctx || !parsed || !field_name || !out_values) return false;
    for (size_t i = 0; i < arena_arr_len(parsed->fields); i++) {
        if (!eval_sv_eq_ci_lit(parsed->fields[i].key, field_name)) continue;
        if (parsed->fields[i].value.count == 0) continue;
        if (!svu_list_push_temp(ctx, out_values, parsed->fields[i].value)) return false;
    }
    return true;
}

static String_View ctest_join_parsed_field_values(EvalExecContext *ctx,
                                                  const Ctest_Parse_Result *parsed,
                                                  const char *field_name) {
    if (!ctx || !parsed || !field_name) return nob_sv_from_cstr("");
    SV_List values = {0};
    if (!ctest_collect_parsed_field_values(ctx, parsed, field_name, &values)) return nob_sv_from_cstr("");
    if (arena_arr_len(values) == 0) return nob_sv_from_cstr("");
    return eval_sv_join_semi_temp(ctx, values, arena_arr_len(values));
}

static bool ctest_parse_size_value_sv(String_View value, size_t *out_value) {
    if (!out_value || value.count == 0) return false;

    size_t result = 0;
    for (size_t i = 0; i < value.count; i++) {
        unsigned char ch = (unsigned char)value.data[i];
        if (!isdigit(ch)) return false;
        result = (result * 10U) + (size_t)(ch - '0');
    }

    *out_value = result;
    return true;
}

static String_View ctest_submit_visible_var(EvalExecContext *ctx,
                                            const char *primary_name,
                                            const char *fallback_name) {
    if (!ctx || !primary_name) return nob_sv_from_cstr("");

    String_View value = eval_var_get_visible(ctx, nob_sv_from_cstr(primary_name));
    if (value.count > 0 || !fallback_name) return value;
    return eval_var_get_visible(ctx, nob_sv_from_cstr(fallback_name));
}

static String_View ctest_configure_resolve_command(EvalExecContext *ctx, String_View source_dir) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_CONFIGURE_COMMAND"));
    if (command.count > 0) return command;

    command = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_COMMAND"));
    if (command.count == 0) return nob_sv_from_cstr("");

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, command.data, command.count);
    if (source_dir.count > 0) {
        nob_sb_append_cstr(&sb, ";");
        nob_sb_append_buf(&sb, source_dir.data, source_dir.count);
    }

    String_View resolved = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return resolved;
}

static bool ctest_sv_has_prefix_ci(String_View value, const char *prefix) {
    size_t prefix_len = 0;
    if (!prefix) return false;
    prefix_len = strlen(prefix);
    if (value.count < prefix_len) return false;
    for (size_t i = 0; i < prefix_len; i++) {
        if (tolower((unsigned char)value.data[i]) != tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

static String_View ctest_update_join_settings(EvalExecContext *ctx, String_View lhs, String_View rhs) {
    if (!ctx) return nob_sv_from_cstr("");
    if (lhs.count == 0) return rhs;
    if (rhs.count == 0) return lhs;

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, lhs.data ? lhs.data : "", lhs.count);
    nob_sb_append_cstr(&sb, ";");
    nob_sb_append_buf(&sb, rhs.data ? rhs.data : "", rhs.count);
    String_View joined = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return joined;
}

static bool ctest_update_is_truthy(String_View raw) {
    if (raw.count == 0) return false;
    return !eval_sv_eq_ci_lit(raw, "0") &&
           !eval_sv_eq_ci_lit(raw, "FALSE") &&
           !eval_sv_eq_ci_lit(raw, "OFF") &&
           !eval_sv_eq_ci_lit(raw, "NO") &&
           !eval_sv_eq_ci_lit(raw, "N") &&
           !eval_sv_eq_ci_lit(raw, "IGNORE") &&
           !eval_sv_eq_ci_lit(raw, "NOTFOUND");
}

static String_View ctest_update_canonical_type(String_View raw) {
    if (eval_sv_eq_ci_lit(raw, "bzr")) return nob_sv_from_cstr("bzr");
    if (eval_sv_eq_ci_lit(raw, "cvs")) return nob_sv_from_cstr("cvs");
    if (eval_sv_eq_ci_lit(raw, "git")) return nob_sv_from_cstr("git");
    if (eval_sv_eq_ci_lit(raw, "hg")) return nob_sv_from_cstr("hg");
    if (eval_sv_eq_ci_lit(raw, "p4")) return nob_sv_from_cstr("p4");
    if (eval_sv_eq_ci_lit(raw, "svn")) return nob_sv_from_cstr("svn");
    return nob_sv_from_cstr("");
}

static String_View ctest_update_detect_type(EvalExecContext *ctx, String_View source_dir) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View source_markers[][2] = {
        {nob_sv_from_cstr(".git"), nob_sv_from_cstr("git")},
        {nob_sv_from_cstr(".svn"), nob_sv_from_cstr("svn")},
        {nob_sv_from_cstr("CVS"), nob_sv_from_cstr("cvs")},
        {nob_sv_from_cstr(".hg"), nob_sv_from_cstr("hg")},
        {nob_sv_from_cstr(".bzr"), nob_sv_from_cstr("bzr")},
    };

    if (source_dir.count > 0) {
        for (size_t i = 0; i < sizeof(source_markers) / sizeof(source_markers[0]); i++) {
            String_View marker_path =
                eval_sv_path_join(eval_temp_arena(ctx), source_dir, source_markers[i][0]);
            if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
            if (ctest_path_exists(ctx, marker_path)) return source_markers[i][1];
        }
    }

    if (eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_P4_COMMAND")).count > 0 ||
        eval_var_get_visible(ctx, nob_sv_from_cstr("P4COMMAND")).count > 0 ||
        eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_P4_UPDATE_CUSTOM")).count > 0) {
        return nob_sv_from_cstr("p4");
    }

    return nob_sv_from_cstr("");
}

static bool ctest_update_resolve_type(EvalExecContext *ctx,
                                      const Node *node,
                                      String_View source_dir,
                                      String_View *out_type) {
    if (!ctx || !node || !out_type) return false;
    *out_type = nob_sv_from_cstr("");

    String_View raw = ctest_submit_visible_var(ctx, "UPDATE_TYPE", "CTEST_UPDATE_TYPE");
    if (raw.count > 0) {
        String_View canonical = ctest_update_canonical_type(raw);
        if (canonical.count == 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_update() UPDATE_TYPE must be one of bzr, cvs, git, hg, p4, or svn"),
                                  raw);
            return false;
        }
        *out_type = canonical;
        return true;
    }

    *out_type = ctest_update_detect_type(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static String_View ctest_update_resolve_command(EvalExecContext *ctx,
                                                String_View update_type,
                                                bool *out_custom_command) {
    if (out_custom_command) *out_custom_command = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_UPDATE_COMMAND"));
    if (command.count > 0) return command;

    if (eval_sv_eq_ci_lit(update_type, "git")) {
        command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_GIT_UPDATE_CUSTOM"));
        if (command.count > 0) {
            if (out_custom_command) *out_custom_command = true;
            return command;
        }
        command = ctest_submit_visible_var(ctx, "CTEST_GIT_COMMAND", "GITCOMMAND");
        return command.count > 0 ? command : nob_sv_from_cstr("git");
    }
    if (eval_sv_eq_ci_lit(update_type, "svn")) {
        command = ctest_submit_visible_var(ctx, "CTEST_SVN_COMMAND", "SVNCOMMAND");
        return command.count > 0 ? command : nob_sv_from_cstr("svn");
    }
    if (eval_sv_eq_ci_lit(update_type, "cvs")) {
        command = ctest_submit_visible_var(ctx, "CTEST_CVS_COMMAND", "CVSCOMMAND");
        return command.count > 0 ? command : nob_sv_from_cstr("cvs");
    }
    if (eval_sv_eq_ci_lit(update_type, "hg")) {
        command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_HG_COMMAND"));
        return command.count > 0 ? command : nob_sv_from_cstr("hg");
    }
    if (eval_sv_eq_ci_lit(update_type, "bzr")) {
        command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BZR_COMMAND"));
        return command.count > 0 ? command : nob_sv_from_cstr("bzr");
    }
    if (eval_sv_eq_ci_lit(update_type, "p4")) {
        command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_P4_UPDATE_CUSTOM"));
        if (command.count > 0) {
            if (out_custom_command) *out_custom_command = true;
            return command;
        }
        command = ctest_submit_visible_var(ctx, "CTEST_P4_COMMAND", "P4COMMAND");
        return command.count > 0 ? command : nob_sv_from_cstr("p4");
    }

    return eval_var_get_visible(ctx, nob_sv_from_cstr("UPDATE_COMMAND"));
}

static String_View ctest_update_resolve_options(EvalExecContext *ctx, String_View update_type) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View options = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_UPDATE_OPTIONS"));
    if (options.count > 0) return options;

    if (eval_sv_eq_ci_lit(update_type, "git")) {
        options = ctest_submit_visible_var(ctx, "CTEST_GIT_UPDATE_OPTIONS", "GIT_UPDATE_OPTIONS");
    } else if (eval_sv_eq_ci_lit(update_type, "svn")) {
        String_View base = ctest_submit_visible_var(ctx, "CTEST_SVN_OPTIONS", "CTEST_SVN_OPTIONS");
        String_View update = ctest_submit_visible_var(ctx, "CTEST_SVN_UPDATE_OPTIONS", "SVN_UPDATE_OPTIONS");
        options = ctest_update_join_settings(ctx, base, update);
    } else if (eval_sv_eq_ci_lit(update_type, "cvs")) {
        options = ctest_submit_visible_var(ctx, "CTEST_CVS_UPDATE_OPTIONS", "CVS_UPDATE_OPTIONS");
    } else if (eval_sv_eq_ci_lit(update_type, "hg")) {
        options = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_HG_UPDATE_OPTIONS"));
    } else if (eval_sv_eq_ci_lit(update_type, "bzr")) {
        options = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BZR_UPDATE_OPTIONS"));
    } else if (eval_sv_eq_ci_lit(update_type, "p4")) {
        String_View base = ctest_submit_visible_var(ctx, "CTEST_P4_OPTIONS", "CTEST_P4_OPTIONS");
        String_View update =
            ctest_submit_visible_var(ctx, "CTEST_P4_UPDATE_OPTIONS", "CTEST_P4_UPDATE_OPTIONS");
        options = ctest_update_join_settings(ctx, base, update);
    }

    if (options.count > 0) return options;
    return eval_var_get_visible(ctx, nob_sv_from_cstr("UPDATE_OPTIONS"));
}

static bool ctest_build_is_makefile_generator(String_View generator) {
    return eval_sv_eq_ci_lit(generator, "Unix Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MinGW Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MSYS Makefiles") ||
           eval_sv_eq_ci_lit(generator, "NMake Makefiles") ||
           eval_sv_eq_ci_lit(generator, "Watcom WMake") ||
           eval_sv_eq_ci_lit(generator, "Borland Makefiles");
}

static String_View ctest_build_resolve_parallel_level(EvalExecContext *ctx,
                                                      const Ctest_Parse_Result *parsed) {
    if (!ctx || !parsed) return nob_sv_from_cstr("");

    String_View parallel_level = ctest_parsed_field_value(parsed, "PARALLEL_LEVEL");
    if (parallel_level.count > 0) return parallel_level;

    const char *env_parallel = eval_getenv_temp(ctx, "CMAKE_BUILD_PARALLEL_LEVEL");
    if (!env_parallel || env_parallel[0] == '\0') return nob_sv_from_cstr("");
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(env_parallel));
}

static String_View ctest_build_resolve_command(EvalExecContext *ctx,
                                               String_View configuration,
                                               String_View parallel_level,
                                               String_View flags,
                                               String_View target) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View explicit_command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BUILD_COMMAND"));
    if (explicit_command.count > 0) return explicit_command;

    String_View command_name = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_COMMAND"));
    if (command_name.count == 0) command_name = nob_sv_from_cstr("cmake");

    SV_List argv = {0};
    if (!svu_list_push_temp(ctx, &argv, command_name)) return nob_sv_from_cstr("");
    if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--build"))) return nob_sv_from_cstr("");
    if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("."))) return nob_sv_from_cstr("");

    if (target.count > 0) {
        if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--target"))) return nob_sv_from_cstr("");
        if (!svu_list_push_temp(ctx, &argv, target)) return nob_sv_from_cstr("");
    }
    if (configuration.count > 0) {
        if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--config"))) return nob_sv_from_cstr("");
        if (!svu_list_push_temp(ctx, &argv, configuration)) return nob_sv_from_cstr("");
    }
    if (parallel_level.count > 0) {
        if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--parallel"))) return nob_sv_from_cstr("");
        if (!svu_list_push_temp(ctx, &argv, parallel_level)) return nob_sv_from_cstr("");
    }

    bool append_make_i = !eval_policy_is_new(ctx, EVAL_POLICY_CMP0061) &&
                         ctest_build_is_makefile_generator(eval_var_get_visible(ctx,
                                                                                nob_sv_from_cstr("CMAKE_GENERATOR")));
    if (flags.count > 0 || append_make_i) {
        if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--"))) return nob_sv_from_cstr("");
        if (flags.count > 0 && !ctest_append_command_tokens_temp(ctx, flags, &argv)) {
            return nob_sv_from_cstr("");
        }
        if (append_make_i && !svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("-i"))) {
            return nob_sv_from_cstr("");
        }
    }

    return eval_sv_join_semi_temp(ctx, argv, arena_arr_len(argv));
}

static bool ctest_submit_resolve_numeric_setting(EvalExecContext *ctx,
                                                 const Node *node,
                                                 const Ctest_Parse_Result *parsed,
                                                 const char *field_name,
                                                 const char *fallback_var,
                                                 size_t *out_value) {
    if (!ctx || !node || !parsed || !out_value) return false;
    *out_value = 0;

    String_View raw = field_name ? ctest_parsed_field_value(parsed, field_name) : nob_sv_from_cstr("");
    if (raw.count == 0 && fallback_var) {
        raw = eval_var_get_visible(ctx, nob_sv_from_cstr(fallback_var));
    }
    if (raw.count == 0) return true;

    if (!ctest_parse_size_value_sv(raw, out_value)) {
        const char *label = field_name ? field_name : fallback_var;
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "ctest_submit() received an invalid ");
        nob_sb_append_cstr(&sb, label ? label : "numeric value");
        String_View cause = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
        nob_sb_free(sb);
        if (eval_should_stop(ctx)) return false;
        (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, raw);
        return false;
    }

    return true;
}

static bool ctest_submit_resolve_tls_verify(EvalExecContext *ctx,
                                            bool *out_has_tls_verify,
                                            bool *out_tls_verify) {
    if (!out_has_tls_verify || !out_tls_verify) return false;
    *out_has_tls_verify = false;
    *out_tls_verify = true;
    if (!ctx) return false;

    String_View curl_options = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_CURL_OPTIONS"));
    if (curl_options.count == 0) return true;

    SV_List items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), curl_options, &items)) return false;
    for (size_t i = 0; i < arena_arr_len(items); i++) {
        if (eval_sv_eq_ci_lit(items[i], "CURLOPT_SSL_VERIFYPEER_OFF") ||
            eval_sv_eq_ci_lit(items[i], "CURLOPT_SSL_VERIFYHOST_OFF")) {
            *out_has_tls_verify = true;
            *out_tls_verify = false;
            break;
        }
    }

    return true;
}

static String_View ctest_submit_format_size_temp(EvalExecContext *ctx, size_t value) {
    if (!ctx) return nob_sv_from_cstr("");
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(nob_temp_sprintf("%zu", value)));
}

static String_View ctest_submit_format_long_temp(EvalExecContext *ctx, long value) {
    if (!ctx) return nob_sv_from_cstr("");
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(nob_temp_sprintf("%ld", value)));
}

static String_View ctest_submit_make_assignment_temp(EvalExecContext *ctx,
                                                     const char *name,
                                                     String_View value) {
    if (!ctx || !name) return nob_sv_from_cstr("");

    size_t name_len = strlen(name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 1 + value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, name, name_len);
    buf[name_len] = '=';
    if (value.count > 0) memcpy(buf + name_len + 1, value.data, value.count);
    buf[name_len + 1 + value.count] = '\0';
    return nob_sv_from_parts(buf, name_len + 1 + value.count);
}

static bool ctest_keyword_in_list(String_View tok, const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (eval_sv_eq_ci_lit(tok, items[i])) return true;
    }
    return false;
}

static bool ctest_is_any_known_keyword(String_View tok, const Ctest_Parse_Spec *spec) {
    return ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count) ||
           ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count) ||
           ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count);
}

static bool ctest_publish_var_keyword(EvalExecContext *ctx, String_View key, String_View value) {
    if (value.count == 0) return true;
    if (eval_sv_eq_ci_lit(key, "RETURN_VALUE") ||
        eval_sv_eq_ci_lit(key, "CAPTURE_CMAKE_ERROR") ||
        eval_sv_eq_ci_lit(key, "NUMBER_ERRORS") ||
        eval_sv_eq_ci_lit(key, "NUMBER_WARNINGS") ||
        eval_sv_eq_ci_lit(key, "DEFECT_COUNT")) {
        return eval_var_set_current(ctx, value, nob_sv_from_cstr("0"));
    }
    if (eval_sv_eq_ci_lit(key, "BUILD_ID")) {
        return eval_var_set_current(ctx, value, nob_sv_from_cstr(""));
    }
    return true;
}

static bool ctest_set_session_field(EvalExecContext *ctx, const char *field_name, String_View value) {
    if (!ctx || !field_name) return false;
    size_t total = sizeof("NOBIFY_CTEST_SESSION::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    int n = snprintf(buf, total + 1, "NOBIFY_CTEST_SESSION::%s", field_name);
    if (n < 0) return ctx_oom(ctx);
    return eval_var_set_current(ctx, nob_sv_from_cstr(buf), value);
}

static String_View ctest_get_session_field(EvalExecContext *ctx, const char *field_name) {
    if (!ctx || !field_name) return nob_sv_from_cstr("");
    size_t total = sizeof("NOBIFY_CTEST_SESSION::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    int n = snprintf(buf, total + 1, "NOBIFY_CTEST_SESSION::%s", field_name);
    if (n < 0) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return eval_var_get_visible(ctx, nob_sv_from_cstr(buf));
}

static String_View ctest_source_root(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_SOURCE_DIR"));
    return v.count > 0 ? v : eval_current_source_dir(ctx);
}

static String_View ctest_resolve_source_dir(EvalExecContext *ctx, String_View raw) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw.count == 0) return nob_sv_from_cstr("");
    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw, eval_current_source_dir_for_paths(ctx), false);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_normalize_temp(ctx, resolved);
}

static String_View ctest_default_source_setting(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View value = ctest_submit_visible_var(ctx, "CTEST_SOURCE_DIRECTORY", "PROJECT_SOURCE_DIR");
    if (value.count > 0) return ctest_resolve_source_dir(ctx, value);
    return ctest_resolve_source_dir(ctx, ctest_source_root(ctx));
}

static String_View ctest_resolve_binary_dir(EvalExecContext *ctx, String_View raw) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw.count == 0) return nob_sv_from_cstr("");
    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw, ctest_current_binary_dir(ctx), false);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_normalize_temp(ctx, resolved);
}

static String_View ctest_default_binary_setting(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View value = ctest_submit_visible_var(ctx, "CTEST_BINARY_DIRECTORY", "PROJECT_BINARY_DIR");
    if (value.count > 0) return ctest_resolve_binary_dir(ctx, value);
    return ctest_resolve_binary_dir(ctx, ctest_binary_root(ctx));
}

static bool ctest_publish_common_dirs(EvalExecContext *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return false;
    if (source_dir.count > 0 && !eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY"), source_dir)) return false;
    if (binary_dir.count > 0 && !eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY"), binary_dir)) return false;
    return true;
}

static bool ctest_path_exists(EvalExecContext *ctx, String_View path) {
    bool exists = false;
    return eval_service_file_exists(ctx, path, &exists) && exists;
}

static bool ctest_ensure_directory(EvalExecContext *ctx, String_View dir) {
    return eval_service_mkdir(ctx, dir);
}

static String_View ctest_session_resolved_build_dir(EvalExecContext *ctx) {
    String_View build = ctest_get_session_field(ctx, "BUILD");
    if (build.count > 0) return build;
    return ctest_default_binary_setting(ctx);
}

static String_View ctest_session_resolved_source_dir(EvalExecContext *ctx) {
    String_View source = ctest_get_session_field(ctx, "SOURCE");
    if (source.count > 0) return source;
    return ctest_default_source_setting(ctx);
}

static String_View ctest_testing_root(EvalExecContext *ctx, String_View build_dir) {
    if (!ctx || build_dir.count == 0) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), build_dir, nob_sv_from_cstr("Testing"));
}

static String_View ctest_tag_dir(EvalExecContext *ctx, String_View testing_dir, String_View tag) {
    if (!ctx || testing_dir.count == 0 || tag.count == 0) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), testing_dir, tag);
}

typedef struct {
    bool exists;
    String_View tag;
    String_View model;
    String_View group;
} Ctest_Tag_File_Details;

static bool ctest_read_tag_file_details_temp(EvalExecContext *ctx,
                                             String_View path,
                                             Ctest_Tag_File_Details *out_details) {
    if (!ctx || !out_details) return false;
    memset(out_details, 0, sizeof(*out_details));

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!nob_file_exists(path_c) || nob_get_file_type(path_c) == NOB_FILE_DIRECTORY) return true;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return false;

    out_details->exists = true;

    size_t line_start = 0;
    size_t line_index = 0;
    while (line_start <= sb.count && line_index < 3) {
        size_t line_end = line_start;
        while (line_end < sb.count && sb.items[line_end] != '\n' && sb.items[line_end] != '\r') line_end++;

        String_View line = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items + line_start, line_end - line_start));
        if (line_index == 0) out_details->tag = line;
        else if (line_index == 1) out_details->model = line;
        else out_details->group = line;

        while (line_end < sb.count && (sb.items[line_end] == '\n' || sb.items[line_end] == '\r')) line_end++;
        line_start = line_end;
        line_index++;
    }

    nob_sb_free(sb);
    if (out_details->group.count == 0) out_details->group = out_details->model;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static String_View ctest_generate_tag_temp(EvalExecContext *ctx) {
    (void)ctx;
    s_ctest_session_tag_counter++;
    return nob_sv_from_cstr(nob_temp_sprintf("cmk2nob-ctest-%zu", s_ctest_session_tag_counter));
}

static String_View ctest_start_canonical_model(String_View model) {
    if (eval_sv_eq_ci_lit(model, "Experimental")) return nob_sv_from_cstr("Experimental");
    if (eval_sv_eq_ci_lit(model, "Continuous")) return nob_sv_from_cstr("Continuous");
    if (eval_sv_eq_ci_lit(model, "Nightly")) return nob_sv_from_cstr("Nightly");
    return nob_sv_from_cstr("");
}

static String_View ctest_start_checkout_command(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    String_View command = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_CHECKOUT_COMMAND"));
    if (command.count > 0) return command;
    return eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_CVS_CHECKOUT"));
}

static bool ctest_start_run_checkout_command(EvalExecContext *ctx,
                                             const Node *node,
                                             String_View source_dir,
                                             String_View *out_working_directory) {
    if (!ctx || !node) return false;
    if (out_working_directory) *out_working_directory = nob_sv_from_cstr("");

    String_View command = ctest_start_checkout_command(ctx);
    if (command.count == 0) return true;

    SV_List argv = {0};
    if (!eval_split_command_line_temp(ctx, EVAL_CMDLINE_NATIVE, command, &argv)) return false;
    if (arena_arr_len(argv) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_start() checkout command did not produce an executable"),
                              command);
        return false;
    }

    String_View working_directory = eval_sv_path_normalize_temp(ctx, svu_dirname(source_dir));
    if (working_directory.count == 0) working_directory = source_dir;
    if (eval_should_stop(ctx)) return false;

    if (!ctest_ensure_directory(ctx, working_directory)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_start() failed to create checkout working directory"),
                              working_directory);
        return false;
    }

    Eval_Process_Run_Request req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = working_directory,
    };
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &req, &proc)) return false;
    if (!proc.started || proc.exit_code != 0) {
        String_View detail = proc.stderr_text.count > 0 ? proc.stderr_text
                            : proc.result_text.count > 0 ? proc.result_text
                            : command;
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_start() checkout command failed"),
                              detail);
        return false;
    }

    if (out_working_directory) *out_working_directory = working_directory;
    return true;
}

static bool ctest_write_manifest(EvalExecContext *ctx,
                                 String_View command_name,
                                 String_View manifest_path,
                                 String_View tag,
                                 String_View track,
                                 String_View parts,
                                 String_View files) {
    if (!ctx || manifest_path.count == 0) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "COMMAND=");
    nob_sb_append_buf(&sb, command_name.data, command_name.count);
    nob_sb_append_cstr(&sb, "\nTAG=");
    nob_sb_append_buf(&sb, tag.data ? tag.data : "", tag.count);
    nob_sb_append_cstr(&sb, "\nTRACK=");
    nob_sb_append_buf(&sb, track.data ? track.data : "", track.count);
    nob_sb_append_cstr(&sb, "\nPARTS=");
    nob_sb_append_buf(&sb, parts.data ? parts.data : "", parts.count);
    nob_sb_append_cstr(&sb, "\nFILES=");
    nob_sb_append_buf(&sb, files.data ? files.data : "", files.count);
    nob_sb_append_cstr(&sb, "\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, manifest_path, contents, false);
}

static void ctest_xml_append_escaped(Nob_String_Builder *sb, String_View value) {
    if (!sb || value.count == 0) return;

    for (size_t i = 0; i < value.count; i++) {
        char ch = value.data[i];
        switch (ch) {
            case '&': nob_sb_append_cstr(sb, "&amp;"); break;
            case '<': nob_sb_append_cstr(sb, "&lt;"); break;
            case '>': nob_sb_append_cstr(sb, "&gt;"); break;
            case '"': nob_sb_append_cstr(sb, "&quot;"); break;
            case '\'': nob_sb_append_cstr(sb, "&apos;"); break;
            default: nob_sb_append_buf(sb, &ch, 1); break;
        }
    }
}

static bool ctest_xml_append_labels_block(EvalExecContext *ctx,
                                          Nob_String_Builder *sb,
                                          String_View labels) {
    if (!ctx || !sb || labels.count == 0) return true;

    SV_List label_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), labels, &label_items)) {
        return false;
    }

    nob_sb_append_cstr(sb, "  <Labels>\n");
    for (size_t i = 0; i < arena_arr_len(label_items); i++) {
        if (label_items[i].count == 0) continue;
        nob_sb_append_cstr(sb, "    <Label>");
        ctest_xml_append_escaped(sb, label_items[i]);
        nob_sb_append_cstr(sb, "</Label>\n");
    }
    nob_sb_append_cstr(sb, "  </Labels>\n");
    return true;
}

static bool ctest_sv_has_prefix(String_View value, const char *prefix) {
    size_t prefix_len = 0;
    if (!prefix) return false;
    prefix_len = strlen(prefix);
    return value.count >= prefix_len && memcmp(value.data, prefix, prefix_len) == 0;
}

static bool ctest_append_label_items_unique(EvalExecContext *ctx,
                                            String_View labels,
                                            SV_List *out_items) {
    if (!ctx || !out_items) return false;
    if (labels.count == 0) return true;

    SV_List items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), labels, &items)) return false;
    for (size_t i = 0; i < arena_arr_len(items); i++) {
        if (items[i].count == 0) continue;
        if (!ctest_list_push_unique_temp(ctx, out_items, items[i])) return false;
    }
    return true;
}

static bool ctest_labels_intersect(EvalExecContext *ctx, String_View lhs, String_View rhs) {
    if (!ctx || lhs.count == 0 || rhs.count == 0) return false;

    SV_List lhs_items = {0};
    SV_List rhs_items = {0};
    if (!ctest_append_label_items_unique(ctx, lhs, &lhs_items)) return false;
    if (!ctest_append_label_items_unique(ctx, rhs, &rhs_items)) return false;

    for (size_t i = 0; i < arena_arr_len(lhs_items); i++) {
        for (size_t j = 0; j < arena_arr_len(rhs_items); j++) {
            if (nob_sv_eq(lhs_items[i], rhs_items[j])) return true;
        }
    }

    return false;
}

static String_View ctest_merge_labels_temp(EvalExecContext *ctx, String_View lhs, String_View rhs) {
    if (!ctx) return nob_sv_from_cstr("");

    SV_List merged = {0};
    if (!ctest_append_label_items_unique(ctx, lhs, &merged)) return nob_sv_from_cstr("");
    if (!ctest_append_label_items_unique(ctx, rhs, &merged)) return nob_sv_from_cstr("");
    if (arena_arr_len(merged) == 0) return nob_sv_from_cstr("");
    return eval_sv_join_semi_temp(ctx, merged, arena_arr_len(merged));
}

static String_View ctest_coverage_source_path_from_record(EvalExecContext *ctx,
                                                          const Eval_Property_Record *record) {
    static const char scoped_prefix[] = "DIRECTORY::";
    if (!ctx || !record || record->object_id.count == 0) return nob_sv_from_cstr("");

    if (ctest_sv_has_prefix(record->object_id, scoped_prefix)) {
        size_t prefix_len = sizeof(scoped_prefix) - 1;
        size_t split_index = (size_t)-1;
        for (size_t i = prefix_len; i + 1 < record->object_id.count; i++) {
            if (record->object_id.data[i] == ':' && record->object_id.data[i + 1] == ':') {
                split_index = i;
                break;
            }
        }
        if (split_index != (size_t)-1) {
            String_View scope_dir =
                nob_sv_from_parts(record->object_id.data + prefix_len, split_index - prefix_len);
            String_View item_object = nob_sv_from_parts(record->object_id.data + split_index + 2,
                                                        record->object_id.count - (split_index + 2));
            String_View path = item_object;
            if (!eval_sv_is_abs_path(item_object) && scope_dir.count > 0) {
                path = eval_sv_path_join(eval_temp_arena(ctx), scope_dir, item_object);
                if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
            }
            return eval_sv_path_normalize_temp(ctx, path);
        }
    }

    return eval_sv_path_normalize_temp(ctx, record->object_id);
}

static bool ctest_coverage_collect_filtered_sources(EvalExecContext *ctx,
                                                    String_View requested_labels,
                                                    Ctest_Coverage_Source_Entry_List *out_entries) {
    if (!ctx || !out_entries) return false;
    *out_entries = NULL;
    if (requested_labels.count == 0) return true;

    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.properties.records); i++) {
        const Eval_Property_Record *record = &ctx->semantic_state.properties.records[i];
        if (!eval_sv_eq_ci_lit(record->scope_upper, "SOURCE")) continue;
        if (!eval_sv_eq_ci_lit(record->property_upper, "LABELS")) continue;
        if (record->value.count == 0) continue;
        bool matches_filter = ctest_labels_intersect(ctx, requested_labels, record->value);
        if (eval_should_stop(ctx)) return false;
        if (!matches_filter) continue;

        String_View source_path = ctest_coverage_source_path_from_record(ctx, record);
        if (eval_should_stop(ctx)) return false;
        if (source_path.count == 0) continue;

        bool merged = false;
        for (size_t j = 0; j < arena_arr_len(*out_entries); j++) {
            if (!eval_sv_key_eq((*out_entries)[j].path, source_path)) continue;
            (*out_entries)[j].labels =
                ctest_merge_labels_temp(ctx, (*out_entries)[j].labels, record->value);
            if (eval_should_stop(ctx)) return false;
            merged = true;
            break;
        }
        if (merged) continue;

        Ctest_Coverage_Source_Entry entry = {
            .path = source_path,
            .labels = record->value,
        };
        if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), *out_entries, entry)) return false;
    }

    return true;
}

static bool ctest_xml_append_filtered_sources_block(EvalExecContext *ctx,
                                                    Nob_String_Builder *sb,
                                                    String_View requested_labels) {
    if (!ctx || !sb || requested_labels.count == 0) return true;

    Ctest_Coverage_Source_Entry_List entries = {0};
    if (!ctest_coverage_collect_filtered_sources(ctx, requested_labels, &entries)) return false;

    nob_sb_append_cstr(sb, "  <FilteredSourceCount>");
    nob_sb_append_cstr(sb, nob_temp_sprintf("%zu", arena_arr_len(entries)));
    nob_sb_append_cstr(sb, "</FilteredSourceCount>\n");

    nob_sb_append_cstr(sb, "  <FilteredSources>\n");
    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        SV_List label_items = {0};
        if (!ctest_append_label_items_unique(ctx, entries[i].labels, &label_items)) return false;

        nob_sb_append_cstr(sb, "    <Source>\n");
        nob_sb_append_cstr(sb, "      <Path>");
        ctest_xml_append_escaped(sb, entries[i].path);
        nob_sb_append_cstr(sb, "</Path>\n");
        nob_sb_append_cstr(sb, "      <Labels>\n");
        for (size_t j = 0; j < arena_arr_len(label_items); j++) {
            nob_sb_append_cstr(sb, "        <Label>");
            ctest_xml_append_escaped(sb, label_items[j]);
            nob_sb_append_cstr(sb, "</Label>\n");
        }
        nob_sb_append_cstr(sb, "      </Labels>\n");
        nob_sb_append_cstr(sb, "    </Source>\n");
    }
    nob_sb_append_cstr(sb, "  </FilteredSources>\n");
    return true;
}

static String_View ctest_first_nonempty_line_temp(EvalExecContext *ctx, String_View text) {
    if (!ctx || text.count == 0) return nob_sv_from_cstr("");

    size_t i = 0;
    while (i < text.count) {
        while (i < text.count && (text.data[i] == '\n' || text.data[i] == '\r')) i++;
        size_t line_start = i;
        while (i < text.count && text.data[i] != '\n' && text.data[i] != '\r') i++;
        if (i > line_start) {
            return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(text.data + line_start, i - line_start));
        }
    }

    return nob_sv_from_cstr("");
}

static void ctest_coverage_report_local_output(EvalExecContext *ctx,
                                               const Ctest_Coverage_Request *req,
                                               String_View stdout_text) {
    if (!ctx || !req) return;

    String_View summary = ctest_first_nonempty_line_temp(ctx, stdout_text);
    if (summary.count == 0) return;

    nob_log(NOB_INFO,
            "ctest_coverage summary: %.*s",
            (int)summary.count,
            summary.data ? summary.data : "");
}

static bool ctest_sv_contains_ci(String_View haystack, const char *needle) {
    size_t needle_len = 0;
    if (!needle) return false;
    needle_len = strlen(needle);
    if (needle_len == 0 || haystack.count < needle_len) return false;

    for (size_t i = 0; i + needle_len <= haystack.count; i++) {
        bool matches = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (tolower((unsigned char)haystack.data[i + j]) != tolower((unsigned char)needle[j])) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

static bool ctest_build_count_issues(EvalExecContext *ctx,
                                     String_View stdout_text,
                                     String_View stderr_text,
                                     size_t *out_errors,
                                     size_t *out_warnings) {
    if (!ctx || !out_errors || !out_warnings) return false;
    *out_errors = 0;
    *out_warnings = 0;

    String_View streams[2] = {stdout_text, stderr_text};
    for (size_t stream_i = 0; stream_i < 2; stream_i++) {
        String_View text = streams[stream_i];
        size_t i = 0;
        while (i < text.count) {
            size_t line_start = i;
            while (i < text.count && text.data[i] != '\n' && text.data[i] != '\r') i++;
            String_View line = nob_sv_trim(nob_sv_from_parts(text.data + line_start, i - line_start));
            while (i < text.count && (text.data[i] == '\n' || text.data[i] == '\r')) i++;

            if (line.count == 0) continue;
            if (ctest_sv_contains_ci(line, "error")) (*out_errors)++;
            if (ctest_sv_contains_ci(line, "warning")) (*out_warnings)++;
        }
    }

    return true;
}

static bool ctest_write_build_xml(EvalExecContext *ctx,
                                  String_View build_xml_path,
                                  const Ctest_Build_Request *req,
                                  size_t error_count,
                                  size_t warning_count,
                                  String_View stdout_text,
                                  String_View stderr_text,
                                  int exit_code) {
    if (!ctx || build_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Build>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->core.resolved_build);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <BuildCommand>");
    ctest_xml_append_escaped(&sb, req->build_command);
    nob_sb_append_cstr(&sb, "</BuildCommand>\n");

    nob_sb_append_cstr(&sb, "  <Configuration>");
    ctest_xml_append_escaped(&sb, req->configuration);
    nob_sb_append_cstr(&sb, "</Configuration>\n");

    nob_sb_append_cstr(&sb, "  <ParallelLevel>");
    ctest_xml_append_escaped(&sb, req->parallel_level);
    nob_sb_append_cstr(&sb, "</ParallelLevel>\n");

    nob_sb_append_cstr(&sb, "  <Flags>");
    ctest_xml_append_escaped(&sb, req->flags);
    nob_sb_append_cstr(&sb, "</Flags>\n");

    nob_sb_append_cstr(&sb, "  <Target>");
    ctest_xml_append_escaped(&sb, req->target);
    nob_sb_append_cstr(&sb, "</Target>\n");

    nob_sb_append_cstr(&sb, "  <Append>");
    nob_sb_append_cstr(&sb, req->append_mode ? "true" : "false");
    nob_sb_append_cstr(&sb, "</Append>\n");

    nob_sb_append_cstr(&sb, "  <ErrorCount>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", error_count));
    nob_sb_append_cstr(&sb, "</ErrorCount>\n");

    nob_sb_append_cstr(&sb, "  <WarningCount>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", warning_count));
    nob_sb_append_cstr(&sb, "</WarningCount>\n");

    nob_sb_append_cstr(&sb, "  <ExitCode>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", exit_code));
    nob_sb_append_cstr(&sb, "</ExitCode>\n");

    nob_sb_append_cstr(&sb, "  <StdOut>");
    ctest_xml_append_escaped(&sb, stdout_text);
    nob_sb_append_cstr(&sb, "</StdOut>\n");

    nob_sb_append_cstr(&sb, "  <StdErr>");
    ctest_xml_append_escaped(&sb, stderr_text);
    nob_sb_append_cstr(&sb, "</StdErr>\n");

    nob_sb_append_cstr(&sb, "</Build>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, build_xml_path, contents, false);
}

typedef struct {
    String_View source_dir;
    String_View binary_dir;
} Ctest_Test_Stream_Dir;

typedef Ctest_Test_Stream_Dir *Ctest_Test_Stream_Dir_List;

static uint64_t ctest_test_hash_sv(String_View value) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < value.count; i++) {
        hash ^= (unsigned char)value.data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool ctest_test_plan_push_unique_temp(EvalExecContext *ctx,
                                             Ctest_Test_Plan_Entry_List *out_plan,
                                             Ctest_Test_Plan_Entry entry) {
    if (!ctx || !out_plan || entry.name.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(*out_plan); i++) {
        if (!nob_sv_eq((*out_plan)[i].name, entry.name)) continue;
        if (!nob_sv_eq((*out_plan)[i].declared_source_dir, entry.declared_source_dir)) continue;
        return true;
    }
    return arena_arr_push(eval_temp_arena(ctx), *out_plan, entry);
}

static bool ctest_collect_test_plan_from_stream(EvalExecContext *ctx, Ctest_Test_Plan_Entry_List *out_plan) {
    if (!ctx || !out_plan) return false;
    *out_plan = NULL;
    if (!ctx->stream) return true;

    bool testing_enabled = false;
    Ctest_Test_Stream_Dir_List dir_stack = NULL;
    Event_Stream_Iterator it = event_stream_iter(ctx->stream);
    while (event_stream_next(&it)) {
        const Event *ev = it.current;
        if (!ev) continue;

        switch (ev->h.kind) {
        case EVENT_DIRECTORY_ENTER: {
            Ctest_Test_Stream_Dir dir = {
                .source_dir = ev->as.directory_enter.source_dir,
                .binary_dir = ev->as.directory_enter.binary_dir,
            };
            if (!arena_arr_push(eval_temp_arena(ctx), dir_stack, dir)) return false;
            break;
        }
        case EVENT_DIRECTORY_LEAVE:
            if (arena_arr_len(dir_stack) > 0) {
                arena_arr_set_len(dir_stack, arena_arr_len(dir_stack) - 1);
            }
            break;
        case EVENT_TEST_ENABLE:
            testing_enabled = ev->as.test_enable.enabled;
            break;
        case EVENT_TEST_ADD: {
            String_View declared_source_dir =
                arena_arr_len(dir_stack) > 0 ? arena_arr_last(dir_stack).source_dir : ctx->source_dir;
            String_View declared_build_dir =
                arena_arr_len(dir_stack) > 0 ? arena_arr_last(dir_stack).binary_dir : ctx->binary_dir;
            Ctest_Test_Plan_Entry entry = {
                .name = ev->as.test_add.name,
                .command = ev->as.test_add.command,
                .working_dir = ev->as.test_add.working_dir,
                .declared_source_dir = declared_source_dir,
                .declared_build_dir = declared_build_dir,
                .command_expand_lists = ev->as.test_add.command_expand_lists,
            };
            if (!ctest_test_plan_push_unique_temp(ctx, out_plan, entry)) return false;
            break;
        }
        default:
            break;
        }
    }

    if (!testing_enabled) *out_plan = NULL;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static void ctest_test_sort_plan_randomized(Ctest_Test_Plan_Entry_List plan) {
    if (!plan) return;

    for (size_t i = 0; i < arena_arr_len(plan); i++) {
        size_t best = i;
        uint64_t best_hash = ctest_test_hash_sv(plan[i].name) ^ ctest_test_hash_sv(plan[i].declared_source_dir);
        for (size_t j = i + 1; j < arena_arr_len(plan); j++) {
            uint64_t candidate_hash =
                ctest_test_hash_sv(plan[j].name) ^ ctest_test_hash_sv(plan[j].declared_source_dir);
            if (candidate_hash < best_hash) {
                best = j;
                best_hash = candidate_hash;
            }
        }
        if (best != i) {
            Ctest_Test_Plan_Entry tmp = plan[i];
            plan[i] = plan[best];
            plan[best] = tmp;
        }
    }
}

static bool ctest_test_parse_stop_time(EvalExecContext *ctx,
                                       const Node *node,
                                       String_View raw,
                                       int *out_seconds) {
    if (!ctx || !node || !out_seconds) return false;
    *out_seconds = -1;
    if (raw.count == 0) return true;

    int values[3] = {0, 0, 0};
    size_t value_count = 0;
    size_t cursor = 0;
    while (cursor < raw.count) {
        if (value_count >= 3) break;
        size_t start = cursor;
        while (cursor < raw.count && isdigit((unsigned char)raw.data[cursor])) cursor++;
        if (start == cursor) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_test() STOP_TIME requires HH:MM or HH:MM:SS"),
                                  raw);
            return false;
        }
        String_View piece = nob_sv_from_parts(raw.data + start, cursor - start);
        size_t parsed_piece = 0;
        if (!ctest_parse_size_value_sv(piece, &parsed_piece) || parsed_piece > 59) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_test() STOP_TIME has an invalid time component"),
                                  raw);
            return false;
        }
        values[value_count++] = (int)parsed_piece;
        if (cursor == raw.count) break;
        if (raw.data[cursor] != ':') {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_test() STOP_TIME requires HH:MM or HH:MM:SS"),
                                  raw);
            return false;
        }
        cursor++;
    }

    if ((value_count != 2 && value_count != 3) || values[0] > 23) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_test() STOP_TIME requires HH:MM or HH:MM:SS"),
                              raw);
        return false;
    }

    *out_seconds = values[0] * 3600 + values[1] * 60 + values[2];
    return true;
}

static int ctest_test_current_local_seconds_of_day(void) {
    time_t now = time(NULL);
    struct tm local_tm = {0};
#if defined(_WIN32)
    if (localtime_s(&local_tm, &now) != 0) return -1;
#else
    if (!localtime_r(&now, &local_tm)) return -1;
#endif
    return local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;
}

static bool ctest_write_test_xml(EvalExecContext *ctx,
                                 String_View test_xml_path,
                                 const Ctest_Test_Request *req,
                                 Ctest_Test_Result_Entry_List results,
                                 size_t passed_count,
                                 size_t failed_count) {
    if (!ctx || test_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Testing>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->core.resolved_build);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <ParallelLevel>");
    ctest_xml_append_escaped(&sb, req->parallel_level);
    nob_sb_append_cstr(&sb, "</ParallelLevel>\n");

    nob_sb_append_cstr(&sb, "  <StopTime>");
    ctest_xml_append_escaped(&sb, req->stop_time);
    nob_sb_append_cstr(&sb, "</StopTime>\n");

    nob_sb_append_cstr(&sb, "  <TestLoad>");
    ctest_xml_append_escaped(&sb, req->test_load);
    nob_sb_append_cstr(&sb, "</TestLoad>\n");

    nob_sb_append_cstr(&sb, "  <ScheduleRandom>");
    nob_sb_append_cstr(&sb, req->schedule_random ? "true" : "false");
    nob_sb_append_cstr(&sb, "</ScheduleRandom>\n");

    nob_sb_append_cstr(&sb, "  <Append>");
    nob_sb_append_cstr(&sb, req->append_mode ? "true" : "false");
    nob_sb_append_cstr(&sb, "</Append>\n");

    nob_sb_append_cstr(&sb, "  <OutputJunit>");
    ctest_xml_append_escaped(&sb, req->output_junit);
    nob_sb_append_cstr(&sb, "</OutputJunit>\n");

    nob_sb_append_cstr(&sb, "  <TotalTests>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", arena_arr_len(results)));
    nob_sb_append_cstr(&sb, "</TotalTests>\n");

    nob_sb_append_cstr(&sb, "  <PassedTests>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", passed_count));
    nob_sb_append_cstr(&sb, "</PassedTests>\n");

    nob_sb_append_cstr(&sb, "  <FailedTests>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", failed_count));
    nob_sb_append_cstr(&sb, "</FailedTests>\n");

    nob_sb_append_cstr(&sb, "  <TestList>\n");
    for (size_t i = 0; i < arena_arr_len(results); i++) {
        nob_sb_append_cstr(&sb, "    <Test>");
        ctest_xml_append_escaped(&sb, results[i].name);
        nob_sb_append_cstr(&sb, "</Test>\n");
    }
    nob_sb_append_cstr(&sb, "  </TestList>\n");

    nob_sb_append_cstr(&sb, "  <Tests>\n");
    for (size_t i = 0; i < arena_arr_len(results); i++) {
        nob_sb_append_cstr(&sb, "    <Test Name=\"");
        ctest_xml_append_escaped(&sb, results[i].name);
        nob_sb_append_cstr(&sb, "\" Status=\"");
        nob_sb_append_cstr(&sb, results[i].passed ? "passed" : "failed");
        nob_sb_append_cstr(&sb, "\">\n");

        nob_sb_append_cstr(&sb, "      <Command>");
        ctest_xml_append_escaped(&sb, results[i].command);
        nob_sb_append_cstr(&sb, "</Command>\n");

        nob_sb_append_cstr(&sb, "      <WorkingDirectory>");
        ctest_xml_append_escaped(&sb, results[i].working_dir);
        nob_sb_append_cstr(&sb, "</WorkingDirectory>\n");

        nob_sb_append_cstr(&sb, "      <ExitCode>");
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", results[i].exit_code));
        nob_sb_append_cstr(&sb, "</ExitCode>\n");

        nob_sb_append_cstr(&sb, "      <Started>");
        nob_sb_append_cstr(&sb, results[i].started ? "true" : "false");
        nob_sb_append_cstr(&sb, "</Started>\n");

        nob_sb_append_cstr(&sb, "      <TimedOut>");
        nob_sb_append_cstr(&sb, results[i].timed_out ? "true" : "false");
        nob_sb_append_cstr(&sb, "</TimedOut>\n");

        nob_sb_append_cstr(&sb, "      <StdOut>");
        ctest_xml_append_escaped(&sb, results[i].stdout_text);
        nob_sb_append_cstr(&sb, "</StdOut>\n");

        nob_sb_append_cstr(&sb, "      <StdErr>");
        ctest_xml_append_escaped(&sb, results[i].stderr_text);
        nob_sb_append_cstr(&sb, "</StdErr>\n");

        nob_sb_append_cstr(&sb, "      <Result>");
        ctest_xml_append_escaped(&sb, results[i].result_text);
        nob_sb_append_cstr(&sb, "</Result>\n");

        nob_sb_append_cstr(&sb, "    </Test>\n");
    }
    nob_sb_append_cstr(&sb, "  </Tests>\n");
    nob_sb_append_cstr(&sb, "</Testing>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, test_xml_path, contents, false);
}

static bool ctest_write_test_junit(EvalExecContext *ctx,
                                   String_View junit_path,
                                   Ctest_Test_Result_Entry_List results,
                                   size_t failed_count) {
    if (!ctx) return false;
    if (junit_path.count == 0) return true;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuite name=\"ctest_test\" tests=\"");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", arena_arr_len(results)));
    nob_sb_append_cstr(&sb, "\" failures=\"");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", failed_count));
    nob_sb_append_cstr(&sb, "\" errors=\"0\">\n");

    for (size_t i = 0; i < arena_arr_len(results); i++) {
        nob_sb_append_cstr(&sb, "  <testcase name=\"");
        ctest_xml_append_escaped(&sb, results[i].name);
        nob_sb_append_cstr(&sb, "\" classname=\"ctest_test\">\n");
        if (!results[i].passed) {
            nob_sb_append_cstr(&sb, "    <failure message=\"");
            if (results[i].timed_out) {
                ctest_xml_append_escaped(&sb, nob_sv_from_cstr("timed out"));
            } else if (!results[i].started) {
                ctest_xml_append_escaped(&sb, nob_sv_from_cstr("failed to start"));
            } else {
                ctest_xml_append_escaped(&sb,
                                         nob_sv_from_cstr(nob_temp_sprintf("exit code %d", results[i].exit_code)));
            }
            nob_sb_append_cstr(&sb, "\">");
            if (results[i].stderr_text.count > 0) ctest_xml_append_escaped(&sb, results[i].stderr_text);
            else ctest_xml_append_escaped(&sb, results[i].result_text);
            nob_sb_append_cstr(&sb, "</failure>\n");
        }
        nob_sb_append_cstr(&sb, "    <system-out>");
        ctest_xml_append_escaped(&sb, results[i].stdout_text);
        nob_sb_append_cstr(&sb, "</system-out>\n");
        nob_sb_append_cstr(&sb, "    <system-err>");
        ctest_xml_append_escaped(&sb, results[i].stderr_text);
        nob_sb_append_cstr(&sb, "</system-err>\n");
        nob_sb_append_cstr(&sb, "  </testcase>\n");
    }

    nob_sb_append_cstr(&sb, "</testsuite>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, junit_path, contents, false);
}

typedef enum {
    CTEST_REPEAT_NONE = 0,
    CTEST_REPEAT_UNTIL_FAIL,
    CTEST_REPEAT_UNTIL_PASS,
    CTEST_REPEAT_AFTER_TIMEOUT,
} Ctest_Repeat_Mode;

static bool ctest_parse_repeat_mode_and_count(String_View repeat,
                                              Ctest_Repeat_Mode *out_mode,
                                              size_t *out_count) {
    if (!out_mode || !out_count) return false;
    *out_mode = CTEST_REPEAT_NONE;
    *out_count = 1;
    if (repeat.count == 0) return true;

    size_t colon_index = repeat.count;
    for (size_t i = 0; i < repeat.count; i++) {
        if (repeat.data[i] == ':') {
            colon_index = i;
            break;
        }
    }
    if (colon_index == 0 || colon_index + 1 >= repeat.count) return false;

    String_View mode_sv = nob_sv_from_parts(repeat.data, colon_index);
    String_View count_sv = nob_sv_from_parts(repeat.data + colon_index + 1, repeat.count - colon_index - 1);
    size_t count = 0;
    if (!ctest_parse_size_value_sv(count_sv, &count) || count == 0) return false;

    if (eval_sv_eq_ci_lit(mode_sv, "UNTIL_FAIL")) *out_mode = CTEST_REPEAT_UNTIL_FAIL;
    else if (eval_sv_eq_ci_lit(mode_sv, "UNTIL_PASS")) *out_mode = CTEST_REPEAT_UNTIL_PASS;
    else if (eval_sv_eq_ci_lit(mode_sv, "AFTER_TIMEOUT")) *out_mode = CTEST_REPEAT_AFTER_TIMEOUT;
    else return false;

    *out_count = count;
    return true;
}

static size_t ctest_extract_defect_count_from_text(String_View text) {
    size_t total = 0;
    bool saw_explicit_summary = false;
    size_t i = 0;
    while (i < text.count) {
        size_t line_start = i;
        while (i < text.count && text.data[i] != '\n' && text.data[i] != '\r') i++;
        String_View line = nob_sv_trim(nob_sv_from_parts(text.data + line_start, i - line_start));
        while (i < text.count && (text.data[i] == '\n' || text.data[i] == '\r')) i++;
        if (line.count == 0) continue;

        const char *explicit_markers[] = {"ERROR SUMMARY:", "defect count:", "defects="};
        for (size_t marker_i = 0; marker_i < sizeof(explicit_markers) / sizeof(explicit_markers[0]); marker_i++) {
            const char *marker = explicit_markers[marker_i];
            size_t marker_len = strlen(marker);
            for (size_t pos = 0; pos + marker_len <= line.count; pos++) {
                bool match = true;
                for (size_t j = 0; j < marker_len; j++) {
                    if (tolower((unsigned char)line.data[pos + j]) !=
                        tolower((unsigned char)marker[j])) {
                        match = false;
                        break;
                    }
                }
                if (!match) continue;

                size_t digit_pos = pos + marker_len;
                while (digit_pos < line.count && !isdigit((unsigned char)line.data[digit_pos])) digit_pos++;
                if (digit_pos >= line.count) break;
                size_t digit_end = digit_pos;
                while (digit_end < line.count && isdigit((unsigned char)line.data[digit_end])) digit_end++;
                size_t parsed = 0;
                if (ctest_parse_size_value_sv(nob_sv_from_parts(line.data + digit_pos, digit_end - digit_pos),
                                              &parsed)) {
                    total += parsed;
                    saw_explicit_summary = true;
                }
                break;
            }
        }

        if (saw_explicit_summary) continue;
        if (ctest_sv_contains_ci(line, "addresssanitizer") ||
            ctest_sv_contains_ci(line, "leaksanitizer") ||
            ctest_sv_contains_ci(line, "undefinedbehaviorsanitizer") ||
            ctest_sv_contains_ci(line, "runtime error:") ||
            ctest_sv_contains_ci(line, "invalid read") ||
            ctest_sv_contains_ci(line, "invalid write") ||
            ctest_sv_contains_ci(line, "use-after-free") ||
            ctest_sv_contains_ci(line, "definitely lost")) {
            total++;
        }
    }
    return total;
}

static size_t ctest_memcheck_extract_defect_count(String_View stdout_text, String_View stderr_text) {
    return ctest_extract_defect_count_from_text(stdout_text) + ctest_extract_defect_count_from_text(stderr_text);
}

static bool ctest_write_memcheck_xml(EvalExecContext *ctx,
                                     String_View memcheck_xml_path,
                                     const Ctest_Memcheck_Request *req,
                                     Ctest_Test_Result_Entry_List results,
                                     size_t total_defects,
                                     size_t failed_count) {
    if (!ctx || memcheck_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MemCheck>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->core.resolved_build);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <BackendType>");
    ctest_xml_append_escaped(&sb, req->backend_type);
    nob_sb_append_cstr(&sb, "</BackendType>\n");

    nob_sb_append_cstr(&sb, "  <BackendCommand>");
    ctest_xml_append_escaped(&sb, req->backend_command);
    nob_sb_append_cstr(&sb, "</BackendCommand>\n");

    nob_sb_append_cstr(&sb, "  <BackendOptions>");
    ctest_xml_append_escaped(&sb, req->backend_options);
    nob_sb_append_cstr(&sb, "</BackendOptions>\n");

    nob_sb_append_cstr(&sb, "  <SuppressionsFile>");
    ctest_xml_append_escaped(&sb, req->suppression_file);
    nob_sb_append_cstr(&sb, "</SuppressionsFile>\n");

    nob_sb_append_cstr(&sb, "  <OutputJunit>");
    ctest_xml_append_escaped(&sb, req->output_junit);
    nob_sb_append_cstr(&sb, "</OutputJunit>\n");

    nob_sb_append_cstr(&sb, "  <DefectCount>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", total_defects));
    nob_sb_append_cstr(&sb, "</DefectCount>\n");

    nob_sb_append_cstr(&sb, "  <FailedTests>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", failed_count));
    nob_sb_append_cstr(&sb, "</FailedTests>\n");

    nob_sb_append_cstr(&sb, "  <Tests>\n");
    for (size_t i = 0; i < arena_arr_len(results); i++) {
        nob_sb_append_cstr(&sb, "    <Test Name=\"");
        ctest_xml_append_escaped(&sb, results[i].name);
        nob_sb_append_cstr(&sb, "\" Status=\"");
        nob_sb_append_cstr(&sb, results[i].passed ? "passed" : "failed");
        nob_sb_append_cstr(&sb, "\">\n");

        nob_sb_append_cstr(&sb, "      <Command>");
        ctest_xml_append_escaped(&sb, results[i].command);
        nob_sb_append_cstr(&sb, "</Command>\n");

        nob_sb_append_cstr(&sb, "      <BackendCommand>");
        ctest_xml_append_escaped(&sb, results[i].backend_command);
        nob_sb_append_cstr(&sb, "</BackendCommand>\n");

        nob_sb_append_cstr(&sb, "      <WorkingDirectory>");
        ctest_xml_append_escaped(&sb, results[i].working_dir);
        nob_sb_append_cstr(&sb, "</WorkingDirectory>\n");

        nob_sb_append_cstr(&sb, "      <Defects>");
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", results[i].defect_count));
        nob_sb_append_cstr(&sb, "</Defects>\n");

        nob_sb_append_cstr(&sb, "      <ExitCode>");
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", results[i].exit_code));
        nob_sb_append_cstr(&sb, "</ExitCode>\n");

        nob_sb_append_cstr(&sb, "      <StdOut>");
        ctest_xml_append_escaped(&sb, results[i].stdout_text);
        nob_sb_append_cstr(&sb, "</StdOut>\n");

        nob_sb_append_cstr(&sb, "      <StdErr>");
        ctest_xml_append_escaped(&sb, results[i].stderr_text);
        nob_sb_append_cstr(&sb, "</StdErr>\n");

        nob_sb_append_cstr(&sb, "    </Test>\n");
    }
    nob_sb_append_cstr(&sb, "  </Tests>\n");
    nob_sb_append_cstr(&sb, "</MemCheck>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, memcheck_xml_path, contents, false);
}

static bool ctest_update_collect_output_files(EvalExecContext *ctx,
                                              String_View stdout_text,
                                              String_View *out_files,
                                              size_t *out_count) {
    if (!ctx || !out_files || !out_count) return false;
    *out_files = nob_sv_from_cstr("");
    *out_count = 0;

    static const char *const explicit_prefixes[] = {"Updated:", "Modified:", "File:"};
    SV_List explicit_files = {0};
    SV_List fallback_files = {0};
    size_t i = 0;
    while (i < stdout_text.count) {
        size_t line_start = i;
        while (i < stdout_text.count && stdout_text.data[i] != '\n' && stdout_text.data[i] != '\r') i++;
        String_View line = nob_sv_trim(nob_sv_from_parts(stdout_text.data + line_start, i - line_start));
        while (i < stdout_text.count && (stdout_text.data[i] == '\n' || stdout_text.data[i] == '\r')) i++;

        if (line.count == 0) continue;

        bool used_explicit_prefix = false;
        for (size_t prefix_i = 0; prefix_i < sizeof(explicit_prefixes) / sizeof(explicit_prefixes[0]); prefix_i++) {
            size_t prefix_len = strlen(explicit_prefixes[prefix_i]);
            if (!ctest_sv_has_prefix_ci(line, explicit_prefixes[prefix_i])) continue;
            String_View candidate =
                nob_sv_trim(nob_sv_from_parts(line.data + prefix_len, line.count - prefix_len));
            if (candidate.count > 0 &&
                !ctest_list_push_unique_temp(ctx, &explicit_files, candidate)) {
                return false;
            }
            used_explicit_prefix = true;
            break;
        }
        if (used_explicit_prefix) continue;

        if (!ctest_list_push_unique_temp(ctx, &fallback_files, line)) return false;
    }

    SV_List *selected = arena_arr_len(explicit_files) > 0 ? &explicit_files : &fallback_files;
    if (arena_arr_len(*selected) == 0) return true;

    *out_count = arena_arr_len(*selected);
    *out_files = eval_sv_join_semi_temp(ctx, *selected, arena_arr_len(*selected));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_write_update_xml(EvalExecContext *ctx,
                                   String_View update_xml_path,
                                   const Ctest_Update_Request *req,
                                   String_View updated_files,
                                   size_t updated_count,
                                   String_View stdout_text,
                                   String_View stderr_text,
                                   int exit_code) {
    if (!ctx || update_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Update>\n");

    nob_sb_append_cstr(&sb, "  <SourceDirectory>");
    ctest_xml_append_escaped(&sb, req->core.resolved_source);
    nob_sb_append_cstr(&sb, "</SourceDirectory>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->core.resolved_build);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <UpdateType>");
    ctest_xml_append_escaped(&sb, req->update_type);
    nob_sb_append_cstr(&sb, "</UpdateType>\n");

    nob_sb_append_cstr(&sb, "  <UpdateCommand>");
    ctest_xml_append_escaped(&sb, req->update_command);
    nob_sb_append_cstr(&sb, "</UpdateCommand>\n");

    nob_sb_append_cstr(&sb, "  <UpdateOptions>");
    ctest_xml_append_escaped(&sb, req->update_options);
    nob_sb_append_cstr(&sb, "</UpdateOptions>\n");

    nob_sb_append_cstr(&sb, "  <UpdateVersionOnly>");
    nob_sb_append_cstr(&sb, req->version_only ? "true" : "false");
    nob_sb_append_cstr(&sb, "</UpdateVersionOnly>\n");

    nob_sb_append_cstr(&sb, "  <UpdateVersionOverride>");
    ctest_xml_append_escaped(&sb, req->version_override);
    nob_sb_append_cstr(&sb, "</UpdateVersionOverride>\n");

    nob_sb_append_cstr(&sb, "  <UpdatedCount>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", updated_count));
    nob_sb_append_cstr(&sb, "</UpdatedCount>\n");

    nob_sb_append_cstr(&sb, "  <UpdatedFiles>\n");
    if (updated_files.count > 0) {
        SV_List file_items = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), updated_files, &file_items)) {
            nob_sb_free(sb);
            return false;
        }
        for (size_t i = 0; i < arena_arr_len(file_items); i++) {
            if (file_items[i].count == 0) continue;
            nob_sb_append_cstr(&sb, "    <File>");
            ctest_xml_append_escaped(&sb, file_items[i]);
            nob_sb_append_cstr(&sb, "</File>\n");
        }
    }
    nob_sb_append_cstr(&sb, "  </UpdatedFiles>\n");

    nob_sb_append_cstr(&sb, "  <ExitCode>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", exit_code));
    nob_sb_append_cstr(&sb, "</ExitCode>\n");

    nob_sb_append_cstr(&sb, "  <StdOut>");
    ctest_xml_append_escaped(&sb, stdout_text);
    nob_sb_append_cstr(&sb, "</StdOut>\n");

    nob_sb_append_cstr(&sb, "  <StdErr>");
    ctest_xml_append_escaped(&sb, stderr_text);
    nob_sb_append_cstr(&sb, "</StdErr>\n");

    nob_sb_append_cstr(&sb, "</Update>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, update_xml_path, contents, false);
}

static bool ctest_write_upload_xml(EvalExecContext *ctx, String_View upload_xml_path, String_View files) {
    if (!ctx || upload_xml_path.count == 0) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Upload>\n");

    SV_List file_items = {0};
    if (files.count > 0 && !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), files, &file_items)) {
        nob_sb_free(sb);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(file_items); i++) {
        if (file_items[i].count == 0) continue;
        nob_sb_append_cstr(&sb, "  <File>");
        ctest_xml_append_escaped(&sb, file_items[i]);
        nob_sb_append_cstr(&sb, "</File>\n");
    }

    nob_sb_append_cstr(&sb, "</Upload>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, upload_xml_path, contents, false);
}

static bool ctest_write_coverage_xml(EvalExecContext *ctx,
                                     String_View coverage_xml_path,
                                     const Ctest_Coverage_Request *req,
                                     String_View stdout_text,
                                     String_View stderr_text,
                                     int exit_code) {
    if (!ctx || coverage_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Coverage>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->build_dir);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <Command>");
    ctest_xml_append_escaped(&sb, req->coverage_command);
    nob_sb_append_cstr(&sb, "</Command>\n");

    nob_sb_append_cstr(&sb, "  <ExtraFlags>");
    ctest_xml_append_escaped(&sb, req->coverage_extra_flags);
    nob_sb_append_cstr(&sb, "</ExtraFlags>\n");

    nob_sb_append_cstr(&sb, "  <Append>");
    nob_sb_append_cstr(&sb, req->append_mode ? "true" : "false");
    nob_sb_append_cstr(&sb, "</Append>\n");

    if (!ctest_xml_append_labels_block(ctx, &sb, req->labels)) {
        nob_sb_free(sb);
        return false;
    }
    if (!ctest_xml_append_filtered_sources_block(ctx, &sb, req->labels)) {
        nob_sb_free(sb);
        return false;
    }

    nob_sb_append_cstr(&sb, "  <ExitCode>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", exit_code));
    nob_sb_append_cstr(&sb, "</ExitCode>\n");

    nob_sb_append_cstr(&sb, "  <StdOut>");
    ctest_xml_append_escaped(&sb, stdout_text);
    nob_sb_append_cstr(&sb, "</StdOut>\n");

    nob_sb_append_cstr(&sb, "  <StdErr>");
    ctest_xml_append_escaped(&sb, stderr_text);
    nob_sb_append_cstr(&sb, "</StdErr>\n");

    nob_sb_append_cstr(&sb, "</Coverage>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, coverage_xml_path, contents, false);
}

static bool ctest_write_configure_xml(EvalExecContext *ctx,
                                      String_View configure_xml_path,
                                      const Ctest_Configure_Request *req,
                                      String_View stdout_text,
                                      String_View stderr_text,
                                      int exit_code) {
    if (!ctx || configure_xml_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Configure>\n");

    nob_sb_append_cstr(&sb, "  <SourceDirectory>");
    ctest_xml_append_escaped(&sb, req->resolved_source);
    nob_sb_append_cstr(&sb, "</SourceDirectory>\n");

    nob_sb_append_cstr(&sb, "  <BuildDirectory>");
    ctest_xml_append_escaped(&sb, req->build_dir);
    nob_sb_append_cstr(&sb, "</BuildDirectory>\n");

    nob_sb_append_cstr(&sb, "  <ConfigureCommand>");
    ctest_xml_append_escaped(&sb, req->configure_command);
    nob_sb_append_cstr(&sb, "</ConfigureCommand>\n");

    nob_sb_append_cstr(&sb, "  <Options>");
    ctest_xml_append_escaped(&sb, req->configure_options);
    nob_sb_append_cstr(&sb, "</Options>\n");

    nob_sb_append_cstr(&sb, "  <Append>");
    nob_sb_append_cstr(&sb, req->append_mode ? "true" : "false");
    nob_sb_append_cstr(&sb, "</Append>\n");

    if (!ctest_xml_append_labels_block(ctx, &sb, req->labels_for_subprojects)) {
        nob_sb_free(sb);
        return false;
    }

    nob_sb_append_cstr(&sb, "  <ExitCode>");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", exit_code));
    nob_sb_append_cstr(&sb, "</ExitCode>\n");

    nob_sb_append_cstr(&sb, "  <StdOut>");
    ctest_xml_append_escaped(&sb, stdout_text);
    nob_sb_append_cstr(&sb, "</StdOut>\n");

    nob_sb_append_cstr(&sb, "  <StdErr>");
    ctest_xml_append_escaped(&sb, stderr_text);
    nob_sb_append_cstr(&sb, "</StdErr>\n");

    nob_sb_append_cstr(&sb, "</Configure>\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, configure_xml_path, contents, false);
}

static bool ctest_write_submit_manifest(EvalExecContext *ctx,
                                        String_View manifest_path,
                                        String_View tag,
                                        String_View track,
                                        const Ctest_Submit_Request *req) {
    if (!ctx || manifest_path.count == 0 || !req) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "COMMAND=ctest_submit\n");
    nob_sb_append_cstr(&sb, "SIGNATURE=");
    nob_sb_append_cstr(&sb, req->is_cdash_upload ? "CDASH_UPLOAD" : "DEFAULT");
    nob_sb_append_cstr(&sb, "\nTAG=");
    nob_sb_append_buf(&sb, tag.data ? tag.data : "", tag.count);
    nob_sb_append_cstr(&sb, "\nTRACK=");
    nob_sb_append_buf(&sb, track.data ? track.data : "", track.count);
    nob_sb_append_cstr(&sb, "\nPARTS=");
    nob_sb_append_buf(&sb, req->parts.data ? req->parts.data : "", req->parts.count);
    nob_sb_append_cstr(&sb, "\nFILES=");
    nob_sb_append_buf(&sb, req->files.data ? req->files.data : "", req->files.count);
    nob_sb_append_cstr(&sb, "\nSUBMIT_URL=");
    nob_sb_append_buf(&sb, req->submit_url.data ? req->submit_url.data : "", req->submit_url.count);
    nob_sb_append_cstr(&sb, "\nHTTPHEADERS=");
    nob_sb_append_buf(&sb, req->http_headers.data ? req->http_headers.data : "", req->http_headers.count);
    nob_sb_append_cstr(&sb, "\nRETRY_COUNT=");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", req->retry_count));
    nob_sb_append_cstr(&sb, "\nRETRY_DELAY=");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", req->retry_delay_sec));
    nob_sb_append_cstr(&sb, "\nINACTIVITY_TIMEOUT=");
    if (req->has_inactivity_timeout) {
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", req->inactivity_timeout_sec));
    }
    nob_sb_append_cstr(&sb, "\nCDASH_UPLOAD=");
    nob_sb_append_buf(&sb, req->cdash_upload_file.data ? req->cdash_upload_file.data : "", req->cdash_upload_file.count);
    nob_sb_append_cstr(&sb, "\nCDASH_UPLOAD_TYPE=");
    nob_sb_append_buf(&sb, req->cdash_upload_type.data ? req->cdash_upload_type.data : "", req->cdash_upload_type.count);
    nob_sb_append_cstr(&sb, "\n");

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return eval_write_text_file(ctx, manifest_path, contents, false);
}

static bool ctest_ensure_session_tag(EvalExecContext *ctx,
                                     String_View build_dir_override,
                                     String_View model,
                                     String_View group,
                                     bool append_mode,
                                     String_View *out_tag,
                                     String_View *out_testing_dir,
                                     String_View *out_tag_file,
                                     String_View *out_tag_dir) {
    if (!ctx || !out_tag || !out_testing_dir || !out_tag_file || !out_tag_dir) return false;

    String_View build_dir = build_dir_override.count > 0 ? build_dir_override : ctest_session_resolved_build_dir(ctx);
    String_View testing_dir = ctest_get_session_field(ctx, "TESTING_DIR");
    if (testing_dir.count == 0) testing_dir = ctest_testing_root(ctx, build_dir);
    if (eval_should_stop(ctx)) return false;

    String_View tag_file = eval_sv_path_join(eval_temp_arena(ctx), testing_dir, nob_sv_from_cstr("TAG"));
    if (eval_should_stop(ctx)) return false;

    String_View tag = ctest_get_session_field(ctx, "TAG");
    if (tag.count == 0 && append_mode && ctest_path_exists(ctx, tag_file)) {
        Ctest_Tag_File_Details details = {0};
        if (!ctest_read_tag_file_details_temp(ctx, tag_file, &details)) return false;
        tag = details.tag;
    }
    if (tag.count == 0) tag = ctest_generate_tag_temp(ctx);

    String_View tag_dir_path = ctest_tag_dir(ctx, testing_dir, tag);
    if (eval_should_stop(ctx)) return false;

    if (!ctest_ensure_directory(ctx, testing_dir)) return false;
    if (!ctest_ensure_directory(ctx, tag_dir_path)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, tag.data, tag.count);
    nob_sb_append_cstr(&sb, "\n");
    nob_sb_append_buf(&sb, model.data ? model.data : "", model.count);
    nob_sb_append_cstr(&sb, "\n");
    nob_sb_append_buf(&sb, group.data ? group.data : "", group.count);
    nob_sb_append_cstr(&sb, "\n");
    String_View tag_contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    if (!eval_write_text_file(ctx, tag_file, tag_contents, false)) return false;

    if (!ctest_set_session_field(ctx, "TAG", tag)) return false;
    if (!ctest_set_session_field(ctx, "TESTING_DIR", testing_dir)) return false;
    if (!ctest_set_session_field(ctx, "TAG_FILE", tag_file)) return false;
    if (!ctest_set_session_field(ctx, "TAG_DIR", tag_dir_path)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_TAG"), tag)) return false;
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), build_dir)) return false;

    *out_tag = tag;
    *out_testing_dir = testing_dir;
    *out_tag_file = tag_file;
    *out_tag_dir = tag_dir_path;
    return true;
}

static bool ctest_resolve_files(EvalExecContext *ctx,
                                const Node *node,
                                String_View command_name,
                                String_View raw_files,
                                String_View base_dir,
                                bool required,
                                bool validate_exists,
                                String_View *out_resolved) {
    if (!ctx || !node || !out_resolved) return false;
    *out_resolved = nob_sv_from_cstr("");

    if (raw_files.count == 0) {
        if (required) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest command requires FILES"),
                                  command_name);
            return false;
        }
        return true;
    }

    SV_List raw_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_files, &raw_items)) return false;

    SV_List resolved_items = {0};
    for (size_t i = 0; i < arena_arr_len(raw_items); i++) {
        if (raw_items[i].count == 0) continue;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw_items[i], base_dir, false);
        resolved = eval_sv_path_normalize_temp(ctx, resolved);
        if (eval_should_stop(ctx)) return false;
        if (validate_exists && !ctest_path_exists(ctx, resolved)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest command file does not exist"),
                                  resolved);
            return false;
        }
        if (!svu_list_push_temp(ctx, &resolved_items, resolved)) return false;
    }

    if (required && arena_arr_len(resolved_items) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest command requires one or more files"),
                              command_name);
        return false;
    }

    *out_resolved = eval_sv_join_semi_temp(ctx, resolved_items, arena_arr_len(resolved_items));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_resolve_scripts(EvalExecContext *ctx,
                                  const SV_List raw_scripts,
                                  String_View base_dir,
                                  String_View **out_resolved_scripts) {
    if (!ctx || !out_resolved_scripts) return false;
    *out_resolved_scripts = NULL;

    for (size_t i = 0; i < arena_arr_len(raw_scripts); i++) {
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw_scripts[i], base_dir, false);
        resolved = eval_sv_path_normalize_temp(ctx, resolved);
        if (eval_should_stop(ctx)) return false;
        if (!eval_sv_arr_push_temp(ctx, out_resolved_scripts, resolved)) return false;
    }
    return true;
}

static bool ctest_copy_sv_list_to_event(EvalExecContext *ctx,
                                        const SV_List src,
                                        SV_List *out_dst) {
    if (!ctx || !out_dst) return false;
    *out_dst = NULL;

    for (size_t i = 0; i < arena_arr_len(src); i++) {
        if (!eval_sv_arr_push_event(ctx, out_dst, src[i])) return false;
    }
    return true;
}

static bool ctest_new_process_begin(EvalExecContext *ctx, Ctest_New_Process_State *state) {
    if (!ctx || !state) return false;
    memset(state, 0, sizeof(*state));

    state->message_check_count = arena_arr_len(ctx->message_check_stack);
    state->user_commands_count = arena_arr_len(ctx->command_state.user_commands);
    state->known_targets_count = arena_arr_len(ctx->semantic_state.targets.records);
    state->alias_targets_count = arena_arr_len(ctx->semantic_state.targets.aliases);
    state->dependency_provider_command_name = ctx->semantic_state.package.dependency_provider.command_name;
    state->dependency_provider_supports_find_package = ctx->semantic_state.package.dependency_provider.supports_find_package;
    state->dependency_provider_supports_fetchcontent_makeavailable_serial =
        ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial;
    state->dependency_provider_active_find_package_depth =
        ctx->semantic_state.package.dependency_provider.active_find_package_depth;
    state->dependency_provider_active_fetchcontent_makeavailable_depth =
        ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth;
    state->active_find_packages_count = arena_arr_len(ctx->semantic_state.package.active_find_packages);
    state->fetchcontent_declarations_count = arena_arr_len(ctx->semantic_state.fetchcontent.declarations);
    state->fetchcontent_states_count = arena_arr_len(ctx->semantic_state.fetchcontent.states);
    state->active_fetchcontent_makeavailable_count =
        arena_arr_len(ctx->semantic_state.fetchcontent.active_makeavailable);
    state->watched_variables_count = arena_arr_len(ctx->command_state.watched_variables);
    state->watched_variable_commands_count = arena_arr_len(ctx->command_state.watched_variable_commands);
    state->cpack_module_loaded = ctx->cpack_module_loaded;
    state->cpack_component_module_loaded = ctx->cpack_component_module_loaded;
    state->fetchcontent_module_loaded = ctx->fetchcontent_module_loaded;

    if (!eval_scope_push(ctx)) return false;
    state->scope_pushed = true;
    return true;
}

static void ctest_new_process_end(EvalExecContext *ctx, const Ctest_New_Process_State *state) {
    if (!ctx || !state) return;

    if (state->scope_pushed) eval_scope_pop(ctx);
    if (ctx->message_check_stack) {
        arena_arr_set_len(ctx->message_check_stack, state->message_check_count);
    }
    if (ctx->command_state.user_commands) {
        arena_arr_set_len(ctx->command_state.user_commands, state->user_commands_count);
    }
    if (ctx->semantic_state.targets.records) {
        arena_arr_set_len(ctx->semantic_state.targets.records, state->known_targets_count);
    }
    if (ctx->semantic_state.targets.aliases) {
        arena_arr_set_len(ctx->semantic_state.targets.aliases, state->alias_targets_count);
    }
    ctx->semantic_state.package.dependency_provider.command_name = state->dependency_provider_command_name;
    ctx->semantic_state.package.dependency_provider.supports_find_package = state->dependency_provider_supports_find_package;
    ctx->semantic_state.package.dependency_provider.supports_fetchcontent_makeavailable_serial =
        state->dependency_provider_supports_fetchcontent_makeavailable_serial;
    ctx->semantic_state.package.dependency_provider.active_find_package_depth =
        state->dependency_provider_active_find_package_depth;
    ctx->semantic_state.package.dependency_provider.active_fetchcontent_makeavailable_depth =
        state->dependency_provider_active_fetchcontent_makeavailable_depth;
    if (ctx->semantic_state.package.active_find_packages) {
        arena_arr_set_len(ctx->semantic_state.package.active_find_packages, state->active_find_packages_count);
    }
    if (ctx->semantic_state.fetchcontent.declarations) {
        arena_arr_set_len(ctx->semantic_state.fetchcontent.declarations, state->fetchcontent_declarations_count);
    }
    if (ctx->semantic_state.fetchcontent.states) {
        arena_arr_set_len(ctx->semantic_state.fetchcontent.states, state->fetchcontent_states_count);
    }
    if (ctx->semantic_state.fetchcontent.active_makeavailable) {
        arena_arr_set_len(ctx->semantic_state.fetchcontent.active_makeavailable,
                          state->active_fetchcontent_makeavailable_count);
    }
    if (ctx->command_state.watched_variables) {
        arena_arr_set_len(ctx->command_state.watched_variables, state->watched_variables_count);
    }
    if (ctx->command_state.watched_variable_commands) {
        arena_arr_set_len(ctx->command_state.watched_variable_commands, state->watched_variable_commands_count);
    }
    ctx->cpack_module_loaded = state->cpack_module_loaded;
    ctx->cpack_component_module_loaded = state->cpack_component_module_loaded;
    ctx->fetchcontent_module_loaded = state->fetchcontent_module_loaded;
    eval_exec_clear_pending_flow(ctx);
    eval_clear_return_state(ctx);
    eval_reset_stop_request(ctx);
}

static bool ctest_apply_resolved_context(EvalExecContext *ctx,
                                         String_View command_name,
                                         const Ctest_Parse_Result *parsed,
                                         bool needs_source,
                                         bool needs_build,
                                         String_View *out_resolved_source,
                                         String_View *out_resolved_build) {
    if (!ctx || !parsed) return false;

    String_View resolved_source = nob_sv_from_cstr("");
    String_View resolved_build = nob_sv_from_cstr("");

    if (needs_source) {
        String_View source = ctest_parsed_field_value(parsed, "SOURCE");
        if (source.count > 0) {
            resolved_source = ctest_resolve_source_dir(ctx, source);
        } else {
            resolved_source = ctest_session_resolved_source_dir(ctx);
        }
        if (eval_should_stop(ctx)) return false;
        if (!ctest_set_field(ctx, command_name, "RESOLVED_SOURCE", resolved_source)) return false;
    }

    if (needs_build) {
        String_View build = ctest_parsed_field_value(parsed, "BUILD");
        if (build.count > 0) {
            resolved_build = ctest_resolve_binary_dir(ctx, build);
        } else {
            resolved_build = ctest_session_resolved_build_dir(ctx);
        }
        if (eval_should_stop(ctx)) return false;
        if (!ctest_set_field(ctx, command_name, "RESOLVED_BUILD", resolved_build)) return false;
    }

    if (out_resolved_source) *out_resolved_source = resolved_source;
    if (out_resolved_build) *out_resolved_build = resolved_build;
    return ctest_publish_common_dirs(ctx, resolved_source, resolved_build);
}

static bool ctest_parse_generic(EvalExecContext *ctx,
                                const Node *node,
                                String_View command_name,
                                const SV_List *args,
                                const Ctest_Parse_Spec *spec,
                                Ctest_Parse_Result *out_res) {
    if (!ctx || !node || !args || !spec || !out_res) return false;
    memset(out_res, 0, sizeof(*out_res));

    for (size_t i = 0; i < arena_arr_len(*args); i++) {
        String_View tok = (*args)[i];

        if (ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count)) {
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, nob_sv_from_cstr("1"))) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), nob_sv_from_cstr("1"))) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count)) {
            if (i + 1 >= arena_arr_len(*args)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command keyword requires a value"),
                                      tok);
                return false;
            }
            String_View value = (*args)[++i];
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, value)) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), value)) return false;
            if (!ctest_publish_var_keyword(ctx, tok, value)) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count)) {
            SV_List items = {0};
            size_t j = i + 1;
            for (; j < arena_arr_len(*args); j++) {
                if (ctest_is_any_known_keyword((*args)[j], spec)) break;
                if (!svu_list_push_temp(ctx, &items, (*args)[j])) return false;
            }
            if (arena_arr_len(items) == 0) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command list keyword requires one or more values"),
                                      tok);
                return false;
            }
            String_View joined = eval_sv_join_semi_temp(ctx, items, arena_arr_len(items));
            if (!ctest_record_parsed_field(eval_temp_arena(ctx), out_res, tok, joined)) return false;
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), joined)) {
                return false;
            }
            i = j - 1;
            continue;
        }

        if (!svu_list_push_temp(ctx, &out_res->positionals, tok)) return false;
    }

    if (arena_arr_len(out_res->positionals) < spec->min_positionals || arena_arr_len(out_res->positionals) > spec->max_positionals) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest command received an invalid number of positional arguments"),
                              command_name);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(out_res->positionals); i++) {
        char field[32];
        snprintf(field, sizeof(field), "POSITIONAL_%zu", i);
        if (!ctest_set_field(ctx, command_name, field, out_res->positionals[i])) return false;
    }

    return true;
}

static String_View ctest_current_binary_dir(EvalExecContext *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View ctest_binary_root(EvalExecContext *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

typedef struct {
    const char *name;
    const char *status_command;
} Ctest_Submit_Part_Def;

static const Ctest_Submit_Part_Def s_ctest_submit_part_defs[] = {
    {"Start", "ctest_start"},
    {"Update", "ctest_update"},
    {"Configure", "ctest_configure"},
    {"Build", "ctest_build"},
    {"Test", "ctest_test"},
    {"Coverage", "ctest_coverage"},
    {"MemCheck", "ctest_memcheck"},
    {"Notes", NULL},
    {"ExtraFiles", NULL},
    {"Upload", "ctest_upload"},
    {"Submit", NULL},
    {"Done", NULL},
};

static String_View ctest_submit_canonical_part(String_View raw_part) {
    for (size_t i = 0; i < sizeof(s_ctest_submit_part_defs) / sizeof(s_ctest_submit_part_defs[0]); i++) {
        if (eval_sv_eq_ci_lit(raw_part, s_ctest_submit_part_defs[i].name)) {
            return nob_sv_from_cstr(s_ctest_submit_part_defs[i].name);
        }
    }
    return nob_sv_from_cstr("");
}

static bool ctest_submit_step_is_available(EvalExecContext *ctx, const Eval_Ctest_Step_Record *step) {
    if (!ctx || !step || step->status.count == 0) return false;
    if (!ctest_step_kind_requires_submit_files(step->kind)) return true;
    return eval_ctest_step_submit_files(ctx, step).count > 0;
}

static bool ctest_submit_part_is_available(EvalExecContext *ctx, const Ctest_Submit_Part_Def *part_def) {
    if (!ctx || !part_def || !part_def->name) return false;
    if (part_def->status_command) {
        const Eval_Ctest_Step_Record *step =
            eval_ctest_find_last_step_by_part(ctx, nob_sv_from_cstr(part_def->name));
        return ctest_submit_step_is_available(ctx, step);
    }
    if (strcmp(part_def->name, "Notes") == 0) {
        return eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_NOTES_FILES")).count > 0;
    }
    if (strcmp(part_def->name, "ExtraFiles") == 0) {
        return eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_EXTRA_SUBMIT_FILES")).count > 0;
    }
    return false;
}

static bool ctest_append_joined_list_unique(EvalExecContext *ctx, String_View joined, SV_List *out_items) {
    if (!ctx || !out_items) return false;
    if (joined.count == 0) return true;

    SV_List items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), joined, &items)) return false;
    for (size_t i = 0; i < arena_arr_len(items); i++) {
        if (items[i].count == 0) continue;
        if (!ctest_list_push_unique_temp(ctx, out_items, items[i])) return false;
    }
    return true;
}

static bool ctest_submit_resolve_parts(EvalExecContext *ctx,
                                       const Node *node,
                                       const Ctest_Parse_Result *parsed,
                                       String_View *out_parts) {
    if (!ctx || !node || !parsed || !out_parts) return false;
    *out_parts = nob_sv_from_cstr("");

    String_View raw_parts = ctest_join_parsed_field_values(ctx, parsed, "PARTS");
    if (eval_should_stop(ctx)) return false;

    SV_List resolved_parts = {0};
    if (raw_parts.count > 0) {
        SV_List input_parts = {0};
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw_parts, &input_parts)) return false;
        for (size_t i = 0; i < arena_arr_len(input_parts); i++) {
            if (input_parts[i].count == 0) continue;
            String_View canonical = ctest_submit_canonical_part(input_parts[i]);
            if (canonical.count == 0) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest_submit() received an invalid PARTS value"),
                                      input_parts[i]);
                return false;
            }
            if (!ctest_list_push_unique_temp(ctx, &resolved_parts, canonical)) return false;
        }
    } else {
        for (size_t i = 0; i < sizeof(s_ctest_submit_part_defs) / sizeof(s_ctest_submit_part_defs[0]); i++) {
            if (!ctest_submit_part_is_available(ctx, &s_ctest_submit_part_defs[i])) continue;
            if (!ctest_list_push_unique_temp(ctx,
                                             &resolved_parts,
                                             nob_sv_from_cstr(s_ctest_submit_part_defs[i].name))) {
                return false;
            }
        }
    }

    *out_parts = eval_sv_join_semi_temp(ctx, resolved_parts, arena_arr_len(resolved_parts));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_submit_append_resolved_files(EvalExecContext *ctx,
                                               const Node *node,
                                               String_View command_name,
                                               String_View raw_files,
                                               String_View base_dir,
                                               bool required,
                                               SV_List *out_files) {
    if (!ctx || !node || !out_files) return false;

    String_View joined = nob_sv_from_cstr("");
    if (!ctest_resolve_files(ctx, node, command_name, raw_files, base_dir, required, true, &joined)) return false;
    return ctest_append_joined_list_unique(ctx, joined, out_files);
}

static String_View ctest_submit_resolve_legacy_url(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");

    String_View method = ctest_submit_visible_var(ctx, "DROP_METHOD", "CTEST_DROP_METHOD");
    String_View site = ctest_submit_visible_var(ctx, "DROP_SITE", "CTEST_DROP_SITE");
    String_View location = ctest_submit_visible_var(ctx, "DROP_LOCATION", "CTEST_DROP_LOCATION");
    if (method.count == 0 || site.count == 0 || location.count == 0) return nob_sv_from_cstr("");

    String_View user = ctest_submit_visible_var(ctx, "DROP_SITE_USER", "CTEST_DROP_SITE_USER");
    String_View password = ctest_submit_visible_var(ctx, "DROP_SITE_PASSWORD", "CTEST_DROP_SITE_PASSWORD");

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, method.data, method.count);
    nob_sb_append_cstr(&sb, "://");
    if (user.count > 0) {
        nob_sb_append_buf(&sb, user.data, user.count);
        if (password.count > 0) {
            nob_sb_append_cstr(&sb, ":");
            nob_sb_append_buf(&sb, password.data, password.count);
        }
        nob_sb_append_cstr(&sb, "@");
    }
    nob_sb_append_buf(&sb, site.data, site.count);
    if (location.count > 0 && location.data[0] != '/') nob_sb_append_cstr(&sb, "/");
    nob_sb_append_buf(&sb, location.data, location.count);

    String_View url = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return url;
}

static String_View ctest_submit_resolve_url(EvalExecContext *ctx, const Ctest_Parse_Result *parsed) {
    if (!ctx || !parsed) return nob_sv_from_cstr("");

    String_View submit_url = ctest_parsed_field_value(parsed, "SUBMIT_URL");
    if (submit_url.count > 0) return submit_url;

    submit_url = ctest_submit_visible_var(ctx, "SUBMIT_URL", "CTEST_SUBMIT_URL");
    if (submit_url.count > 0) return submit_url;

    return ctest_submit_resolve_legacy_url(ctx);
}

typedef struct {
    bool started;
    bool timed_out;
    int exit_code;
    long http_code;
    String_View body;
    String_View stderr_text;
    String_View result_text;
} Ctest_Submit_Curl_Result;

static bool ctest_submit_run_curl(EvalExecContext *ctx,
                                  SV_List argv,
                                  Ctest_Submit_Curl_Result *out_result) {
    if (!ctx || !out_result) return false;
    memset(out_result, 0, sizeof(*out_result));

    Eval_Process_Run_Request req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = ctest_current_binary_dir(ctx),
    };
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &req, &proc)) return false;

    out_result->started = proc.started;
    out_result->timed_out = proc.timed_out;
    out_result->exit_code = proc.exit_code;
    out_result->stderr_text = proc.stderr_text;
    out_result->result_text = proc.result_text;
    out_result->body = proc.stdout_text;
    out_result->http_code = 0;

    static const char *marker = "\n__NOBIFY_HTTP_CODE=";
    size_t marker_len = strlen(marker);
    if (proc.stdout_text.count >= marker_len) {
        for (size_t i = proc.stdout_text.count - marker_len + 1; i-- > 0;) {
            if (memcmp(proc.stdout_text.data + i, marker, marker_len) != 0) continue;

            size_t value_start = i + marker_len;
            size_t value_end = value_start;
            while (value_end < proc.stdout_text.count &&
                   proc.stdout_text.data[value_end] != '\n' &&
                   proc.stdout_text.data[value_end] != '\r') {
                value_end++;
            }

            String_View raw_code = nob_sv_from_parts(proc.stdout_text.data + value_start, value_end - value_start);
            if (raw_code.count > 0) {
                char buf[32];
                if (raw_code.count < sizeof(buf)) {
                    memcpy(buf, raw_code.data, raw_code.count);
                    buf[raw_code.count] = '\0';
                    out_result->http_code = strtol(buf, NULL, 10);
                }
            }

            out_result->body = nob_sv_from_parts(proc.stdout_text.data, i);
            while (out_result->body.count > 0) {
                char tail = out_result->body.data[out_result->body.count - 1];
                if (tail != '\n' && tail != '\r') break;
                out_result->body.count--;
            }
            break;
        }
    }

    return true;
}

static bool ctest_submit_append_curl_common_args(EvalExecContext *ctx,
                                                 const Ctest_Submit_Request *req,
                                                 SV_List *out_argv) {
    if (!ctx || !req || !out_argv) return false;

    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("curl"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--silent"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--show-error"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--location"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--fail"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--request"))) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("POST"))) return false;

    if (req->has_inactivity_timeout) {
        if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--speed-time"))) return false;
        if (!svu_list_push_temp(ctx, out_argv, ctest_submit_format_size_temp(ctx, req->inactivity_timeout_sec))) {
            return false;
        }
        if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--speed-limit"))) return false;
        if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("1"))) return false;
    }

    if (req->has_tls_verify && !req->tls_verify) {
        if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--insecure"))) return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->http_header_items); i++) {
        if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--header"))) return false;
        if (!svu_list_push_temp(ctx, out_argv, req->http_header_items[i])) return false;
    }

    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--write-out"))) return false;
    if (!svu_list_push_temp(ctx,
                            out_argv,
                            nob_sv_from_cstr("\n__NOBIFY_HTTP_CODE=%{http_code}\n"))) {
        return false;
    }

    return true;
}

static bool ctest_submit_read_file_temp(EvalExecContext *ctx,
                                        String_View path,
                                        String_View *out_contents) {
    if (!ctx || !out_contents) return false;
    *out_contents = nob_sv_from_cstr("");

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return false;
    *out_contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static String_View ctest_submit_lower_ascii_temp(EvalExecContext *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < input.count; i++) {
        unsigned char ch = (unsigned char)input.data[i];
        buf[i] = (char)tolower(ch);
    }
    buf[input.count] = '\0';
    return nob_sv_from_parts(buf, input.count);
}

static bool ctest_submit_contains_sv(String_View haystack, String_View needle) {
    if (needle.count == 0) return true;
    if (haystack.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= haystack.count; i++) {
        if (memcmp(haystack.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static String_View ctest_submit_extract_build_id(EvalExecContext *ctx, String_View body) {
    if (!ctx || body.count == 0) return nob_sv_from_cstr("");

    static const char *const keys[] = {"buildid", "build_id", "build-id"};
    String_View lower = ctest_submit_lower_ascii_temp(ctx, body);
    if (lower.count == 0 && body.count > 0) return nob_sv_from_cstr("");

    for (size_t k = 0; k < NOB_ARRAY_LEN(keys); k++) {
        size_t key_len = strlen(keys[k]);
        for (size_t i = 0; i + key_len <= lower.count; i++) {
            if (memcmp(lower.data + i, keys[k], key_len) != 0) continue;

            size_t j = i + key_len;
            while (j < lower.count &&
                   (lower.data[j] == '"' ||
                    lower.data[j] == '\'' ||
                    isspace((unsigned char)lower.data[j]))) {
                j++;
            }
            if (j >= lower.count ||
                (lower.data[j] != ':' && lower.data[j] != '=' && lower.data[j] != '>')) {
                continue;
            }
            j++;
            while (j < lower.count &&
                   (lower.data[j] == '"' ||
                    lower.data[j] == '\'' ||
                    isspace((unsigned char)lower.data[j]))) {
                j++;
            }

            size_t start = j;
            while (j < lower.count && isdigit((unsigned char)lower.data[j])) j++;
            if (j > start) return nob_sv_from_parts(body.data + start, j - start);
        }
    }

    return nob_sv_from_cstr("");
}

static bool ctest_submit_response_requests_upload(EvalExecContext *ctx, String_View body) {
    if (!ctx || body.count == 0) return true;
    String_View lower = ctest_submit_lower_ascii_temp(ctx, body);
    if (ctest_submit_contains_sv(lower, nob_sv_from_cstr("upload=0")) ||
        ctest_submit_contains_sv(lower, nob_sv_from_cstr("\"upload\":0")) ||
        ctest_submit_contains_sv(lower, nob_sv_from_cstr("upload=false")) ||
        ctest_submit_contains_sv(lower, nob_sv_from_cstr("\"upload\":false")) ||
        ctest_submit_contains_sv(lower, nob_sv_from_cstr("already_present=1")) ||
        ctest_submit_contains_sv(lower, nob_sv_from_cstr("\"already_present\":true"))) {
        return false;
    }
    return true;
}

static bool ctest_submit_sleep_retry_delay(size_t seconds) {
    if (seconds == 0) return true;
#if defined(_WIN32)
    Sleep((DWORD)(seconds * 1000U));
#else
    sleep(seconds);
#endif
    return true;
}

typedef struct {
    bool success;
    size_t attempts;
    int exit_code;
    long http_code;
    String_View response_body;
    String_View response_detail;
    String_View build_id;
} Ctest_Submit_Remote_Result;

static String_View ctest_submit_best_response_detail(const Ctest_Submit_Curl_Result *curl_result) {
    if (!curl_result) return nob_sv_from_cstr("");
    if (curl_result->stderr_text.count > 0) return curl_result->stderr_text;
    if (curl_result->result_text.count > 0) return curl_result->result_text;
    return curl_result->body;
}

static bool ctest_submit_append_text_form_arg(EvalExecContext *ctx,
                                              SV_List *out_argv,
                                              const char *name,
                                              String_View value) {
    if (!ctx || !out_argv || !name) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--form-string"))) return false;
    return svu_list_push_temp(ctx, out_argv, ctest_submit_make_assignment_temp(ctx, name, value));
}

static bool ctest_submit_append_file_form_arg(EvalExecContext *ctx,
                                              SV_List *out_argv,
                                              const char *name,
                                              String_View path) {
    if (!ctx || !out_argv || !name) return false;
    if (!svu_list_push_temp(ctx, out_argv, nob_sv_from_cstr("--form"))) return false;

    size_t name_len = strlen(name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 2 + path.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, name, name_len);
    buf[name_len] = '=';
    buf[name_len + 1] = '@';
    if (path.count > 0) memcpy(buf + name_len + 2, path.data, path.count);
    buf[name_len + 2 + path.count] = '\0';
    return svu_list_push_temp(ctx, out_argv, nob_sv_from_parts(buf, name_len + 2 + path.count));
}

static bool ctest_submit_append_file_list_form_args(EvalExecContext *ctx,
                                                    SV_List *out_argv,
                                                    String_View files) {
    if (!ctx || !out_argv) return false;
    if (files.count == 0) return true;

    SV_List file_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), files, &file_items)) return false;
    for (size_t i = 0; i < arena_arr_len(file_items); i++) {
        if (file_items[i].count == 0) continue;
        if (!ctest_submit_append_file_form_arg(ctx, out_argv, "file", file_items[i])) return false;
    }
    return true;
}

static bool ctest_submit_run_request_with_retry(EvalExecContext *ctx,
                                                const Ctest_Submit_Request *req,
                                                SV_List argv,
                                                size_t *io_attempts,
                                                Ctest_Submit_Curl_Result *out_result) {
    if (!ctx || !req || !io_attempts || !out_result) return false;
    memset(out_result, 0, sizeof(*out_result));

    size_t max_attempts = req->retry_count + 1;
    for (size_t attempt = 0; attempt < max_attempts; attempt++) {
        Ctest_Submit_Curl_Result curl_result = {0};
        if (!ctest_submit_run_curl(ctx, argv, &curl_result)) return false;
        (*io_attempts)++;

        bool timed_out = curl_result.timed_out || curl_result.exit_code == 28;
        if (curl_result.exit_code == 0 || !timed_out || attempt + 1 >= max_attempts) {
            *out_result = curl_result;
            return true;
        }

        ctest_submit_sleep_retry_delay(req->retry_delay_sec);
    }

    return true;
}

static bool ctest_submit_execute_default_remote(EvalExecContext *ctx,
                                                const Ctest_Submit_Request *req,
                                                String_View tag,
                                                String_View track,
                                                String_View manifest,
                                                Ctest_Submit_Remote_Result *out_result) {
    if (!ctx || !req || !out_result) return false;
    memset(out_result, 0, sizeof(*out_result));

    SV_List argv = {0};
    if (!ctest_submit_append_curl_common_args(ctx, req, &argv)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "command", nob_sv_from_cstr("ctest_submit"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "signature", nob_sv_from_cstr("DEFAULT"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "model", req->model)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "tag", tag)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "track", track)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &argv, "parts", req->parts)) return false;
    if (!ctest_submit_append_file_form_arg(ctx, &argv, "manifest", manifest)) return false;
    if (!ctest_submit_append_file_list_form_args(ctx, &argv, req->files)) return false;
    if (!svu_list_push_temp(ctx, &argv, req->submit_url)) return false;

    Ctest_Submit_Curl_Result curl_result = {0};
    if (!ctest_submit_run_request_with_retry(ctx, req, argv, &out_result->attempts, &curl_result)) return false;

    out_result->success = curl_result.exit_code == 0;
    out_result->exit_code = curl_result.exit_code;
    out_result->http_code = curl_result.http_code;
    out_result->response_body = curl_result.body;
    out_result->response_detail = ctest_submit_best_response_detail(&curl_result);
    out_result->build_id = ctest_submit_extract_build_id(ctx, curl_result.body);
    return true;
}

static bool ctest_submit_execute_cdash_upload_remote(EvalExecContext *ctx,
                                                     const Ctest_Submit_Request *req,
                                                     Ctest_Submit_Remote_Result *out_result) {
    if (!ctx || !req || !out_result) return false;
    memset(out_result, 0, sizeof(*out_result));

    SV_List file_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), req->files, &file_items)) return false;
    if (arena_arr_len(file_items) != 1 || file_items[0].count == 0) {
        out_result->success = false;
        out_result->exit_code = 1;
        out_result->response_detail = nob_sv_from_cstr("ctest_submit(CDASH_UPLOAD ...) requires exactly one file");
        return true;
    }

    String_View payload = nob_sv_from_cstr("");
    if (!ctest_submit_read_file_temp(ctx, file_items[0], &payload)) {
        out_result->success = false;
        out_result->exit_code = 1;
        out_result->response_detail = nob_sv_from_cstr("failed to read CDASH_UPLOAD file");
        return true;
    }

    String_View hash = nob_sv_from_cstr("");
    if (!eval_hash_compute_hex_temp(ctx, nob_sv_from_cstr("MD5"), payload, &hash)) return false;

    SV_List probe_argv = {0};
    if (!ctest_submit_append_curl_common_args(ctx, req, &probe_argv)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "command", nob_sv_from_cstr("ctest_submit"))) {
        return false;
    }
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "signature", nob_sv_from_cstr("CDASH_UPLOAD"))) {
        return false;
    }
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "phase", nob_sv_from_cstr("probe"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "type", req->cdash_upload_type)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "filename", file_items[0])) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "hash_algo", nob_sv_from_cstr("MD5"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &probe_argv, "hash", hash)) return false;
    if (!ctest_submit_append_text_form_arg(ctx,
                                           &probe_argv,
                                           "size",
                                           ctest_submit_format_size_temp(ctx, payload.count))) {
        return false;
    }
    if (!svu_list_push_temp(ctx, &probe_argv, req->submit_url)) return false;

    Ctest_Submit_Curl_Result probe_result = {0};
    if (!ctest_submit_run_request_with_retry(ctx, req, probe_argv, &out_result->attempts, &probe_result)) {
        return false;
    }
    if (probe_result.exit_code != 0) {
        out_result->success = false;
        out_result->exit_code = probe_result.exit_code;
        out_result->http_code = probe_result.http_code;
        out_result->response_body = probe_result.body;
        out_result->response_detail = ctest_submit_best_response_detail(&probe_result);
        return true;
    }

    out_result->build_id = ctest_submit_extract_build_id(ctx, probe_result.body);
    if (!ctest_submit_response_requests_upload(ctx, probe_result.body)) {
        out_result->success = true;
        out_result->exit_code = 0;
        out_result->http_code = probe_result.http_code;
        out_result->response_body = probe_result.body;
        out_result->response_detail = probe_result.body;
        return true;
    }

    SV_List upload_argv = {0};
    if (!ctest_submit_append_curl_common_args(ctx, req, &upload_argv)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "command", nob_sv_from_cstr("ctest_submit"))) {
        return false;
    }
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "signature", nob_sv_from_cstr("CDASH_UPLOAD"))) {
        return false;
    }
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "phase", nob_sv_from_cstr("upload"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "type", req->cdash_upload_type)) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "filename", file_items[0])) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "hash_algo", nob_sv_from_cstr("MD5"))) return false;
    if (!ctest_submit_append_text_form_arg(ctx, &upload_argv, "hash", hash)) return false;
    if (!ctest_submit_append_text_form_arg(ctx,
                                           &upload_argv,
                                           "size",
                                           ctest_submit_format_size_temp(ctx, payload.count))) {
        return false;
    }
    if (!ctest_submit_append_file_form_arg(ctx, &upload_argv, "file", file_items[0])) return false;
    if (!svu_list_push_temp(ctx, &upload_argv, req->submit_url)) return false;

    Ctest_Submit_Curl_Result upload_result = {0};
    if (!ctest_submit_run_request_with_retry(ctx, req, upload_argv, &out_result->attempts, &upload_result)) {
        return false;
    }

    out_result->success = upload_result.exit_code == 0;
    out_result->exit_code = upload_result.exit_code;
    out_result->http_code = upload_result.http_code;
    out_result->response_body = upload_result.body;
    out_result->response_detail = ctest_submit_best_response_detail(&upload_result);
    if (out_result->build_id.count == 0) {
        out_result->build_id = ctest_submit_extract_build_id(ctx, upload_result.body);
    }
    return true;
}

static bool ctest_submit_append_part_files(EvalExecContext *ctx,
                                           const Node *node,
                                           String_View parts,
                                           String_View build_dir,
                                           SV_List *out_files) {
    if (!ctx || !node || !out_files) return false;
    if (parts.count == 0) return true;

    SV_List part_items = {0};
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), parts, &part_items)) return false;

    String_View base_dir = build_dir.count > 0 ? build_dir : ctest_current_binary_dir(ctx);
    for (size_t i = 0; i < arena_arr_len(part_items); i++) {
        if (eval_sv_eq_ci_lit(part_items[i], "Notes")) {
            if (!ctest_submit_append_resolved_files(ctx,
                                                    node,
                                                    nob_sv_from_cstr("ctest_submit"),
                                                    eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_NOTES_FILES")),
                                                    base_dir,
                                                    false,
                                                    out_files)) {
                return false;
            }
        } else if (eval_sv_eq_ci_lit(part_items[i], "ExtraFiles")) {
            if (!ctest_submit_append_resolved_files(ctx,
                                                    node,
                                                    nob_sv_from_cstr("ctest_submit"),
                                                    eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_EXTRA_SUBMIT_FILES")),
                                                    base_dir,
                                                    false,
                                                    out_files)) {
                return false;
            }
        } else {
            const Eval_Ctest_Step_Record *step = eval_ctest_find_last_step_by_part(ctx, part_items[i]);
            if (!step) continue;
            if (!ctest_append_joined_list_unique(ctx, eval_ctest_step_submit_files(ctx, step), out_files)) {
                return false;
            }
        }
    }

    return true;
}

static bool ctest_append_command_tokens_temp(EvalExecContext *ctx, String_View raw, SV_List *out_argv) {
    if (!ctx || !out_argv) return false;
    if (raw.count == 0) return true;

    bool prefer_semicolon_split = false;
    for (size_t i = 0; i < raw.count; i++) {
        if (raw.data[i] == ';') {
            prefer_semicolon_split = true;
            break;
        }
    }

    SV_List tokens = {0};
    if (prefer_semicolon_split) {
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), raw, &tokens)) return false;
    } else {
        if (!eval_split_command_line_temp(ctx, EVAL_CMDLINE_NATIVE, raw, &tokens)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(tokens); i++) {
        if (tokens[i].count == 0) continue;
        if (!svu_list_push_temp(ctx, out_argv, tokens[i])) return false;
    }
    return true;
}

static bool ctest_path_is_within(String_View path, String_View root) {
    if (root.count == 0) return false;
    if (path.count < root.count) return false;
    if (memcmp(path.data, root.data, root.count) != 0) return false;
    if (path.count == root.count) return true;
    char next = path.data[root.count];
    return next == '/' || next == '\\';
}

static bool ctest_remove_leaf(const char *path, bool is_dir) {
#if defined(_WIN32)
    if (is_dir) return RemoveDirectoryA(path) != 0;
    return DeleteFileA(path) != 0;
#else
    if (is_dir) return rmdir(path) == 0;
    return unlink(path) == 0;
#endif
}

typedef struct {
    bool ok;
} Ctest_Remove_Walk_State;

static bool ctest_remove_tree_walk(Nob_Walk_Entry entry) {
    Ctest_Remove_Walk_State *state = (Ctest_Remove_Walk_State*)entry.data;
    bool ok = ctest_remove_leaf(entry.path, entry.type == NOB_FILE_DIRECTORY);
    if (state) state->ok = state->ok && ok;
    return ok;
}

static bool ctest_remove_tree(const char *path) {
    Ctest_Remove_Walk_State state = { .ok = true };
    if (!path || !path[0]) return false;
    if (!nob_file_exists(path)) return true;
    if (!nob_walk_dir(path, ctest_remove_tree_walk, .data = &state, .post_order = true)) {
        return false;
    }
    return state.ok;
}

/* Shared internal runtime for operational ctest_* steps that already resolve
 * canonical session context but are not all fully process-backed yet. */
static Ctest_Step_Runtime_Def ctest_step_runtime_def(Eval_Ctest_Step_Kind step_kind,
                                                     const char *command_name,
                                                     const char *submit_part,
                                                     const Ctest_Parse_Spec *spec,
                                                     bool needs_source,
                                                     bool needs_build) {
    Ctest_Step_Runtime_Def def = {
        .step_kind = step_kind,
        .command_name = nob_sv_from_cstr(command_name),
        .submit_part = nob_sv_from_cstr(submit_part),
        .spec = spec,
        .needs_source = needs_source,
        .needs_build = needs_build,
    };
    return def;
}

static bool ctest_step_runtime_parse_base(EvalExecContext *ctx,
                                          const Node *node,
                                          const Ctest_Step_Runtime_Def *def,
                                          Ctest_Step_Runtime_Request *out_req) {
    if (!ctx || !node || !def || !def->spec || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->def = *def;
    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    return ctest_parse_generic(ctx,
                               node,
                               out_req->def.command_name,
                               &out_req->argv,
                               out_req->def.spec,
                               &out_req->parsed);
}

static bool ctest_step_runtime_resolve_context(EvalExecContext *ctx, Ctest_Step_Runtime_Request *req) {
    if (!ctx || !req) return false;
    if (!ctest_apply_resolved_context(ctx,
                                      req->def.command_name,
                                      &req->parsed,
                                      req->def.needs_source,
                                      req->def.needs_build,
                                      &req->resolved_source,
                                      &req->resolved_build)) {
        return false;
    }
    req->model = ctest_get_session_field(ctx, "MODEL");
    req->track = ctest_get_session_field(ctx, "TRACK");
    return true;
}

static bool ctest_resolve_path_from_base(EvalExecContext *ctx,
                                         String_View raw_path,
                                         String_View base_dir,
                                         String_View *out_path) {
    if (!ctx || !out_path) return false;
    *out_path = nob_sv_from_cstr("");
    if (raw_path.count == 0) return true;

    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw_path, base_dir, false);
    if (eval_should_stop(ctx)) return false;
    *out_path = eval_sv_path_normalize_temp(ctx, resolved);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_memcheck_parse_integer_field(EvalExecContext *ctx,
                                               const Node *node,
                                               const Ctest_Parse_Result *parsed,
                                               const char *field_name,
                                               bool require_positive,
                                               String_View *out_value) {
    if (!ctx || !node || !parsed || !field_name || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    String_View raw = ctest_parsed_field_value(parsed, field_name);
    if (raw.count == 0) return true;

    size_t parsed_value = 0;
    if (!ctest_parse_size_value_sv(raw, &parsed_value) || (require_positive && parsed_value == 0)) {
        Nob_String_Builder sb = {0};
        nob_sb_append_cstr(&sb, "ctest_memcheck() ");
        nob_sb_append_cstr(&sb, field_name);
        nob_sb_append_cstr(&sb, require_positive ? " requires a positive integer" : " requires a non-negative integer");
        String_View cause = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
        nob_sb_free(sb);
        if (eval_should_stop(ctx)) return false;
        (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, raw);
        return false;
    }

    *out_value = ctest_submit_format_size_temp(ctx, parsed_value);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_memcheck_parse_schedule_random(EvalExecContext *ctx,
                                                 const Node *node,
                                                 const Ctest_Parse_Result *parsed,
                                                 String_View *out_value) {
    if (!ctx || !node || !parsed || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    String_View raw = ctest_parsed_field_value(parsed, "SCHEDULE_RANDOM");
    if (raw.count == 0) return true;
    if (eval_sv_eq_ci_lit(raw, "ON")) {
        *out_value = nob_sv_from_cstr("ON");
        return true;
    }
    if (eval_sv_eq_ci_lit(raw, "OFF")) {
        *out_value = nob_sv_from_cstr("OFF");
        return true;
    }

    (void)ctest_emit_diag(ctx,
                          node,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("ctest_memcheck() SCHEDULE_RANDOM requires ON or OFF"),
                          raw);
    return false;
}

static bool ctest_memcheck_parse_repeat(EvalExecContext *ctx,
                                        const Node *node,
                                        const Ctest_Parse_Result *parsed,
                                        String_View *out_value) {
    if (!ctx || !node || !parsed || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    String_View raw = ctest_parsed_field_value(parsed, "REPEAT");
    if (raw.count == 0) return true;

    size_t colon_index = raw.count;
    for (size_t i = 0; i < raw.count; i++) {
        if (raw.data[i] == ':') {
            colon_index = i;
            break;
        }
    }
    if (colon_index == 0 || colon_index + 1 >= raw.count) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_memcheck() REPEAT requires <mode>:<n>"),
                              raw);
        return false;
    }

    String_View mode = nob_sv_from_parts(raw.data, colon_index);
    String_View canonical_mode = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(mode, "UNTIL_FAIL")) canonical_mode = nob_sv_from_cstr("UNTIL_FAIL");
    else if (eval_sv_eq_ci_lit(mode, "UNTIL_PASS")) canonical_mode = nob_sv_from_cstr("UNTIL_PASS");
    else if (eval_sv_eq_ci_lit(mode, "AFTER_TIMEOUT")) canonical_mode = nob_sv_from_cstr("AFTER_TIMEOUT");
    else {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_memcheck() REPEAT mode is not supported"),
                              mode);
        return false;
    }

    String_View raw_count = nob_sv_from_parts(raw.data + colon_index + 1, raw.count - colon_index - 1);
    size_t repeat_count = 0;
    if (!ctest_parse_size_value_sv(raw_count, &repeat_count) || repeat_count == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_memcheck() REPEAT count requires a positive integer"),
                              raw_count);
        return false;
    }

    Nob_String_Builder sb = {0};
    nob_sb_append_buf(&sb, canonical_mode.data, canonical_mode.count);
    nob_sb_append_cstr(&sb, ":");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("%zu", repeat_count));
    *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_parse_memcheck_request(EvalExecContext *ctx,
                                         const Node *node,
                                         Ctest_Memcheck_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    Ctest_Step_Runtime_Def def = ctest_step_runtime_def(EVAL_CTEST_STEP_MEMCHECK,
                                                        "ctest_memcheck",
                                                        "MemCheck",
                                                        &s_ctest_memcheck_parse_spec,
                                                        false,
                                                        true);
    if (!ctest_step_runtime_parse_base(ctx, node, &def, &out_req->core)) {
        return false;
    }
    if (!ctest_step_runtime_resolve_context(ctx, &out_req->core)) return false;
    if (eval_should_stop(ctx)) return false;

    if (!ctest_memcheck_parse_integer_field(ctx, node, &out_req->core.parsed, "START", false, &out_req->start)) return false;
    if (!ctest_memcheck_parse_integer_field(ctx, node, &out_req->core.parsed, "END", false, &out_req->end)) return false;
    if (!ctest_memcheck_parse_integer_field(ctx, node, &out_req->core.parsed, "STRIDE", true, &out_req->stride)) return false;
    if (!ctest_memcheck_parse_integer_field(ctx,
                                            node,
                                            &out_req->core.parsed,
                                            "PARALLEL_LEVEL",
                                            true,
                                            &out_req->parallel_level)) {
        return false;
    }
    if (!ctest_memcheck_parse_schedule_random(ctx, node, &out_req->core.parsed, &out_req->schedule_random)) return false;
    if (!ctest_memcheck_parse_repeat(ctx, node, &out_req->core.parsed, &out_req->repeat)) return false;

    String_View resource_spec_file = ctest_parsed_field_value(&out_req->core.parsed, "RESOURCE_SPEC_FILE");
    if (resource_spec_file.count == 0) {
        resource_spec_file = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_RESOURCE_SPEC_FILE"));
    }
    if (!ctest_resolve_path_from_base(ctx,
                                      resource_spec_file,
                                      ctest_current_binary_dir(ctx),
                                      &out_req->resource_spec_file)) {
        return false;
    }

    out_req->test_load = ctest_parsed_field_value(&out_req->core.parsed, "TEST_LOAD");
    if (out_req->test_load.count == 0) {
        out_req->test_load = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_TEST_LOAD"));
    }

    out_req->stop_time = ctest_parsed_field_value(&out_req->core.parsed, "STOP_TIME");
    if (!ctest_test_parse_stop_time(ctx, node, out_req->stop_time, &out_req->stop_time_seconds)) return false;
    out_req->has_stop_time = out_req->stop_time.count > 0;

    if (!ctest_resolve_path_from_base(ctx,
                                      ctest_parsed_field_value(&out_req->core.parsed, "OUTPUT_JUNIT"),
                                      out_req->core.resolved_build,
                                      &out_req->output_junit)) {
        return false;
    }

    out_req->backend_type = ctest_submit_visible_var(ctx, "CTEST_MEMORYCHECK_TYPE", "MEMORYCHECK_TYPE");
    out_req->backend_command =
        ctest_submit_visible_var(ctx, "CTEST_MEMORYCHECK_COMMAND", "MEMORYCHECK_COMMAND");
    out_req->backend_options = ctest_submit_visible_var(ctx,
                                                        "CTEST_MEMORYCHECK_COMMAND_OPTIONS",
                                                        "MEMORYCHECK_COMMAND_OPTIONS");
    out_req->backend_sanitizer_options =
        ctest_submit_visible_var(ctx,
                                 "CTEST_MEMORYCHECK_SANITIZER_OPTIONS",
                                 "MEMORYCHECK_SANITIZER_OPTIONS");
    if (!ctest_resolve_path_from_base(ctx,
                                      ctest_submit_visible_var(ctx,
                                                               "CTEST_MEMORYCHECK_SUPPRESSIONS_FILE",
                                                               "MEMORYCHECK_SUPPRESSIONS_FILE"),
                                      out_req->core.resolved_build,
                                      &out_req->suppression_file)) {
        return false;
    }
    if (out_req->backend_type.count == 0 && out_req->backend_command.count > 0) {
        out_req->backend_type = ctest_sv_contains_ci(out_req->backend_command, "valgrind")
                                    ? nob_sv_from_cstr("Valgrind")
                                    : nob_sv_from_cstr("Generic");
    }

    out_req->return_var = ctest_parsed_field_value(&out_req->core.parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var =
        ctest_parsed_field_value(&out_req->core.parsed, "CAPTURE_CMAKE_ERROR");
    out_req->defect_count_var = ctest_parsed_field_value(&out_req->core.parsed, "DEFECT_COUNT");
    out_req->append_mode = ctest_parsed_field_value(&out_req->core.parsed, "APPEND").count > 0;
    out_req->stop_on_failure =
        ctest_parsed_field_value(&out_req->core.parsed, "STOP_ON_FAILURE").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->core.parsed, "QUIET").count > 0;

    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_memcheck_commit_failed(EvalExecContext *ctx, const Ctest_Memcheck_Request *req) {
    if (!ctx || !req) return false;
    return ctest_commit_failed_step_record(ctx,
                                           &req->core.argv,
                                           EVAL_CTEST_STEP_MEMCHECK,
                                           nob_sv_from_cstr("ctest_memcheck"),
                                           nob_sv_from_cstr("MemCheck"),
                                           req->core.model,
                                           nob_sv_from_cstr(""),
                                           req->core.resolved_build);
}

static bool ctest_memcheck_fail(EvalExecContext *ctx,
                                const Node *node,
                                const Ctest_Memcheck_Request *req,
                                String_View cause,
                                String_View detail) {
    if (!ctx || !node || !req) return false;

    if (req->defect_count_var.count > 0 &&
        !eval_var_set_current(ctx, req->defect_count_var, nob_sv_from_cstr("0"))) {
        return false;
    }
    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("-1"))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0) {
        if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
        return ctest_memcheck_commit_failed(ctx, req);
    }
    if (!ctest_memcheck_commit_failed(ctx, req)) return false;
    (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, detail);
    return false;
}

static bool ctest_execute_memcheck_request(EvalExecContext *ctx,
                                           const Node *node,
                                           const Ctest_Memcheck_Request *req) {
    if (!ctx || !node || !req) return false;

    if (req->start.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "START", req->start)) {
        return false;
    }
    if (req->end.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "END", req->end)) {
        return false;
    }
    if (req->stride.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "STRIDE", req->stride)) {
        return false;
    }
    if (req->parallel_level.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "PARALLEL_LEVEL", req->parallel_level)) {
        return false;
    }
    if (req->stop_time.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "STOP_TIME_RESOLVED", req->stop_time)) {
        return false;
    }
    if (req->schedule_random.count > 0 &&
        !ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "SCHEDULE_RANDOM",
                         req->schedule_random)) {
        return false;
    }
    if (req->repeat.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "REPEAT", req->repeat)) {
        return false;
    }
    if (req->resource_spec_file.count > 0 &&
        !ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "RESOLVED_RESOURCE_SPEC_FILE",
                         req->resource_spec_file)) {
        return false;
    }
    if (req->test_load.count > 0 &&
        !ctest_set_field(ctx, nob_sv_from_cstr("ctest_memcheck"), "RESOLVED_TEST_LOAD", req->test_load)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "BACKEND_TYPE",
                         req->backend_type)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "BACKEND_COMMAND",
                         req->backend_command)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "BACKEND_OPTIONS",
                         req->backend_options)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "BACKEND_SANITIZER_OPTIONS",
                         req->backend_sanitizer_options)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "RESOLVED_SUPPRESSIONS_FILE",
                         req->suppression_file)) {
        return false;
    }
    if (req->output_junit.count > 0 &&
        !ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "RESOLVED_OUTPUT_JUNIT",
                         req->output_junit)) {
        return false;
    }
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), req->core.resolved_build)) {
        return false;
    }

    Ctest_Test_Plan_Entry_List plan = NULL;
    if (!ctest_collect_test_plan_from_stream(ctx, &plan)) return false;
    if (eval_sv_eq_ci_lit(req->schedule_random, "ON")) ctest_test_sort_plan_randomized(plan);

    size_t start_index = 1;
    size_t end_index = arena_arr_len(plan);
    size_t stride_value = 1;
    if (req->start.count > 0 && !ctest_parse_size_value_sv(req->start, &start_index)) return false;
    if (req->end.count > 0 && !ctest_parse_size_value_sv(req->end, &end_index)) return false;
    if (req->stride.count > 0 && !ctest_parse_size_value_sv(req->stride, &stride_value)) return false;
    if (start_index == 0) start_index = 1;
    if (stride_value == 0) stride_value = 1;
    if (end_index > arena_arr_len(plan)) end_index = arena_arr_len(plan);

    Ctest_Repeat_Mode repeat_mode = CTEST_REPEAT_NONE;
    size_t repeat_count = 1;
    if (!ctest_parse_repeat_mode_and_count(req->repeat, &repeat_mode, &repeat_count)) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() failed to interpret REPEAT"),
                                   req->repeat);
    }

    bool has_selected_tests = false;
    for (size_t i = 0; i < arena_arr_len(plan); i++) {
        size_t test_number = i + 1;
        if (test_number < start_index || test_number > end_index) continue;
        if (((test_number - start_index) % stride_value) != 0) continue;
        has_selected_tests = true;
        break;
    }
    if (has_selected_tests && req->backend_command.count == 0) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() requires a configured memory check command"),
                                   nob_sv_from_cstr("Set CTEST_MEMORYCHECK_COMMAND or MEMORYCHECK_COMMAND"));
    }

    Ctest_Test_Result_Entry_List results = NULL;
    size_t total_defects = 0;
    size_t failed_count = 0;

    for (size_t i = 0; i < arena_arr_len(plan); i++) {
        size_t test_number = i + 1;
        if (test_number < start_index || test_number > end_index) continue;
        if (((test_number - start_index) % stride_value) != 0) continue;

        if (req->has_stop_time) {
            int now_seconds = ctest_test_current_local_seconds_of_day();
            if (now_seconds >= 0 && now_seconds >= req->stop_time_seconds) break;
        }

        Ctest_Test_Result_Entry result = {
            .name = plan[i].name,
            .command = plan[i].command,
            .working_dir = req->core.resolved_build,
            .stdout_text = nob_sv_from_cstr(""),
            .stderr_text = nob_sv_from_cstr(""),
            .result_text = nob_sv_from_cstr(""),
            .exit_code = 127,
        };
        if (plan[i].working_dir.count > 0) {
            result.working_dir =
                eval_path_resolve_for_cmake_arg(ctx, plan[i].working_dir, req->core.resolved_build, false);
            if (eval_should_stop(ctx)) return false;
            result.working_dir = eval_sv_path_normalize_temp(ctx, result.working_dir);
            if (eval_should_stop(ctx)) return false;
        }

        Ctest_Test_Result_Entry last_attempt = result;
        for (size_t attempt = 0; attempt < repeat_count; attempt++) {
            SV_List argv = {0};
            if (!ctest_append_command_tokens_temp(ctx, req->backend_command, &argv)) return false;
            if (!ctest_append_command_tokens_temp(ctx, req->backend_options, &argv)) return false;
            if (!ctest_append_command_tokens_temp(ctx, req->backend_sanitizer_options, &argv)) return false;
            if (req->suppression_file.count > 0 && eval_sv_eq_ci_lit(req->backend_type, "Valgrind")) {
                if (!svu_list_push_temp(ctx,
                                        &argv,
                                        ctest_submit_make_assignment_temp(ctx,
                                                                          "--suppressions",
                                                                          req->suppression_file))) {
                    return false;
                }
            }
            if (!svu_list_push_temp(ctx, &argv, nob_sv_from_cstr("--"))) return false;
            if (!ctest_append_command_tokens_temp(ctx, plan[i].command, &argv)) return false;
            if (arena_arr_len(argv) == 0) {
                return ctest_memcheck_fail(ctx,
                                           node,
                                           req,
                                           nob_sv_from_cstr("ctest_memcheck() backend did not produce an executable"),
                                           req->backend_command);
            }

            last_attempt.backend_command = svu_join_space_temp(ctx, argv, arena_arr_len(argv));
            if (eval_should_stop(ctx)) return false;

            Eval_Process_Run_Request process_req = {
                .argv = argv,
                .argc = arena_arr_len(argv),
                .working_directory = result.working_dir,
            };
            Eval_Process_Run_Result proc = {0};
            if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

            last_attempt.started = proc.started;
            last_attempt.timed_out = proc.timed_out;
            last_attempt.exit_code = proc.started ? proc.exit_code : 127;
            last_attempt.stdout_text = proc.stdout_text;
            last_attempt.stderr_text = proc.stderr_text;
            last_attempt.result_text = proc.result_text;
            if (last_attempt.result_text.count == 0) {
                if (!last_attempt.started) last_attempt.result_text = nob_sv_from_cstr("failed to start");
                else if (last_attempt.timed_out) last_attempt.result_text = nob_sv_from_cstr("timed out");
            }
            last_attempt.defect_count =
                ctest_memcheck_extract_defect_count(last_attempt.stdout_text, last_attempt.stderr_text);
            last_attempt.passed = last_attempt.started && !last_attempt.timed_out &&
                                  last_attempt.exit_code == 0 && last_attempt.defect_count == 0;

            if (repeat_mode == CTEST_REPEAT_NONE) break;
            if (repeat_mode == CTEST_REPEAT_UNTIL_PASS && last_attempt.passed) break;
            if (repeat_mode == CTEST_REPEAT_UNTIL_FAIL && !last_attempt.passed) break;
            if (repeat_mode == CTEST_REPEAT_AFTER_TIMEOUT && !last_attempt.timed_out) break;
        }

        total_defects += last_attempt.defect_count;
        if (!last_attempt.passed) failed_count++;
        if (!arena_arr_push(eval_temp_arena(ctx), results, last_attempt)) return false;
        if (req->stop_on_failure && !last_attempt.passed) break;
    }

    if (req->defect_count_var.count > 0 &&
        !eval_var_set_current(ctx,
                              req->defect_count_var,
                              ctest_submit_format_size_temp(ctx, total_defects))) {
        return false;
    }
    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx,
                              req->return_var,
                              nob_sv_from_cstr((failed_count > 0 || total_defects > 0) ? "1" : "0"))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0 &&
        !eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("0"))) {
        return false;
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "DEFECT_COUNT_RESOLVED",
                         ctest_submit_format_size_temp(ctx, total_defects))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_memcheck"),
                         "FAILED_COUNT",
                         ctest_submit_format_size_temp(ctx, failed_count))) {
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->core.resolved_build,
                                  req->core.model,
                                  track,
                                  req->append_mode,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() failed to prepare Testing/<tag> staging"),
                                   req->core.resolved_build);
    }

    String_View memcheck_xml =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("MemCheck.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_memcheck_xml(ctx, memcheck_xml, req, results, total_defects, failed_count)) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() failed to write MemCheck.xml"),
                                   memcheck_xml);
    }
    if (!ctest_write_test_junit(ctx, req->output_junit, results, failed_count)) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() failed to write OUTPUT_JUNIT"),
                                   req->output_junit);
    }

    String_View manifest =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("MemCheckManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_memcheck"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("MemCheck"),
                              memcheck_xml)) {
        return ctest_memcheck_fail(ctx,
                                   node,
                                   req,
                                   nob_sv_from_cstr("ctest_memcheck() failed to write the staged memcheck manifest"),
                                   manifest);
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_memcheck"),
        .kind = nob_sv_from_cstr("MEMCHECK_XML"),
        .status = nob_sv_from_cstr("MEMCHECKED"),
        .base_dir = tag_dir_path,
        .primary_path = memcheck_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_MEMCHECK,
        .command_name = nob_sv_from_cstr("ctest_memcheck"),
        .submit_part = nob_sv_from_cstr("MemCheck"),
        .status = nob_sv_from_cstr("MEMCHECKED"),
        .model = req->core.model,
        .track = track,
        .build_dir = req->core.resolved_build,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
        .output_junit = req->output_junit,
    };
    return ctest_commit_step_record(ctx, &req->core.argv, step, &artifact);
}

static bool ctest_parse_empty_binary_directory_request(EvalExecContext *ctx,
                                                       const Node *node,
                                                       Ctest_Empty_Binary_Directory_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(out_req->argv) != 1) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() requires exactly one directory argument"),
                              nob_sv_from_cstr("Usage: ctest_empty_binary_directory(<dir>)"));
        return false;
    }

    out_req->root = eval_sv_path_normalize_temp(ctx, ctest_binary_root(ctx));
    out_req->target = eval_path_resolve_for_cmake_arg(ctx, out_req->argv[0], ctest_current_binary_dir(ctx), false);
    out_req->target = eval_sv_path_normalize_temp(ctx, out_req->target);
    if (eval_should_stop(ctx)) return false;

    if (out_req->target.count == 0 || out_req->root.count == 0 || !ctest_path_is_within(out_req->target, out_req->root)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() refuses to operate outside CMAKE_BINARY_DIR"),
                              out_req->target);
        return false;
    }

    return true;
}

static bool ctest_execute_empty_binary_directory_request(EvalExecContext *ctx,
                                                         const Node *node,
                                                         const Ctest_Empty_Binary_Directory_Request *req) {
    if (!ctx || !node || !req) return false;

    char *target_c = eval_sv_to_cstr_temp(ctx, req->target);
    EVAL_OOM_RETURN_IF_NULL(ctx, target_c, false);

    if (nob_file_exists(target_c)) {
        if (nob_get_file_type(target_c) != NOB_FILE_DIRECTORY) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() requires a directory path"),
                                  req->target);
            return false;
        }
        if (!ctest_remove_tree(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to remove directory contents"),
                                  req->target);
            return false;
        }
    }
    if (!nob_mkdir_if_not_exists(target_c)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() failed to recreate directory"),
                              req->target);
        return false;
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_empty_binary_directory"), "DIRECTORY", req->target)) return false;
    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_empty_binary_directory"),
                                       &req->argv,
                                       nob_sv_from_cstr("CLEARED"));
}

static bool ctest_parse_read_custom_files_request(EvalExecContext *ctx,
                                                  const Node *node,
                                                  Ctest_Read_Custom_Files_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (arena_arr_len(out_req->argv) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_read_custom_files() requires one or more directories"),
                              nob_sv_from_cstr("Usage: ctest_read_custom_files(<dir>...)"));
        return false;
    }

    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    static const char *const custom_names[] = {"CTestCustom.ctest", "CTestCustom.cmake"};
    for (size_t i = 0; i < arena_arr_len(out_req->argv); i++) {
        String_View dir = eval_path_resolve_for_cmake_arg(ctx, out_req->argv[i], base_dir, false);
        if (eval_should_stop(ctx)) return false;
        for (size_t name_i = 0; name_i < NOB_ARRAY_LEN(custom_names); name_i++) {
            String_View custom = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr(custom_names[name_i]));
            if (eval_should_stop(ctx)) return false;

            char *custom_c = eval_sv_to_cstr_temp(ctx, custom);
            EVAL_OOM_RETURN_IF_NULL(ctx, custom_c, false);
            if (nob_file_exists(custom_c) && nob_get_file_type(custom_c) != NOB_FILE_DIRECTORY) {
                if (!svu_list_push_temp(ctx, &out_req->loaded_files, custom)) return false;
            }
        }
    }

    return true;
}

static bool ctest_execute_read_custom_files_request(EvalExecContext *ctx,
                                                    const Ctest_Read_Custom_Files_Request *req) {
    if (!ctx || !req) return false;

    for (size_t i = 0; i < arena_arr_len(req->loaded_files); i++) {
        Eval_Result exec_res = eval_execute_file(ctx, req->loaded_files[i], false, nob_sv_from_cstr(""));
        if (eval_result_is_fatal(exec_res)) return false;
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_read_custom_files"),
                         "LOADED_FILES",
                         eval_sv_join_semi_temp(ctx, req->loaded_files, arena_arr_len(req->loaded_files)))) {
        return false;
    }
    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_read_custom_files"),
                                       &req->argv,
                                       nob_sv_from_cstr("MODELED"));
}

static bool ctest_parse_build_request(EvalExecContext *ctx,
                                      const Node *node,
                                      Ctest_Build_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    Ctest_Step_Runtime_Def def = ctest_step_runtime_def(EVAL_CTEST_STEP_BUILD,
                                                        "ctest_build",
                                                        "Build",
                                                        &s_ctest_build_parse_spec,
                                                        false,
                                                        true);
    if (!ctest_step_runtime_parse_base(ctx, node, &def, &out_req->core)) return false;
    if (!ctest_step_runtime_resolve_context(ctx, &out_req->core)) return false;

    if (out_req->core.model.count == 0) out_req->core.model = nob_sv_from_cstr("Experimental");
    out_req->configuration = ctest_parsed_field_value(&out_req->core.parsed, "CONFIGURATION");
    if (out_req->configuration.count == 0) {
        out_req->configuration = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BUILD_CONFIGURATION"));
    }
    out_req->parallel_level = ctest_build_resolve_parallel_level(ctx, &out_req->core.parsed);
    if (eval_should_stop(ctx)) return false;
    out_req->flags = ctest_parsed_field_value(&out_req->core.parsed, "FLAGS");
    if (out_req->flags.count == 0) {
        out_req->flags = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BUILD_FLAGS"));
    }
    out_req->project_name = ctest_parsed_field_value(&out_req->core.parsed, "PROJECT_NAME");
    out_req->target = ctest_parsed_field_value(&out_req->core.parsed, "TARGET");
    if (out_req->target.count == 0) {
        out_req->target = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BUILD_TARGET"));
    }
    out_req->build_command = ctest_build_resolve_command(ctx,
                                                         out_req->configuration,
                                                         out_req->parallel_level,
                                                         out_req->flags,
                                                         out_req->target);
    if (eval_should_stop(ctx)) return false;
    out_req->number_errors_var = ctest_parsed_field_value(&out_req->core.parsed, "NUMBER_ERRORS");
    out_req->number_warnings_var = ctest_parsed_field_value(&out_req->core.parsed, "NUMBER_WARNINGS");
    out_req->return_var = ctest_parsed_field_value(&out_req->core.parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var =
        ctest_parsed_field_value(&out_req->core.parsed, "CAPTURE_CMAKE_ERROR");
    out_req->append_mode = ctest_parsed_field_value(&out_req->core.parsed, "APPEND").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->core.parsed, "QUIET").count > 0;
    return true;
}

static bool ctest_build_commit_failed(EvalExecContext *ctx, const Ctest_Build_Request *req) {
    if (!ctx || !req) return false;
    return ctest_commit_failed_step_record(ctx,
                                           &req->core.argv,
                                           EVAL_CTEST_STEP_BUILD,
                                           nob_sv_from_cstr("ctest_build"),
                                           nob_sv_from_cstr("Build"),
                                           req->core.model,
                                           nob_sv_from_cstr(""),
                                           req->core.resolved_build);
}

static bool ctest_build_fail(EvalExecContext *ctx,
                             const Node *node,
                             const Ctest_Build_Request *req,
                             String_View cause,
                             String_View detail) {
    if (!ctx || !node || !req) return false;

    if (req->number_errors_var.count > 0 &&
        !eval_var_set_current(ctx, req->number_errors_var, nob_sv_from_cstr("0"))) {
        return false;
    }
    if (req->number_warnings_var.count > 0 &&
        !eval_var_set_current(ctx, req->number_warnings_var, nob_sv_from_cstr("0"))) {
        return false;
    }
    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("-1"))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0) {
        if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
        return ctest_build_commit_failed(ctx, req);
    }
    if (!ctest_build_commit_failed(ctx, req)) return false;
    (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, detail);
    return false;
}

static bool ctest_execute_build_request(EvalExecContext *ctx,
                                        const Node *node,
                                        const Ctest_Build_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_build"), "BUILD_COMMAND", req->build_command)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "CONFIGURATION_RESOLVED",
                         req->configuration)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "PARALLEL_LEVEL_RESOLVED",
                         req->parallel_level)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "FLAGS_RESOLVED",
                         req->flags)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "TARGET_RESOLVED",
                         req->target)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "PROJECT_NAME_RESOLVED",
                         req->project_name)) {
        return false;
    }
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), req->core.resolved_build)) {
        return false;
    }

    SV_List argv = {0};
    if (!ctest_append_command_tokens_temp(ctx, req->build_command, &argv)) return false;
    if (arena_arr_len(argv) == 0) {
        return ctest_build_fail(ctx,
                                node,
                                req,
                                nob_sv_from_cstr("ctest_build() build command did not produce an executable"),
                                req->build_command);
    }

    Eval_Process_Run_Request process_req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = req->core.resolved_build,
    };
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_build"), "OUTPUT", proc.stdout_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_build"), "ERROR_OUTPUT", proc.stderr_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_build"), "PROCESS_RESULT", proc.result_text)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "EXIT_CODE",
                         ctest_submit_format_long_temp(ctx, (long)proc.exit_code))) {
        return false;
    }

    bool execution_error = !proc.started || proc.timed_out;
    if (execution_error) {
        String_View detail = proc.stderr_text.count > 0 ? proc.stderr_text
                            : proc.result_text.count > 0 ? proc.result_text
                            : req->build_command;
        return ctest_build_fail(ctx,
                                node,
                                req,
                                nob_sv_from_cstr("ctest_build() failed to run the build command"),
                                detail);
    }

    size_t error_count = 0;
    size_t warning_count = 0;
    if (!ctest_build_count_issues(ctx, proc.stdout_text, proc.stderr_text, &error_count, &warning_count)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "ERROR_COUNT",
                         ctest_submit_format_size_temp(ctx, error_count))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_build"),
                         "WARNING_COUNT",
                         ctest_submit_format_size_temp(ctx, warning_count))) {
        return false;
    }

    if (req->number_errors_var.count > 0 &&
        !eval_var_set_current(ctx, req->number_errors_var, ctest_submit_format_size_temp(ctx, error_count))) {
        return false;
    }
    if (req->number_warnings_var.count > 0 &&
        !eval_var_set_current(ctx, req->number_warnings_var, ctest_submit_format_size_temp(ctx, warning_count))) {
        return false;
    }
    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx,
                              req->return_var,
                              ctest_submit_format_long_temp(ctx, (long)proc.exit_code))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0 &&
        !eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("0"))) {
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->core.resolved_build,
                                  req->core.model,
                                  track,
                                  req->append_mode,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    String_View build_xml =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("Build.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_build_xml(ctx,
                               build_xml,
                               req,
                               error_count,
                               warning_count,
                               proc.stdout_text,
                               proc.stderr_text,
                               proc.exit_code)) {
        return false;
    }

    String_View manifest =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("BuildManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_build"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("Build"),
                              build_xml)) {
        return false;
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_build"),
        .kind = nob_sv_from_cstr("BUILD_XML"),
        .status = nob_sv_from_cstr("BUILT"),
        .base_dir = tag_dir_path,
        .primary_path = build_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_BUILD,
        .command_name = nob_sv_from_cstr("ctest_build"),
        .submit_part = nob_sv_from_cstr("Build"),
        .status = nob_sv_from_cstr("BUILT"),
        .model = req->core.model,
        .track = track,
        .build_dir = req->core.resolved_build,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
    };
    return ctest_commit_step_record(ctx, &req->core.argv, step, &artifact);
}

Eval_Result eval_handle_ctest_build(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Build_Request req = {0};
    if (!ctest_parse_build_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_build_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool ctest_parse_test_request(EvalExecContext *ctx,
                                     const Node *node,
                                     Ctest_Test_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    Ctest_Step_Runtime_Def def = ctest_step_runtime_def(EVAL_CTEST_STEP_TEST,
                                                        "ctest_test",
                                                        "Test",
                                                        &s_ctest_test_parse_spec,
                                                        false,
                                                        true);
    if (!ctest_step_runtime_parse_base(ctx, node, &def, &out_req->core)) return false;
    if (!ctest_step_runtime_resolve_context(ctx, &out_req->core)) return false;

    if (!ctest_memcheck_parse_integer_field(ctx,
                                            node,
                                            &out_req->core.parsed,
                                            "PARALLEL_LEVEL",
                                            true,
                                            &out_req->parallel_level)) {
        return false;
    }

    out_req->stop_time = ctest_parsed_field_value(&out_req->core.parsed, "STOP_TIME");
    if (!ctest_test_parse_stop_time(ctx, node, out_req->stop_time, &out_req->stop_time_seconds)) return false;
    out_req->has_stop_time = out_req->stop_time.count > 0;

    out_req->test_load = ctest_parsed_field_value(&out_req->core.parsed, "TEST_LOAD");
    if (out_req->test_load.count == 0) {
        out_req->test_load = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_TEST_LOAD"));
    }

    if (!ctest_resolve_path_from_base(ctx,
                                      ctest_parsed_field_value(&out_req->core.parsed, "OUTPUT_JUNIT"),
                                      out_req->core.resolved_build,
                                      &out_req->output_junit)) {
        return false;
    }

    out_req->return_var = ctest_parsed_field_value(&out_req->core.parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var =
        ctest_parsed_field_value(&out_req->core.parsed, "CAPTURE_CMAKE_ERROR");
    out_req->append_mode = ctest_parsed_field_value(&out_req->core.parsed, "APPEND").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->core.parsed, "QUIET").count > 0;
    out_req->schedule_random =
        ctest_parsed_field_value(&out_req->core.parsed, "SCHEDULE_RANDOM").count > 0;
    return true;
}

static bool ctest_test_commit_failed(EvalExecContext *ctx, const Ctest_Test_Request *req) {
    if (!ctx || !req) return false;
    return ctest_commit_failed_step_record(ctx,
                                           &req->core.argv,
                                           EVAL_CTEST_STEP_TEST,
                                           nob_sv_from_cstr("ctest_test"),
                                           nob_sv_from_cstr("Test"),
                                           req->core.model,
                                           nob_sv_from_cstr(""),
                                           req->core.resolved_build);
}

static bool ctest_test_fail(EvalExecContext *ctx,
                            const Node *node,
                            const Ctest_Test_Request *req,
                            String_View cause,
                            String_View detail) {
    if (!ctx || !node || !req) return false;

    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("-1"))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0) {
        if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
        return ctest_test_commit_failed(ctx, req);
    }
    if (!ctest_test_commit_failed(ctx, req)) return false;
    (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, detail);
    return false;
}

static bool ctest_execute_test_request(EvalExecContext *ctx,
                                       const Node *node,
                                       const Ctest_Test_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "PARALLEL_LEVEL_RESOLVED",
                         req->parallel_level)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_test"), "STOP_TIME_RESOLVED", req->stop_time)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_test"), "RESOLVED_TEST_LOAD", req->test_load)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "SCHEDULE_RANDOM",
                         req->schedule_random ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "RESOLVED_OUTPUT_JUNIT",
                         req->output_junit)) {
        return false;
    }
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), req->core.resolved_build)) {
        return false;
    }

    Ctest_Test_Plan_Entry_List plan = NULL;
    if (!ctest_collect_test_plan_from_stream(ctx, &plan)) return false;
    if (req->schedule_random) ctest_test_sort_plan_randomized(plan);

    SV_List planned_names = {0};
    for (size_t i = 0; i < arena_arr_len(plan); i++) {
        if (!svu_list_push_temp(ctx, &planned_names, plan[i].name)) return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "TEST_PLAN",
                         eval_sv_join_semi_temp(ctx, planned_names, arena_arr_len(planned_names)))) {
        return false;
    }

    Ctest_Test_Result_Entry_List results = NULL;
    size_t passed_count = 0;
    size_t failed_count = 0;
    for (size_t i = 0; i < arena_arr_len(plan); i++) {
        if (req->has_stop_time) {
            int now_seconds = ctest_test_current_local_seconds_of_day();
            if (now_seconds >= 0 && now_seconds >= req->stop_time_seconds) break;
        }

        Ctest_Test_Result_Entry result = {
            .name = plan[i].name,
            .command = plan[i].command,
            .working_dir = req->core.resolved_build,
            .exit_code = 127,
            .stdout_text = nob_sv_from_cstr(""),
            .stderr_text = nob_sv_from_cstr(""),
            .result_text = nob_sv_from_cstr(""),
        };

        if (plan[i].working_dir.count > 0) {
            result.working_dir =
                eval_path_resolve_for_cmake_arg(ctx, plan[i].working_dir, req->core.resolved_build, false);
            if (eval_should_stop(ctx)) return false;
            result.working_dir = eval_sv_path_normalize_temp(ctx, result.working_dir);
            if (eval_should_stop(ctx)) return false;
        }

        SV_List argv = {0};
        if (!ctest_append_command_tokens_temp(ctx, plan[i].command, &argv)) return false;
        if (arena_arr_len(argv) == 0) {
            result.stderr_text = nob_sv_from_cstr("empty test command");
            result.result_text = nob_sv_from_cstr("empty test command");
            if (!arena_arr_push(eval_temp_arena(ctx), results, result)) return false;
            failed_count++;
            continue;
        }

        Eval_Process_Run_Request process_req = {
            .argv = argv,
            .argc = arena_arr_len(argv),
            .working_directory = result.working_dir,
        };
        Eval_Process_Run_Result proc = {0};
        if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

        result.started = proc.started;
        result.timed_out = proc.timed_out;
        result.exit_code = proc.started ? proc.exit_code : 127;
        result.stdout_text = proc.stdout_text;
        result.stderr_text = proc.stderr_text;
        result.result_text = proc.result_text;
        if (result.result_text.count == 0) {
            if (!result.started) result.result_text = nob_sv_from_cstr("failed to start");
            else if (result.timed_out) result.result_text = nob_sv_from_cstr("timed out");
        }
        result.passed = result.started && !result.timed_out && result.exit_code == 0;

        if (!arena_arr_push(eval_temp_arena(ctx), results, result)) return false;
        if (result.passed) passed_count++;
        else failed_count++;
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "TEST_COUNT",
                         ctest_submit_format_size_temp(ctx, arena_arr_len(results)))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "PASSED_COUNT",
                         ctest_submit_format_size_temp(ctx, passed_count))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_test"),
                         "FAILED_COUNT",
                         ctest_submit_format_size_temp(ctx, failed_count))) {
        return false;
    }

    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, ctest_submit_format_size_temp(ctx, failed_count))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0 &&
        !eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("0"))) {
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->core.resolved_build,
                                  req->core.model,
                                  track,
                                  req->append_mode,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return ctest_test_fail(ctx,
                               node,
                               req,
                               nob_sv_from_cstr("ctest_test() failed to prepare Testing/<tag> staging"),
                               req->core.resolved_build);
    }

    String_View test_xml =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("Test.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_test_xml(ctx, test_xml, req, results, passed_count, failed_count)) {
        return ctest_test_fail(ctx,
                               node,
                               req,
                               nob_sv_from_cstr("ctest_test() failed to write Test.xml"),
                               test_xml);
    }

    if (!ctest_write_test_junit(ctx, req->output_junit, results, failed_count)) {
        return ctest_test_fail(ctx,
                               node,
                               req,
                               nob_sv_from_cstr("ctest_test() failed to write OUTPUT_JUNIT"),
                               req->output_junit);
    }

    String_View manifest =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("TestManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_test"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("Test"),
                              test_xml)) {
        return ctest_test_fail(ctx,
                               node,
                               req,
                               nob_sv_from_cstr("ctest_test() failed to write the staged test manifest"),
                               manifest);
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_test"),
        .kind = nob_sv_from_cstr("TEST_XML"),
        .status = nob_sv_from_cstr("TESTED"),
        .base_dir = tag_dir_path,
        .primary_path = test_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_TEST,
        .command_name = nob_sv_from_cstr("ctest_test"),
        .submit_part = nob_sv_from_cstr("Test"),
        .status = nob_sv_from_cstr("TESTED"),
        .model = req->core.model,
        .track = track,
        .build_dir = req->core.resolved_build,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
        .output_junit = req->output_junit,
    };
    return ctest_commit_step_record(ctx, &req->core.argv, step, &artifact);
}

static bool ctest_parse_configure_request(EvalExecContext *ctx,
                                          const Node *node,
                                          Ctest_Configure_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_parse_generic(ctx,
                             node,
                             nob_sv_from_cstr("ctest_configure"),
                             &out_req->argv,
                             &s_ctest_configure_parse_spec,
                             &out_req->parsed)) {
        return false;
    }

    String_View source = ctest_parsed_field_value(&out_req->parsed, "SOURCE");
    String_View build = ctest_parsed_field_value(&out_req->parsed, "BUILD");
    out_req->resolved_source = source.count > 0 ? ctest_resolve_source_dir(ctx, source)
                                                : ctest_session_resolved_source_dir(ctx);
    out_req->build_dir = build.count > 0 ? ctest_resolve_binary_dir(ctx, build)
                                         : ctest_session_resolved_build_dir(ctx);
    if (eval_should_stop(ctx)) return false;

    out_req->model = ctest_get_session_field(ctx, "MODEL");
    if (out_req->model.count == 0) out_req->model = nob_sv_from_cstr("Experimental");
    out_req->configure_command = ctest_configure_resolve_command(ctx, out_req->resolved_source);
    out_req->configure_options = ctest_parsed_field_value(&out_req->parsed, "OPTIONS");
    out_req->labels_for_subprojects = ctest_submit_visible_var(ctx, "CTEST_LABELS_FOR_SUBPROJECTS", NULL);
    out_req->return_var = ctest_parsed_field_value(&out_req->parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var = ctest_parsed_field_value(&out_req->parsed, "CAPTURE_CMAKE_ERROR");
    out_req->append_mode = ctest_parsed_field_value(&out_req->parsed, "APPEND").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->parsed, "QUIET").count > 0;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_execute_configure_request(EvalExecContext *ctx,
                                            const Node *node,
                                            const Ctest_Configure_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "RESOLVED_SOURCE", req->resolved_source)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "RESOLVED_BUILD", req->build_dir)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "CONFIGURE_COMMAND", req->configure_command)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_configure"),
                         "OPTIONS_RESOLVED",
                         req->configure_options)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_configure"),
                         "LABELS_FOR_SUBPROJECTS",
                         req->labels_for_subprojects)) {
        return false;
    }
    if (!ctest_publish_common_dirs(ctx, req->resolved_source, req->build_dir)) return false;

    if (req->configure_command.count == 0) {
        if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("1"))) {
            return false;
        }
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_CONFIGURE,
                                                   nob_sv_from_cstr("ctest_configure"),
                                                   nob_sv_from_cstr("Configure"),
                                                   req->model,
                                                   req->resolved_source,
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_CONFIGURE,
                                             nob_sv_from_cstr("ctest_configure"),
                                             nob_sv_from_cstr("Configure"),
                                             req->model,
                                             req->resolved_source,
                                             req->build_dir)) {
            return false;
        }
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_configure() requires CTEST_CONFIGURE_COMMAND or CMAKE_COMMAND"),
                              nob_sv_from_cstr("Set the documented configure command before running the configure step"));
        return false;
    }

    if (!ctest_ensure_directory(ctx, req->build_dir)) {
        if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("1"))) {
            return false;
        }
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_CONFIGURE,
                                                   nob_sv_from_cstr("ctest_configure"),
                                                   nob_sv_from_cstr("Configure"),
                                                   req->model,
                                                   req->resolved_source,
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_CONFIGURE,
                                             nob_sv_from_cstr("ctest_configure"),
                                             nob_sv_from_cstr("Configure"),
                                             req->model,
                                             req->resolved_source,
                                             req->build_dir)) {
            return false;
        }
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_configure() failed to create build directory"),
                              req->build_dir);
        return false;
    }

    SV_List argv = {0};
    if (!ctest_append_command_tokens_temp(ctx, req->configure_command, &argv)) return false;
    if (arena_arr_len(argv) == 0) {
        if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("1"))) {
            return false;
        }
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_CONFIGURE,
                                                   nob_sv_from_cstr("ctest_configure"),
                                                   nob_sv_from_cstr("Configure"),
                                                   req->model,
                                                   req->resolved_source,
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_CONFIGURE,
                                             nob_sv_from_cstr("ctest_configure"),
                                             nob_sv_from_cstr("Configure"),
                                             req->model,
                                             req->resolved_source,
                                             req->build_dir)) {
            return false;
        }
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_configure() configure command did not produce an executable"),
                              req->configure_command);
        return false;
    }
    if (!ctest_append_command_tokens_temp(ctx, req->configure_options, &argv)) return false;

    Eval_Process_Run_Request process_req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = req->build_dir,
    };
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "OUTPUT", proc.stdout_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "ERROR_OUTPUT", proc.stderr_text)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_configure"), "PROCESS_RESULT", proc.result_text)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_configure"),
                         "EXIT_CODE",
                         ctest_submit_format_long_temp(ctx, (long)proc.exit_code))) {
        return false;
    }

    bool success = proc.started && !proc.timed_out && proc.exit_code == 0;
    String_View rv_value = success ? nob_sv_from_cstr("0")
                                   : ctest_submit_format_size_temp(ctx,
                                                                   proc.exit_code > 0
                                                                       ? (size_t)proc.exit_code
                                                                       : 1U);
    if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, rv_value)) return false;

    if (!success) {
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_CONFIGURE,
                                                   nob_sv_from_cstr("ctest_configure"),
                                                   nob_sv_from_cstr("Configure"),
                                                   req->model,
                                                   req->resolved_source,
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_CONFIGURE,
                                             nob_sv_from_cstr("ctest_configure"),
                                             nob_sv_from_cstr("Configure"),
                                             req->model,
                                             req->resolved_source,
                                             req->build_dir)) {
            return false;
        }

        String_View detail = proc.stderr_text.count > 0 ? proc.stderr_text
                            : proc.result_text.count > 0 ? proc.result_text
                            : req->configure_command;
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_configure() configure command failed"),
                              detail);
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->build_dir,
                                  req->model,
                                  track,
                                  req->append_mode,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    String_View configure_xml = eval_sv_path_join(eval_temp_arena(ctx),
                                                  tag_dir_path,
                                                  nob_sv_from_cstr("Configure.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_configure_xml(ctx,
                                   configure_xml,
                                   req,
                                   proc.stdout_text,
                                   proc.stderr_text,
                                   proc.exit_code)) {
        return false;
    }

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx),
                                             tag_dir_path,
                                             nob_sv_from_cstr("ConfigureManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_configure"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("Configure"),
                              configure_xml)) {
        return false;
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_configure"),
        .kind = nob_sv_from_cstr("CONFIGURE_XML"),
        .status = nob_sv_from_cstr("CONFIGURED"),
        .base_dir = tag_dir_path,
        .primary_path = configure_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_CONFIGURE,
        .command_name = nob_sv_from_cstr("ctest_configure"),
        .submit_part = nob_sv_from_cstr("Configure"),
        .status = nob_sv_from_cstr("CONFIGURED"),
        .model = req->model,
        .track = track,
        .source_dir = req->resolved_source,
        .build_dir = req->build_dir,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
    };
    return ctest_commit_step_record(ctx, &req->argv, step, &artifact);
}

Eval_Result eval_handle_ctest_configure(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Configure_Request req = {0};
    if (!ctest_parse_configure_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_configure_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool ctest_parse_coverage_request(EvalExecContext *ctx,
                                         const Node *node,
                                         Ctest_Coverage_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_parse_generic(ctx,
                             node,
                             nob_sv_from_cstr("ctest_coverage"),
                             &out_req->argv,
                             &s_ctest_coverage_parse_spec,
                             &out_req->parsed)) {
        return false;
    }

    String_View build = ctest_parsed_field_value(&out_req->parsed, "BUILD");
    out_req->build_dir = build.count > 0 ? ctest_resolve_binary_dir(ctx, build) : ctest_session_resolved_build_dir(ctx);
    if (eval_should_stop(ctx)) return false;

    out_req->model = ctest_get_session_field(ctx, "MODEL");
    if (out_req->model.count == 0) out_req->model = nob_sv_from_cstr("Experimental");
    out_req->coverage_command = ctest_submit_visible_var(ctx, "CTEST_COVERAGE_COMMAND", "COVERAGE_COMMAND");
    out_req->coverage_extra_flags = ctest_submit_visible_var(ctx,
                                                             "CTEST_COVERAGE_EXTRA_FLAGS",
                                                             "COVERAGE_EXTRA_FLAGS");
    out_req->labels = ctest_join_parsed_field_values(ctx, &out_req->parsed, "LABELS");
    out_req->return_var = ctest_parsed_field_value(&out_req->parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var = ctest_parsed_field_value(&out_req->parsed, "CAPTURE_CMAKE_ERROR");
    out_req->append_mode = ctest_parsed_field_value(&out_req->parsed, "APPEND").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->parsed, "QUIET").count > 0;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_execute_coverage_request(EvalExecContext *ctx,
                                           const Node *node,
                                           const Ctest_Coverage_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "RESOLVED_BUILD", req->build_dir)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "COVERAGE_COMMAND", req->coverage_command)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_coverage"),
                         "COVERAGE_EXTRA_FLAGS",
                         req->coverage_extra_flags)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "LABELS_RESOLVED", req->labels)) return false;
    if (!ctest_publish_common_dirs(ctx, ctest_get_session_field(ctx, "SOURCE"), req->build_dir)) return false;

    if (req->coverage_command.count == 0) {
        if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("1"))) {
            return false;
        }
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_COVERAGE,
                                                   nob_sv_from_cstr("ctest_coverage"),
                                                   nob_sv_from_cstr("Coverage"),
                                                   req->model,
                                                   nob_sv_from_cstr(""),
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_COVERAGE,
                                             nob_sv_from_cstr("ctest_coverage"),
                                             nob_sv_from_cstr("Coverage"),
                                             req->model,
                                             nob_sv_from_cstr(""),
                                             req->build_dir)) {
            return false;
        }
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_coverage() requires CTEST_COVERAGE_COMMAND or COVERAGE_COMMAND"),
                              nob_sv_from_cstr("Set the documented coverage tool command before running the coverage step"));
        return false;
    }

    SV_List command_argv = {0};
    if (!ctest_append_command_tokens_temp(ctx, req->coverage_command, &command_argv)) return false;
    if (arena_arr_len(command_argv) == 0) {
        if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("1"))) {
            return false;
        }
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_COVERAGE,
                                                   nob_sv_from_cstr("ctest_coverage"),
                                                   nob_sv_from_cstr("Coverage"),
                                                   req->model,
                                                   nob_sv_from_cstr(""),
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_COVERAGE,
                                             nob_sv_from_cstr("ctest_coverage"),
                                             nob_sv_from_cstr("Coverage"),
                                             req->model,
                                             nob_sv_from_cstr(""),
                                             req->build_dir)) {
            return false;
        }
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_coverage() coverage command did not produce an executable"),
                              req->coverage_command);
        return false;
    }

    SV_List argv = {0};
    if (!svu_list_push_temp(ctx, &argv, command_argv[0])) return false;
    if (!ctest_append_command_tokens_temp(ctx, req->coverage_extra_flags, &argv)) return false;
    for (size_t i = 1; i < arena_arr_len(command_argv); i++) {
        if (!svu_list_push_temp(ctx, &argv, command_argv[i])) return false;
    }

    Eval_Process_Run_Request process_req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
        .working_directory = req->build_dir,
    };
    if (ctest_parsed_field_value(&req->parsed, "QUIET").count == 0) {
        nob_log(NOB_INFO,
                "ctest_coverage: collecting coverage in %.*s",
                (int)req->build_dir.count,
                req->build_dir.data ? req->build_dir.data : "");
    }
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "OUTPUT", proc.stdout_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "ERROR_OUTPUT", proc.stderr_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_coverage"), "PROCESS_RESULT", proc.result_text)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_coverage"),
                         "EXIT_CODE",
                         ctest_submit_format_long_temp(ctx, (long)proc.exit_code))) {
        return false;
    }

    bool success = proc.started && !proc.timed_out && proc.exit_code == 0;
    String_View rv_value = success ? nob_sv_from_cstr("0")
                                   : ctest_submit_format_size_temp(ctx,
                                                                   proc.exit_code > 0
                                                                       ? (size_t)proc.exit_code
                                                                       : 1U);
    if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, rv_value)) return false;

    if (!success) {
        if (req->capture_cmake_error_var.count > 0) {
            if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
            return ctest_commit_failed_step_record(ctx,
                                                   &req->argv,
                                                   EVAL_CTEST_STEP_COVERAGE,
                                                   nob_sv_from_cstr("ctest_coverage"),
                                                   nob_sv_from_cstr("Coverage"),
                                                   req->model,
                                                   nob_sv_from_cstr(""),
                                                   req->build_dir);
        }
        if (!ctest_commit_failed_step_record(ctx,
                                             &req->argv,
                                             EVAL_CTEST_STEP_COVERAGE,
                                             nob_sv_from_cstr("ctest_coverage"),
                                             nob_sv_from_cstr("Coverage"),
                                             req->model,
                                             nob_sv_from_cstr(""),
                                             req->build_dir)) {
            return false;
        }

        String_View detail = proc.stderr_text.count > 0 ? proc.stderr_text
                            : proc.result_text.count > 0 ? proc.result_text
                            : req->coverage_command;
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_coverage() coverage command failed"),
                              detail);
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->build_dir,
                                  req->model,
                                  track,
                                  false,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    ctest_coverage_report_local_output(ctx, req, proc.stdout_text);

    String_View coverage_xml = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("Coverage.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_coverage_xml(ctx, coverage_xml, req, proc.stdout_text, proc.stderr_text, proc.exit_code)) {
        return false;
    }

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx),
                                             tag_dir_path,
                                             nob_sv_from_cstr("CoverageManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_coverage"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("Coverage"),
                              coverage_xml)) {
        return false;
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_coverage"),
        .kind = nob_sv_from_cstr("COVERAGE_XML"),
        .status = nob_sv_from_cstr("COLLECTED"),
        .base_dir = tag_dir_path,
        .primary_path = coverage_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_COVERAGE,
        .command_name = nob_sv_from_cstr("ctest_coverage"),
        .submit_part = nob_sv_from_cstr("Coverage"),
        .status = nob_sv_from_cstr("COLLECTED"),
        .model = req->model,
        .track = track,
        .build_dir = req->build_dir,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
    };
    return ctest_commit_step_record(ctx, &req->argv, step, &artifact);
}

Eval_Result eval_handle_ctest_coverage(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Coverage_Request req = {0};
    if (!ctest_parse_coverage_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_coverage_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool ctest_parse_update_request(EvalExecContext *ctx,
                                       const Node *node,
                                       Ctest_Update_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    Ctest_Step_Runtime_Def def = ctest_step_runtime_def(EVAL_CTEST_STEP_UPDATE,
                                                        "ctest_update",
                                                        "Update",
                                                        &s_ctest_update_parse_spec,
                                                        true,
                                                        true);
    if (!ctest_step_runtime_parse_base(ctx, node, &def, &out_req->core)) return false;
    if (!ctest_step_runtime_resolve_context(ctx, &out_req->core)) return false;

    if (out_req->core.model.count == 0) out_req->core.model = nob_sv_from_cstr("Experimental");
    if (!ctest_update_resolve_type(ctx, node, out_req->core.resolved_source, &out_req->update_type)) {
        return false;
    }
    out_req->update_command =
        ctest_update_resolve_command(ctx, out_req->update_type, &out_req->custom_command);
    out_req->update_options = out_req->custom_command
        ? nob_sv_from_cstr("")
        : ctest_update_resolve_options(ctx, out_req->update_type);
    out_req->return_var = ctest_parsed_field_value(&out_req->core.parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var =
        ctest_parsed_field_value(&out_req->core.parsed, "CAPTURE_CMAKE_ERROR");
    out_req->version_override = eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_UPDATE_VERSION_OVERRIDE"));
    out_req->version_only = out_req->version_override.count == 0 &&
        ctest_update_is_truthy(eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_UPDATE_VERSION_ONLY")));
    out_req->quiet = ctest_parsed_field_value(&out_req->core.parsed, "QUIET").count > 0;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_update_commit_failed(EvalExecContext *ctx, const Ctest_Update_Request *req) {
    if (!ctx || !req) return false;
    return ctest_commit_failed_step_record(ctx,
                                           &req->core.argv,
                                           EVAL_CTEST_STEP_UPDATE,
                                           nob_sv_from_cstr("ctest_update"),
                                           nob_sv_from_cstr("Update"),
                                           req->core.model,
                                           req->core.resolved_source,
                                           req->core.resolved_build);
}

static bool ctest_update_fail(EvalExecContext *ctx,
                              const Node *node,
                              const Ctest_Update_Request *req,
                              String_View cause,
                              String_View detail) {
    if (!ctx || !node || !req) return false;

    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr("-1"))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0) {
        if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
        return ctest_update_commit_failed(ctx, req);
    }
    if (!ctest_update_commit_failed(ctx, req)) return false;
    (void)ctest_emit_diag(ctx, node, EV_DIAG_ERROR, cause, detail);
    return false;
}

static bool ctest_execute_update_request(EvalExecContext *ctx,
                                         const Node *node,
                                         const Ctest_Update_Request *req) {
    if (!ctx || !node || !req) return false;

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_update"), "UPDATE_TYPE", req->update_type)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_update"), "UPDATE_COMMAND", req->update_command)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "UPDATE_OPTIONS_RESOLVED",
                         req->update_options)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "VERSION_ONLY",
                         req->version_only ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "VERSION_OVERRIDE",
                         req->version_override)) {
        return false;
    }
    if (!ctest_publish_common_dirs(ctx, req->core.resolved_source, req->core.resolved_build)) return false;

    Eval_Process_Run_Result proc = {0};
    String_View stdout_text = nob_sv_from_cstr("");
    String_View stderr_text = nob_sv_from_cstr("");
    String_View process_result = nob_sv_from_cstr("0");
    String_View updated_files = nob_sv_from_cstr("");
    size_t updated_count = 0;
    int exit_code = 0;
    bool success = true;

    if (req->version_override.count == 0 && !req->version_only) {
        if (req->update_command.count == 0) {
            return ctest_update_fail(ctx,
                                     node,
                                     req,
                                     nob_sv_from_cstr("ctest_update() requires CTEST_UPDATE_COMMAND, UPDATE_COMMAND, or a detected VCS command"),
                                     nob_sv_from_cstr("Set the documented update command or VCS-specific variables before running the update step"));
        }

        SV_List argv = {0};
        if (!ctest_append_command_tokens_temp(ctx, req->update_command, &argv)) return false;
        if (arena_arr_len(argv) == 0) {
            return ctest_update_fail(ctx,
                                     node,
                                     req,
                                     nob_sv_from_cstr("ctest_update() update command did not produce an executable"),
                                     req->update_command);
        }
        if (!req->custom_command && !ctest_append_command_tokens_temp(ctx, req->update_options, &argv)) {
            return false;
        }

        Eval_Process_Run_Request process_req = {
            .argv = argv,
            .argc = arena_arr_len(argv),
            .working_directory = req->core.resolved_source,
        };
        if (!req->quiet) {
            nob_log(NOB_INFO,
                    "ctest_update: updating %.*s",
                    (int)req->core.resolved_source.count,
                    req->core.resolved_source.data ? req->core.resolved_source.data : "");
        }
        if (!eval_process_run_capture(ctx, &process_req, &proc)) return false;

        stdout_text = proc.stdout_text;
        stderr_text = proc.stderr_text;
        process_result = proc.result_text;
        exit_code = proc.exit_code;
        success = proc.started && !proc.timed_out && proc.exit_code == 0;
        if (success && !ctest_update_collect_output_files(ctx, stdout_text, &updated_files, &updated_count)) {
            return false;
        }
    } else if (req->version_override.count > 0) {
        stdout_text = req->version_override;
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_update"), "OUTPUT", stdout_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_update"), "ERROR_OUTPUT", stderr_text)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_update"), "PROCESS_RESULT", process_result)) return false;
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "EXIT_CODE",
                         ctest_submit_format_long_temp(ctx, (long)exit_code))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "UPDATED_FILES",
                         updated_files)) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_update"),
                         "UPDATED_COUNT",
                         ctest_submit_format_size_temp(ctx, updated_count))) {
        return false;
    }

    if (!success) {
        String_View detail = stderr_text.count > 0 ? stderr_text
                            : process_result.count > 0 ? process_result
                            : req->update_command;
        return ctest_update_fail(ctx,
                                 node,
                                 req,
                                 nob_sv_from_cstr("ctest_update() update command failed"),
                                 detail);
    }

    if (req->return_var.count > 0 &&
        !eval_var_set_current(ctx, req->return_var, ctest_submit_format_size_temp(ctx, updated_count))) {
        return false;
    }
    if (req->capture_cmake_error_var.count > 0 &&
        !eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("0"))) {
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->core.resolved_build,
                                  req->core.model,
                                  track,
                                  false,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    String_View update_xml =
        eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("Update.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_update_xml(ctx,
                                update_xml,
                                req,
                                updated_files,
                                updated_count,
                                stdout_text,
                                stderr_text,
                                exit_code)) {
        return false;
    }

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx),
                                             tag_dir_path,
                                             nob_sv_from_cstr("UpdateManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_update"),
                              manifest,
                              tag,
                              track,
                              nob_sv_from_cstr("Update"),
                              update_xml)) {
        return false;
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_update"),
        .kind = nob_sv_from_cstr("UPDATE_XML"),
        .status = nob_sv_from_cstr("UPDATED"),
        .base_dir = tag_dir_path,
        .primary_path = update_xml,
        .aux_paths = manifest,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_UPDATE,
        .command_name = nob_sv_from_cstr("ctest_update"),
        .submit_part = nob_sv_from_cstr("Update"),
        .status = nob_sv_from_cstr("UPDATED"),
        .model = req->core.model,
        .track = track,
        .source_dir = req->core.resolved_source,
        .build_dir = req->core.resolved_build,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
    };
    return ctest_commit_step_record(ctx, &req->core.argv, step, &artifact);
}

Eval_Result eval_handle_ctest_empty_binary_directory(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Empty_Binary_Directory_Request req = {0};
    if (!ctest_parse_empty_binary_directory_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_empty_binary_directory_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_memcheck(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Memcheck_Request req = {0};
    if (!ctest_parse_memcheck_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_memcheck_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_read_custom_files(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Read_Custom_Files_Request req = {0};
    if (!ctest_parse_read_custom_files_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_read_custom_files_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool ctest_parse_double(String_View sv, double *out_value) {
    if (!out_value) return false;
    if (sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    double v = strtod(buf, &end);
    if (!end || *end != '\0') return false;
    *out_value = v;
    return true;
}

static void ctest_sleep_for_duration(double seconds) {
    if (seconds <= 0.0) return;

    double millis_f = (seconds * 1000.0) + 0.5;
    if (millis_f <= 0.0) return;

    unsigned millis = millis_f >= (double)UINT_MAX ? UINT_MAX : (unsigned)millis_f;
#if defined(_WIN32)
    Sleep((DWORD)millis);
#else
    usleep((useconds_t)millis * 1000u);
#endif
}

static bool ctest_parse_run_script_request(EvalExecContext *ctx,
                                           const Node *node,
                                           Ctest_Run_Script_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(out_req->argv); i++) {
        if (eval_sv_eq_ci_lit(out_req->argv[i], "NEW_PROCESS")) {
            out_req->new_process = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(out_req->argv[i], "RETURN_VALUE")) {
            if (i + 1 >= arena_arr_len(out_req->argv)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest_run_script(RETURN_VALUE ...) requires a variable"),
                                      nob_sv_from_cstr("Usage: ctest_run_script(... RETURN_VALUE <var>)"));
                return false;
            }
            out_req->return_var = out_req->argv[++i];
            continue;
        }
        if (!svu_list_push_temp(ctx, &out_req->scripts, out_req->argv[i])) return false;
    }

    if (arena_arr_len(out_req->scripts) == 0) {
        String_View current = eval_current_list_file(ctx);
        if (current.count == 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_run_script() needs a script when no current list file is available"),
                                  nob_sv_from_cstr("Usage: ctest_run_script(<script>...)"));
            return false;
        }
        if (!svu_list_push_temp(ctx, &out_req->scripts, current)) return false;
    }

    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    if (!ctest_resolve_scripts(ctx, out_req->scripts, base_dir, &out_req->resolved_scripts)) return false;

    SV_List stable_argv = NULL;
    SV_List stable_scripts = NULL;
    SV_List stable_resolved_scripts = NULL;
    if (!ctest_copy_sv_list_to_event(ctx, out_req->argv, &stable_argv)) return false;
    if (!ctest_copy_sv_list_to_event(ctx, out_req->scripts, &stable_scripts)) return false;
    if (!ctest_copy_sv_list_to_event(ctx, out_req->resolved_scripts, &stable_resolved_scripts)) return false;
    out_req->argv = stable_argv;
    out_req->scripts = stable_scripts;
    out_req->resolved_scripts = stable_resolved_scripts;
    out_req->return_var = sv_copy_to_event_arena(ctx, out_req->return_var);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_execute_run_script_request(EvalExecContext *ctx, const Ctest_Run_Script_Request *req) {
    if (!ctx || !req) return false;

    eval_command_tx_preserve_scope_vars_on_failure(ctx);
    const char *rv_text = "0";
    for (size_t i = 0; i < arena_arr_len(req->resolved_scripts); i++) {
        if (eval_should_stop(ctx)) eval_reset_stop_request(ctx);
        size_t error_count_before = ctx->runtime_state.run_report.error_count;
        Eval_Result exec_res;
        if (req->new_process) {
            Ctest_New_Process_State child_state = {0};
            if (!ctest_new_process_begin(ctx, &child_state)) return false;
            exec_res = eval_execute_file(ctx, req->resolved_scripts[i], false, nob_sv_from_cstr(""));
            ctest_new_process_end(ctx, &child_state);
            if (ctx->oom) return false;
        } else {
            exec_res = eval_execute_file(ctx, req->resolved_scripts[i], false, nob_sv_from_cstr(""));
        }
        if (eval_result_is_fatal(exec_res)) {
            if (ctx->oom) return false;
            if (eval_should_stop(ctx) &&
                ctx->runtime_state.run_report.error_count > error_count_before) {
                eval_reset_stop_request(ctx);
            } else {
                return false;
            }
        }
        /* Per-script SEND_ERROR diagnostics should not suppress later scripts or
         * the final RETURN_VALUE assignment for ctest_run_script(). */
        if (eval_should_stop(ctx)) eval_reset_stop_request(ctx);
        rv_text = ctx->runtime_state.run_report.error_count > error_count_before ? "1" : "0";
    }

    if (req->return_var.count > 0 && !eval_var_set_current(ctx, req->return_var, nob_sv_from_cstr(rv_text))) return false;
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "EXECUTION_MODE",
                         req->new_process ? nob_sv_from_cstr("NEW_PROCESS") : nob_sv_from_cstr("IN_PROCESS"))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "SCRIPTS",
                         eval_sv_join_semi_temp(ctx, req->scripts, arena_arr_len(req->scripts)))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "RESOLVED_SCRIPTS",
                         eval_sv_join_semi_temp(ctx, req->resolved_scripts, arena_arr_len(req->resolved_scripts)))) {
        return false;
    }
    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_run_script"),
                                       &req->argv,
                                       nob_sv_from_cstr("MODELED"));
}

static bool ctest_parse_sleep_request(EvalExecContext *ctx,
                                      const Node *node,
                                      Ctest_Sleep_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!(arena_arr_len(out_req->argv) == 1 || arena_arr_len(out_req->argv) == 3)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_sleep() expects one or three numeric arguments"),
                              nob_sv_from_cstr("Usage: ctest_sleep(<seconds>) or ctest_sleep(<time1> <duration> <time2>)"));
        return false;
    }

    double values[3] = {0};
    for (size_t i = 0; i < arena_arr_len(out_req->argv); i++) {
        if (!ctest_parse_double(out_req->argv[i], &values[i])) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_sleep() requires numeric arguments"),
                                  out_req->argv[i]);
            return false;
        }
    }

    if (arena_arr_len(out_req->argv) == 1) {
        out_req->duration = out_req->argv[0];
        return true;
    }

    double computed = values[0] + values[1] - values[2];
    if (computed < 0.0) computed = 0.0;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%.3f", computed);
    if (n < 0 || (size_t)n >= sizeof(buf)) return ctx_oom(ctx);
    out_req->duration = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_execute_sleep_request(EvalExecContext *ctx, const Ctest_Sleep_Request *req) {
    if (!ctx || !req) return false;
    double duration_value = 0.0;
    if (!ctest_parse_double(req->duration, &duration_value)) return false;
    ctest_sleep_for_duration(duration_value);
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_sleep"), "DURATION", req->duration)) return false;
    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_sleep"),
                                       &req->argv,
                                       nob_sv_from_cstr("NOOP"));
}

static bool ctest_parse_start_request(EvalExecContext *ctx,
                                      const Node *node,
                                      Ctest_Start_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_parse_generic(ctx,
                             node,
                             nob_sv_from_cstr("ctest_start"),
                             &out_req->argv,
                             &s_ctest_start_parse_spec,
                             &out_req->parsed)) {
        return false;
    }

    out_req->append_mode = ctest_parsed_field_value(&out_req->parsed, "APPEND").count > 0;
    out_req->quiet = ctest_parsed_field_value(&out_req->parsed, "QUIET").count > 0;
    if (arena_arr_len(out_req->parsed.positionals) == 0) {
        if (!out_req->append_mode) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest command received an invalid number of positional arguments"),
                                  nob_sv_from_cstr("ctest_start"));
            return false;
        }
        out_req->model = nob_sv_from_cstr("");
    } else {
        out_req->model = out_req->parsed.positionals[0];
    }

    if (out_req->model.count > 0) {
        String_View canonical_model = ctest_start_canonical_model(out_req->model);
        if (canonical_model.count == 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_start() requires Experimental, Continuous, or Nightly as the dashboard model"),
                                  out_req->model);
            return false;
        }
        out_req->model = canonical_model;
    }

    String_View source = arena_arr_len(out_req->parsed.positionals) >= 2
        ? out_req->parsed.positionals[1]
        : eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY"));
    if (source.count == 0) source = ctest_source_root(ctx);
    String_View build = arena_arr_len(out_req->parsed.positionals) >= 3
        ? out_req->parsed.positionals[2]
        : eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY"));
    if (build.count == 0) build = ctest_binary_root(ctx);
    out_req->group = ctest_parsed_field_value(&out_req->parsed, "GROUP");
    if (out_req->group.count == 0) out_req->group = ctest_parsed_field_value(&out_req->parsed, "TRACK");
    out_req->resolved_source = ctest_resolve_source_dir(ctx, source);
    out_req->resolved_build = ctest_resolve_binary_dir(ctx, build);
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_execute_start_request(EvalExecContext *ctx,
                                        const Node *node,
                                        const Ctest_Start_Request *req) {
    if (!ctx || !node || !req) return false;

    String_View testing_dir_guess = ctest_get_session_field(ctx, "TESTING_DIR");
    if (testing_dir_guess.count == 0) testing_dir_guess = ctest_testing_root(ctx, req->resolved_build);
    if (eval_should_stop(ctx)) return false;

    String_View tag_file_guess = eval_sv_path_join(eval_temp_arena(ctx), testing_dir_guess, nob_sv_from_cstr("TAG"));
    if (eval_should_stop(ctx)) return false;

    Ctest_Tag_File_Details existing = {0};
    if (req->append_mode && ctest_path_exists(ctx, tag_file_guess)) {
        if (!ctest_read_tag_file_details_temp(ctx, tag_file_guess, &existing)) return false;
    }

    String_View effective_model = req->model;
    if (effective_model.count == 0 && existing.model.count > 0) {
        effective_model = ctest_start_canonical_model(existing.model);
    }
    if (effective_model.count == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_start() requires Experimental, Continuous, or Nightly as the dashboard model"),
                              nob_sv_from_cstr("Use APPEND with an existing TAG file or pass an explicit model"));
        return false;
    }

    String_View existing_group = existing.group.count > 0 ? existing.group : existing.model;
    String_View effective_group = req->group;
    if (effective_group.count == 0 && existing_group.count > 0) effective_group = existing_group;
    if (effective_group.count == 0) effective_group = effective_model;

    if (req->append_mode &&
        existing.exists &&
        ((req->model.count > 0 && existing.model.count > 0 && !nob_sv_eq(effective_model, existing.model)) ||
         (req->group.count > 0 && existing_group.count > 0 && !nob_sv_eq(effective_group, existing_group)))) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_WARNING,
                              nob_sv_from_cstr("ctest_start(APPEND) overriding model/group from existing TAG file"),
                              tag_file_guess);
    }

    String_View checkout_command = ctest_start_checkout_command(ctx);
    String_View checkout_working_directory = nob_sv_from_cstr("");
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "CHECKOUT_COMMAND", checkout_command)) return false;
    if (!ctest_start_run_checkout_command(ctx, node, req->resolved_source, &checkout_working_directory)) return false;
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_start"),
                         "CHECKOUT_WORKING_DIRECTORY",
                         checkout_working_directory)) {
        return false;
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "MODEL", effective_model)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "GROUP", effective_group)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TRACK", effective_group)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "RESOLVED_SOURCE", req->resolved_source)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "RESOLVED_BUILD", req->resolved_build)) return false;

    if (!ctest_set_session_field(ctx, "MODEL", effective_model)) return false;
    if (!ctest_set_session_field(ctx, "GROUP", effective_group)) return false;
    if (!ctest_set_session_field(ctx, "SOURCE", req->resolved_source)) return false;
    if (!ctest_set_session_field(ctx, "BUILD", req->resolved_build)) return false;
    if (!ctest_set_session_field(ctx, "TRACK", effective_group)) return false;

    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->resolved_build,
                                  effective_model,
                                  effective_group,
                                  req->append_mode,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_MODEL"), effective_model)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_GROUP"), effective_group)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CTEST_TRACK"), effective_group)) return false;
    if (!ctest_publish_common_dirs(ctx, req->resolved_source, req->resolved_build)) return false;
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_START,
        .command_name = nob_sv_from_cstr("ctest_start"),
        .submit_part = nob_sv_from_cstr("Start"),
        .status = nob_sv_from_cstr("STAGED"),
        .model = effective_model,
        .track = effective_group,
        .source_dir = req->resolved_source,
        .build_dir = req->resolved_build,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
    };
    return ctest_commit_step_record(ctx, &req->argv, step, NULL);
}

static bool ctest_parse_submit_request(EvalExecContext *ctx,
                                       const Node *node,
                                       Ctest_Submit_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_parse_generic(ctx,
                             node,
                             nob_sv_from_cstr("ctest_submit"),
                             &out_req->argv,
                             &s_ctest_submit_parse_spec,
                             &out_req->parsed)) {
        return false;
    }

    out_req->model = ctest_get_session_field(ctx, "MODEL");
    if (out_req->model.count == 0) out_req->model = nob_sv_from_cstr("Experimental");
    out_req->build_dir = ctest_session_resolved_build_dir(ctx);
    out_req->submit_url = ctest_submit_resolve_url(ctx, &out_req->parsed);
    out_req->build_id_var = ctest_parsed_field_value(&out_req->parsed, "BUILD_ID");
    out_req->return_var = ctest_parsed_field_value(&out_req->parsed, "RETURN_VALUE");
    out_req->capture_cmake_error_var = ctest_parsed_field_value(&out_req->parsed, "CAPTURE_CMAKE_ERROR");
    out_req->http_headers = ctest_join_parsed_field_values(ctx, &out_req->parsed, "HTTPHEADER");
    if (!ctest_collect_parsed_field_values(ctx, &out_req->parsed, "HTTPHEADER", &out_req->http_header_items)) {
        return false;
    }
    out_req->cdash_upload_file = ctest_parsed_field_value(&out_req->parsed, "CDASH_UPLOAD");
    out_req->cdash_upload_type = ctest_parsed_field_value(&out_req->parsed, "CDASH_UPLOAD_TYPE");
    out_req->quiet = ctest_parsed_field_value(&out_req->parsed, "QUIET").count > 0;
    out_req->is_cdash_upload = out_req->cdash_upload_file.count > 0;
    if (!ctest_submit_resolve_numeric_setting(ctx,
                                              node,
                                              &out_req->parsed,
                                              "RETRY_COUNT",
                                              "CTEST_SUBMIT_RETRY_COUNT",
                                              &out_req->retry_count)) {
        return false;
    }
    if (!ctest_submit_resolve_numeric_setting(ctx,
                                              node,
                                              &out_req->parsed,
                                              "RETRY_DELAY",
                                              "CTEST_SUBMIT_RETRY_DELAY",
                                              &out_req->retry_delay_sec)) {
        return false;
    }
    if (!ctest_submit_resolve_numeric_setting(ctx,
                                              node,
                                              &out_req->parsed,
                                              NULL,
                                              "CTEST_SUBMIT_INACTIVITY_TIMEOUT",
                                              &out_req->inactivity_timeout_sec)) {
        return false;
    }
    out_req->has_inactivity_timeout =
        eval_var_get_visible(ctx, nob_sv_from_cstr("CTEST_SUBMIT_INACTIVITY_TIMEOUT")).count > 0;
    if (!ctest_submit_resolve_tls_verify(ctx, &out_req->has_tls_verify, &out_req->tls_verify)) return false;
    if (eval_should_stop(ctx)) return false;

    String_View explicit_parts = ctest_join_parsed_field_values(ctx, &out_req->parsed, "PARTS");
    String_View explicit_files = ctest_join_parsed_field_values(ctx, &out_req->parsed, "FILES");
    if (eval_should_stop(ctx)) return false;

    String_View base_dir = out_req->build_dir.count > 0 ? out_req->build_dir : ctest_current_binary_dir(ctx);

    if (out_req->is_cdash_upload) {
        if (explicit_parts.count > 0 || explicit_files.count > 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_submit(CDASH_UPLOAD ...) does not accept PARTS or FILES"),
                                  nob_sv_from_cstr("Use the documented CDASH_UPLOAD signature"));
            return false;
        }

        SV_List resolved_files = {0};
        if (!ctest_submit_append_resolved_files(ctx,
                                                node,
                                                nob_sv_from_cstr("ctest_submit"),
                                                out_req->cdash_upload_file,
                                                base_dir,
                                                true,
                                                &resolved_files)) {
            return false;
        }
        out_req->files = eval_sv_join_semi_temp(ctx, resolved_files, arena_arr_len(resolved_files));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (out_req->cdash_upload_type.count > 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_submit() only accepts CDASH_UPLOAD_TYPE with CDASH_UPLOAD"),
                              out_req->cdash_upload_type);
        return false;
    }

    if (!ctest_submit_resolve_parts(ctx, node, &out_req->parsed, &out_req->parts)) return false;

    SV_List resolved_files = {0};
    if (!ctest_submit_append_resolved_files(ctx,
                                            node,
                                            nob_sv_from_cstr("ctest_submit"),
                                            explicit_files,
                                            base_dir,
                                            false,
                                            &resolved_files)) {
        return false;
    }
    if (!ctest_submit_append_part_files(ctx, node, out_req->parts, out_req->build_dir, &resolved_files)) {
        return false;
    }

    out_req->files = eval_sv_join_semi_temp(ctx, resolved_files, arena_arr_len(resolved_files));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool ctest_submit_set_optional_var(EvalExecContext *ctx, String_View var_name, String_View value) {
    if (!ctx || var_name.count == 0) return true;
    return eval_var_set_current(ctx, var_name, value);
}

static bool ctest_execute_submit_request(EvalExecContext *ctx,
                                         const Node *node,
                                         const Ctest_Submit_Request *req) {
    if (!ctx || !node || !req) return false;

    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->build_dir,
                                  req->model,
                                  ctest_get_session_field(ctx, "TRACK"),
                                  false,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    String_View track = ctest_get_session_field(ctx, "TRACK");
    String_View manifest_name = req->is_cdash_upload ? nob_sv_from_cstr("CDashUploadManifest.txt")
                                                     : nob_sv_from_cstr("SubmitManifest.txt");
    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, manifest_name);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_submit_manifest(ctx, manifest, tag, track, req)) {
        return false;
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "SIGNATURE",
                         req->is_cdash_upload ? nob_sv_from_cstr("CDASH_UPLOAD")
                                              : nob_sv_from_cstr("DEFAULT"))) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TAG", tag)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TAG_FILE", tag_file)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "TESTING_DIR", testing_dir)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "MANIFEST", manifest)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "SUBMIT_URL", req->submit_url)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "HTTPHEADER", req->http_headers)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "CDASH_UPLOAD", req->cdash_upload_file)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "CDASH_UPLOAD_TYPE", req->cdash_upload_type)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESOLVED_PARTS", req->parts)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESOLVED_FILES", req->files)) return false;
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "RETRY_COUNT_RESOLVED",
                         ctest_submit_format_size_temp(ctx, req->retry_count))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "RETRY_DELAY_RESOLVED",
                         ctest_submit_format_size_temp(ctx, req->retry_delay_sec))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "INACTIVITY_TIMEOUT",
                         req->has_inactivity_timeout
                             ? ctest_submit_format_size_temp(ctx, req->inactivity_timeout_sec)
                             : nob_sv_from_cstr(""))) {
        return false;
    }

    Ctest_Submit_Remote_Result remote = {0};
    if (req->submit_url.count == 0) {
        remote.success = false;
        remote.exit_code = 1;
        remote.response_detail =
            nob_sv_from_cstr("ctest_submit() requires SUBMIT_URL, CTEST_SUBMIT_URL, or legacy drop-site settings");
    } else if (req->is_cdash_upload) {
        if (!ctest_submit_execute_cdash_upload_remote(ctx, req, &remote)) return false;
    } else {
        if (!ctest_submit_execute_default_remote(ctx, req, tag, track, manifest, &remote)) return false;
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "ATTEMPTS",
                         ctest_submit_format_size_temp(ctx, remote.attempts))) {
        return false;
    }
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_submit"),
                         "HTTP_CODE",
                         ctest_submit_format_long_temp(ctx, remote.http_code))) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESPONSE", remote.response_body)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "RESPONSE_DETAIL", remote.response_detail)) {
        return false;
    }
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_submit"), "BUILD_ID_RESULT", remote.build_id)) {
        return false;
    }

    String_View rv_value = remote.success ? nob_sv_from_cstr("0")
                                          : ctest_submit_format_size_temp(ctx,
                                                                          remote.exit_code > 0
                                                                              ? (size_t)remote.exit_code
                                                                              : 1U);
    if (!ctest_submit_set_optional_var(ctx, req->return_var, rv_value)) return false;
    if (!ctest_submit_set_optional_var(ctx,
                                       req->build_id_var,
                                       remote.success ? remote.build_id : nob_sv_from_cstr(""))) {
        return false;
    }

    if (remote.success) {
        return eval_ctest_publish_metadata(ctx,
                                           nob_sv_from_cstr("ctest_submit"),
                                           &req->argv,
                                           nob_sv_from_cstr("SUBMITTED"));
    }

    if (req->capture_cmake_error_var.count > 0) {
        if (!eval_var_set_current(ctx, req->capture_cmake_error_var, nob_sv_from_cstr("-1"))) return false;
        return eval_ctest_publish_metadata(ctx,
                                           nob_sv_from_cstr("ctest_submit"),
                                           &req->argv,
                                           nob_sv_from_cstr("FAILED"));
    }

    (void)ctest_emit_diag(ctx,
                          node,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("ctest_submit() failed to submit dashboard data"),
                          remote.response_detail.count > 0 ? remote.response_detail : req->submit_url);
    return false;
}

static bool ctest_parse_upload_request(EvalExecContext *ctx,
                                       const Node *node,
                                       Ctest_Upload_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    if (!ctest_parse_generic(ctx,
                             node,
                             nob_sv_from_cstr("ctest_upload"),
                             &out_req->argv,
                             &s_ctest_upload_parse_spec,
                             &out_req->parsed)) {
        return false;
    }

    out_req->model = ctest_get_session_field(ctx, "MODEL");
    if (out_req->model.count == 0) out_req->model = nob_sv_from_cstr("Experimental");
    out_req->build_dir = ctest_session_resolved_build_dir(ctx);
    out_req->capture_cmake_error_var = ctest_parsed_field_value(&out_req->parsed, "CAPTURE_CMAKE_ERROR");
    out_req->quiet = ctest_parsed_field_value(&out_req->parsed, "QUIET").count > 0;
    return ctest_resolve_files(ctx,
                               node,
                               nob_sv_from_cstr("ctest_upload"),
                               ctest_parsed_field_value(&out_req->parsed, "FILES"),
                               out_req->build_dir.count > 0 ? out_req->build_dir : ctest_current_binary_dir(ctx),
                               true,
                               true,
                               &out_req->files);
}

static bool ctest_execute_upload_request(EvalExecContext *ctx, const Ctest_Upload_Request *req) {
    if (!ctx || !req) return false;

    String_View tag = nob_sv_from_cstr("");
    String_View testing_dir = nob_sv_from_cstr("");
    String_View tag_file = nob_sv_from_cstr("");
    String_View tag_dir_path = nob_sv_from_cstr("");
    if (!ctest_ensure_session_tag(ctx,
                                  req->build_dir,
                                  req->model,
                                  ctest_get_session_field(ctx, "TRACK"),
                                  false,
                                  &tag,
                                  &testing_dir,
                                  &tag_file,
                                  &tag_dir_path)) {
        return false;
    }

    String_View upload_xml = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("Upload.xml"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_upload_xml(ctx, upload_xml, req->files)) return false;

    String_View manifest = eval_sv_path_join(eval_temp_arena(ctx), tag_dir_path, nob_sv_from_cstr("UploadManifest.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!ctest_write_manifest(ctx,
                              nob_sv_from_cstr("ctest_upload"),
                              manifest,
                              tag,
                              ctest_get_session_field(ctx, "TRACK"),
                              nob_sv_from_cstr(""),
                              req->files)) {
        return false;
    }

    Eval_Canonical_Artifact artifact = {
        .producer = nob_sv_from_cstr("ctest_upload"),
        .kind = nob_sv_from_cstr("UPLOAD_XML"),
        .status = nob_sv_from_cstr("STAGED"),
        .base_dir = tag_dir_path,
        .primary_path = upload_xml,
        .aux_paths = req->files,
    };
    Eval_Ctest_Step_Record step = {
        .kind = EVAL_CTEST_STEP_UPLOAD,
        .command_name = nob_sv_from_cstr("ctest_upload"),
        .submit_part = nob_sv_from_cstr("Upload"),
        .status = nob_sv_from_cstr("STAGED"),
        .model = req->model,
        .track = ctest_get_session_field(ctx, "TRACK"),
        .build_dir = req->build_dir,
        .testing_dir = testing_dir,
        .tag = tag,
        .tag_file = tag_file,
        .tag_dir = tag_dir_path,
        .manifest = manifest,
        .submit_files = req->files,
    };
    return ctest_commit_step_record(ctx, &req->argv, step, &artifact);
}

Eval_Result eval_handle_ctest_run_script(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Run_Script_Request req = {0};
    if (!ctest_parse_run_script_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_run_script_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_sleep(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Sleep_Request req = {0};
    if (!ctest_parse_sleep_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_sleep_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_start(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Start_Request req = {0};
    if (!ctest_parse_start_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_start_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_submit(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Submit_Request req = {0};
    if (!ctest_parse_submit_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_submit_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_test(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Test_Request req = {0};
    if (!ctest_parse_test_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_test_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_update(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Update_Request req = {0};
    if (!ctest_parse_update_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_update_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_upload(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Upload_Request req = {0};
    if (!ctest_parse_upload_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_upload_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
