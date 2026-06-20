#include "eval_try_compile_internal.h"

#include "eval_expr.h"
#include "lexer.h"

typedef struct {
    String_View name;
    String_View alias_of;
    Cmake_Target_Type type;
    bool imported;
    bool alias;
    Try_Compile_Source_List sources;
    SV_List compile_definitions;
    SV_List compile_options;
    SV_List include_directories;
    SV_List link_options;
    SV_List link_directories;
    SV_List link_libraries;
    Try_Compile_Lang_Props c_lang;
    Try_Compile_Lang_Props cxx_lang;
    bool building;
    bool built;
    String_View artifact_path;
} Try_Compile_Mini_Target;

typedef struct {
    Try_Compile_Mini_Target *items;
    String_View project_name;
} Try_Compile_Mini_Project;

static bool try_compile_emit_probe_replay(EvalExecContext *ctx,
                                          Cmake_Event_Origin origin,
                                          const Try_Compile_Request *req,
                                          const Try_Compile_Execution_Result *exec_res) {
    String_View action_key = nob_sv_from_cstr("");
    Event_Replay_Opcode opcode = EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE;
    if (!ctx || !req) return false;
    opcode = req->signature == TRY_COMPILE_SIGNATURE_PROJECT
        ? EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT
        : EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE;
    if (!eval_begin_replay_action(ctx,
                                  origin,
                                  EVENT_REPLAY_ACTION_PROBE,
                                  opcode,
                                  EVENT_REPLAY_PHASE_CONFIGURE,
                                  req->binary_dir,
                                  &action_key) ||
        !eval_emit_replay_action_add_output(ctx, origin, action_key, req->binary_dir) ||
        !eval_emit_replay_action_add_argv(ctx, origin, action_key, 0, req->result_var) ||
        !eval_emit_replay_action_add_argv(ctx,
                                          origin,
                                          action_key,
                                          1,
                                          exec_res ? exec_res->artifact_path : nob_sv_from_cstr(""))) {
        return false;
    }
    if (req->signature == TRY_COMPILE_SIGNATURE_PROJECT) {
        if (!eval_emit_replay_action_add_input(ctx, origin, action_key, req->source_dir) ||
            !eval_emit_replay_action_add_argv(ctx,
                                              origin,
                                              action_key,
                                              2,
                                              req->target_name.count > 0 ? req->target_name
                                                                         : req->project_name)) {
            return false;
        }
    } else {
        for (size_t i = 0; i < req->source_items.count; ++i) {
            if (!eval_emit_replay_action_add_input(ctx,
                                                   origin,
                                                   action_key,
                                                   req->source_items.items[i].path)) {
                return false;
            }
        }
        if (!eval_emit_replay_action_add_argv(ctx,
                                              origin,
                                              action_key,
                                              2,
                                              exec_res && exec_res->ok ? nob_sv_from_cstr("1")
                                                                       : nob_sv_from_cstr("0"))) {
            return false;
        }
    }
    return true;
}

static bool try_compile_cache_upsert(EvalExecContext *ctx, String_View key, String_View value) {
    if (!ctx) return false;
    Eval_Cache_Entry *entry = NULL;
    if (ctx->scope_state.cache_entries) entry = stbds_shgetp_null(ctx->scope_state.cache_entries, nob_temp_sv_to_cstr(key));
    if (entry) {
        entry->value.data = sv_copy_to_event_arena(ctx, value);
        entry->value.type = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("INTERNAL"));
        entry->value.doc = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("try_compile result"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    char *stable_key = (char*)arena_alloc(eval_event_arena(ctx), key.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);
    if (key.count > 0) memcpy(stable_key, key.data, key.count);
    stable_key[key.count] = '\0';

    Eval_Cache_Value cv = {0};
    cv.data = sv_copy_to_event_arena(ctx, value);
    cv.type = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("INTERNAL"));
    cv.doc = sv_copy_to_event_arena(ctx, nob_sv_from_cstr("try_compile result"));
    if (eval_should_stop(ctx)) return false;

    Eval_Cache_Entry *entries = ctx->scope_state.cache_entries;
    stbds_shput(entries, stable_key, cv);
    ctx->scope_state.cache_entries = entries;
    return true;
}

static bool try_compile_publish_result(EvalExecContext *ctx,
                                       Cmake_Event_Origin origin,
                                       const Try_Compile_Request *req,
                                       String_View result,
                                       String_View output_text) {
    if (!ctx || !req) return false;
    if (req->no_cache) {
        if (!eval_var_set_current(ctx, req->result_var, result)) return false;
    } else {
        if (!try_compile_cache_upsert(ctx, req->result_var, result)) return false;
        if (!eval_emit_var_set_cache(ctx, origin, req->result_var, result)) return false;
    }
    if (req->output_var.count > 0) {
        if (!eval_var_set_current(ctx, req->output_var, output_text)) return false;
    }
    return true;
}

static bool try_compile_append_define_arg(EvalExecContext *ctx, Nob_Cmd *cmd, String_View def, bool msvc) {
    if (!ctx || !cmd) return false;
    if (def.count == 0) return true;
    if (nob_sv_starts_with(def, nob_sv_from_cstr("-D")) || nob_sv_starts_with(def, nob_sv_from_cstr("/D"))) {
        def = nob_sv_from_parts(def.data + 2, def.count - 2);
    }
    const char *prefix = msvc ? "/D" : "-D";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, def));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_include_arg(EvalExecContext *ctx, Nob_Cmd *cmd, String_View dir, bool msvc) {
    if (!ctx || !cmd) return false;
    if (dir.count == 0) return true;
    String_View resolved = try_compile_resolve_in_dir(ctx, dir, try_compile_current_src_dir(ctx));
    const char *prefix = msvc ? "/I" : "-I";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, resolved));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_link_dir_arg(EvalExecContext *ctx, Nob_Cmd *cmd, String_View dir, bool msvc) {
    if (!ctx || !cmd) return false;
    if (dir.count == 0) return true;
    String_View resolved = try_compile_resolve_in_dir(ctx, dir, try_compile_current_src_dir(ctx));
    const char *prefix = msvc ? "/LIBPATH:" : "-L";
    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, prefix, resolved));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_tokenized_flags(EvalExecContext *ctx,
                                               Nob_Cmd *cmd,
                                               String_View raw_flags) {
    if (!ctx || !cmd) return false;
    if (raw_flags.count == 0) return true;
    size_t i = 0;
    while (i < raw_flags.count) {
        while (i < raw_flags.count && isspace((unsigned char)raw_flags.data[i])) i++;
        size_t start = i;
        while (i < raw_flags.count && !isspace((unsigned char)raw_flags.data[i])) i++;
        if (i > start) {
            char *arg = eval_sv_to_cstr_temp(ctx, nob_sv_from_parts(raw_flags.data + start, i - start));
            EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
            nob_cmd_append(cmd, arg);
        }
    }
    return true;
}

static bool try_compile_compiler_id_is_clang(String_View id) {
    return eval_sv_eq_ci_lit(id, "Clang") || eval_sv_eq_ci_lit(id, "AppleClang");
}

static bool try_compile_append_prefixed_semicolon_items(EvalExecContext *ctx,
                                                        Nob_Cmd *cmd,
                                                        String_View items,
                                                        const char *prefix) {
    if (!ctx || !cmd || items.count == 0) return true;
    size_t start = 0;
    for (size_t i = 0; i <= items.count; ++i) {
        if (i == items.count || items.data[i] == ';') {
            String_View item = nob_sv_trim(nob_sv_from_parts(items.data + start, i - start));
            if (item.count > 0) {
                char *item_c = eval_sv_to_cstr_temp(ctx, item);
                EVAL_OOM_RETURN_IF_NULL(ctx, item_c, false);
                if (prefix && prefix[0] != '\0') nob_cmd_append(cmd, prefix);
                nob_cmd_append(cmd, item_c);
            }
            start = i + 1;
        }
    }
    return true;
}

static bool try_compile_append_toolchain_flags(EvalExecContext *ctx,
                                               Nob_Cmd *cmd,
                                               Try_Compile_Language lang,
                                               bool msvc) {
    if (!ctx || !cmd || msvc) return true;
    const struct Eval_Toolchain_Model *model = eval_toolchain_current(ctx);
    if (!model) return true;
    const Eval_Toolchain_Language_Model *language = (lang == TRY_COMPILE_LANG_CXX) ? &model->cxx : &model->c;
    if (model->sysroot.count > 0) {
        String_View arg = try_compile_concat_prefix_temp(ctx, "--sysroot=", model->sysroot);
        char *arg_c = eval_sv_to_cstr_temp(ctx, arg);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg_c, false);
        nob_cmd_append(cmd, arg_c);
    }
    if (language->target_triple.count > 0 && try_compile_compiler_id_is_clang(language->compiler_id)) {
        String_View arg = try_compile_concat_prefix_temp(ctx, "--target=", language->target_triple);
        char *arg_c = eval_sv_to_cstr_temp(ctx, arg);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg_c, false);
        nob_cmd_append(cmd, arg_c);
    }
    if (model->sdkroot.count > 0 &&
        (eval_sv_eq_ci_lit(model->platform_id, "Darwin") ||
         eval_sv_eq_ci_lit(model->platform_id, "iOS"))) {
        char *sdkroot_c = eval_sv_to_cstr_temp(ctx, model->sdkroot);
        EVAL_OOM_RETURN_IF_NULL(ctx, sdkroot_c, false);
        nob_cmd_append(cmd, "-isysroot", sdkroot_c);
    }
    if ((eval_sv_eq_ci_lit(model->platform_id, "Darwin") ||
         eval_sv_eq_ci_lit(model->platform_id, "iOS")) &&
        !try_compile_append_prefixed_semicolon_items(ctx, cmd, model->osx_architectures, "-arch")) {
        return false;
    }
    if (model->osx_deployment_target.count > 0) {
        const char *flag = eval_sv_eq_ci_lit(model->platform_id, "iOS")
            ? "-miphoneos-version-min="
            : (eval_sv_eq_ci_lit(model->platform_id, "Darwin") ? "-mmacosx-version-min=" : NULL);
        if (flag) {
            String_View arg = try_compile_concat_prefix_temp(ctx, flag, model->osx_deployment_target);
            char *arg_c = eval_sv_to_cstr_temp(ctx, arg);
            EVAL_OOM_RETURN_IF_NULL(ctx, arg_c, false);
            nob_cmd_append(cmd, arg_c);
        }
    }
    return true;
}

static bool try_compile_append_required_compile_settings(EvalExecContext *ctx,
                                                         Nob_Cmd *cmd,
                                                         const Try_Compile_Request *req,
                                                         Try_Compile_Language lang,
                                                         bool msvc) {
    if (!ctx || !cmd || !req) return false;

    for (size_t i = 0; i < arena_arr_len(req->compile_definitions); i++) {
        if (!try_compile_append_define_arg(ctx, cmd, req->compile_definitions[i], msvc)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->include_directories); i++) {
        if (!try_compile_append_include_arg(ctx, cmd, req->include_directories[i], msvc)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->compile_options); i++) {
        char *arg = eval_sv_to_cstr_temp(ctx, req->compile_options[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
    }

    String_View required_defs = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_DEFINITIONS"));
    if (required_defs.count > 0) {
        SV_List parts = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_defs, &parts)) return false;
        for (size_t i = 0; i < arena_arr_len(parts); i++) {
            if (!try_compile_append_define_arg(ctx, cmd, parts[i], msvc)) return false;
        }
    }

    String_View required_includes = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_INCLUDES"));
    if (required_includes.count > 0) {
        SV_List incs = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_includes, &incs)) return false;
        for (size_t i = 0; i < arena_arr_len(incs); i++) {
            if (!try_compile_append_include_arg(ctx, cmd, incs[i], msvc)) return false;
        }
    }

    if (!try_compile_append_tokenized_flags(ctx, cmd, eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_FLAGS")))) {
        return false;
    }

    const Try_Compile_Lang_Props *props = (lang == TRY_COMPILE_LANG_CXX) ? &req->cxx_lang : &req->c_lang;
    if (props->has_value && props->standard.count > 0 && !msvc) {
        bool extensions = !props->extensions_set || props->extensions;
        const char *prefix = (lang == TRY_COMPILE_LANG_CXX)
            ? (extensions ? "gnu++" : "c++")
            : (extensions ? "gnu" : "c");
        String_View std_flag = nob_sv_from_cstr("");
        if (nob_sv_starts_with(props->standard, nob_sv_from_cstr("gnu")) ||
            nob_sv_starts_with(props->standard, nob_sv_from_cstr("c"))) {
            std_flag = try_compile_concat_prefix_temp(ctx, "-std=", props->standard);
        } else {
            std_flag = try_compile_concat_prefix_temp(
                ctx,
                "-std=",
                try_compile_concat_prefix_temp(ctx, prefix, props->standard));
        }
        char *std_c = eval_sv_to_cstr_temp(ctx, std_flag);
        EVAL_OOM_RETURN_IF_NULL(ctx, std_c, false);
        nob_cmd_append(cmd, std_c);
    }

    return true;
}

static bool try_compile_append_link_library_arg(EvalExecContext *ctx,
                                                Nob_Cmd *cmd,
                                                String_View lib,
                                                const Try_Compile_Target_Artifact *artifacts,
                                                size_t artifact_count,
                                                bool msvc) {
    if (!ctx || !cmd) return false;
    if (lib.count == 0) return true;
    for (size_t i = 0; i < artifact_count; i++) {
        if (nob_sv_eq(artifacts[i].key, lib)) {
            char *artifact_c = eval_sv_to_cstr_temp(ctx, artifacts[i].value);
            EVAL_OOM_RETURN_IF_NULL(ctx, artifact_c, false);
            nob_cmd_append(cmd, artifact_c);
            return true;
        }
    }

    if (msvc) {
        char *arg = eval_sv_to_cstr_temp(ctx, lib);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
        return true;
    }

    if (nob_sv_starts_with(lib, nob_sv_from_cstr("-l")) ||
        nob_sv_starts_with(lib, nob_sv_from_cstr("-L")) ||
        eval_sv_is_abs_path(lib) ||
        nob_sv_end_with(lib, ".a") ||
        nob_sv_end_with(lib, ".so") ||
        nob_sv_end_with(lib, ".dylib") ||
        nob_sv_end_with(lib, ".o")) {
        char *arg = eval_sv_to_cstr_temp(ctx, lib);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
        return true;
    }

    char *arg = eval_sv_to_cstr_temp(ctx, try_compile_concat_prefix_temp(ctx, "-l", lib));
    EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
    nob_cmd_append(cmd, arg);
    return true;
}

static bool try_compile_append_required_link_settings(EvalExecContext *ctx,
                                                      Nob_Cmd *cmd,
                                                      const Try_Compile_Request *req,
                                                      const Try_Compile_Target_Artifact *artifacts,
                                                      size_t artifact_count,
                                                      bool msvc) {
    if (!ctx || !cmd || !req) return false;

    for (size_t i = 0; i < arena_arr_len(req->link_options); i++) {
        char *arg = eval_sv_to_cstr_temp(ctx, req->link_options[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
        nob_cmd_append(cmd, arg);
    }

    for (size_t i = 0; i < arena_arr_len(req->link_directories); i++) {
        if (!try_compile_append_link_dir_arg(ctx, cmd, req->link_directories[i], msvc)) return false;
    }

    String_View required_link_options = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LINK_OPTIONS"));
    if (required_link_options.count > 0) {
        SV_List opts = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_link_options, &opts)) return false;
        for (size_t i = 0; i < arena_arr_len(opts); i++) {
            char *arg = eval_sv_to_cstr_temp(ctx, opts[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, arg, false);
            nob_cmd_append(cmd, arg);
        }
    }

    String_View required_link_dirs = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LINK_DIRECTORIES"));
    if (required_link_dirs.count > 0) {
        SV_List dirs = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_link_dirs, &dirs)) return false;
        for (size_t i = 0; i < arena_arr_len(dirs); i++) {
            if (!try_compile_append_link_dir_arg(ctx, cmd, dirs[i], msvc)) return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(req->link_libraries); i++) {
        if (!try_compile_append_link_library_arg(ctx, cmd, req->link_libraries[i], artifacts, artifact_count, msvc)) {
            return false;
        }
    }

    String_View required_libs = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_REQUIRED_LIBRARIES"));
    if (required_libs.count > 0) {
        SV_List libs = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), required_libs, &libs)) return false;
        for (size_t i = 0; i < arena_arr_len(libs); i++) {
            if (!try_compile_append_link_library_arg(ctx, cmd, libs[i], artifacts, artifact_count, msvc)) return false;
        }
    }

    return true;
}

static bool try_compile_materialize_output_path(EvalExecContext *ctx,
                                                String_View bindir,
                                                String_View base_name,
                                                const char *ext,
                                                String_View *out_path) {
    if (!ctx || !out_path) return false;
    String_View name = base_name.count > 0 ? base_name : nob_sv_from_cstr("cmk2nob_try_compile");
    if (ext && ext[0] != '\0') {
        name = svu_concat_suffix_temp(ctx, name, ext);
    }
    *out_path = eval_sv_path_join(eval_temp_arena(ctx), bindir, name);
    return true;
}

static bool try_compile_sv_has_dir_prefix(String_View path, String_View prefix) {
    if (path.count == 0 || prefix.count == 0) return false;
    while (path.count > 2 && path.data[0] == '.' && (path.data[1] == '/' || path.data[1] == '\\')) {
        path = nob_sv_from_parts(path.data + 2, path.count - 2);
    }
    while (prefix.count > 2 && prefix.data[0] == '.' && (prefix.data[1] == '/' || prefix.data[1] == '\\')) {
        prefix = nob_sv_from_parts(prefix.data + 2, prefix.count - 2);
    }
    if (path.count < prefix.count) return false;
    if (memcmp(path.data, prefix.data, prefix.count) != 0) return false;
    return path.count == prefix.count ||
           path.data[prefix.count] == '/' ||
           path.data[prefix.count] == '\\';
}

static String_View try_compile_display_source_path(EvalExecContext *ctx,
                                                   const Try_Compile_Request *req,
                                                   String_View path) {
    if (!ctx || !req || path.count == 0 || !eval_sv_is_abs_path(path)) return path;

    String_View base = req->current_src_dir;
    if (base.count == 0) return path;
    if (!eval_sv_is_abs_path(base)) {
        String_View cwd = eval_process_cwd_temp(ctx);
        if (cwd.count == 0) return path;
        if (eval_sv_eq_ci_lit(base, ".") || eval_sv_eq_ci_lit(base, "./.")) {
            base = cwd;
        } else {
            base = eval_sv_path_join(eval_temp_arena(ctx), cwd, base);
        }
    }
    if (!try_compile_sv_has_dir_prefix(path, base)) return path;

    size_t rel_offset = base.count;
    while (rel_offset < path.count && (path.data[rel_offset] == '/' || path.data[rel_offset] == '\\')) {
        rel_offset++;
    }
    if (rel_offset >= path.count) return path;

    String_View rel = nob_sv_from_parts(path.data + rel_offset, path.count - rel_offset);
    if (eval_sv_eq_ci_lit(req->current_src_dir, ".")) {
        return eval_sv_path_join(eval_temp_arena(ctx), nob_sv_from_cstr("."), rel);
    }
    return rel;
}

static bool try_compile_execute_source_request_direct(EvalExecContext *ctx,
                                                      const Try_Compile_Request *req,
                                                      const Try_Compile_Target_Artifact *artifacts,
                                                      size_t artifact_count,
                                                      Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};

    Nob_String_Builder log = {0};
    SV_List object_paths = {0};
    size_t compile_units = 0;
    bool any_cxx = false;
    bool any_c = false;

    for (size_t i = 0; i < req->source_items.count; i++) {
        Try_Compile_Source_Item item = req->source_items.items[i];
        Try_Compile_Language lang = item.language != TRY_COMPILE_LANG_AUTO
            ? item.language
            : try_compile_detect_language(item.path);
        if (lang != TRY_COMPILE_LANG_C && lang != TRY_COMPILE_LANG_CXX) continue;
        bool changed = false;
        Cmake_Event_Origin origin = {0};
        String_View cmake_lang = lang == TRY_COMPILE_LANG_CXX ? nob_sv_from_cstr("CXX") : nob_sv_from_cstr("C");
        if (!eval_toolchain_enable_language(ctx,
                                            origin,
                                            nob_sv_from_cstr("try_compile"),
                                            cmake_lang,
                                            &changed)) {
            nob_sb_free(log);
            return false;
        }
        if (changed && !eval_toolchain_emit_snapshot(ctx)) {
            nob_sb_free(log);
            return false;
        }
    }

    bool msvc = eval_toolchain_uses_msvc(ctx);

    for (size_t i = 0; i < req->source_items.count; i++) {
        Try_Compile_Source_Item item = req->source_items.items[i];
        String_View src = item.path;
        String_View display_src = item.path;
        if (!eval_sv_is_abs_path(src) && !try_compile_sv_has_dir_prefix(src, req->current_src_dir)) {
            src = eval_sv_path_join(eval_temp_arena(ctx), req->current_src_dir, src);
        }
        Try_Compile_Language lang = item.language != TRY_COMPILE_LANG_AUTO
            ? item.language
            : try_compile_detect_language(src);

        if (lang == TRY_COMPILE_LANG_HEADERS) continue;
        if (lang == TRY_COMPILE_LANG_AUTO) {
            nob_sb_append_cstr(&log, "try_compile failed: unsupported source language\n");
            out_res->output = try_compile_finish_log(ctx, &log);
            return true;
        }
        if (!try_compile_file_exists_sv(ctx, src)) {
            nob_sb_append_cstr(&log, "try_compile source file not found: ");
            display_src = try_compile_display_source_path(ctx, req, display_src);
            nob_sb_append_buf(&log, display_src.data, display_src.count);
            nob_sb_append(&log, '\n');
            out_res->output = try_compile_finish_log(ctx, &log);
            return true;
        }

        compile_units++;
        any_cxx = any_cxx || (lang == TRY_COMPILE_LANG_CXX);
        any_c = any_c || (lang == TRY_COMPILE_LANG_C);

        String_View object_suffix = eval_toolchain_object_suffix(ctx);
        String_View obj_name = sv_copy_to_arena(eval_temp_arena(ctx),
                                                nob_sv_from_cstr(nob_temp_sprintf("obj_%zu%.*s",
                                                                                 i,
                                                                                 (int)object_suffix.count,
                                                                                 object_suffix.data ? object_suffix.data : "")));
        String_View obj_path = eval_sv_path_join(eval_temp_arena(ctx), req->binary_dir, obj_name);
        char *compiler_c = eval_sv_to_cstr_temp(
            ctx,
            lang == TRY_COMPILE_LANG_CXX
                ? eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"))
                : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER")));
        char *src_c = eval_sv_to_cstr_temp(ctx, src);
        char *obj_c = eval_sv_to_cstr_temp(ctx, obj_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, compiler_c, false);
        EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
        EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, compiler_c);
        if (!try_compile_append_toolchain_flags(ctx, &cmd, lang, msvc)) {
            nob_cmd_free(cmd);
            nob_sb_free(log);
            return false;
        }
        if (msvc) {
            String_View fo_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                                  nob_sv_from_cstr(nob_temp_sprintf("/Fo:%s", obj_c)));
            nob_cmd_append(&cmd, "/nologo", "/c", src_c, fo_arg.data);
        } else
        {
            nob_cmd_append(&cmd, "-c", src_c, "-o", obj_c);
        }

        if (!try_compile_append_required_compile_settings(ctx, &cmd, req, lang, msvc)) {
            nob_cmd_free(cmd);
            nob_sb_free(log);
            return false;
        }

        bool cmd_ok = false;
        if (!try_compile_run_command_captured(ctx, &cmd, req->binary_dir, &log, &cmd_ok)) {
            nob_cmd_free(cmd);
            nob_sb_free(log);
            return false;
        }
        nob_cmd_free(cmd);
        if (!cmd_ok) {
            out_res->output = try_compile_finish_log(ctx, &log);
            out_res->ok = false;
            return true;
        }

        if (!eval_sv_arr_push_temp(ctx, &object_paths, obj_path)) {
            nob_sb_free(log);
            return false;
        }
    }

    if (compile_units == 0) {
        nob_sb_append_cstr(&log, "try_compile failed: no compilable source units\n");
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->ok = false;
        return true;
    }

    String_View target_type = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_TARGET_TYPE"));
    Try_Compile_Build_Kind build_kind = req->has_build_kind
        ? req->build_kind
        : (req->signature == TRY_COMPILE_SIGNATURE_SOURCE
        ? (eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY")
            ? TRY_COMPILE_BUILD_STATIC_LIBRARY
            : TRY_COMPILE_BUILD_EXECUTABLE)
        : TRY_COMPILE_BUILD_EXECUTABLE);

    String_View output_path = nob_sv_from_cstr("");
    if (build_kind == TRY_COMPILE_BUILD_STATIC_LIBRARY) {
        String_View static_prefix = eval_toolchain_static_library_prefix(ctx);
        String_View static_suffix = eval_toolchain_static_library_suffix(ctx);
        String_View static_base = req->output_name.count > 0
            ? req->output_name
            : nob_sv_from_cstr("cmk2nob_try_compile");
        char *static_suffix_c = eval_sv_to_cstr_temp(ctx, static_suffix);
        EVAL_OOM_RETURN_IF_NULL(ctx, static_suffix_c, false);
        if (static_prefix.count > 0) {
            char *static_base_c = eval_sv_to_cstr_temp(ctx, static_base);
            EVAL_OOM_RETURN_IF_NULL(ctx, static_base_c, false);
            char *static_name_c = nob_temp_sprintf("%.*s%s",
                                                   (int)static_prefix.count,
                                                   static_prefix.data ? static_prefix.data : "",
                                                   static_base_c);
            static_base = nob_sv_from_cstr(static_name_c);
        }
        if (!try_compile_materialize_output_path(ctx,
                                                 req->binary_dir,
                                                 static_base,
                                                 static_suffix_c,
                                                 &output_path)) {
            nob_sb_free(log);
            return false;
        }
        char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        Nob_Cmd cmd = {0};
        char *archive_tool_c = eval_sv_to_cstr_temp(ctx, eval_toolchain_archive_tool(ctx));
        EVAL_OOM_RETURN_IF_NULL(ctx, archive_tool_c, false);
        if (msvc) {
            String_View out_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                                   nob_sv_from_cstr(nob_temp_sprintf("/OUT:%s", out_c)));
            nob_cmd_append(&cmd, archive_tool_c, "/NOLOGO", out_arg.data);
        } else {
            nob_cmd_append(&cmd, archive_tool_c, "rcs", out_c);
        }
        for (size_t i = 0; i < arena_arr_len(object_paths); i++) {
            char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
            nob_cmd_append(&cmd, obj_c);
        }
        bool cmd_ok = false;
        if (!try_compile_run_command_captured(ctx, &cmd, req->binary_dir, &log, &cmd_ok)) {
            nob_cmd_free(cmd);
            nob_sb_free(log);
            return false;
        }
        nob_cmd_free(cmd);
        out_res->ok = cmd_ok;
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->artifact_path = cmd_ok ? output_path : nob_sv_from_cstr("");
        return true;
    }

    Try_Compile_Language link_lang = TRY_COMPILE_LANG_C;
    if (req->linker_language.count > 0) {
        if (eval_sv_eq_ci_lit(req->linker_language, "CXX")) link_lang = TRY_COMPILE_LANG_CXX;
        else link_lang = TRY_COMPILE_LANG_C;
    } else if (any_cxx) {
        link_lang = TRY_COMPILE_LANG_CXX;
    } else if (any_c) {
        link_lang = TRY_COMPILE_LANG_C;
    }

    if (!try_compile_materialize_output_path(ctx,
                                             req->binary_dir,
                                             req->output_name.count > 0
                                                 ? req->output_name
                                                 : nob_sv_from_cstr("cmk2nob_try_compile"),
                                             eval_sv_to_cstr_temp(ctx, eval_toolchain_executable_suffix(ctx)),
                                             &output_path)) {
        nob_sb_free(log);
        return false;
    }

    char *linker_c = eval_sv_to_cstr_temp(
        ctx,
        link_lang == TRY_COMPILE_LANG_CXX
            ? eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"))
            : eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER")));
    char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, linker_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);

    Nob_Cmd link_cmd = {0};
    nob_cmd_append(&link_cmd, linker_c);
    if (!try_compile_append_toolchain_flags(ctx, &link_cmd, link_lang, msvc)) {
        nob_cmd_free(link_cmd);
        nob_sb_free(log);
        return false;
    }
    if (msvc) {
        String_View fe_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                              nob_sv_from_cstr(nob_temp_sprintf("/Fe:%s", out_c)));
        nob_cmd_append(&link_cmd, "/nologo", fe_arg.data);
    } else
    {
        nob_cmd_append(&link_cmd, "-o", out_c);
    }
    for (size_t i = 0; i < arena_arr_len(object_paths); i++) {
        char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
        nob_cmd_append(&link_cmd, obj_c);
    }
    if (!try_compile_append_required_link_settings(ctx, &link_cmd, req, artifacts, artifact_count, msvc)) {
        nob_cmd_free(link_cmd);
        nob_sb_free(log);
        return false;
    }

    bool link_ok = false;
    if (!try_compile_run_command_captured(ctx, &link_cmd, req->binary_dir, &log, &link_ok)) {
        nob_cmd_free(link_cmd);
        nob_sb_free(log);
        return false;
    }
    nob_cmd_free(link_cmd);

    out_res->ok = link_ok;
    out_res->artifact_path = link_ok ? output_path : nob_sv_from_cstr("");
    out_res->output = try_compile_finish_log(ctx, &log);
    return true;
}

static bool try_compile_sb_append_sv(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    if (sv.count > 0) nob_sb_append_buf(sb, sv.data, sv.count);
    return true;
}

static bool try_compile_sb_append_bracket_arg(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, "[=[");
    if (!try_compile_sb_append_sv(sb, sv)) return false;
    nob_sb_append_cstr(sb, "]=]");
    return true;
}

static bool try_compile_parse_script_in_arena(EvalExecContext *ctx,
                                              Arena *arena,
                                              String_View script,
                                              Ast_Root *out_ast) {
    if (!ctx || !arena || !out_ast) return false;
    *out_ast = NULL;
    Lexer lx = lexer_init(script);
    Token_List toks = NULL;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!arena_arr_push(arena, toks, t)) return ctx_oom(ctx);
    }
    *out_ast = parse_tokens(arena, toks);
    return true;
}

static bool try_compile_cmake_flag_to_cache_set(EvalExecContext *ctx,
                                                String_View flag,
                                                Nob_String_Builder *sb) {
    if (!ctx || !sb || flag.count < 3 || !nob_sv_starts_with(flag, nob_sv_from_cstr("-D"))) return true;
    String_View body = nob_sv_from_parts(flag.data + 2, flag.count - 2);
    size_t eq = SIZE_MAX;
    for (size_t i = 0; i < body.count; i++) {
        if (body.data[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq == SIZE_MAX || eq == 0) return true;
    String_View key = nob_sv_from_parts(body.data, eq);
    String_View value = nob_sv_from_parts(body.data + eq + 1, body.count - eq - 1);
    for (size_t i = 0; i < key.count; i++) {
        if (key.data[i] == ':') {
            key = nob_sv_from_parts(key.data, i);
            break;
        }
    }
    if (key.count == 0) return true;
    nob_sb_append_cstr(sb, "set(");
    if (!try_compile_sb_append_bracket_arg(sb, key)) return false;
    nob_sb_append(&sb[0], ' ');
    if (!try_compile_sb_append_bracket_arg(sb, value)) return false;
    nob_sb_append_cstr(sb, " CACHE INTERNAL \"try_compile CMAKE_FLAGS\")\n");
    return true;
}

static bool try_compile_append_cache_set_if_visible(EvalExecContext *ctx,
                                                    Nob_String_Builder *sb,
                                                    const char *key) {
    if (!ctx || !sb || !key) return false;
    String_View value = eval_var_get_visible(ctx, nob_sv_from_cstr(key));
    if (value.count == 0) return true;
    nob_sb_append_cstr(sb, "set(");
    if (!try_compile_sb_append_bracket_arg(sb, nob_sv_from_cstr(key))) return false;
    nob_sb_append(&sb[0], ' ');
    if (!try_compile_sb_append_bracket_arg(sb, value)) return false;
    nob_sb_append_cstr(sb, " CACHE INTERNAL \"try_compile inherited\")\n");
    return true;
}

static bool try_compile_build_mini_prelude(EvalExecContext *ctx,
                                           const Try_Compile_Request *req,
                                           Nob_String_Builder *sb) {
    if (!ctx || !req || !sb) return false;
    const char *keys[] = {
        "CMAKE_TOOLCHAIN_FILE",
        "CMAKE_SYSROOT",
        "CMAKE_C_COMPILER",
        "CMAKE_CXX_COMPILER",
        "CMAKE_C_COMPILER_TARGET",
        "CMAKE_CXX_COMPILER_TARGET",
        "CMAKE_AR",
        "CMAKE_RANLIB",
        "CMAKE_LINKER",
        "CMAKE_RC_COMPILER",
        "CMAKE_TRY_COMPILE_TARGET_TYPE",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(keys); i++) {
        if (!try_compile_append_cache_set_if_visible(ctx, sb, keys[i])) return false;
    }
    for (size_t i = 0; i < arena_arr_len(req->cmake_flags); i++) {
        if (!try_compile_cmake_flag_to_cache_set(ctx, req->cmake_flags[i], sb)) return false;
    }
    return true;
}

static Try_Compile_Mini_Target *try_compile_mini_find_target(Try_Compile_Mini_Project *project,
                                                             String_View name) {
    if (!project || name.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(project->items); i++) {
        if (nob_sv_eq(project->items[i].name, name)) return &project->items[i];
    }
    return NULL;
}

static Try_Compile_Mini_Target *try_compile_mini_get_or_add_target(EvalExecContext *ctx,
                                                                    Try_Compile_Mini_Project *project,
                                                                    String_View name) {
    if (!ctx || !project || name.count == 0) return NULL;
    Try_Compile_Mini_Target *target = try_compile_mini_find_target(project, name);
    if (target) return target;
    Try_Compile_Mini_Target item = {0};
    item.name = name;
    item.type = EV_TARGET_LIBRARY_UNKNOWN;
    if (!arena_arr_push(ctx->arena, project->items, item)) {
        (void)ctx_oom(ctx);
        return NULL;
    }
    return &project->items[arena_arr_len(project->items) - 1];
}

static bool try_compile_mini_push_sv(EvalExecContext *ctx, SV_List *list, String_View value) {
    if (!ctx || !list || value.count == 0) return true;
    return eval_sv_arr_push_temp(ctx, list, value);
}

static bool try_compile_mini_set_bool_prop(String_View value, bool *out) {
    if (!out) return false;
    if (eval_sv_eq_ci_lit(value, "ON") || eval_sv_eq_ci_lit(value, "TRUE") ||
        eval_sv_eq_ci_lit(value, "YES") || eval_sv_eq_ci_lit(value, "1")) {
        *out = true;
    } else {
        *out = false;
    }
    return true;
}

static bool try_compile_mini_apply_prop(EvalExecContext *ctx,
                                        Try_Compile_Mini_Target *target,
                                        const Event_Target_Prop_Set *prop) {
    if (!ctx || !target || !prop) return false;
    if (eval_sv_eq_ci_lit(prop->key, "SOURCES") && prop->op == EV_PROP_SET) {
        SV_List parts = NULL;
        if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), prop->value, &parts)) return false;
        for (size_t i = 0; i < arena_arr_len(parts); i++) {
            Try_Compile_Language lang = try_compile_detect_language(parts[i]);
            if (!try_compile_source_push(ctx,
                                         &target->sources,
                                         (Try_Compile_Source_Item){.path = parts[i], .language = lang})) {
                return false;
            }
        }
        return true;
    }
    if (eval_sv_eq_ci_lit(prop->key, "C_STANDARD")) {
        target->c_lang.has_value = true;
        target->c_lang.standard = prop->value;
    } else if (eval_sv_eq_ci_lit(prop->key, "C_STANDARD_REQUIRED")) {
        target->c_lang.has_value = true;
        target->c_lang.standard_required = eval_truthy(ctx, prop->value);
    } else if (eval_sv_eq_ci_lit(prop->key, "C_EXTENSIONS")) {
        target->c_lang.has_value = true;
        target->c_lang.extensions_set = true;
        (void)try_compile_mini_set_bool_prop(prop->value, &target->c_lang.extensions);
    } else if (eval_sv_eq_ci_lit(prop->key, "CXX_STANDARD")) {
        target->cxx_lang.has_value = true;
        target->cxx_lang.standard = prop->value;
    } else if (eval_sv_eq_ci_lit(prop->key, "CXX_STANDARD_REQUIRED")) {
        target->cxx_lang.has_value = true;
        target->cxx_lang.standard_required = eval_truthy(ctx, prop->value);
    } else if (eval_sv_eq_ci_lit(prop->key, "CXX_EXTENSIONS")) {
        target->cxx_lang.has_value = true;
        target->cxx_lang.extensions_set = true;
        (void)try_compile_mini_set_bool_prop(prop->value, &target->cxx_lang.extensions);
    }
    return true;
}

static bool try_compile_collect_mini_project(EvalExecContext *ctx,
                                             Event_Stream *stream,
                                             Try_Compile_Mini_Project *out_project,
                                             Nob_String_Builder *log) {
    if (!ctx || !stream || !out_project) return false;
    *out_project = (Try_Compile_Mini_Project){0};
    for (size_t i = 0; i < stream->count; i++) {
        const Event *ev = &stream->items[i];
        switch (ev->h.kind) {
            case EVENT_PROJECT_DECLARE:
                if (out_project->project_name.count == 0) out_project->project_name = ev->as.project_declare.name;
                break;
            case EVENT_DIAG:
                if (log && ev->as.diag.severity >= EVENT_DIAG_SEVERITY_ERROR) {
                    nob_sb_append_cstr(log, "mini configure diagnostic: ");
                    try_compile_sb_append_sv(log, ev->as.diag.cause);
                    if (ev->as.diag.hint.count > 0) {
                        nob_sb_append_cstr(log, " (");
                        try_compile_sb_append_sv(log, ev->as.diag.hint);
                        nob_sb_append(&log[0], ')');
                    }
                    nob_sb_append(&log[0], '\n');
                }
                break;
            case EVENT_TARGET_DECLARE: {
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_declare.name);
                if (!target) return false;
                target->type = ev->as.target_declare.target_type;
                target->imported = ev->as.target_declare.imported;
                target->alias = ev->as.target_declare.alias;
                target->alias_of = ev->as.target_declare.alias_of;
            } break;
            case EVENT_TARGET_ADD_SOURCE: {
                if (ev->as.target_add_source.visibility == EV_VISIBILITY_INTERFACE) break;
                if (ev->as.target_add_source.source_kind != EVENT_TARGET_SOURCE_REGULAR) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_add_source.target_name);
                if (!target) return false;
                Try_Compile_Language lang = try_compile_detect_language(ev->as.target_add_source.path);
                if (!try_compile_source_push(ctx,
                                             &target->sources,
                                             (Try_Compile_Source_Item){
                                                 .path = ev->as.target_add_source.path,
                                                 .language = lang,
                                             })) {
                    return false;
                }
            } break;
            case EVENT_TARGET_COMPILE_DEFINITIONS: {
                if (ev->as.target_compile_definitions.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_compile_definitions.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->compile_definitions, ev->as.target_compile_definitions.item)) return false;
            } break;
            case EVENT_TARGET_COMPILE_OPTIONS: {
                if (ev->as.target_compile_options.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_compile_options.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->compile_options, ev->as.target_compile_options.item)) return false;
            } break;
            case EVENT_TARGET_INCLUDE_DIRECTORIES: {
                if (ev->as.target_include_directories.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_include_directories.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->include_directories, ev->as.target_include_directories.path)) return false;
            } break;
            case EVENT_TARGET_LINK_OPTIONS: {
                if (ev->as.target_link_options.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_link_options.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->link_options, ev->as.target_link_options.item)) return false;
            } break;
            case EVENT_TARGET_LINK_DIRECTORIES: {
                if (ev->as.target_link_directories.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_link_directories.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->link_directories, ev->as.target_link_directories.path)) return false;
            } break;
            case EVENT_TARGET_LINK_LIBRARIES: {
                if (ev->as.target_link_libraries.visibility == EV_VISIBILITY_INTERFACE) break;
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_link_libraries.target_name);
                if (!target || !try_compile_mini_push_sv(ctx, &target->link_libraries, ev->as.target_link_libraries.item)) return false;
            } break;
            case EVENT_TARGET_PROP_SET: {
                Try_Compile_Mini_Target *target =
                    try_compile_mini_get_or_add_target(ctx, out_project, ev->as.target_prop_set.target_name);
                if (!target || !try_compile_mini_apply_prop(ctx, target, &ev->as.target_prop_set)) return false;
            } break;
            default:
                break;
        }
    }
    return true;
}

static bool try_compile_mini_target_is_buildable(const Try_Compile_Mini_Target *target) {
    return target &&
           !target->imported &&
           !target->alias &&
           (target->type == EV_TARGET_EXECUTABLE || target->type == EV_TARGET_LIBRARY_STATIC);
}

static Try_Compile_Mini_Target *try_compile_select_mini_target(Try_Compile_Mini_Project *project,
                                                               String_View requested,
                                                               String_View project_name,
                                                               Nob_String_Builder *log) {
    if (!project) return NULL;
    if (requested.count > 0) {
        Try_Compile_Mini_Target *target = try_compile_mini_find_target(project, requested);
        if (!target) {
            if (log) {
                nob_sb_append_cstr(log, "try_compile(PROJECT) target not found: ");
                try_compile_sb_append_sv(log, requested);
                nob_sb_append(&log[0], '\n');
            }
            return NULL;
        }
        if (target->alias && target->alias_of.count > 0) target = try_compile_mini_find_target(project, target->alias_of);
        return target;
    }

    if (project_name.count > 0) {
        Try_Compile_Mini_Target *target = try_compile_mini_find_target(project, project_name);
        if (try_compile_mini_target_is_buildable(target)) return target;
    }
    if (project->project_name.count > 0) {
        Try_Compile_Mini_Target *target = try_compile_mini_find_target(project, project->project_name);
        if (try_compile_mini_target_is_buildable(target)) return target;
    }

    Try_Compile_Mini_Target *selected = NULL;
    size_t buildable_count = 0;
    for (size_t i = 0; i < arena_arr_len(project->items); i++) {
        if (!try_compile_mini_target_is_buildable(&project->items[i])) continue;
        selected = &project->items[i];
        buildable_count++;
    }
    if (buildable_count == 1) return selected;
    if (log) {
        nob_sb_append_cstr(log, buildable_count == 0
            ? "try_compile(PROJECT) found no buildable executable/static-library target\n"
            : "try_compile(PROJECT) target selection is ambiguous; pass TARGET\n");
    }
    return NULL;
}

static bool try_compile_mini_artifact_push(EvalExecContext *ctx,
                                           Try_Compile_Target_Artifact **artifacts,
                                           String_View key,
                                           String_View value) {
    if (!ctx || !artifacts || key.count == 0 || value.count == 0) return true;
    Try_Compile_Target_Artifact item = {.key = key, .value = value};
    if (!arena_arr_push(ctx->arena, *artifacts, item)) return ctx_oom(ctx);
    return true;
}

static bool try_compile_build_mini_target(EvalExecContext *ctx,
                                          const Try_Compile_Request *base_req,
                                          Try_Compile_Mini_Project *project,
                                          Try_Compile_Mini_Target *target,
                                          Try_Compile_Target_Artifact **artifacts,
                                          Nob_String_Builder *log) {
    if (!ctx || !base_req || !project || !target || !artifacts) return false;
    if (target->built) return true;
    if (target->building) {
        if (log) {
            nob_sb_append_cstr(log, "try_compile(PROJECT) target dependency cycle at: ");
            try_compile_sb_append_sv(log, target->name);
            nob_sb_append(&log[0], '\n');
        }
        return true;
    }
    if (target->alias && target->alias_of.count > 0) {
        Try_Compile_Mini_Target *real = try_compile_mini_find_target(project, target->alias_of);
        return real ? try_compile_build_mini_target(ctx, base_req, project, real, artifacts, log) : true;
    }
    if (!try_compile_mini_target_is_buildable(target)) {
        if (log) {
            nob_sb_append_cstr(log, "try_compile(PROJECT) unsupported target type for: ");
            try_compile_sb_append_sv(log, target->name);
            nob_sb_append(&log[0], '\n');
        }
        return true;
    }

    target->building = true;
    for (size_t i = 0; i < arena_arr_len(target->link_libraries); i++) {
        Try_Compile_Mini_Target *dep = try_compile_mini_find_target(project, target->link_libraries[i]);
        if (!dep) continue;
        if (!try_compile_build_mini_target(ctx, base_req, project, dep, artifacts, log)) return false;
        if (dep->artifact_path.count > 0 &&
            !try_compile_mini_artifact_push(ctx, artifacts, dep->name, dep->artifact_path)) {
            return false;
        }
    }

    Try_Compile_Request build_req = {0};
    build_req.signature = TRY_COMPILE_SIGNATURE_SOURCE;
    build_req.result_var = base_req->result_var;
    build_req.binary_dir = base_req->binary_dir;
    bool synthetic_source_project =
        nob_sv_eq(base_req->project_name, nob_sv_from_cstr("NobifyTryCompile")) &&
        nob_sv_eq(base_req->target_name, nob_sv_from_cstr("cmk2nob_try_compile"));
    build_req.current_src_dir = synthetic_source_project
        ? base_req->current_src_dir
        : (base_req->source_dir.count > 0 ? base_req->source_dir : base_req->current_src_dir);
    build_req.current_bin_dir = base_req->binary_dir;
    build_req.source_items = target->sources;
    build_req.compile_definitions = target->compile_definitions;
    build_req.compile_options = target->compile_options;
    build_req.include_directories = target->include_directories;
    build_req.link_options = target->link_options;
    build_req.link_directories = target->link_directories;
    build_req.link_libraries = target->link_libraries;
    build_req.linker_language = base_req->linker_language;
    build_req.c_lang = target->c_lang.has_value ? target->c_lang : base_req->c_lang;
    build_req.cxx_lang = target->cxx_lang.has_value ? target->cxx_lang : base_req->cxx_lang;
    build_req.output_name = target->name;
    build_req.has_build_kind = true;
    build_req.build_kind = target->type == EV_TARGET_LIBRARY_STATIC
        ? TRY_COMPILE_BUILD_STATIC_LIBRARY
        : TRY_COMPILE_BUILD_EXECUTABLE;

    Try_Compile_Execution_Result build_res = {0};
    bool ok = try_compile_execute_source_request_direct(ctx,
                                                        &build_req,
                                                        *artifacts,
                                                        arena_arr_len(*artifacts),
                                                        &build_res);
    if (!ok) return false;
    if (build_res.output.count > 0 && log) try_compile_sb_append_sv(log, build_res.output);
    target->built = build_res.ok;
    target->building = false;
    target->artifact_path = build_res.artifact_path;
    return true;
}

static bool try_compile_configure_mini_project(EvalExecContext *ctx,
                                               const Try_Compile_Request *req,
                                               String_View cmake_lists,
                                               Event_Stream **out_stream,
                                               EvalRunResult *out_run,
                                               Arena **out_mini_arena,
                                               Nob_String_Builder *log) {
    if (!ctx || !req || !out_stream || !out_run || !out_mini_arena) return false;
    *out_stream = NULL;
    *out_run = (EvalRunResult){0};
    *out_mini_arena = NULL;

    String_View contents = {0};
    bool found = false;
    if (!eval_service_read_file(ctx, cmake_lists, &contents, &found)) return false;
    if (!found) {
        if (log) {
            nob_sb_append_cstr(log, "try_compile(PROJECT) source directory is missing CMakeLists.txt: ");
            try_compile_sb_append_sv(log, req->source_dir);
            nob_sb_append(&log[0], '\n');
        }
        return true;
    }

    Nob_String_Builder script = {0};
    if (!try_compile_build_mini_prelude(ctx, req, &script)) {
        nob_sb_free(script);
        return false;
    }
    try_compile_sb_append_sv(&script, contents);
    String_View script_sv = nob_sv_from_parts(script.items, script.count);
    Ast_Root ast = NULL;
    Arena *mini_arena = arena_create(1024 * 1024);
    Arena *mini_scratch = arena_create(256 * 1024);
    if (!mini_arena || !mini_scratch) {
        if (mini_arena) arena_destroy(mini_arena);
        if (mini_scratch) arena_destroy(mini_scratch);
        nob_sb_free(script);
        return ctx_oom(ctx);
    }
    bool parsed = try_compile_parse_script_in_arena(ctx, mini_arena, script_sv, &ast);
    if (!parsed) {
        arena_destroy(mini_scratch);
        arena_destroy(mini_arena);
        nob_sb_free(script);
        return false;
    }

    EvalSession_Config cfg = {0};
    cfg.persistent_arena = mini_arena;
    cfg.services = ctx->services;
    cfg.compat_profile = ctx->runtime_state.compat_profile;
    cfg.source_root = req->source_dir;
    cfg.binary_root = req->binary_dir;
    cfg.target.toolchain_file = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TOOLCHAIN_FILE"));
    cfg.target.system_name = ctx->toolchain.target_system_name;
    cfg.target.system_processor = ctx->toolchain.target_system_processor;
    cfg.target.system_version = ctx->toolchain.target_system_version;
    cfg.target.sysroot = ctx->toolchain.sysroot;
    cfg.target.c_compiler = ctx->toolchain.c.compiler;
    cfg.target.cxx_compiler = ctx->toolchain.cxx.compiler;
    cfg.target.c_compiler_id = ctx->toolchain.c.compiler_id;
    cfg.target.cxx_compiler_id = ctx->toolchain.cxx.compiler_id;
    cfg.target.c_compiler_target = ctx->toolchain.c.target_triple;
    cfg.target.cxx_compiler_target = ctx->toolchain.cxx.target_triple;
    cfg.target.archive_tool = ctx->toolchain.archive_tool;
    cfg.target.ranlib_tool = ctx->toolchain.ranlib_tool;
    cfg.target.link_tool = ctx->toolchain.link_tool;
    cfg.target.resource_compiler = ctx->toolchain.resource_compiler;

    EvalSession *session = eval_session_create(&cfg);
    if (!session) {
        arena_destroy(mini_scratch);
        arena_destroy(mini_arena);
        nob_sb_free(script);
        return false;
    }
    Event_Stream *stream = event_stream_create(mini_arena);
    if (!stream) {
        eval_session_destroy(session);
        arena_destroy(mini_scratch);
        arena_destroy(mini_arena);
        nob_sb_free(script);
        return false;
    }
    EvalExec_Request run_req = {0};
    run_req.scratch_arena = mini_scratch;
    run_req.source_dir = req->source_dir;
    run_req.binary_dir = req->binary_dir;
    run_req.list_file = arena_strndup(mini_arena, cmake_lists.data ? cmake_lists.data : "", cmake_lists.count);
    run_req.mode = EVAL_EXEC_MODE_PROJECT;
    run_req.stream = stream;
    if (!run_req.list_file) {
        eval_session_destroy(session);
        arena_destroy(mini_scratch);
        arena_destroy(mini_arena);
        nob_sb_free(script);
        return ctx_oom(ctx);
    }
    *out_run = eval_session_run(session, &run_req, ast);
    eval_session_destroy(session);
    arena_destroy(mini_scratch);
    nob_sb_free(script);
    *out_stream = stream;
    *out_mini_arena = mini_arena;
    return true;
}

static bool try_compile_execute_mini_project(EvalExecContext *ctx,
                                             const Try_Compile_Request *req,
                                             Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};
    Nob_String_Builder log = {0};

    String_View cmake_lists = eval_sv_path_join(eval_temp_arena(ctx),
                                                req->source_dir,
                                                nob_sv_from_cstr("CMakeLists.txt"));
    if (eval_should_stop(ctx)) return false;
    char *bindir_c = eval_sv_to_cstr_temp(ctx, req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
    (void)try_compile_mkdir_p_local(ctx, bindir_c);

    Event_Stream *stream = NULL;
    EvalRunResult run = {0};
    Arena *mini_arena = NULL;
    if (!try_compile_configure_mini_project(ctx, req, cmake_lists, &stream, &run, &mini_arena, &log)) {
        nob_sb_free(log);
        return false;
    }
    if (!stream) {
        out_res->ok = false;
        out_res->output = try_compile_finish_log(ctx, &log);
        if (mini_arena) arena_destroy(mini_arena);
        return true;
    }

    Try_Compile_Mini_Project project = {0};
    if (!try_compile_collect_mini_project(ctx, stream, &project, &log)) {
        if (mini_arena) arena_destroy(mini_arena);
        nob_sb_free(log);
        return false;
    }
    if (!eval_result_is_ok(run.result) || run.report.error_count > 0) {
        out_res->ok = false;
        out_res->output = try_compile_finish_log(ctx, &log);
        if (mini_arena) arena_destroy(mini_arena);
        return true;
    }

    Try_Compile_Mini_Target *target =
        try_compile_select_mini_target(&project, req->target_name, req->project_name, &log);
    if (!target) {
        out_res->ok = false;
        out_res->output = try_compile_finish_log(ctx, &log);
        if (mini_arena) arena_destroy(mini_arena);
        return true;
    }

    Try_Compile_Target_Artifact *artifacts = NULL;
    if (!try_compile_build_mini_target(ctx, req, &project, target, &artifacts, &log)) {
        if (mini_arena) arena_destroy(mini_arena);
        nob_sb_free(log);
        return false;
    }
    out_res->ok = target->built;
    out_res->artifact_path = target->built
        ? sv_copy_to_event_arena(ctx, target->artifact_path)
        : nob_sv_from_cstr("");
    if (eval_should_stop(ctx)) {
        if (mini_arena) arena_destroy(mini_arena);
        nob_sb_free(log);
        return false;
    }
    out_res->output = try_compile_finish_log(ctx, &log);
    if (mini_arena) arena_destroy(mini_arena);
    return true;
}

static bool try_compile_generate_source_project(EvalExecContext *ctx,
                                                const Try_Compile_Request *req,
                                                String_View *out_source_dir) {
    if (!ctx || !req || !out_source_dir) return false;
    *out_source_dir = eval_sv_path_join(eval_temp_arena(ctx),
                                        req->binary_dir,
                                        nob_sv_from_cstr("NobifyTryCompileSource"));
    char *source_dir_c = eval_sv_to_cstr_temp(ctx, *out_source_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, source_dir_c, false);
    (void)try_compile_mkdir_p_local(ctx, source_dir_c);

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "cmake_minimum_required(VERSION 3.28)\n");
    nob_sb_append_cstr(&sb, "project(NobifyTryCompile");
    bool any_c = false;
    bool any_cxx = false;
    for (size_t i = 0; i < req->source_items.count; i++) {
        Try_Compile_Language lang = req->source_items.items[i].language != TRY_COMPILE_LANG_AUTO
            ? req->source_items.items[i].language
            : try_compile_detect_language(req->source_items.items[i].path);
        any_c = any_c || lang == TRY_COMPILE_LANG_C;
        any_cxx = any_cxx || lang == TRY_COMPILE_LANG_CXX;
    }
    nob_sb_append_cstr(&sb, " LANGUAGES");
    if (any_c || !any_cxx) nob_sb_append_cstr(&sb, " C");
    if (any_cxx) nob_sb_append_cstr(&sb, " CXX");
    nob_sb_append_cstr(&sb, ")\n");

    String_View target_type = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_TARGET_TYPE"));
    bool static_library = eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY");
    nob_sb_append_cstr(&sb, static_library
        ? "add_library(cmk2nob_try_compile STATIC\n"
        : "add_executable(cmk2nob_try_compile\n");
    for (size_t i = 0; i < req->source_items.count; i++) {
        nob_sb_append_cstr(&sb, "  ");
        String_View path = req->source_items.items[i].path;
        if (!eval_sv_is_abs_path(path)) {
            path = eval_path_resolve_for_cmake_arg(ctx, path, req->current_src_dir, true);
            if (eval_should_stop(ctx)) {
                nob_sb_free(sb);
                return false;
            }
        }
        if (!eval_sv_is_abs_path(path)) {
            String_View cwd = eval_process_cwd_temp(ctx);
            if (cwd.count > 0) path = eval_sv_path_join(eval_temp_arena(ctx), cwd, path);
        }
        if (!try_compile_sb_append_bracket_arg(&sb, path)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append(&sb, '\n');
    }
    nob_sb_append_cstr(&sb, ")\n");

    if (arena_arr_len(req->compile_definitions) > 0) {
        nob_sb_append_cstr(&sb, "target_compile_definitions(cmk2nob_try_compile PRIVATE\n");
        for (size_t i = 0; i < arena_arr_len(req->compile_definitions); i++) {
            nob_sb_append_cstr(&sb, "  ");
            try_compile_sb_append_bracket_arg(&sb, req->compile_definitions[i]);
            nob_sb_append(&sb, '\n');
        }
        nob_sb_append_cstr(&sb, ")\n");
    }
    if (arena_arr_len(req->link_options) > 0) {
        nob_sb_append_cstr(&sb, "target_link_options(cmk2nob_try_compile PRIVATE\n");
        for (size_t i = 0; i < arena_arr_len(req->link_options); i++) {
            nob_sb_append_cstr(&sb, "  ");
            try_compile_sb_append_bracket_arg(&sb, req->link_options[i]);
            nob_sb_append(&sb, '\n');
        }
        nob_sb_append_cstr(&sb, ")\n");
    }
    if (arena_arr_len(req->link_libraries) > 0) {
        nob_sb_append_cstr(&sb, "target_link_libraries(cmk2nob_try_compile PRIVATE\n");
        for (size_t i = 0; i < arena_arr_len(req->link_libraries); i++) {
            nob_sb_append_cstr(&sb, "  ");
            try_compile_sb_append_bracket_arg(&sb, req->link_libraries[i]);
            nob_sb_append(&sb, '\n');
        }
        nob_sb_append_cstr(&sb, ")\n");
    }
    if (req->c_lang.has_value && req->c_lang.standard.count > 0) {
        nob_sb_append_cstr(&sb, "set_target_properties(cmk2nob_try_compile PROPERTIES C_STANDARD ");
        try_compile_sb_append_bracket_arg(&sb, req->c_lang.standard);
        nob_sb_append_cstr(&sb, ")\n");
    }
    if (req->cxx_lang.has_value && req->cxx_lang.standard.count > 0) {
        nob_sb_append_cstr(&sb, "set_target_properties(cmk2nob_try_compile PROPERTIES CXX_STANDARD ");
        try_compile_sb_append_bracket_arg(&sb, req->cxx_lang.standard);
        nob_sb_append_cstr(&sb, ")\n");
    }

    String_View cmake_lists = eval_sv_path_join(eval_temp_arena(ctx), *out_source_dir, nob_sv_from_cstr("CMakeLists.txt"));
    String_View contents = nob_sv_from_parts(sb.items, sb.count);
    bool ok = eval_service_write_file(ctx, cmake_lists, contents, false);
    nob_sb_free(sb);
    return ok;
}

bool try_compile_execute_source_request(EvalExecContext *ctx,
                                        const Try_Compile_Request *req,
                                        Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    String_View generated_source_dir = nob_sv_from_cstr("");
    if (!try_compile_generate_source_project(ctx, req, &generated_source_dir)) return false;

    Try_Compile_Request project_req = *req;
    project_req.signature = TRY_COMPILE_SIGNATURE_PROJECT;
    project_req.source_dir = generated_source_dir;
    project_req.project_name = nob_sv_from_cstr("NobifyTryCompile");
    project_req.target_name = nob_sv_from_cstr("cmk2nob_try_compile");
    project_req.output_name = nob_sv_from_cstr("cmk2nob_try_compile");
    return try_compile_execute_mini_project(ctx, &project_req, out_res);
}

bool try_compile_execute_project_request(EvalExecContext *ctx,
                                         const Node *node,
                                         const Try_Compile_Request *req,
                                         Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};
    (void)node;

    Nob_String_Builder log = {0};
    if (req->source_dir.count == 0 || req->source_dir.data == NULL) {
        nob_sb_append_cstr(&log, "try_compile(PROJECT) source directory is missing\n");
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->ok = false;
        return true;
    }

    String_View cmake_lists = eval_sv_path_join(eval_temp_arena(ctx),
                                                req->source_dir,
                                                nob_sv_from_cstr("CMakeLists.txt"));
    if (eval_should_stop(ctx)) return false;
    if (!try_compile_file_exists_sv(ctx, cmake_lists)) {
        nob_sb_append_cstr(&log, "try_compile(PROJECT) source directory is missing CMakeLists.txt: ");
        nob_sb_append_buf(&log, req->source_dir.data, req->source_dir.count);
        nob_sb_append(&log, '\n');
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->ok = false;
        return true;
    }
    return try_compile_execute_mini_project(ctx, req, out_res);
}

Eval_Result try_compile_execute_and_publish(EvalExecContext *ctx,
                                            const Node *node,
                                            const Try_Compile_Request *req) {
    if (!ctx || !node || !req || eval_should_stop(ctx)) return eval_result_fatal();
    Cmake_Event_Origin origin = eval_origin_from_node(ctx, node);

    Try_Compile_Execution_Result exec_res = {0};
    bool ok = req->signature == TRY_COMPILE_SIGNATURE_PROJECT
        ? try_compile_execute_project_request(ctx, node, req, &exec_res)
        : try_compile_execute_source_request(ctx, req, &exec_res);
    if (!ok) return eval_result_from_ctx(ctx);

    if (req->signature == TRY_COMPILE_SIGNATURE_SOURCE && req->copy_file_path.count > 0) {
        String_View target_type = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_TRY_COMPILE_TARGET_TYPE"));
        if (eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY")) {
            exec_res.ok = false;
            if (exec_res.output.count == 0) {
                exec_res.output = nob_sv_from_cstr("try_compile COPY_FILE is invalid when CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY");
            }
            if (req->copy_file_error_var.count > 0) {
                (void)eval_var_set_current(ctx, req->copy_file_error_var, nob_sv_from_cstr("COPY_FILE requires an executable artifact"));
            }
        } else if (exec_res.ok && exec_res.artifact_path.count > 0) {
            String_View dst = try_compile_resolve_in_dir(ctx, req->copy_file_path, req->current_bin_dir);
            String_View parent = svu_dirname(dst);
            char *parent_c = eval_sv_to_cstr_temp(ctx, parent);
            EVAL_OOM_RETURN_IF_NULL(ctx, parent_c, eval_result_fatal());
            (void)try_compile_mkdir_p_local(ctx, parent_c);
            bool copied = eval_service_copy_file(ctx, exec_res.artifact_path, dst);
            if (req->copy_file_error_var.count > 0) {
                (void)eval_var_set_current(ctx,
                                           req->copy_file_error_var,
                                           copied ? nob_sv_from_cstr("") : nob_sv_from_cstr("try_compile COPY_FILE failed"));
            }
            if (!copied) exec_res.ok = false;
        } else if (req->copy_file_error_var.count > 0) {
            (void)eval_var_set_current(ctx, req->copy_file_error_var, nob_sv_from_cstr("try_compile COPY_FILE failed"));
        }
    }

    String_View result = exec_res.ok ? nob_sv_from_cstr("TRUE") : nob_sv_from_cstr("FALSE");
    String_View output_text = exec_res.output.count > 0 ? exec_res.output : nob_sv_from_cstr("");
    if (!try_compile_publish_result(ctx, origin, req, result, output_text)) {
        return eval_result_from_ctx(ctx);
    }
    if (!try_compile_emit_probe_replay(ctx, origin, req, &exec_res)) {
        return eval_result_from_ctx(ctx);
    }

    if (req->log_description.count > 0 && !req->no_log) {
        Nob_String_Builder log = {0};
        nob_sb_append_buf(&log, req->log_description.data, req->log_description.count);
        nob_sb_append_cstr(&log, ": ");
        nob_sb_append_cstr(&log, exec_res.ok ? "success" : "failure");
        if (req->binary_dir.count > 0) {
            nob_sb_append_cstr(&log, " (");
            nob_sb_append_buf(&log, req->binary_dir.data, req->binary_dir.count);
            nob_sb_append(&log, ')');
        }
        (void)eval_append_configure_log(ctx, node, nob_sv_from_parts(log.items, log.count));
        nob_sb_free(log);
    }

    return eval_result_from_ctx(ctx);
}
