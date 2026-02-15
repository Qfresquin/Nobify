#include "transpiler_v2.h"
#include "transpiler_legacy_bridge.h"
#include "diagnostics.h"
#include "arena_dyn.h"
#include <string.h>

static void transpiler_v2_ir_program_init(Cmake_IR_Program *program) {
    if (!program) return;
    memset(program, 0, sizeof(*program));
}

static void transpiler_v2_frontend_normalize(Ast_Root root, Cmake_IR_Program *program) {
    (void)root;
    (void)program;
    // Skeleton phase: normalization will be implemented incrementally.
}

static void transpiler_v2_semantic_to_ir(Ast_Root root, Cmake_IR_Program *program) {
    (void)root;
    (void)program;
    // Skeleton phase: semantic interpreter v2 will populate canonical IR.
}

static void transpiler_v2_planner(const Cmake_IR_Program *program, Build_Model *model) {
    (void)program;
    (void)model;
    // Skeleton phase: IR planner remains pending.
}

static void transpiler_v2_codegen(const Build_Model *model, String_Builder *sb) {
    (void)model;
    (void)sb;
    // Skeleton phase: codegen v2 remains pending.
}

void transpile_datree_v2(Ast_Root root,
                         String_Builder *sb,
                         const Transpiler_Run_Options *options,
                         const Transpiler_Compat_Profile *compat) {
    Transpiler_Compat_Profile effective_compat = {0};
    if (compat) {
        effective_compat = *compat;
    } else {
        effective_compat.kind = TRANSPILER_COMPAT_PROFILE_CMAKE_3_X;
        effective_compat.cmake_version = sv_from_cstr("3.x");
        effective_compat.allow_behavior_drift = false;
    }

    Cmake_IR_Program program = {0};
    transpiler_v2_ir_program_init(&program);
    transpiler_v2_frontend_normalize(root, &program);
    transpiler_v2_semantic_to_ir(root, &program);

    // During migration, v2 is intentionally fallback-first to preserve behavior.
    (void)effective_compat;
    transpile_datree_legacy_bridge(root, sb, options);

    // Reserved hooks to keep architecture shape explicit in this phase.
    transpiler_v2_planner(&program, NULL);
    transpiler_v2_codegen(NULL, sb);
}
