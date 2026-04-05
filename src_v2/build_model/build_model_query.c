#include "build_model_internal.h"

#include "../genex/genex.h"

typedef enum {
    BM_EFFECTIVE_INCLUDE_DIRECTORIES = 0,
    BM_EFFECTIVE_COMPILE_DEFINITIONS,
    BM_EFFECTIVE_COMPILE_OPTIONS,
    BM_EFFECTIVE_COMPILE_FEATURES,
    BM_EFFECTIVE_LINK_LIBRARIES,
    BM_EFFECTIVE_LINK_OPTIONS,
    BM_EFFECTIVE_LINK_DIRECTORIES,
} BM_Effective_Query_Kind;

typedef struct {
    const Build_Model *model;
    const BM_Query_Eval_Context *eval_ctx;
    Arena *scratch;
    BM_Target_Id consumer_target_id;
    BM_Target_Id current_target_id;
} BM_Query_Genex_Context;

static bool bm_eval_item_span(const Build_Model *model,
                              BM_Target_Id owner_target_id,
                              const BM_Query_Eval_Context *ctx,
                              Arena *scratch,
                              BM_String_Item_Span raw_items,
                              BM_String_Item_Span *out);
static bool bm_query_target_raw_property_first(const Build_Model *model,
                                               BM_Target_Id id,
                                               String_View property_name,
                                               String_View *out);
static bool bm_append_split_values(Arena *scratch,
                                   BM_String_Item_View **out,
                                   BM_String_Item_View item,
                                   String_View value);
static bool bm_collect_target_raw_property_items(Arena *scratch,
                                                 BM_String_Item_View **out,
                                                 const BM_Target_Record *target,
                                                 String_View property_name,
                                                 BM_Visibility visibility);

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

static const BM_Test_Record *bm_model_test(const Build_Model *model, BM_Test_Id id) {
    if (!model || id == BM_TEST_ID_INVALID || (size_t)id >= arena_arr_len(model->tests)) return NULL;
    return &model->tests[id];
}

static const BM_Install_Rule_Record *bm_model_install_rule(const Build_Model *model, BM_Install_Rule_Id id) {
    if (!model || id == BM_INSTALL_RULE_ID_INVALID || (size_t)id >= arena_arr_len(model->install_rules)) return NULL;
    return &model->install_rules[id];
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

static BM_String_Item_Span bm_item_span(const BM_String_Item_View *items) {
    BM_String_Item_Span span = {0};
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

static BM_Target_Id_Span bm_target_id_span(const BM_Target_Id *items) {
    BM_Target_Id_Span span = {0};
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

static const BM_Raw_Property_Record *bm_find_raw_property(const BM_Raw_Property_Record *records,
                                                          String_View property_name) {
    for (size_t i = 0; i < arena_arr_len(records); ++i) {
        if (bm_sv_eq_ci_query(records[i].name, property_name)) return &records[i];
    }
    return NULL;
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

static bool bm_append_string_copy(Arena *scratch, String_View **out, String_View value) {
    char *copy = NULL;
    if (!scratch || !out) return false;
    if (value.count == 0) return arena_arr_push(scratch, *out, nob_sv_from_cstr(""));
    copy = arena_strndup(scratch, value.data ? value.data : "", value.count);
    if (!copy) return false;
    return arena_arr_push(scratch, *out, nob_sv_from_parts(copy, value.count));
}

static bool bm_push_item_copy(Arena *scratch,
                              BM_String_Item_View **out,
                              BM_String_Item_View item) {
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

static bool bm_collect_raw_property_items(Arena *scratch,
                                          BM_String_Item_View **out,
                                          const BM_Raw_Property_Record *record) {
    if (!scratch || !out || !record) return false;
    for (size_t i = 0; i < arena_arr_len(record->items); ++i) {
        if (!bm_push_item_copy(scratch, out, (BM_String_Item_View){
                .value = record->items[i],
                .visibility = BM_VISIBILITY_PRIVATE,
                .flags = record->flags,
                .provenance = record->provenance,
            })) {
            return false;
        }
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

        case BM_EFFECTIVE_LINK_LIBRARIES: {
            const BM_Raw_Property_Record *record =
                bm_find_raw_property(model->global_properties.raw_properties, nob_sv_from_cstr("LINK_LIBRARIES"));
            if (!record) return true;
            return bm_collect_raw_property_items(scratch, out, record);
        }

        case BM_EFFECTIVE_LINK_OPTIONS:
            return bm_collect_items(scratch, out, model->global_properties.link_options, false);

        case BM_EFFECTIVE_LINK_DIRECTORIES:
            return bm_collect_items(scratch, out, model->global_properties.link_directories, false);
    }

    return true;
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

            case BM_EFFECTIVE_LINK_LIBRARIES: {
                const BM_Raw_Property_Record *record =
                    bm_find_raw_property(directory->raw_properties, nob_sv_from_cstr("LINK_LIBRARIES"));
                if (record && !bm_collect_raw_property_items(scratch, out, record)) return false;
                break;
            }

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

static bool bm_collect_target_items(Arena *scratch,
                                    BM_String_Item_View **out,
                                    const BM_Target_Record *target,
                                    BM_Effective_Query_Kind kind,
                                    bool public_only) {
    String_View raw_property_name = nob_sv_from_cstr("");
    BM_Visibility raw_visibility = public_only ? BM_VISIBILITY_INTERFACE : BM_VISIBILITY_PRIVATE;
    if (!scratch || !out || !target) return false;

    switch (kind) {
        case BM_EFFECTIVE_INCLUDE_DIRECTORIES:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES")
                                            : nob_sv_from_cstr("INCLUDE_DIRECTORIES");
            if (!bm_collect_items(scratch, out, target->include_directories, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);

        case BM_EFFECTIVE_COMPILE_DEFINITIONS:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_COMPILE_DEFINITIONS")
                                            : nob_sv_from_cstr("COMPILE_DEFINITIONS");
            if (!bm_collect_items(scratch, out, target->compile_definitions, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);

        case BM_EFFECTIVE_COMPILE_OPTIONS:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS")
                                            : nob_sv_from_cstr("COMPILE_OPTIONS");
            if (!bm_collect_items(scratch, out, target->compile_options, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);

        case BM_EFFECTIVE_COMPILE_FEATURES: {
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES")
                                            : nob_sv_from_cstr("COMPILE_FEATURES");
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);
        }

        case BM_EFFECTIVE_LINK_LIBRARIES:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES")
                                            : nob_sv_from_cstr("LINK_LIBRARIES");
            if (!bm_collect_items(scratch, out, target->link_libraries, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);

        case BM_EFFECTIVE_LINK_OPTIONS:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_LINK_OPTIONS")
                                            : nob_sv_from_cstr("LINK_OPTIONS");
            if (!bm_collect_items(scratch, out, target->link_options, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);

        case BM_EFFECTIVE_LINK_DIRECTORIES:
            raw_property_name = public_only ? nob_sv_from_cstr("INTERFACE_LINK_DIRECTORIES")
                                            : nob_sv_from_cstr("LINK_DIRECTORIES");
            if (!bm_collect_items(scratch, out, target->link_directories, public_only)) return false;
            return bm_collect_target_raw_property_items(scratch, out, target, raw_property_name, raw_visibility);
    }

    return true;
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

static bool bm_resolve_link_library_target_id(const Build_Model *model,
                                              String_View item,
                                              BM_Target_Id *out) {
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    if (out) *out = BM_TARGET_ID_INVALID;
    if (!out) return false;

    id = bm_find_target_by_name_id(model, item);
    if (id == BM_TARGET_ID_INVALID) return false;

    id = bm_resolve_alias_target_id(model, id);
    if (id == BM_TARGET_ID_INVALID) return false;

    *out = id;
    return true;
}

static bool bm_collect_dependency_interface(const Build_Model *model,
                                            BM_Target_Id id,
                                            const BM_Query_Eval_Context *ctx,
                                            Arena *scratch,
                                            uint8_t *visited,
                                            BM_String_Item_View **out,
                                            BM_Effective_Query_Kind kind);

static bool bm_collect_dependency_usage_from_link_items(const Build_Model *model,
                                                        const BM_Target_Record *target,
                                                        const BM_Query_Eval_Context *ctx,
                                                        Arena *scratch,
                                                        uint8_t *visited,
                                                        BM_String_Item_View **out,
                                                        BM_Effective_Query_Kind kind) {
    BM_String_Item_Span evaluated = {0};
    if (!model || !target || !scratch || !visited || !out) return false;

    if (!bm_eval_item_span(model,
                           target->id,
                           ctx,
                           scratch,
                           bm_item_span(target->link_libraries),
                           &evaluated)) {
        return false;
    }

    for (size_t i = 0; i < evaluated.count; ++i) {
        BM_Target_Id dep_id = BM_TARGET_ID_INVALID;
        if (!bm_resolve_link_library_target_id(model, evaluated.items[i].value, &dep_id)) continue;
        if (!bm_collect_dependency_interface(model, dep_id, ctx, scratch, visited, out, kind)) return false;
    }

    return true;
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

static bool bm_collect_target_raw_property_items(Arena *scratch,
                                                 BM_String_Item_View **out,
                                                 const BM_Target_Record *target,
                                                 String_View property_name,
                                                 BM_Visibility visibility) {
    const BM_Raw_Property_Record *record = NULL;
    BM_String_Item_View item = {0};
    if (!scratch || !out || !target || property_name.count == 0) return false;
    record = bm_find_raw_property(target->raw_properties, property_name);
    if (!record) return true;

    item.visibility = visibility;
    item.flags = record->flags;
    item.provenance = record->provenance;
    for (size_t i = 0; i < arena_arr_len(record->items); ++i) {
        if (!bm_append_split_values(scratch, out, item, record->items[i])) return false;
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

    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("SOURCES"))) {
        return bm_query_append_joined_raw_record(scratch,
                                                 out,
                                                 &(BM_Raw_Property_Record){.items = target->sources});
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("LINK_LIBRARIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_libraries, BM_VISIBILITY_PRIVATE, BM_VISIBILITY_PUBLIC)) {
            return false;
        }
        bm_query_target_raw_property_first(model, id, property_name, &raw);
        return bm_query_append_joined_values(scratch, out, structured, raw);
    }
    if (bm_sv_eq_ci_query(property_name, nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"))) {
        if (!bm_query_append_joined_items(scratch, &structured, target->link_libraries, BM_VISIBILITY_PUBLIC, BM_VISIBILITY_INTERFACE)) {
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

static String_View bm_uppercase_copy_query(Arena *scratch, String_View value) {
    char *copy = NULL;
    if (!scratch || value.count == 0) return nob_sv_from_cstr("");
    copy = arena_strndup(scratch, value.data ? value.data : "", value.count);
    if (!copy) return nob_sv_from_cstr("");
    for (size_t i = 0; i < value.count; ++i) copy[i] = (char)toupper((unsigned char)copy[i]);
    return nob_sv_from_parts(copy, value.count);
}

static String_View bm_property_name_with_config_prefix(Arena *scratch,
                                                       const char *prefix,
                                                       String_View config) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !prefix || config.count == 0) return nob_sv_from_cstr("");
    nob_sb_append_cstr(&sb, prefix);
    nob_sb_append_buf(&sb, config.data ? config.data : "", config.count);
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return copy ? nob_sv_from_parts(copy, sb.count) : nob_sv_from_cstr("");
}

static bool bm_query_target_raw_property_first(const Build_Model *model,
                                               BM_Target_Id id,
                                               String_View property_name,
                                               String_View *out) {
    BM_String_Span span = bm_query_target_raw_property_items(model, id, property_name);
    if (out) *out = span.count > 0 ? span.items[0] : nob_sv_from_cstr("");
    return true;
}

static bool bm_query_target_imported_property_for_config(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         String_View active_config,
                                                         const char *config_prefix,
                                                         const char *base_name,
                                                         Arena *scratch,
                                                         String_View *out) {
    String_View upper_cfg = nob_sv_from_cstr("");
    String_View config_name = {0};
    String_View property_name = {0};
    String_View mapped_property = {0};
    if (!scratch || !out || !base_name || !config_prefix) return false;
    *out = nob_sv_from_cstr("");

    if (active_config.count > 0) {
        upper_cfg = bm_uppercase_copy_query(scratch, active_config);
        property_name = bm_property_name_with_config_prefix(scratch, config_prefix, upper_cfg);
        if (property_name.count > 0) {
            bm_query_target_raw_property_first(model, id, property_name, out);
            if (out->count > 0) return true;
        }

        mapped_property = bm_property_name_with_config_prefix(scratch, "MAP_IMPORTED_CONFIG_", upper_cfg);
        if (mapped_property.count > 0) {
            BM_String_Span mapped = bm_query_target_raw_property_items(model, id, mapped_property);
            for (size_t i = 0; i < mapped.count; ++i) {
                size_t start = 0;
                for (size_t k = 0; k <= mapped.items[i].count; ++k) {
                    bool sep = (k == mapped.items[i].count) || (mapped.items[i].data[k] == ';');
                    if (!sep) continue;
                    config_name = nob_sv_trim(nob_sv_from_parts(mapped.items[i].data + start, k - start));
                    start = k + 1;
                    if (config_name.count == 0) continue;
                    property_name = bm_property_name_with_config_prefix(
                        scratch,
                        config_prefix,
                        bm_uppercase_copy_query(scratch, config_name));
                    if (property_name.count == 0) continue;
                    bm_query_target_raw_property_first(model, id, property_name, out);
                    if (out->count > 0) return true;
                }
            }
        }
    }

    return bm_query_target_raw_property_first(model, id, nob_sv_from_cstr(base_name), out);
}

static bool bm_query_target_local_file_internal(const Build_Model *model,
                                                BM_Target_Id id,
                                                bool linker_file,
                                                Arena *scratch,
                                                String_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    BM_Target_Kind kind = bm_query_target_kind(model, resolved_id);
    BM_Directory_Id owner = bm_query_target_owner_directory(model, resolved_id);
    String_View output_name = bm_query_target_output_name(model, resolved_id);
    String_View prefix = bm_query_target_prefix(model, resolved_id);
    String_View suffix = bm_query_target_suffix(model, resolved_id);
    String_View owner_binary_dir = bm_query_directory_binary_dir(model, owner);
    String_View output_dir = nob_sv_from_cstr("");
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    String_View basename = {0};
    if (!scratch || !out || !bm_target_id_is_valid(resolved_id)) return false;
    *out = nob_sv_from_cstr("");

    if (kind == BM_TARGET_EXECUTABLE) {
        output_dir = bm_query_target_runtime_output_directory(model, resolved_id);
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
    } else if (kind == BM_TARGET_STATIC_LIBRARY) {
        output_dir = bm_query_target_archive_output_directory(model, resolved_id);
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        if (prefix.count == 0) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) suffix = nob_sv_from_cstr(".a");
        nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&sb, suffix.data ? suffix.data : "", suffix.count);
    } else if (kind == BM_TARGET_SHARED_LIBRARY || kind == BM_TARGET_MODULE_LIBRARY) {
        output_dir = bm_query_target_library_output_directory(model, resolved_id);
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        if (prefix.count == 0) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) suffix = nob_sv_from_cstr(".so");
        nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&sb, suffix.data ? suffix.data : "", suffix.count);
    } else {
        nob_sb_free(sb);
        return true;
    }

    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    basename = nob_sv_from_parts(copy, strlen(copy));

    if (output_dir.count == 0) {
        *out = bm_join_relative_path_query(scratch, owner_binary_dir, basename);
    } else {
        *out = bm_join_relative_path_query(scratch,
                                           bm_join_relative_path_query(scratch, owner_binary_dir, output_dir),
                                           basename);
    }
    (void)linker_file;
    return true;
}

static bool bm_query_target_effective_file_internal(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    bool linker_file,
                                                    Arena *scratch,
                                                    String_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    String_View property_value = nob_sv_from_cstr("");
    String_View source_base = {0};
    if (!scratch || !out || !target) return false;
    *out = nob_sv_from_cstr("");

    if (!target->imported) {
        return bm_query_target_local_file_internal(model, resolved_id, linker_file, scratch, out);
    }

    if (linker_file) {
        if (!bm_query_target_imported_property_for_config(model,
                                                          resolved_id,
                                                          ctx ? ctx->config : nob_sv_from_cstr(""),
                                                          "IMPORTED_IMPLIB_",
                                                          "IMPORTED_IMPLIB",
                                                          scratch,
                                                          &property_value)) {
            return false;
        }
        if (property_value.count == 0 &&
            !bm_query_target_imported_property_for_config(model,
                                                          resolved_id,
                                                          ctx ? ctx->config : nob_sv_from_cstr(""),
                                                          "IMPORTED_LOCATION_",
                                                          "IMPORTED_LOCATION",
                                                          scratch,
                                                          &property_value)) {
            return false;
        }
    } else if (!bm_query_target_imported_property_for_config(model,
                                                             resolved_id,
                                                             ctx ? ctx->config : nob_sv_from_cstr(""),
                                                             "IMPORTED_LOCATION_",
                                                             "IMPORTED_LOCATION",
                                                             scratch,
                                                             &property_value)) {
        return false;
    }

    if (property_value.count == 0) return true;
    source_base = bm_query_directory_source_dir(model, bm_query_target_owner_directory(model, resolved_id));
    *out = bm_join_relative_path_query(scratch, source_base, property_value);
    return true;
}

static String_View bm_query_genex_target_property_cb(void *userdata,
                                                     String_View target_name,
                                                     String_View property_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    id = bm_resolve_alias_target_id(ctx->model, id);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_property_value(ctx->model, id, property_name, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static String_View bm_query_genex_target_file_cb(void *userdata, String_View target_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_effective_file_internal(ctx->model, id, ctx->eval_ctx, false, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static String_View bm_query_genex_target_linker_file_cb(void *userdata, String_View target_name) {
    BM_Query_Genex_Context *ctx = (BM_Query_Genex_Context*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!ctx || !ctx->model) return nob_sv_from_cstr("");
    id = bm_find_target_by_name_id(ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_effective_file_internal(ctx->model, id, ctx->eval_ctx, true, ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static bool bm_eval_item_span(const Build_Model *model,
                              BM_Target_Id owner_target_id,
                              const BM_Query_Eval_Context *ctx,
                              Arena *scratch,
                              BM_String_Item_Span raw_items,
                              BM_String_Item_Span *out) {
    BM_String_Item_View *items = NULL;
    BM_Query_Genex_Context gx_userdata = {0};
    BM_Query_Eval_Context default_ctx = {0};
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;

    default_ctx = ctx ? *ctx : bm_default_query_eval_context(owner_target_id, BM_QUERY_USAGE_COMPILE);
    gx_userdata.model = model;
    gx_userdata.eval_ctx = &default_ctx;
    gx_userdata.scratch = scratch;
    gx_userdata.consumer_target_id = owner_target_id;
    gx_userdata.current_target_id = bm_target_id_is_valid(default_ctx.current_target_id)
        ? default_ctx.current_target_id
        : owner_target_id;

    for (size_t i = 0; i < raw_items.count; ++i) {
        Genex_Context gx = {0};
        Genex_Result gx_result = {0};
        gx.arena = scratch;
        gx.config = default_ctx.config;
        gx.platform_id = default_ctx.platform_id;
        gx.compile_language = default_ctx.compile_language;
        gx.current_target_name = bm_query_target_name(model, gx_userdata.current_target_id);
        gx.link_only_active = default_ctx.usage_mode == BM_QUERY_USAGE_LINK;
        gx.build_interface_active = default_ctx.build_interface_active;
        gx.install_interface_active = default_ctx.install_interface_active;
        gx.target_name_case_insensitive = false;
        gx.max_depth = 128;
        gx.max_target_property_depth = 64;
        gx.read_target_property = bm_query_genex_target_property_cb;
        gx.read_target_file = bm_query_genex_target_file_cb;
        gx.read_target_linker_file = bm_query_genex_target_linker_file_cb;
        gx.userdata = &gx_userdata;

        gx_result = genex_eval(&gx, raw_items.items[i].value);
        if (gx_result.status != GENEX_OK) return false;
        if (!bm_append_split_values(scratch, &items, raw_items.items[i], gx_result.value)) return false;
    }

    out->items = items;
    out->count = arena_arr_len(items);
    return true;
}

static bool bm_query_target_effective_items_common(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   const BM_Query_Eval_Context *ctx,
                                                   Arena *scratch,
                                                   BM_String_Item_Span *out,
                                                   BM_Effective_Query_Kind kind) {
    const BM_Target_Record *target = bm_model_target(model, id);
    BM_String_Item_View *raw_items = NULL;
    BM_String_Item_Span evaluated = {0};
    uint8_t *visited = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!model || !target || !scratch) return false;

    if (!bm_collect_global_effective_items(model, scratch, &raw_items, kind)) return false;
    if (!bm_collect_directory_chain_items(model, target->owner_directory_id, scratch, &raw_items, kind)) return false;
    if (!bm_collect_target_items(scratch, &raw_items, target, kind, false)) return false;

    visited = arena_alloc_array_zero(scratch, uint8_t, arena_arr_len(model->targets));
    if (!visited) return false;
    visited[id] = 1;

    if (!bm_collect_dependency_usage_from_link_items(model, target, ctx, scratch, visited, &raw_items, kind)) return false;
    if (!bm_eval_item_span(model,
                           id,
                           ctx,
                           scratch,
                           (BM_String_Item_Span){.items = raw_items, .count = arena_arr_len(raw_items)},
                           &evaluated)) {
        return false;
    }

    *out = evaluated;
    return true;
}

static bool bm_query_target_effective_values_common(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    Arena *scratch,
                                                    BM_String_Span *out,
                                                    BM_Effective_Query_Kind kind) {
    BM_String_Item_Span item_span = {0};
    String_View *values = NULL;

    if (!out) return false;
    out->items = NULL;
    out->count = 0;

    if (!bm_query_target_effective_items_common(model, id, ctx, scratch, &item_span, kind)) return false;

    for (size_t i = 0; i < item_span.count; ++i) {
        if (!arena_arr_push(scratch, values, item_span.items[i].value)) return false;
    }

    out->items = values;
    out->count = arena_arr_len(values);
    return true;
}

bool bm_model_has_project(const Build_Model *model) { return model ? model->project.present : false; }
bool bm_target_id_is_valid(BM_Target_Id id) { return id != BM_TARGET_ID_INVALID; }
bool bm_build_step_id_is_valid(BM_Build_Step_Id id) { return id != BM_BUILD_STEP_ID_INVALID; }
bool bm_directory_id_is_valid(BM_Directory_Id id) { return id != BM_DIRECTORY_ID_INVALID; }
bool bm_test_id_is_valid(BM_Test_Id id) { return id != BM_TEST_ID_INVALID; }
bool bm_package_id_is_valid(BM_Package_Id id) { return id != BM_PACKAGE_ID_INVALID; }

size_t bm_query_directory_count(const Build_Model *model) { return model ? arena_arr_len(model->directories) : 0; }
size_t bm_query_target_count(const Build_Model *model) { return model ? arena_arr_len(model->targets) : 0; }
size_t bm_query_build_step_count(const Build_Model *model) { return model ? arena_arr_len(model->build_steps) : 0; }
size_t bm_query_test_count(const Build_Model *model) { return model ? arena_arr_len(model->tests) : 0; }
size_t bm_query_install_rule_count(const Build_Model *model) { return model ? arena_arr_len(model->install_rules) : 0; }
size_t bm_query_package_count(const Build_Model *model) { return model ? arena_arr_len(model->packages) : 0; }
size_t bm_query_cpack_install_type_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_install_types) : 0; }
size_t bm_query_cpack_component_group_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_component_groups) : 0; }
size_t bm_query_cpack_component_count(const Build_Model *model) { return model ? arena_arr_len(model->cpack_components) : 0; }

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
    const BM_Target_Record *target = bm_model_target(model, id);
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

bool bm_query_target_is_alias(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? target->alias : false;
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

String_View bm_query_target_source_raw(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || source_index >= arena_arr_len(target->source_records)) return nob_sv_from_cstr("");
    return target->source_records[source_index].raw_path;
}

String_View bm_query_target_source_effective(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || source_index >= arena_arr_len(target->source_records)) return nob_sv_from_cstr("");
    return target->source_records[source_index].effective_path;
}

bool bm_query_target_source_generated(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || source_index >= arena_arr_len(target->source_records)) return false;
    return target->source_records[source_index].generated;
}

BM_Build_Step_Id bm_query_target_source_producer_step(const Build_Model *model, BM_Target_Id id, size_t source_index) {
    const BM_Target_Record *target = bm_model_target(model, id);
    if (!target || source_index >= arena_arr_len(target->source_records)) return BM_BUILD_STEP_ID_INVALID;
    return target->source_records[source_index].producer_step_id;
}

BM_Target_Id_Span bm_query_target_dependencies_explicit(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_target_id_span(target->explicit_dependency_ids) : (BM_Target_Id_Span){0};
}

BM_String_Item_Span bm_query_target_link_libraries_raw(const Build_Model *model, BM_Target_Id id) {
    const BM_Target_Record *target = bm_model_target(model, id);
    return target ? bm_item_span(target->link_libraries) : (BM_String_Item_Span){0};
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

bool bm_query_target_effective_include_directories_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_include_directories_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_include_directories_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_INCLUDE_DIRECTORIES);
}

bool bm_query_target_effective_compile_definitions_items(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         Arena *scratch,
                                                         BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_definitions_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_definitions_items_with_context(const Build_Model *model,
                                                                      BM_Target_Id id,
                                                                      const BM_Query_Eval_Context *ctx,
                                                                      Arena *scratch,
                                                                      BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_DEFINITIONS);
}

bool bm_query_target_effective_compile_options_items(const Build_Model *model,
                                                     BM_Target_Id id,
                                                     Arena *scratch,
                                                     BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_options_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_options_items_with_context(const Build_Model *model,
                                                                  BM_Target_Id id,
                                                                  const BM_Query_Eval_Context *ctx,
                                                                  Arena *scratch,
                                                                  BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_OPTIONS);
}

bool bm_query_target_effective_link_libraries_items(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    Arena *scratch,
                                                    BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_libraries_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_libraries_items_with_context(const Build_Model *model,
                                                                 BM_Target_Id id,
                                                                 const BM_Query_Eval_Context *ctx,
                                                                 Arena *scratch,
                                                                 BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_LIBRARIES);
}

bool bm_query_target_effective_link_options_items(const Build_Model *model,
                                                  BM_Target_Id id,
                                                  Arena *scratch,
                                                  BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_options_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_options_items_with_context(const Build_Model *model,
                                                               BM_Target_Id id,
                                                               const BM_Query_Eval_Context *ctx,
                                                               Arena *scratch,
                                                               BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_OPTIONS);
}

bool bm_query_target_effective_link_directories_items(const Build_Model *model,
                                                      BM_Target_Id id,
                                                      Arena *scratch,
                                                      BM_String_Item_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_directories_items_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_directories_items_with_context(const Build_Model *model,
                                                                   BM_Target_Id id,
                                                                   const BM_Query_Eval_Context *ctx,
                                                                   Arena *scratch,
                                                                   BM_String_Item_Span *out) {
    return bm_query_target_effective_items_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_DIRECTORIES);
}

bool bm_query_target_effective_include_directories(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_include_directories_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_include_directories_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_INCLUDE_DIRECTORIES);
}

bool bm_query_target_effective_compile_definitions(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_definitions_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_definitions_with_context(const Build_Model *model,
                                                                BM_Target_Id id,
                                                                const BM_Query_Eval_Context *ctx,
                                                                Arena *scratch,
                                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_DEFINITIONS);
}

bool bm_query_target_effective_compile_options(const Build_Model *model,
                                               BM_Target_Id id,
                                               Arena *scratch,
                                               BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_COMPILE);
    return bm_query_target_effective_compile_options_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_compile_options_with_context(const Build_Model *model,
                                                            BM_Target_Id id,
                                                            const BM_Query_Eval_Context *ctx,
                                                            Arena *scratch,
                                                            BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_OPTIONS);
}

bool bm_query_target_effective_link_libraries(const Build_Model *model,
                                              BM_Target_Id id,
                                              Arena *scratch,
                                              BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_libraries_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_libraries_with_context(const Build_Model *model,
                                                           BM_Target_Id id,
                                                           const BM_Query_Eval_Context *ctx,
                                                           Arena *scratch,
                                                           BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_LIBRARIES);
}

bool bm_query_target_effective_link_options(const Build_Model *model,
                                            BM_Target_Id id,
                                            Arena *scratch,
                                            BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_options_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_options_with_context(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         const BM_Query_Eval_Context *ctx,
                                                         Arena *scratch,
                                                         BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_OPTIONS);
}

bool bm_query_target_effective_link_directories(const Build_Model *model,
                                                BM_Target_Id id,
                                                Arena *scratch,
                                                BM_String_Span *out) {
    BM_Query_Eval_Context ctx = bm_default_query_eval_context(id, BM_QUERY_USAGE_LINK);
    return bm_query_target_effective_link_directories_with_context(model, id, &ctx, scratch, out);
}

bool bm_query_target_effective_link_directories_with_context(const Build_Model *model,
                                                             BM_Target_Id id,
                                                             const BM_Query_Eval_Context *ctx,
                                                             Arena *scratch,
                                                             BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_LINK_DIRECTORIES);
}

bool bm_query_target_effective_compile_features(const Build_Model *model,
                                                BM_Target_Id id,
                                                const BM_Query_Eval_Context *ctx,
                                                Arena *scratch,
                                                BM_String_Span *out) {
    return bm_query_target_effective_values_common(model, id, ctx, scratch, out, BM_EFFECTIVE_COMPILE_FEATURES);
}

bool bm_query_target_effective_file(const Build_Model *model,
                                    BM_Target_Id id,
                                    const BM_Query_Eval_Context *ctx,
                                    Arena *scratch,
                                    String_View *out) {
    return bm_query_target_effective_file_internal(model, id, ctx, false, scratch, out);
}

bool bm_query_target_effective_linker_file(const Build_Model *model,
                                           BM_Target_Id id,
                                           const BM_Query_Eval_Context *ctx,
                                           Arena *scratch,
                                           String_View *out) {
    return bm_query_target_effective_file_internal(model, id, ctx, true, scratch, out);
}

bool bm_query_target_imported_link_languages(const Build_Model *model,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *ctx,
                                             Arena *scratch,
                                             BM_String_Span *out) {
    String_View property_value = nob_sv_from_cstr("");
    String_View *values = NULL;
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;
    if (!bm_query_target_imported_property_for_config(model,
                                                      id,
                                                      ctx ? ctx->config : nob_sv_from_cstr(""),
                                                      "IMPORTED_LINK_INTERFACE_LANGUAGES_",
                                                      "IMPORTED_LINK_INTERFACE_LANGUAGES",
                                                      scratch,
                                                      &property_value)) {
        return false;
    }
    if (property_value.count == 0) return true;
    for (size_t i = 0, start = 0; i <= property_value.count; ++i) {
        bool sep = (i == property_value.count) || (property_value.data[i] == ';');
        String_View piece = {0};
        if (!sep) continue;
        piece = nob_sv_trim(nob_sv_from_parts(property_value.data + start, i - start));
        start = i + 1;
        if (piece.count == 0) continue;
        if (!bm_append_string_copy(scratch, &values, piece)) return false;
    }
    out->items = values;
    out->count = arena_arr_len(values);
    return true;
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

BM_Target_Id bm_query_install_rule_target(const Build_Model *model, BM_Install_Rule_Id id) {
    const BM_Install_Rule_Record *rule = bm_model_install_rule(model, id);
    return rule ? rule->resolved_target_id : BM_TARGET_ID_INVALID;
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
