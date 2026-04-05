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
                                  bool is_before,
                                  bool is_system) {
    BM_String_Item_View item = {0};
    if (!bm_copy_string(builder->arena, value, &item.value)) {
        return bm_builder_error(builder, ev, "failed to copy target property item", "increase arena capacity");
    }
    item.visibility = bm_visibility_from_event(visibility);
    item.flags = BM_ITEM_FLAG_NONE;
    if (is_before) item.flags |= BM_ITEM_FLAG_BEFORE;
    if (is_system) item.flags |= BM_ITEM_FLAG_SYSTEM;
    item.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!bm_append_item(builder->arena, dest, item)) {
        return bm_builder_error(builder, ev, "failed to append target property item", "increase arena capacity");
    }
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
                                           const Event *ev,
                                           String_View raw_path) {
    BM_Target_Source_Record record = {0};
    if (!builder || !target || !ev) return false;
    if (!bm_copy_string(builder->arena, raw_path, &record.raw_path)) {
        return bm_builder_error(builder, ev, "failed to copy target source path", "increase arena capacity");
    }
    record.producer_step_id = BM_BUILD_STEP_ID_INVALID;
    record.provenance = bm_provenance_from_event(builder->arena, ev);
    if (!arena_arr_push(builder->arena, target->source_records, record)) {
        return bm_builder_error(builder, ev, "failed to append target source record", "increase arena capacity");
    }
    return true;
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

bool bm_builder_handle_target_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_TARGET_DECLARE: {
            BM_Directory_Id current_directory_id = bm_builder_current_directory_id(builder);
            BM_Target_Record target = {0};
            if (current_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder, ev, "target declaration without an active directory", "emit directory enter before declaring targets");
            }
            if (bm_draft_find_target_const(draft, ev->as.target_declare.name)) {
                return bm_builder_error(builder, ev, "duplicate target name", "ensure target names are unique");
            }

            target.id = (BM_Target_Id)arena_arr_len(draft->targets);
            target.owner_directory_id = current_directory_id;
            target.provenance = bm_provenance_from_event(builder->arena, ev);
            target.kind = bm_target_kind_from_event(ev->as.target_declare.target_type);
            target.imported = ev->as.target_declare.imported;
            target.alias = ev->as.target_declare.alias;
            target.alias_of_id = BM_TARGET_ID_INVALID;

            if (!bm_copy_string(builder->arena, ev->as.target_declare.name, &target.name) ||
                !bm_copy_string(builder->arena, ev->as.target_declare.alias_of, &target.alias_of_name) ||
                !arena_arr_push(builder->arena, draft->targets, target) ||
                !bm_add_name_index(builder->arena, &draft->target_name_index, target.name, target.id)) {
                return bm_builder_error(builder, ev, "failed to append target", "increase arena capacity");
            }
            return true;
        }

        case EVENT_TARGET_ADD_SOURCE: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_add_source.target_name);
            String_View owned = {0};
            if (!target) return bm_builder_error(builder, ev, "target source references an unknown target", "declare the target before adding sources");
            if (!bm_copy_string(builder->arena, ev->as.target_add_source.path, &owned) ||
                !arena_arr_push(builder->arena, target->sources, owned)) {
                return bm_builder_error(builder, ev, "failed to append target source", "increase arena capacity");
            }
            if (!bm_target_append_source_record(builder, target, ev, ev->as.target_add_source.path)) return false;
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
                                         false,
                                         false);
        }

        case EVENT_TARGET_LINK_OPTIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_link_options.target_name);
            if (!target) return bm_builder_error(builder, ev, "target link options references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->link_options,
                                         ev->as.target_link_options.item,
                                         ev->as.target_link_options.visibility,
                                         ev->as.target_link_options.is_before,
                                         false);
        }

        case EVENT_TARGET_LINK_DIRECTORIES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_link_directories.target_name);
            if (!target) return bm_builder_error(builder, ev, "target link directories references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->link_directories,
                                         ev->as.target_link_directories.path,
                                         ev->as.target_link_directories.visibility,
                                         false,
                                         false);
        }

        case EVENT_TARGET_INCLUDE_DIRECTORIES: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_include_directories.target_name);
            if (!target) return bm_builder_error(builder, ev, "target include directories references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->include_directories,
                                         ev->as.target_include_directories.path,
                                         ev->as.target_include_directories.visibility,
                                         ev->as.target_include_directories.is_before,
                                         ev->as.target_include_directories.is_system);
        }

        case EVENT_TARGET_COMPILE_DEFINITIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_compile_definitions.target_name);
            if (!target) return bm_builder_error(builder, ev, "target compile definitions references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->compile_definitions,
                                         ev->as.target_compile_definitions.item,
                                         ev->as.target_compile_definitions.visibility,
                                         false,
                                         false);
        }

        case EVENT_TARGET_COMPILE_OPTIONS: {
            BM_Target_Record *target = bm_draft_find_target(draft, ev->as.target_compile_options.target_name);
            if (!target) return bm_builder_error(builder, ev, "target compile options references an unknown target", "declare the target first");
            return bm_target_append_item(builder,
                                         ev,
                                         &target->compile_options,
                                         ev->as.target_compile_options.item,
                                         ev->as.target_compile_options.visibility,
                                         ev->as.target_compile_options.is_before,
                                         false);
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
