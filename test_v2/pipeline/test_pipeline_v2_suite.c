#include "test_v2_assert.h"
#include "test_case_pack.h"
#include "test_semantic_pipeline.h"
#include "test_snapshot_support.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_query.h"
#include "build_model_validate.h"

#include <stdlib.h>
#include <string.h>

typedef Test_Case_Pack_Entry Pipeline_Case;
typedef Pipeline_Case *Pipeline_Case_List;

static const char *pipeline_target_type_name(BM_Target_Kind type) {
    switch (type) {
        case BM_TARGET_EXECUTABLE: return "EXECUTABLE";
        case BM_TARGET_STATIC_LIBRARY: return "STATIC_LIB";
        case BM_TARGET_SHARED_LIBRARY: return "SHARED_LIB";
        case BM_TARGET_MODULE_LIBRARY: return "MODULE_LIB";
        case BM_TARGET_INTERFACE_LIBRARY: return "INTERFACE_LIB";
        case BM_TARGET_OBJECT_LIBRARY: return "OBJECT_LIB";
        case BM_TARGET_UTILITY: return "UTILITY";
    }
    return "UNKNOWN";
}

static const char *pipeline_build_step_kind_name(BM_Build_Step_Kind kind) {
    switch (kind) {
        case BM_BUILD_STEP_OUTPUT_RULE: return "OUTPUT_RULE";
        case BM_BUILD_STEP_CUSTOM_TARGET: return "CUSTOM_TARGET";
        case BM_BUILD_STEP_TARGET_PRE_BUILD: return "TARGET_PRE_BUILD";
        case BM_BUILD_STEP_TARGET_PRE_LINK: return "TARGET_PRE_LINK";
        case BM_BUILD_STEP_TARGET_POST_BUILD: return "TARGET_POST_BUILD";
    }
    return "UNKNOWN";
}

static const char *pipeline_replay_action_kind_name(BM_Replay_Action_Kind kind) {
    switch (kind) {
        case BM_REPLAY_ACTION_FILESYSTEM: return "FILESYSTEM";
        case BM_REPLAY_ACTION_PROCESS: return "PROCESS";
        case BM_REPLAY_ACTION_PROBE: return "PROBE";
        case BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION: return "DEPENDENCY_MATERIALIZATION";
        case BM_REPLAY_ACTION_TEST_DRIVER: return "TEST_DRIVER";
        case BM_REPLAY_ACTION_HOST_EFFECT: return "HOST_EFFECT";
    }
    return "UNKNOWN";
}

static const char *pipeline_replay_phase_name(BM_Replay_Phase phase) {
    switch (phase) {
        case BM_REPLAY_PHASE_CONFIGURE: return "CONFIGURE";
        case BM_REPLAY_PHASE_BUILD: return "BUILD";
        case BM_REPLAY_PHASE_TEST: return "TEST";
        case BM_REPLAY_PHASE_INSTALL: return "INSTALL";
        case BM_REPLAY_PHASE_EXPORT: return "EXPORT";
        case BM_REPLAY_PHASE_PACKAGE: return "PACKAGE";
        case BM_REPLAY_PHASE_HOST_ONLY: return "HOST_ONLY";
    }
    return "UNKNOWN";
}

static const char *pipeline_replay_opcode_name(BM_Replay_Opcode opcode) {
    switch (opcode) {
        case BM_REPLAY_OPCODE_NONE: return "none";
        case BM_REPLAY_OPCODE_FS_MKDIR: return "fs_mkdir";
        case BM_REPLAY_OPCODE_FS_WRITE_TEXT: return "fs_write_text";
        case BM_REPLAY_OPCODE_FS_APPEND_TEXT: return "fs_append_text";
        case BM_REPLAY_OPCODE_FS_COPY_FILE: return "fs_copy_file";
        case BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: return "host_download_local";
        case BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR: return "host_archive_create_paxr";
        case BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR: return "host_archive_extract_tar";
        case BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE: return "host_lock_acquire";
        case BM_REPLAY_OPCODE_HOST_LOCK_RELEASE: return "host_lock_release";
        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE: return "probe_try_compile_source";
        case BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT: return "probe_try_compile_project";
        case BM_REPLAY_OPCODE_PROBE_TRY_RUN: return "probe_try_run";
        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR: return "deps_fetchcontent_source_dir";
        case BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE: return "deps_fetchcontent_local_archive";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY: return "test_driver_ctest_empty_binary_directory";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL: return "test_driver_ctest_start_local";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF: return "test_driver_ctest_configure_self";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF: return "test_driver_ctest_build_self";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST: return "test_driver_ctest_test";
        case BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP: return "test_driver_ctest_sleep";
    }
    return "unknown";
}

static const char *pipeline_export_kind_name(BM_Export_Kind kind) {
    switch (kind) {
        case BM_EXPORT_INSTALL: return "INSTALL";
        case BM_EXPORT_BUILD_TREE: return "BUILD_TREE";
        case BM_EXPORT_PACKAGE_REGISTRY: return "PACKAGE_REGISTRY";
    }
    return "UNKNOWN";
}

static const char *pipeline_export_source_kind_name(BM_Export_Source_Kind kind) {
    switch (kind) {
        case BM_EXPORT_SOURCE_INSTALL_EXPORT: return "INSTALL_EXPORT";
        case BM_EXPORT_SOURCE_TARGETS: return "TARGETS";
        case BM_EXPORT_SOURCE_EXPORT_SET: return "EXPORT_SET";
        case BM_EXPORT_SOURCE_PACKAGE: return "PACKAGE";
    }
    return "UNKNOWN";
}

static size_t pipeline_count_non_private_items(BM_String_Item_Span items) {
    size_t count = 0;
    for (size_t i = 0; i < items.count; ++i) {
        if (items.items[i].visibility != BM_VISIBILITY_PRIVATE) count++;
    }
    return count;
}

static bool pipeline_sv_contains(String_View haystack, String_View needle) {
    if (needle.count == 0 || haystack.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= haystack.count; ++i) {
        if (memcmp(haystack.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static void pipeline_init_event(Event *ev, Event_Kind kind, size_t line) {
    if (!ev) return;
    *ev = (Event){0};
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr("graph_src/CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static String_View pipeline_stable_path_in_workspace(Arena *arena, String_View path) {
    const char *cwd = NULL;
    size_t cwd_len = 0;
    (void)arena;
    if (path.count == 0) return path;
    cwd = nob_get_current_dir_temp();
    if (!cwd || cwd[0] == '\0') return path;
    cwd_len = strlen(cwd);
    if (path.count < cwd_len || memcmp(path.data, cwd, cwd_len) != 0) return path;
    if (path.count == cwd_len) return nob_sv_from_cstr(".");
    if (path.data[cwd_len] != '/' && path.data[cwd_len] != '\\') return path;
    return nob_sv_from_parts(path.data + cwd_len + 1, path.count - cwd_len - 1);
}

static void append_model_snapshot(Nob_String_Builder *sb, const Build_Model *model) {
    size_t target_count = bm_query_target_count(model);
    size_t package_count = bm_query_package_count(model);
    size_t test_count = bm_query_test_count(model);
    size_t install_rule_count = bm_query_install_rule_count(model);
    size_t cpack_group_count = bm_query_cpack_component_group_count(model);
    size_t cpack_type_count = bm_query_cpack_install_type_count(model);
    size_t cpack_component_count = bm_query_cpack_component_count(model);
    BM_Directory_Id root_directory = bm_query_root_directory(model);
    BM_String_Item_Span root_include_dirs = bm_query_directory_include_directories_raw(model, root_directory);
    BM_String_Item_Span root_system_include_dirs = bm_query_directory_system_include_directories_raw(model, root_directory);
    BM_String_Item_Span root_link_dirs = bm_query_directory_link_directories_raw(model, root_directory);
    BM_String_Item_Span global_compile_defs = bm_query_global_compile_definitions_raw(model);
    BM_String_Item_Span global_compile_opts = bm_query_global_compile_options_raw(model);
    BM_String_Item_Span global_link_opts = bm_query_global_link_options_raw(model);
    BM_String_Span global_link_libs = bm_query_global_raw_property_items(model, nob_sv_from_cstr("LINK_LIBRARIES"));

    nob_sb_append_cstr(sb, "MODEL project=");
    test_snapshot_append_escaped_sv(sb, bm_query_project_name(model));
    nob_sb_append_cstr(sb, nob_temp_sprintf(
        " targets=%zu packages=%zu tests=%zu install_enabled=%d testing_enabled=%d cpack_groups=%zu cpack_types=%zu cpack_components=%zu\n",
        target_count,
        package_count,
        test_count,
        install_rule_count > 0 ? 1 : 0,
        bm_query_testing_enabled(model) ? 1 : 0,
        cpack_group_count,
        cpack_type_count,
        cpack_component_count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "DIR include=%zu system_include=%zu link=%zu\n",
        root_include_dirs.count,
        root_system_include_dirs.count,
        root_link_dirs.count));

    nob_sb_append_cstr(sb, nob_temp_sprintf(
        "GLOBAL compile_defs=%zu compile_opts=%zu link_opts=%zu link_libs=%zu\n",
        global_compile_defs.count,
        global_compile_opts.count,
        global_link_opts.count,
        global_link_libs.count));

    if (target_count > 0) {
        BM_Target_Id target_id = 0;
        BM_String_Span sources = bm_query_target_sources_raw(model, target_id);
        BM_Target_Id_Span deps = bm_query_target_dependencies_explicit(model, target_id);
        BM_String_Item_Span link_libs = bm_query_target_link_libraries_raw(model, target_id);
        BM_String_Item_Span link_opts = bm_query_target_link_options_raw(model, target_id);
        BM_String_Item_Span link_dirs = bm_query_target_link_directories_raw(model, target_id);

        nob_sb_append_cstr(sb, "TARGET0 name=");
        test_snapshot_append_escaped_sv(sb, bm_query_target_name(model, target_id));
        nob_sb_append_cstr(sb, nob_temp_sprintf(
            " type=%s sources=%zu deps=%zu link_libs=%zu interface_libs=%zu link_opts=%zu link_dirs=%zu\n",
            pipeline_target_type_name(bm_query_target_kind(model, target_id)),
            sources.count,
            deps.count,
            link_libs.count,
            pipeline_count_non_private_items(link_libs),
            link_opts.count,
            link_dirs.count));
    }
}

static bool pipeline_snapshot_from_script(const char *script,
                                          const char *current_file,
                                          Nob_String_Builder *out_sb) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    bool ok = false;

    if (!out_sb) return false;

    test_semantic_pipeline_config_init(&config);
    config.current_file = current_file ? current_file : "CMakeLists.txt";

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &config);
    if (!ok) return false;

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVAL_OK %d\n", fixture.eval_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("BUILDER_OK %d\n", fixture.build.builder_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("FREEZE_OK %d\n", fixture.build.freeze_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", fixture.stream ? fixture.stream->count : 0));
    if (fixture.build.freeze_ok && fixture.build.model) {
        append_model_snapshot(out_sb, fixture.build.model);
    }

    test_semantic_pipeline_fixture_destroy(&fixture);
    return true;
}

static void append_pipeline_build_graph_snapshot(Nob_String_Builder *sb, const Build_Model *model) {
    size_t build_step_count = bm_query_build_step_count(model);
    size_t replay_action_count = bm_query_replay_action_count(model);
    size_t test_count = bm_query_test_count(model);
    size_t target_count = bm_query_target_count(model);
    size_t export_count = bm_query_export_count(model);
    size_t cpack_package_count = bm_query_cpack_package_count(model);
    Arena *scratch = arena_create(64 * 1024);

    nob_sb_append_cstr(sb, nob_temp_sprintf("BUILD_STEPS count=%zu\n", build_step_count));
    for (size_t i = 0; i < build_step_count; ++i) {
        BM_Build_Step_Id step_id = (BM_Build_Step_Id)i;
        BM_Target_Id owner_target = bm_query_build_step_owner_target(model, step_id);
        nob_sb_append_cstr(sb, nob_temp_sprintf("STEP[%zu] kind=%s owner_target=", i,
                                                pipeline_build_step_kind_name(
                                                    bm_query_build_step_kind(model, step_id))));
        test_snapshot_append_escaped_sv(sb,
                                        bm_target_id_is_valid(owner_target)
                                            ? bm_query_target_name(model, owner_target)
                                            : nob_sv_from_cstr(""));
        nob_sb_append_cstr(sb, nob_temp_sprintf(
            " outputs=%zu byproducts=%zu target_deps=%zu producer_deps=%zu file_deps=%zu commands=%zu\n",
            bm_query_build_step_outputs(model, step_id).count,
            bm_query_build_step_byproducts(model, step_id).count,
            bm_query_build_step_target_dependencies(model, step_id).count,
            bm_query_build_step_producer_dependencies(model, step_id).count,
            bm_query_build_step_file_dependencies(model, step_id).count,
            bm_query_build_step_command_count(model, step_id)));
    }

    nob_sb_append_cstr(sb, nob_temp_sprintf("REPLAY_ACTIONS count=%zu\n", replay_action_count));
    for (size_t i = 0; i < replay_action_count; ++i) {
        BM_Replay_Action_Id action_id = (BM_Replay_Action_Id)i;
        nob_sb_append_cstr(sb, nob_temp_sprintf("REPLAY[%zu] kind=%s opcode=%s phase=%s owner_dir=%u inputs=%zu outputs=%zu argv=%zu env=%zu working_dir=",
                                                i,
                                                pipeline_replay_action_kind_name(
                                                    bm_query_replay_action_kind(model, action_id)),
                                                pipeline_replay_opcode_name(
                                                    bm_query_replay_action_opcode(model, action_id)),
                                                pipeline_replay_phase_name(
                                                    bm_query_replay_action_phase(model, action_id)),
                                                (unsigned)bm_query_replay_action_owner_directory(model, action_id),
                                                bm_query_replay_action_inputs(model, action_id).count,
                                                bm_query_replay_action_outputs(model, action_id).count,
                                                bm_query_replay_action_argv(model, action_id).count,
                                                bm_query_replay_action_environment(model, action_id).count));
        test_snapshot_append_escaped_sv(sb, bm_query_replay_action_working_directory(model, action_id));
        nob_sb_append_cstr(sb, "\n");
    }

    nob_sb_append_cstr(sb, nob_temp_sprintf("TESTS count=%zu\n", test_count));
    for (size_t i = 0; i < test_count; ++i) {
        BM_Test_Id test_id = (BM_Test_Id)i;
        BM_String_Span configs = bm_query_test_configurations(model, test_id);
        nob_sb_append_cstr(sb, nob_temp_sprintf("TEST[%zu] name=", i));
        test_snapshot_append_escaped_sv(sb, bm_query_test_name(model, test_id));
        nob_sb_append_cstr(sb, " owner_dir=");
        nob_sb_append_cstr(sb, nob_temp_sprintf("%u", (unsigned)bm_query_test_owner_directory(model, test_id)));
        nob_sb_append_cstr(sb, " working_dir=");
        test_snapshot_append_escaped_sv(sb, bm_query_test_working_directory(model, test_id));
        nob_sb_append_cstr(sb, nob_temp_sprintf(" expand_lists=%d configs=%zu command=",
                                                bm_query_test_command_expand_lists(model, test_id) ? 1 : 0,
                                                configs.count));
        test_snapshot_append_escaped_sv(sb, bm_query_test_command(model, test_id));
        nob_sb_append_cstr(sb, "\n");
    }

    nob_sb_append_cstr(sb, nob_temp_sprintf("EXPORTS count=%zu\n", export_count));
    for (size_t i = 0; i < export_count; ++i) {
        BM_Export_Id export_id = (BM_Export_Id)i;
        BM_Target_Id_Span targets = bm_query_export_targets(model, export_id);
        nob_sb_append_cstr(sb, nob_temp_sprintf("EXPORT[%zu] kind=%s source=%s name=",
                                                i,
                                                pipeline_export_kind_name(bm_query_export_kind(model, export_id)),
                                                pipeline_export_source_kind_name(
                                                    bm_query_export_source_kind(model, export_id))));
        test_snapshot_append_escaped_sv(sb, bm_query_export_name(model, export_id));
        nob_sb_append_cstr(sb, " namespace=");
        test_snapshot_append_escaped_sv(sb, bm_query_export_namespace(model, export_id));
        nob_sb_append_cstr(sb, " output=");
        test_snapshot_append_escaped_sv(sb,
                                        scratch
                                            ? pipeline_stable_path_in_workspace(
                                                  scratch,
                                                  bm_query_export_output_file_path(model, export_id, scratch))
                                            : nob_sv_from_cstr(""));
        nob_sb_append_cstr(sb, " registry_prefix=");
        test_snapshot_append_escaped_sv(sb, bm_query_export_registry_prefix(model, export_id));
        nob_sb_append_cstr(sb, nob_temp_sprintf(" enabled=%d targets=%zu\n",
                                                bm_query_export_enabled(model, export_id) ? 1 : 0,
                                                targets.count));
    }

    nob_sb_append_cstr(sb, nob_temp_sprintf("CPACK_PACKAGES count=%zu\n", cpack_package_count));
    for (size_t i = 0; i < cpack_package_count; ++i) {
        BM_CPack_Package_Id package_id = (BM_CPack_Package_Id)i;
        BM_String_Span generators = bm_query_cpack_package_generators(model, package_id);
        BM_String_Span components = bm_query_cpack_package_components_all(model, package_id);
        nob_sb_append_cstr(sb, nob_temp_sprintf("CPACK_PACKAGE[%zu] name=", i));
        test_snapshot_append_escaped_sv(sb, bm_query_cpack_package_name(model, package_id));
        nob_sb_append_cstr(sb, " version=");
        test_snapshot_append_escaped_sv(sb, bm_query_cpack_package_version(model, package_id));
        nob_sb_append_cstr(sb, " file=");
        test_snapshot_append_escaped_sv(sb, bm_query_cpack_package_file_name(model, package_id));
        nob_sb_append_cstr(sb, " output=");
        test_snapshot_append_escaped_sv(sb,
                                        scratch
                                            ? pipeline_stable_path_in_workspace(
                                                  scratch,
                                                  bm_query_cpack_package_output_directory(model, package_id, scratch))
                                            : nob_sv_from_cstr(""));
        nob_sb_append_cstr(sb, nob_temp_sprintf(" include_toplevel=%d archive_component_install=%d generators=%zu components=%zu\n",
                                                bm_query_cpack_package_include_toplevel_directory(model, package_id) ? 1 : 0,
                                                bm_query_cpack_package_archive_component_install(model, package_id) ? 1 : 0,
                                                generators.count,
                                                components.count));
        for (size_t generator_index = 0; generator_index < generators.count; ++generator_index) {
            nob_sb_append_cstr(sb, "GENERATOR ");
            test_snapshot_append_escaped_sv(sb, generators.items[generator_index]);
            nob_sb_append_cstr(sb, "\n");
        }
        for (size_t component_index = 0; component_index < components.count; ++component_index) {
            nob_sb_append_cstr(sb, "COMPONENT ");
            test_snapshot_append_escaped_sv(sb, components.items[component_index]);
            nob_sb_append_cstr(sb, "\n");
        }
    }

    for (size_t target_index = 0; target_index < target_count; ++target_index) {
        BM_Target_Id target_id = (BM_Target_Id)target_index;
        size_t source_count = bm_query_target_source_count(model, target_id);
        nob_sb_append_cstr(sb, "TARGET name=");
        test_snapshot_append_escaped_sv(sb, bm_query_target_name(model, target_id));
        nob_sb_append_cstr(sb, nob_temp_sprintf(" source_count=%zu\n", source_count));
        for (size_t source_index = 0; source_index < source_count; ++source_index) {
            BM_Build_Step_Id producer = bm_query_target_source_producer_step(model, target_id, source_index);
            nob_sb_append_cstr(sb, nob_temp_sprintf("SOURCE[%zu] generated=%d producer=",
                                                    source_index,
                                                    bm_query_target_source_generated(model, target_id, source_index) ? 1 : 0));
            if (bm_build_step_id_is_valid(producer)) {
                nob_sb_append_cstr(sb, nob_temp_sprintf("%u", (unsigned)producer));
            } else {
                nob_sb_append_cstr(sb, "INVALID");
            }
            nob_sb_append_cstr(sb, " effective=");
            test_snapshot_append_escaped_sv(sb,
                                            bm_query_target_source_effective(model,
                                                                             target_id,
                                                                             source_index));
            nob_sb_append_cstr(sb, "\n");
        }
    }

    if (scratch) arena_destroy(scratch);
}

static bool pipeline_build_graph_snapshot_from_script(const char *script,
                                                      Nob_String_Builder *out_sb) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    bool ok = false;

    if (!out_sb) return false;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "graph_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("graph_src");
    config.binary_dir = nob_sv_from_cstr("graph_build");

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &config);
    if (!ok) return false;

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVAL_OK %d\n", fixture.eval_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("BUILDER_OK %d\n", fixture.build.builder_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("FREEZE_OK %d\n", fixture.build.freeze_ok ? 1 : 0));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", fixture.stream ? fixture.stream->count : 0));
    if (fixture.build.freeze_ok && fixture.build.model) {
        append_pipeline_build_graph_snapshot(out_sb, fixture.build.model);
    }

    test_semantic_pipeline_fixture_destroy(&fixture);
    return true;
}

static bool render_pipeline_case_snapshot_to_sb(Arena *arena,
                                                Pipeline_Case pipeline_case,
                                                Nob_String_Builder *out_sb) {
    diag_reset();
    (void)arena;

    Nob_String_Builder case_sb = {0};
    bool ok = pipeline_snapshot_from_script(pipeline_case.script.data, "CMakeLists.txt", &case_sb);
    if (!ok || case_sb.count == 0) {
        nob_sb_free(case_sb);
        return false;
    }

    nob_sb_append_buf(out_sb, case_sb.items, case_sb.count);
    nob_sb_free(case_sb);
    return true;
}

static bool render_pipeline_casepack_snapshot_to_arena(Arena *arena,
                                                       Pipeline_Case_List cases,
                                                       String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE pipeline\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", arena_arr_len(cases)));

    for (size_t i = 0; i < arena_arr_len(cases); i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases[i].name.data, cases[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_pipeline_case_snapshot_to_sb(arena, cases[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < arena_arr_len(cases)) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_pipeline_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(8 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View actual = {0};
    bool ok = true;

    if (!test_snapshot_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Pipeline_Case_List cases = NULL;
    if (!test_snapshot_parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }

    if (!render_pipeline_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render pipeline snapshot");
        ok = false;
        goto done;
    }

    if (!test_snapshot_assert_golden_output(arena, input_path, expected_path, actual, NULL, NULL)) {
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static bool assert_pipeline_build_graph_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(8 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View actual = {0};
    bool ok = true;

    if (!test_snapshot_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Pipeline_Case_List cases = NULL;
    if (!test_snapshot_parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE pipeline_build_graph\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", arena_arr_len(cases)));
    for (size_t i = 0; i < arena_arr_len(cases); ++i) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases[i].name.data, cases[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");
        if (!pipeline_build_graph_snapshot_from_script(cases[i].script.data, &sb)) {
            nob_sb_free(sb);
            ok = false;
            goto done;
        }
        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < arena_arr_len(cases)) nob_sb_append_cstr(&sb, "\n");
    }

    {
        size_t len = sb.count;
        char *text = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
        nob_sb_free(sb);
        if (!text) {
            ok = false;
            goto done;
        }
        actual = nob_sv_from_parts(text, len);
    }

    if (!test_snapshot_assert_golden_output(arena, input_path, expected_path, actual, NULL, NULL)) {
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *PIPELINE_GOLDEN_DIR = "test_v2/pipeline/golden";

TEST(pipeline_golden_all_cases) {
    ASSERT(assert_pipeline_golden_casepack(
        nob_temp_sprintf("%s/pipeline_all.cmake", PIPELINE_GOLDEN_DIR),
        nob_temp_sprintf("%s/pipeline_all.txt", PIPELINE_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(pipeline_golden_build_graph_cases) {
    ASSERT(assert_pipeline_build_graph_golden_casepack(
        nob_temp_sprintf("%s/pipeline_build_graph.cmake", PIPELINE_GOLDEN_DIR),
        nob_temp_sprintf("%s/pipeline_build_graph.txt", PIPELINE_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(pipeline_build_graph_snapshot_surfaces_replay_actions) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    Nob_String_Builder sb = {0};

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    pipeline_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("graph_src");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("graph_build");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_TEST_ENABLE, 2);
    ev.as.test_enable.enabled = true;
    ASSERT(event_stream_push(stream, &ev));

    {
        String_View configs[] = {nob_sv_from_cstr("Debug")};
        pipeline_init_event(&ev, EVENT_TEST_ADD, 3);
        ev.as.test_add.name = nob_sv_from_cstr("smoke");
        ev.as.test_add.command = nob_sv_from_cstr("app");
        ev.as.test_add.working_dir = nob_sv_from_cstr("graph_build/tests");
        ev.as.test_add.command_expand_lists = true;
        ev.as.test_add.configurations = configs;
        ev.as.test_add.configuration_count = NOB_ARRAY_LEN(configs);
        ASSERT(event_stream_push(stream, &ev));
    }

    pipeline_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 4);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("snapshot_ctest");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("graph_build");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 5);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("snapshot_ctest");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("graph_build");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("snapshot_ctest");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("reports/junit.xml");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 7);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("snapshot_ctest");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    pipeline_init_event(&ev, EVENT_DIRECTORY_LEAVE, 8);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("graph_src");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("graph_build");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    append_pipeline_build_graph_snapshot(&sb, build.model);
    ASSERT(sb.items != NULL);
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("BUILD_STEPS count=0")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("REPLAY_ACTIONS count=1")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("REPLAY[0] kind=TEST_DRIVER opcode=test_driver_ctest_test phase=TEST")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("inputs=0 outputs=1 argv=2 env=0")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("TESTS count=1")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("TEST[0] name='smoke'")));
    ASSERT(pipeline_sv_contains(nob_sv_from_parts(sb.items, sb.count), nob_sv_from_cstr("expand_lists=1 configs=1 command='app'")));

    nob_sb_free(sb);
    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

void run_pipeline_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "pipeline");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "pipeline suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "pipeline suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_pipeline_golden_all_cases(passed, failed, skipped);
    test_pipeline_golden_build_graph_cases(passed, failed, skipped);
    test_pipeline_build_graph_snapshot_surfaces_replay_actions(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "pipeline suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
