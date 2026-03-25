#include "eval_try_compile_internal.h"

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

static bool try_compile_append_required_compile_settings(EvalExecContext *ctx,
                                                         Nob_Cmd *cmd,
                                                         const Try_Compile_Request *req,
                                                         Try_Compile_Language lang,
                                                         bool msvc) {
    if (!ctx || !cmd || !req) return false;

    for (size_t i = 0; i < arena_arr_len(req->compile_definitions); i++) {
        if (!try_compile_append_define_arg(ctx, cmd, req->compile_definitions[i], msvc)) return false;
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

static bool try_compile_append_cmake_cache_arg(EvalExecContext *ctx,
                                               Nob_Cmd *cmd,
                                               const char *key,
                                               String_View value) {
    if (!ctx || !cmd || !key || key[0] == '\0' || value.count == 0) return false;
    String_View prefix = nob_sv_from_cstr(nob_temp_sprintf("-D%s=", key));
    String_View arg = try_compile_concat_prefix_temp(ctx, prefix.data, value);
    char *arg_c = eval_sv_to_cstr_temp(ctx, arg);
    EVAL_OOM_RETURN_IF_NULL(ctx, arg_c, false);
    nob_cmd_append(cmd, arg_c);
    return true;
}

static bool try_compile_path_contains_internal_build_dir(const char *path) {
    if (!path) return false;
    return strstr(path, "/CMakeFiles/") != NULL ||
           strstr(path, "\\CMakeFiles\\") != NULL ||
           strstr(path, "/CMakeScratch/") != NULL ||
           strstr(path, "\\CMakeScratch\\") != NULL;
}

static bool try_compile_basename_matches_named_artifact(const char *base, const char *target_name) {
    if (!base || !target_name) return false;
    size_t base_len = strlen(base);
    size_t target_len = strlen(target_name);

    if (base_len == target_len && memcmp(base, target_name, target_len) == 0) return true;
    if (base_len == target_len + 4 &&
        memcmp(base, target_name, target_len) == 0 &&
        strcmp(base + target_len, ".exe") == 0) {
        return true;
    }
    if (base_len == target_len + 4 &&
        memcmp(base, target_name, target_len) == 0 &&
        strcmp(base + target_len, ".dll") == 0) {
        return true;
    }
    if (base_len == target_len + 4 &&
        memcmp(base, target_name, target_len) == 0 &&
        strcmp(base + target_len, ".lib") == 0) {
        return true;
    }
    if (base_len == target_len + 6 &&
        memcmp(base, "lib", 3) == 0 &&
        memcmp(base + 3, target_name, target_len) == 0 &&
        strcmp(base + 3 + target_len, ".a") == 0) {
        return true;
    }
    if (base_len == target_len + 7 &&
        memcmp(base, "lib", 3) == 0 &&
        memcmp(base + 3, target_name, target_len) == 0 &&
        strcmp(base + 3 + target_len, ".so") == 0) {
        return true;
    }
    if (base_len == target_len + 10 &&
        memcmp(base, "lib", 3) == 0 &&
        memcmp(base + 3, target_name, target_len) == 0 &&
        strcmp(base + 3 + target_len, ".dylib") == 0) {
        return true;
    }
    return false;
}

typedef struct {
    const char *target_name;
    const char *best_path;
    size_t best_len;
} Try_Compile_Project_Artifact_Search;

typedef struct {
    EvalExecContext *ctx;
    const char *binary_dir_c;
    Try_Compile_Source_List *sources;
} Try_Compile_Project_Source_Search;

static bool try_compile_project_artifact_visit(Nob_Walk_Entry entry) {
    Try_Compile_Project_Artifact_Search *search = (Try_Compile_Project_Artifact_Search *)entry.data;
    if (!search || !entry.path) return false;
    if (entry.type != NOB_FILE_REGULAR) return true;
    if (try_compile_path_contains_internal_build_dir(entry.path)) return true;

    const char *base = strrchr(entry.path, '/');
    const char *alt = strrchr(entry.path, '\\');
    if (!base || (alt && alt > base)) base = alt;
    base = base ? base + 1 : entry.path;

    if (!try_compile_basename_matches_named_artifact(base, search->target_name)) return true;

    size_t path_len = strlen(entry.path);
    if (!search->best_path || path_len < search->best_len) {
        search->best_path = entry.path;
        search->best_len = path_len;
    }
    return true;
}

static String_View try_compile_project_find_named_artifact(EvalExecContext *ctx,
                                                           String_View binary_dir,
                                                           String_View target_name) {
    if (!ctx || binary_dir.count == 0 || target_name.count == 0) return nob_sv_from_cstr("");
    char *binary_dir_c = eval_sv_to_cstr_temp(ctx, binary_dir);
    char *target_name_c = eval_sv_to_cstr_temp(ctx, target_name);
    EVAL_OOM_RETURN_IF_NULL(ctx, binary_dir_c, nob_sv_from_cstr(""));
    EVAL_OOM_RETURN_IF_NULL(ctx, target_name_c, nob_sv_from_cstr(""));

    Try_Compile_Project_Artifact_Search search = {
        .target_name = target_name_c,
        .best_path = NULL,
        .best_len = 0,
    };
    if (!nob_walk_dir(binary_dir_c, try_compile_project_artifact_visit, .data = &search)) {
        return nob_sv_from_cstr("");
    }
    if (!search.best_path) return nob_sv_from_cstr("");
    return sv_copy_to_arena(eval_temp_arena(ctx), nob_sv_from_cstr(search.best_path));
}

static bool try_compile_path_has_dir_prefix(const char *path, const char *prefix) {
    if (!path || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) return false;
    if (strncmp(path, prefix, prefix_len) != 0) return false;
    return path[prefix_len] == '\0' || path[prefix_len] == '/' || path[prefix_len] == '\\';
}

static bool try_compile_project_source_visit(Nob_Walk_Entry entry) {
    Try_Compile_Project_Source_Search *search = (Try_Compile_Project_Source_Search *)entry.data;
    if (!search || !search->ctx || !search->sources || !entry.path) return false;

    if (search->binary_dir_c && try_compile_path_has_dir_prefix(entry.path, search->binary_dir_c)) {
        *entry.action = NOB_WALK_SKIP;
        return true;
    }
    if (try_compile_path_contains_internal_build_dir(entry.path)) {
        *entry.action = NOB_WALK_SKIP;
        return true;
    }
    if (entry.type != NOB_FILE_REGULAR) return true;

    String_View path = sv_copy_to_arena(eval_temp_arena(search->ctx), nob_sv_from_cstr(entry.path));
    if (eval_should_stop(search->ctx)) return false;

    Try_Compile_Language lang = try_compile_detect_language(path);
    if (lang == TRY_COMPILE_LANG_AUTO || lang == TRY_COMPILE_LANG_HEADERS) return true;
    return try_compile_source_push(search->ctx,
                                   search->sources,
                                   (Try_Compile_Source_Item){
                                       .path = path,
                                       .language = lang,
                                   });
}

static bool try_compile_execute_project_request_fallback(EvalExecContext *ctx,
                                                         const Try_Compile_Request *req,
                                                         Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;

    Try_Compile_Source_List sources = {0};
    char *source_dir_c = eval_sv_to_cstr_temp(ctx, req->source_dir);
    char *binary_dir_c = eval_sv_to_cstr_temp(ctx, req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, source_dir_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, binary_dir_c, false);

    Try_Compile_Project_Source_Search search = {
        .ctx = ctx,
        .binary_dir_c = binary_dir_c,
        .sources = &sources,
    };
    if (!nob_walk_dir(source_dir_c, try_compile_project_source_visit, .data = &search)) {
        return false;
    }

    if (sources.count == 0) {
        Nob_String_Builder log = {0};
        nob_sb_append_cstr(&log, "try_compile(PROJECT) found no compilable source files under: ");
        nob_sb_append_buf(&log, req->source_dir.data, req->source_dir.count);
        nob_sb_append(&log, '\n');
        out_res->ok = false;
        out_res->output = try_compile_finish_log(ctx, &log);
        out_res->artifact_path = nob_sv_from_cstr("");
        return true;
    }

    Try_Compile_Request source_req = *req;
    source_req.signature = TRY_COMPILE_SIGNATURE_SOURCE;
    source_req.source_items = sources;
    return try_compile_execute_source_request(ctx, &source_req, out_res);
}

bool try_compile_execute_source_request(EvalExecContext *ctx,
                                        const Try_Compile_Request *req,
                                        Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};

    Nob_String_Builder log = {0};
    SV_List object_paths = {0};
    size_t compile_units = 0;
    bool any_cxx = false;
    bool any_c = false;
    bool msvc = false;

    for (size_t i = 0; i < req->source_items.count; i++) {
        Try_Compile_Source_Item item = req->source_items.items[i];
        String_View src = item.path;
        if (!eval_sv_is_abs_path(src)) {
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
            nob_sb_append_buf(&log, src.data, src.count);
            nob_sb_append(&log, '\n');
            out_res->output = try_compile_finish_log(ctx, &log);
            return true;
        }

        compile_units++;
        any_cxx = any_cxx || (lang == TRY_COMPILE_LANG_CXX);
        any_c = any_c || (lang == TRY_COMPILE_LANG_C);

        String_View obj_name = sv_copy_to_arena(eval_temp_arena(ctx),
                                                nob_sv_from_cstr(nob_temp_sprintf("obj_%zu%s",
                                                                                 i,
                                                                                 msvc ? ".obj" : ".o")));
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
#if defined(_WIN32)
        msvc = eval_sv_eq_ci_lit(eval_var_get_visible(ctx, nob_sv_from_cstr("MSVC")), "1");
        if (msvc) {
            String_View fo_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                                  nob_sv_from_cstr(nob_temp_sprintf("/Fo:%s", obj_c)));
            nob_cmd_append(&cmd, "/nologo", "/c", src_c, fo_arg.data);
        } else
#endif
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
    Try_Compile_Build_Kind build_kind = req->signature == TRY_COMPILE_SIGNATURE_SOURCE
        ? (eval_sv_eq_ci_lit(target_type, "STATIC_LIBRARY")
            ? TRY_COMPILE_BUILD_STATIC_LIBRARY
            : TRY_COMPILE_BUILD_EXECUTABLE)
        : TRY_COMPILE_BUILD_EXECUTABLE;

    String_View output_path = nob_sv_from_cstr("");
    if (build_kind == TRY_COMPILE_BUILD_STATIC_LIBRARY) {
#if defined(_WIN32)
        if (!try_compile_materialize_output_path(ctx, req->binary_dir, nob_sv_from_cstr("cmk2nob_try_compile"), ".lib", &output_path)) {
            nob_sb_free(log);
            return false;
        }
        char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        Nob_Cmd cmd = {0};
        String_View out_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                               nob_sv_from_cstr(nob_temp_sprintf("/OUT:%s", out_c)));
        nob_cmd_append(&cmd, "lib.exe", "/NOLOGO", out_arg.data);
        for (size_t i = 0; i < arena_arr_len(object_paths); i++) {
            char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
            nob_cmd_append(&cmd, obj_c);
        }
#else
        if (!try_compile_materialize_output_path(ctx, req->binary_dir, nob_sv_from_cstr("libcmk2nob_try_compile"), ".a", &output_path)) {
            nob_sb_free(log);
            return false;
        }
        char *out_c = eval_sv_to_cstr_temp(ctx, output_path);
        EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "ar", "rcs", out_c);
        for (size_t i = 0; i < arena_arr_len(object_paths); i++) {
            char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths[i]);
            EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
            nob_cmd_append(&cmd, obj_c);
        }
#endif
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

#if defined(_WIN32)
    msvc = eval_sv_eq_ci_lit(eval_var_get_visible(ctx, nob_sv_from_cstr("MSVC")), "1");
#endif
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
                                             nob_sv_from_cstr("cmk2nob_try_compile"),
#if defined(_WIN32)
                                             ".exe",
#else
                                             "",
#endif
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
#if defined(_WIN32)
    if (msvc) {
        String_View fe_arg = sv_copy_to_arena(eval_temp_arena(ctx),
                                              nob_sv_from_cstr(nob_temp_sprintf("/Fe:%s", out_c)));
        nob_cmd_append(&link_cmd, "/nologo", fe_arg.data);
    } else
#endif
    {
        nob_cmd_append(&link_cmd, "-o", out_c);
    }
    for (size_t i = 0; i < arena_arr_len(object_paths); i++) {
        char *obj_c = eval_sv_to_cstr_temp(ctx, object_paths[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, obj_c, false);
        nob_cmd_append(&link_cmd, obj_c);
    }
    if (!try_compile_append_required_link_settings(ctx, &link_cmd, req, NULL, 0, msvc)) {
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

bool try_compile_execute_project_request(EvalExecContext *ctx,
                                         const Node *node,
                                         const Try_Compile_Request *req,
                                         Try_Compile_Execution_Result *out_res) {
    if (!ctx || !req || !out_res) return false;
    *out_res = (Try_Compile_Execution_Result){0};
    (void)node;

    Nob_String_Builder log = {0};
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

    char *bindir_c = eval_sv_to_cstr_temp(ctx, req->binary_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, bindir_c, false);
    (void)try_compile_mkdir_p_local(ctx, bindir_c);

    String_View cmake_cmd = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_COMMAND"));
    if (cmake_cmd.count == 0 || eval_sv_eq_ci_lit(cmake_cmd, "cmake")) {
        return try_compile_execute_project_request_fallback(ctx, req, out_res);
    }
    char *cmake_cmd_c = eval_sv_to_cstr_temp(ctx, cmake_cmd);
    char *source_dir_c = eval_sv_to_cstr_temp(ctx, req->source_dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, cmake_cmd_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, source_dir_c, false);

    Nob_Cmd configure_cmd = {0};
    nob_cmd_append(&configure_cmd, cmake_cmd_c, "-S", source_dir_c, "-B", bindir_c);

    String_View c_compiler = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER"));
    if (c_compiler.count > 0 &&
        !try_compile_append_cmake_cache_arg(ctx, &configure_cmd, "CMAKE_C_COMPILER", c_compiler)) {
        nob_cmd_free(configure_cmd);
        nob_sb_free(log);
        return false;
    }

    String_View cxx_compiler = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER"));
    if (cxx_compiler.count > 0 &&
        !try_compile_append_cmake_cache_arg(ctx, &configure_cmd, "CMAKE_CXX_COMPILER", cxx_compiler)) {
        nob_cmd_free(configure_cmd);
        nob_sb_free(log);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(req->cmake_flags); i++) {
        char *flag_c = eval_sv_to_cstr_temp(ctx, req->cmake_flags[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, flag_c, false);
        nob_cmd_append(&configure_cmd, flag_c);
    }

    bool configure_ok = false;
    if (!try_compile_run_command_captured(ctx, &configure_cmd, req->binary_dir, &log, &configure_ok)) {
        nob_cmd_free(configure_cmd);
        nob_sb_free(log);
        return false;
    }
    nob_cmd_free(configure_cmd);
    if (!configure_ok) {
        out_res->ok = false;
        out_res->output = try_compile_finish_log(ctx, &log);
        return true;
    }

    Nob_Cmd build_cmd = {0};
    nob_cmd_append(&build_cmd, cmake_cmd_c, "--build", bindir_c);
    if (req->target_name.count > 0) {
        char *target_c = eval_sv_to_cstr_temp(ctx, req->target_name);
        EVAL_OOM_RETURN_IF_NULL(ctx, target_c, false);
        nob_cmd_append(&build_cmd, "--target", target_c);
    }

    bool build_ok = false;
    if (!try_compile_run_command_captured(ctx, &build_cmd, req->binary_dir, &log, &build_ok)) {
        nob_cmd_free(build_cmd);
        nob_sb_free(log);
        return false;
    }
    nob_cmd_free(build_cmd);

    out_res->ok = build_ok;
    out_res->output = try_compile_finish_log(ctx, &log);
    if (!build_ok) {
        out_res->artifact_path = nob_sv_from_cstr("");
        return true;
    }

    if (req->target_name.count > 0) {
        out_res->artifact_path = try_compile_project_find_named_artifact(ctx,
                                                                         req->binary_dir,
                                                                         req->target_name);
    }
    return true;
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
            char *src_c = eval_sv_to_cstr_temp(ctx, exec_res.artifact_path);
            char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
            EVAL_OOM_RETURN_IF_NULL(ctx, parent_c, eval_result_fatal());
            EVAL_OOM_RETURN_IF_NULL(ctx, src_c, eval_result_fatal());
            EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, eval_result_fatal());
            (void)try_compile_mkdir_p_local(ctx, parent_c);
            bool copied = nob_copy_file(src_c, dst_c);
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

    String_View result = exec_res.ok ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
    String_View output_text = exec_res.output.count > 0 ? exec_res.output : nob_sv_from_cstr("");
    if (!try_compile_publish_result(ctx, origin, req, result, output_text)) {
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
