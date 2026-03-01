#include "eval_install.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <string.h>

static bool emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
}

static bool install_emit_diag(Evaluator_Context *ctx,
                              const Node *node,
                              Cmake_Event_Origin o,
                              Cmake_Diag_Severity severity,
                              String_View cause,
                              String_View hint) {
    return eval_emit_diag(ctx,
                          severity,
                          nob_sv_from_cstr("eval_install"),
                          node->as.cmd.name,
                          o,
                          cause,
                          hint);
}

static bool install_emit_rule(Evaluator_Context *ctx,
                              Cmake_Event_Origin o,
                              Cmake_Install_Rule_Type rule_type,
                              String_View item,
                              String_View destination) {
    Cmake_Event ev = {0};
    ev.kind = EV_INSTALL_ADD_RULE;
    ev.origin = o;
    ev.as.install_add_rule.rule_type = rule_type;
    ev.as.install_add_rule.item = sv_copy_to_event_arena(ctx, item);
    ev.as.install_add_rule.destination = sv_copy_to_event_arena(ctx, destination);
    return emit_event(ctx, ev);
}

static bool install_is_files_like_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "DESTINATION") ||
           eval_sv_eq_ci_lit(tok, "TYPE") ||
           eval_sv_eq_ci_lit(tok, "RENAME") ||
           eval_sv_eq_ci_lit(tok, "PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "CONFIGURATIONS") ||
           eval_sv_eq_ci_lit(tok, "COMPONENT") ||
           eval_sv_eq_ci_lit(tok, "OPTIONAL") ||
           eval_sv_eq_ci_lit(tok, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(tok, "FILE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "DIRECTORY_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "USE_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "NO_SOURCE_PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "FILES_MATCHING") ||
           eval_sv_eq_ci_lit(tok, "PATTERN") ||
           eval_sv_eq_ci_lit(tok, "REGEX") ||
           eval_sv_eq_ci_lit(tok, "MESSAGE_NEVER") ||
           eval_sv_eq_ci_lit(tok, "FOLLOW_SYMLINK_CHAIN");
}

static bool install_is_targets_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "EXPORT") ||
           eval_sv_eq_ci_lit(tok, "RUNTIME_DEPENDENCIES") ||
           eval_sv_eq_ci_lit(tok, "RUNTIME_DEPENDENCY_SET") ||
           eval_sv_eq_ci_lit(tok, "INCLUDES") ||
           eval_sv_eq_ci_lit(tok, "DESTINATION") ||
           eval_sv_eq_ci_lit(tok, "PERMISSIONS") ||
           eval_sv_eq_ci_lit(tok, "CONFIGURATIONS") ||
           eval_sv_eq_ci_lit(tok, "COMPONENT") ||
           eval_sv_eq_ci_lit(tok, "NAMELINK_COMPONENT") ||
           eval_sv_eq_ci_lit(tok, "OPTIONAL") ||
           eval_sv_eq_ci_lit(tok, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(tok, "NAMELINK_ONLY") ||
           eval_sv_eq_ci_lit(tok, "NAMELINK_SKIP") ||
           eval_sv_eq_ci_lit(tok, "ARCHIVE") ||
           eval_sv_eq_ci_lit(tok, "LIBRARY") ||
           eval_sv_eq_ci_lit(tok, "RUNTIME") ||
           eval_sv_eq_ci_lit(tok, "OBJECTS") ||
           eval_sv_eq_ci_lit(tok, "FRAMEWORK") ||
           eval_sv_eq_ci_lit(tok, "BUNDLE") ||
           eval_sv_eq_ci_lit(tok, "PRIVATE_HEADER") ||
           eval_sv_eq_ci_lit(tok, "PUBLIC_HEADER") ||
           eval_sv_eq_ci_lit(tok, "RESOURCE") ||
           eval_sv_eq_ci_lit(tok, "FILE_SET") ||
           eval_sv_eq_ci_lit(tok, "CXX_MODULES_BMI");
}

static bool install_destination_from_type(String_View type, String_View *out_destination) {
    if (!out_destination) return false;
    *out_destination = nob_sv_from_cstr("");
    if (eval_sv_eq_ci_lit(type, "BIN")) *out_destination = nob_sv_from_cstr("bin");
    else if (eval_sv_eq_ci_lit(type, "SBIN")) *out_destination = nob_sv_from_cstr("sbin");
    else if (eval_sv_eq_ci_lit(type, "LIB")) *out_destination = nob_sv_from_cstr("lib");
    else if (eval_sv_eq_ci_lit(type, "INCLUDE")) *out_destination = nob_sv_from_cstr("include");
    else if (eval_sv_eq_ci_lit(type, "SYSCONF")) *out_destination = nob_sv_from_cstr("etc");
    else if (eval_sv_eq_ci_lit(type, "SHAREDSTATE")) *out_destination = nob_sv_from_cstr("com");
    else if (eval_sv_eq_ci_lit(type, "LOCALSTATE")) *out_destination = nob_sv_from_cstr("var");
    else if (eval_sv_eq_ci_lit(type, "RUNSTATE")) *out_destination = nob_sv_from_cstr("var/run");
    else if (eval_sv_eq_ci_lit(type, "DATA")) *out_destination = nob_sv_from_cstr("share");
    else if (eval_sv_eq_ci_lit(type, "INFO")) *out_destination = nob_sv_from_cstr("share/info");
    else if (eval_sv_eq_ci_lit(type, "LOCALE")) *out_destination = nob_sv_from_cstr("share/locale");
    else if (eval_sv_eq_ci_lit(type, "MAN")) *out_destination = nob_sv_from_cstr("share/man");
    else if (eval_sv_eq_ci_lit(type, "DOC")) *out_destination = nob_sv_from_cstr("share/doc");
    return out_destination->count > 0;
}

static bool install_collect_destinations(Evaluator_Context *ctx,
                                         const Node *node,
                                         Cmake_Event_Origin o,
                                         SV_List args,
                                         size_t start,
                                         SV_List *out_destinations) {
    if (!ctx || !node || !out_destinations) return false;
    for (size_t i = start; i < args.count; i++) {
        if (!eval_sv_eq_ci_lit(args.items[i], "DESTINATION")) continue;
        if (i + 1 >= args.count) {
            install_emit_diag(ctx,
                              node,
                              o,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("install(... DESTINATION) requires a destination path"),
                              nob_sv_from_cstr("Usage: ... DESTINATION <dir>"));
            return false;
        }
        if (!svu_list_push_temp(ctx, out_destinations, args.items[i + 1])) return false;
        i++;
    }
    return true;
}

static String_View install_tagged_item_temp(Evaluator_Context *ctx, const char *tag, String_View payload) {
    if (!ctx || !tag) return nob_sv_from_cstr("");
    size_t tag_len = strlen(tag);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), tag_len + 2 + payload.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    memcpy(buf, tag, tag_len);
    buf[tag_len] = ':';
    buf[tag_len + 1] = ':';
    if (payload.count > 0) memcpy(buf + tag_len + 2, payload.data, payload.count);
    buf[tag_len + 2 + payload.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool install_handle_files_like(Evaluator_Context *ctx,
                                      const Node *node,
                                      Cmake_Event_Origin o,
                                      SV_List args,
                                      Cmake_Install_Rule_Type rule_type) {
    SV_List items = {0};
    size_t i = 1;
    for (; i < args.count; i++) {
        if (install_is_files_like_keyword(args.items[i])) break;
        if (!svu_list_push_temp(ctx, &items, args.items[i])) return false;
    }

    String_View destination = nob_sv_from_cstr("");
    String_View type = nob_sv_from_cstr("");
    for (; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "DESTINATION")) {
            if (i + 1 >= args.count) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(... DESTINATION) requires a destination path"),
                                  nob_sv_from_cstr("Usage: ... DESTINATION <dir>"));
                return true;
            }
            destination = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "TYPE")) {
            if (i + 1 >= args.count) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(... TYPE) requires a type name"),
                                  nob_sv_from_cstr("Valid examples: BIN, LIB, INCLUDE, DATA, DOC"));
                return true;
            }
            type = args.items[++i];
        } else if (eval_sv_eq_ci_lit(args.items[i], "RENAME") ||
                   eval_sv_eq_ci_lit(args.items[i], "COMPONENT") ||
                   eval_sv_eq_ci_lit(args.items[i], "PATTERN") ||
                   eval_sv_eq_ci_lit(args.items[i], "REGEX")) {
            if (i + 1 < args.count) i++;
        }
    }

    if (destination.count > 0 && type.count > 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() TYPE and DESTINATION cannot be used together"),
                          nob_sv_from_cstr("Choose TYPE or DESTINATION"));
        return true;
    }

    if (destination.count == 0 && type.count > 0) {
        if (!install_destination_from_type(type, &destination)) {
            install_emit_diag(ctx,
                              node,
                              o,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("install() unknown TYPE value"),
                              type);
            return true;
        }
    }

    if (destination.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() missing DESTINATION/TYPE"),
                          nob_sv_from_cstr("Usage: install(FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return true;
    }

    if (items.count == 0) {
        if (rule_type == EV_INSTALL_RULE_DIRECTORY) {
            // CMake allows install(DIRECTORY DESTINATION <dir>) to create destination only.
            return true;
        }
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() has no items for selected signature"),
                          nob_sv_from_cstr("Usage: install(FILES|PROGRAMS|DIRECTORY <items...> DESTINATION <dir>)"));
        return true;
    }

    for (size_t j = 0; j < items.count; j++) {
        if (!install_emit_rule(ctx, o, rule_type, items.items[j], destination)) return false;
    }
    return true;
}

static bool install_handle_targets_like(Evaluator_Context *ctx,
                                        const Node *node,
                                        Cmake_Event_Origin o,
                                        SV_List args,
                                        bool imported_runtime_artifacts) {
    SV_List targets = {0};
    size_t i = 1;
    for (; i < args.count; i++) {
        if (install_is_targets_keyword(args.items[i])) break;
        if (!svu_list_push_temp(ctx, &targets, args.items[i])) return false;
    }

    if (targets.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(TARGETS/IMPORTED_RUNTIME_ARTIFACTS) requires at least one target"),
                          nob_sv_from_cstr("Usage: install(TARGETS <tgt>... DESTINATION <dir>)"));
        return true;
    }

    SV_List destinations = {0};
    if (!install_collect_destinations(ctx, node, o, args, i, &destinations)) return false;
    if (destinations.count == 0) {
        // CMake allows some target installs without explicit destination depending on artifact/category.
        // Keep evaluator behavior permissive and preserve the rule with empty destination.
        if (!svu_list_push_temp(ctx, &destinations, nob_sv_from_cstr(""))) return false;
    }

    for (size_t ti = 0; ti < targets.count; ti++) {
        String_View item = targets.items[ti];
        if (imported_runtime_artifacts) {
            item = install_tagged_item_temp(ctx, "IMPORTED_RUNTIME_ARTIFACTS", item);
            if (ctx->oom) return false;
        }
        for (size_t di = 0; di < destinations.count; di++) {
            if (!install_emit_rule(ctx, o, EV_INSTALL_RULE_TARGET, item, destinations.items[di])) return false;
        }
    }
    return true;
}

static bool install_is_script_code_option(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "ALL_COMPONENTS") ||
           eval_sv_eq_ci_lit(tok, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(tok, "COMPONENT");
}

static bool install_handle_script_code_block(Evaluator_Context *ctx,
                                             const Node *node,
                                             Cmake_Event_Origin o,
                                             SV_List args) {
    bool emitted_any = false;
    for (size_t i = 0; i < args.count; i++) {
        if (eval_sv_eq_ci_lit(args.items[i], "SCRIPT") || eval_sv_eq_ci_lit(args.items[i], "CODE")) {
            bool is_code = eval_sv_eq_ci_lit(args.items[i], "CODE");
            if (i + 1 >= args.count) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  is_code ? nob_sv_from_cstr("install(CODE) requires code content")
                                          : nob_sv_from_cstr("install(SCRIPT) requires a script path"),
                                  is_code ? nob_sv_from_cstr("Usage: install(CODE <code>)")
                                          : nob_sv_from_cstr("Usage: install(SCRIPT <script>)"));
                return true;
            }

            String_View item = install_tagged_item_temp(ctx,
                                                        is_code ? "CODE" : "SCRIPT",
                                                        args.items[i + 1]);
            if (ctx->oom) return false;
            if (!install_emit_rule(ctx, o, EV_INSTALL_RULE_FILE, item, nob_sv_from_cstr(""))) return false;
            emitted_any = true;
            i++;
            continue;
        }

        if (!install_is_script_code_option(args.items[i])) {
            install_emit_diag(ctx,
                              node,
                              o,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("install(SCRIPT/CODE) received unexpected argument"),
                              args.items[i]);
            return true;
        }
        if (eval_sv_eq_ci_lit(args.items[i], "COMPONENT") && i + 1 < args.count) i++;
    }

    if (!emitted_any) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(SCRIPT/CODE) requires at least one SCRIPT or CODE clause"),
                          nob_sv_from_cstr("Usage: install([[SCRIPT <file>] [CODE <code>]] [COMPONENT <component>])"));
    }
    return true;
}

static bool install_handle_export_like(Evaluator_Context *ctx,
                                       const Node *node,
                                       Cmake_Event_Origin o,
                                       SV_List args,
                                       const char *tag) {
    if (args.count < 2) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(EXPORT...) requires an export name"),
                          nob_sv_from_cstr("Usage: install(EXPORT <name> DESTINATION <dir>)"));
        return true;
    }

    SV_List destinations = {0};
    if (!install_collect_destinations(ctx, node, o, args, 2, &destinations)) return false;
    if (destinations.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(EXPORT...) requires DESTINATION"),
                          nob_sv_from_cstr("Usage: install(EXPORT <name> DESTINATION <dir>)"));
        return true;
    }

    String_View item = install_tagged_item_temp(ctx, tag, args.items[1]);
    if (ctx->oom) return false;
    for (size_t i = 0; i < destinations.count; i++) {
        if (!install_emit_rule(ctx, o, EV_INSTALL_RULE_FILE, item, destinations.items[i])) return false;
    }
    return true;
}

static bool install_handle_runtime_dependency_set(Evaluator_Context *ctx,
                                                  const Node *node,
                                                  Cmake_Event_Origin o,
                                                  SV_List args) {
    if (args.count < 2) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(RUNTIME_DEPENDENCY_SET) requires a set name"),
                          nob_sv_from_cstr("Usage: install(RUNTIME_DEPENDENCY_SET <set> DESTINATION <dir>)"));
        return true;
    }

    SV_List destinations = {0};
    if (!install_collect_destinations(ctx, node, o, args, 2, &destinations)) return false;
    if (destinations.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(RUNTIME_DEPENDENCY_SET) requires DESTINATION"),
                          nob_sv_from_cstr("Usage: install(RUNTIME_DEPENDENCY_SET <set> DESTINATION <dir>)"));
        return true;
    }

    String_View item = install_tagged_item_temp(ctx, "RUNTIME_DEPENDENCY_SET", args.items[1]);
    if (ctx->oom) return false;
    for (size_t i = 0; i < destinations.count; i++) {
        if (!install_emit_rule(ctx, o, EV_INSTALL_RULE_TARGET, item, destinations.items[i])) return false;
    }
    return true;
}

bool eval_handle_install(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return !eval_should_stop(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    if (a.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() requires a signature keyword"),
                          nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY|SCRIPT|CODE|EXPORT ...)"));
        return !eval_should_stop(ctx);
    }

    bool ok = true;
    if (eval_sv_eq_ci_lit(a.items[0], "TARGETS")) {
        ok = install_handle_targets_like(ctx, node, o, a, false);
    } else if (eval_sv_eq_ci_lit(a.items[0], "FILES")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_FILE);
    } else if (eval_sv_eq_ci_lit(a.items[0], "PROGRAMS")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_PROGRAM);
    } else if (eval_sv_eq_ci_lit(a.items[0], "DIRECTORY")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_DIRECTORY);
    } else if (eval_sv_eq_ci_lit(a.items[0], "SCRIPT") ||
               eval_sv_eq_ci_lit(a.items[0], "CODE")) {
        ok = install_handle_script_code_block(ctx, node, o, a);
    } else if (eval_sv_eq_ci_lit(a.items[0], "EXPORT")) {
        ok = install_handle_export_like(ctx, node, o, a, "EXPORT");
    } else if (eval_sv_eq_ci_lit(a.items[0], "EXPORT_ANDROID_MK")) {
        ok = install_handle_export_like(ctx, node, o, a, "EXPORT_ANDROID_MK");
    } else if (eval_sv_eq_ci_lit(a.items[0], "RUNTIME_DEPENDENCY_SET")) {
        ok = install_handle_runtime_dependency_set(ctx, node, o, a);
    } else if (eval_sv_eq_ci_lit(a.items[0], "IMPORTED_RUNTIME_ARTIFACTS")) {
        ok = install_handle_targets_like(ctx, node, o, a, true);
    } else {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() unsupported rule type"),
                          a.items[0]);
    }

    if (!ok && !ctx->oom) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() failed due to internal evaluator error"),
                          nob_sv_from_cstr("Check previous diagnostics"));
    }
    return !eval_should_stop(ctx);
}

