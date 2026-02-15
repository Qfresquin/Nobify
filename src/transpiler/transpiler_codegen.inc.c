// ============================================================================
// GERACAO DE CODIGO A PARTIR DO MODELO
// ============================================================================

static void append_sanitized_identifier(String_Builder *sb, String_View input) {
    if (input.count == 0) {
        sb_append_cstr(sb, "target");
        return;
    }

    char first = input.data[0];
    if (!(isalpha((unsigned char)first) || first == '_')) {
        sb_append(sb, '_');
    }

    for (size_t i = 0; i < input.count; i++) {
        unsigned char c = (unsigned char)input.data[i];
        if (isalnum(c) || c == '_') {
            sb_append(sb, (char)c);
        } else {
            sb_append(sb, '_');
        }
    }
}

// Gera codigo C para um target
static void sb_append_c_string_literal(String_Builder *sb, String_View s) {
    sb_append(sb, '"');
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        switch (c) {
            case '\\': sb_append_cstr(sb, "\\\\"); break;
            case '"':  sb_append_cstr(sb, "\\\""); break;
            case '\n': sb_append_cstr(sb, "\\n"); break;
            case '\r': sb_append_cstr(sb, "\\r"); break;
            case '\t': sb_append_cstr(sb, "\\t"); break;
            default:   sb_append(sb, (char)c); break;
        }
    }
    sb_append(sb, '"');
}

static bool sv_contains_char(String_View s, char ch) {
    for (size_t i = 0; i < s.count; i++) {
        if (s.data[i] == ch) return true;
    }
    return false;
}

static bool sv_contains_substr(String_View s, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || s.count < nlen) return false;
    for (size_t i = 0; i + nlen <= s.count; i++) {
        if (memcmp(s.data + i, needle, nlen) == 0) return true;
    }
    return false;
}

static bool sv_has_file_ext_ci(String_View path, const char *ext) {
    size_t ext_len = strlen(ext);
    if (path.count < ext_len) return false;
    String_View tail = nob_sv_from_parts(path.data + path.count - ext_len, ext_len);
    return sv_eq_ci(tail, sv_from_cstr(ext));
}

static bool source_should_compile(String_View src) {
    return sv_has_file_ext_ci(src, ".c")   ||
           sv_has_file_ext_ci(src, ".cc")  ||
           sv_has_file_ext_ci(src, ".cpp") ||
           sv_has_file_ext_ci(src, ".cxx") ||
           sv_has_file_ext_ci(src, ".m")   ||
           sv_has_file_ext_ci(src, ".mm")  ||
           sv_has_file_ext_ci(src, ".s")   ||
           sv_has_file_ext_ci(src, ".asm");
}

typedef struct {
    char *key;
    int value;
} Fast_String_Set_Entry;

typedef struct {
    Build_Model *model;
    Build_Config active_cfg;
} Codegen_Logic_Vars;

static bool fast_string_set_insert(Fast_String_Set_Entry **set, Arena *arena, String_View value) {
    if (!set || !arena || value.count == 0 || !value.data) return false;
    Fast_String_Set_Entry *map = *set;
    if (ds_shgetp_null(map, nob_temp_sv_to_cstr(value)) != NULL) return false;
    char *stable = arena_strndup(arena, value.data, value.count);
    if (!stable) return false;
    ds_shput(map, stable, 1);
    *set = map;
    return true;
}

static void string_list_add_unique_fast(String_List *list, Arena *arena, Fast_String_Set_Entry **set, String_View value) {
    if (!list || !arena) return;
    if (fast_string_set_insert(set, arena, value)) {
        string_list_add(list, arena, value);
    }
}

static void string_list_add_all_unique_fast(String_List *dst, Arena *arena, Fast_String_Set_Entry **set, const String_List *src) {
    if (!dst || !arena || !src) return;
    for (size_t i = 0; i < src->count; i++) {
        string_list_add_unique_fast(dst, arena, set, src->items[i]);
    }
}

static String_View codegen_active_config_name(Build_Config cfg) {
    switch (cfg) {
        case CONFIG_DEBUG: return sv_from_cstr("Debug");
        case CONFIG_RELEASE: return sv_from_cstr("Release");
        case CONFIG_RELWITHDEBINFO: return sv_from_cstr("RelWithDebInfo");
        case CONFIG_MINSIZEREL: return sv_from_cstr("MinSizeRel");
        default: return sv_from_cstr("Debug");
    }
}

static String_View codegen_logic_get_var(void *userdata, String_View name, bool *is_set) {
    if (is_set) *is_set = false;
    if (!userdata || name.count == 0) return sv_from_cstr("");
    Codegen_Logic_Vars *vars = (Codegen_Logic_Vars*)userdata;
    Build_Model *model = vars->model;
    if (!model) return sv_from_cstr("");

    if (nob_sv_eq(name, sv_from_cstr("CMAKE_BUILD_TYPE"))) {
        if (is_set) *is_set = true;
        String_View default_cfg = build_model_get_default_config(model);
        if (default_cfg.count > 0) return default_cfg;
        return codegen_active_config_name(vars->active_cfg);
    }
    if (nob_sv_eq(name, sv_from_cstr("WIN32"))) {
        if (is_set) *is_set = true;
        return build_model_is_windows(model) ? sv_from_cstr("1") : sv_from_cstr("0");
    }
    if (nob_sv_eq(name, sv_from_cstr("UNIX"))) {
        if (is_set) *is_set = true;
        return build_model_is_unix(model) ? sv_from_cstr("1") : sv_from_cstr("0");
    }
    if (nob_sv_eq(name, sv_from_cstr("APPLE"))) {
        if (is_set) *is_set = true;
        return build_model_is_apple(model) ? sv_from_cstr("1") : sv_from_cstr("0");
    }
    if (nob_sv_eq(name, sv_from_cstr("LINUX"))) {
        if (is_set) *is_set = true;
        return build_model_is_linux(model) ? sv_from_cstr("1") : sv_from_cstr("0");
    }
    if (nob_sv_eq(name, sv_from_cstr("CMAKE_SYSTEM_NAME"))) {
        if (is_set) *is_set = true;
        return build_model_get_system_name(model);
    }

    String_View cache_val = build_model_get_cache_variable(model, name);
    if (cache_val.count > 0 || build_model_has_cache_variable(model, name)) {
        if (is_set) *is_set = true;
        return cache_val;
    }
    String_View env_val = build_model_get_env_var(model, name);
    if (env_val.count > 0 || build_model_has_env_var(model, name)) {
        if (is_set) *is_set = true;
        return env_val;
    }
    return sv_from_cstr("");
}

static bool target_has_dependents(Build_Model *model, String_View target_name) {
    if (!model || target_name.count == 0) return false;
    size_t target_count = build_model_get_target_count(model);
    for (size_t i = 0; i < target_count; i++) {
        Build_Target *t = build_model_get_target_at(model, i);
        const String_List *deps = build_target_get_string_list(t, BUILD_TARGET_LIST_DEPENDENCIES);
        for (size_t j = 0; j < deps->count; j++) {
            if (nob_sv_eq(deps->items[j], target_name)) return true;
        }
        const String_List *iface_deps = build_target_get_string_list(t, BUILD_TARGET_LIST_INTERFACE_DEPENDENCIES);
        for (size_t j = 0; j < iface_deps->count; j++) {
            if (nob_sv_eq(iface_deps->items[j], target_name)) return true;
        }
    }
    return false;
}

static void sb_append_target_output_path_literal(String_Builder *sb, Build_Target *target, Build_Config active_cfg);
static Build_Target *resolve_alias_target(Build_Model *model, Build_Target *target);

static void generate_custom_commands(Build_Model *model, const Custom_Command *commands, size_t count, Build_Config active_cfg, String_Builder *sb) {
    for (size_t i = 0; i < count; i++) {
        const Custom_Command *cmd = &commands[i];
        if (cmd->command.count == 0) continue;

        String_Builder shell_builder = {0};
        if (cmd->working_dir.count > 0) {
            sb_append_cstr(&shell_builder, "cd ");
            sb_append(&shell_builder, '"');
            sb_append_buf(&shell_builder, cmd->working_dir.data, cmd->working_dir.count);
            sb_append(&shell_builder, '"');
            sb_append_cstr(&shell_builder, " && ");
        }
        sb_append_buf(&shell_builder, cmd->command.data, cmd->command.count);
        String_View shell_cmd = sb_to_sv(shell_builder);

        if (cmd->comment.count > 0) {
            sb_append_cstr(sb, "    nob_log(NOB_INFO, ");
            sb_append_c_string_literal(sb, cmd->comment);
            sb_append_cstr(sb, ");\n");
        }
        sb_append_cstr(sb, "    {\n");
        bool has_outputs = cmd->outputs.count > 0 || cmd->byproducts.count > 0;
        if (has_outputs) {
            sb_append_cstr(sb, "        bool run_custom = false;\n");
            for (size_t j = 0; j < cmd->outputs.count; j++) {
                sb_append_cstr(sb, "        if (!nob_file_exists(");
                sb_append_c_string_literal(sb, cmd->outputs.items[j]);
                sb_append_cstr(sb, ")) run_custom = true;\n");
            }
            for (size_t j = 0; j < cmd->byproducts.count; j++) {
                sb_append_cstr(sb, "        if (!nob_file_exists(");
                sb_append_c_string_literal(sb, cmd->byproducts.items[j]);
                sb_append_cstr(sb, ")) run_custom = true;\n");
            }
            if (cmd->depfile.count > 0) {
                sb_append_cstr(sb, "        if (!nob_file_exists(");
                sb_append_c_string_literal(sb, cmd->depfile);
                sb_append_cstr(sb, ")) run_custom = true;\n");
            }
            if (cmd->depends.count > 0 && (cmd->outputs.count > 0 || cmd->byproducts.count > 0)) {
                const String_List *rebuild_outputs = cmd->outputs.count > 0 ? &cmd->outputs : &cmd->byproducts;
                sb_appendf(sb, "        const char *deps_custom_%zu[] = {", i);
                for (size_t j = 0; j < cmd->depends.count; j++) {
                    if (j > 0) sb_append_cstr(sb, ", ");
                    Build_Target *dep_target = build_model_find_target(model, cmd->depends.items[j]);
                    dep_target = resolve_alias_target(model, dep_target);
                    if (dep_target) {
                        sb_append_target_output_path_literal(sb, dep_target, active_cfg);
                    } else {
                        sb_append_c_string_literal(sb, cmd->depends.items[j]);
                    }
                }
                sb_append_cstr(sb, "};\n");
                for (size_t j = 0; j < rebuild_outputs->count; j++) {
                    sb_append_cstr(sb, "        if (!run_custom) {\n");
                    sb_append_cstr(sb, "            int nr = nob_needs_rebuild(");
                    sb_append_c_string_literal(sb, rebuild_outputs->items[j]);
                    sb_appendf(sb, ", deps_custom_%zu, %zu);\n", i, cmd->depends.count);
                    sb_append_cstr(sb, "            if (nr < 0) return 1;\n");
                    sb_append_cstr(sb, "            if (nr > 0) run_custom = true;\n");
                    sb_append_cstr(sb, "        }\n");
                }
            }
            sb_append_cstr(sb, "        if (run_custom) {\n");
        }
        sb_append_cstr(sb, "        Nob_Cmd custom_cmd = {0};\n");
        sb_append_cstr(sb, "        const char *custom_shell = ");
        sb_append_c_string_literal(sb, shell_cmd);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "        #if defined(_WIN32)\n");
        sb_append_cstr(sb, "        nob_cmd_append(&custom_cmd, \"cmd\", \"/C\", custom_shell);\n");
        sb_append_cstr(sb, "        #else\n");
        sb_append_cstr(sb, "        nob_cmd_append(&custom_cmd, \"sh\", \"-c\", custom_shell);\n");
        sb_append_cstr(sb, "        #endif\n");
        sb_append_cstr(sb, "        if (!nob_cmd_run_sync(custom_cmd)) return 1;\n");
        if (has_outputs) {
            sb_append_cstr(sb, "        }\n");
        }
        sb_append_cstr(sb, "    }\n");

        nob_sb_free(shell_builder);
    }
}

static void split_install_entry(String_View entry, String_View default_destination, String_View *item, String_View *destination) {
    for (size_t i = 0; i < entry.count; i++) {
        if (entry.data[i] == '\t') {
            *item = nob_sv_from_parts(entry.data, i);
            *destination = nob_sv_from_parts(entry.data + i + 1, entry.count - (i + 1));
            if (destination->count == 0) *destination = default_destination;
            return;
        }
    }
    *item = entry;
    *destination = default_destination;
}

static String_View source_property_internal_key_temp(String_View source, const char *prop_name) {
    return sv_from_cstr(nob_temp_sprintf("__SRC_PROP__"SV_Fmt"__%s", SV_Arg(source), prop_name));
}

static String_View sv_trim_spaces(String_View sv) {
    size_t begin = 0;
    size_t end = sv.count;
    while (begin < end && isspace((unsigned char)sv.data[begin])) begin++;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;
    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static void split_source_property_values(String_View value, bool split_whitespace, Arena *arena, String_List *out) {
    if (!arena || !out || value.count == 0) return;

    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool is_sep = (i == value.count) || value.data[i] == ';' ||
                      (split_whitespace && i < value.count && isspace((unsigned char)value.data[i]));
        if (!is_sep) continue;
        if (i > start) {
            String_View item = sv_trim_spaces(nob_sv_from_parts(value.data + start, i - start));
            if (item.count > 0) string_list_add_unique(out, arena, item);
        }
        start = i + 1;
    }
}

static Build_Target *resolve_alias_target(Build_Model *model, Build_Target *target) {
    Build_Target *current = target;
    for (int depth = 0; current && build_target_get_type(current) == TARGET_ALIAS && depth < 16; depth++) {
        const String_List *deps = build_target_get_string_list(current, BUILD_TARGET_LIST_DEPENDENCIES);
        if (deps->count == 0) break;
        current = build_model_find_target(model, deps->items[0]);
    }
    return current;
}

static bool target_is_linkable_artifact(Build_Target *target) {
    if (!target) return false;
    Target_Type type = build_target_get_type(target);
    return type == TARGET_STATIC_LIB || type == TARGET_SHARED_LIB || type == TARGET_IMPORTED;
}

static String_View target_property_for_config(Build_Target *target, Build_Config cfg, const char *base_key, String_View fallback) {
    if (!target) return fallback;
    String_View suffix = build_model_config_suffix(cfg);
    if (suffix.count > 0) {
        String_View cfg_key = sv_from_cstr(nob_temp_sprintf("%s_%s", base_key, nob_temp_sv_to_cstr(suffix)));
        String_View cfg_val = build_target_get_property(target, cfg_key);
        if (cfg_val.count > 0) return cfg_val;
    }
    String_View base = build_target_get_property(target, sv_from_cstr(base_key));
    if (base.count > 0) return base;
    return fallback;
}

static void collect_interface_usage_recursive(
    Build_Model *model,
    Build_Target *target,
    uint8_t *visited,
    Fast_String_Set_Entry **compile_defs_set,
    Fast_String_Set_Entry **compile_opts_set,
    Fast_String_Set_Entry **include_dirs_set,
    Fast_String_Set_Entry **link_opts_set,
    Fast_String_Set_Entry **link_dirs_set,
    Fast_String_Set_Entry **link_libs_set,
    Fast_String_Set_Entry **link_targets_set,
    String_List *compile_defs,
    String_List *compile_opts,
    String_List *include_dirs,
    String_List *link_opts,
    String_List *link_dirs,
    String_List *link_libs,
    String_List *link_targets
) {
    if (!model || !target) return;
    Arena *arena = build_model_get_arena(model);
    if (!arena) return;
    Build_Target *base = resolve_alias_target(model, target);
    if (!base) return;

    int idx = build_model_find_target_index(model, build_target_get_name(base));
    if (idx >= 0) {
        if (visited[idx]) return;
        visited[idx] = 1;
    }

    const String_List *base_iface_defs = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_COMPILE_DEFINITIONS);
    for (size_t i = 0; i < base_iface_defs->count; i++) {
        string_list_add_unique_fast(compile_defs, arena, compile_defs_set, base_iface_defs->items[i]);
    }
    const String_List *base_iface_opts = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_COMPILE_OPTIONS);
    for (size_t i = 0; i < base_iface_opts->count; i++) {
        string_list_add_unique_fast(compile_opts, arena, compile_opts_set, base_iface_opts->items[i]);
    }
    const String_List *base_iface_includes = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_INCLUDE_DIRECTORIES);
    for (size_t i = 0; i < base_iface_includes->count; i++) {
        string_list_add_unique_fast(include_dirs, arena, include_dirs_set, base_iface_includes->items[i]);
    }
    const String_List *base_iface_link_opts = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_LINK_OPTIONS);
    for (size_t i = 0; i < base_iface_link_opts->count; i++) {
        string_list_add_unique_fast(link_opts, arena, link_opts_set, base_iface_link_opts->items[i]);
    }
    const String_List *base_iface_link_dirs = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_LINK_DIRECTORIES);
    for (size_t i = 0; i < base_iface_link_dirs->count; i++) {
        string_list_add_unique_fast(link_dirs, arena, link_dirs_set, base_iface_link_dirs->items[i]);
    }
    const String_List *base_iface_libs = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_LIBS);
    for (size_t i = 0; i < base_iface_libs->count; i++) {
        string_list_add_unique_fast(link_libs, arena, link_libs_set, base_iface_libs->items[i]);
    }

    const String_List *base_iface_deps = build_target_get_string_list(base, BUILD_TARGET_LIST_INTERFACE_DEPENDENCIES);
    for (size_t i = 0; i < base_iface_deps->count; i++) {
        String_View dep_name = base_iface_deps->items[i];
        Build_Target *dep = build_model_find_target(model, dep_name);
        dep = resolve_alias_target(model, dep);
        if (!dep) continue;

        if (target_is_linkable_artifact(dep)) {
            string_list_add_unique_fast(link_targets, arena, link_targets_set, build_target_get_name(dep));
        }
        collect_interface_usage_recursive(
            model, dep, visited,
            compile_defs_set, compile_opts_set, include_dirs_set,
            link_opts_set, link_dirs_set, link_libs_set, link_targets_set,
            compile_defs, compile_opts, include_dirs,
            link_opts, link_dirs, link_libs, link_targets
        );
    }
}

static String_View codegen_copy_builder_to_model_arena(Build_Model *model, const String_Builder *sb) {
    if (!model || !sb) return sv_from_cstr("");
    Arena *arena = build_model_get_arena(model);
    if (!arena) return sv_from_cstr("");
    return sv_from_cstr(arena_strndup(arena, sb->items ? sb->items : "", sb->count));
}

static void generate_output_custom_commands(Build_Model *model, String_Builder *sb) {
    size_t output_custom_count = 0;
    const Custom_Command *output_commands = build_model_get_output_custom_commands(model, &output_custom_count);
    if (!model || output_custom_count == 0 || !output_commands) return;

    Build_Config active_cfg = build_model_config_from_string(build_model_get_default_config(model));
    sb_appendf(sb, "    // Custom commands: OUTPUT (%zu)\n", output_custom_count);

    for (size_t i = 0; i < output_custom_count; i++) {
        const Custom_Command *cmd = &output_commands[i];
        if (cmd->command.count == 0 || cmd->outputs.count == 0) continue;

        if (cmd->comment.count > 0) {
            sb_append_cstr(sb, "    nob_log(NOB_INFO, ");
            sb_append_c_string_literal(sb, cmd->comment);
            sb_append_cstr(sb, ");\n");
        }

        sb_appendf(sb, "    {\n");
        sb_appendf(sb, "        bool run_custom = false;\n");

        for (size_t j = 0; j < cmd->outputs.count; j++) {
            sb_append_cstr(sb, "        if (!nob_file_exists(");
            sb_append_c_string_literal(sb, cmd->outputs.items[j]);
            sb_append_cstr(sb, ")) run_custom = true;\n");
        }
        for (size_t j = 0; j < cmd->byproducts.count; j++) {
            sb_append_cstr(sb, "        if (!nob_file_exists(");
            sb_append_c_string_literal(sb, cmd->byproducts.items[j]);
            sb_append_cstr(sb, ")) run_custom = true;\n");
        }

        if (cmd->depends.count > 0) {
            sb_appendf(sb, "        const char *deps_%zu[] = {", i);
            for (size_t j = 0; j < cmd->depends.count; j++) {
                if (j > 0) sb_append_cstr(sb, ", ");
                Build_Target *dep_target = build_model_find_target(model, cmd->depends.items[j]);
                dep_target = resolve_alias_target(model, dep_target);
                if (dep_target) {
                    sb_append_target_output_path_literal(sb, dep_target, active_cfg);
                } else {
                    sb_append_c_string_literal(sb, cmd->depends.items[j]);
                }
            }
            sb_append_cstr(sb, "};\n");

            for (size_t j = 0; j < cmd->outputs.count; j++) {
                sb_append_cstr(sb, "        if (!run_custom) {\n");
                sb_append_cstr(sb, "            int nr = nob_needs_rebuild(");
                sb_append_c_string_literal(sb, cmd->outputs.items[j]);
                sb_appendf(sb, ", deps_%zu, %zu);\n", i, cmd->depends.count);
                sb_append_cstr(sb, "            if (nr < 0) return 1;\n");
                sb_append_cstr(sb, "            if (nr > 0) run_custom = true;\n");
                sb_append_cstr(sb, "        }\n");
            }
        }

        sb_append_cstr(sb, "        if (run_custom) {\n");
        String_Builder shell_builder = {0};
        if (cmd->working_dir.count > 0) {
            sb_append_cstr(&shell_builder, "cd ");
            sb_append(&shell_builder, '"');
            sb_append_buf(&shell_builder, cmd->working_dir.data, cmd->working_dir.count);
            sb_append(&shell_builder, '"');
            sb_append_cstr(&shell_builder, " && ");
        }
        sb_append_buf(&shell_builder, cmd->command.data, cmd->command.count);
        String_View shell_cmd = sb_to_sv(shell_builder);

        sb_append_cstr(sb, "            Nob_Cmd custom_cmd = {0};\n");
        sb_append_cstr(sb, "            const char *custom_shell = ");
        sb_append_c_string_literal(sb, shell_cmd);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            #if defined(_WIN32)\n");
        sb_append_cstr(sb, "            nob_cmd_append(&custom_cmd, \"cmd\", \"/C\", custom_shell);\n");
        sb_append_cstr(sb, "            #else\n");
        sb_append_cstr(sb, "            nob_cmd_append(&custom_cmd, \"sh\", \"-c\", custom_shell);\n");
        sb_append_cstr(sb, "            #endif\n");
        sb_append_cstr(sb, "            if (!nob_cmd_run_sync(custom_cmd)) return 1;\n");
        sb_append_cstr(sb, "        }\n");
        sb_append_cstr(sb, "    }\n");
        nob_sb_free(shell_builder);
    }
    sb_append_cstr(sb, "\n");
}

static void sb_append_target_output_path_literal(String_Builder *sb, Build_Target *target, Build_Config active_cfg) {
    Target_Type target_type = build_target_get_type(target);
    String_View default_cfg = codegen_active_config_name(active_cfg);
    if (target_type == TARGET_IMPORTED) {
        String_View imported_location = target_property_for_config(target, active_cfg, "IMPORTED_LOCATION", sv_from_cstr(""));
        if (imported_location.count > 0) {
            sb_append_c_string_literal(sb, imported_location);
            return;
        }
        sb_append_c_string_literal(sb, sv_from_cstr(""));
        return;
    }
    if (target_type == TARGET_INTERFACE_LIB || target_type == TARGET_ALIAS || target_type == TARGET_OBJECT_LIB) {
        sb_append_c_string_literal(sb, sv_from_cstr(""));
        return;
    }

    String_View target_name = build_target_get_name(target);
    String_View artifact_name = build_target_get_property_computed(target, sv_from_cstr("OUTPUT_NAME"), default_cfg);
    if (artifact_name.count == 0) artifact_name = target_name;
    String_View out_dir = build_target_get_property_computed(target, sv_from_cstr("OUTPUT_DIRECTORY"), default_cfg);
    if (out_dir.count == 0) out_dir = sv_from_cstr("build");
    if (target_type == TARGET_EXECUTABLE) {
        String_View runtime_dir = build_target_get_property_computed(target, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"), default_cfg);
        if (runtime_dir.count > 0) out_dir = runtime_dir;
    } else if (target_type == TARGET_STATIC_LIB) {
        String_View archive_dir = build_target_get_property_computed(target, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"), default_cfg);
        if (archive_dir.count > 0) out_dir = archive_dir;
    }
    String_View prefix = build_target_get_property_computed(target, sv_from_cstr("PREFIX"), default_cfg);
    String_View suffix = build_target_get_property_computed(target, sv_from_cstr("SUFFIX"), default_cfg);

    String_Builder path = {0};
    sb_append_buf(&path, out_dir.data, out_dir.count);
    sb_append(&path, '/');
    sb_append_buf(&path, prefix.data, prefix.count);
    sb_append_buf(&path, artifact_name.data, artifact_name.count);
    sb_append_buf(&path, suffix.data, suffix.count);
    sb_append_c_string_literal(sb, sb_to_sv(path));
    nob_sb_free(path);
}

static String_View cpack_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    size_t component_count = build_model_get_cpack_component_count(model);
    size_t component_group_count = build_model_get_cpack_component_group_count(model);
    size_t install_type_count = build_model_get_cpack_install_type_count(model);
    if (component_count == 0 &&
        component_group_count == 0 &&
        install_type_count == 0) {
        return sv_from_cstr("");
    }

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack component manifest\n");
    for (size_t i = 0; i < install_type_count; i++) {
        CPack_Install_Type *it = build_model_get_cpack_install_type_at(model, i);
        sb_append_cstr(&manifest, "install_type:");
        String_View install_type_name = build_cpack_install_type_get_name(it);
        sb_append_buf(&manifest, install_type_name.data, install_type_name.count);
        sb_append_cstr(&manifest, "|display=");
        String_View install_type_display_name = build_cpack_install_type_get_display_name(it);
        sb_append_buf(&manifest, install_type_display_name.data, install_type_display_name.count);
        sb_append(&manifest, '\n');
    }
    for (size_t i = 0; i < component_group_count; i++) {
        CPack_Component_Group *g = build_model_get_cpack_component_group_at(model, i);
        sb_append_cstr(&manifest, "group:");
        String_View group_name = build_cpack_group_get_name(g);
        sb_append_buf(&manifest, group_name.data, group_name.count);
        sb_append_cstr(&manifest, "|display=");
        String_View group_display_name = build_cpack_group_get_display_name(g);
        sb_append_buf(&manifest, group_display_name.data, group_display_name.count);
        sb_append_cstr(&manifest, "|description=");
        String_View group_description = build_cpack_group_get_description(g);
        sb_append_buf(&manifest, group_description.data, group_description.count);
        sb_append_cstr(&manifest, "|parent=");
        String_View parent_group = build_cpack_group_get_parent_group(g);
        sb_append_buf(&manifest, parent_group.data, parent_group.count);
        sb_append_cstr(&manifest, "|expanded=");
        sb_append_cstr(&manifest, build_cpack_group_get_expanded(g) ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|bold=");
        sb_append_cstr(&manifest, build_cpack_group_get_bold_title(g) ? "ON" : "OFF");
        sb_append(&manifest, '\n');
    }
    for (size_t i = 0; i < component_count; i++) {
        CPack_Component *c = build_model_get_cpack_component_at(model, i);
        sb_append_cstr(&manifest, "component:");
        String_View component_name = build_cpack_component_get_name(c);
        sb_append_buf(&manifest, component_name.data, component_name.count);
        sb_append_cstr(&manifest, "|display=");
        String_View component_display_name = build_cpack_component_get_display_name(c);
        sb_append_buf(&manifest, component_display_name.data, component_display_name.count);
        sb_append_cstr(&manifest, "|description=");
        String_View component_description = build_cpack_component_get_description(c);
        sb_append_buf(&manifest, component_description.data, component_description.count);
        sb_append_cstr(&manifest, "|group=");
        String_View component_group = build_cpack_component_get_group(c);
        sb_append_buf(&manifest, component_group.data, component_group.count);
        sb_append_cstr(&manifest, "|required=");
        sb_append_cstr(&manifest, build_cpack_component_get_required(c) ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|hidden=");
        sb_append_cstr(&manifest, build_cpack_component_get_hidden(c) ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|disabled=");
        sb_append_cstr(&manifest, build_cpack_component_get_disabled(c) ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|downloaded=");
        sb_append_cstr(&manifest, build_cpack_component_get_downloaded(c) ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|depends=");
        const String_List *depends = build_cpack_component_get_depends(c);
        for (size_t d = 0; d < depends->count; d++) {
            if (d > 0) sb_append(&manifest, ';');
            sb_append_buf(&manifest, depends->items[d].data, depends->items[d].count);
        }
        sb_append_cstr(&manifest, "|install_types=");
        const String_List *install_types = build_cpack_component_get_install_types(c);
        for (size_t t = 0; t < install_types->count; t++) {
            if (t > 0) sb_append(&manifest, ';');
            sb_append_buf(&manifest, install_types->items[t].data, install_types->items[t].count);
        }
        sb_append(&manifest, '\n');
    }

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static bool cpack_codegen_string_is_false(String_View value) {
    if (value.count == 0) return true;
    return sv_eq_ci(value, sv_from_cstr("0")) ||
           sv_eq_ci(value, sv_from_cstr("OFF")) ||
           sv_eq_ci(value, sv_from_cstr("FALSE")) ||
           sv_eq_ci(value, sv_from_cstr("NO")) ||
           sv_eq_ci(value, sv_from_cstr("N")) ||
           sv_eq_ci(value, sv_from_cstr("IGNORE")) ||
           sv_eq_ci(value, sv_from_cstr("NOTFOUND")) ||
           sv_eq_ci(value, sv_from_cstr("-NOTFOUND"));
}

static String_View cpack_archive_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View archive_enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_ARCHIVE_ENABLED"));
    if (cpack_codegen_string_is_false(archive_enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_FILE_NAME"));
    String_View components = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_COMPONENTS"));
    String_View depends = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_DEPENDS"));
    String_View archive_gens = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_ARCHIVE_GENERATORS"));
    String_View archive_ext = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_ARCHIVE_EXT"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (file_name.count == 0) file_name = sv_from_cstr(nob_temp_sprintf("%s-%s", nob_temp_sv_to_cstr(name), nob_temp_sv_to_cstr(version)));
    if (archive_ext.count == 0) archive_ext = sv_from_cstr(".tar.gz");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack archive manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append_cstr(&manifest, "\narchive_generators=");
    sb_append_buf(&manifest, archive_gens.data, archive_gens.count);
    sb_append_cstr(&manifest, "\narchive_extension=");
    sb_append_buf(&manifest, archive_ext.data, archive_ext.count);
    sb_append_cstr(&manifest, "\ncomponents=");
    sb_append_buf(&manifest, components.data, components.count);
    sb_append_cstr(&manifest, "\ndepends=");
    sb_append_buf(&manifest, depends.data, depends.count);
    sb_append(&manifest, '\n');

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_deb_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DEB_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("CPACK_DEBIAN_PACKAGE_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("CPACK_DEBIAN_PACKAGE_VERSION"));
    String_View release = build_model_get_cache_variable(model, sv_from_cstr("CPACK_DEBIAN_PACKAGE_RELEASE"));
    String_View arch = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DEB_ARCH"));
    String_View depends = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DEB_DEPENDS"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DEB_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (release.count == 0) release = sv_from_cstr("1");
    if (arch.count == 0) arch = sv_from_cstr("amd64");
    if (file_name.count == 0) file_name = sv_from_cstr("package_0.1.0-1_amd64.deb");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack DEB manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nrelease=");
    sb_append_buf(&manifest, release.data, release.count);
    sb_append_cstr(&manifest, "\narch=");
    sb_append_buf(&manifest, arch.data, arch.count);
    sb_append_cstr(&manifest, "\ndepends=");
    sb_append_buf(&manifest, depends.data, depends.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_rpm_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("CPACK_RPM_PACKAGE_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("CPACK_RPM_PACKAGE_VERSION"));
    String_View release = build_model_get_cache_variable(model, sv_from_cstr("CPACK_RPM_PACKAGE_RELEASE"));
    String_View arch = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_ARCH"));
    String_View license = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_LICENSE"));
    String_View requires = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_REQUIRES"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (release.count == 0) release = sv_from_cstr("1");
    if (arch.count == 0) arch = sv_from_cstr("x86_64");
    if (license.count == 0) license = sv_from_cstr("unknown");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0-1.x86_64.rpm");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack RPM manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nrelease=");
    sb_append_buf(&manifest, release.data, release.count);
    sb_append_cstr(&manifest, "\narch=");
    sb_append_buf(&manifest, arch.data, arch.count);
    sb_append_cstr(&manifest, "\nlicense=");
    sb_append_buf(&manifest, license.data, license.count);
    sb_append_cstr(&manifest, "\nrequires=");
    sb_append_buf(&manifest, requires.data, requires.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_nsis_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View display_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_DISPLAY_NAME"));
    String_View install_dir = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_INSTALL_DIR"));
    String_View contact = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_CONTACT"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (display_name.count == 0) display_name = sv_from_cstr("Package 0.1.0");
    if (install_dir.count == 0) install_dir = sv_from_cstr("Package");
    if (contact.count == 0) contact = sv_from_cstr("unknown");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0-setup.exe");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack NSIS manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\ndisplay_name=");
    sb_append_buf(&manifest, display_name.data, display_name.count);
    sb_append_cstr(&manifest, "\ninstall_directory=");
    sb_append_buf(&manifest, install_dir.data, install_dir.count);
    sb_append_cstr(&manifest, "\ncontact=");
    sb_append_buf(&manifest, contact.data, contact.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_wix_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View product_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_PRODUCT_NAME"));
    String_View arch = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_ARCH"));
    String_View cultures = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_CULTURES"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (product_name.count == 0) product_name = name;
    if (arch.count == 0) arch = sv_from_cstr("x64");
    if (cultures.count == 0) cultures = sv_from_cstr("en-us");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.msi");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack WIX manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nproduct_name=");
    sb_append_buf(&manifest, product_name.data, product_name.count);
    sb_append_cstr(&manifest, "\narchitecture=");
    sb_append_buf(&manifest, arch.data, arch.count);
    sb_append_cstr(&manifest, "\ncultures=");
    sb_append_buf(&manifest, cultures.data, cultures.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');

    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_dmg_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DMG_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View volume = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DMG_VOLUME_NAME"));
    String_View format = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DMG_FORMAT"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DMG_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (volume.count == 0) volume = name;
    if (format.count == 0) format = sv_from_cstr("UDZO");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.dmg");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack DMG manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nvolume_name=");
    sb_append_buf(&manifest, volume.data, volume.count);
    sb_append_cstr(&manifest, "\nformat=");
    sb_append_buf(&manifest, format.data, format.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_bundle_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_NAME"));
    String_View plist = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_PLIST"));
    String_View icon = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_ICON"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (file_name.count == 0) file_name = sv_from_cstr("package.app");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack Bundle manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nplist=");
    sb_append_buf(&manifest, plist.data, plist.count);
    sb_append_cstr(&manifest, "\nicon=");
    sb_append_buf(&manifest, icon.data, icon.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_productbuild_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_PRODUCTBUILD_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View identifier = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_PRODUCTBUILD_IDENTIFIER"));
    String_View identity = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_PRODUCTBUILD_IDENTITY"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_PRODUCTBUILD_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (identifier.count == 0) identifier = sv_from_cstr("com.cmk2nob.package");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.pkg");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack ProductBuild manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nidentifier=");
    sb_append_buf(&manifest, identifier.data, identifier.count);
    sb_append_cstr(&manifest, "\nidentity=");
    sb_append_buf(&manifest, identity.data, identity.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_ifw_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_IFW_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_IFW_PACKAGE_NAME"));
    String_View title = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_IFW_PACKAGE_TITLE"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_VERSION"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_IFW_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("Package");
    if (title.count == 0) title = name;
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.ifw");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack IFW manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\ntitle=");
    sb_append_buf(&manifest, title.data, title.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_nuget_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View id = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_ID"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_VERSION"));
    String_View authors = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_AUTHORS"));
    String_View description = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_DESCRIPTION"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_FILE_NAME"));
    if (id.count == 0) id = sv_from_cstr("Package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (authors.count == 0) authors = sv_from_cstr("unknown");
    if (description.count == 0) description = id;
    if (file_name.count == 0) file_name = sv_from_cstr("Package.0.1.0.nupkg");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack NuGet manifest\n");
    sb_append_cstr(&manifest, "id=");
    sb_append_buf(&manifest, id.data, id.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\nauthors=");
    sb_append_buf(&manifest, authors.data, authors.count);
    sb_append_cstr(&manifest, "\ndescription=");
    sb_append_buf(&manifest, description.data, description.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_freebsd_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_VERSION"));
    String_View origin = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_ORIGIN"));
    String_View depends = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_DEPENDS"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (origin.count == 0) origin = sv_from_cstr("devel/package");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.pkg.txz");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack FreeBSD manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\norigin=");
    sb_append_buf(&manifest, origin.data, origin.count);
    sb_append_cstr(&manifest, "\ndepends=");
    sb_append_buf(&manifest, depends.data, depends.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static String_View cpack_cygwin_manifest_text(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    String_View enabled = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_ENABLED"));
    if (cpack_codegen_string_is_false(enabled)) return sv_from_cstr("");

    String_View name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_NAME"));
    String_View version = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_VERSION"));
    String_View depends = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_DEPENDS"));
    String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_FILE_NAME"));
    if (name.count == 0) name = sv_from_cstr("package");
    if (version.count == 0) version = sv_from_cstr("0.1.0");
    if (file_name.count == 0) file_name = sv_from_cstr("package-0.1.0.tar.xz");

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack Cygwin manifest\n");
    sb_append_cstr(&manifest, "name=");
    sb_append_buf(&manifest, name.data, name.count);
    sb_append_cstr(&manifest, "\nversion=");
    sb_append_buf(&manifest, version.data, version.count);
    sb_append_cstr(&manifest, "\ndepends=");
    sb_append_buf(&manifest, depends.data, depends.count);
    sb_append_cstr(&manifest, "\nfile_name=");
    sb_append_buf(&manifest, file_name.data, file_name.count);
    sb_append(&manifest, '\n');
    return codegen_copy_builder_to_model_arena(model, &manifest);
}

static void generate_install_code(Build_Model *model, String_Builder *sb) {
    if (!model || !sb) return;
    const String_List *install_targets = build_model_get_install_rule_list(model, INSTALL_RULE_TARGET);
    const String_List *install_files = build_model_get_install_rule_list(model, INSTALL_RULE_FILE);
    const String_List *install_programs = build_model_get_install_rule_list(model, INSTALL_RULE_PROGRAM);
    const String_List *install_directories = build_model_get_install_rule_list(model, INSTALL_RULE_DIRECTORY);
    size_t total_rules = install_targets->count + install_files->count +
                         install_directories->count + install_programs->count;
    String_View cpack_manifest = cpack_manifest_text(model);
    String_View cpack_archive_manifest = cpack_archive_manifest_text(model);
    String_View cpack_deb_manifest = cpack_deb_manifest_text(model);
    String_View cpack_rpm_manifest = cpack_rpm_manifest_text(model);
    String_View cpack_nsis_manifest = cpack_nsis_manifest_text(model);
    String_View cpack_wix_manifest = cpack_wix_manifest_text(model);
    String_View cpack_dmg_manifest = cpack_dmg_manifest_text(model);
    String_View cpack_bundle_manifest = cpack_bundle_manifest_text(model);
    String_View cpack_productbuild_manifest = cpack_productbuild_manifest_text(model);
    String_View cpack_ifw_manifest = cpack_ifw_manifest_text(model);
    String_View cpack_nuget_manifest = cpack_nuget_manifest_text(model);
    String_View cpack_freebsd_manifest = cpack_freebsd_manifest_text(model);
    String_View cpack_cygwin_manifest = cpack_cygwin_manifest_text(model);
    if (total_rules == 0 && cpack_manifest.count == 0 && cpack_archive_manifest.count == 0 &&
        cpack_deb_manifest.count == 0 && cpack_rpm_manifest.count == 0 &&
        cpack_nsis_manifest.count == 0 && cpack_wix_manifest.count == 0 &&
        cpack_dmg_manifest.count == 0 && cpack_bundle_manifest.count == 0 &&
        cpack_productbuild_manifest.count == 0 && cpack_ifw_manifest.count == 0 &&
        cpack_nuget_manifest.count == 0 && cpack_freebsd_manifest.count == 0 &&
        cpack_cygwin_manifest.count == 0) return;
    Build_Config active_cfg = build_model_config_from_string(build_model_get_default_config(model));

    String_View default_dest = build_model_has_install_prefix(model) ? build_model_get_install_prefix(model) : sv_from_cstr("install");
    sb_append_cstr(sb, "    if (argc > 1 && strcmp(argv[1], \"install\") == 0) {\n");

    for (size_t i = 0; i < install_targets->count; i++) {
        String_View target_name = {0}, destination = {0};
        split_install_entry(install_targets->items[i], default_dest, &target_name, &destination);
        Build_Target *target = build_model_find_target(model, target_name);
        if (!target) continue;
        Target_Type target_type = build_target_get_type(target);
        if (target_type == TARGET_INTERFACE_LIB || target_type == TARGET_ALIAS || target_type == TARGET_OBJECT_LIB) continue;
        if (target_type == TARGET_IMPORTED &&
            target_property_for_config(target, active_cfg, "IMPORTED_LOCATION", sv_from_cstr("")).count == 0) continue;

        sb_append_cstr(sb, "        if (!nob_mkdir_if_not_exists(");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ")) return 1;\n");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *src = ");
        sb_append_target_output_path_literal(sb, target, active_cfg);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *dst = nob_temp_sprintf(\"%s/%s\", ");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ", nob_path_name(src));\n");
        sb_append_cstr(sb, "            if (!nob_copy_file(src, dst)) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }

    for (size_t i = 0; i < install_files->count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(install_files->items[i], default_dest, &src, &destination);
        sb_append_cstr(sb, "        if (!nob_mkdir_if_not_exists(");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ")) return 1;\n");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *src = ");
        sb_append_c_string_literal(sb, src);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *dst = nob_temp_sprintf(\"%s/%s\", ");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ", nob_path_name(src));\n");
        sb_append_cstr(sb, "            if (!nob_copy_file(src, dst)) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }

    for (size_t i = 0; i < install_programs->count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(install_programs->items[i], default_dest, &src, &destination);
        sb_append_cstr(sb, "        if (!nob_mkdir_if_not_exists(");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ")) return 1;\n");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *src = ");
        sb_append_c_string_literal(sb, src);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *dst = nob_temp_sprintf(\"%s/%s\", ");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ", nob_path_name(src));\n");
        sb_append_cstr(sb, "            if (!nob_copy_file(src, dst)) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }

    for (size_t i = 0; i < install_directories->count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(install_directories->items[i], default_dest, &src, &destination);
        sb_append_cstr(sb, "        if (!nob_mkdir_if_not_exists(");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ")) return 1;\n");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *src = ");
        sb_append_c_string_literal(sb, src);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *dst = nob_temp_sprintf(\"%s/%s\", ");
        sb_append_c_string_literal(sb, destination);
        sb_append_cstr(sb, ", nob_path_name(src));\n");
        sb_append_cstr(sb, "            if (!nob_copy_directory_recursively(src, dst)) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }

    if (cpack_manifest.count > 0) {
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_components_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *manifest_text = ");
        sb_append_c_string_literal(sb, cpack_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(manifest_path, manifest_text, strlen(manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_archive_manifest.count > 0) {
        String_View archive_file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_META_FILE_NAME"));
        String_View archive_ext = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_ARCHIVE_EXT"));
        if (archive_file_name.count == 0) archive_file_name = sv_from_cstr("package");
        if (archive_ext.count == 0) archive_ext = sv_from_cstr(".tar.gz");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *archive_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_archive_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *archive_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_archive_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(archive_manifest_path, archive_manifest_text, strlen(archive_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *archive_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s%s",
            nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(archive_file_name), nob_temp_sv_to_cstr(archive_ext))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(archive_out, archive_manifest_text, strlen(archive_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_deb_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DEB_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.deb");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *deb_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_deb_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *deb_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_deb_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(deb_manifest_path, deb_manifest_text, strlen(deb_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *deb_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(deb_out, deb_manifest_text, strlen(deb_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_rpm_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_RPM_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.rpm");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *rpm_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_rpm_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *rpm_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_rpm_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(rpm_manifest_path, rpm_manifest_text, strlen(rpm_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *rpm_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(rpm_out, rpm_manifest_text, strlen(rpm_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_nsis_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NSIS_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.exe");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *nsis_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_nsis_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *nsis_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_nsis_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(nsis_manifest_path, nsis_manifest_text, strlen(nsis_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *nsis_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(nsis_out, nsis_manifest_text, strlen(nsis_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_wix_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_WIX_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.msi");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *wix_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_wix_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *wix_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_wix_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(wix_manifest_path, wix_manifest_text, strlen(wix_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *wix_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(wix_out, wix_manifest_text, strlen(wix_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_dmg_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_DMG_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.dmg");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *dmg_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_dmg_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *dmg_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_dmg_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(dmg_manifest_path, dmg_manifest_text, strlen(dmg_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *dmg_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(dmg_out, dmg_manifest_text, strlen(dmg_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_bundle_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_BUNDLE_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.app");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *bundle_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_bundle_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *bundle_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_bundle_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(bundle_manifest_path, bundle_manifest_text, strlen(bundle_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *bundle_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(bundle_out, bundle_manifest_text, strlen(bundle_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_productbuild_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_PRODUCTBUILD_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.pkg");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *productbuild_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_productbuild_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *productbuild_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_productbuild_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(productbuild_manifest_path, productbuild_manifest_text, strlen(productbuild_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *productbuild_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(productbuild_out, productbuild_manifest_text, strlen(productbuild_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_ifw_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_IFW_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.ifw");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *ifw_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_ifw_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *ifw_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_ifw_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(ifw_manifest_path, ifw_manifest_text, strlen(ifw_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *ifw_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(ifw_out, ifw_manifest_text, strlen(ifw_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_nuget_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_NUGET_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("Package.0.1.0.nupkg");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *nuget_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_nuget_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *nuget_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_nuget_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(nuget_manifest_path, nuget_manifest_text, strlen(nuget_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *nuget_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(nuget_out, nuget_manifest_text, strlen(nuget_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_freebsd_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_FREEBSD_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.pkg.txz");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *freebsd_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_freebsd_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *freebsd_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_freebsd_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(freebsd_manifest_path, freebsd_manifest_text, strlen(freebsd_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *freebsd_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(freebsd_out, freebsd_manifest_text, strlen(freebsd_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }
    if (cpack_cygwin_manifest.count > 0) {
        String_View file_name = build_model_get_cache_variable(model, sv_from_cstr("__CPACK_CYGWIN_FILE_NAME"));
        if (file_name.count == 0) file_name = sv_from_cstr("package.tar.xz");
        sb_append_cstr(sb, "        {\n");
        sb_append_cstr(sb, "            const char *cygwin_manifest_path = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/cpack_cygwin_manifest.txt", nob_temp_sv_to_cstr(default_dest))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            const char *cygwin_manifest_text = ");
        sb_append_c_string_literal(sb, cpack_cygwin_manifest);
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(cygwin_manifest_path, cygwin_manifest_text, strlen(cygwin_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "            const char *cygwin_out = ");
        sb_append_c_string_literal(sb, sv_from_cstr(nob_temp_sprintf("%s/%s", nob_temp_sv_to_cstr(default_dest), nob_temp_sv_to_cstr(file_name))));
        sb_append_cstr(sb, ";\n");
        sb_append_cstr(sb, "            if (!nob_write_entire_file(cygwin_out, cygwin_manifest_text, strlen(cygwin_manifest_text))) return 1;\n");
        sb_append_cstr(sb, "        }\n");
    }

    sb_append_cstr(sb, "        return 0;\n");
    sb_append_cstr(sb, "    }\n\n");
}
static void generate_target_code(Build_Model *model, Build_Target *target, String_Builder *sb) {
    if (!model || !target || !sb) return;
    Arena *arena = build_model_get_arena(model);
    if (!arena) return;
    String_View target_name = build_target_get_name(target);
    Target_Type target_type = build_target_get_type(target);

    String_Builder ident_builder = {0};
    append_sanitized_identifier(&ident_builder, target_name);
    String_View ident = sb_to_sv(ident_builder);

    sb_appendf(sb, "    // --- Target: "SV_Fmt" ---\n", SV_Arg(target_name));
    if (target_type == TARGET_UTILITY && build_target_is_exclude_from_all(target) && !target_has_dependents(model, target_name)) {
        sb_appendf(sb, "    // Skipping EXCLUDE_FROM_ALL utility target: "SV_Fmt"\n\n", SV_Arg(target_name));
        nob_sb_free(ident_builder);
        return;
    }
    sb_appendf(sb, "    Nob_Cmd cmd_"SV_Fmt" = {0};\n", SV_Arg(ident));
    sb_appendf(sb, "    Nob_File_Paths objs_"SV_Fmt" = {0};\n\n", SV_Arg(ident));
    Build_Config active_cfg = build_model_config_from_string(build_model_get_default_config(model));

    size_t pre_build_count = 0;
    const Custom_Command *pre_build_commands = build_target_get_custom_commands(target, true, &pre_build_count);
    if (pre_build_count > 0 && pre_build_commands) {
        sb_appendf(sb, "    // Custom commands: PRE_BUILD (%zu)\n", pre_build_count);
        generate_custom_commands(model, pre_build_commands, pre_build_count, active_cfg, sb);
        sb_appendf(sb, "\n");
    }

    bool can_compile_sources =
        target_type == TARGET_EXECUTABLE ||
        target_type == TARGET_STATIC_LIB ||
        target_type == TARGET_SHARED_LIB ||
        target_type == TARGET_OBJECT_LIB;
    String_List all_compile_defs = {0};
    String_List all_compile_opts = {0};
    String_List all_include_dirs = {0};
    String_List all_link_opts = {0};
    String_List all_link_dirs = {0};
    String_List all_link_libs = {0};
    String_List all_link_targets = {0};
    string_list_init(&all_compile_defs);
    string_list_init(&all_compile_opts);
    string_list_init(&all_include_dirs);
    string_list_init(&all_link_opts);
    string_list_init(&all_link_dirs);
    string_list_init(&all_link_libs);
    string_list_init(&all_link_targets);
    Fast_String_Set_Entry *all_compile_defs_set = NULL;
    Fast_String_Set_Entry *all_compile_opts_set = NULL;
    Fast_String_Set_Entry *all_include_dirs_set = NULL;
    Fast_String_Set_Entry *all_link_opts_set = NULL;
    Fast_String_Set_Entry *all_link_dirs_set = NULL;
    Fast_String_Set_Entry *all_link_libs_set = NULL;
    Fast_String_Set_Entry *all_link_targets_set = NULL;

    Codegen_Logic_Vars logic_vars = {
        .model = model,
        .active_cfg = active_cfg,
    };
    Logic_Eval_Context logic_ctx = {
        .get_var = codegen_logic_get_var,
        .userdata = &logic_vars,
    };
    const String_List *global_defs = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_DEFINITIONS);
    const String_List *global_compile_opts = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_COMPILE_OPTIONS);
    const String_List *global_link_opts = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_LINK_OPTIONS);
    const String_List *global_link_libs = build_model_get_string_list(model, BUILD_MODEL_LIST_GLOBAL_LINK_LIBRARIES);
    const String_List *global_include_dirs = build_model_get_string_list(model, BUILD_MODEL_LIST_INCLUDE_DIRS);
    const String_List *global_system_include_dirs = build_model_get_string_list(model, BUILD_MODEL_LIST_SYSTEM_INCLUDE_DIRS);
    const String_List *global_link_dirs = build_model_get_string_list(model, BUILD_MODEL_LIST_LINK_DIRS);
    const String_List *target_dependencies = build_target_get_string_list(target, BUILD_TARGET_LIST_DEPENDENCIES);
    const String_List *target_object_dependencies = build_target_get_string_list(target, BUILD_TARGET_LIST_OBJECT_DEPENDENCIES);
    const String_List *target_sources = build_target_get_string_list(target, BUILD_TARGET_LIST_SOURCES);

    String_List effective_compile_defs = {0};
    string_list_init(&effective_compile_defs);
    build_target_collect_effective_compile_definitions(target, arena, &logic_ctx, &effective_compile_defs);
    string_list_add_all_unique_fast(&all_compile_defs, arena, &all_compile_defs_set, &effective_compile_defs);
    for (size_t i = 0; i < global_defs->count; i++) {
        string_list_add_unique_fast(&all_compile_defs, arena, &all_compile_defs_set, global_defs->items[i]);
    }

    String_List effective_compile_opts = {0};
    string_list_init(&effective_compile_opts);
    build_target_collect_effective_compile_options(target, arena, &logic_ctx, &effective_compile_opts);
    string_list_add_all_unique_fast(&all_compile_opts, arena, &all_compile_opts_set, &effective_compile_opts);
    for (size_t i = 0; i < global_compile_opts->count; i++) {
        string_list_add_unique_fast(&all_compile_opts, arena, &all_compile_opts_set, global_compile_opts->items[i]);
    }

    String_List effective_include_dirs = {0};
    string_list_init(&effective_include_dirs);
    build_target_collect_effective_include_directories(target, arena, &logic_ctx, &effective_include_dirs);
    string_list_add_all_unique_fast(&all_include_dirs, arena, &all_include_dirs_set, &effective_include_dirs);
    for (size_t i = 0; i < global_include_dirs->count; i++) {
        string_list_add_unique_fast(&all_include_dirs, arena, &all_include_dirs_set, global_include_dirs->items[i]);
    }
    for (size_t i = 0; i < global_system_include_dirs->count; i++) {
        string_list_add_unique_fast(&all_include_dirs, arena, &all_include_dirs_set, global_system_include_dirs->items[i]);
    }
    String_List effective_link_opts = {0};
    string_list_init(&effective_link_opts);
    build_target_collect_effective_link_options(target, arena, &logic_ctx, &effective_link_opts);
    string_list_add_all_unique_fast(&all_link_opts, arena, &all_link_opts_set, &effective_link_opts);
    for (size_t i = 0; i < global_link_opts->count; i++) {
        string_list_add_unique_fast(&all_link_opts, arena, &all_link_opts_set, global_link_opts->items[i]);
    }
    String_List effective_link_dirs = {0};
    string_list_init(&effective_link_dirs);
    build_target_collect_effective_link_directories(target, arena, &logic_ctx, &effective_link_dirs);
    string_list_add_all_unique_fast(&all_link_dirs, arena, &all_link_dirs_set, &effective_link_dirs);
    for (size_t i = 0; i < global_link_dirs->count; i++) {
        string_list_add_unique_fast(&all_link_dirs, arena, &all_link_dirs_set, global_link_dirs->items[i]);
    }

    String_List effective_link_libs = {0};
    string_list_init(&effective_link_libs);
    build_target_collect_effective_link_libraries(target, arena, &logic_ctx, &effective_link_libs);
    string_list_add_all_unique_fast(&all_link_libs, arena, &all_link_libs_set, &effective_link_libs);
    for (size_t i = 0; i < global_link_libs->count; i++) {
        string_list_add_unique_fast(&all_link_libs, arena, &all_link_libs_set, global_link_libs->items[i]);
    }

    size_t target_count = build_model_get_target_count(model);
    uint8_t *visited = target_count > 0 ? arena_alloc_zero(arena, target_count * sizeof(uint8_t)) : NULL;
    if (target_count > 0 && !visited) {
        ds_shfree(all_compile_defs_set);
        ds_shfree(all_compile_opts_set);
        ds_shfree(all_include_dirs_set);
        ds_shfree(all_link_opts_set);
        ds_shfree(all_link_dirs_set);
        ds_shfree(all_link_libs_set);
        ds_shfree(all_link_targets_set);
        nob_sb_free(ident_builder);
        return;
    }
    for (size_t i = 0; i < target_dependencies->count; i++) {
        Build_Target *dep = build_model_find_target(model, target_dependencies->items[i]);
        dep = resolve_alias_target(model, dep);
        if (!dep) continue;
        if (target_is_linkable_artifact(dep)) {
            string_list_add_unique_fast(&all_link_targets, arena, &all_link_targets_set, build_target_get_name(dep));
        }
        collect_interface_usage_recursive(
            model, dep, visited,
            &all_compile_defs_set, &all_compile_opts_set, &all_include_dirs_set,
            &all_link_opts_set, &all_link_dirs_set, &all_link_libs_set, &all_link_targets_set,
            &all_compile_defs, &all_compile_opts, &all_include_dirs,
            &all_link_opts, &all_link_dirs, &all_link_libs, &all_link_targets
        );
    }
    
    // Compilar fontes
    if (can_compile_sources) {
        sb_appendf(sb, "    // Compilar fontes\n");
        for (size_t i = 0; i < target_sources->count; i++) {
            String_View src = target_sources->items[i];
            if (!source_should_compile(src)) continue;
            sb_appendf(sb, "    {\n");
            sb_appendf(sb, "        Nob_Cmd cc_cmd = {0};\n");
            sb_append_cstr(sb, "        #if defined(_MSC_VER) && !defined(__clang__)\n");
            sb_appendf(sb, "        const char *obj = \"build/"SV_Fmt"_%zu.obj\";\n", SV_Arg(ident), i);
            sb_append_cstr(sb, "        #else\n");
            sb_appendf(sb, "        const char *obj = \"build/"SV_Fmt"_%zu.o\";\n", SV_Arg(ident), i);
            sb_append_cstr(sb, "        #endif\n");
            sb_appendf(sb, "        nob_cc(&cc_cmd);\n");
            sb_append_cstr(sb, "        #if defined(_MSC_VER) && !defined(__clang__)\n");
            sb_append_cstr(sb, "        nob_cmd_append(&cc_cmd, \"/nologo\", \"/c\", ");
            sb_append_c_string_literal(sb, src);
            sb_append_cstr(sb, ", nob_temp_sprintf(\"/Fo:%s\", obj));\n");
            sb_append_cstr(sb, "        #else\n");
            sb_append_cstr(sb, "        nob_cmd_append(&cc_cmd, \"-c\", ");
            sb_append_c_string_literal(sb, src);
            sb_append_cstr(sb, ", \"-o\", obj);\n");
            sb_append_cstr(sb, "        #endif\n");

            for (size_t j = 0; j < all_compile_defs.count; j++) {
                String_View def = all_compile_defs.items[j];
                sb_append_cstr(sb, "        #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"/D"SV_Fmt"\");\n", SV_Arg(def));
                sb_append_cstr(sb, "        #else\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"-D"SV_Fmt"\");\n", SV_Arg(def));
                sb_append_cstr(sb, "        #endif\n");
            }

            String_List source_compile_defs = {0};
            string_list_init(&source_compile_defs);
            String_View src_defs_value = build_target_get_property(target, source_property_internal_key_temp(src, "COMPILE_DEFINITIONS"));
            split_source_property_values(src_defs_value, false, arena, &source_compile_defs);
            for (size_t j = 0; j < source_compile_defs.count; j++) {
                String_View def = source_compile_defs.items[j];
                sb_append_cstr(sb, "        #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"/D"SV_Fmt"\");\n", SV_Arg(def));
                sb_append_cstr(sb, "        #else\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"-D"SV_Fmt"\");\n", SV_Arg(def));
                sb_append_cstr(sb, "        #endif\n");
            }

            for (size_t j = 0; j < all_include_dirs.count; j++) {
                String_View inc = all_include_dirs.items[j];
                sb_append_cstr(sb, "        #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"/I"SV_Fmt"\");\n", SV_Arg(inc));
                sb_append_cstr(sb, "        #else\n");
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \"-I"SV_Fmt"\");\n", SV_Arg(inc));
                sb_append_cstr(sb, "        #endif\n");
            }
            for (size_t j = 0; j < all_compile_opts.count; j++) {
                String_View opt = all_compile_opts.items[j];
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \""SV_Fmt"\");\n", SV_Arg(opt));
            }

            String_List source_compile_opts = {0};
            string_list_init(&source_compile_opts);
            String_View src_opts_value = build_target_get_property(target, source_property_internal_key_temp(src, "COMPILE_OPTIONS"));
            split_source_property_values(src_opts_value, true, arena, &source_compile_opts);
            for (size_t j = 0; j < source_compile_opts.count; j++) {
                String_View opt = source_compile_opts.items[j];
                sb_appendf(sb, "        nob_cmd_append(&cc_cmd, \""SV_Fmt"\");\n", SV_Arg(opt));
            }

            sb_appendf(sb, "        if (!nob_cmd_run_sync(cc_cmd)) return 1;\n");
            sb_appendf(sb, "        nob_da_append(&objs_"SV_Fmt", obj);\n", SV_Arg(ident));
            sb_appendf(sb, "    }\n");
        }
        sb_appendf(sb, "\n");
    }
    
    bool can_link_target =
        target_type == TARGET_EXECUTABLE ||
        target_type == TARGET_STATIC_LIB ||
        target_type == TARGET_SHARED_LIB;
    bool is_object_lib = target_type == TARGET_OBJECT_LIB;
    bool archive_only_target = target_type == TARGET_STATIC_LIB;

    // Linkagem
    if (can_link_target) {
        sb_appendf(sb, "    // Linkagem\n");
        sb_appendf(sb, "    cmd_"SV_Fmt".count = 0;\n", SV_Arg(ident));
    }

    String_View default_cfg = build_model_get_default_config(model);
    String_View artifact_name = build_target_get_property_computed(target, sv_from_cstr("OUTPUT_NAME"), default_cfg);
    if (artifact_name.count == 0) artifact_name = target_name;
    String_View output_dir = build_target_get_property_computed(target, sv_from_cstr("OUTPUT_DIRECTORY"), default_cfg);
    if (output_dir.count == 0) output_dir = sv_from_cstr("build");
    String_View runtime_dir = build_target_get_property_computed(target, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"), default_cfg);
    if (runtime_dir.count == 0) runtime_dir = output_dir;
    String_View archive_dir = build_target_get_property_computed(target, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"), default_cfg);
    if (archive_dir.count == 0) archive_dir = output_dir;
    String_View library_dir = output_dir;
    String_View artifact_prefix = build_target_get_property_computed(target, sv_from_cstr("PREFIX"), default_cfg);
    String_View artifact_suffix = build_target_get_property_computed(target, sv_from_cstr("SUFFIX"), default_cfg);
    
    if (can_link_target) {
        switch (target_type) {
            case TARGET_EXECUTABLE:
                sb_appendf(sb, "    nob_cc(&cmd_"SV_Fmt");\n", SV_Arg(ident));
                sb_append_cstr(sb, "    #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"/nologo\", nob_temp_sprintf(\"/Fe:%%s\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\"));\n",
                          SV_Arg(ident), SV_Arg(runtime_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #else\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-o\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\");\n",
                          SV_Arg(ident), SV_Arg(runtime_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #endif\n");
                break;
            case TARGET_STATIC_LIB:
                sb_append_cstr(sb, "    #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"lib.exe\", \"/NOLOGO\", nob_temp_sprintf(\"/OUT:%%s\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\"));\n",
                          SV_Arg(ident), SV_Arg(archive_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #else\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"ar\", \"rcs\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\");\n",
                          SV_Arg(ident), SV_Arg(archive_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #endif\n");
                break;
            case TARGET_SHARED_LIB:
                sb_appendf(sb, "    nob_cc(&cmd_"SV_Fmt");\n", SV_Arg(ident));
                sb_append_cstr(sb, "    #if defined(_MSC_VER) && !defined(__clang__)\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"/nologo\", \"/LD\", nob_temp_sprintf(\"/Fe:%%s\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\"));\n",
                          SV_Arg(ident), SV_Arg(library_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #else\n");
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-shared\", \"-fPIC\");\n", SV_Arg(ident));
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-o\", \""SV_Fmt"/"SV_Fmt SV_Fmt SV_Fmt"\");\n",
                          SV_Arg(ident), SV_Arg(library_dir), SV_Arg(artifact_prefix), SV_Arg(artifact_name), SV_Arg(artifact_suffix));
                sb_append_cstr(sb, "    #endif\n");
                break;
            default:
                break;
        }
    } else if (is_object_lib) {
        sb_appendf(sb, "    // OBJECT library: apenas compila objetos, sem link final\n\n");
    } else {
        sb_appendf(sb, "    // Target sem etapa de build/link local (INTERFACE/IMPORTED/ALIAS)\n\n");
    }
    
    if (can_link_target) {
        // Adiciona objetos
        sb_appendf(sb, "    for (size_t i = 0; i < objs_"SV_Fmt".count; i++) {\n", SV_Arg(ident));
        sb_appendf(sb, "        nob_cmd_append(&cmd_"SV_Fmt", objs_"SV_Fmt".items[i]);\n", 
                  SV_Arg(ident), SV_Arg(ident));
        sb_appendf(sb, "    }\n");

        // Adiciona objetos vindos de OBJECT libraries dependentes
        for (size_t i = 0; i < target_object_dependencies->count; i++) {
            Build_Target *obj_dep = build_model_find_target(model, target_object_dependencies->items[i]);
            obj_dep = resolve_alias_target(model, obj_dep);
            if (!obj_dep || build_target_get_type(obj_dep) != TARGET_OBJECT_LIB) continue;

            String_Builder dep_ident_builder = {0};
            append_sanitized_identifier(&dep_ident_builder, build_target_get_name(obj_dep));
            String_View dep_ident = sb_to_sv(dep_ident_builder);
            sb_appendf(sb, "    for (size_t i = 0; i < objs_"SV_Fmt".count; i++) {\n", SV_Arg(dep_ident));
            sb_appendf(sb, "        nob_cmd_append(&cmd_"SV_Fmt", objs_"SV_Fmt".items[i]);\n",
                      SV_Arg(ident), SV_Arg(dep_ident));
            sb_appendf(sb, "    }\n");
            nob_sb_free(dep_ident_builder);
        }

        if (!archive_only_target) {
        for (size_t i = 0; i < all_link_opts.count; i++) {
            String_View opt = all_link_opts.items[i];
            sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \""SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(opt));
        }

        for (size_t i = 0; i < all_link_dirs.count; i++) {
            String_View dir = all_link_dirs.items[i];
            sb_append_cstr(sb, "    #if defined(_MSC_VER) && !defined(__clang__)\n");
            sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", nob_temp_sprintf(\"/LIBPATH:%%s\", \""SV_Fmt"\"));\n", SV_Arg(ident), SV_Arg(dir));
            sb_append_cstr(sb, "    #else\n");
            if (nob_sv_starts_with(dir, sv_from_cstr("-L"))) {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \""SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(dir));
            } else {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-L"SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(dir));
            }
            sb_append_cstr(sb, "    #endif\n");
        }
        for (size_t i = 0; i < all_link_targets.count; i++) {
            Build_Target *dep = build_model_find_target(model, all_link_targets.items[i]);
            dep = resolve_alias_target(model, dep);
            if (!dep || !target_is_linkable_artifact(dep)) continue;
            sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", ", SV_Arg(ident));
            sb_append_target_output_path_literal(sb, dep, active_cfg);
            sb_append_cstr(sb, ");\n");
        }
        for (size_t i = 0; i < all_link_libs.count; i++) {
            String_View lib = all_link_libs.items[i];
            if (lib.count == 0) continue;
            if (sv_eq_ci(lib, sv_from_cstr("debug")) ||
                sv_eq_ci(lib, sv_from_cstr("optimized")) ||
                sv_eq_ci(lib, sv_from_cstr("general"))) {
                continue;
            }

            sb_append_cstr(sb, "    #if defined(_MSC_VER) && !defined(__clang__)\n");
            if (sv_contains_substr(lib, "::")) {
                // Unresolved imported namespace token; skip in generated command line.
            } else if (nob_sv_end_with(lib, ".lib") ||
                       nob_sv_end_with(lib, ".a") ||
                       nob_sv_end_with(lib, ".dll") ||
                       nob_sv_end_with(lib, ".so") ||
                       nob_sv_end_with(lib, ".dylib") ||
                       sv_contains_char(lib, '/') ||
                       sv_contains_char(lib, '\\')) {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \""SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(lib));
            } else {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \""SV_Fmt".lib\");\n", SV_Arg(ident), SV_Arg(lib));
            }
            sb_append_cstr(sb, "    #else\n");
            if (sv_contains_substr(lib, "::")) {
                // Unresolved imported namespace token; skip in generated command line.
            } else if (nob_sv_eq(lib, sv_from_cstr("-framework"))) {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-framework\");\n", SV_Arg(ident));
            } else if (nob_sv_starts_with(lib, sv_from_cstr("-")) ||
                       nob_sv_end_with(lib, ".a") ||
                       nob_sv_end_with(lib, ".so") ||
                       nob_sv_end_with(lib, ".dylib") ||
                       nob_sv_end_with(lib, ".dll") ||
                       nob_sv_end_with(lib, ".lib") ||
                       sv_contains_char(lib, '/') ||
                       sv_contains_char(lib, '\\')) {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \""SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(lib));
            } else {
                sb_appendf(sb, "    nob_cmd_append(&cmd_"SV_Fmt", \"-l"SV_Fmt"\");\n", SV_Arg(ident), SV_Arg(lib));
            }
            sb_append_cstr(sb, "    #endif\n");
        }
        }
    
        // Executa comando
        sb_appendf(sb, "    if (!nob_cmd_run_sync(cmd_"SV_Fmt")) return 1;\n\n", SV_Arg(ident));
    }

    size_t post_build_count = 0;
    const Custom_Command *post_build_commands = build_target_get_custom_commands(target, false, &post_build_count);
    if (post_build_count > 0 && post_build_commands) {
        sb_appendf(sb, "    // Custom commands: POST_BUILD (%zu)\n", post_build_count);
        generate_custom_commands(model, post_build_commands, post_build_count, active_cfg, sb);
        sb_appendf(sb, "\n");
    }

    ds_shfree(all_compile_defs_set);
    ds_shfree(all_compile_opts_set);
    ds_shfree(all_include_dirs_set);
    ds_shfree(all_link_opts_set);
    ds_shfree(all_link_dirs_set);
    ds_shfree(all_link_libs_set);
    ds_shfree(all_link_targets_set);

    nob_sb_free(ident_builder);
}

// Gera código C completo a partir do modelo
static void generate_from_model(Build_Model *model, String_Builder *sb) {
    sb_append_cstr(sb, "#define NOB_IMPLEMENTATION\n");
    sb_append_cstr(sb, "#include \"nob.h\"\n\n");
    sb_append_cstr(sb, "int main(int argc, char **argv) {\n");
    sb_append_cstr(sb, "    NOB_GO_REBUILD_URSELF(argc, argv);\n");
    sb_append_cstr(sb, "    if (!nob_mkdir_if_not_exists(\"build\")) return 1;\n\n");

    generate_output_custom_commands(model, sb);
    
    // Gera código em ordem topológica para respeitar dependências.
    size_t sorted_count = 0;
    Build_Target **sorted = build_model_topological_sort(model, &sorted_count);
    if (sorted) {
        for (size_t i = 0; i < sorted_count; i++) {
            Build_Target *target = sorted[i];
            generate_target_code(model, target, sb);
        }
    } else {
        // Fallback defensivo para manter geração mesmo com dependências inválidas/cíclicas.
        size_t target_count = build_model_get_target_count(model);
        for (size_t i = 0; i < target_count; i++) {
            Build_Target *target = build_model_get_target_at(model, i);
            generate_target_code(model, target, sb);
        }
    }
    generate_install_code(model, sb);
    
    sb_append_cstr(sb, "    return 0;\n");
    sb_append_cstr(sb, "}\n");
}




