#include "eval_command_caps.h"

#include "evaluator_internal.h"

static Command_Capability eval_capability_make(String_View name,
                                               Eval_Command_Impl_Level level,
                                               Eval_Command_Fallback fallback) {
    Command_Capability c = {0};
    c.command_name = name;
    c.implemented_level = level;
    c.fallback_behavior = fallback;
    return c;
}

// Central capability registry consumed by dispatcher, API and docs.
static const Eval_Command_Cap_Entry COMMAND_CAPS[] = {
    {"add_compile_options", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_custom_command", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"add_custom_target", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_definitions", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_executable", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_library", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_link_options", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_subdirectory", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"add_test", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"block", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"break", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"cmake_minimum_required", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"cmake_path", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"cmake_policy", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"continue", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"cpack_add_component", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"cpack_add_component_group", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"cpack_add_install_type", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"enable_testing", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"endblock", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"file", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"find_package", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"include", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"include_directories", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"include_guard", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"install", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"link_directories", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"link_libraries", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"list", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"math", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"message", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"project", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"return", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"set", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"set_property", EVAL_CMD_IMPL_PARTIAL, EVAL_FALLBACK_ERROR_CONTINUE},
    {"set_target_properties", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"string", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_compile_definitions", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_compile_options", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_include_directories", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_link_directories", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_link_libraries", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"target_link_options", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"try_compile", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
    {"unset", EVAL_CMD_IMPL_FULL, EVAL_FALLBACK_NOOP_WARN},
};
static const size_t COMMAND_CAPS_COUNT = sizeof(COMMAND_CAPS) / sizeof(COMMAND_CAPS[0]);

bool eval_command_caps_lookup(String_View name, Command_Capability *out_capability) {
    if (!out_capability) return false;
    for (size_t i = 0; i < COMMAND_CAPS_COUNT; i++) {
        if (!eval_sv_eq_ci_lit(name, COMMAND_CAPS[i].name)) continue;
        *out_capability = eval_capability_make(name, COMMAND_CAPS[i].level, COMMAND_CAPS[i].fallback);
        return true;
    }
    *out_capability = eval_capability_make(name, EVAL_CMD_IMPL_MISSING, EVAL_FALLBACK_NOOP_WARN);
    return false;
}
