#ifndef EVAL_OPT_PARSER_H_
#define EVAL_OPT_PARSER_H_

#include "evaluator_internal.h"

typedef enum {
    EVAL_OPT_FLAG = 0,
    EVAL_OPT_SINGLE,
    EVAL_OPT_OPTIONAL_SINGLE,
    EVAL_OPT_MULTI,
} Eval_Opt_Kind;

typedef struct {
    int id;
    const char *name;
    Eval_Opt_Kind kind;
} Eval_Opt_Spec;

typedef struct {
    Cmake_Event_Origin origin;
    String_View component;
    String_View command;
    bool unknown_as_positional;
    bool warn_unknown;
} Eval_Opt_Parse_Config;

typedef bool (*Eval_Opt_On_Option_Fn)(Evaluator_Context *ctx,
                                      void *userdata,
                                      int id,
                                      SV_List values,
                                      size_t token_index);

typedef bool (*Eval_Opt_On_Positional_Fn)(Evaluator_Context *ctx,
                                          void *userdata,
                                          String_View value,
                                          size_t token_index);

bool eval_opt_parse_walk(Evaluator_Context *ctx,
                         SV_List args,
                         size_t start,
                         const Eval_Opt_Spec *specs,
                         size_t spec_count,
                         Eval_Opt_Parse_Config cfg,
                         Eval_Opt_On_Option_Fn on_option,
                         Eval_Opt_On_Positional_Fn on_positional,
                         void *userdata);

bool eval_opt_token_is_keyword(String_View tok,
                               const Eval_Opt_Spec *specs,
                               size_t spec_count);

#endif // EVAL_OPT_PARSER_H_
