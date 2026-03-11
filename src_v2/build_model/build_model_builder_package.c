#include "build_model_internal.h"

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

        default:
            return bm_builder_error(builder, ev, "unexpected package handler event", "fix build model package dispatch");
    }
}
