#include "eval_host.h"

#include "evaluator_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void nob__cmd_append(Nob_Cmd *cmd, size_t n, ...);

typedef struct {
    String_View config;
    String_View parallel_level;
    String_View target;
    String_View project_name;
    bool saw_project_name;
} Build_Command_Options;

static bool host_capture_command_stdout(Evaluator_Context *ctx, String_View command, String_View *out_text) {
    if (!ctx || !out_text) return false;
    *out_text = nob_sv_from_cstr("");
    if (command.count == 0) return true;

    static size_t s_capture_counter = 0;
    s_capture_counter++;

    String_View current_bin = eval_var_get(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_BINARY_DIR));
    if (current_bin.count == 0) current_bin = ctx->binary_dir;

    String_View file_name = sv_copy_to_temp_arena(
        ctx,
        nob_sv_from_cstr(nob_temp_sprintf("site_name_capture_%zu.txt", s_capture_counter)));
    if (eval_should_stop(ctx)) return false;

    String_View out_path = eval_sv_path_join(eval_temp_arena(ctx), current_bin, file_name);
    if (eval_should_stop(ctx)) return false;

    char *command_c = eval_sv_to_cstr_temp(ctx, command);
    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, command_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, command_c);
    bool ok = nob_cmd_run(&cmd, .stdout_path = out_c);
    nob_cmd_free(cmd);
    if (!ok) return true;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(out_c, &sb)) return true;

    size_t len = sb.count;
    while (len > 0 && (sb.items[len - 1] == '\n' || sb.items[len - 1] == '\r')) len--;
    *out_text = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, len));
    nob_sb_free(sb);
    return !eval_should_stop(ctx);
}

static String_View host_format_size_t_temp(Evaluator_Context *ctx, size_t value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%zu", value);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
}

static String_View host_format_ull_temp(Evaluator_Context *ctx, unsigned long long value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%llu", value);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
}

static bool host_is_makefile_generator(String_View generator) {
    return eval_sv_eq_ci_lit(generator, "Unix Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MinGW Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MSYS Makefiles") ||
           eval_sv_eq_ci_lit(generator, "NMake Makefiles") ||
           eval_sv_eq_ci_lit(generator, "Watcom WMake") ||
           eval_sv_eq_ci_lit(generator, "Borland Makefiles");
}

static bool host_build_command_parse(Evaluator_Context *ctx,
                                     const Node *node,
                                     const SV_List *args,
                                     String_View *out_var,
                                     Build_Command_Options *out_opt) {
    if (!ctx || !node || !args || !out_var || !out_opt) return false;
    *out_var = nob_sv_from_cstr("");
    memset(out_opt, 0, sizeof(*out_opt));

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*args) == 0) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("build_command() requires an output variable"),
                             nob_sv_from_cstr("Usage: build_command(<out-var> [CONFIGURATION <cfg>] [TARGET <tgt>])"));
        return false;
    }

    *out_var = (*args)[0];

    size_t index = 1;
    size_t positional_count = 0;
    while (index < arena_arr_len(*args)) {
        String_View tok = (*args)[index];
        if (eval_sv_eq_ci_lit(tok, "CONFIGURATION") ||
            eval_sv_eq_ci_lit(tok, "PARALLEL_LEVEL") ||
            eval_sv_eq_ci_lit(tok, "PROJECT_NAME") ||
            eval_sv_eq_ci_lit(tok, "TARGET")) {
            break;
        }
        positional_count++;
        index++;
    }

    if (positional_count > 3) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("build_command() received too many positional arguments"),
                             nob_sv_from_cstr("Supported legacy positionals: <makecommand> [makefile_name] [target]"));
        return false;
    }

    if (positional_count == 3) out_opt->target = (*args)[3];

    while (index < arena_arr_len(*args)) {
        String_View key = (*args)[index++];
        if (index >= arena_arr_len(*args)) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("host"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("build_command() keyword requires a value"),
                                 key);
            return false;
        }
        String_View value = (*args)[index++];

        if (eval_sv_eq_ci_lit(key, "CONFIGURATION")) {
            out_opt->config = value;
        } else if (eval_sv_eq_ci_lit(key, "PARALLEL_LEVEL")) {
            out_opt->parallel_level = value;
        } else if (eval_sv_eq_ci_lit(key, "TARGET")) {
            out_opt->target = value;
        } else if (eval_sv_eq_ci_lit(key, "PROJECT_NAME")) {
            out_opt->project_name = value;
            out_opt->saw_project_name = true;
        } else {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("host"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("build_command() received an unsupported argument"),
                                 key);
            return false;
        }
    }

    return true;
}

static String_View host_build_command_text_temp(Evaluator_Context *ctx,
                                                String_View command_name,
                                                const Build_Command_Options *opt,
                                                bool append_make_i) {
    if (!ctx || !opt) return nob_sv_from_cstr("");
    if (command_name.count == 0) command_name = nob_sv_from_cstr("cmake");

    size_t total = command_name.count + sizeof(" --build .") - 1;
    if (opt->target.count > 0) total += sizeof(" --target ") - 1 + opt->target.count;
    if (opt->config.count > 0) total += sizeof(" --config ") - 1 + opt->config.count;
    if (opt->parallel_level.count > 0) total += sizeof(" --parallel ") - 1 + opt->parallel_level.count;
    if (append_make_i) total += sizeof(" -- -i") - 1;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, command_name.data, command_name.count);
    off += command_name.count;

    memcpy(buf + off, " --build .", sizeof(" --build .") - 1);
    off += sizeof(" --build .") - 1;

    if (opt->target.count > 0) {
        memcpy(buf + off, " --target ", sizeof(" --target ") - 1);
        off += sizeof(" --target ") - 1;
        memcpy(buf + off, opt->target.data, opt->target.count);
        off += opt->target.count;
    }
    if (opt->config.count > 0) {
        memcpy(buf + off, " --config ", sizeof(" --config ") - 1);
        off += sizeof(" --config ") - 1;
        memcpy(buf + off, opt->config.data, opt->config.count);
        off += opt->config.count;
    }
    if (opt->parallel_level.count > 0) {
        memcpy(buf + off, " --parallel ", sizeof(" --parallel ") - 1);
        off += sizeof(" --parallel ") - 1;
        memcpy(buf + off, opt->parallel_level.data, opt->parallel_level.count);
        off += opt->parallel_level.count;
    }
    if (append_make_i) {
        memcpy(buf + off, " -- -i", sizeof(" -- -i") - 1);
        off += sizeof(" -- -i") - 1;
    }

    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool host_info_query_value(Evaluator_Context *ctx,
                                  String_View key,
                                  String_View *out_value,
                                  bool *out_supported) {
    if (!ctx || !out_value || !out_supported) return false;
    *out_value = nob_sv_from_cstr("");
    *out_supported = true;

    if (eval_sv_eq_ci_lit(key, "NUMBER_OF_LOGICAL_CORES")) {
        size_t count = 0;
        if (!eval_host_logical_cores(&count)) {
            *out_supported = false;
            return true;
        }
        *out_value = host_format_size_t_temp(ctx, count);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(key, "HOSTNAME")) {
        String_View hostname = nob_sv_from_cstr("");
        if (!eval_host_hostname_temp(ctx, &hostname)) return false;
        *out_value = hostname;
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(key, "TOTAL_VIRTUAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "AVAILABLE_VIRTUAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "TOTAL_PHYSICAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "AVAILABLE_PHYSICAL_MEMORY")) {
        Eval_Host_Memory_Info info = {0};
        if (!eval_host_memory_info(&info)) {
            *out_supported = false;
            return true;
        }

        unsigned long long value = 0;
        if (eval_sv_eq_ci_lit(key, "TOTAL_VIRTUAL_MEMORY")) value = info.total_virtual_mib;
        else if (eval_sv_eq_ci_lit(key, "AVAILABLE_VIRTUAL_MEMORY")) value = info.available_virtual_mib;
        else if (eval_sv_eq_ci_lit(key, "TOTAL_PHYSICAL_MEMORY")) value = info.total_physical_mib;
        else value = info.available_physical_mib;

        *out_value = host_format_ull_temp(ctx, value);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(key, "IS_64BIT")) {
        *out_value = sizeof(void*) >= 8 ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "OS_NAME")) {
        *out_value = eval_detect_host_system_name();
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "OS_RELEASE")) {
        *out_value = eval_host_os_release_temp(ctx);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(key, "OS_VERSION")) {
        *out_value = eval_host_os_version_temp(ctx);
        return !eval_should_stop(ctx);
    }
    if (eval_sv_eq_ci_lit(key, "OS_PLATFORM")) {
        *out_value = eval_detect_host_processor();
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "MSYSTEM_PREFIX")) {
#if defined(_WIN32)
        const char *value = eval_getenv_temp(ctx, "MSYSTEM_PREFIX");
        if (!value) value = "";
        *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(value));
        return !eval_should_stop(ctx);
#else
        *out_supported = false;
        return true;
#endif
    }

    *out_supported = false;
    return true;
}

bool eval_handle_cmake_host_system_information(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(args) < 4 ||
        !eval_sv_eq_ci_lit(args[0], "RESULT") ||
        !eval_sv_eq_ci_lit(args[2], "QUERY")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("cmake_host_system_information() requires RESULT and QUERY clauses"),
                             nob_sv_from_cstr("Usage: cmake_host_system_information(RESULT <var> QUERY <key>...)"));
        return !eval_should_stop(ctx);
    }

    String_View result_var = args[1];
    String_View *values = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(args) - 3);
    EVAL_OOM_RETURN_IF_NULL(ctx, values, false);

    size_t value_count = 0;
    for (size_t i = 3; i < arena_arr_len(args); i++) {
        String_View value = nob_sv_from_cstr("");
        bool supported = false;
        if (!host_info_query_value(ctx, args[i], &value, &supported)) return false;
        if (!supported) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("host"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("cmake_host_system_information() query key is not implemented yet"),
                                 args[i]);
            value = nob_sv_from_cstr("");
        }
        values[value_count++] = value;
    }

    String_View result = eval_sv_join_semi_temp(ctx, values, value_count);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set(ctx, result_var, result)) return false;
    return !eval_should_stop(ctx);
}

bool eval_handle_site_name(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(args) != 1) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("site_name() requires exactly one output variable"),
                             nob_sv_from_cstr("Usage: site_name(<out-var>)"));
        return !eval_should_stop(ctx);
    }

    String_View value = nob_sv_from_cstr("");
    String_View hostname_command = eval_var_get(ctx, nob_sv_from_cstr("HOSTNAME"));
    if (hostname_command.count > 0) {
        if (!host_capture_command_stdout(ctx, hostname_command, &value)) return false;
        if (value.count == 0) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_WARNING,
                                 nob_sv_from_cstr("host"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("site_name() HOSTNAME helper command produced no output"),
                                 hostname_command);
        }
    } else {
        if (!eval_host_hostname_temp(ctx, &value)) return false;
        if (value.count == 0) {
            (void)eval_emit_diag(ctx,
                                 EV_DIAG_WARNING,
                                 nob_sv_from_cstr("host"),
                                 node->as.cmd.name,
                                 o,
                                 nob_sv_from_cstr("site_name() could not determine host name"),
                                 nob_sv_from_cstr("Result variable set to empty string"));
        }
    }

    if (!eval_var_set(ctx, args[0], value)) return false;
    return !eval_should_stop(ctx);
}

bool eval_handle_build_name(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (arena_arr_len(args) != 1) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("build_name() requires exactly one output variable"),
                             nob_sv_from_cstr("Usage: build_name(<out-var>)"));
        return !eval_should_stop(ctx);
    }

    if (eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr(EVAL_POLICY_CMP0036)), "NEW")) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("build_name() is disallowed by CMP0036"),
                             nob_sv_from_cstr("Set CMP0036 to OLD only for legacy compatibility"));
        return !eval_should_stop(ctx);
    }

    String_View system_name = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    if (system_name.count == 0) system_name = eval_detect_host_system_name();
    String_View compiler_id = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"));
    if (compiler_id.count == 0) compiler_id = nob_sv_from_cstr("Unknown");

    size_t total = system_name.count + 1 + compiler_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, system_name.data, system_name.count);
    buf[system_name.count] = '-';
    memcpy(buf + system_name.count + 1, compiler_id.data, compiler_id.count);
    buf[total] = '\0';

    if (!eval_var_set(ctx, args[0], nob_sv_from_parts(buf, total))) return false;
    return !eval_should_stop(ctx);
}

bool eval_handle_build_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    String_View out_var = nob_sv_from_cstr("");
    Build_Command_Options opt = {0};
    if (!host_build_command_parse(ctx, node, &args, &out_var, &opt)) return !eval_should_stop(ctx);

    if (opt.saw_project_name) {
        (void)eval_emit_diag(ctx,
                             EV_DIAG_WARNING,
                             nob_sv_from_cstr("host"),
                             node->as.cmd.name,
                             o,
                             nob_sv_from_cstr("build_command(PROJECT_NAME ...) is parsed but ignored by evaluator v2"),
                             opt.project_name);
    }

    bool cmp0061_new = eval_sv_eq_ci_lit(eval_policy_get_effective(ctx, nob_sv_from_cstr(EVAL_POLICY_CMP0061)), "NEW");
    String_View generator = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_GENERATOR"));
    bool append_make_i = !cmp0061_new && host_is_makefile_generator(generator);

    String_View cmake_command = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_COMMAND"));
    String_View value = host_build_command_text_temp(ctx, cmake_command, &opt, append_make_i);
    if (eval_should_stop(ctx)) return false;

    if (!eval_var_set(ctx, out_var, value)) return false;
    return !eval_should_stop(ctx);
}
