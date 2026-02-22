#include "build_model_validate.h"

#include "../diagnostics/diagnostics.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Build_Model *model;
    bool has_errors;
    bool has_warnings;
    void *diagnostics;
} Build_Model_Validate_Ctx;

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

        if (target->owner_directory_index >= model->directory_node_count) {
            bm_validate_report(ctx,
                               true,
                               target,
                               nob_temp_sprintf("target owner directory index out of range (%zu)", target->owner_directory_index),
                               "ensure builder assigns a valid directory scope to each target");
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
                                 const char *list_name) {
    if (!ctx || !ctx->model || !owner || !list) return;

    for (size_t i = 0; i < list->count; i++) {
        String_View ref = list->items[i];
        if (ref.count == 0) continue;
        if (build_model_find_target_index(ctx->model, ref) >= 0) continue;

        bm_validate_report(
            ctx,
            true,
            owner,
            nob_temp_sprintf("missing dependency in %s: '%.*s'", list_name ? list_name : "list", SV_Arg(ref)),
            "declare the dependency target before using this dependency edge"
        );
    }
}

static void bm_validate_dependencies(Build_Model_Validate_Ctx *ctx) {
    if (!ctx || !ctx->model) return;

    for (size_t i = 0; i < ctx->model->target_count; i++) {
        const Build_Target *target = ctx->model->targets[i];
        if (!target) continue;

        bm_validate_ref_list(ctx, target, &target->dependencies, "dependencies");
        bm_validate_ref_list(ctx, target, &target->object_dependencies, "object_dependencies");
        bm_validate_ref_list(ctx, target, &target->interface_dependencies, "interface_dependencies");
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
    };

    for (size_t li = 0; li < sizeof(lists) / sizeof(lists[0]); li++) {
        const String_List *list = lists[li];
        for (size_t i = 0; i < list->count; i++) {
            String_View ref = list->items[i];
            if (ref.count == 0) continue;
            int dep_idx = build_model_find_target_index(ctx->model, ref);
            if (dep_idx < 0) continue;
            if (bm_cycle_visit(ctx, (size_t)dep_idx, state)) return true;
        }
    }

    state[idx] = 2;
    return false;
}

static bool bm_install_rule_has_destination(String_View entry) {
    if (entry.count == 0 || !entry.data) return false;
    for (size_t i = 0; i < entry.count; i++) {
        if (entry.data[i] == '\t') {
            return (i + 1) < entry.count;
        }
    }
    return false;
}

static bool bm_has_cpack_group(const Build_Model *model, String_View name) {
    if (!model || name.count == 0) return false;
    for (size_t i = 0; i < model->cpack_component_group_count; i++) {
        if (nob_sv_eq(model->cpack_component_groups[i].name, name)) return true;
    }
    return false;
}

static bool bm_has_cpack_install_type(const Build_Model *model, String_View name) {
    if (!model || name.count == 0) return false;
    for (size_t i = 0; i < model->cpack_install_type_count; i++) {
        if (nob_sv_eq(model->cpack_install_types[i].name, name)) return true;
    }
    return false;
}

static bool bm_has_cpack_component(const Build_Model *model, String_View name) {
    if (!model || name.count == 0) return false;
    for (size_t i = 0; i < model->cpack_component_count; i++) {
        if (nob_sv_eq(model->cpack_components[i].name, name)) return true;
    }
    return false;
}

bool build_model_check_cycles_ex(const Build_Model *model, Arena *scratch, void *diagnostics) {
    (void)diagnostics;
    if (!model) {
        diag_log(DIAG_SEV_ERROR,
                 "build_model_validate",
                 "<build-model>",
                 0,
                 0,
                 "build_model_check_cycles_ex",
                 "null model passed to cycle checker",
                 "ensure builder_finish() succeeded before validation");
        return false;
    }
    if (model->target_count == 0) return true;

    uint8_t *state = NULL;
    bool using_heap = false;
    if (scratch) {
        state = arena_alloc_array_zero(scratch, uint8_t, model->target_count);
    } else {
        state = (uint8_t*)calloc(model->target_count, sizeof(uint8_t));
        using_heap = true;
    }
    if (!state) {
        diag_log(DIAG_SEV_ERROR,
                 "build_model_validate",
                 "<build-model>",
                 0,
                 0,
                 "build_model_check_cycles_ex",
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

    if (using_heap) free(state);
    return !has_cycle;
}

bool build_model_check_cycles(const Build_Model *model, void *diagnostics) {
    return build_model_check_cycles_ex(model, NULL, diagnostics);
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

    for (size_t i = 0; i < ctx->model->test_count; i++) {
        const Build_Test *test = &ctx->model->tests[i];
        if (test->name.count == 0) {
            bm_validate_report(ctx,
                               true,
                               NULL,
                               nob_temp_sprintf("test at index %zu has empty name", i),
                               "set a non-empty test name in add_test()");
        }
        if (test->command.count == 0) {
            bm_validate_report(ctx,
                               true,
                               NULL,
                               nob_temp_sprintf("test '%.*s' has empty command", SV_Arg(test->name)),
                               "set COMMAND in add_test()");
        }
    }

    const String_List *install_lists[] = {
        &ctx->model->install_rules.targets,
        &ctx->model->install_rules.files,
        &ctx->model->install_rules.programs,
        &ctx->model->install_rules.directories,
    };
    const char *install_names[] = {"TARGETS", "FILES", "PROGRAMS", "DIRECTORY"};
    for (size_t li = 0; li < sizeof(install_lists) / sizeof(install_lists[0]); li++) {
        const String_List *list = install_lists[li];
        for (size_t i = 0; i < list->count; i++) {
            if (!bm_install_rule_has_destination(list->items[i])) {
                bm_validate_report(ctx,
                                   true,
                                   NULL,
                                   nob_temp_sprintf("install(%s) rule missing destination at index %zu", install_names[li], i),
                                   "provide DESTINATION for every install() rule");
            }
        }
    }

    for (size_t i = 0; i < ctx->model->cpack_component_count; i++) {
        const CPack_Component *component = &ctx->model->cpack_components[i];
        if (component->group.count > 0 && !bm_has_cpack_group(ctx->model, component->group)) {
            bm_validate_report(ctx,
                               true,
                               NULL,
                               nob_temp_sprintf("cpack component '%.*s' references unknown group '%.*s'",
                                                SV_Arg(component->name),
                                                SV_Arg(component->group)),
                               "declare the group via cpack_add_component_group()");
        }

        for (size_t d = 0; d < component->depends.count; d++) {
            String_View dep = component->depends.items[d];
            if (dep.count == 0) continue;
            if (!bm_has_cpack_component(ctx->model, dep)) {
                bm_validate_report(ctx,
                                   true,
                                   NULL,
                                   nob_temp_sprintf("cpack component '%.*s' depends on unknown component '%.*s'",
                                                    SV_Arg(component->name),
                                                    SV_Arg(dep)),
                                   "declare dependency component via cpack_add_component()");
            }
        }

        for (size_t t = 0; t < component->install_types.count; t++) {
            String_View install_type = component->install_types.items[t];
            if (install_type.count == 0) continue;
            if (!bm_has_cpack_install_type(ctx->model, install_type)) {
                bm_validate_report(ctx,
                                   true,
                                   NULL,
                                   nob_temp_sprintf("cpack component '%.*s' references unknown install type '%.*s'",
                                                    SV_Arg(component->name),
                                                    SV_Arg(install_type)),
                                   "declare install type via cpack_add_install_type()");
            }
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
