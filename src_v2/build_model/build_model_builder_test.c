#include "build_model_internal.h"

bool bm_builder_handle_test_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_TEST_ENABLE:
            draft->testing_enabled = ev->as.test_enable.enabled;
            return true;

        case EVENT_TEST_ADD: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Test_Record test = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "test declaration without an active directory", "emit directory enter before adding tests");
            }
            test.id = (BM_Test_Id)arena_arr_len(draft->tests);
            test.owner_directory_id = current_directory_id;
            test.provenance = bm_provenance_from_event(builder->arena, ev);
            test.command_expand_lists = ev->as.test_add.command_expand_lists;
            if (!bm_copy_string(builder->arena, ev->as.test_add.name, &test.name) ||
                !bm_copy_string(builder->arena, ev->as.test_add.command, &test.command) ||
                !bm_copy_string(builder->arena, ev->as.test_add.working_dir, &test.working_dir) ||
                !arena_arr_push(builder->arena, draft->tests, test) ||
                !bm_add_name_index(builder->arena, &draft->test_name_index, test.name, test.id)) {
                return bm_builder_error(builder, ev, "failed to append test record", "increase arena capacity");
            }
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected test handler event", "fix build model test dispatch");
    }
}
