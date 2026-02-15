#ifndef TRANSPILER_V2_H_
#define TRANSPILER_V2_H_

#include "transpiler.h"

void transpile_datree_v2(Ast_Root root,
                         String_Builder *sb,
                         const Transpiler_Run_Options *options,
                         const Transpiler_Compat_Profile *compat);

#endif // TRANSPILER_V2_H_

