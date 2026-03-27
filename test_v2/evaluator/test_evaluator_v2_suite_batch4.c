#include "test_evaluator_v2_support.h"
#include "test_fs.h"

typedef enum {
    CTEST_SUBMIT_MOCK_REMOTE_SUCCESS_AFTER_TIMEOUT = 0,
    CTEST_SUBMIT_MOCK_CDASH_UPLOAD_SUCCESS,
    CTEST_SUBMIT_MOCK_FAILURE,
} Ctest_Submit_Mock_Mode;

typedef struct {
    Ctest_Submit_Mock_Mode mode;
    size_t timeout_failures_remaining;
    size_t call_count;
    size_t submit_call_count;
    size_t update_call_count;
    size_t build_call_count;
    size_t file_form_count;
    bool saw_auth_header;
    bool saw_extra_header;
    bool saw_manifest_form;
    bool saw_parts_form;
    bool saw_probe_phase;
    bool saw_upload_phase;
    bool saw_hash_form;
    bool saw_type_form;
    bool saw_legacy_url;
    bool saw_submit_url;
    bool saw_upload_xml_form;
    bool saw_upload_payload_form;
    bool saw_update_tool;
    bool saw_update_mode_flag;
    bool saw_update_mode_value;
    bool saw_build_tool;
    char last_url[512];
    char update_working_directory[512];
} Ctest_Submit_Mock_Process_Data;

typedef struct {
    size_t call_count;
    bool saw_checkout_tool;
    bool saw_flag;
    bool saw_value_with_space;
    char working_directory[512];
} Ctest_Start_Mock_Process_Data;

typedef struct {
    size_t call_count;
    size_t build_call_count;
    bool saw_configure_tool;
    bool saw_build_tool;
    bool saw_source_arg;
    bool saw_preset_flag;
    bool saw_preset_value;
    char working_directory[512];
    char source_arg[512];
} Ctest_Configure_Mock_Process_Data;

typedef struct {
    size_t call_count;
    bool saw_coverage_tool;
    bool saw_fast_flag;
    bool saw_xml_flag;
    bool saw_mode_flag;
    bool saw_mode_value;
    bool args_in_expected_order;
    char working_directory[512];
} Ctest_Coverage_Mock_Process_Data;

typedef struct {
    size_t call_count;
    bool saw_build_tool;
    bool saw_build_flag;
    bool saw_target_flag;
    bool saw_target_value;
    bool saw_config_flag;
    bool saw_config_value;
    bool saw_parallel_flag;
    bool saw_parallel_value;
    char working_directory[512];
} Ctest_Build_Mock_Process_Data;

typedef struct {
    size_t call_count;
    bool saw_test_pass_tool;
    bool saw_test_fail_tool;
    bool saw_pass_arg;
    bool saw_fail_arg;
    char default_working_directory[512];
    char custom_working_directory[512];
} Ctest_Test_Mock_Process_Data;

typedef struct {
    Ctest_Submit_Mock_Process_Data submit;
    size_t memcheck_call_count;
    bool saw_memcheck_tool;
    bool saw_backend_xml_flag;
    bool saw_backend_trace_children_flag;
    bool saw_backend_sanitizer_flag;
    bool saw_suppressions_flag;
    bool saw_separator;
    bool saw_test_pass_tool;
    bool saw_test_fail_tool;
    bool saw_pass_arg;
    bool saw_fail_arg;
    char default_working_directory[512];
    char custom_working_directory[512];
} Ctest_Memcheck_Mock_Process_Data;

static bool ctest_submit_mock_has_prefix(String_View value, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    return value.count >= prefix_len && memcmp(value.data, prefix, prefix_len) == 0;
}

static bool ctest_submit_mock_contains(String_View value, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || value.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= value.count; i++) {
        if (memcmp(value.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool ctest_submit_mock_process_run(void *user_data,
                                          Arena *scratch_arena,
                                          const Eval_Process_Run_Request *request,
                                          Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Submit_Mock_Process_Data *data = (Ctest_Submit_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("0");
    data->call_count++;

    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("update-tool"))) {
        data->update_call_count++;
        data->saw_update_tool = true;
        for (size_t i = 1; i < request->argc; i++) {
            if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--mode")) && i + 1 < request->argc) {
                data->saw_update_mode_flag = true;
                if (nob_sv_eq(request->argv[i + 1], nob_sv_from_cstr("sync"))) {
                    data->saw_update_mode_value = true;
                }
                i++;
            }
        }
        if (request->working_directory.count > 0) {
            size_t n = request->working_directory.count < sizeof(data->update_working_directory) - 1
                ? request->working_directory.count
                : sizeof(data->update_working_directory) - 1;
            memcpy(data->update_working_directory, request->working_directory.data, n);
            data->update_working_directory[n] = '\0';
        }
        out_result->exit_code = 0;
        out_result->stdout_text = nob_sv_from_cstr("Updated: src/main.c\nUpdated: src/lib.c\n");
        return true;
    }

    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("build-tool"))) {
        data->build_call_count++;
        data->saw_build_tool = true;
        out_result->exit_code = 0;
        out_result->stdout_text = nob_sv_from_cstr("Built target all\n");
        return true;
    }

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("curl"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->submit_call_count++;

    if (request->argc > 0) {
        String_View url = request->argv[request->argc - 1];
        size_t n = url.count < sizeof(data->last_url) - 1 ? url.count : sizeof(data->last_url) - 1;
        memcpy(data->last_url, url.data, n);
        data->last_url[n] = '\0';
        if (ctest_submit_mock_has_prefix(url, "https://submit.example.test/submit.php?project=Nobify")) {
            data->saw_legacy_url = true;
        }
        if (ctest_submit_mock_has_prefix(url, "https://cdash.example.test/api/v1")) {
            data->saw_submit_url = true;
        }
        if (ctest_submit_mock_has_prefix(url, "https://fail.example.test/submit")) {
            data->saw_submit_url = true;
        }
    }

    for (size_t i = 1; i < request->argc; i++) {
        String_View arg = request->argv[i];
        if (nob_sv_eq(arg, nob_sv_from_cstr("--header")) && i + 1 < request->argc) {
            String_View header = request->argv[++i];
            if (nob_sv_eq(header, nob_sv_from_cstr("Authorization: Bearer one"))) data->saw_auth_header = true;
            if (nob_sv_eq(header, nob_sv_from_cstr("X-Nobify: yes"))) data->saw_extra_header = true;
            continue;
        }
        if ((nob_sv_eq(arg, nob_sv_from_cstr("--form")) ||
             nob_sv_eq(arg, nob_sv_from_cstr("--form-string"))) &&
            i + 1 < request->argc) {
            String_View form = request->argv[++i];
            if (ctest_submit_mock_has_prefix(form, "manifest=@")) data->saw_manifest_form = true;
            if (ctest_submit_mock_has_prefix(form, "file=@")) {
                data->file_form_count++;
                if (ctest_submit_mock_contains(form, "/Upload.xml")) data->saw_upload_xml_form = true;
                if (ctest_submit_mock_contains(form, "/upload.bin")) data->saw_upload_payload_form = true;
            }
            if (ctest_submit_mock_has_prefix(form, "parts=")) data->saw_parts_form = true;
            if (nob_sv_eq(form, nob_sv_from_cstr("phase=probe"))) data->saw_probe_phase = true;
            if (nob_sv_eq(form, nob_sv_from_cstr("phase=upload"))) data->saw_upload_phase = true;
            if (ctest_submit_mock_has_prefix(form, "hash=")) data->saw_hash_form = true;
            if (ctest_submit_mock_has_prefix(form, "type=")) data->saw_type_form = true;
        }
    }

    if (data->mode == CTEST_SUBMIT_MOCK_REMOTE_SUCCESS_AFTER_TIMEOUT &&
        data->timeout_failures_remaining > 0) {
        data->timeout_failures_remaining--;
        out_result->exit_code = 28;
        out_result->stderr_text = nob_sv_from_cstr("Operation timed out");
        out_result->result_text = nob_sv_from_cstr("28");
        return true;
    }

    if (data->mode == CTEST_SUBMIT_MOCK_CDASH_UPLOAD_SUCCESS) {
        if (data->saw_probe_phase && !data->saw_upload_phase) {
            out_result->exit_code = 0;
            out_result->stdout_text = nob_sv_from_cstr("upload=1\nbuildid=88\n__NOBIFY_HTTP_CODE=200\n");
            return true;
        }
        out_result->exit_code = 0;
        out_result->stdout_text = nob_sv_from_cstr("buildid=88\n__NOBIFY_HTTP_CODE=200\n");
        return true;
    }

    if (data->mode == CTEST_SUBMIT_MOCK_FAILURE) {
        out_result->exit_code = 6;
        out_result->stderr_text = nob_sv_from_cstr("Could not resolve host");
        out_result->result_text = nob_sv_from_cstr("6");
        return true;
    }

    out_result->exit_code = 0;
    out_result->stdout_text = nob_sv_from_cstr("buildid=321\n__NOBIFY_HTTP_CODE=200\n");
    return true;
}

static bool ctest_start_mock_process_run(void *user_data,
                                         Arena *scratch_arena,
                                         const Eval_Process_Run_Request *request,
                                         Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Start_Mock_Process_Data *data = (Ctest_Start_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("0");
    data->call_count++;

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("checkout-tool"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->saw_checkout_tool = true;
    if (request->argc >= 2 && nob_sv_eq(request->argv[1], nob_sv_from_cstr("--flag"))) data->saw_flag = true;
    if (request->argc >= 3 &&
        nob_sv_eq(request->argv[2], nob_sv_from_cstr("value with space"))) {
        data->saw_value_with_space = true;
    }
    if (request->working_directory.count > 0) {
        size_t n = request->working_directory.count < sizeof(data->working_directory) - 1
            ? request->working_directory.count
            : sizeof(data->working_directory) - 1;
        memcpy(data->working_directory, request->working_directory.data, n);
        data->working_directory[n] = '\0';
    }

    out_result->exit_code = 0;
    return true;
}

static bool ctest_configure_mock_process_run(void *user_data,
                                             Arena *scratch_arena,
                                             const Eval_Process_Run_Request *request,
                                             Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Configure_Mock_Process_Data *data = (Ctest_Configure_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("0");
    data->call_count++;

    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("build-tool"))) {
        data->build_call_count++;
        data->saw_build_tool = true;
        if (request->working_directory.count > 0) {
            size_t n = request->working_directory.count < sizeof(data->working_directory) - 1
                ? request->working_directory.count
                : sizeof(data->working_directory) - 1;
            memcpy(data->working_directory, request->working_directory.data, n);
            data->working_directory[n] = '\0';
        }
        out_result->exit_code = 0;
        out_result->stdout_text = nob_sv_from_cstr("Built target all\n");
        return true;
    }

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("configure-tool"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->saw_configure_tool = true;
    for (size_t i = 1; i < request->argc; i++) {
        if (!data->saw_source_arg && request->argv[i].count > 0 && request->argv[i].data[0] != '-') {
            size_t n = request->argv[i].count < sizeof(data->source_arg) - 1
                ? request->argv[i].count
                : sizeof(data->source_arg) - 1;
            memcpy(data->source_arg, request->argv[i].data, n);
            data->source_arg[n] = '\0';
            data->saw_source_arg = true;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--preset"))) data->saw_preset_flag = true;
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("dev"))) data->saw_preset_value = true;
    }

    if (request->working_directory.count > 0) {
        size_t n = request->working_directory.count < sizeof(data->working_directory) - 1
            ? request->working_directory.count
            : sizeof(data->working_directory) - 1;
        memcpy(data->working_directory, request->working_directory.data, n);
        data->working_directory[n] = '\0';
    }

    out_result->exit_code = 0;
    out_result->stdout_text = nob_sv_from_cstr("Configuring done\nGenerating done\n");
    return true;
}

static bool ctest_coverage_mock_process_run(void *user_data,
                                            Arena *scratch_arena,
                                            const Eval_Process_Run_Request *request,
                                            Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Coverage_Mock_Process_Data *data = (Ctest_Coverage_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("0");
    data->call_count++;

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("coverage-tool"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->saw_coverage_tool = true;
    data->args_in_expected_order =
        request->argc == 5 &&
        nob_sv_eq(request->argv[1], nob_sv_from_cstr("--fast")) &&
        nob_sv_eq(request->argv[2], nob_sv_from_cstr("--xml")) &&
        nob_sv_eq(request->argv[3], nob_sv_from_cstr("--mode")) &&
        nob_sv_eq(request->argv[4], nob_sv_from_cstr("scan"));
    for (size_t i = 1; i < request->argc; i++) {
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--fast"))) data->saw_fast_flag = true;
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--xml"))) data->saw_xml_flag = true;
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--mode"))) data->saw_mode_flag = true;
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("scan"))) data->saw_mode_value = true;
    }

    if (request->working_directory.count > 0) {
        size_t n = request->working_directory.count < sizeof(data->working_directory) - 1
            ? request->working_directory.count
            : sizeof(data->working_directory) - 1;
        memcpy(data->working_directory, request->working_directory.data, n);
        data->working_directory[n] = '\0';
    }

    out_result->exit_code = 0;
    out_result->stdout_text = nob_sv_from_cstr("Covered: 42\n");
    return true;
}

static bool ctest_build_mock_process_run(void *user_data,
                                         Arena *scratch_arena,
                                         const Eval_Process_Run_Request *request,
                                         Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Build_Mock_Process_Data *data = (Ctest_Build_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("1");
    data->call_count++;

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("build-tool"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->saw_build_tool = true;
    for (size_t i = 1; i < request->argc; i++) {
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--")) ) data->saw_build_flag = true;
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--target")) && i + 1 < request->argc) {
            data->saw_target_flag = true;
            if (nob_sv_eq(request->argv[i + 1], nob_sv_from_cstr("demo"))) data->saw_target_value = true;
            i++;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--config")) && i + 1 < request->argc) {
            data->saw_config_flag = true;
            if (nob_sv_eq(request->argv[i + 1], nob_sv_from_cstr("Debug"))) data->saw_config_value = true;
            i++;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--parallel")) && i + 1 < request->argc) {
            data->saw_parallel_flag = true;
            if (nob_sv_eq(request->argv[i + 1], nob_sv_from_cstr("5"))) data->saw_parallel_value = true;
            i++;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--keep-going"))) data->saw_build_flag = true;
    }

    if (request->working_directory.count > 0) {
        size_t n = request->working_directory.count < sizeof(data->working_directory) - 1
            ? request->working_directory.count
            : sizeof(data->working_directory) - 1;
        memcpy(data->working_directory, request->working_directory.data, n);
        data->working_directory[n] = '\0';
    }

    out_result->exit_code = 1;
    out_result->stdout_text = nob_sv_from_cstr("warning: deprecated API used\n");
    out_result->stderr_text = nob_sv_from_cstr("error: failed to compile demo.c\n");
    return true;
}

static bool ctest_test_mock_process_run(void *user_data,
                                        Arena *scratch_arena,
                                        const Eval_Process_Run_Request *request,
                                        Eval_Process_Run_Result *out_result) {
    (void)scratch_arena;
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Test_Mock_Process_Data *data = (Ctest_Test_Mock_Process_Data*)user_data;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    data->call_count++;

    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("test-pass"))) {
        data->saw_test_pass_tool = true;
        if (request->argc > 1 && nob_sv_eq(request->argv[1], nob_sv_from_cstr("--alpha"))) {
            data->saw_pass_arg = true;
        }
        if (request->working_directory.count > 0) {
            size_t n = request->working_directory.count < sizeof(data->default_working_directory) - 1
                ? request->working_directory.count
                : sizeof(data->default_working_directory) - 1;
            memcpy(data->default_working_directory, request->working_directory.data, n);
            data->default_working_directory[n] = '\0';
        }
        out_result->exit_code = 0;
        out_result->result_text = nob_sv_from_cstr("0");
        out_result->stdout_text = nob_sv_from_cstr("pass output\n");
        return true;
    }

    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("test-fail"))) {
        data->saw_test_fail_tool = true;
        if (request->argc > 1 && nob_sv_eq(request->argv[1], nob_sv_from_cstr("--beta"))) {
            data->saw_fail_arg = true;
        }
        if (request->working_directory.count > 0) {
            size_t n = request->working_directory.count < sizeof(data->custom_working_directory) - 1
                ? request->working_directory.count
                : sizeof(data->custom_working_directory) - 1;
            memcpy(data->custom_working_directory, request->working_directory.data, n);
            data->custom_working_directory[n] = '\0';
        }
        out_result->exit_code = 1;
        out_result->result_text = nob_sv_from_cstr("1");
        out_result->stderr_text = nob_sv_from_cstr("fail output\n");
        return true;
    }

    out_result->exit_code = 127;
    out_result->result_text = nob_sv_from_cstr("127");
    out_result->stderr_text = nob_sv_from_cstr("unexpected process");
    return true;
}

static bool ctest_memcheck_mock_process_run(void *user_data,
                                            Arena *scratch_arena,
                                            const Eval_Process_Run_Request *request,
                                            Eval_Process_Run_Result *out_result) {
    if (!user_data || !request || !out_result || request->argc == 0) return false;

    Ctest_Memcheck_Mock_Process_Data *data = (Ctest_Memcheck_Mock_Process_Data*)user_data;
    if (nob_sv_eq(request->argv[0], nob_sv_from_cstr("curl"))) {
        return ctest_submit_mock_process_run(&data->submit, scratch_arena, request, out_result);
    }

    (void)scratch_arena;
    memset(out_result, 0, sizeof(*out_result));
    out_result->started = true;
    out_result->result_text = nob_sv_from_cstr("0");

    if (!nob_sv_eq(request->argv[0], nob_sv_from_cstr("memcheck-tool"))) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("unexpected process");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    data->memcheck_call_count++;
    data->saw_memcheck_tool = true;

    size_t separator_index = request->argc;
    for (size_t i = 1; i < request->argc; i++) {
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--xml=yes"))) {
            data->saw_backend_xml_flag = true;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--trace-children=yes"))) {
            data->saw_backend_trace_children_flag = true;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--keep-debuginfo=yes"))) {
            data->saw_backend_sanitizer_flag = true;
            continue;
        }
        if (ctest_submit_mock_has_prefix(request->argv[i], "--suppressions=")) {
            data->saw_suppressions_flag = true;
            continue;
        }
        if (nob_sv_eq(request->argv[i], nob_sv_from_cstr("--"))) {
            data->saw_separator = true;
            separator_index = i;
            break;
        }
    }

    if (separator_index + 1 >= request->argc) {
        out_result->exit_code = 127;
        out_result->stderr_text = nob_sv_from_cstr("missing test tool");
        out_result->result_text = nob_sv_from_cstr("127");
        return true;
    }

    if (request->working_directory.count > 0) {
        char *dest = NULL;
        size_t dest_size = 0;
        if (nob_sv_eq(request->argv[separator_index + 1], nob_sv_from_cstr("memcheck-pass"))) {
            dest = data->default_working_directory;
            dest_size = sizeof(data->default_working_directory);
        } else if (nob_sv_eq(request->argv[separator_index + 1], nob_sv_from_cstr("memcheck-fail"))) {
            dest = data->custom_working_directory;
            dest_size = sizeof(data->custom_working_directory);
        }
        if (dest) {
            size_t n = request->working_directory.count < dest_size - 1
                ? request->working_directory.count
                : dest_size - 1;
            memcpy(dest, request->working_directory.data, n);
            dest[n] = '\0';
        }
    }

    if (nob_sv_eq(request->argv[separator_index + 1], nob_sv_from_cstr("memcheck-pass"))) {
        data->saw_test_pass_tool = true;
        if (separator_index + 2 < request->argc &&
            nob_sv_eq(request->argv[separator_index + 2], nob_sv_from_cstr("--alpha"))) {
            data->saw_pass_arg = true;
        }
        out_result->exit_code = 0;
        out_result->stdout_text = nob_sv_from_cstr("memcheck clean\n");
        return true;
    }

    if (nob_sv_eq(request->argv[separator_index + 1], nob_sv_from_cstr("memcheck-fail"))) {
        data->saw_test_fail_tool = true;
        if (separator_index + 2 < request->argc &&
            nob_sv_eq(request->argv[separator_index + 2], nob_sv_from_cstr("--beta"))) {
            data->saw_fail_arg = true;
        }
        out_result->exit_code = 0;
        out_result->stderr_text =
            nob_sv_from_cstr("ERROR SUMMARY: 2 errors from 2 contexts (suppressed: 0 from 0)\n");
        return true;
    }

    out_result->exit_code = 127;
    out_result->stderr_text = nob_sv_from_cstr("unexpected test tool");
    out_result->result_text = nob_sv_from_cstr("127");
    return true;
}

TEST(evaluator_load_cache_supports_documented_legacy_and_prefix_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_legacy_plain"));
    ASSERT(nob_mkdir_if_not_exists("cache_legacy_filtered"));
    ASSERT(nob_mkdir_if_not_exists("cache_prefix_empty"));
    ASSERT(nob_write_entire_file("cache_legacy_plain/CMakeCache.txt",
                                 "FIRST:STRING=one\n"
                                 "HIDE_PLAIN:INTERNAL=hidden-plain\n",
                                 strlen("FIRST:STRING=one\n"
                                        "HIDE_PLAIN:INTERNAL=hidden-plain\n")));
    ASSERT(nob_write_entire_file("cache_legacy_filtered/CMakeCache.txt",
                                 "KEEP:STRING=keep\n"
                                 "DROP:STRING=drop-me\n"
                                 "HIDE_FILTER:INTERNAL=secret-filter\n",
                                 strlen("KEEP:STRING=keep\n"
                                        "DROP:STRING=drop-me\n"
                                        "HIDE_FILTER:INTERNAL=secret-filter\n")));
    ASSERT(nob_write_entire_file("cache_prefix_empty/CMakeCache.txt",
                                 "EMPTY:STRING=\n"
                                 "KEEP:STRING=keep-prefix\n",
                                 strlen("EMPTY:STRING=\n"
                                        "KEEP:STRING=keep-prefix\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(PFX_EMPTY sentinel)\n"
        "load_cache(cache_legacy_plain)\n"
        "load_cache(cache_legacy_filtered INCLUDE_INTERNALS HIDE_FILTER EXCLUDE DROP)\n"
        "load_cache(cache_prefix_empty READ_WITH_PREFIX PFX_ EMPTY KEEP)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FIRST")), nob_sv_from_cstr("one")));
    ASSERT(eval_test_var_defined(ctx, nob_sv_from_cstr("FIRST")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("HIDE_PLAIN")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("KEEP")), nob_sv_from_cstr("keep")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HIDE_FILTER")), nob_sv_from_cstr("secret-filter")));
    ASSERT(eval_test_var_defined(ctx, nob_sv_from_cstr("KEEP")));
    ASSERT(eval_test_var_defined(ctx, nob_sv_from_cstr("HIDE_FILTER")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("DROP")));

    ASSERT(!eval_test_var_defined(ctx, nob_sv_from_cstr("PFX_EMPTY")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("PFX_KEEP")),
                     nob_sv_from_cstr("keep-prefix")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("PFX_KEEP")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_load_cache_allows_read_with_prefix_but_rejects_legacy_form_in_script_mode) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_script_only"));
    ASSERT(nob_write_entire_file("cache_script_only/CMakeCache.txt",
                                 "FOO:STRING=from-cache\n"
                                 "BAR:INTERNAL=hidden\n",
                                 strlen("FOO:STRING=from-cache\n"
                                        "BAR:INTERNAL=hidden\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "script.cmake";
    init.exec_mode = EVAL_EXEC_MODE_SCRIPT;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "load_cache(cache_script_only READ_WITH_PREFIX PFX_ FOO)\n"
        "load_cache(cache_script_only INCLUDE_INTERNALS BAR)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    size_t legacy_form_errors = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("load_cache() legacy form is available only in CMake projects"))) {
            legacy_form_errors++;
        }
    }
    ASSERT(legacy_form_errors == 1);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("PFX_FOO")),
                     nob_sv_from_cstr("from-cache")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("FOO")));
    ASSERT(!eval_test_cache_defined(ctx, nob_sv_from_cstr("BAR")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("modules"));
    ASSERT(nob_mkdir_if_not_exists("include"));
    ASSERT(nob_write_entire_file("meta_impl.cpp", "int meta_impl = 0;\n", strlen("int meta_impl = 0;\n")));
    ASSERT(nob_write_entire_file("modules/core.cppm", "export module core;\n", strlen("export module core;\n")));
    ASSERT(nob_write_entire_file("include/meta.hpp", "#pragma once\n", strlen("#pragma once\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(meta_lib STATIC meta_impl.cpp)\n"
        "target_sources(meta_lib PUBLIC FILE_SET mods TYPE CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_include_directories(meta_lib PUBLIC include)\n"
        "target_compile_definitions(meta_lib PUBLIC META_DEF=1)\n"
        "target_compile_options(meta_lib PUBLIC -Wall)\n"
        "target_compile_features(meta_lib PUBLIC cxx_std_20)\n"
        "target_link_libraries(meta_lib PUBLIC dep::lib)\n"
        "set_target_properties(meta_lib PROPERTIES EXPORT_NAME meta-export-name CXX_EXTENSIONS OFF)\n"
        "install(TARGETS meta_lib EXPORT DemoExport FILE_SET mods DESTINATION include/modules)\n"
        "export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo:: CXX_MODULES_DIRECTORY cxx-modules)\n"
        "export(EXPORT DemoExport NAMESPACE Demo:: CXX_MODULES_DIRECTORY export-modules)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_DIRECTORY")),
                     nob_sv_from_cstr("export-modules")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_NAME")),
                     nob_sv_from_cstr("DemoExport")));

    String_View export_targets = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "meta-targets.cmake", &export_targets));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULES_DIRECTORY \"cxx-modules\")")));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules/cxx-modules-cc9f26f1f4e6.cmake\")")));

    String_View targets_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6.cmake",
                                             &targets_trampoline));
    ASSERT(sv_contains_sv(targets_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-cc9f26f1f4e6-noconfig.cmake\")")));

    String_View targets_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6-noconfig.cmake",
                                             &targets_config));
    ASSERT(sv_contains_sv(targets_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    String_View target_modules = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/target-meta-export-name-noconfig.cmake",
                                             &target_modules));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_TARGET \"Demo::meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_EXPORT_NAME \"meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SETS \"mods\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SET_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_DIRS_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_INCLUDE_DIRECTORIES \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_DEFINITIONS \"META_DEF=1\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_OPTIONS \"-Wall\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_FEATURES \"cxx_std_20\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_LINK_LIBRARIES \"dep::lib\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_CXX_EXTENSIONS \"OFF\")")));

    String_View export_default = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "DemoExport.cmake", &export_default));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_NAME \"DemoExport\")")));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/export-modules/cxx-modules-DemoExport.cmake\")")));

    String_View export_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport.cmake",
                                             &export_trampoline));
    ASSERT(sv_contains_sv(export_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-DemoExport-noconfig.cmake\")")));

    String_View export_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport-noconfig.cmake",
                                             &export_config));
    ASSERT(sv_contains_sv(export_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_rejects_invalid_extension_and_alias_targets) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real INTERFACE)\n"
        "add_library(alias_real ALIAS real)\n"
        "export(TARGETS real FILE bad-export.txt)\n"
        "export(TARGETS alias_real FILE alias-export.cmake)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_bad_extension = false;
    bool saw_alias_reject = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(... FILE ...) requires a filename ending in .cmake"))) {
            saw_bad_extension = true;
        }
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(TARGETS ...) may not export ALIAS targets"))) {
            saw_alias_reject = true;
        }
    }
    ASSERT(saw_bad_extension);
    ASSERT(saw_alias_reject);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_models_metadata_and_safe_local_effects) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Ctest_Configure_Mock_Process_Data configure_mock = {0};
    EvalServices services = {
        .user_data = &configure_mock,
        .process_run_capture = ctest_configure_mock_process_run,
    };

    ASSERT(nob_mkdir_if_not_exists("ctest_bin"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe/sub"));
    ASSERT(nob_mkdir_if_not_exists("ctest_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_custom"));
    ASSERT(nob_write_entire_file("ctest_bin/a.txt", "A\n", strlen("A\n")));
    ASSERT(nob_write_entire_file("ctest_bin/b.txt", "B\n", strlen("B\n")));
    ASSERT(nob_write_entire_file("ctest_bin/notes.txt", "NOTES\n", strlen("NOTES\n")));
    ASSERT(nob_write_entire_file("ctest_bin/wipe/sub/junk.txt", "junk\n", strlen("junk\n")));
    ASSERT(nob_write_entire_file("ctest_custom/CTestCustom.cmake",
                                 "set(CTEST_CUSTOM_LOADED yes)\n",
                                 strlen("set(CTEST_CUSTOM_LOADED yes)\n")));
    ASSERT(nob_write_entire_file("ctest_custom/CTestCustom.ctest",
                                 "set(CTEST_CUSTOM_LEGACY yes)\n",
                                 strlen("set(CTEST_CUSTOM_LEGACY yes)\n")));
    ASSERT(nob_write_entire_file("ctest_script.cmake",
                                 "set(CTEST_SCRIPT_LOADED 1)\n",
                                 strlen("set(CTEST_SCRIPT_LOADED 1)\n")));
    ASSERT(nob_write_entire_file("ctest_script_child.cmake",
                                 "set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                 "function(ctest_child_only_fn)\n"
                                 "endfunction()\n"
                                 "set(CTEST_TRACK ChildTrack)\n",
                                 strlen("set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                        "function(ctest_child_only_fn)\n"
                                        "endfunction()\n"
                                        "set(CTEST_TRACK ChildTrack)\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_bin)\n"
        "set(CMAKE_COMMAND configure-tool)\n"
        "set(CTEST_BUILD_COMMAND build-tool)\n"
        "ctest_start(Experimental ctest_src . GROUP Nightly QUIET APPEND)\n"
        "ctest_configure(RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE QUIET)\n"
        "ctest_build(TARGET all NUMBER_ERRORS BUILD_ERRS NUMBER_WARNINGS BUILD_WARNS RETURN_VALUE BUILD_RV CAPTURE_CMAKE_ERROR BUILD_CE APPEND)\n"
        "ctest_test(RETURN_VALUE TEST_RV CAPTURE_CMAKE_ERROR TEST_CE PARALLEL_LEVEL 2 SCHEDULE_RANDOM)\n"
        "ctest_coverage(LABELS core ui RETURN_VALUE COV_RV CAPTURE_CMAKE_ERROR COV_CE)\n"
        "ctest_memcheck(RETURN_VALUE MEM_RV CAPTURE_CMAKE_ERROR MEM_CE DEFECT_COUNT MEM_DEFECTS SCHEDULE_RANDOM ON)\n"
        "ctest_update(RETURN_VALUE UPD_RV CAPTURE_CMAKE_ERROR UPD_CE QUIET)\n"
        "ctest_submit(PARTS Start Build Test FILES notes.txt RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n"
        "ctest_upload(FILES a.txt b.txt CAPTURE_CMAKE_ERROR UPLOAD_CE)\n"
        "ctest_empty_binary_directory(wipe)\n"
        "ctest_empty_binary_directory(fresh)\n"
        "ctest_read_custom_files(ctest_custom)\n"
        "ctest_run_script(ctest_script.cmake RETURN_VALUE SCRIPT_RV)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_child.cmake RETURN_VALUE SCRIPT_CHILD_RV)\n"
        "if(DEFINED CTEST_SCRIPT_CHILD_ONLY)\n"
        "  set(CHILD_VAR_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_VAR_LEAK 0)\n"
        "endif()\n"
        "if(COMMAND ctest_child_only_fn)\n"
        "  set(CHILD_FN_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_FN_LEAK 0)\n"
        "endif()\n"
        "ctest_sleep(0.25)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(configure_mock.call_count == 2);
    ASSERT(configure_mock.saw_configure_tool);
    ASSERT(configure_mock.build_call_count == 1);
    ASSERT(configure_mock.saw_build_tool);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_LAST_COMMAND")),
                     nob_sv_from_cstr("ctest_sleep")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::GROUP")),
                     nob_sv_from_cstr("Nightly")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TRACK")),
                     nob_sv_from_cstr("Nightly")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::GROUP")),
                     nob_sv_from_cstr("Nightly")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_TRACK")),
                     nob_sv_from_cstr("Nightly")));
    String_View ctest_tag = eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_TAG"));
    ASSERT(ctest_tag.count > 0);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::PARTS")),
                     nob_sv_from_cstr("Start;Build;Test")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_empty_binary_directory::STATUS")),
                     nob_sv_from_cstr("CLEARED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_ERRS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_WARNS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("MEM_DEFECTS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPD_RV")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPD_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::SCHEDULE_RANDOM")),
                     nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::STATUS")),
                     nob_sv_from_cstr("MEMCHECKED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(eval_test_ctest_step_count(ctx) >= 8);
    String_View step_status = {0};
    String_View submit_part = {0};
    ASSERT(eval_test_ctest_step_find(ctx, nob_sv_from_cstr("ctest_start"), &step_status, &submit_part));
    ASSERT(nob_sv_eq(step_status, nob_sv_from_cstr("STAGED")));
    ASSERT(nob_sv_eq(submit_part, nob_sv_from_cstr("Start")));
    ASSERT(eval_test_ctest_step_find(ctx, nob_sv_from_cstr("ctest_build"), &step_status, &submit_part));
    ASSERT(nob_sv_eq(step_status, nob_sv_from_cstr("BUILT")));
    ASSERT(nob_sv_eq(submit_part, nob_sv_from_cstr("Build")));
    ASSERT(eval_test_ctest_step_find(ctx, nob_sv_from_cstr("ctest_upload"), &step_status, &submit_part));
    ASSERT(nob_sv_eq(step_status, nob_sv_from_cstr("STAGED")));
    ASSERT(nob_sv_eq(submit_part, nob_sv_from_cstr("Upload")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SCRIPT_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SCRIPT_CHILD_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CHILD_VAR_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CHILD_FN_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_CUSTOM_LOADED")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_CUSTOM_LEGACY")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_LOADED")), nob_sv_from_cstr("1")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_CHILD_ONLY")).count == 0);
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::TAG_FILE")),
                          nob_sv_from_cstr("ctest_bin/Testing/TAG")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::TESTING_DIR")),
                          nob_sv_from_cstr("ctest_bin/Testing")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::TAG_DIR")),
                          ctest_tag));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::TAG_FILE")),
                          nob_sv_from_cstr("ctest_bin/Testing/TAG")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::TAG")), ctest_tag));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE")),
                          nob_sv_from_cstr("ctest_bin/Testing/TAG")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/notes.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/a.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/b.txt")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::EXECUTION_MODE")),
                     nob_sv_from_cstr("NEW_PROCESS")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::RESOLVED_SCRIPTS")),
                          nob_sv_from_cstr("ctest_script_child.cmake")));

    String_View tag_file = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE"));
    char *tag_file_c = arena_strndup(temp_arena, tag_file.data, tag_file.count);
    ASSERT(tag_file_c != NULL);
    ASSERT(nob_file_exists(tag_file_c));

    Nob_String_Builder tag_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(tag_file_c, &tag_sb));
    ASSERT(strstr(tag_sb.items, "Experimental") != NULL);
    ASSERT(strstr(tag_sb.items, "Nightly") != NULL);
    ASSERT(strstr(tag_sb.items, tag_file_c) == NULL);
    ASSERT(memmem(tag_sb.items, tag_sb.count, ctest_tag.data, ctest_tag.count) != NULL);
    nob_sb_free(tag_sb);

    String_View submit_manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::MANIFEST"));
    char *submit_manifest_c = arena_strndup(temp_arena, submit_manifest.data, submit_manifest.count);
    ASSERT(submit_manifest_c != NULL);
    ASSERT(nob_file_exists(submit_manifest_c));

    Nob_String_Builder submit_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(submit_manifest_c, &submit_sb));
    ASSERT(strstr(submit_sb.items, "COMMAND=ctest_submit") != NULL);
    ASSERT(strstr(submit_sb.items, "PARTS=Start;Build;Test") != NULL);
    ASSERT(strstr(submit_sb.items, "FILES=") != NULL);
    ASSERT(strstr(submit_sb.items, "notes.txt") != NULL);
    nob_sb_free(submit_sb);

    String_View upload_manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::MANIFEST"));
    char *upload_manifest_c = arena_strndup(temp_arena, upload_manifest.data, upload_manifest.count);
    ASSERT(upload_manifest_c != NULL);
    ASSERT(nob_file_exists(upload_manifest_c));

    Nob_String_Builder upload_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(upload_manifest_c, &upload_sb));
    ASSERT(strstr(upload_sb.items, "COMMAND=ctest_upload") != NULL);
    ASSERT(strstr(upload_sb.items, "a.txt") != NULL);
    ASSERT(strstr(upload_sb.items, "b.txt") != NULL);
    nob_sb_free(upload_sb);

    ASSERT(!nob_file_exists("ctest_bin/wipe/sub/junk.txt"));
    ASSERT(nob_file_exists("ctest_bin/wipe"));
    ASSERT(nob_file_exists("ctest_bin/fresh"));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_start_models_documented_group_append_and_checkout_flow) {
    ASSERT(nob_mkdir_if_not_exists("ctest_start_checkout_parent"));
    ASSERT(nob_mkdir_if_not_exists("ctest_start_checkout_bin"));

    char first_tag_buf[256] = {0};

    {
        Arena *temp_arena = arena_create(2 * 1024 * 1024);
        Arena *event_arena = arena_create(2 * 1024 * 1024);
        ASSERT(temp_arena && event_arena);

        Cmake_Event_Stream *stream = event_stream_create(event_arena);
        ASSERT(stream != NULL);

        Ctest_Start_Mock_Process_Data mock = {0};
        EvalServices services = {
            .user_data = &mock,
            .process_run_capture = ctest_start_mock_process_run,
        };

        Eval_Test_Init init = {0};
        init.arena = temp_arena;
        init.event_arena = event_arena;
        init.stream = stream;
        init.source_dir = nob_sv_from_cstr(".");
        init.binary_dir = nob_sv_from_cstr(".");
        init.current_file = "CMakeLists.txt";
        init.services = &services;

        Eval_Test_Runtime *ctx = eval_test_create(&init);
        ASSERT(ctx != NULL);

        Ast_Root root = parse_cmake(
            temp_arena,
            "set(CMAKE_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CMAKE_CURRENT_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CTEST_SOURCE_DIRECTORY ctest_start_checkout_parent/source_tree)\n"
            "set(CTEST_BINARY_DIRECTORY .)\n"
            "set(CTEST_CHECKOUT_COMMAND [=[checkout-tool --flag \"value with space\"]=])\n"
            "ctest_start(Experimental GROUP GroupExperimental QUIET)\n");
        ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

        const Eval_Run_Report *report = eval_test_report(ctx);
        ASSERT(report != NULL);
        ASSERT(report->error_count == 0);
        ASSERT(report->warning_count == 0);

        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::MODEL")),
                         nob_sv_from_cstr("Experimental")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::GROUP")),
                         nob_sv_from_cstr("GroupExperimental")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TRACK")),
                         nob_sv_from_cstr("GroupExperimental")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::GROUP")),
                         nob_sv_from_cstr("GroupExperimental")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::QUIET")),
                         nob_sv_from_cstr("1")));
        ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::CHECKOUT_COMMAND")),
                              nob_sv_from_cstr("checkout-tool --flag")));
        ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::CHECKOUT_WORKING_DIRECTORY")),
                              nob_sv_from_cstr("ctest_start_checkout_parent")));

        ASSERT(mock.call_count == 1);
        ASSERT(mock.saw_checkout_tool);
        ASSERT(mock.saw_flag);
        ASSERT(mock.saw_value_with_space);
        ASSERT(strstr(mock.working_directory, "ctest_start_checkout_parent") != NULL);

        const char *tag_file_c = "ctest_start_checkout_bin/Testing/TAG";
        ASSERT(nob_file_exists(tag_file_c));

        Nob_String_Builder tag_sb = {0};
        ASSERT(evaluator_read_entire_file_cstr(tag_file_c, &tag_sb));
        ASSERT(strstr(tag_sb.items, "Experimental") != NULL);
        ASSERT(strstr(tag_sb.items, "GroupExperimental") != NULL);
        size_t first_tag_len = 0;
        while (first_tag_len < tag_sb.count &&
               tag_sb.items[first_tag_len] != '\n' &&
               tag_sb.items[first_tag_len] != '\r') {
            first_tag_len++;
        }
        ASSERT(first_tag_len > 0 && first_tag_len < sizeof(first_tag_buf));
        memcpy(first_tag_buf, tag_sb.items, first_tag_len);
        first_tag_buf[first_tag_len] = '\0';
        nob_sb_free(tag_sb);

        eval_test_destroy(ctx);
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
    }

    {
        Arena *temp_arena = arena_create(2 * 1024 * 1024);
        Arena *event_arena = arena_create(2 * 1024 * 1024);
        ASSERT(temp_arena && event_arena);

        Cmake_Event_Stream *stream = event_stream_create(event_arena);
        ASSERT(stream != NULL);

        Eval_Test_Init init = {0};
        init.arena = temp_arena;
        init.event_arena = event_arena;
        init.stream = stream;
        init.source_dir = nob_sv_from_cstr(".");
        init.binary_dir = nob_sv_from_cstr(".");
        init.current_file = "CMakeLists.txt";

        Eval_Test_Runtime *ctx = eval_test_create(&init);
        ASSERT(ctx != NULL);

        Ast_Root root = parse_cmake(
            temp_arena,
            "set(CMAKE_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CMAKE_CURRENT_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CTEST_SOURCE_DIRECTORY ctest_start_checkout_parent/source_tree)\n"
            "set(CTEST_BINARY_DIRECTORY .)\n"
            "ctest_start(APPEND)\n");
        ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

        const Eval_Run_Report *report = eval_test_report(ctx);
        ASSERT(report != NULL);
        ASSERT(report->error_count == 0);
        ASSERT(report->warning_count == 0);

        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG")),
                         nob_sv_from_cstr(first_tag_buf)));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::MODEL")),
                         nob_sv_from_cstr("Experimental")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::GROUP")),
                         nob_sv_from_cstr("GroupExperimental")));

        eval_test_destroy(ctx);
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
    }

    {
        Arena *temp_arena = arena_create(2 * 1024 * 1024);
        Arena *event_arena = arena_create(2 * 1024 * 1024);
        ASSERT(temp_arena && event_arena);

        Cmake_Event_Stream *stream = event_stream_create(event_arena);
        ASSERT(stream != NULL);

        Eval_Test_Init init = {0};
        init.arena = temp_arena;
        init.event_arena = event_arena;
        init.stream = stream;
        init.source_dir = nob_sv_from_cstr(".");
        init.binary_dir = nob_sv_from_cstr(".");
        init.current_file = "CMakeLists.txt";

        Eval_Test_Runtime *ctx = eval_test_create(&init);
        ASSERT(ctx != NULL);

        Ast_Root root = parse_cmake(
            temp_arena,
            "set(CMAKE_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CMAKE_CURRENT_BINARY_DIR ctest_start_checkout_bin)\n"
            "set(CTEST_SOURCE_DIRECTORY ctest_start_checkout_parent/source_tree)\n"
            "set(CTEST_BINARY_DIRECTORY .)\n"
            "ctest_start(APPEND Continuous GROUP OverrideGroup)\n");
        ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

        const Eval_Run_Report *report = eval_test_report(ctx);
        ASSERT(report != NULL);
        ASSERT(report->error_count == 0);
        ASSERT(report->warning_count == 1);

        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG")),
                         nob_sv_from_cstr(first_tag_buf)));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::MODEL")),
                         nob_sv_from_cstr("Continuous")));
        ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::GROUP")),
                         nob_sv_from_cstr("OverrideGroup")));

        bool saw_append_warning = false;
        for (size_t i = 0; i < stream->count; i++) {
            const Cmake_Event *ev = &stream->items[i];
            if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_WARNING) continue;
            if (nob_sv_eq(ev->as.diag.cause,
                          nob_sv_from_cstr("ctest_start(APPEND) overriding model/group from existing TAG file"))) {
                saw_append_warning = true;
                break;
            }
        }
        ASSERT(saw_append_warning);

        eval_test_destroy(ctx);
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
    }

    TEST_PASS();
}

TEST(evaluator_ctest_configure_executes_documented_command_and_stages_submit_part) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_configure_exec_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_configure_exec_bin"));

    Ctest_Configure_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_configure_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_configure_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_configure_exec_bin)\n"
        "set(CMAKE_COMMAND configure-tool)\n"
        "set(CTEST_LABELS_FOR_SUBPROJECTS \"core;ui\")\n"
        "ctest_start(Experimental ctest_configure_exec_src .)\n"
        "ctest_configure(OPTIONS \"--preset;dev\" RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE APPEND QUIET)\n"
        "unset(NOBIFY_CTEST::ctest_configure::CONFIGURE_XML)\n"
        "ctest_submit(PARTS Configure RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_configure_tool);
    ASSERT(mock.saw_source_arg);
    ASSERT(mock.saw_preset_flag);
    ASSERT(mock.saw_preset_value);
    ASSERT(strstr(mock.working_directory, "ctest_configure_exec_bin") != NULL);
    ASSERT(strstr(mock.source_arg, "ctest_configure_exec_src") != NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::STATUS")),
                     nob_sv_from_cstr("CONFIGURED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::LABELS_FOR_SUBPROJECTS")),
                     nob_sv_from_cstr("core;ui")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                          nob_sv_from_cstr("Configure")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("Configure.xml")));

    String_View configure_xml = {0};
    ASSERT(eval_test_canonical_artifact_find(ctx,
                                             nob_sv_from_cstr("ctest_configure"),
                                             nob_sv_from_cstr("CONFIGURE_XML"),
                                             &configure_xml));
    char *configure_xml_c = arena_strndup(temp_arena, configure_xml.data, configure_xml.count);
    ASSERT(configure_xml_c != NULL);
    ASSERT(nob_file_exists(configure_xml_c));

    Nob_String_Builder configure_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(configure_xml_c, &configure_sb));
    ASSERT(strstr(configure_sb.items, "<Configure>") != NULL);
    ASSERT(strstr(configure_sb.items, "configure-tool") != NULL);
    ASSERT(strstr(configure_sb.items, "--preset;dev") != NULL);
    ASSERT(strstr(configure_sb.items, "<Append>true</Append>") != NULL);
    ASSERT(strstr(configure_sb.items, "<Labels>") != NULL);
    ASSERT(strstr(configure_sb.items, "<Label>core</Label>") != NULL);
    ASSERT(strstr(configure_sb.items, "<Label>ui</Label>") != NULL);
    nob_sb_free(configure_sb);

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder manifest_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &manifest_sb));
    ASSERT(strstr(manifest_sb.items, "COMMAND=ctest_configure") != NULL);
    ASSERT(strstr(manifest_sb.items, "PARTS=Configure") != NULL);
    ASSERT(strstr(manifest_sb.items, "Configure.xml") != NULL);
    nob_sb_free(manifest_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_configure_captures_missing_command_without_fatal_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_configure_missing_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_configure_missing_bin"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_configure_missing_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_configure_missing_bin)\n"
        "set(CMAKE_COMMAND \"\")\n"
        "ctest_start(Experimental ctest_configure_missing_src .)\n"
        "ctest_configure(RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::CONFIGURE_XML")).count == 0);
    String_View step_status = {0};
    String_View submit_part = {0};
    ASSERT(eval_test_ctest_step_find(ctx, nob_sv_from_cstr("ctest_configure"), &step_status, &submit_part));
    ASSERT(nob_sv_eq(step_status, nob_sv_from_cstr("FAILED")));
    ASSERT(nob_sv_eq(submit_part, nob_sv_from_cstr("Configure")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_configure_uses_documented_ctest_directory_defaults_without_start) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("src"));
    ASSERT(nob_mkdir_if_not_exists("build"));
    ASSERT(nob_mkdir_if_not_exists("src/ctest_configure_defaults_src"));
    ASSERT(nob_mkdir_if_not_exists("build/ctest_configure_defaults_bin"));
    ASSERT(nob_mkdir_if_not_exists("src/ctest_configure_wrong_src"));
    ASSERT(nob_mkdir_if_not_exists("build/ctest_configure_wrong_bin"));
    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char source_root[_TINYDIR_PATH_MAX] = {0};
    char binary_root[_TINYDIR_PATH_MAX] = {0};
    ASSERT(snprintf(source_root, sizeof(source_root), "%s/src", cwd) > 0);
    ASSERT(snprintf(binary_root, sizeof(binary_root), "%s/build", cwd) > 0);

    Ctest_Configure_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_configure_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_root);
    init.binary_dir = nob_sv_from_cstr(binary_root);
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_SOURCE_DIR ctest_configure_wrong_src)\n"
        "set(CMAKE_BINARY_DIR ctest_configure_wrong_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR .)\n"
        "set(CTEST_SOURCE_DIRECTORY ctest_configure_defaults_src)\n"
        "set(CTEST_BINARY_DIRECTORY ctest_configure_defaults_bin)\n"
        "set(CMAKE_COMMAND configure-tool)\n"
        "ctest_configure(RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE QUIET)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_configure_tool);
    ASSERT(strstr(mock.working_directory, "ctest_configure_defaults_bin") != NULL);
    ASSERT(strstr(mock.source_arg, "ctest_configure_defaults_src") != NULL);
    ASSERT(strstr(mock.working_directory, "ctest_configure_wrong_bin") == NULL);
    ASSERT(strstr(mock.source_arg, "ctest_configure_wrong_src") == NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("0")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_configure_defaults_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_configure_defaults_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY")),
                          nob_sv_from_cstr("ctest_configure_defaults_src")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY")),
                          nob_sv_from_cstr("ctest_configure_defaults_bin")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_build_executes_documented_command_and_stages_submit_part) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_build_exec_bin"));

    Ctest_Build_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_build_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_build_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_build_exec_bin)\n"
        "set(CMAKE_COMMAND build-tool)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_build(CONFIGURATION Debug PARALLEL_LEVEL 5 FLAGS --keep-going TARGET demo NUMBER_ERRORS BUILD_ERRS NUMBER_WARNINGS BUILD_WARNS RETURN_VALUE BUILD_RV CAPTURE_CMAKE_ERROR BUILD_CE APPEND QUIET)\n"
        "ctest_submit(PARTS Build RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_build_tool);
    ASSERT(mock.saw_target_flag);
    ASSERT(mock.saw_target_value);
    ASSERT(mock.saw_config_flag);
    ASSERT(mock.saw_config_value);
    ASSERT(mock.saw_parallel_flag);
    ASSERT(mock.saw_parallel_value);
    ASSERT(mock.saw_build_flag);
    ASSERT(strstr(mock.working_directory, "ctest_build_exec_bin") != NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_ERRS")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_WARNS")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILD_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::STATUS")),
                     nob_sv_from_cstr("BUILT")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::CONFIGURATION_RESOLVED")),
                     nob_sv_from_cstr("Debug")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::PARALLEL_LEVEL_RESOLVED")),
                     nob_sv_from_cstr("5")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::FLAGS_RESOLVED")),
                     nob_sv_from_cstr("--keep-going")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::TARGET_RESOLVED")),
                     nob_sv_from_cstr("demo")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::ERROR_COUNT")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::WARNING_COUNT")),
                     nob_sv_from_cstr("1")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                          nob_sv_from_cstr("Build")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("Build.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("BuildManifest.txt")));

    String_View build_xml = {0};
    ASSERT(eval_test_canonical_artifact_find(ctx,
                                             nob_sv_from_cstr("ctest_build"),
                                             nob_sv_from_cstr("BUILD_XML"),
                                             &build_xml));
    char *build_xml_c = arena_strndup(temp_arena, build_xml.data, build_xml.count);
    ASSERT(build_xml_c != NULL);
    ASSERT(nob_file_exists(build_xml_c));

    Nob_String_Builder build_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(build_xml_c, &build_sb));
    ASSERT(strstr(build_sb.items, "<Build>") != NULL);
    ASSERT(strstr(build_sb.items, "build-tool") != NULL);
    ASSERT(strstr(build_sb.items, "Debug") != NULL);
    ASSERT(strstr(build_sb.items, "--keep-going") != NULL);
    ASSERT(strstr(build_sb.items, "<ErrorCount>1</ErrorCount>") != NULL);
    ASSERT(strstr(build_sb.items, "<WarningCount>1</WarningCount>") != NULL);
    nob_sb_free(build_sb);

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder manifest_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &manifest_sb));
    ASSERT(strstr(manifest_sb.items, "COMMAND=ctest_build") != NULL);
    ASSERT(strstr(manifest_sb.items, "PARTS=Build") != NULL);
    ASSERT(strstr(manifest_sb.items, "Build.xml") != NULL);
    nob_sb_free(manifest_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_test_executes_plan_and_stages_test_xml_without_submitting_junit) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_test_exec_bin"));
    ASSERT(nob_mkdir_if_not_exists("ctest_test_exec_bin/work"));

    Ctest_Test_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_test_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_test_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_test_exec_bin)\n"
        "enable_testing()\n"
        "add_test(NAME pass COMMAND test-pass --alpha)\n"
        "add_test(NAME fail COMMAND test-fail --beta WORKING_DIRECTORY work)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_test(PARALLEL_LEVEL 3 TEST_LOAD 4 OUTPUT_JUNIT junit.xml RETURN_VALUE TEST_RV CAPTURE_CMAKE_ERROR TEST_CE APPEND QUIET SCHEDULE_RANDOM)\n"
        "ctest_submit(PARTS Test RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 2);
    ASSERT(mock.saw_test_pass_tool);
    ASSERT(mock.saw_test_fail_tool);
    ASSERT(mock.saw_pass_arg);
    ASSERT(mock.saw_fail_arg);
    ASSERT(strstr(mock.default_working_directory, "ctest_test_exec_bin") != NULL);
    ASSERT(strstr(mock.custom_working_directory, "ctest_test_exec_bin/work") != NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::STATUS")),
                     nob_sv_from_cstr("TESTED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::PARALLEL_LEVEL_RESOLVED")),
                     nob_sv_from_cstr("3")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_TEST_LOAD")),
                     nob_sv_from_cstr("4")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::SCHEDULE_RANDOM")),
                     nob_sv_from_cstr("ON")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_OUTPUT_JUNIT")),
                          nob_sv_from_cstr("ctest_test_exec_bin/junit.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                          nob_sv_from_cstr("Test")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("Test.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("TestManifest.txt")));
    ASSERT(!sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                           nob_sv_from_cstr("junit.xml")));

    String_View test_xml = {0};
    ASSERT(eval_test_canonical_artifact_find(ctx,
                                             nob_sv_from_cstr("ctest_test"),
                                             nob_sv_from_cstr("TEST_XML"),
                                             &test_xml));
    char *test_xml_c = arena_strndup(temp_arena, test_xml.data, test_xml.count);
    ASSERT(test_xml_c != NULL);
    ASSERT(nob_file_exists(test_xml_c));

    Nob_String_Builder test_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(test_xml_c, &test_sb));
    ASSERT(strstr(test_sb.items, "<Testing>") != NULL);
    ASSERT(strstr(test_sb.items, "test-pass --alpha") != NULL);
    ASSERT(strstr(test_sb.items, "test-fail --beta") != NULL);
    ASSERT(strstr(test_sb.items, "<FailedTests>1</FailedTests>") != NULL);
    ASSERT(strstr(test_sb.items, "ctest_test_exec_bin/junit.xml") != NULL);
    nob_sb_free(test_sb);

    String_View junit = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_OUTPUT_JUNIT"));
    char *junit_c = arena_strndup(temp_arena, junit.data, junit.count);
    ASSERT(junit_c != NULL);
    ASSERT(nob_file_exists(junit_c));

    Nob_String_Builder junit_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(junit_c, &junit_sb));
    ASSERT(strstr(junit_sb.items, "<testsuite") != NULL);
    ASSERT(strstr(junit_sb.items, "name=\"pass\"") != NULL);
    ASSERT(strstr(junit_sb.items, "name=\"fail\"") != NULL);
    ASSERT(strstr(junit_sb.items, "<failure") != NULL);
    nob_sb_free(junit_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_coverage_executes_documented_command_order_and_stages_submit_part) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_coverage_exec_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_coverage_exec_src/src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_coverage_exec_bin"));
    ASSERT(nob_write_entire_file("ctest_coverage_exec_src/src/main.c",
                                 "int main(void) { return 0; }\n",
                                 strlen("int main(void) { return 0; }\n")));
    ASSERT(nob_write_entire_file("ctest_coverage_exec_src/src/net.c",
                                 "int net(void) { return 0; }\n",
                                 strlen("int net(void) { return 0; }\n")));

    Ctest_Coverage_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_coverage_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_coverage_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_coverage_exec_bin)\n"
        "set(COVERAGE_COMMAND \"coverage-tool --mode scan\")\n"
        "set(COVERAGE_EXTRA_FLAGS \"--fast;--xml\")\n"
        "set_source_files_properties(ctest_coverage_exec_src/src/main.c PROPERTIES LABELS \"core;ui\")\n"
        "set_source_files_properties(ctest_coverage_exec_src/src/net.c PROPERTIES LABELS infra)\n"
        "ctest_start(Experimental ctest_coverage_exec_src .)\n"
        "ctest_coverage(LABELS core ui RETURN_VALUE COV_RV CAPTURE_CMAKE_ERROR COV_CE APPEND QUIET)\n"
        "ctest_submit(PARTS Coverage RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_coverage_tool);
    ASSERT(mock.saw_fast_flag);
    ASSERT(mock.saw_xml_flag);
    ASSERT(mock.saw_mode_flag);
    ASSERT(mock.saw_mode_value);
    ASSERT(mock.args_in_expected_order);
    ASSERT(strstr(mock.working_directory, "ctest_coverage_exec_bin") != NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COV_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COV_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::STATUS")),
                     nob_sv_from_cstr("COLLECTED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::LABELS_RESOLVED")),
                     nob_sv_from_cstr("core;ui")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_coverage_exec_bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                          nob_sv_from_cstr("Coverage")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("Coverage.xml")));

    String_View coverage_xml = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::COVERAGE_XML"));
    char *coverage_xml_c = arena_strndup(temp_arena, coverage_xml.data, coverage_xml.count);
    ASSERT(coverage_xml_c != NULL);
    ASSERT(nob_file_exists(coverage_xml_c));

    Nob_String_Builder coverage_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(coverage_xml_c, &coverage_sb));
    ASSERT(strstr(coverage_sb.items, "<Coverage>") != NULL);
    ASSERT(strstr(coverage_sb.items, "coverage-tool --mode scan") != NULL);
    ASSERT(strstr(coverage_sb.items, "--fast;--xml") != NULL);
    ASSERT(strstr(coverage_sb.items, "<Append>true</Append>") != NULL);
    ASSERT(strstr(coverage_sb.items, "<Label>core</Label>") != NULL);
    ASSERT(strstr(coverage_sb.items, "<Label>ui</Label>") != NULL);
    ASSERT(strstr(coverage_sb.items, "<FilteredSourceCount>1</FilteredSourceCount>") != NULL);
    ASSERT(strstr(coverage_sb.items, "ctest_coverage_exec_src/src/main.c") != NULL);
    ASSERT(strstr(coverage_sb.items, "ctest_coverage_exec_src/src/net.c") == NULL);
    nob_sb_free(coverage_sb);

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder manifest_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &manifest_sb));
    ASSERT(strstr(manifest_sb.items, "COMMAND=ctest_coverage") != NULL);
    ASSERT(strstr(manifest_sb.items, "PARTS=Coverage") != NULL);
    ASSERT(strstr(manifest_sb.items, "Coverage.xml") != NULL);
    nob_sb_free(manifest_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_update_executes_documented_command_and_stages_submit_part) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_update_exec_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_update_exec_src/.git"));
    ASSERT(nob_mkdir_if_not_exists("ctest_update_exec_bin"));

    Ctest_Submit_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_submit_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_update_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_update_exec_bin)\n"
        "set(CTEST_SUBMIT_URL https://submit.example.test/submit.php?project=Nobify)\n"
        "set(CTEST_GIT_COMMAND update-tool)\n"
        "set(CTEST_GIT_UPDATE_OPTIONS \"--mode;sync\")\n"
        "ctest_start(Experimental ctest_update_exec_src .)\n"
        "ctest_update(RETURN_VALUE UPD_RV CAPTURE_CMAKE_ERROR UPD_CE QUIET)\n"
        "ctest_submit(PARTS Update RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.call_count == 2);
    ASSERT(mock.submit_call_count == 1);
    ASSERT(mock.update_call_count == 1);
    ASSERT(mock.saw_update_tool);
    ASSERT(mock.saw_update_mode_flag);
    ASSERT(mock.saw_update_mode_value);
    ASSERT(strstr(mock.update_working_directory, "ctest_update_exec_src") != NULL);
    ASSERT(mock.saw_manifest_form);
    ASSERT(mock.saw_parts_form);
    ASSERT(mock.file_form_count == 2);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPD_RV")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPD_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::STATUS")),
                     nob_sv_from_cstr("UPDATED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATE_TYPE")),
                     nob_sv_from_cstr("git")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATE_COMMAND")),
                     nob_sv_from_cstr("update-tool")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATE_OPTIONS_RESOLVED")),
                     nob_sv_from_cstr("--mode;sync")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATED_COUNT")),
                     nob_sv_from_cstr("2")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATED_FILES")),
                          nob_sv_from_cstr("src/main.c")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::UPDATED_FILES")),
                          nob_sv_from_cstr("src/lib.c")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("SUBMITTED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                     nob_sv_from_cstr("Update")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Update.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("UpdateManifest.txt")));

    String_View update_xml = {0};
    ASSERT(eval_test_canonical_artifact_find(ctx,
                                             nob_sv_from_cstr("ctest_update"),
                                             nob_sv_from_cstr("UPDATE_XML"),
                                             &update_xml));
    char *update_xml_c = arena_strndup(temp_arena, update_xml.data, update_xml.count);
    ASSERT(update_xml_c != NULL);
    ASSERT(nob_file_exists(update_xml_c));

    Nob_String_Builder update_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(update_xml_c, &update_sb));
    ASSERT(strstr(update_sb.items, "<Update>") != NULL);
    ASSERT(strstr(update_sb.items, "<UpdateType>git</UpdateType>") != NULL);
    ASSERT(strstr(update_sb.items, "update-tool") != NULL);
    ASSERT(strstr(update_sb.items, "--mode;sync") != NULL);
    ASSERT(strstr(update_sb.items, "<UpdatedCount>2</UpdatedCount>") != NULL);
    ASSERT(strstr(update_sb.items, "<File>src/main.c</File>") != NULL);
    ASSERT(strstr(update_sb.items, "<File>src/lib.c</File>") != NULL);
    nob_sb_free(update_sb);

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder manifest_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &manifest_sb));
    ASSERT(strstr(manifest_sb.items, "COMMAND=ctest_update") != NULL);
    ASSERT(strstr(manifest_sb.items, "PARTS=Update") != NULL);
    ASSERT(strstr(manifest_sb.items, "Update.xml") != NULL);
    nob_sb_free(manifest_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_coverage_captures_missing_command_without_fatal_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_coverage_missing_bin"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_coverage_missing_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_coverage_missing_bin)\n"
        "set(CTEST_BINARY_DIRECTORY ctest_coverage_missing_bin)\n"
        "ctest_coverage(RETURN_VALUE COV_RV CAPTURE_CMAKE_ERROR COV_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COV_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COV_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_coverage::COVERAGE_XML")).count == 0);
    String_View step_status = {0};
    String_View submit_part = {0};
    ASSERT(eval_test_ctest_step_find(ctx, nob_sv_from_cstr("ctest_coverage"), &step_status, &submit_part));
    ASSERT(nob_sv_eq(step_status, nob_sv_from_cstr("FAILED")));
    ASSERT(nob_sv_eq(submit_part, nob_sv_from_cstr("Coverage")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_run_script_returns_last_script_status) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_write_entire_file("ctest_script_error_then_ok_1.cmake",
                                 "message(SEND_ERROR first_script_failed)\n",
                                 strlen("message(SEND_ERROR first_script_failed)\n")));
    ASSERT(nob_write_entire_file("ctest_script_error_then_ok_2.cmake",
                                 "set(CTEST_LAST_SCRIPT_OK 1)\n",
                                 strlen("set(CTEST_LAST_SCRIPT_OK 1)\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "ctest_run_script(ctest_script_error_then_ok_1.cmake ctest_script_error_then_ok_2.cmake RETURN_VALUE SCRIPT_LAST_RV)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SCRIPT_LAST_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CTEST_LAST_SCRIPT_OK")), nob_sv_from_cstr("1")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_memcheck_executes_backend_and_stages_submit_part) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_memcheck_exec_bin"));
    ASSERT(nob_mkdir_if_not_exists("ctest_memcheck_exec_bin/work"));
    ASSERT(nob_write_entire_file("ctest_memcheck_exec_bin/ctest-resource.json",
                                 "{ }\n",
                                 strlen("{ }\n")));
    ASSERT(nob_write_entire_file("ctest_memcheck_exec_bin/suppressions.supp",
                                 "leak:ignore\n",
                                 strlen("leak:ignore\n")));

    Ctest_Memcheck_Mock_Process_Data mock = {0};
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_memcheck_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_memcheck_exec_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_memcheck_exec_bin)\n"
        "set(CTEST_SUBMIT_URL https://submit.example.test/submit.php?project=Nobify)\n"
        "set(CTEST_TEST_LOAD 7)\n"
        "set(CTEST_MEMORYCHECK_COMMAND memcheck-tool)\n"
        "set(CTEST_MEMORYCHECK_TYPE Valgrind)\n"
        "set(CTEST_MEMORYCHECK_COMMAND_OPTIONS \"--xml=yes;--trace-children=yes\")\n"
        "set(CTEST_MEMORYCHECK_SANITIZER_OPTIONS \"--keep-debuginfo=yes\")\n"
        "set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE suppressions.supp)\n"
        "set(CTEST_RESOURCE_SPEC_FILE ctest-resource.json)\n"
        "enable_testing()\n"
        "add_test(NAME pass COMMAND memcheck-pass --alpha)\n"
        "add_test(NAME defect COMMAND memcheck-fail --beta WORKING_DIRECTORY work)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_memcheck(START 1 END 2 STRIDE 1 PARALLEL_LEVEL 3 SCHEDULE_RANDOM OFF RETURN_VALUE MEM_RV CAPTURE_CMAKE_ERROR MEM_CE DEFECT_COUNT MEM_DEFECTS OUTPUT_JUNIT reports/memcheck.xml QUIET)\n"
        "ctest_submit(PARTS MemCheck RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    ASSERT(mock.memcheck_call_count == 2);
    ASSERT(mock.submit.submit_call_count == 1);
    ASSERT(mock.submit.file_form_count == 2);
    ASSERT(mock.submit.saw_manifest_form);
    ASSERT(mock.submit.saw_parts_form);
    ASSERT(mock.saw_memcheck_tool);
    ASSERT(mock.saw_backend_xml_flag);
    ASSERT(mock.saw_backend_trace_children_flag);
    ASSERT(mock.saw_backend_sanitizer_flag);
    ASSERT(mock.saw_suppressions_flag);
    ASSERT(mock.saw_separator);
    ASSERT(mock.saw_test_pass_tool);
    ASSERT(mock.saw_test_fail_tool);
    ASSERT(mock.saw_pass_arg);
    ASSERT(mock.saw_fail_arg);
    ASSERT(strstr(mock.default_working_directory, "ctest_memcheck_exec_bin") != NULL);
    ASSERT(strstr(mock.custom_working_directory, "ctest_memcheck_exec_bin/work") != NULL);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("MEM_RV")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("MEM_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("MEM_DEFECTS")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::STATUS")),
                     nob_sv_from_cstr("MEMCHECKED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::START")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::END")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::STRIDE")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::PARALLEL_LEVEL")),
                     nob_sv_from_cstr("3")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::SCHEDULE_RANDOM")),
                     nob_sv_from_cstr("OFF")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_memcheck_exec_bin")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_TEST_LOAD")),
                     nob_sv_from_cstr("7")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx,
                                            nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_RESOURCE_SPEC_FILE")),
                          nob_sv_from_cstr("ctest_memcheck_exec_bin/ctest-resource.json")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx,
                                            nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_OUTPUT_JUNIT")),
                          nob_sv_from_cstr("ctest_memcheck_exec_bin/reports/memcheck.xml")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::BACKEND_TYPE")),
                     nob_sv_from_cstr("Valgrind")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::BACKEND_COMMAND")),
                     nob_sv_from_cstr("memcheck-tool")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::BACKEND_OPTIONS")),
                     nob_sv_from_cstr("--xml=yes;--trace-children=yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx,
                                       nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::BACKEND_SANITIZER_OPTIONS")),
                     nob_sv_from_cstr("--keep-debuginfo=yes")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx,
                                            nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_SUPPRESSIONS_FILE")),
                          nob_sv_from_cstr("ctest_memcheck_exec_bin/suppressions.supp")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::DEFECT_COUNT_RESOLVED")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::FAILED_COUNT")),
                     nob_sv_from_cstr("1")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                          nob_sv_from_cstr("MemCheck")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("MemCheck.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("MemCheckManifest.txt")));
    ASSERT(!sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                           nob_sv_from_cstr("reports/memcheck.xml")));

    String_View memcheck_xml = {0};
    ASSERT(eval_test_canonical_artifact_find(ctx,
                                             nob_sv_from_cstr("ctest_memcheck"),
                                             nob_sv_from_cstr("MEMCHECK_XML"),
                                             &memcheck_xml));
    char *memcheck_xml_c = arena_strndup(temp_arena, memcheck_xml.data, memcheck_xml.count);
    ASSERT(memcheck_xml_c != NULL);
    ASSERT(nob_file_exists(memcheck_xml_c));

    Nob_String_Builder memcheck_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(memcheck_xml_c, &memcheck_sb));
    ASSERT(strstr(memcheck_sb.items, "<MemCheck>") != NULL);
    ASSERT(strstr(memcheck_sb.items, "<BackendType>Valgrind</BackendType>") != NULL);
    ASSERT(strstr(memcheck_sb.items, "<BackendCommand>memcheck-tool</BackendCommand>") != NULL);
    ASSERT(strstr(memcheck_sb.items, "<DefectCount>2</DefectCount>") != NULL);
    ASSERT(strstr(memcheck_sb.items, "<FailedTests>1</FailedTests>") != NULL);
    ASSERT(strstr(memcheck_sb.items, "memcheck-pass --alpha") != NULL);
    ASSERT(strstr(memcheck_sb.items, "memcheck-fail --beta") != NULL);
    ASSERT(strstr(memcheck_sb.items, "--suppressions=") != NULL);
    nob_sb_free(memcheck_sb);

    String_View junit = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_memcheck::RESOLVED_OUTPUT_JUNIT"));
    char *junit_c = arena_strndup(temp_arena, junit.data, junit.count);
    ASSERT(junit_c != NULL);
    ASSERT(nob_file_exists(junit_c));

    Nob_String_Builder junit_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(junit_c, &junit_sb));
    ASSERT(strstr(junit_sb.items, "<testsuite") != NULL);
    ASSERT(strstr(junit_sb.items, "name=\"pass\"") != NULL);
    ASSERT(strstr(junit_sb.items, "name=\"defect\"") != NULL);
    ASSERT(strstr(junit_sb.items, "<failure") != NULL);
    nob_sb_free(junit_sb);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_test_workspace_golden_updates_target_repo_root) {
    const char *repo_root = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    const char *probe_rel = "Temp_tests/golden_update_probe.txt";
    const char *probe_text = "probe\n";
    char probe_abs[_TINYDIR_PATH_MAX] = {0};
    int n = 0;

    if (!repo_root || repo_root[0] == '\0') {
        TEST_PASS();
        return;
    }

    n = snprintf(probe_abs, sizeof(probe_abs), "%s/%s", repo_root, probe_rel);
    ASSERT(n > 0 && n < (int)sizeof(probe_abs));
    ASSERT(test_fs_delete_file_like(probe_abs));

    ASSERT(test_ws_update_golden_file(probe_rel, probe_text, strlen(probe_text)));
    ASSERT(nob_file_exists(probe_abs));
    ASSERT(!nob_file_exists(probe_rel));
    ASSERT(test_fs_delete_file_like(probe_abs));
    TEST_PASS();
}

TEST(evaluator_ctest_submit_models_documented_local_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_submit_defaults_bin"));
    ASSERT(nob_write_entire_file("ctest_submit_defaults_bin/notes.md",
                                 "notes\n",
                                 strlen("notes\n")));
    ASSERT(nob_write_entire_file("ctest_submit_defaults_bin/extra.log",
                                 "extra\n",
                                 strlen("extra\n")));
    ASSERT(nob_write_entire_file("ctest_submit_defaults_bin/upload.bin",
                                 "upload\n",
                                 strlen("upload\n")));

    Ctest_Submit_Mock_Process_Data mock = {
        .mode = CTEST_SUBMIT_MOCK_REMOTE_SUCCESS_AFTER_TIMEOUT,
        .timeout_failures_remaining = 1,
    };
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_submit_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_submit_defaults_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_submit_defaults_bin)\n"
        "set(CTEST_DROP_METHOD https)\n"
        "set(CTEST_DROP_SITE submit.example.test)\n"
        "set(CTEST_DROP_LOCATION submit.php?project=Nobify)\n"
        "set(CTEST_SUBMIT_INACTIVITY_TIMEOUT 9)\n"
        "set(CTEST_NOTES_FILES notes.md)\n"
        "set(CTEST_EXTRA_SUBMIT_FILES extra.log)\n"
        "set(CTEST_UPDATE_COMMAND update-tool)\n"
        "set(CTEST_BUILD_COMMAND build-tool)\n"
        "ctest_start(Experimental . . TRACK Nightly)\n"
        "ctest_update()\n"
        "ctest_build()\n"
        "ctest_test()\n"
        "ctest_memcheck()\n"
        "ctest_upload(FILES upload.bin)\n"
        "unset(NOBIFY_CTEST::ctest_start::STATUS)\n"
        "unset(NOBIFY_CTEST::ctest_update::STATUS)\n"
        "unset(NOBIFY_CTEST::ctest_build::STATUS)\n"
        "unset(NOBIFY_CTEST::ctest_test::STATUS)\n"
        "unset(NOBIFY_CTEST::ctest_memcheck::STATUS)\n"
        "unset(NOBIFY_CTEST::ctest_upload::UPLOAD_XML)\n"
        "unset(NOBIFY_CTEST::ctest_upload::RESOLVED_FILES)\n"
        "ctest_submit(HTTPHEADER \"Authorization: Bearer one\" HTTPHEADER \"X-Nobify: yes\" RETRY_COUNT 1 RETRY_DELAY 0 RETURN_VALUE SUBMIT_RV BUILD_ID SUBMIT_BUILD_ID)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUBMIT_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUBMIT_BUILD_ID")), nob_sv_from_cstr("321")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::SIGNATURE")),
                     nob_sv_from_cstr("DEFAULT")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::SUBMIT_URL")),
                     nob_sv_from_cstr("https://submit.example.test/submit.php?project=Nobify")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::HTTPHEADER")),
                     nob_sv_from_cstr("Authorization: Bearer one;X-Nobify: yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                     nob_sv_from_cstr("Start;Update;Build;Test;MemCheck;Notes;ExtraFiles;Upload")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("SUBMITTED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::ATTEMPTS")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::HTTP_CODE")),
                     nob_sv_from_cstr("200")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::BUILD_ID_RESULT")),
                     nob_sv_from_cstr("321")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RETRY_COUNT_RESOLVED")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RETRY_DELAY_RESOLVED")),
                     nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::INACTIVITY_TIMEOUT")),
                     nob_sv_from_cstr("9")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_submit_defaults_bin/notes.md")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_submit_defaults_bin/extra.log")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Update.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("UpdateManifest.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Build.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("BuildManifest.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Test.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("TestManifest.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/MemCheck.xml")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("MemCheckManifest.txt")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_submit_defaults_bin/upload.bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Upload.xml")));

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder submit_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &submit_sb));
    ASSERT(strstr(submit_sb.items, "SIGNATURE=DEFAULT") != NULL);
    ASSERT(strstr(submit_sb.items, "SUBMIT_URL=https://submit.example.test/submit.php?project=Nobify") != NULL);
    ASSERT(strstr(submit_sb.items, "HTTPHEADERS=Authorization: Bearer one;X-Nobify: yes") != NULL);
    ASSERT(strstr(submit_sb.items, "RETRY_COUNT=1") != NULL);
    ASSERT(strstr(submit_sb.items, "INACTIVITY_TIMEOUT=9") != NULL);
    ASSERT(strstr(submit_sb.items, "PARTS=Start;Update;Build;Test;MemCheck;Notes;ExtraFiles;Upload") != NULL);
    ASSERT(strstr(submit_sb.items, "notes.md") != NULL);
    ASSERT(strstr(submit_sb.items, "extra.log") != NULL);
    ASSERT(strstr(submit_sb.items, "upload.bin") != NULL);
    nob_sb_free(submit_sb);

    ASSERT(mock.call_count == 4);
    ASSERT(mock.submit_call_count == 2);
    ASSERT(mock.update_call_count == 1);
    ASSERT(mock.build_call_count == 1);
    ASSERT(mock.saw_auth_header);
    ASSERT(mock.saw_extra_header);
    ASSERT(mock.saw_manifest_form);
    ASSERT(mock.saw_parts_form);
    ASSERT(mock.file_form_count == 24);
    ASSERT(mock.saw_legacy_url);
    ASSERT(mock.saw_update_tool);
    ASSERT(mock.saw_build_tool);
    ASSERT(mock.saw_upload_xml_form);
    ASSERT(mock.saw_upload_payload_form);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_submit_models_cdash_upload_signature) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_submit_upload_api_bin"));
    ASSERT(nob_write_entire_file("ctest_submit_upload_api_bin/artifact.dat",
                                 "artifact\n",
                                 strlen("artifact\n")));

    Ctest_Submit_Mock_Process_Data mock = {
        .mode = CTEST_SUBMIT_MOCK_CDASH_UPLOAD_SUCCESS,
    };
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_submit_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_submit_upload_api_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_submit_upload_api_bin)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_submit(CDASH_UPLOAD artifact.dat CDASH_UPLOAD_TYPE CoverageData SUBMIT_URL https://cdash.example.test/api/v1 HTTPHEADER \"Authorization: Bearer two\" RETURN_VALUE UPLOAD_API_RV BUILD_ID UPLOAD_API_BUILD_ID)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPLOAD_API_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPLOAD_API_BUILD_ID")), nob_sv_from_cstr("88")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::SIGNATURE")),
                     nob_sv_from_cstr("CDASH_UPLOAD")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::CDASH_UPLOAD_TYPE")),
                     nob_sv_from_cstr("CoverageData")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::SUBMIT_URL")),
                     nob_sv_from_cstr("https://cdash.example.test/api/v1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("SUBMITTED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::ATTEMPTS")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::BUILD_ID_RESULT")),
                     nob_sv_from_cstr("88")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_submit_upload_api_bin/artifact.dat")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")).count == 0);

    String_View manifest = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::MANIFEST"));
    char *manifest_c = arena_strndup(temp_arena, manifest.data, manifest.count);
    ASSERT(manifest_c != NULL);
    ASSERT(nob_file_exists(manifest_c));

    Nob_String_Builder submit_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(manifest_c, &submit_sb));
    ASSERT(strstr(submit_sb.items, "SIGNATURE=CDASH_UPLOAD") != NULL);
    ASSERT(strstr(submit_sb.items, "CDASH_UPLOAD=artifact.dat") != NULL);
    ASSERT(strstr(submit_sb.items, "CDASH_UPLOAD_TYPE=CoverageData") != NULL);
    ASSERT(strstr(submit_sb.items, "FILES=") != NULL);
    ASSERT(strstr(submit_sb.items, "artifact.dat") != NULL);
    nob_sb_free(submit_sb);

    ASSERT(mock.call_count == 2);
    ASSERT(mock.saw_probe_phase);
    ASSERT(mock.saw_upload_phase);
    ASSERT(mock.saw_hash_form);
    ASSERT(mock.saw_type_form);
    ASSERT(mock.file_form_count == 1);
    ASSERT(mock.saw_submit_url);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_submit_captures_remote_failures) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_submit_failure_bin"));

    Ctest_Submit_Mock_Process_Data mock = {
        .mode = CTEST_SUBMIT_MOCK_FAILURE,
    };
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_submit_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_submit_failure_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_submit_failure_bin)\n"
        "set(CTEST_SUBMIT_URL https://fail.example.test/submit)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_submit(RETURN_VALUE FAIL_RV BUILD_ID FAIL_BUILD_ID CAPTURE_CMAKE_ERROR FAIL_CE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FAIL_RV")), nob_sv_from_cstr("6")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("FAIL_BUILD_ID")).count == 0);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FAIL_CE")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::STATUS")),
                     nob_sv_from_cstr("FAILED")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::ATTEMPTS")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::HTTP_CODE")),
                     nob_sv_from_cstr("0")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESPONSE_DETAIL")),
                          nob_sv_from_cstr("Could not resolve host")));

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_manifest_form);
    ASSERT(mock.saw_submit_url);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_upload_stages_upload_xml_and_submit_part) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_upload_remote_bin"));
    ASSERT(nob_write_entire_file("ctest_upload_remote_bin/upload.bin",
                                 "upload\n",
                                 strlen("upload\n")));

    Ctest_Submit_Mock_Process_Data mock = {
        .mode = CTEST_SUBMIT_MOCK_REMOTE_SUCCESS_AFTER_TIMEOUT,
    };
    EvalServices services = {
        .user_data = &mock,
        .process_run_capture = ctest_submit_mock_process_run,
    };

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    init.services = &services;

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_upload_remote_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_upload_remote_bin)\n"
        "set(CTEST_SUBMIT_URL https://cdash.example.test/api/v1)\n"
        "ctest_start(Experimental . .)\n"
        "ctest_upload(FILES upload.bin QUIET CAPTURE_CMAKE_ERROR UPLOAD_CE)\n"
        "ctest_submit(PARTS Upload RETURN_VALUE UPLOAD_PART_RV BUILD_ID UPLOAD_PART_BUILD_ID)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPLOAD_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPLOAD_PART_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("UPLOAD_PART_BUILD_ID")), nob_sv_from_cstr("321")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::QUIET")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_PARTS")),
                     nob_sv_from_cstr("Upload")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_upload_remote_bin/upload.bin")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("/Upload.xml")));

    String_View upload_xml = eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::UPLOAD_XML"));
    char *upload_xml_c = arena_strndup(temp_arena, upload_xml.data, upload_xml.count);
    ASSERT(upload_xml_c != NULL);
    ASSERT(nob_file_exists(upload_xml_c));

    Nob_String_Builder upload_xml_sb = {0};
    ASSERT(evaluator_read_entire_file_cstr(upload_xml_c, &upload_xml_sb));
    ASSERT(strstr(upload_xml_sb.items, "<Upload>") != NULL);
    ASSERT(strstr(upload_xml_sb.items, "ctest_upload_remote_bin/upload.bin") != NULL);
    nob_sb_free(upload_xml_sb);

    ASSERT(mock.call_count == 1);
    ASSERT(mock.saw_submit_url);
    ASSERT(mock.saw_manifest_form);
    ASSERT(mock.saw_parts_form);
    ASSERT(mock.file_form_count == 2);
    ASSERT(mock.saw_upload_xml_form);
    ASSERT(mock.saw_upload_payload_form);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_submit_rejects_invalid_parts_and_mixed_signatures) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_submit_invalid_bin"));
    ASSERT(nob_write_entire_file("ctest_submit_invalid_bin/artifact.dat",
                                 "artifact\n",
                                 strlen("artifact\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_submit_invalid_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_submit_invalid_bin)\n"
        "ctest_submit(PARTS Bogus)\n"
        "ctest_submit(CDASH_UPLOAD artifact.dat FILES artifact.dat)\n"
        "ctest_submit(CDASH_UPLOAD_TYPE Logs)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_invalid_part = false;
    bool saw_mixed_signature = false;
    bool saw_type_without_upload = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("ctest_submit() received an invalid PARTS value"))) {
            saw_invalid_part = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_submit(CDASH_UPLOAD ...) does not accept PARTS or FILES"))) {
            saw_mixed_signature = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_submit() only accepts CDASH_UPLOAD_TYPE with CDASH_UPLOAD"))) {
            saw_type_without_upload = true;
        }
    }

    ASSERT(saw_invalid_part);
    ASSERT(saw_mixed_signature);
    ASSERT(saw_type_without_upload);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_rejects_invalid_and_unsupported_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("safe_bin"));
    ASSERT(nob_write_entire_file("ctest_script_bad.cmake",
                                 "set(UNUSED 1)\n",
                                 strlen("set(UNUSED 1)\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR safe_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR safe_bin)\n"
        "ctest_empty_binary_directory(../outside)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_bad.cmake RETURN_VALUE SCRIPT_BAD_RV)\n"
        "ctest_sleep(1 2)\n"
        "ctest_build(BUILD)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SCRIPT_BAD_RV")), nob_sv_from_cstr("0")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("UNUSED")).count == 0);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_memcheck_rejects_invalid_documented_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_memcheck_invalid_bin"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_memcheck_invalid_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_memcheck_invalid_bin)\n"
        "ctest_memcheck(SCHEDULE_RANDOM)\n"
        "ctest_memcheck(SCHEDULE_RANDOM maybe)\n"
        "ctest_memcheck(PARALLEL_LEVEL 0)\n"
        "ctest_memcheck(REPEAT SOMEDAY:3)\n"
        "ctest_memcheck(REPEAT UNTIL_PASS:0)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    bool saw_missing_value = false;
    bool saw_schedule_random = false;
    bool saw_parallel_level = false;
    bool saw_repeat_mode = false;
    bool saw_repeat_count = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("ctest command keyword requires a value"))) {
            saw_missing_value = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_memcheck() SCHEDULE_RANDOM requires ON or OFF"))) {
            saw_schedule_random = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_memcheck() PARALLEL_LEVEL requires a positive integer"))) {
            saw_parallel_level = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_memcheck() REPEAT mode is not supported"))) {
            saw_repeat_mode = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_memcheck() REPEAT count requires a positive integer"))) {
            saw_repeat_count = true;
        }
    }

    ASSERT(saw_missing_value);
    ASSERT(saw_schedule_random);
    ASSERT(saw_parallel_level);
    ASSERT(saw_repeat_mode);
    ASSERT(saw_repeat_count);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_entrypoints_reject_incomplete_argument_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "ctest_empty_binary_directory()\n"
        "ctest_read_custom_files()\n"
        "ctest_run_script(RETURN_VALUE)\n"
        "ctest_sleep(alpha)\n"
        "ctest_start()\n"
        "ctest_submit(FILES)\n"
        "ctest_upload()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 7);

    bool saw_empty_dir_arity = false;
    bool saw_custom_dirs_arity = false;
    bool saw_run_script_return = false;
    bool saw_sleep_numeric = false;
    bool saw_start_positionals = false;
    bool saw_submit_files_values = false;
    bool saw_upload_files_required = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("ctest_empty_binary_directory() requires exactly one directory argument"))) {
            saw_empty_dir_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_read_custom_files() requires one or more directories"))) {
            saw_custom_dirs_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_run_script(RETURN_VALUE ...) requires a variable"))) {
            saw_run_script_return = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest_sleep() requires numeric arguments"))) {
            saw_sleep_numeric = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest command received an invalid number of positional arguments"))) {
            saw_start_positionals = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest command list keyword requires one or more values"))) {
            saw_submit_files_values = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("ctest command requires FILES"))) {
            saw_upload_files_required = true;
        }
    }

    ASSERT(saw_empty_dir_arity);
    ASSERT(saw_custom_dirs_arity);
    ASSERT(saw_run_script_return);
    ASSERT(saw_sleep_numeric);
    ASSERT(saw_start_positionals);
    ASSERT(saw_submit_files_values);
    ASSERT(saw_upload_files_required);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_register_and_model_compat_paths) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(legacy_iface INTERFACE)\n"
        "export_library_dependencies(legacy_deps.cmake)\n"
        "make_directory(legacy_dir/sub)\n"
        "write_file(legacy_dir/sub/out.txt alpha beta)\n"
        "install_files(share .txt first.txt second.txt)\n"
        "install_programs(bin tool.sh)\n"
        "install_targets(lib legacy_iface)\n"
        "load_command(legacy_cmd ./module)\n"
        "output_required_files(input.c output.txt)\n"
        "set(LEGACY_LIST a;b;c;b)\n"
        "remove(LEGACY_LIST b)\n"
        "qt_wrap_cpp(LegacyLib LEGACY_MOCS foo.hpp bar.hpp)\n"
        "qt_wrap_ui(LegacyLib LEGACY_UI_HDRS LEGACY_UI_SRCS dialog.ui)\n"
        "subdir_depends(src dep1 dep2)\n"
        "subdirs(dir_a dir_b)\n"
        "use_mangled_mesa(mesa out prefix)\n"
        "utility_source(CACHE_EXE /bin/tool generated.c)\n"
        "variable_requires(TESTVAR OUTVAR NEED1 NEED2)\n"
        "variable_watch(WATCH_ME watch-cmd)\n"
        "set(WATCH_ME touched)\n"
        "unset(WATCH_ME)\n"
        "fltk_wrap_ui(FltkLib main.fl)\n"
        "write_file(legacy_dir/sub/appended.txt one)\n"
        "write_file(legacy_dir/sub/appended.txt two APPEND)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_file_exists("legacy_dir/sub"));
    ASSERT(nob_file_exists("legacy_dir/sub/out.txt"));
    String_View out_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/out.txt", &out_txt));
    ASSERT(nob_sv_eq(out_txt, nob_sv_from_cstr("alphabeta")));

    String_View appended_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/appended.txt", &appended_txt));
    ASSERT(nob_sv_eq(appended_txt, nob_sv_from_cstr("onetwo")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("LEGACY_MOCS")), nob_sv_from_cstr("moc_foo.cxx;moc_bar.cxx")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_HDRS")), nob_sv_from_cstr("ui_dialog.h")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_SRCS")), nob_sv_from_cstr("ui_dialog.cxx")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("FltkLib_FLTK_UI_SRCS")),
                     nob_sv_from_cstr("fluid_main.cxx;fluid_main.h")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::export_library_dependencies::ARGS")),
                     nob_sv_from_cstr("legacy_deps.cmake")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::load_command::ARGS")),
                     nob_sv_from_cstr("legacy_cmd;./module")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::subdirs::ARGS")),
                     nob_sv_from_cstr("dir_a;dir_b")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VAR")),
                     nob_sv_from_cstr("WATCH_ME")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_ACTION")),
                     nob_sv_from_cstr("UNSET")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VALUE")),
                     nob_sv_from_cstr("touched")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_COMMAND")),
                     nob_sv_from_cstr("watch-cmd")));

    size_t install_rule_count = 0;
    bool saw_unknown_command = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_INSTALL_ADD_RULE) install_rule_count++;
        if (ev->h.kind == EV_DIAGNOSTIC && nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command"))) {
            saw_unknown_command = true;
        }
    }
    ASSERT(install_rule_count >= 4);
    ASSERT(!saw_unknown_command);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_reject_invalid_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "make_directory()\n"
        "write_file()\n"
        "write_file(legacy.txt APPEND)\n"
        "remove(ONLY_VAR)\n"
        "variable_watch(A B C)\n"
        "qt_wrap_cpp(LegacyLib ONLY_OUT)\n"
        "qt_wrap_ui(LegacyLib ONLY_HDRS ONLY_SRCS)\n"
        "fltk_wrap_ui()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 8);

    bool saw_make_directory = false;
    bool saw_write_file = false;
    bool saw_write_file_append = false;
    bool saw_remove = false;
    bool saw_variable_watch = false;
    bool saw_qt_wrap_cpp = false;
    bool saw_qt_wrap_ui = false;
    bool saw_fltk_wrap_ui = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("make_directory() requires at least one directory"))) {
            saw_make_directory = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("write_file() requires a path and at least one content argument"))) {
            saw_write_file = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("write_file(APPEND) still requires content"))) {
            saw_write_file_append = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("remove() requires a variable and one or more values"))) {
            saw_remove = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("variable_watch() requires a variable and an optional command"))) {
            saw_variable_watch = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("qt_wrap_cpp() requires a library, output variable and one or more headers"))) {
            saw_qt_wrap_cpp = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("qt_wrap_ui() requires a library, header var, source var and one or more UI files"))) {
            saw_qt_wrap_ui = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("fltk_wrap_ui() requires a target name and one or more UI files"))) {
            saw_fltk_wrap_ui = true;
        }
    }

    ASSERT(saw_make_directory);
    ASSERT(saw_write_file);
    ASSERT(saw_write_file_append);
    ASSERT(saw_remove);
    ASSERT(saw_variable_watch);
    ASSERT(saw_qt_wrap_cpp);
    ASSERT(saw_qt_wrap_ui);
    ASSERT(saw_fltk_wrap_ui);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "add_library(imported_mod STATIC IMPORTED)\n"
        "add_executable(app app.c)\n"
        "target_sources(real PRIVATE priv.c PUBLIC pub.h INTERFACE iface.h)\n"
        "target_sources(real PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/public.hpp include/detail.hpp)\n"
        "target_sources(real INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS api FILES api/iface.hpp)\n"
        "target_sources(real PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_sources(imported_mod INTERFACE FILE_SET CXX_MODULES BASE_DIRS imported FILES imported/api.cppm)\n"
        "target_compile_features(real PRIVATE cxx_std_20 PUBLIC cxx_std_17 INTERFACE c_std_11)\n"
        "target_precompile_headers(real PRIVATE pch.h PUBLIC pch_pub.h INTERFACE <vector>)\n"
        "target_precompile_headers(app REUSE_FROM real)\n"
        "get_target_property(REAL_SOURCES real SOURCES)\n"
        "get_target_property(REAL_IFACE_SOURCES real INTERFACE_SOURCES)\n"
        "get_target_property(REAL_HEADER_SETS real HEADER_SETS)\n"
        "get_target_property(REAL_INTERFACE_HEADER_SETS real INTERFACE_HEADER_SETS)\n"
        "get_target_property(REAL_HEADER_SET real HEADER_SET)\n"
        "get_target_property(REAL_HEADER_DIRS real HEADER_DIRS)\n"
        "get_target_property(REAL_HEADER_SET_API real HEADER_SET_API)\n"
        "get_target_property(REAL_HEADER_DIRS_API real HEADER_DIRS_API)\n"
        "get_target_property(REAL_CXX_MODULE_SETS real CXX_MODULE_SETS)\n"
        "get_target_property(REAL_CXX_MODULE_SET real CXX_MODULE_SET)\n"
        "get_target_property(REAL_CXX_MODULE_DIRS real CXX_MODULE_DIRS)\n"
        "get_target_property(IMPORTED_IFACE_CXX_MODULE_SETS imported_mod INTERFACE_CXX_MODULE_SETS)\n"
        "get_target_property(IMPORTED_CXX_MODULE_SET imported_mod CXX_MODULE_SET)\n"
        "get_target_property(IMPORTED_CXX_MODULE_DIRS imported_mod CXX_MODULE_DIRS)\n"
        "get_target_property(REAL_COMPILE_FEATURES real COMPILE_FEATURES)\n"
        "get_target_property(REAL_IFACE_COMPILE_FEATURES real INTERFACE_COMPILE_FEATURES)\n"
        "get_target_property(REAL_PCH real PRECOMPILE_HEADERS)\n"
        "get_target_property(REAL_IFACE_PCH real INTERFACE_PRECOMPILE_HEADERS)\n"
        "get_target_property(APP_REUSE app PRECOMPILE_HEADERS_REUSE_FROM)\n"
        "target_sources(real bad.c another.c)\n"
        "target_sources(real INTERFACE FILE_SET ifacemods TYPE CXX_MODULES FILES iface_bad.cppm)\n"
        "target_sources(real PUBLIC FILE_SET custom_modules FILES missing_type.cppm)\n"
        "target_compile_features(alias_real PRIVATE bad_feature)\n"
        "target_precompile_headers(missing_pch PRIVATE missing.h)\n"
        "target_sources(missing_src PRIVATE bad.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_priv_source = false;
    bool saw_pub_source = false;
    bool saw_module_source_event = false;
    bool saw_iface_prop = false;
    bool saw_header_set_prop = false;
    bool saw_interface_header_sets_prop = false;
    bool saw_cxx_module_sets_prop = false;
    bool saw_cxx_module_set_prop = false;
    bool saw_cxx_module_dirs_prop = false;
    bool saw_imported_cxx_module_sets_prop = false;
    bool saw_compile_feature_local = false;
    bool saw_compile_feature_iface = false;
    bool saw_pch_local = false;
    bool saw_pch_iface = false;
    bool saw_reuse_from = false;
    bool saw_reuse_dep = false;
    bool saw_visibility_error = false;
    bool saw_cxx_module_interface_error = false;
    bool saw_cxx_module_missing_type_error = false;
    bool saw_alias_error = false;
    bool saw_missing_pch_error = false;
    bool saw_missing_src_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("real"))) {
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("priv.c"))) saw_priv_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("pub.h"))) saw_pub_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("core.cppm"))) saw_module_source_event = true;
            ASSERT(!sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("iface.h")));
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("real"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_SOURCES")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("iface.h"))) {
                saw_iface_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("HEADER_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("include/public.hpp"))) {
                saw_header_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_HEADER_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("api"))) {
                saw_interface_header_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_cxx_module_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules/core.cppm"))) {
                saw_cxx_module_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_DIRS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules"))) {
                saw_cxx_module_dirs_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("cxx_std_20"))) {
                saw_compile_feature_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("c_std_11"))) {
                saw_compile_feature_iface = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch.h"))) {
                saw_pch_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS")) &&
                (sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("vector")) ||
                 sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch_pub.h")))) {
                saw_pch_iface = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imported_mod"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_imported_cxx_module_sets_prop = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM")) &&
                   nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("real"))) {
            saw_reuse_from = true;
        } else if (ev->h.kind == EV_TARGET_ADD_DEPENDENCY &&
                   nob_sv_eq(ev->as.target_add_dependency.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_add_dependency.dependency_name, nob_sv_from_cstr("real"))) {
            saw_reuse_dep = true;
        } else if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause,
                          nob_sv_from_cstr("target command requires PUBLIC, PRIVATE or INTERFACE before items"))) {
                saw_visibility_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET TYPE CXX_MODULES) may not use INTERFACE scope on non-IMPORTED targets"))) {
                saw_cxx_module_interface_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET ...) requires TYPE for non-default file-set names"))) {
                saw_cxx_module_missing_type_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_compile_features() cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_precompile_headers() target was not declared"))) {
                saw_missing_pch_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources() target was not declared"))) {
                saw_missing_src_error = true;
            }
        }
    }

    ASSERT(saw_priv_source);
    ASSERT(saw_pub_source);
    ASSERT(!saw_module_source_event);
    ASSERT(saw_iface_prop);
    ASSERT(saw_header_set_prop);
    ASSERT(saw_interface_header_sets_prop);
    ASSERT(saw_cxx_module_sets_prop);
    ASSERT(saw_cxx_module_set_prop);
    ASSERT(saw_cxx_module_dirs_prop);
    ASSERT(saw_imported_cxx_module_sets_prop);
    ASSERT(saw_compile_feature_local);
    ASSERT(saw_compile_feature_iface);
    ASSERT(saw_pch_local);
    ASSERT(saw_pch_iface);
    ASSERT(saw_reuse_from);
    ASSERT(saw_reuse_dep);
    ASSERT(saw_visibility_error);
    ASSERT(saw_cxx_module_interface_error);
    ASSERT(saw_cxx_module_missing_type_error);
    ASSERT(saw_alias_error);
    ASSERT(saw_missing_pch_error);
    ASSERT(saw_missing_src_error);

    String_View real_sources = eval_test_var_get(ctx, nob_sv_from_cstr("REAL_SOURCES"));
    ASSERT(semicolon_list_count(real_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 0), nob_sv_from_cstr("priv.c")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 1), nob_sv_from_cstr("pub.h")));

    String_View real_iface_sources = eval_test_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_SOURCES"));
    ASSERT(semicolon_list_count(real_iface_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 0), nob_sv_from_cstr("pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 1), nob_sv_from_cstr("iface.h")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_INTERFACE_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS;api")));

    String_View real_header_set = eval_test_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET"));
    ASSERT(semicolon_list_count(real_header_set) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 0), nob_sv_from_cstr("include/public.hpp")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 1), nob_sv_from_cstr("include/detail.hpp")));

    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS")),
                          nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET_API")),
                          nob_sv_from_cstr("api/iface.hpp")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS_API")),
                          nob_sv_from_cstr("api")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SET")),
                          nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("modules")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IMPORTED_IFACE_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_SET")),
                          nob_sv_from_cstr("imported/api.cppm")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("imported")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_20;cxx_std_17")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_17;c_std_11")));

    String_View real_pch = eval_test_var_get(ctx, nob_sv_from_cstr("REAL_PCH"));
    ASSERT(semicolon_list_count(real_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 0), nob_sv_from_cstr("pch.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 1), nob_sv_from_cstr("pch_pub.h")));

    String_View real_iface_pch = eval_test_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_PCH"));
    ASSERT(semicolon_list_count(real_iface_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 0), nob_sv_from_cstr("pch_pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 1), nob_sv_from_cstr("vector")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("APP_REUSE")), nob_sv_from_cstr("real")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_usage_commands_store_canonical_direct_and_interface_properties) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(usage_props STATIC usage.c)\n"
        "target_compile_definitions(usage_props PRIVATE LOCAL_DEF PUBLIC PUB_DEF INTERFACE IFACE_DEF)\n"
        "target_compile_options(usage_props BEFORE PRIVATE -local PUBLIC -pub INTERFACE -iface)\n"
        "target_include_directories(usage_props SYSTEM BEFORE PRIVATE inc/local PUBLIC inc/pub AFTER INTERFACE inc/iface)\n"
        "target_link_directories(usage_props PRIVATE lib/local PUBLIC lib/pub INTERFACE lib/iface)\n"
        "target_link_options(usage_props BEFORE PRIVATE LINK_LOCAL PUBLIC LINK_PUBLIC INTERFACE LINK_IFACE)\n"
        "target_link_libraries(usage_props PRIVATE privlib PUBLIC publib DEBUG dbgpub INTERFACE ifacelib OPTIMIZED optiface)\n"
        "get_target_property(DIRECT_DEFS usage_props COMPILE_DEFINITIONS)\n"
        "get_target_property(IFACE_DEFS usage_props INTERFACE_COMPILE_DEFINITIONS)\n"
        "get_target_property(DIRECT_OPTS usage_props COMPILE_OPTIONS)\n"
        "get_target_property(IFACE_OPTS usage_props INTERFACE_COMPILE_OPTIONS)\n"
        "get_target_property(DIRECT_INCS usage_props INCLUDE_DIRECTORIES)\n"
        "get_target_property(IFACE_INCS usage_props INTERFACE_INCLUDE_DIRECTORIES)\n"
        "get_target_property(DIRECT_LINK_DIRS usage_props LINK_DIRECTORIES)\n"
        "get_target_property(IFACE_LINK_DIRS usage_props INTERFACE_LINK_DIRECTORIES)\n"
        "get_target_property(DIRECT_LINK_OPTS usage_props LINK_OPTIONS)\n"
        "get_target_property(IFACE_LINK_OPTS usage_props INTERFACE_LINK_OPTIONS)\n"
        "get_target_property(DIRECT_LINK_LIBS usage_props LINK_LIBRARIES)\n"
        "get_target_property(IFACE_LINK_LIBS usage_props INTERFACE_LINK_LIBRARIES)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_public_compile_option_before = false;
    bool saw_public_include_dir_system_before = false;
    bool saw_interface_include_dir_after = false;
    bool saw_interface_link_option_before = false;
    bool saw_debug_link_library = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_OPTIONS &&
            nob_sv_eq(ev->as.target_compile_options.target_name, nob_sv_from_cstr("usage_props")) &&
            nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-pub")) &&
            ev->as.target_compile_options.visibility == EV_VISIBILITY_PUBLIC &&
            ev->as.target_compile_options.is_before) {
            saw_public_compile_option_before = true;
        } else if (ev->h.kind == EV_TARGET_INCLUDE_DIRECTORIES &&
                   nob_sv_eq(ev->as.target_include_directories.target_name, nob_sv_from_cstr("usage_props")) &&
                   sv_contains_sv(ev->as.target_include_directories.path, nob_sv_from_cstr("inc/pub")) &&
                   ev->as.target_include_directories.visibility == EV_VISIBILITY_PUBLIC &&
                   ev->as.target_include_directories.is_system &&
                   ev->as.target_include_directories.is_before) {
            saw_public_include_dir_system_before = true;
        } else if (ev->h.kind == EV_TARGET_INCLUDE_DIRECTORIES &&
                   nob_sv_eq(ev->as.target_include_directories.target_name, nob_sv_from_cstr("usage_props")) &&
                   sv_contains_sv(ev->as.target_include_directories.path, nob_sv_from_cstr("inc/iface")) &&
                   ev->as.target_include_directories.visibility == EV_VISIBILITY_INTERFACE &&
                   ev->as.target_include_directories.is_system &&
                   !ev->as.target_include_directories.is_before) {
            saw_interface_include_dir_after = true;
        } else if (ev->h.kind == EV_TARGET_LINK_OPTIONS &&
                   nob_sv_eq(ev->as.target_link_options.target_name, nob_sv_from_cstr("usage_props")) &&
                   nob_sv_eq(ev->as.target_link_options.item, nob_sv_from_cstr("LINK_IFACE")) &&
                   ev->as.target_link_options.visibility == EV_VISIBILITY_INTERFACE &&
                   ev->as.target_link_options.is_before) {
            saw_interface_link_option_before = true;
        } else if (ev->h.kind == EV_TARGET_LINK_LIBRARIES &&
                   nob_sv_eq(ev->as.target_link_libraries.target_name, nob_sv_from_cstr("usage_props")) &&
                   nob_sv_eq(ev->as.target_link_libraries.item,
                             nob_sv_from_cstr("$<$<CONFIG:Debug>:dbgpub>")) &&
                   ev->as.target_link_libraries.visibility == EV_VISIBILITY_PUBLIC) {
            saw_debug_link_library = true;
        }
    }

    ASSERT(saw_public_compile_option_before);
    ASSERT(saw_public_include_dir_system_before);
    ASSERT(saw_interface_include_dir_after);
    ASSERT(saw_interface_link_option_before);
    ASSERT(saw_debug_link_library);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_DEFS")),
                     nob_sv_from_cstr("LOCAL_DEF;PUB_DEF")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_DEFS")),
                     nob_sv_from_cstr("PUB_DEF;IFACE_DEF")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_OPTS")),
                     nob_sv_from_cstr("-local;-pub")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_OPTS")),
                     nob_sv_from_cstr("-pub;-iface")));

    String_View direct_incs = eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_INCS"));
    ASSERT(semicolon_list_count(direct_incs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(direct_incs, 0), nob_sv_from_cstr("inc/local")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(direct_incs, 1), nob_sv_from_cstr("inc/pub")));

    String_View iface_incs = eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_INCS"));
    ASSERT(semicolon_list_count(iface_incs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(iface_incs, 0), nob_sv_from_cstr("inc/pub")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(iface_incs, 1), nob_sv_from_cstr("inc/iface")));

    String_View direct_link_dirs = eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_LINK_DIRS"));
    ASSERT(semicolon_list_count(direct_link_dirs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(direct_link_dirs, 0), nob_sv_from_cstr("lib/local")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(direct_link_dirs, 1), nob_sv_from_cstr("lib/pub")));

    String_View iface_link_dirs = eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_LINK_DIRS"));
    ASSERT(semicolon_list_count(iface_link_dirs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(iface_link_dirs, 0), nob_sv_from_cstr("lib/pub")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(iface_link_dirs, 1), nob_sv_from_cstr("lib/iface")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_LINK_OPTS")),
                     nob_sv_from_cstr("LINK_LOCAL;LINK_PUBLIC")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_LINK_OPTS")),
                     nob_sv_from_cstr("LINK_PUBLIC;LINK_IFACE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIRECT_LINK_LIBS")),
                     nob_sv_from_cstr("privlib;publib;$<$<CONFIG:Debug>:dbgpub>")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IFACE_LINK_LIBS")),
                     nob_sv_from_cstr("publib;$<$<CONFIG:Debug>:dbgpub>;ifacelib;$<$<NOT:$<CONFIG:Debug>>:optiface>")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_source_group_supports_files_tree_and_regex_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "source_group(\"Root Files\" FILES main.c util.c REGULAR_EXPRESSION [=[.*\\.(c|h)$]=])\n"
        "source_group(TREE src PREFIX Generated FILES src/a.c src/sub/b.c)\n"
        "source_group(Texts [=[.*\\.txt$]=])\n"
        "source_group(TREE src FILES ../outside.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_main_group = false;
    bool saw_util_group = false;
    bool saw_tree_root = false;
    bool saw_tree_sub = false;
    bool saw_c_regex = false;
    bool saw_c_regex_name = false;
    bool saw_txt_regex = false;
    bool saw_txt_regex_name = false;
    bool saw_tree_outside_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_VAR_SET) {
            if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("main.c")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_main_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("util.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_util_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/a.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated"))) {
                saw_tree_root = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/sub/b.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated\\sub"))) {
                saw_tree_sub = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.(c|h)$"))) {
                saw_c_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_c_regex_name = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.txt$"))) {
                saw_txt_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Texts"))) {
                saw_txt_regex_name = true;
            }
        } else if (ev->h.kind == EV_DIAGNOSTIC &&
                   ev->as.diag.severity == EV_DIAG_ERROR &&
                   nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("source_group(TREE ...) file is outside the declared tree root"))) {
            saw_tree_outside_error = true;
        }
    }

    ASSERT(saw_main_group);
    ASSERT(saw_util_group);
    ASSERT(saw_tree_root);
    ASSERT(saw_tree_sub);
    ASSERT(saw_c_regex);
    ASSERT(saw_c_regex_name);
    ASSERT(saw_txt_regex);
    ASSERT(saw_txt_regex_name);
    ASSERT(saw_tree_outside_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_mode_severity_mapping) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(NOTICE n)\n"
        "message(STATUS s)\n"
        "message(VERBOSE v)\n"
        "message(DEBUG d)\n"
        "message(TRACE t)\n"
        "message(WARNING w)\n"
        "message(AUTHOR_WARNING aw)\n"
        "message(DEPRECATION dep)\n"
        "message(SEND_ERROR se)\n"
        "message(CHECK_START probe)\n"
        "message(CHECK_PASS ok)\n"
        "message(CHECK_START probe2)\n"
        "message(CHECK_FAIL fail)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 3);
    ASSERT(report->error_count == 1);

    size_t warning_diag_count = 0;
    size_t error_diag_count = 0;
    bool saw_check_pass_cause = false;
    bool saw_check_fail_cause = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_WARNING) warning_diag_count++;
        if (ev->as.diag.severity == EV_DIAG_ERROR) error_diag_count++;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe - ok"))) saw_check_pass_cause = true;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe2 - fail"))) saw_check_fail_cause = true;
    }

    ASSERT(warning_diag_count == 3);
    ASSERT(error_diag_count == 1);
    ASSERT(!saw_check_pass_cause);
    ASSERT(!saw_check_fail_cause);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_check_pass_without_start_is_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "message(CHECK_PASS done)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"))) {
            found = true;
            break;
        }
    }
    ASSERT(found);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_deprecation_respects_control_variables) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_WARN_DEPRECATED FALSE)\n"
        "message(DEPRECATION hidden)\n"
        "set(CMAKE_WARN_DEPRECATED TRUE)\n"
        "message(DEPRECATION shown)\n"
        "set(CMAKE_ERROR_DEPRECATED TRUE)\n"
        "message(DEPRECATION err)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    bool saw_hidden = false;
    bool saw_shown_warn = false;
    bool saw_err_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("hidden"))) saw_hidden = true;
        if (ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("shown"))) {
            saw_shown_warn = true;
        }
        if (ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err"))) {
            saw_err_error = true;
        }
    }
    ASSERT(!saw_hidden);
    ASSERT(saw_shown_warn);
    ASSERT(saw_err_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_configure_log_persists_yaml_file) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(CHECK_START feature-probe)\n"
        "message(CONFIGURE_LOG probe-start)\n"
        "message(CHECK_PASS yes)\n"
        "message(CONFIGURE_LOG probe-end)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    String_View log_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "./CMakeFiles/CMakeConfigureLog.yaml", &log_text));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("kind: \"message-v1\"")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-start")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-end")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("feature-probe")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_and_unset_env_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(ENV{NOBIFY_ENV_A} valueA)\n"
        "set(ENV{NOBIFY_ENV_A})\n"
        "set(ENV{NOBIFY_ENV_B} valueB ignored)\n"
        "add_executable(env_forms main.c)\n"
        "target_compile_definitions(env_forms PRIVATE A=$ENV{NOBIFY_ENV_A} B=$ENV{NOBIFY_ENV_B})\n"
        "unset(ENV{NOBIFY_ENV_B})\n"
        "target_compile_definitions(env_forms PRIVATE B2=$ENV{NOBIFY_ENV_B})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_extra_args_warn = false;
    bool saw_a_empty = false;
    bool saw_b_value = false;
    bool saw_b2_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"))) {
            saw_extra_args_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("A="))) saw_a_empty = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B=valueB"))) saw_b_value = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B2="))) saw_b2_empty = true;
    }
    ASSERT(saw_extra_args_warn);
    ASSERT(saw_a_empty);
    ASSERT(saw_b_value);
    ASSERT(saw_b2_empty);

    const char *env_b = getenv("NOBIFY_ENV_B");
    ASSERT(env_b == NULL);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_process_env_service_overlays_execute_process_and_timestamp) {
#if defined(_WIN32)
    char cmd_path[_TINYDIR_PATH_MAX] = {0};
    if (!test_ws_host_program_in_path("cmd", cmd_path)) {
        TEST_SKIP("requires cmd on PATH");
    }
#else
    if (!test_ws_host_path_exists("/bin/sh")) {
        TEST_SKIP("requires /bin/sh");
    }
#endif

    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND cmd /C \"echo %NOBIFY_ENV_CHILD_F3%\" "
        "OUTPUT_VARIABLE CHILD_B OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#else
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND /bin/sh -c \"printf '%s' $NOBIFY_ENV_CHILD_F3\" "
        "OUTPUT_VARIABLE CHILD_B)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CHILD_B")), nob_sv_from_cstr("valueB")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ENV_TS")), nob_sv_from_cstr("2000")));
    ASSERT(getenv("NOBIFY_ENV_CHILD_F3") == NULL);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(parse_direct)\n"
        "  cmake_parse_arguments(ARG \"OPT;FAST\" \"DEST\" \"TARGETS;CONFIGS\" ${ARGN})\n"
        "  list(GET ARG_TARGETS 0 ARG_T0)\n"
        "  list(GET ARG_TARGETS 1 ARG_T1)\n"
        "endmacro()\n"
        "function(parse_argv)\n"
        "  cmake_parse_arguments(PARSE_ARGV 1 FN \"FLAG\" \"ONE\" \"MULTI;MULTI\")\n"
        "  add_executable(parse_argv_t main.c)\n"
        "  list(GET FN_MULTI 0 FN_M0)\n"
        "  list(GET FN_MULTI 1 FN_M1)\n"
        "  if(DEFINED FN_ONE)\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=1)\n"
        "  else()\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=0)\n"
        "  endif()\n"
        "  target_compile_definitions(parse_argv_t PRIVATE FLAG=${FN_FLAG} M0=${FN_M0} M1=${FN_M1} UNPARSED=${FN_UNPARSED_ARGUMENTS})\n"
        "endfunction()\n"
        "parse_direct(OPT EXTRA DEST bin TARGETS a b CONFIGS)\n"
        "parse_argv(skip FLAG TAIL ONE \"\" MULTI alpha beta)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 1);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_OPT")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_FAST")), nob_sv_from_cstr("FALSE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_DEST")), nob_sv_from_cstr("bin")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_TARGETS")), nob_sv_from_cstr("a;b")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_T0")), nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_T1")), nob_sv_from_cstr("b")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_UNPARSED_ARGUMENTS")), nob_sv_from_cstr("EXTRA")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_KEYWORDS_MISSING_VALUES")), nob_sv_from_cstr("CONFIGS")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("ARG_CONFIGS")).count == 0);

    bool saw_dup_warn = false;
    bool saw_flag = false;
    bool saw_one_defined_old = false;
    bool saw_m0 = false;
    bool saw_m1 = false;
    bool saw_unparsed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("cmake_parse_arguments() keyword appears more than once across keyword lists"))) {
            saw_dup_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("parse_argv_t"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FLAG=TRUE"))) saw_flag = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ONE_DEFINED=0"))) saw_one_defined_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M0=alpha"))) saw_m0 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M1=beta"))) saw_m1 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("UNPARSED=TAIL"))) saw_unparsed = true;
    }

    ASSERT(saw_dup_warn);
    ASSERT(saw_flag);
    ASSERT(saw_one_defined_old);
    ASSERT(saw_m0);
    ASSERT(saw_m1);
    ASSERT(saw_unparsed);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_parse_arguments_rejects_invalid_shapes_and_contexts) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_parse_arguments()\n"
        "cmake_parse_arguments(PARSE_ARGV 0 BAD \"\" \"\")\n"
        "cmake_parse_arguments(PARSE_ARGV 0 BAD \"\" \"\" \"\")\n"
        "function(parse_bad_index)\n"
        "  cmake_parse_arguments(PARSE_ARGV nope BAD \"\" \"\" \"\")\n"
        "endfunction()\n"
        "parse_bad_index(alpha)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 4);

    bool saw_requires_four = false;
    bool saw_parse_argv_shape = false;
    bool saw_parse_argv_context = false;
    bool saw_parse_argv_index = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("cmake_parse_arguments"))) continue;

        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_parse_arguments() requires at least four arguments"))) {
            saw_requires_four = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires index, prefix and three keyword lists"))) {
            saw_parse_argv_shape = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) may only be used in function() scope"))) {
            saw_parse_argv_context = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("cmake_parse_arguments(PARSE_ARGV ...) requires a non-negative integer index"))) {
            saw_parse_argv_index = true;
        }
    }

    ASSERT(saw_requires_four);
    ASSERT(saw_parse_argv_shape);
    ASSERT(saw_parse_argv_context);
    ASSERT(saw_parse_argv_index);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_unset_env_rejects_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "unset(ENV{NOBIFY_ENV_OPT} CACHE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("unset(ENV{...}) does not accept options"))) {
            saw_error = true;
            break;
        }
    }
    ASSERT(saw_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_cmp0126_old_and_new_semantics) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHE_OLD local_old)\n"
        "set(CACHE_OLD cache_old CACHE STRING \"doc\")\n"
        "add_executable(cache_old_t main.c)\n"
        "target_compile_definitions(cache_old_t PRIVATE OLD_CA=${CACHE_OLD})\n"
        "cmake_policy(SET CMP0126 NEW)\n"
        "set(CACHE_NEW local_new)\n"
        "set(CACHE_NEW cache_new CACHE STRING \"doc\" FORCE)\n"
        "add_executable(cache_new_t main.c)\n"
        "target_compile_definitions(cache_new_t PRIVATE NEW_CB=${CACHE_NEW})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_binding_from_cache = false;
    bool saw_new_binding_from_local = false;
    bool saw_cache_old_set = false;
    bool saw_cache_new_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY && ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_OLD")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_old"))) {
                saw_cache_old_set = true;
            }
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_NEW")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_new"))) {
                saw_cache_new_set = true;
            }
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_CA=cache_old"))) {
            saw_old_binding_from_cache = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_CB=local_new"))) {
            saw_new_binding_from_local = true;
        }
    }
    ASSERT(saw_cache_old_set);
    ASSERT(saw_cache_new_set);
    ASSERT(saw_old_binding_from_cache);
    ASSERT(saw_new_binding_from_local);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_policy_version_defaults_cmp0126_to_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CACHE_VER local_ver)\n"
        "set(CACHE_VER cache_ver CACHE STRING \"doc\")\n"
        "add_executable(cache_ver_t main.c)\n"
        "target_compile_definitions(cache_ver_t PRIVATE VER=${CACHE_VER})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_cache_ver_set = false;
    bool saw_local_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_VER")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_ver"))) {
            saw_cache_ver_set = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VER=local_ver"))) {
            saw_local_binding = true;
        }
    }
    ASSERT(saw_cache_ver_set);
    ASSERT(saw_local_binding);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_var_commands_reject_invalid_option_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BAD_CACHE value CACHE STRING)\n"
        "set(BAD_TYPE value CACHE INVALID \"doc\")\n"
        "set(BAD_TAIL value CACHE STRING \"doc\" EXTRA)\n"
        "option()\n"
        "option(\"\" \"doc\")\n"
        "mark_as_advanced(FORCE)\n"
        "unset(BAD_VAR BAD_OPTION)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 7);

    bool saw_set_missing_doc = false;
    bool saw_set_invalid_type = false;
    bool saw_set_trailing = false;
    bool saw_option_shape = false;
    bool saw_option_empty_name = false;
    bool saw_mark_missing = false;
    bool saw_unset_unsupported = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(... CACHE ...) requires <type> and <docstring>"))) {
            saw_set_missing_doc = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(... CACHE ...) received invalid cache type"))) {
            saw_set_invalid_type = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(... CACHE ...) received unsupported trailing arguments"))) {
            saw_set_trailing = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("option() requires <variable> <help_text> [value]"))) {
            saw_option_shape = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("option() requires a non-empty variable name"))) {
            saw_option_empty_name = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("mark_as_advanced() requires at least one variable name"))) {
            saw_mark_missing = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("unset() received unsupported option"))) {
            saw_unset_unsupported = true;
        }
    }

    ASSERT(saw_set_missing_doc);
    ASSERT(saw_set_invalid_type);
    ASSERT(saw_set_trailing);
    ASSERT(saw_option_shape);
    ASSERT(saw_option_empty_name);
    ASSERT(saw_mark_missing);
    ASSERT(saw_unset_unsupported);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}


void run_evaluator_v2_batch4(int *passed, int *failed, int *skipped) {
    test_evaluator_load_cache_supports_documented_legacy_and_prefix_forms(passed, failed, skipped);
    test_evaluator_load_cache_allows_read_with_prefix_but_rejects_legacy_form_in_script_mode(passed, failed, skipped);
    test_evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file(passed, failed, skipped);
    test_evaluator_export_rejects_invalid_extension_and_alias_targets(passed, failed, skipped);
    test_evaluator_ctest_family_models_metadata_and_safe_local_effects(passed, failed, skipped);
    test_evaluator_ctest_start_models_documented_group_append_and_checkout_flow(passed, failed, skipped);
    test_evaluator_ctest_configure_executes_documented_command_and_stages_submit_part(passed, failed, skipped);
    test_evaluator_ctest_configure_captures_missing_command_without_fatal_error(passed, failed, skipped);
    test_evaluator_ctest_configure_uses_documented_ctest_directory_defaults_without_start(passed, failed, skipped);
    test_evaluator_ctest_build_executes_documented_command_and_stages_submit_part(passed, failed, skipped);
    test_evaluator_ctest_test_executes_plan_and_stages_test_xml_without_submitting_junit(passed, failed, skipped);
    test_evaluator_ctest_coverage_executes_documented_command_order_and_stages_submit_part(passed, failed, skipped);
    test_evaluator_ctest_update_executes_documented_command_and_stages_submit_part(passed, failed, skipped);
    test_evaluator_ctest_coverage_captures_missing_command_without_fatal_error(passed, failed, skipped);
    test_evaluator_ctest_run_script_returns_last_script_status(passed, failed, skipped);
    test_evaluator_ctest_memcheck_executes_backend_and_stages_submit_part(passed, failed, skipped);
    test_evaluator_test_workspace_golden_updates_target_repo_root(passed, failed, skipped);
    test_evaluator_ctest_submit_models_documented_local_surface(passed, failed, skipped);
    test_evaluator_ctest_submit_models_cdash_upload_signature(passed, failed, skipped);
    test_evaluator_ctest_submit_captures_remote_failures(passed, failed, skipped);
    test_evaluator_ctest_upload_stages_upload_xml_and_submit_part(passed, failed, skipped);
    test_evaluator_ctest_submit_rejects_invalid_parts_and_mixed_signatures(passed, failed, skipped);
    test_evaluator_ctest_family_rejects_invalid_and_unsupported_forms(passed, failed, skipped);
    test_evaluator_ctest_memcheck_rejects_invalid_documented_shapes(passed, failed, skipped);
    test_evaluator_ctest_entrypoints_reject_incomplete_argument_shapes(passed, failed, skipped);
    test_evaluator_batch8_legacy_commands_register_and_model_compat_paths(passed, failed, skipped);
    test_evaluator_batch8_legacy_commands_reject_invalid_forms(passed, failed, skipped);
    test_evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements(passed, failed, skipped);
    test_evaluator_target_usage_commands_store_canonical_direct_and_interface_properties(passed, failed, skipped);
    test_evaluator_source_group_supports_files_tree_and_regex_forms(passed, failed, skipped);
    test_evaluator_message_mode_severity_mapping(passed, failed, skipped);
    test_evaluator_message_check_pass_without_start_is_error(passed, failed, skipped);
    test_evaluator_message_deprecation_respects_control_variables(passed, failed, skipped);
    test_evaluator_message_configure_log_persists_yaml_file(passed, failed, skipped);
    test_evaluator_set_and_unset_env_forms(passed, failed, skipped);
    test_evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms(passed, failed, skipped);
    test_evaluator_cmake_parse_arguments_rejects_invalid_shapes_and_contexts(passed, failed, skipped);
    test_evaluator_unset_env_rejects_options(passed, failed, skipped);
    test_evaluator_set_cache_cmp0126_old_and_new_semantics(passed, failed, skipped);
    test_evaluator_set_cache_policy_version_defaults_cmp0126_to_new(passed, failed, skipped);
    test_evaluator_var_commands_reject_invalid_option_shapes(passed, failed, skipped);
}

void run_evaluator_v2_integration_batch4(int *passed, int *failed, int *skipped) {
    test_evaluator_process_env_service_overlays_execute_process_and_timestamp(passed, failed, skipped);
}
