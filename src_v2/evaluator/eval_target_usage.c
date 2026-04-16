#include "eval_target_internal.h"
#include "bm_compile_features.h"

static bool target_usage_store_target_property(EvalExecContext *ctx,
                                               Cmake_Event_Origin origin,
                                               String_View target_name,
                                               String_View key,
                                               String_View value,
                                               Cmake_Target_Property_Op op) {
    return eval_property_write(ctx,
                               origin,
                               nob_sv_from_cstr("TARGET"),
                               target_name,
                               key,
                               value,
                               op,
                               false);
}

static bool target_usage_store_and_emit_target_property(EvalExecContext *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View target_name,
                                                        String_View key,
                                                        String_View value,
                                                        Cmake_Target_Property_Op op) {
    if (!target_usage_store_target_property(ctx, origin, target_name, key, value, op)) return false;
    return eval_emit_target_prop_set(ctx, origin, target_name, key, value, op);
}

typedef struct {
    String_View name;
    String_View type;
    int kind;
    SV_List base_dirs;
    SV_List files;
    size_t next_index;
} Target_File_Set_Parse;

typedef enum {
    TARGET_FILE_SET_HEADERS = 0,
    TARGET_FILE_SET_CXX_MODULES,
} Target_File_Set_Kind;

static Event_Target_File_Set_Kind target_file_set_kind_to_event(Target_File_Set_Kind kind) {
    return kind == TARGET_FILE_SET_CXX_MODULES
        ? EVENT_TARGET_FILE_SET_CXX_MODULES
        : EVENT_TARGET_FILE_SET_HEADERS;
}

static Event_Target_Source_Kind target_file_set_kind_to_source_kind(Target_File_Set_Kind kind) {
    return kind == TARGET_FILE_SET_CXX_MODULES
        ? EVENT_TARGET_SOURCE_FILE_SET_CXX_MODULES
        : EVENT_TARGET_SOURCE_FILE_SET_HEADERS;
}

typedef struct {
    Cmake_Visibility visibility;
    SV_List items;
} Target_Usage_Visibility_Group;

typedef struct {
    String_View target_name;
    Target_Usage_Visibility_Group *groups;
} Target_Usage_Grouped_Request;

typedef struct {
    Cmake_Visibility visibility;
    String_View item;
    bool is_before;
    bool is_system;
} Target_Usage_Item_Entry;

typedef struct {
    String_View target_name;
    Target_Usage_Item_Entry *items;
} Target_Usage_Items_Request;

typedef struct {
    String_View target_name;
    Target_Usage_Item_Entry *items;
    String_View trailing_qualifier;
} Target_Link_Libraries_Request;

typedef enum {
    TARGET_PCH_REQUEST_GROUPS = 0,
    TARGET_PCH_REQUEST_REUSE_FROM,
} Target_Pch_Request_Kind;

typedef struct {
    String_View target_name;
    Target_Pch_Request_Kind kind;
    String_View donor_target;
    Target_Usage_Visibility_Group *groups;
} Target_Precompile_Headers_Request;

typedef enum {
    TARGET_SOURCES_ENTRY_ITEM = 0,
    TARGET_SOURCES_ENTRY_FILE_SET,
} Target_Sources_Entry_Kind;

typedef struct {
    Target_Sources_Entry_Kind kind;
    Cmake_Visibility visibility;
    String_View item;
    Target_File_Set_Parse file_set;
} Target_Sources_Entry;

typedef struct {
    String_View target_name;
    Target_Sources_Entry *entries;
} Target_Sources_Request;

typedef BM_Compile_Feature_Lang Target_Compile_Feature_Lang;
typedef BM_Compile_Feature_Info Target_Compile_Feature_Info;

typedef String_View (*Target_Usage_Normalize_Fn)(EvalExecContext *ctx, String_View item);
typedef bool (*Target_Usage_Emit_Item_Fn)(EvalExecContext *ctx,
                                          Cmake_Event_Origin origin,
                                          String_View target_name,
                                          const Target_Usage_Item_Entry *entry);

static bool target_usage_visibility_writes_direct(Cmake_Visibility visibility);
static bool target_usage_visibility_writes_interface(Cmake_Visibility visibility);
static bool target_usage_emit_include_directories_entry(EvalExecContext *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View target_name,
                                                        const Target_Usage_Item_Entry *entry);

static String_View target_usage_identity_item(EvalExecContext *ctx, String_View item) {
    (void)ctx;
    return item;
}

static String_View target_compile_definition_normalize_temp(EvalExecContext *ctx, String_View item) {
    (void)ctx;
    return eval_normalize_compile_definition_item(item);
}

static String_View target_usage_resolve_path_item_temp(EvalExecContext *ctx, String_View item) {
    if (!ctx) return item;
    return eval_path_resolve_for_cmake_arg(ctx, item, eval_current_source_dir_for_paths(ctx), true);
}

static bool target_usage_target_property_temp(EvalExecContext *ctx,
                                              String_View target_name,
                                              String_View key,
                                              String_View *out_value,
                                              bool *out_set) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (out_set) *out_set = false;
    if (!ctx) return false;

    String_View prop_upper = eval_property_upper_name_temp(ctx, key);
    if (eval_should_stop(ctx)) return false;

    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!eval_property_engine_get(ctx, nob_sv_from_cstr("TARGET"), target_name, prop_upper, &value, &have)) {
        return false;
    }

    if (out_set) *out_set = have;
    if (out_value) *out_value = have ? value : nob_sv_from_cstr("");
    return true;
}

static bool target_usage_target_property_nonempty(EvalExecContext *ctx,
                                                  String_View target_name,
                                                  String_View key,
                                                  bool *out_nonempty) {
    if (out_nonempty) *out_nonempty = false;
    String_View value = nob_sv_from_cstr("");
    bool have = false;
    if (!target_usage_target_property_temp(ctx, target_name, key, &value, &have)) return false;
    if (out_nonempty) *out_nonempty = have && value.count > 0;
    return true;
}

static bool target_usage_require_interface_only_on_imported_groups(EvalExecContext *ctx,
                                                                   const Node *node,
                                                                   String_View target_name,
                                                                   const Target_Usage_Visibility_Group *groups,
                                                                   String_View cause) {
    if (!ctx || !node) return false;
    if (!eval_target_is_imported(ctx, target_name)) return true;

    for (size_t gi = 0; gi < arena_arr_len(groups); gi++) {
        if (groups[gi].visibility == EV_VISIBILITY_INTERFACE) continue;
        return target_diag_error(ctx, node, cause, target_name);
    }
    return true;
}

static bool target_usage_require_interface_only_on_imported_items(EvalExecContext *ctx,
                                                                  const Node *node,
                                                                  String_View target_name,
                                                                  const Target_Usage_Item_Entry *entries,
                                                                  String_View cause) {
    if (!ctx || !node) return false;
    if (!eval_target_is_imported(ctx, target_name)) return true;

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        if (entries[i].visibility == EV_VISIBILITY_INTERFACE) continue;
        return target_diag_error(ctx, node, cause, target_name);
    }
    return true;
}

static bool target_usage_parse_int_sv(String_View value, int *out_value) {
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

static const Target_Compile_Feature_Info *target_compile_feature_lookup(String_View feature) {
    return bm_compile_feature_lookup(feature);
}

static String_View target_compile_feature_lang_compile_var(Target_Compile_Feature_Lang lang) {
    return bm_compile_feature_lang_compile_var(lang);
}

static String_View target_compile_feature_lang_standard_prop(Target_Compile_Feature_Lang lang) {
    return bm_compile_feature_lang_standard_prop(lang);
}

static String_View target_compile_feature_lang_standard_required_prop(Target_Compile_Feature_Lang lang) {
    return bm_compile_feature_lang_standard_required_prop(lang);
}

static bool target_compile_feature_supported(EvalExecContext *ctx,
                                             const Target_Compile_Feature_Info *info) {
    if (!ctx || !info) return false;
    String_View features = eval_var_get_visible(ctx, target_compile_feature_lang_compile_var(info->lang));
    if (features.count == 0) return false;

    SV_List items = NULL;
    if (!eval_sv_split_semicolon_genex_aware(eval_temp_arena(ctx), features, &items)) return false;
    for (size_t i = 0; i < arena_arr_len(items); i++) {
        if (eval_sv_eq_ci_lit(items[i], info->name)) return true;
    }
    return false;
}

static bool target_compile_feature_apply_meta(EvalExecContext *ctx,
                                              Cmake_Event_Origin origin,
                                              String_View target_name,
                                              Cmake_Visibility visibility,
                                              const Target_Compile_Feature_Info *info) {
    if (!ctx || !info) return false;
    if (!info->meta || info->standard <= 0) return true;
    if (!target_usage_visibility_writes_direct(visibility)) return true;

    String_View standard_prop = target_compile_feature_lang_standard_prop(info->lang);
    String_View required_prop = target_compile_feature_lang_standard_required_prop(info->lang);
    if (standard_prop.count == 0 || required_prop.count == 0) return true;

    int current_standard = 0;
    String_View current = nob_sv_from_cstr("");
    bool have = false;
    if (!target_usage_target_property_temp(ctx, target_name, standard_prop, &current, &have)) return false;
    if (have) (void)target_usage_parse_int_sv(current, &current_standard);
    if (current_standard >= info->standard) return true;

    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", info->standard);
    if (n < 0 || (size_t)n >= sizeof(buf)) return false;

    if (!target_usage_store_and_emit_target_property(ctx,
                                                     origin,
                                                     target_name,
                                                     standard_prop,
                                                     nob_sv_from_parts(buf, (size_t)n),
                                                     EV_PROP_SET)) {
        return false;
    }
    return target_usage_store_and_emit_target_property(ctx,
                                                       origin,
                                                       target_name,
                                                       required_prop,
                                                       nob_sv_from_cstr("1"),
                                                       EV_PROP_SET);
}

static bool target_compile_features_validate_request(EvalExecContext *ctx,
                                                     const Node *node,
                                                     Cmake_Event_Origin origin,
                                                     const Target_Usage_Grouped_Request *req) {
    if (!ctx || !node || !req) return false;
    if (!target_usage_require_interface_only_on_imported_groups(
            ctx,
            node,
            req->target_name,
            req->groups,
            nob_sv_from_cstr("target_compile_features() may only set INTERFACE items on IMPORTED targets"))) {
        return false;
    }

    for (size_t gi = 0; gi < arena_arr_len(req->groups); gi++) {
        const Target_Usage_Visibility_Group *group = &req->groups[gi];
        for (size_t ii = 0; ii < arena_arr_len(group->items); ii++) {
            const Target_Compile_Feature_Info *info = target_compile_feature_lookup(group->items[ii]);
            if (!info) {
                return target_diag_error(ctx,
                                         node,
                                         nob_sv_from_cstr("target_compile_features() unknown compile feature"),
                                         group->items[ii]);
            }
            if (!target_compile_feature_supported(ctx, info)) {
                return target_diag_error(ctx,
                                         node,
                                         nob_sv_from_cstr("target_compile_features() requested compile feature is not available"),
                                         group->items[ii]);
            }
            if (!target_compile_feature_apply_meta(ctx,
                                                   origin,
                                                   req->target_name,
                                                   group->visibility,
                                                   info)) {
                return false;
            }
        }
    }

    return true;
}

static bool target_pch_validate_genex_item(EvalExecContext *ctx,
                                           const Node *node,
                                           String_View item) {
    if (!ctx || !node) return false;
    if (!(item.count >= 2 && item.data[0] == '$' && item.data[1] == '<')) return true;

    size_t depth = 0;
    size_t outer_colon = SIZE_MAX;
    for (size_t i = 0; i < item.count; i++) {
        if (item.data[i] == '$' && (i + 1) < item.count && item.data[i + 1] == '<') {
            depth++;
            i++;
            continue;
        }
        if (item.data[i] == '>' && depth > 0) {
            depth--;
            continue;
        }
        if (item.data[i] == ':' && depth == 1 && outer_colon == SIZE_MAX) outer_colon = i;
    }

    if (outer_colon == SIZE_MAX || outer_colon + 1 >= item.count) return true;
    String_View payload = nob_sv_from_parts(item.data + outer_colon + 1,
                                            item.count - (outer_colon + 2));
    if (payload.count == 0) return true;
    if ((payload.count >= 2 && payload.data[0] == '$' && payload.data[1] == '<') ||
        memchr(payload.data, ',', payload.count) != NULL) {
        return true;
    }
    if ((payload.data[0] == '<' && payload.data[payload.count - 1] == '>') ||
        eval_sv_is_abs_path(payload)) {
        return true;
    }

    return target_diag_error(ctx,
                             node,
                             nob_sv_from_cstr("target_precompile_headers() generator-expression headers must use absolute paths or angle brackets"),
                             item);
}

static bool target_precompile_headers_validate_request(EvalExecContext *ctx,
                                                       const Node *node,
                                                       const Target_Precompile_Headers_Request *req) {
    if (!ctx || !node || !req) return false;

    if (req->kind == TARGET_PCH_REQUEST_REUSE_FROM) {
        if (eval_target_is_imported(ctx, req->target_name)) {
            return target_diag_error(ctx,
                                     node,
                                     nob_sv_from_cstr("target_precompile_headers() may only set INTERFACE headers on IMPORTED targets"),
                                     req->target_name);
        }

        bool have_direct = false;
        bool have_interface = false;
        if (!target_usage_target_property_nonempty(ctx, req->target_name, nob_sv_from_cstr("PRECOMPILE_HEADERS"), &have_direct)) {
            return false;
        }
        if (!target_usage_target_property_nonempty(ctx,
                                                   req->target_name,
                                                   nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS"),
                                                   &have_interface)) {
            return false;
        }
        if (have_direct || have_interface) {
            return target_diag_error(ctx,
                                     node,
                                     nob_sv_from_cstr("target_precompile_headers() may not combine direct headers with REUSE_FROM"),
                                     req->target_name);
        }
        return true;
    }

    if (!target_usage_require_interface_only_on_imported_groups(
            ctx,
            node,
            req->target_name,
            req->groups,
            nob_sv_from_cstr("target_precompile_headers() may only set INTERFACE headers on IMPORTED targets"))) {
        return false;
    }

    bool has_reuse_from = false;
    if (!target_usage_target_property_nonempty(ctx,
                                               req->target_name,
                                               nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM"),
                                               &has_reuse_from)) {
        return false;
    }
    if (has_reuse_from) {
        return target_diag_error(ctx,
                                 node,
                                 nob_sv_from_cstr("target_precompile_headers() may not combine direct headers with REUSE_FROM"),
                                 req->target_name);
    }

    for (size_t gi = 0; gi < arena_arr_len(req->groups); gi++) {
        for (size_t ii = 0; ii < arena_arr_len(req->groups[gi].items); ii++) {
            if (!target_pch_validate_genex_item(ctx, node, req->groups[gi].items[ii])) return false;
        }
    }
    return true;
}

static bool target_usage_visibility_writes_direct(Cmake_Visibility visibility) {
    return visibility != EV_VISIBILITY_INTERFACE;
}

static bool target_usage_visibility_writes_interface(Cmake_Visibility visibility) {
    return visibility == EV_VISIBILITY_PUBLIC || visibility == EV_VISIBILITY_INTERFACE;
}

static Cmake_Target_Property_Op target_usage_item_property_op(const Target_Usage_Item_Entry *entry) {
    if (!entry) return EV_PROP_APPEND_LIST;
    return entry->is_before ? EV_PROP_PREPEND_LIST : EV_PROP_APPEND_LIST;
}

static String_View target_usage_wrap_build_interface_temp(EvalExecContext *ctx, String_View item) {
    String_View parts[3] = {
        nob_sv_from_cstr("$<BUILD_INTERFACE:"),
        item,
        nob_sv_from_cstr(">"),
    };
    return svu_join_no_sep_temp(ctx, parts, 3);
}

static bool target_usage_append_item_entry(EvalExecContext *ctx,
                                           Target_Usage_Item_Entry **entries,
                                           Cmake_Visibility visibility,
                                           String_View item,
                                           bool is_before,
                                           bool is_system) {
    if (!ctx || !entries) return false;
    if (item.count == 0) return true;

    Target_Usage_Item_Entry entry = {
        .visibility = visibility,
        .item = item,
        .is_before = is_before,
        .is_system = is_system,
    };
    return arena_arr_push(eval_temp_arena(ctx), *entries, entry);
}

static bool target_usage_group_append_item(EvalExecContext *ctx,
                                           Target_Usage_Visibility_Group **groups,
                                           Cmake_Visibility visibility,
                                           String_View item) {
    if (!ctx || !groups) return false;
    if (item.count == 0) return true;

    if (arena_arr_len(*groups) == 0 || arena_arr_last(*groups).visibility != visibility) {
        Target_Usage_Visibility_Group group = {0};
        group.visibility = visibility;
        if (!arena_arr_push(eval_temp_arena(ctx), *groups, group)) return false;
    }

    return svu_list_push_temp(ctx, &arena_arr_last(*groups).items, item);
}

static bool target_usage_parse_visibility_groups(EvalExecContext *ctx,
                                                 const Node *node,
                                                 SV_List args,
                                                 size_t start,
                                                 bool require_visibility,
                                                 Target_Usage_Normalize_Fn normalize,
                                                 Target_Usage_Visibility_Group **out_groups) {
    if (!ctx || !node || !out_groups) return false;
    *out_groups = NULL;

    Cmake_Visibility visibility = EV_VISIBILITY_UNSPECIFIED;
    for (size_t i = start; i < arena_arr_len(args); i++) {
        if (target_usage_parse_visibility(args[i], &visibility)) continue;
        if (require_visibility && visibility == EV_VISIBILITY_UNSPECIFIED) {
            return target_usage_require_visibility(ctx, node);
        }

        String_View item = normalize ? normalize(ctx, args[i]) : args[i];
        if (eval_should_stop(ctx)) return false;
        if (!target_usage_group_append_item(ctx, out_groups, visibility, item)) return false;
    }

    return true;
}

static bool target_usage_apply_visibility_groups(EvalExecContext *ctx,
                                                 Cmake_Event_Origin origin,
                                                 String_View target_name,
                                                 const Target_Usage_Visibility_Group *groups,
                                                 String_View direct_prop,
                                                 String_View interface_prop) {
    if (!ctx) return false;

    for (size_t gi = 0; gi < arena_arr_len(groups); gi++) {
        const Target_Usage_Visibility_Group *group = &groups[gi];
        for (size_t ii = 0; ii < arena_arr_len(group->items); ii++) {
            String_View item = group->items[ii];
            if (target_usage_visibility_writes_direct(group->visibility)) {
                if (!target_usage_store_and_emit_target_property(ctx,
                                                                 origin,
                                                                 target_name,
                                                                 direct_prop,
                                                                 item,
                                                                 EV_PROP_APPEND_LIST)) {
                    return false;
                }
            }
            if (target_usage_visibility_writes_interface(group->visibility)) {
                if (!target_usage_store_and_emit_target_property(ctx,
                                                                 origin,
                                                                 target_name,
                                                                 interface_prop,
                                                                 item,
                                                                 EV_PROP_APPEND_LIST)) {
                    return false;
                }
            }
        }
    }

    return true;
}

static bool target_usage_apply_item_entries(EvalExecContext *ctx,
                                            Cmake_Event_Origin origin,
                                            String_View target_name,
                                            const Target_Usage_Item_Entry *entries,
                                            String_View direct_prop,
                                            String_View interface_prop,
                                            Target_Usage_Emit_Item_Fn emit_item) {
    if (!ctx) return false;

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        const Target_Usage_Item_Entry *entry = &entries[i];
        Cmake_Target_Property_Op prop_op = target_usage_item_property_op(entry);

        if (direct_prop.count > 0 && target_usage_visibility_writes_direct(entry->visibility)) {
            if (!target_usage_store_target_property(
                    ctx, origin, target_name, direct_prop, entry->item, prop_op)) {
                return false;
            }
        }
        if (interface_prop.count > 0 && target_usage_visibility_writes_interface(entry->visibility)) {
            if (!target_usage_store_target_property(
                    ctx, origin, target_name, interface_prop, entry->item, prop_op)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        const Target_Usage_Item_Entry *entry = &entries[i];
        if (emit_item && !emit_item(ctx, origin, target_name, entry)) return false;
    }

    return true;
}

static bool target_usage_apply_include_directory_entries(EvalExecContext *ctx,
                                                         Cmake_Event_Origin origin,
                                                         String_View target_name,
                                                         const Target_Usage_Item_Entry *entries) {
    if (!ctx) return false;

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        const Target_Usage_Item_Entry *entry = &entries[i];
        Cmake_Target_Property_Op prop_op = target_usage_item_property_op(entry);

        if (target_usage_visibility_writes_direct(entry->visibility)) {
            if (!target_usage_store_target_property(ctx,
                                                    origin,
                                                    target_name,
                                                    nob_sv_from_cstr("INCLUDE_DIRECTORIES"),
                                                    entry->item,
                                                    prop_op)) {
                return false;
            }
        }
        if (target_usage_visibility_writes_interface(entry->visibility)) {
            if (!target_usage_store_target_property(ctx,
                                                    origin,
                                                    target_name,
                                                    nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES"),
                                                    entry->item,
                                                    prop_op)) {
                return false;
            }
            if (entry->is_system &&
                !target_usage_store_target_property(ctx,
                                                    origin,
                                                    target_name,
                                                    nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES"),
                                                    entry->item,
                                                    prop_op)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        const Target_Usage_Item_Entry *entry = &entries[i];
        if (!target_usage_emit_include_directories_entry(ctx, origin, target_name, entry)) return false;
    }

    return true;
}

static bool target_usage_emit_link_libraries_entry(EvalExecContext *ctx,
                                                   Cmake_Event_Origin origin,
                                                   String_View target_name,
                                                   const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_link_libraries(ctx, origin, target_name, entry->visibility, entry->item);
}

static bool target_usage_emit_link_options_entry(EvalExecContext *ctx,
                                                 Cmake_Event_Origin origin,
                                                 String_View target_name,
                                                 const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_link_options(
        ctx, origin, target_name, entry->visibility, entry->item, entry->is_before);
}

static bool target_usage_emit_link_directories_entry(EvalExecContext *ctx,
                                                     Cmake_Event_Origin origin,
                                                     String_View target_name,
                                                     const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_link_directories(ctx, origin, target_name, entry->visibility, entry->item);
}

static bool target_usage_emit_include_directories_entry(EvalExecContext *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View target_name,
                                                        const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_include_directories(ctx,
                                                origin,
                                                target_name,
                                                entry->visibility,
                                                entry->item,
                                                entry->is_system,
                                                entry->is_before);
}

static bool target_usage_emit_compile_definitions_entry(EvalExecContext *ctx,
                                                        Cmake_Event_Origin origin,
                                                        String_View target_name,
                                                        const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_compile_definitions(ctx, origin, target_name, entry->visibility, entry->item);
}

static bool target_usage_emit_compile_options_entry(EvalExecContext *ctx,
                                                    Cmake_Event_Origin origin,
                                                    String_View target_name,
                                                    const Target_Usage_Item_Entry *entry) {
    return eval_emit_target_compile_options(
        ctx, origin, target_name, entry->visibility, entry->item, entry->is_before);
}

static bool target_usage_apply_compile_feature_groups(EvalExecContext *ctx,
                                                      Cmake_Event_Origin origin,
                                                      String_View target_name,
                                                      const Target_Usage_Visibility_Group *groups) {
    if (!ctx) return false;

    for (size_t gi = 0; gi < arena_arr_len(groups); gi++) {
        const Target_Usage_Visibility_Group *group = &groups[gi];
        for (size_t ii = 0; ii < arena_arr_len(group->items); ii++) {
            String_View item = group->items[ii];
            if (target_usage_visibility_writes_direct(group->visibility) &&
                !target_usage_store_target_property(ctx,
                                                    origin,
                                                    target_name,
                                                    nob_sv_from_cstr("COMPILE_FEATURES"),
                                                    item,
                                                    EV_PROP_APPEND_LIST)) {
                return false;
            }
            if (target_usage_visibility_writes_interface(group->visibility) &&
                !target_usage_store_target_property(ctx,
                                                    origin,
                                                    target_name,
                                                    nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                                    item,
                                                    EV_PROP_APPEND_LIST)) {
                return false;
            }
            if (!eval_emit_target_compile_features(ctx, origin, target_name, group->visibility, item)) {
                return false;
            }
        }
    }

    return true;
}

static bool target_usage_parse_item_request(EvalExecContext *ctx,
                                            const Node *node,
                                            Cmake_Event_Origin origin,
                                            SV_List args,
                                            String_View missing_cause,
                                            String_View usage_hint,
                                            bool require_visibility,
                                            bool allow_before,
                                            bool allow_system,
                                            bool allow_after,
                                            Target_Usage_Normalize_Fn normalize,
                                            Target_Usage_Items_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       origin,
                                       EV_DIAG_ERROR,
                                       EVAL_DIAG_MISSING_REQUIRED,
                                       "dispatcher",
                                       missing_cause,
                                       usage_hint);
        return false;
    }

    *out_req = (Target_Usage_Items_Request){0};
    out_req->target_name = args[0];
    if (!target_usage_validate_target(ctx, node, out_req->target_name)) return false;

    Cmake_Visibility visibility = EV_VISIBILITY_UNSPECIFIED;
    bool is_before = false;
    bool is_system = false;
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (allow_system && eval_sv_eq_ci_lit(args[i], "SYSTEM")) {
            is_system = true;
            continue;
        }
        if (allow_before && eval_sv_eq_ci_lit(args[i], "BEFORE")) {
            is_before = true;
            continue;
        }
        if (allow_after && eval_sv_eq_ci_lit(args[i], "AFTER")) {
            is_before = false;
            continue;
        }
        if (target_usage_parse_visibility(args[i], &visibility)) continue;
        if (require_visibility && visibility == EV_VISIBILITY_UNSPECIFIED) {
            return target_usage_require_visibility(ctx, node);
        }

        String_View item = normalize ? normalize(ctx, args[i]) : args[i];
        if (eval_should_stop(ctx)) return false;
        if (!target_usage_append_item_entry(
                ctx, &out_req->items, visibility, item, is_before, is_system)) {
            return false;
        }
    }

    return true;
}

static bool target_compile_features_parse_request(EvalExecContext *ctx,
                                                  const Node *node,
                                                  Cmake_Event_Origin origin,
                                                  SV_List args,
                                                  Target_Usage_Grouped_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("target_compile_features() requires target, visibility and features"),
            nob_sv_from_cstr("Usage: target_compile_features(<tgt> <PUBLIC|PRIVATE|INTERFACE> <features...>)"));
        return false;
    }

    *out_req = (Target_Usage_Grouped_Request){0};
    out_req->target_name = args[0];
    if (!target_usage_validate_target(ctx, node, out_req->target_name)) return false;

    return target_usage_parse_visibility_groups(
        ctx, node, args, 1, true, target_usage_identity_item, &out_req->groups);
}

static bool target_precompile_headers_parse_request(EvalExecContext *ctx,
                                                    const Node *node,
                                                    Cmake_Event_Origin origin,
                                                    SV_List args,
                                                    Target_Precompile_Headers_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("target_precompile_headers() requires target and arguments"),
            nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> <PUBLIC|PRIVATE|INTERFACE> <headers...>)"));
        return false;
    }

    *out_req = (Target_Precompile_Headers_Request){0};
    out_req->target_name = args[0];
    if (!target_usage_validate_target(ctx, node, out_req->target_name)) return false;

    if (eval_sv_eq_ci_lit(args[1], "REUSE_FROM")) {
        if (arena_arr_len(args) != 3) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
                ctx,
                node,
                origin,
                EV_DIAG_ERROR,
                EVAL_DIAG_MISSING_REQUIRED,
                "dispatcher",
                nob_sv_from_cstr("target_precompile_headers(REUSE_FROM) expects exactly one donor target"),
                nob_sv_from_cstr("Usage: target_precompile_headers(<tgt> REUSE_FROM <other-target>)"));
            return false;
        }
        if (!target_usage_validate_target(ctx, node, args[2])) return false;
        out_req->kind = TARGET_PCH_REQUEST_REUSE_FROM;
        out_req->donor_target = args[2];
        return true;
    }

    out_req->kind = TARGET_PCH_REQUEST_GROUPS;
    return target_usage_parse_visibility_groups(
        ctx, node, args, 1, true, target_pch_item_normalize_temp, &out_req->groups);
}

static bool target_link_libraries_parse_request(EvalExecContext *ctx,
                                                const Node *node,
                                                Cmake_Event_Origin origin,
                                                SV_List args,
                                                Target_Link_Libraries_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("target_link_libraries() requires target and at least one item"),
            nob_sv_from_cstr("Usage: target_link_libraries(<tgt> [PRIVATE|PUBLIC|INTERFACE] <item>...)"));
        return false;
    }

    *out_req = (Target_Link_Libraries_Request){0};
    out_req->target_name = args[0];
    if (!target_usage_validate_target(ctx, node, out_req->target_name)) return false;

    Cmake_Visibility visibility = EV_VISIBILITY_UNSPECIFIED;
    String_View qualifier = nob_sv_from_cstr("");
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (target_usage_parse_visibility(args[i], &visibility)) continue;
        if (eval_sv_eq_ci_lit(args[i], "DEBUG") ||
            eval_sv_eq_ci_lit(args[i], "OPTIMIZED") ||
            eval_sv_eq_ci_lit(args[i], "GENERAL")) {
            qualifier = args[i];
            continue;
        }

        String_View item = args[i];
        if (eval_sv_eq_ci_lit(qualifier, "DEBUG")) {
            item = wrap_link_item_with_config_genex_temp(
                ctx, item, nob_sv_from_cstr("$<$<CONFIG:Debug>:"));
        } else if (eval_sv_eq_ci_lit(qualifier, "OPTIMIZED")) {
            item = wrap_link_item_with_config_genex_temp(
                ctx, item, nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:"));
        }
        if (eval_should_stop(ctx)) return false;

        if (!target_usage_append_item_entry(
                ctx, &out_req->items, visibility, item, false, false)) {
            return false;
        }
        qualifier = nob_sv_from_cstr("");
    }

    out_req->trailing_qualifier = qualifier;
    return true;
}

static bool target_sources_is_file_set_clause_keyword(String_View tok) {
    return eval_sv_eq_ci_lit(tok, "TYPE") ||
           eval_sv_eq_ci_lit(tok, "BASE_DIRS") ||
           eval_sv_eq_ci_lit(tok, "FILES");
}

static bool target_sources_is_group_boundary(String_View tok) {
    return target_usage_parse_visibility(tok, &(Cmake_Visibility){0}) ||
           eval_sv_eq_ci_lit(tok, "FILE_SET");
}

static bool target_sources_file_set_kind_from_type(String_View type, Target_File_Set_Kind *out_kind) {
    if (!out_kind) return false;
    if (eval_sv_eq_ci_lit(type, "HEADERS")) {
        *out_kind = TARGET_FILE_SET_HEADERS;
        return true;
    }
    if (eval_sv_eq_ci_lit(type, "CXX_MODULES")) {
        *out_kind = TARGET_FILE_SET_CXX_MODULES;
        return true;
    }
    return false;
}

static bool target_sources_append_file_set_items(EvalExecContext *ctx,
                                                 SV_List args,
                                                 size_t *io_index,
                                                 String_View current_src_dir,
                                                 SV_List *out_items) {
    if (!ctx || !io_index || !out_items) return false;
    while (*io_index < arena_arr_len(args)) {
        String_View tok = args[*io_index];
        if (target_sources_is_group_boundary(tok) || target_sources_is_file_set_clause_keyword(tok)) break;
        String_View resolved = eval_path_resolve_for_cmake_arg(ctx, tok, current_src_dir, true);
        if (eval_should_stop(ctx)) return false;
        if (!svu_list_push_temp(ctx, out_items, resolved)) return false;
        (*io_index)++;
    }
    return true;
}

static bool target_sources_parse_file_set(EvalExecContext *ctx,
                                          const Node *node,
                                          Cmake_Event_Origin origin,
                                          SV_List args,
                                          size_t start,
                                          Target_File_Set_Parse *out_parse) {
    if (!ctx || !node || !out_parse) return false;
    *out_parse = (Target_File_Set_Parse){0};

    if (start >= arena_arr_len(args) || target_sources_is_group_boundary(args[start])) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires a set name"),
                          nob_sv_from_cstr("Usage: target_sources(<tgt> <vis> FILE_SET <name> [TYPE HEADERS|CXX_MODULES] [BASE_DIRS <dir>...] FILES <file>...)"));
        return false;
    }

    out_parse->name = args[start++];
    if (eval_sv_eq_ci_lit(out_parse->name, "HEADERS")) {
        out_parse->type = nob_sv_from_cstr("HEADERS");
    } else if (eval_sv_eq_ci_lit(out_parse->name, "CXX_MODULES")) {
        out_parse->type = nob_sv_from_cstr("CXX_MODULES");
    }

    bool saw_base_dirs = false;
    bool saw_files = false;
    String_View current_src_dir = eval_current_source_dir_for_paths(ctx);

    while (start < arena_arr_len(args)) {
        String_View tok = args[start];
        if (target_sources_is_group_boundary(tok)) break;

        if (eval_sv_eq_ci_lit(tok, "TYPE")) {
            if (start + 1 >= arena_arr_len(args) ||
                target_sources_is_group_boundary(args[start + 1]) ||
                target_sources_is_file_set_clause_keyword(args[start + 1])) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET TYPE) requires a file-set type"),
                                  nob_sv_from_cstr("Supported types in this batch: HEADERS, CXX_MODULES"));
                return false;
            }
            out_parse->type = args[start + 1];
            start += 2;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "BASE_DIRS")) {
            start++;
            size_t before = arena_arr_len(out_parse->base_dirs);
            if (!target_sources_append_file_set_items(ctx,
                                                      args,
                                                      &start,
                                                      current_src_dir,
                                                      &out_parse->base_dirs)) {
                return false;
            }
            if (arena_arr_len(out_parse->base_dirs) == before) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET BASE_DIRS) requires at least one base directory"),
                                  nob_sv_from_cstr(""));
                return false;
            }
            saw_base_dirs = true;
            continue;
        }

        if (eval_sv_eq_ci_lit(tok, "FILES")) {
            start++;
            size_t before = arena_arr_len(out_parse->files);
            if (!target_sources_append_file_set_items(ctx,
                                                      args,
                                                      &start,
                                                      current_src_dir,
                                                      &out_parse->files)) {
                return false;
            }
            if (arena_arr_len(out_parse->files) == before) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET FILES) requires at least one file"),
                                  nob_sv_from_cstr(""));
                return false;
            }
            saw_files = true;
            continue;
        }

        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) received an unexpected argument"),
                          tok);
        return false;
    }

    if (out_parse->type.count == 0) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires TYPE for non-default file-set names"),
                          nob_sv_from_cstr("Use FILE_SET HEADERS ..., FILE_SET CXX_MODULES ..., or FILE_SET <name> TYPE HEADERS|CXX_MODULES ..."));
        return false;
    }
    Target_File_Set_Kind kind = TARGET_FILE_SET_HEADERS;
    if (!target_sources_file_set_kind_from_type(out_parse->type, &kind)) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) currently supports only TYPE HEADERS or TYPE CXX_MODULES"),
                          out_parse->type);
        return false;
    }
    out_parse->kind = (int)kind;
    if (!saw_base_dirs) {
        if (!svu_list_push_temp(ctx, &out_parse->base_dirs, current_src_dir)) return false;
    }
    if (!saw_files) {
        target_diag_error(ctx,
                          node,
                          nob_sv_from_cstr("target_sources(FILE_SET ...) requires FILES"),
                          nob_sv_from_cstr(""));
        return false;
    }

    out_parse->next_index = start;
    (void)origin;
    return true;
}

static bool target_sources_parse_request(EvalExecContext *ctx,
                                         const Node *node,
                                         Cmake_Event_Origin origin,
                                         SV_List args,
                                         Target_Sources_Request *out_req) {
    if (!ctx || !node || !out_req) return false;
    if (arena_arr_len(args) < 3) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            origin,
            EV_DIAG_ERROR,
            EVAL_DIAG_MISSING_REQUIRED,
            "dispatcher",
            nob_sv_from_cstr("target_sources() requires target, visibility and items"),
            nob_sv_from_cstr("Usage: target_sources(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"));
        return false;
    }

    *out_req = (Target_Sources_Request){0};
    out_req->target_name = args[0];
    if (!target_usage_validate_target(ctx, node, out_req->target_name)) return false;

    Cmake_Visibility visibility = EV_VISIBILITY_UNSPECIFIED;
    String_View current_src_dir = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 1; i < arena_arr_len(args); i++) {
        if (target_usage_parse_visibility(args[i], &visibility)) continue;

        if (eval_sv_eq_ci_lit(args[i], "FILE_SET")) {
            if (visibility == EV_VISIBILITY_UNSPECIFIED) {
                return target_usage_require_visibility(ctx, node);
            }

            Target_Sources_Entry entry = {0};
            entry.kind = TARGET_SOURCES_ENTRY_FILE_SET;
            entry.visibility = visibility;
            if (!target_sources_parse_file_set(ctx, node, origin, args, i + 1, &entry.file_set)) {
                return false;
            }
            if (!arena_arr_push(eval_temp_arena(ctx), out_req->entries, entry)) return false;
            i = entry.file_set.next_index - 1;
            continue;
        }

        if (visibility == EV_VISIBILITY_UNSPECIFIED) {
            return target_usage_require_visibility(ctx, node);
        }

        Target_Sources_Entry entry = {0};
        entry.kind = TARGET_SOURCES_ENTRY_ITEM;
        entry.visibility = visibility;
        entry.item = eval_path_resolve_for_cmake_arg(ctx, args[i], current_src_dir, true);
        if (eval_should_stop(ctx)) return false;
        if (!arena_arr_push(eval_temp_arena(ctx), out_req->entries, entry)) return false;
    }

    return true;
}

static bool target_sources_store_file_set(EvalExecContext *ctx,
                                          Cmake_Event_Origin origin,
                                          String_View target_name,
                                          Cmake_Visibility vis,
                                          const Target_File_Set_Parse *file_set) {
    if (!ctx || !file_set) return false;
    String_View set_name_upper = eval_property_upper_name_temp(ctx, file_set->name);
    if (eval_should_stop(ctx)) return false;

    bool is_headers = file_set->kind == TARGET_FILE_SET_HEADERS;
    bool is_default_set =
        (is_headers && eval_sv_eq_ci_lit(file_set->name, "HEADERS")) ||
        (!is_headers && eval_sv_eq_ci_lit(file_set->name, "CXX_MODULES"));
    const char *sets_prop_name = is_headers ? "HEADER_SETS" : "CXX_MODULE_SETS";
    const char *interface_sets_prop_name = is_headers ? "INTERFACE_HEADER_SETS" : "INTERFACE_CXX_MODULE_SETS";
    const char *set_prop_prefix = is_headers ? "HEADER_SET_" : "CXX_MODULE_SET_";
    const char *dirs_prop_prefix = is_headers ? "HEADER_DIRS_" : "CXX_MODULE_DIRS_";
    const char *set_prop_default = is_headers ? "HEADER_SET" : "CXX_MODULE_SET";
    const char *dirs_prop_default = is_headers ? "HEADER_DIRS" : "CXX_MODULE_DIRS";

    String_View set_prop = nob_sv_from_cstr(nob_temp_sprintf("%s%.*s",
                                                             set_prop_prefix,
                                                             (int)set_name_upper.count,
                                                             set_name_upper.data));
    String_View dirs_prop = nob_sv_from_cstr(nob_temp_sprintf("%s%.*s",
                                                              dirs_prop_prefix,
                                                              (int)set_name_upper.count,
                                                              set_name_upper.data));
    if (!eval_emit_target_file_set_declare(ctx,
                                           origin,
                                           target_name,
                                           file_set->name,
                                           target_file_set_kind_to_event((Target_File_Set_Kind)file_set->kind),
                                           vis)) {
        return false;
    }
    if (vis != EV_VISIBILITY_INTERFACE) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(sets_prop_name),
                                                         file_set->name,
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }
    if (vis != EV_VISIBILITY_PRIVATE) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(interface_sets_prop_name),
                                                         file_set->name,
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }

    for (size_t i = 0; i < arena_arr_len(file_set->base_dirs); i++) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         dirs_prop,
                                                         file_set->base_dirs[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
        if (!eval_emit_target_file_set_add_base_dir(ctx,
                                                    origin,
                                                    target_name,
                                                    file_set->name,
                                                    file_set->base_dirs[i])) {
            return false;
        }
        if (is_default_set &&
            !target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(dirs_prop_default),
                                                         file_set->base_dirs[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
        if (is_headers) {
            String_View build_interface_dir =
                target_usage_wrap_build_interface_temp(ctx, file_set->base_dirs[i]);
            if (eval_should_stop(ctx)) return false;

            if (vis != EV_VISIBILITY_INTERFACE &&
                !target_usage_store_and_emit_target_property(ctx,
                                                             origin,
                                                             target_name,
                                                             nob_sv_from_cstr("INCLUDE_DIRECTORIES"),
                                                             build_interface_dir,
                                                             EV_PROP_APPEND_LIST)) {
                return false;
            }
            if (vis != EV_VISIBILITY_PRIVATE &&
                !target_usage_store_and_emit_target_property(ctx,
                                                             origin,
                                                             target_name,
                                                             nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES"),
                                                             build_interface_dir,
                                                             EV_PROP_APPEND_LIST)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < arena_arr_len(file_set->files); i++) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         set_prop,
                                                         file_set->files[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
        if (!eval_emit_target_add_source(ctx,
                                         origin,
                                         target_name,
                                         vis,
                                         file_set->files[i],
                                         target_file_set_kind_to_source_kind((Target_File_Set_Kind)file_set->kind),
                                         file_set->name)) {
            return false;
        }
        if (is_default_set &&
            !target_usage_store_and_emit_target_property(ctx,
                                                         origin,
                                                         target_name,
                                                         nob_sv_from_cstr(set_prop_default),
                                                         file_set->files[i],
                                                         EV_PROP_APPEND_LIST)) {
            return false;
        }
    }
    return true;
}

Eval_Result eval_handle_add_dependencies(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(a) < 2) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "dispatcher", nob_sv_from_cstr("add_dependencies() requires target and at least one dependency"), nob_sv_from_cstr("Usage: add_dependencies(<target> <target-dependency>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View target_name = a[0];
    if (!eval_target_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() target was not declared"), target_name);
        return eval_result_from_ctx(ctx);
    }
    if (eval_target_alias_known(ctx, target_name)) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot be used on ALIAS targets"), target_name);
        return eval_result_from_ctx(ctx);
    }

    for (size_t i = 1; i < arena_arr_len(a); i++) {
        String_View dep = a[i];
        if (dep.count == 0) continue;

        if (!eval_target_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_FOUND, "dispatcher", nob_sv_from_cstr("add_dependencies() dependency target was not declared"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (eval_target_alias_known(ctx, dep)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_STATE, "dispatcher", nob_sv_from_cstr("add_dependencies() cannot depend on ALIAS targets"), dep);
            return eval_result_from_ctx(ctx);
        }
        if (!eval_property_write(ctx,
                                 o,
                                 nob_sv_from_cstr("TARGET"),
                                 target_name,
                                 nob_sv_from_cstr("MANUALLY_ADDED_DEPENDENCIES"),
                                 dep,
                                 EV_PROP_APPEND_LIST,
                                 false)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_emit_target_dependency(ctx, o, target_name, dep)) return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_libraries(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Link_Libraries_Request req = {0};
    if (!target_link_libraries_parse_request(ctx, node, o, a, &req)) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_item_entries(ctx,
                                         o,
                                         req.target_name,
                                         req.items,
                                         nob_sv_from_cstr("LINK_LIBRARIES"),
                                         nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"),
                                         target_usage_emit_link_libraries_entry)) {
        return eval_result_from_ctx(ctx);
    }

    if (req.trailing_qualifier.count > 0) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx,
                                       node,
                                       o,
                                       EV_DIAG_WARNING,
                                       EVAL_DIAG_INVALID_STATE,
                                       "dispatcher",
                                       nob_sv_from_cstr("target_link_libraries() qualifier without following item"),
                                       req.trailing_qualifier);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_options(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Items_Request req = {0};
    if (!target_usage_parse_item_request(
            ctx,
            node,
            o,
            a,
            nob_sv_from_cstr("target_link_options() requires target and items"),
            nob_sv_from_cstr("Usage: target_link_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"),
            true,
            true,
            false,
            false,
            target_usage_identity_item,
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_usage_require_interface_only_on_imported_items(
            ctx,
            node,
            req.target_name,
            req.items,
            nob_sv_from_cstr("target_link_options() may only set INTERFACE items on IMPORTED targets"))) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_item_entries(ctx,
                                         o,
                                         req.target_name,
                                         req.items,
                                         nob_sv_from_cstr("LINK_OPTIONS"),
                                         nob_sv_from_cstr("INTERFACE_LINK_OPTIONS"),
                                         target_usage_emit_link_options_entry)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_link_directories(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Items_Request req = {0};
    if (!target_usage_parse_item_request(
            ctx,
            node,
            o,
            a,
            nob_sv_from_cstr("target_link_directories() requires target and items"),
            nob_sv_from_cstr("Usage: target_link_directories(<tgt> <PUBLIC|PRIVATE|INTERFACE> <dirs...>)"),
            true,
            false,
            false,
            false,
            target_usage_resolve_path_item_temp,
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_usage_require_interface_only_on_imported_items(
            ctx,
            node,
            req.target_name,
            req.items,
            nob_sv_from_cstr("target_link_directories() may only set INTERFACE items on IMPORTED targets"))) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_item_entries(ctx,
                                         o,
                                         req.target_name,
                                         req.items,
                                         nob_sv_from_cstr("LINK_DIRECTORIES"),
                                         nob_sv_from_cstr("INTERFACE_LINK_DIRECTORIES"),
                                         target_usage_emit_link_directories_entry)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_include_directories(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Items_Request req = {0};
    if (!target_usage_parse_item_request(
            ctx,
            node,
            o,
            a,
            nob_sv_from_cstr("target_include_directories() requires target and items"),
            nob_sv_from_cstr("Usage: target_include_directories(<tgt> [SYSTEM] [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"),
            true,
            true,
            true,
            true,
            target_usage_resolve_path_item_temp,
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_usage_require_interface_only_on_imported_items(
            ctx,
            node,
            req.target_name,
            req.items,
            nob_sv_from_cstr("target_include_directories() may only set INTERFACE items on IMPORTED targets"))) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_include_directory_entries(ctx, o, req.target_name, req.items)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_definitions(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Items_Request req = {0};
    if (!target_usage_parse_item_request(
            ctx,
            node,
            o,
            a,
            nob_sv_from_cstr("target_compile_definitions() requires target and items"),
            nob_sv_from_cstr("Usage: target_compile_definitions(<tgt> <PUBLIC|PRIVATE|INTERFACE> <items...>)"),
            true,
            false,
            false,
            false,
            target_compile_definition_normalize_temp,
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_usage_require_interface_only_on_imported_items(
            ctx,
            node,
            req.target_name,
            req.items,
            nob_sv_from_cstr("target_compile_definitions() may only set INTERFACE items on IMPORTED targets"))) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_item_entries(ctx,
                                         o,
                                         req.target_name,
                                         req.items,
                                         nob_sv_from_cstr("COMPILE_DEFINITIONS"),
                                         nob_sv_from_cstr("INTERFACE_COMPILE_DEFINITIONS"),
                                         target_usage_emit_compile_definitions_entry)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_options(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Items_Request req = {0};
    if (!target_usage_parse_item_request(
            ctx,
            node,
            o,
            a,
            nob_sv_from_cstr("target_compile_options() requires target and items"),
            nob_sv_from_cstr("Usage: target_compile_options(<tgt> [BEFORE] <PUBLIC|PRIVATE|INTERFACE> <items...>)"),
            true,
            true,
            false,
            false,
            target_usage_identity_item,
            &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_usage_require_interface_only_on_imported_items(
            ctx,
            node,
            req.target_name,
            req.items,
            nob_sv_from_cstr("target_compile_options() may only set INTERFACE items on IMPORTED targets"))) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_item_entries(ctx,
                                         o,
                                         req.target_name,
                                         req.items,
                                         nob_sv_from_cstr("COMPILE_OPTIONS"),
                                         nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS"),
                                         target_usage_emit_compile_options_entry)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_sources(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Sources_Request req = {0};
    if (!target_sources_parse_request(ctx, node, o, a, &req)) {
        return eval_result_from_ctx(ctx);
    }

    for (size_t i = 0; i < arena_arr_len(req.entries); i++) {
        const Target_Sources_Entry *entry = &req.entries[i];
        if (entry->kind == TARGET_SOURCES_ENTRY_FILE_SET) {
            if (entry->file_set.kind == TARGET_FILE_SET_CXX_MODULES &&
                entry->visibility == EV_VISIBILITY_INTERFACE &&
                !eval_target_is_imported(ctx, req.target_name)) {
                target_diag_error(ctx,
                                  node,
                                  nob_sv_from_cstr("target_sources(FILE_SET TYPE CXX_MODULES) may not use INTERFACE scope on non-IMPORTED targets"),
                                  req.target_name);
                return eval_result_from_ctx(ctx);
            }
            if (!target_sources_store_file_set(
                    ctx, o, req.target_name, entry->visibility, &entry->file_set)) {
                return eval_result_from_ctx(ctx);
            }
            continue;
        }

        if (entry->visibility != EV_VISIBILITY_INTERFACE) {
            if (!target_usage_store_target_property(ctx,
                                                    o,
                                                    req.target_name,
                                                    nob_sv_from_cstr("SOURCES"),
                                                    entry->item,
                                                    EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (entry->visibility != EV_VISIBILITY_PRIVATE) {
            if (!target_usage_store_and_emit_target_property(ctx,
                                                             o,
                                                             req.target_name,
                                                             nob_sv_from_cstr("INTERFACE_SOURCES"),
                                                             entry->item,
                                                             EV_PROP_APPEND_LIST)) {
                return eval_result_from_ctx(ctx);
            }
        }
        if (!eval_emit_target_add_source(ctx,
                                         o,
                                         req.target_name,
                                         entry->visibility,
                                         entry->item,
                                         EVENT_TARGET_SOURCE_REGULAR,
                                         nob_sv_from_cstr(""))) {
            return eval_result_from_ctx(ctx);
        }
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_compile_features(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Usage_Grouped_Request req = {0};
    if (!target_compile_features_parse_request(ctx, node, o, a, &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_compile_features_validate_request(ctx, node, o, &req)) {
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_compile_feature_groups(ctx, o, req.target_name, req.groups)) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_target_precompile_headers(EvalExecContext *ctx, const Node *node) {
    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List a = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    Target_Precompile_Headers_Request req = {0};
    if (!target_precompile_headers_parse_request(ctx, node, o, a, &req)) {
        return eval_result_from_ctx(ctx);
    }
    if (!target_precompile_headers_validate_request(ctx, node, &req)) {
        return eval_result_from_ctx(ctx);
    }

    if (req.kind == TARGET_PCH_REQUEST_REUSE_FROM) {
        if (!target_usage_store_and_emit_target_property(ctx,
                                                         o,
                                                         req.target_name,
                                                         nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM"),
                                                         req.donor_target,
                                                         EV_PROP_SET)) {
            return eval_result_from_ctx(ctx);
        }
        if (!eval_sv_key_eq(req.target_name, req.donor_target)) {
            if (!eval_emit_target_dependency(ctx, o, req.target_name, req.donor_target)) {
                return eval_result_from_ctx(ctx);
            }
        }
        return eval_result_from_ctx(ctx);
    }

    if (!target_usage_apply_visibility_groups(ctx,
                                              o,
                                              req.target_name,
                                              req.groups,
                                              nob_sv_from_cstr("PRECOMPILE_HEADERS"),
                                              nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS"))) {
        return eval_result_from_ctx(ctx);
    }

    return eval_result_from_ctx(ctx);
}
