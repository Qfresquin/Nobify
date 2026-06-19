#include "build_model_internal.h"

static bool bm_install_item_has_tag_separator(String_View item) {
    for (size_t i = 0; i + 1 < item.count; ++i) {
        if (item.data[i] == ':' && item.data[i + 1] == ':') return true;
    }
    return false;
}

static BM_Install_Rule_Item_Kind bm_install_item_kind_from_string(String_View item) {
    if (nob_sv_starts_with(item, nob_sv_from_cstr("SCRIPT::"))) return BM_INSTALL_RULE_ITEM_SCRIPT;
    if (nob_sv_starts_with(item, nob_sv_from_cstr("CODE::"))) return BM_INSTALL_RULE_ITEM_CODE;
    if (nob_sv_starts_with(item, nob_sv_from_cstr("EXPORT_ANDROID_MK::"))) {
        return BM_INSTALL_RULE_ITEM_EXPORT_ANDROID_MK;
    }
    if (nob_sv_starts_with(item, nob_sv_from_cstr("IMPORTED_RUNTIME_ARTIFACTS::"))) {
        return BM_INSTALL_RULE_ITEM_IMPORTED_RUNTIME_ARTIFACTS;
    }
    if (nob_sv_starts_with(item, nob_sv_from_cstr("RUNTIME_DEPENDENCY_SET::"))) {
        return BM_INSTALL_RULE_ITEM_RUNTIME_DEPENDENCY_SET;
    }
    return bm_install_item_has_tag_separator(item) ? BM_INSTALL_RULE_ITEM_TAGGED_UNKNOWN
                                                  : BM_INSTALL_RULE_ITEM_PATH;
}

static bool bm_install_item_starts_with_generator_expression(String_View item) {
    String_View trimmed = nob_sv_trim(item);
    return trimmed.count >= 2 && trimmed.data[0] == '$' && trimmed.data[1] == '<';
}

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
                !bm_copy_string(builder->arena, ev->as.install_rule_add.rename, &rule.rename) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.component, &rule.component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.archive_component, &rule.archive_component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.library_component, &rule.library_component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.runtime_component, &rule.runtime_component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.includes_component, &rule.includes_component) ||
                !bm_copy_string(builder->arena, ev->as.install_rule_add.public_header_component, &rule.public_header_component) ||
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
            arena_arr_last(draft->install_rules).item_view.raw = arena_arr_last(draft->install_rules).item;
            arena_arr_last(draft->install_rules).item_view.kind =
                bm_install_item_kind_from_string(arena_arr_last(draft->install_rules).item);
            arena_arr_last(draft->install_rules).item_view.starts_with_generator_expression =
                bm_install_item_starts_with_generator_expression(arena_arr_last(draft->install_rules).item);
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected install handler event", "fix build model install dispatch");
    }
}
