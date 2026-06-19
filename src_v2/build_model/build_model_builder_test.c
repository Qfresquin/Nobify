#include "build_model_internal.h"

static bool bm_test_sv_eq_ci(String_View lhs, String_View rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        char a = lhs.data ? lhs.data[i] : '\0';
        char b = rhs.data ? rhs.data[i] : '\0';
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return true;
}

static bool bm_test_property_kind_from_name(String_View name, BM_Test_Property_Kind *out) {
    static const struct {
        const char *name;
        BM_Test_Property_Kind kind;
    } known[] = {
        {"DISABLED", BM_TEST_PROPERTY_DISABLED},
        {"WILL_FAIL", BM_TEST_PROPERTY_WILL_FAIL},
        {"SKIP_RETURN_CODE", BM_TEST_PROPERTY_SKIP_RETURN_CODE},
        {"PASS_REGULAR_EXPRESSION", BM_TEST_PROPERTY_PASS_REGULAR_EXPRESSION},
        {"FAIL_REGULAR_EXPRESSION", BM_TEST_PROPERTY_FAIL_REGULAR_EXPRESSION},
        {"SKIP_REGULAR_EXPRESSION", BM_TEST_PROPERTY_SKIP_REGULAR_EXPRESSION},
        {"TIMEOUT", BM_TEST_PROPERTY_TIMEOUT},
        {"TIMEOUT_AFTER_MATCH", BM_TEST_PROPERTY_TIMEOUT_AFTER_MATCH},
        {"REQUIRED_FILES", BM_TEST_PROPERTY_REQUIRED_FILES},
        {"ENVIRONMENT", BM_TEST_PROPERTY_ENVIRONMENT},
        {"ENVIRONMENT_MODIFICATION", BM_TEST_PROPERTY_ENVIRONMENT_MODIFICATION},
        {"DEPENDS", BM_TEST_PROPERTY_DEPENDS},
        {"FIXTURES_SETUP", BM_TEST_PROPERTY_FIXTURES_SETUP},
        {"FIXTURES_REQUIRED", BM_TEST_PROPERTY_FIXTURES_REQUIRED},
        {"FIXTURES_CLEANUP", BM_TEST_PROPERTY_FIXTURES_CLEANUP},
        {"LABELS", BM_TEST_PROPERTY_LABELS},
        {"WORKING_DIRECTORY", BM_TEST_PROPERTY_WORKING_DIRECTORY},
        {"CROSSCOMPILING_EMULATOR", BM_TEST_PROPERTY_CROSSCOMPILING_EMULATOR},
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(known); ++i) {
        if (bm_test_sv_eq_ci(name, nob_sv_from_cstr(known[i].name))) {
            if (out) *out = known[i].kind;
            return true;
        }
    }
    return false;
}

static BM_Test_Property_Record *bm_test_property_ensure(BM_Builder *builder,
                                                        BM_Test_Record *test,
                                                        BM_Test_Property_Kind kind,
                                                        const Event *ev) {
    BM_Test_Property_Record record = {0};
    if (!builder || !test) return NULL;
    for (size_t i = 0; i < arena_arr_len(test->properties); ++i) {
        if (test->properties[i].kind == kind) return &test->properties[i];
    }
    record.kind = kind;
    record.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!arena_arr_push(builder->arena, test->properties, record)) {
        bm_builder_error(builder, ev, "failed to append typed test property", "increase arena capacity");
        return NULL;
    }
    return &arena_arr_last(test->properties);
}

static bool bm_test_copy_items(BM_Builder *builder,
                               const Event *ev,
                               const String_View *items,
                               size_t item_count,
                               String_View **out) {
    if (!builder || !out) return false;
    *out = NULL;
    for (size_t i = 0; i < item_count; ++i) {
        String_View owned = {0};
        if (!bm_copy_string(builder->arena, items[i], &owned) ||
            !arena_arr_push(builder->arena, *out, owned)) {
            return bm_builder_error(builder, ev, "failed to copy typed test property item", "increase arena capacity");
        }
    }
    return true;
}

static bool bm_test_join_items(Arena *arena, const String_View *items, size_t item_count, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < item_count; ++i) {
        if (i > 0) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, items[i].data ? items[i].data : "", items[i].count);
    }
    if (sb.count == 0) {
        nob_sb_free(sb);
        return true;
    }
    size_t count = sb.count;
    copy = arena_strndup(arena, sb.items ? sb.items : "", count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, count);
    return true;
}

static BM_Directory_Id bm_draft_find_directory_by_source(const Build_Model_Draft *draft,
                                                         String_View source_dir) {
    if (!draft) return BM_DIRECTORY_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->directories); ++i) {
        if (nob_sv_eq(draft->directories[i].source_dir, source_dir)) return (BM_Directory_Id)i;
    }
    return BM_DIRECTORY_ID_INVALID;
}

static BM_Test_Record *bm_draft_find_test_in_directory(Build_Model_Draft *draft,
                                                       String_View name,
                                                       BM_Directory_Id owner_directory_id) {
    if (!draft || owner_directory_id == BM_DIRECTORY_ID_INVALID) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->tests); ++i) {
        if (draft->tests[i].owner_directory_id == owner_directory_id &&
            nob_sv_eq(draft->tests[i].name, name)) {
            return &draft->tests[i];
        }
    }
    return NULL;
}

static BM_Target_Id bm_draft_resolve_alias_target_id(const Build_Model_Draft *draft, BM_Target_Id id) {
    size_t remaining = draft ? arena_arr_len(draft->targets) : 0;
    BM_Target_Id current = id;
    while (remaining-- > 0) {
        if (current == BM_TARGET_ID_INVALID || (size_t)current >= arena_arr_len(draft->targets)) {
            return BM_TARGET_ID_INVALID;
        }
        const BM_Target_Record *target = &draft->targets[current];
        if (!target->alias) return current;
        current = target->alias_of_id;
        if (current == BM_TARGET_ID_INVALID && !bm_string_view_is_empty(target->alias_of_name)) {
            current = bm_draft_find_target_id(draft, target->alias_of_name);
        }
    }
    return BM_TARGET_ID_INVALID;
}

static BM_Target_Id bm_draft_resolve_test_command_target(const Build_Model_Draft *draft,
                                                         const BM_Test_Record *test) {
    if (!draft || !test || !test->uses_name_signature || arena_arr_len(test->command_argv) == 0) {
        return BM_TARGET_ID_INVALID;
    }
    BM_Target_Id id = bm_draft_find_target_id(draft, test->command_argv[0]);
    id = bm_draft_resolve_alias_target_id(draft, id);
    if (id == BM_TARGET_ID_INVALID || (size_t)id >= arena_arr_len(draft->targets)) return BM_TARGET_ID_INVALID;
    return draft->targets[id].kind == BM_TARGET_EXECUTABLE ? id : BM_TARGET_ID_INVALID;
}

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
            test.uses_name_signature = ev->as.test_add.uses_name_signature;
            test.resolved_command_target_id = BM_TARGET_ID_INVALID;
            for (size_t arg_index = 0; arg_index < ev->as.test_add.command_arg_count; ++arg_index) {
                String_View owned_arg = {0};
                if (!bm_copy_string(builder->arena,
                                    ev->as.test_add.command_argv[arg_index],
                                    &owned_arg) ||
                    !arena_arr_push(builder->arena, test.command_argv, owned_arg)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "failed to append test command argument",
                                            "increase arena capacity");
                }
            }
            if (ev->as.test_add.command_arg_count == 0 && ev->as.test_add.command.count > 0) {
                String_View owned_arg = {0};
                if (!bm_copy_string(builder->arena, ev->as.test_add.command, &owned_arg) ||
                    !arena_arr_push(builder->arena, test.command_argv, owned_arg)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "failed to append legacy test command argument",
                                            "increase arena capacity");
                }
            }
            for (size_t cfg_index = 0; cfg_index < ev->as.test_add.configuration_count; ++cfg_index) {
                String_View owned_cfg = {0};
                if (!bm_copy_string(builder->arena,
                                    ev->as.test_add.configurations[cfg_index],
                                    &owned_cfg) ||
                    !arena_arr_push(builder->arena, test.configurations, owned_cfg)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "failed to append test configuration",
                                            "increase arena capacity");
                }
            }
            if (!bm_copy_string(builder->arena, ev->as.test_add.name, &test.name) ||
                !bm_copy_string(builder->arena, ev->as.test_add.command, &test.command) ||
                !bm_copy_string(builder->arena, ev->as.test_add.working_dir, &test.working_dir) ||
                !arena_arr_push(builder->arena, draft->tests, test) ||
                !bm_add_name_index(builder->arena, &draft->test_name_index, test.name, test.id)) {
                return bm_builder_error(builder, ev, "failed to append test record", "increase arena capacity");
            }
            draft->tests[test.id].resolved_command_target_id =
                bm_draft_resolve_test_command_target(draft, &draft->tests[test.id]);
            return true;
        }

        case EVENT_TEST_PROPERTY_MUTATE: {
            BM_Directory_Id directory_id =
                bm_draft_find_directory_by_source(draft, ev->as.test_property_mutate.directory);
            BM_Test_Record *test = bm_draft_find_test_in_directory(
                draft, ev->as.test_property_mutate.test_name, directory_id);
            BM_Test_Property_Kind kind = BM_TEST_PROPERTY_DISABLED;
            bool known_property = false;
            if (!test) {
                return bm_builder_error(builder,
                                        ev,
                                        "test property mutation for an unknown test",
                                        "emit test declarations before test property mutations in the selected directory");
            }
            known_property =
                bm_test_property_kind_from_name(ev->as.test_property_mutate.property_name, &kind);
            if (known_property) {
                BM_Test_Property_Record *record = bm_test_property_ensure(builder, test, kind, ev);
                String_View *items = NULL;
                if (!record ||
                    !bm_test_copy_items(builder,
                                        ev,
                                        ev->as.test_property_mutate.items,
                                        ev->as.test_property_mutate.item_count,
                                        &items) ||
                    !bm_apply_string_mutation(builder->arena,
                                              &record->items,
                                              items,
                                              arena_arr_len(items),
                                              ev->as.test_property_mutate.op)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "failed to mutate typed test property",
                                            "increase arena capacity");
                }
                record->flags = ev->as.test_property_mutate.flags;
                record->provenance = bm_provenance_from_event(builder->arena, ev);
            } else if (!bm_record_raw_property(builder->arena,
                                               &test->raw_properties,
                                               ev->as.test_property_mutate.property_name,
                                               ev->as.test_property_mutate.op,
                                               ev->as.test_property_mutate.flags,
                                               ev->as.test_property_mutate.items,
                                               ev->as.test_property_mutate.item_count,
                                               bm_provenance_from_event(builder->arena, ev))) {
                return bm_builder_error(builder,
                                        ev,
                                        "failed to append test property",
                                        "increase arena capacity");
            }
            if (bm_test_sv_eq_ci(ev->as.test_property_mutate.property_name,
                                 nob_sv_from_cstr("WORKING_DIRECTORY"))) {
                String_View joined = {0};
                if (!bm_test_join_items(builder->arena,
                                        ev->as.test_property_mutate.items,
                                        ev->as.test_property_mutate.item_count,
                                        &joined) ||
                    !bm_copy_string(builder->arena, joined, &test->working_dir)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "failed to update test working directory",
                                            "increase arena capacity");
                }
            }
            return true;
        }

        default:
            return bm_builder_error(builder, ev, "unexpected test handler event", "fix build model test dispatch");
    }
}
