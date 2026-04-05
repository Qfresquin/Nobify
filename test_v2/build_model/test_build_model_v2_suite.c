#include "test_v2_assert.h"
#include "test_fs.h"
#include "test_semantic_pipeline.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "build_model_query.h"

static bool build_model_mkdirs(const char *path) {
    char buf[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && !nob_mkdir_if_not_exists(buf)) return false;
        buf[i] = '/';
    }

    return nob_mkdir_if_not_exists(buf);
}

static bool build_model_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !build_model_mkdirs(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

static bool build_model_sv_contains(String_View haystack, String_View needle) {
    if (needle.count == 0 || haystack.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= haystack.count; ++i) {
        if (memcmp(haystack.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static void build_model_init_event(Event *ev, Event_Kind kind, size_t line) {
    *ev = (Event){0};
    ev->h.kind = kind;
    ev->h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev->h.origin.line = line;
    ev->h.origin.col = 1;
}

static BM_Directory_Id build_model_find_directory_id(const Build_Model *model,
                                                     String_View source_dir,
                                                     String_View binary_dir) {
    size_t count = bm_query_directory_count(model);
    for (size_t i = 0; i < count; ++i) {
        BM_Directory_Id id = (BM_Directory_Id)i;
        if (nob_sv_eq(bm_query_directory_source_dir(model, id), source_dir) &&
            nob_sv_eq(bm_query_directory_binary_dir(model, id), binary_dir)) {
            return id;
        }
    }
    return BM_DIRECTORY_ID_INVALID;
}

static BM_Build_Step_Id build_model_find_step_by_kind_and_owner(const Build_Model *model,
                                                                BM_Build_Step_Kind kind,
                                                                BM_Target_Id owner_target_id) {
    size_t count = bm_query_build_step_count(model);
    for (size_t i = 0; i < count; ++i) {
        BM_Build_Step_Id id = (BM_Build_Step_Id)i;
        if (bm_query_build_step_kind(model, id) != kind) continue;
        if (bm_query_build_step_owner_target(model, id) != owner_target_id) continue;
        return id;
    }
    return BM_BUILD_STEP_ID_INVALID;
}

static size_t build_model_find_target_source_index_containing(const Build_Model *model,
                                                              BM_Target_Id target_id,
                                                              const char *needle) {
    size_t count = bm_query_target_source_count(model, target_id);
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < count; ++i) {
        if (build_model_sv_contains(bm_query_target_source_effective(model, target_id, i), needle_sv)) {
            return i;
        }
    }
    return count;
}

TEST(build_model_builder_directory_scope_events) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    Event ev = {0};
    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 2);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_PROPERTY_MUTATE, 3);
    ev.as.directory_property_mutate.property_name = nob_sv_from_cstr("INCLUDE_DIRECTORIES");
    ev.as.directory_property_mutate.op = EVENT_PROPERTY_MUTATE_APPEND_LIST;
    ev.as.directory_property_mutate.modifier_flags = EVENT_PROPERTY_MODIFIER_NONE;
    String_View include_items[] = {nob_sv_from_cstr("sub/include")};
    ev.as.directory_property_mutate.items = include_items;
    ev.as.directory_property_mutate.item_count = NOB_ARRAY_LEN(include_items);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 4);
    ev.as.target_declare.name = nob_sv_from_cstr("sub_lib");
    ev.as.target_declare.target_type = EV_TARGET_LIBRARY_STATIC;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 5);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 6);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.model != NULL);
    const Build_Model *model = build.model;
    ASSERT(bm_query_directory_count(model) == 2);

    BM_Directory_Id sub_dir = build_model_find_directory_id(model,
                                                            nob_sv_from_cstr("sub"),
                                                            nob_sv_from_cstr("sub-build"));
    ASSERT(sub_dir != BM_DIRECTORY_ID_INVALID);

    BM_String_Item_Span sub_includes = bm_query_directory_include_directories_raw(model, sub_dir);
    ASSERT(sub_includes.count == 1);
    ASSERT(nob_sv_eq(sub_includes.items[0].value, nob_sv_from_cstr("sub/include")));

    BM_Target_Id target_id = bm_query_target_by_name(model, nob_sv_from_cstr("sub_lib"));
    ASSERT(target_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_owner_directory(model, target_id) == sub_dir);
    ASSERT(bm_query_target_kind(model, target_id) == BM_TARGET_STATIC_LIBRARY);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_validate_does_not_infer_link_library_targets) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    Event ev = {0};
    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 2);
    ev.as.target_declare.name = nob_sv_from_cstr("app");
    ev.as.target_declare.target_type = EV_TARGET_EXECUTABLE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_LINK_LIBRARIES, 3);
    ev.as.target_link_libraries.target_name = nob_sv_from_cstr("app");
    ev.as.target_link_libraries.visibility = EV_VISIBILITY_PRIVATE;
    ev.as.target_link_libraries.item = nob_sv_from_cstr("MissingTargetLikeName");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 4);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.model != NULL);
    const Build_Model *model = build.model;

    BM_Target_Id app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    BM_Target_Id_Span explicit_deps = bm_query_target_dependencies_explicit(model, app_id);
    BM_String_Item_Span raw_link_libs = bm_query_target_link_libraries_raw(model, app_id);
    ASSERT(explicit_deps.count == 0);
    ASSERT(raw_link_libs.count == 1);
    ASSERT(nob_sv_eq(raw_link_libs.items[0].value, nob_sv_from_cstr("MissingTargetLikeName")));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_add_subdirectory_defaults_rebase_binary_dirs_out_of_source) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    BM_Directory_Id root_dir = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id lib_dir = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id app_dir = BM_DIRECTORY_ID_INVALID;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id shared_id = BM_TARGET_ID_INVALID;
    BM_Target_Id plugin_id = BM_TARGET_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    const Build_Model *model = NULL;

    ASSERT(build_model_write_text_file(
        "subdir_src/lib/CMakeLists.txt",
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"));
    ASSERT(build_model_write_text_file(
        "subdir_src/app/CMakeLists.txt",
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "subdir_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("subdir_src");
    config.binary_dir = nob_sv_from_cstr("subdir_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test C)\n"
        "add_subdirectory(lib)\n"
        "add_subdirectory(app)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    root_dir = build_model_find_directory_id(model,
                                             nob_sv_from_cstr("subdir_src"),
                                             nob_sv_from_cstr("subdir_build"));
    lib_dir = build_model_find_directory_id(model,
                                            nob_sv_from_cstr("subdir_src/lib"),
                                            nob_sv_from_cstr("subdir_build/lib"));
    app_dir = build_model_find_directory_id(model,
                                            nob_sv_from_cstr("subdir_src/app"),
                                            nob_sv_from_cstr("subdir_build/app"));
    ASSERT(root_dir != BM_DIRECTORY_ID_INVALID);
    ASSERT(lib_dir != BM_DIRECTORY_ID_INVALID);
    ASSERT(app_dir != BM_DIRECTORY_ID_INVALID);

    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    shared_id = bm_query_target_by_name(model, nob_sv_from_cstr("shared"));
    plugin_id = bm_query_target_by_name(model, nob_sv_from_cstr("plugin"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(core_id != BM_TARGET_ID_INVALID);
    ASSERT(shared_id != BM_TARGET_ID_INVALID);
    ASSERT(plugin_id != BM_TARGET_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_target_owner_directory(model, core_id) == lib_dir);
    ASSERT(bm_query_target_owner_directory(model, shared_id) == lib_dir);
    ASSERT(bm_query_target_owner_directory(model, plugin_id) == lib_dir);
    ASSERT(bm_query_target_owner_directory(model, app_id) == app_dir);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_generated_sources_rebase_to_binary_dir_and_link_producer_steps) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Build_Step_Id step_id = BM_BUILD_STEP_ID_INVALID;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "gen_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("gen_src");
    config.binary_dir = nob_sv_from_cstr("gen_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test C)\n"
        "add_custom_command(\n"
        "  OUTPUT generated/generated.c generated/generated.h\n"
        "  COMMAND echo gen\n"
        "  DEPENDS schema.idl\n"
        "  BYPRODUCTS generated/generated.log)\n"
        "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_build_step_count(model) == 1);

    step_id = 0;
    ASSERT(bm_query_build_step_kind(model, step_id) == BM_BUILD_STEP_OUTPUT_RULE);
    ASSERT(bm_query_build_step_outputs(model, step_id).count == 2);
    ASSERT(bm_query_build_step_byproducts(model, step_id).count == 1);
    ASSERT(bm_query_build_step_file_dependencies(model, step_id).count == 1);
    ASSERT(nob_sv_eq(bm_query_build_step_file_dependencies(model, step_id).items[0],
                     nob_sv_from_cstr("gen_src/schema.idl")));

    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_source_count(model, app_id) == 2);

    bool saw_generated = false;
    for (size_t i = 0; i < bm_query_target_source_count(model, app_id); ++i) {
        String_View effective = bm_query_target_source_effective(model, app_id, i);
        if (!build_model_sv_contains(effective, nob_sv_from_cstr("generated/generated.c"))) continue;
        ASSERT(bm_query_target_source_generated(model, app_id, i));
        ASSERT(bm_query_target_source_producer_step(model, app_id, i) == step_id);
        saw_generated = true;
    }
    ASSERT(saw_generated);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_build_steps_classify_target_producer_and_file_dependencies_and_preserve_hooks) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id helper_id = BM_TARGET_ID_INVALID;
    BM_Target_Id prepare_id = BM_TARGET_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Build_Step_Id output_rule_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id custom_target_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id pre_build_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id pre_link_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id post_build_id = BM_BUILD_STEP_ID_INVALID;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "graph_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("graph_src");
    config.binary_dir = nob_sv_from_cstr("graph_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test C)\n"
        "add_custom_command(OUTPUT generated.c COMMAND echo gen DEPENDS schema.idl)\n"
        "add_executable(helper helper.c)\n"
        "add_custom_target(prepare DEPENDS helper ${CMAKE_CURRENT_BINARY_DIR}/generated.c extra.txt BYPRODUCTS prepared.txt)\n"
        "add_executable(app main.c)\n"
        "add_custom_command(TARGET app PRE_BUILD COMMAND echo before BYPRODUCTS before.txt)\n"
        "add_custom_command(TARGET app PRE_LINK COMMAND echo pre BYPRODUCTS pre.txt)\n"
        "add_custom_command(TARGET app POST_BUILD COMMAND echo post BYPRODUCTS post.txt)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_build_step_count(model) == 5);

    helper_id = bm_query_target_by_name(model, nob_sv_from_cstr("helper"));
    prepare_id = bm_query_target_by_name(model, nob_sv_from_cstr("prepare"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(helper_id != BM_TARGET_ID_INVALID);
    ASSERT(prepare_id != BM_TARGET_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_kind(model, prepare_id) == BM_TARGET_UTILITY);

    output_rule_id = 0;
    ASSERT(bm_query_build_step_kind(model, output_rule_id) == BM_BUILD_STEP_OUTPUT_RULE);

    custom_target_id = build_model_find_step_by_kind_and_owner(model,
                                                               BM_BUILD_STEP_CUSTOM_TARGET,
                                                               prepare_id);
    pre_build_id = build_model_find_step_by_kind_and_owner(model,
                                                           BM_BUILD_STEP_TARGET_PRE_BUILD,
                                                           app_id);
    pre_link_id = build_model_find_step_by_kind_and_owner(model,
                                                          BM_BUILD_STEP_TARGET_PRE_LINK,
                                                          app_id);
    post_build_id = build_model_find_step_by_kind_and_owner(model,
                                                            BM_BUILD_STEP_TARGET_POST_BUILD,
                                                            app_id);
    ASSERT(custom_target_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(pre_build_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(pre_link_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(post_build_id != BM_BUILD_STEP_ID_INVALID);

    ASSERT(bm_query_build_step_target_dependencies(model, custom_target_id).count == 1);
    ASSERT(bm_query_build_step_target_dependencies(model, custom_target_id).items[0] == helper_id);
    ASSERT(bm_query_build_step_producer_dependencies(model, custom_target_id).count == 1);
    ASSERT(bm_query_build_step_producer_dependencies(model, custom_target_id).items[0] == output_rule_id);
    ASSERT(bm_query_build_step_file_dependencies(model, custom_target_id).count == 1);
    ASSERT(nob_sv_eq(bm_query_build_step_file_dependencies(model, custom_target_id).items[0],
                     nob_sv_from_cstr("graph_src/extra.txt")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_resolves_direct_and_binary_generated_source_producers) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    size_t binary_index = 0;
    size_t direct_index = 0;
    const char *cwd = nob_get_current_dir_temp();
    char *direct_output_path = NULL;
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(cwd != NULL);
    direct_output_path = arena_strdup(arena, nob_temp_sprintf("%s/match_build/generated/direct.c", cwd));
    ASSERT(direct_output_path != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("match_src");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("match_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 2);
    ev.as.target_declare.name = nob_sv_from_cstr("app");
    ev.as.target_declare.target_type = EV_TARGET_EXECUTABLE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 3);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr("main.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 4);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr("generated/from_binary.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 5);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr(direct_output_path);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 6);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("step_binary");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 7);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("step_binary");
    ev.as.build_step_add_output.path = nob_sv_from_cstr("generated/from_binary.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 8);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("step_direct");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 9);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("step_direct");
    ev.as.build_step_add_output.path = nob_sv_from_cstr(direct_output_path);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 10);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("match_src");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("match_build");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena,
                                                          validate_arena,
                                                          model_arena,
                                                          stream,
                                                          &build));
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    ASSERT(bm_query_build_step_count(model) == 2);
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    binary_index = build_model_find_target_source_index_containing(model, app_id, "generated/from_binary.c");
    direct_index = build_model_find_target_source_index_containing(model, app_id, "generated/direct.c");
    ASSERT(binary_index < bm_query_target_source_count(model, app_id));
    ASSERT(direct_index < bm_query_target_source_count(model, app_id));
    ASSERT(bm_query_target_source_generated(model, app_id, binary_index));
    ASSERT(bm_query_target_source_generated(model, app_id, direct_index));
    ASSERT(bm_build_step_id_is_valid(bm_query_target_source_producer_step(model, app_id, binary_index)));
    ASSERT(bm_build_step_id_is_valid(bm_query_target_source_producer_step(model, app_id, direct_index)));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_resolves_source_and_stripped_generated_source_producers) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    size_t source_index = 0;
    size_t stripped_index = 0;
    const char *cwd = nob_get_current_dir_temp();
    char *source_root_abs = NULL;
    char *strip_source_path = NULL;
    char *source_output_path = NULL;
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(cwd != NULL);
    source_root_abs = arena_strdup(arena, nob_temp_sprintf("%s/strip_src", cwd));
    source_output_path = arena_strdup(arena, nob_temp_sprintf("%s/generated/source_match.c", source_root_abs));
    strip_source_path = arena_strdup(arena, nob_temp_sprintf("%s/generated/strip_match.c", source_root_abs));
    ASSERT(source_root_abs != NULL);
    ASSERT(source_output_path != NULL);
    ASSERT(strip_source_path != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(source_root_abs);
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 2);
    ev.as.target_declare.name = nob_sv_from_cstr("app");
    ev.as.target_declare.target_type = EV_TARGET_EXECUTABLE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 3);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr("main.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 4);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr("generated/source_match.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_ADD_SOURCE, 5);
    ev.as.target_add_source.target_name = nob_sv_from_cstr("app");
    ev.as.target_add_source.path = nob_sv_from_cstr(strip_source_path);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 6);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("step_source");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 7);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("step_source");
    ev.as.build_step_add_output.path = nob_sv_from_cstr(source_output_path);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 8);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("step_strip");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 9);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("step_strip");
    ev.as.build_step_add_output.path = nob_sv_from_cstr("generated/strip_match.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 10);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(source_root_abs);
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena,
                                                          validate_arena,
                                                          model_arena,
                                                          stream,
                                                          &build));
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    source_index = build_model_find_target_source_index_containing(model, app_id, "generated/source_match.c");
    stripped_index = build_model_find_target_source_index_containing(model, app_id, "generated/strip_match.c");
    ASSERT(source_index < bm_query_target_source_count(model, app_id));
    ASSERT(stripped_index < bm_query_target_source_count(model, app_id));
    ASSERT(bm_query_target_source_generated(model, app_id, source_index));
    ASSERT(bm_query_target_source_generated(model, app_id, stripped_index));
    ASSERT(bm_build_step_id_is_valid(bm_query_target_source_producer_step(model, app_id, source_index)));
    ASSERT(bm_build_step_id_is_valid(bm_query_target_source_producer_step(model, app_id, stripped_index)));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_resolves_byproduct_producers_and_keeps_unresolved_file_dependencies) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id prepare_id = BM_TARGET_ID_INVALID;
    BM_Build_Step_Id prepare_step_id = BM_BUILD_STEP_ID_INVALID;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "byproduct_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("byproduct_src");
    config.binary_dir = nob_sv_from_cstr("byproduct_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test C)\n"
        "add_custom_command(OUTPUT generated/main.c COMMAND echo gen BYPRODUCTS generated/sidecar.log)\n"
        "add_custom_target(prepare DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/sidecar.log extra.txt)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    prepare_id = bm_query_target_by_name(model, nob_sv_from_cstr("prepare"));
    ASSERT(prepare_id != BM_TARGET_ID_INVALID);
    prepare_step_id = build_model_find_step_by_kind_and_owner(model,
                                                              BM_BUILD_STEP_CUSTOM_TARGET,
                                                              prepare_id);
    ASSERT(prepare_step_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(bm_query_build_step_producer_dependencies(model, prepare_step_id).count == 1);
    ASSERT(bm_query_build_step_producer_dependencies(model, prepare_step_id).items[0] == 0);
    ASSERT(bm_query_build_step_file_dependencies(model, prepare_step_id).count == 1);
    ASSERT(nob_sv_eq(bm_query_build_step_file_dependencies(model, prepare_step_id).items[0],
                     nob_sv_from_cstr("byproduct_src/extra.txt")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_marks_generated_sources_without_producer_steps) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    size_t marked_a = 0;
    size_t marked_b = 0;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "mark_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("mark_src");
    config.binary_dir = nob_sv_from_cstr("mark_src");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test C)\n"
        "add_executable(app main.c marked_a.c marked_b.c)\n"
        "set_source_files_properties(marked_a.c PROPERTIES GENERATED TRUE)\n"
        "set_property(SOURCE marked_b.c PROPERTY GENERATED TRUE)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    marked_a = build_model_find_target_source_index_containing(model, app_id, "marked_a.c");
    marked_b = build_model_find_target_source_index_containing(model, app_id, "marked_b.c");
    ASSERT(marked_a < bm_query_target_source_count(model, app_id));
    ASSERT(marked_b < bm_query_target_source_count(model, app_id));
    ASSERT(bm_query_target_source_generated(model, app_id, marked_a));
    ASSERT(bm_query_target_source_generated(model, app_id, marked_b));
    ASSERT(bm_query_target_source_producer_step(model, app_id, marked_a) == BM_BUILD_STEP_ID_INVALID);
    ASSERT(bm_query_target_source_producer_step(model, app_id, marked_b) == BM_BUILD_STEP_ID_INVALID);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_freeze_rejects_duplicate_effective_producers_and_execution_cycles) {
    Arena *dup_arena = arena_create(2 * 1024 * 1024);
    Arena *dup_validate_arena = arena_create(512 * 1024);
    Arena *dup_model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result dup_build = {0};
    Event_Stream *dup_stream = NULL;
    Event ev = {0};
    Test_Semantic_Pipeline_Config cycle_config = {0};
    Test_Semantic_Pipeline_Fixture cycle_fixture = {0};
    ASSERT(dup_arena != NULL);
    ASSERT(dup_validate_arena != NULL);
    ASSERT(dup_model_arena != NULL);

    dup_stream = event_stream_create(dup_arena);
    ASSERT(dup_stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("dup_src");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("dup_build");
    ASSERT(event_stream_push(dup_stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 2);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("dup_one");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(dup_stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 3);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("dup_one");
    ev.as.build_step_add_output.path = nob_sv_from_cstr("generated/out.c");
    ASSERT(event_stream_push(dup_stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_DECLARE, 4);
    ev.as.build_step_declare.step_key = nob_sv_from_cstr("dup_two");
    ev.as.build_step_declare.step_kind = EVENT_BUILD_STEP_OUTPUT_RULE;
    ASSERT(event_stream_push(dup_stream, &ev));

    build_model_init_event(&ev, EVENT_BUILD_STEP_ADD_OUTPUT, 5);
    ev.as.build_step_add_output.step_key = nob_sv_from_cstr("dup_two");
    ev.as.build_step_add_output.path = nob_sv_from_cstr("generated/out.c");
    ASSERT(event_stream_push(dup_stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 6);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("dup_src");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("dup_build");
    ASSERT(event_stream_push(dup_stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(dup_arena,
                                                          dup_validate_arena,
                                                          dup_model_arena,
                                                          dup_stream,
                                                          &dup_build));
    ASSERT(dup_build.validate_ok);
    ASSERT(!dup_build.freeze_ok);

    test_semantic_pipeline_config_init(&cycle_config);
    cycle_config.current_file = "cycle_src/CMakeLists.txt";
    cycle_config.source_dir = nob_sv_from_cstr("cycle_src");
    cycle_config.binary_dir = nob_sv_from_cstr("cycle_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &cycle_fixture,
        "project(Test C)\n"
        "add_custom_target(prepare DEPENDS app)\n"
        "add_executable(app main.c)\n"
        "add_dependencies(app prepare)\n",
        &cycle_config));
    ASSERT(cycle_fixture.eval_ok);
    ASSERT(cycle_fixture.build.validate_ok);
    ASSERT(!cycle_fixture.build.freeze_ok);

    arena_destroy(dup_arena);
    arena_destroy(dup_validate_arena);
    arena_destroy(dup_model_arena);
    test_semantic_pipeline_fixture_destroy(&cycle_fixture);
    TEST_PASS();
}

void run_build_model_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "build_model");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "build-model suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "build-model suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_build_model_builder_directory_scope_events(passed, failed, skipped);
    test_build_model_validate_does_not_infer_link_library_targets(passed, failed, skipped);
    test_build_model_add_subdirectory_defaults_rebase_binary_dirs_out_of_source(passed, failed, skipped);
    test_build_model_generated_sources_rebase_to_binary_dir_and_link_producer_steps(passed, failed, skipped);
    test_build_model_build_steps_classify_target_producer_and_file_dependencies_and_preserve_hooks(passed, failed, skipped);
    test_build_model_resolves_direct_and_binary_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_source_and_stripped_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_byproduct_producers_and_keeps_unresolved_file_dependencies(passed, failed, skipped);
    test_build_model_marks_generated_sources_without_producer_steps(passed, failed, skipped);
    test_build_model_freeze_rejects_duplicate_effective_producers_and_execution_cycles(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "build-model suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
