
// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

static String_View target_property_for_config(Build_Target *target, Build_Config cfg, const char *base_key, String_View fallback);
static String_View eval_get_target_property_value(Evaluator_Context *ctx, Build_Target *target, String_View prop_name);
static String_View join_string_list_with_semicolon(Evaluator_Context *ctx, const String_List *list);

static bool sv_eq_ci(String_View a, String_View b);
typedef enum Loop_Flow_Signal {
    LOOP_FLOW_NONE = 0,
    LOOP_FLOW_RETURN,
    LOOP_FLOW_BREAK,
    LOOP_FLOW_CONTINUE,
} Loop_Flow_Signal;

static String_View genex_trim(String_View sv) {
    size_t begin = 0;
    while (begin < sv.count && isspace((unsigned char)sv.data[begin])) begin++;
    size_t end = sv.count;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;
    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static bool sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    String_View tail = nob_sv_from_parts(sv.data + (sv.count - suffix.count), suffix.count);
    return sv_eq_ci(tail, suffix);
}

static bool sv_is_i64_literal(String_View sv) {
    if (sv.count == 0) return false;
    size_t i = 0;
    if (sv.data[i] == '+' || sv.data[i] == '-') {
        i++;
        if (i >= sv.count) return false;
    }
    for (; i < sv.count; i++) {
        if (!isdigit((unsigned char)sv.data[i])) return false;
    }
    return true;
}

static bool cmake_string_is_false(String_View value) {
    String_View v = genex_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, sv_from_cstr("0"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("FALSE"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("OFF"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("NO"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("N"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("IGNORE"))) return true;
    if (sv_eq_ci(v, sv_from_cstr("NOTFOUND"))) return true;
    if (sv_ends_with_ci(v, sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

static String_View genex_target_property_cb(void *ud, Build_Target *target, String_View prop_name) {
    Evaluator_Context *ctx = (Evaluator_Context *)ud;
    return eval_get_target_property_value(ctx, target, prop_name);
}

static String_View eval_generator_expression(Evaluator_Context *ctx, String_View content) {
    if (!ctx || !ctx->model) return sv_from_cstr("");

    Genex_Eval_Context genex = {0};
    genex.arena = ctx->arena;
    genex.model = ctx->model;
    genex.default_config = ctx->model->default_config;
    genex.get_target_property = genex_target_property_cb;
    genex.userdata = ctx;
    return genex_evaluate(&genex, content);
}



// Protótipos para funções estáticas
static void eval_node(Evaluator_Context *ctx, Node node);
static void resolve_args_to_list(Evaluator_Context *ctx, Args args, size_t start_idx, String_List *out_list);
static void eval_register_macro(Evaluator_Context *ctx, const Node *node);
static bool eval_invoke_macro(Evaluator_Context *ctx, String_View name, Args args);
static void eval_register_function(Evaluator_Context *ctx, const Node *node);
static bool eval_invoke_function(Evaluator_Context *ctx, String_View name, Args args);
static String_View resolve_arg(Evaluator_Context *ctx, Arg arg);
static bool eval_handle_flow_control_command(Evaluator_Context *ctx, String_View cmd_name);
static bool eval_condition(Evaluator_Context *ctx, Args condition);
static void list_load_var(Evaluator_Context *ctx, String_View var_name, String_List *out);
static void list_store_var(Evaluator_Context *ctx, String_View var_name, String_List *list);
static void cpack_sync_common_metadata(Evaluator_Context *ctx);
static long parse_long_or_default(String_View sv, long fallback);
static bool list_index_to_offset(size_t count, long idx, size_t *out);
static String_View regex_replace_with_backrefs(Evaluator_Context *ctx, String_View pattern, String_View input, String_View replacement);
static bool ensure_parent_dirs_for_path(Arena *arena, String_View file_path);
static String_View configure_expand_variables(Evaluator_Context *ctx, String_View input, bool at_only);
static bool path_is_absolute_sv(String_View path);
static String_View path_join_arena(Arena *arena, String_View base, String_View rel);
static bool path_has_separator(String_View path);
static String_View path_basename_sv(String_View path);
static String_View filename_ext_sv(String_View name);
static bool eval_real_probes_enabled(Evaluator_Context *ctx);
static bool eval_real_probe_check_symbol_exists(Evaluator_Context *ctx, String_View symbol, String_View headers, bool *used_probe);
static bool eval_real_probe_check_c_source_compiles(Evaluator_Context *ctx, String_View source, bool *used_probe);
static bool eval_has_pending_flow_control(const Evaluator_Context *ctx);
static Loop_Flow_Signal eval_consume_loop_flow_signal(Evaluator_Context *ctx);
static void eval_append_link_library_value(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View value);

/*static String_View normalize_source_path(Evaluator_Context *ctx, String_View src) {
    if (!ctx || src.count == 0) return src;
    if (path_is_absolute_sv(src)) return src;
    if (nob_sv_starts_with(src, sv_from_cstr("$"))) return src;
    if (nob_sv_starts_with(src, sv_from_cstr("-"))) return src;
    if (ctx->current_source_dir.count == 1 && ctx->current_source_dir.data[0] == '.') return src;
    return path_join_arena(ctx->arena, ctx->current_source_dir, src);
}*/

static bool sv_bool_is_true(String_View value) {
    return nob_sv_eq(value, sv_from_cstr("ON")) ||
           nob_sv_eq(value, sv_from_cstr("TRUE")) ||
           nob_sv_eq(value, sv_from_cstr("1")) ||
           nob_sv_eq(value, sv_from_cstr("YES"));
}

static bool is_library_type_keyword(String_View arg) {
    return nob_sv_eq(arg, sv_from_cstr("STATIC")) ||
           nob_sv_eq(arg, sv_from_cstr("SHARED")) ||
           nob_sv_eq(arg, sv_from_cstr("MODULE")) ||
           nob_sv_eq(arg, sv_from_cstr("OBJECT")) ||
           nob_sv_eq(arg, sv_from_cstr("INTERFACE"));
}

static bool is_common_target_keyword(String_View arg) {
    return nob_sv_eq(arg, sv_from_cstr("EXCLUDE_FROM_ALL")) ||
           nob_sv_eq(arg, sv_from_cstr("IMPORTED")) ||
           nob_sv_eq(arg, sv_from_cstr("GLOBAL")) ||
           nob_sv_eq(arg, sv_from_cstr("ALIAS"));
}

static bool parse_visibility_token(String_View arg, Visibility *out_visibility) {
    if (nob_sv_eq(arg, sv_from_cstr("PUBLIC"))) {
        *out_visibility = VISIBILITY_PUBLIC;
        return true;
    }
    if (nob_sv_eq(arg, sv_from_cstr("PRIVATE"))) {
        *out_visibility = VISIBILITY_PRIVATE;
        return true;
    }
    if (nob_sv_eq(arg, sv_from_cstr("INTERFACE"))) {
        *out_visibility = VISIBILITY_INTERFACE;
        return true;
    }
    return false;
}

static bool cmake_check_truthy_candidate(String_View value) {
    String_View v = genex_trim(value);
    if (v.count == 0) return false;
    if (cmake_string_is_false(v)) return false;
    return true;
}

static bool eval_check_include_exists(Evaluator_Context *ctx, String_View header) {
    (void)ctx;
    return cmake_check_truthy_candidate(header);
}

static bool eval_check_symbol_exists(Evaluator_Context *ctx, String_View symbol, String_View headers) {
    (void)ctx;
    return cmake_check_truthy_candidate(symbol) && cmake_check_truthy_candidate(headers);
}

static bool eval_check_function_exists(Evaluator_Context *ctx, String_View function_name) {
    (void)ctx;
    return cmake_check_truthy_candidate(function_name);
}

static bool eval_check_type_size_value(String_View type_name, size_t *out_size) {
    if (!out_size) return false;
    String_View t = genex_trim(type_name);
    if (t.count == 0) return false;

    if (sv_eq_ci(t, sv_from_cstr("char")) || sv_eq_ci(t, sv_from_cstr("signed char")) || sv_eq_ci(t, sv_from_cstr("unsigned char"))) {
        *out_size = sizeof(char);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("short")) || sv_eq_ci(t, sv_from_cstr("short int")) ||
        sv_eq_ci(t, sv_from_cstr("signed short")) || sv_eq_ci(t, sv_from_cstr("unsigned short"))) {
        *out_size = sizeof(short);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("int")) || sv_eq_ci(t, sv_from_cstr("signed int")) || sv_eq_ci(t, sv_from_cstr("unsigned int"))) {
        *out_size = sizeof(int);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("long")) || sv_eq_ci(t, sv_from_cstr("long int")) ||
        sv_eq_ci(t, sv_from_cstr("signed long")) || sv_eq_ci(t, sv_from_cstr("unsigned long"))) {
        *out_size = sizeof(long);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("long long")) || sv_eq_ci(t, sv_from_cstr("long long int")) ||
        sv_eq_ci(t, sv_from_cstr("signed long long")) || sv_eq_ci(t, sv_from_cstr("unsigned long long")) ||
        sv_eq_ci(t, sv_from_cstr("__int64")) || sv_eq_ci(t, sv_from_cstr("unsigned __int64"))) {
        *out_size = sizeof(long long);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("float"))) {
        *out_size = sizeof(float);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("double"))) {
        *out_size = sizeof(double);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("long double"))) {
        *out_size = sizeof(long double);
        return true;
    }
    if (sv_eq_ci(t, sv_from_cstr("void *")) || sv_eq_ci(t, sv_from_cstr("char *")) ||
        sv_eq_ci(t, sv_from_cstr("size_t")) || sv_eq_ci(t, sv_from_cstr("ssize_t")) ||
        sv_eq_ci(t, sv_from_cstr("intptr_t")) || sv_eq_ci(t, sv_from_cstr("uintptr_t")) ||
        sv_eq_ci(t, sv_from_cstr("time_t")) || sv_eq_ci(t, sv_from_cstr("off_t")) ||
        sv_eq_ci(t, sv_from_cstr("curl_off_t"))) {
        *out_size = sizeof(void*);
        return true;
    }

    return false;
}

static bool sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

// ============================================================================
// PROCESSAMENTO DE LISTAS
// ============================================================================

static void split_semicolon_list(Evaluator_Context *ctx, String_View value, String_List *out) {
    if (!ctx || !out) return;
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool is_sep = (i == value.count) || (value.data[i] == ';');
        if (!is_sep) continue;
        if (i > start) {
            String_View item = nob_sv_from_parts(value.data + start, i - start);
            string_list_add(out, ctx->arena, item);
        }
        start = i + 1;
    }
}

typedef void (*Eval_SV_Item_Fn)(Evaluator_Context *ctx, String_View item, void *ud);

// Itera itens separados por ';' em `raw`.
// - trim_ws: se true, aplica genex_trim(item) antes de entregar ao callback
// - pula itens vazios (antes ou depois do trim)
// - preserva o fallback: se split não produzir nada mas raw não for vazio, entrega o raw como item único.
static void eval_foreach_semicolon_item(Evaluator_Context *ctx, String_View raw, bool trim_ws, Eval_SV_Item_Fn fn, void *ud) {
    if (!ctx || !fn) return;

    String_List items = {0};
    string_list_init(&items);
    split_semicolon_list(ctx, raw, &items);

    if (items.count == 0 && raw.count > 0) {
        String_View it = trim_ws ? genex_trim(raw) : raw;
        if (it.count == 0) return;
        fn(ctx, it, ud);
        return;
    }

    for (size_t i = 0; i < items.count; i++) {
        String_View it = items.items[i];
        if (trim_ws) it = genex_trim(it);
        if (it.count == 0) continue;
        fn(ctx, it, ud);
    }
}

typedef struct List_Add_Ud {
    String_List *list;
    bool unique;
} List_Add_Ud;

static void eval_list_add_item(Evaluator_Context *ctx, String_View item, void *ud) {
    List_Add_Ud *u = (List_Add_Ud*)ud;
    if (!u || !u->list) return;
    if (u->unique) string_list_add_unique(u->list, ctx->arena, item);
    else string_list_add(u->list, ctx->arena, item);
}

typedef struct Target_Vis_Args {
    Build_Target *target;
    Visibility visibility;
    size_t start_idx; // índice onde começam os itens (args) do comando
} Target_Vis_Args;

// Parseia: <target> [PUBLIC|PRIVATE|INTERFACE] <...>
// - visibility default: PRIVATE
// - start_idx: 2 se args[1] for token de visibilidade, senão 1
// Retorna false se args insuficientes, target não existe, ou ctx inválido.
static bool eval_parse_target_visibility(Evaluator_Context *ctx,
                                        Args args,
                                        size_t min_args_total,
                                        Target_Vis_Args *out)
{
    if (!out) return false;
    out->target = NULL;
    out->visibility = VISIBILITY_PRIVATE;
    out->start_idx = 1;

    if (!ctx || !ctx->model) return false;
    if (args.count < min_args_total) return false;
    if (args.count < 1) return false;

    String_View target_name = resolve_arg(ctx, args.items[0]);
    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) return false;

    Visibility vis = VISIBILITY_PRIVATE;
    size_t start = 1;
    if (args.count > 1) {
        String_View maybe_vis = resolve_arg(ctx, args.items[1]);
        if (parse_visibility_token(maybe_vis, &vis)) {
            start = 2;
        }
    }

    out->target = target;
    out->visibility = vis;
    out->start_idx = start;
    return true;
}

// Helper simples para comandos que só precisam de target lookup (sem visibility)
// Retorna NULL se target não for encontrado ou args insuficientes
static Build_Target* eval_get_target_from_args(Evaluator_Context *ctx, Args args, size_t min_args) {
    if (!ctx || !ctx->model) return NULL;
    if (args.count < min_args) return NULL;
    if (args.count < 1) return NULL;
    
    String_View target_name = resolve_arg(ctx, args.items[0]);
    return build_model_find_target(ctx->model, target_name);
}

typedef void (*Eval_Target_Vis_Item_Fn)(
    Evaluator_Context *ctx,
    Build_Target *target,
    Visibility visibility,
    String_View item,
    void *ud
);

// Itera args no formato:
//   <target> [PUBLIC|PRIVATE|INTERFACE] item... [PUBLIC|...] item...
//
// - Resolve o target em args[0]
// - visibility inicia em PRIVATE
// - Quando encontra token de visibilidade, atualiza e continua
// - Para qualquer outro arg, chama on_item(ctx, target, visibility, arg, ud)
//
// Retorna false se args insuficientes, ctx/model nulos ou target não encontrado.
static bool eval_target_foreach_scoped_item(Evaluator_Context *ctx, Args args, size_t min_args_total, Eval_Target_Vis_Item_Fn on_item, void *ud) {
    if (!ctx || !ctx->model) return false;
    if (!on_item) return false;
    if (args.count < min_args_total) return false;
    if (args.count < 1) return false;

    String_View target_name = resolve_arg(ctx, args.items[0]);
    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) return false;

    Visibility visibility = VISIBILITY_PRIVATE;

    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (parse_visibility_token(arg, &visibility)) {
            continue;
        }
        on_item(ctx, target, visibility, arg, ud);
    }

    return true;
}

static void eval_tgtvis_add_include_dir(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View item, void *ud) {
    (void)ud;
    build_target_add_include_directory(target, ctx->arena, item, visibility, CONFIG_ALL);
}

static void eval_tgtvis_add_link_option(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View item, void *ud) {
    (void)ud;
    build_target_add_link_option(target, ctx->arena, item, visibility, CONFIG_ALL);
}

static void eval_tgtvis_add_link_directory(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View item, void *ud) {
    (void)ud;
    build_target_add_link_directory(target, ctx->arena, item, visibility, CONFIG_ALL);
}

static void eval_tgtvis_add_link_library_value(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View item, void *ud) {
    (void)ud;
    eval_append_link_library_value(ctx, target, visibility, item);
}

// Inicializa um contexto de avaliação
static Evaluator_Context* eval_context_create(Arena *arena) {
    if (!arena) return NULL;
    Evaluator_Context *ctx = arena_alloc_zero(arena, sizeof(Evaluator_Context));
    if (!ctx) return NULL;
    ctx->arena = arena;
    ctx->model = build_model_create(arena);
    if (!ctx->model) return NULL;
    ctx->scope_capacity = 8;
    ctx->scopes = arena_alloc_array_zero(arena, Eval_Scope, ctx->scope_capacity);
    if (!ctx->scopes) return NULL;
    ctx->scope_count = 1;
    ctx->current_source_dir = sv_from_cstr(".");
    ctx->current_binary_dir = sv_from_cstr(".");
    ctx->current_list_dir = sv_from_cstr(".");
    return ctx;
}

static String_View sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return sv_from_cstr("");
    if (sv.count == 0 || sv.data == NULL) {
        char *empty = arena_alloc(arena, 1);
        if (!empty) return sv_from_cstr("");
        empty[0] = '\0';
        return sv_from_cstr(empty);
    }
    return sv_from_cstr(arena_strndup(arena, sv.data, sv.count));
}

// Define uma variável no escopo atual (ou pai se PARENT_SCOPE)
static void eval_set_var(Evaluator_Context *ctx, String_View key, 
                        String_View value, bool parent_scope, bool cache_var) {
    
    // CORREÇÃO: Duplicar strings para a Arena para garantir persistência
    String_View safe_key = sv_copy_to_arena(ctx->arena, key);
    String_View safe_val = sv_copy_to_arena(ctx->arena, value);

    if (cache_var) {
        build_model_set_cache_variable(ctx->model, safe_key, safe_val, 
                                       sv_from_cstr("STRING"), sv_from_cstr(""));
        return;
    }
    
    size_t target_idx = ctx->scope_count - 1;
    if (parent_scope && target_idx > 0) {
        target_idx--;
    }
    
    Eval_Scope *scope = &ctx->scopes[target_idx];
    
    for (size_t i = 0; i < scope->vars.count; i++) {
        if (nob_sv_eq(scope->vars.keys[i], safe_key)) {
            scope->vars.values[i] = safe_val;
            return;
        }
    }
    
    if (scope->vars.count >= scope->vars.capacity) {
        if (!arena_da_reserve_pair(
                ctx->arena,
                (void**)&scope->vars.keys,
                sizeof(*scope->vars.keys),
                (void**)&scope->vars.values,
                sizeof(*scope->vars.values),
                &scope->vars.capacity,
                scope->vars.count + 1)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "set",
                "falha de alocacao ao expandir variaveis de escopo",
                "reduza o script de entrada ou aumente memoria disponivel");
            return;
        }
    }
    
    scope->vars.keys[scope->vars.count] = safe_key;
    scope->vars.values[scope->vars.count] = safe_val;
    scope->vars.count++;
}

static void eval_set_env_var(Evaluator_Context *ctx, String_View env_key, String_View value) {
    if (!ctx || !ctx->model) return;
    build_model_set_env_var(ctx->model, ctx->arena, env_key, value);
}

static void eval_unset_var(Evaluator_Context *ctx, String_View key, bool cache_var) {
    if (!ctx) return;

    if (cache_var) {
        Property_List *cache = &ctx->model->cache_variables;
        for (size_t i = 0; i < cache->count; i++) {
            if (nob_sv_eq(cache->items[i].name, key)) {
                for (size_t j = i + 1; j < cache->count; j++) {
                    cache->items[j - 1] = cache->items[j];
                }
                cache->count--;
                return;
            }
        }
        return;
    }

    if (nob_sv_starts_with(key, sv_from_cstr("ENV{")) && nob_sv_end_with(key, "}")) {
        String_View env_name = nob_sv_from_parts(key.data + 4, key.count - 5);
        Property_List *envs = &ctx->model->environment_variables;
        for (size_t i = 0; i < envs->count; i++) {
            if (nob_sv_eq(envs->items[i].name, env_name)) {
                for (size_t j = i + 1; j < envs->count; j++) {
                    envs->items[j - 1] = envs->items[j];
                }
                envs->count--;
                return;
            }
        }
        return;
    }

    if (ctx->scope_count == 0) return;
    Eval_Scope *scope = &ctx->scopes[ctx->scope_count - 1];
    for (size_t i = 0; i < scope->vars.count; i++) {
        if (nob_sv_eq(scope->vars.keys[i], key)) {
            for (size_t j = i + 1; j < scope->vars.count; j++) {
                scope->vars.keys[j - 1] = scope->vars.keys[j];
                scope->vars.values[j - 1] = scope->vars.values[j];
            }
            scope->vars.count--;
            return;
        }
    }
}

// Obtém o valor de uma variável (procura do escopo atual para cima)
static String_View eval_get_var(Evaluator_Context *ctx, String_View key) {
    // 1. Verifica variáveis de escopo
    for (size_t i = ctx->scope_count; i > 0; i--) {
        Eval_Scope *scope = &ctx->scopes[i - 1];
        for (size_t j = 0; j < scope->vars.count; j++) {
            if (nob_sv_eq(scope->vars.keys[j], key)) {
                return scope->vars.values[j];
            }
        }
    }

    // 2. Verifica variáveis de cache no modelo (fallback)
    String_View cache_val = build_model_get_cache_variable(ctx->model, key);
    if (cache_val.count > 0) {
        return cache_val;
    }
    
    // 3. Verifica variáveis de ambiente (começando com ENV{})
    if (nob_sv_starts_with(key, sv_from_cstr("ENV{")) && nob_sv_end_with(key, "}")) {
        // Extrai nome: ENV{PATH} -> PATH
        String_View env_name = nob_sv_from_parts(key.data + 4, key.count - 5);
        String_View env_override = property_list_find(&ctx->model->environment_variables, env_name);
        if (env_override.count > 0 || ctx->model->environment_variables.count > 0) {
            for (size_t i = 0; i < ctx->model->environment_variables.count; i++) {
                if (nob_sv_eq(ctx->model->environment_variables.items[i].name, env_name)) {
                    return ctx->model->environment_variables.items[i].value;
                }
            }
        }
        const char *cstr = nob_temp_sv_to_cstr(env_name);
        const char *env_val = getenv(cstr);
        return env_val ? sv_from_cstr(env_val) : sv_from_cstr("");
    }
    
    // 4. Verifica se é uma variável especial do CMake
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"))) {
        return ctx->current_source_dir;
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"))) {
        return ctx->current_binary_dir;
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_LIST_DIR"))) {
        return ctx->current_list_dir;
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_ROOT"))) {
        String_View local_builtin = path_join_arena(ctx->arena, ctx->current_source_dir, sv_from_cstr("cmake_builtin"));
        if (nob_file_exists(nob_temp_sv_to_cstr(local_builtin))) return local_builtin;
        return sv_from_cstr("");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MODULE_PATH"))) {
        String_Builder sb = {0};
        bool first = true;
        // não retorne esses String_View nem guarde em ctx/structs se vierem de nob_temp_* porque eles são 
        // temporários e podem ser sobrescritos a qualquer momento.
        String_View default_local = sv_from_cstr(nob_temp_sprintf("%s/CMake", nob_temp_sv_to_cstr(ctx->current_source_dir)));
        if (nob_file_exists(nob_temp_sv_to_cstr(default_local))) {
            sb_append_buf(&sb, default_local.data, default_local.count);
            first = false;
        }
        String_View cmake_root = eval_get_var(ctx, sv_from_cstr("CMAKE_ROOT"));
        if (cmake_root.count > 0) {
            String_View modules_dir = sv_from_cstr(nob_temp_sprintf("%s/Modules", nob_temp_sv_to_cstr(cmake_root)));
            if (nob_file_exists(nob_temp_sv_to_cstr(modules_dir))) {
                if (!first) sb_append(&sb, ';');
                sb_append_buf(&sb, modules_dir.data, modules_dir.count);
                first = false;
            }
        }
        String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count));
        nob_sb_free(sb);
        return out;
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_VERSION"))) {
        return sv_from_cstr("3.16.0");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MAJOR_VERSION"))) {
        return sv_from_cstr("3");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MINOR_VERSION"))) {
        return sv_from_cstr("16");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_PATCH_VERSION"))) {
        return sv_from_cstr("0");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_COMMAND"))) {
        return sv_from_cstr("cmake");
    }
    if (nob_sv_eq(key, sv_from_cstr("WIN32"))) {
        return ctx->model->is_windows ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("UNIX"))) {
        return ctx->model->is_unix ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("APPLE"))) {
        return ctx->model->is_apple ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("LINUX"))) {
        return ctx->model->is_linux ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("MINGW"))) {
#if defined(__MINGW32__) || defined(__MINGW64__)
        return sv_from_cstr("TRUE");
#else
        return sv_from_cstr("FALSE");
#endif
    }
    if (nob_sv_eq(key, sv_from_cstr("CYGWIN"))) {
#if defined(__CYGWIN__)
        return sv_from_cstr("TRUE");
#else
        return sv_from_cstr("FALSE");
#endif
    }
    if (nob_sv_eq(key, sv_from_cstr("MSVC"))) {
#if defined(_MSC_VER) && !defined(__clang__)
        return sv_from_cstr("TRUE");
#else
        return sv_from_cstr("FALSE");
#endif
    }
    if (nob_sv_eq(key, sv_from_cstr("WINDOWS_STORE")) || nob_sv_eq(key, sv_from_cstr("UWP"))) {
        return sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CROSSCOMPILING"))) {
        return sv_from_cstr("FALSE");
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_SYSTEM_NAME")) ||
        nob_sv_eq(key, sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"))) {
#if defined(_WIN32)
        return sv_from_cstr("Windows");
#elif defined(__APPLE__)
        return sv_from_cstr("Darwin");
#elif defined(__linux__)
        return sv_from_cstr("Linux");
#elif defined(__unix__) || defined(__unix)
        return sv_from_cstr("Unix");
#else
        return sv_from_cstr("");
#endif
    }
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_SYSTEM_PROCESSOR")) ||
        nob_sv_eq(key, sv_from_cstr("CMAKE_HOST_SYSTEM_PROCESSOR"))) {
#if defined(__x86_64__) || defined(_M_X64)
        return sv_from_cstr("x86_64");
#elif defined(__i386__) || defined(_M_IX86)
        return sv_from_cstr("x86");
#elif defined(__aarch64__) || defined(_M_ARM64)
        return sv_from_cstr("arm64");
#elif defined(__arm__) || defined(_M_ARM)
        return sv_from_cstr("arm");
#else
        return sv_from_cstr("");
#endif
    }
    
    return sv_from_cstr(""); // Não encontrada
}

static bool eval_has_var(Evaluator_Context *ctx, String_View key) {
    if (!ctx) return false;

    for (size_t i = 0; i < ctx->model->cache_variables.count; i++) {
        if (nob_sv_eq(ctx->model->cache_variables.items[i].name, key)) {
            return true;
        }
    }

    for (size_t i = ctx->scope_count; i > 0; i--) {
        Eval_Scope *scope = &ctx->scopes[i - 1];
        for (size_t j = 0; j < scope->vars.count; j++) {
            if (nob_sv_eq(scope->vars.keys[j], key)) {
                return true;
            }
        }
    }

    if (nob_sv_starts_with(key, sv_from_cstr("ENV{")) && nob_sv_end_with(key, "}")) {
        String_View env_name = nob_sv_from_parts(key.data + 4, key.count - 5);
        for (size_t i = 0; i < ctx->model->environment_variables.count; i++) {
            if (nob_sv_eq(ctx->model->environment_variables.items[i].name, env_name)) {
                return true;
            }
        }
        const char *env_val = getenv(nob_temp_sv_to_cstr(env_name));
        return env_val != NULL;
    }

    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_BINARY_DIR"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CURRENT_LIST_DIR"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_ROOT"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MODULE_PATH"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_VERSION"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MAJOR_VERSION"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_MINOR_VERSION"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_PATCH_VERSION"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_COMMAND"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("WIN32"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("UNIX"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("APPLE"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("LINUX"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("MINGW"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CYGWIN"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("MSVC"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("WINDOWS_STORE"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("UWP"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_CROSSCOMPILING"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_SYSTEM_NAME"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_SYSTEM_PROCESSOR"))) return true;
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_HOST_SYSTEM_PROCESSOR"))) return true;
    return false;
}

// Lê um arquivo inteiro diretamente para a Arena
static const char *g_check_state_keys[] = {
    "CMAKE_REQUIRED_FLAGS",
    "CMAKE_REQUIRED_DEFINITIONS",
    "CMAKE_REQUIRED_INCLUDES",
    "CMAKE_REQUIRED_LIBRARIES",
    "CMAKE_REQUIRED_LINK_OPTIONS",
    "CMAKE_REQUIRED_LINK_DIRECTORIES",
    "CMAKE_REQUIRED_QUIET",
    "CMAKE_EXTRA_INCLUDE_FILES"
};

static void eval_check_state_push(Evaluator_Context *ctx, bool reset) {
    if (!ctx) return;
    size_t key_count = sizeof(g_check_state_keys) / sizeof(g_check_state_keys[0]);
    if (!arena_da_reserve(ctx->arena, (void**)&ctx->check_state_stack.items, &ctx->check_state_stack.capacity,
            sizeof(*ctx->check_state_stack.items), ctx->check_state_stack.count + 1)) {
        return;
    }

    Eval_Check_State_Frame *frame = &ctx->check_state_stack.items[ctx->check_state_stack.count++];
    memset(frame, 0, sizeof(*frame));
    frame->count = key_count;
    frame->keys = arena_alloc_array(ctx->arena, String_View, key_count);
    frame->values = arena_alloc_array(ctx->arena, String_View, key_count);
    frame->was_set = arena_alloc_array(ctx->arena, bool, key_count);
    if (!frame->keys || !frame->values || !frame->was_set) {
        ctx->check_state_stack.count--;
        return;
    }

    for (size_t i = 0; i < key_count; i++) {
        String_View key = sv_from_cstr(g_check_state_keys[i]);
        frame->keys[i] = key;
        frame->was_set[i] = eval_has_var(ctx, key);
        frame->values[i] = frame->was_set[i] ? eval_get_var(ctx, key) : sv_from_cstr("");
    }

    if (reset) {
        for (size_t i = 0; i < key_count; i++) {
            eval_unset_var(ctx, frame->keys[i], false);
            eval_unset_var(ctx, frame->keys[i], true);
        }
    }
}

static void eval_check_state_pop(Evaluator_Context *ctx) {
    if (!ctx) return;
    if (ctx->check_state_stack.count == 0) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_pop_check_state",
            "cmake_pop_check_state sem estado salvo", "chame cmake_push_check_state antes do pop");
        return;
    }

    Eval_Check_State_Frame *frame = &ctx->check_state_stack.items[ctx->check_state_stack.count - 1];
    ctx->check_state_stack.count--;
    for (size_t i = 0; i < frame->count; i++) {
        if (frame->was_set[i]) {
            eval_unset_var(ctx, frame->keys[i], true);
            eval_set_var(ctx, frame->keys[i], frame->values[i], false, false);
        } else {
            eval_unset_var(ctx, frame->keys[i], false);
            eval_unset_var(ctx, frame->keys[i], true);
        }
    }
}

static void eval_cmake_push_check_state_command(Evaluator_Context *ctx, Args args) {
    bool reset = false;
    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("RESET"))) {
            reset = true;
        }
    }
    eval_check_state_push(ctx, reset);
}

static void eval_cmake_pop_check_state_command(Evaluator_Context *ctx, Args args) {
    (void)args;
    eval_check_state_pop(ctx);
}

static String_View arena_read_file(Arena *arena, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return (String_View){0};

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length < 0) {
        fclose(f);
        return (String_View){0};
    }

    // Aloca +1 para null terminator (segurança)
    char *data = arena_alloc(arena, length + 1);
    if (!data) {
        fclose(f);
        return (String_View){0};
    }

    size_t read = fread(data, 1, length, f);
    fclose(f);
    
    data[read] = '\0';
    return (String_View){.data = data, .count = read};
}

#define arena_da_append_or_return_void(arena, list, item) do { \
    if (!arena_da_reserve((arena), (void**)&(list)->items, &(list)->capacity, sizeof((list)->items[0]), (list)->count + 1)) { \
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "internal", \
            "falha de alocacao ao expandir array interno", \
            "reduza o script de entrada ou aumente memoria disponivel"); \
        return; \
    } \
    (list)->items[(list)->count++] = (item); \
} while(0)

#define arena_da_append_or_return_false(arena, list, item) do { \
    if (!arena_da_reserve((arena), (void**)&(list)->items, &(list)->capacity, sizeof((list)->items[0]), (list)->count + 1)) { \
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "internal", \
            "falha de alocacao ao expandir array interno", \
            "reduza o script de entrada ou aumente memoria disponivel"); \
        return false; \
    } \
    (list)->items[(list)->count++] = (item); \
} while(0)

// Tokeniza string diretamente para a Arena
static bool arena_tokenize(Arena *arena, String_View content, Token_List *out_tokens) {
    Lexer l = lexer_init(content);
    Token_List tokens = {0};

    for (Token t = lexer_next(&l); t.kind != TOKEN_END; t = lexer_next(&l)) {
        if (!arena_da_reserve(arena, (void**)&tokens.items, &tokens.capacity, sizeof(tokens.items[0]), tokens.count + 1)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "internal",
                     "falha de alocacao ao expandir token list",
                     "reduza o arquivo de entrada ou aumente memoria disponivel");
            return false;
        }
        tokens.items[tokens.count++] = t;
    }

    *out_tokens = tokens;
    return true;
}


// Gerenciamento de Escopo de Avaliação
static void eval_push_scope(Evaluator_Context *ctx) {
    if (!arena_da_reserve(ctx->arena, (void**)&ctx->scopes, &ctx->scope_capacity,
            sizeof(*ctx->scopes), ctx->scope_count + 1)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "internal",
            "falha de alocacao ao expandir pilha de escopos",
            "reduza o nivel de aninhamento ou aumente memoria disponivel");
        return;
    }
    memset(&ctx->scopes[ctx->scope_count], 0, sizeof(Eval_Scope));
    ctx->scope_count++;
}

static void eval_pop_scope(Evaluator_Context *ctx) {
    if (ctx->scope_count > 1) {
        ctx->scope_count--;
    }
}

static Eval_Macro* eval_find_macro(Evaluator_Context *ctx, String_View name) {
    for (size_t i = 0; i < ctx->macros.count; i++) {
        if (nob_sv_eq(ctx->macros.items[i].name, name)) {
            return &ctx->macros.items[i];
        }
    }
    return NULL;
}

static bool eval_call_stack_push(Evaluator_Context *ctx, String_View name) {
    for (size_t i = 0; i < ctx->call_stack.count; i++) {
        if (nob_sv_eq(ctx->call_stack.names[i], name)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "macro/function",
                nob_temp_sprintf("chamada recursiva detectada: "SV_Fmt, SV_Arg(name)),
                "quebre o ciclo de chamadas ou adicione condicao de parada");
            return false;
        }
    }

    if (ctx->call_stack.count >= ctx->call_stack.capacity) {
        if (!arena_da_reserve(ctx->arena, (void**)&ctx->call_stack.names, &ctx->call_stack.capacity,
                sizeof(*ctx->call_stack.names), ctx->call_stack.count + 1)) {
            return false;
        }
    }

    ctx->call_stack.names[ctx->call_stack.count++] = name;
    return true;
}

static void eval_call_stack_pop(Evaluator_Context *ctx) {
    if (ctx->call_stack.count > 0) {
        ctx->call_stack.count--;
    }
}

static bool eval_include_stack_contains(Evaluator_Context *ctx, String_View path) {
    for (size_t i = 0; i < ctx->include_stack.count; i++) {
#if defined(_WIN32)
        if (sv_eq_ci(ctx->include_stack.paths[i], path)) return true;
#else
        if (nob_sv_eq(ctx->include_stack.paths[i], path)) return true;
#endif
    }
    return false;
}

static bool eval_include_stack_push(Evaluator_Context *ctx, String_View path) {
    if (!ctx) return false;
    if (ctx->include_stack.count >= ctx->include_stack.capacity) {
        if (!arena_da_reserve(ctx->arena, (void**)&ctx->include_stack.paths, &ctx->include_stack.capacity,
                sizeof(*ctx->include_stack.paths), ctx->include_stack.count + 1)) {
            return false;
        }
    }
    ctx->include_stack.paths[ctx->include_stack.count++] = path;
    return true;
}

static void eval_include_stack_pop(Evaluator_Context *ctx) {
    if (ctx && ctx->include_stack.count > 0) {
        ctx->include_stack.count--;
    }
}

static String_View eval_arg_to_raw_string(Evaluator_Context *ctx, Arg arg) {
    if (arg.count == 0) return sv_from_cstr("");
    if (arg.count == 1) {
        return sv_from_cstr(arena_strndup(ctx->arena, arg.items[0].text.data, arg.items[0].text.count));
    }

    String_Builder sb = {0};
    for (size_t i = 0; i < arg.count; i++) {
        sb_append_buf(&sb, arg.items[i].text.data, arg.items[i].text.count);
    }
    char *raw = arena_strndup(ctx->arena, sb.items, sb.count);
    nob_sb_free(sb);
    return sv_from_cstr(raw);
}

static bool is_custom_command_keyword(String_View arg) {
    return sv_eq_ci(arg, sv_from_cstr("TARGET")) ||
           sv_eq_ci(arg, sv_from_cstr("OUTPUT")) ||
           sv_eq_ci(arg, sv_from_cstr("PRE_BUILD")) ||
           sv_eq_ci(arg, sv_from_cstr("PRE_LINK")) ||
           sv_eq_ci(arg, sv_from_cstr("POST_BUILD")) ||
           sv_eq_ci(arg, sv_from_cstr("COMMAND")) ||
           sv_eq_ci(arg, sv_from_cstr("DEPENDS")) ||
           sv_eq_ci(arg, sv_from_cstr("BYPRODUCTS")) ||
           sv_eq_ci(arg, sv_from_cstr("MAIN_DEPENDENCY")) ||
           sv_eq_ci(arg, sv_from_cstr("IMPLICIT_DEPENDS")) ||
           sv_eq_ci(arg, sv_from_cstr("DEPFILE")) ||
           sv_eq_ci(arg, sv_from_cstr("WORKING_DIRECTORY")) ||
           sv_eq_ci(arg, sv_from_cstr("COMMENT")) ||
           sv_eq_ci(arg, sv_from_cstr("APPEND")) ||
           sv_eq_ci(arg, sv_from_cstr("VERBATIM")) ||
           sv_eq_ci(arg, sv_from_cstr("USES_TERMINAL")) ||
           sv_eq_ci(arg, sv_from_cstr("COMMAND_EXPAND_LISTS")) ||
           sv_eq_ci(arg, sv_from_cstr("DEPENDS_EXPLICIT_ONLY")) ||
           sv_eq_ci(arg, sv_from_cstr("JOB_POOL")) ||
           sv_eq_ci(arg, sv_from_cstr("JOB_SERVER_AWARE")) ||
           sv_eq_ci(arg, sv_from_cstr("CODEGEN"));
}

static void custom_command_copy_list(Arena *arena, String_List *dst, const String_List *src) {
    if (!arena || !dst || !src) return;
    for (size_t i = 0; i < src->count; i++) {
        string_list_add(dst, arena, src->items[i]);
    }
}

static Custom_Command* evaluator_target_add_custom_command_ex(Build_Target *target, Arena *arena, bool pre_build, String_View command, String_View working_dir, String_View comment) {
    return build_target_add_custom_command_ex(target, arena, pre_build, command, working_dir, comment);
}

static Custom_Command* build_model_add_output_custom_command(Build_Model *model, Arena *arena, String_View command, String_View working_dir, String_View comment) {
    return build_model_add_custom_command_output_ex(model, arena, command, working_dir, comment);
}

static size_t parse_custom_command_list(Evaluator_Context *ctx, Args args, size_t start, String_List *out) {
    size_t i = start;
    while (i < args.count) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (is_custom_command_keyword(arg)) break;
        if (arg.count > 0) string_list_add(out, ctx->arena, arg);
        i++;
    }
    return i;
}

static void sb_append_custom_command_part(String_Builder *sb, String_View part, bool command_expand_lists) {
    if (!sb || part.count == 0) return;
    if (!command_expand_lists) {
        if (sb->count > 0) sb_append(sb, ' ');
        sb_append_buf(sb, part.data, part.count);
        return;
    }

    size_t token_start = 0;
    for (size_t i = 0; i <= part.count; i++) {
        bool sep = (i == part.count) || part.data[i] == ';';
        if (!sep) continue;
        if (i > token_start) {
            if (sb->count > 0) sb_append(sb, ' ');
            sb_append_buf(sb, part.data + token_start, i - token_start);
        }
        token_start = i + 1;
    }
}

static String_View join_commands_with_and(Arena *arena, String_List commands) {
    if (!arena || commands.count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < commands.count; i++) {
        if (commands.items[i].count == 0) continue;
        if (sb.count > 0) sb_append_cstr(&sb, " && ");
        sb_append_buf(&sb, commands.items[i].data, commands.items[i].count);
    }
    String_View out = sb.count > 0 ? sv_from_cstr(arena_strndup(arena, sb.items, sb.count)) : sv_from_cstr("");
    nob_sb_free(sb);
    return out;
}

static String_View join_command_args(Evaluator_Context *ctx, Args args, size_t start, size_t end, bool command_expand_lists) {
    if (start >= end) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = start; i < end; i++) {
        String_View part = resolve_arg(ctx, args.items[i]);
        if (part.count == 0) continue;
        sb_append_custom_command_part(&sb, part, command_expand_lists);
    }
    String_View out = sb.count > 0 ? sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count)) : sv_from_cstr("");
    nob_sb_free(sb);
    return out;
}

static Custom_Command *find_output_custom_command_by_output(Build_Model *model, String_View output) {
    if (!model || output.count == 0) return NULL;
    for (size_t i = 0; i < model->output_custom_command_count; i++) {
        Custom_Command *cmd = &model->output_custom_commands[i];
        for (size_t j = 0; j < cmd->outputs.count; j++) {
            if (nob_sv_eq(cmd->outputs.items[j], output)) return cmd;
        }
    }
    return NULL;
}

static void append_custom_command_command(Arena *arena, Custom_Command *cmd, String_View extra) {
    if (!arena || !cmd || extra.count == 0) return;
    if (cmd->command.count == 0) {
        cmd->command = extra;
        return;
    }
    String_Builder sb = {0};
    sb_append_buf(&sb, cmd->command.data, cmd->command.count);
    sb_append_cstr(&sb, " && ");
    sb_append_buf(&sb, extra.data, extra.count);
    cmd->command = sb.count > 0 ? sv_from_cstr(arena_strndup(arena, sb.items, sb.count)) : sv_from_cstr("");
    nob_sb_free(sb);
}


static String_View internal_install_export_set_key(Evaluator_Context *ctx, String_View export_set_name) {
    if (!ctx || export_set_name.count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__INSTALL_EXPORT_SET__");
    sb_append_buf(&sb, export_set_name.data, export_set_name.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static bool is_install_keyword(String_View arg) {
    return nob_sv_eq(arg, sv_from_cstr("TARGETS")) ||
           nob_sv_eq(arg, sv_from_cstr("FILES")) ||
           nob_sv_eq(arg, sv_from_cstr("PROGRAMS")) ||
           nob_sv_eq(arg, sv_from_cstr("DIRECTORY")) ||
           nob_sv_eq(arg, sv_from_cstr("EXPORT")) ||
           nob_sv_eq(arg, sv_from_cstr("DESTINATION")) ||
           nob_sv_eq(arg, sv_from_cstr("RUNTIME")) ||
           nob_sv_eq(arg, sv_from_cstr("LIBRARY")) ||
           nob_sv_eq(arg, sv_from_cstr("ARCHIVE")) ||
           nob_sv_eq(arg, sv_from_cstr("OPTIONAL")) ||
           nob_sv_eq(arg, sv_from_cstr("COMPONENT"));
}

static bool is_install_target_group_keyword(String_View arg) {
    return nob_sv_eq(arg, sv_from_cstr("RUNTIME")) ||
           nob_sv_eq(arg, sv_from_cstr("LIBRARY")) ||
           nob_sv_eq(arg, sv_from_cstr("ARCHIVE"));
}

static void eval_set_callable_builtin_args(Evaluator_Context *ctx, size_t param_count, String_View *resolved_args, size_t arg_count) {
    char *argc_buf = nob_temp_sprintf("%zu", arg_count);
    eval_set_var(ctx, sv_from_cstr("ARGC"), sv_from_cstr(argc_buf), false, false);

    String_Builder argv_sb = {0};
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) sb_append(&argv_sb, ';');
        sb_append_buf(&argv_sb, resolved_args[i].data, resolved_args[i].count);

        char *argv_key = nob_temp_sprintf("ARGV%zu", i);
        eval_set_var(ctx, sv_from_cstr(argv_key), resolved_args[i], false, false);
    }
    String_View argv_val = argv_sb.count > 0 ? sb_to_sv(argv_sb) : sv_from_cstr("");
    eval_set_var(ctx, sv_from_cstr("ARGV"), argv_val, false, false);
    nob_sb_free(argv_sb);

    String_Builder argn_sb = {0};
    for (size_t i = param_count; i < arg_count; i++) {
        if (argn_sb.count > 0) sb_append(&argn_sb, ';');
        sb_append_buf(&argn_sb, resolved_args[i].data, resolved_args[i].count);
    }
    String_View argn_val = argn_sb.count > 0 ? sb_to_sv(argn_sb) : sv_from_cstr("");
    eval_set_var(ctx, sv_from_cstr("ARGN"), argn_val, false, false);
    nob_sb_free(argn_sb);
}

static void eval_set_macro_builtin_args(Evaluator_Context *ctx, Eval_Macro *macro, String_View *resolved_args, size_t arg_count) {
    eval_set_callable_builtin_args(ctx, macro ? macro->param_count : 0, resolved_args, arg_count);
}

static Eval_Function* eval_find_function(Evaluator_Context *ctx, String_View name) {
    for (size_t i = 0; i < ctx->functions.count; i++) {
        if (nob_sv_eq(ctx->functions.items[i].name, name)) {
            return &ctx->functions.items[i];
        }
    }
    return NULL;
}

static void eval_set_function_builtin_args(Evaluator_Context *ctx, Eval_Function *function, String_View *resolved_args, size_t arg_count) {
    eval_set_callable_builtin_args(ctx, function ? function->param_count : 0, resolved_args, arg_count);
}

static void eval_register_macro(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return;
    if (node->kind != NODE_MACRO) return;

    Eval_Macro *macro = eval_find_macro(ctx, node->as.func_def.name);
    if (!macro) {
        if (!arena_da_reserve(ctx->arena, (void**)&ctx->macros.items, &ctx->macros.capacity,
                sizeof(*ctx->macros.items), ctx->macros.count + 1)) {
            return;
        }
        macro = &ctx->macros.items[ctx->macros.count++];
    }

    memset(macro, 0, sizeof(*macro));
    macro->name = node->as.func_def.name;
    macro->body = node->as.func_def.body;
    macro->param_count = node->as.func_def.params.count;
    if (macro->param_count > 0) {
        macro->params = arena_alloc_array(ctx->arena, String_View, macro->param_count);
        if (!macro->params) {
            macro->param_count = 0;
            return;
        }
        for (size_t i = 0; i < macro->param_count; i++) {
            macro->params[i] = eval_arg_to_raw_string(ctx, node->as.func_def.params.items[i]);
        }
    }
}

static bool eval_invoke_macro(Evaluator_Context *ctx, String_View name, Args args) {
    Eval_Macro *macro = eval_find_macro(ctx, name);
    if (!macro) return false;

    if (!eval_call_stack_push(ctx, macro->name)) {
        return true;
    }

    size_t arg_count = args.count;
    String_View *resolved_args = NULL;
    if (arg_count > 0) {
        resolved_args = arena_alloc_array(ctx->arena, String_View, arg_count);
        if (!resolved_args) {
            eval_call_stack_pop(ctx);
            return true;
        }
        for (size_t i = 0; i < arg_count; i++) {
            resolved_args[i] = resolve_arg(ctx, args.items[i]);
        }
    }

    for (size_t i = 0; i < macro->param_count; i++) {
        String_View value = i < arg_count ? resolved_args[i] : sv_from_cstr("");
        eval_set_var(ctx, macro->params[i], value, false, false);
    }
    eval_set_macro_builtin_args(ctx, macro, resolved_args, arg_count);

    for (size_t i = 0; i < macro->body.count; i++) {
        eval_node(ctx, macro->body.items[i]);
    }

    eval_call_stack_pop(ctx);
    return true;
}

static void eval_register_function(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || !node) return;
    if (node->kind != NODE_FUNCTION) return;

    Eval_Function *function = eval_find_function(ctx, node->as.func_def.name);
    if (!function) {
        if (!arena_da_reserve(ctx->arena, (void**)&ctx->functions.items, &ctx->functions.capacity,
                sizeof(*ctx->functions.items), ctx->functions.count + 1)) {
            return;
        }
        function = &ctx->functions.items[ctx->functions.count++];
    }

    memset(function, 0, sizeof(*function));
    function->name = node->as.func_def.name;
    function->body = node->as.func_def.body;
    function->param_count = node->as.func_def.params.count;
    if (function->param_count > 0) {
        function->params = arena_alloc_array(ctx->arena, String_View, function->param_count);
        if (!function->params) {
            function->param_count = 0;
            return;
        }
        for (size_t i = 0; i < function->param_count; i++) {
            function->params[i] = eval_arg_to_raw_string(ctx, node->as.func_def.params.items[i]);
        }
    }
}

static bool eval_invoke_function(Evaluator_Context *ctx, String_View name, Args args) {
    Eval_Function *function = eval_find_function(ctx, name);
    if (!function) return false;

    if (!eval_call_stack_push(ctx, function->name)) {
        return true;
    }

    size_t arg_count = args.count;
    String_View *resolved_args = NULL;
    if (arg_count > 0) {
        resolved_args = arena_alloc_array(ctx->arena, String_View, arg_count);
        if (!resolved_args) {
            eval_call_stack_pop(ctx);
            return true;
        }
        for (size_t i = 0; i < arg_count; i++) {
            resolved_args[i] = resolve_arg(ctx, args.items[i]);
        }
    }

    eval_push_scope(ctx);
    bool previous_in_function_call = ctx->in_function_call;
    bool previous_return_requested = ctx->return_requested;
    ctx->in_function_call = true;
    ctx->return_requested = false;

    for (size_t i = 0; i < function->param_count; i++) {
        String_View value = i < arg_count ? resolved_args[i] : sv_from_cstr("");
        eval_set_var(ctx, function->params[i], value, false, false);
    }
    eval_set_function_builtin_args(ctx, function, resolved_args, arg_count);

    for (size_t i = 0; i < function->body.count; i++) {
        eval_node(ctx, function->body.items[i]);
        if (ctx->return_requested) break;
    }

    ctx->return_requested = previous_return_requested;
    ctx->in_function_call = previous_in_function_call;
    ctx->break_requested = false;
    ctx->continue_requested = false;
    eval_pop_scope(ctx);
    eval_call_stack_pop(ctx);
    return true;
}


// ============================================================================
// RESOLUÇÃO DE ARGUMENTOS E EXPRESSÕES
// ============================================================================

// Resolve uma string com interpolação de variáveis
enum { MAX_RESOLVE_DEPTH = 64 };

static String_View resolve_string_depth(Evaluator_Context *ctx, String_View input, size_t depth);

static String_View resolve_string(Evaluator_Context *ctx, String_View input) {
    return resolve_string_depth(ctx, input, 0);
}

static String_View resolve_string_depth(Evaluator_Context *ctx, String_View input, size_t depth) {
    if (depth > MAX_RESOLVE_DEPTH) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "resolve_arg",
            "limite de recursao excedido ao resolver string",
            "verifique expansao recursiva de variaveis");
        return input;
    }

    bool has_special = false;
    for (size_t i = 0; i < input.count; i++) {
        if (input.data[i] == '$') {
            has_special = true;
            break;
        }
    }
    
    if (!has_special) return input;
    
    String_Builder sb = {0};
    size_t i = 0;
    
    while (i < input.count) {
        if (input.data[i] == '$' && i + 1 < input.count) {
            char next_char = input.data[i + 1];

            if (next_char == '{') {
                size_t start = i + 2;
                size_t j = start;
                int brace_depth = 1;
                
                while (j < input.count && brace_depth > 0) {
                    if (input.data[j] == '{') brace_depth++;
                    else if (input.data[j] == '}') brace_depth--;
                    j++;
                }
                
                if (brace_depth == 0) {
                    String_View var_name = nob_sv_from_parts(input.data + start, j - 1 - start);
                    String_View var_val = eval_get_var(ctx, var_name);
                    var_val = resolve_string_depth(ctx, var_val, depth + 1);
                    
                    sb_append_buf(&sb, var_val.data, var_val.count);
                    i = j;
                    continue;
                }
            } else if (next_char == '<') {
                size_t start = i + 2;
                size_t j = start;
                int angle_depth = 1;
                
                while (j < input.count && angle_depth > 0) {
                    if (input.data[j] == '<') angle_depth++;
                    else if (input.data[j] == '>') angle_depth--;
                    j++;
                }
                
                if (angle_depth == 0) {
                    String_View content = nob_sv_from_parts(input.data + start, j - 1 - start);
                    String_View resolved_inner = resolve_string_depth(ctx, content, depth + 1);
                    String_View result = eval_generator_expression(ctx, resolved_inner);
                    
                    sb_append_buf(&sb, result.data, result.count);
                    i = j;
                    continue;
                }
            }
        }
        
        sb_append(&sb, input.data[i]);
        i++;
    }
    
    if (sb.count == 0) {
        nob_sb_free(sb);
        return sv_from_cstr("");
    }

    char *result = arena_strndup(ctx->arena, sb.items, sb.count);
    nob_sb_free(sb);
    
    return sv_from_cstr(result);
}

// Resolve um argumento da AST (lista de tokens) para string
static String_View resolve_arg(Evaluator_Context *ctx, Arg arg) {
    String_Builder sb = {0};
    
    for (size_t i = 0; i < arg.count; i++) {
        Token t = arg.items[i];
        
        switch (t.kind) {
            case TOKEN_STRING: {
                // Remove aspas e resolve
                String_View content = t.text;
                if (content.count >= 2) {
                    content.data++;
                    content.count -= 2;
                }
                String_View resolved = resolve_string(ctx, content);
                sb_append_buf(&sb, resolved.data, resolved.count);
                break;
            }
            case TOKEN_VAR: {
                String_View content = t.text;
                if (content.count > 3) { // ${X}
                    content.data += 2;
                    content.count -= 3;
                }
                String_View resolved = resolve_string(ctx, content);
                String_View var_val = eval_get_var(ctx, resolved);
                sb_append_buf(&sb, var_val.data, var_val.count);
                break;
            }
            case TOKEN_GEN_EXP: {
                String_View resolved = resolve_string(ctx, t.text);
                sb_append_buf(&sb, resolved.data, resolved.count);
                break;
            }
            case TOKEN_RAW_STRING: {
                // Remove brackets [[ ... ]]
                String_View content = t.text;
                size_t eq_count = 0;
                size_t start = 0;
                
                // Encontra a abertura
                while (start < content.count && content.data[start] == '[') {
                    start++;
                    while (start < content.count && content.data[start] == '=') {
                        start++;
                        eq_count++;
                    }
                    if (start < content.count && content.data[start] == '[') {
                        start++;
                        break;
                    }
                }
                
                // Encontra o fechamento
                size_t end = content.count;
                while (end > 0 && content.data[end - 1] == ']') {
                    end--;
                    size_t eq_seen = 0;
                    while (end > 0 && eq_seen < eq_count && content.data[end - 1] == '=') {
                        end--;
                        eq_seen++;
                    }
                    if (eq_seen == eq_count && end > 0 && content.data[end - 1] == ']') {
                        end--;
                        break;
                    }
                }
                
                if (start < end) {
                    String_View raw_content = nob_sv_from_parts(content.data + start, end - start);
                    sb_append_buf(&sb, raw_content.data, raw_content.count);
                }
                break;
            }
            default: {
                // Identifier ou outros tokens
                sb_append_buf(&sb, t.text.data, t.text.count);
                break;
            }
        }
    }
    
    if (sb.count == 0) {
        nob_sb_free(sb);
        return sv_from_cstr("");
    }

    char *result = arena_strndup(ctx->arena, sb.items, sb.count);
    nob_sb_free(sb);
    
    return sv_from_cstr(result);
}

// Resolve uma lista de argumentos para uma lista de strings
static void resolve_args_to_list(Evaluator_Context *ctx, Args args, 
                                 size_t start_idx, String_List *out_list) {
    for (size_t i = start_idx; i < args.count; i++) {
        String_View val = resolve_arg(ctx, args.items[i]);
        
        // Split por ponto e vírgula (formato de lista do CMake)
        size_t start = 0;
        for (size_t k = 0; k < val.count; k++) {
            if (val.data[k] == ';') {
                if (k > start) {
                    String_View item = nob_sv_from_parts(val.data + start, k - start);
                    item = resolve_string(ctx, item);
                    string_list_add(out_list, ctx->arena, item);
                }
                start = k + 1;
            }
        }
        
        if (start < val.count) {
            String_View item = nob_sv_from_parts(val.data + start, val.count - start);
            item = resolve_string(ctx, item);
            string_list_add(out_list, ctx->arena, item);
        }
    }
}

// ============================================================================
// AVALIAÇÃO DE COMANDOS DO CMAKE
// ============================================================================

// Avalia o comando 'message'
static bool separate_arguments_is_mode(String_View mode) {
    return sv_eq_ci(mode, sv_from_cstr("UNIX_COMMAND")) ||
           sv_eq_ci(mode, sv_from_cstr("WINDOWS_COMMAND")) ||
           sv_eq_ci(mode, sv_from_cstr("NATIVE_COMMAND"));
}

static void split_command_line_like_cmake(Evaluator_Context *ctx, String_View input, String_List *out) {
    if (!ctx || !out) return;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        String_Builder token = {0};
        bool in_quote = false;
        char quote_char = '\0';

        while (i < input.count) {
            char c = input.data[i];

            if (c == '\\' && (i + 1) < input.count &&
                (input.data[i + 1] == '"' || input.data[i + 1] == '\'')) {
                i++;
                c = input.data[i];
            }

            if (!in_quote && isspace((unsigned char)c)) break;

            if ((c == '"' || c == '\'')) {
                if (!in_quote) {
                    in_quote = true;
                    quote_char = c;
                    i++;
                    continue;
                }
                if (quote_char == c) {
                    in_quote = false;
                    quote_char = '\0';
                    i++;
                    continue;
                }
            }

            if (c == '\\' && (i + 1) < input.count) {
                char next = input.data[i + 1];
                if (next == '\\' || isspace((unsigned char)next)) {
                    sb_append(&token, next);
                    i += 2;
                    continue;
                }
            }

            sb_append(&token, c);
            i++;
        }

        if (token.count > 0) {
            String_View item = sv_from_cstr(arena_strndup(ctx->arena, token.items, token.count));
            string_list_add(out, ctx->arena, item);
        }
        nob_sb_free(token);
    }
}

// Avalia o comando 'separate_arguments'
static void eval_separate_arguments_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View input = sv_from_cstr("");

    if (args.count == 1) {
        input = eval_get_var(ctx, out_var);
    } else {
        size_t start = 1;
        String_View mode = resolve_arg(ctx, args.items[1]);
        if (separate_arguments_is_mode(mode)) {
            start = 2;
        }

        String_Builder sb = {0};
        for (size_t i = start; i < args.count; i++) {
            String_View piece = resolve_arg(ctx, args.items[i]);
            if (piece.count == 0) continue;
            if (sb.count > 0) sb_append(&sb, ' ');
            sb_append_buf(&sb, piece.data, piece.count);
        }
        input = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
        nob_sb_free(sb);
    }

    String_List tokens = {0};
    string_list_init(&tokens);
    split_command_line_like_cmake(ctx, input, &tokens);

    String_Builder out = {0};
    for (size_t i = 0; i < tokens.count; i++) {
        if (i > 0) sb_append(&out, ';');
        sb_append_buf(&out, tokens.items[i].data, tokens.items[i].count);
    }
    eval_set_var(ctx, out_var, sb_to_sv(out), false, false);
    nob_sb_free(out);
}

static void eval_list_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View mode = resolve_arg(ctx, args.items[0]);
    String_View var_name = resolve_arg(ctx, args.items[1]);

    if (nob_sv_eq(mode, sv_from_cstr("APPEND")) || nob_sv_eq(mode, sv_from_cstr("PREPEND"))) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_List incoming = {0};
        resolve_args_to_list(ctx, args, 2, &incoming);

        String_List out = {0};
        string_list_init(&out);
        if (nob_sv_eq(mode, sv_from_cstr("PREPEND"))) {
            for (size_t i = 0; i < incoming.count; i++) string_list_add(&out, ctx->arena, incoming.items[i]);
            for (size_t i = 0; i < items.count; i++) string_list_add(&out, ctx->arena, items.items[i]);
        } else {
            for (size_t i = 0; i < items.count; i++) string_list_add(&out, ctx->arena, items.items[i]);
            for (size_t i = 0; i < incoming.count; i++) string_list_add(&out, ctx->arena, incoming.items[i]);
        }
        list_store_var(ctx, var_name, &out);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("REMOVE_ITEM"))) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_List remove = {0};
        resolve_args_to_list(ctx, args, 2, &remove);
        String_List out = {0};
        string_list_init(&out);
        for (size_t i = 0; i < items.count; i++) {
            bool should_remove = false;
            for (size_t j = 0; j < remove.count; j++) {
                if (nob_sv_eq(items.items[i], remove.items[j])) {
                    should_remove = true;
                    break;
                }
            }
            if (!should_remove) string_list_add(&out, ctx->arena, items.items[i]);
        }
        list_store_var(ctx, var_name, &out);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("REMOVE_DUPLICATES"))) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_List out = {0};
        string_list_init(&out);
        for (size_t i = 0; i < items.count; i++) {
            bool duplicate = false;
            for (size_t j = 0; j < out.count; j++) {
                if (nob_sv_eq(items.items[i], out.items[j])) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) string_list_add(&out, ctx->arena, items.items[i]);
        }
        list_store_var(ctx, var_name, &out);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("LENGTH")) && args.count >= 3) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_View out_var = resolve_arg(ctx, args.items[2]);
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf("%zu", items.count)), false, false);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("GET")) && args.count >= 4) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_View out_var = resolve_arg(ctx, args.items[args.count - 1]);
        String_Builder sb = {0};
        bool first = true;
        for (size_t i = 2; i + 1 < args.count; i++) {
            long idx = parse_long_or_default(resolve_arg(ctx, args.items[i]), LONG_MIN);
            size_t offset = 0;
            if (!list_index_to_offset(items.count, idx, &offset)) continue;
            if (!first) sb_append(&sb, ';');
            sb_append_buf(&sb, items.items[offset].data, items.items[offset].count);
            first = false;
        }
        eval_set_var(ctx, out_var, sb_to_sv(sb), false, false);
        nob_sb_free(sb);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("FIND")) && args.count >= 4) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_View needle = resolve_arg(ctx, args.items[2]);
        String_View out_var = resolve_arg(ctx, args.items[3]);
        long found = -1;
        for (size_t i = 0; i < items.count; i++) {
            if (nob_sv_eq(items.items[i], needle)) {
                found = (long)i;
                break;
            }
        }
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf("%ld", found)), false, false);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("JOIN")) && args.count >= 4) {
        String_List items = {0};
        list_load_var(ctx, var_name, &items);
        String_View glue = resolve_arg(ctx, args.items[2]);
        String_View out_var = resolve_arg(ctx, args.items[3]);
        String_Builder sb = {0};
        for (size_t i = 0; i < items.count; i++) {
            if (i > 0) sb_append_buf(&sb, glue.data, glue.count);
            sb_append_buf(&sb, items.items[i].data, items.items[i].count);
        }
        eval_set_var(ctx, out_var, sb_to_sv(sb), false, false);
        nob_sb_free(sb);
        return;
    }
}

// Avalia o comando 'list(REMOVE_ITEM ...)'
static void eval_remove_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;
    String_View var_name = resolve_arg(ctx, args.items[0]);
    if (var_name.count == 0) return;

    String_List items = {0};
    list_load_var(ctx, var_name, &items);
    String_List remove = {0};
    resolve_args_to_list(ctx, args, 1, &remove);

    String_List out = {0};
    string_list_init(&out);
    for (size_t i = 0; i < items.count; i++) {
        bool should_remove = false;
        for (size_t j = 0; j < remove.count; j++) {
            if (nob_sv_eq(items.items[i], remove.items[j])) {
                should_remove = true;
                break;
            }
        }
        if (!should_remove) string_list_add(&out, ctx->arena, items.items[i]);
    }
    list_store_var(ctx, var_name, &out);
}

// Avalia o comando 'variable_requires'
static void eval_variable_requires_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View test_var_name = resolve_arg(ctx, args.items[0]);
    String_View result_var_name = resolve_arg(ctx, args.items[1]);
    if (test_var_name.count == 0 || result_var_name.count == 0) return;

    String_View test_value = eval_get_var(ctx, test_var_name);
    if (cmake_string_is_false(test_value)) return;

    bool all_ok = true;
    for (size_t i = 2; i < args.count; i++) {
        String_View required_name = resolve_arg(ctx, args.items[i]);
        if (required_name.count == 0) continue;
        String_View required_value = eval_get_var(ctx, required_name);
        bool ok = !cmake_string_is_false(required_value);
        if (!ok) {
            all_ok = false;
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "variable_requires",
                nob_temp_sprintf("variavel requerida nao satisfeita: "SV_Fmt, SV_Arg(required_name)),
                "garanta que a variavel esteja definida com valor verdadeiro antes de chamar variable_requires");
        }
    }

    eval_set_var(ctx, result_var_name, all_ok ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"), false, false);
}

// Avalia o comando 'write_file'
static void eval_write_file_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View raw_path = resolve_arg(ctx, args.items[0]);
    if (raw_path.count == 0) return;
    String_View output_path = path_is_absolute_sv(raw_path)
        ? raw_path
        : path_join_arena(ctx->arena, ctx->current_binary_dir, raw_path);

    bool append_mode = false;
    String_Builder content = {0};
    bool first = true;
    for (size_t i = 1; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("APPEND"))) {
            append_mode = true;
            continue;
        }
        if (!first) sb_append(&content, ' ');
        sb_append_buf(&content, tok.data, tok.count);
        first = false;
    }

    if (!append_mode) {
        if (!ensure_parent_dirs_for_path(ctx->arena, output_path)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "write_file",
                nob_temp_sprintf("falha ao preparar diretorio para escrita: "SV_Fmt, SV_Arg(output_path)),
                "verifique permissao de escrita e caminho de destino");
            nob_sb_free(content);
            return;
        }
        if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), content.items ? content.items : "", content.count)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "write_file",
                nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(output_path)),
                "verifique permissao de escrita e caminho de destino");
        }
        nob_sb_free(content);
        return;
    }

    Nob_String_Builder existing = {0};
    if (nob_file_exists(nob_temp_sv_to_cstr(output_path))) {
        if (!nob_read_entire_file(nob_temp_sv_to_cstr(output_path), &existing)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "write_file",
                nob_temp_sprintf("falha ao ler arquivo para append: "SV_Fmt, SV_Arg(output_path)),
                "verifique permissao de leitura e integridade do arquivo");
            nob_sb_free(content);
            return;
        }
    }

    String_Builder merged = {0};
    sb_append_buf(&merged, existing.items ? existing.items : "", existing.count);
    sb_append_buf(&merged, content.items ? content.items : "", content.count);

    if (!ensure_parent_dirs_for_path(ctx->arena, output_path)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "write_file",
            nob_temp_sprintf("falha ao preparar diretorio para append: "SV_Fmt, SV_Arg(output_path)),
            "verifique permissao de escrita e caminho de destino");
        nob_sb_free(existing);
        nob_sb_free(content);
        nob_sb_free(merged);
        return;
    }
    if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), merged.items ? merged.items : "", merged.count)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "write_file",
            nob_temp_sprintf("falha ao gravar append em arquivo: "SV_Fmt, SV_Arg(output_path)),
            "verifique permissao de escrita e caminho de destino");
    }

    nob_sb_free(existing);
    nob_sb_free(content);
    nob_sb_free(merged);
}

// Avalia o comando 'string'
static void eval_string_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View mode = resolve_arg(ctx, args.items[0]);

    if (nob_sv_eq(mode, sv_from_cstr("APPEND")) && args.count >= 2) {
        String_View var_name = resolve_arg(ctx, args.items[1]);
        String_View curr = eval_get_var(ctx, var_name);
        String_Builder sb = {0};
        sb_append_buf(&sb, curr.data, curr.count);
        for (size_t i = 2; i < args.count; i++) {
            String_View piece = resolve_arg(ctx, args.items[i]);
            sb_append_buf(&sb, piece.data, piece.count);
        }
        eval_set_var(ctx, var_name, sb_to_sv(sb), false, false);
        nob_sb_free(sb);
        return;
    }

    if ((nob_sv_eq(mode, sv_from_cstr("TOUPPER")) || nob_sv_eq(mode, sv_from_cstr("TOLOWER")) ||
         nob_sv_eq(mode, sv_from_cstr("STRIP"))) && args.count >= 3) {
        String_View input = resolve_arg(ctx, args.items[1]);
        String_View out_var = resolve_arg(ctx, args.items[2]);
        String_View out = input;

        if (nob_sv_eq(mode, sv_from_cstr("TOUPPER")) || nob_sv_eq(mode, sv_from_cstr("TOLOWER"))) {
            char *buf = arena_strndup(ctx->arena, input.data, input.count);
            for (size_t i = 0; i < input.count; i++) {
                buf[i] = nob_sv_eq(mode, sv_from_cstr("TOUPPER"))
                    ? (char)toupper((unsigned char)buf[i])
                    : (char)tolower((unsigned char)buf[i]);
            }
            out = sv_from_cstr(buf);
        } else {
            size_t start = 0;
            size_t end = input.count;
            while (start < end && isspace((unsigned char)input.data[start])) start++;
            while (end > start && isspace((unsigned char)input.data[end - 1])) end--;
            out = nob_sv_from_parts(input.data + start, end - start);
        }

        eval_set_var(ctx, out_var, out, false, false);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("REPLACE")) && args.count >= 5) {
        String_View needle = resolve_arg(ctx, args.items[1]);
        String_View repl = resolve_arg(ctx, args.items[2]);
        String_View out_var = resolve_arg(ctx, args.items[3]);
        String_Builder input_sb = {0};
        for (size_t i = 4; i < args.count; i++) {
            if (i > 4) sb_append(&input_sb, ' ');
            String_View p = resolve_arg(ctx, args.items[i]);
            sb_append_buf(&input_sb, p.data, p.count);
        }
        String_View input = sb_to_sv(input_sb);

        String_Builder out = {0};
        size_t i = 0;
        if (needle.count == 0) {
            sb_append_buf(&out, input.data, input.count);
        } else {
            while (i < input.count) {
                if (i + needle.count <= input.count &&
                    memcmp(input.data + i, needle.data, needle.count) == 0) {
                    sb_append_buf(&out, repl.data, repl.count);
                    i += needle.count;
                } else {
                    sb_append(&out, input.data[i]);
                    i++;
                }
            }
        }

        eval_set_var(ctx, out_var, sb_to_sv(out), false, false);
        nob_sb_free(input_sb);
        nob_sb_free(out);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("JOIN")) && args.count >= 4) {
        String_View glue = resolve_arg(ctx, args.items[1]);
        String_View out_var = resolve_arg(ctx, args.items[2]);
        String_Builder out = {0};
        for (size_t i = 3; i < args.count; i++) {
            String_View p = resolve_arg(ctx, args.items[i]);
            if (i > 3) sb_append_buf(&out, glue.data, glue.count);
            sb_append_buf(&out, p.data, p.count);
        }
        eval_set_var(ctx, out_var, sb_to_sv(out), false, false);
        nob_sb_free(out);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("REGEX")) && args.count >= 5) {
        String_View submode = resolve_arg(ctx, args.items[1]);
        String_View pattern = resolve_arg(ctx, args.items[2]);
        String_View out_var = resolve_arg(ctx, args.items[3]);
        String_Builder input_sb = {0};
        for (size_t i = 4; i < args.count; i++) {
            if (i > 4) sb_append(&input_sb, ' ');
            String_View p = resolve_arg(ctx, args.items[i]);
            sb_append_buf(&input_sb, p.data, p.count);
        }
        String_View input = sb_to_sv(input_sb);

        if (nob_sv_eq(submode, sv_from_cstr("MATCH"))) {
            if (nob_sv_eq(pattern, sv_from_cstr("#define LIBCURL_VERSION \"[^\"]*"))) {
                String_View key = sv_from_cstr("#define LIBCURL_VERSION \"");
                const char *hit = strstr(nob_temp_sv_to_cstr(input), nob_temp_sv_to_cstr(key));
                if (hit) {
                    const char *end = strchr(hit + key.count, '"');
                    size_t len = end ? (size_t)(end - hit) : strlen(hit);
                    eval_set_var(ctx, out_var, nob_sv_from_parts(hit, len), false, false);
                } else {
                    eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                }
            } else if (nob_sv_eq(pattern, sv_from_cstr("#define LIBCURL_VERSION_NUM 0x[0-9a-fA-F]+"))) {
                String_View key = sv_from_cstr("#define LIBCURL_VERSION_NUM 0x");
                const char *hit = strstr(nob_temp_sv_to_cstr(input), nob_temp_sv_to_cstr(key));
                if (hit) {
                    const char *p = hit + key.count;
                    while (*p && isxdigit((unsigned char)*p)) p++;
                    eval_set_var(ctx, out_var, nob_sv_from_parts(hit, (size_t)(p - hit)), false, false);
                } else {
                    eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                }
            } else if (strstr(nob_temp_sv_to_cstr(pattern), "MINGW64_VERSION=") != NULL) {
                const char *prefix = "MINGW64_VERSION=";
                const char *hit = strstr(nob_temp_sv_to_cstr(input), prefix);
                if (hit) {
                    const char *p = hit + strlen(prefix);
                    const char *num_start = p;
                    while (*p && isdigit((unsigned char)*p)) p++;
                    if (*p == '.') p++;
                    while (*p && isdigit((unsigned char)*p)) p++;
                    if (p > num_start) {
                        eval_set_var(ctx, out_var, nob_sv_from_parts(hit, (size_t)(p - hit)), false, false);
                    } else {
                        eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                    }
                } else {
                    eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                }
            } else if (strstr(nob_temp_sv_to_cstr(pattern), "_WIN32_WINNT=0x") != NULL) {
                const char *prefix = "_WIN32_WINNT=0x";
                const char *hit = strstr(nob_temp_sv_to_cstr(input), prefix);
                if (hit) {
                    const char *p = hit + strlen(prefix);
                    while (*p && isxdigit((unsigned char)*p)) p++;
                    eval_set_var(ctx, out_var, nob_sv_from_parts(hit, (size_t)(p - hit)), false, false);
                } else {
                    eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                }
            } else {
                const char *hit = strstr(nob_temp_sv_to_cstr(input), nob_temp_sv_to_cstr(pattern));
                if (hit) {
                    eval_set_var(ctx, out_var, sv_from_cstr(arena_strdup(ctx->arena, hit)), false, false);
                } else {
                    const char *mingw_hit = strstr(nob_temp_sv_to_cstr(input), "MINGW64_VERSION=");
                    if (mingw_hit) {
                        const char *p = mingw_hit + strlen("MINGW64_VERSION=");
                        while (*p && (isdigit((unsigned char)*p) || *p == '.')) p++;
                        eval_set_var(ctx, out_var, nob_sv_from_parts(mingw_hit, (size_t)(p - mingw_hit)), false, false);
                    } else {
                        const char *winnt_hit = strstr(nob_temp_sv_to_cstr(input), "_WIN32_WINNT=0x");
                        if (winnt_hit) {
                            const char *p = winnt_hit + strlen("_WIN32_WINNT=0x");
                            while (*p && isxdigit((unsigned char)*p)) p++;
                            eval_set_var(ctx, out_var, nob_sv_from_parts(winnt_hit, (size_t)(p - winnt_hit)), false, false);
                        } else {
                            eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
                        }
                    }
                }
            }
        } else if (nob_sv_eq(submode, sv_from_cstr("REPLACE")) && args.count >= 6) {
            String_View replacement = resolve_arg(ctx, args.items[3]);
            out_var = resolve_arg(ctx, args.items[4]);
            nob_sb_free(input_sb);
            input_sb = (String_Builder){0};
            for (size_t i = 5; i < args.count; i++) {
                if (i > 5) sb_append(&input_sb, ' ');
                String_View p = resolve_arg(ctx, args.items[i]);
                sb_append_buf(&input_sb, p.data, p.count);
            }
            input = sb_to_sv(input_sb);
            String_View replaced = regex_replace_with_backrefs(ctx, pattern, input, replacement);
            eval_set_var(ctx, out_var, replaced, false, false);
        }

        nob_sb_free(input_sb);
        return;
    }
}

// Avalia o comando 'message'
static void eval_message_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    
    String_View type = resolve_arg(ctx, args.items[0]);
    bool has_explicit_type =
        nob_sv_eq(type, sv_from_cstr("STATUS")) ||
        nob_sv_eq(type, sv_from_cstr("WARNING")) ||
        nob_sv_eq(type, sv_from_cstr("AUTHOR_WARNING")) ||
        nob_sv_eq(type, sv_from_cstr("SEND_ERROR")) ||
        nob_sv_eq(type, sv_from_cstr("ERROR")) ||
        nob_sv_eq(type, sv_from_cstr("FATAL_ERROR")) ||
        nob_sv_eq(type, sv_from_cstr("NOTICE")) ||
        nob_sv_eq(type, sv_from_cstr("VERBOSE")) ||
        nob_sv_eq(type, sv_from_cstr("DEBUG")) ||
        nob_sv_eq(type, sv_from_cstr("TRACE"));
    
    String_Builder sb = {0};
    bool first = true;
    
    size_t start_idx = has_explicit_type ? 1 : 0;
    for (size_t i = start_idx; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (!first) sb_append_cstr(&sb, " ");
        sb_append_buf(&sb, arg.data, arg.count);
        first = false;
    }
    
    String_View message = sb_to_sv(sb);
    
    if (nob_sv_eq(type, sv_from_cstr("STATUS"))) {
        nob_log(NOB_INFO, SV_Fmt, SV_Arg(message));
    } else if (nob_sv_eq(type, sv_from_cstr("WARNING"))) {
        nob_log(NOB_WARNING, SV_Fmt, SV_Arg(message));
    } else if (nob_sv_eq(type, sv_from_cstr("ERROR"))) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "message",
            nob_temp_sv_to_cstr(message), "");
    } else if (nob_sv_eq(type, sv_from_cstr("FATAL_ERROR"))) {
        if (ctx->continue_on_fatal_error) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "message",
                nob_temp_sv_to_cstr(message), "FATAL_ERROR ignorado por modo de compatibilidade");
        } else {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "message",
                nob_temp_sv_to_cstr(message), "avaliacao interrompida por FATAL_ERROR");
            ctx->skip_evaluation = true;
        }
    } else {
        // Tipo desconhecido, trata como STATUS
        nob_log(NOB_INFO, SV_Fmt, SV_Arg(message));
    }
    
    nob_sb_free(sb);
}


// ============================================================================
// COMPILAÇÃO E BUILD
// ============================================================================

static void eval_target_compile_definitions_command(Evaluator_Context *ctx, Args args) {
    Target_Vis_Args tv;
    if (!eval_parse_target_visibility(ctx, args, 3, &tv)) return;

    for (size_t i = tv.start_idx; i < args.count; i++) {
        String_View definition = resolve_arg(ctx, args.items[i]);
        build_target_add_definition(tv.target, ctx->arena, definition, tv.visibility, CONFIG_ALL);
    }
}

static void eval_target_compile_options_command(Evaluator_Context *ctx, Args args) {
    Target_Vis_Args tv;
    if (!eval_parse_target_visibility(ctx, args, 3, &tv)) return;

    for (size_t i = tv.start_idx; i < args.count; i++) {
        String_View option = resolve_arg(ctx, args.items[i]);
        build_target_add_compile_option(tv.target, ctx->arena, option, tv.visibility, CONFIG_ALL);
    }
}

static void eval_target_sources_command(Evaluator_Context *ctx, Args args) {
    Target_Vis_Args tv;
    // no seu código atual: args.count < 2 retorna.
    // mas ele aceita vis opcional e itens depois, então:
    // - mínimo total = 2 (target + pelo menos 1 item)
    // - continua compatível com o comportamento atual (que aceitava args.count==2)
    if (!eval_parse_target_visibility(ctx, args, 2, &tv)) return;

    for (size_t i = tv.start_idx; i < args.count; i++) {
        String_View src = resolve_arg(ctx, args.items[i]);
        if (tv.visibility != VISIBILITY_INTERFACE) {
            build_target_add_source(tv.target, ctx->arena, src);
        }
    }
}

static void list_load_var(Evaluator_Context *ctx, String_View var_name, String_List *out) {
    string_list_init(out);
    String_View curr = eval_get_var(ctx, var_name);
    split_semicolon_list(ctx, curr, out);
}

static void list_store_var(Evaluator_Context *ctx, String_View var_name, String_List *list) {
    String_Builder sb = {0};
    for (size_t i = 0; i < list->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, list->items[i].data, list->items[i].count);
    }
    eval_set_var(ctx, var_name, sb_to_sv(sb), false, false);
    nob_sb_free(sb);
}

static long parse_long_or_default(String_View sv, long fallback) {
    const char *text = nob_temp_sv_to_cstr(sv);
    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (!endptr || endptr == text || *endptr != '\0') return fallback;
    return value;
}


static void eval_math_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;

    String_View mode = resolve_arg(ctx, args.items[0]);
    if (!sv_eq_ci(mode, sv_from_cstr("EXPR"))) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "math",
            "math chamado com argumentos incorretos",
            "use math(EXPR <var> <expr> [OUTPUT_FORMAT <DECIMAL|HEXADECIMAL>])");
        return;
    }

    String_View out_var = resolve_arg(ctx, args.items[1]);
    String_View expr = resolve_arg(ctx, args.items[2]);
    bool output_hex = false;

    for (size_t i = 3; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_FORMAT")) && i + 1 < args.count) {
            String_View fmt = resolve_arg(ctx, args.items[i + 1]);
            if (sv_eq_ci(fmt, sv_from_cstr("HEXADECIMAL"))) {
                output_hex = true;
            } else if (sv_eq_ci(fmt, sv_from_cstr("DECIMAL"))) {
                output_hex = false;
            } else {
                diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "math",
                    nob_temp_sprintf("valor invalido para OUTPUT_FORMAT: "SV_Fmt, SV_Arg(fmt)),
                    "use OUTPUT_FORMAT DECIMAL ou HEXADECIMAL");
                eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
                return;
            }
            i++;
        } else if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_FORMAT")) && i + 1 >= args.count) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "math",
                "math EXPR chamado com argumentos incorretos",
                "forneca OUTPUT_FORMAT seguido de DECIMAL ou HEXADECIMAL");
            eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
            return;
        } else {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "math",
                nob_temp_sprintf("argumento inesperado em math(EXPR ...): "SV_Fmt, SV_Arg(tok)),
                "use apenas OUTPUT_FORMAT como argumento opcional");
            eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
            return;
        }
    }

    long long value = 0;
    Math_Eval_Status status = math_eval_i64(expr, &value);
    if (status != MATH_EVAL_OK) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "math",
            nob_temp_sprintf("falha ao avaliar expressao: "SV_Fmt, SV_Arg(expr)),
            "verifique operadores, divisao por zero e range de 64 bits");
        eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
        return;
    }

    if (output_hex) {
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf("0x%llx", (unsigned long long)value)), false, false);
    } else {
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf("%lld", value)), false, false);
    }
}

static bool list_index_to_offset(size_t count, long idx, size_t *out) {
    if (count == 0) return false;
    long pos = idx;
    if (idx < 0) pos = (long)count + idx;
    if (pos < 0 || pos >= (long)count) return false;
    *out = (size_t)pos;
    return true;
}

static String_View regex_replace_with_backrefs(Evaluator_Context *ctx, String_View pattern, String_View input, String_View replacement) {
    if (nob_sv_eq(pattern, sv_from_cstr("[^\"]+\"")) && replacement.count == 0) {
        for (size_t i = 0; i < input.count; i++) {
            if (input.data[i] == '"') {
                return nob_sv_from_parts(input.data + i + 1, input.count - i - 1);
            }
        }
        return input;
    }

    if (nob_sv_eq(pattern, sv_from_cstr("[^0]+0x")) && replacement.count == 0) {
        for (size_t i = 0; i + 1 < input.count; i++) {
            if (input.data[i] == '0' && (input.data[i + 1] == 'x' || input.data[i + 1] == 'X')) {
                return nob_sv_from_parts(input.data + i, input.count - i);
            }
        }
        return input;
    }

    if (nob_sv_eq(pattern, sv_from_cstr("([0-9]+\\.[0-9]+\\.[0-9]+).+")) &&
        nob_sv_eq(replacement, sv_from_cstr("\\1"))) {
        size_t i = 0;
        while (i < input.count && !isdigit((unsigned char)input.data[i])) i++;
        size_t start = i;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i >= input.count || input.data[i] != '.') return input;
        i++;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i >= input.count || input.data[i] != '.') return input;
        i++;
        while (i < input.count && isdigit((unsigned char)input.data[i])) i++;
        if (i <= start) return input;
        return nob_sv_from_parts(input.data + start, i - start);
    }

    if (pattern.count == 0) return input;

    String_Builder out = {0};
    size_t i = 0;
    while (i < input.count) {
        if (i + pattern.count <= input.count &&
            memcmp(input.data + i, pattern.data, pattern.count) == 0) {
            sb_append_buf(&out, replacement.data, replacement.count);
            i += pattern.count;
        } else {
            sb_append(&out, input.data[i]);
            i++;
        }
    }
    String_View result = out.count > 0
        ? sv_from_cstr(arena_strndup(ctx->arena, out.items, out.count))
        : sv_from_cstr("");
    nob_sb_free(out);
    return result;
}


// Avalia o comando ''add_custom_command''
static void eval_add_custom_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;

    String_View first = resolve_arg(ctx, args.items[0]);
    bool mode_target = sv_eq_ci(first, sv_from_cstr("TARGET"));
    bool mode_output = sv_eq_ci(first, sv_from_cstr("OUTPUT"));
    if (!mode_target && !mode_output) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
            "assinatura nao suportada", "use add_custom_command(TARGET ...) ou add_custom_command(OUTPUT ...)");
        return;
    }

    Build_Target *target = NULL;
    bool pre_build = true;
    bool post_build = false;
    bool got_stage_keyword = false;
    bool append_mode = false;
    bool verbatim = false;
    bool uses_terminal = false;
    bool command_expand_lists = false;
    bool depends_explicit_only = false;
    bool codegen = false;
    String_View working_dir = sv_from_cstr("");
    String_View comment = sv_from_cstr("");
    String_View main_dependency = sv_from_cstr("");
    String_View depfile = sv_from_cstr("");
    String_List outputs = {0}, byproducts = {0}, depends = {0}, commands = {0};
    string_list_init(&outputs);
    string_list_init(&byproducts);
    string_list_init(&depends);
    string_list_init(&commands);

    size_t i = 1;
    if (mode_target) {
        if (args.count < 3) return;
        String_View target_name = resolve_arg(ctx, args.items[1]);
        target = build_model_find_target(ctx->model, target_name);
        if (!target) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
                nob_temp_sprintf("target nao encontrado: "SV_Fmt, SV_Arg(target_name)),
                "defina o target antes de associar custom command");
            return;
        }
        i = 2;
    } else {
        i = parse_custom_command_list(ctx, args, 1, &outputs);
    }

    while (i < args.count) {
        String_View key = resolve_arg(ctx, args.items[i]);

        if (sv_eq_ci(key, sv_from_cstr("OUTPUT"))) {
            i = parse_custom_command_list(ctx, args, i + 1, &outputs);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("PRE_BUILD"))) {
            got_stage_keyword = true;
            pre_build = true;
            post_build = false;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("POST_BUILD"))) {
            got_stage_keyword = true;
            post_build = true;
            pre_build = false;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("PRE_LINK"))) {
            got_stage_keyword = true;
            pre_build = true;
            post_build = false;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("DEPENDS"))) {
            i = parse_custom_command_list(ctx, args, i + 1, &depends);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("BYPRODUCTS"))) {
            i = parse_custom_command_list(ctx, args, i + 1, &byproducts);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("MAIN_DEPENDENCY")) && i + 1 < args.count) {
            main_dependency = resolve_arg(ctx, args.items[i + 1]);
            if (main_dependency.count > 0) string_list_add_unique(&depends, ctx->arena, main_dependency);
            i += 2;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("IMPLICIT_DEPENDS"))) {
            i++;
            while (i + 1 < args.count) {
                String_View maybe_kw = resolve_arg(ctx, args.items[i]);
                if (is_custom_command_keyword(maybe_kw)) break;
                // Par linguagem/arquivo: linguagem e arquivo dependente.
                String_View implicit_dep = resolve_arg(ctx, args.items[i + 1]);
                if (implicit_dep.count > 0) string_list_add_unique(&depends, ctx->arena, implicit_dep);
                i += 2;
            }
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("DEPFILE")) && i + 1 < args.count) {
            depfile = resolve_arg(ctx, args.items[i + 1]);
            i += 2;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("WORKING_DIRECTORY")) && i + 1 < args.count) {
            working_dir = resolve_arg(ctx, args.items[i + 1]);
            i += 2;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("COMMENT")) && i + 1 < args.count) {
            comment = resolve_arg(ctx, args.items[i + 1]);
            i += 2;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("APPEND"))) {
            append_mode = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("VERBATIM"))) {
            verbatim = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("USES_TERMINAL"))) {
            uses_terminal = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("COMMAND_EXPAND_LISTS"))) {
            command_expand_lists = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("DEPENDS_EXPLICIT_ONLY"))) {
            depends_explicit_only = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("CODEGEN"))) {
            codegen = true;
            i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("JOB_POOL")) || sv_eq_ci(key, sv_from_cstr("JOB_SERVER_AWARE"))) {
            if (i + 1 < args.count) i += 2;
            else i++;
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("COMMAND"))) {
            size_t cmd_start = i + 1;
            size_t cmd_end = cmd_start;
            while (cmd_end < args.count) {
                String_View part = resolve_arg(ctx, args.items[cmd_end]);
                if (is_custom_command_keyword(part)) break;
                cmd_end++;
            }
            if (cmd_start < cmd_end) {
                String_View maybe_args = resolve_arg(ctx, args.items[cmd_start]);
                if (sv_eq_ci(maybe_args, sv_from_cstr("ARGS"))) {
                    cmd_start++;
                }
            }
            String_View command = join_command_args(ctx, args, cmd_start, cmd_end, command_expand_lists);
            if (command.count > 0) string_list_add(&commands, ctx->arena, command);
            i = cmd_end;
            continue;
        }
        i++;
    }

    if (commands.count == 0) return;
    if (mode_target && !got_stage_keyword) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
            "TARGET sem PRE_BUILD/PRE_LINK/POST_BUILD", "assumindo PRE_BUILD por compatibilidade");
    }
    if (mode_output && outputs.count == 0) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
            "forma OUTPUT sem arquivos de saida", "adicione OUTPUT <arquivo...>");
        return;
    }
    if (mode_target && append_mode) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
            "APPEND com assinatura TARGET nao e suportado no CMake", "ignorado para manter compatibilidade");
    }

    if (main_dependency.count > 0) string_list_add_unique(&depends, ctx->arena, main_dependency);
    if (depfile.count > 0) string_list_add_unique(&byproducts, ctx->arena, depfile);
    if (mode_target) {
        for (size_t d = 0; d < depends.count; d++) {
            Build_Target *dep_target = build_model_find_target(ctx->model, depends.items[d]);
            if (dep_target) {
                build_target_add_dependency(target, ctx->arena, dep_target->name);
            }
        }
    }

    String_View merged_command = join_commands_with_and(ctx->arena, commands);
    if (mode_target) {
        Custom_Command *cmd = evaluator_target_add_custom_command_ex(target, ctx->arena, pre_build || !post_build, merged_command, working_dir, comment);
        if (!cmd) return;
        custom_command_copy_list(ctx->arena, &cmd->outputs, &outputs);
        custom_command_copy_list(ctx->arena, &cmd->byproducts, &byproducts);
        custom_command_copy_list(ctx->arena, &cmd->depends, &depends);
        cmd->main_dependency = main_dependency;
        cmd->depfile = depfile;
        cmd->append = append_mode;
        cmd->verbatim = verbatim;
        cmd->uses_terminal = uses_terminal;
        cmd->command_expand_lists = command_expand_lists;
        cmd->depends_explicit_only = depends_explicit_only;
        cmd->codegen = codegen;
    } else {
        Custom_Command *cmd = NULL;
        if (append_mode) {
            cmd = find_output_custom_command_by_output(ctx->model, outputs.items[0]);
            if (!cmd) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_custom_command",
                    "APPEND sem comando OUTPUT anterior", "criando novo comando para manter compatibilidade");
            }
        }
        if (!cmd) {
            cmd = build_model_add_output_custom_command(ctx->model, ctx->arena, merged_command, working_dir, comment);
        } else {
            append_custom_command_command(ctx->arena, cmd, merged_command);
        }
        if (!cmd) return;
        if (!append_mode) custom_command_copy_list(ctx->arena, &cmd->outputs, &outputs);
        custom_command_copy_list(ctx->arena, &cmd->byproducts, &byproducts);
        custom_command_copy_list(ctx->arena, &cmd->depends, &depends);
        if (cmd->main_dependency.count == 0) cmd->main_dependency = main_dependency;
        if (cmd->depfile.count == 0) cmd->depfile = depfile;
        cmd->append = append_mode;
        cmd->verbatim = cmd->verbatim || verbatim;
        cmd->uses_terminal = cmd->uses_terminal || uses_terminal;
        cmd->command_expand_lists = cmd->command_expand_lists || command_expand_lists;
        cmd->depends_explicit_only = cmd->depends_explicit_only || depends_explicit_only;
        cmd->codegen = cmd->codegen || codegen;
    }
}

// Avalia o comando 'set'
static void eval_set_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    
    String_View key = resolve_arg(ctx, args.items[0]);
    bool parent_scope = false;
    bool cache_var = false;
    
    // Verifica keywords
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("PARENT_SCOPE"))) {
            parent_scope = true;
        } else if (nob_sv_eq(arg, sv_from_cstr("CACHE"))) {
            cache_var = true;
            // Próximo argumento é o tipo, depois docstring - pulamos
            break;
        }
    }
    
    // Concatena valores
    String_Builder sb = {0};
    bool first = true;
    
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        
        // Para em keywords especiais
        if (nob_sv_eq(arg, sv_from_cstr("PARENT_SCOPE")) ||
            nob_sv_eq(arg, sv_from_cstr("CACHE"))) {
            break;
        }
        
        if (!first) sb_append_cstr(&sb, ";");
        sb_append_buf(&sb, arg.data, arg.count);
        first = false;
    }
    
    String_View value = sb_to_sv(sb);
    if (nob_sv_eq(key, sv_from_cstr("CMAKE_BUILD_TYPE")) && value.count > 0) {
        if (sv_eq_ci(value, sv_from_cstr("Debug"))) {
            build_model_set_default_config(ctx->model, sv_from_cstr("Debug"));
        } else if (sv_eq_ci(value, sv_from_cstr("Release"))) {
            build_model_set_default_config(ctx->model, sv_from_cstr("Release"));
        } else if (sv_eq_ci(value, sv_from_cstr("RelWithDebInfo"))) {
            build_model_set_default_config(ctx->model, sv_from_cstr("RelWithDebInfo"));
        } else if (sv_eq_ci(value, sv_from_cstr("MinSizeRel"))) {
            build_model_set_default_config(ctx->model, sv_from_cstr("MinSizeRel"));
        }
    }
    if (nob_sv_starts_with(key, sv_from_cstr("ENV{")) && nob_sv_end_with(key, "}")) {
        String_View env_name = nob_sv_from_parts(key.data + 4, key.count - 5);
        eval_set_env_var(ctx, env_name, value);
    } else {
        eval_set_var(ctx, key, value, parent_scope, cache_var);
        if (nob_sv_starts_with(key, sv_from_cstr("CPACK_"))) {
            cpack_sync_common_metadata(ctx);
        }
    }
    nob_sb_free(sb);
}

// Avalia o comando 'unset'
static void eval_unset_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;

    String_View key = resolve_arg(ctx, args.items[0]);
    bool cache_var = false;
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("CACHE"))) {
            cache_var = true;
            break;
        }
    }

    eval_unset_var(ctx, key, cache_var);
}

// Avalia o comando 'option'
static void eval_option_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    
    String_View name = resolve_arg(ctx, args.items[0]);
    String_View description = args.count > 1 ? resolve_arg(ctx, args.items[1]) : sv_from_cstr("");
    String_View default_val = args.count > 2 ? resolve_arg(ctx, args.items[2]) : sv_from_cstr("OFF");
    
    // Option é uma variável de cache BOOL
    build_model_set_cache_variable(ctx->model, name, default_val, 
                                   sv_from_cstr("BOOL"), description);
}

// Avalia uma expressão de condição a partir de uma string
static bool eval_condition_from_string(Evaluator_Context *ctx, String_View expr) {
    if (!ctx) return false;
    String_View trimmed = genex_trim(expr);
    if (trimmed.count == 0) return false;

    Token_List tokens = {0};
    if (!arena_tokenize(ctx->arena, trimmed, &tokens)) return false;
    if (tokens.count == 0) return false;

    Args cond = {0};
    cond.items = arena_alloc_array_zero(ctx->arena, Arg, tokens.count);
    if (!cond.items) return false;

    for (size_t i = 0; i < tokens.count; i++) {
        cond.items[i].items = arena_alloc_array(ctx->arena, Token, 1);
        if (!cond.items[i].items) return false;
        cond.items[i].items[0] = tokens.items[i];
        cond.items[i].count = 1;
        cond.items[i].capacity = 1;
        cond.count++;
        cond.capacity++;
    }

    return eval_condition(ctx, cond);
}

// Avalia a expressão de dependência de uma opção dependente
static bool eval_dependent_option_depends(Evaluator_Context *ctx, String_View depends_expr) {
    String_View dep = genex_trim(depends_expr);
    if (dep.count == 0) return false;

    size_t start = 0;
    bool any_clause = false;
    for (size_t i = 0; i <= dep.count; i++) {
        bool sep = (i == dep.count) || dep.data[i] == ';';
        if (!sep) continue;

        String_View clause = genex_trim(nob_sv_from_parts(dep.data + start, i - start));
        start = i + 1;
        if (clause.count == 0) continue;
        any_clause = true;
        if (!eval_condition_from_string(ctx, clause)) return false;
    }

    return any_clause;
}

// Avalia o comando 'option' para opções dependentes, que tem uma expressão de dependência
static void eval_cmake_dependent_option_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 5) return;

    String_View name = resolve_arg(ctx, args.items[0]);
    String_View description = resolve_arg(ctx, args.items[1]);
    String_View default_val = resolve_arg(ctx, args.items[2]);
    String_View depends_expr = resolve_arg(ctx, args.items[3]);
    String_View force_val = resolve_arg(ctx, args.items[4]);

    bool enabled = eval_dependent_option_depends(ctx, depends_expr);
    String_View final_val = enabled ? default_val : force_val;

    build_model_set_cache_variable(ctx->model, name, final_val, sv_from_cstr("BOOL"), description);
}

// Avalia o comando 'check_symbol_exists'
static void eval_check_symbol_exists_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;
    String_View symbol = resolve_arg(ctx, args.items[0]);
    String_View headers = resolve_arg(ctx, args.items[1]);
    String_View out_var = resolve_arg(ctx, args.items[2]);
    bool ok = false;
    bool used_probe = false;
    if (eval_real_probes_enabled(ctx)) {
        ok = eval_real_probe_check_symbol_exists(ctx, symbol, headers, &used_probe);
        if (!used_probe) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "check_symbol_exists",
                "probe real indisponivel (compiler nao encontrado); usando heuristica",
                "defina CMAKE_C_COMPILER/CC para habilitar probe real");
        }
    }
    if (!used_probe) {
        ok = eval_check_symbol_exists(ctx, symbol, headers);
    }
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_function_exists'
static void eval_check_function_exists_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View function_name = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    bool ok = eval_check_function_exists(ctx, function_name);
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_include_file'
static void eval_check_include_file_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View header = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    bool ok = eval_check_include_exists(ctx, header);
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_include_files' para multiplos arquivos separados por ';'
static void eval_check_include_files_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View headers = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);

    bool ok = true;
    size_t start = 0;
    for (size_t i = 0; i <= headers.count; i++) {
        bool sep = (i == headers.count) || headers.data[i] == ';';
        if (!sep) continue;
        String_View item = genex_trim(nob_sv_from_parts(headers.data + start, i - start));
        start = i + 1;
        if (item.count == 0) continue;
        if (!eval_check_include_exists(ctx, item)) {
            ok = false;
            break;
        }
    }
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}
// Avalia o comando 'check_type_size'
static void eval_check_type_size_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View type_name = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);

    size_t size_val = 0;
    bool ok = eval_check_type_size_value(type_name, &size_val);
    if (ok) {
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf("%zu", size_val)), false, false);
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("HAVE_%s", nob_temp_sv_to_cstr(out_var))), sv_from_cstr("1"), false, false);
    } else {
        eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("HAVE_%s", nob_temp_sv_to_cstr(out_var))), sv_from_cstr("0"), false, false);
    }
}

// Avalia o comando 'check_c_compiler_flag'
static bool eval_check_c_compiler_flag(Evaluator_Context *ctx, String_View flag) {
    (void)ctx;
    String_View f = genex_trim(flag);
    if (f.count == 0) return false;
    if (cmake_string_is_false(f)) return false;
    if (nob_sv_starts_with(f, sv_from_cstr("-")) || nob_sv_starts_with(f, sv_from_cstr("/"))) {
        return true;
    }
    return cmake_check_truthy_candidate(f);
}

// Avalia o comando 'check_c_compiler_flag' e armazena resultado em variável
static void eval_check_c_compiler_flag_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View flag = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    bool ok = eval_check_c_compiler_flag(ctx, flag);
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_struct_has_member'
static bool eval_check_struct_has_member(Evaluator_Context *ctx, String_View struct_name, String_View member, String_View headers) {
    (void)ctx;
    String_View s = genex_trim(struct_name);
    String_View m = genex_trim(member);
    String_View h = genex_trim(headers);
    if (s.count == 0 || m.count == 0 || h.count == 0) return false;
    if (!cmake_check_truthy_candidate(m)) return false;
    if (!cmake_check_truthy_candidate(h)) return false;
    if (cmake_string_is_false(s)) return false;
    return true;
}

// Avalia o comando 'check_struct_has_member' e armazena resultado em variável
static void eval_check_struct_has_member_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 4) return;
    String_View struct_name = resolve_arg(ctx, args.items[0]);
    String_View member = resolve_arg(ctx, args.items[1]);
    String_View headers = resolve_arg(ctx, args.items[2]);
    String_View out_var = resolve_arg(ctx, args.items[3]);
    bool ok = eval_check_struct_has_member(ctx, struct_name, member, headers);
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_c_source_compiles'
static bool eval_check_c_source_compiles(Evaluator_Context *ctx, String_View source) {
    (void)ctx;
    String_View s = genex_trim(source);
    if (s.count == 0) return false;
    if (cmake_string_is_false(s)) return false;
    return true;
}

// Avalia o comando 'check_c_source_compiles' e armazena resultado em variável
static bool eval_real_probes_enabled(Evaluator_Context *ctx) {
    String_View opt1 = eval_get_var(ctx, sv_from_cstr("CMK2NOB_REAL_PROBES"));
    if (opt1.count > 0) return !cmake_string_is_false(opt1);

    String_View opt2 = eval_get_var(ctx, sv_from_cstr("CMAKE2NOB_REAL_PROBES"));
    if (opt2.count > 0) return !cmake_string_is_false(opt2);

    const char *env1 = getenv("CMK2NOB_REAL_PROBES");
    if (env1 && env1[0] != '\0') return !cmake_string_is_false(sv_from_cstr(env1));

    const char *env2 = getenv("CMAKE2NOB_REAL_PROBES");
    if (env2 && env2[0] != '\0') return !cmake_string_is_false(sv_from_cstr(env2));

    return false;
}

// Obtém o timeout para probes reais a partir de variáveis de ambiente ou opções, com valor padrão de 5000ms
typedef struct {
    bool active;
    bool had_old;
    String_View old_value;
} Eval_Cc_Env_Override;

// Obtem o timeout para probes reais a partir de variaveis de ambiente ou opcoes, com valor padrao de 5000ms
static unsigned long eval_real_probe_timeout_ms(Evaluator_Context *ctx) {
    unsigned long default_ms = 5000;

    if (ctx) {
        String_View opt1 = eval_get_var(ctx, sv_from_cstr("CMK2NOB_REAL_PROBE_TIMEOUT_MS"));
        if (opt1.count > 0) {
            long parsed = strtol(nob_temp_sv_to_cstr(opt1), NULL, 10);
            if (parsed <= 0) return 0;
            return (unsigned long)parsed;
        }

        String_View opt2 = eval_get_var(ctx, sv_from_cstr("CMAKE2NOB_REAL_PROBE_TIMEOUT_MS"));
        if (opt2.count > 0) {
            long parsed = strtol(nob_temp_sv_to_cstr(opt2), NULL, 10);
            if (parsed <= 0) return 0;
            return (unsigned long)parsed;
        }
    }

    const char *env1 = getenv("CMK2NOB_REAL_PROBE_TIMEOUT_MS");
    if (env1 && env1[0] != '\0') {
        long parsed = strtol(env1, NULL, 10);
        if (parsed <= 0) return 0;
        return (unsigned long)parsed;
    }

    const char *env2 = getenv("CMAKE2NOB_REAL_PROBE_TIMEOUT_MS");
    if (env2 && env2[0] != '\0') {
        long parsed = strtol(env2, NULL, 10);
        if (parsed <= 0) return 0;
        return (unsigned long)parsed;
    }

    return default_ms;
}

static Toolchain_Driver eval_make_toolchain_driver(Evaluator_Context *ctx) {
    Toolchain_Driver drv = {0};
    drv.arena = ctx ? ctx->arena : NULL;
    drv.build_dir = ctx ? ctx->current_binary_dir : sv_from_cstr(".");
    drv.timeout_ms = eval_real_probe_timeout_ms(ctx);

    const char *dbg = getenv("CMK2NOB_DEBUG_PROBES");
    drv.debug = (dbg && dbg[0] != '\0' && strcmp(dbg, "0") != 0);
    return drv;
}

static String_View eval_toolchain_compiler(Evaluator_Context *ctx) {
    String_View c_compiler = eval_get_var(ctx, sv_from_cstr("CMAKE_C_COMPILER"));
    if (c_compiler.count > 0) return c_compiler;
    return toolchain_default_c_compiler();
}

static Eval_Cc_Env_Override eval_push_cc_override(Evaluator_Context *ctx, String_View compiler) {
    Eval_Cc_Env_Override ov = {0};
    if (!ctx) return ov;

    String_View cc = genex_trim(compiler);
    if (cc.count == 0) return ov;

    const char *old_cc = getenv("CC");
    if (old_cc && old_cc[0] != '\0') {
        ov.had_old = true;
        ov.old_value = sv_copy_to_arena(ctx->arena, sv_from_cstr(old_cc));
    }

#if defined(_WIN32)
    ov.active = (_putenv_s("CC", nob_temp_sv_to_cstr(cc)) == 0);
#else
    ov.active = (setenv("CC", nob_temp_sv_to_cstr(cc), 1) == 0);
#endif
    return ov;
}

static void eval_pop_cc_override(Eval_Cc_Env_Override *ov) {
    if (!ov || !ov->active) return;
#if defined(_WIN32)
    if (ov->had_old) {
        (void)_putenv_s("CC", nob_temp_sv_to_cstr(ov->old_value));
    } else {
        (void)_putenv_s("CC", "");
    }
#else
    if (ov->had_old) {
        (void)setenv("CC", nob_temp_sv_to_cstr(ov->old_value), 1);
    } else {
        (void)unsetenv("CC");
    }
#endif
}

static bool eval_real_probe_check_library_exists(Evaluator_Context *ctx, String_View library, String_View function_name, String_View location, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!ctx) return false;

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) return false;

    Eval_Cc_Env_Override ov = eval_push_cc_override(ctx, compiler);
    bool ok = toolchain_probe_check_library_exists(&drv, library, function_name, location, used_probe);
    eval_pop_cc_override(&ov);
    return ok;
}

static bool eval_real_probe_check_c_source_runs(Evaluator_Context *ctx, String_View source, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!ctx) return false;

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) return false;

    Eval_Cc_Env_Override ov = eval_push_cc_override(ctx, compiler);
    bool ok = toolchain_probe_check_c_source_runs(&drv, source, used_probe);
    eval_pop_cc_override(&ov);
    return ok;
}

static bool eval_real_probe_check_c_source_compiles(Evaluator_Context *ctx, String_View source, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!ctx) return false;

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) return false;

    Eval_Cc_Env_Override ov = eval_push_cc_override(ctx, compiler);
    bool ok = toolchain_probe_check_c_source_compiles(&drv, source, used_probe);
    eval_pop_cc_override(&ov);
    return ok;
}

static bool eval_real_probe_check_symbol_exists(Evaluator_Context *ctx, String_View symbol, String_View headers, bool *used_probe) {
    if (used_probe) *used_probe = false;
    if (!ctx) return false;

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) return false;

    Eval_Cc_Env_Override ov = eval_push_cc_override(ctx, compiler);
    bool ok = toolchain_probe_check_symbol_exists(&drv, symbol, headers, used_probe);
    eval_pop_cc_override(&ov);
    return ok;
}
static bool eval_check_library_exists(Evaluator_Context *ctx, String_View library, String_View function_name, String_View location) {
    (void)ctx;
    String_View lib = genex_trim(library);
    String_View fun = genex_trim(function_name);
    String_View loc = genex_trim(location);
    if (!cmake_check_truthy_candidate(lib)) return false;
    if (!cmake_check_truthy_candidate(fun)) return false;
    // LOCATION vazio e valido em CMake para usar search path padrao.
    if (loc.count > 0 && cmake_string_is_false(loc)) return false;
    return true;
}

// Avalia o comando 'check_library_exists' e armazena resultado em variável
static bool eval_check_c_source_runs(Evaluator_Context *ctx, String_View source) {
    // Sem executar binario real: mesma heuristica do "compiles".
    return eval_check_c_source_compiles(ctx, source);
}

// Avalia o comando 'check_c_source_runs' e armazena resultado em variável, usando probe real se disponível
static void eval_check_c_source_compiles_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View source = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    bool ok = false;
    bool used_probe = false;
    if (eval_real_probes_enabled(ctx)) {
        ok = eval_real_probe_check_c_source_compiles(ctx, source, &used_probe);
        if (!used_probe) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "check_c_source_compiles",
                "probe real indisponivel (compiler nao encontrado); usando heuristica",
                "defina CMAKE_C_COMPILER/CC para habilitar probe real");
        }
    }
    if (!used_probe) {
        ok = eval_check_c_source_compiles(ctx, source);
    }
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_library_exists' e armazena resultado em variável, usando probe real se disponível
static void eval_check_library_exists_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 4) return;
    String_View library = resolve_arg(ctx, args.items[0]);
    String_View function_name = resolve_arg(ctx, args.items[1]);
    String_View location = resolve_arg(ctx, args.items[2]);
    String_View out_var = resolve_arg(ctx, args.items[3]);
    bool ok = false;
    bool used_probe = false;
    if (eval_real_probes_enabled(ctx)) {
        ok = eval_real_probe_check_library_exists(ctx, library, function_name, location, &used_probe);
        if (!used_probe) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "check_library_exists",
                "probe real indisponivel (compiler nao encontrado); usando heuristica",
                "defina CMAKE_C_COMPILER/CC para habilitar probe real");
        }
    }
    if (!used_probe) {
        ok = eval_check_library_exists(ctx, library, function_name, location);
    }
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_c_source_runs' e armazena resultado em variável, usando probe real se disponível
static void eval_check_c_source_runs_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View source = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    bool ok = false;
    bool used_probe = false;
    if (eval_real_probes_enabled(ctx)) {
        ok = eval_real_probe_check_c_source_runs(ctx, source, &used_probe);
        if (!used_probe) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "check_c_source_runs",
                "probe real indisponivel (compiler nao encontrado); usando heuristica",
                "defina CMAKE_C_COMPILER/CC para habilitar probe real");
        }
    }
    if (!used_probe) {
        ok = eval_check_c_source_runs(ctx, source);
    }
    eval_set_var(ctx, out_var, ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
}

// Avalia o comando 'check_struct_has_member' e armazena resultado em variável, usando probe real se disponível
static void eval_cmake_minimum_required_command(Evaluator_Context *ctx, Args args) {
    
//TODO: a implementacao atual so suporta "VERSION x.y" e ignora o resto dos argumentos
    String_View min_version = sv_from_cstr("3.0");

    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("VERSION")) && i + 1 < args.count) {
            min_version = resolve_arg(ctx, args.items[i + 1]);
            break;
        }
    }

    String_View min_only = min_version;
    const char *version_c = nob_temp_sv_to_cstr(min_version);
    const char *dots = strstr(version_c, "...");
    if (dots) {
        size_t off = (size_t)(dots - version_c);
        min_only = nob_sv_from_parts(min_version.data, off);
    }

    eval_set_var(ctx, sv_from_cstr("CMAKE_MINIMUM_REQUIRED_VERSION"), min_only, false, false);
    if (!eval_has_var(ctx, sv_from_cstr("CMAKE_VERSION"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_VERSION"), sv_from_cstr("3.16.0"), false, false);
    }

    String_View cmv = eval_get_var(ctx, sv_from_cstr("CMAKE_VERSION"));
    size_t major = 0, minor = 0, patch = 0;
    sscanf(nob_temp_sv_to_cstr(cmv), "%zu.%zu.%zu", &major, &minor, &patch);
    eval_set_var(ctx, sv_from_cstr("CMAKE_MAJOR_VERSION"), sv_from_cstr(nob_temp_sprintf("%zu", major)), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_MINOR_VERSION"), sv_from_cstr(nob_temp_sprintf("%zu", minor)), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_PATCH_VERSION"), sv_from_cstr(nob_temp_sprintf("%zu", patch)), false, false);
}

static bool try_compile_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) ||
           sv_eq_ci(tok, sv_from_cstr("COPY_FILE")) ||
           sv_eq_ci(tok, sv_from_cstr("CMAKE_FLAGS")) ||
           sv_eq_ci(tok, sv_from_cstr("COMPILE_DEFINITIONS")) ||
           sv_eq_ci(tok, sv_from_cstr("LINK_OPTIONS")) ||
           sv_eq_ci(tok, sv_from_cstr("LINK_LIBRARIES")) ||
           sv_eq_ci(tok, sv_from_cstr("NO_CACHE")) ||
           sv_eq_ci(tok, sv_from_cstr("LOG_DESCRIPTION")) ||
           sv_eq_ci(tok, sv_from_cstr("SOURCES")) ||
           sv_eq_ci(tok, sv_from_cstr("SOURCE_FROM_CONTENT")) ||
           sv_eq_ci(tok, sv_from_cstr("SOURCE_FROM_VAR"));
}

static void try_compile_append_required_settings(Evaluator_Context *ctx,
                                                 String_List *compile_definitions,
                                                 String_List *link_libraries) {
    List_Add_Ud ud_defs = { .list = compile_definitions, .unique = true };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CMAKE_REQUIRED_DEFINITIONS")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_defs);

    List_Add_Ud ud_libs = { .list = link_libraries, .unique = true };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CMAKE_REQUIRED_LIBRARIES")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_libs);
}

static String_View try_compile_run_compile(Evaluator_Context *ctx,
                                           String_View compiler,
                                           String_View src_path,
                                           String_View out_path,
                                           String_List *compile_definitions,
                                           String_List *link_options,
                                           String_List *link_libraries,
                                           bool *out_ok) {
    if (!ctx) {
        if (out_ok) *out_ok = false;
        return sv_from_cstr("");
    }

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = compile_definitions,
        .link_options = link_options,
        .link_libraries = link_libraries,
    };
    Toolchain_Compile_Result result = {0};
    bool invoked = toolchain_try_compile(&drv, &req, &result);
    if (out_ok) *out_ok = invoked && result.ok;
    if (!invoked) return sv_from_cstr("");
    return result.output;
}

static void eval_try_compile_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 3) return;
    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View bindir_arg = resolve_arg(ctx, args.items[1]);
    String_View src_arg = resolve_arg(ctx, args.items[2]);
    String_View output_var = sv_from_cstr("");
    String_View copy_file_path = sv_from_cstr("");

    String_List compile_definitions = {0};
    String_List link_options = {0};
    String_List link_libraries = {0};
    string_list_init(&compile_definitions);
    string_list_init(&link_options);
    string_list_init(&link_libraries);

    for (size_t i = 3; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("OUTPUT_VARIABLE"))) {
            if (i + 1 < args.count) output_var = resolve_arg(ctx, args.items[i + 1]);
            i++;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("COPY_FILE"))) {
            if (i + 1 < args.count) copy_file_path = resolve_arg(ctx, args.items[i + 1]);
            i++;
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("COMPILE_DEFINITIONS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_compile_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&compile_definitions, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("LINK_OPTIONS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_compile_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&link_options, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("LINK_LIBRARIES"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_compile_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&link_libraries, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("CMAKE_FLAGS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_compile_keyword(v)) {
                    i = j - 1;
                    break;
                }
                if (nob_sv_starts_with(v, sv_from_cstr("-D"))) {
                    String_View kv = nob_sv_from_parts(v.data + 2, v.count - 2);
                    const char *eq = memchr(kv.data, '=', kv.count);
                    if (eq) {
                        String_View key = nob_sv_from_parts(kv.data, (size_t)(eq - kv.data));
                        String_View val = nob_sv_from_parts(eq + 1, kv.count - (size_t)(eq - kv.data) - 1);
                        eval_set_var(ctx, key, val, false, false);
                    }
                }
                i = j;
            }
            continue;
        }
    }

    try_compile_append_required_settings(ctx, &compile_definitions, &link_libraries);

    String_View bindir = path_is_absolute_sv(bindir_arg)
        ? bindir_arg
        : path_join_arena(ctx->arena, ctx->current_binary_dir, bindir_arg);
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(bindir));

    String_View src_path = path_is_absolute_sv(src_arg)
        ? src_arg
        : path_join_arena(ctx->arena, ctx->current_list_dir, src_arg);
    if (!nob_file_exists(nob_temp_sv_to_cstr(src_path))) {
        eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
        if (output_var.count > 0) {
            eval_set_var(ctx, output_var,
                sv_from_cstr(nob_temp_sprintf("try_compile source file not found: " SV_Fmt, SV_Arg(src_path))),
                false, false);
        }
        return;
    }

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) {
        eval_set_var(ctx, out_var, sv_from_cstr("0"), false, false);
        if (output_var.count > 0) {
            eval_set_var(ctx, output_var,
                sv_from_cstr("try_compile failed: compiler not available (configure CMAKE_C_COMPILER/CC)"),
                false, false);
        }
        return;
    }

    #if defined(_WIN32)
        String_View out_path = path_join_arena(ctx->arena, bindir, sv_from_cstr("cmk2nob_try_compile.exe"));
    #else
        String_View out_path = path_join_arena(ctx->arena, bindir, sv_from_cstr("cmk2nob_try_compile"));
    #endif

    bool compile_ok = false;
    String_View compile_output = try_compile_run_compile(ctx, compiler, src_path, out_path,
        &compile_definitions, &link_options, &link_libraries, &compile_ok);

    eval_set_var(ctx, out_var, compile_ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);

    if (compile_ok && copy_file_path.count > 0 && nob_file_exists(nob_temp_sv_to_cstr(out_path))) {
        String_View copy_path = path_is_absolute_sv(copy_file_path)
            ? copy_file_path
            : path_join_arena(ctx->arena, ctx->current_binary_dir, copy_file_path);
        (void)nob_copy_file(nob_temp_sv_to_cstr(out_path), nob_temp_sv_to_cstr(copy_path));
    }

    if (output_var.count > 0) {
        if (sv_eq_ci(out_var, sv_from_cstr("MINGW64_VERSION")) && compile_ok) {
            #if defined(__MINGW64_VERSION_MAJOR) && defined(__MINGW64_VERSION_MINOR)
                String_View mingw_output = sv_from_cstr(nob_temp_sprintf("MINGW64_VERSION=%d.%d",
                (int)__MINGW64_VERSION_MAJOR, (int)__MINGW64_VERSION_MINOR));
                eval_set_var(ctx, output_var, mingw_output, false, false);
            return;
            #elif defined(__MINGW32__))
                eval_set_var(ctx, output_var, sv_from_cstr("MINGW64_VERSION=1.0"), false, false);
            return;
            #endif
        } else if (sv_eq_ci(out_var, sv_from_cstr("HAVE_WIN32_WINNT")) && compile_ok) {
            String_View required_defs = eval_get_var(ctx, sv_from_cstr("CMAKE_REQUIRED_DEFINITIONS"));
            String_View extracted = sv_from_cstr("");

            size_t start = 0;
            for (size_t p = 0; p <= required_defs.count; p++) {
                bool is_sep = (p == required_defs.count) || (required_defs.data[p] == ';');
                if (!is_sep) continue;
                if (p > start) {
                    String_View item = nob_sv_from_parts(required_defs.data + start, p - start);
                    if (nob_sv_starts_with(item, sv_from_cstr("-D"))) {
                        item = nob_sv_from_parts(item.data + 2, item.count - 2);
                    }
                    const char *eq = memchr(item.data, '=', item.count);
                    if (eq) {
                        size_t key_len = (size_t)(eq - item.data);
                        String_View key = nob_sv_from_parts(item.data, key_len);
                        if (sv_eq_ci(key, sv_from_cstr("_WIN32_WINNT"))) {
                            extracted = nob_sv_from_parts(eq + 1, item.count - key_len - 1);
                            break;
                        }
                    }
                }
                start = p + 1;
            }

            if (extracted.count > 0) {
                eval_set_var(ctx, output_var, sv_from_cstr(nob_temp_sprintf("_WIN32_WINNT=" SV_Fmt, SV_Arg(extracted))), false, false);
                return;
            } else {
                #if defined(_WIN32_WINNT)
                    #define C2NOB_STR2(x) #x
                    #define C2NOB_STR(x) C2NOB_STR2(x)
                    eval_set_var(ctx, output_var, sv_from_cstr("_WIN32_WINNT=" C2NOB_STR(_WIN32_WINNT)), false, false);
                    #undef C2NOB_STR
                    #undef C2NOB_STR2
                #else
                    eval_set_var(ctx, output_var, sv_from_cstr("_WIN32_WINNT=0x0601"), false, false);
                #endif
                return;
            }
        }
        eval_set_var(ctx, output_var, compile_output, false, false);
    }
}

// Avalia o comando 'create_test_sourcelist'
static void eval_create_test_sourcelist_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View driver_file = resolve_arg(ctx, args.items[1]);
    if (out_var.count == 0 || driver_file.count == 0) return;

    String_Builder list_sb = {0};
    sb_append_buf(&list_sb, driver_file.data, driver_file.count);

    String_View extra_include = sv_from_cstr("");
    for (size_t i = 2; i < args.count; i++) {
        String_View item = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(item, sv_from_cstr("EXTRA_INCLUDE"))) {
            if (i + 1 < args.count) extra_include = resolve_arg(ctx, args.items[i + 1]);
            break;
        }
        if (sv_eq_ci(item, sv_from_cstr("FUNCTION"))) {
            break;
        }
        if (item.count == 0) continue;
        sb_append(&list_sb, ';');
        sb_append_buf(&list_sb, item.data, item.count);
    }
    eval_set_var(ctx, out_var, sb_to_sv(list_sb), false, false);

    String_View driver_path = path_is_absolute_sv(driver_file)
        ? driver_file
        : path_join_arena(ctx->arena, ctx->current_binary_dir, driver_file);
    if (ensure_parent_dirs_for_path(ctx->arena, driver_path)) {
        String_Builder file_sb = {0};
        sb_append_cstr(&file_sb, "/* generated by cmk2nob: create_test_sourcelist */\n");
        if (extra_include.count > 0) {
            sb_append_cstr(&file_sb, "#include \"");
            sb_append_buf(&file_sb, extra_include.data, extra_include.count);
            sb_append_cstr(&file_sb, "\"\n");
        }
        sb_append_cstr(&file_sb, "int main(int argc, char **argv) {\n");
        sb_append_cstr(&file_sb, "    (void)argc;\n");
        sb_append_cstr(&file_sb, "    (void)argv;\n");
        sb_append_cstr(&file_sb, "    return 0;\n");
        sb_append_cstr(&file_sb, "}\n");
        if (!nob_write_entire_file(nob_temp_sv_to_cstr(driver_path), file_sb.items, file_sb.count)) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "create_test_sourcelist",
                nob_temp_sprintf("falha ao gerar driver de teste: "SV_Fmt, SV_Arg(driver_path)),
                "verifique permissao de escrita no diretorio de build");
        }
        nob_sb_free(file_sb);
    }

    nob_sb_free(list_sb);
}

// Verifica se o valor é uma keyword de qt_wrap_cpp/qt_wrap_ui
static bool qt_wrap_is_keyword(String_View value) {
    return sv_eq_ci(value, sv_from_cstr("OPTIONS")) ||
           sv_eq_ci(value, sv_from_cstr("TARGET")) ||
           sv_eq_ci(value, sv_from_cstr("DEPENDS"));
}

// Extrai o "stem" do nome do arquivo de origem, para gerar nome de arquivo de saída correspondente
static String_View qt_wrap_source_stem(String_View input) {
    String_View base = path_basename_sv(input);
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < base.count; i++) {
        if (base.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return base;
    return nob_sv_from_parts(base.data, dot);
}

// Avalia o comando 'qt_wrap_cpp'
static void eval_qt_wrap_cpp_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    if (out_var.count == 0) return;

    String_Builder list_sb = {0};
    bool first = true;

    for (size_t i = 1; i < args.count; i++) {
        String_View src = resolve_arg(ctx, args.items[i]);
        if (qt_wrap_is_keyword(src)) break;
        if (src.count == 0) continue;

        String_View stem = qt_wrap_source_stem(src);
        String_View generated_name = sv_from_cstr(
            nob_temp_sprintf("moc_%.*s.cxx", (int)stem.count, stem.data));
        String_View generated_path = path_join_arena(ctx->arena, ctx->current_binary_dir, generated_name);

        if (!first) sb_append(&list_sb, ';');
        sb_append_buf(&list_sb, generated_path.data, generated_path.count);
        first = false;

        if (ensure_parent_dirs_for_path(ctx->arena, generated_path)) {
            const char *content = "/* generated by cmk2nob: qt_wrap_cpp */\n";
            if (!nob_write_entire_file(nob_temp_sv_to_cstr(generated_path), content, strlen(content))) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "qt_wrap_cpp",
                    nob_temp_sprintf("falha ao gerar arquivo MOC: "SV_Fmt, SV_Arg(generated_path)),
                    "verifique permissao de escrita no diretorio de build");
            }
        }
    }

    eval_set_var(ctx, out_var, sb_to_sv(list_sb), false, false);
    nob_sb_free(list_sb);
}

// Avalia o comando 'qt_wrap_ui'
static void eval_qt_wrap_ui_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    if (out_var.count == 0) return;

    String_Builder list_sb = {0};
    bool first = true;

    for (size_t i = 1; i < args.count; i++) {
        String_View ui_file = resolve_arg(ctx, args.items[i]);
        if (qt_wrap_is_keyword(ui_file)) break;
        if (ui_file.count == 0) continue;

        String_View stem = qt_wrap_source_stem(ui_file);
        String_View generated_name = sv_from_cstr(
            nob_temp_sprintf("ui_%.*s.h", (int)stem.count, stem.data));
        String_View generated_path = path_join_arena(ctx->arena, ctx->current_binary_dir, generated_name);

        if (!first) sb_append(&list_sb, ';');
        sb_append_buf(&list_sb, generated_path.data, generated_path.count);
        first = false;

        if (ensure_parent_dirs_for_path(ctx->arena, generated_path)) {
            const char *content =
                "/* generated by cmk2nob: qt_wrap_ui */\n"
                "#pragma once\n";
            if (!nob_write_entire_file(nob_temp_sv_to_cstr(generated_path), content, strlen(content))) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "qt_wrap_ui",
                    nob_temp_sprintf("falha ao gerar header UI: "SV_Fmt, SV_Arg(generated_path)),
                    "verifique permissao de escrita no diretorio de build");
            }
        }
    }

    eval_set_var(ctx, out_var, sb_to_sv(list_sb), false, false);
    nob_sb_free(list_sb);
}

// Verifica se o token é uma keyword de execute_process que indica início de nova seção de argumentos
static bool exec_program_is_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("ARGS")) ||
           sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) ||
           sv_eq_ci(tok, sv_from_cstr("RETURN_VALUE"));
}

// Avalia o comando 'fltk_wrap_ui'
static void eval_fltk_wrap_ui_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    if (out_var.count == 0) return;

    String_Builder list_sb = {0};
    bool first = true;

    for (size_t i = 1; i < args.count; i++) {
        String_View ui_file = resolve_arg(ctx, args.items[i]);
        if (ui_file.count == 0) continue;

        String_View stem = qt_wrap_source_stem(ui_file);
        String_View generated_name = sv_from_cstr(
            nob_temp_sprintf("%.*s.cxx", (int)stem.count, stem.data));
        String_View generated_path = path_join_arena(ctx->arena, ctx->current_binary_dir, generated_name);

        if (!first) sb_append(&list_sb, ';');
        sb_append_buf(&list_sb, generated_path.data, generated_path.count);
        first = false;

        if (ensure_parent_dirs_for_path(ctx->arena, generated_path)) {
            String_Builder content = {0};
            sb_append_cstr(&content, "/* generated by cmk2nob: fltk_wrap_ui */\n");
            sb_append_cstr(&content, "int cmk2nob_fltk_wrap_ui_stub(void) { return 0; }\n");
            if (!nob_write_entire_file(nob_temp_sv_to_cstr(generated_path), content.items, content.count)) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "fltk_wrap_ui",
                    nob_temp_sprintf("falha ao gerar wrapper FLTK: "SV_Fmt, SV_Arg(generated_path)),
                    "verifique permissao de escrita no diretorio de build");
            }
            nob_sb_free(content);
        }
    }

    eval_set_var(ctx, out_var, sb_to_sv(list_sb), false, false);
    nob_sb_free(list_sb);
}



// Avalia o comando 'execute_process'
// Avalia o comando 'execute_process'
static void eval_execute_process_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_View result_var = sv_from_cstr("");
    String_View output_var = sv_from_cstr("");
    String_View error_var = sv_from_cstr("");
    String_View working_dir = sv_from_cstr("");
    bool output_strip = false;
    bool error_strip = false;
    bool output_quiet = false;
    bool error_quiet = false;
    unsigned long timeout_ms = 0;

    String_View command_items[64] = {0};
    size_t command_count = 0;

    for (size_t i = 0; i < args.count; i++) {
        String_View token = resolve_arg(ctx, args.items[i]);

        if (sv_eq_ci(token, sv_from_cstr("COMMAND"))) {
            i++;
            while (i < args.count) {
                String_View item = resolve_arg(ctx, args.items[i]);
                if (sv_eq_ci(item, sv_from_cstr("COMMAND")) ||
                    sv_eq_ci(item, sv_from_cstr("WORKING_DIRECTORY")) ||
                    sv_eq_ci(item, sv_from_cstr("RESULT_VARIABLE")) ||
                    sv_eq_ci(item, sv_from_cstr("OUTPUT_VARIABLE")) ||
                    sv_eq_ci(item, sv_from_cstr("ERROR_VARIABLE")) ||
                    sv_eq_ci(item, sv_from_cstr("OUTPUT_STRIP_TRAILING_WHITESPACE")) ||
                    sv_eq_ci(item, sv_from_cstr("ERROR_STRIP_TRAILING_WHITESPACE")) ||
                    sv_eq_ci(item, sv_from_cstr("OUTPUT_QUIET")) ||
                    sv_eq_ci(item, sv_from_cstr("ERROR_QUIET")) ||
                    sv_eq_ci(item, sv_from_cstr("TIMEOUT")) ||
                    sv_eq_ci(item, sv_from_cstr("ENCODING")) ||
                    sv_eq_ci(item, sv_from_cstr("COMMAND_ECHO")) ||
                    sv_eq_ci(item, sv_from_cstr("COMMAND_ERROR_IS_FATAL"))) {
                    i--;
                    break;
                }

                if (command_count < (sizeof(command_items) / sizeof(command_items[0]))) {
                    command_items[command_count++] = item;
                }
                i++;
            }
            continue;
        }

        if (sv_eq_ci(token, sv_from_cstr("WORKING_DIRECTORY")) && i + 1 < args.count) {
            working_dir = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("RESULT_VARIABLE")) && i + 1 < args.count) {
            result_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("OUTPUT_VARIABLE")) && i + 1 < args.count) {
            output_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("ERROR_VARIABLE")) && i + 1 < args.count) {
            error_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("OUTPUT_STRIP_TRAILING_WHITESPACE"))) {
            output_strip = true;
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("ERROR_STRIP_TRAILING_WHITESPACE"))) {
            error_strip = true;
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("OUTPUT_QUIET"))) {
            output_quiet = true;
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("ERROR_QUIET"))) {
            error_quiet = true;
            continue;
        }
        if (sv_eq_ci(token, sv_from_cstr("TIMEOUT")) && i + 1 < args.count) {
            long timeout_s = parse_long_or_default(resolve_arg(ctx, args.items[++i]), 0);
            if (timeout_s > 0) timeout_ms = (unsigned long)timeout_s * 1000UL;
            continue;
        }
        if ((sv_eq_ci(token, sv_from_cstr("ENCODING")) ||
             sv_eq_ci(token, sv_from_cstr("COMMAND_ECHO")) ||
             sv_eq_ci(token, sv_from_cstr("COMMAND_ERROR_IS_FATAL"))) && i + 1 < args.count) {
            i++;
            continue;
        }
    }

    if (command_count == 0) return;

    String_View exec_cwd = ctx->current_binary_dir;
    if (working_dir.count > 0) {
        exec_cwd = path_is_absolute_sv(working_dir)
            ? working_dir
            : path_join_arena(ctx->arena, ctx->current_binary_dir, working_dir);
    }

    String_View scratch_dir = path_join_arena(ctx->arena, ctx->current_binary_dir, sv_from_cstr(".cmk2nob_exec"));

    Sys_Process_Request req = {0};
    req.arena = ctx->arena;
    req.working_dir = exec_cwd;
    req.timeout_ms = timeout_ms;
    req.argv = command_items;
    req.argv_count = command_count;
    req.capture_stdout = (output_var.count > 0) && !output_quiet;
    req.capture_stderr = (error_var.count > 0) && !error_quiet;
    req.strip_stdout_trailing_ws = output_strip;
    req.strip_stderr_trailing_ws = error_strip;
    req.scratch_dir = scratch_dir;

    Sys_Process_Result result = {0};
    bool ran = sys_run_process(&req, &result);
    if (!ran) {
        if (result_var.count > 0) eval_set_var(ctx, result_var, sv_from_cstr("1"), false, false);
        if (output_var.count > 0) eval_set_var(ctx, output_var, sv_from_cstr(""), false, false);
        if (error_var.count > 0) eval_set_var(ctx, error_var, sv_from_cstr(""), false, false);
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "execute_process",
                 "falha ao executar processo",
                 "verifique comando e diretorio de trabalho");
        return;
    }

    if (result_var.count > 0) {
        eval_set_var(ctx, result_var, result.exit_code == 0 ? sv_from_cstr("0") : sv_from_cstr("1"), false, false);
    }
    if (output_var.count > 0) {
        eval_set_var(ctx, output_var, output_quiet ? sv_from_cstr("") : result.stdout_text, false, false);
    }
    if (error_var.count > 0) {
        eval_set_var(ctx, error_var, error_quiet ? sv_from_cstr("") : result.stderr_text, false, false);
    }
}

// Avalia o comando 'exec_program'
static void eval_exec_program_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_View executable = resolve_arg(ctx, args.items[0]);
    if (executable.count == 0) return;

    String_View working_dir = sv_from_cstr("");
    String_View output_var = sv_from_cstr("");
    String_View return_var = sv_from_cstr("");

    String_View argv_items[64] = {0};
    size_t argv_count = 0;
    argv_items[argv_count++] = executable;

    size_t i = 1;
    if (i < args.count) {
        String_View maybe_dir = resolve_arg(ctx, args.items[i]);
        if (!exec_program_is_keyword(maybe_dir)) {
            working_dir = maybe_dir;
            i++;
        }
    }

    for (; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("ARGS"))) {
            i++;
            while (i < args.count) {
                String_View a = resolve_arg(ctx, args.items[i]);
                if (exec_program_is_keyword(a)) {
                    i--;
                    break;
                }
                if (argv_count < (sizeof(argv_items) / sizeof(argv_items[0]))) {
                    argv_items[argv_count++] = a;
                }
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) && i + 1 < args.count) {
            output_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("RETURN_VALUE")) && i + 1 < args.count) {
            return_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }
    }

    String_View exec_cwd = ctx->current_binary_dir;
    if (working_dir.count > 0) {
        exec_cwd = path_is_absolute_sv(working_dir)
            ? working_dir
            : path_join_arena(ctx->arena, ctx->current_binary_dir, working_dir);
    }

    String_View scratch_dir = path_join_arena(ctx->arena, ctx->current_binary_dir, sv_from_cstr(".cmk2nob_exec"));

    Sys_Process_Request req = {0};
    req.arena = ctx->arena;
    req.working_dir = exec_cwd;
    req.timeout_ms = 0;
    req.argv = argv_items;
    req.argv_count = argv_count;
    req.capture_stdout = output_var.count > 0;
    req.capture_stderr = false;
    req.strip_stdout_trailing_ws = true;
    req.strip_stderr_trailing_ws = false;
    req.scratch_dir = scratch_dir;

    Sys_Process_Result result = {0};
    bool ran = sys_run_process(&req, &result);
    if (!ran) {
        if (output_var.count > 0) eval_set_var(ctx, output_var, sv_from_cstr(""), false, false);
        if (return_var.count > 0) eval_set_var(ctx, return_var, sv_from_cstr("1"), false, false);
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "exec_program",
                 "falha ao executar processo",
                 "verifique comando e diretorio de trabalho");
        return;
    }

    if (output_var.count > 0) {
        eval_set_var(ctx, output_var, result.stdout_text, false, false);
    }
    if (return_var.count > 0) {
        eval_set_var(ctx, return_var, result.exit_code == 0 ? sv_from_cstr("0") : sv_from_cstr("1"), false, false);
    }
}
static bool try_run_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("COMPILE_OUTPUT_VARIABLE")) ||
           sv_eq_ci(tok, sv_from_cstr("RUN_OUTPUT_VARIABLE")) ||
           sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) ||
           sv_eq_ci(tok, sv_from_cstr("CMAKE_FLAGS")) ||
           sv_eq_ci(tok, sv_from_cstr("COMPILE_DEFINITIONS")) ||
           sv_eq_ci(tok, sv_from_cstr("LINK_OPTIONS")) ||
           sv_eq_ci(tok, sv_from_cstr("LINK_LIBRARIES")) ||
           sv_eq_ci(tok, sv_from_cstr("ARGS"));
}

// Extrai a parte major de um token de versão, retornando fallback se o formato for inválido
static int cmake_file_api_parse_version_major(String_View tok, int fallback) {
    if (tok.count == 0) return fallback;
    const char *text = nob_temp_sv_to_cstr(tok);
    if ((text[0] == 'v' || text[0] == 'V') && text[1] != '\0') text++;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (!end || end == text || parsed <= 0) return fallback;
    return (int)parsed;
}

static void json_append_escaped(String_Builder *sb, String_View s) {
    if (!sb) return;
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '\\') sb_append_cstr(sb, "\\\\");
        else if (c == '"') sb_append_cstr(sb, "\\\"");
        else if (c == '\n') sb_append_cstr(sb, "\\n");
        else if (c == '\r') sb_append_cstr(sb, "\\r");
        else if (c == '\t') sb_append_cstr(sb, "\\t");
        else if (c < 0x20) sb_append_cstr(sb, "?");
        else sb_append(sb, (char)c);
    }
}

// Verifica se o token é uma keyword de cmake_file_api que indica início de nova seção de argumentos
static bool cmake_file_api_is_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("QUERY")) ||
           sv_eq_ci(tok, sv_from_cstr("API_VERSION")) ||
           sv_eq_ci(tok, sv_from_cstr("CLIENT"));
}

// Emite um arquivo de consulta para o CMake File API, criando diretórios pai conforme necessário
static void cmake_file_api_emit_query(Evaluator_Context *ctx,
                                      String_View query_root,
                                      String_View kind,
                                      String_View version_token) {
    if (!ctx || kind.count == 0) return;
    int major = cmake_file_api_parse_version_major(version_token, 1);
    String_View file_name = sv_from_cstr(nob_temp_sprintf("%s-v%d.json", nob_temp_sv_to_cstr(kind), major));
    String_View out_path = path_join_arena(ctx->arena, query_root, file_name);
    if (!ensure_parent_dirs_for_path(ctx->arena, out_path)) return;
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(out_path), "", 0);
}

// Avalia o comando 'cmake_file_api'
static void eval_cmake_file_api_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_View mode = resolve_arg(ctx, args.items[0]);
    if (!sv_eq_ci(mode, sv_from_cstr("QUERY"))) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_file_api",
            "somente modo QUERY e suportado",
            "use cmake_file_api(QUERY [API_VERSION <v>] [CLIENT <name>] <KIND> <VER> ...)");
        return;
    }

    String_View api_root = path_join_arena(ctx->arena, ctx->current_binary_dir, sv_from_cstr(".cmake/api"));
    String_View v1_root = path_join_arena(ctx->arena, api_root, sv_from_cstr("v1"));
    String_View query_root = path_join_arena(ctx->arena, v1_root, sv_from_cstr("query"));
    (void)ensure_parent_dirs_for_path(ctx->arena, path_join_arena(ctx->arena, query_root, sv_from_cstr(".keep")));
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(query_root));

    String_View client_name = sv_from_cstr("");
    size_t i = 1;
    if (i < args.count && sv_eq_ci(resolve_arg(ctx, args.items[i]), sv_from_cstr("CLIENT")) && i + 1 < args.count) {
        client_name = resolve_arg(ctx, args.items[i + 1]);
        i += 2;
    }

    if (i < args.count && sv_eq_ci(resolve_arg(ctx, args.items[i]), sv_from_cstr("API_VERSION")) && i + 1 < args.count) {
        String_View v = resolve_arg(ctx, args.items[i + 1]);
        int api_major = cmake_file_api_parse_version_major(v, 1);
        if (api_major != 1) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_file_api",
                nob_temp_sprintf("API_VERSION v%d solicitado; usando v1", api_major),
                "a implementacao atual materializa apenas .cmake/api/v1");
        }
        i += 2;
    }

    String_View effective_query_root = query_root;
    if (client_name.count > 0) {
        String_View client_dir = path_join_arena(ctx->arena, query_root,
            sv_from_cstr(nob_temp_sprintf("client-%s", nob_temp_sv_to_cstr(client_name))));
        effective_query_root = path_join_arena(ctx->arena, client_dir, sv_from_cstr("query"));
        (void)ensure_parent_dirs_for_path(ctx->arena, path_join_arena(ctx->arena, effective_query_root, sv_from_cstr(".keep")));
        (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(effective_query_root));
    }

    bool emitted_any = false;
    while (i < args.count) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (cmake_file_api_is_keyword(tok)) {
            i++;
            continue;
        }

        String_View kind = tok;
        String_View ver = sv_from_cstr("1");
        if (i + 1 < args.count) {
            String_View maybe_ver = resolve_arg(ctx, args.items[i + 1]);
            if (!cmake_file_api_is_keyword(maybe_ver)) {
                ver = maybe_ver;
                i += 2;
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }

        cmake_file_api_emit_query(ctx, effective_query_root, kind, ver);
        emitted_any = true;
    }

    if (!emitted_any) {
        String_View out_path = path_join_arena(ctx->arena, effective_query_root, sv_from_cstr("query.json"));
        if (ensure_parent_dirs_for_path(ctx->arena, out_path)) {
            (void)nob_write_entire_file(nob_temp_sv_to_cstr(out_path), "", 0);
        }
    }

    eval_set_var(ctx, sv_from_cstr("CMAKE_FILE_API"), sv_from_cstr("1"), false, false);
}

static bool cmake_instrumentation_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("API_VERSION")) ||
           sv_eq_ci(tok, sv_from_cstr("DATA_VERSION")) ||
           sv_eq_ci(tok, sv_from_cstr("HOOKS")) ||
           sv_eq_ci(tok, sv_from_cstr("QUERIES")) ||
           sv_eq_ci(tok, sv_from_cstr("CALLBACK"));
}

static String_View join_list_semicolon(Evaluator_Context *ctx, String_List *items) {
    String_Builder sb = {0};
    for (size_t i = 0; i < items->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, items->items[i].data, items->items[i].count);
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View join_list_space(Evaluator_Context *ctx, String_List *items) {
    String_Builder sb = {0};
    for (size_t i = 0; i < items->count; i++) {
        if (i > 0) sb_append(&sb, ' ');
        sb_append_buf(&sb, items->items[i].data, items->items[i].count);
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return out;
}

static void eval_cmake_instrumentation_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 4) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cmake_instrumentation",
            "assinatura invalida",
            "use cmake_instrumentation(API_VERSION <v> DATA_VERSION <v> [HOOKS ...] [QUERIES ...] [CALLBACK ...])");
        return;
    }

    int api_version = 0;
    int data_version = 0;
    String_List hooks = {0};
    String_List queries = {0};
    String_List callbacks = {0};
    string_list_init(&hooks);
    string_list_init(&queries);
    string_list_init(&callbacks);

    for (size_t i = 0; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("API_VERSION")) && i + 1 < args.count) {
            String_View v = resolve_arg(ctx, args.items[++i]);
            api_version = cmake_file_api_parse_version_major(v, 0);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DATA_VERSION")) && i + 1 < args.count) {
            String_View v = resolve_arg(ctx, args.items[++i]);
            data_version = cmake_file_api_parse_version_major(v, 0);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("HOOKS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (cmake_instrumentation_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&hooks, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("QUERIES"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (cmake_instrumentation_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&queries, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("CALLBACK"))) {
            String_List cb_parts = {0};
            string_list_init(&cb_parts);
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (cmake_instrumentation_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add(&cb_parts, ctx->arena, v);
                i = j;
            }
            if (cb_parts.count > 0) {
                string_list_add(&callbacks, ctx->arena, join_list_space(ctx, &cb_parts));
            }
            continue;
        }
    }

    if (api_version == 0 || data_version == 0) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cmake_instrumentation",
            "API_VERSION e DATA_VERSION sao obrigatorios",
            "forneca API_VERSION 1 DATA_VERSION 1");
        return;
    }
    if (api_version != 1 || data_version != 1) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_instrumentation",
            nob_temp_sprintf("versoes solicitadas API=%d DATA=%d; usando v1", api_version, data_version),
            "somente Instrumentation API/Data v1 e suportado");
        api_version = 1;
        data_version = 1;
    }

    String_View gate = eval_get_var(ctx, sv_from_cstr("CMAKE_EXPERIMENTAL_INSTRUMENTATION"));
    if (gate.count > 0 && cmake_string_is_false(gate)) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_instrumentation",
            "CMAKE_EXPERIMENTAL_INSTRUMENTATION desabilitado; gerando query mesmo assim",
            "habilite o gate para refletir o comportamento oficial do CMake");
    }

    String_View root = path_join_arena(ctx->arena, ctx->current_binary_dir, sv_from_cstr(".cmake/instrumentation/v1/query/generated"));
    (void)ensure_parent_dirs_for_path(ctx->arena, path_join_arena(ctx->arena, root, sv_from_cstr(".keep")));
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(root));

    static size_t s_instr_query_counter = 0;
    s_instr_query_counter++;
    String_View out_path = path_join_arena(ctx->arena, root, sv_from_cstr(nob_temp_sprintf("query_%zu.json", s_instr_query_counter)));

    String_Builder json = {0};
    sb_append_cstr(&json, "{\n");
    sb_append_cstr(&json, "  \"version\": 1");
    if (hooks.count > 0) {
        sb_append_cstr(&json, ",\n  \"hooks\": [");
        for (size_t i = 0; i < hooks.count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            json_append_escaped(&json, hooks.items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }
    if (queries.count > 0) {
        sb_append_cstr(&json, ",\n  \"queries\": [");
        for (size_t i = 0; i < queries.count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            json_append_escaped(&json, queries.items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }
    if (callbacks.count > 0) {
        sb_append_cstr(&json, ",\n  \"callbacks\": [");
        for (size_t i = 0; i < callbacks.count; i++) {
            if (i > 0) sb_append_cstr(&json, ", ");
            sb_append(&json, '"');
            json_append_escaped(&json, callbacks.items[i]);
            sb_append(&json, '"');
        }
        sb_append(&json, ']');
    }
    sb_append_cstr(&json, "\n}\n");
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(out_path), json.items, json.count);
    nob_sb_free(json);

    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION"), sv_from_cstr("ON"), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION_API_VERSION"), sv_from_cstr("1"), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION_DATA_VERSION"), sv_from_cstr("1"), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION_HOOKS"), join_list_semicolon(ctx, &hooks), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION_QUERIES"), join_list_semicolon(ctx, &queries), false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INSTRUMENTATION_CALLBACKS"), join_list_semicolon(ctx, &callbacks), false, false);
}

// Avalia o comando 'try_run'
static void eval_try_run_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 4) return;

    String_View run_result_var = resolve_arg(ctx, args.items[0]);
    String_View compile_result_var = resolve_arg(ctx, args.items[1]);
    if (run_result_var.count == 0 || compile_result_var.count == 0) return;
    String_View bindir_arg = resolve_arg(ctx, args.items[2]);
    String_View src_arg = resolve_arg(ctx, args.items[3]);

    String_View compile_output_var = sv_from_cstr("");
    String_View run_output_var = sv_from_cstr("");
    String_View output_var = sv_from_cstr("");
    String_List compile_definitions = {0};
    String_List link_options = {0};
    String_List link_libraries = {0};
    String_List run_args = {0};
    string_list_init(&compile_definitions);
    string_list_init(&link_options);
    string_list_init(&link_libraries);
    string_list_init(&run_args);

    for (size_t i = 4; i < args.count; i++) {
        String_View a = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(a, sv_from_cstr("COMPILE_OUTPUT_VARIABLE"))) {
            if (i + 1 < args.count) compile_output_var = resolve_arg(ctx, args.items[i + 1]);
            i++;
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("RUN_OUTPUT_VARIABLE"))) {
            if (i + 1 < args.count) run_output_var = resolve_arg(ctx, args.items[i + 1]);
            i++;
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("OUTPUT_VARIABLE"))) {
            if (i + 1 < args.count) output_var = resolve_arg(ctx, args.items[i + 1]);
            i++;
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("COMPILE_DEFINITIONS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_run_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&compile_definitions, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("LINK_OPTIONS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_run_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&link_options, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("LINK_LIBRARIES"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_run_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add_unique(&link_libraries, ctx->arena, v);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("CMAKE_FLAGS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_run_keyword(v)) {
                    i = j - 1;
                    break;
                }
                if (nob_sv_starts_with(v, sv_from_cstr("-D"))) {
                    String_View kv = nob_sv_from_parts(v.data + 2, v.count - 2);
                    const char *eq = memchr(kv.data, '=', kv.count);
                    if (eq) {
                        String_View key = nob_sv_from_parts(kv.data, (size_t)(eq - kv.data));
                        String_View val = nob_sv_from_parts(eq + 1, kv.count - (size_t)(eq - kv.data) - 1);
                        eval_set_var(ctx, key, val, false, false);
                    }
                }
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(a, sv_from_cstr("ARGS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View v = resolve_arg(ctx, args.items[j]);
                if (try_run_keyword(v)) {
                    i = j - 1;
                    break;
                }
                string_list_add(&run_args, ctx->arena, v);
                i = j;
            }
            continue;
        }
    }

    try_compile_append_required_settings(ctx, &compile_definitions, &link_libraries);
    String_View bindir = path_is_absolute_sv(bindir_arg)
        ? bindir_arg
        : path_join_arena(ctx->arena, ctx->current_binary_dir, bindir_arg);
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(bindir));
    String_View src_path = path_is_absolute_sv(src_arg)
        ? src_arg
        : path_join_arena(ctx->arena, ctx->current_list_dir, src_arg);

    if (!nob_file_exists(nob_temp_sv_to_cstr(src_path))) {
        eval_set_var(ctx, compile_result_var, sv_from_cstr("0"), false, false);
        eval_set_var(ctx, run_result_var, sv_from_cstr("FAILED_TO_RUN"), false, false);
        if (compile_output_var.count > 0) {
            eval_set_var(ctx, compile_output_var,
                sv_from_cstr(nob_temp_sprintf("try_run source file not found: " SV_Fmt, SV_Arg(src_path))),
                false, false);
        }
        if (run_output_var.count > 0) eval_set_var(ctx, run_output_var, sv_from_cstr("try_run skipped: source not found"), false, false);
        if (output_var.count > 0) eval_set_var(ctx, output_var, sv_from_cstr("try_run skipped: source not found"), false, false);
        return;
    }

    Toolchain_Driver drv = eval_make_toolchain_driver(ctx);
    String_View compiler = eval_toolchain_compiler(ctx);
    if (!toolchain_compiler_available(&drv, compiler)) {
        eval_set_var(ctx, compile_result_var, sv_from_cstr("0"), false, false);
        eval_set_var(ctx, run_result_var, sv_from_cstr("FAILED_TO_RUN"), false, false);
        if (compile_output_var.count > 0) {
            eval_set_var(ctx, compile_output_var,
                sv_from_cstr("try_run failed: compiler not available (configure CMAKE_C_COMPILER/CC)"),
                false, false);
        }
        if (run_output_var.count > 0) eval_set_var(ctx, run_output_var, sv_from_cstr("try_run skipped: compiler unavailable"), false, false);
        if (output_var.count > 0) eval_set_var(ctx, output_var, sv_from_cstr("try_run skipped: compiler unavailable"), false, false);
        return;
    }

    #if defined(_WIN32)
        String_View out_path = path_join_arena(ctx->arena, bindir, sv_from_cstr("cmk2nob_try_run.exe"));
    #else
        String_View out_path = path_join_arena(ctx->arena, bindir, sv_from_cstr("cmk2nob_try_run"));
    #endif

    Toolchain_Compile_Request req = {
        .compiler = compiler,
        .src_path = src_path,
        .out_path = out_path,
        .compile_definitions = &compile_definitions,
        .link_options = &link_options,
        .link_libraries = &link_libraries,
    };

    String_View cross = eval_get_var(ctx, sv_from_cstr("CMAKE_CROSSCOMPILING"));
    bool cross_compiling = (cross.count > 0) && !cmake_string_is_false(cross);

    if (cross_compiling) {
        Toolchain_Compile_Result compile_out = {0};
        bool invoked = toolchain_try_compile(&drv, &req, &compile_out);
        bool compile_ok = invoked && compile_out.ok;
        eval_set_var(ctx, compile_result_var, compile_ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        if (compile_output_var.count > 0) {
            eval_set_var(ctx, compile_output_var, compile_out.output, false, false);
        }

        eval_set_var(ctx, run_result_var, sv_from_cstr("FAILED_TO_RUN"), false, false);
        String_View run_skip = compile_ok
            ? sv_from_cstr("try_run skipped due to CMAKE_CROSSCOMPILING")
            : sv_from_cstr("try_run skipped: compilation failed");
        if (run_output_var.count > 0) eval_set_var(ctx, run_output_var, run_skip, false, false);
        if (output_var.count > 0) eval_set_var(ctx, output_var, run_skip, false, false);
        return;
    }

    Toolchain_Compile_Result compile_out = {0};
    int run_rc = 1;
    String_View run_output = sv_from_cstr("");
    bool invoked = toolchain_try_run(&drv, &req, &run_args, &compile_out, &run_rc, &run_output);
    bool compile_ok = invoked && compile_out.ok;

    eval_set_var(ctx, compile_result_var, compile_ok ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
    if (compile_output_var.count > 0) {
        eval_set_var(ctx, compile_output_var, compile_out.output, false, false);
    }

    if (!compile_ok) {
        eval_set_var(ctx, run_result_var, sv_from_cstr("FAILED_TO_RUN"), false, false);
        String_View run_skip = sv_from_cstr("try_run skipped: compilation failed");
        if (run_output_var.count > 0) eval_set_var(ctx, run_output_var, run_skip, false, false);
        if (output_var.count > 0) eval_set_var(ctx, output_var, run_skip, false, false);
        return;
    }

    eval_set_var(ctx, run_result_var, sv_from_cstr(nob_temp_sprintf("%d", run_rc)), false, false);
    if (run_output_var.count > 0) eval_set_var(ctx, run_output_var, run_output, false, false);
    if (output_var.count > 0) eval_set_var(ctx, output_var, run_output, false, false);
}

static void append_list_item(String_Builder *sb, bool *first, String_View value) {
    if (!sb || !first || value.count == 0) return;
    if (!*first) sb_append(sb, ';');
    sb_append_buf(sb, value.data, value.count);
    *first = false;
}

static void eval_get_cmake_property_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View prop = resolve_arg(ctx, args.items[1]);

    String_Builder sb = {0};
    bool first = true;

    if (sv_eq_ci(prop, sv_from_cstr("CACHE_VARIABLES"))) {
        for (size_t i = 0; i < ctx->model->cache_variables.count; i++) {
            append_list_item(&sb, &first, ctx->model->cache_variables.items[i].name);
        }
    } else if (sv_eq_ci(prop, sv_from_cstr("VARIABLES"))) {
        for (size_t s = 0; s < ctx->scope_count; s++) {
            Eval_Scope *scope = &ctx->scopes[s];
            for (size_t i = 0; i < scope->vars.count; i++) {
                append_list_item(&sb, &first, scope->vars.keys[i]);
            }
        }
        for (size_t i = 0; i < ctx->model->cache_variables.count; i++) {
            append_list_item(&sb, &first, ctx->model->cache_variables.items[i].name);
        }
    } else if (sv_eq_ci(prop, sv_from_cstr("MACROS"))) {
        for (size_t i = 0; i < ctx->macros.count; i++) {
            append_list_item(&sb, &first, ctx->macros.items[i].name);
        }
    } else if (sv_eq_ci(prop, sv_from_cstr("COMMANDS"))) {
        append_list_item(&sb, &first, sv_from_cstr("set"));
        append_list_item(&sb, &first, sv_from_cstr("project"));
        append_list_item(&sb, &first, sv_from_cstr("add_executable"));
        append_list_item(&sb, &first, sv_from_cstr("add_library"));
    }

    eval_set_var(ctx, out_var, sb_to_sv(sb), false, false);
    nob_sb_free(sb);
}

// Avalia o comando 'project'
static void eval_project_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model) return;
    if (args.count < 1) return;

    String_View name = resolve_arg(ctx, args.items[0]);
    String_View version = sv_from_cstr("");

    // Define variáveis do projeto (semântica do evaluator)
    eval_set_var(ctx, sv_from_cstr("PROJECT_NAME"), name, false, false);
    eval_set_var(ctx, sv_from_cstr("PROJECT_SOURCE_DIR"), ctx->current_source_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("PROJECT_BINARY_DIR"), ctx->current_binary_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_SOURCE_DIR"), ctx->current_source_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_BINARY_DIR"), ctx->current_binary_dir, false, false);

    // Parse VERSION / LANGUAGES
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("VERSION")) && i + 1 < args.count) {
            version = resolve_arg(ctx, args.items[i + 1]);
            eval_set_var(ctx, sv_from_cstr("PROJECT_VERSION"), version, false, false);
            i++;
        } else if (nob_sv_eq(arg, sv_from_cstr("LANGUAGES"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View lang = resolve_arg(ctx, args.items[j]);
                if (nob_sv_eq(lang, sv_from_cstr("C"))) {
                    build_model_enable_language(ctx->model, ctx->arena, sv_from_cstr("C"));
                } else if (nob_sv_eq(lang, sv_from_cstr("CXX"))) {
                    build_model_enable_language(ctx->model, ctx->arena, sv_from_cstr("CXX"));
                }
            }
            break;
        }
    }

    // Persistir no modelo (dados/invariantes)
    build_model_set_project_info(ctx->model, name, version);
}

// Avalia o comando 'enable_language'
static void eval_enable_language_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;

    for (size_t i = 0; i < args.count; i++) {
        String_View lang = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(lang, sv_from_cstr("OPTIONAL"))) continue;
        if (lang.count == 0) continue;

        String_View canonical = lang;
        if (sv_eq_ci(lang, sv_from_cstr("C"))) canonical = sv_from_cstr("C");
        else if (sv_eq_ci(lang, sv_from_cstr("CXX"))) canonical = sv_from_cstr("CXX");
        else if (sv_eq_ci(lang, sv_from_cstr("ASM"))) canonical = sv_from_cstr("ASM");

        build_model_enable_language(ctx->model, ctx->arena, canonical);

        const char *compiler_default = "cc";
        if (nob_sv_eq(canonical, sv_from_cstr("CXX"))) compiler_default = "c++";
        else if (nob_sv_eq(canonical, sv_from_cstr("ASM"))) compiler_default = "as";

        const char *compiler_env = NULL;
        if (nob_sv_eq(canonical, sv_from_cstr("C"))) compiler_env = getenv("CC");
        else if (nob_sv_eq(canonical, sv_from_cstr("CXX"))) compiler_env = getenv("CXX");
        else if (nob_sv_eq(canonical, sv_from_cstr("ASM"))) compiler_env = getenv("AS");
        const char *compiler = (compiler_env && compiler_env[0]) ? compiler_env : compiler_default;

        String_View loaded_key = sv_from_cstr(nob_temp_sprintf("CMAKE_%s_COMPILER_LOADED", nob_temp_sv_to_cstr(canonical)));
        String_View compiler_key = sv_from_cstr(nob_temp_sprintf("CMAKE_%s_COMPILER", nob_temp_sv_to_cstr(canonical)));
        eval_set_var(ctx, loaded_key, sv_from_cstr("1"), false, false);
        eval_set_var(ctx, compiler_key, sv_from_cstr(compiler), false, false);
    }
}

// Avalia o comando 'add_executable'
static void eval_add_executable_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    
    String_View name = resolve_arg(ctx, args.items[0]);
    if (args.count >= 3) {
        String_View mode = resolve_arg(ctx, args.items[1]);
        if (nob_sv_eq(mode, sv_from_cstr("ALIAS"))) {
            Build_Target *alias_target = build_model_add_target(ctx->model, name, TARGET_ALIAS);
            if (!alias_target) return;
            String_View aliased = resolve_arg(ctx, args.items[2]);
            build_target_set_alias(alias_target, ctx->arena, aliased);
            return;
        }
    }

    bool imported = false;
    Target_Type type = TARGET_EXECUTABLE;
    Build_Target *target = NULL;
    
    // Processa fontes e opcoes
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);

        if (nob_sv_eq(arg, sv_from_cstr("IMPORTED"))) {
            imported = true;
            type = TARGET_IMPORTED;
            continue;
        }

        if (!target) {
            target = build_model_add_target(ctx->model, name, type);
            if (!target) return;
        }
        
        if (nob_sv_eq(arg, sv_from_cstr("WIN32"))) {
            build_target_set_flag(target, TARGET_FLAG_WIN32_EXECUTABLE, true);
        } else if (nob_sv_eq(arg, sv_from_cstr("MACOSX_BUNDLE"))) {
            build_target_set_flag(target, TARGET_FLAG_MACOSX_BUNDLE, true);
        } else if (nob_sv_eq(arg, sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            build_target_set_flag(target, TARGET_FLAG_EXCLUDE_FROM_ALL, true);
        } else if (nob_sv_eq(arg, sv_from_cstr("GLOBAL")) && imported) {
            // Flag valida para imported targets, sem efeito no modelo atual.
        } else if (nob_sv_eq(arg, sv_from_cstr("IMPORTED"))) {
            // Ja tratado acima.
        } else if (!imported) {
            build_target_add_source(target, ctx->arena, arg);
        }
    }

    if (!target) {
        target = build_model_add_target(ctx->model, name, type);
        if (!target) return;
    }
    if (imported) {
        build_target_set_flag(target, TARGET_FLAG_IMPORTED, true);
    }
}
// Avalia o comando 'add_library'
static void eval_add_library_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    
    String_View name = resolve_arg(ctx, args.items[0]);

    if (args.count >= 3) {
        String_View mode = resolve_arg(ctx, args.items[1]);
        if (nob_sv_eq(mode, sv_from_cstr("ALIAS"))) {
            Build_Target *alias_target = build_model_add_target(ctx->model, name, TARGET_ALIAS);
            if (!alias_target) return;
            String_View aliased = resolve_arg(ctx, args.items[2]);
            build_target_set_alias(alias_target, ctx->arena, aliased);
            return;
        }
    }
    
    // Determina o tipo padrao baseado em BUILD_SHARED_LIBS
    Target_Type type = TARGET_STATIC_LIB;
    String_View shared_libs_var = eval_get_var(ctx, sv_from_cstr("BUILD_SHARED_LIBS"));
    if (sv_bool_is_true(shared_libs_var)) {
        type = TARGET_SHARED_LIB;
    }

    bool imported = false;
    bool exclude_from_all = false;
    size_t source_start = 1;
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("STATIC"))) {
            type = TARGET_STATIC_LIB;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("SHARED")) || nob_sv_eq(arg, sv_from_cstr("MODULE"))) {
            type = TARGET_SHARED_LIB;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("OBJECT"))) {
            type = TARGET_OBJECT_LIB;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("INTERFACE"))) {
            type = TARGET_INTERFACE_LIB;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            exclude_from_all = true;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("IMPORTED"))) {
            imported = true;
            source_start = i + 1;
            continue;
        }
        if (nob_sv_eq(arg, sv_from_cstr("GLOBAL")) && imported) {
            source_start = i + 1;
            continue;
        }
        if (is_library_type_keyword(arg) || is_common_target_keyword(arg)) {
            source_start = i + 1;
            continue;
        }
        source_start = i;
        break;
    }
    
    if (imported) {
        type = TARGET_IMPORTED;
    }

    Build_Target *target = build_model_add_target(ctx->model, name, type);
    if (!target) return;
    build_target_set_flag(target, TARGET_FLAG_EXCLUDE_FROM_ALL, exclude_from_all);
    build_target_set_flag(target, TARGET_FLAG_IMPORTED, imported);
    
    bool accepts_sources = (type == TARGET_STATIC_LIB || type == TARGET_SHARED_LIB || type == TARGET_OBJECT_LIB);
    if (!accepts_sources) return;
    
    for (size_t i = source_start; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (is_library_type_keyword(arg) || is_common_target_keyword(arg)) {
            continue;
        }
        build_target_add_source(target, ctx->arena, arg);
    }
}

// Avalia o comando 'add_custom_target'
static void eval_add_custom_target_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;

    String_View name = resolve_arg(ctx, args.items[0]);
    Build_Target *target = build_model_add_target(ctx->model, name, TARGET_UTILITY);
    if (!target) return;

    String_View working_dir = sv_from_cstr("");
    String_View comment = sv_from_cstr("");
    String_List depends = {0}, byproducts = {0}, commands = {0};
    string_list_init(&depends);
    string_list_init(&byproducts);
    string_list_init(&commands);

    size_t i = 1;
    if (i < args.count && nob_sv_eq(resolve_arg(ctx, args.items[i]), sv_from_cstr("ALL"))) {
        build_target_set_flag(target, TARGET_FLAG_EXCLUDE_FROM_ALL, false);
        i++;
    } else {
        build_target_set_flag(target, TARGET_FLAG_EXCLUDE_FROM_ALL, true);
    }

    while (i < args.count) {
        String_View key = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(key, sv_from_cstr("DEPENDS"))) {
            i = parse_custom_command_list(ctx, args, i + 1, &depends);
            continue;
        }
        if (nob_sv_eq(key, sv_from_cstr("BYPRODUCTS"))) {
            i = parse_custom_command_list(ctx, args, i + 1, &byproducts);
            continue;
        }
        if (nob_sv_eq(key, sv_from_cstr("WORKING_DIRECTORY")) && i + 1 < args.count) {
            working_dir = resolve_arg(ctx, args.items[i + 1]);
            i += 2;
            continue;
        }
        if (nob_sv_eq(key, sv_from_cstr("COMMENT")) && i + 1 < args.count) {
            comment = resolve_arg(ctx, args.items[i + 1]);
            i += 2;
            continue;
        }
        if (nob_sv_eq(key, sv_from_cstr("COMMAND"))) {
            size_t cmd_start = i + 1;
            size_t cmd_end = cmd_start;
            while (cmd_end < args.count) {
                String_View part = resolve_arg(ctx, args.items[cmd_end]);
                if (is_custom_command_keyword(part)) break;
                cmd_end++;
            }
            String_View command = join_command_args(ctx, args, cmd_start, cmd_end, false);
            if (command.count > 0) string_list_add(&commands, ctx->arena, command);
            i = cmd_end;
            continue;
        }
        i++;
    }

    for (size_t d = 0; d < depends.count; d++) {
        Build_Target *dep_target = build_model_find_target(ctx->model, depends.items[d]);
        if (dep_target) {
            build_target_add_dependency(target, ctx->arena, dep_target->name);
        }
    }

    if (commands.count == 0) return;
    String_View merged_command = join_commands_with_and(ctx->arena, commands);
    Custom_Command *cmd = evaluator_target_add_custom_command_ex(target, ctx->arena, true, merged_command, working_dir, comment);
    if (!cmd) return;
    custom_command_copy_list(ctx->arena, &cmd->depends, &depends);
    custom_command_copy_list(ctx->arena, &cmd->byproducts, &byproducts);
}

static void eval_include_directories_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    bool system_mode = false;

    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("BEFORE")) || sv_eq_ci(arg, sv_from_cstr("AFTER"))) {
            continue;
        }
        if (sv_eq_ci(arg, sv_from_cstr("SYSTEM"))) {
            system_mode = true;
            continue;
        }

        String_View path = genex_trim(arg);
        if (path.count == 0) continue;
        build_model_add_include_directory(ctx->model, ctx->arena, path, system_mode);
    }
}


// ============================================================================
// PROPRIEDADES DE TARGETS
// ============================================================================

static bool is_directory_property_key(String_View prop) {
    if (sv_eq_ci(prop, sv_from_cstr("INCLUDE_DIRECTORIES"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("SYSTEM_INCLUDE_DIRECTORIES"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("LINK_DIRECTORIES"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("COMPILE_OPTIONS"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("LINK_OPTIONS"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("COMPILE_DEFINITIONS"))) return true;
    if (sv_eq_ci(prop, sv_from_cstr("DEFINITIONS"))) return true;
    return false;
}

typedef struct DirProp_Ud {
    String_View prop;
} DirProp_Ud;

static void eval_dirprop_on_item(Evaluator_Context *ctx, String_View item, void *ud) {
    DirProp_Ud *u = (DirProp_Ud*)ud;
    if (!u) return;
    String_View prop = u->prop;

    if (sv_eq_ci(prop, sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        build_model_add_include_directory(ctx->model, ctx->arena, item, false);
    } else if (sv_eq_ci(prop, sv_from_cstr("SYSTEM_INCLUDE_DIRECTORIES"))) {
        build_model_add_include_directory(ctx->model, ctx->arena, item, true);
    } else if (sv_eq_ci(prop, sv_from_cstr("LINK_DIRECTORIES"))) {
        build_model_add_link_directory(ctx->model, ctx->arena, item);
    } else if (sv_eq_ci(prop, sv_from_cstr("COMPILE_OPTIONS"))) {
        build_model_add_global_compile_option(ctx->model, ctx->arena, item);
    } else if (sv_eq_ci(prop, sv_from_cstr("LINK_OPTIONS"))) {
        build_model_add_global_link_option(ctx->model, ctx->arena, item);
    } else if (sv_eq_ci(prop, sv_from_cstr("COMPILE_DEFINITIONS")) ||
               sv_eq_ci(prop, sv_from_cstr("DEFINITIONS"))) {

        item = genex_trim(item);
        if (item.count == 0) return;

        if (nob_sv_starts_with(item, sv_from_cstr("-D")) ||
            nob_sv_starts_with(item, sv_from_cstr("/D"))) {
            if (item.count <= 2) return;
            item = nob_sv_from_parts(item.data + 2, item.count - 2);
            item = genex_trim(item);
            if (item.count == 0) return;
        }

        build_model_add_global_definition(ctx->model, ctx->arena, item);
    }
}

// Avalia o comando 'set_directory_properties'
static void eval_set_directory_properties_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;

    size_t props_idx = SIZE_MAX;
    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("PROPERTIES"))) {
            props_idx = i;
            break;
        }
    }
    if (props_idx == SIZE_MAX || props_idx + 2 > args.count) return;

    size_t i = props_idx + 1;
    while (i < args.count) {
        String_View prop = resolve_arg(ctx, args.items[i++]);
        if (prop.count == 0) continue;

        size_t value_start = i;
        while (i < args.count) {
            String_View maybe_next_prop = resolve_arg(ctx, args.items[i]);
            if (is_directory_property_key(maybe_next_prop)) break;
            i++;
        }
        if (value_start == i) continue;

        for (size_t v = value_start; v < i; v++) {
            String_View raw_item = resolve_arg(ctx, args.items[v]);
            DirProp_Ud ud = { .prop = prop };
            eval_foreach_semicolon_item(ctx, raw_item, /*trim_ws=*/true, eval_dirprop_on_item, &ud);
        }
    }
}

// Avalia o comando 'add_compile_options'
static void eval_add_compile_options_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View opt = resolve_arg(ctx, args.items[i]);
        if (opt.count == 0) continue;
        build_model_add_global_compile_option(ctx->model, ctx->arena, opt);
    }
}

// Avalia o comando 'add_compile_definitions'
static void eval_add_compile_definitions_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View def = resolve_arg(ctx, args.items[i]);
        if (nob_sv_starts_with(def, sv_from_cstr("-D"))) {
            def = nob_sv_from_parts(def.data + 2, def.count - 2);
        }
        def = genex_trim(def);
        if (def.count == 0) continue;
        build_model_add_global_definition(ctx->model, ctx->arena, def);
    }
}

// Avalia o comando 'add_definitions'
static void eval_add_definitions_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View arg = genex_trim(resolve_arg(ctx, args.items[i]));
        if (arg.count == 0) continue;
        build_model_process_global_definition_arg(ctx->model, ctx->arena, arg);
    }
}

static bool aux_source_directory_has_supported_extension(String_View ext) {
    return sv_eq_ci(ext, sv_from_cstr(".c")) ||
           sv_eq_ci(ext, sv_from_cstr(".cc")) ||
           sv_eq_ci(ext, sv_from_cstr(".cpp")) ||
           sv_eq_ci(ext, sv_from_cstr(".cxx")) ||
           sv_eq_ci(ext, sv_from_cstr(".c++")) ||
           sv_eq_ci(ext, sv_from_cstr(".m")) ||
           sv_eq_ci(ext, sv_from_cstr(".mm"));
}

// Avalia o comando 'aux_source_directory'
static void eval_aux_source_directory_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 2) return;

    String_View input_dir = resolve_arg(ctx, args.items[0]);
    String_View out_var = resolve_arg(ctx, args.items[1]);
    if (out_var.count == 0) return;

    String_View scan_dir = path_is_absolute_sv(input_dir)
        ? input_dir
        : path_join_arena(ctx->arena, ctx->current_source_dir, input_dir);

    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(nob_temp_sv_to_cstr(scan_dir), &children)) {
        eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
        return;
    }

    String_Builder list = {0};
    bool first = true;
    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (nob_sv_eq(name, sv_from_cstr(".")) || nob_sv_eq(name, sv_from_cstr(".."))) continue;
        if (!aux_source_directory_has_supported_extension(filename_ext_sv(name))) continue;

        String_View full_path = path_join_arena(ctx->arena, scan_dir, name);
        if (nob_get_file_type(nob_temp_sv_to_cstr(full_path)) != NOB_FILE_REGULAR) continue;

        if (!first) sb_append(&list, ';');
        sb_append_buf(&list, full_path.data, full_path.count);
        first = false;
    }

    nob_da_free(children);
    eval_set_var(ctx, out_var, sb_to_sv(list), false, false);
    nob_sb_free(list);
}

static String_View source_property_internal_key(Evaluator_Context *ctx, String_View source, const char *prop_name) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__SRC_PROP__");
    sb_append_buf(&sb, source.data, source.count);
    sb_append_cstr(&sb, "__");
    sb_append_cstr(&sb, prop_name);
    String_View out = sv_copy_to_arena(ctx->arena, sb_to_sv(sb));
    nob_sb_free(sb);
    return out;
}

static void apply_source_property_to_matching_targets(Evaluator_Context *ctx,
                                                      String_List *sources,
                                                      const char *prop_name,
                                                      String_View value) {
    if (!ctx || !ctx->model || !sources || !prop_name) return;
    for (size_t t = 0; t < ctx->model->target_count; t++) {
        Build_Target *target = &ctx->model->targets[t];
        for (size_t s = 0; s < sources->count; s++) {
            String_View src = sources->items[s];
            if (src.count == 0) continue;
            bool source_found = false;
            for (size_t i = 0; i < target->sources.count; i++) {
                if (nob_sv_eq(target->sources.items[i], src)) {
                    source_found = true;
                    break;
                }
            }
            if (!source_found) continue;
            String_View key = source_property_internal_key(ctx, src, prop_name);
            build_target_set_property(target, ctx->arena, key, value);
        }
    }
}

// Avalia o comando 'get_source_file_property'
static void eval_get_source_file_property_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 3) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View source = resolve_arg(ctx, args.items[1]);
    String_View prop = resolve_arg(ctx, args.items[2]);
    if (source.count == 0 || prop.count == 0) {
        eval_set_var(ctx, out_var, sv_from_cstr("NOTFOUND"), false, false);
        return;
    }

    const char *prop_name = nob_temp_sv_to_cstr(prop);
    if (sv_eq_ci(prop, sv_from_cstr("COMPILE_FLAGS"))) {
        prop_name = "COMPILE_OPTIONS";
    }

    for (size_t t = 0; t < ctx->model->target_count; t++) {
        Build_Target *target = &ctx->model->targets[t];
        bool source_found = false;
        for (size_t s = 0; s < target->sources.count; s++) {
            if (nob_sv_eq(target->sources.items[s], source)) {
                source_found = true;
                break;
            }
        }
        if (!source_found) continue;

        String_View key = source_property_internal_key(ctx, source, prop_name);
        String_View value = build_target_get_property(target, key);
        if (value.count > 0) {
            eval_set_var(ctx, out_var, value, false, false);
            return;
        }
    }

    eval_set_var(ctx, out_var, sv_from_cstr("NOTFOUND"), false, false);
}

// Avalia o comando 'remove_definitions'
static void eval_remove_definitions_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View def = genex_trim(resolve_arg(ctx, args.items[i]));
        if (def.count == 0) continue;
        if (nob_sv_starts_with(def, sv_from_cstr("-D")) ||
            nob_sv_starts_with(def, sv_from_cstr("/D"))) {
            def = nob_sv_from_parts(def.data + 2, def.count - 2);
        }
        def = genex_trim(def);
        if (def.count == 0) continue;
        build_model_remove_global_definition(ctx->model, def);
    }
}

// Avalia o comando 'set_source_files_properties'
static void eval_set_source_files_properties_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 4) return;

    size_t props_idx = SIZE_MAX;
    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("PROPERTIES"))) {
            props_idx = i;
            break;
        }
    }
    if (props_idx == SIZE_MAX || props_idx + 2 > args.count) return;

    String_List sources = {0};
    string_list_init(&sources);
    for (size_t i = 0; i < props_idx; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("DIRECTORY")) || sv_eq_ci(arg, sv_from_cstr("TARGET_DIRECTORY"))) {
            if (i + 1 < props_idx) i++;
            continue;
        }
        arg = genex_trim(arg);
        if (arg.count == 0) continue;
        string_list_add_unique(&sources, ctx->arena, arg);
    }
    if (sources.count == 0) return;

    for (size_t i = props_idx + 1; i + 1 < args.count; i += 2) {
        String_View prop = resolve_arg(ctx, args.items[i]);
        String_View value = resolve_arg(ctx, args.items[i + 1]);

        if (sv_eq_ci(prop, sv_from_cstr("COMPILE_OPTIONS"))) {
            apply_source_property_to_matching_targets(ctx, &sources, "COMPILE_OPTIONS", value);
        } else if (sv_eq_ci(prop, sv_from_cstr("COMPILE_DEFINITIONS"))) {
            apply_source_property_to_matching_targets(ctx, &sources, "COMPILE_DEFINITIONS", value);
        } else if (sv_eq_ci(prop, sv_from_cstr("COMPILE_FLAGS"))) {
            // Mapeamento de compatibilidade: COMPILE_FLAGS impacta linha de compilacao como opcoes.
            apply_source_property_to_matching_targets(ctx, &sources, "COMPILE_OPTIONS", value);
        }
    }
}

// Avalia o comando 'include_regular_expression'
static void eval_include_regular_expression_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;

    String_View regex_match = resolve_arg(ctx, args.items[0]);
    eval_set_var(ctx, sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION"), regex_match, false, false);

    if (args.count >= 2) {
        String_View regex_complain = resolve_arg(ctx, args.items[1]);
        eval_set_var(ctx, sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN"), regex_complain, false, false);
    }
}

// Avalia o comando 'site_name'
static void eval_site_name_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View out_var = resolve_arg(ctx, args.items[0]);
    if (out_var.count == 0) return;

    const char *site = getenv("COMPUTERNAME");
    if (!site || !site[0]) site = getenv("HOSTNAME");
    if (!site || !site[0]) site = "unknown-site";

    eval_set_var(ctx, out_var, sv_from_cstr(site), false, false);
}

// Avalia o comando 'add_link_options'
static void eval_add_link_options_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View opt = resolve_arg(ctx, args.items[i]);
        if (opt.count == 0) continue;
        build_model_add_global_link_option(ctx->model, ctx->arena, opt);
    }
}

// Avalia o comando 'link_directories'
static void eval_link_directories_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View dir = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(dir, sv_from_cstr("BEFORE")) || sv_eq_ci(dir, sv_from_cstr("AFTER"))) {
            continue;
        }
        dir = genex_trim(dir);
        if (dir.count == 0) continue;
        build_model_add_link_directory(ctx->model, ctx->arena, dir);
    }
}

static void eval_append_link_library_item(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View item) {
    if (item.count == 0) return;
    item = genex_trim(item);
    if (item.count == 0) return;

    if (sv_eq_ci(item, sv_from_cstr("debug")) ||
        sv_eq_ci(item, sv_from_cstr("optimized")) ||
        sv_eq_ci(item, sv_from_cstr("general"))) {
        return;
    }

    if (nob_sv_starts_with(item, sv_from_cstr("-framework "))) {
        if (target) {
            String_View fw = genex_trim(nob_sv_from_parts(item.data + 11, item.count - 11));
            build_target_add_library(target, ctx->arena, sv_from_cstr("-framework"), visibility);
            if (fw.count > 0) build_target_add_library(target, ctx->arena, fw, visibility);
        } else {
            build_model_add_global_link_library(ctx->model, ctx->arena, item);
        }
        return;
    }

    if (target) {
        Build_Target *dep = build_model_find_target(ctx->model, item);
        if (dep) {
            if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
                build_target_add_dependency(target, ctx->arena, dep->name);
            }
            if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
                build_target_add_interface_dependency(target, ctx->arena, dep->name);
            }
        } else {
            build_target_add_library(target, ctx->arena, item, visibility);
        }
    } else {
        build_model_add_global_link_library(ctx->model, ctx->arena, item);
    }
}

static void eval_append_link_library_value(Evaluator_Context *ctx, Build_Target *target, Visibility visibility, String_View value) {
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool at_sep = (i == value.count) || value.data[i] == ';';
        if (!at_sep) continue;
        String_View item = genex_trim(nob_sv_from_parts(value.data + start, i - start));
        eval_append_link_library_item(ctx, target, visibility, item);
        start = i + 1;
    }
}

// Avalia o comando 'link_libraries'
static void eval_link_libraries_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View value = resolve_arg(ctx, args.items[i]);
        eval_append_link_library_value(ctx, NULL, VISIBILITY_PRIVATE, value);
    }
}
static void eval_target_include_directories_command(Evaluator_Context *ctx, Args args) {
    (void)eval_target_foreach_scoped_item(ctx, args, 2, eval_tgtvis_add_include_dir, NULL);
}

static void eval_target_link_libraries_command(Evaluator_Context *ctx, Args args) {
    (void)eval_target_foreach_scoped_item(ctx, args, 2, eval_tgtvis_add_link_library_value, NULL);
}

static void eval_target_link_options_command(Evaluator_Context *ctx, Args args) {
    (void)eval_target_foreach_scoped_item(ctx, args, 3, eval_tgtvis_add_link_option, NULL);
}

static void eval_target_link_directories_command(Evaluator_Context *ctx, Args args) {
    (void)eval_target_foreach_scoped_item(ctx, args, 3, eval_tgtvis_add_link_directory, NULL);
}


// ============================================================================
// COMPILAÇÃO E BUILD
// ============================================================================

typedef struct CompileFeatures_Ud {
    Build_Target *target;
    Visibility visibility;
} CompileFeatures_Ud;

// Avalia cada item de uma lista de features de compilação, aplicando as que forem reconhecidas.
static void eval_compile_feature_item(Evaluator_Context *ctx, String_View feat, void *ud) {
    CompileFeatures_Ud *u = (CompileFeatures_Ud*)ud;
    if (!u || !u->target) return;

    if (nob_sv_starts_with(feat, sv_from_cstr("c_std_")) && feat.count > 6) {
        String_View ver = nob_sv_from_parts(feat.data + 6, feat.count - 6);
        String_View opt = sv_copy_to_arena(ctx->arena, sv_from_cstr(nob_temp_sprintf("-std=c"SV_Fmt, SV_Arg(ver))));
        build_target_add_compile_option(u->target, ctx->arena, opt, u->visibility, CONFIG_ALL);
    } else if (nob_sv_starts_with(feat, sv_from_cstr("cxx_std_")) && feat.count > 8) {
        String_View ver = nob_sv_from_parts(feat.data + 8, feat.count - 8);
        String_View opt = sv_copy_to_arena(ctx->arena, sv_from_cstr(nob_temp_sprintf("-std=c++"SV_Fmt, SV_Arg(ver))));
        build_target_add_compile_option(u->target, ctx->arena, opt, u->visibility, CONFIG_ALL);
    }
}

// Avalia o comando 'target_compile_features'
static void eval_target_compile_features_command(Evaluator_Context *ctx, Args args) {
    Target_Vis_Args tv;
    if (!eval_parse_target_visibility(ctx, args, 3, &tv)) return;

    CompileFeatures_Ud ud = { .target = tv.target, .visibility = tv.visibility };
    for (size_t i = tv.start_idx; i < args.count; i++) {
        String_View raw = resolve_arg(ctx, args.items[i]);
        eval_foreach_semicolon_item(ctx, raw, /*trim_ws=*/true, eval_compile_feature_item, &ud);
    }
}


// ============================================================================
// PRECOMPILED HEADERS
// ============================================================================

typedef struct Pch_Ud {
    Build_Target *target;
    Visibility visibility;
} Pch_Ud;

static void eval_pch_header_item(Evaluator_Context *ctx, String_View header, void *ud) {
    Pch_Ud *u = (Pch_Ud*)ud;
    if (!u || !u->target) return;

    build_target_add_compile_option(u->target, ctx->arena, sv_from_cstr("-include"), u->visibility, CONFIG_ALL);
    build_target_add_compile_option(u->target, ctx->arena, header, u->visibility, CONFIG_ALL);
}

static void eval_target_precompile_headers_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model) return;
    if (args.count < 3) return;

    // Verificar REUSE_FROM antes do parse normal
    if (args.count > 1) {
        String_View second = resolve_arg(ctx, args.items[1]);
        if (sv_eq_ci(second, sv_from_cstr("REUSE_FROM"))) {
            // Suporte minimo: aceita assinatura REUSE_FROM sem erro, sem transferencia de estado.
            return;
        }
    }

    Target_Vis_Args tv;
    if (!eval_parse_target_visibility(ctx, args, 3, &tv)) return;

    Pch_Ud ud = { .target = tv.target, .visibility = tv.visibility };
    for (size_t i = tv.start_idx; i < args.count; i++) {
        String_View raw = resolve_arg(ctx, args.items[i]);
        eval_foreach_semicolon_item(ctx, raw, /*trim_ws=*/true, eval_pch_header_item, &ud);
    }
}


// ============================================================================
// DEPENDÊNCIAS
// ============================================================================

typedef struct AddDeps_Ud {
    Build_Target *target;
} AddDeps_Ud;

static void eval_add_dep_item(Evaluator_Context *ctx, String_View dep, void *ud) {
    AddDeps_Ud *u = (AddDeps_Ud*)ud;
    if (!u || !u->target) return;
    build_target_add_dependency(u->target, ctx->arena, dep);
}

// Avalia o comando 'add_dependencies'
static void eval_add_dependencies_command(Evaluator_Context *ctx, Args args) {
    Build_Target *target = eval_get_target_from_args(ctx, args, 2);
    if (!target) {
        if (args.count >= 1) {
            String_View target_name = resolve_arg(ctx, args.items[0]);
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_dependencies",
                nob_temp_sprintf("target principal nao encontrado: "SV_Fmt, SV_Arg(target_name)),
                "declare o target antes de add_dependencies");
        }
        return;
    }

    AddDeps_Ud ud = { .target = target };
    for (size_t i = 1; i < args.count; i++) {
        String_View raw_dep = resolve_arg(ctx, args.items[i]);
        eval_foreach_semicolon_item(ctx, raw_dep, /*trim_ws=*/false, eval_add_dep_item, &ud);
    }
}

// Avalia o comando 'include_external_msproject'
static void eval_include_external_msproject_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 2) return;

    String_View target_name = resolve_arg(ctx, args.items[0]);
    String_View project_location = resolve_arg(ctx, args.items[1]);
    if (target_name.count == 0) return;

    Build_Target *target = build_model_add_target(ctx->model, target_name, TARGET_UTILITY);
    if (!target) return;

    if (project_location.count > 0) {
        build_target_set_property(
            target, ctx->arena, sv_from_cstr("EXTERNAL_MSPROJECT_LOCATION"), project_location);
    }

    size_t depends_start = SIZE_MAX;
    for (size_t i = 2; i < args.count; i++) {
        String_View token = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(token, sv_from_cstr("DEPENDS"))) {
            depends_start = i + 1;
            break;
        }
    }
    if (depends_start == SIZE_MAX) return;

    AddDeps_Ud ud = { .target = target };
    for (size_t i = depends_start; i < args.count; i++) {
        String_View dep_raw = resolve_arg(ctx, args.items[i]);
        eval_foreach_semicolon_item(ctx, dep_raw, /*trim_ws=*/false, eval_add_dep_item, &ud);
    }
}

// Avalia o comando 'load_command'
static void eval_load_command_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 1) return;
    String_View command_name = resolve_arg(ctx, args.items[0]);
    if (command_name.count == 0) return;

    String_View loaded_var = sv_from_cstr(nob_temp_sprintf("%s_LOADED", nob_temp_sv_to_cstr(command_name)));
    eval_set_var(ctx, loaded_var, sv_from_cstr("TRUE"), false, false);

    String_View internal_key =
        sv_from_cstr(nob_temp_sprintf("__LOADED_COMMAND__%s", nob_temp_sv_to_cstr(command_name)));
    build_model_set_cache_variable(
        ctx->model, internal_key, sv_from_cstr("TRUE"), sv_from_cstr("INTERNAL"), sv_from_cstr(""));
}

static String_View join_args_with_semicolon(Evaluator_Context *ctx, String_View *items, size_t count) {
    if (count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < count; i++) {
        if (items[i].count == 0 || items[i].data == NULL) continue;
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, items[i].data, items[i].count);
    }
    if (sb.count == 0 || sb.items == NULL) {
        nob_sb_free(sb);
        return sv_from_cstr("");
    }
    char *dup = arena_strndup(ctx->arena, sb.items, sb.count);
    String_View out = dup ? sv_from_cstr(dup) : sv_from_cstr("");
    nob_sb_free(sb);
    return out;
}

// Avalia o comando 'write_basic_package_version_file'
static void eval_write_basic_package_version_file_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    String_View out_arg = resolve_arg(ctx, args.items[0]);
    String_View out_path = path_is_absolute_sv(out_arg)
        ? out_arg
        : path_join_arena(ctx->arena, ctx->current_binary_dir, out_arg);

    if (!ensure_parent_dirs_for_path(ctx->arena, out_path)) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "write_basic_package_version_file",
            nob_temp_sprintf("falha ao preparar diretorio para: "SV_Fmt, SV_Arg(out_path)),
            "arquivo de versao de pacote nao sera gerado");
        return;
    }

    const char *content =
        "# Generated by cmk2nob (partial support)\n"
        "set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
        "set(PACKAGE_VERSION_EXACT TRUE)\n";
    if (!nob_write_entire_file(nob_temp_sv_to_cstr(out_path), content, strlen(content))) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "write_basic_package_version_file",
            nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(out_path)),
            "arquivo de versao de pacote nao sera gerado");
    }
}

// Avalia o comando 'configure_package_config_file'
static void eval_configure_package_config_file_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;
    String_View input_arg = resolve_arg(ctx, args.items[0]);
    String_View output_arg = resolve_arg(ctx, args.items[1]);
    String_View input_path = path_is_absolute_sv(input_arg)
        ? input_arg
        : path_join_arena(ctx->arena, ctx->current_list_dir, input_arg);
    String_View output_path = path_is_absolute_sv(output_arg)
        ? output_arg
        : path_join_arena(ctx->arena, ctx->current_binary_dir, output_arg);

    if (!ensure_parent_dirs_for_path(ctx->arena, output_path)) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "configure_package_config_file",
            nob_temp_sprintf("falha ao preparar diretorio para: "SV_Fmt, SV_Arg(output_path)),
            "arquivo de config de pacote nao sera gerado");
        return;
    }

    String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(input_path));
    if (!content.data) {
        const char *fallback =
            "# Generated by cmk2nob (partial support)\n"
            "set(PACKAGE_INIT_DONE TRUE)\n";
        if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), fallback, strlen(fallback))) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "configure_package_config_file",
                nob_temp_sprintf("falha ao escrever fallback: "SV_Fmt, SV_Arg(output_path)),
                "arquivo de config de pacote nao sera gerado");
        }
        return;
    }

    String_View rendered = configure_expand_variables(ctx, content, false);
    if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), rendered.data, rendered.count)) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "configure_package_config_file",
            nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(output_path)),
            "arquivo de config de pacote nao sera gerado");
    }
}

static String_View append_property_value(Evaluator_Context *ctx, String_View current, String_View value, bool append, bool append_string) {
    if (!append && !append_string) return value;
    if (current.count == 0) return value;
    if (value.count == 0) return current;

    String_Builder sb = {0};
    sb_append_buf(&sb, current.data, current.count);
    if (append) sb_append(&sb, ';');
    sb_append_buf(&sb, value.data, value.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static void eval_apply_target_property(Evaluator_Context *ctx, Build_Target *target, String_View key, String_View value, bool append, bool append_string) {
    String_View current = build_target_get_property(target, key);
    String_View final_value = append_property_value(ctx, current, value, append, append_string);
    build_target_set_property_smart(target, ctx->arena, key, final_value);
}

static String_View target_property_list_to_sv(Evaluator_Context *ctx, String_List *list) {
    if (!list || list->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < list->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, list->items[i].data, list->items[i].count);
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View eval_get_target_property_value(Evaluator_Context *ctx, Build_Target *target, String_View prop_name) {
    String_View value = build_target_get_property_computed(target, prop_name, ctx->model->default_config);
    return sv_copy_to_arena(ctx->arena, value);
}

static String_View internal_advanced_key_for_var(Evaluator_Context *ctx, String_View var_name) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__ADVANCED__");
    sb_append_buf(&sb, var_name.data, var_name.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View internal_global_property_key(Evaluator_Context *ctx, String_View prop_name) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__GLOBAL_PROP__");
    sb_append_buf(&sb, prop_name.data, prop_name.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View canonical_property_scope(String_View scope) {
    if (sv_eq_ci(scope, sv_from_cstr("GLOBAL"))) return sv_from_cstr("GLOBAL");
    if (sv_eq_ci(scope, sv_from_cstr("DIRECTORY"))) return sv_from_cstr("DIRECTORY");
    if (sv_eq_ci(scope, sv_from_cstr("TARGET"))) return sv_from_cstr("TARGET");
    if (sv_eq_ci(scope, sv_from_cstr("SOURCE"))) return sv_from_cstr("SOURCE");
    if (sv_eq_ci(scope, sv_from_cstr("TEST"))) return sv_from_cstr("TEST");
    if (sv_eq_ci(scope, sv_from_cstr("VARIABLE"))) return sv_from_cstr("VARIABLE");
    if (sv_eq_ci(scope, sv_from_cstr("CACHE")) || sv_eq_ci(scope, sv_from_cstr("CACHED_VARIABLE"))) {
        return sv_from_cstr("CACHED_VARIABLE");
    }
    return sv_from_cstr("");
}

static String_View internal_property_definition_key(Evaluator_Context *ctx,
                                                    String_View canonical_scope,
                                                    String_View prop_name,
                                                    const char *field) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__PROPDEF__");
    sb_append_buf(&sb, canonical_scope.data, canonical_scope.count);
    sb_append_cstr(&sb, "__");
    sb_append_buf(&sb, prop_name.data, prop_name.count);
    sb_append_cstr(&sb, "__");
    sb_append_cstr(&sb, field);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static bool property_definition_exists(Evaluator_Context *ctx, String_View scope, String_View prop_name) {
    String_View canonical_scope = canonical_property_scope(scope);
    if (canonical_scope.count == 0 || prop_name.count == 0) return false;
    String_View key = internal_property_definition_key(ctx, canonical_scope, prop_name, "DEFINED");
    String_View value = build_model_get_cache_variable(ctx->model, key);
    return value.count > 0 && !cmake_string_is_false(value);
}

static String_View property_definition_docs(Evaluator_Context *ctx,
                                            String_View scope,
                                            String_View prop_name,
                                            bool full_docs) {
    String_View canonical_scope = canonical_property_scope(scope);
    if (canonical_scope.count == 0 || prop_name.count == 0) return sv_from_cstr("NOTFOUND");
    String_View key = internal_property_definition_key(
        ctx, canonical_scope, prop_name, full_docs ? "FULL_DOCS" : "BRIEF_DOCS");
    String_View value = build_model_get_cache_variable(ctx->model, key);
    return value.count > 0 ? value : sv_from_cstr("NOTFOUND");
}

// Avalia o comando 'define_property'
static void eval_define_property_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 3) return;

    String_View *resolved = arena_alloc_array(ctx->arena, String_View, args.count);
    if (!resolved) return;
    for (size_t i = 0; i < args.count; i++) {
        resolved[i] = resolve_arg(ctx, args.items[i]);
    }

    String_View canonical_scope = canonical_property_scope(resolved[0]);
    if (canonical_scope.count == 0) return;

    size_t prop_idx = SIZE_MAX;
    for (size_t i = 1; i < args.count; i++) {
        if (sv_eq_ci(resolved[i], sv_from_cstr("PROPERTY"))) {
            prop_idx = i;
            break;
        }
    }
    if (prop_idx == SIZE_MAX || prop_idx + 1 >= args.count) return;

    String_View prop_name = resolved[prop_idx + 1];
    if (prop_name.count == 0) return;

    bool inherited = false;
    String_View brief_docs = sv_from_cstr("");
    String_View full_docs = sv_from_cstr("");

    size_t i = prop_idx + 2;
    while (i < args.count) {
        String_View token = resolved[i];
        if (sv_eq_ci(token, sv_from_cstr("INHERITED"))) {
            inherited = true;
            i++;
            continue;
        }

        bool brief_mode = sv_eq_ci(token, sv_from_cstr("BRIEF_DOCS"));
        bool full_mode = sv_eq_ci(token, sv_from_cstr("FULL_DOCS"));
        if (!brief_mode && !full_mode) {
            i++;
            continue;
        }

        size_t doc_start = i + 1;
        i = doc_start;
        while (i < args.count) {
            String_View next = resolved[i];
            if (sv_eq_ci(next, sv_from_cstr("INHERITED")) ||
                sv_eq_ci(next, sv_from_cstr("BRIEF_DOCS")) ||
                sv_eq_ci(next, sv_from_cstr("FULL_DOCS"))) {
                break;
            }
            i++;
        }

        String_View docs = join_args_with_semicolon(ctx, resolved + doc_start, i - doc_start);
        if (brief_mode) {
            brief_docs = docs;
        } else {
            full_docs = docs;
        }
    }

    build_model_set_cache_variable(
        ctx->model,
        internal_property_definition_key(ctx, canonical_scope, prop_name, "DEFINED"),
        sv_from_cstr("TRUE"),
        sv_from_cstr("INTERNAL"),
        sv_from_cstr(""));
    build_model_set_cache_variable(
        ctx->model,
        internal_property_definition_key(ctx, canonical_scope, prop_name, "INHERITED"),
        inherited ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"),
        sv_from_cstr("INTERNAL"),
        sv_from_cstr(""));
    if (brief_docs.count > 0) {
        build_model_set_cache_variable(
            ctx->model,
            internal_property_definition_key(ctx, canonical_scope, prop_name, "BRIEF_DOCS"),
            brief_docs,
            sv_from_cstr("INTERNAL"),
            sv_from_cstr(""));
    }
    if (full_docs.count > 0) {
        build_model_set_cache_variable(
            ctx->model,
            internal_property_definition_key(ctx, canonical_scope, prop_name, "FULL_DOCS"),
            full_docs,
            sv_from_cstr("INTERNAL"),
            sv_from_cstr(""));
    }
}

// Avalia o comando 'mark_as_advanced'
static void eval_mark_as_advanced_command(Evaluator_Context *ctx, Args args) {
    if (args.count == 0) return;
    bool clear_mode = false;
    size_t start = 0;
    String_View first = resolve_arg(ctx, args.items[0]);
    if (sv_eq_ci(first, sv_from_cstr("CLEAR"))) {
        clear_mode = true;
        start = 1;
    } else if (sv_eq_ci(first, sv_from_cstr("FORCE"))) {
        start = 1;
    }

    for (size_t i = start; i < args.count; i++) {
        String_View var_name = resolve_arg(ctx, args.items[i]);
        if (var_name.count == 0) continue;
        String_View key = internal_advanced_key_for_var(ctx, var_name);
        build_model_set_cache_variable(ctx->model, key,
            clear_mode ? sv_from_cstr("FALSE") : sv_from_cstr("TRUE"),
            sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    }
}

// Avalia o comando 'set_property'
static void eval_set_property_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;

    String_View *resolved = arena_alloc_array(ctx->arena, String_View, args.count);
    if (!resolved) return;
    for (size_t i = 0; i < args.count; i++) {
        resolved[i] = resolve_arg(ctx, args.items[i]);
    }

    size_t prop_idx = args.count;
    for (size_t i = 1; i < args.count; i++) {
        if (sv_eq_ci(resolved[i], sv_from_cstr("PROPERTY"))) {
            prop_idx = i;
            break;
        }
    }
    if (prop_idx == args.count || prop_idx + 1 >= args.count) return;

    bool append = false;
    bool append_string = false;
    for (size_t i = 1; i < prop_idx; i++) {
        if (sv_eq_ci(resolved[i], sv_from_cstr("APPEND"))) append = true;
        if (sv_eq_ci(resolved[i], sv_from_cstr("APPEND_STRING"))) append_string = true;
    }

    String_View scope = resolved[0];
    String_View prop_name = resolved[prop_idx + 1];
    String_View value = join_args_with_semicolon(ctx, resolved + prop_idx + 2, args.count - (prop_idx + 2));

    if (sv_eq_ci(scope, sv_from_cstr("TARGET"))) {
        for (size_t i = 1; i < prop_idx; i++) {
            if (sv_eq_ci(resolved[i], sv_from_cstr("APPEND")) || sv_eq_ci(resolved[i], sv_from_cstr("APPEND_STRING"))) {
                continue;
            }
            Build_Target *target = build_model_find_target(ctx->model, resolved[i]);
            if (!target) continue;
            eval_apply_target_property(ctx, target, prop_name, value, append, append_string);
        }
        return;
    }

    if (sv_eq_ci(scope, sv_from_cstr("GLOBAL"))) {
        if (sv_eq_ci(prop_name, sv_from_cstr("USE_FOLDERS")) ||
            sv_eq_ci(prop_name, sv_from_cstr("PREDEFINED_TARGETS_FOLDER"))) {
            String_View k = internal_global_property_key(ctx, prop_name);
            String_View curr = build_model_get_cache_variable(ctx->model, k);
            String_View final_value = append_property_value(ctx, curr, value, append, append_string);
            build_model_set_cache_variable(ctx->model, k, final_value, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
        }
        return;
    }

    if (sv_eq_ci(scope, sv_from_cstr("CACHE"))) {
        String_View entry = sv_from_cstr("");
        for (size_t i = 1; i < prop_idx; i++) {
            if (sv_eq_ci(resolved[i], sv_from_cstr("APPEND")) || sv_eq_ci(resolved[i], sv_from_cstr("APPEND_STRING"))) continue;
            entry = resolved[i];
            break;
        }
        if (entry.count == 0) return;

        if (sv_eq_ci(prop_name, sv_from_cstr("VALUE"))) {
            String_View curr = build_model_get_cache_variable(ctx->model, entry);
            String_View final_value = append_property_value(ctx, curr, value, append, append_string);
            build_model_set_cache_variable(ctx->model, entry, final_value, sv_from_cstr("STRING"), sv_from_cstr(""));
            return;
        }
        if (sv_eq_ci(prop_name, sv_from_cstr("ADVANCED"))) {
            String_View k = internal_advanced_key_for_var(ctx, entry);
            bool truthy = sv_bool_is_true(value);
            build_model_set_cache_variable(ctx->model, k, truthy ? sv_from_cstr("TRUE") : sv_from_cstr("FALSE"),
                                           sv_from_cstr("INTERNAL"), sv_from_cstr(""));
            return;
        }
    }
}


// ============================================================================
// PROPRIEDADES DE TARGETS
// ============================================================================

// Avalia o comando 'get_target_property'
static void eval_get_target_property_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;
    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View target_name = resolve_arg(ctx, args.items[1]);
    String_View prop_name = resolve_arg(ctx, args.items[2]);

    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) {
        eval_set_var(ctx, out_var, sv_from_cstr(nob_temp_sprintf(SV_Fmt "-NOTFOUND", SV_Arg(target_name))), false, false);
        return;
    }
    String_View value = eval_get_target_property_value(ctx, target, prop_name);
    eval_set_var(ctx, out_var, value, false, false);
}

// Avalia o comando 'get_directory_property'
static void eval_get_directory_property_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    size_t idx = 1;

    // Assinatura opcional: get_directory_property(<out> DIRECTORY <dir> <prop|DEFINITION ...>)
    if (idx < args.count && sv_eq_ci(resolve_arg(ctx, args.items[idx]), sv_from_cstr("DIRECTORY"))) {
        if (idx + 2 >= args.count) return;
        idx += 2; // Ignora caminho: evaluator atual mantem estado global de diretorio.
    }
    if (idx >= args.count) return;

    String_View prop_or_mode = resolve_arg(ctx, args.items[idx]);
    if (sv_eq_ci(prop_or_mode, sv_from_cstr("DEFINITION"))) {
        if (idx + 1 >= args.count) {
            eval_set_var(ctx, out_var, sv_from_cstr(""), false, false);
            return;
        }
        String_View var_name = resolve_arg(ctx, args.items[idx + 1]);
        eval_set_var(ctx, out_var, eval_get_var(ctx, var_name), false, false);
        return;
    }

    String_View value = sv_from_cstr("");
    if (sv_eq_ci(prop_or_mode, sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->directories.include_dirs);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("SYSTEM_INCLUDE_DIRECTORIES"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->directories.system_include_dirs);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("LINK_DIRECTORIES"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->directories.link_dirs);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("COMPILE_DEFINITIONS")) ||
               sv_eq_ci(prop_or_mode, sv_from_cstr("DEFINITIONS"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->global_definitions);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("COMPILE_OPTIONS"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->global_compile_options);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("LINK_OPTIONS"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->global_link_options);
    } else if (sv_eq_ci(prop_or_mode, sv_from_cstr("LINK_LIBRARIES"))) {
        value = target_property_list_to_sv(ctx, &ctx->model->global_link_libraries);
    }

    eval_set_var(ctx, out_var, value, false, false);
}

// Avalia o comando 'get_property'
static void eval_get_property_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 4) return;

    String_View *resolved = arena_alloc_array(ctx->arena, String_View, args.count);
    if (!resolved) return;
    for (size_t i = 0; i < args.count; i++) resolved[i] = resolve_arg(ctx, args.items[i]);

    String_View out_var = resolved[0];
    String_View scope = resolved[1];
    size_t prop_idx = args.count;
    for (size_t i = 2; i < args.count; i++) {
        if (sv_eq_ci(resolved[i], sv_from_cstr("PROPERTY"))) {
            prop_idx = i;
            break;
        }
    }
    if (prop_idx == args.count || prop_idx + 1 >= args.count) return;

    String_View prop_name = resolved[prop_idx + 1];
    bool query_set = false;
    bool query_defined = false;
    bool query_brief_docs = false;
    bool query_full_docs = false;
    for (size_t i = prop_idx + 2; i < args.count; i++) {
        if (sv_eq_ci(resolved[i], sv_from_cstr("SET"))) query_set = true;
        if (sv_eq_ci(resolved[i], sv_from_cstr("DEFINED"))) query_defined = true;
        if (sv_eq_ci(resolved[i], sv_from_cstr("BRIEF_DOCS"))) query_brief_docs = true;
        if (sv_eq_ci(resolved[i], sv_from_cstr("FULL_DOCS"))) query_full_docs = true;
    }

    if (sv_eq_ci(scope, sv_from_cstr("TARGET")) && args.count >= 5) {
        Build_Target *target = build_model_find_target(ctx->model, resolved[2]);
        String_View value = target ? eval_get_target_property_value(ctx, target, prop_name) : sv_from_cstr("");
        bool has_value = value.count > 0;
        bool has_definition = property_definition_exists(ctx, scope, prop_name);
        if (query_brief_docs || query_full_docs) {
            eval_set_var(ctx, out_var, property_definition_docs(ctx, scope, prop_name, query_full_docs), false, false);
        } else if (query_set) {
            eval_set_var(ctx, out_var, has_value ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else if (query_defined) {
            eval_set_var(ctx, out_var, (has_value || has_definition) ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else {
            eval_set_var(ctx, out_var, value, false, false);
        }
        return;
    }

    if (sv_eq_ci(scope, sv_from_cstr("VARIABLE")) && args.count >= 5) {
        String_View var_name = resolved[2];
        String_View value = sv_from_cstr("");
        if (sv_eq_ci(prop_name, sv_from_cstr("VALUE"))) {
            value = eval_get_var(ctx, var_name);
        }
        bool has_value = value.count > 0;
        bool has_definition = property_definition_exists(ctx, scope, prop_name);
        if (query_brief_docs || query_full_docs) {
            eval_set_var(ctx, out_var, property_definition_docs(ctx, scope, prop_name, query_full_docs), false, false);
        } else if (query_set) {
            eval_set_var(ctx, out_var, has_value ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else if (query_defined) {
            eval_set_var(ctx, out_var, (has_value || has_definition) ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else {
            eval_set_var(ctx, out_var, value, false, false);
        }
        return;
    }

    if (sv_eq_ci(scope, sv_from_cstr("CACHE")) && args.count >= 5) {
        String_View cache_key = resolved[2];
        String_View value = sv_from_cstr("");
        if (sv_eq_ci(prop_name, sv_from_cstr("VALUE"))) {
            value = build_model_get_cache_variable(ctx->model, cache_key);
        } else if (sv_eq_ci(prop_name, sv_from_cstr("ADVANCED"))) {
            value = build_model_get_cache_variable(ctx->model, internal_advanced_key_for_var(ctx, cache_key));
        }
        bool has_value = value.count > 0;
        bool has_definition = property_definition_exists(ctx, scope, prop_name);
        if (query_brief_docs || query_full_docs) {
            eval_set_var(ctx, out_var, property_definition_docs(ctx, scope, prop_name, query_full_docs), false, false);
        } else if (query_set) {
            eval_set_var(ctx, out_var, has_value ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else if (query_defined) {
            eval_set_var(ctx, out_var, (has_value || has_definition) ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else {
            eval_set_var(ctx, out_var, value, false, false);
        }
        return;
    }

    if (sv_eq_ci(scope, sv_from_cstr("GLOBAL"))) {
        String_View value = sv_from_cstr("");
        if (sv_eq_ci(prop_name, sv_from_cstr("GENERATOR_IS_MULTI_CONFIG"))) {
            value = sv_from_cstr("0");
        } else if (sv_eq_ci(prop_name, sv_from_cstr("TARGETS"))) {
            String_Builder sb = {0};
            for (size_t i = 0; i < ctx->model->target_count; i++) {
                if (i > 0) sb_append(&sb, ';');
                sb_append_buf(&sb, ctx->model->targets[i].name.data, ctx->model->targets[i].name.count);
            }
            value = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
            nob_sb_free(sb);
        } else {
            value = build_model_get_cache_variable(ctx->model, internal_global_property_key(ctx, prop_name));
        }
        bool has_value = value.count > 0;
        bool has_definition = property_definition_exists(ctx, scope, prop_name);
        if (query_brief_docs || query_full_docs) {
            eval_set_var(ctx, out_var, property_definition_docs(ctx, scope, prop_name, query_full_docs), false, false);
        } else if (query_set) {
            eval_set_var(ctx, out_var, has_value ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else if (query_defined) {
            eval_set_var(ctx, out_var, (has_value || has_definition) ? sv_from_cstr("1") : sv_from_cstr("0"), false, false);
        } else {
            eval_set_var(ctx, out_var, value, false, false);
        }
        return;
    }
}


// Avalia o comando 'set_target_properties'
static void eval_set_target_properties_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 4) return;

    size_t props_idx = args.count;
    for (size_t i = 0; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("PROPERTIES"))) {
            props_idx = i;
            break;
        }
    }

    if (props_idx == args.count || props_idx == 0 || props_idx + 2 > args.count) return;

    for (size_t t = 0; t < props_idx; t++) {
        String_View target_name = resolve_arg(ctx, args.items[t]);
        Build_Target *target = build_model_find_target(ctx->model, target_name);
        if (!target) continue;

        for (size_t i = props_idx + 1; i + 1 < args.count; i += 2) {
            String_View key = resolve_arg(ctx, args.items[i]);
            String_View value = resolve_arg(ctx, args.items[i + 1]);
            eval_apply_target_property(ctx, target, key, value, false, false);
        }
    }
}


// ============================================================================
// EXPORT E INSTALAÇÃO
// ============================================================================

// Avalia o comando 'install'
static void eval_install_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;

    String_View mode = resolve_arg(ctx, args.items[0]);

    if (nob_sv_eq(mode, sv_from_cstr("TARGETS"))) {
        String_List targets = {0};
        string_list_init(&targets);
        String_View export_set_name = sv_from_cstr("");
        size_t i = 1;
        while (i < args.count) {
            String_View item = resolve_arg(ctx, args.items[i]);
            if (is_install_keyword(item)) break;
            string_list_add(&targets, ctx->arena, item);
            i++;
        }
        if (targets.count == 0) return;

        String_View general_dest = ctx->model->install_rules.prefix.count > 0 ? ctx->model->install_rules.prefix : sv_from_cstr("install");
        String_View runtime_dest = sv_from_cstr("");
        String_View library_dest = sv_from_cstr("");
        String_View archive_dest = sv_from_cstr("");
        String_View section = sv_from_cstr("");

        while (i < args.count) {
            String_View key = resolve_arg(ctx, args.items[i]);
            if (is_install_target_group_keyword(key)) {
                section = key;
                i++;
                continue;
            }
            if (nob_sv_eq(key, sv_from_cstr("EXPORT")) && i + 1 < args.count) {
                export_set_name = resolve_arg(ctx, args.items[i + 1]);
                i += 2;
                continue;
            }
            if (nob_sv_eq(key, sv_from_cstr("DESTINATION")) && i + 1 < args.count) {
                String_View dest = resolve_arg(ctx, args.items[i + 1]);
                if (section.count == 0) {
                    general_dest = dest;
                } else if (nob_sv_eq(section, sv_from_cstr("RUNTIME"))) {
                    runtime_dest = dest;
                } else if (nob_sv_eq(section, sv_from_cstr("LIBRARY"))) {
                    library_dest = dest;
                } else if (nob_sv_eq(section, sv_from_cstr("ARCHIVE"))) {
                    archive_dest = dest;
                }
                i += 2;
                continue;
            }
            i++;
        }

        if (ctx->model->install_rules.prefix.count == 0) {
            build_model_set_install_prefix(ctx->model, general_dest);
        }

        for (size_t t = 0; t < targets.count; t++) {
            String_View target_name = targets.items[t];
            String_View destination = general_dest;
            Build_Target *target = build_model_find_target(ctx->model, target_name);
            if (target) {
                if (target->type == TARGET_EXECUTABLE && runtime_dest.count > 0) destination = runtime_dest;
                else if (target->type == TARGET_STATIC_LIB && archive_dest.count > 0) destination = archive_dest;
                else if (target->type == TARGET_SHARED_LIB && library_dest.count > 0) destination = library_dest;
            }
            build_model_add_install_rule(ctx->model, ctx->arena, INSTALL_RULE_TARGET, target_name, destination);
        }
        if (export_set_name.count > 0) {
            String_View set_key = internal_install_export_set_key(ctx, export_set_name);
            String_View existing = build_model_get_cache_variable(ctx->model, set_key);
            String_List all_targets = {0};
            string_list_init(&all_targets);
            List_Add_Ud ud_existing = { .list = &all_targets, .unique = false };
            eval_foreach_semicolon_item(ctx, existing, /*trim_ws=*/true, eval_list_add_item, &ud_existing);
            for (size_t t = 0; t < targets.count; t++) {
                string_list_add_unique(&all_targets, ctx->arena, targets.items[t]);
            }
            String_Builder list_sb = {0};
            for (size_t t = 0; t < all_targets.count; t++) {
                if (t > 0) sb_append(&list_sb, ';');
                sb_append_buf(&list_sb, all_targets.items[t].data, all_targets.items[t].count);
            }
            String_View merged_targets = sv_from_cstr(arena_strndup(ctx->arena, list_sb.items, list_sb.count));
            nob_sb_free(list_sb);
            build_model_set_cache_variable(ctx->model, set_key, merged_targets, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
        }
        build_model_set_install_enabled(ctx->model, true);
        return;
    }

    size_t dest_idx = args.count;
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("DESTINATION"))) {
            dest_idx = i;
            break;
        }
    }

    String_View destination = sv_from_cstr("install");
    if (dest_idx + 1 < args.count) {
        destination = resolve_arg(ctx, args.items[dest_idx + 1]);
    } else if (ctx->model->install_rules.prefix.count > 0) {
        destination = ctx->model->install_rules.prefix;
    }
    if (ctx->model->install_rules.prefix.count == 0) {
        build_model_set_install_prefix(ctx->model, destination);
    }

    size_t end = dest_idx < args.count ? dest_idx : args.count;
    if (end <= 1) return;

    if (nob_sv_eq(mode, sv_from_cstr("FILES"))) {
        for (size_t i = 1; i < end; i++) {
            String_View item = resolve_arg(ctx, args.items[i]);
            if (is_install_keyword(item)) continue;
            build_model_add_install_rule(ctx->model, ctx->arena, INSTALL_RULE_FILE, item, destination);
        }
        build_model_set_install_enabled(ctx->model, true);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("PROGRAMS"))) {
        for (size_t i = 1; i < end; i++) {
            String_View item = resolve_arg(ctx, args.items[i]);
            if (is_install_keyword(item)) continue;
            build_model_add_install_rule(ctx->model, ctx->arena, INSTALL_RULE_PROGRAM, item, destination);
        }
        build_model_set_install_enabled(ctx->model, true);
        return;
    }

    if (nob_sv_eq(mode, sv_from_cstr("DIRECTORY"))) {
        for (size_t i = 1; i < end; i++) {
            String_View item = resolve_arg(ctx, args.items[i]);
            if (is_install_keyword(item)) continue;
            build_model_add_install_rule(ctx->model, ctx->arena, INSTALL_RULE_DIRECTORY, item, destination);
        }
        build_model_set_install_enabled(ctx->model, true);
    }
}

// Avalia o comando 'enable_testing'
static void eval_enable_testing_command(Evaluator_Context *ctx, Args args) {
    (void)args;
    if (!ctx || !ctx->model) return;
    build_model_set_testing_enabled(ctx->model, true);
    eval_set_var(ctx, sv_from_cstr("CMAKE_TESTING_ENABLED"), sv_from_cstr("ON"), false, false);
}

static String_View path_parent_dir_arena(Arena *arena, String_View full_path);

static String_View ctest_option_value(Evaluator_Context *ctx, Args args, const char *opt_name) {
    if (!ctx) return sv_from_cstr("");
    String_View opt = sv_from_cstr(opt_name);
    for (size_t i = 0; i + 1 < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, opt)) return resolve_arg(ctx, args.items[i + 1]);
    }
    return sv_from_cstr("");
}

static void ctest_set_common_dirs(Evaluator_Context *ctx, String_View source_dir, String_View binary_dir) {
    if (!ctx) return;
    if (source_dir.count == 0) source_dir = ctx->current_source_dir;
    if (binary_dir.count == 0) binary_dir = ctx->current_binary_dir;
    eval_set_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"), source_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"), binary_dir, false, false);
}

// Avalia o comando 'ctest_start'
static void eval_ctest_start_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    eval_set_var(ctx, sv_from_cstr("CTEST_SCRIPT_MODE"), sv_from_cstr("ON"), false, false);

    String_View model = args.count > 0 ? resolve_arg(ctx, args.items[0]) : sv_from_cstr("Experimental");
    String_View track = args.count > 1 ? resolve_arg(ctx, args.items[1]) : sv_from_cstr("Experimental");
    String_View source_dir = ctest_option_value(ctx, args, "SOURCE");
    String_View binary_dir = ctest_option_value(ctx, args, "BINARY");
    ctest_set_common_dirs(ctx, source_dir, binary_dir);

    eval_set_var(ctx, sv_from_cstr("CTEST_DASHBOARD_MODEL"), model, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_DASHBOARD_TRACK"), track, false, false);
}

// Avalia o comando 'ctest_update'
static void eval_ctest_update_command(Evaluator_Context *ctx, Args args) {
    (void)args;
    if (!ctx) return;
    eval_set_var(ctx, sv_from_cstr("CTEST_UPDATE_RETURN_VALUE"), sv_from_cstr("0"), false, false);
}

// Avalia o comando 'ctest_configure'
static void eval_ctest_configure_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    String_View build_dir = ctest_option_value(ctx, args, "BUILD");
    String_View source_dir = ctest_option_value(ctx, args, "SOURCE");
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");

    ctest_set_common_dirs(ctx, source_dir, build_dir);
    eval_set_var(ctx, sv_from_cstr("CTEST_CONFIGURE_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
}

// Avalia o comando 'ctest_build'
static void eval_ctest_build_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");
    String_View err_var = ctest_option_value(ctx, args, "NUMBER_ERRORS");
    String_View warn_var = ctest_option_value(ctx, args, "NUMBER_WARNINGS");
    eval_set_var(ctx, sv_from_cstr("CTEST_BUILD_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
    if (err_var.count > 0) eval_set_var(ctx, err_var, sv_from_cstr("0"), false, false);
    if (warn_var.count > 0) eval_set_var(ctx, warn_var, sv_from_cstr("0"), false, false);
}

// Avalia o comando 'ctest_test'
static void eval_ctest_test_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model) return;
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");
    String_View count = sv_from_cstr(nob_temp_sprintf("%zu", ctx->model->test_count));
    eval_set_var(ctx, sv_from_cstr("CTEST_TEST_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_TESTS_RUN"), count, false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
}

// Avalia o comando 'ctest_coverage'
static void eval_ctest_coverage_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
}

static bool ctest_coverage_collect_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("TARBALL")) ||
           sv_eq_ci(tok, sv_from_cstr("SOURCE")) ||
           sv_eq_ci(tok, sv_from_cstr("BUILD")) ||
           sv_eq_ci(tok, sv_from_cstr("GCOV_COMMAND")) ||
           sv_eq_ci(tok, sv_from_cstr("GCOV_OPTIONS")) ||
           sv_eq_ci(tok, sv_from_cstr("TARBALL_COMPRESSION")) ||
           sv_eq_ci(tok, sv_from_cstr("QUIET")) ||
           sv_eq_ci(tok, sv_from_cstr("GLOB")) ||
           sv_eq_ci(tok, sv_from_cstr("DELETE"));
}

static bool ctest_coverage_is_artifact(String_View name) {
    String_View ext = filename_ext_sv(name);
    return sv_eq_ci(ext, sv_from_cstr(".gcda")) ||
           sv_eq_ci(ext, sv_from_cstr(".gcno")) ||
           sv_eq_ci(ext, sv_from_cstr(".gcov"));
}

static void ctest_coverage_collect_recursive(Evaluator_Context *ctx, String_View dir, String_List *files) {
    if (!ctx || dir.count == 0 || !files) return;
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(nob_temp_sv_to_cstr(dir), &children)) return;
    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (nob_sv_eq(name, sv_from_cstr(".")) || nob_sv_eq(name, sv_from_cstr(".."))) continue;
        String_View child = path_join_arena(ctx->arena, dir, name);
        Nob_File_Type t = nob_get_file_type(nob_temp_sv_to_cstr(child));
        if (t == NOB_FILE_DIRECTORY) {
            ctest_coverage_collect_recursive(ctx, child, files);
            continue;
        }
        if (t == NOB_FILE_REGULAR && ctest_coverage_is_artifact(name)) {
            string_list_add(files, ctx->arena, child);
        }
    }
    nob_da_free(children);
}

// Avalia o comando 'ctest_coverage_collect_gcov'
static void eval_ctest_coverage_collect_gcov_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;

    String_View tarball = sv_from_cstr("");
    String_View source_dir = sv_from_cstr("");
    String_View build_dir = sv_from_cstr("");
    String_View gcov_command = sv_from_cstr("");
    String_View tarball_compression = sv_from_cstr("FROM_EXT");
    bool quiet = false;
    bool delete_after = false;

    String_List gcov_options = {0};
    String_List files = {0};
    string_list_init(&gcov_options);
    string_list_init(&files);

    for (size_t i = 0; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("TARBALL")) && i + 1 < args.count) {
            tarball = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("SOURCE")) && i + 1 < args.count) {
            source_dir = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("BUILD")) && i + 1 < args.count) {
            build_dir = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("GCOV_COMMAND")) && i + 1 < args.count) {
            gcov_command = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("GCOV_OPTIONS"))) {
            for (size_t j = i + 1; j < args.count; j++) {
                String_View opt = resolve_arg(ctx, args.items[j]);
                if (ctest_coverage_collect_keyword(opt)) {
                    i = j - 1;
                    break;
                }
                string_list_add(&gcov_options, ctx->arena, opt);
                i = j;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("TARBALL_COMPRESSION")) && i + 1 < args.count) {
            tarball_compression = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("QUIET"))) {
            quiet = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DELETE"))) {
            delete_after = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("GLOB"))) {
            continue;
        }
    }

    if (tarball.count == 0) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "ctest_coverage_collect_gcov",
            "parametro TARBALL e obrigatorio",
            "use ctest_coverage_collect_gcov(TARBALL <arquivo.tar> [SOURCE ...] [BUILD ...])");
        eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE"), sv_from_cstr("1"), false, false);
        return;
    }

    if (build_dir.count == 0) build_dir = eval_get_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"));
    if (build_dir.count == 0) build_dir = ctx->current_binary_dir;
    if (source_dir.count == 0) source_dir = eval_get_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"));
    if (source_dir.count == 0) source_dir = ctx->current_source_dir;
    if (gcov_command.count == 0) gcov_command = sv_from_cstr("gcov");

    if (!path_is_absolute_sv(build_dir)) build_dir = path_join_arena(ctx->arena, ctx->current_binary_dir, build_dir);
    if (!path_is_absolute_sv(source_dir)) source_dir = path_join_arena(ctx->arena, ctx->current_source_dir, source_dir);

    ctest_coverage_collect_recursive(ctx, build_dir, &files);

    String_View coverage_dir = path_join_arena(ctx->arena, build_dir, sv_from_cstr("Testing/CoverageInfo"));
    String_View data_json_path = path_join_arena(ctx->arena, coverage_dir, sv_from_cstr("data.json"));
    String_View labels_json_path = path_join_arena(ctx->arena, coverage_dir, sv_from_cstr("Labels.json"));
    String_View coverage_xml_path = path_join_arena(ctx->arena, coverage_dir, sv_from_cstr("Coverage.xml"));

    if (!ensure_parent_dirs_for_path(ctx->arena, path_join_arena(ctx->arena, coverage_dir, sv_from_cstr(".keep"))) ||
        !nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(coverage_dir))) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "ctest_coverage_collect_gcov",
            nob_temp_sprintf("falha ao preparar diretorio de cobertura: "SV_Fmt, SV_Arg(coverage_dir)),
            "verifique permissao de escrita no diretorio de build");
        eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE"), sv_from_cstr("1"), false, false);
        return;
    }

    String_Builder data_json = {0};
    sb_append_cstr(&data_json, "{\n");
    sb_append_cstr(&data_json, "  \"format\": \"cmk2nob-cdash-gcov-v1\",\n");
    sb_append_cstr(&data_json, "  \"source\": \"");
    json_append_escaped(&data_json, source_dir);
    sb_append_cstr(&data_json, "\",\n  \"build\": \"");
    json_append_escaped(&data_json, build_dir);
    sb_append_cstr(&data_json, "\",\n  \"gcov_command\": \"");
    json_append_escaped(&data_json, gcov_command);
    sb_append_cstr(&data_json, "\",\n  \"gcov_options\": [");
    for (size_t i = 0; i < gcov_options.count; i++) {
        if (i > 0) sb_append_cstr(&data_json, ", ");
        sb_append(&data_json, '"');
        json_append_escaped(&data_json, gcov_options.items[i]);
        sb_append(&data_json, '"');
    }
    sb_append_cstr(&data_json, "],\n  \"files\": [");
    for (size_t i = 0; i < files.count; i++) {
        if (i > 0) sb_append_cstr(&data_json, ", ");
        sb_append(&data_json, '"');
        json_append_escaped(&data_json, files.items[i]);
        sb_append(&data_json, '"');
    }
    sb_append_cstr(&data_json, "]\n}\n");
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(data_json_path), data_json.items ? data_json.items : "", data_json.count);
    nob_sb_free(data_json);

    (void)nob_write_entire_file(nob_temp_sv_to_cstr(labels_json_path), "{}\n", 3);

    String_Builder coverage_xml = {0};
    sb_append_cstr(&coverage_xml, "<Site BuildName=\"cmk2nob\" Name=\"cmk2nob\">\n");
    sb_append_cstr(&coverage_xml, "  <Coverage>\n");
    sb_append_cstr(&coverage_xml, "    <CoverageLog>\n");
    for (size_t i = 0; i < files.count; i++) {
        sb_append_cstr(&coverage_xml, "      <File>");
        json_append_escaped(&coverage_xml, files.items[i]);
        sb_append_cstr(&coverage_xml, "</File>\n");
    }
    sb_append_cstr(&coverage_xml, "    </CoverageLog>\n");
    sb_append_cstr(&coverage_xml, "  </Coverage>\n");
    sb_append_cstr(&coverage_xml, "</Site>\n");
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(coverage_xml_path), coverage_xml.items ? coverage_xml.items : "", coverage_xml.count);
    nob_sb_free(coverage_xml);

    String_View tarball_path = path_is_absolute_sv(tarball) ? tarball : path_join_arena(ctx->arena, ctx->current_binary_dir, tarball);
    if (!ensure_parent_dirs_for_path(ctx->arena, tarball_path)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "ctest_coverage_collect_gcov",
            nob_temp_sprintf("falha ao preparar TARBALL: "SV_Fmt, SV_Arg(tarball_path)),
            "verifique permissao de escrita e caminho de destino");
        eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE"), sv_from_cstr("1"), false, false);
        return;
    }

    String_Builder tar_manifest = {0};
    sb_append_cstr(&tar_manifest, "# cmk2nob-cdash-gcov-bundle-v1\n");
    sb_append_cstr(&tar_manifest, "source=");
    sb_append_buf(&tar_manifest, source_dir.data, source_dir.count);
    sb_append_cstr(&tar_manifest, "\nbuild=");
    sb_append_buf(&tar_manifest, build_dir.data, build_dir.count);
    sb_append_cstr(&tar_manifest, "\ncompression=");
    sb_append_buf(&tar_manifest, tarball_compression.data, tarball_compression.count);
    sb_append_cstr(&tar_manifest, "\ngcov_command=");
    sb_append_buf(&tar_manifest, gcov_command.data, gcov_command.count);
    sb_append_cstr(&tar_manifest, "\nmetadata=data.json;Labels.json;Coverage.xml\n");
    for (size_t i = 0; i < files.count; i++) {
        sb_append_cstr(&tar_manifest, "file=");
        sb_append_buf(&tar_manifest, files.items[i].data, files.items[i].count);
        sb_append(&tar_manifest, '\n');
    }
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(tarball_path), tar_manifest.items ? tar_manifest.items : "", tar_manifest.count);
    nob_sb_free(tar_manifest);

    if (delete_after) {
        for (size_t i = 0; i < files.count; i++) {
            (void)nob_delete_file(nob_temp_sv_to_cstr(files.items[i]));
        }
    }

    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV"), sv_from_cstr("ON"), false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_TARBALL"), tarball_path, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_BUILD"), build_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_SOURCE"), source_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_FILE_COUNT"),
                 sv_from_cstr(nob_temp_sprintf("%zu", files.count)), false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_DATA_JSON"), data_json_path, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_LABELS_JSON"), labels_json_path, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_COVERAGE_XML"), coverage_xml_path, false, false);
    eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COLLECT_GCOV_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (!quiet) {
        nob_log(NOB_INFO, "ctest_coverage_collect_gcov: %zu arquivos de cobertura empacotados", files.count);
    }
}

static void eval_ctest_memcheck_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");
    eval_set_var(ctx, sv_from_cstr("CTEST_MEMCHECK_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
}

static void eval_ctest_submit_command(Evaluator_Context *ctx, Args args) {
    if (!ctx) return;
    String_View ret_var = ctest_option_value(ctx, args, "RETURN_VALUE");
    eval_set_var(ctx, sv_from_cstr("CTEST_SUBMIT_RETURN_VALUE"), sv_from_cstr("0"), false, false);
    if (ret_var.count > 0) eval_set_var(ctx, ret_var, sv_from_cstr("0"), false, false);
}

static void eval_ctest_upload_command(Evaluator_Context *ctx, Args args) {
    (void)args;
    if (!ctx) return;
    eval_set_var(ctx, sv_from_cstr("CTEST_UPLOAD_RETURN_VALUE"), sv_from_cstr("0"), false, false);
}

static void eval_ctest_run_script_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    bool ok = true;
    String_View return_var = sv_from_cstr("");

    for (size_t i = 0; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("NEW_PROCESS"))) continue;
        if (sv_eq_ci(tok, sv_from_cstr("RETURN_VALUE")) && i + 1 < args.count) {
            return_var = resolve_arg(ctx, args.items[++i]);
            continue;
        }

        String_View script = path_is_absolute_sv(tok) ? tok : path_join_arena(ctx->arena, ctx->current_list_dir, tok);
        if (!nob_file_exists(nob_temp_sv_to_cstr(script))) {
            ok = false;
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "ctest_run_script",
                nob_temp_sprintf("script nao encontrado: "SV_Fmt, SV_Arg(script)),
                "forneca caminho valido para script CTest (-S)");
            continue;
        }

        String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(script));
        if (!content.data) {
            ok = false;
            continue;
        }
        Token_List tokens = {0};
        if (!arena_tokenize(ctx->arena, content, &tokens)) {
            ok = false;
            continue;
        }
        Ast_Root root = parse_tokens(ctx->arena, tokens);
        String_View old_list = ctx->current_list_dir;
        ctx->current_list_dir = path_parent_dir_arena(ctx->arena, script);
        for (size_t n = 0; n < root.count; n++) eval_node(ctx, root.items[n]);
        ctx->current_list_dir = old_list;
    }

    eval_set_var(ctx, sv_from_cstr("CTEST_RUN_SCRIPT_RETURN_VALUE"), ok ? sv_from_cstr("0") : sv_from_cstr("1"), false, false);
    if (return_var.count > 0) {
        eval_set_var(ctx, return_var, ok ? sv_from_cstr("0") : sv_from_cstr("1"), false, false);
    }
}

static void eval_ctest_read_custom_files_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    String_View dir = resolve_arg(ctx, args.items[0]);
    if (dir.count == 0) return;
    String_View resolved = path_is_absolute_sv(dir) ? dir : path_join_arena(ctx->arena, ctx->current_list_dir, dir);
    eval_set_var(ctx, sv_from_cstr("CTEST_CUSTOM_FILES_DIRECTORY"), resolved, false, false);
}

static void eval_ctest_sleep_command(Evaluator_Context *ctx, Args args) {
    (void)ctx;
    (void)args;
}

static void eval_ctest_empty_binary_directory_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    String_View dir = resolve_arg(ctx, args.items[0]);
    if (dir.count == 0) return;
    String_View resolved = path_is_absolute_sv(dir) ? dir : path_join_arena(ctx->arena, ctx->current_binary_dir, dir);
    (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(resolved));
    eval_set_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"), resolved, false, false);
}


// ============================================================================
// PACKAGING (CPack)
// ============================================================================

static bool cpack_is_archive_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("7Z")) ||
           sv_eq_ci(gen, sv_from_cstr("TBZ2")) ||
           sv_eq_ci(gen, sv_from_cstr("TGZ")) ||
           sv_eq_ci(gen, sv_from_cstr("TXZ")) ||
           sv_eq_ci(gen, sv_from_cstr("TZ")) ||
           sv_eq_ci(gen, sv_from_cstr("TZST")) ||
           sv_eq_ci(gen, sv_from_cstr("ZIP"));
}

static bool cpack_is_deb_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("DEB"));
}

static bool cpack_is_rpm_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("RPM"));
}

static bool cpack_is_nsis_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("NSIS"));
}

static bool cpack_is_wix_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("WIX"));
}

static bool cpack_is_dmg_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("DMG")) ||
           sv_eq_ci(gen, sv_from_cstr("DragNDrop"));
}

static bool cpack_is_bundle_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("BUNDLE")) ||
           sv_eq_ci(gen, sv_from_cstr("Bundle"));
}

static bool cpack_is_productbuild_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("PRODUCTBUILD")) ||
           sv_eq_ci(gen, sv_from_cstr("productbuild"));
}

static bool cpack_is_ifw_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("IFW"));
}

static bool cpack_is_nuget_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("NuGet"));
}

static bool cpack_is_freebsd_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("FreeBSD"));
}

static bool cpack_is_cygwin_generator(String_View gen) {
    return sv_eq_ci(gen, sv_from_cstr("Cygwin"));
}

static String_View cpack_archive_extension_for_generator(String_View gen) {
    if (sv_eq_ci(gen, sv_from_cstr("7Z"))) return sv_from_cstr(".7z");
    if (sv_eq_ci(gen, sv_from_cstr("TBZ2"))) return sv_from_cstr(".tar.bz2");
    if (sv_eq_ci(gen, sv_from_cstr("TGZ"))) return sv_from_cstr(".tar.gz");
    if (sv_eq_ci(gen, sv_from_cstr("TXZ"))) return sv_from_cstr(".tar.xz");
    if (sv_eq_ci(gen, sv_from_cstr("TZ"))) return sv_from_cstr(".tar.Z");
    if (sv_eq_ci(gen, sv_from_cstr("TZST"))) return sv_from_cstr(".tar.zst");
    if (sv_eq_ci(gen, sv_from_cstr("ZIP"))) return sv_from_cstr(".zip");
    return sv_from_cstr(".tar.gz");
}

static String_View cpack_join_with_comma_space(Evaluator_Context *ctx, String_List *items) {
    if (!ctx || !items || items->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < items->count; i++) {
        if (i > 0) sb_append_cstr(&sb, ", ");
        sb_append_buf(&sb, items->items[i].data, items->items[i].count);
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View cpack_normalize_linux_pkg_name(Evaluator_Context *ctx, String_View raw) {
    if (!ctx || raw.count == 0) return sv_from_cstr("package");
    char *buf = arena_strndup(ctx->arena, raw.data, raw.count);
    for (size_t i = 0; i < raw.count; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (isalnum(c) || c == '+' || c == '.' || c == '-') {
            buf[i] = (char)tolower(c);
        } else {
            buf[i] = '-';
        }
    }
    return sv_from_cstr(buf);
}

static String_View cpack_normalize_windows_pkg_name(Evaluator_Context *ctx, String_View raw) {
    if (!ctx || raw.count == 0) return sv_from_cstr("Package");
    char *buf = arena_strndup(ctx->arena, raw.data, raw.count);
    for (size_t i = 0; i < raw.count; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            continue;
        }
        if (isspace(c)) {
            buf[i] = '-';
        } else {
            buf[i] = '_';
        }
    }
    return sv_from_cstr(buf);
}

static String_View cpack_normalize_apple_pkg_name(Evaluator_Context *ctx, String_View raw) {
    if (!ctx || raw.count == 0) return sv_from_cstr("Package");
    char *buf = arena_strndup(ctx->arena, raw.data, raw.count);
    for (size_t i = 0; i < raw.count; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            continue;
        }
        if (isspace(c)) {
            buf[i] = '-';
        } else {
            buf[i] = '_';
        }
    }
    return sv_from_cstr(buf);
}

static void cpack_append_generator(Evaluator_Context *ctx, String_View generator) {
    if (!ctx || generator.count == 0) return;
    String_List gens = {0};
    string_list_init(&gens);
    List_Add_Ud ud = { .list = &gens, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CPACK_GENERATOR")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud);
    if (string_list_add_unique(&gens, ctx->arena, generator)) {
        eval_set_var(ctx, sv_from_cstr("CPACK_GENERATOR"), join_string_list_with_semicolon(ctx, &gens), false, false);
    }
}

static void cpack_split_version(Evaluator_Context *ctx, String_View version,
                                String_View *out_major, String_View *out_minor, String_View *out_patch) {
    if (!ctx) return;
    String_View major = sv_from_cstr("0");
    String_View minor = sv_from_cstr("0");
    String_View patch = sv_from_cstr("0");
    if (version.count > 0) {
        size_t p1 = version.count;
        size_t p2 = version.count;
        for (size_t i = 0; i < version.count; i++) {
            if (version.data[i] == '.') {
                p1 = i;
                break;
            }
        }
        if (p1 < version.count) {
            for (size_t i = p1 + 1; i < version.count; i++) {
                if (version.data[i] == '.') {
                    p2 = i;
                    break;
                }
            }
        }
        if (p1 == version.count) {
            major = sv_from_cstr(arena_strndup(ctx->arena, version.data, version.count));
        } else {
            major = sv_from_cstr(arena_strndup(ctx->arena, version.data, p1));
            if (p2 == version.count) {
                minor = sv_from_cstr(arena_strndup(ctx->arena, version.data + p1 + 1, version.count - (p1 + 1)));
            } else {
                minor = sv_from_cstr(arena_strndup(ctx->arena, version.data + p1 + 1, p2 - (p1 + 1)));
                patch = sv_from_cstr(arena_strndup(ctx->arena, version.data + p2 + 1, version.count - (p2 + 1)));
            }
        }
        if (major.count == 0) major = sv_from_cstr("0");
        if (minor.count == 0) minor = sv_from_cstr("0");
        if (patch.count == 0) patch = sv_from_cstr("0");
    }
    if (out_major) *out_major = major;
    if (out_minor) *out_minor = minor;
    if (out_patch) *out_patch = patch;
}

static void cpack_sync_common_metadata(Evaluator_Context *ctx) {
    if (!ctx || !ctx->model) return;

    String_View package_name = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_NAME"));
    if (package_name.count == 0) package_name = eval_get_var(ctx, sv_from_cstr("PROJECT_NAME"));
    if (package_name.count == 0) package_name = eval_get_var(ctx, sv_from_cstr("CMAKE_PROJECT_NAME"));
    if (package_name.count == 0 && ctx->model->project_name.count > 0) package_name = ctx->model->project_name;
    if (package_name.count == 0) package_name = sv_from_cstr("Package");

    String_View package_version = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION"));
    if (package_version.count == 0) {
        String_View major = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_MAJOR"));
        String_View minor = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_MINOR"));
        String_View patch = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_PATCH"));
        if (major.count > 0 || minor.count > 0 || patch.count > 0) {
            if (major.count == 0) major = sv_from_cstr("0");
            if (minor.count == 0) minor = sv_from_cstr("0");
            if (patch.count == 0) patch = sv_from_cstr("0");
            package_version = sv_from_cstr(nob_temp_sprintf(SV_Fmt"."SV_Fmt"."SV_Fmt,
                SV_Arg(major), SV_Arg(minor), SV_Arg(patch)));
        }
    }
    if (package_version.count == 0) package_version = eval_get_var(ctx, sv_from_cstr("PROJECT_VERSION"));
    if (package_version.count == 0) package_version = eval_get_var(ctx, sv_from_cstr("CMAKE_PROJECT_VERSION"));
    if (package_version.count == 0 && ctx->model->project_version.count > 0) package_version = ctx->model->project_version;
    if (package_version.count == 0) package_version = sv_from_cstr("0.1.0");

    String_View major = sv_from_cstr("0");
    String_View minor = sv_from_cstr("0");
    String_View patch = sv_from_cstr("0");
    cpack_split_version(ctx, package_version, &major, &minor, &patch);

    String_List components = {0};
    string_list_init(&components);
    List_Add_Ud ud_components = { .list = &components, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CPACK_COMPONENTS_ALL")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_components);
    for (size_t i = 0; i < ctx->model->cpack_component_count; i++) {
        string_list_add_unique(&components, ctx->arena, ctx->model->cpack_components[i].name);
    }
    String_View components_norm = join_string_list_with_semicolon(ctx, &components);

    String_List depends = {0};
    string_list_init(&depends);
    List_Add_Ud ud_depends = { .list = &depends, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_DEPENDS")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_depends);
    for (size_t i = 0; i < ctx->model->cpack_component_count; i++) {
        CPack_Component *c = &ctx->model->cpack_components[i];
        for (size_t d = 0; d < c->depends.count; d++) {
            string_list_add_unique(&depends, ctx->arena, c->depends.items[d]);
        }
    }
    String_View depends_norm = join_string_list_with_semicolon(ctx, &depends);

    String_List generators = {0};
    String_List archive_generators = {0};
    String_List deb_generators = {0};
    String_List rpm_generators = {0};
    String_List nsis_generators = {0};
    String_List wix_generators = {0};
    String_List dmg_generators = {0};
    String_List bundle_generators = {0};
    String_List productbuild_generators = {0};
    String_List ifw_generators = {0};
    String_List nuget_generators = {0};
    String_List freebsd_generators = {0};
    String_List cygwin_generators = {0};
    string_list_init(&generators);
    string_list_init(&archive_generators);
    string_list_init(&deb_generators);
    string_list_init(&rpm_generators);
    string_list_init(&nsis_generators);
    string_list_init(&wix_generators);
    string_list_init(&dmg_generators);
    string_list_init(&bundle_generators);
    string_list_init(&productbuild_generators);
    string_list_init(&ifw_generators);
    string_list_init(&nuget_generators);
    string_list_init(&freebsd_generators);
    string_list_init(&cygwin_generators);
    List_Add_Ud ud_generators = { .list = &generators, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                eval_get_var(ctx, sv_from_cstr("CPACK_GENERATOR")),
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_generators);
    bool archive_enabled = false;
    bool deb_enabled = false;
    bool rpm_enabled = false;
    bool nsis_enabled = false;
    bool wix_enabled = false;
    bool dmg_enabled = false;
    bool bundle_enabled = false;
    bool productbuild_enabled = false;
    bool ifw_enabled = false;
    bool nuget_enabled = false;
    bool freebsd_enabled = false;
    bool cygwin_enabled = false;
    for (size_t i = 0; i < generators.count; i++) {
        String_View g = generators.items[i];
        if (cpack_is_archive_generator(g)) {
            archive_enabled = true;
            string_list_add_unique(&archive_generators, ctx->arena, g);
        }
        if (cpack_is_deb_generator(g)) {
            deb_enabled = true;
            string_list_add_unique(&deb_generators, ctx->arena, g);
        }
        if (cpack_is_rpm_generator(g)) {
            rpm_enabled = true;
            string_list_add_unique(&rpm_generators, ctx->arena, g);
        }
        if (cpack_is_nsis_generator(g)) {
            nsis_enabled = true;
            string_list_add_unique(&nsis_generators, ctx->arena, g);
        }
        if (cpack_is_wix_generator(g)) {
            wix_enabled = true;
            string_list_add_unique(&wix_generators, ctx->arena, g);
        }
        if (cpack_is_dmg_generator(g)) {
            dmg_enabled = true;
            string_list_add_unique(&dmg_generators, ctx->arena, g);
        }
        if (cpack_is_bundle_generator(g)) {
            bundle_enabled = true;
            string_list_add_unique(&bundle_generators, ctx->arena, g);
        }
        if (cpack_is_productbuild_generator(g)) {
            productbuild_enabled = true;
            string_list_add_unique(&productbuild_generators, ctx->arena, g);
        }
        if (cpack_is_ifw_generator(g)) {
            ifw_enabled = true;
            string_list_add_unique(&ifw_generators, ctx->arena, g);
        }
        if (cpack_is_nuget_generator(g)) {
            nuget_enabled = true;
            string_list_add_unique(&nuget_generators, ctx->arena, g);
        }
        if (cpack_is_freebsd_generator(g)) {
            freebsd_enabled = true;
            string_list_add_unique(&freebsd_generators, ctx->arena, g);
        }
        if (cpack_is_cygwin_generator(g)) {
            cygwin_enabled = true;
            string_list_add_unique(&cygwin_generators, ctx->arena, g);
        }
    }
    String_View archive_ext = archive_generators.count > 0
        ? cpack_archive_extension_for_generator(archive_generators.items[0])
        : sv_from_cstr("");
    String_View archive_generators_norm = join_string_list_with_semicolon(ctx, &archive_generators);
    String_View deb_generators_norm = join_string_list_with_semicolon(ctx, &deb_generators);
    String_View rpm_generators_norm = join_string_list_with_semicolon(ctx, &rpm_generators);
    String_View nsis_generators_norm = join_string_list_with_semicolon(ctx, &nsis_generators);
    String_View wix_generators_norm = join_string_list_with_semicolon(ctx, &wix_generators);
    String_View dmg_generators_norm = join_string_list_with_semicolon(ctx, &dmg_generators);
    String_View bundle_generators_norm = join_string_list_with_semicolon(ctx, &bundle_generators);
    String_View productbuild_generators_norm = join_string_list_with_semicolon(ctx, &productbuild_generators);
    String_View ifw_generators_norm = join_string_list_with_semicolon(ctx, &ifw_generators);
    String_View nuget_generators_norm = join_string_list_with_semicolon(ctx, &nuget_generators);
    String_View freebsd_generators_norm = join_string_list_with_semicolon(ctx, &freebsd_generators);
    String_View cygwin_generators_norm = join_string_list_with_semicolon(ctx, &cygwin_generators);

    String_View package_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_FILE_NAME"));
    if (package_file_name.count == 0) {
        package_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt, SV_Arg(package_name), SV_Arg(package_version)));
    }

    String_View linux_pkg_name = cpack_normalize_linux_pkg_name(ctx, package_name);
    String_View windows_pkg_name = cpack_normalize_windows_pkg_name(ctx, package_name);
    String_View apple_pkg_name = cpack_normalize_apple_pkg_name(ctx, package_name);
    String_View deb_version = package_version;
    String_View deb_release = eval_get_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_RELEASE"));
    if (deb_release.count == 0) deb_release = sv_from_cstr("1");
    String_View deb_arch = eval_get_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_ARCHITECTURE"));
    if (deb_arch.count == 0) deb_arch = ctx->model->is_linux ? sv_from_cstr("amd64") : sv_from_cstr("amd64");
    String_View deb_depends = eval_get_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_DEPENDS"));
    if (deb_depends.count == 0) {
        String_List deps = {0};
        string_list_init(&deps);
        List_Add_Ud ud_deps = { .list = &deps, .unique = false };
        eval_foreach_semicolon_item(ctx, depends_norm, /*trim_ws=*/true, eval_list_add_item, &ud_deps);
        deb_depends = cpack_join_with_comma_space(ctx, &deps);
    }
    String_View deb_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_DEBIAN_FILE_NAME"));
    if (deb_file_name.count == 0) {
        deb_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"_"SV_Fmt"-"SV_Fmt"_"SV_Fmt".deb",
            SV_Arg(linux_pkg_name), SV_Arg(deb_version), SV_Arg(deb_release), SV_Arg(deb_arch)));
    }

    String_View rpm_version = package_version;
    String_View rpm_release = eval_get_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_RELEASE"));
    if (rpm_release.count == 0) rpm_release = sv_from_cstr("1");
    String_View rpm_arch = eval_get_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_ARCHITECTURE"));
    if (rpm_arch.count == 0) rpm_arch = ctx->model->is_linux ? sv_from_cstr("x86_64") : sv_from_cstr("x86_64");
    String_View rpm_license = eval_get_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_LICENSE"));
    if (rpm_license.count == 0) rpm_license = sv_from_cstr("unknown");
    String_View rpm_requires = eval_get_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_REQUIRES"));
    if (rpm_requires.count == 0) {
        String_List deps = {0};
        string_list_init(&deps);
        List_Add_Ud ud_deps = { .list = &deps, .unique = false };
        eval_foreach_semicolon_item(ctx, depends_norm, /*trim_ws=*/true, eval_list_add_item, &ud_deps);
        rpm_requires = cpack_join_with_comma_space(ctx, &deps);
    }
    String_View rpm_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_RPM_FILE_NAME"));
    if (rpm_file_name.count == 0) {
        rpm_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt"-"SV_Fmt"."SV_Fmt".rpm",
            SV_Arg(linux_pkg_name), SV_Arg(rpm_version), SV_Arg(rpm_release), SV_Arg(rpm_arch)));
    }

    String_View nsis_display_name = eval_get_var(ctx, sv_from_cstr("CPACK_NSIS_DISPLAY_NAME"));
    if (nsis_display_name.count == 0) {
        nsis_display_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt" "SV_Fmt, SV_Arg(package_name), SV_Arg(package_version)));
    }
    String_View nsis_install_dir = eval_get_var(ctx, sv_from_cstr("CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY"));
    if (nsis_install_dir.count == 0) nsis_install_dir = windows_pkg_name;
    String_View nsis_contact = eval_get_var(ctx, sv_from_cstr("CPACK_PACKAGE_CONTACT"));
    if (nsis_contact.count == 0) nsis_contact = sv_from_cstr("unknown");
    String_View nsis_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_NSIS_FILE_NAME"));
    if (nsis_file_name.count == 0) {
        nsis_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt"-setup.exe",
            SV_Arg(windows_pkg_name), SV_Arg(package_version)));
    }

    String_View wix_product_name = eval_get_var(ctx, sv_from_cstr("CPACK_WIX_PRODUCT_NAME"));
    if (wix_product_name.count == 0) wix_product_name = package_name;
    String_View wix_arch = eval_get_var(ctx, sv_from_cstr("CPACK_WIX_ARCHITECTURE"));
    if (wix_arch.count == 0) wix_arch = sv_from_cstr("x64");
    String_View wix_cultures = eval_get_var(ctx, sv_from_cstr("CPACK_WIX_CULTURES"));
    if (wix_cultures.count == 0) wix_cultures = sv_from_cstr("en-us");
    String_View wix_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_WIX_FILE_NAME"));
    if (wix_file_name.count == 0) {
        wix_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".msi",
            SV_Arg(windows_pkg_name), SV_Arg(package_version)));
    }

    String_View dmg_volume_name = eval_get_var(ctx, sv_from_cstr("CPACK_DMG_VOLUME_NAME"));
    if (dmg_volume_name.count == 0) dmg_volume_name = package_name;
    String_View dmg_format = eval_get_var(ctx, sv_from_cstr("CPACK_DMG_FORMAT"));
    if (dmg_format.count == 0) dmg_format = sv_from_cstr("UDZO");
    String_View dmg_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_DMG_FILE_NAME"));
    if (dmg_file_name.count == 0) {
        dmg_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".dmg",
            SV_Arg(apple_pkg_name), SV_Arg(package_version)));
    }

    String_View bundle_name = eval_get_var(ctx, sv_from_cstr("CPACK_BUNDLE_NAME"));
    if (bundle_name.count == 0) bundle_name = package_name;
    String_View bundle_plist = eval_get_var(ctx, sv_from_cstr("CPACK_BUNDLE_PLIST"));
    String_View bundle_icon = eval_get_var(ctx, sv_from_cstr("CPACK_BUNDLE_ICON"));
    String_View bundle_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_BUNDLE_FILE_NAME"));
    if (bundle_file_name.count == 0) {
        bundle_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt".app", SV_Arg(apple_pkg_name)));
    }

    String_View productbuild_identifier = eval_get_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_IDENTIFIER"));
    if (productbuild_identifier.count == 0) {
        productbuild_identifier = sv_from_cstr(nob_temp_sprintf("com.cmk2nob.%s", nob_temp_sv_to_cstr(linux_pkg_name)));
    }
    String_View productbuild_signing_identity = eval_get_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_IDENTITY_NAME"));
    String_View productbuild_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_FILE_NAME"));
    if (productbuild_file_name.count == 0) {
        productbuild_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".pkg",
            SV_Arg(apple_pkg_name), SV_Arg(package_version)));
    }

    String_View ifw_package_name = eval_get_var(ctx, sv_from_cstr("CPACK_IFW_PACKAGE_NAME"));
    if (ifw_package_name.count == 0) ifw_package_name = package_name;
    String_View ifw_package_title = eval_get_var(ctx, sv_from_cstr("CPACK_IFW_PACKAGE_TITLE"));
    if (ifw_package_title.count == 0) ifw_package_title = package_name;
    String_View ifw_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_IFW_FILE_NAME"));
    if (ifw_file_name.count == 0) {
        ifw_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".ifw", SV_Arg(apple_pkg_name), SV_Arg(package_version)));
    }

    String_View nuget_id = eval_get_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_ID"));
    if (nuget_id.count == 0) nuget_id = windows_pkg_name;
    String_View nuget_version = eval_get_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_VERSION"));
    if (nuget_version.count == 0) nuget_version = package_version;
    String_View nuget_authors = eval_get_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_AUTHORS"));
    if (nuget_authors.count == 0) nuget_authors = sv_from_cstr("unknown");
    String_View nuget_description = eval_get_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_DESCRIPTION"));
    if (nuget_description.count == 0) nuget_description = package_name;
    String_View nuget_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_NUGET_FILE_NAME"));
    if (nuget_file_name.count == 0) {
        nuget_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"."SV_Fmt".nupkg", SV_Arg(nuget_id), SV_Arg(nuget_version)));
    }

    String_View freebsd_name = eval_get_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_NAME"));
    if (freebsd_name.count == 0) freebsd_name = linux_pkg_name;
    String_View freebsd_version = eval_get_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_VERSION"));
    if (freebsd_version.count == 0) freebsd_version = package_version;
    String_View freebsd_origin = eval_get_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_ORIGIN"));
    if (freebsd_origin.count == 0) freebsd_origin = sv_from_cstr(nob_temp_sprintf("devel/%s", nob_temp_sv_to_cstr(linux_pkg_name)));
    String_View freebsd_deps = eval_get_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_DEPENDS"));
    if (freebsd_deps.count == 0) freebsd_deps = cpack_join_with_comma_space(ctx, &depends);
    String_View freebsd_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_FREEBSD_FILE_NAME"));
    if (freebsd_file_name.count == 0) {
        freebsd_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".pkg.txz", SV_Arg(freebsd_name), SV_Arg(freebsd_version)));
    }

    String_View cygwin_name = eval_get_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_NAME"));
    if (cygwin_name.count == 0) cygwin_name = windows_pkg_name;
    String_View cygwin_version = eval_get_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_VERSION"));
    if (cygwin_version.count == 0) cygwin_version = package_version;
    String_View cygwin_deps = eval_get_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_DEPENDS"));
    if (cygwin_deps.count == 0) cygwin_deps = cpack_join_with_comma_space(ctx, &depends);
    String_View cygwin_file_name = eval_get_var(ctx, sv_from_cstr("CPACK_CYGWIN_FILE_NAME"));
    if (cygwin_file_name.count == 0) {
        cygwin_file_name = sv_from_cstr(nob_temp_sprintf(SV_Fmt"-"SV_Fmt".tar.xz", SV_Arg(cygwin_name), SV_Arg(cygwin_version)));
    }

    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_NAME"), package_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION"), package_version, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_MAJOR"), major, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_MINOR"), minor, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_VERSION_PATCH"), patch, false, false);
    if (components_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_COMPONENTS_ALL"), components_norm, false, false);
    if (depends_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_DEPENDS"), depends_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PACKAGE_FILE_NAME"), package_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_ARCHIVE_ENABLED"), archive_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (archive_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_ARCHIVE_GENERATORS"), archive_generators_norm, false, false);
    if (archive_ext.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_ARCHIVE_FILE_EXTENSION"), archive_ext, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEB_ENABLED"), deb_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (deb_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_DEB_GENERATORS"), deb_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_NAME"), linux_pkg_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_VERSION"), deb_version, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_RELEASE"), deb_release, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_ARCHITECTURE"), deb_arch, false, false);
    if (deb_depends.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_PACKAGE_DEPENDS"), deb_depends, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DEBIAN_FILE_NAME"), deb_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_ENABLED"), rpm_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (rpm_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_RPM_GENERATORS"), rpm_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_NAME"), linux_pkg_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_VERSION"), rpm_version, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_RELEASE"), rpm_release, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_ARCHITECTURE"), rpm_arch, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_LICENSE"), rpm_license, false, false);
    if (rpm_requires.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_RPM_PACKAGE_REQUIRES"), rpm_requires, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_RPM_FILE_NAME"), rpm_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_ENABLED"), nsis_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (nsis_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_GENERATORS"), nsis_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_DISPLAY_NAME"), nsis_display_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY"), nsis_install_dir, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_CONTACT"), nsis_contact, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NSIS_FILE_NAME"), nsis_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_WIX_ENABLED"), wix_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (wix_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_WIX_GENERATORS"), wix_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_WIX_PRODUCT_NAME"), wix_product_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_WIX_ARCHITECTURE"), wix_arch, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_WIX_CULTURES"), wix_cultures, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_WIX_FILE_NAME"), wix_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DMG_ENABLED"), dmg_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (dmg_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_DMG_GENERATORS"), dmg_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DMG_VOLUME_NAME"), dmg_volume_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DMG_FORMAT"), dmg_format, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_DMG_FILE_NAME"), dmg_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_ENABLED"), bundle_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (bundle_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_GENERATORS"), bundle_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_NAME"), bundle_name, false, false);
    if (bundle_plist.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_PLIST"), bundle_plist, false, false);
    if (bundle_icon.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_ICON"), bundle_icon, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_BUNDLE_FILE_NAME"), bundle_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_ENABLED"), productbuild_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (productbuild_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_GENERATORS"), productbuild_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_IDENTIFIER"), productbuild_identifier, false, false);
    if (productbuild_signing_identity.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_IDENTITY_NAME"), productbuild_signing_identity, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_PRODUCTBUILD_FILE_NAME"), productbuild_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_IFW_ENABLED"), ifw_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (ifw_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_IFW_GENERATORS"), ifw_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_IFW_PACKAGE_NAME"), ifw_package_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_IFW_PACKAGE_TITLE"), ifw_package_title, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_IFW_FILE_NAME"), ifw_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_ENABLED"), nuget_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (nuget_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_GENERATORS"), nuget_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_ID"), nuget_id, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_VERSION"), nuget_version, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_AUTHORS"), nuget_authors, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_PACKAGE_DESCRIPTION"), nuget_description, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_NUGET_FILE_NAME"), nuget_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_ENABLED"), freebsd_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (freebsd_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_GENERATORS"), freebsd_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_NAME"), freebsd_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_VERSION"), freebsd_version, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_ORIGIN"), freebsd_origin, false, false);
    if (freebsd_deps.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_PACKAGE_DEPENDS"), freebsd_deps, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_FREEBSD_FILE_NAME"), freebsd_file_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_ENABLED"), cygwin_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    if (cygwin_generators_norm.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_GENERATORS"), cygwin_generators_norm, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_NAME"), cygwin_name, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_VERSION"), cygwin_version, false, false);
    if (cygwin_deps.count > 0) eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_PACKAGE_DEPENDS"), cygwin_deps, false, false);
    eval_set_var(ctx, sv_from_cstr("CPACK_CYGWIN_FILE_NAME"), cygwin_file_name, false, false);

    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_META_NAME"), package_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_META_VERSION"), package_version, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_META_COMPONENTS"), components_norm, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_META_DEPENDS"), depends_norm, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_META_FILE_NAME"), package_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_ARCHIVE_ENABLED"),
                                   archive_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_ARCHIVE_GENERATORS"), archive_generators_norm, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_ARCHIVE_EXT"), archive_ext, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DEB_ENABLED"),
                                   deb_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DEB_FILE_NAME"), deb_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DEB_DEPENDS"), deb_depends, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DEB_ARCH"), deb_arch, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_RPM_ENABLED"),
                                   rpm_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_RPM_FILE_NAME"), rpm_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_RPM_REQUIRES"), rpm_requires, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_RPM_ARCH"), rpm_arch, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_RPM_LICENSE"), rpm_license, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NSIS_ENABLED"),
                                   nsis_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NSIS_FILE_NAME"), nsis_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NSIS_DISPLAY_NAME"), nsis_display_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NSIS_INSTALL_DIR"), nsis_install_dir, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NSIS_CONTACT"), nsis_contact, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_WIX_ENABLED"),
                                   wix_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_WIX_FILE_NAME"), wix_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_WIX_PRODUCT_NAME"), wix_product_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_WIX_ARCH"), wix_arch, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_WIX_CULTURES"), wix_cultures, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DMG_ENABLED"),
                                   dmg_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DMG_FILE_NAME"), dmg_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DMG_VOLUME_NAME"), dmg_volume_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_DMG_FORMAT"), dmg_format, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_BUNDLE_ENABLED"),
                                   bundle_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_BUNDLE_FILE_NAME"), bundle_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_BUNDLE_NAME"), bundle_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_BUNDLE_PLIST"), bundle_plist, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_BUNDLE_ICON"), bundle_icon, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_PRODUCTBUILD_ENABLED"),
                                   productbuild_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_PRODUCTBUILD_FILE_NAME"), productbuild_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_PRODUCTBUILD_IDENTIFIER"), productbuild_identifier, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_PRODUCTBUILD_IDENTITY"), productbuild_signing_identity, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_IFW_ENABLED"),
                                   ifw_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_IFW_FILE_NAME"), ifw_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_IFW_PACKAGE_NAME"), ifw_package_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_IFW_PACKAGE_TITLE"), ifw_package_title, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_ENABLED"),
                                   nuget_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_FILE_NAME"), nuget_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_ID"), nuget_id, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_VERSION"), nuget_version, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_AUTHORS"), nuget_authors, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_NUGET_DESCRIPTION"), nuget_description, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_ENABLED"),
                                   freebsd_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_FILE_NAME"), freebsd_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_NAME"), freebsd_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_VERSION"), freebsd_version, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_ORIGIN"), freebsd_origin, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_FREEBSD_DEPENDS"), freebsd_deps, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_CYGWIN_ENABLED"),
                                   cygwin_enabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"),
                                   sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_CYGWIN_FILE_NAME"), cygwin_file_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_CYGWIN_NAME"), cygwin_name, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_CYGWIN_VERSION"), cygwin_version, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, sv_from_cstr("__CPACK_CYGWIN_DEPENDS"), cygwin_deps, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
}

static String_View cpack_var_token_upper(Evaluator_Context *ctx, String_View value) {
    if (!ctx) return sv_from_cstr("");
    if (value.count == 0) return value;
    char *buf = arena_strndup(ctx->arena, value.data, value.count);
    for (size_t i = 0; i < value.count; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (isalnum(c)) buf[i] = (char)toupper(c);
        else buf[i] = '_';
    }
    return sv_from_cstr(buf);
}

static bool cpack_component_keyword(String_View tok) {
    return sv_eq_ci(tok, sv_from_cstr("DISPLAY_NAME")) ||
           sv_eq_ci(tok, sv_from_cstr("DESCRIPTION")) ||
           sv_eq_ci(tok, sv_from_cstr("GROUP")) ||
           sv_eq_ci(tok, sv_from_cstr("DEPENDS")) ||
           sv_eq_ci(tok, sv_from_cstr("INSTALL_TYPES")) ||
           sv_eq_ci(tok, sv_from_cstr("REQUIRED")) ||
           sv_eq_ci(tok, sv_from_cstr("HIDDEN")) ||
           sv_eq_ci(tok, sv_from_cstr("DISABLED")) ||
           sv_eq_ci(tok, sv_from_cstr("DOWNLOADED")) ||
           sv_eq_ci(tok, sv_from_cstr("ARCHIVE_FILE")) ||
           sv_eq_ci(tok, sv_from_cstr("PLIST"));
}

static void eval_cpack_ifw_configure_file_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View input_arg = resolve_arg(ctx, args.items[0]);
    String_View output_arg = resolve_arg(ctx, args.items[1]);
    if (input_arg.count == 0 || output_arg.count == 0) return;

    String_View input_path = path_is_absolute_sv(input_arg) ? input_arg : path_join_arena(ctx->arena, ctx->current_list_dir, input_arg);
    String_View output_path = path_is_absolute_sv(output_arg) ? output_arg : path_join_arena(ctx->arena, ctx->current_binary_dir, output_arg);

    bool copy_only = false;
    bool at_only = false;
    for (size_t i = 2; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("COPYONLY"))) copy_only = true;
        if (sv_eq_ci(tok, sv_from_cstr("@ONLY"))) at_only = true;
    }

    String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(input_path));
    if (!content.data) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cpack_ifw_configure_file",
                 nob_temp_sprintf("falha ao ler arquivo: "SV_Fmt, SV_Arg(input_path)),
                 "verifique se o template existe");
        return;
    }

    String_View rendered = copy_only ? content : configure_expand_variables(ctx, content, at_only);
    if (!ensure_parent_dirs_for_path(ctx->arena, output_path)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cpack_ifw_configure_file",
                 nob_temp_sprintf("falha ao preparar diretorio para: "SV_Fmt, SV_Arg(output_path)),
                 "verifique permissao de escrita");
        return;
    }
    if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), rendered.data, rendered.count)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "cpack_ifw_configure_file",
                 nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(output_path)),
                 "verifique permissao de escrita");
        return;
    }

    eval_set_var(ctx, sv_from_cstr("CPACK_IFW_CONFIGURED_FILE"), output_path, false, false);
}

static void eval_cpack_add_install_type_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 1) return;
    String_View name = resolve_arg(ctx, args.items[0]);
    if (name.count == 0) return;

    CPack_Install_Type *install_type = build_model_get_or_create_cpack_install_type(ctx->model, ctx->arena, name);
    if (!install_type) return;

    for (size_t i = 1; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("DISPLAY_NAME")) && i + 1 < args.count) {
            build_cpack_install_type_set_display_name(install_type, resolve_arg(ctx, args.items[++i]));
        }
    }

    if (ctx->model->cpack_install_type_count > 0) {
        String_Builder sb = {0};
        for (size_t i = 0; i < ctx->model->cpack_install_type_count; i++) {
            if (i > 0) sb_append(&sb, ';');
            sb_append_buf(&sb, ctx->model->cpack_install_types[i].name.data, ctx->model->cpack_install_types[i].name.count);
        }
        eval_set_var(ctx, sv_from_cstr("CPACK_ALL_INSTALL_TYPES"),
            sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count)), false, false);
        nob_sb_free(sb);
    }

    String_View upper = cpack_var_token_upper(ctx, name);
    if (install_type->display_name.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_INSTALL_TYPE_%s_DISPLAY_NAME", nob_temp_sv_to_cstr(upper))),
            install_type->display_name, false, false);
    }
    cpack_sync_common_metadata(ctx);
}

static void eval_cpack_add_component_group_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 1) return;
    String_View name = resolve_arg(ctx, args.items[0]);
    if (name.count == 0) return;

    CPack_Component_Group *group = build_model_get_or_create_cpack_group(ctx->model, ctx->arena, name);
    if (!group) return;

    for (size_t i = 1; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("DISPLAY_NAME")) && i + 1 < args.count) {
            build_cpack_group_set_display_name(group, resolve_arg(ctx, args.items[++i]));
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DESCRIPTION")) && i + 1 < args.count) {
            build_cpack_group_set_description(group, resolve_arg(ctx, args.items[++i]));
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("PARENT_GROUP")) && i + 1 < args.count) {
            build_cpack_group_set_parent_group(group, resolve_arg(ctx, args.items[++i]));
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("EXPANDED"))) {
            build_cpack_group_set_expanded(group, true);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("BOLD_TITLE"))) {
            build_cpack_group_set_bold_title(group, true);
            continue;
        }
    }

    if (ctx->model->cpack_component_group_count > 0) {
        String_Builder sb = {0};
        for (size_t i = 0; i < ctx->model->cpack_component_group_count; i++) {
            if (i > 0) sb_append(&sb, ';');
            sb_append_buf(&sb, ctx->model->cpack_component_groups[i].name.data, ctx->model->cpack_component_groups[i].name.count);
        }
        eval_set_var(ctx, sv_from_cstr("CPACK_COMPONENT_GROUPS_ALL"),
            sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count)), false, false);
        nob_sb_free(sb);
    }

    String_View upper = cpack_var_token_upper(ctx, name);
    if (group->display_name.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_GROUP_%s_DISPLAY_NAME", nob_temp_sv_to_cstr(upper))),
            group->display_name, false, false);
    }
    if (group->description.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_GROUP_%s_DESCRIPTION", nob_temp_sv_to_cstr(upper))),
            group->description, false, false);
    }
    if (group->parent_group.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_GROUP_%s_PARENT_GROUP", nob_temp_sv_to_cstr(upper))),
            group->parent_group, false, false);
    }
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_GROUP_%s_EXPANDED", nob_temp_sv_to_cstr(upper))),
        group->expanded ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_GROUP_%s_BOLD_TITLE", nob_temp_sv_to_cstr(upper))),
        group->bold_title ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    cpack_sync_common_metadata(ctx);
}

static void eval_cpack_add_component_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 1) return;
    String_View name = resolve_arg(ctx, args.items[0]);
    if (name.count == 0) return;

    CPack_Component *component = build_model_get_or_create_cpack_component(ctx->model, ctx->arena, name);
    if (!component) return;
    build_cpack_component_clear_dependencies(component);
    build_cpack_component_clear_install_types(component);

    for (size_t i = 1; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("DISPLAY_NAME")) && i + 1 < args.count) {
            build_cpack_component_set_display_name(component, resolve_arg(ctx, args.items[++i]));
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DESCRIPTION")) && i + 1 < args.count) {
            build_cpack_component_set_description(component, resolve_arg(ctx, args.items[++i]));
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("GROUP")) && i + 1 < args.count) {
            build_cpack_component_set_group(component, resolve_arg(ctx, args.items[++i]));
            (void)build_model_get_or_create_cpack_group(ctx->model, ctx->arena, component->group);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DEPENDS"))) {
            i++;
            while (i < args.count) {
                String_View dep = resolve_arg(ctx, args.items[i]);
                if (cpack_component_keyword(dep)) {
                    i--;
                    break;
                }
                if (dep.count > 0) build_cpack_component_add_dependency(component, ctx->arena, dep);
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("INSTALL_TYPES"))) {
            i++;
            while (i < args.count) {
                String_View it = resolve_arg(ctx, args.items[i]);
                if (cpack_component_keyword(it)) {
                    i--;
                    break;
                }
                if (it.count > 0) {
                    build_cpack_component_add_install_type(component, ctx->arena, it);
                    (void)build_model_get_or_create_cpack_install_type(ctx->model, ctx->arena, it);
                }
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("REQUIRED"))) {
            build_cpack_component_set_required(component, true);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("HIDDEN"))) {
            build_cpack_component_set_hidden(component, true);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DISABLED"))) {
            build_cpack_component_set_disabled(component, true);
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("DOWNLOADED"))) {
            build_cpack_component_set_downloaded(component, true);
            continue;
        }
        if ((sv_eq_ci(tok, sv_from_cstr("ARCHIVE_FILE")) || sv_eq_ci(tok, sv_from_cstr("PLIST"))) && i + 1 < args.count) {
            i++;
            continue;
        }
    }

    if (ctx->model->cpack_component_count > 0) {
        String_Builder sb = {0};
        for (size_t i = 0; i < ctx->model->cpack_component_count; i++) {
            if (i > 0) sb_append(&sb, ';');
            sb_append_buf(&sb, ctx->model->cpack_components[i].name.data, ctx->model->cpack_components[i].name.count);
        }
        eval_set_var(ctx, sv_from_cstr("CPACK_COMPONENTS_ALL"),
            sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count)), false, false);
        nob_sb_free(sb);
    }

    String_View upper = cpack_var_token_upper(ctx, name);
    if (component->display_name.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_DISPLAY_NAME", nob_temp_sv_to_cstr(upper))),
            component->display_name, false, false);
    }
    if (component->description.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_DESCRIPTION", nob_temp_sv_to_cstr(upper))),
            component->description, false, false);
    }
    if (component->group.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_GROUP", nob_temp_sv_to_cstr(upper))),
            component->group, false, false);
    }
    if (component->depends.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_DEPENDS", nob_temp_sv_to_cstr(upper))),
            join_string_list_with_semicolon(ctx, &component->depends), false, false);
    }
    if (component->install_types.count > 0) {
        eval_set_var(ctx,
            sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_INSTALL_TYPES", nob_temp_sv_to_cstr(upper))),
            join_string_list_with_semicolon(ctx, &component->install_types), false, false);
    }
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_REQUIRED", nob_temp_sv_to_cstr(upper))),
        component->required ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_HIDDEN", nob_temp_sv_to_cstr(upper))),
        component->hidden ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_DISABLED", nob_temp_sv_to_cstr(upper))),
        component->disabled ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    eval_set_var(ctx,
        sv_from_cstr(nob_temp_sprintf("CPACK_COMPONENT_%s_DOWNLOADED", nob_temp_sv_to_cstr(upper))),
        component->downloaded ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
    cpack_sync_common_metadata(ctx);
}

static bool add_test_keyword(String_View arg) {
    return sv_eq_ci(arg, sv_from_cstr("NAME")) ||
           sv_eq_ci(arg, sv_from_cstr("COMMAND")) ||
           sv_eq_ci(arg, sv_from_cstr("CONFIGURATIONS")) ||
           sv_eq_ci(arg, sv_from_cstr("WORKING_DIRECTORY")) ||
           sv_eq_ci(arg, sv_from_cstr("COMMAND_EXPAND_LISTS"));
}

static String_View join_string_list_with_semicolon(Evaluator_Context *ctx, const String_List *list) {
    if (!ctx || !list || list->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < list->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, list->items[i].data, list->items[i].count);
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View join_string_list_with_space_and_expand_semicolons(Evaluator_Context *ctx, const String_List *list, bool expand_semicolons) {
    if (!ctx || !list || list->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < list->count; i++) {
        if (i > 0) sb_append(&sb, ' ');
        String_View item = list->items[i];
        if (!expand_semicolons) {
            sb_append_buf(&sb, item.data, item.count);
            continue;
        }
        for (size_t j = 0; j < item.count; j++) {
            sb_append(&sb, item.data[j] == ';' ? ' ' : item.data[j]);
        }
    }
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static void eval_add_test_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 1) return;

    String_View test_name = sv_from_cstr("");
    String_View working_dir = sv_from_cstr("");
    bool command_expand_lists = false;
    String_List command_items = {0};
    string_list_init(&command_items);

    String_View first = resolve_arg(ctx, args.items[0]);
    if (sv_eq_ci(first, sv_from_cstr("NAME"))) {
        if (args.count < 4) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_test",
                "assinatura NAME requer ao menos NAME <nome> COMMAND <cmd>",
                "use add_test(NAME my_test COMMAND my_exe [args...])");
            return;
        }
        test_name = resolve_arg(ctx, args.items[1]);
        for (size_t i = 2; i < args.count; i++) {
            String_View key = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(key, sv_from_cstr("COMMAND"))) {
                i++;
                while (i < args.count) {
                    String_View tok = resolve_arg(ctx, args.items[i]);
                    if (add_test_keyword(tok)) {
                        i--;
                        break;
                    }
                    if (tok.count > 0) string_list_add(&command_items, ctx->arena, tok);
                    i++;
                }
                continue;
            }
            if (sv_eq_ci(key, sv_from_cstr("WORKING_DIRECTORY")) && i + 1 < args.count) {
                working_dir = resolve_arg(ctx, args.items[++i]);
                continue;
            }
            if (sv_eq_ci(key, sv_from_cstr("CONFIGURATIONS"))) {
                i++;
                while (i < args.count) {
                    String_View tok = resolve_arg(ctx, args.items[i]);
                    if (add_test_keyword(tok)) {
                        i--;
                        break;
                    }
                    i++;
                }
                continue;
            }
            if (sv_eq_ci(key, sv_from_cstr("COMMAND_EXPAND_LISTS"))) {
                command_expand_lists = true;
                continue;
            }
        }
    } else {
        test_name = first;
        for (size_t i = 1; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (tok.count > 0) string_list_add(&command_items, ctx->arena, tok);
        }
    }

    if (test_name.count == 0) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_test",
            "nome de teste vazio", "forneca um nome valido para add_test");
        return;
    }
    if (command_items.count == 0) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_test",
            nob_temp_sprintf("teste sem COMMAND: "SV_Fmt, SV_Arg(test_name)),
            "adicione um comando executavel ao teste");
        return;
    }

    String_View command = join_string_list_with_space_and_expand_semicolons(ctx, &command_items, command_expand_lists);
    build_model_add_test(ctx->model, test_name, command, working_dir, command_expand_lists);

    if (!ctx->model->enable_testing) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "add_test",
            "add_test chamado sem enable_testing previo",
            "em CMake real, habilite testes com enable_testing()");
    }

    String_List test_names = {0};
    string_list_init(&test_names);
    for (size_t i = 0; i < ctx->model->test_count; i++) {
        string_list_add(&test_names, ctx->arena, ctx->model->tests[i].name);
    }
    String_View names_value = join_string_list_with_semicolon(ctx, &test_names);
    eval_set_var(ctx, sv_from_cstr("CMAKE_CTEST_TESTS"), names_value, false, false);
    eval_set_var(ctx, sv_from_cstr("CMAKE_CTEST_TEST_COUNT"),
        sv_from_cstr(nob_temp_sprintf("%zu", ctx->model->test_count)), false, false);
}

// Avalia o comando 'build_command'
static void eval_build_command_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View target = sv_from_cstr("");
    String_View config = sv_from_cstr("");

    for (size_t i = 1; i < args.count; i++) {
        String_View key = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(key, sv_from_cstr("TARGET")) && i + 1 < args.count) {
            target = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("CONFIGURATION")) && i + 1 < args.count) {
            config = resolve_arg(ctx, args.items[++i]);
            continue;
        }
    }

    String_Builder sb = {0};
    sb_append_cstr(&sb, "cmake --build .");
    if (config.count > 0) {
        sb_append_cstr(&sb, " --config ");
        sb_append_buf(&sb, config.data, config.count);
    }
    if (target.count > 0) {
        sb_append_cstr(&sb, " --target ");
        sb_append_buf(&sb, target.data, target.count);
    }

    String_View cmd = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    eval_set_var(ctx, out_var, cmd, false, false);
}

static Build_Test* eval_find_test_by_name(Build_Model *model, String_View test_name) {
    if (!model || test_name.count == 0) return NULL;
    for (size_t i = 0; i < model->test_count; i++) {
        if (nob_sv_eq(model->tests[i].name, test_name)) return &model->tests[i];
    }
    return NULL;
}

static String_View internal_test_property_key(Evaluator_Context *ctx, String_View test_name, String_View prop_name) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "__TEST_PROP__");
    sb_append_buf(&sb, test_name.data, test_name.count);
    sb_append_cstr(&sb, "__");
    sb_append_buf(&sb, prop_name.data, prop_name.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

// Avalia o comando 'set_tests_properties'
static void eval_set_tests_properties_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 4) return;

    size_t props_idx = SIZE_MAX;
    for (size_t i = 0; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("PROPERTIES"))) {
            props_idx = i;
            break;
        }
    }
    if (props_idx == SIZE_MAX || props_idx == 0 || props_idx + 2 > args.count) return;

    for (size_t p = props_idx + 1; p + 1 < args.count; p += 2) {
        String_View prop_name = resolve_arg(ctx, args.items[p]);
        String_View prop_value = resolve_arg(ctx, args.items[p + 1]);
        if (prop_name.count == 0) continue;

        for (size_t t = 0; t < props_idx; t++) {
            String_View test_name = resolve_arg(ctx, args.items[t]);
            Build_Test *test = eval_find_test_by_name(ctx->model, test_name);
            if (!test) continue;
            String_View key = internal_test_property_key(ctx, test_name, prop_name);
            build_model_set_cache_variable(ctx->model, key, prop_value, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
        }
    }
}

// Avalia o comando 'get_test_property'
static void eval_get_test_property_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || !ctx->model || args.count < 3) return;

    String_View test_name = resolve_arg(ctx, args.items[0]);
    String_View prop_name = resolve_arg(ctx, args.items[1]);
    String_View out_var = resolve_arg(ctx, args.items[2]);

    Build_Test *test = eval_find_test_by_name(ctx->model, test_name);
    if (!test) {
        eval_set_var(ctx, out_var, sv_from_cstr("NOTFOUND"), false, false);
        return;
    }

    String_View value = sv_from_cstr("");
    if (sv_eq_ci(prop_name, sv_from_cstr("NAME"))) {
        value = test->name;
    } else if (sv_eq_ci(prop_name, sv_from_cstr("COMMAND"))) {
        value = test->command;
    } else if (sv_eq_ci(prop_name, sv_from_cstr("WORKING_DIRECTORY"))) {
        value = test->working_directory;
    } else if (sv_eq_ci(prop_name, sv_from_cstr("COMMAND_EXPAND_LISTS"))) {
        value = test->command_expand_lists ? sv_from_cstr("ON") : sv_from_cstr("OFF");
    } else {
        value = build_model_get_cache_variable(ctx->model, internal_test_property_key(ctx, test_name, prop_name));
        if (value.count == 0) value = sv_from_cstr("NOTFOUND");
    }

    eval_set_var(ctx, out_var, value, false, false);
}

static bool path_is_absolute_sv(String_View path) {
    return build_path_is_absolute(path);
}

static String_View path_join_arena(Arena *arena, String_View base, String_View rel) {
    return build_path_join(arena, base, rel);
}

static String_View path_parent_dir_arena(Arena *arena, String_View full_path) {
    return build_path_parent_dir(arena, full_path);
}

static bool path_has_separator(String_View path) {
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') return true;
    }
    return false;
}

static bool ensure_parent_dirs_for_path(Arena *arena, String_View file_path) {
    return sys_ensure_parent_dirs(arena, file_path);
}

static String_View path_make_absolute_arena(Arena *arena, String_View path) {
    return build_path_make_absolute(arena, path);
}

static size_t path_last_separator_index(String_View path) {
    for (size_t i = path.count; i > 0; i--) {
        char c = path.data[i - 1];
        if (c == '/' || c == '\\') return i - 1;
    }
    return SIZE_MAX;
}

static String_View path_basename_sv(String_View path) {
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return path;
    return nob_sv_from_parts(path.data + sep + 1, path.count - (sep + 1));
}

static String_View filename_ext_sv(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return sv_from_cstr("");
    return nob_sv_from_parts(name.data + dot, name.count - dot);
}

static String_View filename_without_ext_sv(String_View name) {
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < name.count; i++) {
        if (name.data[i] == '.') dot = i;
    }
    if (dot == SIZE_MAX || dot == 0) return name;
    return nob_sv_from_parts(name.data, dot);
}

static String_View cmake_path_normalize(Evaluator_Context *ctx, String_View input) {
    if (!ctx) return sv_from_cstr("");
    if (input.count == 0) return sv_from_cstr(".");

    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;
    if (has_drive) {
        pos = 2;
        if (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) {
            absolute = true;
            while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
        }
    } else if (input.data[0] == '/' || input.data[0] == '\\') {
        absolute = true;
        while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
    }

    String_List segments = {0};
    string_list_init(&segments);
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && input.data[pos] != '/' && input.data[pos] != '\\') pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && (input.data[pos] == '/' || input.data[pos] == '\\')) pos++;
        if (seg.count == 0 || nob_sv_eq(seg, sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, sv_from_cstr(".."))) {
            if (segments.count > 0 && !nob_sv_eq(segments.items[segments.count - 1], sv_from_cstr(".."))) {
                segments.count--;
            } else if (!absolute) {
                string_list_add(&segments, ctx->arena, seg);
            }
            continue;
        }
        string_list_add(&segments, ctx->arena, seg);
    }

    String_Builder sb = {0};
    if (has_drive) {
        sb_append(&sb, input.data[0]);
        sb_append(&sb, ':');
    }
    if (absolute) sb_append(&sb, '/');

    for (size_t i = 0; i < segments.count; i++) {
        if ((absolute || has_drive) && sb.count > 0 && sb.items[sb.count - 1] != '/') sb_append(&sb, '/');
        if (!absolute && !has_drive && i > 0) sb_append(&sb, '/');
        sb_append_buf(&sb, segments.items[i].data, segments.items[i].count);
    }

    if (sb.count == 0) {
        if (has_drive && absolute) {
            sb_append(&sb, input.data[0]);
            sb_append_cstr(&sb, ":/");
        } else if (has_drive) {
            sb_append(&sb, input.data[0]);
            sb_append(&sb, ':');
        } else if (absolute) {
            sb_append(&sb, '/');
        } else {
            sb_append(&sb, '.');
        }
    }

    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View cmake_path_root_name(String_View path) {
    bool has_drive = path.count >= 2 &&
                     isalpha((unsigned char)path.data[0]) &&
                     path.data[1] == ':';
    if (has_drive) return nob_sv_from_parts(path.data, 2);
    return sv_from_cstr("");
}

static String_View cmake_path_root_directory(String_View path) {
    if (path.count == 0) return sv_from_cstr("");
    if ((path.count >= 1 && (path.data[0] == '/' || path.data[0] == '\\')) ||
        (path.count >= 3 && isalpha((unsigned char)path.data[0]) && path.data[1] == ':' &&
         (path.data[2] == '/' || path.data[2] == '\\'))) {
        return sv_from_cstr("/");
    }
    return sv_from_cstr("");
}

static String_View cmake_path_root_path(Evaluator_Context *ctx, String_View path) {
    String_View root_name = cmake_path_root_name(path);
    String_View root_dir = cmake_path_root_directory(path);
    if (root_name.count == 0) return root_dir;
    if (root_dir.count == 0) return root_name;
    String_Builder sb = {0};
    sb_append_buf(&sb, root_name.data, root_name.count);
    sb_append_buf(&sb, root_dir.data, root_dir.count);
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}

static String_View cmake_path_relative_part(Evaluator_Context *ctx, String_View path) {
    String_View root = cmake_path_root_path(ctx, path);
    if (root.count == 0) return path;
    if (path.count <= root.count) return sv_from_cstr("");
    String_View rel = nob_sv_from_parts(path.data + root.count, path.count - root.count);
    while (rel.count > 0 && (rel.data[0] == '/' || rel.data[0] == '\\')) {
        rel = nob_sv_from_parts(rel.data + 1, rel.count - 1);
    }
    return rel;
}

static String_View cmake_path_parent_path(Evaluator_Context *ctx, String_View path) {
    (void)ctx;
    size_t sep = path_last_separator_index(path);
    if (sep == SIZE_MAX) return sv_from_cstr("");
    if (sep == 0) return sv_from_cstr("/");
    if (sep == 2 && path.count >= 3 && path.data[1] == ':') {
        return nob_sv_from_parts(path.data, 3);
    }
    return nob_sv_from_parts(path.data, sep);
}

static String_View cmake_path_get_component_value(Evaluator_Context *ctx, String_View input, String_View component, bool *supported) {
    if (supported) *supported = true;
    if (sv_eq_ci(component, sv_from_cstr("ROOT_NAME"))) {
        return cmake_path_root_name(input);
    }
    if (sv_eq_ci(component, sv_from_cstr("ROOT_DIRECTORY"))) {
        return cmake_path_root_directory(input);
    }
    if (sv_eq_ci(component, sv_from_cstr("ROOT_PATH"))) {
        return cmake_path_root_path(ctx, input);
    }
    if (sv_eq_ci(component, sv_from_cstr("FILENAME"))) {
        return path_basename_sv(input);
    }
    if (sv_eq_ci(component, sv_from_cstr("STEM"))) {
        return filename_without_ext_sv(path_basename_sv(input));
    }
    if (sv_eq_ci(component, sv_from_cstr("EXTENSION"))) {
        return filename_ext_sv(path_basename_sv(input));
    }
    if (sv_eq_ci(component, sv_from_cstr("RELATIVE_PART"))) {
        return cmake_path_relative_part(ctx, input);
    }
    if (sv_eq_ci(component, sv_from_cstr("PARENT_PATH"))) {
        return cmake_path_parent_path(ctx, input);
    }
    if (supported) *supported = false;
    return sv_from_cstr("");
}

static String_View cmake_path_relativize(Evaluator_Context *ctx, String_View path, String_View base_dir) {
    String_View a = cmake_path_normalize(ctx, path);
    String_View b = cmake_path_normalize(ctx, base_dir);

    String_List seg_a = {0}, seg_b = {0};
    string_list_init(&seg_a);
    string_list_init(&seg_b);

    size_t start_a = 0;
    size_t start_b = 0;
    String_View root_a = cmake_path_root_path(ctx, a);
    String_View root_b = cmake_path_root_path(ctx, b);
    if (!nob_sv_eq(root_a, root_b)) return a;
    if (root_a.count > 0) {
        start_a = root_a.count;
        start_b = root_b.count;
    }

    for (size_t i = start_a; i <= a.count; i++) {
        bool sep = (i == a.count) || a.data[i] == '/' || a.data[i] == '\\';
        if (!sep) continue;
        if (i > start_a) string_list_add(&seg_a, ctx->arena, nob_sv_from_parts(a.data + start_a, i - start_a));
        start_a = i + 1;
    }
    for (size_t i = start_b; i <= b.count; i++) {
        bool sep = (i == b.count) || b.data[i] == '/' || b.data[i] == '\\';
        if (!sep) continue;
        if (i > start_b) string_list_add(&seg_b, ctx->arena, nob_sv_from_parts(b.data + start_b, i - start_b));
        start_b = i + 1;
    }

    size_t common = 0;
    while (common < seg_a.count && common < seg_b.count && nob_sv_eq(seg_a.items[common], seg_b.items[common])) {
        common++;
    }

    String_Builder sb = {0};
    for (size_t i = common; i < seg_b.count; i++) {
        if (sb.count > 0) sb_append(&sb, '/');
        sb_append_cstr(&sb, "..");
    }
    for (size_t i = common; i < seg_a.count; i++) {
        if (sb.count > 0) sb_append(&sb, '/');
        sb_append_buf(&sb, seg_a.items[i].data, seg_a.items[i].count);
    }
    if (sb.count == 0) sb_append(&sb, '.');
    String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items, sb.count));
    nob_sb_free(sb);
    return out;
}


// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

static bool sv_starts_with_ci(String_View value, String_View prefix) {
    if (prefix.count > value.count) return false;
    return sv_eq_ci(nob_sv_from_parts(value.data, prefix.count), prefix);
}

static void eval_cmake_path_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    String_View mode = resolve_arg(ctx, args.items[0]);

    if (sv_eq_ci(mode, sv_from_cstr("GET")) && args.count >= 4) {
        String_View input_var = resolve_arg(ctx, args.items[1]);
        String_View raw_input = eval_get_var(ctx, input_var);
        if (raw_input.count == 0) {
            // fallback de compatibilidade: trata argumento como caminho literal
            raw_input = input_var;
        }
        String_View input = cmake_path_normalize(ctx, raw_input);
        String_View component = resolve_arg(ctx, args.items[2]);
        String_View out_var = resolve_arg(ctx, args.items[3]);
        String_View result = sv_from_cstr("");
        bool supported = false;
        result = cmake_path_get_component_value(ctx, input, component, &supported);
        if (!supported) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_path",
                nob_temp_sprintf("componente GET nao suportado: "SV_Fmt, SV_Arg(component)),
                "use ROOT_NAME/ROOT_DIRECTORY/ROOT_PATH/FILENAME/STEM/EXTENSION/RELATIVE_PART/PARENT_PATH");
        }
        eval_set_var(ctx, out_var, result, false, false);
        return;
    }

    if (sv_eq_ci(mode, sv_from_cstr("SET")) && args.count >= 2) {
        String_View out_var = resolve_arg(ctx, args.items[1]);
        bool normalize = false;
        String_View value = sv_from_cstr("");
        for (size_t i = 2; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("NORMALIZE"))) {
                normalize = true;
                continue;
            }
            value = tok;
            break;
        }
        if (normalize) value = cmake_path_normalize(ctx, value);
        eval_set_var(ctx, out_var, value, false, false);
        return;
    }

    if (sv_eq_ci(mode, sv_from_cstr("APPEND")) && args.count >= 2) {
        String_View path_var = resolve_arg(ctx, args.items[1]);
        String_View current = eval_get_var(ctx, path_var);
        if (current.count == 0) current = path_var;

        bool normalize = false;
        String_View out_var = path_var;
        for (size_t i = 2; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("NORMALIZE"))) {
                normalize = true;
                continue;
            }
            if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) && i + 1 < args.count) {
                out_var = resolve_arg(ctx, args.items[++i]);
                continue;
            }
            current = path_join_arena(ctx->arena, current, tok);
        }
        if (normalize) current = cmake_path_normalize(ctx, current);
        eval_set_var(ctx, out_var, current, false, false);
        return;
    }

    if (sv_eq_ci(mode, sv_from_cstr("NORMAL_PATH")) && args.count >= 2) {
        String_View path_var = resolve_arg(ctx, args.items[1]);
        String_View value = eval_get_var(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View out_var = path_var;
        for (size_t i = 2; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) && i + 1 < args.count) {
                out_var = resolve_arg(ctx, args.items[++i]);
            }
        }
        value = cmake_path_normalize(ctx, value);
        eval_set_var(ctx, out_var, value, false, false);
        return;
    }

    if (sv_eq_ci(mode, sv_from_cstr("RELATIVE_PATH")) && args.count >= 2) {
        String_View path_var = resolve_arg(ctx, args.items[1]);
        String_View value = eval_get_var(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View base_dir = ctx->current_list_dir;
        String_View out_var = path_var;
        for (size_t i = 2; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("BASE_DIRECTORY")) && i + 1 < args.count) {
                base_dir = resolve_arg(ctx, args.items[++i]);
                continue;
            }
            if (sv_eq_ci(tok, sv_from_cstr("OUTPUT_VARIABLE")) && i + 1 < args.count) {
                out_var = resolve_arg(ctx, args.items[++i]);
                continue;
            }
        }
        String_View rel = cmake_path_relativize(ctx, value, base_dir);
        eval_set_var(ctx, out_var, rel, false, false);
        return;
    }

    if (sv_eq_ci(mode, sv_from_cstr("COMPARE")) && args.count >= 5) {
        String_View lhs = cmake_path_normalize(ctx, resolve_arg(ctx, args.items[1]));
        String_View op = resolve_arg(ctx, args.items[2]);
        String_View rhs = cmake_path_normalize(ctx, resolve_arg(ctx, args.items[3]));
        String_View out_var = resolve_arg(ctx, args.items[4]);
        int cmp = strcmp(nob_temp_sv_to_cstr(lhs), nob_temp_sv_to_cstr(rhs));
        bool ok = false;
        if (sv_eq_ci(op, sv_from_cstr("EQUAL"))) ok = cmp == 0;
        else if (sv_eq_ci(op, sv_from_cstr("NOT_EQUAL"))) ok = cmp != 0;
        else if (sv_eq_ci(op, sv_from_cstr("LESS"))) ok = cmp < 0;
        else if (sv_eq_ci(op, sv_from_cstr("LESS_EQUAL"))) ok = cmp <= 0;
        else if (sv_eq_ci(op, sv_from_cstr("GREATER"))) ok = cmp > 0;
        else if (sv_eq_ci(op, sv_from_cstr("GREATER_EQUAL"))) ok = cmp >= 0;
        eval_set_var(ctx, out_var, ok ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
        return;
    }

    if (sv_starts_with_ci(mode, sv_from_cstr("HAS_")) && args.count >= 3) {
        String_View path_var = resolve_arg(ctx, args.items[1]);
        String_View value = eval_get_var(ctx, path_var);
        if (value.count == 0) value = path_var;
        String_View input = cmake_path_normalize(ctx, value);
        String_View out_var = resolve_arg(ctx, args.items[2]);
        String_View component = nob_sv_from_parts(mode.data + 4, mode.count - 4);
        bool supported = false;
        String_View comp = cmake_path_get_component_value(ctx, input, component, &supported);
        bool has = supported && comp.count > 0;
        eval_set_var(ctx, out_var, has ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
        return;
    }

    if (sv_starts_with_ci(mode, sv_from_cstr("IS_")) && args.count >= 3) {
        String_View path_var = resolve_arg(ctx, args.items[1]);
        String_View value = eval_get_var(ctx, path_var);
        if (value.count == 0) value = path_var;
        value = cmake_path_normalize(ctx, value);
        String_View out_var = resolve_arg(ctx, args.items[2]);
        bool result = false;
        if (sv_eq_ci(mode, sv_from_cstr("IS_ABSOLUTE"))) {
            result = path_is_absolute_sv(value);
        } else if (sv_eq_ci(mode, sv_from_cstr("IS_RELATIVE"))) {
            result = !path_is_absolute_sv(value);
        }
        eval_set_var(ctx, out_var, result ? sv_from_cstr("ON") : sv_from_cstr("OFF"), false, false);
        return;
    }

    diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "cmake_path",
        "assinatura parcial: subcomando nao suportado",
        "use GET/SET/APPEND/COMPARE/HAS_*/IS_*/NORMAL_PATH/RELATIVE_PATH");
}

static void eval_get_filename_component_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 3) return;

    String_View out_var = resolve_arg(ctx, args.items[0]);
    String_View input = resolve_arg(ctx, args.items[1]);
    String_View mode = resolve_arg(ctx, args.items[2]);

    String_View base_dir = ctx->current_list_dir;
    for (size_t i = 3; i + 1 < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(arg, sv_from_cstr("BASE_DIR"))) {
            base_dir = resolve_arg(ctx, args.items[i + 1]);
            break;
        }
    }

    String_View result = sv_from_cstr("");
    if (sv_eq_ci(mode, sv_from_cstr("DIRECTORY")) || sv_eq_ci(mode, sv_from_cstr("PATH"))) {
        size_t sep = path_last_separator_index(input);
        if (sep == SIZE_MAX) result = sv_from_cstr("");
        else result = sv_from_cstr(arena_strndup(ctx->arena, input.data, sep));
    } else if (sv_eq_ci(mode, sv_from_cstr("NAME"))) {
        result = path_basename_sv(input);
    } else if (sv_eq_ci(mode, sv_from_cstr("EXT")) || sv_eq_ci(mode, sv_from_cstr("LAST_EXT"))) {
        result = filename_ext_sv(path_basename_sv(input));
    } else if (sv_eq_ci(mode, sv_from_cstr("NAME_WE")) || sv_eq_ci(mode, sv_from_cstr("NAME_WLE"))) {
        result = filename_without_ext_sv(path_basename_sv(input));
    } else if (sv_eq_ci(mode, sv_from_cstr("ABSOLUTE")) || sv_eq_ci(mode, sv_from_cstr("REALPATH"))) {
        String_View candidate = path_is_absolute_sv(input) ? input : path_join_arena(ctx->arena, base_dir, input);
        result = path_make_absolute_arena(ctx->arena, candidate);
    } else {
        result = path_basename_sv(input);
    }

    eval_set_var(ctx, out_var, result, false, false);
}

static bool export_keyword(String_View v) {
    return sv_eq_ci(v, sv_from_cstr("TARGETS")) ||
           sv_eq_ci(v, sv_from_cstr("FILE")) ||
           sv_eq_ci(v, sv_from_cstr("NAMESPACE")) ||
           sv_eq_ci(v, sv_from_cstr("APPEND")) ||
           sv_eq_ci(v, sv_from_cstr("EXPORT_LINK_INTERFACE_LIBRARIES")) ||
           sv_eq_ci(v, sv_from_cstr("CXX_MODULES_DIRECTORY")) ||
           sv_eq_ci(v, sv_from_cstr("EXPORT_PACKAGE_DEPENDENCIES")) ||
           sv_eq_ci(v, sv_from_cstr("ANDROID_MK")) ||
           sv_eq_ci(v, sv_from_cstr("EXPORT")) ||
           sv_eq_ci(v, sv_from_cstr("PACKAGE"));
}


// ============================================================================
// EXPORT E INSTALAÇÃO
// ============================================================================

static void export_collect_targets_from_install_set(Evaluator_Context *ctx, String_View export_set_name, String_List *out_targets) {
    if (!ctx || !ctx->model || !out_targets || export_set_name.count == 0) return;
    String_View set_key = internal_install_export_set_key(ctx, export_set_name);
    String_View set_targets = build_model_get_cache_variable(ctx->model, set_key);
    List_Add_Ud ud_targets = { .list = out_targets, .unique = false };
    eval_foreach_semicolon_item(ctx, set_targets, /*trim_ws=*/true, eval_list_add_item, &ud_targets);
}

static void export_write_targets_file(Evaluator_Context *ctx,
                                      String_View out_path,
                                      String_View ns,
                                      String_View signature,
                                      String_View export_set_name,
                                      const String_List *targets,
                                      bool append_mode) {
    if (!ctx || out_path.count == 0 || !targets) return;
    if (!ensure_parent_dirs_for_path(ctx->arena, out_path)) return;

    Nob_String_Builder existing = {0};
    if (append_mode && nob_file_exists(nob_temp_sv_to_cstr(out_path))) {
        nob_read_entire_file(nob_temp_sv_to_cstr(out_path), &existing);
    }

    String_Builder sb = {0};
    if (existing.items && existing.count > 0) {
        sb_append_buf(&sb, existing.items, existing.count);
        if (existing.items[existing.count - 1] != '\n') sb_append(&sb, '\n');
    }
    sb_append_cstr(&sb, "# cmk2nob export support\n");
    sb_append_cstr(&sb, "# signature: ");
    sb_append_buf(&sb, signature.data, signature.count);
    sb_append(&sb, '\n');
    if (export_set_name.count > 0) {
        sb_append_cstr(&sb, "# export-set: ");
        sb_append_buf(&sb, export_set_name.data, export_set_name.count);
        sb_append(&sb, '\n');
    }
    if (ns.count > 0) {
        sb_append_cstr(&sb, "# namespace: ");
        sb_append_buf(&sb, ns.data, ns.count);
        sb_append(&sb, '\n');
    }
    sb_append_cstr(&sb, "set(_CMK2NOB_EXPORTED_TARGETS ");
    for (size_t i = 0; i < targets->count; i++) {
        if (i > 0) sb_append(&sb, ';');
        sb_append_buf(&sb, targets->items[i].data, targets->items[i].count);
    }
    sb_append_cstr(&sb, ")\n");
    if (ns.count > 0) {
        sb_append_cstr(&sb, "set(_CMK2NOB_EXPORTED_NAMESPACE \"");
        sb_append_buf(&sb, ns.data, ns.count);
        sb_append_cstr(&sb, "\")\n");
    }

    (void)nob_write_entire_file(nob_temp_sv_to_cstr(out_path), sb.items, sb.count);
    nob_sb_free(existing);
    nob_sb_free(sb);
}

static void export_register_package(Evaluator_Context *ctx, String_View package_name) {
    if (!ctx || !ctx->model || package_name.count == 0) return;
    String_View no_registry = eval_get_var(ctx, sv_from_cstr("CMAKE_EXPORT_NO_PACKAGE_REGISTRY"));
    if (sv_bool_is_true(no_registry)) return;

    String_View registry_val = eval_get_var(ctx, sv_from_cstr("CMAKE_EXPORT_PACKAGE_REGISTRY"));
    String_List packages = {0};
    string_list_init(&packages);
    List_Add_Ud ud_packages = { .list = &packages, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                registry_val,
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_packages);
    string_list_add_unique(&packages, ctx->arena, package_name);
    String_View merged = join_string_list_with_semicolon(ctx, &packages);
    eval_set_var(ctx, sv_from_cstr("CMAKE_EXPORT_PACKAGE_REGISTRY"), merged, false, false);

    String_Builder key_sb = {0};
    sb_append_buf(&key_sb, package_name.data, package_name.count);
    sb_append_cstr(&key_sb, "_DIR");
    String_View dir_key = sv_from_cstr(arena_strndup(ctx->arena, key_sb.items, key_sb.count));
    nob_sb_free(key_sb);
    eval_set_var(ctx, dir_key, ctx->current_binary_dir, false, false);

    String_View registry_dir = path_join_arena(ctx->arena, ctx->current_binary_dir, sv_from_cstr(".cmk2nob_package_registry"));
    if (!nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(registry_dir))) return;
    String_Builder reg_name_sb = {0};
    sb_append_buf(&reg_name_sb, package_name.data, package_name.count);
    sb_append_cstr(&reg_name_sb, ".cmake");
    String_View reg_name = sv_from_cstr(arena_strndup(ctx->arena, reg_name_sb.items, reg_name_sb.count));
    nob_sb_free(reg_name_sb);
    String_View reg_file = path_join_arena(ctx->arena, registry_dir, reg_name);

    String_Builder sb = {0};
    sb_append_cstr(&sb, "# cmk2nob package registry entry\n");
    sb_append_cstr(&sb, "set(");
    sb_append_buf(&sb, dir_key.data, dir_key.count);
    sb_append_cstr(&sb, " \"");
    sb_append_buf(&sb, ctx->current_binary_dir.data, ctx->current_binary_dir.count);
    sb_append_cstr(&sb, "\")\n");
    (void)nob_write_entire_file(nob_temp_sv_to_cstr(reg_file), sb.items, sb.count);
    nob_sb_free(sb);
}

static void eval_export_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_View first = resolve_arg(ctx, args.items[0]);
    if (sv_eq_ci(first, sv_from_cstr("PACKAGE")) && args.count >= 2) {
        String_View package_name = resolve_arg(ctx, args.items[1]);
        export_register_package(ctx, package_name);
        return;
    }

    String_List targets = {0};
    string_list_init(&targets);
    String_View export_set_name = sv_from_cstr("");
    String_View file_path = sv_from_cstr("");
    String_View ns = sv_from_cstr("");
    bool append_mode = false;
    bool mode_targets = false;
    bool mode_export_set = false;

    for (size_t i = 0; i < args.count; i++) {
        String_View key = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(key, sv_from_cstr("TARGETS"))) {
            mode_targets = true;
            i++;
            while (i < args.count) {
                String_View t = resolve_arg(ctx, args.items[i]);
                if (export_keyword(t)) {
                    i--;
                    break;
                }
                if (t.count > 0) string_list_add_unique(&targets, ctx->arena, t);
                i++;
            }
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("EXPORT")) && i + 1 < args.count) {
            mode_export_set = true;
            export_set_name = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("FILE")) && i + 1 < args.count) {
            file_path = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("NAMESPACE")) && i + 1 < args.count) {
            ns = resolve_arg(ctx, args.items[++i]);
            continue;
        }
        if (sv_eq_ci(key, sv_from_cstr("APPEND"))) {
            append_mode = true;
            continue;
        }
    }

    if (!mode_targets && !mode_export_set) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "export",
            "assinatura nao suportada", "use export(TARGETS ...), export(EXPORT ...), ou export(PACKAGE ...)");
        return;
    }

    if (mode_export_set && targets.count == 0) {
        export_collect_targets_from_install_set(ctx, export_set_name, &targets);
        if (targets.count == 0) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "export",
                nob_temp_sprintf("export set vazio ou desconhecido: "SV_Fmt, SV_Arg(export_set_name)),
                "registre targets com install(TARGETS ... EXPORT <nome>) antes de export(EXPORT ...)");
        }
    }

    if (file_path.count == 0) {
        if (mode_export_set && export_set_name.count > 0) {
            String_Builder default_file_sb = {0};
            sb_append_buf(&default_file_sb, export_set_name.data, export_set_name.count);
            sb_append_cstr(&default_file_sb, "Targets.cmake");
            file_path = sv_from_cstr(arena_strndup(ctx->arena, default_file_sb.items, default_file_sb.count));
            nob_sb_free(default_file_sb);
        } else {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "export",
                "FILE ausente para export de targets", "forneca FILE <arquivo> para materializar o export");
            return;
        }
    }
    String_View out_path = path_is_absolute_sv(file_path) ? file_path : path_join_arena(ctx->arena, ctx->current_binary_dir, file_path);
    export_write_targets_file(ctx, out_path, ns,
        mode_export_set ? sv_from_cstr("EXPORT_SET") : sv_from_cstr("TARGETS"),
        export_set_name, &targets, append_mode);
}

static bool find_command_keyword(String_View arg) {
    return sv_eq_ci(arg, sv_from_cstr("NAMES")) ||
           sv_eq_ci(arg, sv_from_cstr("HINTS")) ||
           sv_eq_ci(arg, sv_from_cstr("PATHS")) ||
           sv_eq_ci(arg, sv_from_cstr("PATH_SUFFIXES")) ||
           sv_eq_ci(arg, sv_from_cstr("NO_DEFAULT_PATH")) ||
           sv_eq_ci(arg, sv_from_cstr("REQUIRED")) ||
           sv_eq_ci(arg, sv_from_cstr("QUIET")) ||
           sv_eq_ci(arg, sv_from_cstr("DOC"));
}


// ============================================================================
// SISTEMA DE BUSCA (Find)
// ============================================================================

static void find_split_env_path(Arena *arena, String_View value, String_List *out_dirs) {
    if (!arena || !out_dirs || value.count == 0) return;
    bool semicolon_sep = false;
    for (size_t i = 0; i < value.count; i++) {
        if (value.data[i] == ';') {
            semicolon_sep = true;
            break;
        }
    }
    char sep = semicolon_sep ? ';' : ':';
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool at_sep = (i == value.count) || value.data[i] == sep;
        if (!at_sep) continue;
        if (i > start) {
            String_View d = genex_trim(nob_sv_from_parts(value.data + start, i - start));
            if (d.count > 0) string_list_add_unique(out_dirs, arena, d);
        }
        start = i + 1;
    }
}

static void find_collect_var_paths(Evaluator_Context *ctx, const char *var_name, String_List *out_dirs) {
    if (!ctx || !out_dirs) return;
    String_View val = eval_get_var(ctx, sv_from_cstr(var_name));
    if (val.count == 0) return;
    List_Add_Ud ud_dirs = { .list = out_dirs, .unique = false };
    eval_foreach_semicolon_item(ctx,
                                val,
                                /*trim_ws=*/true,
                                eval_list_add_item,
                                &ud_dirs);
}

static bool path_has_extension(String_View path) {
    String_View base = path_basename_sv(path);
    for (size_t i = 0; i < base.count; i++) {
        if (base.data[i] == '.') return true;
    }
    return false;
}

static void find_collect_program_name_variants(Evaluator_Context *ctx, String_View name, String_List *out_names) {
    if (!ctx || !out_names || name.count == 0) return;
    string_list_add_unique(out_names, ctx->arena, name);
#if defined(_WIN32)
    if (!path_has_extension(name)) {
        string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".exe", SV_Arg(name))));
        string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".cmd", SV_Arg(name))));
        string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".bat", SV_Arg(name))));
        string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".com", SV_Arg(name))));
    }
#endif
}

static void find_collect_library_name_variants(Evaluator_Context *ctx, String_View name, String_List *out_names) {
    if (!ctx || !out_names || name.count == 0) return;
    string_list_add_unique(out_names, ctx->arena, name);
    if (path_has_extension(name)) return;
#if defined(_WIN32)
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".lib", SV_Arg(name))));
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".lib", SV_Arg(name))));
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf(SV_Fmt ".dll", SV_Arg(name))));
#elif defined(__APPLE__)
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".dylib", SV_Arg(name))));
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".a", SV_Arg(name))));
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".so", SV_Arg(name))));
#else
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".so", SV_Arg(name))));
    string_list_add_unique(out_names, ctx->arena, sv_from_cstr(nob_temp_sprintf("lib" SV_Fmt ".a", SV_Arg(name))));
#endif
}

static bool find_search_candidates(Evaluator_Context *ctx, String_List *dirs, String_List *suffixes, String_List *names, String_View *out_path) {
    if (!ctx || !dirs || !names || !out_path) return false;
    for (size_t n = 0; n < names->count; n++) {
        String_View name = names->items[n];
        if (path_is_absolute_sv(name) && nob_file_exists(nob_temp_sv_to_cstr(name))) {
            *out_path = path_make_absolute_arena(ctx->arena, name);
            return true;
        }
    }

    bool no_suffixes = !suffixes || suffixes->count == 0;
    for (size_t d = 0; d < dirs->count; d++) {
        String_View dir = dirs->items[d];
        if (no_suffixes) {
            for (size_t n = 0; n < names->count; n++) {
                String_View candidate = path_join_arena(ctx->arena, dir, names->items[n]);
                if (nob_file_exists(nob_temp_sv_to_cstr(candidate))) {
                    *out_path = path_make_absolute_arena(ctx->arena, candidate);
                    return true;
                }
            }
        } else {
            for (size_t s = 0; s < suffixes->count; s++) {
                String_View base = path_join_arena(ctx->arena, dir, suffixes->items[s]);
                for (size_t n = 0; n < names->count; n++) {
                    String_View candidate = path_join_arena(ctx->arena, base, names->items[n]);
                    if (nob_file_exists(nob_temp_sv_to_cstr(candidate))) {
                        *out_path = path_make_absolute_arena(ctx->arena, candidate);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

typedef enum Find_Command_Kind {
    FIND_COMMAND_PROGRAM = 0,
    FIND_COMMAND_LIBRARY,
    FIND_COMMAND_FILE,
    FIND_COMMAND_PATH,
} Find_Command_Kind;

typedef struct Find_Command_Params {
    String_View out_var;
    String_List names;
    String_List hints;
    String_List paths;
    String_List suffixes;
    bool no_default_path;
    bool required;
    bool quiet;
} Find_Command_Params;

static void find_command_params_init(Find_Command_Params *params) {
    if (!params) return;
    memset(params, 0, sizeof(*params));
    string_list_init(&params->names);
    string_list_init(&params->hints);
    string_list_init(&params->paths);
    string_list_init(&params->suffixes);
}

static bool find_parse_common_args(Evaluator_Context *ctx, Args args, Find_Command_Params *params) {
    if (!ctx || !params || args.count < 2) return false;

    params->out_var = resolve_arg(ctx, args.items[0]);
    size_t i = 1;
    bool got_names = false;
    while (i < args.count) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("NAMES"))) {
            got_names = true;
            i++;
            while (i < args.count) {
                String_View v = resolve_arg(ctx, args.items[i]);
                if (find_command_keyword(v)) break;
                string_list_add_unique(&params->names, ctx->arena, v);
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("HINTS"))) {
            i++;
            while (i < args.count) {
                String_View v = resolve_arg(ctx, args.items[i]);
                if (find_command_keyword(v)) break;
                string_list_add_unique(&params->hints, ctx->arena, v);
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("PATHS"))) {
            i++;
            while (i < args.count) {
                String_View v = resolve_arg(ctx, args.items[i]);
                if (find_command_keyword(v)) break;
                string_list_add_unique(&params->paths, ctx->arena, v);
                i++;
            }
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("PATH_SUFFIXES"))) {
            i++;
            while (i < args.count) {
                String_View v = resolve_arg(ctx, args.items[i]);
                if (find_command_keyword(v)) break;
                string_list_add_unique(&params->suffixes, ctx->arena, v);
                i++;
            }
            continue;
        }

        if (sv_eq_ci(tok, sv_from_cstr("NO_DEFAULT_PATH"))) params->no_default_path = true;
        else if (sv_eq_ci(tok, sv_from_cstr("REQUIRED"))) params->required = true;
        else if (sv_eq_ci(tok, sv_from_cstr("QUIET"))) params->quiet = true;
        else if (sv_eq_ci(tok, sv_from_cstr("DOC"))) i++;
        else if (!got_names) {
            got_names = true;
            string_list_add_unique(&params->names, ctx->arena, tok);
        }
        i++;
    }

    return true;
}

static void find_collect_name_variants(Evaluator_Context *ctx, Find_Command_Kind kind, const String_List *names, String_List *variants) {
    if (!ctx || !names || !variants) return;
    for (size_t n = 0; n < names->count; n++) {
        if (kind == FIND_COMMAND_PROGRAM) {
            find_collect_program_name_variants(ctx, names->items[n], variants);
        } else if (kind == FIND_COMMAND_LIBRARY) {
            find_collect_library_name_variants(ctx, names->items[n], variants);
        } else {
            string_list_add_unique(variants, ctx->arena, names->items[n]);
        }
    }
}

static void find_collect_default_search_dirs(Evaluator_Context *ctx, Find_Command_Kind kind, String_List *search_dirs) {
    if (!ctx || !search_dirs) return;

    if (kind == FIND_COMMAND_PROGRAM) {
        find_collect_var_paths(ctx, "CMAKE_PROGRAM_PATH", search_dirs);
        const char *env_path = getenv("PATH");
        if (env_path) find_split_env_path(ctx->arena, sv_from_cstr(env_path), search_dirs);
#if defined(_WIN32)
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("C:/Windows/System32"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("C:/Windows"));
#else
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/bin"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/local/bin"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/bin"));
#endif
        return;
    }

    find_collect_var_paths(ctx, "CMAKE_LIBRARY_PATH", search_dirs);
    String_List prefixes = {0};
    string_list_init(&prefixes);
    find_collect_var_paths(ctx, "CMAKE_PREFIX_PATH", &prefixes);
    if (kind == FIND_COMMAND_LIBRARY) {
        for (size_t k = 0; k < prefixes.count; k++) {
            string_list_add_unique(search_dirs, ctx->arena, path_join_arena(ctx->arena, prefixes.items[k], sv_from_cstr("lib")));
            string_list_add_unique(search_dirs, ctx->arena, path_join_arena(ctx->arena, prefixes.items[k], sv_from_cstr("lib64")));
        }
#if defined(_WIN32)
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("C:/Windows/System32"));
#else
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/lib"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/local/lib"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/lib"));
#endif
        return;
    }

    if (kind == FIND_COMMAND_FILE || kind == FIND_COMMAND_PATH) {
        find_collect_var_paths(ctx, "CMAKE_INCLUDE_PATH", search_dirs);
        for (size_t k = 0; k < prefixes.count; k++) {
            string_list_add_unique(search_dirs, ctx->arena, path_join_arena(ctx->arena, prefixes.items[k], sv_from_cstr("include")));
            string_list_add_unique(search_dirs, ctx->arena, path_join_arena(ctx->arena, prefixes.items[k], sv_from_cstr("share")));
        }
        string_list_add_unique(search_dirs, ctx->arena, ctx->current_list_dir);
        string_list_add_unique(search_dirs, ctx->arena, ctx->current_source_dir);
        string_list_add_unique(search_dirs, ctx->arena, ctx->current_binary_dir);
#if defined(_WIN32)
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("C:/"));
#else
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/include"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/usr/local/include"));
        string_list_add_unique(search_dirs, ctx->arena, sv_from_cstr("/etc"));
#endif
    }
}

static void find_report_required_failure(Evaluator_Context *ctx, Find_Command_Kind kind, String_View out_var, bool quiet) {
    if (!ctx) return;
    if (kind == FIND_COMMAND_PROGRAM) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_program",
            nob_temp_sprintf("programa nao encontrado para %s", nob_temp_sv_to_cstr(out_var)),
            quiet ? "" : "adicione PATHS/HINTS ou ajuste PATH");
        return;
    }
    if (kind == FIND_COMMAND_LIBRARY) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_library",
            nob_temp_sprintf("biblioteca nao encontrada para %s", nob_temp_sv_to_cstr(out_var)),
            quiet ? "" : "adicione PATHS/HINTS ou ajuste CMAKE_LIBRARY_PATH");
        return;
    }
    if (kind == FIND_COMMAND_FILE) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_file",
            nob_temp_sprintf("arquivo nao encontrado para %s", nob_temp_sv_to_cstr(out_var)),
            quiet ? "" : "adicione PATHS/HINTS ou ajuste CMAKE_INCLUDE_PATH");
        return;
    }
    diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "find_path",
        nob_temp_sprintf("diretorio nao encontrado para %s", nob_temp_sv_to_cstr(out_var)),
        quiet ? "" : "adicione PATHS/HINTS ou ajuste CMAKE_INCLUDE_PATH");
}

static bool find_search_path_candidates(Evaluator_Context *ctx, String_List *dirs, String_List *suffixes, String_List *names, String_View *out_dir) {
    if (!ctx || !dirs || !names || !out_dir) return false;

    bool no_suffixes = !suffixes || suffixes->count == 0;
    for (size_t d = 0; d < dirs->count; d++) {
        String_View dir = dirs->items[d];
        if (no_suffixes) {
            for (size_t n = 0; n < names->count; n++) {
                String_View candidate = path_join_arena(ctx->arena, dir, names->items[n]);
                if (nob_file_exists(nob_temp_sv_to_cstr(candidate))) {
                    *out_dir = path_make_absolute_arena(ctx->arena, dir);
                    return true;
                }
            }
        } else {
            for (size_t s = 0; s < suffixes->count; s++) {
                String_View base = path_join_arena(ctx->arena, dir, suffixes->items[s]);
                for (size_t n = 0; n < names->count; n++) {
                    String_View candidate = path_join_arena(ctx->arena, base, names->items[n]);
                    if (nob_file_exists(nob_temp_sv_to_cstr(candidate))) {
                        *out_dir = path_make_absolute_arena(ctx->arena, base);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static void eval_find_common_command(Evaluator_Context *ctx, Args args, Find_Command_Kind kind) {
    if (!ctx) return;

    Find_Command_Params params;
    find_command_params_init(&params);
    if (!find_parse_common_args(ctx, args, &params)) return;

    String_List variants = {0};
    String_List search_dirs = {0};
    string_list_init(&variants);
    string_list_init(&search_dirs);

    find_collect_name_variants(ctx, kind, &params.names, &variants);
    for (size_t h = 0; h < params.hints.count; h++) string_list_add_unique(&search_dirs, ctx->arena, params.hints.items[h]);
    for (size_t p = 0; p < params.paths.count; p++) string_list_add_unique(&search_dirs, ctx->arena, params.paths.items[p]);
    if (!params.no_default_path) {
        find_collect_default_search_dirs(ctx, kind, &search_dirs);
    }

    String_View found = sv_from_cstr("");
    bool ok = false;
    if (kind == FIND_COMMAND_PATH) {
        ok = find_search_path_candidates(ctx, &search_dirs, &params.suffixes, &variants, &found);
    } else {
        ok = find_search_candidates(ctx, &search_dirs, &params.suffixes, &variants, &found);
    }
    if (ok) {
        eval_set_var(ctx, params.out_var, found, false, false);
        eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(params.out_var))), sv_from_cstr("TRUE"), false, false);
        return;
    }

    String_View notfound = sv_from_cstr(nob_temp_sprintf("%s-NOTFOUND", nob_temp_sv_to_cstr(params.out_var)));
    eval_set_var(ctx, params.out_var, notfound, false, false);
    eval_set_var(ctx, sv_from_cstr(nob_temp_sprintf("%s_FOUND", nob_temp_sv_to_cstr(params.out_var))), sv_from_cstr("FALSE"), false, false);
    if (params.required) {
        find_report_required_failure(ctx, kind, params.out_var, params.quiet);
    }
}

static void eval_find_program_command(Evaluator_Context *ctx, Args args) {
    eval_find_common_command(ctx, args, FIND_COMMAND_PROGRAM);
}

static void eval_find_library_command(Evaluator_Context *ctx, Args args) {
    eval_find_common_command(ctx, args, FIND_COMMAND_LIBRARY);
}

static void eval_find_file_command(Evaluator_Context *ctx, Args args) {
    eval_find_common_command(ctx, args, FIND_COMMAND_FILE);
}

static void eval_find_path_command(Evaluator_Context *ctx, Args args) {
    eval_find_common_command(ctx, args, FIND_COMMAND_PATH);
}

static bool include_try_candidate(Evaluator_Context *ctx, String_View base_dir, String_View requested, String_View *out_final_path) {
    String_View candidate = path_join_arena(ctx->arena, base_dir, requested);
    const char *candidate_c = nob_temp_sv_to_cstr(candidate);
    if (nob_file_exists(candidate_c)) {
        *out_final_path = candidate;
        return true;
    }

    String_View with_ext = sv_from_cstr(nob_temp_sprintf("%s.cmake", candidate_c));
    if (nob_file_exists(nob_temp_sv_to_cstr(with_ext))) {
        *out_final_path = sv_from_cstr(arena_strdup(ctx->arena, nob_temp_sv_to_cstr(with_ext)));
        return true;
    }
    return false;
}

static String_View include_module_name_from_requested(Evaluator_Context *ctx, String_View requested) {
    (void)ctx;
    String_View name = path_basename_sv(requested);
    if (nob_sv_end_with(name, ".cmake") && name.count > 6) {
        name = nob_sv_from_parts(name.data, name.count - 6);
    }
    return name;
}

static void eval_include_ctest_module(Evaluator_Context *ctx) {
    if (!ctx || !ctx->model) return;

    if (!eval_has_var(ctx, sv_from_cstr("BUILD_TESTING"))) {
        eval_set_var(ctx, sv_from_cstr("BUILD_TESTING"), sv_from_cstr("ON"), false, false);
    }

    String_View build_testing = eval_get_var(ctx, sv_from_cstr("BUILD_TESTING"));
    bool testing_enabled = !cmake_string_is_false(build_testing);
    if (testing_enabled) {
        eval_enable_testing_command(ctx, (Args){0});
    } else {
        eval_set_var(ctx, sv_from_cstr("CMAKE_TESTING_ENABLED"), sv_from_cstr("OFF"), false, false);
    }

    String_View project_name = eval_get_var(ctx, sv_from_cstr("PROJECT_NAME"));
    if (project_name.count == 0) {
        project_name = eval_get_var(ctx, sv_from_cstr("CMAKE_PROJECT_NAME"));
    }
    if (project_name.count == 0) {
        project_name = sv_from_cstr("Project");
    }

    if (!eval_has_var(ctx, sv_from_cstr("CTEST_PROJECT_NAME"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_PROJECT_NAME"), project_name, false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"), ctx->current_source_dir, false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"), ctx->current_binary_dir, false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_COMMAND"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_COMMAND"), sv_from_cstr("ctest"), false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_NIGHTLY_START_TIME"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_NIGHTLY_START_TIME"), sv_from_cstr("00:00:00 UTC"), false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_DROP_METHOD"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_DROP_METHOD"), sv_from_cstr("http"), false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_DROP_SITE"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_DROP_SITE"), sv_from_cstr(""), false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_DROP_LOCATION"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_DROP_LOCATION"), sv_from_cstr(""), false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_DROP_SITE_CDASH"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_DROP_SITE_CDASH"), sv_from_cstr("TRUE"), false, false);
    }

    String_View timeout = eval_get_var(ctx, sv_from_cstr("CTEST_TEST_TIMEOUT"));
    if (timeout.count == 0) timeout = sv_from_cstr("1500");
    if (!eval_has_var(ctx, sv_from_cstr("DART_TESTING_TIMEOUT"))) {
        eval_set_var(ctx, sv_from_cstr("DART_TESTING_TIMEOUT"), timeout, false, false);
    }
    eval_set_var(ctx, sv_from_cstr("CMAKE_CTEST_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
}

static void eval_include_ctest_script_mode_module(Evaluator_Context *ctx) {
    if (!ctx || !ctx->model) return;
    eval_set_var(ctx, sv_from_cstr("CTEST_SCRIPT_MODE"), sv_from_cstr("ON"), false, false);
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_SOURCE_DIRECTORY"), ctx->current_source_dir, false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_BINARY_DIRECTORY"), ctx->current_binary_dir, false, false);
    }
    if (!eval_has_var(ctx, sv_from_cstr("CTEST_COMMAND"))) {
        eval_set_var(ctx, sv_from_cstr("CTEST_COMMAND"), sv_from_cstr("ctest"), false, false);
    }
}

static void eval_include_ctest_use_launchers_module(Evaluator_Context *ctx) {
    if (!ctx || !ctx->model) return;

    String_View use_launchers = eval_get_var(ctx, sv_from_cstr("CTEST_USE_LAUNCHERS"));
    if (use_launchers.count == 0) {
        use_launchers = sv_from_cstr("ON");
        eval_set_var(ctx, sv_from_cstr("CTEST_USE_LAUNCHERS"), use_launchers, false, false);
    }

    bool enabled = !cmake_string_is_false(use_launchers);
    String_View launcher_compile = enabled ? sv_from_cstr("${CTEST_COMMAND};--launch;compile") : sv_from_cstr("");
    String_View launcher_link = enabled ? sv_from_cstr("${CTEST_COMMAND};--launch;link") : sv_from_cstr("");
    String_View launcher_custom = enabled ? sv_from_cstr("${CTEST_COMMAND};--launch;custom") : sv_from_cstr("");

    eval_set_var(ctx, sv_from_cstr("RULE_LAUNCH_COMPILE"), launcher_compile, false, false);
    eval_set_var(ctx, sv_from_cstr("RULE_LAUNCH_LINK"), launcher_link, false, false);
    eval_set_var(ctx, sv_from_cstr("RULE_LAUNCH_CUSTOM"), launcher_custom, false, false);

    build_model_set_cache_variable(ctx->model, internal_global_property_key(ctx, sv_from_cstr("RULE_LAUNCH_COMPILE")),
                                   launcher_compile, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, internal_global_property_key(ctx, sv_from_cstr("RULE_LAUNCH_LINK")),
                                   launcher_link, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    build_model_set_cache_variable(ctx->model, internal_global_property_key(ctx, sv_from_cstr("RULE_LAUNCH_CUSTOM")),
                                   launcher_custom, sv_from_cstr("INTERNAL"), sv_from_cstr(""));
    eval_set_var(ctx, sv_from_cstr("CMAKE_CTEST_USE_LAUNCHERS_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
}

static bool include_handle_builtin_module(Evaluator_Context *ctx, String_View requested) {
    String_View module = include_module_name_from_requested(ctx, requested);

    if (sv_eq_ci(module, sv_from_cstr("CTest"))) {
        eval_include_ctest_module(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CTestScriptMode"))) {
        eval_include_ctest_script_mode_module(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CTestUseLaunchers"))) {
        eval_include_ctest_use_launchers_module(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CTestCoverageCollectGCOV"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CTEST_COVERAGE_COLLECT_GCOV_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        if (!eval_has_var(ctx, sv_from_cstr("CTEST_COVERAGE_COMMAND"))) {
            eval_set_var(ctx, sv_from_cstr("CTEST_COVERAGE_COMMAND"), sv_from_cstr("gcov"), false, false);
        }
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackArchive"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_ARCHIVE_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        if (!eval_has_var(ctx, sv_from_cstr("CPACK_GENERATOR"))) {
            eval_set_var(ctx, sv_from_cstr("CPACK_GENERATOR"), sv_from_cstr("TGZ"), false, false);
        }
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackDeb"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_DEB_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("DEB"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackRPM"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_RPM_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("RPM"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackNSIS"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_NSIS_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("NSIS"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackWIX"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_WIX_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("WIX"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackDMG"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_DMG_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("DragNDrop"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackBundle"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_BUNDLE_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("Bundle"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackProductBuild"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_PRODUCTBUILD_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("productbuild"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackIFW"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_IFW_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("IFW"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackIFWConfigureFile"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_IFW_CONFIGURE_FILE_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackNuGet"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_NUGET_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("NuGet"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackFreeBSD"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_FREEBSD_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("FreeBSD"));
        cpack_sync_common_metadata(ctx);
        return true;
    }
    if (sv_eq_ci(module, sv_from_cstr("CPackCygwin"))) {
        eval_set_var(ctx, sv_from_cstr("CMAKE_CPACK_CYGWIN_MODULE_INITIALIZED"), sv_from_cstr("ON"), false, false);
        cpack_append_generator(ctx, sv_from_cstr("Cygwin"));
        cpack_sync_common_metadata(ctx);
        return true;
    }

    if (sv_eq_ci(module, sv_from_cstr("CMakeDependentOption")) ||
        sv_eq_ci(module, sv_from_cstr("CheckCCompilerFlag")) ||
        sv_eq_ci(module, sv_from_cstr("CMakePushCheckState")) ||
        sv_eq_ci(module, sv_from_cstr("CheckFunctionExists")) ||
        sv_eq_ci(module, sv_from_cstr("CheckIncludeFile")) ||
        sv_eq_ci(module, sv_from_cstr("CheckIncludeFiles")) ||
        sv_eq_ci(module, sv_from_cstr("CheckLibraryExists")) ||
        sv_eq_ci(module, sv_from_cstr("CheckSymbolExists")) ||
        sv_eq_ci(module, sv_from_cstr("CheckTypeSize")) ||
        sv_eq_ci(module, sv_from_cstr("CheckCSourceCompiles")) ||
        sv_eq_ci(module, sv_from_cstr("CheckStructHasMember")) ||
        sv_eq_ci(module, sv_from_cstr("CheckCSourceRuns")) ||
        sv_eq_ci(module, sv_from_cstr("CMakePackageConfigHelpers")) ||
        sv_eq_ci(module, sv_from_cstr("CPack"))) {
        if (sv_eq_ci(module, sv_from_cstr("CPack"))) {
            if (!eval_has_var(ctx, sv_from_cstr("CPACK_GENERATOR"))) {
                eval_set_var(ctx, sv_from_cstr("CPACK_GENERATOR"), sv_from_cstr("TGZ"), false, false);
            }
            cpack_sync_common_metadata(ctx);
        }
        return true;
    }

    if (sv_eq_ci(module, sv_from_cstr("GNUInstallDirs"))) {
        if (!eval_has_var(ctx, sv_from_cstr("CMAKE_INSTALL_BINDIR"))) {
            eval_set_var(ctx, sv_from_cstr("CMAKE_INSTALL_BINDIR"), sv_from_cstr("bin"), false, false);
        }
        if (!eval_has_var(ctx, sv_from_cstr("CMAKE_INSTALL_LIBDIR"))) {
            eval_set_var(ctx, sv_from_cstr("CMAKE_INSTALL_LIBDIR"), sv_from_cstr("lib"), false, false);
        }
        if (!eval_has_var(ctx, sv_from_cstr("CMAKE_INSTALL_INCLUDEDIR"))) {
            eval_set_var(ctx, sv_from_cstr("CMAKE_INSTALL_INCLUDEDIR"), sv_from_cstr("include"), false, false);
        }
        if (!eval_has_var(ctx, sv_from_cstr("CMAKE_INSTALL_DATADIR"))) {
            eval_set_var(ctx, sv_from_cstr("CMAKE_INSTALL_DATADIR"), sv_from_cstr("share"), false, false);
        }
        return true;
    }

    return false;
}

static void eval_include_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View requested = resolve_arg(ctx, args.items[0]);
    bool optional = false;
    for (size_t i = 1; i < args.count; i++) {
        String_View arg = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(arg, sv_from_cstr("OPTIONAL"))) optional = true;
    }

    String_View final_path = {0};
    bool found = false;

    if (path_is_absolute_sv(requested)) {
        const char *req_c = nob_temp_sv_to_cstr(requested);
        if (nob_file_exists(req_c)) {
            final_path = requested;
            found = true;
        } else {
            String_View with_ext = sv_from_cstr(nob_temp_sprintf("%s.cmake", req_c));
            if (nob_file_exists(nob_temp_sv_to_cstr(with_ext))) {
                final_path = sv_from_cstr(arena_strdup(ctx->arena, nob_temp_sv_to_cstr(with_ext)));
                found = true;
            }
        }
    } else {
        found = include_try_candidate(ctx, ctx->current_list_dir, requested, &final_path);

        if (!found) {
            String_View module_paths = eval_get_var(ctx, sv_from_cstr("CMAKE_MODULE_PATH"));
            size_t start = 0;
            for (size_t i = 0; i <= module_paths.count; i++) {
                bool is_sep = (i == module_paths.count) || (module_paths.data[i] == ';');
                if (!is_sep) continue;
                if (i > start) {
                    String_View module_dir = nob_sv_from_parts(module_paths.data + start, i - start);
                    if (include_try_candidate(ctx, module_dir, requested, &final_path)) {
                        found = true;
                        break;
                    }
                }
                start = i + 1;
            }
        }

        if (!found) {
            String_View cmake_root = eval_get_var(ctx, sv_from_cstr("CMAKE_ROOT"));
            if (cmake_root.count > 0) {
                String_View modules_dir = path_join_arena(ctx->arena, cmake_root, sv_from_cstr("Modules"));
                if (include_try_candidate(ctx, modules_dir, requested, &final_path)) {
                    found = true;
                } else {
                    found = include_try_candidate(ctx, cmake_root, requested, &final_path);
                }
            }
        }
    }

    if (!found) {
        if (!path_has_separator(requested) && include_handle_builtin_module(ctx, requested)) {
            return;
        }
        if (optional || !path_has_separator(requested)) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "include",
                nob_temp_sprintf("arquivo de include nao encontrado: "SV_Fmt, SV_Arg(requested)),
                "ignorando include ausente (compatibilidade)");
            return;
        }
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "include",
            nob_temp_sprintf("arquivo de include nao encontrado: "SV_Fmt, SV_Arg(requested)),
            "forneca caminho valido ou use OPTIONAL");
        return;
    }

    String_View normalized = path_make_absolute_arena(ctx->arena, final_path);
    if (eval_include_stack_contains(ctx, normalized)) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "include",
            nob_temp_sprintf("include ciclico detectado e ignorado: "SV_Fmt, SV_Arg(normalized)),
            "remova include direto/indireto do mesmo arquivo");
        return;
    }
    if (!eval_include_stack_push(ctx, normalized)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "include",
            nob_temp_sprintf("falha ao registrar include: "SV_Fmt, SV_Arg(normalized)),
            "reduza profundidade de includes ou aumente memoria");
        return;
    }

    String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(normalized));
    if (!content.data) {
        eval_include_stack_pop(ctx);
        return;
    }
    Token_List tokens = {0};
    if (!arena_tokenize(ctx->arena, content, &tokens)) {
        eval_include_stack_pop(ctx);
        return;
    }
    Ast_Root root = parse_tokens(ctx->arena, tokens);

    String_View old_list = ctx->current_list_dir;
    ctx->current_list_dir = path_parent_dir_arena(ctx->arena, normalized);
    for (size_t i = 0; i < root.count; i++) eval_node(ctx, root.items[i]);
    ctx->current_list_dir = old_list;
    eval_include_stack_pop(ctx);
}

static String_View configure_expand_variables(Evaluator_Context *ctx, String_View input, bool at_only) {
    String_Builder out = {0};
    for (size_t i = 0; i < input.count; i++) {
        if (input.data[i] == '@') {
            size_t j = i + 1;
            while (j < input.count && input.data[j] != '@') j++;
            if (j < input.count && j > i + 1) {
                String_View key = nob_sv_from_parts(input.data + i + 1, j - (i + 1));
                String_View val = eval_get_var(ctx, key);
                sb_append_buf(&out, val.data, val.count);
                i = j;
                continue;
            }
        }
        if (!at_only && input.data[i] == '$' && i + 1 < input.count && input.data[i + 1] == '{') {
            size_t j = i + 2;
            while (j < input.count && input.data[j] != '}') j++;
            if (j < input.count) {
                String_View key = nob_sv_from_parts(input.data + i + 2, j - (i + 2));
                String_View val = eval_get_var(ctx, key);
                sb_append_buf(&out, val.data, val.count);
                i = j;
                continue;
            }
        }
        sb_append(&out, input.data[i]);
    }
    String_View result = sv_from_cstr(arena_strndup(ctx->arena, out.items, out.count));
    nob_sb_free(out);
    return result;
}

static String_View try_resolve_cmake_template(Evaluator_Context *ctx, String_View requested) {
    String_View name = path_basename_sv(requested);
    if (name.count == 0) return sv_from_cstr("");

    String_View cmake_root = eval_get_var(ctx, sv_from_cstr("CMAKE_ROOT"));
    if (cmake_root.count > 0) {
        String_View candidate = path_join_arena(ctx->arena, cmake_root, sv_from_cstr("Templates"));
        candidate = path_join_arena(ctx->arena, candidate, name);
        if (nob_file_exists(nob_temp_sv_to_cstr(candidate))) return candidate;
    }
    return sv_from_cstr("");
}

static void eval_configure_file_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 2) return;
    String_View input_arg = resolve_arg(ctx, args.items[0]);
    String_View input_path = path_join_arena(ctx->arena, ctx->current_list_dir, input_arg);
    String_View output_path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[1]));

    bool copy_only = false;
    bool at_only = false;
    for (size_t i = 2; i < args.count; i++) {
        String_View a = resolve_arg(ctx, args.items[i]);
        if (nob_sv_eq(a, sv_from_cstr("COPYONLY"))) copy_only = true;
        if (nob_sv_eq(a, sv_from_cstr("@ONLY"))) at_only = true;
    }

    if (path_is_absolute_sv(input_arg)) input_path = input_arg;

    String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(input_path));
    if (!content.data) {
        String_View fallback = try_resolve_cmake_template(ctx, input_arg);
        if (fallback.count > 0) {
            input_path = fallback;
            content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(input_path));
        }
    }

    if (!content.data) {
        String_View base = path_basename_sv(input_arg);
        bool cpack_template = nob_sv_eq(base, sv_from_cstr("CPackConfig.cmake.in")) ||
                              nob_sv_eq(base, sv_from_cstr("CPackSourceConfig.cmake.in"));
        if (cpack_template) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "configure_file",
                nob_temp_sprintf("template ausente para CPack: "SV_Fmt, SV_Arg(input_path)),
                "gerando arquivo vazio por compatibilidade");
            content = sv_from_cstr("# generated: missing CPack template\n");
        } else {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "configure_file",
                nob_temp_sprintf("falha ao ler arquivo: "SV_Fmt, SV_Arg(input_path)),
                "verifique se o arquivo existe");
            return;
        }
    }

    String_View rendered = copy_only ? content : configure_expand_variables(ctx, content, at_only);
    if (!ensure_parent_dirs_for_path(ctx->arena, output_path)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "configure_file",
            nob_temp_sprintf("falha ao preparar diretorio para: "SV_Fmt, SV_Arg(output_path)),
            "verifique permissao de escrita");
        return;
    }
    if (!nob_write_entire_file(nob_temp_sv_to_cstr(output_path), rendered.data, rendered.count)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "configure_file",
            nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(output_path)),
            "verifique permissao de escrita");
    }
}

static bool simple_glob_match(String_View pattern, String_View path) {
    // Pratico: suporta '*' e compara por sufixo/prefixo simples.
    const char *star = memchr(pattern.data, '*', pattern.count);
    if (!star) return nob_sv_eq(pattern, path);
    size_t left = (size_t)(star - pattern.data);
    size_t right = pattern.count - left - 1;
    if (path.count < left + right) return false;
    if (left > 0 && memcmp(pattern.data, path.data, left) != 0) return false;
    if (right > 0 && memcmp(pattern.data + pattern.count - right, path.data + path.count - right, right) != 0) return false;
    return true;
}

static bool file_path_is_dot_or_dotdot(String_View p) {
    return nob_sv_eq(p, sv_from_cstr(".")) || nob_sv_eq(p, sv_from_cstr(".."));
}

static bool file_delete_path_recursive(Evaluator_Context *ctx, String_View path) {
    if (!ctx) return false;
    return sys_delete_path_recursive(ctx->arena, path);
}

static bool file_copy_entry_to_destination(Evaluator_Context *ctx, String_View src, String_View destination) {
    if (!ctx) return false;
    return sys_copy_entry_to_destination(ctx->arena, src, destination);
}

static bool file_collect_glob_recursive(Evaluator_Context *ctx,
                                        String_View base_dir,
                                        String_View rel_prefix,
                                        String_View pattern,
                                        bool list_directories,
                                        String_View relative_base,
                                        String_Builder *list,
                                        bool *first) {
    if (!ctx || !list || !first) return false;
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(nob_temp_sv_to_cstr(base_dir), &children)) return false;
    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (file_path_is_dot_or_dotdot(name)) continue;

        String_View child_abs = path_join_arena(ctx->arena, base_dir, name);
        String_View child_rel = rel_prefix.count > 0 ? path_join_arena(ctx->arena, rel_prefix, name) : name;
        Nob_File_Type type = nob_get_file_type(nob_temp_sv_to_cstr(child_abs));

        bool can_emit = (type == NOB_FILE_REGULAR || type == NOB_FILE_SYMLINK || type == NOB_FILE_OTHER) ||
                        (list_directories && type == NOB_FILE_DIRECTORY);
        if (can_emit && simple_glob_match(pattern, child_rel)) {
            String_View emit = child_abs;
            if (relative_base.count > 0) {
                emit = cmake_path_relativize(ctx, child_abs, relative_base);
            }
            if (!*first) sb_append(list, ';');
            sb_append_buf(list, emit.data, emit.count);
            *first = false;
        }
        if (type == NOB_FILE_DIRECTORY) {
            (void)file_collect_glob_recursive(ctx, child_abs, child_rel, pattern, list_directories, relative_base, list, first);
        }
    }
    nob_da_free(children);
    return true;
}

static void eval_file_command(Evaluator_Context *ctx, Args args) {
    if (args.count < 1) return;
    String_View mode = resolve_arg(ctx, args.items[0]);

    if (nob_sv_eq(mode, sv_from_cstr("READ")) && args.count >= 3) {
        String_View path = path_join_arena(ctx->arena, ctx->current_list_dir, resolve_arg(ctx, args.items[1]));
        String_View out_var = resolve_arg(ctx, args.items[2]);
        String_View content = arena_read_file(ctx->arena, nob_temp_sv_to_cstr(path));
        if (content.data) eval_set_var(ctx, out_var, content, false, false);
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("WRITE")) && args.count >= 3) {
        String_View path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[1]));
        String_Builder b = {0};
        for (size_t i = 2; i < args.count; i++) {
            if (i > 2) sb_append(&b, ' ');
            String_View v = resolve_arg(ctx, args.items[i]);
            sb_append_buf(&b, v.data, v.count);
        }
        if (!ensure_parent_dirs_for_path(ctx->arena, path)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                nob_temp_sprintf("falha ao preparar diretorio para escrita: "SV_Fmt, SV_Arg(path)),
                "verifique permissao de escrita e caminho de destino");
            nob_sb_free(b);
            return;
        }
        if (!nob_write_entire_file(nob_temp_sv_to_cstr(path), b.items, b.count)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                nob_temp_sprintf("falha ao escrever arquivo: "SV_Fmt, SV_Arg(path)),
                "verifique permissao de escrita e caminho de destino");
        }
        nob_sb_free(b);
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("APPEND")) && args.count >= 3) {
        String_View path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[1]));
        Nob_String_Builder existing = {0};
        if (nob_file_exists(nob_temp_sv_to_cstr(path))) {
            if (!nob_read_entire_file(nob_temp_sv_to_cstr(path), &existing)) {
                diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                    nob_temp_sprintf("falha ao ler arquivo para append: "SV_Fmt, SV_Arg(path)),
                    "verifique permissao de leitura e integridade do arquivo");
                return;
            }
        }
        String_Builder b = {0};
        sb_append_buf(&b, existing.items, existing.count);
        for (size_t i = 2; i < args.count; i++) {
            String_View v = resolve_arg(ctx, args.items[i]);
            sb_append_buf(&b, v.data, v.count);
        }
        if (!ensure_parent_dirs_for_path(ctx->arena, path)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                nob_temp_sprintf("falha ao preparar diretorio para append: "SV_Fmt, SV_Arg(path)),
                "verifique permissao de escrita e caminho de destino");
            nob_sb_free(existing);
            nob_sb_free(b);
            return;
        }
        if (!nob_write_entire_file(nob_temp_sv_to_cstr(path), b.items, b.count)) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                nob_temp_sprintf("falha ao gravar append em arquivo: "SV_Fmt, SV_Arg(path)),
                "verifique permissao de escrita e caminho de destino");
        }
        nob_sb_free(existing);
        nob_sb_free(b);
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("MAKE_DIRECTORY")) && args.count >= 2) {
        for (size_t i = 1; i < args.count; i++) {
            String_View d = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[i]));
            if (!nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(d))) {
                diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "file",
                    nob_temp_sprintf("falha ao criar diretorio: "SV_Fmt, SV_Arg(d)),
                    "verifique permissao de escrita no diretorio pai");
            }
        }
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("GLOB")) && args.count >= 3) {
        String_View out_var = resolve_arg(ctx, args.items[1]);
        String_Builder list = {0};
        bool first = true;
        for (size_t p = 2; p < args.count; p++) {
            String_View pat = path_join_arena(ctx->arena, ctx->current_list_dir, resolve_arg(ctx, args.items[p]));
            String_View base = pat;
            size_t cut = pat.count;
            while (cut > 0) {
                char c = pat.data[cut - 1];
                if (c == '/' || c == '\\') break;
                cut--;
            }
            if (cut > 0) base = nob_sv_from_parts(pat.data, cut - 1);
            Nob_File_Paths children = {0};
            if (!nob_read_entire_dir(nob_temp_sv_to_cstr(base), &children)) continue;
            for (size_t i = 0; i < children.count; i++) {
                String_View child = sv_from_cstr(children.items[i]);
                if (simple_glob_match(pat, child)) {
                    if (!first) sb_append(&list, ';');
                    sb_append_buf(&list, child.data, child.count);
                    first = false;
                }
            }
            nob_da_free(children);
        }
        eval_set_var(ctx, out_var, sb_to_sv(list), false, false);
        nob_sb_free(list);
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("GLOB_RECURSE")) && args.count >= 3) {
        String_View out_var = resolve_arg(ctx, args.items[1]);
        bool list_directories = true;
        String_View relative_base = sv_from_cstr("");
        size_t p = 2;
        while (p < args.count) {
            String_View tok = resolve_arg(ctx, args.items[p]);
            if (sv_eq_ci(tok, sv_from_cstr("LIST_DIRECTORIES")) && p + 1 < args.count) {
                String_View onoff = resolve_arg(ctx, args.items[++p]);
                list_directories = sv_bool_is_true(onoff);
                p++;
                continue;
            }
            if (sv_eq_ci(tok, sv_from_cstr("RELATIVE")) && p + 1 < args.count) {
                relative_base = path_join_arena(ctx->arena, ctx->current_list_dir, resolve_arg(ctx, args.items[++p]));
                p++;
                continue;
            }
            if (sv_eq_ci(tok, sv_from_cstr("CONFIGURE_DEPENDS"))) {
                p++;
                continue;
            }
            break;
        }

        String_Builder list = {0};
        bool first = true;
        for (; p < args.count; p++) {
            String_View raw_pat = resolve_arg(ctx, args.items[p]);
            if (raw_pat.count == 0) continue;
            String_View pat = raw_pat;
            String_View base = ctx->current_list_dir;
            size_t cut = raw_pat.count;
            while (cut > 0) {
                char c = raw_pat.data[cut - 1];
                if (c == '/' || c == '\\') break;
                cut--;
            }
            if (cut > 0) {
                String_View prefix = nob_sv_from_parts(raw_pat.data, cut - 1);
                base = path_join_arena(ctx->arena, ctx->current_list_dir, prefix);
                pat = nob_sv_from_parts(raw_pat.data + cut, raw_pat.count - cut);
            }
            (void)file_collect_glob_recursive(ctx, base, sv_from_cstr(""), pat, list_directories, relative_base, &list, &first);
        }
        eval_set_var(ctx, out_var, sb_to_sv(list), false, false);
        nob_sb_free(list);
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("COPY")) && args.count >= 4) {
        String_List sources = {0};
        string_list_init(&sources);
        String_View destination = sv_from_cstr("");
        size_t i = 1;
        while (i < args.count) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("DESTINATION"))) {
                if (i + 1 < args.count) destination = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[i + 1]));
                break;
            }
            string_list_add(&sources, ctx->arena, tok);
            i++;
        }
        if (destination.count == 0) return;
        (void)nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(destination));
        for (size_t s = 0; s < sources.count; s++) {
            String_View src = path_join_arena(ctx->arena, ctx->current_list_dir, sources.items[s]);
            if (!file_copy_entry_to_destination(ctx, src, destination)) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "file",
                    nob_temp_sprintf("falha ao copiar: "SV_Fmt, SV_Arg(src)),
                    "verifique se o caminho existe e permissoes");
            }
        }
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("RENAME")) && args.count >= 3) {
        String_View old_path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[1]));
        String_View new_path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[2]));
        bool no_replace = false;
        String_View result_var = sv_from_cstr("");
        for (size_t i = 3; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("NO_REPLACE"))) no_replace = true;
            if (sv_eq_ci(tok, sv_from_cstr("RESULT")) && i + 1 < args.count) {
                result_var = resolve_arg(ctx, args.items[++i]);
            }
        }
        bool ok = true;
        if (no_replace && nob_file_exists(nob_temp_sv_to_cstr(new_path))) {
            ok = false;
        } else {
            if (!ensure_parent_dirs_for_path(ctx->arena, new_path)) ok = false;
            else ok = nob_rename(nob_temp_sv_to_cstr(old_path), nob_temp_sv_to_cstr(new_path));
        }
        if (result_var.count > 0) {
            eval_set_var(ctx, result_var, ok ? sv_from_cstr("0") : sv_from_cstr("1"), false, false);
        }
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("REMOVE")) || nob_sv_eq(mode, sv_from_cstr("REMOVE_RECURSE"))) {
        bool recursive = nob_sv_eq(mode, sv_from_cstr("REMOVE_RECURSE"));
        for (size_t i = 1; i < args.count; i++) {
            String_View path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[i]));
            Nob_File_Type t = nob_get_file_type(nob_temp_sv_to_cstr(path));
            if ((int)t < 0) continue;
            bool ok = true;
            if (recursive) {
                ok = file_delete_path_recursive(ctx, path);
            } else if (t == NOB_FILE_DIRECTORY) {
                ok = remove(nob_temp_sv_to_cstr(path)) == 0;
            } else {
                ok = nob_delete_file(nob_temp_sv_to_cstr(path));
            }
            if (!ok) {
                diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "file",
                    nob_temp_sprintf("falha ao remover: "SV_Fmt, SV_Arg(path)),
                    recursive ? "verifique permissao/locks em arquivos filhos" : "use REMOVE_RECURSE para diretorios nao vazios");
            }
        }
        return;
    }
    if (nob_sv_eq(mode, sv_from_cstr("DOWNLOAD")) && args.count >= 3) {
        String_View url = resolve_arg(ctx, args.items[1]);
        String_View out_path = path_join_arena(ctx->arena, ctx->current_binary_dir, resolve_arg(ctx, args.items[2]));
        String_View status_var = sv_from_cstr("");
        String_View log_var = sv_from_cstr("");
        for (size_t i = 3; i < args.count; i++) {
            String_View tok = resolve_arg(ctx, args.items[i]);
            if (sv_eq_ci(tok, sv_from_cstr("STATUS")) && i + 1 < args.count) status_var = resolve_arg(ctx, args.items[++i]);
            else if (sv_eq_ci(tok, sv_from_cstr("LOG")) && i + 1 < args.count) log_var = resolve_arg(ctx, args.items[++i]);
        }
        String_View log_msg = sv_from_cstr("");
        bool ok = sys_download_to_path(ctx->arena, url, out_path, &log_msg);
        if (status_var.count > 0) {
            eval_set_var(ctx, status_var, ok ? sv_from_cstr("0;\"OK\"") : sv_from_cstr("1;\"FAILED\""), false, false);
        }
        if (log_var.count > 0) {
            eval_set_var(ctx, log_var, log_msg, false, false);
        }
        if (!ok) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "file",
                nob_temp_sprintf("DOWNLOAD falhou: "SV_Fmt " -> "SV_Fmt, SV_Arg(url), SV_Arg(out_path)),
                "use URL valida ou file://<origem>, e confirme conectividade/permissoes");
        }
        return;
    }
}

static void eval_make_directory_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    for (size_t i = 0; i < args.count; i++) {
        String_View raw = resolve_arg(ctx, args.items[i]);
        if (raw.count == 0) continue;
        String_View path = path_is_absolute_sv(raw) ? raw : path_join_arena(ctx->arena, ctx->current_binary_dir, raw);
        (void)ensure_parent_dirs_for_path(ctx->arena, path_join_arena(ctx->arena, path, sv_from_cstr(".keep")));
        if (!nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(path))) {
            diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "make_directory",
                nob_temp_sprintf("falha ao criar diretorio: "SV_Fmt, SV_Arg(path)),
                "verifique permissao de escrita no diretorio pai");
        }
    }
}

static void eval_use_mangled_mesa_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 2) return;

    String_View mesa_arg = resolve_arg(ctx, args.items[0]);
    String_View out_arg = resolve_arg(ctx, args.items[1]);
    if (mesa_arg.count == 0 || out_arg.count == 0) return;

    String_View mesa_dir = path_is_absolute_sv(mesa_arg)
        ? mesa_arg
        : path_join_arena(ctx->arena, ctx->current_list_dir, mesa_arg);
    String_View out_dir = path_is_absolute_sv(out_arg)
        ? out_arg
        : path_join_arena(ctx->arena, ctx->current_binary_dir, out_arg);

    Nob_File_Type mesa_type = nob_get_file_type(nob_temp_sv_to_cstr(mesa_dir));
    if (mesa_type != NOB_FILE_DIRECTORY) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "use_mangled_mesa",
            nob_temp_sprintf("diretorio Mesa invalido: "SV_Fmt, SV_Arg(mesa_dir)),
            "use use_mangled_mesa(PATH_TO_MESA OUTPUT_DIRECTORY) com PATH_TO_MESA existente");
        return;
    }

    String_View gl_mangle_root = path_join_arena(ctx->arena, mesa_dir, sv_from_cstr("gl_mangle.h"));
    String_View gl_mangle_gl = path_join_arena(ctx->arena, path_join_arena(ctx->arena, mesa_dir, sv_from_cstr("GL")), sv_from_cstr("gl_mangle.h"));
    if (!nob_file_exists(nob_temp_sv_to_cstr(gl_mangle_root)) &&
        !nob_file_exists(nob_temp_sv_to_cstr(gl_mangle_gl))) {
        diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "use_mangled_mesa",
            nob_temp_sprintf("gl_mangle.h nao encontrado em: "SV_Fmt, SV_Arg(mesa_dir)),
            "confira se PATH_TO_MESA aponta para os headers Mesa mangled");
    }

    if (!nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(out_dir))) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "use_mangled_mesa",
            nob_temp_sprintf("falha ao criar diretorio de saida: "SV_Fmt, SV_Arg(out_dir)),
            "verifique permissao de escrita no diretorio pai");
        return;
    }
    if (!nob_copy_directory_recursively(nob_temp_sv_to_cstr(mesa_dir), nob_temp_sv_to_cstr(out_dir))) {
        diag_log(DIAG_SEV_ERROR, "transpiler", "<input>", 0, 0, "use_mangled_mesa",
            nob_temp_sprintf("falha ao copiar headers Mesa de "SV_Fmt " para "SV_Fmt, SV_Arg(mesa_dir), SV_Arg(out_dir)),
            "verifique permissoes e estrutura de diretorios");
        return;
    }

    build_model_add_include_directory(ctx->model, ctx->arena, out_dir, false);
    eval_set_var(ctx, sv_from_cstr("MANGLED_MESA_OUTPUT_DIR"), out_dir, false, false);
}

static void eval_add_subdirectory_impl(Evaluator_Context *ctx, String_View sub_dir) {
    if (!ctx || sub_dir.count == 0) return;

    const char *curr_dir_cstr = nob_temp_sv_to_cstr(ctx->current_source_dir);
    const char *sub_dir_cstr  = nob_temp_sv_to_cstr(sub_dir);

    const char *full_path = path_is_absolute_sv(sub_dir)
        ? nob_temp_sprintf("%s/CMakeLists.txt", sub_dir_cstr)
        : nob_temp_sprintf("%s/%s/CMakeLists.txt", curr_dir_cstr, sub_dir_cstr);

    if (!nob_file_exists(full_path)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", curr_dir_cstr, 0, 0, "add_subdirectory",
            nob_temp_sprintf("arquivo nao encontrado: %s", full_path),
            "confira o caminho e se existe CMakeLists.txt no subdiretorio");
        return;
    }

    nob_log(NOB_INFO, "Entrando: %s", sub_dir_cstr);

    String_View content = arena_read_file(ctx->arena, full_path);
    if (content.data == NULL) {
        diag_log(DIAG_SEV_ERROR, "transpiler", curr_dir_cstr, 0, 0, "add_subdirectory",
            nob_temp_sprintf("falha ao ler: %s", full_path),
            "verifique permissoes de leitura e encoding do arquivo");
        return;
    }

    // Context Switching
    String_View old_source = ctx->current_source_dir;
    String_View old_binary = ctx->current_binary_dir;
    String_View old_list   = ctx->current_list_dir;

    const char *new_path_cstr = path_is_absolute_sv(sub_dir)
        ? sub_dir_cstr
        : nob_temp_sprintf("%s/%s", curr_dir_cstr, sub_dir_cstr);
    String_View new_path = sv_from_cstr(arena_strdup(ctx->arena, new_path_cstr));

    ctx->current_source_dir = new_path;
    ctx->current_binary_dir = new_path;
    ctx->current_list_dir   = new_path;

    eval_push_scope(ctx);

    // Tokeniza (nova assinatura)
    Token_List tokens = {0};
    if (!arena_tokenize(ctx->arena, content, &tokens)) {
        diag_log(DIAG_SEV_ERROR, "transpiler", curr_dir_cstr, 0, 0, "add_subdirectory",
            "falha ao tokenizar CMakeLists.txt do subdiretorio",
            "reduza o arquivo ou aumente memoria disponivel");
        eval_pop_scope(ctx);
        ctx->current_source_dir = old_source;
        ctx->current_binary_dir = old_binary;
        ctx->current_list_dir   = old_list;
        return;
    }

    Ast_Root sub_root = parse_tokens(ctx->arena, tokens);

    for (size_t i = 0; i < sub_root.count; i++) {
        eval_node(ctx, sub_root.items[i]);
    }

    eval_pop_scope(ctx);
    ctx->current_source_dir = old_source;
    ctx->current_binary_dir = old_binary;
    ctx->current_list_dir   = old_list;
}

// Avalia o comando 'add_subdirectory'
static void eval_add_subdirectory(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;
    eval_add_subdirectory_impl(ctx, resolve_arg(ctx, args.items[0]));
}

static void eval_subdirs_command(Evaluator_Context *ctx, Args args) {
    if (!ctx || args.count < 1) return;

    String_List include_dirs = {0};
    String_List exclude_dirs = {0};
    string_list_init(&include_dirs);
    string_list_init(&exclude_dirs);

    bool in_exclude_section = false;
    for (size_t i = 0; i < args.count; i++) {
        String_View tok = resolve_arg(ctx, args.items[i]);
        if (sv_eq_ci(tok, sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            in_exclude_section = true;
            continue;
        }
        if (sv_eq_ci(tok, sv_from_cstr("PREORDER")) || tok.count == 0) {
            continue;
        }
        if (in_exclude_section) string_list_add(&exclude_dirs, ctx->arena, tok);
        else string_list_add(&include_dirs, ctx->arena, tok);
    }

    for (size_t i = 0; i < include_dirs.count; i++) {
        String_View sub_dir = include_dirs.items[i];
        bool excluded = false;
        for (size_t j = 0; j < exclude_dirs.count; j++) {
            if (nob_sv_eq(sub_dir, exclude_dirs.items[j])) {
                excluded = true;
                break;
            }
        }
        if (!excluded) eval_add_subdirectory_impl(ctx, sub_dir);
    }
}

// ============================================================================
// AVALIAÇÃO DE ESTRUTURAS DE CONTROLE
// ============================================================================


// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

static bool sv_eq_ci_lit(String_View value, const char *lit) {
    size_t lit_len = strlen(lit);
    if (value.count != lit_len) return false;
    for (size_t i = 0; i < lit_len; i++) {
        unsigned char a = (unsigned char)value.data[i];
        unsigned char b = (unsigned char)lit[i];
        if (toupper(a) != toupper(b)) return false;
    }
    return true;
}

static bool parse_i64_sv(String_View value, long long *out) {
    const char *cstr = nob_temp_sv_to_cstr(value);
    char *end = NULL;
    long long num = strtoll(cstr, &end, 10);
    if (!end || *end != '\0') return false;
    *out = num;
    return true;
}

static int parse_version_part(const char *s, size_t len, size_t *idx) {
    long long val = 0;
    bool has_digit = false;
    while (*idx < len && s[*idx] >= '0' && s[*idx] <= '9') {
        has_digit = true;
        val = val * 10 + (s[*idx] - '0');
        (*idx)++;
    }
    return has_digit ? (int)val : 0;
}

static int compare_versions_sv(String_View a, String_View b) {
    const char *as = nob_temp_sv_to_cstr(a);
    const char *bs = nob_temp_sv_to_cstr(b);
    size_t al = strlen(as), bl = strlen(bs);
    size_t ai = 0, bi = 0;

    while (ai < al || bi < bl) {
        int ap = parse_version_part(as, al, &ai);
        int bp = parse_version_part(bs, bl, &bi);

        if (ap < bp) return -1;
        if (ap > bp) return 1;

        if (ai < al && as[ai] == '.') ai++;
        if (bi < bl && bs[bi] == '.') bi++;

        while (ai < al && as[ai] != '.' && (as[ai] < '0' || as[ai] > '9')) ai++;
        while (bi < bl && bs[bi] != '.' && (bs[bi] < '0' || bs[bi] > '9')) bi++;
    }

    return 0;
}

static String_View eval_condition_operand(Evaluator_Context *ctx, String_View token, bool quoted, bool comparison_operand) {
    if (quoted) return token;
    if (eval_has_var(ctx, token)) return eval_get_var(ctx, token);
    if (sv_is_i64_literal(token)) return token;
    if (sv_eq_ci(token, sv_from_cstr("TRUE")) ||
        sv_eq_ci(token, sv_from_cstr("FALSE")) ||
        sv_eq_ci(token, sv_from_cstr("ON")) ||
        sv_eq_ci(token, sv_from_cstr("OFF")) ||
        sv_eq_ci(token, sv_from_cstr("YES")) ||
        sv_eq_ci(token, sv_from_cstr("NO")) ||
        sv_eq_ci(token, sv_from_cstr("Y")) ||
        sv_eq_ci(token, sv_from_cstr("N")) ||
        sv_eq_ci(token, sv_from_cstr("IGNORE")) ||
        sv_eq_ci(token, sv_from_cstr("NOTFOUND")) ||
        sv_ends_with_ci(token, sv_from_cstr("-NOTFOUND"))) {
        return token;
    }
    // Em comparadores, identificadores nao resolvidos devem permanecer literais.
    // Em forma unaria (if(X)), indefinidos viram vazio/falso.
    if (comparison_operand) return token;
    return sv_from_cstr("");
}

static bool eval_condition_truthy(String_View value) {
    return !cmake_string_is_false(value);
}

static bool eval_condition_comparison(String_View lhs, String_View op, String_View rhs) {
    if (sv_eq_ci_lit(op, "STREQUAL")) {
        return nob_sv_eq(lhs, rhs);
    }

    if (sv_eq_ci_lit(op, "EQUAL") ||
        sv_eq_ci_lit(op, "LESS") ||
        sv_eq_ci_lit(op, "GREATER") ||
        sv_eq_ci_lit(op, "LESS_EQUAL") ||
        sv_eq_ci_lit(op, "GREATER_EQUAL")) {
        long long li = 0, ri = 0;
        bool ln = parse_i64_sv(lhs, &li);
        bool rn = parse_i64_sv(rhs, &ri);

        if (ln && rn) {
            if (sv_eq_ci_lit(op, "EQUAL")) return li == ri;
            if (sv_eq_ci_lit(op, "LESS")) return li < ri;
            if (sv_eq_ci_lit(op, "GREATER")) return li > ri;
            if (sv_eq_ci_lit(op, "LESS_EQUAL")) return li <= ri;
            return li >= ri;
        }

        int cmp = strcmp(nob_temp_sv_to_cstr(lhs), nob_temp_sv_to_cstr(rhs));
        if (sv_eq_ci_lit(op, "EQUAL")) return cmp == 0;
        if (sv_eq_ci_lit(op, "LESS")) return cmp < 0;
        if (sv_eq_ci_lit(op, "GREATER")) return cmp > 0;
        if (sv_eq_ci_lit(op, "LESS_EQUAL")) return cmp <= 0;
        return cmp >= 0;
    }

    if (sv_eq_ci_lit(op, "VERSION_LESS") ||
        sv_eq_ci_lit(op, "VERSION_GREATER") ||
        sv_eq_ci_lit(op, "VERSION_EQUAL") ||
        sv_eq_ci_lit(op, "VERSION_LESS_EQUAL") ||
        sv_eq_ci_lit(op, "VERSION_GREATER_EQUAL")) {
        int cmp = compare_versions_sv(lhs, rhs);
        if (sv_eq_ci_lit(op, "VERSION_LESS")) return cmp < 0;
        if (sv_eq_ci_lit(op, "VERSION_GREATER")) return cmp > 0;
        if (sv_eq_ci_lit(op, "VERSION_EQUAL")) return cmp == 0;
        if (sv_eq_ci_lit(op, "VERSION_LESS_EQUAL")) return cmp <= 0;
        return cmp >= 0;
    }

    return false;
}

static bool eval_condition_parse_or(Evaluator_Context *ctx, String_View *tokens, bool *quoted_tokens, size_t count, size_t *idx);

static bool eval_condition_parse_atom(Evaluator_Context *ctx, String_View *tokens, bool *quoted_tokens, size_t count, size_t *idx) {
    if (*idx >= count) return false;

    String_View lhs_tok = tokens[(*idx)++];
    bool lhs_quoted = quoted_tokens ? quoted_tokens[*idx - 1] : false;

    if (nob_sv_eq(lhs_tok, sv_from_cstr("("))) {
        bool v = eval_condition_parse_or(ctx, tokens, quoted_tokens, count, idx);
        if (*idx < count && nob_sv_eq(tokens[*idx], sv_from_cstr(")"))) {
            (*idx)++;
        }
        return v;
    }
    if (nob_sv_eq(lhs_tok, sv_from_cstr(")"))) {
        return false;
    }

    if (sv_eq_ci_lit(lhs_tok, "DEFINED")) {
        if (*idx >= count) return false;
        String_View var_name = tokens[(*idx)++];
        return eval_has_var(ctx, var_name);
    }

    String_View lhs = eval_condition_operand(ctx, lhs_tok, lhs_quoted, false);

    if (*idx < count) {
        String_View op = tokens[*idx];
        if (sv_eq_ci_lit(op, "STREQUAL") ||
            sv_eq_ci_lit(op, "EQUAL") ||
            sv_eq_ci_lit(op, "LESS") ||
            sv_eq_ci_lit(op, "GREATER") ||
            sv_eq_ci_lit(op, "LESS_EQUAL") ||
            sv_eq_ci_lit(op, "GREATER_EQUAL") ||
            sv_eq_ci_lit(op, "VERSION_LESS") ||
            sv_eq_ci_lit(op, "VERSION_GREATER") ||
            sv_eq_ci_lit(op, "VERSION_EQUAL") ||
            sv_eq_ci_lit(op, "VERSION_LESS_EQUAL") ||
            sv_eq_ci_lit(op, "VERSION_GREATER_EQUAL")) {
            (*idx)++;
            if (*idx >= count) return false;
            String_View rhs_tok = tokens[(*idx)++];
            bool rhs_quoted = quoted_tokens ? quoted_tokens[*idx - 1] : false;
            lhs = eval_condition_operand(ctx, lhs_tok, lhs_quoted, true);
            String_View rhs = eval_condition_operand(ctx, rhs_tok, rhs_quoted, true);
            return eval_condition_comparison(lhs, op, rhs);
        }
    }

    return eval_condition_truthy(lhs);
}

static bool eval_condition_parse_not(Evaluator_Context *ctx, String_View *tokens, bool *quoted_tokens, size_t count, size_t *idx) {
    if (*idx < count && sv_eq_ci_lit(tokens[*idx], "NOT")) {
        (*idx)++;
        return !eval_condition_parse_not(ctx, tokens, quoted_tokens, count, idx);
    }
    return eval_condition_parse_atom(ctx, tokens, quoted_tokens, count, idx);
}

static bool eval_condition_parse_and(Evaluator_Context *ctx, String_View *tokens, bool *quoted_tokens, size_t count, size_t *idx) {
    bool acc = eval_condition_parse_not(ctx, tokens, quoted_tokens, count, idx);
    while (*idx < count && sv_eq_ci_lit(tokens[*idx], "AND")) {
        (*idx)++;
        bool rhs = eval_condition_parse_not(ctx, tokens, quoted_tokens, count, idx);
        acc = acc && rhs;
    }
    return acc;
}

static bool eval_condition_parse_or(Evaluator_Context *ctx, String_View *tokens, bool *quoted_tokens, size_t count, size_t *idx) {
    bool acc = eval_condition_parse_and(ctx, tokens, quoted_tokens, count, idx);
    while (*idx < count && sv_eq_ci_lit(tokens[*idx], "OR")) {
        (*idx)++;
        bool rhs = eval_condition_parse_and(ctx, tokens, quoted_tokens, count, idx);
        acc = acc || rhs;
    }
    return acc;
}

// Avalia condicao com precedencia: NOT > comparador > AND > OR.
static bool eval_condition(Evaluator_Context *ctx, Args condition) {
    if (condition.count == 0) return false;

    String_View *tokens = arena_alloc_array(ctx->arena, String_View, condition.count);
    bool *quoted_tokens = arena_alloc_array_zero(ctx->arena, bool, condition.count);
    if (!tokens || !quoted_tokens) return false;

    for (size_t i = 0; i < condition.count; i++) {
        tokens[i] = resolve_arg(ctx, condition.items[i]);
        quoted_tokens[i] = condition.items[i].count == 1 &&
                           (condition.items[i].items[0].kind == TOKEN_STRING ||
                            condition.items[i].items[0].kind == TOKEN_RAW_STRING);
    }

    size_t idx = 0;
    return eval_condition_parse_or(ctx, tokens, quoted_tokens, condition.count, &idx);
}

static bool eval_handle_flow_control_command(Evaluator_Context *ctx, String_View cmd_name) {
    if (sv_eq_ci_lit(cmd_name, "break")) {
        if (ctx->loop_depth == 0) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "break",
                "break usado fora de loop", "use break apenas dentro de foreach() ou while()");
            return true;
        }
        ctx->break_requested = true;
        return true;
    }

    if (sv_eq_ci_lit(cmd_name, "continue")) {
        if (ctx->loop_depth == 0) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "continue",
                "continue usado fora de loop", "use continue apenas dentro de foreach() ou while()");
            return true;
        }
        ctx->continue_requested = true;
        return true;
    }

    if (sv_eq_ci_lit(cmd_name, "return")) {
        if (!ctx->in_function_call) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "return",
                "return usado fora de function()", "use return apenas dentro de function()");
            return true;
        }
        ctx->return_requested = true;
        return true;
    }

    return false;
}

static bool eval_has_pending_flow_control(const Evaluator_Context *ctx) {
    return ctx->return_requested || ctx->break_requested || ctx->continue_requested;
}

static Loop_Flow_Signal eval_consume_loop_flow_signal(Evaluator_Context *ctx) {
    if (ctx->return_requested) return LOOP_FLOW_RETURN;
    if (ctx->break_requested) {
        ctx->break_requested = false;
        return LOOP_FLOW_BREAK;
    }
    if (ctx->continue_requested) {
        ctx->continue_requested = false;
        return LOOP_FLOW_CONTINUE;
    }
    return LOOP_FLOW_NONE;
}

// Avalia um nó IF
static void eval_if_statement(Evaluator_Context *ctx, Node *node) {
    bool condition_true = eval_condition(ctx, node->as.if_stmt.condition);
    
    if (condition_true) {
        // Avalia bloco THEN
        for (size_t i = 0; i < node->as.if_stmt.then_block.count; i++) {
            eval_node(ctx, node->as.if_stmt.then_block.items[i]);
            if (eval_has_pending_flow_control(ctx)) return;
        }
    } else if (node->as.if_stmt.else_block.count > 0) {
        // Avalia bloco ELSE
        for (size_t i = 0; i < node->as.if_stmt.else_block.count; i++) {
            eval_node(ctx, node->as.if_stmt.else_block.items[i]);
            if (eval_has_pending_flow_control(ctx)) return;
        }
    }
}

// Avalia um nó FOREACH
static void eval_foreach_statement(Evaluator_Context *ctx, Node *node) {
    if (node->as.foreach_stmt.args.count < 1) return;

    String_View var_name = resolve_arg(ctx, node->as.foreach_stmt.args.items[0]);
    String_List items = {0};
    string_list_init(&items);

    if (node->as.foreach_stmt.args.count >= 2) {
        String_View second = resolve_arg(ctx, node->as.foreach_stmt.args.items[1]);
        if (sv_eq_ci(second, sv_from_cstr("IN"))) {
            enum {
                FOREACH_ITEMS_MODE = 0,
                FOREACH_LISTS_MODE = 1
            } mode = FOREACH_ITEMS_MODE;

            for (size_t i = 2; i < node->as.foreach_stmt.args.count; i++) {
                String_View tok = resolve_arg(ctx, node->as.foreach_stmt.args.items[i]);
                if (sv_eq_ci(tok, sv_from_cstr("ITEMS"))) {
                    mode = FOREACH_ITEMS_MODE;
                    continue;
                }
                if (sv_eq_ci(tok, sv_from_cstr("LISTS"))) {
                    mode = FOREACH_LISTS_MODE;
                    continue;
                }

                if (mode == FOREACH_LISTS_MODE) {
                    String_View list_value = eval_get_var(ctx, tok);
                    if (list_value.count == 0) continue;
                    size_t start = 0;
                    for (size_t k = 0; k <= list_value.count; k++) {
                        bool sep = (k == list_value.count) || (list_value.data[k] == ';');
                        if (!sep) continue;
                        if (k > start) {
                            String_View item = nob_sv_from_parts(list_value.data + start, k - start);
                            string_list_add(&items, ctx->arena, item);
                        }
                        start = k + 1;
                    }
                } else {
                    size_t start = 0;
                    for (size_t k = 0; k <= tok.count; k++) {
                        bool sep = (k == tok.count) || (tok.data[k] == ';');
                        if (!sep) continue;
                        if (k > start) {
                            String_View item = nob_sv_from_parts(tok.data + start, k - start);
                            string_list_add(&items, ctx->arena, item);
                        }
                        start = k + 1;
                    }
                }
            }
        } else {
            resolve_args_to_list(ctx, node->as.foreach_stmt.args, 1, &items);
        }
    }

    // Executa o corpo para cada item
    ctx->loop_depth++;
    for (size_t i = 0; i < items.count; i++) {
        eval_set_var(ctx, var_name, items.items[i], false, false);
        
        for (size_t j = 0; j < node->as.foreach_stmt.body.count; j++) {
            eval_node(ctx, node->as.foreach_stmt.body.items[j]);
            Loop_Flow_Signal signal = eval_consume_loop_flow_signal(ctx);
            if (signal == LOOP_FLOW_RETURN || signal == LOOP_FLOW_BREAK) {
                ctx->loop_depth--;
                return;
            }
            if (signal == LOOP_FLOW_CONTINUE) break;
        }
    }
    ctx->loop_depth--;
}

static void eval_while_statement(Evaluator_Context *ctx, Node *node) {
    const size_t max_iterations = 100000;
    size_t iteration = 0;

    ctx->loop_depth++;
    while (eval_condition(ctx, node->as.while_stmt.condition)) {
        if (iteration++ > max_iterations) {
            diag_log(DIAG_SEV_WARNING, "transpiler", "<input>", 0, 0, "while",
                "limite de iteracoes do while excedido", "verifique condicao do loop ou use break()");
            break;
        }

        for (size_t i = 0; i < node->as.while_stmt.body.count; i++) {
            eval_node(ctx, node->as.while_stmt.body.items[i]);
            Loop_Flow_Signal signal = eval_consume_loop_flow_signal(ctx);
            if (signal == LOOP_FLOW_RETURN || signal == LOOP_FLOW_BREAK) {
                ctx->loop_depth--;
                return;
            }
            if (signal == LOOP_FLOW_CONTINUE) break;
        }
    }
    ctx->loop_depth--;
}
