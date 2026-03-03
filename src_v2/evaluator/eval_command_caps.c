#include "eval_command_caps.h"

#include "evaluator_internal.h"
#include "eval_command_registry.h"

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
#define COMMAND_CAP_ENTRY(name, handler, level, fallback) {name, level, fallback},
    EVAL_COMMAND_REGISTRY(COMMAND_CAP_ENTRY)
#undef COMMAND_CAP_ENTRY
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
