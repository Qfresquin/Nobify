#ifndef TEST_EVALUATOR_V2_ENV_H_
#define TEST_EVALUATOR_V2_ENV_H_

#include "arena.h"
#include "arena_dyn.h"
#include "eval_expr.h"
#include "evaluator.h"
#include "evaluator_internal.h"
#include "lexer.h"
#include "parser.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    Arena *temp_arena;
    Arena *event_arena;
    Cmake_Event_Stream stream;
    Evaluator_Context *ctx;
} Eval_Test_Env;

bool env_init(Eval_Test_Env *env);
void env_free(Eval_Test_Env *env);
bool run_script(Eval_Test_Env *env, const char *script);

size_t count_events(const Eval_Test_Env *env, Cmake_Event_Kind kind);
bool has_event_item(const Eval_Test_Env *env, Cmake_Event_Kind kind, const char *item);
size_t count_target_prop_events(const Eval_Test_Env *env,
                                const char *target_name,
                                const char *key,
                                const char *value,
                                Cmake_Target_Property_Op op);
size_t count_diag_warnings_for_command(const Eval_Test_Env *env, const char *command);
size_t count_diag_errors_for_command(const Eval_Test_Env *env, const char *command);
bool has_diag_cause_contains(const Eval_Test_Env *env, Cmake_Diag_Severity sev, const char *needle);

#endif // TEST_EVALUATOR_V2_ENV_H_
