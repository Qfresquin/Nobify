#include "build_model_internal.h"

bool bm_builder_handle_install_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_INSTALL_RULE_ADD: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Install_Rule_Record rule = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "install rule without an active directory", "emit directory enter before adding install rules");
            }

            rule.id = (BM_Install_Rule_Id)arena_arr_len(draft->install_rules);
            rule.kind = bm_install_rule_kind_from_event(ev->as.install_rule_add.rule_type);
            rule.owner_directory_id = current_directory_id;
            rule.provenance = bm_provenance_from_event(builder->arena, ev);
            rule.resolved_target_id = BM_TARGET_ID_INVALID;
            if (!bm_copy_string(builder->arena, ev->as.install_rule_add.item, &rule.item) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.destination, &rule.destination) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.component, &rule.component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.namelink_component, &rule.namelink_component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.export_name, &rule.export_name) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.archive_destination, &rule.archive_destination) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.library_destination, &rule.library_destination) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.runtime_destination, &rule.runtime_destination) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.includes_destination, &rule.includes_destination) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.public_header_destination, &rule.public_header_destination) ||
                !arena_arr_push(builder->arena, draft->install_rules, rule)) {
                return bm_builder_error(builder, ev, "failed to append install rule", "increase arena capacity");
            }
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected install handler event", "fix build model install dispatch");
    }
}
