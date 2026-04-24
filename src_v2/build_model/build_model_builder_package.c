#include "build_model_internal.h"

static BM_CPack_Package_Record *bm_draft_find_cpack_package(Build_Model_Draft *draft,
                                                            String_View package_key) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->cpack_packages); ++i) {
        if (nob_sv_eq(draft->cpack_packages[i].package_key, package_key)) return &draft->cpack_packages[i];
    }
    return NULL;
}

bool bm_builder_handle_package_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
    if (!builder || !draft || !ev) return false;
    if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
        return bm_builder_error(builder, ev, "package or CPack event without an active directory", "emit directory enter before recording package state");
    }

    switch (ev->h.kind) {
        case EVENT_PACKAGE_FIND_RESULT: {
            BM_Package_Record package = {0};
            package.id = (BM_Package_Id)arena_arr_len(draft->packages);
            package.owner_directory_id = current_directory_id;
            package.provenance = bm_provenance_from_event(builder->arena, ev);
            package.found = ev->as.package_find_result.found;
            package.required = ev->as.package_find_result.required;
            package.quiet = ev->as.package_find_result.quiet;
            if (!bm_copy_string(builder->arena, ev->as.package_find_result.package_name, &package.package_name) ||
                !bm_copy_string(builder->arena, ev->as.package_find_result.mode, &package.mode) ||
                !bm_copy_string(builder->arena, ev->as.package_find_result.found_path, &package.found_path) ||
                !arena_arr_push(builder->arena, draft->packages, package) ||
                !bm_add_name_index(builder->arena, &draft->package_name_index, package.package_name, package.id)) {
                return bm_builder_error(builder, ev, "failed to append package result", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_ADD_INSTALL_TYPE: {
            BM_CPack_Install_Type_Record install_type = {0};
            install_type.id = (BM_CPack_Install_Type_Id)arena_arr_len(draft->cpack_install_types);
            install_type.owner_directory_id = current_directory_id;
            install_type.provenance = bm_provenance_from_event(builder->arena, ev);
            if (!bm_copy_string(builder->arena, ev->as.cpack_add_install_type.name, &install_type.name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_install_type.display_name, &install_type.display_name) ||
                !arena_arr_push(builder->arena, draft->cpack_install_types, install_type)) {
                return bm_builder_error(builder, ev, "failed to append CPack install type", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_ADD_COMPONENT_GROUP: {
            BM_CPack_Component_Group_Record group = {0};
            group.id = (BM_CPack_Component_Group_Id)arena_arr_len(draft->cpack_component_groups);
            group.owner_directory_id = current_directory_id;
            group.provenance = bm_provenance_from_event(builder->arena, ev);
            group.parent_group_id = BM_CPACK_COMPONENT_GROUP_ID_INVALID;
            group.expanded = ev->as.cpack_add_component_group.expanded;
            group.bold_title = ev->as.cpack_add_component_group.bold_title;
            if (!bm_copy_string(builder->arena, ev->as.cpack_add_component_group.name, &group.name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component_group.display_name, &group.display_name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component_group.description, &group.description) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component_group.parent_group, &group.parent_group_name) ||
                !arena_arr_push(builder->arena, draft->cpack_component_groups, group)) {
                return bm_builder_error(builder, ev, "failed to append CPack component group", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_ADD_COMPONENT: {
            BM_CPack_Component_Record component = {0};
            component.id = (BM_CPack_Component_Id)arena_arr_len(draft->cpack_components);
            component.owner_directory_id = current_directory_id;
            component.provenance = bm_provenance_from_event(builder->arena, ev);
            component.group_id = BM_CPACK_COMPONENT_GROUP_ID_INVALID;
            component.required = ev->as.cpack_add_component.required;
            component.hidden = ev->as.cpack_add_component.hidden;
            component.disabled = ev->as.cpack_add_component.disabled;
            component.downloaded = ev->as.cpack_add_component.downloaded;
            if (!bm_copy_string(builder->arena, ev->as.cpack_add_component.name, &component.name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component.display_name, &component.display_name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component.description, &component.description) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component.group, &component.group_name) ||
                !bm_split_cmake_list(builder->arena, ev->as.cpack_add_component.depends, &component.dependency_names) ||
                !bm_split_cmake_list(builder->arena, ev->as.cpack_add_component.install_types, &component.install_type_names) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component.archive_file, &component.archive_file) ||
                !bm_copy_string(builder->arena, ev->as.cpack_add_component.plist, &component.plist) ||
                !arena_arr_push(builder->arena, draft->cpack_components, component)) {
                return bm_builder_error(builder, ev, "failed to append CPack component", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_PACKAGE_DECLARE: {
            BM_CPack_Package_Record record = {0};
            record.id = (BM_CPack_Package_Id)arena_arr_len(draft->cpack_packages);
            record.owner_directory_id = current_directory_id;
            record.provenance = bm_provenance_from_event(builder->arena, ev);
            record.include_toplevel_directory = ev->as.cpack_package_declare.include_toplevel_directory;
            record.archive_component_install = ev->as.cpack_package_declare.archive_component_install;
            if (!bm_copy_string(builder->arena, ev->as.cpack_package_declare.package_key, &record.package_key) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.package_name, &record.package_name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.package_version, &record.package_version) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.package_file_name, &record.package_file_name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.package_directory, &record.package_directory) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.archive_file_name, &record.archive_file_name) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.archive_file_extension, &record.archive_file_extension) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.components_grouping, &record.components_grouping) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_declare.project_config_file, &record.project_config_file) ||
                !bm_split_cmake_list(builder->arena, ev->as.cpack_package_declare.components_all, &record.components_all) ||
                !arena_arr_push(builder->arena, draft->cpack_packages, record)) {
                return bm_builder_error(builder, ev, "failed to append CPack package plan", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_PACKAGE_ADD_GENERATOR: {
            BM_CPack_Package_Record *record = bm_draft_find_cpack_package(draft, ev->as.cpack_package_add_generator.package_key);
            String_View generator = {0};
            if (!record) {
                return bm_builder_error(builder,
                                        ev,
                                        "CPack package generator referenced unknown package plan",
                                        "emit cpack_package_declare before cpack_package_add_generator");
            }
            if (!bm_copy_string(builder->arena, ev->as.cpack_package_add_generator.generator, &generator) ||
                !bm_append_string(builder->arena, &record->generators, generator)) {
                return bm_builder_error(builder, ev, "failed to append CPack package generator", "increase arena capacity");
            }
            return true;
        }

        case EVENT_CPACK_PACKAGE_ARCHIVE_NAME_OVERRIDE: {
            BM_CPack_Package_Record *record =
                bm_draft_find_cpack_package(draft, ev->as.cpack_package_archive_name_override.package_key);
            BM_String_Pair pair = {0};
            if (!record) {
                return bm_builder_error(builder,
                                        ev,
                                        "CPack archive name override referenced unknown package plan",
                                        "emit cpack_package_declare before cpack_package_archive_name_override");
            }
            if (!bm_copy_string(builder->arena, ev->as.cpack_package_archive_name_override.archive_key, &pair.key) ||
                !bm_copy_string(builder->arena, ev->as.cpack_package_archive_name_override.archive_file_name, &pair.value) ||
                !arena_arr_push(builder->arena, record->archive_name_overrides, pair)) {
                return bm_builder_error(builder, ev, "failed to append CPack archive name override", "increase arena capacity");
            }
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected package handler event", "fix build model package dispatch");
    }
}
