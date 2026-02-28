#ifndef EVAL_POLICY_ENGINE_H_
#define EVAL_POLICY_ENGINE_H_

#include <stdbool.h>

#include "nob.h"

typedef enum {
    POLICY_STATUS_UNSET = 0,
    POLICY_STATUS_OLD,
    POLICY_STATUS_NEW,
} Eval_Policy_Status;

typedef enum {
    EVAL_POLICY_SCOPE_FLOW_BLOCK = 0,
} Eval_Policy_Scope;

typedef enum {
    EVAL_POLICY_IMPL_SUPPORTED = 0,
    EVAL_POLICY_IMPL_PARTIAL,
} Eval_Policy_Impl_Status;

typedef struct {
    const char *policy_id;
    Eval_Policy_Status default_before_version;
    Eval_Policy_Status default_at_or_after_version;
    Eval_Policy_Scope scope;
    Eval_Policy_Impl_Status status;
    int switch_major;
    int switch_minor;
    int switch_patch;
} Eval_Policy_Default_Entry;

bool eval_policy_get_default_entry(String_View policy_id, Eval_Policy_Default_Entry *out_entry);
bool eval_policy_is_supported_flow_block(String_View policy_id);
String_View eval_policy_status_to_sv(Eval_Policy_Status status);

#endif // EVAL_POLICY_ENGINE_H_
