#include "build_model_internal.h"

static BM_Export_Record *bm_builder_find_export_by_key(Build_Model_Draft *draft,
                                                       String_View export_key) {
    if (!draft || export_key.count == 0) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        if (nob_sv_eq(draft->exports[i].export_key, export_key)) {
            return &draft->exports[i];
        }
    }
    return NULL;
}

bool bm_builder_handle_export_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_EXPORT_INSTALL: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Export_Record record = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "export install without an active directory", "emit directory enter before adding installed exports");
            }

            record.id = (BM_Export_Id)arena_arr_len(draft->exports);
            record.owner_directory_id = current_directory_id;
            record.provenance = bm_provenance_from_event(builder->arena, ev);
            record.kind = BM_EXPORT_INSTALL;
            record.source_kind = BM_EXPORT_SOURCE_INSTALL_EXPORT;
            record.enabled = true;
            if (!bm_copy_string(builder->arena, ev->as.export_install.export_name, &record.name) ||
                !bm_copy_string(builder->arena, ev->as.export_install.export_namespace, &record.export_namespace) ||
                !bm_copy_string(builder->arena, ev->as.export_install.destination, &record.destination) ||
                !bm_copy_string(builder->arena, ev->as.export_install.file_name, &record.file_name) ||
                !bm_copy_string(builder->arena, ev->as.export_install.component, &record.component) ||
                !arena_arr_push(builder->arena, draft->exports, record)) {
                return bm_builder_error(builder, ev, "failed to append installed export", "increase arena capacity");
            }
            return true;
        }

        case EVENT_EXPORT_BUILD_DECLARE: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Export_Record record = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "build-tree export without an active directory", "emit directory enter before adding standalone exports");
            }

            record.id = (BM_Export_Id)arena_arr_len(draft->exports);
            record.owner_directory_id = current_directory_id;
            record.provenance = bm_provenance_from_event(builder->arena, ev);
            record.kind = BM_EXPORT_BUILD_TREE;
            switch (ev->as.export_build_declare.source_kind) {
                case EVENT_EXPORT_SOURCE_TARGETS:
                    record.source_kind = BM_EXPORT_SOURCE_TARGETS;
                    break;
                case EVENT_EXPORT_SOURCE_EXPORT_SET:
                    record.source_kind = BM_EXPORT_SOURCE_EXPORT_SET;
                    break;
                case EVENT_EXPORT_SOURCE_INSTALL_EXPORT:
                    record.source_kind = BM_EXPORT_SOURCE_INSTALL_EXPORT;
                    break;
                case EVENT_EXPORT_SOURCE_PACKAGE:
                    record.source_kind = BM_EXPORT_SOURCE_PACKAGE;
                    break;
            }
            record.enabled = true;
            record.append = ev->as.export_build_declare.append;
            if (!bm_copy_string(builder->arena, ev->as.export_build_declare.export_key, &record.export_key) ||
                !bm_copy_string(builder->arena, ev->as.export_build_declare.logical_name, &record.name) ||
                !bm_copy_string(builder->arena, ev->as.export_build_declare.file_path, &record.output_file_path) ||
                !bm_copy_string(builder->arena, ev->as.export_build_declare.export_namespace, &record.export_namespace) ||
                !bm_copy_string(builder->arena, ev->as.export_build_declare.cxx_modules_directory, &record.cxx_modules_directory) ||
                !arena_arr_push(builder->arena, draft->exports, record)) {
                return bm_builder_error(builder, ev, "failed to append build-tree export", "increase arena capacity");
            }
            return true;
        }

        case EVENT_EXPORT_BUILD_ADD_TARGET: {
            BM_Export_Record *record =
                bm_builder_find_export_by_key(draft, ev->as.export_build_add_target.export_key);
            String_View target_name = {0};
            if (!record) {
                return bm_builder_error(builder, ev, "export target membership references an unknown export key", "emit EVENT_EXPORT_BUILD_DECLARE before EVENT_EXPORT_BUILD_ADD_TARGET");
            }
            target_name = ev->as.export_build_add_target.target_name;
            if (!bm_copy_string(builder->arena, target_name, &target_name) ||
                !arena_arr_push(builder->arena, record->target_names, target_name)) {
                return bm_builder_error(builder, ev, "failed to append export target membership", "increase arena capacity");
            }
            return true;
        }

        case EVENT_EXPORT_PACKAGE_REGISTRY: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Export_Record record = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "package registry export without an active directory", "emit directory enter before adding standalone exports");
            }

            record.id = (BM_Export_Id)arena_arr_len(draft->exports);
            record.owner_directory_id = current_directory_id;
            record.provenance = bm_provenance_from_event(builder->arena, ev);
            record.kind = BM_EXPORT_PACKAGE_REGISTRY;
            record.source_kind = BM_EXPORT_SOURCE_PACKAGE;
            record.enabled = ev->as.export_package_registry.enabled;
            if (!bm_copy_string(builder->arena, ev->as.export_package_registry.package_name, &record.name) ||
                !bm_copy_string(builder->arena, ev->as.export_package_registry.prefix, &record.registry_prefix) ||
                !arena_arr_push(builder->arena, draft->exports, record)) {
                return bm_builder_error(builder, ev, "failed to append package registry export", "increase arena capacity");
            }
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected export handler event", "fix build model export dispatch");
    }
}
