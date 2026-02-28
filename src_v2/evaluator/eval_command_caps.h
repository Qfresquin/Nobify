#ifndef EVAL_COMMAND_CAPS_H_
#define EVAL_COMMAND_CAPS_H_

#include "evaluator.h"

typedef struct {
    const char *name;
    Eval_Command_Impl_Level level;
    Eval_Command_Fallback fallback;
} Eval_Command_Cap_Entry;

bool eval_command_caps_lookup(String_View name, Command_Capability *out_capability);

#endif // EVAL_COMMAND_CAPS_H_
