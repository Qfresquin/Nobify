#include "eval_target_internal.h"

Eval_Result eval_handle_add_dependencies(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_dependencies() requires target and at least one dependency"), nob_sv_from_cstr("Usage: add_dependencies(<target> <target-dependency>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View target_name = a[0];
    if (!eval_target_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() target was not declared"), target_name);
        return eval_result_from_ctx(ctx);
    }
    if (eval_target_alias_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot be used on ALIAS targets"), target_name);
        return eval_result_from_ctx(ctx);
    }

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View dep = a[i];
        if (dep.count == 0) continue;

        if (!eval_target_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() dependency target was not declared"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (eval_target_alias_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot depend on ALIAS targets"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_dependency(ctx, o, target_name, dep)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_libraries(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx) || arena_arr_len(a) < 2) return eval_result_from_ctx(ctx);

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View qualifier = nob_sv_from_cstr("");

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "DEBUG") ||
            eval_sv_eq_ci_lit(a[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(a[i], "GENERAL")) {
            qualifier = a[i];
            continue;
        }

        String_View item = a[i];
        if (eval_sv_eq_ci_lit(qualifier, "DEBUG")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<CONFIG:Debug>:"));
        } else if (eval_sv_eq_ci_lit(qualifier, "OPTIMIZED")) {
            item = wrap_link_item_with_config_genex_temp(ctx,
                                                         item,
                                                         nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:"));
        }

        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_LIBRARIES"),
                                 item,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_libraries(ctx, o, tgt, vis, item)) return eval_result_from_ctx(ctx);
        qualifier = nob_sv_from_cstr("");
    }

    if (qualifier.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("target_link_libraries() qualifier without following item"), qualifier);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_link_options() requires target and items"), nob_sv_from_cstr("Usage: target_link_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        (void)vis;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_OPTIONS"),
                                 a[i],
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_options(ctx, o, tgt, vis, a[i], is_before)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_link_directories() requires target and items"), nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("LINK_DIRECTORIES"),
                                 resolved,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_link_directories(ctx, o, tgt, vis, resolved)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_include_directories(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_include_directories() requires target and items"), nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_system = false;
    bool is_before = false;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "AFTER")) {
            is_before = false;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        (void)vis;
        (void)is_system;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("INCLUDE_DIRECTORIES"),
                                 resolved,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_include_directories(ctx, o, tgt, vis, resolved, is_system, is_before)) {
            return eval_result_from_ctx(ctx);
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_definitions(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_definitions() requires target and items"), nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        String_View item = eval_normalize_compile_definition_item(a[i]);
        if (item.count == 0) continue;

        (void)vis;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("COMPILE_DEFINITIONS"),
                                 item,
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_compile_definitions(ctx, o, tgt, vis, item)) return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_options(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_options() requires target and items"), nob_sv_from_cstr("Usage: target_compile_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);
    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (eval_sv_eq_ci_lit(a[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PRIVATE")) {
            vis = EV_VISIBILITY_PRIVATE;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PUBLIC")) {
            vis = EV_VISIBILITY_PUBLIC;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "INTERFACE")) {
            vis = EV_VISIBILITY_INTERFACE;
            continue;
        }

        (void)vis;
        (void)is_before;
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 tgt,
                                 nob_sv_from_cstr("COMPILE_OPTIONS"),
                                 a[i],
                                 EV_PROP_APPEND_LIST,
                                 true)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_compile_options(ctx, o, tgt, vis, a[i], is_before)) return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_sources(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_sources() requires target, visibility and items"), nob_sv_from_cstr("Usage: target_sources(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    String_View cur_src = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (eval_sv_eq_ci_lit(a[i], "FILE_SET")) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_IMPLEMENTED, "dispatcher", nob_sv_from_cstr("target_sources(FILE_SET ...) is not implemented yet"), nob_sv_from_cstr("Supported in this batch: source item groups with PRIVATE, PUBLIC and INTERFACE"));
            return eval_result_from_ctx(ctx);
        }
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }

        String_View item = eval_path_resolve_for_cmake_arg(ctx, a[i], cur_src, true);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!eval_emit_target_add_source(ctx, o, tgt, item)) return eval_result_from_ctx(ctx);
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!eval_emit_target_prop_set(ctx,
                                           o,
                                           tgt,
                                           nob_sv_from_cstr("INTERFACE_SOURCES"),
                                           item,
                                           EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_features(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_compile_features() requires target, visibility and features"), nob_sv_from_cstr("Usage: target_compile_features(<tgt> <PUBLIC|PRIVATE|INTERFACE> <features...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }
        if (a[i].count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!eval_emit_target_prop_set(ctx,
                                           o,
                                           tgt,
                                           nob_sv_from_cstr("COMPILE_FEATURES"),
                                           a[i],
                                           EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!eval_emit_target_prop_set(ctx,
                                           o,
                                           tgt,
                                           nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                           a[i],
                                           EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_precompile_headers(Evaluator_Context *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_precompile_headers() requires target and arguments"), nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> <PUBLIC|PRIVATE|INTERFACE> <headers...>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View tgt = a[0];
    if (!target_usage_validate_target(ctx, node, tgt)) return eval_result_from_ctx(ctx);

    if (eval_sv_eq_ci_lit(a[1], "REUSE_FROM")) {
        if (arena_arr_len(a) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("target_precompile_headers(REUSE_FROM) expects exactly one donor target"), nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> REUSE_FROM <other-target>)"));
            return eval_result_from_ctx(ctx);
        }
        if (!target_usage_validate_target(ctx, node, a[2])) return eval_result_from_ctx(ctx);
        if (!eval_emit_target_prop_set(ctx,
                                       o,
                                       tgt,
                                       nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM"),
                                       a[2],
                                       EV_PROP_SET)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_sv_key_eq(tgt, a[2])) {
            if (!eval_emit_target_dependency(ctx, o, tgt, a[2])) return eval_result_from_ctx(ctx);
        }
        return eval_result_from_ctx(ctx);
    }

    Cmake_Visibility vis = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = 1; i < arena_arr_len(a); i++) {
        if (target_usage_parse_visibility(a[i], &vis)) continue;
        if (vis == EV_VISIBILITY_UNSPECIFIED) {
            (void)target_usage_require_visibility(ctx, node);
            return eval_result_from_ctx(ctx);
        }

        String_View item = target_pch_item_normalize_temp(ctx, a[i]);
        if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
        if (item.count == 0) continue;

        if (vis != EV_VISIBILITY_INTERFACE) {
            if (!eval_emit_target_prop_set(ctx,
                                           o,
                                           tgt,
                                           nob_sv_from_cstr("PRECOMPILE_HEADERS"),
                                           item,
                                           EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (vis != EV_VISIBILITY_PRIVATE) {
            if (!eval_emit_target_prop_set(ctx,
                                           o,
                                           tgt,
                                           nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS"),
                                           item,
                                           EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
    }

    return eval_result_from_ctx(ctx);
}
