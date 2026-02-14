#ifndef MATH_PARSER_H_
#define MATH_PARSER_H_

#include "build_model.h"

typedef enum {
    MATH_EVAL_OK,
    MATH_EVAL_INVALID_EXPR,
    MATH_EVAL_DIV_ZERO,
    MATH_EVAL_RANGE,
} Math_Eval_Status;

Math_Eval_Status math_eval_i64(String_View expr, long long *out_value);

#endif // MATH_PARSER_H_