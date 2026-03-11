#include "build_model_internal.h"

bool bm_builder_handle_project_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_PROJECT_DECLARE: {
            BM_Project_Record project = {0};
            project.present = true;
            project.declaration_provenance = bm_provenance_from_event(builder->arena, ev);
            project.minimum_required_version = draft->project.minimum_required_version;
            project.minimum_required_fatal_if_too_old = draft->project.minimum_required_fatal_if_too_old;
            project.minimum_required_provenance = draft->project.minimum_required_provenance;
            if (!bm_copy_string(builder->arena, ev->as.project_declare.name, &project.name) ||
                !bm_copy_string(builder->arena, ev->as.project_declare.version, &project.version) ||
                !bm_copy_string(builder->arena, ev->as.project_declare.description, &project.description) ||
                !bm_copy_string(builder->arena, ev->as.project_declare.homepage_url, &project.homepage_url) ||
                !bm_split_cmake_list(builder->arena, ev->as.project_declare.languages, &project.languages)) {
                return bm_builder_error(builder, ev, "failed to record project declaration", "increase arena capacity");
            }
            draft->project = project;
            return true;
        }

        case EVENT_PROJECT_MINIMUM_REQUIRED:
            if (!bm_copy_string(builder->arena,
                                ev->as.project_minimum_required.version,
                                &draft->project.minimum_required_version)) {
                return bm_builder_error(builder, ev, "failed to record minimum required version", "increase arena capacity");
            }
            draft->project.minimum_required_fatal_if_too_old = ev->as.project_minimum_required.fatal_if_too_old;
            draft->project.minimum_required_provenance = bm_provenance_from_event(builder->arena, ev);
            return true;

        default:
            return bm_builder_error(builder, ev, "unexpected project handler event", "fix build model project dispatch");
    }
}
