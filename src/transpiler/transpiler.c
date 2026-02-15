#include "transpiler.h"
#include "diagnostics.h"
#include "arena_dyn.h"
#include "genex_evaluator.h"
#include "logic_model.h"
#include "math_parser.h"
#include "sys_utils.h"
#include "toolchain_driver.h"
#include "transpiler_effects.h"
#include "cmake_path_utils.h"
#include "cmake_regex_utils.h"
#include "cmake_glob_utils.h"
#include "find_search_utils.h"
#include "cmake_meta_io.h"
#include "ctest_coverage_utils.h"
#include "ds_adapter.h"
#include "transpiler_legacy_bridge.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#if defined(__MINGW32__)
#include <_mingw.h>
#endif

#include "transpiler_evaluator.inc.c"
#include "transpiler_dispatcher.inc.c"
#include "transpiler_codegen.inc.c"

// ============================================================================
// FUNCOES PUBLICAS
// ============================================================================

static void transpile_datree_legacy_impl(Ast_Root root, String_Builder *sb, const Transpiler_Run_Options *options) {
    Transpiler_Run_Options effective = {0};
    if (options) effective = *options;

    // Cria arena para toda a avaliacao
    Arena *arena = arena_create(1024 * 1024); // 1MB
    if (!arena) {
        nob_log(NOB_ERROR, "Falha ao criar arena de memoria");
        return;
    }

    // Cria contexto de avaliacao
    Evaluator_Context *ctx = eval_context_create(arena);
    if (!ctx) {
        nob_log(NOB_ERROR, "Falha ao criar contexto de avaliacao");
        arena_destroy(arena);
        return;
    }
    ctx->continue_on_fatal_error = effective.continue_on_fatal_error;

    if (effective.input_path && effective.input_path[0] != '\0') {
        String_View input_sv = sv_from_cstr(effective.input_path);
        String_View base_dir_abs = cmk_path_make_absolute(arena, cmk_path_parent(arena, input_sv));
        String_View base_dir = cmk_path_normalize(arena, base_dir_abs);
        ctx->current_source_dir = base_dir;
        ctx->current_binary_dir = base_dir;
        ctx->current_list_dir = base_dir;
    }

    // Avalia a AST e popula o modelo
    for (size_t i = 0; i < root.count; i++) {
        eval_node(ctx, root.items[i]);
    }

    // Gera codigo C a partir do modelo
    generate_from_model(ctx->model, sb);

    // Libera memoria (arena faz cleanup automatico)
    arena_destroy(arena);
}

void transpile_datree_legacy_bridge(Ast_Root root, String_Builder *sb, const Transpiler_Run_Options *options) {
    transpile_datree_legacy_impl(root, sb, options);
}

void transpile_datree_ex(Ast_Root root, String_Builder *sb, const Transpiler_Run_Options *options) {
    transpile_datree_legacy_impl(root, sb, options);
}

void transpile_datree_with_input_path(Ast_Root root, String_Builder *sb, const char *input_path) {
    Transpiler_Run_Options options = {0};
    options.input_path = input_path;
    transpile_datree_ex(root, sb, &options);
}

void transpile_datree(Ast_Root root, String_Builder *sb) {
    transpile_datree_ex(root, sb, NULL);
}

void transpiler_set_continue_on_fatal_error(bool enabled) {
    (void)enabled;
}

void define_cache_var_build_model(Build_Model *model, const char *key, const char *val) {
    build_model_set_cache_variable(model, sv_from_cstr(key), sv_from_cstr(val),
                                   sv_from_cstr("STRING"), sv_from_cstr(""));
}

void dump_evaluation_context(const Evaluator_Context *ctx) {
    printf("=== Evaluation Context Dump ===\n");
    printf("Scopes: %zu\n", ctx->scope_count);
    printf("Current source dir: "SV_Fmt"\n", SV_Arg(ctx->current_source_dir));
    printf("Current binary dir: "SV_Fmt"\n", SV_Arg(ctx->current_binary_dir));

    // Dump do modelo
    if (ctx->model) {
        build_model_dump(ctx->model, stdout);
    }
}
