// --- START OF FILE eval_expr.h ---
#ifndef EVAL_EXPR_H_
#define EVAL_EXPR_H_

#include <stdbool.h>
#include "nob.h"
#include "parser.h"

struct Evaluator_Context;

// Expansão de Variáveis:
// Resolve ${VAR}, ${${VAR}}, $ENV{VAR}, \${VAR}
// Retorna uma String_View temporária alocada na TEMP ARENA (ctx->arena).
String_View eval_expand_vars(struct Evaluator_Context *ctx, String_View input);

// Avaliação de if() / while().
// Retorna false em erro de sintaxe e emite EV_DIAGNOSTIC.
bool eval_condition(struct Evaluator_Context *ctx, const Args *raw_condition);

// Lógica de "Truthiness" do CMake (v2 spec)
// Requer o contexto pois faz fallback para lookup de variável.
bool eval_truthy(struct Evaluator_Context *ctx, String_View v);

#endif // EVAL_EXPR_H_