// --- START OF FILE eval_dispatcher.h ---
#ifndef EVAL_DISPATCHER_H_
#define EVAL_DISPATCHER_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

// Ponto de entrada principal para roteamento de comandos CMake.
// Falhas graves retornam EVAL_RESULT_FATAL. Erros semânticos não fatais retornam EVAL_RESULT_SOFT_ERROR.
Eval_Result eval_dispatch_command(struct Evaluator_Context *ctx, const Node *node);

// Usado pelo eval_expr.c para o predicado: if(COMMAND nome)
bool eval_dispatcher_is_known_command(const struct Evaluator_Context *ctx, String_View name);
bool eval_dispatcher_get_command_capability(const struct Evaluator_Context *ctx,
                                            String_View name,
                                            Command_Capability *out_capability);

// Semeia os comandos nativos built-in no contexto recém-criado.
bool eval_dispatcher_seed_builtin_commands(EvalRegistry *registry);

#endif // EVAL_DISPATCHER_H_
