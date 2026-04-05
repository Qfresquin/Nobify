#include "build_model_internal.h"

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

        default:
            return bm_builder_error(builder, ev, "unexpected export handler event", "fix build model export dispatch");
    }
}
