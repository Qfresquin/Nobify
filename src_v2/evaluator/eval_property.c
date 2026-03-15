#include "evaluator_internal.h"

#include "stb_ds.h"
#include "sv_utils.h"

#include <ctype.h>
#include <string.h>

static const char *k_global_defs_var = "NOBIFY_GLOBAL_COMPILE_DEFINITIONS";
static const char *k_global_opts_var = "NOBIFY_GLOBAL_COMPILE_OPTIONS";
static const char *k_global_link_opts_var = "NOBIFY_GLOBAL_LINK_OPTIONS";

static String_View merge_property_value_temp(Evaluator_Context *ctx,
                                             String_View current,
                                             String_View incoming,
                                             Cmake_Target_Property_Op op) {
    if (op == EV_PROP_SET) return incoming;
    if (incoming.count == 0) return current;
    if (current.count == 0) return incoming;

    bool with_semicolon = (op == EV_PROP_APPEND_LIST);
    size_t total = current.count + incoming.count + (with_semicolon ? 1 : 0);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, current.data, current.count);
    off += current.count;
    if (with_semicolon) buf[off++] = ';';
    memcpy(buf + off, incoming.data, incoming.count);
    off += incoming.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static bool is_current_directory_object(Evaluator_Context *ctx, String_View object_id) {
    if (!ctx) return false;
    if (object_id.count == 0) return true;
    if (eval_sv_eq_ci_lit(object_id, ".")) return true;

    String_View cur_src = eval_current_source_dir(ctx);
    String_View cur_bin = eval_current_binary_dir(ctx);
    if (svu_eq_ci_sv(object_id, cur_src)) return true;
    if (svu_eq_ci_sv(object_id, cur_bin)) return true;
    return false;
}

static bool set_output_var_value(Evaluator_Context *ctx, String_View out_var, String_View value) {
    return eval_var_set_current(ctx, out_var, value);
}

static bool set_output_var_notfound(Evaluator_Context *ctx, String_View out_var) {
    String_View suffix = nob_sv_from_cstr("-NOTFOUND");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), out_var.count + suffix.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memcpy(buf, out_var.data, out_var.count);
    memcpy(buf + out_var.count, suffix.data, suffix.count);
    buf[out_var.count + suffix.count] = '\0';
    return eval_var_set_current(ctx, out_var, nob_sv_from_cstr(buf));
}

static bool set_output_var_literal_notfound(Evaluator_Context *ctx, String_View out_var) {
    return eval_var_set_current(ctx, out_var, nob_sv_from_cstr("NOTFOUND"));
}

static String_View property_ascii_lower_temp(Evaluator_Context *ctx, String_View value) {
    if (!ctx || value.count == 0) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < value.count; i++) {
        buf[i] = (char)tolower((unsigned char)value.data[i]);
    }
    buf[value.count] = '\0';
    return nob_sv_from_cstr(buf);
}

static Event_Property_Mutate_Op property_mutate_op_from_legacy(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return EVENT_PROPERTY_MUTATE_SET;
        case EV_PROP_APPEND_LIST: return EVENT_PROPERTY_MUTATE_APPEND_LIST;
        case EV_PROP_APPEND_STRING: return EVENT_PROPERTY_MUTATE_APPEND_STRING;
    }
    return EVENT_PROPERTY_MUTATE_SET;
}

static bool emit_property_write_semantic_event(Evaluator_Context *ctx,
                                               Event_Origin origin,
                                               String_View scope_upper,
                                               String_View object_id,
                                               String_View property_upper,
                                               String_View value,
                                               Cmake_Target_Property_Op op) {
    if (!ctx) return false;

    bool is_directory_scope =
        eval_sv_eq_ci_lit(scope_upper, "DIRECTORY") && is_current_directory_object(ctx, object_id);
    bool is_global_scope = eval_sv_eq_ci_lit(scope_upper, "GLOBAL");
    if (!is_directory_scope && !is_global_scope) return true;

    SV_List items = {0};
    if (value.count > 0 && !eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), value, &items)) return false;

    Event_Property_Mutate_Op semantic_op = property_mutate_op_from_legacy(op);
    if (is_directory_scope) {
        return eval_emit_directory_property_mutate(ctx,
                                                   origin,
                                                   property_upper,
                                                   semantic_op,
                                                   EVENT_PROPERTY_MODIFIER_NONE,
                                                   items,
                                                   arena_arr_len(items));
    }

    return eval_emit_global_property_mutate(ctx,
                                            origin,
                                            property_upper,
                                            semantic_op,
                                            EVENT_PROPERTY_MODIFIER_NONE,
                                            items,
                                            arena_arr_len(items));
}

static bool target_get_declared_dir_temp(Evaluator_Context *ctx, String_View target_name, String_View *out_dir) {
    if (!ctx || !out_dir) return false;
    return eval_target_declared_dir(ctx, target_name, out_dir);
}

static bool property_append_unique_temp(Evaluator_Context *ctx, SV_List *list, String_View value) {
    if (!ctx || !list) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (eval_sv_key_eq((*list)[i], value)) return true;
    }
    return svu_list_push_temp(ctx, list, value);
}

static String_View eval_property_store_key_temp(Evaluator_Context *ctx,
                                                String_View scope_upper,
                                                String_View object_id,
                                                String_View prop_upper) {
    static const char prefix[] = "NOBIFY_PROPERTY_";
    if (!ctx) return nob_sv_from_cstr("");

    bool has_obj = object_id.count > 0;
    size_t total = (sizeof(prefix) - 1) + scope_upper.count + 2 + prop_upper.count;
    if (has_obj) total += 2 + object_id.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, scope_upper.data, scope_upper.count);
    off += scope_upper.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (has_obj) {
        memcpy(buf + off, object_id.data, object_id.count);
        off += object_id.count;
        buf[off++] = ':';
        buf[off++] = ':';
    }
    memcpy(buf + off, prop_upper.data, prop_upper.count);
    off += prop_upper.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static Eval_Var_Entry *property_store_entry(Evaluator_Context *ctx, String_View store_key) {
    if (!ctx || store_key.count == 0) return NULL;
    return stbds_shgetp_null(ctx->command_state.property_store, nob_temp_sv_to_cstr(store_key));
}

bool eval_property_store_set_key(Evaluator_Context *ctx, String_View store_key, String_View value) {
    if (!ctx || store_key.count == 0) return false;

    String_View stable_value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;

    Eval_Var_Entry *entry = property_store_entry(ctx, store_key);
    if (entry) {
        entry->value = stable_value;
        return true;
    }

    char *stable_key = arena_strndup(ctx->event_arena, store_key.data, store_key.count);
    EVAL_OOM_RETURN_IF_NULL(ctx, stable_key, false);

    Eval_Var_Entry *table = ctx->command_state.property_store;
    stbds_shput(table, stable_key, stable_value);
    ctx->command_state.property_store = table;
    return true;
}

bool eval_property_store_get_key(Evaluator_Context *ctx,
                                 String_View store_key,
                                 String_View *out_value,
                                 bool *out_set) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (out_set) *out_set = false;
    if (!ctx || store_key.count == 0) return false;

    Eval_Var_Entry *entry = property_store_entry(ctx, store_key);
    if (!entry) return true;
    if (out_value) *out_value = entry->value;
    if (out_set) *out_set = true;
    return true;
}

static const Eval_Property_Definition *eval_property_definition_find_impl(Evaluator_Context *ctx,
                                                                          String_View scope_upper,
                                                                          String_View property_upper) {
    if (!ctx) return NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->property_definitions); i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions[i];
        if (!eval_sv_key_eq(def->scope_upper, scope_upper)) continue;
        if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
        return def;
    }
    return NULL;
}

const Eval_Property_Definition *eval_property_definition_find(Evaluator_Context *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name) {
    if (!ctx) return NULL;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return NULL;

    const Eval_Property_Definition *def =
        eval_property_definition_find_impl(ctx, scope_upper, property_upper);
    if (def) return def;

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        return eval_property_definition_find_impl(ctx, nob_sv_from_cstr("CACHED_VARIABLE"), property_upper);
    }
    return NULL;
}

bool eval_property_define(Evaluator_Context *ctx, const Eval_Property_Definition *definition) {
    if (!ctx || !definition) return false;
    if (eval_property_definition_find_impl(ctx, definition->scope_upper, definition->property_upper)) {
        return true;
    }

    Eval_Property_Definition stored = *definition;
    stored.scope_upper = sv_copy_to_event_arena(ctx, definition->scope_upper);
    stored.property_upper = sv_copy_to_event_arena(ctx, definition->property_upper);
    if (definition->has_brief_docs) stored.brief_docs = sv_copy_to_event_arena(ctx, definition->brief_docs);
    if (definition->has_full_docs) stored.full_docs = sv_copy_to_event_arena(ctx, definition->full_docs);
    if (definition->has_initialize_from_variable) {
        stored.initialize_from_variable = sv_copy_to_event_arena(ctx, definition->initialize_from_variable);
    }
    if (eval_should_stop(ctx)) return false;

    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->property_definitions, stored);
}

bool eval_property_is_defined(Evaluator_Context *ctx, String_View scope_upper, String_View property_name) {
    if (!ctx) return false;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return false;
    return eval_property_definition_find_impl(ctx, scope_upper, property_upper) != NULL;
}

bool eval_target_apply_defined_initializers(Evaluator_Context *ctx,
                                            Event_Origin origin,
                                            String_View target_name) {
    if (!ctx || eval_should_stop(ctx)) return false;

    for (size_t i = 0; i < arena_arr_len(ctx->property_definitions); i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions[i];
        if (!eval_sv_eq_ci_lit(def->scope_upper, "TARGET")) continue;
        if (!def->has_initialize_from_variable) continue;
        if (!eval_var_defined_visible(ctx, def->initialize_from_variable)) continue;
        if (!eval_emit_target_prop_set(ctx,
                                       origin,
                                       target_name,
                                       def->property_upper,
                                       eval_var_get_visible(ctx, def->initialize_from_variable),
                                       EV_PROP_SET)) {
            return false;
        }
    }

    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

bool eval_property_write(Evaluator_Context *ctx,
                         Event_Origin origin,
                         String_View scope_upper,
                         String_View object_id,
                         String_View property_name,
                         String_View value,
                         Cmake_Target_Property_Op op,
                         bool emit_var_event) {
    if (!ctx) return false;

    String_View prop_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return false;

    String_View store_key = eval_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
    if (eval_should_stop(ctx)) return false;

    String_View current = nob_sv_from_cstr("");
    if (!eval_property_store_get_key(ctx, store_key, &current, NULL)) return false;
    String_View merged = merge_property_value_temp(ctx, current, value, op);
    if (eval_should_stop(ctx)) return false;

    if (!emit_property_write_semantic_event(ctx, origin, scope_upper, object_id, prop_upper, value, op)) {
        return false;
    }

    if (!eval_property_store_set_key(ctx, store_key, merged)) return false;
    if (!eval_var_set_current(ctx, store_key, merged)) return false;
    if (emit_var_event && !eval_emit_var_set_current(ctx, origin, store_key, merged)) return false;

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY") && is_current_directory_object(ctx, object_id)) {
        if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_OPTIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_opts_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_opts_var), next)) return false;
        } else if (eval_sv_eq_ci_lit(prop_upper, "COMPILE_DEFINITIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_defs_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_defs_var), next)) return false;
        } else if (eval_sv_eq_ci_lit(prop_upper, "LINK_OPTIONS")) {
            String_View cur = eval_var_get_visible(ctx, nob_sv_from_cstr(k_global_link_opts_var));
            String_View next = merge_property_value_temp(ctx, cur, value, op);
            if (eval_should_stop(ctx)) return false;
            if (!eval_var_set_current(ctx, nob_sv_from_cstr(k_global_link_opts_var), next)) return false;
        }
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") && eval_sv_eq_ci_lit(prop_upper, "VALUE")) {
        if (!eval_var_set_current(ctx, object_id, merged)) return false;
        if (!eval_emit_var_set_cache(ctx, origin, object_id, merged)) return false;
    }

    return true;
}

static String_View target_property_from_store_temp(Evaluator_Context *ctx,
                                                   String_View target_name,
                                                   String_View prop_upper,
                                                   bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View store_key =
        eval_property_store_key_temp(ctx, nob_sv_from_cstr("TARGET"), target_name, prop_upper);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!eval_property_store_get_key(ctx, store_key, &value, &have)) return nob_sv_from_cstr("");
    if (out_set) *out_set = have;
    return have ? value : nob_sv_from_cstr("");
}

static bool property_parent_directory_temp(Evaluator_Context *ctx,
                                           String_View dir,
                                           String_View *out_parent) {
    if (!ctx || !out_parent) return false;
    *out_parent = nob_sv_from_cstr("");

    String_View norm = eval_sv_path_normalize_temp(ctx, dir);
    if (eval_should_stop(ctx)) return false;
    if (norm.count == 0) return true;
    if (nob_sv_eq(norm, nob_sv_from_cstr("/"))) return true;
    if (norm.count == 3 && norm.data[1] == ':' &&
        (norm.data[2] == '/' || norm.data[2] == '\\')) return true;

    size_t end = norm.count;
    while (end > 0) {
        char c = norm.data[end - 1];
        if (c != '/' && c != '\\') break;
        end--;
    }
    if (end == 0) return true;

    size_t slash = SIZE_MAX;
    for (size_t i = 0; i < end; i++) {
        char c = norm.data[i];
        if (c == '/' || c == '\\') slash = i;
    }
    if (slash == SIZE_MAX) return true;
    if (slash == 0) {
        *out_parent = nob_sv_from_cstr("/");
        return true;
    }
    if (norm.data[slash - 1] == ':') {
        *out_parent = nob_sv_from_parts(norm.data, slash + 1);
        return true;
    }
    *out_parent = nob_sv_from_parts(norm.data, slash);
    return true;
}

static String_View property_value_from_store_temp(Evaluator_Context *ctx,
                                                  String_View scope_upper,
                                                  String_View object_id,
                                                  String_View prop_upper,
                                                  bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
        return target_property_from_store_temp(ctx, object_id, prop_upper, out_set);
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE") &&
        eval_sv_eq_ci_lit(prop_upper, "VALUE") &&
        eval_cache_defined(ctx, object_id)) {
        if (out_set) *out_set = true;
        return eval_var_get_visible(ctx, object_id);
    }

    String_View store_key = eval_property_store_key_temp(ctx, scope_upper, object_id, prop_upper);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!eval_property_store_get_key(ctx, store_key, &value, &have)) return nob_sv_from_cstr("");
    if (out_set) *out_set = have;
    return have ? value : nob_sv_from_cstr("");
}

static String_View property_value_from_directory_chain_temp(Evaluator_Context *ctx,
                                                            String_View start_dir,
                                                            String_View prop_upper,
                                                            bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View cur = start_dir;
    while (cur.count > 0) {
        bool have = false;
        String_View value = property_value_from_store_temp(ctx,
                                                           nob_sv_from_cstr("DIRECTORY"),
                                                           cur,
                                                           prop_upper,
                                                           &have);
        if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        if (have) {
            if (out_set) *out_set = true;
            return value;
        }

        String_View parent = nob_sv_from_cstr("");
        if (!property_parent_directory_temp(ctx, cur, &parent)) return nob_sv_from_cstr("");
        if (parent.count == 0 || eval_sv_key_eq(parent, cur)) break;
        cur = parent;
    }

    bool have_global = false;
    String_View value = property_value_from_store_temp(ctx,
                                                       nob_sv_from_cstr("GLOBAL"),
                                                       nob_sv_from_cstr(""),
                                                       prop_upper,
                                                       &have_global);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have_global && out_set) *out_set = true;
    return have_global ? value : nob_sv_from_cstr("");
}

static String_View resolve_property_value_temp(Evaluator_Context *ctx,
                                               String_View scope_upper,
                                               String_View object_id,
                                               String_View prop_name,
                                               bool allow_inherit,
                                               String_View inherit_directory,
                                               bool *out_set) {
    if (out_set) *out_set = false;
    if (!ctx) return nob_sv_from_cstr("");

    String_View prop_upper = eval_property_upper_name_temp(ctx, prop_name);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    bool have = false;
    String_View value = property_value_from_store_temp(ctx, scope_upper, object_id, prop_upper, &have);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (have) {
        if (out_set) *out_set = true;
        return value;
    }
    if (!allow_inherit) return nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(scope_upper, "DIRECTORY")) {
        return property_value_from_directory_chain_temp(ctx, object_id, prop_upper, out_set);
    }

    if (eval_sv_eq_ci_lit(scope_upper, "TARGET") ||
        eval_sv_eq_ci_lit(scope_upper, "SOURCE") ||
        eval_sv_eq_ci_lit(scope_upper, "TEST")) {
        String_View base_dir = inherit_directory;
        if (base_dir.count == 0 && eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!target_get_declared_dir_temp(ctx, object_id, &base_dir)) return nob_sv_from_cstr("");
            if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
        }
        if (base_dir.count > 0) {
            return property_value_from_directory_chain_temp(ctx, base_dir, prop_upper, out_set);
        }
    }

    return nob_sv_from_cstr("");
}

static bool property_known_for_scope(Evaluator_Context *ctx,
                                     String_View scope_upper,
                                     String_View object_id,
                                     String_View prop_name,
                                     String_View inherit_directory) {
    if (!ctx) return false;
    if (eval_property_definition_find(ctx, scope_upper, prop_name)) {
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    if (eval_should_stop(ctx)) return false;

    bool have = false;
    (void)resolve_property_value_temp(ctx,
                                      scope_upper,
                                      object_id,
                                      prop_name,
                                      false,
                                      inherit_directory,
                                      &have);
    if (eval_should_stop(ctx)) return false;
    return have;
}

bool eval_property_query_mode_parse(Evaluator_Context *ctx,
                                    const Node *node,
                                    Event_Origin origin,
                                    String_View token,
                                    Eval_Property_Query_Mode *out_mode) {
    if (!ctx || !node || !out_mode) return false;
    if (eval_sv_eq_ci_lit(token, "SET")) {
        *out_mode = EVAL_PROP_QUERY_SET;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "DEFINED")) {
        *out_mode = EVAL_PROP_QUERY_DEFINED;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "BRIEF_DOCS")) {
        *out_mode = EVAL_PROP_QUERY_BRIEF_DOCS;
        return true;
    }
    if (eval_sv_eq_ci_lit(token, "FULL_DOCS")) {
        *out_mode = EVAL_PROP_QUERY_FULL_DOCS;
        return true;
    }

    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                   node,
                                   origin,
                                   EV_DIAG_ERROR,
                                   EVAL_DIAG_UNSUPPORTED_OPERATION,
                                   "dispatcher",
                                   nob_sv_from_cstr("get_property() received unsupported query mode"),
                                   token);
    return false;
}

bool eval_property_query(Evaluator_Context *ctx,
                         const Node *node,
                         Event_Origin origin,
                         String_View out_var,
                         String_View scope_upper,
                         String_View object_id,
                         String_View validation_object,
                         String_View property_name,
                         Eval_Property_Query_Mode mode,
                         String_View inherit_directory,
                         bool validate_object,
                         bool missing_as_notfound) {
    if (!ctx || !node) return false;

    const Eval_Property_Definition *def =
        eval_property_definition_find(ctx, scope_upper, property_name);
    if (eval_should_stop(ctx)) return false;
    bool allow_inherit = def && def->inherited;

    if (mode == EVAL_PROP_QUERY_DEFINED) {
        return set_output_var_value(ctx,
                                    out_var,
                                    property_known_for_scope(ctx,
                                                             scope_upper,
                                                             object_id,
                                                             property_name,
                                                             inherit_directory)
                                        ? nob_sv_from_cstr("1")
                                        : nob_sv_from_cstr("0"));
    }

    if (mode == EVAL_PROP_QUERY_BRIEF_DOCS || mode == EVAL_PROP_QUERY_FULL_DOCS) {
        if (!def) return set_output_var_notfound(ctx, out_var);
        if (mode == EVAL_PROP_QUERY_BRIEF_DOCS) {
            return set_output_var_value(ctx,
                                        out_var,
                                        def->has_brief_docs ? def->brief_docs : nob_sv_from_cstr(""));
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    def->has_full_docs ? def->full_docs : nob_sv_from_cstr(""));
    }

    String_View validated_name = validation_object.count > 0 ? validation_object : object_id;
    if (validate_object && (mode == EVAL_PROP_QUERY_VALUE || mode == EVAL_PROP_QUERY_SET)) {
        if (eval_sv_eq_ci_lit(scope_upper, "TARGET")) {
            if (!eval_target_known(ctx, validated_name)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               origin,
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_NOT_FOUND,
                                               "dispatcher",
                                               nob_sv_from_cstr("get_property(TARGET ...) target was not declared"),
                                               validated_name);
                return !eval_result_is_fatal(eval_result_from_ctx(ctx));
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
            if (!eval_cache_defined(ctx, validated_name)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                               node,
                                               origin,
                                               EV_DIAG_ERROR,
                                               EVAL_DIAG_NOT_FOUND,
                                               "dispatcher",
                                               nob_sv_from_cstr("get_property(CACHE ...) cache entry does not exist"),
                                               validated_name);
                return !eval_result_is_fatal(eval_result_from_ctx(ctx));
            }
        } else if (eval_sv_eq_ci_lit(scope_upper, "TEST")) {
            String_View test_dir =
                inherit_directory.count > 0 ? inherit_directory : eval_current_source_dir_for_paths(ctx);
            if (!eval_test_exists_in_directory_scope(ctx, validated_name, test_dir)) {
                EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                    ctx,
                    node,
                    origin,
                    EV_DIAG_ERROR,
                    EVAL_DIAG_NOT_FOUND,
                    "dispatcher",
                    nob_sv_from_cstr("get_property(TEST ...) test was not declared in selected directory scope"),
                    validated_name);
                return !eval_result_is_fatal(eval_result_from_ctx(ctx));
            }
        }
    }

    bool have = false;
    String_View value = resolve_property_value_temp(ctx,
                                                    scope_upper,
                                                    object_id,
                                                    property_name,
                                                    allow_inherit,
                                                    inherit_directory,
                                                    &have);
    if (eval_should_stop(ctx)) return false;

    if (mode == EVAL_PROP_QUERY_SET) {
        return set_output_var_value(ctx, out_var, have ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0"));
    }

    if (!have) {
        if (missing_as_notfound) return set_output_var_notfound(ctx, out_var);
        return eval_var_unset_current(ctx, out_var);
    }
    return set_output_var_value(ctx, out_var, value);
}

bool eval_property_query_cmake(Evaluator_Context *ctx,
                               const Node *node,
                               Event_Origin origin,
                               String_View out_var,
                               String_View property_name) {
    (void)node;
    (void)origin;
    if (!ctx) return false;

    SV_List values = NULL;
    if (eval_sv_eq_ci_lit(property_name, "VARIABLES")) {
        SV_List visible_names = NULL;
        if (!eval_var_collect_visible_names(ctx, &visible_names)) return false;
        for (size_t i = 0; i < arena_arr_len(visible_names); i++) {
            if (!property_append_unique_temp(ctx, &values, visible_names[i])) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "CACHE_VARIABLES")) {
        ptrdiff_t n = stbds_shlen(ctx->scope_state.cache_entries);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!ctx->scope_state.cache_entries[i].key) continue;
            if (!property_append_unique_temp(ctx,
                                             &values,
                                             nob_sv_from_cstr(ctx->scope_state.cache_entries[i].key))) {
                return false;
            }
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "MACROS")) {
        Eval_Command_State *commands = eval_command_slice(ctx);
        for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
            const User_Command *cmd = &commands->user_commands[i];
            if (cmd->kind != USER_CMD_MACRO) continue;
            if (!property_append_unique_temp(ctx, &values, cmd->name)) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "COMMANDS")) {
        Eval_Command_State *commands = eval_command_slice(ctx);
        if (ctx->registry) {
            for (size_t i = 0; i < arena_arr_len(ctx->registry->native_commands); i++) {
                String_View lowered = property_ascii_lower_temp(ctx, ctx->registry->native_commands[i].name);
                if (eval_should_stop(ctx)) return false;
                if (!property_append_unique_temp(ctx, &values, lowered)) return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
            String_View lowered = property_ascii_lower_temp(ctx, commands->user_commands[i].name);
            if (eval_should_stop(ctx)) return false;
            if (!property_append_unique_temp(ctx, &values, lowered)) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (eval_sv_eq_ci_lit(property_name, "COMPONENTS")) {
        Eval_Command_State *commands = eval_command_slice(ctx);
        for (size_t i = 0; i < arena_arr_len(commands->install_components); i++) {
            if (!property_append_unique_temp(ctx, &values, commands->install_components[i])) return false;
        }
        return set_output_var_value(ctx,
                                    out_var,
                                    eval_sv_join_semi_temp(ctx, values, arena_arr_len(values)));
    }

    if (!eval_property_query(ctx,
                             node,
                             origin,
                             out_var,
                             nob_sv_from_cstr("GLOBAL"),
                             nob_sv_from_cstr(""),
                             nob_sv_from_cstr(""),
                             property_name,
                             EVAL_PROP_QUERY_VALUE,
                             nob_sv_from_cstr(""),
                             false,
                             false)) {
        return false;
    }
    if (!eval_var_defined_visible(ctx, out_var)) return set_output_var_literal_notfound(ctx, out_var);
    return true;
}
