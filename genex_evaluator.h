#ifndef GENEX_EVALUATOR_H_
#define GENEX_EVALUATOR_H_

#include "build_model.h"

typedef String_View (*Genex_Target_Property_Fn)(void *ud, Build_Target *target, String_View prop_name);

typedef struct {
    Arena *arena;
    Build_Model *model;
    String_View default_config;
    Genex_Target_Property_Fn get_target_property;
    void *userdata;
} Genex_Eval_Context;

String_View genex_evaluate(const Genex_Eval_Context *ctx, String_View content);

#endif // GENEX_EVALUATOR_H_