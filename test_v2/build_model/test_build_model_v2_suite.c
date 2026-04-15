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

static bool build_model_string_span_contains(BM_String_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i], needle_sv)) return true;
    }
    return false;
}

static bool build_model_string_item_span_contains(BM_String_Item_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i].value, needle_sv)) return true;
    }
    return false;
}

static bool build_model_string_span_contains_substring(BM_String_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (build_model_sv_contains(span.items[i], needle_sv)) return true;
    }
    return false;
}

static bool build_model_string_item_span_contains_substring(BM_String_Item_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (build_model_sv_contains(span.items[i].value, needle_sv)) return true;
    }
    return false;
}

static bool build_model_string_item_equals_at(BM_String_Item_Span span, size_t index, const char *expected) {
    return index < span.count && nob_sv_eq(span.items[index].value, nob_sv_from_cstr(expected ? expected : ""));
}

static bool build_model_string_equals_at(BM_String_Span span, size_t index, const char *expected) {
    return index < span.count && nob_sv_eq(span.items[index], nob_sv_from_cstr(expected ? expected : ""));
}

static bool build_model_string_item_contains_at(BM_String_Item_Span span, size_t index, const char *needle) {
    return index < span.count && build_model_sv_contains(span.items[index].value, nob_sv_from_cstr(needle ? needle : ""));
}

static bool build_model_string_span_equals(BM_String_Span lhs, BM_String_Span rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        if (!nob_sv_eq(lhs.items[i], rhs.items[i])) return false;
    }
    return true;
}

static bool build_model_string_item_span_equals(BM_String_Item_Span lhs, BM_String_Item_Span rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        if (!nob_sv_eq(lhs.items[i].value, rhs.items[i].value) ||
            lhs.items[i].visibility != rhs.items[i].visibility ||
            lhs.items[i].flags != rhs.items[i].flags) {
            return false;
        }
    }
    return true;
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

TEST(build_model_replay_actions_freeze_query_and_preserve_order) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Directory_Id sub_dir = BM_DIRECTORY_ID_INVALID;
    BM_Replay_Action_Id nested_id = (BM_Replay_Action_Id)0;
    BM_Replay_Action_Id root_id = (BM_Replay_Action_Id)1;
    BM_String_Span argv = {0};
    BM_String_Span env = {0};

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 2);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 3);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("nested_fs");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_FILESYSTEM;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_FS_COPY_FILE;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("work/config");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 4);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("nested_fs");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("inputs/template.in");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 5);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("nested_fs");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("outputs/generated.txt");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("nested_fs");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("0644");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 7);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("sub");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("sub-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 8);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("root_proc");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_PROCESS;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_NONE;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_BUILD;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 9);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("root_proc");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("artifacts/out.bin");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 10);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("root_proc");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("tool");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 11);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    sub_dir = build_model_find_directory_id(model, nob_sv_from_cstr("sub"), nob_sv_from_cstr("sub-build"));
    ASSERT(sub_dir != BM_DIRECTORY_ID_INVALID);

    ASSERT(bm_query_replay_action_count(model) == 2);

    ASSERT(bm_query_replay_action_kind(model, nested_id) == BM_REPLAY_ACTION_FILESYSTEM);
    ASSERT(bm_query_replay_action_opcode(model, nested_id) == BM_REPLAY_OPCODE_FS_COPY_FILE);
    ASSERT(bm_query_replay_action_phase(model, nested_id) == BM_REPLAY_PHASE_CONFIGURE);
    ASSERT(bm_query_replay_action_owner_directory(model, nested_id) == sub_dir);
    ASSERT(nob_sv_eq(bm_query_replay_action_working_directory(model, nested_id), nob_sv_from_cstr("work/config")));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_inputs(model, nested_id), 0, "inputs/template.in"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, nested_id), 0, "outputs/generated.txt"));
    argv = bm_query_replay_action_argv(model, nested_id);
    env = bm_query_replay_action_environment(model, nested_id);
    ASSERT(argv.count == 1);
    ASSERT(build_model_string_equals_at(argv, 0, "0644"));
    ASSERT(env.count == 0);

    ASSERT(bm_query_replay_action_kind(model, root_id) == BM_REPLAY_ACTION_PROCESS);
    ASSERT(bm_query_replay_action_opcode(model, root_id) == BM_REPLAY_OPCODE_NONE);
    ASSERT(bm_query_replay_action_phase(model, root_id) == BM_REPLAY_PHASE_BUILD);
    ASSERT(bm_query_replay_action_owner_directory(model, root_id) == bm_query_root_directory(model));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, root_id), 0, "artifacts/out.bin"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, root_id), 0, "tool"));

    ASSERT(!bm_replay_action_id_is_valid(BM_REPLAY_ACTION_ID_INVALID));
    ASSERT(bm_query_replay_action_opcode(model, BM_REPLAY_ACTION_ID_INVALID) == BM_REPLAY_OPCODE_NONE);
    ASSERT(bm_query_replay_action_owner_directory(model, BM_REPLAY_ACTION_ID_INVALID) == BM_DIRECTORY_ID_INVALID);
    ASSERT(bm_query_replay_action_inputs(model, BM_REPLAY_ACTION_ID_INVALID).count == 0);
    ASSERT(bm_query_replay_action_outputs(model, BM_REPLAY_ACTION_ID_INVALID).count == 0);
    ASSERT(bm_query_replay_action_argv(model, BM_REPLAY_ACTION_ID_INVALID).count == 0);
    ASSERT(bm_query_replay_action_environment(model, BM_REPLAY_ACTION_ID_INVALID).count == 0);
    ASSERT(bm_query_replay_action_working_directory(model, BM_REPLAY_ACTION_ID_INVALID).count == 0);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_replay_actions_reject_invalid_opcode_payload_shapes) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_write");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_FILESYSTEM;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_FS_WRITE_TEXT;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 3);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_write");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("generated/out.txt");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 4);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(!build.validate_ok);
    ASSERT(!build.freeze_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);

    arena = arena_create(2 * 1024 * 1024);
    validate_arena = arena_create(512 * 1024);
    model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_extract");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_HOST_EFFECT;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 3);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("bad_extract");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("sample.tar");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_extract");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("out");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_extract");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("unexpected");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 6);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build = (Test_Semantic_Pipeline_Build_Result){0};
    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(!build.validate_ok);
    ASSERT(!build.freeze_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);

    arena = arena_create(2 * 1024 * 1024);
    validate_arena = arena_create(512 * 1024);
    model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_ctest_build");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 3);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_ctest_build");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 4);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_build");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Debug");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 5);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build = (Test_Semantic_Pipeline_Build_Result){0};
    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(!build.validate_ok);
    ASSERT(!build.freeze_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);

    arena = arena_create(2 * 1024 * 1024);
    validate_arena = arena_create(512 * 1024);
    model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 3);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 4);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Experimental");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Nightly");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 2;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 7);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 3;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("2");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 8);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 4;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("gcov");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 9);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build = (Test_Semantic_Pipeline_Build_Result){0};
    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(!build.validate_ok);
    ASSERT(!build.freeze_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);

    arena = arena_create(2 * 1024 * 1024);
    validate_arena = arena_create(512 * 1024);
    model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 3);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("resource.json");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 5);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("reports/memcheck.junit.xml");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Experimental");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 7);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Nightly");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 8);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 2;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 9);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 3;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 10);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 4;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 11);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 5;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 12);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 6;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 13);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 7;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 14);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 8;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("0");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 15);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 9;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("0");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 16);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 10;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 17);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 11;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 18);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 12;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Valgrind");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 19);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_ctest_memcheck");
    ev.as.replay_action_add_argv.arg_index = 13;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("0");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 20);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build = (Test_Semantic_Pipeline_Build_Result){0};
    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(!build.validate_ok);
    ASSERT(!build.freeze_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_tests_freeze_owner_working_dir_expand_lists_and_configurations) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Directory_Id sub_dir = BM_DIRECTORY_ID_INVALID;
    BM_Test_Id test_id = (BM_Test_Id)0;
    String_View configurations[2] = {
        nob_sv_from_cstr("Debug"),
        nob_sv_from_cstr("RelWithDebInfo"),
    };

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 2);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr("tests");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr("tests-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TEST_ENABLE, 3);
    ev.as.test_enable.enabled = true;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TEST_ADD, 4);
    ev.as.test_add.name = nob_sv_from_cstr("cfg_only");
    ev.as.test_add.command = nob_sv_from_cstr("app --mode smoke");
    ev.as.test_add.working_dir = nob_sv_from_cstr("work");
    ev.as.test_add.command_expand_lists = true;
    ev.as.test_add.configurations = configurations;
    ev.as.test_add.configuration_count = NOB_ARRAY_LEN(configurations);
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 5);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr("tests");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr("tests-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 6);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    sub_dir = build_model_find_directory_id(model, nob_sv_from_cstr("tests"), nob_sv_from_cstr("tests-build"));
    ASSERT(sub_dir != BM_DIRECTORY_ID_INVALID);
    ASSERT(bm_query_testing_enabled(model));
    ASSERT(bm_query_test_count(model) == 1);
    ASSERT(nob_sv_eq(bm_query_test_name(model, test_id), nob_sv_from_cstr("cfg_only")));
    ASSERT(nob_sv_eq(bm_query_test_command(model, test_id), nob_sv_from_cstr("app --mode smoke")));
    ASSERT(bm_query_test_owner_directory(model, test_id) == sub_dir);
    ASSERT(nob_sv_eq(bm_query_test_working_directory(model, test_id), nob_sv_from_cstr("work")));
    ASSERT(bm_query_test_command_expand_lists(model, test_id));
    ASSERT(build_model_string_equals_at(bm_query_test_configurations(model, test_id), 0, "Debug"));
    ASSERT(build_model_string_equals_at(bm_query_test_configurations(model, test_id), 1, "RelWithDebInfo"));
    ASSERT(bm_query_test_configurations(model, BM_TEST_ID_INVALID).count == 0);
    ASSERT(!bm_query_test_command_expand_lists(model, BM_TEST_ID_INVALID));
    ASSERT(bm_query_test_owner_directory(model, BM_TEST_ID_INVALID) == BM_DIRECTORY_ID_INVALID);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_replay_actions_accept_c3_opcodes_and_queries) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("fetch_src");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("deps");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 3);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("fetch_src");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("deps/zlib-src");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("fetch_src");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("deps/zlib-build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("fetch_src");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("zlib");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 6);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("probe_run");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_PROBE;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_PROBE_TRY_RUN;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("probe");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 7);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("probe_run");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("probe/bin");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 8);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("probe_run");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("TRY_RUN_RESULT");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 9);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("ctest_local");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 10);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("ctest_local");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 11);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_local");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("reports/junit.xml");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 12);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_local");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 13);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    ASSERT(bm_query_replay_action_count(model) == 3);
    ASSERT(bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)0) == BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR);
    ASSERT(bm_query_replay_action_kind(model, (BM_Replay_Action_Id)0) == BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION);
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)0), 0, "deps/zlib-src"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)0), 1, "deps/zlib-build"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)0), 0, "zlib"));

    ASSERT(bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)1) == BM_REPLAY_OPCODE_PROBE_TRY_RUN);
    ASSERT(bm_query_replay_action_kind(model, (BM_Replay_Action_Id)1) == BM_REPLAY_ACTION_PROBE);
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)1), 0, "probe/bin"));

    ASSERT(bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)2) == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST);
    ASSERT(bm_query_replay_action_kind(model, (BM_Replay_Action_Id)2) == BM_REPLAY_ACTION_TEST_DRIVER);
    ASSERT(bm_query_replay_action_phase(model, (BM_Replay_Action_Id)2) == BM_REPLAY_PHASE_TEST);
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)2), 0, "ctest_build"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)2), 0, "reports/junit.xml"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)2), 1, "1"));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_replay_actions_accept_c5_ctest_coverage_and_memcheck_queries) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 3);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("src/main.c");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Experimental");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 6);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("Nightly");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 7);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 2;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("1");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 8);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 3;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("2");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 9);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 4;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("gcov");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 10);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_coverage");
    ev.as.replay_action_add_argv.arg_index = 5;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("--json-format");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 11);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("ctest_memcheck");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_TEST_DRIVER;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_TEST;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 12);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("ctest_memcheck");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("ctest-resource.json");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 13);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("ctest_memcheck");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("suppressions.supp");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 14);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("ctest_memcheck");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("ctest_build");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 15);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("ctest_memcheck");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("reports/memcheck.junit.xml");
    ASSERT(event_stream_push(stream, &ev));

    {
        const String_View mem_argv[] = {
            nob_sv_from_cstr("Experimental"),
            nob_sv_from_cstr("Nightly"),
            nob_sv_from_cstr("0"),
            nob_sv_from_cstr("1"),
            nob_sv_from_cstr("2"),
            nob_sv_from_cstr("1"),
            nob_sv_from_cstr("3"),
            nob_sv_from_cstr("23:59:59"),
            nob_sv_from_cstr("1"),
            nob_sv_from_cstr("0"),
            nob_sv_from_cstr("UNTIL_PASS:2"),
            nob_sv_from_cstr("8"),
            nob_sv_from_cstr("Valgrind"),
            nob_sv_from_cstr("2"),
            nob_sv_from_cstr("valgrind"),
            nob_sv_from_cstr("--tool=memcheck"),
        };
        for (size_t i = 0; i < NOB_ARRAY_LEN(mem_argv); ++i) {
            build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 16 + (uint64_t)i);
            ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("ctest_memcheck");
            ev.as.replay_action_add_argv.arg_index = i;
            ev.as.replay_action_add_argv.value = mem_argv[i];
            ASSERT(event_stream_push(stream, &ev));
        }
    }

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 40);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    ASSERT(bm_query_replay_action_count(model) == 2);

    ASSERT(bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)0) == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL);
    ASSERT(bm_query_replay_action_kind(model, (BM_Replay_Action_Id)0) == BM_REPLAY_ACTION_TEST_DRIVER);
    ASSERT(bm_query_replay_action_phase(model, (BM_Replay_Action_Id)0) == BM_REPLAY_PHASE_TEST);
    ASSERT(build_model_string_equals_at(bm_query_replay_action_inputs(model, (BM_Replay_Action_Id)0), 0, "src/main.c"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)0), 0, "ctest_build"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)0), 0, "Experimental"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)0), 3, "2"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)0), 4, "gcov"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)0), 5, "--json-format"));

    ASSERT(bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)1) == BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL);
    ASSERT(build_model_string_equals_at(bm_query_replay_action_inputs(model, (BM_Replay_Action_Id)1), 0, "ctest-resource.json"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_inputs(model, (BM_Replay_Action_Id)1), 1, "suppressions.supp"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)1), 0, "ctest_build"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_outputs(model, (BM_Replay_Action_Id)1), 1, "reports/memcheck.junit.xml"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)1), 12, "Valgrind"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)1), 13, "2"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)1), 14, "valgrind"));
    ASSERT(build_model_string_equals_at(bm_query_replay_action_argv(model, (BM_Replay_Action_Id)1), 15, "--tool=memcheck"));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_replay_actions_reject_malformed_ordering) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 2);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("missing");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("input.txt");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 3);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(!build.builder_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);

    arena = arena_create(2 * 1024 * 1024);
    validate_arena = arena_create(512 * 1024);
    model_arena = arena_create(2 * 1024 * 1024);
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("bad_argv");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_PROCESS;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_BUILD;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 3);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("bad_argv");
    ev.as.replay_action_add_argv.arg_index = 1;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("tool");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 4);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build = (Test_Semantic_Pipeline_Build_Result){0};
    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(!build.builder_ok);

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_context_aware_queries_expand_usage_requirements_and_target_property_genex) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id iface_id = BM_TARGET_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_c = {0};
    BM_Query_Eval_Context compile_cxx = {0};
    BM_Query_Eval_Context compile_install = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_Query_Eval_Context debug_compile = {0};
    BM_String_Item_Span include_items = {0};
    BM_String_Item_Span compile_opts = {0};
    BM_String_Item_Span link_items = {0};
    BM_String_Span defs = {0};
    String_View property_value = {0};
    String_View raw_property_value = {0};
    bool found_custom_prop = false;

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "p3_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("p3_query_src");
    config.binary_dir = nob_sv_from_cstr("p3_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "add_library(iface INTERFACE)\n"
        "set_target_properties(iface PROPERTIES CUSTOM_TAG dbg-tag)\n"
        "target_include_directories(iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:iface_build/include>\"\n"
        "  \"$<INSTALL_INTERFACE:iface_install/include>\")\n"
        "target_compile_options(iface INTERFACE\n"
        "  \"$<$<COMPILE_LANGUAGE:C>:-DC_ONLY>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:CXX>:-DCXX_ONLY>\")\n"
        "target_link_libraries(iface INTERFACE \"$<LINK_ONLY:m>\")\n"
        "add_library(raw_iface INTERFACE IMPORTED)\n"
        "set_target_properties(raw_iface PROPERTIES\n"
        "  INTERFACE_INCLUDE_DIRECTORIES raw_iface/include\n"
        "  INTERFACE_COMPILE_DEFINITIONS RAW_IFACE_DEF=1)\n"
        "add_executable(app main.c main.cpp)\n"
        "target_link_libraries(app PRIVATE iface raw_iface)\n"
        "target_compile_definitions(app PRIVATE \"$<$<CONFIG:Debug>:$<TARGET_PROPERTY:iface,CUSTOM_TAG>>\")\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    iface_id = bm_query_target_by_name(model, nob_sv_from_cstr("iface"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(iface_id != BM_TARGET_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    compile_c.current_target_id = app_id;
    compile_c.usage_mode = BM_QUERY_USAGE_COMPILE;
    compile_c.compile_language = nob_sv_from_cstr("C");
    compile_c.build_interface_active = true;
    compile_c.install_interface_active = false;

    compile_cxx = compile_c;
    compile_cxx.compile_language = nob_sv_from_cstr("CXX");

    compile_install = compile_c;
    compile_install.build_interface_active = false;
    compile_install.install_interface_active = true;

    link_ctx.current_target_id = app_id;
    link_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    link_ctx.build_interface_active = true;
    link_ctx.install_interface_active = false;

    debug_compile = compile_c;
    debug_compile.config = nob_sv_from_cstr("Debug");

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &compile_c,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(build_model_string_item_span_contains(include_items, "iface_build/include"));
    ASSERT(build_model_string_item_span_contains(include_items, "raw_iface/include"));
    ASSERT(!build_model_string_item_span_contains(include_items, "iface_install/include"));

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &compile_install,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(!build_model_string_item_span_contains(include_items, "iface_build/include"));
    ASSERT(build_model_string_item_span_contains(include_items, "iface_install/include"));

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &compile_c,
                                                                        query_arena,
                                                                        &compile_opts));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DC_ONLY"));
    ASSERT(!build_model_string_item_span_contains(compile_opts, "-DCXX_ONLY"));

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &compile_cxx,
                                                                        query_arena,
                                                                        &compile_opts));
    ASSERT(!build_model_string_item_span_contains(compile_opts, "-DC_ONLY"));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DCXX_ONLY"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_items));
    ASSERT(build_model_string_item_span_contains(link_items, "m"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &compile_c,
                                                                       query_arena,
                                                                       &link_items));
    ASSERT(!build_model_string_item_span_contains(link_items, "m"));

    ASSERT(bm_query_target_effective_compile_definitions_with_context(model,
                                                                      app_id,
                                                                      &compile_c,
                                                                      query_arena,
                                                                      &defs));
    ASSERT(!build_model_string_span_contains(defs, "dbg-tag"));

    ASSERT(bm_query_target_effective_compile_definitions_with_context(model,
                                                                      app_id,
                                                                      &debug_compile,
                                                                      query_arena,
                                                                      &defs));
    ASSERT(build_model_string_span_contains(defs, "dbg-tag"));
    ASSERT(build_model_string_span_contains(defs, "RAW_IFACE_DEF=1"));

    ASSERT(bm_query_target_property_value(model,
                                          iface_id,
                                          nob_sv_from_cstr("CUSTOM_TAG"),
                                          query_arena,
                                          &property_value));
    ASSERT(nob_sv_eq(property_value, nob_sv_from_cstr("dbg-tag")));

    ASSERT(bm_query_target_property_value(model,
                                          bm_query_target_by_name(model, nob_sv_from_cstr("raw_iface")),
                                          nob_sv_from_cstr("INTERFACE_INCLUDE_DIRECTORIES"),
                                          query_arena,
                                          &raw_property_value));
    ASSERT(nob_sv_eq(raw_property_value, nob_sv_from_cstr("raw_iface/include")));

    for (size_t i = 0; i < bm_query_target_raw_property_count(model, iface_id); ++i) {
        if (nob_sv_eq(bm_query_target_raw_property_name(model, iface_id, i), nob_sv_from_cstr("CUSTOM_TAG"))) {
            found_custom_prop = true;
            ASSERT(build_model_string_span_contains(
                bm_query_target_raw_property_items(model, iface_id, nob_sv_from_cstr("CUSTOM_TAG")),
                "dbg-tag"));
            break;
        }
    }
    ASSERT(found_custom_prop);

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_platform_context_and_typed_platform_properties_are_queryable) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id bundle_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context linux_ctx = {0};
    BM_Query_Eval_Context windows_ctx = {0};
    BM_String_Span defs = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "p4_platform_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("p4_platform_src");
    config.binary_dir = nob_sv_from_cstr("p4_platform_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(iface INTERFACE)\n"
        "target_compile_definitions(iface INTERFACE\n"
        "  \"$<$<PLATFORM_ID:Linux>:LINUX_ONLY>\"\n"
        "  \"$<$<PLATFORM_ID:Windows>:WINDOWS_ONLY>\")\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE iface)\n"
        "set_target_properties(app PROPERTIES WIN32_EXECUTABLE ON)\n"
        "add_executable(bundle bundle.c)\n"
        "set_target_properties(bundle PROPERTIES MACOSX_BUNDLE ON)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    bundle_id = bm_query_target_by_name(model, nob_sv_from_cstr("bundle"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(bundle_id != BM_TARGET_ID_INVALID);

    linux_ctx.current_target_id = app_id;
    linux_ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    linux_ctx.compile_language = nob_sv_from_cstr("C");
    linux_ctx.platform_id = nob_sv_from_cstr("Linux");
    linux_ctx.build_interface_active = true;
    linux_ctx.install_interface_active = false;

    windows_ctx = linux_ctx;
    windows_ctx.platform_id = nob_sv_from_cstr("Windows");

    ASSERT(bm_query_target_effective_compile_definitions_with_context(model,
                                                                      app_id,
                                                                      &linux_ctx,
                                                                      query_arena,
                                                                      &defs));
    ASSERT(build_model_string_span_contains(defs, "LINUX_ONLY"));
    ASSERT(!build_model_string_span_contains(defs, "WINDOWS_ONLY"));

    ASSERT(bm_query_target_effective_compile_definitions_with_context(model,
                                                                      app_id,
                                                                      &windows_ctx,
                                                                      query_arena,
                                                                      &defs));
    ASSERT(!build_model_string_span_contains(defs, "LINUX_ONLY"));
    ASSERT(build_model_string_span_contains(defs, "WINDOWS_ONLY"));

    ASSERT(bm_query_target_win32_executable(model, app_id));
    ASSERT(!bm_query_target_macosx_bundle(model, app_id));
    ASSERT(!bm_query_target_win32_executable(model, bundle_id));
    ASSERT(bm_query_target_macosx_bundle(model, bundle_id));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_imported_target_queries_resolve_configs_and_mapped_locations) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id ext_id = BM_TARGET_ID_INVALID;
    BM_Target_Id missing_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context default_ctx = {0};
    BM_Query_Eval_Context debug_ctx = {0};
    BM_Query_Eval_Context relwithdebinfo_ctx = {0};
    String_View effective_file = {0};
    String_View effective_linker_file = {0};
    BM_String_Span link_langs = {0};
    ASSERT(query_arena != NULL);

    test_semantic_pipeline_config_init(&config);
    config.current_file = "p3_imported_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("p3_imported_src");
    config.binary_dir = nob_sv_from_cstr("p3_imported_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.so\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.so\n"
        "  IMPORTED_IMPLIB_DEBUG imports/libdebug_link.so\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG \"CXX;C\")\n"
        "add_library(missing STATIC IMPORTED)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ext_id = bm_query_target_by_name(model, nob_sv_from_cstr("ext"));
    missing_id = bm_query_target_by_name(model, nob_sv_from_cstr("missing"));
    ASSERT(ext_id != BM_TARGET_ID_INVALID);
    ASSERT(missing_id != BM_TARGET_ID_INVALID);

    default_ctx.current_target_id = ext_id;
    default_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    default_ctx.build_interface_active = true;
    default_ctx.install_interface_active = false;

    debug_ctx = default_ctx;
    debug_ctx.config = nob_sv_from_cstr("Debug");

    relwithdebinfo_ctx = default_ctx;
    relwithdebinfo_ctx.config = nob_sv_from_cstr("RelWithDebInfo");

    ASSERT(bm_query_target_effective_file(model, ext_id, &default_ctx, query_arena, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("p3_imported_src/imports/libbase.so")));

    ASSERT(bm_query_target_effective_file(model, ext_id, &debug_ctx, query_arena, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("p3_imported_src/imports/libdebug.so")));

    ASSERT(bm_query_target_effective_file(model, ext_id, &relwithdebinfo_ctx, query_arena, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("p3_imported_src/imports/libdebug.so")));

    ASSERT(bm_query_target_effective_linker_file(model, ext_id, &debug_ctx, query_arena, &effective_linker_file));
    ASSERT(nob_sv_eq(effective_linker_file, nob_sv_from_cstr("p3_imported_src/imports/libdebug_link.so")));

    ASSERT(bm_query_target_effective_linker_file(model, ext_id, &default_ctx, query_arena, &effective_linker_file));
    ASSERT(nob_sv_eq(effective_linker_file, nob_sv_from_cstr("p3_imported_src/imports/libbase.so")));

    ASSERT(bm_query_target_imported_link_languages(model, ext_id, &debug_ctx, query_arena, &link_langs));
    ASSERT(build_model_string_span_contains(link_langs, "CXX"));
    ASSERT(build_model_string_span_contains(link_langs, "C"));

    ASSERT(build_model_string_span_contains(
        bm_query_target_raw_property_items(model, ext_id, nob_sv_from_cstr("MAP_IMPORTED_CONFIG_RELWITHDEBINFO")),
        "Debug"));

    ASSERT(bm_query_target_effective_file(model, missing_id, &default_ctx, query_arena, &effective_file));
    ASSERT(effective_file.count == 0);

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_preserves_imported_global_across_property_orderings) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Target_Id pre_id = BM_TARGET_ID_INVALID;
    BM_Target_Id post_id = BM_TARGET_ID_INVALID;
    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_PROP_SET, 2);
    ev.as.target_prop_set.target_name = nob_sv_from_cstr("pre");
    ev.as.target_prop_set.key = nob_sv_from_cstr("IMPORTED");
    ev.as.target_prop_set.value = nob_sv_from_cstr("1");
    ev.as.target_prop_set.op = EV_PROP_SET;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_PROP_SET, 3);
    ev.as.target_prop_set.target_name = nob_sv_from_cstr("pre");
    ev.as.target_prop_set.key = nob_sv_from_cstr("IMPORTED_GLOBAL");
    ev.as.target_prop_set.value = nob_sv_from_cstr("1");
    ev.as.target_prop_set.op = EV_PROP_SET;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 4);
    ev.as.target_declare.name = nob_sv_from_cstr("pre");
    ev.as.target_declare.target_type = EV_TARGET_LIBRARY_STATIC;
    ev.as.target_declare.imported = true;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_DECLARE, 5);
    ev.as.target_declare.name = nob_sv_from_cstr("post");
    ev.as.target_declare.target_type = EV_TARGET_LIBRARY_STATIC;
    ev.as.target_declare.imported = true;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_TARGET_PROP_SET, 6);
    ev.as.target_prop_set.target_name = nob_sv_from_cstr("post");
    ev.as.target_prop_set.key = nob_sv_from_cstr("IMPORTED_GLOBAL");
    ev.as.target_prop_set.value = nob_sv_from_cstr("1");
    ev.as.target_prop_set.op = EV_PROP_SET;
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 7);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
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
    pre_id = bm_query_target_by_name(model, nob_sv_from_cstr("pre"));
    post_id = bm_query_target_by_name(model, nob_sv_from_cstr("post"));
    ASSERT(pre_id != BM_TARGET_ID_INVALID);
    ASSERT(post_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_is_imported(model, pre_id));
    ASSERT(bm_query_target_is_imported(model, post_id));
    ASSERT(bm_query_target_is_imported_global(model, pre_id));
    ASSERT(bm_query_target_is_imported_global(model, post_id));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    TEST_PASS();
}

TEST(build_model_alias_and_unknown_target_identity_queries_are_canonical) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id local_real_id = BM_TARGET_ID_INVALID;
    BM_Target_Id local_alias_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_local_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_local_alias_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_global_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_global_alias_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_unknown_id = BM_TARGET_ID_INVALID;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "target_identity_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("target_identity_src");
    config.binary_dir = nob_sv_from_cstr("target_identity_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(local_real STATIC real.c)\n"
        "add_library(local_alias ALIAS local_real)\n"
        "add_library(imported_local STATIC IMPORTED)\n"
        "set_target_properties(imported_local PROPERTIES IMPORTED_LOCATION imports/liblocal.a)\n"
        "add_library(imported_local_alias ALIAS imported_local)\n"
        "add_library(imported_global STATIC IMPORTED GLOBAL)\n"
        "set_target_properties(imported_global PROPERTIES IMPORTED_LOCATION imports/libglobal.a)\n"
        "add_library(imported_global_alias ALIAS imported_global)\n"
        "add_library(imported_unknown UNKNOWN IMPORTED)\n"
        "set_target_properties(imported_unknown PROPERTIES IMPORTED_LOCATION imports/libunknown.a)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    local_real_id = bm_query_target_by_name(model, nob_sv_from_cstr("local_real"));
    local_alias_id = bm_query_target_by_name(model, nob_sv_from_cstr("local_alias"));
    imported_local_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_local"));
    imported_local_alias_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_local_alias"));
    imported_global_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_global"));
    imported_global_alias_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_global_alias"));
    imported_unknown_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_unknown"));
    ASSERT(local_real_id != BM_TARGET_ID_INVALID);
    ASSERT(local_alias_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_local_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_local_alias_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_global_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_global_alias_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_unknown_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_target_kind(model, local_real_id) == BM_TARGET_STATIC_LIBRARY);
    ASSERT(bm_query_target_kind(model, local_alias_id) == BM_TARGET_STATIC_LIBRARY);
    ASSERT(bm_query_target_is_alias(model, local_alias_id));
    ASSERT(bm_query_target_alias_of(model, local_alias_id) == local_real_id);
    ASSERT(bm_query_target_is_alias_global(model, local_alias_id));
    ASSERT(!bm_query_target_is_imported(model, local_alias_id));

    ASSERT(bm_query_target_is_imported(model, imported_local_id));
    ASSERT(!bm_query_target_is_imported_global(model, imported_local_id));
    ASSERT(bm_query_target_kind(model, imported_local_alias_id) == BM_TARGET_STATIC_LIBRARY);
    ASSERT(bm_query_target_is_alias(model, imported_local_alias_id));
    ASSERT(bm_query_target_alias_of(model, imported_local_alias_id) == imported_local_id);
    ASSERT(!bm_query_target_is_alias_global(model, imported_local_alias_id));

    ASSERT(bm_query_target_is_imported(model, imported_global_id));
    ASSERT(bm_query_target_is_imported_global(model, imported_global_id));
    ASSERT(bm_query_target_kind(model, imported_global_alias_id) == BM_TARGET_STATIC_LIBRARY);
    ASSERT(bm_query_target_is_alias(model, imported_global_alias_id));
    ASSERT(bm_query_target_alias_of(model, imported_global_alias_id) == imported_global_id);
    ASSERT(bm_query_target_is_alias_global(model, imported_global_alias_id));

    ASSERT(bm_query_target_is_imported(model, imported_unknown_id));
    ASSERT(bm_query_target_kind(model, imported_unknown_id) == BM_TARGET_UNKNOWN_LIBRARY);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_query_session_reuses_effective_item_and_value_results) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *session_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Session *session = NULL;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_String_Item_Span include_items_a = {0};
    BM_String_Item_Span include_items_b = {0};
    BM_String_Span include_values_a = {0};
    BM_String_Span include_values_b = {0};
    const BM_Query_Session_Stats *stats = NULL;

    ASSERT(session_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "session_reuse_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("session_reuse_src");
    config.binary_dir = nob_sv_from_cstr("session_reuse_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE iface/include)\n"
        "target_compile_definitions(iface INTERFACE IFACE_DEF=1)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE iface)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    session = bm_query_session_create(session_arena, model);
    ASSERT(session != NULL);
    stats = bm_query_session_stats(session);
    ASSERT(stats != NULL);
    ASSERT(stats->effective_item_hits == 0);
    ASSERT(stats->effective_item_misses == 0);
    ASSERT(stats->effective_value_hits == 0);
    ASSERT(stats->effective_value_misses == 0);

    compile_ctx.current_target_id = app_id;
    compile_ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    compile_ctx.compile_language = nob_sv_from_cstr("C");
    compile_ctx.build_interface_active = true;
    compile_ctx.install_interface_active = false;

    ASSERT(bm_query_session_target_effective_include_directories_items(session, app_id, &compile_ctx, &include_items_a));
    ASSERT(stats->effective_item_hits == 0);
    ASSERT(stats->effective_item_misses == 1);
    ASSERT(build_model_string_item_span_contains_substring(include_items_a, "iface/include"));

    ASSERT(bm_query_session_target_effective_include_directories_items(session, app_id, &compile_ctx, &include_items_b));
    ASSERT(stats->effective_item_hits == 1);
    ASSERT(stats->effective_item_misses == 1);
    ASSERT(build_model_string_item_span_equals(include_items_a, include_items_b));

    ASSERT(bm_query_session_target_effective_include_directories(session, app_id, &compile_ctx, &include_values_a));
    ASSERT(stats->effective_value_hits == 0);
    ASSERT(stats->effective_value_misses == 1);
    ASSERT(build_model_string_span_contains_substring(include_values_a, "iface/include"));

    ASSERT(bm_query_session_target_effective_include_directories(session, app_id, &compile_ctx, &include_values_b));
    ASSERT(stats->effective_value_hits == 1);
    ASSERT(stats->effective_value_misses == 1);
    ASSERT(stats->effective_item_hits == 1);
    ASSERT(stats->effective_item_misses == 1);
    ASSERT(build_model_string_span_equals(include_values_a, include_values_b));

    arena_destroy(session_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_query_session_splits_effective_contexts_without_merging_semantics) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *session_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id tool_id = BM_TARGET_ID_INVALID;
    BM_Query_Session *session = NULL;
    BM_Query_Eval_Context app_debug_c_linux = {0};
    BM_Query_Eval_Context tool_debug_c_linux = {0};
    BM_Query_Eval_Context app_release_cxx_linux = {0};
    BM_Query_Eval_Context app_release_c_windows = {0};
    BM_Query_Eval_Context app_install = {0};
    BM_String_Span defs = {0};
    BM_String_Span includes = {0};
    const BM_Query_Session_Stats *stats = NULL;

    ASSERT(session_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "session_split_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("session_split_src");
    config.binary_dir = nob_sv_from_cstr("session_split_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:build/include>\"\n"
        "  \"$<INSTALL_INTERFACE:install/include>\")\n"
        "target_compile_definitions(iface INTERFACE\n"
        "  \"$<$<CONFIG:Debug>:CFG_DEBUG>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:C>:LANG_C>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:CXX>:LANG_CXX>\"\n"
        "  \"$<$<PLATFORM_ID:Linux>:PLATFORM_LINUX>\"\n"
        "  \"$<$<PLATFORM_ID:Windows>:PLATFORM_WINDOWS>\"\n"
        "  \"$<TARGET_PROPERTY:CUSTOM_TAG>\")\n"
        "add_executable(app main.c)\n"
        "set_target_properties(app PROPERTIES CUSTOM_TAG APP_TAG)\n"
        "target_link_libraries(app PRIVATE iface)\n"
        "add_executable(tool tool.c)\n"
        "set_target_properties(tool PROPERTIES CUSTOM_TAG TOOL_TAG)\n"
        "target_link_libraries(tool PRIVATE iface)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    tool_id = bm_query_target_by_name(model, nob_sv_from_cstr("tool"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(tool_id != BM_TARGET_ID_INVALID);

    session = bm_query_session_create(session_arena, model);
    ASSERT(session != NULL);
    stats = bm_query_session_stats(session);
    ASSERT(stats != NULL);

    app_debug_c_linux.current_target_id = app_id;
    app_debug_c_linux.usage_mode = BM_QUERY_USAGE_COMPILE;
    app_debug_c_linux.config = nob_sv_from_cstr("Debug");
    app_debug_c_linux.platform_id = nob_sv_from_cstr("Linux");
    app_debug_c_linux.compile_language = nob_sv_from_cstr("C");
    app_debug_c_linux.build_interface_active = true;
    app_debug_c_linux.install_interface_active = false;

    tool_debug_c_linux = app_debug_c_linux;
    tool_debug_c_linux.current_target_id = tool_id;

    app_release_cxx_linux = app_debug_c_linux;
    app_release_cxx_linux.config = nob_sv_from_cstr("");
    app_release_cxx_linux.compile_language = nob_sv_from_cstr("CXX");

    app_release_c_windows = app_debug_c_linux;
    app_release_c_windows.config = nob_sv_from_cstr("");
    app_release_c_windows.platform_id = nob_sv_from_cstr("Windows");

    app_install = app_debug_c_linux;
    app_install.config = nob_sv_from_cstr("");
    app_install.build_interface_active = false;
    app_install.install_interface_active = true;

    ASSERT(bm_query_session_target_effective_compile_definitions(session, app_id, &app_debug_c_linux, &defs));
    ASSERT(build_model_string_span_contains(defs, "CFG_DEBUG"));
    ASSERT(build_model_string_span_contains(defs, "LANG_C"));
    ASSERT(build_model_string_span_contains(defs, "PLATFORM_LINUX"));
    ASSERT(build_model_string_span_contains(defs, "APP_TAG"));
    ASSERT(!build_model_string_span_contains(defs, "TOOL_TAG"));
    ASSERT(!build_model_string_span_contains(defs, "LANG_CXX"));
    ASSERT(!build_model_string_span_contains(defs, "PLATFORM_WINDOWS"));

    ASSERT(bm_query_session_target_effective_compile_definitions(session, app_id, &app_debug_c_linux, &defs));

    ASSERT(bm_query_session_target_effective_compile_definitions(session, app_id, &tool_debug_c_linux, &defs));
    ASSERT(build_model_string_span_contains(defs, "TOOL_TAG"));
    ASSERT(!build_model_string_span_contains(defs, "APP_TAG"));

    ASSERT(bm_query_session_target_effective_compile_definitions(session, app_id, &app_release_cxx_linux, &defs));
    ASSERT(build_model_string_span_contains(defs, "LANG_CXX"));
    ASSERT(build_model_string_span_contains(defs, "PLATFORM_LINUX"));
    ASSERT(build_model_string_span_contains(defs, "APP_TAG"));
    ASSERT(!build_model_string_span_contains(defs, "CFG_DEBUG"));
    ASSERT(!build_model_string_span_contains(defs, "LANG_C"));

    ASSERT(bm_query_session_target_effective_compile_definitions(session, app_id, &app_release_c_windows, &defs));
    ASSERT(build_model_string_span_contains(defs, "LANG_C"));
    ASSERT(build_model_string_span_contains(defs, "PLATFORM_WINDOWS"));
    ASSERT(build_model_string_span_contains(defs, "APP_TAG"));
    ASSERT(!build_model_string_span_contains(defs, "PLATFORM_LINUX"));

    ASSERT(bm_query_session_target_effective_include_directories(session, app_id, &app_debug_c_linux, &includes));
    ASSERT(build_model_string_span_contains_substring(includes, "build/include"));
    ASSERT(!build_model_string_span_contains_substring(includes, "install/include"));

    ASSERT(bm_query_session_target_effective_include_directories(session, app_id, &app_install, &includes));
    ASSERT(!build_model_string_span_contains_substring(includes, "build/include"));
    ASSERT(build_model_string_span_contains_substring(includes, "install/include"));

    ASSERT(stats->effective_value_hits == 1);
    ASSERT(stats->effective_value_misses == 6);

    arena_destroy(session_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_query_session_memoizes_imported_target_resolution) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *session_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id ext_id = BM_TARGET_ID_INVALID;
    BM_Query_Session *session = NULL;
    BM_Query_Eval_Context default_ctx = {0};
    BM_Query_Eval_Context debug_ctx = {0};
    BM_Query_Eval_Context relwithdebinfo_ctx = {0};
    String_View effective_file = {0};
    String_View effective_linker_file = {0};
    BM_String_Span link_langs = {0};
    const BM_Query_Session_Stats *stats = NULL;

    ASSERT(session_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "session_imported_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("session_imported_src");
    config.binary_dir = nob_sv_from_cstr("session_imported_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.so\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.so\n"
        "  IMPORTED_IMPLIB_DEBUG imports/libdebug_link.so\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG \"CXX;C\")\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ext_id = bm_query_target_by_name(model, nob_sv_from_cstr("ext"));
    ASSERT(ext_id != BM_TARGET_ID_INVALID);

    session = bm_query_session_create(session_arena, model);
    ASSERT(session != NULL);
    stats = bm_query_session_stats(session);
    ASSERT(stats != NULL);

    default_ctx.current_target_id = ext_id;
    default_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    default_ctx.build_interface_active = true;
    default_ctx.install_interface_active = false;

    debug_ctx = default_ctx;
    debug_ctx.config = nob_sv_from_cstr("Debug");

    relwithdebinfo_ctx = default_ctx;
    relwithdebinfo_ctx.config = nob_sv_from_cstr("RelWithDebInfo");

    ASSERT(bm_query_session_target_effective_file(session, ext_id, &default_ctx, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("session_imported_src/imports/libbase.so")));

    ASSERT(bm_query_session_target_effective_file(session, ext_id, &default_ctx, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("session_imported_src/imports/libbase.so")));

    ASSERT(bm_query_session_target_effective_file(session, ext_id, &debug_ctx, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("session_imported_src/imports/libdebug.so")));

    ASSERT(bm_query_session_target_effective_file(session, ext_id, &relwithdebinfo_ctx, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("session_imported_src/imports/libdebug.so")));

    ASSERT(bm_query_session_target_effective_linker_file(session, ext_id, &debug_ctx, &effective_linker_file));
    ASSERT(nob_sv_eq(effective_linker_file, nob_sv_from_cstr("session_imported_src/imports/libdebug_link.so")));

    ASSERT(bm_query_session_target_effective_linker_file(session, ext_id, &debug_ctx, &effective_linker_file));
    ASSERT(nob_sv_eq(effective_linker_file, nob_sv_from_cstr("session_imported_src/imports/libdebug_link.so")));

    ASSERT(bm_query_session_target_imported_link_languages(session, ext_id, &debug_ctx, &link_langs));
    ASSERT(build_model_string_span_contains(link_langs, "CXX"));
    ASSERT(build_model_string_span_contains(link_langs, "C"));

    ASSERT(bm_query_session_target_imported_link_languages(session, ext_id, &debug_ctx, &link_langs));
    ASSERT(build_model_string_span_contains(link_langs, "CXX"));
    ASSERT(build_model_string_span_contains(link_langs, "C"));

    ASSERT(stats->target_file_hits == 2);
    ASSERT(stats->target_file_misses == 4);
    ASSERT(stats->imported_link_language_hits == 1);
    ASSERT(stats->imported_link_language_misses == 1);

    arena_destroy(session_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_compile_feature_catalog_and_effective_features_are_shared) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id core_c_id = BM_TARGET_ID_INVALID;
    BM_Target_Id core_cxx_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_String_Span features = {0};
    const BM_Compile_Feature_Info *c_restrict = bm_compile_feature_lookup(nob_sv_from_cstr("c_restrict"));
    const BM_Compile_Feature_Info *cxx_generic_lambdas =
        bm_compile_feature_lookup(nob_sv_from_cstr("cxx_generic_lambdas"));
    const BM_Compile_Feature_Info *cxx_std_20 = bm_compile_feature_lookup(nob_sv_from_cstr("cxx_std_20"));

    ASSERT(query_arena != NULL);
    ASSERT(c_restrict != NULL);
    ASSERT(cxx_generic_lambdas != NULL);
    ASSERT(cxx_std_20 != NULL);
    ASSERT(c_restrict->lang == BM_COMPILE_FEATURE_LANG_C);
    ASSERT(c_restrict->standard == 99);
    ASSERT(!c_restrict->meta);
    ASSERT(cxx_generic_lambdas->lang == BM_COMPILE_FEATURE_LANG_CXX);
    ASSERT(cxx_generic_lambdas->standard == 14);
    ASSERT(!cxx_generic_lambdas->meta);
    ASSERT(cxx_std_20->lang == BM_COMPILE_FEATURE_LANG_CXX);
    ASSERT(cxx_std_20->standard == 20);
    ASSERT(cxx_std_20->meta);
    ASSERT(nob_sv_eq(bm_compile_feature_lang_compile_var(BM_COMPILE_FEATURE_LANG_C), nob_sv_from_cstr("CMAKE_C_COMPILE_FEATURES")));
    ASSERT(nob_sv_eq(bm_compile_feature_lang_standard_prop(BM_COMPILE_FEATURE_LANG_CXX), nob_sv_from_cstr("CXX_STANDARD")));
    ASSERT(nob_sv_eq(bm_compile_feature_lang_standard_required_prop(BM_COMPILE_FEATURE_LANG_CXX),
                     nob_sv_from_cstr("CXX_STANDARD_REQUIRED")));
    ASSERT(nob_sv_eq(bm_compile_feature_lang_extensions_prop(BM_COMPILE_FEATURE_LANG_C), nob_sv_from_cstr("C_EXTENSIONS")));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "p3_features_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("p3_features_src");
    config.binary_dir = nob_sv_from_cstr("p3_features_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "add_library(core_c STATIC core.c)\n"
        "target_compile_features(core_c PRIVATE c_std_11)\n"
        "set_target_properties(core_c PROPERTIES\n"
        "  C_STANDARD 11\n"
        "  C_STANDARD_REQUIRED ON\n"
        "  C_EXTENSIONS OFF)\n"
        "add_library(core_cxx STATIC core.cpp)\n"
        "target_compile_features(core_cxx PRIVATE cxx_std_20)\n"
        "set_target_properties(core_cxx PROPERTIES\n"
        "  CXX_STANDARD 20\n"
        "  CXX_STANDARD_REQUIRED ON\n"
        "  CXX_EXTENSIONS OFF)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    core_c_id = bm_query_target_by_name(model, nob_sv_from_cstr("core_c"));
    core_cxx_id = bm_query_target_by_name(model, nob_sv_from_cstr("core_cxx"));
    ASSERT(core_c_id != BM_TARGET_ID_INVALID);
    ASSERT(core_cxx_id != BM_TARGET_ID_INVALID);

    compile_ctx.current_target_id = core_c_id;
    compile_ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    compile_ctx.build_interface_active = true;
    compile_ctx.install_interface_active = false;

    ASSERT(bm_query_target_effective_compile_features(model, core_c_id, &compile_ctx, query_arena, &features));
    ASSERT(build_model_string_span_contains(features, "c_std_11"));

    compile_ctx.current_target_id = core_cxx_id;
    ASSERT(bm_query_target_effective_compile_features(model, core_cxx_id, &compile_ctx, query_arena, &features));
    ASSERT(build_model_string_span_contains(features, "cxx_std_20"));

    ASSERT(nob_sv_eq(bm_query_target_c_standard(model, core_c_id), nob_sv_from_cstr("11")));
    ASSERT(bm_query_target_c_standard_required(model, core_c_id));
    ASSERT(!bm_query_target_c_extensions(model, core_c_id));
    ASSERT(nob_sv_eq(bm_query_target_cxx_standard(model, core_cxx_id), nob_sv_from_cstr("20")));
    ASSERT(bm_query_target_cxx_standard_required(model, core_cxx_id));
    ASSERT(!bm_query_target_cxx_extensions(model, core_cxx_id));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_effective_queries_dedup_and_preserve_first_occurrence) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_String_Item_Span include_items = {0};
    BM_String_Item_Span def_items = {0};
    BM_String_Item_Span opt_items = {0};
    BM_String_Item_Span link_dir_items = {0};
    BM_String_Item_Span link_opt_items = {0};
    BM_String_Item_Span link_lib_items = {0};
    BM_String_Span feature_items = {0};
    ASSERT(query_arena != NULL);

    test_semantic_pipeline_config_init(&config);
    config.current_file = "dedup_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("dedup_query_src");
    config.binary_dir = nob_sv_from_cstr("dedup_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE dedup_inc ./dedup_inc second_inc)\n"
        "target_compile_definitions(iface INTERFACE FIRST=1 SECOND=2 FIRST=1)\n"
        "target_compile_options(iface INTERFACE -Wall -Winvalid-pch -Wall)\n"
        "target_compile_features(iface INTERFACE c_std_11 c_std_11)\n"
        "target_link_directories(iface INTERFACE dedup_lib ./dedup_lib second_lib)\n"
        "target_link_options(iface INTERFACE -Wl,--as-needed -Wl,-z,defs -Wl,--as-needed)\n"
        "target_link_libraries(iface INTERFACE m pthread m)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE iface)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    compile_ctx.current_target_id = app_id;
    compile_ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    compile_ctx.compile_language = nob_sv_from_cstr("C");
    compile_ctx.build_interface_active = true;
    compile_ctx.install_interface_active = false;

    link_ctx.current_target_id = app_id;
    link_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    link_ctx.build_interface_active = true;
    link_ctx.install_interface_active = false;

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &compile_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(include_items.count == 2);
    ASSERT(build_model_string_item_contains_at(include_items, 0, "dedup_inc"));
    ASSERT(build_model_string_item_contains_at(include_items, 1, "second_inc"));

    ASSERT(bm_query_target_effective_compile_definitions_items_with_context(model,
                                                                            app_id,
                                                                            &compile_ctx,
                                                                            query_arena,
                                                                            &def_items));
    ASSERT(def_items.count == 2);
    ASSERT(build_model_string_item_equals_at(def_items, 0, "FIRST=1"));
    ASSERT(build_model_string_item_equals_at(def_items, 1, "SECOND=2"));

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &compile_ctx,
                                                                        query_arena,
                                                                        &opt_items));
    ASSERT(opt_items.count == 2);
    ASSERT(build_model_string_item_equals_at(opt_items, 0, "-Wall"));
    ASSERT(build_model_string_item_equals_at(opt_items, 1, "-Winvalid-pch"));

    ASSERT(bm_query_target_effective_compile_features(model, app_id, &compile_ctx, query_arena, &feature_items));
    ASSERT(feature_items.count == 1);
    ASSERT(build_model_string_equals_at(feature_items, 0, "c_std_11"));

    ASSERT(bm_query_target_effective_link_directories_items_with_context(model,
                                                                         app_id,
                                                                         &link_ctx,
                                                                         query_arena,
                                                                         &link_dir_items));
    ASSERT(link_dir_items.count == 2);
    ASSERT(build_model_string_item_contains_at(link_dir_items, 0, "dedup_lib"));
    ASSERT(build_model_string_item_contains_at(link_dir_items, 1, "second_lib"));

    ASSERT(bm_query_target_effective_link_options_items_with_context(model,
                                                                     app_id,
                                                                     &link_ctx,
                                                                     query_arena,
                                                                     &link_opt_items));
    ASSERT(link_opt_items.count == 2);
    ASSERT(build_model_string_item_equals_at(link_opt_items, 0, "-Wl,--as-needed"));
    ASSERT(build_model_string_item_equals_at(link_opt_items, 1, "-Wl,-z,defs"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_lib_items));
    ASSERT(link_lib_items.count == 3);
    ASSERT(build_model_string_item_equals_at(link_lib_items, 0, "iface"));
    ASSERT(build_model_string_item_equals_at(link_lib_items, 1, "m"));
    ASSERT(build_model_string_item_equals_at(link_lib_items, 2, "pthread"));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_install_and_export_queries_surface_typed_metadata) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(256 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id_Span export_targets = {0};
    ASSERT(query_arena != NULL);

    ASSERT(build_model_write_text_file("install_export_src/src/core.c", "int core_value(void) { return 42; }\n"));
    ASSERT(build_model_write_text_file("install_export_src/include/core.h", "int core_value(void);\n"));
    ASSERT(build_model_write_text_file("install_export_src/cmake/DemoConfig.cmake",
                                       "include(\"${CMAKE_CURRENT_LIST_DIR}/DemoTargets.cmake\")\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "install_export_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("install_export_src");
    config.binary_dir = nob_sv_from_cstr("install_export_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(core STATIC src/core.c)\n"
        "add_library(Demo::core ALIAS core)\n"
        "target_include_directories(core PUBLIC\n"
        "  \"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\"\n"
        "  \"$<INSTALL_INTERFACE:include>\")\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "install(TARGETS core EXPORT DemoTargets\n"
        "  ARCHIVE DESTINATION lib\n"
        "  LIBRARY DESTINATION lib\n"
        "  RUNTIME DESTINATION bin\n"
        "  INCLUDES DESTINATION include\n"
        "  PUBLIC_HEADER DESTINATION include/demo\n"
        "  COMPONENT Development)\n"
        "install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo COMPONENT Development)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT Development)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    ASSERT(core_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_install_rule_count(model) == 2);
    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)0) == BM_INSTALL_RULE_TARGET);
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("Development")));
    ASSERT(nob_sv_eq(bm_query_install_rule_export_name(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(bm_query_install_rule_library_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(bm_query_install_rule_runtime_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("bin")));
    ASSERT(nob_sv_eq(bm_query_install_rule_includes_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("include")));
    ASSERT(nob_sv_eq(bm_query_install_rule_public_header_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("include/demo")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)0) == core_id);

    ASSERT(bm_query_export_count(model) == 1);
    ASSERT(nob_sv_eq(bm_query_export_name(model, (BM_Export_Id)0), nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_export_namespace(model, (BM_Export_Id)0), nob_sv_from_cstr("Demo::")));
    ASSERT(nob_sv_eq(bm_query_export_destination(model, (BM_Export_Id)0), nob_sv_from_cstr("lib/cmake/demo")));
    ASSERT(nob_sv_eq(bm_query_export_file_name(model, (BM_Export_Id)0), nob_sv_from_cstr("DemoTargets.cmake")));
    ASSERT(nob_sv_eq(bm_query_export_component(model, (BM_Export_Id)0), nob_sv_from_cstr("Development")));
    ASSERT(nob_sv_eq(bm_query_export_output_file_path(model, (BM_Export_Id)0, query_arena),
                     nob_sv_from_cstr("lib/cmake/demo/DemoTargets.cmake")));

    export_targets = bm_query_export_targets(model, (BM_Export_Id)0);
    ASSERT(export_targets.count == 1);
    ASSERT(export_targets.items[0] == core_id);

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_install_queries_materialize_effective_default_components) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;

    ASSERT(build_model_write_text_file("install_component_src/core.c", "int core_value(void) { return 5; }\n"));
    ASSERT(build_model_write_text_file("install_component_src/main.c",
                                       "int core_value(void);\n"
                                       "int main(void) { return core_value() == 5 ? 0 : 1; }\n"));
    ASSERT(build_model_write_text_file("install_component_src/notice.txt", "notice\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "install_component_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("install_component_src");
    config.binary_dir = nob_sv_from_cstr("install_component_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Toolkit)\n"
        "add_library(core STATIC core.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "install(TARGETS app DESTINATION bin)\n"
        "install(FILES notice.txt DESTINATION share)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib COMPONENT Development)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(core_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_install_rule_count(model) == 3);
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)0) == app_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("Toolkit")));
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("Toolkit")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)2) == core_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("Development")));

    ASSERT(bm_query_export_count(model) == 1);
    ASSERT(nob_sv_eq(bm_query_export_component(model, (BM_Export_Id)0),
                     nob_sv_from_cstr("Toolkit")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_standalone_export_queries_cover_build_tree_and_package_registry) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id_Span export_targets = {0};

    ASSERT(build_model_write_text_file("standalone_export_src/core.c", "int core_value(void) { return 41; }\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "standalone_export_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("standalone_export_src");
    config.binary_dir = nob_sv_from_cstr("standalone_export_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "install(TARGETS core EXPORT DemoTargets DESTINATION lib)\n"
        "export(TARGETS core FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/StandaloneTargets.cmake NAMESPACE Demo::)\n"
        "export(EXPORT DemoTargets FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/InstallSetTargets.cmake NAMESPACE Demo::)\n"
        "cmake_policy(SET CMP0090 NEW)\n"
        "set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)\n"
        "export(PACKAGE DemoPkg)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    ASSERT(core_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_export_count(model) == 3);

    ASSERT(bm_query_export_kind(model, (BM_Export_Id)0) == BM_EXPORT_BUILD_TREE);
    ASSERT(bm_query_export_source_kind(model, (BM_Export_Id)0) == BM_EXPORT_SOURCE_TARGETS);
    ASSERT(nob_sv_eq(bm_query_export_name(model, (BM_Export_Id)0), nob_sv_from_cstr("StandaloneTargets")));
    ASSERT(nob_sv_eq(bm_query_export_namespace(model, (BM_Export_Id)0), nob_sv_from_cstr("Demo::")));
    ASSERT(build_model_sv_contains(bm_query_export_output_file_path(model, (BM_Export_Id)0, fixture.scratch_arena),
                                   nob_sv_from_cstr("exports/StandaloneTargets.cmake")));
    export_targets = bm_query_export_targets(model, (BM_Export_Id)0);
    ASSERT(export_targets.count == 1);
    ASSERT(export_targets.items[0] == core_id);

    ASSERT(bm_query_export_kind(model, (BM_Export_Id)1) == BM_EXPORT_BUILD_TREE);
    ASSERT(bm_query_export_source_kind(model, (BM_Export_Id)1) == BM_EXPORT_SOURCE_EXPORT_SET);
    ASSERT(nob_sv_eq(bm_query_export_name(model, (BM_Export_Id)1), nob_sv_from_cstr("DemoTargets")));
    ASSERT(build_model_sv_contains(bm_query_export_output_file_path(model, (BM_Export_Id)1, fixture.scratch_arena),
                                   nob_sv_from_cstr("exports/InstallSetTargets.cmake")));
    export_targets = bm_query_export_targets(model, (BM_Export_Id)1);
    ASSERT(export_targets.count == 1);
    ASSERT(export_targets.items[0] == core_id);

    ASSERT(bm_query_export_kind(model, (BM_Export_Id)2) == BM_EXPORT_PACKAGE_REGISTRY);
    ASSERT(bm_query_export_source_kind(model, (BM_Export_Id)2) == BM_EXPORT_SOURCE_PACKAGE);
    ASSERT(bm_query_export_enabled(model, (BM_Export_Id)2));
    ASSERT(nob_sv_eq(bm_query_export_package_name(model, (BM_Export_Id)2), nob_sv_from_cstr("DemoPkg")));
    ASSERT(build_model_sv_contains(bm_query_export_registry_prefix(model, (BM_Export_Id)2),
                                   nob_sv_from_cstr("standalone_export_build")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_package_queries_surface_component_associations) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_CPack_Install_Type_Id full_id = BM_CPACK_INSTALL_TYPE_ID_INVALID;
    BM_CPack_Component_Group_Id base_group_id = BM_CPACK_COMPONENT_GROUP_ID_INVALID;
    BM_CPack_Component_Id runtime_id = BM_CPACK_COMPONENT_ID_INVALID;
    BM_CPack_Component_Id development_id = BM_CPACK_COMPONENT_ID_INVALID;
    BM_Install_Rule_Id_Span runtime_rules = {0};
    BM_Install_Rule_Id_Span development_rules = {0};
    BM_Export_Id_Span development_exports = {0};

    ASSERT(build_model_write_text_file("package_query_src/core.c", "int core_value(void) { return 9; }\n"));
    ASSERT(build_model_write_text_file("package_query_src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("package_query_src/include/core.h", "#define CORE_VALUE 9\n"));
    ASSERT(build_model_write_text_file("package_query_src/docs/runtime.txt", "runtime\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "package_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("package_query_src");
    config.binary_dir = nob_sv_from_cstr("package_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "include(CPackComponent)\n"
        "cpack_add_install_type(Full DISPLAY_NAME \"Full Install\")\n"
        "cpack_add_component_group(base DISPLAY_NAME \"Base\")\n"
        "cpack_add_component(Runtime GROUP base INSTALL_TYPES Full)\n"
        "cpack_add_component(Development GROUP base DEPENDS Runtime INSTALL_TYPES Full)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "add_executable(app main.c)\n"
        "install(TARGETS app RUNTIME DESTINATION bin COMPONENT Runtime)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo COMPONENT Development)\n"
        "install(FILES docs/runtime.txt DESTINATION share/runtime COMPONENT Runtime)\n"
        "install(EXPORT DemoTargets DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT Development)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    full_id = bm_query_cpack_install_type_by_name(model, nob_sv_from_cstr("Full"));
    base_group_id = bm_query_cpack_component_group_by_name(model, nob_sv_from_cstr("base"));
    runtime_id = bm_query_cpack_component_by_name(model, nob_sv_from_cstr("Runtime"));
    development_id = bm_query_cpack_component_by_name(model, nob_sv_from_cstr("Development"));

    ASSERT(full_id != BM_CPACK_INSTALL_TYPE_ID_INVALID);
    ASSERT(base_group_id != BM_CPACK_COMPONENT_GROUP_ID_INVALID);
    ASSERT(runtime_id != BM_CPACK_COMPONENT_ID_INVALID);
    ASSERT(development_id != BM_CPACK_COMPONENT_ID_INVALID);
    ASSERT(nob_sv_eq(bm_query_cpack_install_type_display_name(model, full_id), nob_sv_from_cstr("Full Install")));
    ASSERT(nob_sv_eq(bm_query_cpack_component_group_name(model, base_group_id), nob_sv_from_cstr("base")));
    ASSERT(bm_query_cpack_component_group(model, runtime_id) == base_group_id);
    ASSERT(bm_query_cpack_component_group(model, development_id) == base_group_id);
    ASSERT(bm_query_cpack_component_dependencies(model, development_id).count == 1);
    ASSERT(bm_query_cpack_component_dependencies(model, development_id).items[0] == runtime_id);
    ASSERT(bm_query_cpack_component_install_types(model, runtime_id).count == 1);
    ASSERT(bm_query_cpack_component_install_types(model, runtime_id).items[0] == full_id);

    runtime_rules = bm_query_cpack_component_install_rules(model, runtime_id, fixture.scratch_arena);
    development_rules = bm_query_cpack_component_install_rules(model, development_id, fixture.scratch_arena);
    development_exports = bm_query_cpack_component_exports(model, development_id, fixture.scratch_arena);

    ASSERT(runtime_rules.count == 2);
    ASSERT(development_rules.count == 1);
    ASSERT(development_exports.count == 1);
    ASSERT(bm_query_install_rule_kind(model, runtime_rules.items[0]) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_kind(model, runtime_rules.items[1]) == BM_INSTALL_RULE_FILE);
    ASSERT(bm_query_install_rule_kind(model, development_rules.items[0]) == BM_INSTALL_RULE_TARGET);
    ASSERT(nob_sv_eq(bm_query_export_name(model, development_exports.items[0]), nob_sv_from_cstr("DemoTargets")));
    ASSERT(bm_query_install_rule_for_export_target(model,
                                                   development_exports.items[0],
                                                   bm_query_target_by_name(model, nob_sv_from_cstr("core"))) != BM_INSTALL_RULE_ID_INVALID);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_cpack_package_queries_surface_generation_plan) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_String_Span generators = {0};
    BM_String_Span components_all = {0};
    String_View output_dir = {0};

    test_semantic_pipeline_config_init(&config);
    config.current_file = "cpack_plan_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("cpack_plan_src");
    config.binary_dir = nob_sv_from_cstr("cpack_plan_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(PackMe VERSION 3.5.1 LANGUAGES C)\n"
        "include(CPackComponent)\n"
        "cpack_add_component(Runtime)\n"
        "set(CPACK_GENERATOR \"TGZ;ZIP\")\n"
        "set(CPACK_PACKAGE_DIRECTORY packages/out)\n"
        "set(CPACK_PACKAGE_FILE_NAME PackMe-custom)\n"
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)\n"
        "set(CPACK_COMPONENTS_ALL Runtime)\n"
        "include(CPack)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_cpack_package_count(model) == 1);
    ASSERT(nob_sv_eq(bm_query_cpack_package_name(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("PackMe")));
    ASSERT(nob_sv_eq(bm_query_cpack_package_version(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("3.5.1")));
    ASSERT(nob_sv_eq(bm_query_cpack_package_file_name(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("PackMe-custom")));
    ASSERT(!bm_query_cpack_package_include_toplevel_directory(model, (BM_CPack_Package_Id)0));
    ASSERT(!bm_query_cpack_package_archive_component_install(model, (BM_CPack_Package_Id)0));

    output_dir = bm_query_cpack_package_output_directory(model, (BM_CPack_Package_Id)0, fixture.scratch_arena);
    ASSERT(build_model_sv_contains(output_dir, nob_sv_from_cstr("cpack_plan_build/packages/out")));

    generators = bm_query_cpack_package_generators(model, (BM_CPack_Package_Id)0);
    components_all = bm_query_cpack_package_components_all(model, (BM_CPack_Package_Id)0);
    ASSERT(generators.count == 2);
    ASSERT(build_model_string_equals_at(generators, 0, "TGZ"));
    ASSERT(build_model_string_equals_at(generators, 1, "ZIP"));
    ASSERT(components_all.count == 1);
    ASSERT(build_model_string_equals_at(components_all, 0, "Runtime"));

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
    test_build_model_generated_sources_rebase_to_binary_dir_and_link_producer_steps(passed, failed, skipped);
    test_build_model_build_steps_classify_target_producer_and_file_dependencies_and_preserve_hooks(passed, failed, skipped);
    test_build_model_resolves_direct_and_binary_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_source_and_stripped_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_byproduct_producers_and_keeps_unresolved_file_dependencies(passed, failed, skipped);
    test_build_model_marks_generated_sources_without_producer_steps(passed, failed, skipped);
    test_build_model_freeze_rejects_duplicate_effective_producers_and_execution_cycles(passed, failed, skipped);
    test_build_model_replay_actions_freeze_query_and_preserve_order(passed, failed, skipped);
    test_build_model_replay_actions_reject_invalid_opcode_payload_shapes(passed, failed, skipped);
    test_build_model_tests_freeze_owner_working_dir_expand_lists_and_configurations(passed, failed, skipped);
    test_build_model_replay_actions_accept_c3_opcodes_and_queries(passed, failed, skipped);
    test_build_model_replay_actions_accept_c5_ctest_coverage_and_memcheck_queries(passed, failed, skipped);
    test_build_model_replay_actions_reject_malformed_ordering(passed, failed, skipped);
    test_build_model_context_aware_queries_expand_usage_requirements_and_target_property_genex(passed, failed, skipped);
    test_build_model_platform_context_and_typed_platform_properties_are_queryable(passed, failed, skipped);
    test_build_model_imported_target_queries_resolve_configs_and_mapped_locations(passed, failed, skipped);
    test_build_model_preserves_imported_global_across_property_orderings(passed, failed, skipped);
    test_build_model_alias_and_unknown_target_identity_queries_are_canonical(passed, failed, skipped);
    test_build_model_query_session_reuses_effective_item_and_value_results(passed, failed, skipped);
    test_build_model_query_session_splits_effective_contexts_without_merging_semantics(passed, failed, skipped);
    test_build_model_query_session_memoizes_imported_target_resolution(passed, failed, skipped);
    test_build_model_compile_feature_catalog_and_effective_features_are_shared(passed, failed, skipped);
    test_build_model_effective_queries_dedup_and_preserve_first_occurrence(passed, failed, skipped);
    test_build_model_install_and_export_queries_surface_typed_metadata(passed, failed, skipped);
    test_build_model_install_queries_materialize_effective_default_components(passed, failed, skipped);
    test_build_model_standalone_export_queries_cover_build_tree_and_package_registry(passed, failed, skipped);
    test_build_model_package_queries_surface_component_associations(passed, failed, skipped);
    test_build_model_cpack_package_queries_surface_generation_plan(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "build-model suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
