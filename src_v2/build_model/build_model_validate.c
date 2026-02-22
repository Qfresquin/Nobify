#include "build_model_validate.h"

#include "../diagnostics/diagnostics.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Build_Model *model;
    bool has_errors;
    bool has_warnings;
    void *diagnostics;
} Build_Model_Validate_Ctx;

typedef enum {
    BM_EDGE_LINK_DIRECT = 0,
    BM_EDGE_LINK_HEURISTIC,
} Build_Model_Edge_Mode;

static void bm_validate_report(Build_Model_Validate_Ctx *ctx,
                               bool error,
                               const Build_Target *target,
                               const char *cause,
                               const char *hint) {
    if (!ctx) return;
    if (error) ctx->has_errors = true;
    else ctx->has_warnings = true;

    const char *target_suffix = "";
    if (target && target->name.count > 0) {
        target_suffix = nob_temp_sprintf(" [target=%.*s]", SV_Arg(target->name));
    }

    diag_log(
        error ? DIAG_SEV_ERROR : DIAG_SEV_WARNING,
        "build_model_validate",
        "<build-model>",
        0,
        0,
        "build_model_validate",
        nob_temp_sprintf("%s%s", cause ? cause : "", target_suffix),
        hint ? hint : ""
    );
}

static bool bm_target_type_is_valid(Target_Type type) {
    switch (type) {
        case TARGET_EXECUTABLE:
        case TARGET_STATIC_LIB:
        case TARGET_SHARED_LIB:
        case TARGET_OBJECT_LIB:
        case TARGET_INTERFACE_LIB:
        case TARGET_UTILITY:
        case TARGET_IMPORTED:
        case TARGET_ALIAS:
            return true;
    }
    return false;
}

static bool bm_sv_eq_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    if (sv.count != n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool bm_sv_ends_with_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    if (sv.count < n) return false;
    size_t start = sv.count - n;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[start + i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool bm_is_probable_target_ref(String_View item) {
    if (item.count == 0 || !item.data) return false;
    if (item.count >= 2 && item.data[0] == '$' && item.data[1] == '<') return false;

    if (bm_sv_eq_ci_lit(item, "debug") ||
        bm_sv_eq_ci_lit(item, "optimized") ||
        bm_sv_eq_ci_lit(item, "general")) {
        return false;
    }

    char c0 = item.data[0];
    if (c0 == '-' || c0 == '/' || c0 == '\\' || c0 == '.') return false;

    for (size_t i = 0; i < item.count; i++) {
        char c = item.data[i];
        if (c == '/' || c == '\\' || c == ':' || c == ';' || isspace((unsigned char)c)) return false;
    }

    if (bm_sv_ends_with_ci_lit(item, ".a")) return false;
    if (bm_sv_ends_with_ci_lit(item, ".lib")) return false;
    if (bm_sv_ends_with_ci_lit(item, ".so")) return false;
    if (bm_sv_ends_with_ci_lit(item, ".dylib")) return false;
    if (bm_sv_ends_with_ci_lit(item, ".dll")) return false;
    if (bm_sv_ends_with_ci_lit(item, ".framework")) return false;
    return true;
}

static int bm_find_target_index(const Build_Model *model, String_View name) {
    if (!model || name.count == 0) return -1;
    for (size_t i = 0; i < model->target_count; i++) {
        const Build_Target *t = model->targets[i];
        if (!t) continue;
        if (nob_sv_eq(t->name, name)) return (int)i;
    }
    return -1;
}

static void bm_validate_structural(Build_Model_Validate_Ctx *ctx) {
    if (!ctx || !ctx->model) return;

    const Build_Model *model = ctx->model;
    for (size_t i = 0; i < model->target_count; i++) {
        const Build_Target *target = model->targets[i];
        if (!target) {
            bm_validate_report(ctx,
                               true,
                               NULL,
                               nob_temp_sprintf("null target entry at index %zu", i),
                               "builder emitted a null target pointer");
            continue;
        }

        if (target->name.count == 0 || !target->name.data) {
            bm_validate_report(ctx,
                               true,
                               target,
                               "target has an empty name",
                               "every target must have a stable, non-empty name");
        }

        if (!bm_target_type_is_valid(target->type)) {
            bm_validate_report(ctx,
                               true,
                               target,
                               nob_temp_sprintf("target has invalid type enum value (%d)", (int)target->type),
                               "ensure target declaration uses a supported type");
        }

        for (size_t j = i + 1; j < model->target_count; j++) {
            const Build_Target *other = model->targets[j];
            if (!other) continue;
            if (target->name.count == 0 || other->name.count == 0) continue;
            if (nob_sv_eq(target->name, other->name)) {
                bm_validate_report(ctx,
                                   true,
                                   target,
                                   nob_temp_sprintf("duplicate target name '%.*s'", SV_Arg(target->name)),
                                   "target names must be unique in the final model");
            }
        }
    }
}

static void bm_validate_ref_list(Build_Model_Validate_Ctx *ctx,
                                 const Build_Target *owner,
                                 const String_List *list,
                                 const char *list_name,
                                 Build_Model_Edge_Mode edge_mode) {
    if (!ctx || !ctx->model || !owner || !list) return;

    for (size_t i = 0; i < list->count; i++) {
        String_View ref = list->items[i];
        if (ref.count == 0) continue;
        if (edge_mode == BM_EDGE_LINK_HEURISTIC && !bm_is_probable_target_ref(ref)) continue;
        if (bm_find_target_index(ctx->model, ref) >= 0) continue;

        bm_validate_report(
            ctx,
            true,
            owner,
            nob_temp_sprintf("missing dependency in %s: '%.*s'", list_name ? list_name : "list", SV_Arg(ref)),
            "declare the dependency target first, or use a library path/flag if it is external"
        );
    }
}

static void bm_validate_dependencies(Build_Model_Validate_Ctx *ctx) {
    if (!ctx || !ctx->model) return;

    for (size_t i = 0; i < ctx->model->target_count; i++) {
        const Build_Target *target = ctx->model->targets[i];
        if (!target) continue;

        bm_validate_ref_list(ctx, target, &target->dependencies, "dependencies", BM_EDGE_LINK_DIRECT);
        bm_validate_ref_list(ctx, target, &target->object_dependencies, "object_dependencies", BM_EDGE_LINK_DIRECT);
        bm_validate_ref_list(ctx, target, &target->interface_dependencies, "interface_dependencies", BM_EDGE_LINK_DIRECT);
        bm_validate_ref_list(ctx, target, &target->link_libraries, "link_libraries", BM_EDGE_LINK_HEURISTIC);
        bm_validate_ref_list(ctx, target, &target->interface_libs, "interface_libs", BM_EDGE_LINK_HEURISTIC);
    }
}

static bool bm_cycle_visit(Build_Model_Validate_Ctx *ctx, size_t idx, uint8_t *state) {
    if (!ctx || !ctx->model || !state) return false;
    if (idx >= ctx->model->target_count) return false;

    if (state[idx] == 1) return true;
    if (state[idx] == 2) return false;
    state[idx] = 1;

    const Build_Target *target = ctx->model->targets[idx];
    if (!target) {
        state[idx] = 2;
        return false;
    }

    const String_List *lists[] = {
        &target->dependencies,
        &target->object_dependencies,
        &target->interface_dependencies,
        &target->link_libraries,
        &target->interface_libs,
    };
    Build_Model_Edge_Mode modes[] = {
        BM_EDGE_LINK_DIRECT,
        BM_EDGE_LINK_DIRECT,
        BM_EDGE_LINK_DIRECT,
        BM_EDGE_LINK_HEURISTIC,
        BM_EDGE_LINK_HEURISTIC,
    };

    for (size_t li = 0; li < sizeof(lists) / sizeof(lists[0]); li++) {
        const String_List *list = lists[li];
        for (size_t i = 0; i < list->count; i++) {
            String_View ref = list->items[i];
            if (ref.count == 0) continue;
            if (modes[li] == BM_EDGE_LINK_HEURISTIC && !bm_is_probable_target_ref(ref)) continue;
            int dep_idx = bm_find_target_index(ctx->model, ref);
            if (dep_idx < 0) continue;
            if (bm_cycle_visit(ctx, (size_t)dep_idx, state)) return true;
        }
    }

    state[idx] = 2;
    return false;
}

bool build_model_check_cycles(const Build_Model *model, void *diagnostics) {
    (void)diagnostics;
    if (!model) {
        diag_log(DIAG_SEV_ERROR,
                 "build_model_validate",
                 "<build-model>",
                 0,
                 0,
                 "build_model_check_cycles",
                 "null model passed to cycle checker",
                 "ensure builder_finish() succeeded before validation");
        return false;
    }
    if (model->target_count == 0) return true;

    uint8_t *state = (uint8_t*)calloc(model->target_count, sizeof(uint8_t));
    if (!state) {
        diag_log(DIAG_SEV_ERROR,
                 "build_model_validate",
                 "<build-model>",
                 0,
                 0,
                 "build_model_check_cycles",
                 "out of memory during cycle check",
                 "retry with more memory");
        return false;
    }

    Build_Model_Validate_Ctx ctx = {
        .model = model,
        .has_errors = false,
        .has_warnings = false,
        .diagnostics = diagnostics,
    };
    (void)ctx.diagnostics;

    bool has_cycle = false;
    for (size_t i = 0; i < model->target_count && !has_cycle; i++) {
        if (!model->targets[i]) continue;
        if (bm_cycle_visit(&ctx, i, state)) {
            has_cycle = true;
            bm_validate_report(&ctx,
                               true,
                               model->targets[i],
                               "dependency cycle detected",
                               "break the cycle between targets to proceed");
        }
    }

    free(state);
    return !has_cycle;
}

static void bm_validate_semantics(Build_Model_Validate_Ctx *ctx) {
    if (!ctx || !ctx->model) return;

    for (size_t i = 0; i < ctx->model->target_count; i++) {
        const Build_Target *target = ctx->model->targets[i];
        if (!target) continue;

        if (target->type == TARGET_INTERFACE_LIB) {
            if (target->sources.count > 0) {
                bm_validate_report(ctx,
                                   true,
                                   target,
                                   "INTERFACE target contains sources",
                                   "move sources to a concrete library/executable target");
            }
            if (target->dependencies.count > 0 || target->link_libraries.count > 0) {
                bm_validate_report(ctx,
                                   true,
                                   target,
                                   "INTERFACE target has PRIVATE/PUBLIC link dependencies",
                                   "use INTERFACE link dependencies only");
            }
        }

        bool duplicate_source = false;
        for (size_t a = 0; a < target->sources.count && !duplicate_source; a++) {
            for (size_t b = a + 1; b < target->sources.count; b++) {
                if (nob_sv_eq(target->sources.items[a], target->sources.items[b])) {
                    duplicate_source = true;
                    break;
                }
            }
        }
        if (duplicate_source) {
            bm_validate_report(ctx,
                               false,
                               target,
                               "target contains duplicate sources",
                               "deduplicate target sources to avoid repeated compilation");
        }
    }
}

bool build_model_validate(const Build_Model *model, void *diagnostics) {
    Build_Model_Validate_Ctx ctx = {
        .model = model,
        .has_errors = false,
        .has_warnings = false,
        .diagnostics = diagnostics,
    };
    (void)ctx.diagnostics;

    if (!model) {
        bm_validate_report(&ctx,
                           true,
                           NULL,
                           "null model passed to validator",
                           "ensure builder_finish() succeeded before validation");
        return false;
    }

    bm_validate_structural(&ctx);
    bm_validate_dependencies(&ctx);
    if (!build_model_check_cycles(model, diagnostics)) {
        ctx.has_errors = true;
    }
    bm_validate_semantics(&ctx);

    return !ctx.has_errors;
}
