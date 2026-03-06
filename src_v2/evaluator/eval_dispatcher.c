#include "eval_dispatcher.h"

#include "evaluator_internal.h"
#include "eval_cmake_path.h"
#include "eval_cpack.h"
#include "eval_ctest.h"
#include "eval_custom.h"
#include "eval_diag.h"
#include "eval_directory.h"
#include "eval_file.h"
#include "eval_flow.h"
#include "eval_host.h"
#include "eval_include.h"
#include "eval_install.h"
#include "eval_legacy.h"
#include "eval_meta.h"
#include "eval_package.h"
#include "eval_project.h"
#include "eval_stdlib.h"
#include "eval_target.h"
#include "eval_test.h"
#include "eval_try_compile.h"
#include "eval_vars.h"
#include "eval_command_caps.h"
#include "eval_command_registry.h"

bool eval_dispatcher_seed_builtin_commands(Evaluator_Context *ctx) {
    if (!ctx) return false;
#define SEED_BUILTIN(cmd_name, cmd_handler, cmd_level, cmd_fallback)                                  \
    do {                                                                                               \
        Evaluator_Native_Command_Def def = {0};                                                       \
        def.name = nob_sv_from_cstr(cmd_name);                                                         \
        def.handler = (cmd_handler);                                                                   \
        def.implemented_level = (cmd_level);                                                           \
        def.fallback_behavior = (cmd_fallback);                                                        \
        if (!eval_native_cmd_register_internal(ctx, &def, true, true)) return false;                  \
    } while (0);
    EVAL_COMMAND_REGISTRY(SEED_BUILTIN);
#undef SEED_BUILTIN
    return true;
}

bool eval_dispatcher_get_command_capability(const Evaluator_Context *ctx,
                                            String_View name,
                                            Command_Capability *out_capability) {
    return eval_command_caps_lookup(ctx, name, out_capability);
}

bool eval_dispatcher_is_known_command(const Evaluator_Context *ctx, String_View name) {
    return eval_native_cmd_find_const(ctx, name) != NULL;
}

Eval_Result eval_dispatch_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node || node->kind != NODE_COMMAND) return eval_result_fatal();
    Eval_Runtime_State *runtime = eval_runtime_slice(ctx);
    Event_Origin o = eval_origin_from_node(ctx, node);
    uint32_t argc = (uint32_t) arena_arr_len(node->as.cmd.args);

    const Eval_Native_Command *native = eval_native_cmd_find_const(ctx, node->as.cmd.name);
    if (native) {
        if (!eval_emit_command_begin(ctx,
                                     o,
                                     node->as.cmd.name,
                                     EVENT_COMMAND_DISPATCH_BUILTIN,
                                     argc)) {
            return eval_result_fatal();
        }
        size_t error_count_before = runtime->run_report.error_count;
        Eval_Result native_result = native->handler(ctx, node);
        Eval_Result running_result = eval_result_ok_if_running(ctx);
        bool native_succeeded = !eval_result_is_fatal(native_result) &&
                                !eval_result_is_fatal(running_result) &&
                                runtime->run_report.error_count == error_count_before;
        if (!eval_emit_command_end(ctx,
                                   o,
                                   node->as.cmd.name,
                                   EVENT_COMMAND_DISPATCH_BUILTIN,
                                   argc,
                                   native_succeeded ? EVENT_COMMAND_STATUS_SUCCESS
                                                    : EVENT_COMMAND_STATUS_ERROR)) {
            return eval_result_fatal();
        }
        return eval_result_merge(native_result, running_result);
    }

    User_Command *user = eval_user_cmd_find(ctx, node->as.cmd.name);
    if (user) {
        Event_Command_Dispatch_Kind dispatch_kind =
            (user->kind == USER_CMD_MACRO) ? EVENT_COMMAND_DISPATCH_MACRO
                                           : EVENT_COMMAND_DISPATCH_FUNCTION;
        if (!eval_emit_command_begin(ctx, o, node->as.cmd.name, dispatch_kind, argc)) {
            return eval_result_fatal();
        }
        SV_List args = (user->kind == USER_CMD_MACRO)
            ? eval_resolve_args_literal(ctx, &node->as.cmd.args)
            : eval_resolve_args(ctx, &node->as.cmd.args);
        if (eval_should_stop(ctx)) {
            (void)eval_emit_command_end(ctx,
                                        o,
                                        node->as.cmd.name,
                                        dispatch_kind,
                                        argc,
                                        EVENT_COMMAND_STATUS_ERROR);
            return eval_result_fatal();
        }
        size_t error_count_before = runtime->run_report.error_count;
        if (eval_user_cmd_invoke(ctx, node->as.cmd.name, &args, o)) {
            bool user_succeeded = !eval_result_is_fatal(eval_result_ok_if_running(ctx)) &&
                                  runtime->run_report.error_count == error_count_before;
            if (!eval_emit_command_end(ctx,
                                       o,
                                       node->as.cmd.name,
                                       dispatch_kind,
                                       argc,
                                       user_succeeded ? EVENT_COMMAND_STATUS_SUCCESS
                                                      : EVENT_COMMAND_STATUS_ERROR)) {
                return eval_result_fatal();
            }
            return eval_result_ok_if_running(ctx);
        }
        if (!eval_emit_command_end(ctx,
                                   o,
                                   node->as.cmd.name,
                                   dispatch_kind,
                                   argc,
                                   EVENT_COMMAND_STATUS_ERROR)) {
            return eval_result_fatal();
        }
        return eval_result_from_ctx(ctx);
    }

    if (!eval_emit_command_begin(ctx,
                                 o,
                                 node->as.cmd.name,
                                 EVENT_COMMAND_DISPATCH_UNKNOWN,
                                 argc)) {
        return eval_result_fatal();
    }
    if (!eval_emit_command_end(ctx,
                               o,
                               node->as.cmd.name,
                               EVENT_COMMAND_DISPATCH_UNKNOWN,
                               argc,
                               EVENT_COMMAND_STATUS_UNSUPPORTED)) {
        return eval_result_fatal();
    }

    Event_Diag_Severity sev = EV_DIAG_WARNING;
    if (runtime->unsupported_policy == EVAL_UNSUPPORTED_ERROR) sev = EV_DIAG_ERROR;
    return EVAL_DIAG_RESULT_SEV(ctx,
                                sev,
                                EVAL_DIAG_UNKNOWN_COMMAND,
                                nob_sv_from_cstr("dispatcher"),
                                node->as.cmd.name,
                                o,
                                nob_sv_from_cstr("Unknown command"),
                                runtime->unsupported_policy == EVAL_UNSUPPORTED_NOOP_WARN
                                    ? nob_sv_from_cstr("No-op with warning by policy")
                                    : nob_sv_from_cstr("Ignored during evaluation"));
}
