#include "nob_codegen_internal.h"

#include "arena_dyn.h"

static bool cg_step_emit_runtime_config_branch_prefix(CG_Context *ctx,
                                                      Nob_String_Builder *out,
                                                      size_t branch_index) {
    if (!ctx || !out) return false;
    if (branch_index < arena_arr_len(ctx->known_configs)) {
        nob_sb_append_cstr(out, branch_index == 0 ? "        if (config_matches(g_build_config, " : "        } else if (config_matches(g_build_config, ");
        if (!cg_sb_append_c_string(out, ctx->known_configs[branch_index])) return false;
        nob_sb_append_cstr(out, ")) {\n");
        return true;
    }
    if (arena_arr_len(ctx->known_configs) == 0) return true;
    nob_sb_append_cstr(out, "        } else {\n");
    return true;
}

static bool cg_step_emit_runtime_config_branch_suffix(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    if (arena_arr_len(ctx->known_configs) > 0) nob_sb_append_cstr(out, "        }\n");
    return true;
}

static bool cg_step_emit_verify_declared_paths(const String_View *declared_paths,
                                               Nob_String_Builder *out);

static BM_Query_Eval_Context cg_step_make_query_ctx(CG_Context *ctx,
                                                    const CG_Build_Step_Info *info,
                                                    String_View config) {
    BM_Query_Eval_Context qctx = {0};
    qctx.current_target_id = info ? info->owner_target_id : BM_TARGET_ID_INVALID;
    qctx.usage_mode = BM_QUERY_USAGE_LINK;
    qctx.config = config;
    qctx.platform_id = ctx ? ctx->policy.platform_id : nob_sv_from_cstr("");
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = true;
    qctx.install_interface_active = false;
    return qctx;
}

static bool cg_step_query_effective_view(CG_Context *ctx,
                                         const CG_Build_Step_Info *info,
                                         String_View config,
                                         BM_Build_Step_Effective_View *out) {
    BM_Query_Eval_Context qctx = cg_step_make_query_ctx(ctx, info, config);
    if (!ctx || !info || !out) return false;
    return bm_query_build_step_effective_view(ctx->model, info->id, &qctx, ctx->scratch, out);
}

static bool cg_step_collect_unique_path(Arena *scratch,
                                        String_View **list,
                                        String_View sentinel_path,
                                        String_View value) {
    if (!scratch || !list || value.count == 0) return false;
    if (nob_sv_eq(value, nob_sv_from_cstr("."))) return true;
    if (sentinel_path.count > 0 && nob_sv_eq(value, sentinel_path)) return true;
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if (nob_sv_eq((*list)[i], value)) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_step_collect_unique_target_id(Arena *scratch, BM_Target_Id **list, BM_Target_Id value) {
    if (!scratch || !list || !bm_target_id_is_valid(value)) return false;
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if ((*list)[i] == value) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_step_collect_unique_step_id(Arena *scratch, BM_Build_Step_Id **list, BM_Build_Step_Id value) {
    if (!scratch || !list || !bm_build_step_id_is_valid(value)) return false;
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if ((*list)[i] == value) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_step_emit_dependency_prelude(CG_Context *ctx,
                                            const CG_Build_Step_Info *info,
                                            Nob_String_Builder *out) {
    BM_Target_Id *target_deps = NULL;
    BM_Build_Step_Id *producer_deps = NULL;
    if (!ctx || !info || !out) return false;

    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        BM_Build_Step_Effective_View view = {0};
        if (!cg_step_query_effective_view(ctx, info, config, &view)) return false;
        for (size_t i = 0; i < view.target_dependencies.count; ++i) {
            if (!cg_step_collect_unique_target_id(ctx->scratch, &target_deps, view.target_dependencies.items[i])) return false;
        }
        for (size_t i = 0; i < view.producer_dependencies.count; ++i) {
            if (!cg_step_collect_unique_step_id(ctx->scratch, &producer_deps, view.producer_dependencies.items[i])) return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(target_deps); ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, target_deps[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    for (size_t i = 0; i < arena_arr_len(producer_deps); ++i) {
        const CG_Build_Step_Info *dep = cg_build_step_info(ctx, producer_deps[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    return true;
}

static bool cg_step_collect_rebuild_inputs(CG_Context *ctx,
                                           const CG_Build_Step_Info *info,
                                           const BM_Build_Step_Effective_View *view,
                                           String_View config,
                                           String_View sentinel_path,
                                           String_View **out_inputs) {
    if (!ctx || !info || !view || !out_inputs) return false;

    for (size_t i = 0; i < view->target_dependencies.count; ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, view->target_dependencies.items[i]);
        if (!dep || dep->state_path.count == 0) continue;
        if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, sentinel_path, dep->state_path)) {
            return false;
        }
    }
    for (size_t i = 0; i < view->producer_dependencies.count; ++i) {
        const CG_Build_Step_Info *dep = cg_build_step_info(ctx, view->producer_dependencies.items[i]);
        BM_Build_Step_Effective_View dep_view = {0};
        if (!dep) return false;
        if (dep->sentinel_path.count > 0) {
            if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, sentinel_path, dep->sentinel_path)) {
                return false;
            }
            continue;
        }
        if (!cg_step_query_effective_view(ctx, dep, config, &dep_view)) return false;
        for (size_t output = 0; output < dep_view.outputs.count; ++output) {
            String_View path = {0};
            if (!cg_rebase_path_from_cwd(ctx, dep_view.outputs.items[output], &path) ||
                !cg_step_collect_unique_path(ctx->scratch, out_inputs, sentinel_path, path)) {
                return false;
            }
        }
        for (size_t byproduct = 0; byproduct < dep_view.byproducts.count; ++byproduct) {
            String_View path = {0};
            if (!cg_rebase_path_from_cwd(ctx, dep_view.byproducts.items[byproduct], &path) ||
                !cg_step_collect_unique_path(ctx->scratch, out_inputs, sentinel_path, path)) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < view->file_dependencies.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, view->file_dependencies.items[i], &path)) return false;
        if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, sentinel_path, path)) {
            return false;
        }
    }
    return true;
}

static bool cg_step_emit_rebuild_guard(CG_Context *ctx,
                                       String_View sentinel_path,
                                       const String_View *rebuild_inputs,
                                       Nob_String_Builder *out) {
    size_t rebuild_input_count = 1 + arena_arr_len(rebuild_inputs);
    if (!ctx || !out || sentinel_path.count == 0) return false;
    nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
    if (!cg_sb_append_c_string(out, sentinel_path)) return false;
    nob_sb_append_cstr(out, ", (const char*[]){");
    if (!cg_sb_append_c_string(out, ctx->emit_path_abs)) return false;
    for (size_t i = 0; i < arena_arr_len(rebuild_inputs); ++i) {
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, rebuild_inputs[i])) return false;
    }
    nob_sb_append_cstr(out, "}, ");
    nob_sb_append_cstr(out, nob_temp_sprintf("%zu", rebuild_input_count));
    nob_sb_append_cstr(out, ")) {\n");
    return true;
}

static bool cg_step_collect_declared_paths(CG_Context *ctx,
                                           const BM_Build_Step_Effective_View *view,
                                           String_View **out_paths) {
    if (!ctx || !view || !out_paths) return false;

    for (size_t i = 0; i < view->outputs.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, view->outputs.items[i], &path) ||
            !cg_step_collect_unique_path(ctx->scratch, out_paths, (String_View){0}, path)) {
            return false;
        }
    }
    for (size_t i = 0; i < view->byproducts.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, view->byproducts.items[i], &path) ||
            !cg_step_collect_unique_path(ctx->scratch, out_paths, (String_View){0}, path)) {
            return false;
        }
    }
    return true;
}

static bool cg_step_emit_ensure_declared_paths(String_View sentinel_path,
                                               const String_View *declared_paths,
                                               Nob_String_Builder *out) {
    if (!out) return false;
    if (sentinel_path.count > 0) {
        nob_sb_append_cstr(out, "        if (!ensure_parent_dir(");
        if (!cg_sb_append_c_string(out, sentinel_path)) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    for (size_t i = 0; i < arena_arr_len(declared_paths); ++i) {
        nob_sb_append_cstr(out, "        if (!ensure_parent_dir(");
        if (!cg_sb_append_c_string(out, declared_paths[i])) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    return true;
}

static bool cg_step_emit_command_arg(Nob_String_Builder *out,
                                     const char *cmd_var,
                                     String_View arg,
                                     bool first_arg) {
    if (!out || !cmd_var) return false;
    if (first_arg && nob_sv_eq(arg, nob_sv_from_cstr("cmake"))) {
        return cg_emit_cmd_append_expr(out, cmd_var, "resolve_cmake_bin()");
    }
    if (first_arg && nob_sv_eq(arg, nob_sv_from_cstr("cpack"))) {
        return cg_emit_cmd_append_expr(out, cmd_var, "resolve_cpack_bin()");
    }
    return cg_emit_cmd_append_sv(out, cmd_var, arg);
}

static bool cg_step_emit_commands(CG_Context *ctx,
                                  const CG_Build_Step_Info *info,
                                  const BM_Build_Step_Effective_View *view,
                                  String_View config,
                                  Nob_String_Builder *out) {
    String_View rebased_working_dir = {0};
    bool has_working_dir = false;
    BM_Query_Eval_Context qctx = cg_step_make_query_ctx(ctx, info, config);
    if (!ctx || !info || !view || !out) return false;

    has_working_dir = view->working_directory.count > 0;
    if (has_working_dir && !cg_rebase_path_from_cwd(ctx, view->working_directory, &rebased_working_dir)) {
        return false;
    }

    for (size_t cmd_index = 0; cmd_index < bm_query_build_step_command_count(ctx->model, info->id); ++cmd_index) {
        BM_String_Span argv = {0};
        if (!bm_query_build_step_effective_command_argv(ctx->model,
                                                        info->id,
                                                        cmd_index,
                                                        &qctx,
                                                        ctx->scratch,
                                                        &argv)) {
            return false;
        }
        nob_sb_append_cstr(out, "        {\n");
        nob_sb_append_cstr(out, "            Nob_Cmd step_cmd = {0};\n");
        for (size_t arg = 0; arg < argv.count; ++arg) {
            if (!cg_step_emit_command_arg(out, "step_cmd", argv.items[arg], arg == 0)) return false;
        }
        nob_sb_append_cstr(out, "            bool ok = run_cmd_in_dir(");
        if (has_working_dir) {
            if (!cg_sb_append_c_string(out, rebased_working_dir)) return false;
        } else {
            nob_sb_append_cstr(out, "NULL");
        }
        nob_sb_append_cstr(out, ", &step_cmd);\n");
        nob_sb_append_cstr(out, "            nob_cmd_free(step_cmd);\n");
        nob_sb_append_cstr(out, "            if (!ok) return false;\n");
        nob_sb_append_cstr(out, "        }\n");
    }
    return true;
}

static bool cg_step_compute_sentinel_for_view(CG_Context *ctx,
                                              const CG_Build_Step_Info *info,
                                              const BM_Build_Step_Effective_View *view,
                                              String_View *out) {
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !info || !view || !out) return false;
    if (view->outputs.count > 0) return cg_rebase_path_from_cwd(ctx, view->outputs.items[0], out);
    if (info->sentinel_path.count > 0) {
        *out = info->sentinel_path;
        return true;
    }
    return true;
}

static bool cg_step_emit_config_body(CG_Context *ctx,
                                     const CG_Build_Step_Info *info,
                                     String_View config,
                                     bool output_rule,
                                     Nob_String_Builder *out) {
    BM_Build_Step_Effective_View view = {0};
    String_View *rebuild_inputs = NULL;
    String_View *declared_paths = NULL;
    String_View sentinel_path = {0};
    if (!ctx || !info || !out) return false;
    if (!cg_step_query_effective_view(ctx, info, config, &view)) {
        nob_log(NOB_ERROR, "codegen: failed while querying build-step effective view");
        return false;
    }
    if (!cg_step_collect_declared_paths(ctx, &view, &declared_paths)) {
        nob_log(NOB_ERROR, "codegen: failed while collecting build-step declared paths");
        return false;
    }
    if (!cg_step_compute_sentinel_for_view(ctx, info, &view, &sentinel_path)) {
        nob_log(NOB_ERROR, "codegen: failed while computing build-step sentinel");
        return false;
    }

    if (output_rule) {
        if (!cg_step_collect_rebuild_inputs(ctx, info, &view, config, sentinel_path, &rebuild_inputs)) {
            nob_log(NOB_ERROR, "codegen: failed while collecting build-step rebuild inputs");
            return false;
        }
        if (!cg_step_emit_rebuild_guard(ctx, sentinel_path, rebuild_inputs, out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting build-step rebuild guard");
            return false;
        }
        if (!cg_step_emit_ensure_declared_paths(sentinel_path, declared_paths, out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting build-step declared path mkdirs");
            return false;
        }
        if (!cg_step_emit_commands(ctx, info, &view, config, out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting build-step commands");
            return false;
        }
        if (!cg_step_emit_verify_declared_paths(declared_paths, out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting build-step declared path checks");
            return false;
        }
        nob_sb_append_cstr(out, "    }\n");
    } else {
        if (!cg_step_emit_ensure_declared_paths(nob_sv_from_cstr(""), declared_paths, out) ||
            !cg_step_emit_commands(ctx, info, &view, config, out) ||
            !cg_step_emit_verify_declared_paths(declared_paths, out)) {
            return false;
        }
    }
    return true;
}

static bool cg_step_emit_config_bodies(CG_Context *ctx,
                                       const CG_Build_Step_Info *info,
                                       bool output_rule,
                                       Nob_String_Builder *out) {
    if (!ctx || !info || !out) return false;
    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        if (!cg_step_emit_runtime_config_branch_prefix(ctx, out, branch) ||
            !cg_step_emit_config_body(ctx, info, config, output_rule, out)) {
            return false;
        }
    }
    return cg_step_emit_runtime_config_branch_suffix(ctx, out);
}

static bool cg_step_emit_verify_declared_paths(const String_View *declared_paths,
                                               Nob_String_Builder *out) {
    if (!out) return false;
    if (arena_arr_len(declared_paths) == 0) return true;
    nob_sb_append_cstr(out, "        if (!require_paths((const char*[]){");
    for (size_t i = 0; i < arena_arr_len(declared_paths); ++i) {
        if (i > 0) nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, declared_paths[i])) return false;
    }
    nob_sb_append_cstr(out, "}, ");
    nob_sb_append_cstr(out, nob_temp_sprintf("%zu", arena_arr_len(declared_paths)));
    nob_sb_append_cstr(out, ")) return false;\n");
    return true;
}

static bool cg_step_emit_finalize(const CG_Build_Step_Info *info,
                                  Nob_String_Builder *out) {
    if (!info || !out) return false;
    nob_sb_append_cstr(out, "    step_state = 2;\n");
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

bool cg_emit_step_function(CG_Context *ctx,
                           const CG_Build_Step_Info *info,
                           Nob_String_Builder *out) {
    bool output_rule = false;
    if (!ctx || !info || !out) return false;

    nob_sb_append_cstr(out, "static bool run_");
    nob_sb_append_cstr(out, info->ident);
    nob_sb_append_cstr(out, "(void) {\n");
    nob_sb_append_cstr(out, "    static int step_state = 0;\n");
    nob_sb_append_cstr(out, "    if (step_state == 2) return true;\n");
    nob_sb_append_cstr(out, "    if (step_state == 1) {\n");
    nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"codegen: build-step dependency cycle detected\");\n");
    nob_sb_append_cstr(out, "        return false;\n");
    nob_sb_append_cstr(out, "    }\n");
    nob_sb_append_cstr(out, "    step_state = 1;\n");

    output_rule = info->kind == BM_BUILD_STEP_OUTPUT_RULE;
    if (!cg_step_emit_dependency_prelude(ctx, info, out)) {
        nob_log(NOB_ERROR, "codegen: failed while emitting build-step dependency prelude");
        return false;
    }
    if (!cg_step_emit_config_bodies(ctx, info, output_rule, out)) {
        nob_log(NOB_ERROR, "codegen: failed while emitting build-step config bodies");
        return false;
    }
    if (!cg_step_emit_finalize(info, out)) {
        nob_log(NOB_ERROR, "codegen: failed while finalizing build-step function");
        return false;
    }

    return true;
}
