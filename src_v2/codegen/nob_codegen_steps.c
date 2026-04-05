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

static bool cg_step_emit_dependency_prelude(CG_Context *ctx,
                                            const CG_Build_Step_Info *info,
                                            Nob_String_Builder *out) {
    BM_Target_Id_Span target_deps = {0};
    BM_Build_Step_Id_Span producer_deps = {0};
    if (!ctx || !info || !out) return false;

    target_deps = bm_query_build_step_target_dependencies(ctx->model, info->id);
    producer_deps = bm_query_build_step_producer_dependencies(ctx->model, info->id);

    for (size_t i = 0; i < target_deps.count; ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, target_deps.items[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    for (size_t i = 0; i < producer_deps.count; ++i) {
        const CG_Build_Step_Info *dep = cg_build_step_info(ctx, producer_deps.items[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    return true;
}

static bool cg_step_collect_rebuild_inputs(CG_Context *ctx,
                                           const CG_Build_Step_Info *info,
                                           String_View **out_inputs) {
    BM_Target_Id_Span target_deps = {0};
    BM_Build_Step_Id_Span producer_deps = {0};
    BM_String_Span file_deps = {0};
    if (!ctx || !info || !out_inputs) return false;

    target_deps = bm_query_build_step_target_dependencies(ctx->model, info->id);
    producer_deps = bm_query_build_step_producer_dependencies(ctx->model, info->id);
    file_deps = bm_query_build_step_file_dependencies(ctx->model, info->id);

    for (size_t i = 0; i < target_deps.count; ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, target_deps.items[i]);
        if (!dep || dep->state_path.count == 0) continue;
        if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, info->sentinel_path, dep->state_path)) {
            return false;
        }
    }
    for (size_t i = 0; i < producer_deps.count; ++i) {
        const CG_Build_Step_Info *dep = cg_build_step_info(ctx, producer_deps.items[i]);
        if (!dep) return false;
        if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, info->sentinel_path, dep->sentinel_path)) {
            return false;
        }
    }
    for (size_t i = 0; i < file_deps.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, file_deps.items[i], &path)) return false;
        if (!cg_step_collect_unique_path(ctx->scratch, out_inputs, info->sentinel_path, path)) {
            return false;
        }
    }
    return true;
}

static bool cg_step_emit_rebuild_guard(CG_Context *ctx,
                                       const CG_Build_Step_Info *info,
                                       const String_View *rebuild_inputs,
                                       Nob_String_Builder *out) {
    size_t rebuild_input_count = 1 + arena_arr_len(rebuild_inputs);
    if (!ctx || !info || !out) return false;
    nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
    if (!cg_sb_append_c_string(out, info->sentinel_path)) return false;
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
                                           BM_Build_Step_Id id,
                                           String_View **out_paths) {
    BM_String_Span outputs = bm_query_build_step_outputs(ctx->model, id);
    BM_String_Span byproducts = bm_query_build_step_byproducts(ctx->model, id);
    if (!ctx || !out_paths) return false;

    for (size_t i = 0; i < outputs.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, outputs.items[i], &path) ||
            !cg_step_collect_unique_path(ctx->scratch, out_paths, (String_View){0}, path)) {
            return false;
        }
    }
    for (size_t i = 0; i < byproducts.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_path_from_cwd(ctx, byproducts.items[i], &path) ||
            !cg_step_collect_unique_path(ctx->scratch, out_paths, (String_View){0}, path)) {
            return false;
        }
    }
    return true;
}

static bool cg_step_emit_ensure_declared_paths(const CG_Build_Step_Info *info,
                                               const String_View *declared_paths,
                                               Nob_String_Builder *out) {
    if (!info || !out) return false;
    if (info->uses_stamp) {
        nob_sb_append_cstr(out, "        if (!ensure_parent_dir(");
        if (!cg_sb_append_c_string(out, info->sentinel_path)) return false;
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
                                  Nob_String_Builder *out) {
    String_View working_dir = {0};
    String_View rebased_working_dir = {0};
    bool has_working_dir = false;
    if (!ctx || !info || !out) return false;

    working_dir = bm_query_build_step_working_directory(ctx->model, info->id);
    has_working_dir = working_dir.count > 0;
    if (has_working_dir && !cg_rebase_path_from_cwd(ctx, working_dir, &rebased_working_dir)) {
        return false;
    }

    for (size_t cmd_index = 0; cmd_index < bm_query_build_step_command_count(ctx->model, info->id); ++cmd_index) {
        BM_String_Span argv = bm_query_build_step_command_argv(ctx->model, info->id, cmd_index);
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
            if (!cg_step_emit_runtime_config_branch_prefix(ctx, out, branch)) return false;
            nob_sb_append_cstr(out, "        {\n");
            nob_sb_append_cstr(out, "            Nob_Cmd step_cmd = {0};\n");
            for (size_t arg = 0; arg < argv.count; ++arg) {
                String_View evaluated = {0};
                if (!cg_eval_string_for_config(ctx,
                                               info->owner_target_id,
                                               BM_QUERY_USAGE_LINK,
                                               config,
                                               nob_sv_from_cstr(""),
                                               argv.items[arg],
                                               &evaluated)) {
                    return false;
                }
                if (!cg_step_emit_command_arg(out, "step_cmd", evaluated, arg == 0)) return false;
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
        if (!cg_step_emit_runtime_config_branch_suffix(ctx, out)) return false;
    }
    return true;
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
    if (info->uses_stamp) {
        nob_sb_append_cstr(out, "        if (!write_stamp(");
        if (!cg_sb_append_c_string(out, info->sentinel_path)) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    nob_sb_append_cstr(out, "    }\n");
    nob_sb_append_cstr(out, "    step_state = 2;\n");
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

bool cg_emit_step_function(CG_Context *ctx,
                           const CG_Build_Step_Info *info,
                           Nob_String_Builder *out) {
    String_View *rebuild_inputs = NULL;
    String_View *declared_paths = NULL;
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

    if (bm_query_build_step_append(ctx->model, info->id)) {
        nob_sb_append_cstr(out,
                           "    nob_log(NOB_ERROR, \"codegen: APPEND custom-command steps are not supported yet\");\n");
        nob_sb_append_cstr(out, "    return false;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    if (!cg_step_emit_dependency_prelude(ctx, info, out) ||
        !cg_step_collect_rebuild_inputs(ctx, info, &rebuild_inputs) ||
        !cg_step_collect_declared_paths(ctx, info->id, &declared_paths) ||
        !cg_step_emit_rebuild_guard(ctx, info, rebuild_inputs, out) ||
        !cg_step_emit_ensure_declared_paths(info, declared_paths, out) ||
        !cg_step_emit_commands(ctx, info, out) ||
        !cg_step_emit_verify_declared_paths(declared_paths, out) ||
        !cg_step_emit_finalize(info, out)) {
        return false;
    }

    return true;
}
