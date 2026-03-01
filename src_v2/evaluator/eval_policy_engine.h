#ifndef EVAL_POLICY_ENGINE_H_
#define EVAL_POLICY_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>

#include "nob.h"

typedef enum {
    POLICY_STATUS_UNSET = 0,
    POLICY_STATUS_OLD,
    POLICY_STATUS_NEW,
} Eval_Policy_Status;

String_View eval_policy_status_to_sv(Eval_Policy_Status status);
bool eval_policy_is_id(String_View policy_id);
bool eval_policy_is_known(String_View policy_id);
bool eval_policy_get_intro_version(String_View policy_id, int *major, int *minor, int *patch);
int eval_policy_known_min_id(void);
int eval_policy_known_max_id(void);
size_t eval_policy_known_count(void);

#endif // EVAL_POLICY_ENGINE_H_
