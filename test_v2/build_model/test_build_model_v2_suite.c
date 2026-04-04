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

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "build-model suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
