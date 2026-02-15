#ifndef TRANSPILER_LEGACY_BRIDGE_H_
#define TRANSPILER_LEGACY_BRIDGE_H_

#include "parser.h"
#include "transpiler.h"

// Internal bridge used by v2 during migration.
void transpile_datree_legacy_bridge(Ast_Root root, String_Builder *sb, const Transpiler_Run_Options *options);

#endif // TRANSPILER_LEGACY_BRIDGE_H_

