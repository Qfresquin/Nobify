#include "eval_ctest.h"

#include "eval_hash.h"
#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    bool cpack_component_module_loaded;
    bool fetchcontent_module_loaded;
    bool scope_pushed;
} Ctest_New_Process_State;

typedef struct {
    String_View command_name;
    String_View status;
    const Ctest_Parse_Spec *spec;
    bool needs_source;
    bool needs_build;
    SV_List argv;
    Ctest_Parse_Result parsed;
} Ctest_Metadata_Request;

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
static const char *const s_ctest_configure_flag_keywords[] = {"QUIET"};
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
static const char *const s_ctest_coverage_flag_keywords[] = {"QUIET"};
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
    "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "DEFECT_COUNT", "PARALLEL_LEVEL"
};
static const char *const s_ctest_memcheck_flag_keywords[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
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

static String_View ctest_resolve_binary_dir(EvalExecContext *ctx, String_View raw) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw.count == 0) return nob_sv_from_cstr("");
    String_View resolved = eval_path_resolve_for_cmake_arg(ctx, raw, ctest_current_binary_dir(ctx), false);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_normalize_temp(ctx, resolved);
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
    build = ctest_binary_root(ctx);
    return ctest_resolve_binary_dir(ctx, build);
}

static String_View ctest_session_resolved_source_dir(EvalExecContext *ctx) {
    String_View source = ctest_get_session_field(ctx, "SOURCE");
    if (source.count > 0) return source;
    source = ctest_source_root(ctx);
    return ctest_resolve_source_dir(ctx, source);
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
                                     String_View model,
                                     String_View group,
                                     bool append_mode,
                                     String_View *out_tag,
                                     String_View *out_testing_dir,
                                     String_View *out_tag_file,
                                     String_View *out_tag_dir) {
    if (!ctx || !out_tag || !out_testing_dir || !out_tag_file || !out_tag_dir) return false;

    String_View build_dir = ctest_session_resolved_build_dir(ctx);
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
    ctx->cpack_component_module_loaded = state->cpack_component_module_loaded;
    ctx->fetchcontent_module_loaded = state->fetchcontent_module_loaded;
    eval_exec_clear_pending_flow(ctx);
    eval_clear_return_state(ctx);
    eval_clear_stop_if_not_oom(ctx);
}

static bool ctest_apply_resolved_context(EvalExecContext *ctx,
                                         String_View command_name,
                                         const Ctest_Parse_Result *parsed,
                                         bool needs_source,
                                         bool needs_build) {
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

static String_View ctest_get_command_status(EvalExecContext *ctx, const char *command_name) {
    if (!ctx || !command_name) return nob_sv_from_cstr("");

    size_t total = sizeof("NOBIFY_CTEST::") - 1 + strlen(command_name) + sizeof("::STATUS") - 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    int n = snprintf(buf, total + 1, "NOBIFY_CTEST::%s::STATUS", command_name);
    if (n < 0) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return eval_var_get_visible(ctx, nob_sv_from_cstr(buf));
}

static String_View ctest_submit_canonical_part(String_View raw_part) {
    for (size_t i = 0; i < sizeof(s_ctest_submit_part_defs) / sizeof(s_ctest_submit_part_defs[0]); i++) {
        if (eval_sv_eq_ci_lit(raw_part, s_ctest_submit_part_defs[i].name)) {
            return nob_sv_from_cstr(s_ctest_submit_part_defs[i].name);
        }
    }
    return nob_sv_from_cstr("");
}

static bool ctest_submit_part_is_available(EvalExecContext *ctx, const Ctest_Submit_Part_Def *part_def) {
    if (!ctx || !part_def || !part_def->name) return false;
    if (part_def->status_command) return ctest_get_command_status(ctx, part_def->status_command).count > 0;
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
        } else if (eval_sv_eq_ci_lit(part_items[i], "Upload")) {
            if (!ctest_append_joined_list_unique(ctx,
                                                 eval_var_get_visible(ctx,
                                                                      nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::UPLOAD_XML")),
                                                 out_files)) {
                return false;
            }
            if (!ctest_append_joined_list_unique(ctx,
                                                 eval_var_get_visible(ctx,
                                                                      nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                                                 out_files)) {
                return false;
            }
        }
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

static bool ctest_parse_metadata_request(EvalExecContext *ctx,
                                         const Node *node,
                                         String_View command_name,
                                         const Ctest_Parse_Spec *spec,
                                         bool needs_source,
                                         bool needs_build,
                                         String_View status,
                                         Ctest_Metadata_Request *out_req) {
    if (!ctx || !node || !spec || !out_req) return false;
    memset(out_req, 0, sizeof(*out_req));

    out_req->command_name = command_name;
    out_req->status = status;
    out_req->spec = spec;
    out_req->needs_source = needs_source;
    out_req->needs_build = needs_build;
    out_req->argv = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return false;
    return ctest_parse_generic(ctx, node, command_name, &out_req->argv, spec, &out_req->parsed);
}

static bool ctest_execute_metadata_request(EvalExecContext *ctx, const Ctest_Metadata_Request *req) {
    if (!ctx || !req) return false;
    if (!ctest_apply_resolved_context(ctx,
                                      req->command_name,
                                      &req->parsed,
                                      req->needs_source,
                                      req->needs_build)) {
        return false;
    }
    return eval_ctest_publish_metadata(ctx, req->command_name, &req->argv, req->status);
}

static Eval_Result ctest_handle_metadata_request(EvalExecContext *ctx,
                                                 const Node *node,
                                                 String_View command_name,
                                                 const Ctest_Parse_Spec *spec,
                                                 bool needs_source,
                                                 bool needs_build,
                                                 String_View status) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Metadata_Request req = {0};
    if (!ctest_parse_metadata_request(ctx, node, command_name, spec, needs_source, needs_build, status, &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!ctest_execute_metadata_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
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

    if (nob_file_exists(target_c) && nob_get_file_type(target_c) == NOB_FILE_DIRECTORY) {
        if (!ctest_remove_tree(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to remove directory contents"),
                                  req->target);
            return false;
        }
        if (!nob_mkdir_if_not_exists(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to recreate directory"),
                                  req->target);
            return false;
        }
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
    for (size_t i = 0; i < arena_arr_len(out_req->argv); i++) {
        String_View dir = eval_path_resolve_for_cmake_arg(ctx, out_req->argv[i], base_dir, false);
        String_View custom = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr("CTestCustom.cmake"));
        if (eval_should_stop(ctx)) return false;

        char *custom_c = eval_sv_to_cstr_temp(ctx, custom);
        EVAL_OOM_RETURN_IF_NULL(ctx, custom_c, false);
        if (nob_file_exists(custom_c) && nob_get_file_type(custom_c) != NOB_FILE_DIRECTORY) {
            if (!svu_list_push_temp(ctx, &out_req->loaded_files, custom)) return false;
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

Eval_Result eval_handle_ctest_build(EvalExecContext *ctx, const Node *node) {
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_build"),
                                         &s_ctest_build_parse_spec,
                                         false,
                                         true,
                                         nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_configure(EvalExecContext *ctx, const Node *node) {
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_configure"),
                                         &s_ctest_configure_parse_spec,
                                         true,
                                         true,
                                         nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_coverage(EvalExecContext *ctx, const Node *node) {
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_coverage"),
                                         &s_ctest_coverage_parse_spec,
                                         false,
                                         true,
                                         nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_empty_binary_directory(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Empty_Binary_Directory_Request req = {0};
    if (!ctest_parse_empty_binary_directory_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_empty_binary_directory_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_ctest_memcheck(EvalExecContext *ctx, const Node *node) {
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_memcheck"),
                                         &s_ctest_memcheck_parse_spec,
                                         false,
                                         true,
                                         nob_sv_from_cstr("MODELED"));
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
    return ctest_resolve_scripts(ctx, out_req->scripts, base_dir, &out_req->resolved_scripts);
}

static bool ctest_execute_run_script_request(EvalExecContext *ctx, const Ctest_Run_Script_Request *req) {
    if (!ctx || !req) return false;

    const char *rv_text = "0";
    for (size_t i = 0; i < arena_arr_len(req->resolved_scripts); i++) {
        size_t error_count_before = ctx->runtime_state.run_report.error_count;
        Eval_Result exec_res = eval_result_fatal();
        if (req->new_process) {
            Ctest_New_Process_State child_state = {0};
            if (!ctest_new_process_begin(ctx, &child_state)) return false;
            exec_res = eval_execute_file(ctx, req->resolved_scripts[i], false, nob_sv_from_cstr(""));
            ctest_new_process_end(ctx, &child_state);
            if (ctx->oom) return false;
        } else {
            exec_res = eval_execute_file(ctx, req->resolved_scripts[i], false, nob_sv_from_cstr(""));
        }
        bool script_failed = eval_result_is_fatal(exec_res) ||
                             ctx->runtime_state.run_report.error_count > error_count_before;
        if (script_failed) {
            rv_text = "1";
            break;
        }
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool ctest_execute_sleep_request(EvalExecContext *ctx, const Ctest_Sleep_Request *req) {
    if (!ctx || !req) return false;
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG", tag)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TESTING_DIR", testing_dir)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG_FILE", tag_file)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_start"), "TAG_DIR", tag_dir_path)) return false;

    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_start"),
                                       &req->argv,
                                       nob_sv_from_cstr("STAGED"));
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
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
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

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TAG", tag)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TAG_FILE", tag_file)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "TESTING_DIR", testing_dir)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "UPLOAD_XML", upload_xml)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "MANIFEST", manifest)) return false;
    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_upload"), "RESOLVED_FILES", req->files)) return false;

    return eval_ctest_publish_metadata(ctx,
                                       nob_sv_from_cstr("ctest_upload"),
                                       &req->argv,
                                       nob_sv_from_cstr("STAGED"));
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
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_test"),
                                         &s_ctest_test_parse_spec,
                                         false,
                                         true,
                                         nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_update(EvalExecContext *ctx, const Node *node) {
    return ctest_handle_metadata_request(ctx,
                                         node,
                                         nob_sv_from_cstr("ctest_update"),
                                         &s_ctest_update_parse_spec,
                                         true,
                                         false,
                                         nob_sv_from_cstr("MODELED"));
}

Eval_Result eval_handle_ctest_upload(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    Ctest_Upload_Request req = {0};
    if (!ctest_parse_upload_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    if (!ctest_execute_upload_request(ctx, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}
