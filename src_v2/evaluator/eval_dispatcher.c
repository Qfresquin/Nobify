#include "eval_dispatcher.h"

#include "evaluator_internal.h"
#include "eval_cmake_path.h"
#include "eval_cpack.h"
#include "eval_custom.h"
#include "eval_diag.h"
#include "eval_directory.h"
#include "eval_file.h"
#include "eval_flow.h"
#include "eval_host.h"
#include "eval_include.h"
#include "eval_install.h"
#include "eval_package.h"
#include "eval_project.h"
#include "eval_stdlib.h"
#include "eval_target.h"
#include "eval_test.h"
#include "eval_try_compile.h"
#include "eval_vars.h"
#include "eval_command_caps.h"

typedef bool (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node);

typedef struct {
    const char *name;
    Cmd_Handler fn;
} Command_Entry;

static const Command_Entry DISPATCH[] = {
    {"add_compile_definitions", eval_handle_add_compile_definitions},
    {"add_dependencies", eval_handle_add_dependencies},
    {"add_compile_options", eval_handle_add_compile_options},
    {"add_custom_command", eval_handle_add_custom_command},
    {"add_custom_target", eval_handle_add_custom_target},
    {"add_definitions", eval_handle_add_definitions},
    {"add_executable", eval_handle_add_executable},
    {"add_library", eval_handle_add_library},
    {"add_link_options", eval_handle_add_link_options},
    {"add_subdirectory", eval_handle_add_subdirectory},
    {"add_test", eval_handle_add_test},
    {"block", eval_handle_block},
    {"break", eval_handle_break},
    {"build_command", eval_handle_build_command},
    {"build_name", eval_handle_build_name},
    {"cmake_host_system_information", eval_handle_cmake_host_system_information},
    {"cmake_parse_arguments", eval_handle_cmake_parse_arguments},
    {"cmake_minimum_required", eval_handle_cmake_minimum_required},
    {"cmake_language", eval_handle_cmake_language},
    {"cmake_path", eval_handle_cmake_path},
    {"cmake_policy", eval_handle_cmake_policy},
    {"configure_file", eval_handle_configure_file},
    {"continue", eval_handle_continue},
    {"cpack_add_component", eval_handle_cpack_add_component},
    {"cpack_add_component_group", eval_handle_cpack_add_component_group},
    {"cpack_add_install_type", eval_handle_cpack_add_install_type},
    {"define_property", eval_handle_define_property},
    {"enable_language", eval_handle_enable_language},
    {"enable_testing", eval_handle_enable_testing},
    {"endblock", eval_handle_endblock},
    {"exec_program", eval_handle_exec_program},
    {"execute_process", eval_handle_execute_process},
    {"file", eval_handle_file},
    {"get_cmake_property", eval_handle_get_cmake_property},
    {"get_directory_property", eval_handle_get_directory_property},
    {"find_file", eval_handle_find_file},
    {"find_library", eval_handle_find_library},
    {"find_path", eval_handle_find_path},
    {"find_package", eval_handle_find_package},
    {"find_program", eval_handle_find_program},
    {"get_filename_component", eval_handle_get_filename_component},
    {"get_property", eval_handle_get_property},
    {"get_source_file_property", eval_handle_get_source_file_property},
    {"get_target_property", eval_handle_get_target_property},
    {"get_test_property", eval_handle_get_test_property},
    {"include", eval_handle_include},
    {"include_regular_expression", eval_handle_include_regular_expression},
    {"include_directories", eval_handle_include_directories},
    {"include_guard", eval_handle_include_guard},
    {"install", eval_handle_install},
    {"link_directories", eval_handle_link_directories},
    {"link_libraries", eval_handle_link_libraries},
    {"list", eval_handle_list},
    {"mark_as_advanced", eval_handle_mark_as_advanced},
    {"math", eval_handle_math},
    {"message", eval_handle_message},
    {"option", eval_handle_option},
    {"project", eval_handle_project},
    {"remove_definitions", eval_handle_remove_definitions},
    {"return", eval_handle_return},
    {"separate_arguments", eval_handle_separate_arguments},
    {"set", eval_handle_set},
    {"set_directory_properties", eval_handle_set_directory_properties},
    {"set_property", eval_handle_set_property},
    {"set_source_files_properties", eval_handle_set_source_files_properties},
    {"set_target_properties", eval_handle_set_target_properties},
    {"set_tests_properties", eval_handle_set_tests_properties},
    {"site_name", eval_handle_site_name},
    {"string", eval_handle_string},
    {"source_group", eval_handle_source_group},
    {"target_compile_features", eval_handle_target_compile_features},
    {"target_compile_definitions", eval_handle_target_compile_definitions},
    {"target_compile_options", eval_handle_target_compile_options},
    {"target_include_directories", eval_handle_target_include_directories},
    {"target_link_directories", eval_handle_target_link_directories},
    {"target_link_libraries", eval_handle_target_link_libraries},
    {"target_link_options", eval_handle_target_link_options},
    {"target_precompile_headers", eval_handle_target_precompile_headers},
    {"target_sources", eval_handle_target_sources},
    {"try_compile", eval_handle_try_compile},
    {"try_run", eval_handle_try_run},
    {"unset", eval_handle_unset},
};
static const size_t DISPATCH_COUNT = sizeof(DISPATCH) / sizeof(DISPATCH[0]);

bool eval_dispatcher_get_command_capability(String_View name, Command_Capability *out_capability) {
    return eval_command_caps_lookup(name, out_capability);
}

bool eval_dispatcher_is_known_command(String_View name) {
    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(name, DISPATCH[i].name)) return true;
    }
    return false;
}

bool eval_dispatch_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node || node->kind != NODE_COMMAND) return false;

    for (size_t i = 0; i < DISPATCH_COUNT; i++) {
        if (eval_sv_eq_ci_lit(node->as.cmd.name, DISPATCH[i].name)) {
            if (!DISPATCH[i].fn(ctx, node)) return false;
            return !eval_should_stop(ctx);
        }
    }

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    eval_refresh_runtime_compat(ctx);
    User_Command *user = eval_user_cmd_find(ctx, node->as.cmd.name);
    if (user) {
        SV_List args = (user->kind == USER_CMD_MACRO)
            ? eval_resolve_args_literal(ctx, &node->as.cmd.args)
            : eval_resolve_args(ctx, &node->as.cmd.args);
        if (eval_should_stop(ctx)) return false;
        if (eval_user_cmd_invoke(ctx, node->as.cmd.name, &args, o)) {
            return true;
        }
        return !eval_should_stop(ctx);
    }

    Cmake_Diag_Severity sev = EV_DIAG_WARNING;
    if (ctx->unsupported_policy == EVAL_UNSUPPORTED_ERROR) sev = EV_DIAG_ERROR;
    eval_emit_diag(ctx,
                   sev,
                   nob_sv_from_cstr("dispatcher"),
                   node->as.cmd.name,
                   o,
                   nob_sv_from_cstr("Unknown command"),
                   ctx->unsupported_policy == EVAL_UNSUPPORTED_NOOP_WARN
                       ? nob_sv_from_cstr("No-op with warning by policy")
                       : nob_sv_from_cstr("Ignored during evaluation"));
    return true;
}
