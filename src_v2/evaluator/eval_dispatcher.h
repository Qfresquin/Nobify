// --- START OF FILE eval_dispatcher.h ---
#ifndef EVAL_DISPATCHER_H_
#define EVAL_DISPATCHER_H_

#include <stdbool.h>

#include "parser.h"
#include "evaluator.h"

// Ponto de entrada principal para roteamento de comandos CMake.
// Falhas graves (OOM) retornam false. Erros semânticos emitem EV_DIAGNOSTIC.
bool eval_dispatch_command(struct Evaluator_Context *ctx, const Node *node);

// Usado pelo eval_expr.c para o predicado: if(COMMAND nome)
bool eval_dispatcher_is_known_command(String_View name);
bool eval_dispatcher_get_command_capability(String_View name, Command_Capability *out_capability);

#endif // EVAL_DISPATCHER_H_
