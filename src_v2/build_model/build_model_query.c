#include "build_model_query_internal.h"

static bool bm_eval_item_span(const Build_Model *model,
                              BM_Target_Id owner_target_id,
                              const BM_Query_Eval_Context *ctx,
                              Arena *scratch,
                              BM_String_Item_Span raw_items,
                              BM_String_Item_Span *out);
static bool bm_eval_link_item_span(const Build_Model *model,
                                   BM_Target_Id owner_target_id,
                                   const BM_Query_Eval_Context *ctx,
                                   Arena *scratch,
                                   BM_Link_Item_Span raw_items,
                                   BM_Link_Item_Span *out);
static bool bm_query_target_raw_property_first(const Build_Model *model,
                                               BM_Target_Id id,
                                               String_View property_name,
                                               String_View *out);
static bool bm_append_split_values(Arena *scratch,
                                   BM_String_Item_View **out,
                                   BM_String_Item_View item,
                                   String_View value);
static bool bm_append_split_link_values(Arena *scratch,
                                        BM_Link_Item_View **out,
                                        BM_Link_Item_View item,
                                        String_View value);

static const BM_Directory_Record *bm_model_directory(const Build_Model *model, BM_Directory_Id id) {
    if (!model || id == BM_DIRECTORY_ID_INVALID || (size_t)id >= arena_arr_len(model->directories)) return NULL;
    return &model->directories[id];
}

static const BM_Target_Record *bm_model_target(const Build_Model *model, BM_Target_Id id) {
    if (!model || id == BM_TARGET_ID_INVALID || (size_t)id >= arena_arr_len(model->targets)) return NULL;
    return &model->targets[id];
}

static const BM_Build_Step_Record *bm_model_build_step(const Build_Model *model, BM_Build_Step_Id id) {
    if (!model || id == BM_BUILD_STEP_ID_INVALID || (size_t)id >= arena_arr_len(model->build_steps)) return NULL;
    return &model->build_steps[id];
}

static const BM_Replay_Action_Record *bm_model_replay_action(const Build_Model *model, BM_Replay_Action_Id id) {
    if (!model || id == BM_REPLAY_ACTION_ID_INVALID || (size_t)id >= arena_arr_len(model->replay_actions)) return NULL;
    return &model->replay_actions[id];
}

static const BM_Test_Record *bm_model_test(const Build_Model *model, BM_Test_Id id) {
    if (!model || id == BM_TEST_ID_INVALID || (size_t)id >= arena_arr_len(model->tests)) return NULL;
    return &model->tests[id];
}

static const BM_Install_Rule_Record *bm_model_install_rule(const Build_Model *model, BM_Install_Rule_Id id) {
    if (!model || id == BM_INSTALL_RULE_ID_INVALID || (size_t)id >= arena_arr_len(model->install_rules)) return NULL;
    return &model->install_rules[id];
}

static const BM_Export_Record *bm_model_export(const Build_Model *model, BM_Export_Id id) {
    if (!model || id == BM_EXPORT_ID_INVALID || (size_t)id >= arena_arr_len(model->exports)) return NULL;
    return &model->exports[id];
}

static const BM_Package_Record *bm_model_package(const Build_Model *model, BM_Package_Id id) {
    if (!model || id == BM_PACKAGE_ID_INVALID || (size_t)id >= arena_arr_len(model->packages)) return NULL;
    return &model->packages[id];
}

static const BM_CPack_Install_Type_Record *bm_model_cpack_install_type(const Build_Model *model,
                                                                       BM_CPack_Install_Type_Id id) {
    if (!model || id == BM_CPACK_INSTALL_TYPE_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_install_types)) {
        return NULL;
    }
    return &model->cpack_install_types[id];
}

static const BM_CPack_Component_Group_Record *bm_model_cpack_component_group(const Build_Model *model,
                                                                             BM_CPack_Component_Group_Id id) {
    if (!model || id == BM_CPACK_COMPONENT_GROUP_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_component_groups)) {
        return NULL;
    }
    return &model->cpack_component_groups[id];
}

static const BM_CPack_Component_Record *bm_model_cpack_component(const Build_Model *model,
                                                                 BM_CPack_Component_Id id) {
    if (!model || id == BM_CPACK_COMPONENT_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_components)) {
        return NULL;
    }
    return &model->cpack_components[id];
}

static const BM_CPack_Package_Record *bm_model_cpack_package(const Build_Model *model,
                                                             BM_CPack_Package_Id id) {
    if (!model || id == BM_CPACK_PACKAGE_ID_INVALID || (size_t)id >= arena_arr_len(model->cpack_packages)) {
        return NULL;
    }
    return &model->cpack_packages[id];
}

static BM_String_Item_Span bm_item_span(const BM_String_Item_View *items) {
    BM_String_Item_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static BM_Link_Item_Span bm_link_item_span(const BM_Link_Item_View *items) {
    BM_Link_Item_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static BM_String_Span bm_string_span(const String_View *items) {
    BM_String_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static String_View bm_query_trim_current_dir_prefixes(String_View path) {
    while (path.count >= 2 &&
           path.data[0] == '.' &&
           (path.data[1] == '/' || path.data[1] == '\\')) {
        path.data += 2;
        path.count -= 2;
    }
    return path;
}

static bool bm_query_path_has_prefix(String_View path, String_View prefix) {
    path = bm_query_trim_current_dir_prefixes(path);
    prefix = bm_query_trim_current_dir_prefixes(prefix);
    if (prefix.count == 0 || path.count < prefix.count) return false;
    if (!nob_sv_starts_with(path, prefix)) return false;
    if (path.count == prefix.count) return true;
    return path.data[prefix.count] == '/' || path.data[prefix.count] == '\\';
}

static BM_Target_Id_Span bm_target_id_span(const BM_Target_Id *items) {
    BM_Target_Id_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static BM_Install_Rule_Id_Span bm_install_rule_id_span(const BM_Install_Rule_Id *items) {
    BM_Install_Rule_Id_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static BM_Export_Id_Span bm_export_id_span(const BM_Export_Id *items) {
    BM_Export_Id_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static BM_Build_Step_Id_Span bm_build_step_id_span(const BM_Build_Step_Id *items) {
    BM_Build_Step_Id_Span span = {0};
    span.items = items;
    span.count = arena_arr_len(items);
    return span;
}

static bool bm_sv_eq_ci_query(String_View lhs, String_View rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        unsigned char a = (unsigned char)lhs.data[i];
        unsigned char b = (unsigned char)rhs.data[i];
        if (tolower(a) != tolower(b)) return false;
    }
    return true;
}

static bool bm_component_name_matches(String_View lhs, String_View rhs) {
    String_View effective_lhs = lhs.count > 0 ? lhs : nob_sv_from_cstr("Unspecified");
    String_View effective_rhs = rhs.count > 0 ? rhs : nob_sv_from_cstr("Unspecified");
    return nob_sv_eq(effective_lhs, effective_rhs);
}

static const BM_Raw_Property_Record *bm_find_raw_property(const BM_Raw_Property_Record *records,
                                                          String_View property_name) {
    for (size_t i = 0; i < arena_arr_len(records); ++i) {
        if (bm_sv_eq_ci_query(records[i].name, property_name)) return &records[i];
    }
    return NULL;
}

static bool bm_query_target_raw_property_first(const Build_Model *model,
                                               BM_Target_Id id,
                                               String_View property_name,
                                               String_View *out) {
    BM_String_Span span = bm_query_target_raw_property_items(model, id, property_name);
    if (!out) return false;
    *out = span.count > 0 ? span.items[0] : nob_sv_from_cstr("");
    return true;
}

static BM_Target_Id bm_find_target_by_name_id(const Build_Model *model, String_View name) {
    if (!model) return BM_TARGET_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->target_name_index); ++i) {
        if (nob_sv_eq(model->target_name_index[i].name, name)) return (BM_Target_Id)model->target_name_index[i].id;
    }
    return BM_TARGET_ID_INVALID;
}

static BM_Query_Eval_Context bm_default_query_eval_context(BM_Target_Id current_target_id,
                                                           BM_Query_Usage_Mode usage_mode) {
    BM_Query_Eval_Context ctx = {0};
    ctx.current_target_id = current_target_id;
    ctx.usage_mode = usage_mode;
    ctx.build_interface_active = true;
    ctx.install_interface_active = false;
    return ctx;
}

static bool bm_query_platform_is_windows(const BM_Query_Eval_Context *ctx) {
    if (ctx && bm_sv_eq_ci_query(ctx->platform_id, nob_sv_from_cstr("Windows"))) return true;
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

static bool bm_query_platform_is_darwin(const BM_Query_Eval_Context *ctx) {
    if (ctx && bm_sv_eq_ci_query(ctx->platform_id, nob_sv_from_cstr("Darwin"))) return true;
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

static bool bm_sv_truthy_query(String_View value) {
    String_View trimmed = nob_sv_trim(value);
    if (trimmed.count == 0) return false;
    if (nob_sv_eq(trimmed, nob_sv_from_cstr("0"))) return false;
    if (bm_sv_eq_ci_query(trimmed, nob_sv_from_cstr("FALSE"))) return false;
    if (bm_sv_eq_ci_query(trimmed, nob_sv_from_cstr("OFF"))) return false;
    if (bm_sv_eq_ci_query(trimmed, nob_sv_from_cstr("NO"))) return false;
    if (bm_sv_eq_ci_query(trimmed, nob_sv_from_cstr("N"))) return false;
    return true;
}

static bool bm_sv_is_abs_path_query(String_View path) {
    if (path.count == 0 || !path.data) return false;
    if (path.data[0] == '/' || path.data[0] == '\\') return true;
    if (path.count >= 3 &&
        ((path.data[0] >= 'A' && path.data[0] <= 'Z') || (path.data[0] >= 'a' && path.data[0] <= 'z')) &&
        path.data[1] == ':' &&
        (path.data[2] == '/' || path.data[2] == '\\')) {
        return true;
    }
    return false;
}

static String_View bm_join_relative_path_query(Arena *scratch, String_View base, String_View value) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch) return nob_sv_from_cstr("");
    if (value.count == 0) return base;
    if (bm_sv_is_abs_path_query(value) || base.count == 0) return value;
    nob_sb_append_buf(&sb, base.data ? base.data : "", base.count);
    if (sb.count > 0 && sb.items[sb.count - 1] != '/') nob_sb_append(&sb, '/');
    nob_sb_append_buf(&sb, value.data ? value.data : "", value.count);
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return copy ? nob_sv_from_cstr(copy) : nob_sv_from_cstr("");
}

static bool bm_push_item_copy(Arena *scratch,
                              BM_String_Item_View **out,
                              BM_String_Item_View item) {
    if (!scratch || !out) return false;
    return arena_arr_push(scratch, *out, item);
}

static bool bm_push_link_item_copy(Arena *scratch,
                                   BM_Link_Item_View **out,
                                   BM_Link_Item_View item) {
    if (!scratch || !out) return false;
    return arena_arr_push(scratch, *out, item);
}

static bool bm_collect_items(Arena *scratch,
                             BM_String_Item_View **out,
                             const BM_String_Item_View *items,
                             bool public_only) {
    if (!scratch || !out) return false;
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (public_only && items[i].visibility == BM_VISIBILITY_PRIVATE) continue;
        if (!bm_push_item_copy(scratch, out, items[i])) return false;
    }
    return true;
}

static bool bm_collect_link_items(Arena *scratch,
                                  BM_Link_Item_View **out,
                                  const BM_Link_Item_View *items,
                                  bool public_only) {
    if (!scratch || !out) return false;
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (public_only && items[i].visibility == BM_VISIBILITY_PRIVATE) continue;
        if (!bm_push_link_item_copy(scratch, out, items[i])) return false;
    }
    return true;
}

static bool bm_collect_global_effective_items(const Build_Model *model,
                                              Arena *scratch,
                                              BM_String_Item_View **out,
                                              BM_Effective_Query_Kind kind) {
    if (!model || !scratch || !out) return false;

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            return bm_collect_items(scratch, out, model->global_properties.include_directories, false) &&
                   bm_collect_items(scratch, out, model->global_properties.system_include_directories, false);

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            return bm_collect_items(scratch, out, model->global_properties.compile_definitions, false);

        case BM_EFFECTIVE_COMPILE_OPTIONS:
            return bm_collect_items(scratch, out, model->global_properties.compile_options, false);

        case BM_EFFECTIVE_COMPILE_FEATURES:
            return true;

        case BM_EFFECTIVE_LINK_LIBRARIES:
            return true;

        case BM_EFFECTIVE_LINK_OPTIONS:
            return bm_collect_items(scratch, out, model->global_properties.link_options, false);

        case BM_EFFECTIVE_LINK_DIRECTORIES:
            return bm_collect_items(scratch, out, model->global_properties.link_directories, false);
    }

    return true;
}

static bool bm_collect_global_effective_link_items(const Build_Model *model,
                                                   Arena *scratch,
                                                   BM_Link_Item_View **out) {
    return model && scratch && out &&
           bm_collect_link_items(scratch, out, model->global_properties.link_libraries, false);
}

static bool bm_collect_directory_chain_items(const Build_Model *model,
                                             BM_Directory_Id owner_directory_id,
                                             Arena *scratch,
                                             BM_String_Item_View **out,
                                             BM_Effective_Query_Kind kind) {
    BM_Directory_Id *chain = NULL;
    BM_Directory_Id current = owner_directory_id;

    while (current != BM_DIRECTORY_ID_INVALID) {
        const BM_Directory_Record *directory = bm_model_directory(model, current);
        if (!directory) break;
        if (!arena_arr_push(scratch, chain, current)) return false;
        current = directory->parent_id;
    }

    for (size_t i = arena_arr_len(chain); i > 0; --i) {
        const BM_Directory_Record *directory = bm_model_directory(model, chain[i - 1]);
        if (!directory) continue;

        switch (kind) {
            case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
                if (!bm_collect_items(scratch, out, directory->include_directories, false) ||
                    !bm_collect_items(scratch, out, directory->system_include_directories, false)) {
                    return false;
                }
                break;

            case BM_EFFECTIVE_COMPILE_DEFINITIONS:
                if (!bm_collect_items(scratch, out, directory->compile_definitions, false)) return false;
                break;

            case BM_EFFECTIVE_COMPILE_OPTIONS:
                if (!bm_collect_items(scratch, out, directory->compile_options, false)) return false;
                break;

            case BM_EFFECTIVE_COMPILE_FEATURES:
                break;

            case BM_EFFECTIVE_LINK_LIBRARIES:
                break;

            case BM_EFFECTIVE_LINK_OPTIONS:
                if (!bm_collect_items(scratch, out, directory->link_options, false)) return false;
                break;

            case BM_EFFECTIVE_LINK_DIRECTORIES:
                if (!bm_collect_items(scratch, out, directory->link_directories, false)) return false;
                break;
        }
    }

    return true;
}

static bool bm_collect_directory_chain_link_items(const Build_Model *model,
                                                  BM_Directory_Id owner_directory_id,
                                                  Arena *scratch,
                                                  BM_Link_Item_View **out) {
    BM_Directory_Id *chain = NULL;
    BM_Directory_Id current = owner_directory_id;

    while (current != BM_DIRECTORY_ID_INVALID) {
        const BM_Directory_Record *directory = bm_model_directory(model, current);
        if (!directory) break;
        if (!arena_arr_push(scratch, chain, current)) return false;
        current = directory->parent_id;
    }

    for (size_t i = arena_arr_len(chain); i > 0; --i) {
        const BM_Directory_Record *directory = bm_model_directory(model, chain[i - 1]);
        if (!directory) continue;
        if (!bm_collect_link_items(scratch, out, directory->link_libraries, false)) return false;
    }

    return true;
}

static bool bm_collect_target_items(Arena *scratch,
                                    BM_String_Item_View **out,
                                    const BM_Target_Record *target,
                                    BM_Effective_Query_Kind kind,
                                    bool public_only) {
    if (!scratch || !out || !target) return false;

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            return bm_collect_items(scratch, out, target->include_directories, public_only);

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            return bm_collect_items(scratch, out, target->compile_definitions, public_only);

        case BM_EFFECTIVE_COMPILE_OPTIONS:
            return bm_collect_items(scratch, out, target->compile_options, public_only);

        case BM_EFFECTIVE_COMPILE_FEATURES:
            return bm_collect_items(scratch, out, target->compile_features, public_only);

        case BM_EFFECTIVE_LINK_LIBRARIES:
            return true;

        case BM_EFFECTIVE_LINK_OPTIONS:
            return bm_collect_items(scratch, out, target->link_options, public_only);

        case BM_EFFECTIVE_LINK_DIRECTORIES:
            return bm_collect_items(scratch, out, target->link_directories, public_only);
    }

    return true;
}

static bool bm_collect_target_link_items(Arena *scratch,
                                         BM_Link_Item_View **out,
                                         const BM_Target_Record *target,
                                         bool public_only) {
    return scratch && out && target && bm_collect_link_items(scratch, out, target->link_libraries, public_only);
}

static BM_Target_Id bm_resolve_alias_target_id(const Build_Model *model, BM_Target_Id id) {
    size_t remaining = model ? arena_arr_len(model->targets) : 0;
    BM_Target_Id current = id;

    while (remaining-- > 0) {
        const BM_Target_Record *target = bm_model_target(model, current);
        if (!target) return BM_TARGET_ID_INVALID;
        if (!target->alias) return current;
        current = target->alias_of_id;
        if (current == BM_TARGET_ID_INVALID) return BM_TARGET_ID_INVALID;
    }

    return BM_TARGET_ID_INVALID;
}

static bool bm_collect_dependency_interface(const Build_Model *model,
                                            BM_Target_Id id,
                                            const BM_Query_Eval_Context *ctx,
                                            Arena *scratch,
                                            uint8_t *visited,
                                            BM_String_Item_View **out,
                                            BM_Effective_Query_Kind kind);
static bool bm_collect_link_dependency_interface(const Build_Model *model,
                                                 BM_Target_Id id,
                                                 const BM_Query_Eval_Context *ctx,
                                                 Arena *scratch,
                                                 uint8_t *visited,
                                                 BM_Link_Item_View **out);

static bool bm_collect_evaluated_root_link_library_seeds(const Build_Model *model,
                                                         const BM_Target_Record *target,
                                                         const BM_Query_Eval_Context *ctx,
                                                         Arena *scratch,
                                                         BM_Link_Item_Span *out) {
    BM_Link_Item_View *raw_items = NULL;
    if (!out) return false;
    *out = (BM_Link_Item_Span){0};
    if (!model || !target || !scratch) return false;

    if (!bm_collect_global_effective_link_items(model, scratch, &raw_items) ||
        !bm_collect_directory_chain_link_items(model, target->owner_directory_id, scratch, &raw_items) ||
        !bm_collect_target_link_items(scratch, &raw_items, target, false)) {
        return false;
    }

    return bm_eval_link_item_span(model, target->id, ctx, scratch, bm_link_item_span(raw_items), out);
}

static bool bm_collect_dependency_usage_from_evaluated_link_items(const Build_Model *model,
                                                                  BM_Link_Item_Span evaluated,
                                                                  const BM_Query_Eval_Context *ctx,
                                                                  Arena *scratch,
                                                                  uint8_t *visited,
                                                                  BM_String_Item_View **out,
                                                                  BM_Effective_Query_Kind kind) {
    if (!model || !scratch || !visited || !out) return false;

    for (size_t i = 0; i < evaluated.count; ++i) {
        BM_Target_Id dep_id = evaluated.items[i].target_id;
        if (!bm_target_id_is_valid(dep_id)) continue;
        if (!bm_collect_dependency_interface(model, dep_id, ctx, scratch, visited, out, kind)) return false;
    }

    return true;
}

static bool bm_collect_dependency_usage_from_link_items(const Build_Model *model,
                                                        const BM_Target_Record *target,
                                                        const BM_Query_Eval_Context *ctx,
                                                        Arena *scratch,
                                                        uint8_t *visited,
                                                        BM_String_Item_View **out,
                                                        BM_Effective_Query_Kind kind) {
    BM_Link_Item_Span evaluated = {0};
    if (!model || !target || !scratch || !visited || !out) return false;

    if (!bm_eval_link_item_span(model,
                                target->id,
                                ctx,
                                scratch,
                                bm_link_item_span(target->link_libraries),
                                &evaluated)) {
        return false;
    }

    return bm_collect_dependency_usage_from_evaluated_link_items(model,
                                                                 evaluated,
                                                                 ctx,
                                                                 scratch,
                                                                 visited,
                                                                 out,
                                                                 kind);
}

static bool bm_collect_dependency_interface(const Build_Model *model,
                                            BM_Target_Id id,
                                            const BM_Query_Eval_Context *ctx,
                                            Arena *scratch,
                                            uint8_t *visited,
                                            BM_String_Item_View **out,
                                            BM_Effective_Query_Kind kind) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || !visited) return false;
    if (visited[id]) return true;
    visited[id] = 1;

    if (!bm_collect_target_items(scratch, out, target, kind, true)) return false;
    return bm_collect_dependency_usage_from_link_items(model, target, ctx, scratch, visited, out, kind);
}

static bool bm_collect_link_dependency_usage_from_evaluated_link_items(const Build_Model *model,
                                                                       BM_Link_Item_Span evaluated,
                                                                       const BM_Query_Eval_Context *ctx,
                                                                       Arena *scratch,
                                                                       uint8_t *visited,
                                                                       BM_Link_Item_View **out) {
    if (!model || !scratch || !visited || !out) return false;

    for (size_t i = 0; i < evaluated.count; ++i) {
        BM_Target_Id dep_id = evaluated.items[i].target_id;
        if (!bm_target_id_is_valid(dep_id)) continue;
        if (!bm_collect_link_dependency_interface(model, dep_id, ctx, scratch, visited, out)) return false;
    }

    return true;
}

static bool bm_collect_link_dependency_usage_from_link_items(const Build_Model *model,
                                                             const BM_Target_Record *target,
                                                             const BM_Query_Eval_Context *ctx,
                                                             Arena *scratch,
                                                             uint8_t *visited,
                                                             BM_Link_Item_View **out) {
    BM_Link_Item_Span evaluated = {0};
    if (!model || !target || !scratch || !visited || !out) return false;

    if (!bm_eval_link_item_span(model,
                                target->id,
                                ctx,
                                scratch,
                                bm_link_item_span(target->link_libraries),
                                &evaluated)) {
        return false;
    }

    return bm_collect_link_dependency_usage_from_evaluated_link_items(model,
                                                                      evaluated,
                                                                      ctx,
                                                                      scratch,
                                                                      visited,
                                                                      out);
}

static bool bm_collect_link_dependency_interface(const Build_Model *model,
                                                 BM_Target_Id id,
                                                 const BM_Query_Eval_Context *ctx,
                                                 Arena *scratch,
                                                 uint8_t *visited,
                                                 BM_Link_Item_View **out) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || !visited) return false;
    if (visited[id]) return true;
    visited[id] = 1;

    if (!bm_collect_target_link_items(scratch, out, target, true)) return false;
    return bm_collect_link_dependency_usage_from_link_items(model, target, ctx, scratch, visited, out);
}

static bool bm_append_split_values(Arena *scratch,
                                   BM_String_Item_View **out,
                                   BM_String_Item_View item,
                                   String_View value) {
    size_t start = 0;
    if (!scratch || !out) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i <= value.count; ++i) {
        bool sep = (i == value.count) || (value.data[i] == ';');
        BM_String_Item_View copy = item;
        String_View piece = {0};
        if (!sep) continue;
        piece = nob_sv_trim(nob_sv_from_parts(value.data + start, i - start));
        start = i + 1;
        if (piece.count == 0) continue;
        {
            char *dup = arena_strndup(scratch, piece.data, piece.count);
            if (!dup) return false;
            copy.value = nob_sv_from_parts(dup, piece.count);
        }
        if (!arena_arr_push(scratch, *out, copy)) return false;
    }
    return true;
}

static bool bm_append_split_link_values(Arena *scratch,
                                        BM_Link_Item_View **out,
                                        BM_Link_Item_View item,
                                        String_View value) {
    size_t start = 0;
    if (!scratch || !out) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i <= value.count; ++i) {
        bool sep = (i == value.count) || (value.data[i] == ';');
        BM_Link_Item_View copy = item;
        String_View piece = {0};
        if (!sep) continue;
        piece = nob_sv_trim(nob_sv_from_parts(value.data + start, i - start));
        start = i + 1;
        if (piece.count == 0) continue;
        {
            char *dup = arena_strndup(scratch, piece.data, piece.count);
            if (!dup) return false;
            copy.value = nob_sv_from_parts(dup, piece.count);
        }
        if (!arena_arr_push(scratch, *out, copy)) return false;
    }
    return true;
}

static bool bm_query_append_joined_items(Arena *scratch,
                                         String_View *out,
                                         const BM_String_Item_View *items,
                                         BM_Visibility min_visibility,
                                         BM_Visibility max_visibility) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    bool first = true;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (items[i].visibility < min_visibility || items[i].visibility > max_visibility) continue;
        if (!first) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, items[i].value.data ? items[i].value.data : "", items[i].value.count);
        first = false;
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return true;
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static bool bm_query_append_joined_link_items(Arena *scratch,
                                              String_View *out,
                                              const BM_Link_Item_View *items,
                                              BM_Visibility min_visibility,
                                              BM_Visibility max_visibility) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    bool first = true;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (items[i].visibility < min_visibility || items[i].visibility > max_visibility) continue;
        if (!first) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, items[i].value.data ? items[i].value.data : "", items[i].value.count);
        first = false;
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return true;
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static bool bm_query_append_joined_items_with_flags(Arena *scratch,
                                                    String_View *out,
                                                    const BM_String_Item_View *items,
                                                    BM_Visibility min_visibility,
                                                    BM_Visibility max_visibility,
                                                    uint32_t required_flags,
                                                    uint32_t forbidden_flags) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    bool first = true;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (items[i].visibility < min_visibility || items[i].visibility > max_visibility) continue;
        if ((items[i].flags & required_flags) != required_flags) continue;
        if ((items[i].flags & forbidden_flags) != 0) continue;
        if (!first) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, items[i].value.data ? items[i].value.data : "", items[i].value.count);
        first = false;
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return true;
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static bool bm_query_append_joined_values(Arena *scratch,
                                          String_View *out,
                                          String_View lhs,
                                          String_View rhs) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (lhs.count == 0 && rhs.count == 0) return true;
    if (lhs.count > 0) nob_sb_append_buf(&sb, lhs.data ? lhs.data : "", lhs.count);
    if (lhs.count > 0 && rhs.count > 0) nob_sb_append(&sb, ';');
    if (rhs.count > 0) nob_sb_append_buf(&sb, rhs.data ? rhs.data : "", rhs.count);
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static bool bm_query_append_joined_raw_record(Arena *scratch,
                                              String_View *out,
                                              const BM_Raw_Property_Record *record) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!record) return true;
    for (size_t i = 0; i < arena_arr_len(record->items); ++i) {
        if (i > 0) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, record->items[i].data ? record->items[i].data : "", record->items[i].count);
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return true;
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb.count);
    return true;
}

static void bm_query_join_push_sv(Nob_String_Builder *sb, bool *first, String_View value) {
    if (!sb || !first || value.count == 0) return;
    if (!*first) nob_sb_append(sb, ';');
    nob_sb_append_buf(sb, value.data ? value.data : "", value.count);
    *first = false;
}

static bool bm_query_finalize_joined_sv(Arena *scratch, Nob_String_Builder *sb, String_View *out) {
    char *copy = NULL;
    if (!scratch || !sb || !out) return false;
    *out = nob_sv_from_cstr("");
    if (sb->count == 0) {
        nob_sb_free(*sb);
        return true;
    }
    copy = arena_strndup(scratch, sb->items ? sb->items : "", sb->count);
    nob_sb_free(*sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, sb->count);
    return true;
}

static const BM_Target_Source_Record *bm_query_target_source_record(const Build_Model *model,
                                                                    BM_Target_Id id,
                                                                    size_t source_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || source_index >= arena_arr_len(target->source_records)) return NULL;
    return &target->source_records[source_index];
}

static const BM_Target_File_Set_Record *bm_query_target_file_set_record(const Build_Model *model,
                                                                        BM_Target_Id id,
                                                                        size_t file_set_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || file_set_index >= arena_arr_len(target->file_sets)) return NULL;
    return &target->file_sets[file_set_index];
}

bool bm_query_target_property_value(const Build_Model *model,
                                    BM_Target_Id id,
                                    String_View property_name,
                                    Arena *scratch,
                                    String_View *out) {
    const BM_Target_Record *target = bm_model_target(model, id);
    const BM_Raw_Property_Record *record = NULL;
    String_View structured = nob_sv_from_cstr("");
    String_View raw = nob_sv_from_cstr("");
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!target) return true;

    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("SOURCES")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_SOURCES"))) {
        Nob_String_Builder sb = {0};
        bool first = true;
        bool interface_only = bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_SOURCES"));
        for (size_t i = 0; i < arena_arr_len(target->source_records); ++i) {
            const BM_Target_Source_Record *source = &target->source_records[i];
            if (source->kind != BM_TARGET_SOURCE_REGULAR) continue;
            if (interface_only) {
                if (source->visibility == BM_VISIBILITY_PRIVATE) continue;
            } else if (source->visibility == BM_VISIBILITY_INTERFACE) {
                continue;
            }
            bm_query_join_push_sv(&sb, &first, source->raw_path);
        }
        return bm_query_finalize_joined_sv(scratch, &sb, out);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_SETS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_HEADER_SETS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("CXX_MODULE_SETS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS"))) {
        Nob_String_Builder sb = {0};
        bool first = true;
        bool want_headers =
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_SETS")) ||
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_HEADER_SETS"));
        bool interface_only =
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_HEADER_SETS")) ||
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS"));
        for (size_t i = 0; i < arena_arr_len(target->file_sets); ++i) {
            const BM_Target_File_Set_Record *file_set = &target->file_sets[i];
            if ((want_headers && file_set->kind != BM_TARGET_FILE_SET_HEADERS) ||
                (!want_headers && file_set->kind != BM_TARGET_FILE_SET_CXX_MODULES)) {
                continue;
            }
            if (interface_only) {
                if (file_set->visibility == BM_VISIBILITY_PRIVATE) continue;
            } else if (file_set->visibility == BM_VISIBILITY_INTERFACE) {
                continue;
            }
            bm_query_join_push_sv(&sb, &first, file_set->name);
        }
        return bm_query_finalize_joined_sv(scratch, &sb, out);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_SET")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_DIRS")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("CXX_MODULE_SET")) ||
        bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("CXX_MODULE_DIRS")) ||
        nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_SET_")) ||
        nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_DIRS_")) ||
        nob_sv_starts_with(property_name, nob_sv_from_cstr("CXX_MODULE_SET_")) ||
        nob_sv_starts_with(property_name, nob_sv_from_cstr("CXX_MODULE_DIRS_"))) {
        Nob_String_Builder sb = {0};
        bool first = true;
        bool want_headers =
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_SET")) ||
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_DIRS")) ||
            nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_SET_")) ||
            nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_DIRS_"));
        bool want_dirs =
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("HEADER_DIRS")) ||
            bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("CXX_MODULE_DIRS")) ||
            nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_DIRS_")) ||
            nob_sv_starts_with(property_name, nob_sv_from_cstr("CXX_MODULE_DIRS_"));
        String_View target_set_name = want_headers ? nob_sv_from_cstr("HEADERS")
                                                   : nob_sv_from_cstr("CXX_MODULES");
        if (nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_DIRS_"))) {
            target_set_name = nob_sv_from_parts(property_name.data + strlen("HEADER_DIRS_"),
                                                property_name.count - strlen("HEADER_DIRS_"));
        } else if (nob_sv_starts_with(property_name, nob_sv_from_cstr("HEADER_SET_"))) {
            target_set_name = nob_sv_from_parts(property_name.data + strlen("HEADER_SET_"),
                                                property_name.count - strlen("HEADER_SET_"));
        } else if (nob_sv_starts_with(property_name, nob_sv_from_cstr("CXX_MODULE_DIRS_"))) {
            target_set_name = nob_sv_from_parts(property_name.data + strlen("CXX_MODULE_DIRS_"),
                                                property_name.count - strlen("CXX_MODULE_DIRS_"));
        } else if (nob_sv_starts_with(property_name, nob_sv_from_cstr("CXX_MODULE_SET_"))) {
            target_set_name = nob_sv_from_parts(property_name.data + strlen("CXX_MODULE_SET_"),
                                                property_name.count - strlen("CXX_MODULE_SET_"));
        }

        for (size_t i = 0; i < arena_arr_len(target->file_sets); ++i) {
            const BM_Target_File_Set_Record *file_set = &target->file_sets[i];
            const String_View *items = want_dirs ? file_set->base_dirs : file_set->raw_files;
            if ((want_headers && file_set->kind != BM_TARGET_FILE_SET_HEADERS) ||
                (!want_headers && file_set->kind != BM_TARGET_FILE_SET_CXX_MODULES)) {
                continue;
            }
            if (!bm_sv_eq_ci_query(file_set->name, target_set_name)) continue;
            for (size_t item_index = 0; item_index < arena_arr_len(items); ++item_index) {
                bm_query_join_push_sv(&sb, &first, items[item_index]);
            }
        }
        return bm_query_finalize_joined_sv(scratch, &sb, out);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_LIBRARIES"))) {
        if (!bm_query_append_joined_link_items(scratch,
                                               &structured,
                                               target->link_libraries,
                                               BM_VISIBILITY_PRIVATE,
                                               BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"))) {
        if (!bm_query_append_joined_link_items(scratch,
                                               &structured,
                                               target->link_libraries,
                                               BM_VISIBILITY_PUBLIC,
                                               BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->include_directories, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->include_directories, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES"))) {
        if (!bm_query_append_joined_items_with_flags(scratch,
                                                     &structured,
                                                     target->include_directories,
                                                     BM_VISIBILITY_PUBLIC,
                                                     BM_VISIBILITY_INTERFACE,
                                                     BM_ITEM_FLAG_SYSTEM,
                                                     0)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_definitions, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_DEFINITIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_definitions, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_OPTIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_options, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_options, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("COMPILE_FEATURES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_features, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->compile_features, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_OPTIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_options, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_OPTIONS"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_options, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_DIRECTORIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_directories, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_DIRECTORIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_directories, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("OUTPUT_NAME"))) {
        *out = target->output_name;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("PREFIX"))) {
        *out = target->prefix;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("SUFFIX"))) {
        *out = target->suffix;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"))) {
        *out = target->archive_output_directory;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LIBRARY_OUTPUT_DIRECTORY"))) {
        *out = target->library_output_directory;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"))) {
        *out = target->runtime_output_directory;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("FOLDER"))) {
        *out = target->folder;
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("EXCLUDE_FROM_ALL"))) {
        *out = target->exclude_from_all ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("WIN32_EXECUTABLE"))) {
        *out = target->win32_executable ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
        return true;
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("MACOSX_BUNDLE"))) {
        *out = target->macosx_bundle ? nob_sv_from_cstr("1") : nob_sv_from_cstr("0");
        return true;
    }

    record = bm_find_raw_property(target->raw_properties, property_name);
    return bm_query_append_joined_raw_record(scratch, out, record);
}

#include "build_model_query_imported.c"
#include "build_model_query_effective.c"
#include "build_model_query_session.c"

bool bm_model_has_project(const Build_Model *model) { return model ? model->project.present : false; }
bool bm_target_id_is_valid(BM_Target_Id id) { return id != BM_TARGET_ID_INVALID; }
bool bm_build_step_id_is_valid(BM_Build_Step_Id id) { return id != BM_BUILD_STEP_ID_INVALID; }
bool bm_replay_action_id_is_valid(BM_Replay_Action_Id id) { return id != BM_REPLAY_ACTION_ID_INVALID; }
bool bm_directory_id_is_valid(BM_Directory_Id id) { return id != BM_DIRECTORY_ID_INVALID; }
bool bm_test_id_is_valid(BM_Test_Id id) { return id != BM_TEST_ID_INVALID; }
bool bm_export_id_is_valid(BM_Export_Id id) { return id != BM_EXPORT_ID_INVALID; }
bool bm_package_id_is_valid(BM_Package_Id id) { return id != BM_PACKAGE_ID_INVALID; }

size_t bm_query_directory_count(const Build_Model *model) { return model ? arena_arr_len(model->directories) : 0; }
size_t bm_query_target_count(const Build_Model *model) { return model ? arena_arr_len(model->targets) : 0; }
size_t bm_query_build_step_count(const Build_Model *model) { return model ? arena_arr_len(model->build_steps) : 0; }
size_t bm_query_replay_action_count(const Build_Model *model) { return model ? arena_arr_len(model->replay_actions) : 0; }
size_t bm_query_test_count(const Build_Model *model) { return model ? arena_arr_len(model->tests) : 0; }
size_t bm_query_install_rule_count(const Build_Model *model) { return model ? arena_arr_len(model->install_rules) : 0; }
size_t bm_query_export_count(const Build_Model *model) { return model ? arena_arr_len(model->exports) : 0; }
size_t bm_query_package_count(const Build_Model *model) { return model ? arena_arr_len(model->packages) : 0; }
size_t bm_query_cpack_install_type_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_install_types) : 0; }
size_t bm_query_cpack_component_group_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_component_groups) : 0; }
size_t bm_query_cpack_component_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_components) : 0; }
size_t bm_query_cpack_package_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_packages) : 0; }

String_View bm_query_project_name(const Build_Model *model) { return model ? model->project.name : (String_View){0}; }
String_View bm_query_project_version(const Build_Model *model) { return model ? model->project.version : (String_View){0}; }

BM_String_Span bm_query_project_languages(const Build_Model *model) {
    BM_String_Span span = {0};
    if (!model) return span;
    span.items = model->project.languages;
    span.count = arena_arr_len(model->project.languages);
    return span;
}

BM_Directory_Id bm_query_root_directory(const Build_Model *model) {
    return model ? model->root_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_Directory_Id bm_query_directory_parent(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->parent_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_directory_source_dir(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->source_dir : (String_View){0};
}

String_View bm_query_directory_binary_dir(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? directory->binary_dir : (String_View){0};
}

BM_String_Item_Span bm_query_directory_include_directories_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->include_directories) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_directory_system_include_directories_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->system_include_directories) : (BM_String_Item_Span){0};
}

BM_Link_Item_Span bm_query_directory_link_libraries_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_link_item_span(directory->link_libraries) : (BM_Link_Item_Span){0};
}

BM_String_Item_Span bm_query_directory_link_directories_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->link_directories) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_directory_compile_definitions_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->compile_definitions) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_directory_compile_options_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->compile_options) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_directory_link_options_raw(const Build_Model *model, BM_Directory_Id id) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    return directory ? bm_item_span(directory->link_options) : (BM_String_Item_Span){0};
}

BM_String_Span bm_query_directory_raw_property_items(const Build_Model *model, BM_Directory_Id id, String_View property_name) {
    const BM_Directory_Record *directory = bm_model_directory(model, id);
    const BM_Raw_Property_Record *record = NULL;
    if (!directory) return (BM_String_Span){0};
    record = bm_find_raw_property(directory->raw_properties, property_name);
    return record ? bm_string_span(record->items) : (BM_String_Span){0};
}

BM_String_Item_Span bm_query_global_include_directories_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.include_directories) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_global_system_include_directories_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.system_include_directories) : (BM_String_Item_Span){0};
}

BM_Link_Item_Span bm_query_global_link_libraries_raw(const Build_Model *model) {
    return model ? bm_link_item_span(model->global_properties.link_libraries) : (BM_Link_Item_Span){0};
}

BM_String_Item_Span bm_query_global_link_directories_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.link_directories) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_global_compile_definitions_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.compile_definitions) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_global_compile_options_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.compile_options) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_global_link_options_raw(const Build_Model *model) {
    return model ? bm_item_span(model->global_properties.link_options) : (BM_String_Item_Span){0};
}

BM_String_Span bm_query_global_raw_property_items(const Build_Model *model, String_View property_name) {
    const BM_Raw_Property_Record *record = NULL;
    if (!model) return (BM_String_Span){0};
    record = bm_find_raw_property(model->global_properties.raw_properties, property_name);
    return record ? bm_string_span(record->items) : (BM_String_Span){0};
}

BM_Target_Id bm_query_target_by_name(const Build_Model *model, String_View name) {
    return bm_find_target_by_name_id(model, name);
}

BM_Test_Id bm_query_test_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_TEST_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->test_name_index); ++i) {
        if (nob_sv_eq(model->test_name_index[i].name, name)) return (BM_Test_Id)model->test_name_index[i].id;
    }
    return BM_TEST_ID_INVALID;
}

BM_Package_Id bm_query_package_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_PACKAGE_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->package_name_index); ++i) {
        if (nob_sv_eq(model->package_name_index[i].name, name)) return (BM_Package_Id)model->package_name_index[i].id;
    }
    return BM_PACKAGE_ID_INVALID;
}

String_View bm_query_target_name(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->name : (String_View){0};
}

BM_Target_Kind bm_query_target_kind(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = NULL;
    if (bm_target_id_is_valid(id)) {
        BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
        if (bm_target_id_is_valid(resolved_id)) id = resolved_id;
    }
    target = bm_model_target(model, id);
    return target ? target->kind : BM_TARGET_UTILITY;
}

BM_Directory_Id bm_query_target_owner_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

bool bm_query_target_is_imported(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->imported : false;
}

bool bm_query_target_is_imported_global(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->imported_global : false;
}

bool bm_query_target_is_alias(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->alias : false;
}

bool bm_query_target_is_alias_global(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->alias_global : false;
}

BM_Target_Id bm_query_target_alias_of(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->alias_of_id : BM_TARGET_ID_INVALID;
}

bool bm_query_target_exclude_from_all(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->exclude_from_all : false;
}

BM_String_Span bm_query_target_sources_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_string_span(target->sources) : (BM_String_Span){0};
}

size_t bm_query_target_source_count(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? arena_arr_len(target->source_records) : 0;
}

BM_Target_Source_Kind bm_query_target_source_kind(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->kind : BM_TARGET_SOURCE_REGULAR;
}

BM_Visibility bm_query_target_source_visibility(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->visibility : BM_VISIBILITY_PRIVATE;
}

String_View bm_query_target_source_raw(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->raw_path : nob_sv_from_cstr("");
}

String_View bm_query_target_source_effective(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->effective_path : nob_sv_from_cstr("");
}

bool bm_query_target_source_generated(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->generated : false;
}

bool bm_query_target_source_is_compile_input(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    if (!source) return false;
    return source->visibility != BM_VISIBILITY_INTERFACE &&
           source->kind == BM_TARGET_SOURCE_REGULAR &&
           !source->header_file_only;
}

bool bm_query_target_source_header_file_only(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->header_file_only : false;
}

String_View bm_query_target_source_language(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->language : nob_sv_from_cstr("");
}

BM_String_Item_Span bm_query_target_source_compile_definitions(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? bm_item_span(source->compile_definitions) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_source_compile_options(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? bm_item_span(source->compile_options) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_source_include_directories(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? bm_item_span(source->include_directories) : (BM_String_Item_Span){0};
}

String_View bm_query_target_source_file_set_name(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->file_set_name : nob_sv_from_cstr("");
}

BM_String_Span bm_query_target_source_raw_property_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         size_t source_index,
                                                         String_View property_name) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    const BM_Raw_Property_Record *record = NULL;
    if (!source) return (BM_String_Span){0};
    record = bm_find_raw_property(source->raw_properties, property_name);
    return record ? bm_string_span(record->items) : (BM_String_Span){0};
}

BM_Build_Step_Id bm_query_target_source_producer_step(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Source_Record *source = bm_query_target_source_record(model, id, source_index);
    return source ? source->producer_step_id : BM_BUILD_STEP_ID_INVALID;
}

size_t bm_query_target_file_set_count(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? arena_arr_len(target->file_sets) : 0;
}

String_View bm_query_target_file_set_name(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? file_set->name : nob_sv_from_cstr("");
}

BM_Target_File_Set_Kind bm_query_target_file_set_kind(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? file_set->kind : BM_TARGET_FILE_SET_HEADERS;
}

BM_Visibility bm_query_target_file_set_visibility(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? file_set->visibility : BM_VISIBILITY_PRIVATE;
}

BM_String_Span bm_query_target_file_set_base_dirs(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? bm_string_span(file_set->base_dirs) : (BM_String_Span){0};
}

BM_String_Span bm_query_target_file_set_files_raw(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? bm_string_span(file_set->raw_files) : (BM_String_Span){0};
}

BM_String_Span bm_query_target_file_set_files_effective(const Build_Model *model, BM_Target_Id id, size_t file_set_index) {
    const BM_Target_File_Set_Record *file_set = bm_query_target_file_set_record(model, id, file_set_index);
    return file_set ? bm_string_span(file_set->effective_files) : (BM_String_Span){0};
}

BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_target_id_span(target->explicit_dependency_ids) : (BM_Target_Id_Span){0};
}

BM_Link_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_link_item_span(target->link_libraries) : (BM_Link_Item_Span){0};
}

BM_String_Item_Span bm_query_target_include_directories_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->include_directories) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_compile_definitions_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->compile_definitions) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_compile_options_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->compile_options) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_compile_features_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->compile_features) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_link_options_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->link_options) : (BM_String_Item_Span){0};
}

BM_String_Item_Span bm_query_target_link_directories_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->link_directories) : (BM_String_Item_Span){0};
}

size_t bm_query_target_raw_property_count(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? arena_arr_len(target->raw_properties) : 0;
}

String_View bm_query_target_raw_property_name(const Build_Model *model, BM_Target_Id id, size_t property_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || property_index >= arena_arr_len(target->raw_properties)) return nob_sv_from_cstr("");
    return target->raw_properties[property_index].name;
}

BM_String_Span bm_query_target_raw_property_items(const Build_Model *model, BM_Target_Id id, String_View property_name) {
    const BM_Target_Record *target = bm_model_target(model, id);
    const BM_Raw_Property_Record *record = NULL;
    if (!target) return (BM_String_Span){0};
    record = bm_find_raw_property(target->raw_properties, property_name);
    return record ? bm_string_span(record->items) : (BM_String_Span){0};
}

static String_View bm_query_target_raw_property_first_string(const Build_Model *model,
                                                             BM_Target_Id id,
                                                             String_View property_name) {
    BM_String_Span span = bm_query_target_raw_property_items(model, id, property_name);
    return span.count > 0 ? span.items[0] : nob_sv_from_cstr("");
}

String_View bm_query_target_output_name(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->output_name : (String_View){0};
}

String_View bm_query_target_prefix(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->prefix : (String_View){0};
}

String_View bm_query_target_suffix(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->suffix : (String_View){0};
}

String_View bm_query_target_archive_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->archive_output_directory : (String_View){0};
}

String_View bm_query_target_library_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->library_output_directory : (String_View){0};
}

String_View bm_query_target_runtime_output_directory(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->runtime_output_directory : (String_View){0};
}

String_View bm_query_target_folder(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->folder : (String_View){0};
}

String_View bm_query_target_c_standard(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("C_STANDARD"));
}

bool bm_query_target_c_standard_required(const Build_Model *model, BM_Target_Id id) {
    return bm_sv_truthy_query(bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("C_STANDARD_REQUIRED")));
}

bool bm_query_target_c_extensions(const Build_Model *model, BM_Target_Id id) {
    return bm_sv_truthy_query(bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("C_EXTENSIONS")));
}

String_View bm_query_target_cxx_standard(const Build_Model *model, BM_Target_Id id) {
    return bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("CXX_STANDARD"));
}

bool bm_query_target_cxx_standard_required(const Build_Model *model, BM_Target_Id id) {
    return bm_sv_truthy_query(bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("CXX_STANDARD_REQUIRED")));
}

bool bm_query_target_cxx_extensions(const Build_Model *model, BM_Target_Id id) {
    return bm_sv_truthy_query(bm_query_target_raw_property_first_string(model, id, nob_sv_from_cstr("CXX_EXTENSIONS")));
}

bool bm_query_target_win32_executable(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->win32_executable : false;
}

bool bm_query_target_macosx_bundle(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->macosx_bundle : false;
}

BM_Build_Step_Kind bm_query_build_step_kind(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->kind : BM_BUILD_STEP_OUTPUT_RULE;
}

BM_Directory_Id bm_query_build_step_owner_directory(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_Target_Id bm_query_build_step_owner_target(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->owner_target_id : BM_TARGET_ID_INVALID;
}

bool bm_query_build_step_append(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->append : false;
}

bool bm_query_build_step_verbatim(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->verbatim : false;
}

bool bm_query_build_step_uses_terminal(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->uses_terminal : false;
}

bool bm_query_build_step_command_expand_lists(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->command_expand_lists : false;
}

bool bm_query_build_step_depends_explicit_only(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->depends_explicit_only : false;
}

bool bm_query_build_step_codegen(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->codegen : false;
}

String_View bm_query_build_step_working_directory(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->working_directory : nob_sv_from_cstr("");
}

String_View bm_query_build_step_comment(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->comment : nob_sv_from_cstr("");
}

String_View bm_query_build_step_main_dependency(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->main_dependency : nob_sv_from_cstr("");
}

String_View bm_query_build_step_depfile(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->depfile : nob_sv_from_cstr("");
}

String_View bm_query_build_step_job_pool(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->job_pool : nob_sv_from_cstr("");
}

String_View bm_query_build_step_job_server_aware(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? step->job_server_aware : nob_sv_from_cstr("");
}

BM_String_Span bm_query_build_step_outputs_raw(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->raw_outputs) : (BM_String_Span){0};
}

BM_String_Span bm_query_build_step_outputs(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->effective_outputs) : (BM_String_Span){0};
}

BM_String_Span bm_query_build_step_byproducts_raw(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->raw_byproducts) : (BM_String_Span){0};
}

BM_String_Span bm_query_build_step_byproducts(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->effective_byproducts) : (BM_String_Span){0};
}

BM_String_Span bm_query_build_step_dependency_tokens_raw(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->raw_dependency_tokens) : (BM_String_Span){0};
}

BM_Target_Id_Span bm_query_build_step_target_dependencies(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_target_id_span(step->resolved_target_dependencies) : (BM_Target_Id_Span){0};
}

BM_Build_Step_Id_Span bm_query_build_step_producer_dependencies(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_build_step_id_span(step->resolved_producer_dependencies) : (BM_Build_Step_Id_Span){0};
}

BM_String_Span bm_query_build_step_file_dependencies(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? bm_string_span(step->resolved_file_dependencies) : (BM_String_Span){0};
}

size_t bm_query_build_step_command_count(const Build_Model *model, BM_Build_Step_Id id) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    return step ? arena_arr_len(step->commands) : 0;
}

BM_String_Span bm_query_build_step_command_argv(const Build_Model *model, BM_Build_Step_Id id, size_t command_index) {
    const BM_Build_Step_Record *step = bm_model_build_step(model, id);
    if (!step || command_index >= arena_arr_len(step->commands)) return (BM_String_Span){0};
    return bm_string_span(step->commands[command_index].argv);
}

BM_Replay_Action_Kind bm_query_replay_action_kind(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? action->kind : BM_REPLAY_ACTION_FILESYSTEM;
}

BM_Replay_Opcode bm_query_replay_action_opcode(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? action->opcode : BM_REPLAY_OPCODE_NONE;
}

BM_Replay_Phase bm_query_replay_action_phase(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? action->phase : BM_REPLAY_PHASE_CONFIGURE;
}

BM_Directory_Id bm_query_replay_action_owner_directory(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? action->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_replay_action_working_directory(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? action->working_directory : nob_sv_from_cstr("");
}

BM_String_Span bm_query_replay_action_inputs(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? bm_string_span(action->inputs) : (BM_String_Span){0};
}

BM_String_Span bm_query_replay_action_outputs(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? bm_string_span(action->outputs) : (BM_String_Span){0};
}

BM_String_Span bm_query_replay_action_argv(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? bm_string_span(action->argv) : (BM_String_Span){0};
}

BM_String_Span bm_query_replay_action_environment(const Build_Model *model, BM_Replay_Action_Id id) {
    const BM_Replay_Action_Record *action = bm_model_replay_action(model, id);
    return action ? bm_string_span(action->environment) : (BM_String_Span){0};
}

bool bm_query_testing_enabled(const Build_Model *model) {
    return model ? model->testing_enabled : false;
}

String_View bm_query_test_name(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->name : (String_View){0};
}

String_View bm_query_test_command(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->command : (String_View){0};
}

BM_Directory_Id bm_query_test_owner_directory(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_test_working_directory(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->working_dir : (String_View){0};
}

bool bm_query_test_command_expand_lists(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? test->command_expand_lists : false;
}

BM_String_Span bm_query_test_configurations(const Build_Model *model, BM_Test_Id id) {
    const BM_Test_Record *test = bm_model_test(model, id);
    return test ? bm_string_span(test->configurations) : (BM_String_Span){0};
}

BM_Install_Rule_Kind bm_query_install_rule_kind(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->kind : BM_INSTALL_RULE_FILE;
}

BM_Directory_Id bm_query_install_rule_owner_directory(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_install_rule_item_raw(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->item : (String_View){0};
}

String_View bm_query_install_rule_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->destination : (String_View){0};
}

String_View bm_query_install_rule_component(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->component : (String_View){0};
}

String_View bm_query_install_rule_namelink_component(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->namelink_component : (String_View){0};
}

String_View bm_query_install_rule_export_name(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->export_name : (String_View){0};
}

String_View bm_query_install_rule_archive_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->archive_destination : (String_View){0};
}

String_View bm_query_install_rule_library_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->library_destination : (String_View){0};
}

String_View bm_query_install_rule_runtime_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->runtime_destination : (String_View){0};
}

String_View bm_query_install_rule_includes_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->includes_destination : (String_View){0};
}

String_View bm_query_install_rule_public_header_destination(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->public_header_destination : (String_View){0};
}

BM_Target_Id bm_query_install_rule_target(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->resolved_target_id : BM_TARGET_ID_INVALID;
}

BM_Install_Rule_Id bm_query_install_rule_for_export_target(const Build_Model *model,
                                                           BM_Export_Id export_id,
                                                           BM_Target_Id target_id) {
    String_View export_name = bm_query_export_name(model, export_id);
    if (!model || target_id == BM_TARGET_ID_INVALID || export_name.count == 0) {
        return BM_INSTALL_RULE_ID_INVALID;
    }

    for (size_t i = 0; i < arena_arr_len(model->install_rules); ++i) {
        const BM_Install_Rule_Record *rule = &model->install_rules[i];
        if (rule->kind != BM_INSTALL_RULE_TARGET) continue;
        if (rule->resolved_target_id != target_id) continue;
        if (!nob_sv_eq(rule->export_name, export_name)) continue;
        return (BM_Install_Rule_Id)i;
    }

    return BM_INSTALL_RULE_ID_INVALID;
}

BM_Install_Rule_Id_Span bm_query_install_rules_for_component(const Build_Model *model,
                                                             String_View component,
                                                             Arena *scratch) {
    BM_Install_Rule_Id *matches = NULL;
    if (!model || !scratch) return (BM_Install_Rule_Id_Span){0};

    for (size_t i = 0; i < arena_arr_len(model->install_rules); ++i) {
        const BM_Install_Rule_Record *rule = &model->install_rules[i];
        if (!bm_component_name_matches(rule->component, component)) continue;
        if (!arena_arr_push(scratch, matches, (BM_Install_Rule_Id)i)) return (BM_Install_Rule_Id_Span){0};
    }

    return bm_install_rule_id_span(matches);
}

BM_Export_Kind bm_query_export_kind(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->kind : BM_EXPORT_INSTALL;
}

BM_Export_Source_Kind bm_query_export_source_kind(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->source_kind : BM_EXPORT_SOURCE_INSTALL_EXPORT;
}

BM_Directory_Id bm_query_export_owner_directory(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_export_name(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_export_namespace(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->export_namespace : (String_View){0};
}

String_View bm_query_export_destination(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->destination : (String_View){0};
}

String_View bm_query_export_file_name(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    if (!record) return (String_View){0};
    if (record->kind == BM_EXPORT_BUILD_TREE) {
        if (record->file_name.count > 0) return record->file_name;
        if (record->output_file_path.count == 0) return (String_View){0};
        {
            size_t start = 0;
            for (size_t i = 0; i < record->output_file_path.count; ++i) {
                if (record->output_file_path.data[i] == '/' || record->output_file_path.data[i] == '\\') {
                    start = i + 1;
                }
            }
            return nob_sv_from_parts(record->output_file_path.data + start,
                                     record->output_file_path.count - start);
        }
    }
    if (record->file_name.count > 0) return record->file_name;
    return record->name.count > 0 ? nob_sv_from_parts(record->name.data, record->name.count) : (String_View){0};
}

String_View bm_query_export_output_file_path(const Build_Model *model, BM_Export_Id id, Arena *scratch) {
    const BM_Export_Record *record = bm_model_export(model, id);
    String_View file_name = {0};
    String_View suffixed = {0};
    char *copy = NULL;
    int n = 0;
    if (!record || !scratch) return (String_View){0};
    if (record->kind == BM_EXPORT_BUILD_TREE) return record->output_file_path;
    if (record->kind == BM_EXPORT_PACKAGE_REGISTRY) return (String_View){0};
    file_name = bm_query_export_file_name(model, id);
    if (file_name.count == 0) return (String_View){0};
    if (!nob_sv_end_with(file_name, ".cmake")) {
        copy = arena_alloc(scratch, file_name.count + strlen(".cmake") + 1);
        if (!copy) return (String_View){0};
        n = snprintf(copy, file_name.count + strlen(".cmake") + 1, "%.*s.cmake", (int)file_name.count, file_name.data ? file_name.data : "");
        if (n < 0) return (String_View){0};
        suffixed = nob_sv_from_parts(copy, (size_t)n);
        file_name = suffixed;
    }
    if (record->destination.count == 0) return file_name;
    if (!bm_path_join(scratch, record->destination, file_name, &suffixed)) return (String_View){0};
    return suffixed;
}

String_View bm_query_export_component(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->component : (String_View){0};
}

BM_Target_Id_Span bm_query_export_targets(const Build_Model *model, BM_Export_Id id) {
    BM_Target_Id_Span span = {0};
    const BM_Export_Record *record = bm_model_export(model, id);
    if (!record) return span;
    span.items = record->target_ids;
    span.count = arena_arr_len(record->target_ids);
    return span;
}

bool bm_query_export_enabled(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->enabled : false;
}

String_View bm_query_export_package_name(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    if (!record || record->kind != BM_EXPORT_PACKAGE_REGISTRY) return (String_View){0};
    return record->name;
}

String_View bm_query_export_registry_prefix(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    if (!record || record->kind != BM_EXPORT_PACKAGE_REGISTRY) return (String_View){0};
    return record->registry_prefix;
}

String_View bm_query_export_cxx_modules_directory(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->cxx_modules_directory : (String_View){0};
}

bool bm_query_export_append(const Build_Model *model, BM_Export_Id id) {
    const BM_Export_Record *record = bm_model_export(model, id);
    return record ? record->append : false;
}

BM_Export_Id_Span bm_query_exports_for_component(const Build_Model *model,
                                                 String_View component,
                                                 Arena *scratch) {
    BM_Export_Id *matches = NULL;
    if (!model || !scratch) return (BM_Export_Id_Span){0};

    for (size_t i = 0; i < arena_arr_len(model->exports); ++i) {
        const BM_Export_Record *record = &model->exports[i];
        if (record->kind != BM_EXPORT_INSTALL) continue;
        if (!bm_component_name_matches(record->component, component)) continue;
        if (!arena_arr_push(scratch, matches, (BM_Export_Id)i)) return (BM_Export_Id_Span){0};
    }

    return bm_export_id_span(matches);
}

String_View bm_query_package_name(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->package_name : (String_View){0};
}

BM_Directory_Id bm_query_package_owner_directory(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_package_mode(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->mode : (String_View){0};
}

String_View bm_query_package_found_path(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->found_path : (String_View){0};
}

bool bm_query_package_found(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->found : false;
}

bool bm_query_package_required(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->required : false;
}

bool bm_query_package_quiet(const Build_Model *model, BM_Package_Id id) {
    const BM_Package_Record *package = bm_model_package(model, id);
    return package ? package->quiet : false;
}

BM_CPack_Install_Type_Id bm_query_cpack_install_type_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_CPACK_INSTALL_TYPE_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->cpack_install_types); ++i) {
        if (nob_sv_eq(model->cpack_install_types[i].name, name)) return (BM_CPack_Install_Type_Id)i;
    }
    return BM_CPACK_INSTALL_TYPE_ID_INVALID;
}

String_View bm_query_cpack_install_type_name(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_install_type_display_name(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->display_name : (String_View){0};
}

BM_Directory_Id bm_query_cpack_install_type_owner_directory(const Build_Model *model, BM_CPack_Install_Type_Id id) {
    const BM_CPack_Install_Type_Record *record = bm_model_cpack_install_type(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_CPack_Component_Group_Id bm_query_cpack_component_group_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_CPACK_COMPONENT_GROUP_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->cpack_component_groups); ++i) {
        if (nob_sv_eq(model->cpack_component_groups[i].name, name)) return (BM_CPack_Component_Group_Id)i;
    }
    return BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

String_View bm_query_cpack_component_group_name(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_component_group_display_name(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->display_name : (String_View){0};
}

String_View bm_query_cpack_component_group_description(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->description : (String_View){0};
}

BM_CPack_Component_Group_Id bm_query_cpack_component_group_parent(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->parent_group_id : BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

BM_Directory_Id bm_query_cpack_component_group_owner_directory(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

bool bm_query_cpack_component_group_expanded(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->expanded : false;
}

bool bm_query_cpack_component_group_bold_title(const Build_Model *model, BM_CPack_Component_Group_Id id) {
    const BM_CPack_Component_Group_Record *record = bm_model_cpack_component_group(model, id);
    return record ? record->bold_title : false;
}

BM_CPack_Component_Id bm_query_cpack_component_by_name(const Build_Model *model, String_View name) {
    if (!model) return BM_CPACK_COMPONENT_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(model->cpack_components); ++i) {
        if (nob_sv_eq(model->cpack_components[i].name, name)) return (BM_CPack_Component_Id)i;
    }
    return BM_CPACK_COMPONENT_ID_INVALID;
}

String_View bm_query_cpack_component_name(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->name : (String_View){0};
}

String_View bm_query_cpack_component_display_name(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->display_name : (String_View){0};
}

String_View bm_query_cpack_component_description(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->description : (String_View){0};
}

BM_CPack_Component_Group_Id bm_query_cpack_component_group(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->group_id : BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

BM_CPack_Component_Id_Span bm_query_cpack_component_dependencies(const Build_Model *model, BM_CPack_Component_Id id) {
    BM_CPack_Component_Id_Span span = {0};
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record) return span;
    span.items = record->dependency_ids;
    span.count = arena_arr_len(record->dependency_ids);
    return span;
}

BM_CPack_Install_Type_Id_Span bm_query_cpack_component_install_types(const Build_Model *model, BM_CPack_Component_Id id) {
    BM_CPack_Install_Type_Id_Span span = {0};
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record) return span;
    span.items = record->install_type_ids;
    span.count = arena_arr_len(record->install_type_ids);
    return span;
}

BM_Install_Rule_Id_Span bm_query_cpack_component_install_rules(const Build_Model *model,
                                                               BM_CPack_Component_Id id,
                                                               Arena *scratch) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record || !scratch) return (BM_Install_Rule_Id_Span){0};
    return bm_query_install_rules_for_component(model, record->name, scratch);
}

BM_Export_Id_Span bm_query_cpack_component_exports(const Build_Model *model,
                                                   BM_CPack_Component_Id id,
                                                   Arena *scratch) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    if (!record || !scratch) return (BM_Export_Id_Span){0};
    return bm_query_exports_for_component(model, record->name, scratch);
}

String_View bm_query_cpack_component_archive_file(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->archive_file : (String_View){0};
}

String_View bm_query_cpack_component_plist(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->plist : (String_View){0};
}

bool bm_query_cpack_component_required(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->required : false;
}

bool bm_query_cpack_component_hidden(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->hidden : false;
}

bool bm_query_cpack_component_disabled(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->disabled : false;
}

bool bm_query_cpack_component_downloaded(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->downloaded : false;
}

BM_Directory_Id bm_query_cpack_component_owner_directory(const Build_Model *model, BM_CPack_Component_Id id) {
    const BM_CPack_Component_Record *record = bm_model_cpack_component(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

BM_Directory_Id bm_query_cpack_package_owner_directory(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->owner_directory_id : BM_DIRECTORY_ID_INVALID;
}

String_View bm_query_cpack_package_name(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->package_name : (String_View){0};
}

String_View bm_query_cpack_package_version(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->package_version : (String_View){0};
}

String_View bm_query_cpack_package_file_name(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->package_file_name : (String_View){0};
}

String_View bm_query_cpack_package_output_directory(const Build_Model *model,
                                                    BM_CPack_Package_Id id,
                                                    Arena *scratch) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    const BM_Directory_Record *owner = NULL;
    if (!record) return (String_View){0};
    if (!scratch || bm_sv_is_abs_path_query(record->package_directory)) return record->package_directory;
    owner = bm_model_directory(model, record->owner_directory_id);
    if (!owner) return record->package_directory;
    if (bm_query_path_has_prefix(record->package_directory, owner->binary_dir) ||
        bm_query_path_has_prefix(record->package_directory, owner->source_dir)) {
        return record->package_directory;
    }
    return bm_join_relative_path_query(scratch, owner->binary_dir, record->package_directory);
}

BM_String_Span bm_query_cpack_package_generators(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? bm_string_span(record->generators) : (BM_String_Span){0};
}

bool bm_query_cpack_package_include_toplevel_directory(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->include_toplevel_directory : false;
}

bool bm_query_cpack_package_archive_component_install(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? record->archive_component_install : false;
}

BM_String_Span bm_query_cpack_package_components_all(const Build_Model *model, BM_CPack_Package_Id id) {
    const BM_CPack_Package_Record *record = bm_model_cpack_package(model, id);
    return record ? bm_string_span(record->components_all) : (BM_String_Span){0};
}
