#ifndef BUILD_MODEL_QUERY_INTERNAL_H_
#define BUILD_MODEL_QUERY_INTERNAL_H_

#include "build_model_internal.h"

#include "../genex/genex.h"

typedef enum {
    BM_EFFECTIVE_INCLUDE_DIRECTORIES = 0,
    BM_EFFECTIVE_COMPILE_DEFINITIONS,
    BM_EFFECTIVE_COMPILE_OPTIONS,
    BM_EFFECTIVE_COMPILE_FEATURES,
    BM_EFFECTIVE_LINK_LIBRARIES,
    BM_EFFECTIVE_LINK_OPTIONS,
    BM_EFFECTIVE_LINK_DIRECTORIES,
} BM_Effective_Query_Kind;

typedef struct {
    const Build_Model *model;
    const BM_Query_Eval_Context *eval_ctx;
    Arena *scratch;
    BM_Target_Id consumer_target_id;
    BM_Target_Id current_target_id;
} BM_Query_Genex_Context;

#endif // BUILD_MODEL_QUERY_INTERNAL_H_
