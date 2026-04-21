#include "genex_internal.h"

#include "genex_parse.c"
#include "genex_scan.c"
#include "genex_eval.c"

Genex_Result genex_eval(const Genex_Context *ctx, String_View input) {
    return gx_eval_root(ctx, input);
}
