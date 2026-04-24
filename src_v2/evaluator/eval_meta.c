#include "eval_meta.h"

#include "eval_file_internal.h"
#include "eval_hash.h"
#include "eval_package_internal.h"
#include "evaluator_internal.h"
#include "sv_utils.h"
#include "stb_ds.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool meta_emit_diag(EvalExecContext *ctx,
                           const Node *node,
                           Cmake_Diag_Severity severity,
                           String_View cause,
                           String_View hint) {
    return EVAL_DIAG_BOOL_SEV(ctx, severity, EVAL_DIAG_INVALID_VALUE, nob_sv_from_cstr("eval_meta"), node->as.cmd.name, eval_origin_from_node(ctx, node), cause, hint);
}

static String_View meta_current_bin_dir(EvalExecContext *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View meta_concat3_temp(EvalExecContext *ctx,
                                     const char *prefix,
                                     String_View middle,
                                     const char *suffix) {
    if (!ctx || !prefix || !suffix) return nob_sv_from_cstr("");
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + middle.count + suffix_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, prefix, prefix_len);
    if (middle.count > 0) memcpy(buf + prefix_len, middle.data, middle.count);
    memcpy(buf + prefix_len + middle.count, suffix, suffix_len);
    buf[prefix_len + middle.count + suffix_len] = '\0';
    return nob_sv_from_parts(buf, prefix_len + middle.count + suffix_len);
}

static bool meta_sv_ends_with_ci_lit(String_View value, const char *suffix) {
    if (!suffix) return false;
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value.count) return false;
    size_t start = value.count - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (tolower((unsigned char)value.data[start + i]) !=
            tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static String_View meta_path_basename_temp(EvalExecContext *ctx, String_View path) {
    size_t start = 0;
    if (!ctx || path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = 0; i < path.count; ++i) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = i + 1;
    }
    return nob_sv_from_parts(path.data + start, path.count - start);
}

static bool meta_path_has_prefix(String_View path, String_View prefix) {
    if (prefix.count == 0 || path.count < prefix.count) return false;
    if (!nob_sv_starts_with(path, prefix)) return false;
    if (path.count == prefix.count) return true;
    return path.data[prefix.count] == '/' || path.data[prefix.count] == '\\';
}

static Event_Origin meta_current_origin(EvalExecContext *ctx) {
    Event_Origin origin = {0};
    if (!ctx) return origin;
    origin.file_path = ctx->current_file
        ? nob_sv_from_cstr(ctx->current_file)
        : eval_var_get_visible(ctx, nob_sv_from_cstr(EVAL_VAR_CURRENT_LIST_FILE));
    return origin;
}

static String_View meta_visible_var_or(EvalExecContext *ctx,
                                       const char *name,
                                       String_View fallback) {
    String_View value = {0};
    if (!ctx || !name) return fallback;
    value = eval_var_get_visible(ctx, nob_sv_from_cstr(name));
    return value.count > 0 ? value : fallback;
}

static void meta_sb_append_cmake_escaped(Nob_String_Builder *sb, String_View value) {
    if (!sb || value.count == 0) return;
    for (size_t i = 0; i < value.count; i++) {
        char c = value.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '"') {
            nob_sb_append_cstr(sb, "\\\"");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else {
            nob_sb_append_buf(sb, &c, 1);
        }
    }
}

static void meta_sb_append_set_line(Nob_String_Builder *sb,
                                    const char *key,
                                    String_View value) {
    if (!sb || !key) return;
    nob_sb_append_cstr(sb, "set(");
    nob_sb_append_cstr(sb, key);
    nob_sb_append_cstr(sb, " \"");
    meta_sb_append_cmake_escaped(sb, value);
    nob_sb_append_cstr(sb, "\")\n");
}

static void meta_sb_append_include_line(Nob_String_Builder *sb,
                                        String_View include_path,
                                        bool optional) {
    if (!sb || include_path.count == 0) return;
    nob_sb_append_cstr(sb, "include(\"");
    meta_sb_append_cmake_escaped(sb, include_path);
    nob_sb_append_cstr(sb, "\"");
    if (optional) nob_sb_append_cstr(sb, " OPTIONAL");
    nob_sb_append_cstr(sb, ")\n");
}

static String_View meta_target_property_temp(EvalExecContext *ctx,
                                             String_View target_name,
                                             String_View property_name,
                                             bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View prop_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!eval_property_engine_get(ctx, nob_sv_from_cstr("TARGET"), target_name, prop_upper, &value, &have)) {
        return nob_sv_from_cstr("");
    }
    if (out_set) *out_set = have;
    return have ? value : nob_sv_from_cstr("");
}

static bool meta_store_target_property(EvalExecContext *ctx,
                                       Cmake_Event_Origin origin,
                                       String_View target_name,
                                       String_View property_name,
                                       String_View value,
                                       Cmake_Target_Property_Op op) {
    if (!ctx) return false;
    if (!eval_property_write(ctx,
                             origin,
                             nob_sv_from_cstr("TARGET"),
                             target_name,
                             property_name,
                             value,
                             op,
                             false)) {
        return false;
    }
    return eval_emit_target_prop_set(ctx, origin, target_name, property_name, value, op);
}

static String_View meta_target_export_name_temp(EvalExecContext *ctx,
                                                String_View target_name) {
    bool have = false;
    String_View export_name = meta_target_property_temp(ctx,
                                                        target_name,
                                                        nob_sv_from_cstr("EXPORT_NAME"),
                                                        &have);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have && export_name.count > 0) return export_name;
    return target_name;
}

static bool meta_sv_list_push_unique_temp(EvalExecContext *ctx,
                                          SV_List *list,
                                          String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (eval_sv_key_eq((*list)[i], value)) return true;
    }
    return svu_list_push_temp(ctx, list, value);
}

static bool meta_sv_list_append_split_unique_temp(EvalExecContext *ctx,
                                                  SV_List *list,
                                                  String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;

    SV_List parts = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), value, &parts)) return false;
    for (size_t i = 0; i < arena_arr_len(parts); i++) {
        if (parts[i].count == 0) continue;
        if (!meta_sv_list_push_unique_temp(ctx, list, parts[i])) return false;
    }
    return true;
}

static bool meta_target_collect_cxx_module_sets_temp(EvalExecContext *ctx,
                                                     String_View target_name,
                                                     SV_List *out_sets) {
    if (!ctx || !out_sets) return false;
    *out_sets = NULL;

    bool have_value = false;
    String_View sets = meta_target_property_temp(ctx,
                                                 target_name,
                                                 nob_sv_from_cstr("CXX_MODULE_SETS"),
                                                 &have_value);
    if (eval_should_stop(ctx)) return false;
    if (have_value && !meta_sv_list_append_split_unique_temp(ctx, out_sets, sets)) return false;

    have_value = false;
    String_View iface_sets = meta_target_property_temp(ctx,
                                                       target_name,
                                                       nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS"),
                                                       &have_value);
    if (eval_should_stop(ctx)) return false;
    if (have_value && !meta_sv_list_append_split_unique_temp(ctx, out_sets, iface_sets)) return false;

    bool have_default_set = false;
    bool have_default_dirs = false;
    (void)meta_target_property_temp(ctx,
                                    target_name,
                                    nob_sv_from_cstr("CXX_MODULE_SET"),
                                    &have_default_set);
    if (eval_should_stop(ctx)) return false;
    (void)meta_target_property_temp(ctx,
                                    target_name,
                                    nob_sv_from_cstr("CXX_MODULE_DIRS"),
                                    &have_default_dirs);
    if (eval_should_stop(ctx)) return false;
    if ((have_default_set || have_default_dirs) &&
        !meta_sv_list_push_unique_temp(ctx, out_sets, nob_sv_from_cstr("CXX_MODULES"))) {
        return false;
    }

    return true;
}

static String_View meta_export_cxx_modules_name_temp(EvalExecContext *ctx,
                                                     String_View export_name,
                                                     SV_List targets) {
    if (!ctx) return nob_sv_from_cstr("");
    if (export_name.count > 0) return export_name;

    size_t total = 0;
    for (size_t i = 0; i < arena_arr_len(targets); i++) total += targets[i].count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < arena_arr_len(targets); i++) {
        if (targets[i].count > 0) {
            memcpy(buf + off, targets[i].data, targets[i].count);
            off += targets[i].count;
        }
    }
    buf[off] = '\0';

    String_View digest = nob_sv_from_cstr("");
    if (!eval_hash_compute_hex_temp(ctx,
                                    nob_sv_from_cstr("SHA3_512"),
                                    nob_sv_from_parts(buf, off),
                                    &digest)) {
        return nob_sv_from_cstr("");
    }
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (digest.count > 12) digest.count = 12;
    return digest;
}

static String_View meta_export_cxx_modules_include_path_temp(EvalExecContext *ctx,
                                                             String_View cxx_modules_directory,
                                                             String_View cxx_modules_name) {
    if (!ctx || cxx_modules_directory.count == 0 || cxx_modules_name.count == 0) {
        return nob_sv_from_cstr("");
    }

    String_View rel_name = nob_sv_from_cstr(nob_temp_sprintf("cxx-modules-%.*s.cmake",
                                                             (int)cxx_modules_name.count,
                                                             cxx_modules_name.data));
    if (eval_sv_is_abs_path(cxx_modules_directory)) {
        return eval_sv_path_join(eval_temp_arena(ctx), cxx_modules_directory, rel_name);
    }
    return svu_join_no_sep_temp(ctx,
                                (String_View[]){
                                    nob_sv_from_cstr("${CMAKE_CURRENT_LIST_DIR}/"),
                                    cxx_modules_directory,
                                    nob_sv_from_cstr("/"),
                                    rel_name,
                                },
                                4);
}

static String_View meta_export_cxx_modules_abs_dir_temp(EvalExecContext *ctx,
                                                        String_View export_file_path,
                                                        String_View cxx_modules_directory) {
    if (!ctx || cxx_modules_directory.count == 0) return nob_sv_from_cstr("");
    if (eval_sv_is_abs_path(cxx_modules_directory)) return cxx_modules_directory;

    String_View file_dir = svu_dirname(export_file_path);
    return eval_sv_path_join(eval_temp_arena(ctx), file_dir, cxx_modules_directory);
}

static String_View meta_current_list_include_temp(EvalExecContext *ctx,
                                                  String_View relative_or_absolute_path) {
    if (!ctx || relative_or_absolute_path.count == 0) return nob_sv_from_cstr("");
    if (eval_sv_is_abs_path(relative_or_absolute_path)) return relative_or_absolute_path;
    return svu_join_no_sep_temp(ctx,
                                (String_View[]){
                                    nob_sv_from_cstr("${CMAKE_CURRENT_LIST_DIR}/"),
                                    relative_or_absolute_path,
                                },
                                2);
}

typedef enum {
    META_EXPORT_SIG_NONE = 0,
    META_EXPORT_SIG_TARGETS,
    META_EXPORT_SIG_EXPORT,
    META_EXPORT_SIG_PACKAGE,
} Meta_Export_Signature;

typedef struct {
    Meta_Export_Signature signature;
    bool append;
    String_View file_path;
    String_View ns;
    String_View export_name;
    String_View cxx_modules_directory;
    SV_List targets;
} Meta_Export_Request;

typedef struct {
    String_View name;
    String_View location;
    String_View type_guid;
    String_View project_guid;
    String_View platform;
    SV_List deps;
} Meta_Include_External_MSProject_Request;

static String_View meta_export_logical_name_temp(EvalExecContext *ctx,
                                                 const Meta_Export_Request *req,
                                                 String_View file_path) {
    String_View base = {0};
    if (!ctx || !req) return nob_sv_from_cstr("");
    if (req->signature == META_EXPORT_SIG_EXPORT || req->signature == META_EXPORT_SIG_PACKAGE) {
        return req->export_name;
    }
    base = meta_path_basename_temp(ctx, file_path);
    if (base.count >= strlen(".cmake") &&
        meta_sv_ends_with_ci_lit(base, ".cmake")) {
        base.count -= strlen(".cmake");
    }
    return base;
}

static bool meta_export_write(EvalExecContext *ctx,
                              String_View out_path,
                              bool append,
                              String_View mode,
                              String_View targets,
                              String_View export_name,
                              String_View ns,
                              String_View cxx_modules_directory,
                              String_View cxx_modules_name) {
    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "# evaluator-generated export metadata\n");
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_MODE", mode);
    if (export_name.count > 0) {
        meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_NAME", export_name);
    }
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_TARGETS", targets);
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_NAMESPACE", ns);
    if (cxx_modules_directory.count > 0) {
        meta_sb_append_set_line(&sb,
                                "NOBIFY_EXPORT_CXX_MODULES_DIRECTORY",
                                cxx_modules_directory);
        meta_sb_append_set_line(&sb,
                                "NOBIFY_EXPORT_CXX_MODULES_NAME",
                                cxx_modules_name);
        nob_sb_append_cstr(&sb, "# Include evaluator-generated C++ module metadata\n");
        meta_sb_append_include_line(&sb,
                                    meta_export_cxx_modules_include_path_temp(ctx,
                                                                              cxx_modules_directory,
                                                                              cxx_modules_name),
                                    false);
    }
    bool ok = eval_write_text_file(ctx, out_path, nob_sv_from_parts(sb.items, sb.count), append);
    nob_sb_free(sb);
    return ok;
}

static bool meta_export_assign_last(EvalExecContext *ctx,
                                    String_View mode,
                                    String_View file_path,
                                    String_View targets,
                                    String_View ns,
                                    String_View cxx_modules_directory,
                                    String_View cxx_modules_name) {
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE"), mode)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_FILE"), file_path)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_TARGETS"), targets)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_NAMESPACE"), ns)) return false;
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_DIRECTORY"),
                              cxx_modules_directory)) {
        return false;
    }
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_NAME"),
                              cxx_modules_name)) {
        return false;
    }
    return true;
}

static bool meta_export_write_cxx_module_target_file(EvalExecContext *ctx,
                                                     String_View target_file_path,
                                                     String_View target_name,
                                                     String_View target_export_name,
                                                     String_View ns,
                                                     String_View config) {
    if (!ctx) return false;

    SV_List module_sets = NULL;
    if (!meta_target_collect_cxx_module_sets_temp(ctx, target_name, &module_sets)) return false;
    if (arena_arr_len(module_sets) == 0) return true;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "# evaluator-generated export C++ module metadata\n");
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_SOURCE_TARGET", target_name);
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_EXPORT_NAME", target_export_name);
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_NAMESPACE", ns);
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_CONFIG", config);

    String_View full_target_name = svu_join_no_sep_temp(ctx,
                                                        (String_View[]){
                                                            ns,
                                                            target_export_name,
                                                        },
                                                        2);
    if (eval_should_stop(ctx)) return false;
    meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_TARGET", full_target_name);
    meta_sb_append_set_line(&sb,
                            "NOBIFY_EXPORT_CXX_MODULE_SETS",
                            eval_sv_join_semi_temp(ctx,
                                                   module_sets,
                                                   arena_arr_len(module_sets)));
    if (eval_should_stop(ctx)) return false;

    static const char *k_passthrough_props[] = {
        "CXX_EXTENSIONS",
        "INCLUDE_DIRECTORIES",
        "COMPILE_DEFINITIONS",
        "COMPILE_OPTIONS",
        "COMPILE_FEATURES",
        "LINK_LIBRARIES",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_passthrough_props); i++) {
        bool have = false;
        String_View value = meta_target_property_temp(ctx,
                                                      target_name,
                                                      nob_sv_from_cstr(k_passthrough_props[i]),
                                                      &have);
        if (eval_should_stop(ctx)) return false;
        if (!have) continue;
        meta_sb_append_set_line(&sb,
                                nob_temp_sprintf("NOBIFY_EXPORT_CXX_MODULE_%s",
                                                 k_passthrough_props[i]),
                                value);
    }

    for (size_t i = 0; i < arena_arr_len(module_sets); i++) {
        String_View set_name = module_sets[i];
        String_View upper = eval_property_upper_name_temp(ctx, set_name);
        if (eval_should_stop(ctx)) return false;

        String_View files_prop =
            nob_sv_from_cstr(nob_temp_sprintf("CXX_MODULE_SET_%.*s",
                                              (int)upper.count,
                                              upper.data));
        String_View dirs_prop =
            nob_sv_from_cstr(nob_temp_sprintf("CXX_MODULE_DIRS_%.*s",
                                              (int)upper.count,
                                              upper.data));

        bool have_files = false;
        bool have_dirs = false;
        String_View files = meta_target_property_temp(ctx, target_name, files_prop, &have_files);
        if (eval_should_stop(ctx)) return false;
        String_View dirs = meta_target_property_temp(ctx, target_name, dirs_prop, &have_dirs);
        if (eval_should_stop(ctx)) return false;

        if (eval_sv_eq_ci_lit(set_name, "CXX_MODULES")) {
            if (!have_files) {
                files = meta_target_property_temp(ctx,
                                                  target_name,
                                                  nob_sv_from_cstr("CXX_MODULE_SET"),
                                                  &have_files);
                if (eval_should_stop(ctx)) return false;
            }
            if (!have_dirs) {
                dirs = meta_target_property_temp(ctx,
                                                 target_name,
                                                 nob_sv_from_cstr("CXX_MODULE_DIRS"),
                                                 &have_dirs);
                if (eval_should_stop(ctx)) return false;
            }
            if (have_files) meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_SET", files);
            if (have_dirs) meta_sb_append_set_line(&sb, "NOBIFY_EXPORT_CXX_MODULE_DIRS", dirs);
        }

        if (have_files) {
            meta_sb_append_set_line(&sb,
                                    nob_temp_sprintf("NOBIFY_EXPORT_CXX_MODULE_SET_%.*s",
                                                     (int)upper.count,
                                                     upper.data),
                                    files);
        }
        if (have_dirs) {
            meta_sb_append_set_line(&sb,
                                    nob_temp_sprintf("NOBIFY_EXPORT_CXX_MODULE_DIRS_%.*s",
                                                     (int)upper.count,
                                                     upper.data),
                                    dirs);
        }
    }

    bool ok = eval_write_text_file(ctx,
                                   target_file_path,
                                   nob_sv_from_parts(sb.items, sb.count),
                                   false);
    nob_sb_free(sb);
    return ok;
}

static bool meta_export_write_cxx_module_sidecars(EvalExecContext *ctx,
                                                  String_View export_file_path,
                                                  bool append,
                                                  String_View cxx_modules_directory,
                                                  String_View cxx_modules_name,
                                                  SV_List targets,
                                                  String_View ns) {
    if (!ctx || cxx_modules_directory.count == 0 || cxx_modules_name.count == 0) return true;

    String_View abs_dir = meta_export_cxx_modules_abs_dir_temp(ctx,
                                                               export_file_path,
                                                               cxx_modules_directory);
    if (eval_should_stop(ctx)) return false;
    if (!eval_file_mkdir_p(ctx, abs_dir)) return false;

    String_View trampoline_name =
        nob_sv_from_cstr(nob_temp_sprintf("cxx-modules-%.*s.cmake",
                                          (int)cxx_modules_name.count,
                                          cxx_modules_name.data));
    String_View config_name =
        nob_sv_from_cstr(nob_temp_sprintf("cxx-modules-%.*s-noconfig.cmake",
                                          (int)cxx_modules_name.count,
                                          cxx_modules_name.data));

    String_View trampoline_path = eval_sv_path_join(eval_temp_arena(ctx), abs_dir, trampoline_name);
    if (eval_should_stop(ctx)) return false;
    String_View config_path = eval_sv_path_join(eval_temp_arena(ctx), abs_dir, config_name);
    if (eval_should_stop(ctx)) return false;

    Nob_String_Builder trampoline = {0};
    nob_sb_append_cstr(&trampoline, "# evaluator-generated export C++ module trampoline\n");
    meta_sb_append_include_line(&trampoline,
                                meta_current_list_include_temp(ctx, config_name),
                                false);
    if (!eval_write_text_file(ctx,
                              trampoline_path,
                              nob_sv_from_parts(trampoline.items, trampoline.count),
                              false)) {
        nob_sb_free(trampoline);
        return false;
    }
    nob_sb_free(trampoline);

    Nob_String_Builder config = {0};
    nob_sb_append_cstr(&config, "# evaluator-generated export C++ module config metadata\n");
    for (size_t i = 0; i < arena_arr_len(targets); i++) {
        SV_List module_sets = NULL;
        if (!meta_target_collect_cxx_module_sets_temp(ctx, targets[i], &module_sets)) {
            nob_sb_free(config);
            return false;
        }
        if (arena_arr_len(module_sets) == 0) continue;

        String_View target_export_name = meta_target_export_name_temp(ctx, targets[i]);
        if (eval_should_stop(ctx)) {
            nob_sb_free(config);
            return false;
        }
        String_View target_file_name =
            nob_sv_from_cstr(nob_temp_sprintf("target-%.*s-noconfig.cmake",
                                              (int)target_export_name.count,
                                              target_export_name.data));
        String_View target_file_path =
            eval_sv_path_join(eval_temp_arena(ctx), abs_dir, target_file_name);
        if (eval_should_stop(ctx)) {
            nob_sb_free(config);
            return false;
        }

        if (!meta_export_write_cxx_module_target_file(ctx,
                                                      target_file_path,
                                                      targets[i],
                                                      target_export_name,
                                                      ns,
                                                      nob_sv_from_cstr("noconfig"))) {
            nob_sb_free(config);
            return false;
        }
        meta_sb_append_include_line(&config,
                                    meta_current_list_include_temp(ctx, target_file_name),
                                    false);
    }

    bool ok = eval_write_text_file(ctx,
                                   config_path,
                                   nob_sv_from_parts(config.items, config.count),
                                   append);
    nob_sb_free(config);
    return ok;
}

static bool meta_export_is_targets_option(String_View token) {
    return eval_sv_eq_ci_lit(token, "NAMESPACE") ||
           eval_sv_eq_ci_lit(token, "APPEND") ||
           eval_sv_eq_ci_lit(token, "FILE") ||
           eval_sv_eq_ci_lit(token, "EXPORT_LINK_INTERFACE_LIBRARIES") ||
           eval_sv_eq_ci_lit(token, "CXX_MODULES_DIRECTORY");
}

static String_View meta_export_mode_sv(Meta_Export_Signature signature) {
    if (signature == META_EXPORT_SIG_EXPORT) return nob_sv_from_cstr("EXPORT");
    if (signature == META_EXPORT_SIG_TARGETS) return nob_sv_from_cstr("TARGETS");
    if (signature == META_EXPORT_SIG_PACKAGE) return nob_sv_from_cstr("PACKAGE");
    return nob_sv_from_cstr("");
}

static bool meta_export_parse_package_signature(EvalExecContext *ctx,
                                                const Node *node,
                                                SV_List a,
                                                Meta_Export_Request *out) {
    if (arena_arr_len(a) != 2) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export(PACKAGE ...) requires exactly one package name"),
                             nob_sv_from_cstr("Usage: export(PACKAGE <PackageName>)"));
        return false;
    }
    out->export_name = a[1];
    return true;
}

static bool meta_export_parse_common_option(EvalExecContext *ctx,
                                            const Node *node,
                                            SV_List a,
                                            size_t *io_index,
                                            Meta_Export_Request *out,
                                            const char *namespace_cause,
                                            const char *file_cause,
                                            const char *unsupported_cause) {
    if (!ctx || !node || !io_index || !out) return false;
    String_View token = a[*io_index];

    if (eval_sv_eq_ci_lit(token, "APPEND")) {
        out->append = true;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "EXPORT_LINK_INTERFACE_LIBRARIES")) return true;
    if (eval_sv_eq_ci_lit(token, "CXX_MODULES_DIRECTORY")) {
        if (*io_index + 1 >= arena_arr_len(a)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(... CXX_MODULES_DIRECTORY ...) requires a directory"),
                                 nob_sv_from_cstr("Usage: ... CXX_MODULES_DIRECTORY <dir>"));
            return false;
        }
        out->cxx_modules_directory = a[++(*io_index)];
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "NAMESPACE")) {
        if (*io_index + 1 >= arena_arr_len(a)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr(namespace_cause),
                                 nob_sv_from_cstr("Usage: ... NAMESPACE <ns>"));
            return false;
        }
        out->ns = a[++(*io_index)];
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "FILE")) {
        if (*io_index + 1 >= arena_arr_len(a)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr(file_cause),
                                 nob_sv_from_cstr("Usage: ... FILE <path>"));
            return false;
        }
        out->file_path = a[++(*io_index)];
        return true;
    }

    (void)meta_emit_diag(ctx,
                         node,
                         EV_DIAG_ERROR,
                         nob_sv_from_cstr(unsupported_cause),
                         token);
    return false;
}

static bool meta_export_parse_targets_signature(EvalExecContext *ctx,
                                                const Node *node,
                                                SV_List a,
                                                Meta_Export_Request *out) {
    size_t i = 1;
    for (; i < arena_arr_len(a); i++) {
        if (meta_export_is_targets_option(a[i])) break;
        if (!svu_list_push_temp(ctx, &out->targets, a[i])) return false;
    }

    if (arena_arr_len(out->targets) == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export(TARGETS ...) requires at least one target"),
                             nob_sv_from_cstr("Usage: export(TARGETS <tgt>... FILE <file>)"));
        return false;
    }

    for (; i < arena_arr_len(a); i++) {
        if (!meta_export_parse_common_option(ctx,
                                             node,
                                             a,
                                             &i,
                                             out,
                                             "export(... NAMESPACE ...) requires a value",
                                             "export(... FILE ...) requires a path",
                                             "export(TARGETS ...) received an unsupported argument")) {
            return false;
        }
    }

    return true;
}

static bool meta_export_parse_export_signature(EvalExecContext *ctx,
                                               const Node *node,
                                               SV_List a,
                                               Meta_Export_Request *out) {
    if (arena_arr_len(a) < 2) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export(EXPORT ...) requires an export name"),
                             nob_sv_from_cstr("Usage: export(EXPORT <name> FILE <file>)"));
        return false;
    }

    out->export_name = a[1];
    for (size_t i = 2; i < arena_arr_len(a); i++) {
        if (!meta_export_parse_common_option(ctx,
                                             node,
                                             a,
                                             &i,
                                             out,
                                             "export(EXPORT ... NAMESPACE ...) requires a value",
                                             "export(EXPORT ... FILE ...) requires a path",
                                             "export(EXPORT ...) received an unsupported argument")) {
            return false;
        }
    }

    return true;
}

static bool meta_export_parse_request(EvalExecContext *ctx,
                                      const Node *node,
                                      SV_List a,
                                      Meta_Export_Request *out) {
    if (!ctx || !node || !out) return false;
    if (arena_arr_len(a) == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() requires a signature keyword"),
                             nob_sv_from_cstr("Usage: export(TARGETS ...) or export(EXPORT ...)"));
        return false;
    }

    if (eval_sv_eq_ci_lit(a[0], "PACKAGE")) {
        out->signature = META_EXPORT_SIG_PACKAGE;
        return meta_export_parse_package_signature(ctx, node, a, out);
    }

    if (eval_sv_eq_ci_lit(a[0], "SETUP")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                             a[0]);
        return false;
    }

    if (eval_sv_eq_ci_lit(a[0], "TARGETS")) {
        out->signature = META_EXPORT_SIG_TARGETS;
        return meta_export_parse_targets_signature(ctx, node, a, out);
    }
    if (eval_sv_eq_ci_lit(a[0], "EXPORT")) {
        out->signature = META_EXPORT_SIG_EXPORT;
        return meta_export_parse_export_signature(ctx, node, a, out);
    }

    (void)meta_emit_diag(ctx,
                         node,
                         EV_DIAG_ERROR,
                         nob_sv_from_cstr("export() signature is not implemented in evaluator v2"),
                         a[0]);
    return false;
}

static bool meta_export_validate_targets(EvalExecContext *ctx,
                                         const Node *node,
                                         SV_List targets) {
    if (!ctx || !node) return false;
    for (size_t i = 0; i < arena_arr_len(targets); i++) {
        if (!eval_target_known(ctx, targets[i])) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) target was not declared"),
                                 targets[i]);
            return false;
        }
        if (eval_target_alias_known(ctx, targets[i])) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export(TARGETS ...) may not export ALIAS targets"),
                                 targets[i]);
            return false;
        }
    }
    return true;
}

static bool meta_export_resolve_targets(EvalExecContext *ctx,
                                        const Node *node,
                                        const Meta_Export_Request *req,
                                        SV_List *out_targets) {
    if (!ctx || !node || !req || !out_targets) return false;
    *out_targets = NULL;

    if (req->signature == META_EXPORT_SIG_TARGETS) {
        if (!meta_export_validate_targets(ctx, node, req->targets)) return false;
        *out_targets = req->targets;
        return true;
    }

    String_View map_key = meta_concat3_temp(ctx,
                                            "NOBIFY_INSTALL_EXPORT::",
                                            req->export_name,
                                            "::TARGETS");
    String_View targets_sv = eval_var_get_visible(ctx, map_key);
    if (targets_sv.count == 0) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export(EXPORT ...) could not resolve tracked install export targets"),
                             req->export_name);
        return false;
    }
    return eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), targets_sv, out_targets);
}

static bool meta_export_resolve_file_path(EvalExecContext *ctx,
                                          const Node *node,
                                          const Meta_Export_Request *req,
                                          String_View *out_file_path) {
    String_View current_bin_dir = {0};
    String_View normalized_file_path = {0};
    String_View normalized_current_bin_dir = {0};
    String_View cwd = {0};
    if (!ctx || !node || !req || !out_file_path) return false;

    String_View file_path = req->file_path;
    if (file_path.count == 0) {
        if (req->export_name.count > 0) {
            file_path = meta_concat3_temp(ctx, "", req->export_name, ".cmake");
            if (eval_should_stop(ctx)) return false;
        } else {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export() requires FILE <path> in the supported signatures"),
                                 nob_sv_from_cstr("Usage: export(TARGETS ... FILE <file>) or export(EXPORT ... [FILE <file>])"));
            return false;
        }
    }

    if (!meta_sv_ends_with_ci_lit(file_path, ".cmake")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("export(... FILE ...) requires a filename ending in .cmake"),
                             file_path);
        return false;
    }

    if (!eval_sv_is_abs_path(file_path)) {
        current_bin_dir = meta_current_bin_dir(ctx);
        normalized_file_path = eval_sv_path_normalize_temp(ctx, file_path);
        normalized_current_bin_dir = eval_sv_path_normalize_temp(ctx, current_bin_dir);
        if (normalized_current_bin_dir.count > 0 &&
            !eval_sv_is_abs_path(normalized_current_bin_dir) &&
            meta_path_has_prefix(normalized_file_path, normalized_current_bin_dir)) {
            cwd = eval_process_cwd_temp(ctx);
            if (cwd.count > 0) {
                file_path = eval_sv_path_join(eval_temp_arena(ctx), cwd, normalized_file_path);
                if (eval_should_stop(ctx)) return false;
            } else {
                file_path = normalized_file_path;
            }
        } else {
            file_path = eval_sv_path_join(eval_temp_arena(ctx), current_bin_dir, file_path);
            if (eval_should_stop(ctx)) return false;
        }
    }

    *out_file_path = file_path;
    return true;
}

static bool meta_export_execute_request(EvalExecContext *ctx,
                                        const Node *node,
                                        const Meta_Export_Request *req) {
    if (!ctx || !node || !req) return false;

    if (req->signature == META_EXPORT_SIG_PACKAGE) {
        bool enabled = true;
        if (eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_EXPORT_NO_PACKAGE_REGISTRY")))) {
            enabled = false;
        }

        if (enabled &&
            eval_policy_is_new(ctx, EVAL_POLICY_CMP0090) &&
            !eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_EXPORT_PACKAGE_REGISTRY")))) {
            enabled = false;
        }

        if (!eval_emit_export_package_registry(ctx,
                                               eval_origin_from_node(ctx, node),
                                               req->export_name,
                                               meta_current_bin_dir(ctx),
                                               enabled)) {
            return false;
        }

        if (!enabled) {
            return meta_export_assign_last(ctx,
                                           meta_export_mode_sv(req->signature),
                                           nob_sv_from_cstr(""),
                                           nob_sv_from_cstr(""),
                                           nob_sv_from_cstr(""),
                                           nob_sv_from_cstr(""),
                                           nob_sv_from_cstr(""));
        }

        if (eval_enable_export_host_effects(ctx) &&
            !eval_package_registry_add(ctx, req->export_name, meta_current_bin_dir(ctx))) {
            return false;
        }
        return meta_export_assign_last(ctx,
                                       meta_export_mode_sv(req->signature),
                                       nob_sv_from_cstr(""),
                                       nob_sv_from_cstr(""),
                                       nob_sv_from_cstr(""),
                                       nob_sv_from_cstr(""),
                                       nob_sv_from_cstr(""));
    }

    SV_List targets = NULL;
    if (!meta_export_resolve_targets(ctx, node, req, &targets)) return false;

    String_View file_path = nob_sv_from_cstr("");
    if (!meta_export_resolve_file_path(ctx, node, req, &file_path)) return false;

    String_View targets_joined = eval_sv_join_semi_temp(ctx, targets, arena_arr_len(targets));
    if (eval_should_stop(ctx)) return false;
    String_View mode = meta_export_mode_sv(req->signature);
    String_View cxx_modules_name = meta_export_cxx_modules_name_temp(ctx, req->export_name, targets);
    String_View logical_name = meta_export_logical_name_temp(ctx, req, file_path);
    String_View export_key = eval_alloc_export_key(ctx);
    if (eval_should_stop(ctx)) return false;

    if (!eval_emit_export_build_declare(ctx,
                                        eval_origin_from_node(ctx, node),
                                        export_key,
                                        req->signature == META_EXPORT_SIG_TARGETS
                                            ? EVENT_EXPORT_SOURCE_TARGETS
                                            : EVENT_EXPORT_SOURCE_EXPORT_SET,
                                        logical_name,
                                        file_path,
                                        req->ns,
                                        req->append,
                                        req->cxx_modules_directory)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(targets); ++i) {
        if (!eval_emit_export_build_add_target(ctx,
                                               eval_origin_from_node(ctx, node),
                                               export_key,
                                               targets[i])) {
            return false;
        }
    }

    if (eval_enable_export_host_effects(ctx)) {
        if (!meta_export_write(ctx,
                               file_path,
                               req->append,
                               mode,
                               targets_joined,
                               req->export_name,
                               req->ns,
                               req->cxx_modules_directory,
                               cxx_modules_name)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export() failed to write metadata file"),
                                 file_path);
            return false;
        }

        if (!meta_export_write_cxx_module_sidecars(ctx,
                                                   file_path,
                                                   req->append,
                                                   req->cxx_modules_directory,
                                                   cxx_modules_name,
                                                   targets,
                                                   req->ns)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("export() failed to write C++ module metadata"),
                                 file_path);
            return false;
        }
    }

    return meta_export_assign_last(ctx,
                                   mode,
                                   file_path,
                                   targets_joined,
                                   req->ns,
                                   req->cxx_modules_directory,
                                   cxx_modules_name);
}

bool eval_finalize_cpack_package_snapshot(EvalExecContext *ctx) {
    SV_List generators = NULL;
    SV_List visible_names = NULL;
    String_View generator_list = {0};
    String_View package_name = {0};
    String_View package_version = {0};
    String_View package_file_name = {0};
    String_View package_directory = {0};
    String_View archive_file_name = {0};
    String_View archive_file_extension = {0};
    String_View components_grouping = {0};
    String_View project_config_file = {0};
    String_View components_all = {0};
    String_View package_key = {0};
    Event_Origin origin = {0};
    bool include_toplevel_directory = true;
    bool archive_component_install = false;
    Nob_String_Builder sb = {0};
    char *copy = NULL;

    if (!ctx || !ctx->cpack_module_loaded) return true;

    generator_list = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_GENERATOR"));
    if (generator_list.count == 0) generator_list = nob_sv_from_cstr("TGZ");
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), generator_list, &generators)) return false;
    if (eval_should_stop(ctx)) return false;

    package_name = meta_visible_var_or(ctx,
                                       "CPACK_PACKAGE_NAME",
                                       meta_visible_var_or(ctx,
                                                           "CMAKE_PROJECT_NAME",
                                                           meta_visible_var_or(ctx,
                                                                               "PROJECT_NAME",
                                                                               nob_sv_from_cstr("Package"))));
    package_version = meta_visible_var_or(ctx,
                                          "CPACK_PACKAGE_VERSION",
                                          meta_visible_var_or(ctx,
                                                              "CMAKE_PROJECT_VERSION",
                                                              meta_visible_var_or(ctx,
                                                                                  "PROJECT_VERSION",
                                                                                  nob_sv_from_cstr(""))));
    package_file_name = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_PACKAGE_FILE_NAME"));
    if (package_file_name.count == 0) {
        nob_sb_append_buf(&sb, package_name.data ? package_name.data : "", package_name.count);
        if (package_version.count > 0) {
            nob_sb_append_cstr(&sb, "-");
            nob_sb_append_buf(&sb, package_version.data ? package_version.data : "", package_version.count);
        }
        copy = arena_strndup(eval_temp_arena(ctx), sb.items ? sb.items : "", sb.count);
        nob_sb_free(sb);
        if (!copy) return false;
        package_file_name = nob_sv_from_cstr(copy);
    }

    package_directory = meta_visible_var_or(ctx, "CPACK_PACKAGE_DIRECTORY", meta_current_bin_dir(ctx));
    archive_file_name = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_ARCHIVE_FILE_NAME"));
    archive_file_extension = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_ARCHIVE_FILE_EXTENSION"));
    components_grouping = meta_visible_var_or(ctx, "CPACK_COMPONENTS_GROUPING", nob_sv_from_cstr("ONE_PER_GROUP"));
    project_config_file = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_PROJECT_CONFIG_FILE"));
    {
        String_View raw = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_INCLUDE_TOPLEVEL_DIRECTORY"));
        if (raw.count > 0) include_toplevel_directory = eval_truthy(ctx, raw);
    }
    archive_component_install =
        eval_truthy(ctx, eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_ARCHIVE_COMPONENT_INSTALL")));
    components_all = eval_var_get_visible(ctx, nob_sv_from_cstr("CPACK_COMPONENTS_ALL"));
    package_key = eval_alloc_cpack_package_key(ctx);
    if (eval_should_stop(ctx)) return false;
    origin = meta_current_origin(ctx);

    if (!eval_emit_cpack_package_declare(ctx,
                                         origin,
                                         package_key,
                                         package_name,
                                         package_version,
                                         package_file_name,
                                         package_directory,
                                         archive_file_name,
                                         archive_file_extension,
                                         components_grouping,
                                         project_config_file,
                                         include_toplevel_directory,
                                         archive_component_install,
                                         components_all)) {
        return false;
    }

    if (!eval_var_collect_visible_names(ctx, &visible_names)) return false;
    for (size_t i = 0; i < arena_arr_len(visible_names); ++i) {
        String_View name = visible_names[i];
        String_View prefix = nob_sv_from_cstr("CPACK_ARCHIVE_");
        String_View suffix = nob_sv_from_cstr("_FILE_NAME");
        String_View archive_key = {0};
        String_View value = {0};
        if (name.count <= prefix.count + suffix.count) continue;
        if (!nob_sv_starts_with(name, prefix) || !nob_sv_end_with(name, "_FILE_NAME")) continue;
        if (nob_sv_eq(name, nob_sv_from_cstr("CPACK_ARCHIVE_FILE_NAME"))) continue;
        archive_key = nob_sv_from_parts(name.data + prefix.count, name.count - prefix.count - suffix.count);
        value = eval_var_get_visible(ctx, name);
        if (archive_key.count == 0 || value.count == 0) continue;
        if (!eval_emit_cpack_package_archive_name_override(ctx, origin, package_key, archive_key, value)) return false;
    }

    {
        bool emitted_generator = false;
        for (size_t i = 0; i < arena_arr_len(generators); ++i) {
            String_View generator = nob_sv_trim(generators[i]);
            if (generator.count == 0) continue;
            emitted_generator = true;
            if (!eval_emit_cpack_package_add_generator(ctx, origin, package_key, generator)) return false;
        }
        if (!emitted_generator) {
            if (!eval_emit_cpack_package_add_generator(ctx, origin, package_key, nob_sv_from_cstr("TGZ"))) {
                return false;
            }
        }
    }

    return true;
}

Eval_Result eval_handle_export(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Meta_Export_Request req = {0};
    if (!meta_export_parse_request(ctx, node, a, &req)) return eval_result_from_ctx(ctx);
    if (!meta_export_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool meta_msproject_parse_request(EvalExecContext *ctx,
                                         const Node *node,
                                         SV_List a,
                                         Meta_Include_External_MSProject_Request *out) {
    if (!ctx || !node || !out) return false;
    if (arena_arr_len(a) < 2) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("include_external_msproject() requires project name and location"),
                             nob_sv_from_cstr("Usage: include_external_msproject(<name> <location> [TYPE <guid>] [GUID <guid>] [PLATFORM <name>] [deps...])"));
        return false;
    }

    out->name = a[0];
    out->location = a[1];

    size_t i = 2;
    while (i < arena_arr_len(a)) {
        if (eval_sv_eq_ci_lit(a[i], "TYPE")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(TYPE ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... TYPE <projectTypeGUID>"));
                return false;
            }
            out->type_guid = a[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "GUID")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(GUID ...) requires a GUID value"),
                                     nob_sv_from_cstr("Usage: ... GUID <projectGUID>"));
                return false;
            }
            out->project_guid = a[i + 1];
            i += 2;
            continue;
        }
        if (eval_sv_eq_ci_lit(a[i], "PLATFORM")) {
            if (i + 1 >= arena_arr_len(a)) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("include_external_msproject(PLATFORM ...) requires a value"),
                                     nob_sv_from_cstr("Usage: ... PLATFORM <name>"));
                return false;
            }
            out->platform = a[i + 1];
            i += 2;
            continue;
        }
        break;
    }

    for (; i < arena_arr_len(a); i++) {
        if (!svu_list_push_temp(ctx, &out->deps, a[i])) return false;
    }

    return true;
}

static bool meta_msproject_execute_request(EvalExecContext *ctx,
                                           const Node *node,
                                           const Meta_Include_External_MSProject_Request *req) {
    if (!ctx || !node || !req) return false;
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);
    if (!eval_target_register(ctx, req->name)) return false;
    if (!eval_target_set_imported(ctx, req->name, true)) return false;

    String_View deps_joined = eval_sv_join_semi_temp(ctx, req->deps, arena_arr_len(req->deps));
    if (eval_should_stop(ctx)) return false;
    String_View location = eval_path_resolve_for_cmake_arg(ctx,
                                                           req->location,
                                                           eval_current_source_dir_for_paths(ctx),
                                                           true);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_set_current(ctx,
                              meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", req->name, "::LOCATION"),
                              req->location)) {
        return false;
    }
    if (!eval_var_set_current(ctx,
                              meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", req->name, "::TYPE"),
                              req->type_guid)) {
        return false;
    }
    if (!eval_var_set_current(ctx,
                              meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", req->name, "::GUID"),
                              req->project_guid)) {
        return false;
    }
    if (!eval_var_set_current(ctx,
                              meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", req->name, "::PLATFORM"),
                              req->platform)) {
        return false;
    }
    if (!eval_var_set_current(ctx,
                              meta_concat3_temp(ctx, "NOBIFY_MSPROJECT::", req->name, "::DEPENDENCIES"),
                              deps_joined)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("LOCATION"),
                                    location,
                                    EV_PROP_SET)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("NOBIFY_EXTERNAL_MSPROJECT_TYPE"),
                                    req->type_guid,
                                    EV_PROP_SET)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("NOBIFY_EXTERNAL_MSPROJECT_GUID"),
                                    req->project_guid,
                                    EV_PROP_SET)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("NOBIFY_EXTERNAL_MSPROJECT_PLATFORM"),
                                    req->platform,
                                    EV_PROP_SET)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("NOBIFY_EXTERNAL_MSPROJECT_DEPENDENCIES"),
                                    deps_joined,
                                    EV_PROP_SET)) {
        return false;
    }
    if (!meta_store_target_property(ctx,
                                    origin,
                                    req->name,
                                    nob_sv_from_cstr("IMPORTED"),
                                    nob_sv_from_cstr("1"),
                                    EV_PROP_SET)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->deps); i++) {
        if (!eval_emit_target_dependency(ctx, origin, req->name, req->deps[i])) return false;
    }

    return eval_emit_target_declare(ctx,
                                    origin,
                                    req->name,
                                    EV_TARGET_LIBRARY_UNKNOWN,
                                    true,
                                    false,
                                    nob_sv_from_cstr(""));
}

Eval_Result eval_handle_include_external_msproject(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Meta_Include_External_MSProject_Request req = {0};
    if (!meta_msproject_parse_request(ctx, node, a, &req)) return eval_result_from_ctx(ctx);
    if (!meta_msproject_execute_request(ctx, node, &req)) return eval_result_from_ctx(ctx);
    return eval_result_from_ctx(ctx);
}

static bool meta_file_api_kind_supported(String_View kind) {
    return eval_sv_eq_ci_lit(kind, "CODEMODEL") ||
           eval_sv_eq_ci_lit(kind, "CACHE") ||
           eval_sv_eq_ci_lit(kind, "CMAKEFILES") ||
           eval_sv_eq_ci_lit(kind, "TOOLCHAINS");
}

typedef struct {
    String_View kind_upper;
    String_View kind_json;
    String_View versions;
} Meta_File_Api_Request;

typedef Meta_File_Api_Request *Meta_File_Api_Request_List;

typedef struct {
    String_View api_version;
    Meta_File_Api_Request_List requests;
} Meta_File_Api_Query_Request;

static String_View meta_file_api_kind_json_name(String_View kind) {
    if (eval_sv_eq_ci_lit(kind, "CODEMODEL")) return nob_sv_from_cstr("codemodel");
    if (eval_sv_eq_ci_lit(kind, "CACHE")) return nob_sv_from_cstr("cache");
    if (eval_sv_eq_ci_lit(kind, "CMAKEFILES")) return nob_sv_from_cstr("cmakeFiles");
    if (eval_sv_eq_ci_lit(kind, "TOOLCHAINS")) return nob_sv_from_cstr("toolchains");
    return nob_sv_from_cstr("");
}

static String_View meta_file_api_root_dir(EvalExecContext *ctx) {
    if (!ctx) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), meta_current_bin_dir(ctx), nob_sv_from_cstr(".cmake/api/v1"));
}

static String_View meta_file_api_query_dir(EvalExecContext *ctx) {
    String_View root = meta_file_api_root_dir(ctx);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("query"));
}

static String_View meta_file_api_reply_dir(EvalExecContext *ctx) {
    String_View root = meta_file_api_root_dir(ctx);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    return eval_sv_path_join(eval_temp_arena(ctx), root, nob_sv_from_cstr("reply"));
}

static bool meta_file_api_append_version_fields(Nob_String_Builder *sb, String_View version) {
    static const char *const field_names[] = {"major", "minor", "patch", "tweak"};
    if (!sb || version.count == 0) return false;

    size_t field_index = 0;
    size_t part_start = 0;
    for (size_t i = 0; i <= version.count; i++) {
        if (i < version.count && version.data[i] != '.') {
            if (!isdigit((unsigned char)version.data[i])) return false;
            continue;
        }

        if (i == part_start || field_index >= sizeof(field_names)/sizeof(field_names[0])) return false;
        if (field_index > 0) nob_sb_append_cstr(sb, ", ");
        nob_sb_append_cstr(sb, "\"");
        nob_sb_append_cstr(sb, field_names[field_index]);
        nob_sb_append_cstr(sb, "\": ");
        nob_sb_append_buf(sb, version.data + part_start, i - part_start);
        field_index++;
        part_start = i + 1;
    }

    return field_index > 0;
}

static bool meta_file_api_is_version(String_View token) {
    if (token.count == 0) return false;
    bool saw_digit = false;
    bool prev_dot = true;
    for (size_t i = 0; i < token.count; i++) {
        char c = token.data[i];
        if (c == '.') {
            if (prev_dot) return false;
            prev_dot = true;
            continue;
        }
        if (c < '0' || c > '9') return false;
        prev_dot = false;
        saw_digit = true;
    }
    return saw_digit && !prev_dot;
}

static bool meta_file_api_set_path_var(EvalExecContext *ctx, const char *key, String_View value) {
    if (!ctx || !key) return false;
    return eval_var_set_current(ctx, nob_sv_from_cstr(key), value);
}

static String_View meta_file_api_reply_filename_temp(EvalExecContext *ctx,
                                                     String_View kind_json,
                                                     String_View version) {
    if (!ctx || kind_json.count == 0 || version.count == 0) return nob_sv_from_cstr("");
    size_t total = kind_json.count + sizeof("-v") - 1 + version.count + sizeof(".json") - 1;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, kind_json.data, kind_json.count);
    off += kind_json.count;
    memcpy(buf + off, "-v", sizeof("-v") - 1);
    off += sizeof("-v") - 1;
    memcpy(buf + off, version.data, version.count);
    off += version.count;
    memcpy(buf + off, ".json", sizeof(".json") - 1);
    off += sizeof(".json") - 1;
    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool meta_file_api_parse_int_sv(String_View value, int *out_value) {
    if (out_value) *out_value = 0;
    if (!out_value || value.count == 0) return false;

    char buf[32];
    if (value.count >= sizeof(buf)) return false;
    memcpy(buf, value.data, value.count);
    buf[value.count] = '\0';

    char *end = NULL;
    long parsed = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out_value = (int)parsed;
    return true;
}

static String_View meta_file_api_kind_upper_from_json_name(String_View kind_json) {
    if (eval_sv_eq_ci_lit(kind_json, "codemodel")) return nob_sv_from_cstr("CODEMODEL");
    if (eval_sv_eq_ci_lit(kind_json, "cache")) return nob_sv_from_cstr("CACHE");
    if (eval_sv_eq_ci_lit(kind_json, "cmakeFiles")) return nob_sv_from_cstr("CMAKEFILES");
    if (eval_sv_eq_ci_lit(kind_json, "toolchains")) return nob_sv_from_cstr("TOOLCHAINS");
    return nob_sv_from_cstr("");
}

typedef struct {
    String_View kind_upper;
    String_View kind_json;
    String_View version;
    String_View json_file;
} Meta_File_Api_Object;

typedef Meta_File_Api_Object *Meta_File_Api_Object_List;

static bool meta_file_api_append_object_unique(EvalExecContext *ctx,
                                               Meta_File_Api_Object_List *out_objects,
                                               String_View kind_upper,
                                               String_View kind_json,
                                               String_View version,
                                               String_View *out_json_file) {
    if (out_json_file) *out_json_file = nob_sv_from_cstr("");
    if (!ctx || !out_objects) return false;

    for (size_t i = 0; i < arena_arr_len(*out_objects); i++) {
        if (!eval_sv_key_eq((*out_objects)[i].kind_json, kind_json)) continue;
        if (!eval_sv_key_eq((*out_objects)[i].version, version)) continue;
        if (out_json_file) *out_json_file = (*out_objects)[i].json_file;
        return true;
    }

    String_View json_file = meta_file_api_reply_filename_temp(ctx, kind_json, version);
    if (eval_should_stop(ctx)) return false;

    Meta_File_Api_Object object = {
        .kind_upper = kind_upper,
        .kind_json = kind_json,
        .version = version,
        .json_file = json_file,
    };
    if (!EVAL_ARR_PUSH(ctx, ctx->arena, *out_objects, object)) return false;
    if (out_json_file) *out_json_file = json_file;
    return true;
}

static bool meta_file_api_append_query_record(EvalExecContext *ctx,
                                              Eval_File_Api_Query_Record_List *out_records,
                                              String_View client_name,
                                              String_View client_query_file,
                                              String_View kind_upper,
                                              String_View kind_json,
                                              String_View versions,
                                              bool shared_query) {
    if (!ctx || !out_records || kind_upper.count == 0 || kind_json.count == 0 || versions.count == 0) {
        return false;
    }

    Eval_File_Api_Query_Record record = {
        .client_name = client_name,
        .client_query_file = client_query_file,
        .kind_upper = kind_upper,
        .kind_json = kind_json,
        .versions = versions,
        .shared_query = shared_query,
    };
    return EVAL_ARR_PUSH(ctx, ctx->arena, *out_records, record);
}

static bool meta_file_api_upsert_project_query(EvalExecContext *ctx,
                                               const Meta_File_Api_Request *request) {
    if (!ctx || !request) return false;
    Eval_File_Api_Model *model = &ctx->semantic_state.file_api;

    for (size_t i = 0; i < arena_arr_len(model->queries); i++) {
        Eval_File_Api_Query_Record *record = &model->queries[i];
        if (!record->shared_query || record->client_query_file.count > 0) continue;
        if (!eval_sv_key_eq(record->kind_upper, request->kind_upper)) continue;
        record->kind_json = sv_copy_to_event_arena(ctx, request->kind_json);
        record->versions = sv_copy_to_event_arena(ctx, request->versions);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    Eval_File_Api_Query_Record record = {
        .client_name = nob_sv_from_cstr(""),
        .client_query_file = nob_sv_from_cstr(""),
        .kind_upper = sv_copy_to_event_arena(ctx, request->kind_upper),
        .kind_json = sv_copy_to_event_arena(ctx, request->kind_json),
        .versions = sv_copy_to_event_arena(ctx, request->versions),
        .shared_query = true,
    };
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, model->queries, record);
}

static bool meta_file_api_find_literal(String_View text,
                                       size_t start,
                                       const char *needle,
                                       size_t *out_pos) {
    if (out_pos) *out_pos = SIZE_MAX;
    if (!needle) return false;
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || start >= text.count || needle_len > text.count) return false;

    for (size_t i = start; i + needle_len <= text.count; i++) {
        if (memcmp(text.data + i, needle, needle_len) == 0) {
            if (out_pos) *out_pos = i;
            return true;
        }
    }
    return false;
}

static size_t meta_file_api_skip_ws(String_View text, size_t pos) {
    while (pos < text.count && isspace((unsigned char)text.data[pos])) pos++;
    return pos;
}

static bool meta_file_api_parse_json_string(String_View text,
                                            size_t *io_pos,
                                            String_View *out_value) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (!io_pos || !out_value) return false;
    size_t pos = meta_file_api_skip_ws(text, *io_pos);
    if (pos >= text.count || text.data[pos] != '"') return false;
    pos++;
    size_t start = pos;
    while (pos < text.count) {
        if (text.data[pos] == '\\') {
            pos += 2;
            continue;
        }
        if (text.data[pos] == '"') break;
        pos++;
    }
    if (pos >= text.count) return false;
    *out_value = nob_sv_from_parts(text.data + start, pos - start);
    *io_pos = pos + 1;
    return true;
}

static bool meta_file_api_parse_version_object_temp(EvalExecContext *ctx,
                                                    String_View text,
                                                    size_t *io_pos,
                                                    String_View *out_version) {
    if (out_version) *out_version = nob_sv_from_cstr("");
    if (!ctx || !io_pos || !out_version) return false;
    size_t pos = meta_file_api_skip_ws(text, *io_pos);
    if (pos >= text.count || text.data[pos] != '{') return false;
    pos++;

    int values[4] = {-1, -1, -1, -1};
    static const char *const keys[] = {"major", "minor", "patch", "tweak"};
    while (pos < text.count) {
        pos = meta_file_api_skip_ws(text, pos);
        if (pos < text.count && text.data[pos] == '}') {
            pos++;
            break;
        }

        String_View key = nob_sv_from_cstr("");
        if (!meta_file_api_parse_json_string(text, &pos, &key)) return false;
        pos = meta_file_api_skip_ws(text, pos);
        if (pos >= text.count || text.data[pos] != ':') return false;
        pos++;
        pos = meta_file_api_skip_ws(text, pos);
        size_t num_start = pos;
        while (pos < text.count && isdigit((unsigned char)text.data[pos])) pos++;
        if (num_start == pos) return false;

        int parsed = 0;
        if (!meta_file_api_parse_int_sv(nob_sv_from_parts(text.data + num_start, pos - num_start), &parsed)) {
            return false;
        }
        for (size_t ki = 0; ki < NOB_ARRAY_LEN(keys); ki++) {
            if (eval_sv_eq_ci_lit(key, keys[ki])) {
                values[ki] = parsed;
                break;
            }
        }

        pos = meta_file_api_skip_ws(text, pos);
        if (pos < text.count && text.data[pos] == ',') pos++;
    }

    Nob_String_Builder sb = {0};
    bool first = true;
    for (size_t i = 0; i < NOB_ARRAY_LEN(values); i++) {
        if (values[i] < 0) continue;
        if (!first) nob_sb_append_cstr(&sb, ".");
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", values[i]));
        first = false;
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return false;
    }

    *out_version = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, sb.count));
    nob_sb_free(sb);
    *io_pos = pos;
    if (eval_should_stop(ctx)) return false;
    return true;
}

static bool meta_file_api_parse_client_query_file(EvalExecContext *ctx,
                                                  String_View client_name,
                                                  String_View query_file,
                                                  String_View contents,
                                                  Eval_File_Api_Query_Record_List *out_records) {
    if (!ctx || !out_records) return false;

    size_t search_from = 0;
    while (true) {
        size_t kind_pos = SIZE_MAX;
        if (!meta_file_api_find_literal(contents, search_from, "\"kind\"", &kind_pos)) break;

        size_t pos = kind_pos + strlen("\"kind\"");
        pos = meta_file_api_skip_ws(contents, pos);
        if (pos >= contents.count || contents.data[pos] != ':') {
            search_from = kind_pos + 1;
            continue;
        }
        pos++;

        String_View kind_json = nob_sv_from_cstr("");
        if (!meta_file_api_parse_json_string(contents, &pos, &kind_json)) {
            search_from = kind_pos + 1;
            continue;
        }
        if (meta_file_api_kind_upper_from_json_name(kind_json).count == 0) {
            search_from = pos;
            continue;
        }

        size_t version_pos = SIZE_MAX;
        if (!meta_file_api_find_literal(contents, pos, "\"version\"", &version_pos)) {
            search_from = pos;
            continue;
        }
        pos = version_pos + strlen("\"version\"");
        pos = meta_file_api_skip_ws(contents, pos);
        if (pos >= contents.count || contents.data[pos] != ':') {
            search_from = version_pos + 1;
            continue;
        }
        pos++;
        pos = meta_file_api_skip_ws(contents, pos);

        SV_List versions = NULL;
        if (pos < contents.count && contents.data[pos] == '[') {
            pos++;
            while (pos < contents.count) {
                pos = meta_file_api_skip_ws(contents, pos);
                if (pos < contents.count && contents.data[pos] == ']') {
                    pos++;
                    break;
                }
                String_View version = nob_sv_from_cstr("");
                if (!meta_file_api_parse_version_object_temp(ctx, contents, &pos, &version)) return false;
                if (!svu_list_push_temp(ctx, &versions, version)) return false;
                pos = meta_file_api_skip_ws(contents, pos);
                if (pos < contents.count && contents.data[pos] == ',') pos++;
            }
        } else if (pos < contents.count && contents.data[pos] == '{') {
            String_View version = nob_sv_from_cstr("");
            if (!meta_file_api_parse_version_object_temp(ctx, contents, &pos, &version)) return false;
            if (!svu_list_push_temp(ctx, &versions, version)) return false;
        }
        if (arena_arr_len(versions) == 0) {
            search_from = pos;
            continue;
        }

        String_View joined = eval_sv_join_semi_temp(ctx, versions, arena_arr_len(versions));
        String_View kind_upper = meta_file_api_kind_upper_from_json_name(kind_json);
        if (eval_should_stop(ctx)) return false;
        if (!meta_file_api_append_query_record(ctx,
                                               out_records,
                                               client_name,
                                               query_file,
                                               kind_upper,
                                               kind_json,
                                               joined,
                                               false)) {
            return false;
        }
        search_from = pos;
    }

    return true;
}

static bool meta_file_api_collect_disk_queries(EvalExecContext *ctx,
                                               Eval_File_Api_Query_Record_List *out_records) {
    if (!ctx || !out_records) return false;

    String_View query_dir = meta_file_api_query_dir(ctx);
    if (eval_should_stop(ctx)) return false;
    char *query_dir_c = eval_sv_to_cstr_temp(ctx, query_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, query_dir_c, false);
    if (!nob_file_exists(query_dir_c) || nob_get_file_type(query_dir_c) != NOB_FILE_DIRECTORY) return true;

    Nob_File_Paths entries = {0};
    if (!nob_read_entire_dir(query_dir_c, &entries)) return true;
    for (size_t i = 0; i < entries.count; i++) {
        const char *entry_name = entries.items[i];
        if (!entry_name || strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) continue;

        String_View entry = nob_sv_from_cstr(entry_name);
        String_View full_path = eval_sv_path_join(eval_temp_arena(ctx), query_dir, entry);
        if (eval_should_stop(ctx)) {
            nob_da_free(entries);
            return false;
        }
        char *full_path_c = eval_sv_to_cstr_temp(ctx, full_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, full_path_c, false);
        Nob_File_Type type = nob_get_file_type(full_path_c);

        if (type == NOB_FILE_DIRECTORY && entry.count > 7 && memcmp(entry.data, "client-", 7) == 0) {
            String_View query_file = eval_sv_path_join(eval_temp_arena(ctx), full_path, nob_sv_from_cstr("query.json"));
            if (eval_should_stop(ctx)) {
                nob_da_free(entries);
                return false;
            }
            String_View contents = nob_sv_from_cstr("");
            bool found = false;
            if (!eval_service_read_file(ctx, query_file, &contents, &found)) {
                nob_da_free(entries);
                return false;
            }
            if (found &&
                !meta_file_api_parse_client_query_file(ctx, entry, query_file, contents, out_records)) {
                nob_da_free(entries);
                return false;
            }
            continue;
        }

        if (type != NOB_FILE_REGULAR) continue;
        String_View kind_json = nob_sv_from_cstr("");
        String_View kind_upper = nob_sv_from_cstr("");
        if (entry.count >= 12 && memcmp(entry.data, "codemodel-v", 11) == 0) {
            kind_json = nob_sv_from_cstr("codemodel");
            kind_upper = nob_sv_from_cstr("CODEMODEL");
        } else if (entry.count >= 8 && memcmp(entry.data, "cache-v", 7) == 0) {
            kind_json = nob_sv_from_cstr("cache");
            kind_upper = nob_sv_from_cstr("CACHE");
        } else if (entry.count >= 13 && memcmp(entry.data, "cmakeFiles-v", 12) == 0) {
            kind_json = nob_sv_from_cstr("cmakeFiles");
            kind_upper = nob_sv_from_cstr("CMAKEFILES");
        } else if (entry.count >= 13 && memcmp(entry.data, "toolchains-v", 12) == 0) {
            kind_json = nob_sv_from_cstr("toolchains");
            kind_upper = nob_sv_from_cstr("TOOLCHAINS");
        } else {
            continue;
        }

        size_t dash = SIZE_MAX;
        for (size_t di = 0; di < entry.count; di++) {
            if (entry.data[di] == 'v' && di > 0 && entry.data[di - 1] == '-') {
                dash = di;
                break;
            }
        }
        if (dash == SIZE_MAX || dash + 1 >= entry.count) continue;
        String_View version = nob_sv_from_parts(entry.data + dash + 1, entry.count - dash - 1);
        if (meta_sv_ends_with_ci_lit(version, ".json")) version.count -= 5;
        if (!meta_file_api_is_version(version)) continue;
        if (!meta_file_api_append_query_record(ctx,
                                               out_records,
                                               nob_sv_from_cstr(""),
                                               full_path,
                                               kind_upper,
                                               kind_json,
                                               version,
                                               true)) {
            nob_da_free(entries);
            return false;
        }
    }
    nob_da_free(entries);
    return true;
}

static bool meta_file_api_collect_all_queries(EvalExecContext *ctx,
                                              Eval_File_Api_Query_Record_List *out_records) {
    if (!ctx || !out_records) return false;
    *out_records = NULL;

    for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.file_api.queries); i++) {
        if (!EVAL_ARR_PUSH(ctx, ctx->arena, *out_records, ctx->semantic_state.file_api.queries[i])) return false;
    }
    return meta_file_api_collect_disk_queries(ctx, out_records);
}

static bool meta_file_api_write_object_file(EvalExecContext *ctx,
                                            String_View reply_dir,
                                            const Meta_File_Api_Object *object) {
    if (!ctx || !object) return false;

    String_View reply_path = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, object->json_file);
    if (eval_should_stop(ctx)) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "{\n  \"kind\": \"");
    nob_sb_append_buf(&sb, object->kind_json.data, object->kind_json.count);
    nob_sb_append_cstr(&sb, "\",\n  \"version\": {");
    if (!meta_file_api_append_version_fields(&sb, object->version)) {
        nob_sb_free(sb);
        return false;
    }
    nob_sb_append_cstr(&sb, "},\n");

    if (eval_sv_eq_ci_lit(object->kind_json, "codemodel")) {
        nob_sb_append_cstr(&sb, "  \"paths\": {\n    \"source\": \"");
        nob_sb_append_buf(&sb, ctx->source_dir.data, ctx->source_dir.count);
        nob_sb_append_cstr(&sb, "\",\n    \"build\": \"");
        nob_sb_append_buf(&sb, ctx->binary_dir.data, ctx->binary_dir.count);
        nob_sb_append_cstr(&sb, "\"\n  },\n  \"configurations\": [\n    {\n      \"name\": \"\",\n      \"targets\": [");
        for (size_t i = 0; i < arena_arr_len(ctx->semantic_state.targets.records); i++) {
            Eval_Target_Record *target = &ctx->semantic_state.targets.records[i];
            if (i > 0) nob_sb_append_cstr(&sb, ", ");
            nob_sb_append_cstr(&sb, "{\"name\": \"");
            nob_sb_append_buf(&sb, target->name.data, target->name.count);
            nob_sb_append_cstr(&sb, "\", \"id\": \"");
            nob_sb_append_buf(&sb, target->name.data, target->name.count);
            nob_sb_append_cstr(&sb, "::@nobify\"}");
        }
        nob_sb_append_cstr(&sb, "]\n    }\n  ]\n");
    } else if (eval_sv_eq_ci_lit(object->kind_json, "cache")) {
        nob_sb_append_cstr(&sb, "  \"entries\": [");
        bool first = true;
        ptrdiff_t n = stbds_shlen(ctx->scope_state.cache_entries);
        for (ptrdiff_t i = 0; i < n; i++) {
            Eval_Cache_Entry *entry = &ctx->scope_state.cache_entries[i];
            if (!entry->key) continue;
            if (!first) nob_sb_append_cstr(&sb, ", ");
            first = false;
            nob_sb_append_cstr(&sb, "{\"name\": \"");
            nob_sb_append_cstr(&sb, entry->key);
            nob_sb_append_cstr(&sb, "\", \"type\": \"");
            nob_sb_append_buf(&sb, entry->value.type.data, entry->value.type.count);
            nob_sb_append_cstr(&sb, "\", \"value\": \"");
            meta_sb_append_cmake_escaped(&sb, entry->value.data);
            nob_sb_append_cstr(&sb, "\"}");
        }
        nob_sb_append_cstr(&sb, "]\n");
    } else if (eval_sv_eq_ci_lit(object->kind_json, "cmakeFiles")) {
        nob_sb_append_cstr(&sb, "  \"inputs\": [\n    {\"path\": \"");
        nob_sb_append_cstr(&sb, ctx->current_file ? ctx->current_file : "");
        nob_sb_append_cstr(&sb, "\"}\n  ]\n");
    } else if (eval_sv_eq_ci_lit(object->kind_json, "toolchains")) {
        nob_sb_append_cstr(&sb, "  \"toolchains\": [");
        String_View c_compiler = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER"));
        String_View cxx_compiler = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"));
        nob_sb_append_cstr(&sb, "{\"language\": \"C\", \"compiler\": {\"path\": \"");
        nob_sb_append_buf(&sb, c_compiler.data, c_compiler.count);
        nob_sb_append_cstr(&sb, "\"}}, {\"language\": \"CXX\", \"compiler\": {\"path\": \"");
        nob_sb_append_buf(&sb, cxx_compiler.data, cxx_compiler.count);
        nob_sb_append_cstr(&sb, "\"}}]\n");
    } else {
        nob_sb_append_cstr(&sb, "  \"reply\": true\n");
    }

    nob_sb_append_cstr(&sb, "}\n");
    bool ok = eval_write_text_file(ctx, reply_path, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), false);
    nob_sb_free(sb);
    return ok;
}

static bool meta_file_api_write_index_from_queries(EvalExecContext *ctx,
                                                   String_View index_file,
                                                   const Eval_File_Api_Query_Record_List *queries,
                                                   Meta_File_Api_Object_List objects) {
    if (!ctx || !queries) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "{\n  \"cmake\": {\n    \"generator\": {\"name\": \"Nobify Evaluator\"},\n    \"paths\": {\n      \"source\": \"");
    nob_sb_append_buf(&sb, ctx->source_dir.data, ctx->source_dir.count);
    nob_sb_append_cstr(&sb, "\",\n      \"build\": \"");
    nob_sb_append_buf(&sb, ctx->binary_dir.data, ctx->binary_dir.count);
    nob_sb_append_cstr(&sb, "\"\n    }\n  },\n  \"objects\": [\n");

    for (size_t i = 0; i < arena_arr_len(objects); i++) {
        if (i > 0) nob_sb_append_cstr(&sb, ",\n");
        nob_sb_append_cstr(&sb, "    {\"kind\": \"");
        nob_sb_append_buf(&sb, objects[i].kind_json.data, objects[i].kind_json.count);
        nob_sb_append_cstr(&sb, "\", \"version\": {");
        if (!meta_file_api_append_version_fields(&sb, objects[i].version)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "}, \"jsonFile\": \"");
        nob_sb_append_buf(&sb, objects[i].json_file.data, objects[i].json_file.count);
        nob_sb_append_cstr(&sb, "\"}");
    }
    nob_sb_append_cstr(&sb, "\n  ]");

    bool opened_reply = false;
    for (size_t qi = 0; qi < arena_arr_len(*queries); qi++) {
        const Eval_File_Api_Query_Record *query = &(*queries)[qi];
        if (query->shared_query || query->client_name.count == 0 || query->client_query_file.count == 0) continue;

        if (!opened_reply) {
            nob_sb_append_cstr(&sb, ",\n  \"reply\": {\n");
            opened_reply = true;
        } else {
            nob_sb_append_cstr(&sb, ",\n");
        }

        nob_sb_append_cstr(&sb, "    \"");
        nob_sb_append_buf(&sb, query->client_name.data, query->client_name.count);
        nob_sb_append_cstr(&sb, "\": {\n      \"");
        nob_sb_append_cstr(&sb, "query.json");
        nob_sb_append_cstr(&sb, "\": {\n        \"responses\": [");

        SV_List versions = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), query->versions, &versions)) {
            nob_sb_free(sb);
            return false;
        }
        for (size_t vi = 0; vi < arena_arr_len(versions); vi++) {
            String_View json_file = nob_sv_from_cstr("");
            if (!meta_file_api_append_object_unique(ctx,
                                                    &objects,
                                                    query->kind_upper,
                                                    query->kind_json,
                                                    versions[vi],
                                                    &json_file)) {
                nob_sb_free(sb);
                return false;
            }
            if (vi > 0) nob_sb_append_cstr(&sb, ", ");
            nob_sb_append_cstr(&sb, "{\"kind\": \"");
            nob_sb_append_buf(&sb, query->kind_json.data, query->kind_json.count);
            nob_sb_append_cstr(&sb, "\", \"version\": {");
            if (!meta_file_api_append_version_fields(&sb, versions[vi])) {
                nob_sb_free(sb);
                return false;
            }
            nob_sb_append_cstr(&sb, "}, \"jsonFile\": \"");
            nob_sb_append_buf(&sb, json_file.data, json_file.count);
            nob_sb_append_cstr(&sb, "\"}");
        }
        nob_sb_append_cstr(&sb, "]\n      }\n    }");
    }
    if (opened_reply) nob_sb_append_cstr(&sb, "\n  }");

    nob_sb_append_cstr(&sb, "\n}\n");
    bool ok = eval_write_text_file(ctx, index_file, nob_sv_from_parts(sb.items ? sb.items : "", sb.count), false);
    nob_sb_free(sb);
    return ok;
}

static bool meta_file_api_stage_replies(EvalExecContext *ctx, const Meta_File_Api_Query_Request *req) {
    if (!ctx || !req) return false;

    for (size_t i = 0; i < arena_arr_len(req->requests); i++) {
        if (!meta_file_api_upsert_project_query(ctx, &req->requests[i])) return false;
    }

    String_View root_dir = meta_file_api_root_dir(ctx);
    String_View query_dir = meta_file_api_query_dir(ctx);
    String_View reply_dir = meta_file_api_reply_dir(ctx);
    if (eval_should_stop(ctx)) return false;
    if (!eval_file_mkdir_p(ctx, query_dir)) return false;
    if (!eval_file_mkdir_p(ctx, reply_dir)) return false;

    Eval_File_Api_Query_Record_List all_queries = NULL;
    if (!meta_file_api_collect_all_queries(ctx, &all_queries)) return false;

    Meta_File_Api_Object_List objects = NULL;
    for (size_t qi = 0; qi < arena_arr_len(all_queries); qi++) {
        const Eval_File_Api_Query_Record *query = &all_queries[qi];
        SV_List versions = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), query->versions, &versions)) return false;

        for (size_t vi = 0; vi < arena_arr_len(versions); vi++) {
            String_View json_file = nob_sv_from_cstr("");
            if (!meta_file_api_append_object_unique(ctx,
                                                    &objects,
                                                    query->kind_upper,
                                                    query->kind_json,
                                                    versions[vi],
                                                    &json_file)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(objects); i++) {
        if (!meta_file_api_write_object_file(ctx, reply_dir, &objects[i])) return false;
        String_View reply_var_key = meta_concat3_temp(ctx, "NOBIFY_CMAKE_FILE_API_REPLY::", objects[i].kind_upper, "::FILE");
        String_View reply_path = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, objects[i].json_file);
        if (eval_should_stop(ctx)) return false;
        if (!eval_var_set_current(ctx, reply_var_key, reply_path)) return false;
    }

    ctx->semantic_state.file_api.next_reply_nonce += 1;
    String_View index_name = nob_sv_from_cstr(nob_temp_sprintf("index-%zu.json",
                                                               ctx->semantic_state.file_api.next_reply_nonce));
    String_View index_file = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, index_name);
    if (eval_should_stop(ctx)) return false;
    if (!meta_file_api_write_index_from_queries(ctx, index_file, &all_queries, objects)) return false;

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("CMAKE_FILE_API"), nob_sv_from_cstr("1"))) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::ROOT", root_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::QUERY_DIR", query_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::QUERY_FILE", nob_sv_from_cstr(""))) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::REPLY_DIR", reply_dir)) return false;
    if (!meta_file_api_set_path_var(ctx, "NOBIFY_CMAKE_FILE_API::INDEX_FILE", index_file)) return false;

    Eval_Canonical_Draft draft = {0};
    eval_canonical_draft_init(&draft);

    Eval_Canonical_Artifact index_artifact = {
        .producer = nob_sv_from_cstr("cmake_file_api"),
        .kind = nob_sv_from_cstr("INDEX_FILE"),
        .status = nob_sv_from_cstr("STAGED"),
        .base_dir = reply_dir,
        .primary_path = index_file,
    };
    if (!eval_canonical_draft_add_artifact(ctx, &draft, &index_artifact, NULL)) return false;

    for (size_t i = 0; i < arena_arr_len(objects); i++) {
        String_View reply_path = eval_sv_path_join(eval_temp_arena(ctx), reply_dir, objects[i].json_file);
        if (eval_should_stop(ctx)) return false;
        Eval_Canonical_Artifact reply_artifact = {
            .producer = nob_sv_from_cstr("cmake_file_api"),
            .kind = meta_concat3_temp(ctx, "REPLY_", objects[i].kind_upper, ""),
            .status = nob_sv_from_cstr("STAGED"),
            .base_dir = reply_dir,
            .primary_path = reply_path,
        };
        if (!eval_canonical_draft_add_artifact(ctx, &draft, &reply_artifact, NULL)) return false;
    }

    return eval_canonical_draft_commit(ctx, &draft);
}

static bool meta_file_api_parse_request(EvalExecContext *ctx,
                                        const Node *node,
                                        SV_List a,
                                        Meta_File_Api_Query_Request *out) {
    if (!ctx || !node || !out) return false;
    if (arena_arr_len(a) < 3) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() requires QUERY API_VERSION <major>"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 [CODEMODEL <v>...])"));
        return false;
    }
    if (!eval_sv_eq_ci_lit(a[0], "QUERY")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports the QUERY form in evaluator v2"),
                             a[0]);
        return false;
    }
    if (!eval_sv_eq_ci_lit(a[1], "API_VERSION")) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api(QUERY ...) requires API_VERSION"),
                             nob_sv_from_cstr("Usage: cmake_file_api(QUERY API_VERSION 1 ...)"));
        return false;
    }
    if (!nob_sv_eq(a[2], nob_sv_from_cstr("1"))) {
        (void)meta_emit_diag(ctx,
                             node,
                             EV_DIAG_ERROR,
                             nob_sv_from_cstr("cmake_file_api() only supports API_VERSION 1 in this batch"),
                             a[2]);
        return false;
    }

    out->api_version = a[2];

    size_t i = 3;
    while (i < arena_arr_len(a)) {
        String_View kind = a[i++];
        if (!meta_file_api_kind_supported(kind)) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() object kind is not implemented in evaluator v2"),
                                 kind);
            return false;
        }

        SV_List versions = NULL;
        while (i < arena_arr_len(a) && !meta_file_api_kind_supported(a[i])) {
            if (!meta_file_api_is_version(a[i])) {
                (void)meta_emit_diag(ctx,
                                     node,
                                     EV_DIAG_ERROR,
                                     nob_sv_from_cstr("cmake_file_api() received an invalid object version token"),
                                     a[i]);
                return false;
            }
            if (!svu_list_push_temp(ctx, &versions, a[i])) return false;
            i++;
        }

        if (arena_arr_len(versions) == 0) {
            (void)meta_emit_diag(ctx,
                                 node,
                                 EV_DIAG_ERROR,
                                 nob_sv_from_cstr("cmake_file_api() requires at least one version for each object kind"),
                                 kind);
            return false;
        }

        String_View joined = eval_sv_join_semi_temp(ctx, versions, arena_arr_len(versions));
        if (eval_should_stop(ctx)) return false;
        String_View kind_upper = eval_property_upper_name_temp(ctx, kind);
        String_View kind_json = meta_file_api_kind_json_name(kind);
        if (eval_should_stop(ctx)) return false;

        Meta_File_Api_Request req = {
            .kind_upper = kind_upper,
            .kind_json = kind_json,
            .versions = joined,
        };
        if (!EVAL_ARR_PUSH(ctx, ctx->arena, out->requests, req)) return false;
    }

    return true;
}

static bool meta_file_api_publish_query_vars(EvalExecContext *ctx,
                                             const Meta_File_Api_Query_Request *req) {
    if (!ctx || !req) return false;
    if (!eval_var_set_current(ctx,
                              nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::API_VERSION"),
                              req->api_version)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->requests); i++) {
        if (!eval_var_set_current(ctx,
                                  meta_concat3_temp(ctx,
                                                    "NOBIFY_CMAKE_FILE_API_QUERY::",
                                                    req->requests[i].kind_upper,
                                                    ""),
                                  req->requests[i].versions)) {
            return false;
        }
    }

    return true;
}

static bool meta_file_api_execute_request(EvalExecContext *ctx,
                                          const Meta_File_Api_Query_Request *req) {
    if (!ctx || !req) return false;
    if (!meta_file_api_publish_query_vars(ctx, req)) return false;
    return meta_file_api_stage_replies(ctx, req);
}

Eval_Result eval_handle_cmake_file_api(EvalExecContext *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Meta_File_Api_Query_Request req = {0};
    if (!meta_file_api_parse_request(ctx, node, a, &req)) return eval_result_from_ctx(ctx);
    if (!meta_file_api_execute_request(ctx, &req)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
