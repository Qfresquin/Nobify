// ============================================================================
// GERAÃ‡ÃƒO DE CÃ“DIGO A PARTIR DO MODELO
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

// Gera cÃ³digo C para um target
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

static bool target_has_dependents(Build_Model *model, String_View target_name) {
    if (!model || target_name.count == 0) return false;
    for (size_t i = 0; i < model->target_count; i++) {
        Build_Target *t = &model->targets[i];
        for (size_t j = 0; j < t->dependencies.count; j++) {
            if (nob_sv_eq(t->dependencies.items[j], target_name)) return true;
        }
        for (size_t j = 0; j < t->interface_dependencies.count; j++) {
            if (nob_sv_eq(t->interface_dependencies.items[j], target_name)) return true;
        }
    }
    return false;
}

static void sb_append_target_output_path_literal(String_Builder *sb, Build_Target *target, Build_Config active_cfg);
static Build_Target *resolve_alias_target(Build_Model *model, Build_Target *target);

static void generate_custom_commands(Build_Model *model, Custom_Command *commands, size_t count, Build_Config active_cfg, String_Builder *sb) {
    for (size_t i = 0; i < count; i++) {
        Custom_Command *cmd = &commands[i];
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
                String_List *rebuild_outputs = cmd->outputs.count > 0 ? &cmd->outputs : &cmd->byproducts;
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
    for (int depth = 0; current && current->type == TARGET_ALIAS && depth < 16; depth++) {
        if (current->dependencies.count == 0) break;
        current = build_model_find_target(model, current->dependencies.items[0]);
    }
    return current;
}

static bool target_is_linkable_artifact(Build_Target *target) {
    if (!target) return false;
    return target->type == TARGET_STATIC_LIB || target->type == TARGET_SHARED_LIB || target->type == TARGET_IMPORTED;
}

static String_View target_property_for_config(Build_Target *target, Build_Config cfg, const char *base_key, String_View fallback) {
    if (!target) return fallback;
    String_View suffix = config_suffix(cfg);
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
    String_List *compile_defs,
    String_List *compile_opts,
    String_List *include_dirs,
    String_List *link_opts,
    String_List *link_dirs,
    String_List *link_libs,
    String_List *link_targets
) {
    if (!model || !target) return;
    Build_Target *base = resolve_alias_target(model, target);
    if (!base) return;

    int idx = build_model_find_target_index(model, base->name);
    if (idx >= 0) {
        if (visited[idx]) return;
        visited[idx] = 1;
    }

    for (size_t i = 0; i < base->interface_compile_definitions.count; i++) {
        string_list_add_unique(compile_defs, model->arena, base->interface_compile_definitions.items[i]);
    }
    for (size_t i = 0; i < base->interface_compile_options.count; i++) {
        string_list_add_unique(compile_opts, model->arena, base->interface_compile_options.items[i]);
    }
    for (size_t i = 0; i < base->interface_include_directories.count; i++) {
        string_list_add_unique(include_dirs, model->arena, base->interface_include_directories.items[i]);
    }
    for (size_t i = 0; i < base->interface_link_options.count; i++) {
        string_list_add_unique(link_opts, model->arena, base->interface_link_options.items[i]);
    }
    for (size_t i = 0; i < base->interface_link_directories.count; i++) {
        string_list_add_unique(link_dirs, model->arena, base->interface_link_directories.items[i]);
    }
    for (size_t i = 0; i < base->interface_libs.count; i++) {
        string_list_add_unique(link_libs, model->arena, base->interface_libs.items[i]);
    }

    for (size_t i = 0; i < base->interface_dependencies.count; i++) {
        String_View dep_name = base->interface_dependencies.items[i];
        Build_Target *dep = build_model_find_target(model, dep_name);
        dep = resolve_alias_target(model, dep);
        if (!dep) continue;

        if (target_is_linkable_artifact(dep)) {
            string_list_add_unique(link_targets, model->arena, dep->name);
        }
        collect_interface_usage_recursive(model, dep, visited, compile_defs, compile_opts, include_dirs,
                                         link_opts, link_dirs, link_libs, link_targets);
    }
}

static void generate_output_custom_commands(Build_Model *model, String_Builder *sb) {
    if (!model || model->output_custom_command_count == 0) return;

    Build_Config active_cfg = config_from_string(model->default_config);
    sb_appendf(sb, "    // Custom commands: OUTPUT (%zu)\n", model->output_custom_command_count);

    for (size_t i = 0; i < model->output_custom_command_count; i++) {
        Custom_Command *cmd = &model->output_custom_commands[i];
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
    if (target->type == TARGET_IMPORTED) {
        String_View imported_location = target_property_for_config(target, active_cfg, "IMPORTED_LOCATION", sv_from_cstr(""));
        if (imported_location.count > 0) {
            sb_append_c_string_literal(sb, imported_location);
            return;
        }
        sb_append_c_string_literal(sb, sv_from_cstr(""));
        return;
    }
    if (target->type == TARGET_INTERFACE_LIB || target->type == TARGET_ALIAS || target->type == TARGET_OBJECT_LIB) {
        sb_append_c_string_literal(sb, sv_from_cstr(""));
        return;
    }

    String_View artifact_name = target_property_for_config(target, active_cfg, "OUTPUT_NAME",
        target->output_name.count > 0 ? target->output_name : target->name);
    String_View out_dir = target_property_for_config(target, active_cfg, "OUTPUT_DIRECTORY", sv_from_cstr("build"));
    if (target->type == TARGET_EXECUTABLE) {
        out_dir = target_property_for_config(target, active_cfg, "RUNTIME_OUTPUT_DIRECTORY",
            target->runtime_output_directory.count > 0 ? target->runtime_output_directory : out_dir);
    } else if (target->type == TARGET_STATIC_LIB) {
        out_dir = target_property_for_config(target, active_cfg, "ARCHIVE_OUTPUT_DIRECTORY",
            target->archive_output_directory.count > 0 ? target->archive_output_directory : out_dir);
    }
    String_View prefix = target_property_for_config(target, active_cfg, "PREFIX", target->prefix);
    String_View suffix = target_property_for_config(target, active_cfg, "SUFFIX", target->suffix);

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
    if (model->cpack_component_count == 0 &&
        model->cpack_component_group_count == 0 &&
        model->cpack_install_type_count == 0) {
        return sv_from_cstr("");
    }

    String_Builder manifest = {0};
    sb_append_cstr(&manifest, "# cmk2nob CPack component manifest\n");
    for (size_t i = 0; i < model->cpack_install_type_count; i++) {
        CPack_Install_Type *it = &model->cpack_install_types[i];
        sb_append_cstr(&manifest, "install_type:");
        sb_append_buf(&manifest, it->name.data, it->name.count);
        sb_append_cstr(&manifest, "|display=");
        sb_append_buf(&manifest, it->display_name.data, it->display_name.count);
        sb_append(&manifest, '\n');
    }
    for (size_t i = 0; i < model->cpack_component_group_count; i++) {
        CPack_Component_Group *g = &model->cpack_component_groups[i];
        sb_append_cstr(&manifest, "group:");
        sb_append_buf(&manifest, g->name.data, g->name.count);
        sb_append_cstr(&manifest, "|display=");
        sb_append_buf(&manifest, g->display_name.data, g->display_name.count);
        sb_append_cstr(&manifest, "|description=");
        sb_append_buf(&manifest, g->description.data, g->description.count);
        sb_append_cstr(&manifest, "|parent=");
        sb_append_buf(&manifest, g->parent_group.data, g->parent_group.count);
        sb_append_cstr(&manifest, "|expanded=");
        sb_append_cstr(&manifest, g->expanded ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|bold=");
        sb_append_cstr(&manifest, g->bold_title ? "ON" : "OFF");
        sb_append(&manifest, '\n');
    }
    for (size_t i = 0; i < model->cpack_component_count; i++) {
        CPack_Component *c = &model->cpack_components[i];
        sb_append_cstr(&manifest, "component:");
        sb_append_buf(&manifest, c->name.data, c->name.count);
        sb_append_cstr(&manifest, "|display=");
        sb_append_buf(&manifest, c->display_name.data, c->display_name.count);
        sb_append_cstr(&manifest, "|description=");
        sb_append_buf(&manifest, c->description.data, c->description.count);
        sb_append_cstr(&manifest, "|group=");
        sb_append_buf(&manifest, c->group.data, c->group.count);
        sb_append_cstr(&manifest, "|required=");
        sb_append_cstr(&manifest, c->required ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|hidden=");
        sb_append_cstr(&manifest, c->hidden ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|disabled=");
        sb_append_cstr(&manifest, c->disabled ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|downloaded=");
        sb_append_cstr(&manifest, c->downloaded ? "ON" : "OFF");
        sb_append_cstr(&manifest, "|depends=");
        for (size_t d = 0; d < c->depends.count; d++) {
            if (d > 0) sb_append(&manifest, ';');
            sb_append_buf(&manifest, c->depends.items[d].data, c->depends.items[d].count);
        }
        sb_append_cstr(&manifest, "|install_types=");
        for (size_t t = 0; t < c->install_types.count; t++) {
            if (t > 0) sb_append(&manifest, ';');
            sb_append_buf(&manifest, c->install_types.items[t].data, c->install_types.items[t].count);
        }
        sb_append(&manifest, '\n');
    }

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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

    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
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
    return sv_from_cstr(arena_strndup(model->arena, manifest.items, manifest.count));
}

static void generate_install_code(Build_Model *model, String_Builder *sb) {
    size_t total_rules = model->install_rules.targets.count + model->install_rules.files.count +
                         model->install_rules.directories.count + model->install_rules.programs.count;
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
    Build_Config active_cfg = config_from_string(model->default_config);

    String_View default_dest = model->install_rules.prefix.count > 0 ? model->install_rules.prefix : sv_from_cstr("install");
    sb_append_cstr(sb, "    if (argc > 1 && strcmp(argv[1], \"install\") == 0) {\n");

    for (size_t i = 0; i < model->install_rules.targets.count; i++) {
        String_View target_name = {0}, destination = {0};
        split_install_entry(model->install_rules.targets.items[i], default_dest, &target_name, &destination);
        Build_Target *target = build_model_find_target(model, target_name);
        if (!target) continue;
        if (target->type == TARGET_INTERFACE_LIB || target->type == TARGET_ALIAS || target->type == TARGET_OBJECT_LIB) continue;
        if (target->type == TARGET_IMPORTED &&
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

    for (size_t i = 0; i < model->install_rules.files.count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(model->install_rules.files.items[i], default_dest, &src, &destination);
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

    for (size_t i = 0; i < model->install_rules.programs.count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(model->install_rules.programs.items[i], default_dest, &src, &destination);
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

    for (size_t i = 0; i < model->install_rules.directories.count; i++) {
        String_View src = {0}, destination = {0};
        split_install_entry(model->install_rules.directories.items[i], default_dest, &src, &destination);
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
    String_Builder ident_builder = {0};
    append_sanitized_identifier(&ident_builder, target->name);
    String_View ident = sb_to_sv(ident_builder);

    sb_appendf(sb, "    // --- Target: "SV_Fmt" ---\n", SV_Arg(target->name));
    if (target->type == TARGET_UTILITY && target->exclude_from_all && !target_has_dependents(model, target->name)) {
        sb_appendf(sb, "    // Skipping EXCLUDE_FROM_ALL utility target: "SV_Fmt"\n\n", SV_Arg(target->name));
        nob_sb_free(ident_builder);
        return;
    }
    sb_appendf(sb, "    Nob_Cmd cmd_"SV_Fmt" = {0};\n", SV_Arg(ident));
    sb_appendf(sb, "    Nob_File_Paths objs_"SV_Fmt" = {0};\n\n", SV_Arg(ident));
    Build_Config active_cfg = config_from_string(model->default_config);

    if (target->pre_build_count > 0) {
        sb_appendf(sb, "    // Custom commands: PRE_BUILD (%zu)\n", target->pre_build_count);
        generate_custom_commands(model, target->pre_build_commands, target->pre_build_count, active_cfg, sb);
        sb_appendf(sb, "\n");
    }

    bool can_compile_sources =
        target->type == TARGET_EXECUTABLE ||
        target->type == TARGET_STATIC_LIB ||
        target->type == TARGET_SHARED_LIB ||
        target->type == TARGET_OBJECT_LIB;
    size_t cfg_idx = active_cfg == CONFIG_ALL ? (size_t)CONFIG_DEBUG : (size_t)active_cfg;

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

    for (size_t i = 0; i < target->properties[CONFIG_ALL].compile_definitions.count; i++) {
        string_list_add_unique(&all_compile_defs, model->arena, target->properties[CONFIG_ALL].compile_definitions.items[i]);
    }
    for (size_t i = 0; i < target->properties[cfg_idx].compile_definitions.count; i++) {
        string_list_add_unique(&all_compile_defs, model->arena, target->properties[cfg_idx].compile_definitions.items[i]);
    }
    for (size_t i = 0; i < model->global_definitions.count; i++) {
        string_list_add_unique(&all_compile_defs, model->arena, model->global_definitions.items[i]);
    }
    for (size_t i = 0; i < target->properties[CONFIG_ALL].compile_options.count; i++) {
        string_list_add_unique(&all_compile_opts, model->arena, target->properties[CONFIG_ALL].compile_options.items[i]);
    }
    for (size_t i = 0; i < target->properties[cfg_idx].compile_options.count; i++) {
        string_list_add_unique(&all_compile_opts, model->arena, target->properties[cfg_idx].compile_options.items[i]);
    }
    for (size_t i = 0; i < model->global_compile_options.count; i++) {
        string_list_add_unique(&all_compile_opts, model->arena, model->global_compile_options.items[i]);
    }
    for (size_t i = 0; i < target->properties[CONFIG_ALL].include_directories.count; i++) {
        string_list_add_unique(&all_include_dirs, model->arena, target->properties[CONFIG_ALL].include_directories.items[i]);
    }
    for (size_t i = 0; i < target->properties[cfg_idx].include_directories.count; i++) {
        string_list_add_unique(&all_include_dirs, model->arena, target->properties[cfg_idx].include_directories.items[i]);
    }
    for (size_t i = 0; i < model->directories.include_dirs.count; i++) {
        string_list_add_unique(&all_include_dirs, model->arena, model->directories.include_dirs.items[i]);
    }
    for (size_t i = 0; i < model->directories.system_include_dirs.count; i++) {
        string_list_add_unique(&all_include_dirs, model->arena, model->directories.system_include_dirs.items[i]);
    }
    for (size_t i = 0; i < target->properties[CONFIG_ALL].link_options.count; i++) {
        string_list_add_unique(&all_link_opts, model->arena, target->properties[CONFIG_ALL].link_options.items[i]);
    }
    for (size_t i = 0; i < target->properties[cfg_idx].link_options.count; i++) {
        string_list_add_unique(&all_link_opts, model->arena, target->properties[cfg_idx].link_options.items[i]);
    }
    for (size_t i = 0; i < model->global_link_options.count; i++) {
        string_list_add_unique(&all_link_opts, model->arena, model->global_link_options.items[i]);
    }
    for (size_t i = 0; i < target->properties[CONFIG_ALL].link_directories.count; i++) {
        string_list_add_unique(&all_link_dirs, model->arena, target->properties[CONFIG_ALL].link_directories.items[i]);
    }
    for (size_t i = 0; i < target->properties[cfg_idx].link_directories.count; i++) {
        string_list_add_unique(&all_link_dirs, model->arena, target->properties[cfg_idx].link_directories.items[i]);
    }
    for (size_t i = 0; i < model->directories.link_dirs.count; i++) {
        string_list_add_unique(&all_link_dirs, model->arena, model->directories.link_dirs.items[i]);
    }
    for (size_t i = 0; i < target->link_libraries.count; i++) {
        string_list_add_unique(&all_link_libs, model->arena, target->link_libraries.items[i]);
    }
    for (size_t i = 0; i < model->global_link_libraries.count; i++) {
        string_list_add_unique(&all_link_libs, model->arena, model->global_link_libraries.items[i]);
    }

    uint8_t *visited = arena_alloc_zero(model->arena, model->target_count * sizeof(uint8_t));
    for (size_t i = 0; i < target->dependencies.count; i++) {
        Build_Target *dep = build_model_find_target(model, target->dependencies.items[i]);
        dep = resolve_alias_target(model, dep);
        if (!dep) continue;
        if (target_is_linkable_artifact(dep)) {
            string_list_add_unique(&all_link_targets, model->arena, dep->name);
        }
        collect_interface_usage_recursive(model, dep, visited, &all_compile_defs, &all_compile_opts, &all_include_dirs,
                                         &all_link_opts, &all_link_dirs, &all_link_libs, &all_link_targets);
    }
    
    // Compilar fontes
    if (can_compile_sources) {
        sb_appendf(sb, "    // Compilar fontes\n");
        for (size_t i = 0; i < target->sources.count; i++) {
            String_View src = target->sources.items[i];
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
            split_source_property_values(src_defs_value, false, model->arena, &source_compile_defs);
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
            split_source_property_values(src_opts_value, true, model->arena, &source_compile_opts);
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
        target->type == TARGET_EXECUTABLE ||
        target->type == TARGET_STATIC_LIB ||
        target->type == TARGET_SHARED_LIB;
    bool is_object_lib = target->type == TARGET_OBJECT_LIB;
    bool archive_only_target = target->type == TARGET_STATIC_LIB;

    // Linkagem
    if (can_link_target) {
        sb_appendf(sb, "    // Linkagem\n");
        sb_appendf(sb, "    cmd_"SV_Fmt".count = 0;\n", SV_Arg(ident));
    }

    String_View artifact_name = target_property_for_config(target, active_cfg, "OUTPUT_NAME",
        target->output_name.count > 0 ? target->output_name : target->name);
    String_View output_dir = target_property_for_config(target, active_cfg, "OUTPUT_DIRECTORY",
        target->output_directory.count > 0 ? target->output_directory : sv_from_cstr("build"));
    String_View runtime_dir = target_property_for_config(target, active_cfg, "RUNTIME_OUTPUT_DIRECTORY",
        target->runtime_output_directory.count > 0 ? target->runtime_output_directory : output_dir);
    String_View archive_dir = target_property_for_config(target, active_cfg, "ARCHIVE_OUTPUT_DIRECTORY",
        target->archive_output_directory.count > 0 ? target->archive_output_directory : output_dir);
    String_View library_dir = output_dir;
    String_View artifact_prefix = target_property_for_config(target, active_cfg, "PREFIX", target->prefix);
    String_View artifact_suffix = target_property_for_config(target, active_cfg, "SUFFIX", target->suffix);
    
    if (can_link_target) {
        switch (target->type) {
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
            sb_append_target_output_path_literal(sb, dep, config_from_string(model->default_config));
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

    if (target->post_build_count > 0) {
        sb_appendf(sb, "    // Custom commands: POST_BUILD (%zu)\n", target->post_build_count);
        generate_custom_commands(model, target->post_build_commands, target->post_build_count, active_cfg, sb);
        sb_appendf(sb, "\n");
    }

    nob_sb_free(ident_builder);
}

// Gera cÃ³digo C completo a partir do modelo
static void generate_from_model(Build_Model *model, String_Builder *sb) {
    sb_append_cstr(sb, "#define NOB_IMPLEMENTATION\n");
    sb_append_cstr(sb, "#include \"nob.h\"\n\n");
    sb_append_cstr(sb, "int main(int argc, char **argv) {\n");
    sb_append_cstr(sb, "    NOB_GO_REBUILD_URSELF(argc, argv);\n");
    sb_append_cstr(sb, "    if (!nob_mkdir_if_not_exists(\"build\")) return 1;\n\n");

    generate_output_custom_commands(model, sb);
    
    // Gera cÃ³digo em ordem topolÃ³gica para respeitar dependÃªncias.
    size_t sorted_count = 0;
    Build_Target **sorted = build_model_topological_sort(model, &sorted_count);
    if (sorted) {
        for (size_t i = 0; i < sorted_count; i++) {
            Build_Target *target = sorted[i];
            generate_target_code(model, target, sb);
        }
    } else {
        // Fallback defensivo para manter geraÃ§Ã£o mesmo com dependÃªncias invÃ¡lidas/cÃ­clicas.
        for (size_t i = 0; i < model->target_count; i++) {
            Build_Target *target = &model->targets[i];
            generate_target_code(model, target, sb);
        }
    }
    generate_install_code(model, sb);
    
    sb_append_cstr(sb, "    return 0;\n");
    sb_append_cstr(sb, "}\n");
}


