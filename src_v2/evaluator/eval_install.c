#include "eval_install.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <string.h>

static String_View install_default_component_name(EvalExecContext *ctx);

static bool install_emit_diag(EvalExecContext *ctx,
                              const Node *node,
                              Cmake_Event_Origin o,
                              Cmake_Diag_Severity severity,
                              String_View cause,
                              String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_install"), node->as.cmd.name, o, cause, hint);
}

static bool install_emit_rule(EvalExecContext *ctx,
                              Cmake_Event_Origin o,
                              Cmake_Install_Rule_Type rule_type,
                              String_View item,
                              String_View destination,
                              String_View component,
                              String_View archive_component,
                              String_View library_component,
                              String_View runtime_component,
                              String_View includes_component,
                              String_View public_header_component,
                              String_View namelink_component,
                              String_View export_name,
                              String_View archive_destination,
                              String_View library_destination,
                              String_View runtime_destination,
                              String_View includes_destination,
                              String_View public_header_destination) {
    if (component.count == 0) component = install_default_component_name(ctx);
    return eval_emit_install_rule_add(ctx,
                                      o,
                                      rule_type,
                                      item,
                                      destination,
                                      component,
                                      archive_component,
                                      library_component,
                                      runtime_component,
                                      includes_component,
                                      public_header_component,
                                      namelink_component,
                                      export_name,
                                      archive_destination,
                                      library_destination,
                                      runtime_destination,
                                      includes_destination,
                                      public_header_destination);
}

typedef struct {
    String_View destination;
    String_View component;
    String_View archive_component;
    String_View library_component;
    String_View runtime_component;
    String_View includes_component;
    String_View public_header_component;
    String_View namelink_component;
    String_View export_name;
    String_View archive_destination;
    String_View library_destination;
    String_View runtime_destination;
    String_View includes_destination;
    String_View public_header_destination;
} Install_Target_Metadata;

static bool install_publish_artifact(EvalExecContext *ctx, String_View signature) {
    if (!ctx) return false;
    String_View install_path =
        eval_sv_path_join(eval_temp_arena(ctx), eval_current_binary_dir(ctx), nob_sv_from_cstr("cmake_install.cmake"));
    if (eval_should_stop(ctx)) return false;

    bool exists = false;
    if (!eval_service_file_exists(ctx, install_path, &exists)) return false;

    Nob_String_Builder sb = {0};
    if (!exists) nob_sb_append_cstr(&sb, "# Nobify evaluator-generated install manifest\n");
    nob_sb_append_cstr(&sb, "# install(");
    nob_sb_append_buf(&sb, signature.data ? signature.data : "", signature.count);
    nob_sb_append_cstr(&sb, ")\n");

    String_View contents = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
    bool ok = eval_write_text_file(ctx, install_path, contents, exists);
    nob_sb_free(sb);
    return ok;
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

typedef enum {
    INSTALL_TARGET_SCOPE_GENERAL = 0,
    INSTALL_TARGET_SCOPE_ARCHIVE,
    INSTALL_TARGET_SCOPE_LIBRARY,
    INSTALL_TARGET_SCOPE_RUNTIME,
    INSTALL_TARGET_SCOPE_INCLUDES,
    INSTALL_TARGET_SCOPE_PUBLIC_HEADER,
} Install_Target_Scope;

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

static bool install_collect_destinations(EvalExecContext *ctx,
                                         const Node *node,
                                         Cmake_Event_Origin o,
                                         SV_List args,
                                         size_t start,
                                         SV_List *out_destinations) {
    if (!ctx || !node || !out_destinations) return false;
    for (size_t i = start; i < arena_arr_len(args); i++) {
        if (!eval_sv_eq_ci_lit(args[i], "DESTINATION")) continue;
        if (i + 1 >= arena_arr_len(args)) {
            install_emit_diag(ctx,
                              node,
                              o,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("install(... DESTINATION) requires a destination path"),
                              nob_sv_from_cstr("Usage: ... DESTINATION <dir>"));
            return false;
        }
        if (!svu_list_push_temp(ctx, out_destinations, args[i + 1])) return false;
        i++;
    }
    return true;
}

static String_View install_tagged_item_temp(EvalExecContext *ctx, const char *tag, String_View payload) {
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

static String_View install_default_component_name(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("Unspecified");
    String_View configured =
        eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_INSTALL_DEFAULT_COMPONENT_NAME"));
    return configured.count > 0 ? configured : nob_sv_from_cstr("Unspecified");
}

static bool install_register_component_if_present(EvalExecContext *ctx, String_View component) {
    if (!ctx || component.count == 0) return true;
    return eval_install_component_register(ctx, component);
}

static bool install_register_declared_components(EvalExecContext *ctx,
                                                 SV_List args,
                                                 bool allow_namelink_component) {
    if (!ctx) return false;

    bool saw_component = false;
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "COMPONENT")) {
            if (i + 1 >= arena_arr_len(args)) continue;
            saw_component = true;
            if (!install_register_component_if_present(ctx, args[++i])) return false;
            continue;
        }

        if (allow_namelink_component && eval_sv_eq_ci_lit(args[i], "NAMELINK_COMPONENT")) {
            if (i + 1 >= arena_arr_len(args)) continue;
            if (!install_register_component_if_present(ctx, args[++i])) return false;
        }
    }

    if (saw_component) return true;
    return install_register_component_if_present(ctx, install_default_component_name(ctx));
}

static bool install_handle_files_like(EvalExecContext *ctx,
                                      const Node *node,
                                      Cmake_Event_Origin o,
                                      SV_List args,
                                      Cmake_Install_Rule_Type rule_type) {
    SV_List items = NULL;
    size_t i = 1;
    for (; i < arena_arr_len(args); i++) {
        if (install_is_files_like_keyword(args[i])) break;
        if (!svu_list_push_temp(ctx, &items, args[i])) return false;
    }

    String_View destination = nob_sv_from_cstr("");
    String_View type = nob_sv_from_cstr("");
    String_View component = nob_sv_from_cstr("");
    for (; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "DESTINATION")) {
            if (i + 1 >= arena_arr_len(args)) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(... DESTINATION) requires a destination path"),
                                  nob_sv_from_cstr("Usage: ... DESTINATION <dir>"));
                return true;
            }
            destination = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "TYPE")) {
            if (i + 1 >= arena_arr_len(args)) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(... TYPE) requires a type name"),
                                  nob_sv_from_cstr("Valid examples: BIN, LIB, INCLUDE, DATA, DOC"));
                return true;
            }
            type = args[++i];
        } else if (eval_sv_eq_ci_lit(args[i], "RENAME") ||
                   eval_sv_eq_ci_lit(args[i], "PATTERN") ||
                   eval_sv_eq_ci_lit(args[i], "REGEX")) {
            if (i + 1 < arena_arr_len(args)) i++;
        } else if (eval_sv_eq_ci_lit(args[i], "COMPONENT")) {
            if (i + 1 < arena_arr_len(args)) component = args[++i];
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

    if (arena_arr_len(items) == 0) {
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

    for (size_t j = 0; j < arena_arr_len(items); j++) {
        if (!install_emit_rule(ctx,
                               o,
                               rule_type,
                               items[j],
                               destination,
                               component,
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""))) {
            return false;
        }
    }
    return true;
}

static bool install_handle_targets_like(EvalExecContext *ctx,
                                        const Node *node,
                                        Cmake_Event_Origin o,
                                        SV_List args,
                                        bool imported_runtime_artifacts) {
    SV_List targets = NULL;
    size_t i = 1;
    for (; i < arena_arr_len(args); i++) {
        if (install_is_targets_keyword(args[i])) break;
        if (!svu_list_push_temp(ctx, &targets, args[i])) return false;
    }

    if (arena_arr_len(targets) == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(TARGETS/IMPORTED_RUNTIME_ARTIFACTS) requires at least one target"),
                          nob_sv_from_cstr("Usage: install(TARGETS <tgt>... DESTINATION <dir>)"));
        return true;
    }

    Install_Target_Metadata meta = {0};
    Install_Target_Scope scope = INSTALL_TARGET_SCOPE_GENERAL;
    for (size_t j = i; j < arena_arr_len(args); j++) {
        if (eval_sv_eq_ci_lit(args[j], "EXPORT")) {
            if (j + 1 >= arena_arr_len(args)) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(TARGETS ... EXPORT ...) requires an export name"),
                                  nob_sv_from_cstr("Usage: install(TARGETS <tgt>... EXPORT <name> ...)"));
                return true;
            }
            meta.export_name = args[++j];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "COMPONENT")) {
            if (j + 1 < arena_arr_len(args)) {
                String_View value = args[++j];
                switch (scope) {
                    case INSTALL_TARGET_SCOPE_ARCHIVE: meta.archive_component = value; break;
                    case INSTALL_TARGET_SCOPE_LIBRARY: meta.library_component = value; break;
                    case INSTALL_TARGET_SCOPE_RUNTIME: meta.runtime_component = value; break;
                    case INSTALL_TARGET_SCOPE_INCLUDES: meta.includes_component = value; break;
                    case INSTALL_TARGET_SCOPE_PUBLIC_HEADER: meta.public_header_component = value; break;
                    case INSTALL_TARGET_SCOPE_GENERAL: meta.component = value; break;
                }
            }
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "NAMELINK_COMPONENT")) {
            if (j + 1 < arena_arr_len(args)) meta.namelink_component = args[++j];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "ARCHIVE")) {
            scope = INSTALL_TARGET_SCOPE_ARCHIVE;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "LIBRARY")) {
            scope = INSTALL_TARGET_SCOPE_LIBRARY;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "RUNTIME")) {
            scope = INSTALL_TARGET_SCOPE_RUNTIME;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "INCLUDES")) {
            scope = INSTALL_TARGET_SCOPE_INCLUDES;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "PUBLIC_HEADER")) {
            scope = INSTALL_TARGET_SCOPE_PUBLIC_HEADER;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[j], "DESTINATION")) {
            String_View value = nob_sv_from_cstr("");
            if (j + 1 >= arena_arr_len(args)) {
                install_emit_diag(ctx,
                                  node,
                                  o,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("install(TARGETS ... DESTINATION) requires a destination path"),
                                  nob_sv_from_cstr("Usage: install(TARGETS <tgt>... DESTINATION <dir>)"));
                return true;
            }
            value = args[++j];
            switch (scope) {
                case INSTALL_TARGET_SCOPE_ARCHIVE: meta.archive_destination = value; break;
                case INSTALL_TARGET_SCOPE_LIBRARY: meta.library_destination = value; break;
                case INSTALL_TARGET_SCOPE_RUNTIME: meta.runtime_destination = value; break;
                case INSTALL_TARGET_SCOPE_INCLUDES: meta.includes_destination = value; break;
                case INSTALL_TARGET_SCOPE_PUBLIC_HEADER: meta.public_header_destination = value; break;
                case INSTALL_TARGET_SCOPE_GENERAL: meta.destination = value; break;
            }
            continue;
        }
    }

    if (meta.export_name.count > 0) {
        size_t total = strlen("NOBIFY_INSTALL_EXPORT::") + meta.export_name.count + strlen("::TARGETS");
        char *key_buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, key_buf, false);
        memcpy(key_buf, "NOBIFY_INSTALL_EXPORT::", strlen("NOBIFY_INSTALL_EXPORT::"));
        memcpy(key_buf + strlen("NOBIFY_INSTALL_EXPORT::"), meta.export_name.data, meta.export_name.count);
        memcpy(key_buf + strlen("NOBIFY_INSTALL_EXPORT::") + meta.export_name.count, "::TARGETS", strlen("::TARGETS"));
        key_buf[total] = '\0';
        if (!eval_var_set_current(ctx,
                          nob_sv_from_parts(key_buf, total),
                          eval_sv_join_semi_temp(ctx, targets, arena_arr_len(targets)))) {
            return false;
        }
    }

    for (size_t ti = 0; ti < arena_arr_len(targets); ti++) {
        String_View item = targets[ti];
        if (imported_runtime_artifacts) {
            item = install_tagged_item_temp(ctx, "IMPORTED_RUNTIME_ARTIFACTS", item);
            if (ctx->oom) return false;
        }
        if (!install_emit_rule(ctx,
                               o,
                               EV_INSTALL_RULE_TARGET,
                               item,
                               meta.destination,
                               meta.component,
                               meta.archive_component,
                               meta.library_component,
                               meta.runtime_component,
                               meta.includes_component,
                               meta.public_header_component,
                               meta.namelink_component,
                               meta.export_name,
                               meta.archive_destination,
                               meta.library_destination,
                               meta.runtime_destination,
                               meta.includes_destination,
                               meta.public_header_destination)) {
            return false;
        }
    }
    return true;
}

static bool install_is_script_code_option(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "ALL_COMPONENTS") ||
           eval_sv_eq_ci_lit(tok, "EXCLUDE_FROM_ALL") ||
           eval_sv_eq_ci_lit(tok, "COMPONENT");
}

static bool install_handle_script_code_block(EvalExecContext *ctx,
                                             const Node *node,
                                             Cmake_Event_Origin o,
                                             SV_List args) {
    bool emitted_any = false;
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "SCRIPT") || eval_sv_eq_ci_lit(args[i], "CODE")) {
            bool is_code = eval_sv_eq_ci_lit(args[i], "CODE");
            if (i + 1 >= arena_arr_len(args)) {
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
                                                        args[i + 1]);
            if (ctx->oom) return false;
            if (!install_emit_rule(ctx,
                                   o,
                                   EV_INSTALL_RULE_FILE,
                                   item,
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""),
                                   nob_sv_from_cstr(""))) {
                return false;
            }
            emitted_any = true;
            i++;
            continue;
        }

        if (!install_is_script_code_option(args[i])) {
            install_emit_diag(ctx,
                              node,
                              o,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("install(SCRIPT/CODE) received unexpected argument"),
                              args[i]);
            return true;
        }
        if (eval_sv_eq_ci_lit(args[i], "COMPONENT") && i + 1 < arena_arr_len(args)) i++;
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

static bool install_handle_export_like(EvalExecContext *ctx,
                                       const Node *node,
                                       Cmake_Event_Origin o,
                                       SV_List args,
                                       const char *tag) {
    if (arena_arr_len(args) < 2) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(EXPORT...) requires an export name"),
                          nob_sv_from_cstr("Usage: install(EXPORT <name> DESTINATION <dir>)"));
        return true;
    }

    String_View destination = nob_sv_from_cstr("");
    String_View export_namespace = nob_sv_from_cstr("");
    String_View file_name = nob_sv_from_cstr("");
    String_View component = nob_sv_from_cstr("");
    for (size_t i = 2; i < arena_arr_len(args); ++i) {
        if (eval_sv_eq_ci_lit(args[i], "DESTINATION")) {
            if (i + 1 < arena_arr_len(args)) destination = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "NAMESPACE")) {
            if (i + 1 < arena_arr_len(args)) export_namespace = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "FILE")) {
            if (i + 1 < arena_arr_len(args)) file_name = args[++i];
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "COMPONENT")) {
            if (i + 1 < arena_arr_len(args)) component = args[++i];
            continue;
        }
    }
    if (destination.count == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(EXPORT...) requires DESTINATION"),
                          nob_sv_from_cstr("Usage: install(EXPORT <name> DESTINATION <dir>)"));
        return true;
    }

    if (strcmp(tag, "EXPORT") == 0) {
        if (component.count == 0) component = install_default_component_name(ctx);
        return eval_emit_export_install(ctx, o, args[1], destination, export_namespace, file_name, component);
    }
    {
        String_View item = install_tagged_item_temp(ctx, tag, args[1]);
        if (ctx->oom) return false;
        return install_emit_rule(ctx,
                                 o,
                                 EV_INSTALL_RULE_FILE,
                                 item,
                                 destination,
                                 component,
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""),
                                 nob_sv_from_cstr(""));
    }
}

static bool install_handle_runtime_dependency_set(EvalExecContext *ctx,
                                                  const Node *node,
                                                  Cmake_Event_Origin o,
                                                  SV_List args) {
    if (arena_arr_len(args) < 2) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(RUNTIME_DEPENDENCY_SET) requires a set name"),
                          nob_sv_from_cstr("Usage: install(RUNTIME_DEPENDENCY_SET <set> DESTINATION <dir>)"));
        return true;
    }

    SV_List destinations = NULL;
    if (!install_collect_destinations(ctx, node, o, args, 2, &destinations)) return false;
    if (arena_arr_len(destinations) == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install(RUNTIME_DEPENDENCY_SET) requires DESTINATION"),
                          nob_sv_from_cstr("Usage: install(RUNTIME_DEPENDENCY_SET <set> DESTINATION <dir>)"));
        return true;
    }

    String_View item = install_tagged_item_temp(ctx, "RUNTIME_DEPENDENCY_SET", args[1]);
    if (ctx->oom) return false;
    for (size_t i = 0; i < arena_arr_len(destinations); i++) {
        if (!install_emit_rule(ctx,
                               o,
                               EV_INSTALL_RULE_TARGET,
                               item,
                               destinations[i],
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""),
                               nob_sv_from_cstr(""))) {
            return false;
        }
    }
    return true;
}

Eval_Result eval_handle_install(EvalExecContext *ctx, const Node *node) {
    if (!ctx || !node || eval_should_stop(ctx)) return eval_result_from_ctx(ctx);
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) == 0) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() requires a signature keyword"),
                          nob_sv_from_cstr("Usage: install(TARGETS|FILES|PROGRAMS|DIRECTORY|SCRIPT|CODE|EXPORT ...)"));
        return eval_result_from_ctx(ctx);
    }

    size_t errors_before = ctx->runtime_state.run_report.error_count;
    bool ok = true;
    bool allow_namelink_component = false;
    if (eval_sv_eq_ci_lit(a[0], "TARGETS")) {
        allow_namelink_component = true;
        ok = install_handle_targets_like(ctx, node, o, a, false);
    } else if (eval_sv_eq_ci_lit(a[0], "FILES")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_FILE);
    } else if (eval_sv_eq_ci_lit(a[0], "PROGRAMS")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_PROGRAM);
    } else if (eval_sv_eq_ci_lit(a[0], "DIRECTORY")) {
        ok = install_handle_files_like(ctx, node, o, a, EV_INSTALL_RULE_DIRECTORY);
    } else if (eval_sv_eq_ci_lit(a[0], "SCRIPT") ||
               eval_sv_eq_ci_lit(a[0], "CODE")) {
        ok = install_handle_script_code_block(ctx, node, o, a);
    } else if (eval_sv_eq_ci_lit(a[0], "EXPORT")) {
        ok = install_handle_export_like(ctx, node, o, a, "EXPORT");
    } else if (eval_sv_eq_ci_lit(a[0], "EXPORT_ANDROID_MK")) {
        ok = install_handle_export_like(ctx, node, o, a, "EXPORT_ANDROID_MK");
    } else if (eval_sv_eq_ci_lit(a[0], "RUNTIME_DEPENDENCY_SET")) {
        ok = install_handle_runtime_dependency_set(ctx, node, o, a);
    } else if (eval_sv_eq_ci_lit(a[0], "IMPORTED_RUNTIME_ARTIFACTS")) {
        ok = install_handle_targets_like(ctx, node, o, a, true);
    } else {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() unsupported rule type"),
                          a[0]);
    }

    if (ok && !ctx->oom && ctx->runtime_state.run_report.error_count == errors_before) {
        ok = install_publish_artifact(ctx, a[0]);
    }

    if (ok && !ctx->oom && ctx->runtime_state.run_report.error_count == errors_before) {
        ok = install_register_declared_components(ctx, a, allow_namelink_component);
    }

    if (!ok && !ctx->oom) {
        install_emit_diag(ctx,
                          node,
                          o,
                          EV_DIAG_ERROR,
                          nob_sv_from_cstr("install() failed due to internal evaluator error"),
                          nob_sv_from_cstr("Check previous diagnostics"));
    }
    return eval_result_from_ctx(ctx);
}
