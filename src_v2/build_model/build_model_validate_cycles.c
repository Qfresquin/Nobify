#include "build_model_internal.h"

typedef enum {
    BM_EXEC_NODE_TARGET = 0,
    BM_EXEC_NODE_STEP,
} BM_Exec_Node_Kind;

static bool bm_cycle_visit(const Build_Model_Draft *draft,
                           BM_Target_Id id,
                           Arena *scratch,
                           uint8_t *colors,
                           Diag_Sink *sink,
                           bool *had_error) {
    const BM_Target_Record *target = &draft->targets[id];
    colors[id] = 1;

    for (size_t dep = 0; dep < arena_arr_len(target->explicit_dependency_names); ++dep) {
        BM_Target_Id dep_id = bm_draft_find_target_id(draft, target->explicit_dependency_names[dep]);
        if (dep_id == BM_TARGET_ID_INVALID) continue;

        if (colors[dep_id] == 1) {
            *had_error = true;
            bm_diag_error(sink,
                          target->provenance,
                          "build_model_validate",
                          "cycles",
                          "explicit target dependency cycle detected",
                          "remove or break the dependency cycle between targets");
            continue;
        }

        if (colors[dep_id] == 0 && !bm_cycle_visit(draft, dep_id, scratch, colors, sink, had_error)) {
            return false;
        }
    }

    colors[id] = 2;
    (void)scratch;
    return true;
}

static BM_Provenance bm_exec_node_provenance(const Build_Model *model,
                                             BM_Exec_Node_Kind kind,
                                             size_t id) {
    if (!model) return (BM_Provenance){0};
    if (kind == BM_EXEC_NODE_TARGET && id < arena_arr_len(model->targets)) {
        return model->targets[id].provenance;
    }
    if (kind == BM_EXEC_NODE_STEP && id < arena_arr_len(model->build_steps)) {
        return model->build_steps[id].provenance;
    }
    return (BM_Provenance){0};
}

static bool bm_build_step_produces_path(const BM_Build_Step_Record *step, String_View path) {
    if (!step) return false;
    for (size_t i = 0; i < arena_arr_len(step->effective_outputs); ++i) {
        if (nob_sv_eq(step->effective_outputs[i], path)) return true;
    }
    for (size_t i = 0; i < arena_arr_len(step->effective_byproducts); ++i) {
        if (nob_sv_eq(step->effective_byproducts[i], path)) return true;
    }
    return false;
}

static bool bm_validate_duplicate_step_producers(const Build_Model *model,
                                                 Diag_Sink *sink,
                                                 bool *had_error) {
    if (!model || !had_error) return false;
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        const BM_Build_Step_Record *lhs = &model->build_steps[i];
        for (size_t j = 0; j < arena_arr_len(lhs->effective_outputs); ++j) {
            String_View lhs_path = lhs->effective_outputs[j];
            for (size_t k = 0; k < arena_arr_len(model->build_steps); ++k) {
                const BM_Build_Step_Record *rhs = &model->build_steps[k];
                size_t start = (i == k) ? (j + 1) : 0;
                for (size_t m = start; m < arena_arr_len(rhs->effective_outputs); ++m) {
                    if (!nob_sv_eq(lhs_path, rhs->effective_outputs[m])) continue;
                    *had_error = true;
                    bm_diag_error(sink,
                                  rhs->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "duplicate build-step producer for effective output path",
                                  "ensure each effective output path is owned by exactly one build step");
                }
                for (size_t m = 0; m < arena_arr_len(rhs->effective_byproducts); ++m) {
                    if (!nob_sv_eq(lhs_path, rhs->effective_byproducts[m])) continue;
                    *had_error = true;
                    bm_diag_error(sink,
                                  rhs->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "duplicate build-step producer for effective output path",
                                  "ensure outputs and byproducts do not overlap across producer steps");
                }
            }
        }
        for (size_t j = 0; j < arena_arr_len(lhs->effective_byproducts); ++j) {
            String_View lhs_path = lhs->effective_byproducts[j];
            for (size_t k = 0; k < arena_arr_len(model->build_steps); ++k) {
                const BM_Build_Step_Record *rhs = &model->build_steps[k];
                size_t output_start = 0;
                size_t start = (i == k) ? (j + 1) : 0;
                if (i == k) output_start = 0;
                for (size_t m = output_start; m < arena_arr_len(rhs->effective_outputs); ++m) {
                    if (!nob_sv_eq(lhs_path, rhs->effective_outputs[m])) continue;
                    *had_error = true;
                    bm_diag_error(sink,
                                  rhs->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "duplicate build-step producer for effective byproduct path",
                                  "ensure byproducts do not overlap with effective outputs");
                }
                for (size_t m = start; m < arena_arr_len(rhs->effective_byproducts); ++m) {
                    if (!nob_sv_eq(lhs_path, rhs->effective_byproducts[m])) continue;
                    *had_error = true;
                    bm_diag_error(sink,
                                  rhs->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "duplicate build-step producer for effective byproduct path",
                                  "ensure each effective byproduct path is owned by exactly one build step");
                }
            }
        }
    }
    return true;
}

static bool bm_target_kind_is_hook_owner(BM_Target_Kind kind) {
    return kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static bool bm_validate_step_owner_contracts(const Build_Model *model,
                                             Diag_Sink *sink,
                                             bool *had_error) {
    if (!model || !had_error) return false;
    for (size_t i = 0; i < arena_arr_len(model->build_steps); ++i) {
        const BM_Build_Step_Record *step = &model->build_steps[i];
        const BM_Target_Record *owner = NULL;

        if (step->owner_target_id != BM_TARGET_ID_INVALID) {
            if ((size_t)step->owner_target_id >= arena_arr_len(model->targets)) {
                *had_error = true;
                bm_diag_error(sink,
                              step->provenance,
                              "build_model_validate",
                              "freeze",
                              "build step owner target id is invalid",
                              "freeze must only preserve resolved owner target ids");
                continue;
            }
            owner = &model->targets[step->owner_target_id];
        }

        switch (step->kind) {
            case BM_BUILD_STEP_OUTPUT_RULE:
                break;

            case BM_BUILD_STEP_CUSTOM_TARGET:
                if (!owner || owner->kind != BM_TARGET_UTILITY || owner->alias || owner->imported) {
                    *had_error = true;
                    bm_diag_error(sink,
                                  step->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "custom-target build step must belong to a local utility target",
                                  "attach custom-target execution only to non-alias, non-imported utility targets");
                }
                break;

            case BM_BUILD_STEP_TARGET_PRE_BUILD:
            case BM_BUILD_STEP_TARGET_PRE_LINK:
            case BM_BUILD_STEP_TARGET_POST_BUILD:
                if (!owner || !bm_target_kind_is_hook_owner(owner->kind) || owner->alias || owner->imported) {
                    *had_error = true;
                    bm_diag_error(sink,
                                  step->provenance,
                                  "build_model_validate",
                                  "freeze",
                                  "target hook build step must belong to a concrete local build target",
                                  "attach PRE_BUILD/PRE_LINK/POST_BUILD only to local executable or library targets");
                }
                break;
        }
    }
    return true;
}

static bool bm_validate_target_source_producers(const Build_Model *model,
                                                Diag_Sink *sink,
                                                bool *had_error) {
    if (!model || !had_error) return false;
    for (size_t target_index = 0; target_index < arena_arr_len(model->targets); ++target_index) {
        const BM_Target_Record *target = &model->targets[target_index];
        for (size_t source_index = 0; source_index < arena_arr_len(target->source_records); ++source_index) {
            const BM_Target_Source_Record *source = &target->source_records[source_index];
            const BM_Build_Step_Record *producer = NULL;
            if (source->producer_step_id == BM_BUILD_STEP_ID_INVALID) continue;
            if ((size_t)source->producer_step_id >= arena_arr_len(model->build_steps)) {
                *had_error = true;
                bm_diag_error(sink,
                              source->provenance,
                              "build_model_validate",
                              "freeze",
                              "target source points at an invalid producer step id",
                              "fix producer-step linkage during freeze resolution");
                continue;
            }
            producer = &model->build_steps[source->producer_step_id];
            if (!source->generated || !bm_build_step_produces_path(producer, source->effective_path)) {
                *had_error = true;
                bm_diag_error(sink,
                              source->provenance,
                              "build_model_validate",
                              "freeze",
                              "target source producer step does not own the effective generated path",
                              "link generated sources only to build steps that produce the effective path");
            }
        }
    }
    return true;
}

static size_t bm_exec_node_index(const Build_Model *model,
                                 BM_Exec_Node_Kind kind,
                                 size_t id) {
    size_t target_count = model ? arena_arr_len(model->targets) : 0;
    return kind == BM_EXEC_NODE_TARGET ? id : (target_count + id);
}

static bool bm_exec_visit_node(const Build_Model *model,
                               BM_Exec_Node_Kind kind,
                               size_t id,
                               const BM_Query_Eval_Context *ctx,
                               Arena *scratch,
                               uint8_t *colors,
                               Diag_Sink *sink,
                               bool *had_error);

static bool bm_exec_visit_edge(const Build_Model *model,
                               BM_Exec_Node_Kind from_kind,
                               size_t from_id,
                               BM_Exec_Node_Kind to_kind,
                               size_t to_id,
                               const BM_Query_Eval_Context *ctx,
                               Arena *scratch,
                               uint8_t *colors,
                               Diag_Sink *sink,
                               bool *had_error) {
    size_t to_index = bm_exec_node_index(model, to_kind, to_id);
    if (colors[to_index] == 1) {
        *had_error = true;
        bm_diag_error(sink,
                      bm_exec_node_provenance(model, from_kind, from_id),
                      "build_model_validate",
                      "freeze",
                      "execution dependency cycle detected in frozen build graph",
                      "break the cycle between targets and build steps before code generation");
        return true;
    }
    if (colors[to_index] == 2) return true;
    return bm_exec_visit_node(model, to_kind, to_id, ctx, scratch, colors, sink, had_error);
}

static bool bm_exec_visit_order_span(const Build_Model *model,
                                     BM_Target_Id owner_id,
                                     BM_Build_Order_Node_Span span,
                                     const BM_Query_Eval_Context *ctx,
                                     Arena *scratch,
                                     uint8_t *colors,
                                     Diag_Sink *sink,
                                     bool *had_error) {
    for (size_t i = 0; i < span.count; ++i) {
        const BM_Build_Order_Node *node = &span.items[i];
        if (node->kind == BM_BUILD_ORDER_NODE_TARGET) {
            if (!bm_exec_visit_edge(model,
                                    BM_EXEC_NODE_TARGET,
                                    owner_id,
                                    BM_EXEC_NODE_TARGET,
                                    node->target_id,
                                    ctx,
                                    scratch,
                                    colors,
                                    sink,
                                    had_error)) {
                return false;
            }
        } else if (!bm_exec_visit_edge(model,
                                       BM_EXEC_NODE_TARGET,
                                       owner_id,
                                       BM_EXEC_NODE_STEP,
                                       node->step_id,
                                       ctx,
                                       scratch,
                                       colors,
                                       sink,
                                       had_error)) {
            return false;
        }
    }
    return true;
}

static bool bm_exec_visit_target(const Build_Model *model,
                                 BM_Target_Id id,
                                 const BM_Query_Eval_Context *ctx,
                                 Arena *scratch,
                                 uint8_t *colors,
                                 Diag_Sink *sink,
                                 bool *had_error) {
    BM_Target_Build_Order_View view = {0};
    BM_Query_Eval_Context qctx = ctx ? *ctx : (BM_Query_Eval_Context){0};
    if (!scratch) return false;
    if (!bm_target_id_is_valid(qctx.current_target_id)) qctx.current_target_id = id;
    qctx.usage_mode = BM_QUERY_USAGE_LINK;
    if (!bm_query_target_effective_build_order_view(model, id, &qctx, scratch, &view)) return false;
    return bm_exec_visit_order_span(model, id, view.explicit_prerequisites, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.pre_build_steps, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.generated_source_steps, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.link_prerequisites, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.pre_link_steps, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.post_build_steps, &qctx, scratch, colors, sink, had_error) &&
           bm_exec_visit_order_span(model, id, view.custom_target_steps, &qctx, scratch, colors, sink, had_error);
}

static bool bm_exec_visit_step(const Build_Model *model,
                               BM_Build_Step_Id id,
                               const BM_Query_Eval_Context *ctx,
                               Arena *scratch,
                               uint8_t *colors,
                               Diag_Sink *sink,
                               bool *had_error) {
    BM_Build_Step_Effective_View view = {0};
    BM_Query_Eval_Context qctx = ctx ? *ctx : (BM_Query_Eval_Context){0};
    const BM_Build_Step_Record *step = NULL;
    if (!model || !scratch || (size_t)id >= arena_arr_len(model->build_steps)) return false;
    step = &model->build_steps[id];
    qctx.current_target_id = step->owner_target_id;
    qctx.usage_mode = BM_QUERY_USAGE_LINK;
    if (!bm_query_build_step_effective_view(model, id, &qctx, scratch, &view)) return false;
    for (size_t i = 0; i < view.target_dependencies.count; ++i) {
        if (!bm_exec_visit_edge(model,
                                BM_EXEC_NODE_STEP,
                                id,
                                BM_EXEC_NODE_TARGET,
                                view.target_dependencies.items[i],
                                &qctx,
                                scratch,
                                colors,
                                sink,
                                had_error)) {
            return false;
        }
    }
    for (size_t i = 0; i < view.producer_dependencies.count; ++i) {
        if (!bm_exec_visit_edge(model,
                                BM_EXEC_NODE_STEP,
                                id,
                                BM_EXEC_NODE_STEP,
                                view.producer_dependencies.items[i],
                                &qctx,
                                scratch,
                                colors,
                                sink,
                                had_error)) {
            return false;
        }
    }
    return true;
}

static bool bm_exec_visit_node(const Build_Model *model,
                               BM_Exec_Node_Kind kind,
                               size_t id,
                               const BM_Query_Eval_Context *ctx,
                               Arena *scratch,
                               uint8_t *colors,
                               Diag_Sink *sink,
                               bool *had_error) {
    size_t node_index = bm_exec_node_index(model, kind, id);
    colors[node_index] = 1;
    if (kind == BM_EXEC_NODE_TARGET) {
        if (!bm_exec_visit_target(model, (BM_Target_Id)id, ctx, scratch, colors, sink, had_error)) return false;
    } else {
        if (!bm_exec_visit_step(model, (BM_Build_Step_Id)id, ctx, scratch, colors, sink, had_error)) return false;
    }
    colors[node_index] = 2;
    return true;
}

static bool bm_validate_execution_cycles_for_context(const Build_Model *model,
                                                     const BM_Query_Eval_Context *ctx,
                                                     Arena *scratch,
                                                     Diag_Sink *sink,
                                                     bool *had_error) {
    size_t target_count = model ? arena_arr_len(model->targets) : 0;
    size_t step_count = model ? arena_arr_len(model->build_steps) : 0;
    size_t node_count = target_count + step_count;
    uint8_t *colors = NULL;
    if (!model || !scratch || !had_error) return false;
    if (node_count == 0) return true;

    colors = arena_alloc_array_zero(scratch, uint8_t, node_count);
    if (!colors) return false;

    for (size_t i = 0; i < target_count; ++i) {
        if (colors[bm_exec_node_index(model, BM_EXEC_NODE_TARGET, i)] != 0) continue;
        if (!bm_exec_visit_node(model, BM_EXEC_NODE_TARGET, i, ctx, scratch, colors, sink, had_error)) return false;
    }
    for (size_t i = 0; i < step_count; ++i) {
        if (colors[bm_exec_node_index(model, BM_EXEC_NODE_STEP, i)] != 0) continue;
        if (!bm_exec_visit_node(model, BM_EXEC_NODE_STEP, i, ctx, scratch, colors, sink, had_error)) return false;
    }
    return true;
}

static bool bm_validate_execution_cycles(const Build_Model *model,
                                         Arena *scratch,
                                         Diag_Sink *sink,
                                         bool *had_error) {
    size_t config_count = model ? arena_arr_len(model->known_configurations) : 0;
    if (!model || !scratch || !had_error) return false;
    for (size_t branch = 0; branch <= config_count; ++branch) {
        BM_Query_Eval_Context ctx = {0};
        ctx.config = branch < config_count ? model->known_configurations[branch] : nob_sv_from_cstr("");
        ctx.current_target_id = BM_TARGET_ID_INVALID;
        ctx.usage_mode = BM_QUERY_USAGE_LINK;
        ctx.build_interface_active = true;
        ctx.build_local_interface_active = true;
        ctx.install_interface_active = false;
        if (!bm_validate_execution_cycles_for_context(model, &ctx, scratch, sink, had_error)) return false;
    }
    return true;
}

bool bm_validate_explicit_cycles(const Build_Model_Draft *draft,
                                 Arena *scratch,
                                 Diag_Sink *sink,
                                 bool *had_error) {
    size_t target_count = draft ? arena_arr_len(draft->targets) : 0;
    uint8_t *colors = NULL;

    if (!draft || !scratch || !had_error) return false;
    if (target_count == 0) return true;

    colors = arena_alloc_array_zero(scratch, uint8_t, target_count);
    if (!colors) return false;

    for (size_t i = 0; i < target_count; ++i) {
        if (colors[i] != 0) continue;
        if (!bm_cycle_visit(draft, (BM_Target_Id)i, scratch, colors, sink, had_error)) return false;
    }
    return true;
}

bool bm_validate_execution_graph(const Build_Model *model,
                                 Arena *scratch,
                                 Diag_Sink *sink,
                                 bool *had_error) {
    if (!model || !scratch || !had_error) return false;
    if (!bm_validate_duplicate_step_producers(model, sink, had_error) ||
        !bm_validate_step_owner_contracts(model, sink, had_error) ||
        !bm_validate_target_source_producers(model, sink, had_error) ||
        !bm_validate_execution_cycles(model, scratch, sink, had_error)) {
        return false;
    }
    return true;
}
