// ============================================================================
// FIND PACKAGE LOGIC
// ============================================================================

// Simula o comportamento de um mÃ³dulo Find<Package>.cmake
// Define variÃ¡veis padrÃ£o como <NAME>_FOUND, <NAME>_LIBRARIES, etc.
typedef struct {
    bool required;
    bool quiet;
    bool exact_version;
    bool mode_module_only;
    bool mode_config_only;
    bool no_default_path;
    bool no_cmake_path;
    bool no_cmake_environment_path;
    bool no_system_environment_path;
    bool no_cmake_system_path;
    String_View requested_version;
    String_List required_components;
    String_List optional_components;
    String_List names;
    String_List hints;
    String_List paths;
    String_List path_suffixes;
} Find_Package_Request;

static bool find_package_is_keyword(String_View arg) {
    return sv_eq_ci(arg, sv_from_cstr("EXACT")) ||
           sv_eq_ci(arg, sv_from_cstr("REQUIRED")) ||
           sv_eq_ci(arg, sv_from_cstr("OPTIONAL")) ||
           sv_eq_ci(arg, sv_from_cstr("QUIET")) ||
           sv_eq_ci(arg, sv_from_cstr("MODULE")) ||
           sv_eq_ci(arg, sv_from_cstr("CONFIG")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_MODULE")) ||
           sv_eq_ci(arg, sv_from_cstr("COMPONENTS")) ||
           sv_eq_ci(arg, sv_from_cstr("OPTIONAL_COMPONENTS")) ||
           sv_eq_ci(arg, sv_from_cstr("NAMES")) ||
           sv_eq_ci(arg, sv_from_cstr("HINTS")) ||
           sv_eq_ci(arg, sv_from_cstr("PATHS")) ||
           sv_eq_ci(arg, sv_from_cstr("PATH_SUFFIXES")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_DEFAULT_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_ENVIRONMENT_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_SYSTEM_ENVIRONMENT_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_SYSTEM_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_POLICY_SCOPE")) ||
           sv_eq_ci(arg, sv_from_cstr("BYPASS_PROVIDER")) ||
           sv_eq_ci(arg, sv_from_cstr("UNWIND_INCLUDE"));
}

static bool find_package_is_version_literal(String_View value) {
    if (value.count == 0) return false;
    if (!isdigit((unsigned char)value.data[0])) return false;
    for (size_t i = 0; i < value.count; i++) {
        char c = value.data[i];
        if (!(isdigit((unsigned char)c) || c == '.')) return false;
    }
    return true;
}

static int find_package_parse_version_part(const char *s, size_t len, size_t *idx) {
    int value = 0;
    while (*idx < len && isdigit((unsigned char)s[*idx])) {
        value = value * 10 + (s[*idx] - '0');
        (*idx)++;
    }
    while (*idx < len && s[*idx] != '.') (*idx)++;
    if (*idx < len && s[*idx] == '.') (*idx)++;
    return value;
}

static int find_package_compare_versions(String_View lhs, String_View rhs) {
    const char *ls = nob_temp_sv_to_cstr(lhs);
    const char *rs = nob_temp_sv_to_cstr(rhs);
    size_t li = 0, ri = 0;
    size_t ll = strlen(ls), rl = strlen(rs);
    while (li < ll || ri < rl) {
        int lv = find_package_parse_version_part(ls, ll, &li);
        int rv = find_package_parse_version_part(rs, rl, &ri);
        if (lv < rv) return -1;
        if (lv > rv) return 1;
    }
    return 0;
}

static bool find_package_component_is_available(Evaluator_Context *ctx, String_View pkg_name, String_View component) {
    String_View avail_var = sv_from_cstr(nob_temp_sprintf("%s_AVAILABLE_COMPONENTS", nob_temp_sv_to_cstr(pkg_name)));
    String_View avail = eval_get_var(ctx, avail_var);
    if (avail.count == 0) return true;

    String_List components = {0};
    string_list_init(&components);
    split_semicolon_list(ctx, avail, &components);
    for (size_t i = 0; i < components.count; i++) {
        if (sv_eq_ci(components.items[i], component)) return true;
    }
    return false;
}

typedef enum Find_Package_Source {
    FIND_PACKAGE_SOURCE_NONE = 0,
    FIND_PACKAGE_SOURCE_MODULE,
    FIND_PACKAGE_SOURCE_CONFIG,
    FIND_PACKAGE_SOURCE_CACHE,
} Find_Package_Source;

static const char *find_package_source_name(Find_Package_Source source) {
    switch (source) {
        case FIND_PACKAGE_SOURCE_MODULE: return "module";
        case FIND_PACKAGE_SOURCE_CONFIG: return "config";
        case FIND_PACKAGE_SOURCE_CACHE: return "cache";
        default: return "none";
    }
}

static bool find_package_eval_include_file(Evaluator_Context *ctx, String_View file_path) {
    if (!ctx || file_path.count == 0) return false;
    Token tok = {0};
    tok.kind = TOKEN_IDENTIFIER;
    tok.text = file_path;
    tok.has_space_left = true;
    Arg arg = {0};
    arg.items = &tok;
    arg.count = 1;
    arg.capacity = 1;
    Args include_args = {0};
    include_args.items = &arg;
    include_args.count = 1;
    include_args.capacity = 1;
    eval_include_command(ctx, include_args);
    return true;
}

static String_View find_package_upper_sv_copy(Arena *arena, String_View value) {
    if (!arena || value.count == 0) return sv_from_cstr("");
    char *buf = arena_strndup(arena, value.data, value.count);
    if (!buf) return sv_from_cstr("");
    for (size_t i = 0; i < value.count; i++) {
        buf[i] = (char)toupper((unsigned char)buf[i]);
    }
    return sv_from_cstr(buf);
}

static String_View find_package_lower_sv_copy(Arena *arena, String_View value) {
    if (!arena || value.count == 0) return sv_from_cstr("");
    char *buf = arena_strndup(arena, value.data, value.count);
    if (!buf) return sv_from_cstr("");
    for (size_t i = 0; i < value.count; i++) {
        buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    return sv_from_cstr(buf);
}

static bool find_package_read_found_var(Evaluator_Context *ctx, String_View package_name, const String_List *aliases) {
    if (!ctx) return false;
    String_View found_names[32] = {0};
    size_t found_name_count = 0;

    found_names[found_name_count++] = package_name;
    String_View package_upper = find_package_upper_sv_copy(ctx->arena, package_name);
    if (package_upper.count > 0 && found_name_count < 32) found_names[found_name_count++] = package_upper;

    if (aliases) {
        for (size_t i = 0; i < aliases->count && found_name_count + 2 < 32; i++) {
            String_View a = aliases->items[i];
            found_names[found_name_count++] = a;
            String_View au = find_package_upper_sv_copy(ctx->arena, a);
            if (au.count > 0) found_names[found_name_count++] = au;
        }
    }

    for (size_t i = 0; i < found_name_count; i++) {
        String_View key = sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(found_names[i])));
        String_View value = eval_get_var(ctx, key);
        if (value.count > 0 && !cmake_string_is_false(value)) {
            return true;
        }
    }
    return false;
}

static String_View find_package_read_var(Evaluator_Context *ctx,
                                         String_View package_name,
                                         const String_List *aliases,
                                         const char *suffix) {
    if (!ctx || !suffix) return sv_from_cstr("");

    String_View names[32] = {0};
    size_t name_count = 0;
    names[name_count++] = package_name;

    String_View package_upper = find_package_upper_sv_copy(ctx->arena, package_name);
    if (package_upper.count > 0 && name_count < 32) {
        names[name_count++] = package_upper;
    }

    if (aliases) {
        for (size_t i = 0; i < aliases->count && name_count + 2 < 32; i++) {
            String_View alias = aliases->items[i];
            names[name_count++] = alias;
            String_View alias_upper = find_package_upper_sv_copy(ctx->arena, alias);
            if (alias_upper.count > 0) names[name_count++] = alias_upper;
        }
    }

    for (size_t i = 0; i < name_count; i++) {
        String_View key = sv_from_cstr(nob_temp_sprintf("%s_%s", nob_temp_sv_to_cstr(names[i]), suffix));
        String_View value = eval_get_var(ctx, key);
        if (value.count > 0) return value;
    }

    return sv_from_cstr("");
}

static void find_package_collect_dirs(Evaluator_Context *ctx, String_View value, String_List *out_dirs) {
    if (!ctx || !out_dirs || value.count == 0) return;
    split_semicolon_list(ctx, value, out_dirs);
}

static void find_package_collect_env_dirs(Evaluator_Context *ctx, const char *env_name, String_List *out_dirs) {
    if (!ctx || !env_name || !out_dirs) return;
    const char *env = getenv(env_name);
    if (!env || env[0] == '\0') return;
    find_search_split_env_path(ctx->arena, sv_from_cstr(env), out_dirs);
}

static bool find_package_search_module_file(Evaluator_Context *ctx,
                                            const Find_Package_Request *req,
                                            String_View *out_file) {
    if (!ctx || !req || !out_file) return false;
    *out_file = sv_from_cstr("");

    String_List module_dirs = {0};
    string_list_init(&module_dirs);
    for (size_t i = 0; i < req->hints.count; i++) {
        string_list_add_unique(&module_dirs, ctx->arena, req->hints.items[i]);
    }
    for (size_t i = 0; i < req->paths.count; i++) {
        string_list_add_unique(&module_dirs, ctx->arena, req->paths.items[i]);
    }
    if (!req->no_default_path) {
        if (!req->no_cmake_path) {
            find_package_collect_dirs(ctx, eval_get_var(ctx, sv_from_cstr("CMAKE_MODULE_PATH")), &module_dirs);
        }
        if (!req->no_cmake_environment_path) {
            find_package_collect_env_dirs(ctx, "CMAKE_MODULE_PATH", &module_dirs);
        }
        if (!req->no_cmake_system_path) {
            String_View cmake_root = eval_get_var(ctx, sv_from_cstr("CMAKE_ROOT"));
            if (cmake_root.count > 0) {
                String_View modules_dir = cmk_path_join(ctx->arena, cmake_root, sv_from_cstr("Modules"));
                string_list_add_unique(&module_dirs, ctx->arena, modules_dir);
                string_list_add_unique(&module_dirs, ctx->arena, cmake_root);
            }
        }
    }

    String_List module_names = {0};
    string_list_init(&module_names);
    for (size_t i = 0; i < req->names.count; i++) {
        String_View n = req->names.items[i];
        if (n.count == 0) continue;
        String_View file = sv_from_cstr(nob_temp_sprintf("Find%s.cmake", nob_temp_sv_to_cstr(n)));
        string_list_add_unique(&module_names, ctx->arena, file);
    }
    String_List no_suffixes = {0};
    string_list_init(&no_suffixes);
    return find_search_candidates(ctx->arena, &module_dirs, &no_suffixes, &module_names, out_file);
}

static bool find_package_search_config_file(Evaluator_Context *ctx,
                                            String_View package_name,
                                            const Find_Package_Request *req,
                                            String_View *out_file,
                                            String_View *out_dir) {
    if (!ctx || !req || !out_file || !out_dir) return false;
    *out_file = sv_from_cstr("");
    *out_dir = sv_from_cstr("");

    String_List search_dirs = {0};
    string_list_init(&search_dirs);
    for (size_t i = 0; i < req->hints.count; i++) {
        string_list_add_unique(&search_dirs, ctx->arena, req->hints.items[i]);
    }
    for (size_t i = 0; i < req->paths.count; i++) {
        string_list_add_unique(&search_dirs, ctx->arena, req->paths.items[i]);
    }
    if (!req->no_default_path) {
        if (!req->no_cmake_path) {
            String_View package_dir_var = find_package_read_var(ctx, package_name, &req->names, "DIR");
            if (package_dir_var.count > 0 && !cmake_string_is_false(package_dir_var)) {
                String_List dirs = {0};
                string_list_init(&dirs);
                split_semicolon_list(ctx, package_dir_var, &dirs);
                for (size_t i = 0; i < dirs.count; i++) {
                    if (dirs.items[i].count > 0 && !cmake_string_is_false(dirs.items[i])) {
                        string_list_add_unique(&search_dirs, ctx->arena, dirs.items[i]);
                    }
                }
            }
            find_package_collect_dirs(ctx, eval_get_var(ctx, sv_from_cstr("CMAKE_PREFIX_PATH")), &search_dirs);
        }
        if (!req->no_cmake_environment_path) {
            String_View pkg_upper = find_package_upper_sv_copy(ctx->arena, package_name);
            find_package_collect_env_dirs(ctx, nob_temp_sprintf("%s_DIR", nob_temp_sv_to_cstr(package_name)), &search_dirs);
            if (pkg_upper.count > 0) {
                find_package_collect_env_dirs(ctx, nob_temp_sprintf("%s_DIR", nob_temp_sv_to_cstr(pkg_upper)), &search_dirs);
            }
            for (size_t i = 0; i < req->names.count; i++) {
                String_View alias = req->names.items[i];
                if (alias.count == 0) continue;
                String_View alias_upper = find_package_upper_sv_copy(ctx->arena, alias);
                find_package_collect_env_dirs(ctx, nob_temp_sprintf("%s_DIR", nob_temp_sv_to_cstr(alias)), &search_dirs);
                if (alias_upper.count > 0) {
                    find_package_collect_env_dirs(ctx, nob_temp_sprintf("%s_DIR", nob_temp_sv_to_cstr(alias_upper)), &search_dirs);
                }
            }
            find_package_collect_env_dirs(ctx, "CMAKE_PREFIX_PATH", &search_dirs);
        }
        if (!req->no_system_environment_path) {
            find_package_collect_env_dirs(ctx, "PATH", &search_dirs);
        }
        if (!req->no_cmake_system_path) {
            find_package_collect_dirs(ctx, eval_get_var(ctx, sv_from_cstr("CMAKE_SYSTEM_PREFIX_PATH")), &search_dirs);
#if defined(_WIN32)
            find_package_collect_env_dirs(ctx, "ProgramFiles", &search_dirs);
            find_package_collect_env_dirs(ctx, "ProgramFiles(x86)", &search_dirs);
#else
            string_list_add_unique(&search_dirs, ctx->arena, sv_from_cstr("/usr"));
            string_list_add_unique(&search_dirs, ctx->arena, sv_from_cstr("/usr/local"));
#endif
        }
    }

    String_List suffixes = {0};
    string_list_init(&suffixes);
    string_list_add_unique(&suffixes, ctx->arena, sv_from_cstr("cmake"));
    string_list_add_unique(&suffixes, ctx->arena, sv_from_cstr("lib/cmake"));
    string_list_add_unique(&suffixes, ctx->arena, sv_from_cstr("share/cmake"));
    for (size_t i = 0; i < req->path_suffixes.count; i++) {
        string_list_add_unique(&suffixes, ctx->arena, req->path_suffixes.items[i]);
    }
    for (size_t i = 0; i < req->names.count; i++) {
        String_View n = req->names.items[i];
        if (n.count == 0) continue;
        string_list_add_unique(&suffixes, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib/cmake/%s", nob_temp_sv_to_cstr(n))));
        string_list_add_unique(&suffixes, ctx->arena, sv_from_cstr(nob_temp_sprintf("share/%s/cmake", nob_temp_sv_to_cstr(n))));
    }

    String_List config_names = {0};
    string_list_init(&config_names);
    for (size_t i = 0; i < req->names.count; i++) {
        String_View n = req->names.items[i];
        if (n.count == 0) continue;
        String_View lower = find_package_lower_sv_copy(ctx->arena, n);
        string_list_add_unique(&config_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("%sConfig.cmake", nob_temp_sv_to_cstr(n))));
        string_list_add_unique(&config_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("%s-config.cmake", nob_temp_sv_to_cstr(n))));
        if (lower.count > 0) {
            string_list_add_unique(&config_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("%s-config.cmake", nob_temp_sv_to_cstr(lower))));
        }
    }

    String_List no_suffixes = {0};
    string_list_init(&no_suffixes);
    bool ok = find_search_candidates(ctx->arena, &search_dirs, &no_suffixes, &config_names, out_file);
    if (!ok) {
        ok = find_search_candidates(ctx->arena, &search_dirs, &suffixes, &config_names, out_file);
    }
    if (ok) {
        *out_dir = cmk_path_parent(ctx->arena, *out_file);
    }
    return ok;
}

static bool eval_resolve_package(Evaluator_Context *ctx,
                                 String_View package_name,
                                 const Find_Package_Request *request,
                                 String_View *out_dir,
                                 Find_Package_Source *out_source) {
    if (out_dir) *out_dir = sv_from_cstr("");
    if (out_source) *out_source = FIND_PACKAGE_SOURCE_NONE;
    if (!ctx || !request) return false;

    String_View loaded_file = sv_from_cstr("");
    String_View pkg_dir = sv_from_cstr("");
    Find_Package_Source source = FIND_PACKAGE_SOURCE_NONE;
    bool prefer_config = sv_bool_is_true(eval_get_var(ctx, sv_from_cstr("CMAKE_FIND_PACKAGE_PREFER_CONFIG")));
    bool config_first = request->mode_config_only || (!request->mode_module_only && prefer_config);

    if (!request->mode_module_only) {
        if (config_first || request->mode_config_only) {
            if (find_package_search_config_file(ctx, package_name, request, &loaded_file, &pkg_dir)) {
                find_package_eval_include_file(ctx, loaded_file);
                source = FIND_PACKAGE_SOURCE_CONFIG;
            }
        }
    }
    if (source == FIND_PACKAGE_SOURCE_NONE && !request->mode_config_only) {
        if (find_package_search_module_file(ctx, request, &loaded_file)) {
            find_package_eval_include_file(ctx, loaded_file);
            source = FIND_PACKAGE_SOURCE_MODULE;
        }
    }
    if (source == FIND_PACKAGE_SOURCE_NONE && !request->mode_module_only && !config_first) {
        if (find_package_search_config_file(ctx, package_name, request, &loaded_file, &pkg_dir)) {
            find_package_eval_include_file(ctx, loaded_file);
            source = FIND_PACKAGE_SOURCE_CONFIG;
        }
    }

    bool found = find_package_read_found_var(ctx, package_name, &request->names);
    if (!found) {
        String_View cache_found = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(package_name))));
        if (cache_found.count > 0 && !cmake_string_is_false(cache_found)) {
            found = true;
            source = FIND_PACKAGE_SOURCE_CACHE;
        }
    }

    if (out_dir) *out_dir = pkg_dir;
    if (out_source) *out_source = source;
    return found;
}

static void find_package_apply_imported_target_usage(Evaluator_Context *ctx,
                                                     Build_Target *pkg_target,
                                                     String_View libs,
                                                     String_View includes,
                                                     String_View cflags,
                                                     String_View ldflags,
                                                     String_View link_dirs) {
    if (!ctx || !pkg_target) return;
    if (libs.count > 0) {
        eval_append_link_library_value(ctx, pkg_target, VISIBILITY_INTERFACE, libs);
    }
    if (includes.count > 0) {
        String_List incs = {0};
        string_list_init(&incs);
        split_semicolon_list(ctx, includes, &incs);
        for (size_t i = 0; i < incs.count; i++) {
            String_View inc = genex_trim(incs.items[i]);
            if (inc.count == 0) continue;
            if (nob_sv_starts_with(inc, sv_from_cstr("-I")) && inc.count > 2) {
                inc = nob_sv_from_parts(inc.data + 2, inc.count - 2);
            }
            if (inc.count > 0) build_target_add_include_directory(pkg_target, ctx->arena, inc, VISIBILITY_INTERFACE, CONFIG_ALL);
        }
    }
    if (cflags.count > 0) {
        String_List flags = {0};
        string_list_init(&flags);
        split_command_line_like_cmake(ctx, cflags, &flags);
        for (size_t i = 0; i < flags.count; i++) {
            String_View flag = genex_trim(flags.items[i]);
            if (flag.count == 0) continue;
            if (nob_sv_starts_with(flag, sv_from_cstr("-D")) && flag.count > 2) {
                build_target_add_definition(pkg_target, ctx->arena,
                    nob_sv_from_parts(flag.data + 2, flag.count - 2), VISIBILITY_INTERFACE, CONFIG_ALL);
                continue;
            }
            if (nob_sv_starts_with(flag, sv_from_cstr("-I")) && flag.count > 2) {
                build_target_add_include_directory(pkg_target, ctx->arena,
                    nob_sv_from_parts(flag.data + 2, flag.count - 2), VISIBILITY_INTERFACE, CONFIG_ALL);
                continue;
            }
            build_target_add_compile_option(pkg_target, ctx->arena, flag, VISIBILITY_INTERFACE, CONFIG_ALL);
        }
    }
    if (link_dirs.count > 0) {
        String_List dirs = {0};
        string_list_init(&dirs);
        split_semicolon_list(ctx, link_dirs, &dirs);
        for (size_t i = 0; i < dirs.count; i++) {
            String_View dir = genex_trim(dirs.items[i]);
            if (dir.count == 0) continue;
            if (nob_sv_starts_with(dir, sv_from_cstr("-L")) && dir.count > 2) {
                dir = nob_sv_from_parts(dir.data + 2, dir.count - 2);
            }
            if (dir.count > 0) build_target_add_link_directory(pkg_target, ctx->arena, dir, VISIBILITY_INTERFACE, CONFIG_ALL);
        }
    }
    if (ldflags.count > 0) {
        String_List flags = {0};
        string_list_init(&flags);
        split_command_line_like_cmake(ctx, ldflags, &flags);
        for (size_t i = 0; i < flags.count; i++) {
            String_View flag = genex_trim(flags.items[i]);
            if (flag.count == 0) continue;
            if (nob_sv_starts_with(flag, sv_from_cstr("-L")) && flag.count > 2) {
                String_View dir = nob_sv_from_parts(flag.data + 2, flag.count - 2);
                if (dir.count > 0) build_target_add_link_directory(pkg_target, ctx->arena, dir, VISIBILITY_INTERFACE, CONFIG_ALL);
                continue;
            }
            if (nob_sv_starts_with(flag, sv_from_cstr("-l")) && flag.count > 2) {
                String_View lib = nob_sv_from_parts(flag.data + 2, flag.count - 2);
                eval_append_link_library_item(ctx, pkg_target, VISIBILITY_INTERFACE, lib);
                continue;
            }
            build_target_add_link_option(pkg_target, ctx->arena, flag, VISIBILITY_INTERFACE, CONFIG_ALL);
        }
    }
}

static void configure_package_variables(Evaluator_Context *ctx,
                                        String_View name,
                                        bool found,
                                        String_View pkg_version,
                                        String_View pkg_dir,
                                        Find_Package_Source source,
                                        const Find_Package_Request *request) {
    const char *name_cstr = nob_temp_sv_to_cstr(name);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", name_cstr)),
        found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"), false, false);
    String_View name_upper = find_package_upper_sv_copy(ctx->arena, name);
    if (name_upper.count > 0 && !sv_eq_ci(name_upper, name)) {
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(name_upper))),
            found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"), false, false);
    }

    if (found && pkg_version.count > 0) {
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", name_cstr)), pkg_version, false, false);
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION_STRING", name_cstr)), pkg_version, false, false);
    }
    if (found && pkg_dir.count > 0) {
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_DIR", name_cstr)), pkg_dir, false, false);
    }

    char *config_var = nob_temp_sprintf("%s_CONFIG", name_cstr);
    String_View config_path = sv_from_cstr("");
    if (found && pkg_dir.count > 0) {
        config_path = cmk_path_join(ctx->arena, pkg_dir, sv_from_cstr(nob_temp_sprintf("%sConfig.cmake", name_cstr)));
    }
    eval_set_var(ctx, sv_from_cstr(config_var), config_path, false, false);

    String_Builder components_sb = {0};
    if (request) {
        for (size_t i = 0; i < request->required_components.count; i++) {
            if (components_sb.count > 0) sb_append(&components_sb, ';');
            String_View c = request->required_components.items[i];
            sb_append_buf(&components_sb, c.data, c.count);
        }
        for (size_t i = 0; i < request->optional_components.count; i++) {
            if (components_sb.count > 0) sb_append(&components_sb, ';');
            String_View c = request->optional_components.items[i];
            sb_append_buf(&components_sb, c.data, c.count);
        }
    }
    String_View find_components = sv_from_cstr(arena_strndup(ctx->arena, components_sb.items ? components_sb.items : "", components_sb.count));
    nob_sb_free(components_sb);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FIND_COMPONENTS", name_cstr)), find_components, false, false);

    if (request) {
        for (size_t i = 0; i < request->required_components.count; i++) {
            String_View c = request->required_components.items[i];
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_FIND_REQUIRED_%s", name_cstr, nob_temp_sv_to_cstr(c))),
                sv_from_cstr("TRUE"), false, false);
        }
        for (size_t i = 0; i < request->optional_components.count; i++) {
            String_View c = request->optional_components.items[i];
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_FIND_REQUIRED_%s", name_cstr, nob_temp_sv_to_cstr(c))),
                sv_from_cstr("FALSE"), false, false);
        }
    }

    if (!found) return;

    String_View libs = find_package_read_var(ctx, name, request ? &request->names : NULL, "LIBRARIES");
    String_View include_dirs = find_package_read_var(ctx, name, request ? &request->names : NULL, "INCLUDE_DIRS");
    if (include_dirs.count == 0) {
        include_dirs = find_package_read_var(ctx, name, request ? &request->names : NULL, "INCLUDE_DIR");
    }
    String_View cflags = find_package_read_var(ctx, name, request ? &request->names : NULL, "CFLAGS");
    String_View ldflags = find_package_read_var(ctx, name, request ? &request->names : NULL, "LDFLAGS");
    String_View link_dirs = find_package_read_var(ctx, name, request ? &request->names : NULL, "LINK_DIRECTORIES");
    if (link_dirs.count == 0) {
        link_dirs = find_package_read_var(ctx, name, request ? &request->names : NULL, "LIBRARY_DIRS");
    }

    String_View imported_iface = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, name_cstr));
    Build_Target *pkg_target = build_model_find_target(ctx->model, imported_iface);
    if (!pkg_target && (libs.count > 0 || include_dirs.count > 0 || cflags.count > 0 ||
                        ldflags.count > 0 || link_dirs.count > 0)) {
        pkg_target = build_model_add_target(ctx->model, imported_iface, TARGET_INTERFACE_LIB);
    }
    if (pkg_target) {
        find_package_apply_imported_target_usage(ctx, pkg_target, libs, include_dirs, cflags, ldflags, link_dirs);
        if (source == FIND_PACKAGE_SOURCE_CONFIG) {
            build_target_set_property(pkg_target, ctx->arena, sv_from_cstr("PACKAGE_RESOLUTION_SOURCE"), sv_from_cstr("config"));
        } else if (source == FIND_PACKAGE_SOURCE_MODULE) {
            build_target_set_property(pkg_target, ctx->arena, sv_from_cstr("PACKAGE_RESOLUTION_SOURCE"), sv_from_cstr("module"));
        } else if (source == FIND_PACKAGE_SOURCE_CACHE) {
            build_target_set_property(pkg_target, ctx->arena, sv_from_cstr("PACKAGE_RESOLUTION_SOURCE"), sv_from_cstr("cache"));
        }
    }

    if (request) {
        for (size_t i = 0; i < request->required_components.count; i++) {
            String_View c = request->required_components.items[i];
            String_View component_target_name = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, nob_temp_sv_to_cstr(c)));
            Build_Target *component_target = build_model_add_target(ctx->model, component_target_name, TARGET_INTERFACE_LIB);
            if (component_target) {
                if (imported_iface.count > 0) {
                    build_target_add_library(component_target, ctx->arena, imported_iface, VISIBILITY_INTERFACE);
                }
                find_package_apply_imported_target_usage(ctx, component_target, libs, include_dirs, cflags, ldflags, link_dirs);
            }
        }
        for (size_t i = 0; i < request->optional_components.count; i++) {
            String_View c = request->optional_components.items[i];
            String_View component_target_name = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, nob_temp_sv_to_cstr(c)));
            Build_Target *component_target = build_model_add_target(ctx->model, component_target_name, TARGET_INTERFACE_LIB);
            if (component_target) {
                if (imported_iface.count > 0) {
                    build_target_add_library(component_target, ctx->arena, imported_iface, VISIBILITY_INTERFACE);
                }
                find_package_apply_imported_target_usage(ctx, component_target, libs, include_dirs, cflags, ldflags, link_dirs);
            }
        }
    }
}

static void parse_find_package_request(Evaluator_Context *ctx, Args args, Find_Package_Request *out) {
    memset(out, 0, sizeof(*out));
    string_list_init(&out->required_components);
    string_list_init(&out->optional_components);
    string_list_init(&out->names);
    string_list_init(&out->hints);
    string_list_init(&out->paths);
    string_list_init(&out->path_suffixes);
    if (args.count < 2) return;

    size_t i = 1;
    String_View second = resolve_arg(ctx, args.items[1]);
    if (find_package_is_version_literal(second)) {
        out->requested_version = second;
        i = 2;
    }

    typedef enum Parse_Mode {
        PARSE_MODE_NONE = 0,
        PARSE_MODE_REQUIRED_COMPONENTS,
        PARSE_MODE_OPTIONAL_COMPONENTS,
        PARSE_MODE_NAMES,
        PARSE_MODE_HINTS,
        PARSE_MODE_PATHS,
        PARSE_MODE_PATH_SUFFIXES,
    } Parse_Mode;
    Parse_Mode mode = PARSE_MODE_NONE;
    for (; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("EXACT"))) {
            out->exact_version = true;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("REQUIRED"))) {
            out->required = true;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("OPTIONAL"))) {
            out->required = false;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("QUIET"))) {
            out->quiet = true;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("MODULE"))) {
            out->mode_module_only = true;
            out->mode_config_only = false;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("CONFIG")) || sv_eq_ci(arg, sv_from_cstr("NO_MODULE"))) {
            out->mode_config_only = true;
            out->mode_module_only = false;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("COMPONENTS"))) {
            mode = PARSE_MODE_REQUIRED_COMPONENTS;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("OPTIONAL_COMPONENTS"))) {
            mode = PARSE_MODE_OPTIONAL_COMPONENTS;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NAMES"))) {
            mode = PARSE_MODE_NAMES;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("HINTS"))) {
            mode = PARSE_MODE_HINTS;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("PATHS"))) {
            mode = PARSE_MODE_PATHS;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("PATH_SUFFIXES"))) {
            mode = PARSE_MODE_PATH_SUFFIXES;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NO_DEFAULT_PATH"))) {
            out->no_default_path = true;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_PATH"))) {
            out->no_cmake_path = true;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_ENVIRONMENT_PATH"))) {
            out->no_cmake_environment_path = true;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NO_SYSTEM_ENVIRONMENT_PATH"))) {
            out->no_system_environment_path = true;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("NO_CMAKE_SYSTEM_PATH"))) {
            out->no_cmake_system_path = true;
            mode = PARSE_MODE_NONE;
            continue;
        }
        if (find_package_is_keyword(arg)) {
            mode = PARSE_MODE_NONE;
            continue;
        }

        switch (mode) {
            case PARSE_MODE_REQUIRED_COMPONENTS:
                string_list_add_unique(&out->required_components, ctx->arena, arg);
                break;
            case PARSE_MODE_OPTIONAL_COMPONENTS:
                string_list_add_unique(&out->optional_components, ctx->arena, arg);
                break;
            case PARSE_MODE_NAMES:
                string_list_add_unique(&out->names, ctx->arena, arg);
                break;
            case PARSE_MODE_HINTS:
                string_list_add_unique(&out->hints, ctx->arena, arg);
                break;
            case PARSE_MODE_PATHS:
                string_list_add_unique(&out->paths, ctx->arena, arg);
                break;
            case PARSE_MODE_PATH_SUFFIXES:
                string_list_add_unique(&out->path_suffixes, ctx->arena, arg);
                break;
            default:
                break;
        }
    }
}

static void eval_find_package(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View name = resolve_arg(ctx, args.items[0]);
    const char *name_cstr = arena_strndup(ctx->arena, name.data, name.count);

    Find_Package_Request req = {0};
    parse_find_package_request(ctx, args, &req);
    if (req.names.count == 0) {
        string_list_add_unique(&req.names, ctx->arena, name);
    }

    String_View pkg_dir = sv_from_cstr("");
    Find_Package_Source source = FIND_PACKAGE_SOURCE_NONE;
    bool found = false;

    if (sv_eq_ci(name, sv_from_cstr("PkgConfig"))) {
        String_View exe = sv_from_cstr("");
        found = eval_pkgconfig_detect_executable(ctx, &exe);
        if (found) {
            eval_set_var(ctx, sv_from_cstr("PKG_CONFIG_EXECUTABLE"), exe, false, false);
            source = FIND_PACKAGE_SOURCE_CACHE;
        }
    }
    if (!found) {
        found = eval_resolve_package(ctx, name, &req, &pkg_dir, &source);
    }

    String_View package_version = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", name_cstr)));
    if (found && req.requested_version.count > 0) {
        if (package_version.count == 0) {
            found = false;
        } else {
        int cmp = find_package_compare_versions(package_version, req.requested_version);
        bool version_ok = req.exact_version ? (cmp == 0) : (cmp >= 0);
        if (!version_ok) found = false;
        }
    }

    if (found) {
        for (size_t i = 0; i < req.required_components.count; i++) {
            String_View component = req.required_components.items[i];
            bool comp_found = find_package_component_is_available(ctx, name, component);
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_%s_FOUND", name_cstr, nob_temp_sv_to_cstr(component))),
                comp_found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"),
                false, false);
            if (!comp_found) found = false;
        }
        for (size_t i = 0; i < req.optional_components.count; i++) {
            String_View component = req.optional_components.items[i];
            bool comp_found = find_package_component_is_available(ctx, name, component);
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_%s_FOUND", name_cstr, nob_temp_sv_to_cstr(component))),
                comp_found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"),
                false, false);
        }
    } else {
        for (size_t i = 0; i < req.required_components.count; i++) {
            String_View component = req.required_components.items[i];
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_%s_FOUND", name_cstr, nob_temp_sv_to_cstr(component))),
                sv_from_cstr("FALSE"), false, false);
        }
        for (size_t i = 0; i < req.optional_components.count; i++) {
            String_View component = req.optional_components.items[i];
            eval_set_var(ctx,
                sv_from_cstr(nob_temp_sprintf("%s_%s_FOUND", name_cstr, nob_temp_sv_to_cstr(component))),
                sv_from_cstr("FALSE"), false, false);
        }
    }

    if (!req.quiet) {
        nob_log(NOB_INFO, "Procurando pacote: "SV_Fmt" (source=%s)", SV_Arg(name), find_package_source_name(source));
    }

    build_model_add_package(ctx->model, name, found);
    if (!found && req.required) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_package",
            nob_temp_sprintf("pacote REQUIRED nao encontrado: %s", name_cstr),
            "forneca pacote/module no CMAKE_PREFIX_PATH/CMAKE_MODULE_PATH ou ajuste *_DIR");
        ctx->skip_evaluation = true;
    } else if (!found && !req.quiet) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "find_package",
            nob_temp_sprintf("pacote nao encontrado: %s", name_cstr),
            "resultado parcial: variaveis de package/componentes marcadas como ausentes");
    }

    configure_package_variables(ctx, name, found, package_version, pkg_dir, source, &req);
}

// ============================================================================
// AVALIAÃ‡ÃƒO DE NÃ“S DA AST
// ============================================================================


typedef struct {
    String_View subcommand;
    String_View package_name;
    String_View prefix;
    String_View requested_version;
    bool required;
    bool quiet;
    bool exact_version;
    bool imported_target;
    bool global_target;
} CMake_Pkg_Config_Request;

static bool cmake_pkg_config_is_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("EXTRACT")) ||
           sv_eq_ci(tok, sv_from_cstr("POPULATE")) ||
           sv_eq_ci(tok, sv_from_cstr("IMPORT")) ||
           sv_eq_ci(tok, sv_from_cstr("PACKAGE")) ||
           sv_eq_ci(tok, sv_from_cstr("MODULE")) ||
           sv_eq_ci(tok, sv_from_cstr("PREFIX")) ||
           sv_eq_ci(tok, sv_from_cstr("NAME")) ||
           sv_eq_ci(tok, sv_from_cstr("VERSION")) ||
           sv_eq_ci(tok, sv_from_cstr("EXACT")) ||
           sv_eq_ci(tok, sv_from_cstr("REQUIRED")) ||
           sv_eq_ci(tok, sv_from_cstr("QUIET")) ||
           sv_eq_ci(tok, sv_from_cstr("IMPORTED_TARGET")) ||
           sv_eq_ci(tok, sv_from_cstr("GLOBAL")) ||
           sv_eq_ci(tok, sv_from_cstr("COMPONENTS")) ||
           sv_eq_ci(tok, sv_from_cstr("OPTIONAL_COMPONENTS"));
}

static void cmake_pkg_config_split_flags(Evaluator_Context *ctx, String_View text, String_List *out_items) {
    if (!ctx || !out_items || text.count == 0) return;
    size_t start = 0;
    for (size_t i = 0; i <= text.count; i++) {
        bool at_sep = (i == text.count) || text.data[i] == ';' || isspace((unsigned char)text.data[i]);
        if (!at_sep) continue;
        if (i > start) {
            String_View item = genex_trim(nob_sv_from_parts(text.data + start, i - start));
            if (item.count > 0) string_list_add_unique(out_items, ctx->arena, item);
        }
        start = i + 1;
    }
}

static void parse_cmake_pkg_config_request(Evaluator_Context *ctx, Args args, CMake_Pkg_Config_Request *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->subcommand = sv_from_cstr("IMPORT");
    if (!ctx || args.count == 0) return;

    size_t i = 0;
    String_View first = resolve_arg(ctx, args.items[0]);
    if (sv_eq_ci(first, sv_from_cstr("EXTRACT")) ||
        sv_eq_ci(first, sv_from_cstr("POPULATE")) ||
        sv_eq_ci(first, sv_from_cstr("IMPORT"))) {
        out->subcommand = first;
        i = 1;
    }

    for (; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("REQUIRED"))) {
            out->required = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("QUIET"))) {
            out->quiet = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("EXACT"))) {
            out->exact_version = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("IMPORTED_TARGET"))) {
            out->imported_target = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("GLOBAL"))) {
            out->global_target = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("PREFIX")) && i + 1 < args.count) {
            out->prefix = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("NAME")) && i + 1 < args.count) {
            out->prefix = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("VERSION")) && i + 1 < args.count) {
            out->requested_version = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if ((sv_eq_ci(tok, sv_from_cstr("PACKAGE")) || sv_eq_ci(tok, sv_from_cstr("MODULE"))) && i + 1 < args.count) {
            out->package_name = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("COMPONENTS")) || sv_eq_ci(tok, sv_from_cstr("OPTIONAL_COMPONENTS"))) {
            while (i + 1 < args.count) {
                String_View next = resolve_arg(ctx, args.items[i + 1]);
                if (cmake_pkg_config_is_keyword(next)) break;
                i++;
            }
            continue;
        }
        if (!cmake_pkg_config_is_keyword(tok) && out->package_name.count == 0) {
            out->package_name = tok;
        }
    }

    if (out->prefix.count == 0) out->prefix = out->package_name;
}

static void cmake_pkg_config_configure_variables(Evaluator_Context *ctx,
                                                 const CMake_Pkg_Config_Request *req,
                                                 bool found,
                                                 String_View libs,
                                                 String_View include_dirs,
                                                 String_View cflags,
                                                 String_View ldflags,
                                                 String_View version) {
    if (!ctx || !req || req->prefix.count == 0) return;
    const char *prefix_c = nob_temp_sv_to_cstr(req->prefix);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", prefix_c)),
        found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", prefix_c)),
        found ? version : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LIBRARIES", prefix_c)),
        found ? libs : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_INCLUDE_DIRS", prefix_c)),
        found ? include_dirs : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_CFLAGS", prefix_c)),
        found ? cflags : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LDFLAGS", prefix_c)),
        found ? ldflags : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_MODULE_NAME", prefix_c)),
        req->package_name, false, false);
}

static void cmake_pkg_config_create_imported_target(Evaluator_Context *ctx, const CMake_Pkg_Config_Request *req, bool found, String_View libs, String_View include_dirs, String_View cflags, String_View ldflags) {
    (void)ldflags;
    if (!ctx || !ctx->model || !req || !found || !req->imported_target || req->prefix.count == 0) return;

    String_View target_name = sv_from_cstr(nob_temp_sprintf("PkgConfig::%s", nob_temp_sv_to_cstr(req->prefix)));
    Build_Target *target = build_model_add_target(ctx->model, target_name, TARGET_INTERFACE_LIB);
    if (!target) return;
    if (req->global_target) {
        build_target_set_property(target, ctx->arena, sv_from_cstr("IMPORTED_GLOBAL"), sv_from_cstr("TRUE"));
    }

    String_List lib_items = {0};
    String_List include_items = {0};
    String_List cflag_items = {0};
    string_list_init(&lib_items);
    string_list_init(&include_items);
    string_list_init(&cflag_items);
    cmake_pkg_config_split_flags(ctx, libs, &lib_items);
    cmake_pkg_config_split_flags(ctx, include_dirs, &include_items);
    cmake_pkg_config_split_flags(ctx, cflags, &cflag_items);

    for (size_t i = 0; i < lib_items.count; i++) {
        String_View lib = lib_items.items[i];
        if (lib.count > 2 && nob_sv_starts_with(lib, sv_from_cstr("-l"))) {
            lib = nob_sv_from_parts(lib.data + 2, lib.count - 2);
        }
        if (lib.count > 2 && nob_sv_starts_with(lib, sv_from_cstr("-L"))) {
            String_View dir = nob_sv_from_parts(lib.data + 2, lib.count - 2);
            if (dir.count > 0) build_target_add_link_directory(target, ctx->arena, dir, VISIBILITY_INTERFACE, CONFIG_ALL);
            continue;
        }
        if (lib.count > 0) build_target_add_library(target, ctx->arena, lib, VISIBILITY_INTERFACE);
    }
    for (size_t i = 0; i < include_items.count; i++) {
        String_View inc = include_items.items[i];
        if (inc.count > 2 && nob_sv_starts_with(inc, sv_from_cstr("-I"))) {
            inc = nob_sv_from_parts(inc.data + 2, inc.count - 2);
        }
        if (inc.count > 0) build_target_add_include_directory(target, ctx->arena, inc, VISIBILITY_INTERFACE, CONFIG_ALL);
    }
    for (size_t i = 0; i < cflag_items.count; i++) {
        String_View flag = cflag_items.items[i];
        if (flag.count > 2 && nob_sv_starts_with(flag, sv_from_cstr("-I"))) continue;
        if (flag.count > 2 && nob_sv_starts_with(flag, sv_from_cstr("-D"))) {
            String_View def = nob_sv_from_parts(flag.data + 2, flag.count - 2);
            if (def.count > 0) build_target_add_definition(target, ctx->arena, def, VISIBILITY_INTERFACE, CONFIG_ALL);
            continue;
        }
        if (flag.count > 0) build_target_add_compile_option(target, ctx->arena, flag, VISIBILITY_INTERFACE, CONFIG_ALL);
    }
}

static void eval_cmake_pkg_config_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count == 0) return;

    CMake_Pkg_Config_Request req = {0};
    parse_cmake_pkg_config_request(ctx, args, &req);
    if (req.package_name.count == 0) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cmake_pkg_config",
            "assinatura invalida",
            "use cmake_pkg_config([IMPORT|EXTRACT|POPULATE] <package> [PREFIX <name>] [IMPORTED_TARGET] [REQUIRED] [QUIET])");
        return;
    }

    String_List modules = {0};
    string_list_init(&modules);
    eval_pkgconfig_add_module_specs(ctx, req.package_name, &modules);
    if (modules.count == 0) {
        string_list_add(&modules, ctx->arena, req.package_name);
    }

    String_View executable = sv_from_cstr("");
    bool has_pkgconfig = eval_pkgconfig_detect_executable(ctx, &executable);
    bool found = has_pkgconfig && eval_pkgconfig_run(ctx, executable, &modules, sv_from_cstr("--exists"), false, NULL);
    String_View found_var = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(req.prefix))));
    if (!found && found_var.count > 0 && !cmake_string_is_false(found_var)) found = true;

    String_View version = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", nob_temp_sv_to_cstr(req.prefix))));
    String_View libs_raw = sv_from_cstr("");
    String_View cflags_raw = sv_from_cstr("");
    String_View include_dirs_raw = sv_from_cstr("");
    if (found && has_pkgconfig) {
        if (version.count == 0) {
            (void)eval_pkgconfig_run(ctx, executable, &modules, sv_from_cstr("--modversion"), true, &version);
        }
        (void)eval_pkgconfig_run(ctx, executable, &modules, sv_from_cstr("--libs"), true, &libs_raw);
        (void)eval_pkgconfig_run(ctx, executable, &modules, sv_from_cstr("--cflags"), true, &cflags_raw);
        (void)eval_pkgconfig_run(ctx, executable, &modules, sv_from_cstr("--cflags-only-I"), true, &include_dirs_raw);
    }
    if (req.requested_version.count > 0) {
        if (version.count == 0) {
            found = false;
        } else {
        int cmp = find_package_compare_versions(version, req.requested_version);
        bool version_ok = req.exact_version ? (cmp == 0) : (cmp >= 0);
        if (!version_ok) found = false;
        }
    }

    String_View libs = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LIBRARIES", nob_temp_sv_to_cstr(req.prefix))));
    if (libs.count == 0) libs = eval_pkgconfig_flags_to_semicolon_list(ctx, libs_raw, sv_from_cstr(""));
    String_View include_dirs = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_INCLUDE_DIRS", nob_temp_sv_to_cstr(req.prefix))));
    if (include_dirs.count == 0) include_dirs = eval_pkgconfig_flags_to_semicolon_list(ctx, include_dirs_raw, sv_from_cstr("-I"));
    String_View cflags = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_CFLAGS", nob_temp_sv_to_cstr(req.prefix))));
    if (cflags.count == 0) cflags = eval_pkgconfig_flags_to_semicolon_list(ctx, cflags_raw, sv_from_cstr(""));
    String_View ldflags = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LDFLAGS", nob_temp_sv_to_cstr(req.prefix))));
    if (ldflags.count == 0 && libs.count > 0) ldflags = libs;

    if (!req.quiet) {
        nob_log(NOB_INFO, "Resolvendo cmake_pkg_config para pacote: "SV_Fmt, SV_Arg(req.package_name));
    }

    build_model_add_package(ctx->model, req.package_name, found);
    if (!found && req.required) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cmake_pkg_config",
            nob_temp_sprintf("pacote REQUIRED nao encontrado: "SV_Fmt, SV_Arg(req.package_name)),
            "instale o modulo .pc ou ajuste PKG_CONFIG_PATH");
    } else if (!found && !req.quiet) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_pkg_config",
            nob_temp_sprintf("pacote nao encontrado: "SV_Fmt, SV_Arg(req.package_name)),
            "resultado parcial: variaveis <PREFIX>_* marcadas como ausentes");
    }

    cmake_pkg_config_configure_variables(ctx, &req, found, libs, include_dirs, cflags, ldflags, version);
    cmake_pkg_config_create_imported_target(ctx, &req, found, libs, include_dirs, cflags, ldflags);
}

typedef void (*Eval_Command_Handler)(Evaluator_Context *ctx, Args args);

typedef enum {
    COMMAND_SPEC_FLAG_NONE = 0u,
    COMMAND_SPEC_FLAG_COMPAT_NOOP = 1u << 0,
} Command_Spec_Flags;

typedef struct {
    const char *name;
    Eval_Command_Handler handler;
    size_t min_args;
    size_t max_args; // 0 => sem limite explicito
    unsigned flags;
} Command_Spec;

static bool eval_command_spec_arity_ok(const Command_Spec *spec, Args args) {
    if (!spec) return false;
    if (spec->min_args > 0 && args.count < spec->min_args) return false;
    if (spec->max_args > 0 && args.count > spec->max_args) return false;
    return true;
}

static bool eval_dispatch_known_command(Evaluator_Context *ctx, String_View cmd_name, Args args) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    static const Command_Spec table[] = {
        {"set", eval_set_command},
        {"unset", eval_unset_command},
        {"option", eval_option_command},
        {"cmake_dependent_option", eval_cmake_dependent_option_command},
        {"check_symbol_exists", eval_check_symbol_exists_command},
        {"check_function_exists", eval_check_function_exists_command},
        {"check_include_file", eval_check_include_file_command},
        {"check_include_files", eval_check_include_files_command},
        {"check_type_size", eval_check_type_size_command},
        {"check_c_compiler_flag", eval_check_c_compiler_flag_command},
        {"check_struct_has_member", eval_check_struct_has_member_command},
        {"check_c_source_compiles", eval_check_c_source_compiles_command},
        {"check_library_exists", eval_check_library_exists_command},
        {"check_c_source_runs", eval_check_c_source_runs_command},
        {"try_compile", eval_try_compile_command},
        {"try_run", eval_try_run_command},
        {"execute_process", eval_execute_process_command},
        {"exec_program", eval_exec_program_command},
        {"load_command", eval_load_command_command},
        {"cmake_push_check_state", eval_cmake_push_check_state_command},
        {"cmake_pop_check_state", eval_cmake_pop_check_state_command},
        {"cmake_file_api", eval_cmake_file_api_command},
        {"cmake_instrumentation", eval_cmake_instrumentation_command},
        {"cmake_pkg_config", eval_cmake_pkg_config_command},
        {"pkg_check_modules", eval_pkg_check_modules_command},
        {"pkg_search_module", eval_pkg_search_module_command},
        {"find_package_handle_standard_args", eval_find_package_handle_standard_args_command},
        {"cmake_minimum_required", eval_cmake_minimum_required_command},
        {"project", eval_project_command},
        {"add_executable", eval_add_executable_command},
        {"add_library", eval_add_library_command},
        {"add_custom_target", eval_add_custom_target_command},
        {"add_compile_definitions", eval_add_compile_definitions_command},
        {"add_definitions", eval_add_definitions_command},
        {"aux_source_directory", eval_aux_source_directory_command},
        {"create_test_sourcelist", eval_create_test_sourcelist_command},
        {"add_compile_options", eval_add_compile_options_command},
        {"add_link_options", eval_add_link_options_command},
        {"remove", eval_remove_command},
        {"write_file", eval_write_file_command},
        {"variable_requires", eval_variable_requires_command},
        {"remove_definitions", eval_remove_definitions_command},
        {"include_external_msproject", eval_include_external_msproject_command},
        {"fltk_wrap_ui", eval_fltk_wrap_ui_command},
        {"qt_wrap_cpp", eval_qt_wrap_cpp_command},
        {"qt_wrap_ui", eval_qt_wrap_ui_command},
        {"include_directories", eval_include_directories_command},
        {"link_directories", eval_link_directories_command},
        {"link_libraries", eval_link_libraries_command},
        {"target_include_directories", eval_target_include_directories_command},
        {"target_link_libraries", eval_target_link_libraries_command},
        {"target_link_options", eval_target_link_options_command},
        {"target_link_directories", eval_target_link_directories_command},
        {"add_dependencies", eval_add_dependencies_command},
        {"set_target_properties", eval_set_target_properties_command},
        {"set_source_files_properties", eval_set_source_files_properties_command},
        {"get_source_file_property", eval_get_source_file_property_command},
        {"define_property", eval_define_property_command},
        {"set_property", eval_set_property_command},
        {"get_property", eval_get_property_command},
        {"get_cmake_property", eval_get_cmake_property_command},
        {"get_target_property", eval_get_target_property_command},
        {"mark_as_advanced", eval_mark_as_advanced_command},
        {"target_compile_definitions", eval_target_compile_definitions_command},
        {"target_compile_options", eval_target_compile_options_command},
        {"target_compile_features", eval_target_compile_features_command},
        {"target_precompile_headers", eval_target_precompile_headers_command},
        {"target_sources", eval_target_sources_command},
        {"message", eval_message_command},
        {"list", eval_list_command},
        {"string", eval_string_command},
        {"math", eval_math_command},
        {"cmake_path", eval_cmake_path_command},
        {"get_filename_component", eval_get_filename_component_command},
        {"add_custom_command", eval_add_custom_command},
        {"include", eval_include_command},
        {"enable_testing", eval_enable_testing_command},
        {"add_test", eval_add_test_command},
        {"set_tests_properties", eval_set_tests_properties_command},
        {"ctest_start", eval_ctest_start_command},
        {"ctest_update", eval_ctest_update_command},
        {"ctest_configure", eval_ctest_configure_command},
        {"ctest_build", eval_ctest_build_command},
        {"ctest_test", eval_ctest_test_command},
        {"ctest_coverage", eval_ctest_coverage_command},
        {"ctest_coverage_collect_gcov", eval_ctest_coverage_collect_gcov_command},
        {"ctest_memcheck", eval_ctest_memcheck_command},
        {"ctest_submit", eval_ctest_submit_command},
        {"ctest_upload", eval_ctest_upload_command},
        {"ctest_run_script", eval_ctest_run_script_command},
        {"ctest_read_custom_files", eval_ctest_read_custom_files_command},
        {"ctest_sleep", eval_ctest_sleep_command},
        {"ctest_empty_binary_directory", eval_ctest_empty_binary_directory_command},
        {"cpack_add_component", eval_cpack_add_component_command},
        {"cpack_add_component_group", eval_cpack_add_component_group_command},
        {"cpack_add_install_type", eval_cpack_add_install_type_command},
        {"cpack_ifw_configure_file", eval_cpack_ifw_configure_file_command},
        {"configure_file", eval_configure_file_command},
        {"file", eval_file_command},
        {"make_directory", eval_make_directory_command},
        {"use_mangled_mesa", eval_use_mangled_mesa_command},
        {"add_subdirectory", eval_add_subdirectory},
        {"subdirs", eval_subdirs_command},
        {"install", eval_install_command},
        {"build_command", eval_build_command_command},
        {"find_package", eval_find_package},
        {"find_file", eval_find_file_command},
        {"find_path", eval_find_path_command},
        {"find_program", eval_find_program_command},
        {"find_library", eval_find_library_command},
        {"export", eval_export_command},
        {"write_basic_package_version_file", eval_write_basic_package_version_file_command},
        {"configure_package_config_file", eval_configure_package_config_file_command},
        {"include_regular_expression", eval_include_regular_expression_command},
        {"enable_language", eval_enable_language_command},
        {"site_name", eval_site_name_command},
        {"set_directory_properties", eval_set_directory_properties_command},
        {"get_directory_property", eval_get_directory_property_command},
        {"get_test_property", eval_get_test_property_command},
        {"separate_arguments", eval_separate_arguments_command},
    };
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (sv_eq_ci(cmd_name, sv_from_cstr(table[i].name))) {
            if (!eval_command_spec_arity_ok(&table[i], args)) {
                diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, table[i].name,
                    "assinatura invalida para o comando",
                    "ajuste o numero de argumentos ou atualize a especificacao do comando");
                return true;
            }
            table[i].handler(ctx, args);
            return true;
        }
    }
    return false;
}

static bool eval_is_compat_noop_command(String_View cmd_name) {
    static const char *noop_commands[] = {
        // Fluxo/meta da linguagem que nao altera command line de build diretamente
        "include_guard",
        "block",
        "endblock",
        "cmake_policy",
        "cmake_language",
        "cmake_host_system_information",
        "cmake_parse_arguments",

        // Organizacao de IDE / metadados
        "source_group",
        "use_folders",

        // Comandos legados/auxiliares raros
        "build_name",
        "export_library_dependencies",
        "load_cache",
        "variable_watch",
        "subdir_depends",
        "utility_source",
        "output_required_files",
        "install_files",
        "install_programs",
        "install_targets",
    };

    for (size_t i = 0; i < sizeof(noop_commands) / sizeof(noop_commands[0]); i++) {
        if (sv_eq_ci(cmd_name, sv_from_cstr(noop_commands[i]))) return true;
    }
    return false;
}



// Avalia um único nó da AST
static void eval_node(Evaluator_Context *ctx, Node node) {
    // Se estamos pulando avaliação (em bloco condicional falso)
    if (ctx->skip_evaluation && node.kind != NODE_IF) {
        return;
    }

    switch (node.kind) {
        case NODE_COMMAND: {
            // Limpeza fácil: tudo que for nob_temp_* durante este comando
            // é descartado ao final do comando (sem afetar escopos externos).
            size_t temp_mark = nob_temp_save();

            String_View cmd_name = node.as.cmd.name;

            if (eval_handle_flow_control_command(ctx, cmd_name)) {
                nob_temp_rewind(temp_mark);
                break;
            }

            if (eval_dispatch_known_command(ctx, cmd_name, node.as.cmd.args)) {
                // Comando reconhecido e executado.
            } else if (eval_is_compat_noop_command(cmd_name)) {
                // no-op de compatibilidade.
            } else if (eval_invoke_function(ctx, cmd_name, node.as.cmd.args)) {
                // Função executada.
            } else if (eval_invoke_macro(ctx, cmd_name, node.as.cmd.args)) {
                // Macro executada.
            } else {
                diag_telemetry_record_unsupported_sv(cmd_name);
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0,
                    nob_temp_sv_to_cstr(cmd_name),
                    "comando nao suportado pelo evaluator atual",
                    "adicione mapeamento no dispatcher para suportar este comando");
            }

            nob_temp_rewind(temp_mark);
            break;
        }

        case NODE_IF:
            eval_if_statement(ctx, &node);
            break;
        case NODE_FOREACH:
            eval_foreach_statement(ctx, &node);
            break;
        case NODE_WHILE:
            eval_while_statement(ctx, &node);
            break;
        case NODE_FUNCTION:
            eval_register_function(ctx, &node);
            break;
        case NODE_MACRO:
            eval_register_macro(ctx, &node);
            break;
    }
}
