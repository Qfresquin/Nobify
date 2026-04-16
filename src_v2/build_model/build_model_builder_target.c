#include "build_model_internal.h"

static bool bm_assign_target_string(BM_Builder *builder,
                                    const Event *ev,
                                    String_View *dest,
                                    String_View value) {
    if (bm_copy_string(builder->arena, value, dest)) return true;
    return bm_builder_error(builder, ev, "failed to copy promoted target property", "increase arena capacity");
}

static bool bm_target_append_item(BM_Builder *builder,
                                  const Event *ev,
                                  BM_String_Item_View **dest,
                                  String_View value,
                                  Cmake_Visibility visibility,
                                  Event_Property_Mutate_Op op,
                                  uint32_t flags) {
    BM_String_Item_View item = {0};
    if (!bm_copy_string(builder->arena, value, &item.value)) {
        return bm_builder_error(builder, ev, "failed to copy target property item", "increase arena capacity");
    }
    item.visibility = bm_visibility_from_event(visibility);
    item.flags = flags;
    item.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!bm_apply_item_mutation(builder->arena, dest, &item, 1, op)) {
        return bm_builder_error(builder, ev, "failed to mutate target property item", "increase arena capacity");
    }
    return true;
}

static Event_Property_Mutate_Op bm_target_property_op_from_event(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return EVENT_PROPERTY_MUTATE_SET;
        case EV_PROP_PREPEND_LIST: return EVENT_PROPERTY_MUTATE_PREPEND_LIST;
        case EV_PROP_APPEND_LIST: return EVENT_PROPERTY_MUTATE_APPEND_LIST;
        case EV_PROP_APPEND_STRING: return EVENT_PROPERTY_MUTATE_APPEND_STRING;
    }
    return EVENT_PROPERTY_MUTATE_SET;
}

static bool bm_target_item_bucket_matches(const BM_String_Item_View *item,
                                          BM_Visibility visibility,
                                          uint32_t flags) {
    if (!item) return false;
    if (item->visibility != visibility) return false;
    return (item->flags & BM_ITEM_FLAG_SYSTEM) == (flags & BM_ITEM_FLAG_SYSTEM);
}

static bool bm_target_apply_promoted_item_mutation(Arena *arena,
                                                   BM_String_Item_View **dest,
                                                   const BM_String_Item_View *items,
                                                   size_t count,
                                                   Event_Property_Mutate_Op op,
                                                   BM_Visibility visibility,
                                                   uint32_t flags) {
    BM_String_Item_View *merged = NULL;
    if (!arena || !dest) return false;
    if (op != EVENT_PROPERTY_MUTATE_SET) {
        return bm_apply_item_mutation(arena, dest, items, count, op);
    }

    for (size_t i = 0; i < arena_arr_len(*dest); ++i) {
        if (bm_target_item_bucket_matches(&(*dest)[i], visibility, flags)) continue;
        if (!arena_arr_push(arena, merged, (*dest)[i])) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!arena_arr_push(arena, merged, items[i])) return false;
    }
    *dest = merged;
    return true;
}

static bool bm_target_promote_property_set(BM_Builder *builder,
                                           BM_Target_Record *target,
                                           const Event *ev,
                                           bool *promoted) {
    const Event_Target_Prop_Set *prop = NULL;
    BM_String_Item_View **dest = NULL;
    Cmake_Visibility visibility = EV_VISIBILITY_PRIVATE;
    BM_Visibility item_visibility = BM_VISIBILITY_PRIVATE;
    uint32_t flags = BM_ITEM_FLAG_NONE;
    String_View *values = NULL;
    BM_String_Item_View *items = NULL;
    Event_Property_Mutate_Op mutation_op = EVENT_PROPERTY_MUTATE_SET;
    if (promoted) *promoted = false;
    if (!builder || !target || !ev || ev->h.kind != EVENT_TARGET_PROP_SET) return false;

    prop = &ev->as.target_prop_set;
    mutation_op = bm_target_property_op_from_event(prop->op);
    if (mutation_op == EVENT_PROPERTY_MUTATE_APPEND_STRING) return true;

    if (bm_sv_eq_ci_lit(prop->key, "INCLUDE_DIRECTORIES")) {
        dest = &target->include_directories;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_INCLUDE_DIRECTORIES")) {
        dest = &target->include_directories;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES")) {
        dest = &target->include_directories;
        visibility = EV_VISIBILITY_INTERFACE;
        flags |= BM_ITEM_FLAG_SYSTEM;
    } else if (bm_sv_eq_ci_lit(prop->key, "COMPILE_DEFINITIONS")) {
        dest = &target->compile_definitions;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_COMPILE_DEFINITIONS")) {
        dest = &target->compile_definitions;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "COMPILE_OPTIONS")) {
        dest = &target->compile_options;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_COMPILE_OPTIONS")) {
        dest = &target->compile_options;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "COMPILE_FEATURES")) {
        dest = &target->compile_features;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_COMPILE_FEATURES")) {
        dest = &target->compile_features;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "LINK_LIBRARIES")) {
        dest = &target->link_libraries;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_LINK_LIBRARIES")) {
        dest = &target->link_libraries;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "LINK_OPTIONS")) {
        dest = &target->link_options;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_LINK_OPTIONS")) {
        dest = &target->link_options;
        visibility = EV_VISIBILITY_INTERFACE;
    } else if (bm_sv_eq_ci_lit(prop->key, "LINK_DIRECTORIES")) {
        dest = &target->link_directories;
        visibility = EV_VISIBILITY_PRIVATE;
    } else if (bm_sv_eq_ci_lit(prop->key, "INTERFACE_LINK_DIRECTORIES")) {
        dest = &target->link_directories;
        visibility = EV_VISIBILITY_INTERFACE;
    } else {
        return true;
    }
    item_visibility = bm_visibility_from_event(visibility);

    if (!bm_split_cmake_list(builder->arena, prop->value, &values)) {
        return bm_builder_error(builder, ev, "failed to split promoted target property items", "increase arena capacity");
    }

    for (size_t i = 0; i < arena_arr_len(values); ++i) {
        BM_String_Item_View item = {0};
        if (!bm_copy_string(builder->arena, values[i], &item.value)) {
            return bm_builder_error(builder, ev, "failed to copy promoted target property item", "increase arena capacity");
        }
        item.visibility = item_visibility;
        item.flags = flags;
        item.provenance = bm_provenance_from_event(builder->arena, ev);
        if (!arena_arr_push(builder->arena, items, item)) {
            return bm_builder_error(builder, ev, "failed to append promoted target property item", "increase arena capacity");
        }
    }

    if (!bm_target_apply_promoted_item_mutation(builder->arena,
                                                dest,
                                                items,
                                                arena_arr_len(items),
                                                mutation_op,
                                                item_visibility,
                                                flags)) {
        return bm_builder_error(builder, ev, "failed to mutate promoted target property items", "increase arena capacity");
    }

    if (promoted) *promoted = true;
    return true;
}

static bool bm_target_record_raw_set(BM_Builder *builder,
                                     BM_Target_Record *target,
                                     const Event *ev,
                                     String_View key,
                                     String_View value,
                                     Cmake_Target_Property_Op op) {
    String_View raw_items[1];
    raw_items[0] = value;
    return bm_record_raw_property(builder->arena,
                                  &target->raw_properties,
                                  key,
                                  (op == EV_PROP_APPEND_STRING) ? EVENT_PROPERTY_MUTATE_APPEND_STRING :
                                  (op == EV_PROP_PREPEND_LIST) ? EVENT_PROPERTY_MUTATE_PREPEND_LIST :
                                  (op == EV_PROP_APPEND_LIST) ? EVENT_PROPERTY_MUTATE_APPEND_LIST :
                                                                 EVENT_PROPERTY_MUTATE_SET,
                                  0,
                                  raw_items,
                                  1,
                                  bm_provenance_from_event(builder->arena, ev));
}

static bool bm_target_append_source_record(BM_Builder *builder,
                                           BM_Target_Record *target,
                                           const Event *ev) {
    BM_Target_Source_Record record = {0};
    Cmake_Visibility visibility = EV_VISIBILITY_PRIVATE;
    if (!builder || !target || !ev) return false;
    if (ev->as.target_add_source.visibility != EV_VISIBILITY_UNSPECIFIED) {
        visibility = ev->as.target_add_source.visibility;
    }
    record.kind =
        ev->as.target_add_source.source_kind == EVENT_TARGET_SOURCE_FILE_SET_HEADERS
            ? BM_TARGET_SOURCE_HEADER_FILE_SET
            : (ev->as.target_add_source.source_kind == EVENT_TARGET_SOURCE_FILE_SET_CXX_MODULES
                   ? BM_TARGET_SOURCE_CXX_MODULE_FILE_SET
                   : BM_TARGET_SOURCE_REGULAR);
    record.visibility = bm_visibility_from_event(visibility);
    if (!bm_copy_string(builder->arena, ev->as.target_add_source.path, &record.raw_path) ||
        !bm_copy_string(builder->arena, ev->as.target_add_source.file_set_name, &record.file_set_name)) {
        return bm_builder_error(builder, ev, "failed to copy target source path", "increase arena capacity");
    }
    record.producer_step_id = BM_BUILD_STEP_ID_INVALID;
    record.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!arena_arr_push(builder->arena, target->source_records, record)) {
        return bm_builder_error(builder, ev, "failed to append target source record", "increase arena capacity");
    }
    return true;
}

static BM_Target_File_Set_Record *bm_target_find_file_set(BM_Target_Record *target, String_View name) {
    if (!target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->file_sets); ++i) {
        if (nob_sv_eq(target->file_sets[i].name, name)) return &target->file_sets[i];
    }
    return NULL;
}

static BM_Target_File_Set_Record *bm_target_ensure_file_set(BM_Builder *builder,
                                                            BM_Target_Record *target,
                                                            const Event *ev,
                                                            String_View name) {
    BM_Target_File_Set_Record set = {0};
    BM_Target_File_Set_Record *existing = bm_target_find_file_set(target, name);
    if (existing) return existing;
    if (!builder || !target || !ev) return NULL;
    set.visibility = BM_VISIBILITY_PRIVATE;
    set.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!bm_copy_string(builder->arena, name, &set.name) ||
        !arena_arr_push(builder->arena, target->file_sets, set)) {
        return NULL;
    }
    return &arena_arr_last(target->file_sets);
}

static bool bm_build_step_append_string(BM_Builder *builder,
                                        const Event *ev,
                                        String_View **dest,
                                        String_View value) {
    String_View owned = {0};
    if (!bm_copy_string(builder->arena, value, &owned) ||
        !arena_arr_push(builder->arena, *dest, owned)) {
        return bm_builder_error(builder, ev, "failed to append build step item", "increase arena capacity");
    }
    return true;
}

static bool bm_build_step_append_command(BM_Builder *builder,
                                         const Event *ev,
                                         BM_Build_Step_Record *step,
                                         const String_View *argv,
                                         size_t argc) {
    BM_Build_Step_Command_Record command = {0};
    if (!builder || !ev || !step) return false;
    for (size_t i = 0; i < argc; ++i) {
        if (!bm_build_step_append_string(builder, ev, &command.argv, argv[i])) return false;
    }
    if (!arena_arr_push(builder->arena, step->commands, command)) {
        return bm_builder_error(builder, ev, "failed to append build step command", "increase arena capacity");
    }
    return true;
}

static BM_Target_Record *bm_target_ensure_placeholder(BM_Builder *builder,
                                                      const Event *ev,
                                                      String_View name) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
    BM_Target_Record target = {0};
    BM_Target_Record *existing = NULL;
    if (!builder || !draft) return NULL;
    existing = bm_draft_find_target(draft, name);
    if (existing) return existing;
    if (current_directory_id == BM_DIRECTORY_ID_INVALID) return NULL;

    target.id = (BM_Target_Id)arena_arr_len(draft->targets);
    target.owner_directory_id = current_directory_id;
    target.provenance = bm_provenance_from_event(builder->arena, ev);
    target.kind = BM_TARGET_UTILITY;
    if (!bm_copy_string(builder->arena, name, &target.name) ||
        !arena_arr_push(builder->arena, draft->targets, target) ||
        !bm_add_name_index(builder->arena, &draft->target_name_index, target.name, target.id)) {
        return NULL;
    }
    return &arena_arr_last(draft->targets);
}

bool bm_builder_handle_target_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_TARGET_DECLARE: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Target_Record *target = NULL;
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "target declaration without an active directory", "emit directory enter before declaring targets");
            }
            target = bm_draft_find_target(draft, ev->as.target_declare.name);
            if (target && target->declared) {
                return bm_builder_error(builder, ev, "duplicate target name", "ensure target names are unique");
            }
            if (!target) {
                target = bm_target_ensure_placeholder(builder, ev, ev->as.target_declare.name);
                if (!target) return bm_builder_error(builder, ev, "failed to append target", "increase arena capacity");
            }
            target->owner_directory_id = current_directory_id;
            target->provenance = bm_provenance_from_event(builder->arena, ev);
            target->kind = bm_target_kind_from_event(ev->as.target_declare.target_type);
            target->imported = ev->as.target_declare.imported;
            if (!target->imported) target->imported_global = false;
            target->alias = ev->as.target_declare.alias;
            target->alias_global = false;
            target->declared = true;
            target->alias_of_id = BM_TARGET_ID_INVALID;
            if (!bm_copy_string(builder->arena, ev->as.target_declare.alias_of, &target->alias_of_name)) {
                return bm_builder_error(builder, ev, "failed to copy target alias metadata", "increase arena capacity");
            }
            return true;
        }

        case EVENT_TARGET_ADD_SOURCE: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_add_source.target_name);
            Cmake_Visibility visibility = ev->as.target_add_source.visibility;
            String_View owned = {0};
            if (!target) return bm_builder_error(builder, ev, "target source references an unknown target", "declare the target before adding sources");
            if (visibility == EV_VISIBILITY_UNSPECIFIED) visibility = EV_VISIBILITY_PRIVATE;
            if (ev->as.target_add_source.file_set_name.count > 0 &&
                !bm_target_find_file_set(target, ev->as.target_add_source.file_set_name)) {
                return bm_builder_error(builder, ev, "target source references an unknown file set", "emit file set declaration before file set members");
            }
            if (ev->as.target_add_source.source_kind == EVENT_TARGET_SOURCE_REGULAR &&
                visibility != EV_VISIBILITY_INTERFACE) {
                if (!bm_copy_string(builder->arena, ev->as.target_add_source.path, &owned) ||
                    !arena_arr_push(builder->arena, target->sources, owned)) {
                    return bm_builder_error(builder, ev, "failed to append target source", "increase arena capacity");
                }
            }
            if (!bm_target_append_source_record(builder, target, ev)) return false;
            return true;
        }

        case EVENT_TARGET_FILE_SET_DECLARE: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_file_set_declare.target_name);
            BM_Target_File_Set_Record *file_set = NULL;
            if (!target) return bm_builder_error(builder, ev, "target file set references an unknown target", "declare the target before file sets");
            file_set = bm_target_ensure_file_set(builder, target, ev, ev->as.target_file_set_declare.set_name);
            if (!file_set) return bm_builder_error(builder, ev, "failed to append target file set", "increase arena capacity");
            if (file_set->name.count > 0 && arena_arr_len(file_set->base_dirs) > 0) {
                if (file_set->kind != (ev->as.target_file_set_declare.set_kind == EVENT_TARGET_FILE_SET_CXX_MODULES
                                           ? BM_TARGET_FILE_SET_CXX_MODULES
                                           : BM_TARGET_FILE_SET_HEADERS) ||
                    file_set->visibility != bm_visibility_from_event(ev->as.target_file_set_declare.visibility)) {
                    return bm_builder_error(builder, ev, "target file set declaration conflicts with an existing file set", "keep repeated file set declarations consistent");
                }
            }
            file_set->kind = ev->as.target_file_set_declare.set_kind == EVENT_TARGET_FILE_SET_CXX_MODULES
                ? BM_TARGET_FILE_SET_CXX_MODULES
                : BM_TARGET_FILE_SET_HEADERS;
            file_set->visibility = bm_visibility_from_event(ev->as.target_file_set_declare.visibility == EV_VISIBILITY_UNSPECIFIED
                ? EV_VISIBILITY_PRIVATE
                : ev->as.target_file_set_declare.visibility);
            return true;
        }

        case EVENT_TARGET_FILE_SET_ADD_BASE_DIR: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_file_set_add_base_dir.target_name);
            BM_Target_File_Set_Record *file_set = NULL;
            String_View owned = {0};
            if (!target) return bm_builder_error(builder, ev, "target file set base dir references an unknown target", "declare the target before file sets");
            file_set = bm_target_find_file_set(target, ev->as.target_file_set_add_base_dir.set_name);
            if (!file_set) return bm_builder_error(builder, ev, "target file set base dir references an unknown file set", "emit file set declaration before base dirs");
            if (!bm_copy_string(builder->arena, ev->as.target_file_set_add_base_dir.path, &owned) ||
                !arena_arr_push(builder->arena, file_set->base_dirs, owned)) {
                return bm_builder_error(builder, ev, "failed to append target file set base dir", "increase arena capacity");
            }
            return true;
        }

        case EVENT_TARGET_ADD_DEPENDENCY: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_add_dependency.target_name);
            String_View owned = {0};
            if (!target) return bm_builder_error(builder, ev, "target dependency references an unknown target", "declare the target before adding dependencies");
            if (!bm_copy_string(builder->arena, ev->as.target_add_dependency.dependency_name, &owned) ||
                !arena_arr_push(builder->arena, target->explicit_dependency_names, owned)) {
                return bm_builder_error(builder, ev, "failed to append explicit target dependency", "increase arena capacity");
            }
            return true;
        }

        case EVENT_TARGET_PROP_SET: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_prop_set.target_name);
            bool promoted = false;
            if (!target) {
                target = bm_target_ensure_placeholder(builder, ev, ev->as.target_prop_set.target_name);
            }
            if (!target) return bm_builder_error(builder, ev, "target property references an unknown target", "declare the target before setting properties");

            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "OUTPUT_NAME")) return bm_assign_target_string(builder, ev, &target->output_name, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "PREFIX")) return bm_assign_target_string(builder, ev, &target->prefix, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "SUFFIX")) return bm_assign_target_string(builder, ev, &target->suffix, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "ARCHIVE_OUTPUT_DIRECTORY")) return bm_assign_target_string(builder, ev, &target->archive_output_directory, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "LIBRARY_OUTPUT_DIRECTORY")) return bm_assign_target_string(builder, ev, &target->library_output_directory, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "RUNTIME_OUTPUT_DIRECTORY")) return bm_assign_target_string(builder, ev, &target->runtime_output_directory, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "FOLDER")) return bm_assign_target_string(builder, ev, &target->folder, ev->as.target_prop_set.value);
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "EXCLUDE_FROM_ALL")) {
                target->exclude_from_all = bm_sv_truthy(ev->as.target_prop_set.value);
                return true;
            }
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "WIN32_EXECUTABLE")) {
                target->win32_executable = bm_sv_truthy(ev->as.target_prop_set.value);
                return true;
            }
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "MACOSX_BUNDLE")) {
                target->macosx_bundle = bm_sv_truthy(ev->as.target_prop_set.value);
                return true;
            }
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "IMPORTED")) {
                target->imported = bm_sv_truthy(ev->as.target_prop_set.value);
                if (!target->imported) target->imported_global = false;
            }
            if (bm_sv_eq_ci_lit(ev->as.target_prop_set.key, "IMPORTED_GLOBAL")) {
                target->imported_global = bm_sv_truthy(ev->as.target_prop_set.value);
            }

            if (!bm_target_promote_property_set(builder, target, ev, &promoted)) {
                return false;
            }

            if (promoted) return true;

            if (!bm_target_record_raw_set(builder,
                                          target,
                                          ev,
                                          ev->as.target_prop_set.key,
                                          ev->as.target_prop_set.value,
                                          ev->as.target_prop_set.op)) {
                return bm_builder_error(builder, ev, "failed to record raw target property", "increase arena capacity");
            }
            return true;
        }

        case EVENT_TARGET_LINK_LIBRARIES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_link_libraries.target_name);
            if (!target) return bm_builder_error(builder, ev, "target link libraries references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->link_libraries,
                                         ev->as.target_link_libraries.item,
                                         ev->as.target_link_libraries.visibility,
                                         EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         BM_ITEM_FLAG_NONE);
        }

        case EVENT_TARGET_LINK_OPTIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_link_options.target_name);
            if (!target) return bm_builder_error(builder, ev, "target link options references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->link_options,
                                         ev->as.target_link_options.item,
                                         ev->as.target_link_options.visibility,
                                         ev->as.target_link_options.is_before
                                             ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                             : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         ev->as.target_link_options.is_before ? BM_ITEM_FLAG_BEFORE : BM_ITEM_FLAG_NONE);
        }

        case EVENT_TARGET_LINK_DIRECTORIES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_link_directories.target_name);
            if (!target) return bm_builder_error(builder, ev, "target link directories references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->link_directories,
                                         ev->as.target_link_directories.path,
                                         ev->as.target_link_directories.visibility,
                                         EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         BM_ITEM_FLAG_NONE);
        }

        case EVENT_TARGET_INCLUDE_DIRECTORIES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_include_directories.target_name);
            if (!target) return bm_builder_error(builder, ev, "target include directories references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->include_directories,
                                         ev->as.target_include_directories.path,
                                         ev->as.target_include_directories.visibility,
                                         ev->as.target_include_directories.is_before
                                             ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                             : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         (ev->as.target_include_directories.is_before ? BM_ITEM_FLAG_BEFORE : BM_ITEM_FLAG_NONE) |
                                             (ev->as.target_include_directories.is_system ? BM_ITEM_FLAG_SYSTEM : BM_ITEM_FLAG_NONE));
        }

        case EVENT_TARGET_COMPILE_DEFINITIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_compile_definitions.target_name);
            if (!target) return bm_builder_error(builder, ev, "target compile definitions references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->compile_definitions,
                                         ev->as.target_compile_definitions.item,
                                         ev->as.target_compile_definitions.visibility,
                                         EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         BM_ITEM_FLAG_NONE);
        }

        case EVENT_TARGET_COMPILE_OPTIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_compile_options.target_name);
            if (!target) return bm_builder_error(builder, ev, "target compile options references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->compile_options,
                                         ev->as.target_compile_options.item,
                                         ev->as.target_compile_options.visibility,
                                         ev->as.target_compile_options.is_before
                                             ? EVENT_PROPERTY_MUTATE_PREPEND_LIST
                                             : EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         ev->as.target_compile_options.is_before ? BM_ITEM_FLAG_BEFORE : BM_ITEM_FLAG_NONE);
        }

        case EVENT_TARGET_COMPILE_FEATURES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_compile_features.target_name);
            if (!target) return bm_builder_error(builder, ev, "target compile features references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->compile_features,
                                         ev->as.target_compile_features.item,
                                         ev->as.target_compile_features.visibility,
                                         EVENT_PROPERTY_MUTATE_APPEND_LIST,
                                         BM_ITEM_FLAG_NONE);
        }

        case EVENT_KIND_COUNT:
        default:
            return bm_builder_error(builder, ev, "unexpected target handler event", "fix build model target dispatch");
    }
}

bool bm_builder_handle_build_graph_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_SOURCE_MARK_GENERATED: {
            BM_Source_Generated_Mark_Record mark = {0};
            mark.generated = ev->as.source_mark_generated.generated;
            mark.provenance = bm_provenance_from_event(builder->arena, ev);
            if (!bm_copy_string(builder->arena, ev->as.source_mark_generated.path, &mark.path) ||
                !bm_copy_string(builder->arena, ev->as.source_mark_generated.directory_source_dir, &mark.directory_source_dir) ||
                !bm_copy_string(builder->arena, ev->as.source_mark_generated.directory_binary_dir, &mark.directory_binary_dir) ||
                !arena_arr_push(builder->arena, draft->generated_source_marks, mark)) {
                return bm_builder_error(builder, ev, "failed to append generated source mark", "increase arena capacity");
            }
            return true;
        }

        case EVENT_SOURCE_PROPERTY_MUTATE: {
            BM_Source_Property_Mutation_Record record = {0};
            record.op = (Event_Property_Mutate_Op)ev->as.source_property_mutate.op;
            record.provenance = bm_provenance_from_event(builder->arena, ev);
            if (!bm_copy_string(builder->arena, ev->as.source_property_mutate.path, &record.path) ||
                !bm_copy_string(builder->arena, ev->as.source_property_mutate.directory_source_dir, &record.directory_source_dir) ||
                !bm_copy_string(builder->arena, ev->as.source_property_mutate.directory_binary_dir, &record.directory_binary_dir) ||
                !bm_copy_string(builder->arena, ev->as.source_property_mutate.key, &record.key) ||
                !bm_copy_string(builder->arena, ev->as.source_property_mutate.value, &record.value) ||
                !arena_arr_push(builder->arena, draft->source_property_mutations, record)) {
                return bm_builder_error(builder, ev, "failed to append source property mutation", "increase arena capacity");
            }
            return true;
        }

        case EVENT_BUILD_STEP_DECLARE: {
            BM_Build_Step_Record step = {0};
            BM_Directory_Id owner_directory_id = bm_builder_current_directory_id(builder);
            if (owner_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "build step declaration without an active directory", "emit directory enter before build steps");
            }
            if (bm_draft_find_build_step_const(draft, ev->as.build_step_declare.step_key)) {
                return bm_builder_error(builder, ev, "duplicate build step key", "build step keys must be unique");
            }
            step.id = (BM_Build_Step_Id)arena_arr_len(draft->build_steps);
            step.owner_directory_id = owner_directory_id;
            step.provenance = bm_provenance_from_event(builder->arena, ev);
            step.kind = bm_build_step_kind_from_event(ev->as.build_step_declare.step_kind);
            step.owner_target_id = BM_TARGET_ID_INVALID;
            step.append = ev->as.build_step_declare.append;
            step.verbatim = ev->as.build_step_declare.verbatim;
            step.uses_terminal = ev->as.build_step_declare.uses_terminal;
            step.command_expand_lists = ev->as.build_step_declare.command_expand_lists;
            step.depends_explicit_only = ev->as.build_step_declare.depends_explicit_only;
            step.codegen = ev->as.build_step_declare.codegen;
            if (!bm_copy_string(builder->arena, ev->as.build_step_declare.step_key, &step.step_key) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.owner_target_name, &step.owner_target_name) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.working_directory, &step.working_directory) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.comment, &step.comment) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.main_dependency, &step.main_dependency) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.depfile, &step.depfile) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.job_pool, &step.job_pool) ||
                !bm_copy_string(builder->arena, ev->as.build_step_declare.job_server_aware, &step.job_server_aware) ||
                !arena_arr_push(builder->arena, draft->build_steps, step)) {
                return bm_builder_error(builder, ev, "failed to append build step", "increase arena capacity");
            }
            return true;
        }

        case EVENT_BUILD_STEP_ADD_OUTPUT:
        case EVENT_BUILD_STEP_ADD_BYPRODUCT:
        case EVENT_BUILD_STEP_ADD_DEPENDENCY:
        case EVENT_BUILD_STEP_ADD_COMMAND: {
            BM_Build_Step_Record *step = NULL;
            String_View step_key = nob_sv_from_cstr("");
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_OUTPUT) step_key = ev->as.build_step_add_output.step_key;
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_BYPRODUCT) step_key = ev->as.build_step_add_byproduct.step_key;
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_DEPENDENCY) step_key = ev->as.build_step_add_dependency.step_key;
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_COMMAND) step_key = ev->as.build_step_add_command.step_key;
            step = bm_draft_find_build_step(draft, step_key);
            if (!step) return bm_builder_error(builder, ev, "build step item references an unknown step key", "emit build step declaration before step items");

            if (ev->h.kind == EVENT_BUILD_STEP_ADD_OUTPUT) {
                return bm_build_step_append_string(builder, ev, &step->raw_outputs, ev->as.build_step_add_output.path);
            }
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_BYPRODUCT) {
                return bm_build_step_append_string(builder, ev, &step->raw_byproducts, ev->as.build_step_add_byproduct.path);
            }
            if (ev->h.kind == EVENT_BUILD_STEP_ADD_DEPENDENCY) {
                return bm_build_step_append_string(builder, ev, &step->raw_dependency_tokens, ev->as.build_step_add_dependency.item);
            }
            return bm_build_step_append_command(builder,
                                                ev,
                                                step,
                                                ev->as.build_step_add_command.argv,
                                                ev->as.build_step_add_command.argc);
        }

        case EVENT_KIND_COUNT:
        default:
            return bm_builder_error(builder, ev, "unexpected build graph handler event", "fix build graph dispatch");
    }
}
