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

bool eval_command_caps_lookup(const Evaluator_Context *ctx, String_View name, Command_Capability *out_capability) {
    if (!out_capability) return false;
    const Eval_Native_Command *cmd = eval_native_cmd_find_const(ctx, name);
    if (cmd) {
        *out_capability = eval_capability_make(name, cmd->implemented_level, cmd->fallback_behavior);
        return true;
    }
    *out_capability = eval_capability_make(name, EVAL_CMD_IMPL_MISSING, EVAL_FALLBACK_NOOP_WARN);
    return false;
}
