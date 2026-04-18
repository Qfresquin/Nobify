#include "build_model_internal.h"

static bool bm_directory_apply_mutation(Arena *arena,
                                        BM_String_Item_View **dest,
                                        const Event_Directory_Property_Mutate *mut,
                                        BM_Provenance provenance) {
    BM_String_Item_View *items = NULL;
    uint32_t flags = 0;
    if (mut->modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) flags |= BM_ITEM_FLAG_BEFORE;
    if (mut->modifier_flags & EVENT_PROPERTY_MODIFIER_SYSTEM) flags |= BM_ITEM_FLAG_SYSTEM;
    if (mut->op == EVENT_PROPERTY_MUTATE_PREPEND_LIST) flags |= BM_ITEM_FLAG_BEFORE;

    for (size_t i = 0; i < mut->item_count; ++i) {
        BM_String_Item_View item = {0};
        if (!bm_copy_string(arena, mut->items[i], &item.value)) return false;
        if (mut->typed_item_semantics && i < mut->typed_item_count &&
            !bm_copy_item_semantic(arena, &item.semantic, mut->typed_item_semantics[i])) {
            return false;
        }
        item.visibility = BM_VISIBILITY_PRIVATE;
        item.flags = flags;
        item.provenance = provenance;
        if (!arena_arr_push(arena, items, item)) return false;
    }
    return bm_apply_item_mutation(arena, dest, items, arena_arr_len(items), mut->op);
}

static bool bm_directory_apply_link_mutation(Arena *arena,
                                             BM_Link_Item_View **dest,
                                             const Event_Directory_Property_Mutate *mut,
                                             BM_Provenance provenance) {
    BM_Link_Item_View *items = NULL;
    uint32_t flags = 0;
    if (!arena || !dest || !mut) return false;
    if (mut->modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) flags |= BM_ITEM_FLAG_BEFORE;
    if (mut->modifier_flags & EVENT_PROPERTY_MODIFIER_SYSTEM) flags |= BM_ITEM_FLAG_SYSTEM;
    if (mut->op == EVENT_PROPERTY_MUTATE_PREPEND_LIST) flags |= BM_ITEM_FLAG_BEFORE;

    for (size_t i = 0; i < mut->typed_item_count; ++i) {
        BM_Link_Item_View item = {0};
        Event_Link_Item_Metadata semantic = {0};
        if (!bm_copy_string(arena, mut->typed_items[i], &item.value)) return false;
        if (mut->typed_item_semantics && i < mut->typed_item_count) semantic = mut->typed_item_semantics[i];
        item.visibility = BM_VISIBILITY_PRIVATE;
        item.flags = flags;
        item.provenance = provenance;
        item.target_id = BM_TARGET_ID_INVALID;
        if (!bm_copy_item_semantic(arena, &item.semantic, semantic) ||
            !arena_arr_push(arena, items, item)) {
            return false;
        }
    }

    return bm_apply_link_item_mutation(arena, dest, items, arena_arr_len(items), mut->op);
}

static bool bm_apply_property_event(Arena *arena,
                                    BM_String_Item_View **include_directories,
                                    BM_String_Item_View **system_include_directories,
                                    BM_Link_Item_View **link_libraries,
                                    BM_String_Item_View **link_directories,
                                    BM_String_Item_View **compile_definitions,
                                    BM_String_Item_View **compile_options,
                                    BM_String_Item_View **link_options,
                                    BM_Raw_Property_Record **raw_properties,
                                    const Event_Directory_Property_Mutate *mut,
                                    BM_Provenance provenance) {
    if (bm_sv_eq_ci_lit(mut->property_name, "INCLUDE_DIRECTORIES")) {
        const String_View *items_src = mut->typed_item_count > 0 ? mut->typed_items : mut->items;
        size_t count = mut->typed_item_count > 0 ? mut->typed_item_count : mut->item_count;
        Event_Directory_Property_Mutate typed = *mut;
        typed.items = (String_View*)items_src;
        typed.item_count = count;
        if (mut->modifier_flags & EVENT_PROPERTY_MODIFIER_SYSTEM) {
            return bm_directory_apply_mutation(arena, system_include_directories, &typed, provenance);
        }
        return bm_directory_apply_mutation(arena, include_directories, &typed, provenance);
    }

    if (bm_sv_eq_ci_lit(mut->property_name, "LINK_DIRECTORIES")) {
        Event_Directory_Property_Mutate typed = *mut;
        if (mut->typed_item_count > 0) {
            typed.items = mut->typed_items;
            typed.item_count = mut->typed_item_count;
        }
        return bm_directory_apply_mutation(arena, link_directories, &typed, provenance);
    }

    if (bm_sv_eq_ci_lit(mut->property_name, "LINK_LIBRARIES")) {
        return bm_directory_apply_link_mutation(arena, link_libraries, mut, provenance) &&
               bm_record_raw_property(arena,
                                      raw_properties,
                                      mut->property_name,
                                      mut->op,
                                      mut->modifier_flags,
                                      mut->items,
                                      mut->item_count,
                                      provenance);
    }

    if (bm_sv_eq_ci_lit(mut->property_name, "COMPILE_DEFINITIONS")) {
        Event_Directory_Property_Mutate typed = *mut;
        if (mut->typed_item_count > 0) {
            typed.items = mut->typed_items;
            typed.item_count = mut->typed_item_count;
        }
        return bm_directory_apply_mutation(arena, compile_definitions, &typed, provenance);
    }

    if (bm_sv_eq_ci_lit(mut->property_name, "COMPILE_OPTIONS")) {
        Event_Directory_Property_Mutate typed = *mut;
        if (mut->typed_item_count > 0) {
            typed.items = mut->typed_items;
            typed.item_count = mut->typed_item_count;
        }
        return bm_directory_apply_mutation(arena, compile_options, &typed, provenance);
    }

    if (bm_sv_eq_ci_lit(mut->property_name, "LINK_OPTIONS")) {
        Event_Directory_Property_Mutate typed = *mut;
        if (mut->typed_item_count > 0) {
            typed.items = mut->typed_items;
            typed.item_count = mut->typed_item_count;
        }
        return bm_directory_apply_mutation(arena, link_options, &typed, provenance);
    }

    return bm_record_raw_property(arena,
                                  raw_properties,
                                  mut->property_name,
                                  mut->op,
                                  mut->modifier_flags,
                                  mut->items,
                                  mut->item_count,
                                  provenance);
}

bool bm_builder_handle_directory_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_DIRECTORY_ENTER: {
            BM_Directory_Record directory = {0};
            directory.id = (BM_Directory_Id)arena_arr_len(draft->directories);
            directory.parent_id = bm_builder_current_directory_id(builder);
            directory.owner_directory_id = directory.id;
            directory.provenance = bm_provenance_from_event(builder->arena, ev);
            if (!bm_copy_string(builder->arena, ev->as.directory_enter.source_dir, &directory.source_dir) ||
                !bm_copy_string(builder->arena, ev->as.directory_enter.binary_dir, &directory.binary_dir) ||
                !arena_arr_push(builder->arena, draft->directories, directory) ||
                !arena_arr_push(builder->arena, builder->directory_stack, directory.id)) {
                return bm_builder_error(builder, ev, "failed to append directory record", "increase arena capacity");
            }
            if (draft->root_directory_id == BM_DIRECTORY_ID_INVALID) draft->root_directory_id = directory.id;
            return true;
        }

        case EVENT_DIRECTORY_LEAVE: {
            BM_Directory_Id current_id = bm_builder_current_directory_id(builder);
            if (current_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "directory leave without an active directory", "emit matched directory enter/leave pairs");
            }

            const BM_Directory_Record *current = bm_draft_get_directory_const(draft, current_id);
            if (!current ||
                !nob_sv_eq(current->source_dir, ev->as.directory_leave.source_dir) ||
                !nob_sv_eq(current->binary_dir, ev->as.directory_leave.binary_dir)) {
                return bm_builder_error(builder, ev, "directory leave does not match active directory frame", "preserve exact enter/leave nesting");
            }

            arena_arr_set_len(builder->directory_stack, arena_arr_len(builder->directory_stack) - 1);
            return true;
        }

        case EVENT_DIRECTORY_PROPERTY_MUTATE: {
            BM_Directory_Record *directory = bm_draft_get_directory(draft, bm_builder_current_directory_id(builder));
            if (!directory) {
                return bm_builder_error(builder, ev, "directory property mutation without an active directory", "emit directory enter before mutating directory properties");
            }
            if (!bm_apply_property_event(builder->arena,
                                         &directory->include_directories,
                                         &directory->system_include_directories,
                                         &directory->link_libraries,
                                         &directory->link_directories,
                                         &directory->compile_definitions,
                                         &directory->compile_options,
                                         &directory->link_options,
                                         &directory->raw_properties,
                                         &ev->as.directory_property_mutate,
                                         bm_provenance_from_event(builder->arena, ev))) {
                return bm_builder_error(builder, ev, "failed to record directory property mutation", "increase arena capacity");
            }
            return true;
        }

        case EVENT_GLOBAL_PROPERTY_MUTATE:
            if (!bm_apply_property_event(builder->arena,
                                         &draft->global_properties.include_directories,
                                         &draft->global_properties.system_include_directories,
                                         &draft->global_properties.link_libraries,
                                         &draft->global_properties.link_directories,
                                         &draft->global_properties.compile_definitions,
                                         &draft->global_properties.compile_options,
                                         &draft->global_properties.link_options,
                                         &draft->global_properties.raw_properties,
                                         &ev->as.global_property_mutate,
                                         bm_provenance_from_event(builder->arena, ev))) {
                return bm_builder_error(builder, ev, "failed to record global property mutation", "increase arena capacity");
            }
            return true;

        default:
            return bm_builder_error(builder, ev, "unexpected directory handler event", "fix build model directory dispatch");
    }
}
