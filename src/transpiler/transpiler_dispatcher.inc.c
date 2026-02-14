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
    String_View requested_version;
    String_List required_components;
    String_List optional_components;
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

static bool infer_package_link_name(String_View name, String_View *out_link_name) {
    if (!out_link_name) return false;
    *out_link_name = name;
    if (sv_eq_ci(name, sv_from_cstr("ZLIB"))) {
        *out_link_name = sv_from_cstr("z");
        return true;
    }
    if (sv_eq_ci(name, sv_from_cstr("Threads"))) {
        *out_link_name = sv_from_cstr("pthread");
        return true;
    }
    if (sv_eq_ci(name, sv_from_cstr("OpenGL"))) {
        *out_link_name = sv_from_cstr("GL");
        return true;
    }
    if (sv_eq_ci(name, sv_from_cstr("SDL2"))) {
        *out_link_name = sv_from_cstr("SDL2");
        return true;
    }
    return false;
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

static String_View infer_default_package_version(String_View name) {
    if (sv_eq_ci(name, sv_from_cstr("ZLIB"))) return sv_from_cstr("1.2.13");
    if (sv_eq_ci(name, sv_from_cstr("OpenGL"))) return sv_from_cstr("4.6");
    if (sv_eq_ci(name, sv_from_cstr("Threads"))) return sv_from_cstr("1.0");
    if (sv_eq_ci(name, sv_from_cstr("SDL2"))) return sv_from_cstr("2.30.0");
    return sv_from_cstr("1.0.0");
}

static bool eval_resolve_package_module(Evaluator_Context *ctx, String_View name, String_View *out_link_name) {
    if (!ctx || !ctx->model) return false;
    String_View link_name = name;
    bool found = infer_package_link_name(name, &link_name);
    String_View imported_iface = sv_from_cstr(nob_temp_sprintf("%s::%s", nob_temp_sv_to_cstr(name), nob_temp_sv_to_cstr(name)));
    if (!found && build_model_find_target(ctx->model, imported_iface)) found = true;
    if (out_link_name) *out_link_name = link_name;
    return found;
}

static bool eval_resolve_package_config(Evaluator_Context *ctx, String_View name, String_View *out_dir) {
    String_View dir_var = sv_from_cstr(nob_temp_sprintf("%s_DIR", nob_temp_sv_to_cstr(name)));
    String_View dir = eval_get_var(ctx, dir_var);
    if (out_dir) *out_dir = dir;
    if (dir.count > 0 && !cmake_string_is_false(dir)) return true;

    String_View registry_var = eval_get_var(ctx, sv_from_cstr("CMAKE_EXPORT_PACKAGE_REGISTRY"));
    if (registry_var.count > 0) {
        String_List entries = {0};
        string_list_init(&entries);
        split_semicolon_list(ctx, registry_var, &entries);
        for (size_t i = 0; i < entries.count; i++) {
            if (sv_eq_ci(entries.items[i], name)) return true;
        }
    }
    return false;
}

static bool eval_resolve_package(Evaluator_Context *ctx,
                                 String_View name,
                                 const Find_Package_Request *request,
                                 String_View *out_link_name,
                                 String_View *out_dir) {
    String_View link_name = name;
    String_View pkg_dir = sv_from_cstr("");
    bool prefer_config = false;
    if (ctx) {
        String_View pref = eval_get_var(ctx, sv_from_cstr("CMAKE_FIND_PACKAGE_PREFER_CONFIG"));
        prefer_config = sv_bool_is_true(pref);
    }

    bool config_first = prefer_config || (request && request->mode_config_only);
    bool found = false;
    if (request && request->mode_module_only) {
        found = eval_resolve_package_module(ctx, name, &link_name);
    } else if (request && request->mode_config_only) {
        found = eval_resolve_package_config(ctx, name, &pkg_dir);
    } else if (config_first) {
        found = eval_resolve_package_config(ctx, name, &pkg_dir);
        if (!found) found = eval_resolve_package_module(ctx, name, &link_name);
    } else {
        found = eval_resolve_package_module(ctx, name, &link_name);
        if (!found) found = eval_resolve_package_config(ctx, name, &pkg_dir);
    }

    if (!found && ctx) {
        String_View found_var = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(name))));
        if (found_var.count > 0 && !cmake_string_is_false(found_var)) {
            found = true;
        }
    }
    if (out_link_name) *out_link_name = link_name;
    if (out_dir) *out_dir = pkg_dir;
    return found;
}

static void configure_package_variables(Evaluator_Context *ctx,
                                        String_View name,
                                        bool found,
                                        String_View link_name,
                                        String_View pkg_version,
                                        String_View pkg_dir,
                                        const Find_Package_Request *request) {
    const char *name_cstr = nob_temp_sv_to_cstr(name);
    char *found_var = nob_temp_sprintf("%s_FOUND", name_cstr);
    eval_set_var(ctx, sv_from_cstr(found_var), found ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"), false, false);

    char *libs_var = nob_temp_sprintf("%s_LIBRARIES", name_cstr);
    eval_set_var(ctx, sv_from_cstr(libs_var), found ? link_name : sv_from_cstr(""), false, false);

    char *inc_var = nob_temp_sprintf("%s_INCLUDE_DIRS", name_cstr);
    eval_set_var(ctx, sv_from_cstr(inc_var), sv_from_cstr(""), false, false);

    char *version_var = nob_temp_sprintf("%s_VERSION", name_cstr);
    char *version_string_var = nob_temp_sprintf("%s_VERSION_STRING", name_cstr);
    eval_set_var(ctx, sv_from_cstr(version_var), found ? pkg_version : sv_from_cstr(""), false, false);
    eval_set_var(ctx, sv_from_cstr(version_string_var), found ? pkg_version : sv_from_cstr(""), false, false);

    char *dir_var = nob_temp_sprintf("%s_DIR", name_cstr);
    eval_set_var(ctx, sv_from_cstr(dir_var), found ? pkg_dir : sv_from_cstr(""), false, false);
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

    String_View imported_iface = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, name_cstr));
    Build_Target *pkg_target = build_model_add_target(ctx->model, imported_iface, TARGET_INTERFACE_LIB);
    if (pkg_target && link_name.count > 0) {
        build_target_add_library(pkg_target, ctx->arena, link_name, VISIBILITY_INTERFACE);
    }

    if (request) {
        for (size_t i = 0; i < request->required_components.count; i++) {
            String_View c = request->required_components.items[i];
            String_View component_target_name = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, nob_temp_sv_to_cstr(c)));
            Build_Target *component_target = build_model_add_target(ctx->model, component_target_name, TARGET_INTERFACE_LIB);
            if (component_target && link_name.count > 0) {
                build_target_add_library(component_target, ctx->arena, link_name, VISIBILITY_INTERFACE);
            }
        }
        for (size_t i = 0; i < request->optional_components.count; i++) {
            String_View c = request->optional_components.items[i];
            String_View component_target_name = sv_from_cstr(nob_temp_sprintf("%s::%s", name_cstr, nob_temp_sv_to_cstr(c)));
            Build_Target *component_target = build_model_add_target(ctx->model, component_target_name, TARGET_INTERFACE_LIB);
            if (component_target && link_name.count > 0) {
                build_target_add_library(component_target, ctx->arena, link_name, VISIBILITY_INTERFACE);
            }
        }
    }
}

static void parse_find_package_request(Evaluator_Context *ctx, Args args, Find_Package_Request *out) {
    memset(out, 0, sizeof(*out));
    string_list_init(&out->required_components);
    string_list_init(&out->optional_components);
    if (args.count < 2) return;

    size_t i = 1;
    String_View second = resolve_arg(ctx, args.items[1]);
    if (find_package_is_version_literal(second)) {
        out->requested_version = second;
        i = 2;
    }

    bool parse_required_components = false;
    bool parse_optional_components = false;
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
        if (sv_eq_ci(arg, sv_from_cstr("QUIET"))) {
            out->quiet = true;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("MODULE"))) {
            out->mode_module_only = true;
            out->mode_config_only = false;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("CONFIG")) || sv_eq_ci(arg, sv_from_cstr("NO_MODULE"))) {
            out->mode_config_only = true;
            out->mode_module_only = false;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("COMPONENTS"))) {
            parse_required_components = true;
            parse_optional_components = false;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("OPTIONAL_COMPONENTS"))) {
            parse_optional_components = true;
            parse_required_components = false;
            continue;
        }
        if (find_package_is_keyword(arg)) {
            parse_required_components = false;
            parse_optional_components = false;
            continue;
        }

        if (parse_required_components) {
            string_list_add_unique(&out->required_components, ctx->arena, arg);
            continue;
        }
        if (parse_optional_components) {
            string_list_add_unique(&out->optional_components, ctx->arena, arg);
            continue;
        }
    }
}

static void eval_find_package(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View name = resolve_arg(ctx, args.items[0]);
    const char *name_cstr = arena_strndup(ctx->arena, name.data, name.count);

    Find_Package_Request req = {0};
    parse_find_package_request(ctx, args, &req);

    String_View link_name = name;
    String_View pkg_dir = sv_from_cstr("");
    bool found = eval_resolve_package(ctx, name, &req, &link_name, &pkg_dir);

    String_View package_version = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", name_cstr)));
    if (package_version.count == 0) package_version = infer_default_package_version(name);
    if (found && req.requested_version.count > 0) {
        int cmp = find_package_compare_versions(package_version, req.requested_version);
        bool version_ok = req.exact_version ? (cmp == 0) : (cmp >= 0);
        if (!version_ok) found = false;
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
        nob_log(NOB_INFO, "Procurando pacote: "SV_Fmt, SV_Arg(name));
    }

    build_model_add_package(ctx->model, name, found);
    if (!found && req.required) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_package",
            nob_temp_sprintf("pacote REQUIRED nao encontrado: %s", name_cstr),
            "use find_package(<Pkg> REQUIRED) apenas para pacotes resolvidos pelo evaluator atual");
    } else if (!found && !req.quiet) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "find_package",
            nob_temp_sprintf("pacote nao encontrado: %s", name_cstr),
            "resultado parcial: variaveis de package/componentes foram marcadas como ausentes");
    }

    configure_package_variables(ctx, name, found, link_name, package_version, pkg_dir, &req);
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

    String_View inferred_link = req.package_name;
    bool found = infer_package_link_name(req.package_name, &inferred_link);
    String_View found_var = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(req.prefix))));
    if (!found && found_var.count > 0 && !cmake_string_is_false(found_var)) found = true;

    String_View version = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_VERSION", nob_temp_sv_to_cstr(req.prefix))));
    if (version.count == 0) version = infer_default_package_version(req.package_name);
    if (req.requested_version.count > 0) {
        int cmp = find_package_compare_versions(version, req.requested_version);
        bool version_ok = req.exact_version ? (cmp == 0) : (cmp >= 0);
        if (!version_ok) found = false;
    }

    String_View libs = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LIBRARIES", nob_temp_sv_to_cstr(req.prefix))));
    if (libs.count == 0 && found) libs = inferred_link;
    String_View include_dirs = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_INCLUDE_DIRS", nob_temp_sv_to_cstr(req.prefix))));
    String_View cflags = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_CFLAGS", nob_temp_sv_to_cstr(req.prefix))));
    String_View ldflags = eval_get_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_LDFLAGS", nob_temp_sv_to_cstr(req.prefix))));
    if (ldflags.count == 0 && libs.count > 0) ldflags = libs;

    if (!req.quiet) {
        nob_log(NOB_INFO, "Resolvendo cmake_pkg_config para pacote: "SV_Fmt, SV_Arg(req.package_name));
    }

    build_model_add_package(ctx->model, req.package_name, found);
    if (!found && req.required) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cmake_pkg_config",
            nob_temp_sprintf("pacote REQUIRED nao encontrado: "SV_Fmt, SV_Arg(req.package_name)),
            "defina <PREFIX>_FOUND/<PREFIX>_LIBRARIES ou use um pacote conhecido pelo fallback");
    } else if (!found && !req.quiet) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_pkg_config",
            nob_temp_sprintf("pacote nao encontrado: "SV_Fmt, SV_Arg(req.package_name)),
            "resultado parcial: variaveis <PREFIX>_* marcadas como ausentes");
    }

    cmake_pkg_config_configure_variables(ctx, &req, found, libs, include_dirs, cflags, ldflags, version);
    cmake_pkg_config_create_imported_target(ctx, &req, found, libs, include_dirs, cflags, ldflags);
}
typedef void (*Eval_Command_Handler)(Evaluator_Context *ctx, Args args);

typedef struct {
    const char *name;
    Eval_Command_Handler handler;
} Eval_Command_Dispatch_Entry;

static bool eval_dispatch_known_command(Evaluator_Context *ctx, String_View cmd_name, Args args) {
    static const Eval_Command_Dispatch_Entry table[] = {
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

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (sv_eq_ci(cmd_name, sv_from_cstr(table[i].name))) {
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
