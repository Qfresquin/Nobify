#ifndef EVAL_HASH_H_
#define EVAL_HASH_H_

#include "evaluator_internal.h"

bool eval_hash_compute_hex_temp(Evaluator_Context *ctx, String_View algo, String_View input, String_View *out_hex);
bool eval_hash_is_supported_algo(String_View algo);

#endif // EVAL_HASH_H_
