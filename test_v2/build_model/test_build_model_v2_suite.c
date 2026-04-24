#include "test_v2_assert.h"
#include "test_fs.h"
#include "test_semantic_pipeline.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "build_model_query.h"

#include <ctype.h>
#include <sys/stat.h>

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

static bool build_model_make_executable(const char *path) {
    return path && chmod(path, 0755) == 0;
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

static bool build_model_target_id_span_contains(BM_Target_Id_Span span, BM_Target_Id needle) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i] == needle) return true;
    }
    return false;
}

static bool build_model_build_order_span_contains_target(BM_Build_Order_Node_Span span, BM_Target_Id needle) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i].kind == BM_BUILD_ORDER_NODE_TARGET && span.items[i].target_id == needle) return true;
    }
    return false;
}

static bool build_model_build_order_span_contains_step(BM_Build_Order_Node_Span span, BM_Build_Step_Id needle) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i].kind == BM_BUILD_ORDER_NODE_STEP && span.items[i].step_id == needle) return true;
    }
    return false;
}

static bool build_model_string_span_contains_ci(BM_String_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i].count != needle_sv.count) continue;
        {
            bool same = true;
            for (size_t j = 0; j < needle_sv.count; ++j) {
                if (tolower((unsigned char)span.items[i].data[j]) !=
                    tolower((unsigned char)needle_sv.data[j])) {
                    same = false;
                    break;
                }
            }
            if (same) return true;
        }
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

static bool build_model_string_contains_at(BM_String_Span span, size_t index, const char *needle) {
    return index < span.count && build_model_sv_contains(span.items[index], nob_sv_from_cstr(needle ? needle : ""));
}

static bool build_model_string_item_contains_at(BM_String_Item_Span span, size_t index, const char *needle) {
    return index < span.count && build_model_sv_contains(span.items[index].value, nob_sv_from_cstr(needle ? needle : ""));
}

static bool build_model_string_item_has_flag(BM_String_Item_Span span, const char *needle, BM_Item_Flags flag) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (!nob_sv_eq(span.items[i].value, needle_sv)) continue;
        if ((span.items[i].flags & flag) != 0) return true;
    }
    return false;
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

static size_t build_model_count_string_occurrences(BM_String_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    size_t count = 0;
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i], needle_sv)) count++;
    }
    return count;
}

static size_t build_model_count_string_item_occurrences(BM_String_Item_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    size_t count = 0;
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i].value, needle_sv)) count++;
    }
    return count;
}

static bool build_model_link_item_span_contains(BM_Link_Item_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i].value, needle_sv)) return true;
    }
    return false;
}

static bool build_model_link_item_equals_at(BM_Link_Item_Span span, size_t index, const char *expected) {
    return index < span.count && nob_sv_eq(span.items[index].value, nob_sv_from_cstr(expected ? expected : ""));
}

static size_t build_model_count_link_item_occurrences(BM_Link_Item_Span span, const char *needle) {
    String_View needle_sv = nob_sv_from_cstr(needle ? needle : "");
    size_t count = 0;
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_eq(span.items[i].value, needle_sv)) count++;
    }
    return count;
}

static BM_Replay_Action_Id build_model_find_replay_action_by_output_and_content(const Build_Model *model,
                                                                                BM_Replay_Opcode opcode,
                                                                                const char *output_path,
                                                                                const char *argv_substring) {
    String_View output_sv = nob_sv_from_cstr(output_path ? output_path : "");
    String_View argv_sv = nob_sv_from_cstr(argv_substring ? argv_substring : "");
    size_t count = bm_query_replay_action_count(model);
    for (size_t i = 0; i < count; ++i) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)i;
        BM_String_Span outputs = {0};
        BM_String_Span argv = {0};
        if (bm_query_replay_action_opcode(model, id) != opcode) continue;
        outputs = bm_query_replay_action_outputs(model, id);
        argv = bm_query_replay_action_argv(model, id);
        if (outputs.count == 0 || !build_model_sv_contains(outputs.items[0], output_sv)) continue;
        if (argv.count == 0 || !build_model_sv_contains(argv.items[0], argv_sv)) continue;
        return id;
    }
    return BM_REPLAY_ACTION_ID_INVALID;
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
    BM_Link_Item_Span raw_link_libs = bm_query_target_link_libraries_raw(model, app_id);
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

TEST(build_model_build_step_effective_view_resolves_context_and_target_artifacts) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id tool_id = BM_TARGET_ID_INVALID;
    BM_Build_Step_Id step_id = BM_BUILD_STEP_ID_INVALID;
    BM_Query_Eval_Context debug_linux = {0};
    BM_Build_Step_Effective_View view = {0};
    BM_String_Span argv = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "row54_effective_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row54_effective_src");
    config.binary_dir = nob_sv_from_cstr("row54_effective_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row54 C)\n"
        "add_executable(tool tool.c)\n"
        "set_target_properties(tool PROPERTIES\n"
        "  RUNTIME_OUTPUT_DIRECTORY bin\n"
        "  RUNTIME_OUTPUT_NAME \"$<IF:$<CONFIG:Debug>,tool_dbg,tool_rel>\")\n"
        "add_custom_command(\n"
        "  OUTPUT \"generated/$<CONFIG>-$<PLATFORM_ID>.c\"\n"
        "  COMMAND tool --mode \"$<IF:$<PLATFORM_ID:Linux>,linux,other>\" \"$<TARGET_FILE:tool>\"\n"
        "  DEPENDS \"schema-$<CONFIG>.idl\"\n"
        "  BYPRODUCTS \"logs/$<CONFIG>.stamp\"\n"
        "  WORKING_DIRECTORY \"work/$<CONFIG>\"\n"
        "  DEPFILE \"deps/$<CONFIG>.d\"\n"
        "  COMMENT \"build $<CONFIG>\"\n"
        "  COMMAND_EXPAND_LISTS)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    tool_id = bm_query_target_by_name(model, nob_sv_from_cstr("tool"));
    ASSERT(tool_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_build_step_count(model) == 1);
    step_id = (BM_Build_Step_Id)0;

    debug_linux.config = nob_sv_from_cstr("Debug");
    debug_linux.platform_id = nob_sv_from_cstr("Linux");
    debug_linux.usage_mode = BM_QUERY_USAGE_COMPILE;
    debug_linux.current_target_id = BM_TARGET_ID_INVALID;
    debug_linux.build_interface_active = true;
    debug_linux.build_local_interface_active = true;

    ASSERT(bm_query_build_step_effective_view(model, step_id, &debug_linux, query_arena, &view));
    ASSERT(build_model_string_equals_at(view.outputs, 0, "row54_effective_build/generated/Debug-Linux.c"));
    ASSERT(build_model_string_equals_at(view.byproducts, 0, "row54_effective_build/logs/Debug.stamp"));
    ASSERT(build_model_string_equals_at(view.file_dependencies, 0, "row54_effective_src/schema-Debug.idl"));
    ASSERT(build_model_sv_contains(view.working_directory, nob_sv_from_cstr("work/Debug")));
    ASSERT(build_model_sv_contains(view.depfile, nob_sv_from_cstr("deps/Debug.d")));
    ASSERT(nob_sv_eq(view.comment, nob_sv_from_cstr("build Debug")));
    ASSERT(view.target_dependencies.count == 1);
    ASSERT(build_model_target_id_span_contains(view.target_dependencies, tool_id));

    ASSERT(bm_query_build_step_effective_command_argv(model, step_id, 0, &debug_linux, query_arena, &argv));
    ASSERT(argv.count == 4);
    ASSERT(build_model_string_contains_at(argv, 0, "row54_effective_build/bin/tool_dbg"));
    ASSERT(build_model_string_equals_at(argv, 1, "--mode"));
    ASSERT(build_model_string_equals_at(argv, 2, "linux"));
    ASSERT(build_model_string_contains_at(argv, 3, "row54_effective_build/bin/tool_dbg"));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_custom_command_append_merges_into_original_step_and_rejects_missing_base) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Test_Semantic_Pipeline_Fixture invalid_fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Query_Eval_Context ctx = {0};
    BM_Build_Step_Effective_View view = {0};
    BM_String_Span argv = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "row54_append_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row54_append_src");
    config.binary_dir = nob_sv_from_cstr("row54_append_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row54 C)\n"
        "add_custom_command(OUTPUT out.txt COMMAND sh -c \"printf base\" DEPENDS base.in)\n"
        "add_custom_command(OUTPUT out.txt APPEND COMMAND sh -c \"printf append\" DEPENDS append.in)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_build_step_count(model) == 1);
    ASSERT(bm_query_build_step_command_count(model, (BM_Build_Step_Id)0) == 2);

    ctx.config = nob_sv_from_cstr("Debug");
    ctx.platform_id = nob_sv_from_cstr("Linux");
    ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    ctx.build_interface_active = true;
    ctx.build_local_interface_active = true;

    ASSERT(bm_query_build_step_effective_view(model, (BM_Build_Step_Id)0, &ctx, query_arena, &view));
    ASSERT(view.outputs.count == 1);
    ASSERT(build_model_string_equals_at(view.outputs, 0, "row54_append_build/out.txt"));
    ASSERT(view.file_dependencies.count == 2);
    ASSERT(build_model_string_span_contains(view.file_dependencies, "row54_append_src/base.in"));
    ASSERT(build_model_string_span_contains(view.file_dependencies, "row54_append_src/append.in"));

    ASSERT(bm_query_build_step_effective_command_argv(model, (BM_Build_Step_Id)0, 0, &ctx, query_arena, &argv));
    ASSERT(build_model_string_equals_at(argv, 2, "printf base"));
    ASSERT(bm_query_build_step_effective_command_argv(model, (BM_Build_Step_Id)0, 1, &ctx, query_arena, &argv));
    ASSERT(build_model_string_equals_at(argv, 2, "printf append"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "row54_append_invalid_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row54_append_invalid_src");
    config.binary_dir = nob_sv_from_cstr("row54_append_invalid_build");
    ASSERT(test_semantic_pipeline_fixture_from_script(
        &invalid_fixture,
        "project(Row54 C)\n"
        "add_custom_command(OUTPUT missing.txt APPEND COMMAND echo bad)\n",
        &config));
    ASSERT(invalid_fixture.eval_run.report.error_count >= 1);

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    test_semantic_pipeline_fixture_destroy(&invalid_fixture);
    TEST_PASS();
}

TEST(build_model_known_configurations_include_build_step_genex_fields) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    BM_String_Span known_configs = {0};

    test_semantic_pipeline_config_init(&config);
    config.current_file = "row54_configs_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row54_configs_src");
    config.binary_dir = nob_sv_from_cstr("row54_configs_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row54 C)\n"
        "add_custom_command(\n"
        "  OUTPUT \"out/$<IF:$<CONFIG:Profile>,profile,other>.txt\"\n"
        "  COMMAND echo \"$<IF:$<CONFIG:Asan>,asan,other>\"\n"
        "  DEPENDS \"dep/$<IF:$<CONFIG:Coverage>,cov,other>.in\"\n"
        "  BYPRODUCTS \"by/$<IF:$<CONFIG:Tsan>,tsan,other>.txt\"\n"
        "  WORKING_DIRECTORY \"wd/$<IF:$<CONFIG:MinSizeRel>,min,other>\"\n"
        "  DEPFILE \"depfile/$<IF:$<CONFIG:RelWithDebInfo>,rel,other>.d\"\n"
        "  COMMENT \"comment $<IF:$<CONFIG:Debug>,dbg,other>\")\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    known_configs = bm_query_known_configurations(fixture.build.model);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Profile"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Asan"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Coverage"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Tsan"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "MinSizeRel"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "RelWithDebInfo"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Debug"));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_target_build_order_view_centralizes_explicit_steps_and_config_link_prereqs) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id prepare_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_prepare_id = BM_TARGET_ID_INVALID;
    BM_Target_Id debugdep_id = BM_TARGET_ID_INVALID;
    BM_Target_Id releasedep_id = BM_TARGET_ID_INVALID;
    BM_Build_Step_Id generated_step_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id pre_build_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id pre_link_id = BM_BUILD_STEP_ID_INVALID;
    BM_Build_Step_Id post_build_id = BM_BUILD_STEP_ID_INVALID;
    size_t generated_source_index = 0;
    BM_Query_Eval_Context debug_ctx = {0};
    BM_Query_Eval_Context release_ctx = {0};
    BM_Target_Build_Order_View debug_view = {0};
    BM_Target_Build_Order_View release_view = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "row55_order_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row55_order_src");
    config.binary_dir = nob_sv_from_cstr("row55_order_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row55 C)\n"
        "add_custom_target(prepare COMMAND ${CMAKE_COMMAND} -E echo prepare)\n"
        "add_custom_target(imported_prepare COMMAND ${CMAKE_COMMAND} -E echo imported)\n"
        "add_library(iface INTERFACE)\n"
        "add_dependencies(iface prepare)\n"
        "add_library(ext STATIC IMPORTED GLOBAL)\n"
        "set_target_properties(ext PROPERTIES IMPORTED_LOCATION \"${CMAKE_CURRENT_BINARY_DIR}/libext.a\")\n"
        "add_dependencies(ext imported_prepare)\n"
        "add_library(debugdep STATIC debugdep.c)\n"
        "add_library(releasedep STATIC releasedep.c)\n"
        "add_custom_command(OUTPUT generated.c COMMAND ${CMAKE_COMMAND} -E echo generated)\n"
        "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated.c)\n"
        "add_dependencies(app iface)\n"
        "target_link_libraries(app PRIVATE ext \"$<$<CONFIG:Debug>:debugdep>\" \"$<$<CONFIG:Release>:releasedep>\")\n"
        "add_custom_command(TARGET app PRE_BUILD COMMAND ${CMAKE_COMMAND} -E echo prebuild)\n"
        "add_custom_command(TARGET app PRE_LINK COMMAND ${CMAKE_COMMAND} -E echo prelink)\n"
        "add_custom_command(TARGET app POST_BUILD COMMAND ${CMAKE_COMMAND} -E echo postbuild)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    prepare_id = bm_query_target_by_name(model, nob_sv_from_cstr("prepare"));
    imported_prepare_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_prepare"));
    debugdep_id = bm_query_target_by_name(model, nob_sv_from_cstr("debugdep"));
    releasedep_id = bm_query_target_by_name(model, nob_sv_from_cstr("releasedep"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(prepare_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_prepare_id != BM_TARGET_ID_INVALID);
    ASSERT(debugdep_id != BM_TARGET_ID_INVALID);
    ASSERT(releasedep_id != BM_TARGET_ID_INVALID);

    generated_source_index = build_model_find_target_source_index_containing(model, app_id, "generated.c");
    ASSERT(generated_source_index < bm_query_target_source_count(model, app_id));
    generated_step_id = bm_query_target_source_producer_step(model, app_id, generated_source_index);
    pre_build_id = build_model_find_step_by_kind_and_owner(model, BM_BUILD_STEP_TARGET_PRE_BUILD, app_id);
    pre_link_id = build_model_find_step_by_kind_and_owner(model, BM_BUILD_STEP_TARGET_PRE_LINK, app_id);
    post_build_id = build_model_find_step_by_kind_and_owner(model, BM_BUILD_STEP_TARGET_POST_BUILD, app_id);
    ASSERT(generated_step_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(pre_build_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(pre_link_id != BM_BUILD_STEP_ID_INVALID);
    ASSERT(post_build_id != BM_BUILD_STEP_ID_INVALID);

    debug_ctx.current_target_id = app_id;
    debug_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    debug_ctx.config = nob_sv_from_cstr("Debug");
    debug_ctx.platform_id = nob_sv_from_cstr("Linux");
    debug_ctx.build_interface_active = true;
    debug_ctx.build_local_interface_active = true;
    release_ctx = debug_ctx;
    release_ctx.config = nob_sv_from_cstr("Release");

    ASSERT(bm_query_target_effective_build_order_view(model, app_id, &debug_ctx, query_arena, &debug_view));
    ASSERT(bm_query_target_effective_build_order_view(model, app_id, &release_ctx, query_arena, &release_view));

    ASSERT(debug_view.explicit_prerequisites.count == 1);
    ASSERT(build_model_build_order_span_contains_target(debug_view.explicit_prerequisites, prepare_id));
    ASSERT(build_model_build_order_span_contains_step(debug_view.generated_source_steps, generated_step_id));
    ASSERT(build_model_build_order_span_contains_step(debug_view.pre_build_steps, pre_build_id));
    ASSERT(build_model_build_order_span_contains_step(debug_view.pre_link_steps, pre_link_id));
    ASSERT(build_model_build_order_span_contains_step(debug_view.post_build_steps, post_build_id));
    ASSERT(build_model_build_order_span_contains_target(debug_view.link_prerequisites, imported_prepare_id));
    ASSERT(build_model_build_order_span_contains_target(debug_view.link_prerequisites, debugdep_id));
    ASSERT(!build_model_build_order_span_contains_target(debug_view.link_prerequisites, releasedep_id));
    ASSERT(build_model_build_order_span_contains_target(release_view.link_prerequisites, imported_prepare_id));
    ASSERT(!build_model_build_order_span_contains_target(release_view.link_prerequisites, debugdep_id));
    ASSERT(build_model_build_order_span_contains_target(release_view.link_prerequisites, releasedep_id));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_static_library_link_cycle_is_not_an_execution_order_cycle) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(256 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id a_id = BM_TARGET_ID_INVALID;
    BM_Target_Id b_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context ctx = {0};
    BM_Target_Build_Order_View a_view = {0};
    BM_Target_Build_Order_View b_view = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "row55_static_cycle_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row55_static_cycle_src");
    config.binary_dir = nob_sv_from_cstr("row55_static_cycle_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row55 C)\n"
        "add_library(a STATIC a.c)\n"
        "add_library(b STATIC b.c)\n"
        "target_link_libraries(a PRIVATE b)\n"
        "target_link_libraries(b PRIVATE a)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    a_id = bm_query_target_by_name(model, nob_sv_from_cstr("a"));
    b_id = bm_query_target_by_name(model, nob_sv_from_cstr("b"));
    ASSERT(a_id != BM_TARGET_ID_INVALID);
    ASSERT(b_id != BM_TARGET_ID_INVALID);

    ctx.current_target_id = a_id;
    ctx.usage_mode = BM_QUERY_USAGE_LINK;
    ctx.platform_id = nob_sv_from_cstr("Linux");
    ctx.build_interface_active = true;
    ctx.build_local_interface_active = true;
    ASSERT(bm_query_target_effective_build_order_view(model, a_id, &ctx, query_arena, &a_view));
    ctx.current_target_id = b_id;
    ASSERT(bm_query_target_effective_build_order_view(model, b_id, &ctx, query_arena, &b_view));
    ASSERT(a_view.link_prerequisites.count == 0);
    ASSERT(b_view.link_prerequisites.count == 0);

    arena_destroy(query_arena);
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
        "add_executable(app main.c)\n"
        "add_custom_target(prepare DEPENDS app)\n"
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

TEST(build_model_replay_action_resolved_operands_use_query_context) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    Arena *validate_arena = arena_create(512 * 1024);
    Arena *model_arena = arena_create(2 * 1024 * 1024);
    Arena *query_arena = arena_create(512 * 1024);
    Test_Semantic_Pipeline_Build_Result build = {0};
    Event_Stream *stream = NULL;
    Event ev = {0};
    const Build_Model *model = NULL;
    BM_Query_Eval_Context debug_linux = {0};
    BM_Query_Eval_Context release_windows = {0};
    BM_String_Span resolved = {0};
    BM_String_Span known_configs = {0};

    ASSERT(arena != NULL);
    ASSERT(validate_arena != NULL);
    ASSERT(model_arena != NULL);
    ASSERT(query_arena != NULL);

    stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    build_model_init_event(&ev, EVENT_DIRECTORY_ENTER, 1);
    ev.as.directory_enter.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_enter.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_DECLARE, 2);
    ev.as.replay_action_declare.action_key = nob_sv_from_cstr("cfg_replay");
    ev.as.replay_action_declare.action_kind = EVENT_REPLAY_ACTION_PROCESS;
    ev.as.replay_action_declare.opcode = EVENT_REPLAY_OPCODE_NONE;
    ev.as.replay_action_declare.phase = EVENT_REPLAY_PHASE_CONFIGURE;
    ev.as.replay_action_declare.working_directory = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_INPUT, 3);
    ev.as.replay_action_add_input.action_key = nob_sv_from_cstr("cfg_replay");
    ev.as.replay_action_add_input.path = nob_sv_from_cstr("in/$<IF:$<CONFIG:Debug>,debug,other>.txt");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_OUTPUT, 4);
    ev.as.replay_action_add_output.action_key = nob_sv_from_cstr("cfg_replay");
    ev.as.replay_action_add_output.path = nob_sv_from_cstr("out/$<IF:$<CONFIG:Release>,release,other>.txt");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ARGV, 5);
    ev.as.replay_action_add_argv.action_key = nob_sv_from_cstr("cfg_replay");
    ev.as.replay_action_add_argv.arg_index = 0;
    ev.as.replay_action_add_argv.value = nob_sv_from_cstr("mode=$<IF:$<PLATFORM_ID:Linux>,linux,other>");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_REPLAY_ACTION_ADD_ENV, 6);
    ev.as.replay_action_add_env.action_key = nob_sv_from_cstr("cfg_replay");
    ev.as.replay_action_add_env.key = nob_sv_from_cstr("CFG");
    ev.as.replay_action_add_env.value = nob_sv_from_cstr("$<CONFIG>");
    ASSERT(event_stream_push(stream, &ev));

    build_model_init_event(&ev, EVENT_DIRECTORY_LEAVE, 7);
    ev.as.directory_leave.source_dir = nob_sv_from_cstr(".");
    ev.as.directory_leave.binary_dir = nob_sv_from_cstr(".");
    ASSERT(event_stream_push(stream, &ev));

    ASSERT(test_semantic_pipeline_build_model_from_stream(arena, validate_arena, model_arena, stream, &build));
    ASSERT(build.builder_ok);
    ASSERT(build.validate_ok);
    ASSERT(build.freeze_ok);
    ASSERT(build.model != NULL);

    model = build.model;
    debug_linux.config = nob_sv_from_cstr("Debug");
    debug_linux.platform_id = nob_sv_from_cstr("Linux");
    debug_linux.usage_mode = BM_QUERY_USAGE_COMPILE;
    debug_linux.current_target_id = BM_TARGET_ID_INVALID;
    debug_linux.build_interface_active = true;
    debug_linux.build_local_interface_active = true;
    debug_linux.install_interface_active = false;

    release_windows = debug_linux;
    release_windows.config = nob_sv_from_cstr("Release");
    release_windows.platform_id = nob_sv_from_cstr("Windows");

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_INPUTS,
                                                    &debug_linux,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "in/debug.txt"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_OUTPUTS,
                                                    &debug_linux,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "out/other.txt"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_ARGV,
                                                    &debug_linux,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "mode=linux"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_ENVIRONMENT,
                                                    &debug_linux,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "CFG=Debug"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_INPUTS,
                                                    &release_windows,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "in/other.txt"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_OUTPUTS,
                                                    &release_windows,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "out/release.txt"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_ARGV,
                                                    &release_windows,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "mode=other"));

    ASSERT(bm_query_replay_action_resolved_operands(model,
                                                    (BM_Replay_Action_Id)0,
                                                    BM_REPLAY_OPERAND_ENVIRONMENT,
                                                    &release_windows,
                                                    query_arena,
                                                    &resolved));
    ASSERT(build_model_string_equals_at(resolved, 0, "CFG=Release"));

    known_configs = bm_query_known_configurations(model);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Debug"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Release"));

    arena_destroy(arena);
    arena_destroy(validate_arena);
    arena_destroy(model_arena);
    arena_destroy(query_arena);
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

TEST(build_model_ctest_memcheck_preserves_registered_test_command_surface) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Test_Id test_id = BM_TEST_ID_INVALID;
    String_View command = {0};
    String_View working_dir = {0};

    ASSERT(build_model_write_text_file("ctest_memcheck_surface_src/tools/test_runner.sh",
                                       "#!/bin/sh\nexit 0\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "ctest_memcheck_surface_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("ctest_memcheck_surface_src");
    config.binary_dir = nob_sv_from_cstr("ctest_memcheck_surface_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(NobDiffCtestExtended NONE)\n"
        "enable_testing()\n"
        "set(CTEST_SOURCE_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
        "set(CTEST_BINARY_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}\")\n"
        "file(MAKE_DIRECTORY \"${CTEST_BINARY_DIRECTORY}\")\n"
        "file(MAKE_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}/memcheck_work\")\n"
        "file(RELATIVE_PATH _source_from_build \"${CTEST_BINARY_DIRECTORY}\" \"${CTEST_SOURCE_DIRECTORY}\")\n"
        "set(CTEST_MEMORYCHECK_COMMAND \"/bin/sh\")\n"
        "set(CTEST_MEMORYCHECK_TYPE Generic)\n"
        "set(CTEST_MEMORYCHECK_COMMAND_OPTIONS \"${_source_from_build}/tools/test_runner.sh\")\n"
        "add_test(NAME pass\n"
        "  COMMAND /bin/sh \"${_source_from_build}/tools/test_runner.sh\" pass\n"
        "  WORKING_DIRECTORY \"${_source_from_build}/memcheck_work\")\n"
        "ctest_start(Experimental \"${CTEST_SOURCE_DIRECTORY}\" \"${CTEST_BINARY_DIRECTORY}\" QUIET)\n"
        "ctest_memcheck(APPEND QUIET)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.builder_ok);
    ASSERT(fixture.build.validate_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_test_count(model) == 1);

    test_id = (BM_Test_Id)0;
    command = bm_query_test_command(model, test_id);
    working_dir = bm_query_test_working_directory(model, test_id);

    ASSERT(nob_sv_eq(bm_query_test_name(model, test_id), nob_sv_from_cstr("pass")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr("/bin/sh")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr("tools/test_runner.sh")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr(" pass")));
    ASSERT(!build_model_sv_contains(command, nob_sv_from_cstr("cmake -P CMakeLists.txt")));
    ASSERT(build_model_sv_contains(working_dir, nob_sv_from_cstr("memcheck_work")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_ctest_local_memcheck_relative_command_surface) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Test_Id test_id = BM_TEST_ID_INVALID;
    BM_Replay_Action_Id memcheck_id = BM_REPLAY_ACTION_ID_INVALID;
    String_View command = {0};
    String_View working_dir = {0};
    BM_String_Span memcheck_argv = {0};

    ASSERT(build_model_write_text_file("ctest_local_memcheck_surface/source/src/main.c",
                                       "int main(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("ctest_local_memcheck_surface/source/src/net.c",
                                       "int net(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("ctest_local_memcheck_surface/source/tools/coverage.sh",
                                       "#!/bin/sh\n"
                                       "pwd > coverage.pwd\n"
                                       "printf 'coverage ok\\n'\n"
                                       "exit 0\n"));
    ASSERT(build_model_write_text_file("ctest_local_memcheck_surface/source/tools/test_runner.sh",
                                       "#!/bin/sh\n"
                                       "mode=\"$1\"\n"
                                       "pwd > \"test-${mode}.pwd\"\n"
                                       "printf '%s\\n' \"$mode\"\n"
                                       "exit 0\n"));
    ASSERT(build_model_write_text_file("ctest_local_memcheck_surface/source/tools/memcheck.sh",
                                       "#!/bin/sh\n"
                                       "printf '%s\\n' \"$*\" >> memcheck-args.log\n"
                                       "pwd >> memcheck-cwd.log\n"
                                       "while [ \"$#\" -gt 0 ] && [ \"$1\" != \"--\" ]; do shift; done\n"
                                       "if [ \"$#\" -gt 0 ]; then shift; fi\n"
                                       "\"$@\"\n"
                                       "exit $?\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "ctest_local_memcheck_surface/source/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("ctest_local_memcheck_surface/source");
    config.binary_dir = nob_sv_from_cstr("ctest_local_memcheck_surface/build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(NobDiffCtestExtended NONE)\n"
        "enable_testing()\n"
        "set(CTEST_SOURCE_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\")\n"
        "set(CTEST_BINARY_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}\")\n"
        "file(MAKE_DIRECTORY \"${CTEST_BINARY_DIRECTORY}\")\n"
        "file(RELATIVE_PATH _source_from_build \"${CTEST_BINARY_DIRECTORY}\" \"${CTEST_SOURCE_DIRECTORY}\")\n"
        "file(MAKE_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}/memcheck_work\")\n"
        "set(COVERAGE_COMMAND \"/bin/sh;${_source_from_build}/tools/coverage.sh\")\n"
        "set(CTEST_MEMORYCHECK_COMMAND \"/bin/sh\")\n"
        "set(CTEST_MEMORYCHECK_TYPE Generic)\n"
        "set(CTEST_MEMORYCHECK_COMMAND_OPTIONS \"${_source_from_build}/tools/memcheck.sh\")\n"
        "add_test(NAME pass\n"
        "  COMMAND /bin/sh \"${_source_from_build}/tools/test_runner.sh\" pass\n"
        "  WORKING_DIRECTORY \"${_source_from_build}/memcheck_work\")\n"
        "set_source_files_properties(\"${CTEST_SOURCE_DIRECTORY}/src/main.c\" PROPERTIES LABELS \"core;ui\")\n"
        "set_source_files_properties(\"${CTEST_SOURCE_DIRECTORY}/src/net.c\" PROPERTIES LABELS infra)\n"
        "ctest_start(Experimental \"${CTEST_SOURCE_DIRECTORY}\" \"${CTEST_BINARY_DIRECTORY}\" QUIET)\n"
        "ctest_coverage(LABELS core ui APPEND QUIET)\n"
        "ctest_memcheck(APPEND QUIET)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.builder_ok);
    ASSERT(fixture.build.validate_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ASSERT(bm_query_test_count(model) == 1);

    test_id = (BM_Test_Id)0;
    command = bm_query_test_command(model, test_id);
    working_dir = bm_query_test_working_directory(model, test_id);

    ASSERT(nob_sv_eq(bm_query_test_name(model, test_id), nob_sv_from_cstr("pass")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr("/bin/sh")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr("../source/tools/test_runner.sh")));
    ASSERT(build_model_sv_contains(command, nob_sv_from_cstr(" pass")));
    ASSERT(!build_model_sv_contains(command, nob_sv_from_cstr("cmake -P CMakeLists.txt")));
    ASSERT(build_model_sv_contains(working_dir, nob_sv_from_cstr("../source/memcheck_work")));

    for (size_t i = 0; i < bm_query_replay_action_count(model); ++i) {
        if (bm_query_replay_action_opcode(model, (BM_Replay_Action_Id)i) ==
            BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL) {
            memcheck_id = (BM_Replay_Action_Id)i;
            break;
        }
    }
    ASSERT(memcheck_id != BM_REPLAY_ACTION_ID_INVALID);

    memcheck_argv = bm_query_replay_action_argv(model, memcheck_id);
    ASSERT(memcheck_argv.count >= 16);
    ASSERT(build_model_string_equals_at(memcheck_argv, 12, "Generic"));
    ASSERT(build_model_string_equals_at(memcheck_argv, 13, "2"));
    ASSERT(build_model_string_equals_at(memcheck_argv, 14, "/bin/sh"));
    ASSERT(build_model_string_contains_at(memcheck_argv, 15, "source/tools/memcheck.sh"));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_ctest_external_project_query_preserves_post_memcheck_exists_surface) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Replay_Action_Id test_flag_id = BM_REPLAY_ACTION_ID_INVALID;
    BM_Replay_Action_Id memcheck_flag_id = BM_REPLAY_ACTION_ID_INVALID;
    BM_String_Span test_flag_argv = {0};
    BM_String_Span memcheck_flag_argv = {0};

    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/CMakeLists.txt",
                                       "cmake_minimum_required(VERSION 3.28)\n"
                                       "project(NobDiffCtestExtended NONE)\n"
                                       "enable_testing()\n"
                                       "add_test(NAME pass\n"
                                       "  COMMAND /bin/sh \"${CMAKE_CURRENT_SOURCE_DIR}/tools/test_runner.sh\" pass\n"
                                       "  WORKING_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/memcheck_work\")\n"
                                       "set_source_files_properties(\"${CMAKE_CURRENT_SOURCE_DIR}/src/main.c\" PROPERTIES LABELS \"core;ui\")\n"
                                       "set_source_files_properties(\"${CMAKE_CURRENT_SOURCE_DIR}/src/net.c\" PROPERTIES LABELS infra)\n"));
    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/src/main.c",
                                       "int main(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/src/net.c",
                                       "int net(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/tools/coverage.sh",
                                       "#!/bin/sh\n"
                                       "pwd > coverage.pwd\n"
                                       "printf 'coverage ok\\n'\n"
                                       "exit 0\n"));
    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/tools/test_runner.sh",
                                       "#!/bin/sh\n"
                                       "mode=\"$1\"\n"
                                       "pwd > \"test-${mode}.pwd\"\n"
                                       "printf '%s\\n' \"$mode\"\n"
                                       "exit 0\n"));
    ASSERT(build_model_write_text_file("ctest_query_surface/source/project_src/tools/memcheck.sh",
                                       "#!/bin/sh\n"
                                       "printf '%s\\n' \"$*\" >> memcheck-args.log\n"
                                       "pwd >> memcheck-cwd.log\n"
                                       "while [ \"$#\" -gt 0 ] && [ \"$1\" != \"--\" ]; do shift; done\n"
                                       "if [ \"$#\" -gt 0 ]; then shift; fi\n"
                                       "\"$@\"\n"
                                       "exit $?\n"));
    ASSERT(build_model_make_executable("ctest_query_surface/source/project_src/tools/coverage.sh"));
    ASSERT(build_model_make_executable("ctest_query_surface/source/project_src/tools/test_runner.sh"));
    ASSERT(build_model_make_executable("ctest_query_surface/source/project_src/tools/memcheck.sh"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "ctest_query_surface/source/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("ctest_query_surface/source");
    config.binary_dir = nob_sv_from_cstr("ctest_query_surface/build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CTEST_SOURCE_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/project_src\")\n"
        "set(CTEST_BINARY_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}\")\n"
        "set(CMAKE_GENERATOR \"Unix Makefiles\")\n"
        "set(CTEST_CMAKE_GENERATOR \"${CMAKE_GENERATOR}\")\n"
        "file(RELATIVE_PATH _source_from_build \"${CTEST_BINARY_DIRECTORY}\" \"${CTEST_SOURCE_DIRECTORY}\")\n"
        "file(MAKE_DIRECTORY \"${CTEST_SOURCE_DIRECTORY}/memcheck_work\")\n"
        "set(COVERAGE_COMMAND \"/bin/sh;${_source_from_build}/tools/coverage.sh\")\n"
        "set(CTEST_MEMORYCHECK_COMMAND \"${CTEST_SOURCE_DIRECTORY}/tools/memcheck.sh\")\n"
        "set(CTEST_MEMORYCHECK_TYPE Valgrind)\n"
        "ctest_empty_binary_directory(\"${CTEST_BINARY_DIRECTORY}\")\n"
        "ctest_start(Experimental \"${CTEST_SOURCE_DIRECTORY}\" \"${CTEST_BINARY_DIRECTORY}\" QUIET)\n"
        "ctest_configure(QUIET)\n"
        "ctest_build(QUIET)\n"
        "ctest_test(QUIET)\n"
        "ctest_coverage(LABELS core ui APPEND QUIET)\n"
        "ctest_memcheck(APPEND QUIET)\n"
        "set(_report \"${CTEST_BINARY_DIRECTORY}/__oracle/ctest_extended_report.txt\")\n"
        "if(EXISTS \"${CTEST_SOURCE_DIRECTORY}/memcheck_work/test-pass.pwd\")\n"
        "  file(APPEND \"${_report}\" \"TEST_WORKDIR_EXISTS=1\\n\")\n"
        "else()\n"
        "  file(APPEND \"${_report}\" \"TEST_WORKDIR_EXISTS=0\\n\")\n"
        "endif()\n"
        "if(EXISTS \"${CTEST_SOURCE_DIRECTORY}/memcheck_work/memcheck-args.log\")\n"
        "  file(APPEND \"${_report}\" \"MEMCHECK_ARGS_EXISTS=1\\n\")\n"
        "else()\n"
        "  file(APPEND \"${_report}\" \"MEMCHECK_ARGS_EXISTS=0\\n\")\n"
        "endif()\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.builder_ok);
    ASSERT(fixture.build.validate_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    test_flag_id = build_model_find_replay_action_by_output_and_content(
        model,
        BM_REPLAY_OPCODE_FS_APPEND_TEXT,
        "__oracle/ctest_extended_report.txt",
        "TEST_WORKDIR_EXISTS=");
    memcheck_flag_id = build_model_find_replay_action_by_output_and_content(
        model,
        BM_REPLAY_OPCODE_FS_APPEND_TEXT,
        "__oracle/ctest_extended_report.txt",
        "MEMCHECK_ARGS_EXISTS=");

    ASSERT(test_flag_id != BM_REPLAY_ACTION_ID_INVALID);
    ASSERT(memcheck_flag_id != BM_REPLAY_ACTION_ID_INVALID);

    test_flag_argv = bm_query_replay_action_argv(model, test_flag_id);
    memcheck_flag_argv = bm_query_replay_action_argv(model, memcheck_flag_id);
    ASSERT(test_flag_argv.count >= 1);
    ASSERT(memcheck_flag_argv.count >= 1);
    ASSERT(build_model_string_equals_at(test_flag_argv, 0, "TEST_WORKDIR_EXISTS=1\n"));
    ASSERT(build_model_string_equals_at(memcheck_flag_argv, 0, "MEMCHECK_ARGS_EXISTS=1\n"));

    test_semantic_pipeline_fixture_destroy(&fixture);
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
    BM_Link_Item_Span link_items = {0};
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
    ASSERT(build_model_link_item_span_contains(link_items, "m"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &compile_c,
                                                                       query_arena,
                                                                       &link_items));
    ASSERT(!build_model_link_item_span_contains(link_items, "m"));

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

    ASSERT(bm_query_target_raw_property_value(model,
                                              iface_id,
                                              nob_sv_from_cstr("CUSTOM_TAG"),
                                              query_arena,
                                              &property_value));
    ASSERT(nob_sv_eq(property_value, nob_sv_from_cstr("dbg-tag")));

    ASSERT(bm_query_target_modeled_property_value(model,
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

TEST(build_model_effective_queries_follow_global_directory_and_transitive_link_library_seeds) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_Query_Eval_Context install_ctx = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_String_Item_Span include_items = {0};
    BM_String_Item_Span def_items = {0};
    BM_String_Item_Span compile_opts = {0};
    BM_String_Span features = {0};
    BM_Link_Item_Span link_lib_items = {0};
    BM_String_Item_Span link_opt_items = {0};
    BM_String_Item_Span link_dir_items = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "seed_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("seed_query_src");
    config.binary_dir = nob_sv_from_cstr("seed_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(imported_iface INTERFACE IMPORTED)\n"
        "set_target_properties(imported_iface PROPERTIES\n"
        "  INTERFACE_INCLUDE_DIRECTORIES imported/include\n"
        "  INTERFACE_COMPILE_DEFINITIONS IMPORTED_DEF=1\n"
        "  INTERFACE_COMPILE_OPTIONS -DIMPORTED_OPT\n"
        "  INTERFACE_COMPILE_FEATURES c_std_11\n"
        "  INTERFACE_LINK_OPTIONS -Wl,--export-dynamic\n"
        "  INTERFACE_LINK_DIRECTORIES imported/libdirs\n"
        "  INTERFACE_LINK_LIBRARIES dl)\n"
        "add_library(real_iface INTERFACE)\n"
        "add_library(alias_iface ALIAS real_iface)\n"
        "target_include_directories(real_iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:real/build/include>\"\n"
        "  \"$<INSTALL_INTERFACE:real/install/include>\")\n"
        "target_compile_definitions(real_iface INTERFACE REAL_DEF=1)\n"
        "target_compile_options(real_iface INTERFACE \"$<$<COMPILE_LANGUAGE:C>:-DREAL_C_ONLY>\")\n"
        "target_link_options(real_iface INTERFACE -Wl,--no-undefined)\n"
        "target_link_directories(real_iface INTERFACE real/libdirs)\n"
        "target_link_libraries(real_iface INTERFACE imported_iface)\n"
        "add_library(global_iface INTERFACE)\n"
        "target_include_directories(global_iface INTERFACE global/include)\n"
        "target_compile_definitions(global_iface INTERFACE GLOBAL_DEF=1)\n"
        "target_compile_options(global_iface INTERFACE -DGLOBAL_OPT)\n"
        "target_compile_features(global_iface INTERFACE c_std_99)\n"
        "target_link_options(global_iface INTERFACE -Wl,--as-needed)\n"
        "target_link_directories(global_iface INTERFACE global/libdirs)\n"
        "target_link_libraries(global_iface INTERFACE alias_iface)\n"
        "add_library(dir_iface INTERFACE)\n"
        "target_include_directories(dir_iface INTERFACE dir/include)\n"
        "target_compile_definitions(dir_iface INTERFACE DIR_DEF=1)\n"
        "target_compile_options(dir_iface INTERFACE -DDIR_OPT)\n"
        "target_compile_features(dir_iface INTERFACE c_std_17)\n"
        "target_link_options(dir_iface INTERFACE -Wl,-z,defs)\n"
        "target_link_directories(dir_iface INTERFACE dir/libdirs)\n"
        "target_link_libraries(dir_iface INTERFACE real_iface)\n"
        "add_library(linkonly_iface INTERFACE)\n"
        "target_compile_definitions(linkonly_iface INTERFACE LINKONLY_COMPILE_DEF=1)\n"
        "target_link_libraries(linkonly_iface INTERFACE m m)\n"
        "set_property(GLOBAL APPEND PROPERTY LINK_LIBRARIES global_iface)\n"
        "set_property(DIRECTORY APPEND PROPERTY LINK_LIBRARIES dir_iface \"$<LINK_ONLY:linkonly_iface>\")\n"
        "add_executable(app main.c)\n",
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

    install_ctx = compile_ctx;
    install_ctx.build_interface_active = false;
    install_ctx.install_interface_active = true;

    link_ctx.current_target_id = app_id;
    link_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    link_ctx.build_interface_active = true;
    link_ctx.install_interface_active = false;

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &compile_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(build_model_string_item_span_contains_substring(include_items, "global/include"));
    ASSERT(build_model_string_item_span_contains_substring(include_items, "dir/include"));
    ASSERT(build_model_string_item_span_contains_substring(include_items, "real/build/include"));
    ASSERT(build_model_string_item_span_contains_substring(include_items, "imported/include"));
    ASSERT(!build_model_string_item_span_contains_substring(include_items, "real/install/include"));

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &install_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(!build_model_string_item_span_contains_substring(include_items, "real/build/include"));
    ASSERT(build_model_string_item_span_contains_substring(include_items, "real/install/include"));

    ASSERT(bm_query_target_effective_compile_definitions_items_with_context(model,
                                                                            app_id,
                                                                            &compile_ctx,
                                                                            query_arena,
                                                                            &def_items));
    ASSERT(build_model_string_item_span_contains(def_items, "GLOBAL_DEF=1"));
    ASSERT(build_model_string_item_span_contains(def_items, "DIR_DEF=1"));
    ASSERT(build_model_string_item_span_contains(def_items, "REAL_DEF=1"));
    ASSERT(build_model_string_item_span_contains(def_items, "IMPORTED_DEF=1"));
    ASSERT(!build_model_string_item_span_contains(def_items, "LINKONLY_COMPILE_DEF=1"));
    ASSERT(build_model_count_string_item_occurrences(def_items, "REAL_DEF=1") == 1);
    ASSERT(build_model_count_string_item_occurrences(def_items, "IMPORTED_DEF=1") == 1);

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &compile_ctx,
                                                                        query_arena,
                                                                        &compile_opts));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DGLOBAL_OPT"));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DDIR_OPT"));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DREAL_C_ONLY"));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DIMPORTED_OPT"));

    ASSERT(bm_query_target_effective_compile_features(model, app_id, &compile_ctx, query_arena, &features));
    ASSERT(build_model_string_span_contains(features, "c_std_99"));
    ASSERT(build_model_string_span_contains(features, "c_std_17"));
    ASSERT(build_model_string_span_contains(features, "c_std_11"));
    ASSERT(build_model_count_string_occurrences(features, "c_std_11") == 1);

    ASSERT(bm_query_target_effective_link_options_items_with_context(model,
                                                                     app_id,
                                                                     &link_ctx,
                                                                     query_arena,
                                                                     &link_opt_items));
    ASSERT(build_model_string_item_span_contains(link_opt_items, "-Wl,--as-needed"));
    ASSERT(build_model_string_item_span_contains(link_opt_items, "-Wl,-z,defs"));
    ASSERT(build_model_string_item_span_contains(link_opt_items, "-Wl,--no-undefined"));
    ASSERT(build_model_string_item_span_contains(link_opt_items, "-Wl,--export-dynamic"));

    ASSERT(bm_query_target_effective_link_directories_items_with_context(model,
                                                                         app_id,
                                                                         &link_ctx,
                                                                         query_arena,
                                                                         &link_dir_items));
    ASSERT(build_model_string_item_span_contains_substring(link_dir_items, "global/libdirs"));
    ASSERT(build_model_string_item_span_contains_substring(link_dir_items, "dir/libdirs"));
    ASSERT(build_model_string_item_span_contains_substring(link_dir_items, "real/libdirs"));
    ASSERT(build_model_string_item_span_contains_substring(link_dir_items, "imported/libdirs"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_lib_items));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "global_iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "dir_iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "linkonly_iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "alias_iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "imported_iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "dl"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "m"));
    ASSERT(build_model_count_link_item_occurrences(link_lib_items, "imported_iface") == 1);
    ASSERT(build_model_count_link_item_occurrences(link_lib_items, "m") == 1);

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &compile_ctx,
                                                                       query_arena,
                                                                       &link_lib_items));
    ASSERT(!build_model_link_item_span_contains(link_lib_items, "linkonly_iface"));
    ASSERT(!build_model_link_item_span_contains(link_lib_items, "m"));

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

TEST(build_model_source_effective_language_centralizes_supported_c_and_cxx_classification) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    size_t main_c = 0;
    size_t helper_cpp = 0;
    size_t explicit_cxx = 0;
    size_t header_h = 0;
    size_t skip_c = 0;
    size_t data_txt = 0;

    test_semantic_pipeline_config_init(&config);
    config.current_file = "source_language_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("source_language_src");
    config.binary_dir = nob_sv_from_cstr("source_language_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "add_executable(app main.c helper.cpp explicit_as_cxx.c public.h skip_compile.c data.txt)\n"
        "set_source_files_properties(explicit_as_cxx.c PROPERTIES LANGUAGE CXX)\n"
        "set_source_files_properties(skip_compile.c PROPERTIES HEADER_FILE_ONLY ON)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    main_c = build_model_find_target_source_index_containing(model, app_id, "main.c");
    helper_cpp = build_model_find_target_source_index_containing(model, app_id, "helper.cpp");
    explicit_cxx = build_model_find_target_source_index_containing(model, app_id, "explicit_as_cxx.c");
    header_h = build_model_find_target_source_index_containing(model, app_id, "public.h");
    skip_c = build_model_find_target_source_index_containing(model, app_id, "skip_compile.c");
    data_txt = build_model_find_target_source_index_containing(model, app_id, "data.txt");

    ASSERT(main_c < bm_query_target_source_count(model, app_id));
    ASSERT(helper_cpp < bm_query_target_source_count(model, app_id));
    ASSERT(explicit_cxx < bm_query_target_source_count(model, app_id));
    ASSERT(header_h < bm_query_target_source_count(model, app_id));
    ASSERT(skip_c < bm_query_target_source_count(model, app_id));
    ASSERT(data_txt < bm_query_target_source_count(model, app_id));

    ASSERT(nob_sv_eq(bm_query_target_source_effective_language(model, app_id, main_c), nob_sv_from_cstr("C")));
    ASSERT(nob_sv_eq(bm_query_target_source_effective_language(model, app_id, helper_cpp), nob_sv_from_cstr("CXX")));
    ASSERT(nob_sv_eq(bm_query_target_source_effective_language(model, app_id, explicit_cxx), nob_sv_from_cstr("CXX")));
    ASSERT(bm_query_target_source_effective_language(model, app_id, header_h).count == 0);
    ASSERT(bm_query_target_source_effective_language(model, app_id, skip_c).count == 0);
    ASSERT(bm_query_target_source_effective_language(model, app_id, data_txt).count == 0);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_effective_link_language_uses_config_platform_imported_mapping_and_session_context) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    Arena *session_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id alias_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context linux_ctx = {0};
    BM_Query_Eval_Context windows_ctx = {0};
    BM_Query_Eval_Context rel_windows_ctx = {0};
    BM_Query_Session *session = NULL;
    const BM_Query_Session_Stats *stats = NULL;
    String_View language = {0};

    ASSERT(query_arena != NULL);
    ASSERT(session_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "link_language_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("link_language_src");
    config.binary_dir = nob_sv_from_cstr("link_language_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "add_library(cdep STATIC cdep.c)\n"
        "add_library(cxxdep STATIC cxxdep.cpp)\n"
        "add_library(mapped STATIC IMPORTED GLOBAL)\n"
        "set_target_properties(mapped PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.a\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.a\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES C\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG CXX\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)\n"
        "add_library(cycle_a INTERFACE)\n"
        "add_library(cycle_b INTERFACE)\n"
        "target_link_libraries(cycle_a INTERFACE cycle_b)\n"
        "target_link_libraries(cycle_b INTERFACE cycle_a)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE\n"
        "  cycle_a\n"
        "  \"$<$<PLATFORM_ID:Linux>:cxxdep>\"\n"
        "  \"$<$<PLATFORM_ID:Windows>:cdep>\"\n"
        "  \"$<$<CONFIG:RelWithDebInfo>:mapped>\")\n"
        "add_executable(app_alias ALIAS app)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    alias_id = bm_query_target_by_name(model, nob_sv_from_cstr("app_alias"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(alias_id != BM_TARGET_ID_INVALID);

    linux_ctx.current_target_id = app_id;
    linux_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    linux_ctx.platform_id = nob_sv_from_cstr("Linux");
    linux_ctx.build_interface_active = true;

    windows_ctx = linux_ctx;
    windows_ctx.platform_id = nob_sv_from_cstr("Windows");

    rel_windows_ctx = windows_ctx;
    rel_windows_ctx.config = nob_sv_from_cstr("RelWithDebInfo");

    ASSERT(bm_query_target_effective_link_language(model, app_id, &linux_ctx, query_arena, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("CXX")));

    ASSERT(bm_query_target_effective_link_language(model, app_id, &windows_ctx, query_arena, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("C")));

    ASSERT(bm_query_target_effective_link_language(model, alias_id, &rel_windows_ctx, query_arena, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("CXX")));

    session = bm_query_session_create(session_arena, model);
    ASSERT(session != NULL);
    stats = bm_query_session_stats(session);
    ASSERT(stats != NULL);

    ASSERT(bm_query_session_target_effective_link_language(session, app_id, &linux_ctx, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("CXX")));
    ASSERT(bm_query_session_target_effective_link_language(session, app_id, &linux_ctx, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("CXX")));
    ASSERT(bm_query_session_target_effective_link_language(session, app_id, &windows_ctx, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("C")));
    ASSERT(bm_query_session_target_effective_link_language(session, app_id, &rel_windows_ctx, &language));
    ASSERT(nob_sv_eq(language, nob_sv_from_cstr("CXX")));
    ASSERT(stats->effective_link_language_hits == 1);
    ASSERT(stats->effective_link_language_misses == 3);

    arena_destroy(query_arena);
    arena_destroy(session_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_imported_target_paths_already_rooted_in_source_dir_are_not_rebased_twice) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id ext_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context debug_ctx = {0};
    String_View effective_file = {0};
    String_View resolved_target_file = {0};
    const char *cwd = NULL;
    char cwd_buf[_TINYDIR_PATH_MAX] = {0};

    ASSERT(query_arena != NULL);
    cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    ASSERT(strlen(cwd) + 1 < sizeof(cwd_buf));
    memcpy(cwd_buf, cwd, strlen(cwd) + 1);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "nested/imported_rooted_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("nested/imported_rooted_src");
    config.binary_dir = nob_sv_from_cstr("nested/imported_rooted_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION \"${CMAKE_CURRENT_SOURCE_DIR}/imports/libbase.so\"\n"
        "  IMPORTED_LOCATION_DEBUG \"${CMAKE_CURRENT_SOURCE_DIR}/imports/libdebug.so\"\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ext_id = bm_query_target_by_name(model, nob_sv_from_cstr("ext"));
    ASSERT(ext_id != BM_TARGET_ID_INVALID);

    debug_ctx.current_target_id = ext_id;
    debug_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    debug_ctx.build_interface_active = true;
    debug_ctx.install_interface_active = false;
    debug_ctx.config = nob_sv_from_cstr("RelWithDebInfo");

    ASSERT(bm_query_target_effective_file(model, ext_id, &debug_ctx, query_arena, &effective_file));
    ASSERT(nob_sv_eq(effective_file, nob_sv_from_cstr("nested/imported_rooted_src/imports/libdebug.so")));

    ASSERT(bm_query_resolve_string_with_context(model,
                                                &debug_ctx,
                                                query_arena,
                                                nob_sv_from_cstr("$<TARGET_FILE:ext>"),
                                                &resolved_target_file));
    ASSERT(nob_sv_eq(resolved_target_file,
                     nob_sv_from_cstr(nob_temp_sprintf(
                         "%s/nested/imported_rooted_src/imports/libdebug.so",
                         cwd_buf))));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_imported_target_known_configurations_are_stable_and_deduped) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id ext_id = BM_TARGET_ID_INVALID;
    BM_String_Span known_configs = {0};

    ASSERT(query_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "imported_known_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("imported_known_src");
    config.binary_dir = nob_sv_from_cstr("imported_known_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.so\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.so\n"
        "  IMPORTED_LOCATION_RELEASE imports/librelease.so\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug\n"
        "  MAP_IMPORTED_CONFIG_MINSIZEREL Release)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    ext_id = bm_query_target_by_name(model, nob_sv_from_cstr("ext"));
    ASSERT(ext_id != BM_TARGET_ID_INVALID);

    ASSERT(bm_query_target_imported_known_configurations(model, ext_id, query_arena, &known_configs));
    ASSERT(known_configs.count == 4);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Debug"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Release"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "RelWithDebInfo"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "MinSizeRel"));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_known_configuration_catalog_surfaces_supported_row52_domains) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    BM_String_Span known_configs = {0};

    test_semantic_pipeline_config_init(&config);
    config.current_file = "known_configs_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("known_configs_src");
    config.binary_dir = nob_sv_from_cstr("known_configs_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(iface INTERFACE)\n"
        "target_compile_definitions(iface INTERFACE\n"
        "  \"$<$<CONFIG:Debug>:DBG_CFG>\"\n"
        "  \"$<$<CONFIG:Release>:REL_CFG>\")\n"
        "add_library(ext SHARED IMPORTED)\n"
        "set_target_properties(ext PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libbase.so\n"
        "  IMPORTED_LOCATION_DEBUG imports/libdebug.so\n"
        "  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)\n"
        "file(GENERATE OUTPUT \"$<IF:$<CONFIG:Profile>,${CMAKE_CURRENT_BINARY_DIR}/cfg/profile.txt,${CMAKE_CURRENT_BINARY_DIR}/cfg/other.txt>\" CONTENT \"cfg\")\n"
        "add_test(NAME smoke COMMAND helper CONFIGURATIONS MinSizeRel)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    known_configs = bm_query_known_configurations(fixture.build.model);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Debug"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Release"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "RelWithDebInfo"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "MinSizeRel"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Profile"));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_known_configuration_catalog_detects_strequal_config_comparisons) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    BM_String_Span known_configs = {0};

    test_semantic_pipeline_config_init(&config);
    config.current_file = "known_configs_strequal_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("known_configs_strequal_src");
    config.binary_dir = nob_sv_from_cstr("known_configs_strequal_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "file(GENERATE\n"
        "  OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/cfg/$<$<STREQUAL:$<CONFIG>,Profile>:profile>$<$<NOT:$<STREQUAL:$<CONFIG>,Profile>>:other>.txt\"\n"
        "  CONTENT \"$<$<STREQUAL:$<CONFIG>,Profile>:profile>$<$<NOT:$<STREQUAL:$<CONFIG>,Profile>>:other>\")\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    known_configs = bm_query_known_configurations(fixture.build.model);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Profile"));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_artifact_query_resolves_output_naming_layout_by_config_and_platform) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    Arena *session_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id shared_id = BM_TARGET_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context debug_linux = {0};
    BM_Query_Eval_Context release_linux = {0};
    BM_Query_Eval_Context debug_windows = {0};
    BM_Query_Session *session = NULL;
    const BM_Query_Session_Stats *stats = NULL;
    BM_Target_Artifact_View artifact = {0};
    BM_Target_Artifact_View linker = {0};
    BM_String_Span known_configs = {0};

    ASSERT(query_arena != NULL);
    ASSERT(session_arena != NULL);
    test_semantic_pipeline_config_init(&config);
    config.current_file = "row53_artifact_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row53_artifact_src");
    config.binary_dir = nob_sv_from_cstr("row53_artifact_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Row53 LANGUAGES C)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES\n"
        "  OUTPUT_NAME generic_core\n"
        "  OUTPUT_NAME_RELEASE generic_rel\n"
        "  RELWITHDEBINFO_OUTPUT_NAME legacy_rel\n"
        "  ARCHIVE_OUTPUT_NAME archive_generic\n"
        "  ARCHIVE_OUTPUT_NAME_DEBUG archive_dbg\n"
        "  ARCHIVE_OUTPUT_DIRECTORY artifacts/lib\n"
        "  ARCHIVE_OUTPUT_DIRECTORY_DEBUG \"artifacts/$<IF:$<CONFIG:Debug>,dbg,other>/lib\"\n"
        "  PREFIX pre_\n"
        "  SUFFIX .pkg)\n"
        "add_library(shared SHARED src/shared.c)\n"
        "set_target_properties(shared PROPERTIES\n"
        "  LIBRARY_OUTPUT_NAME_DEBUG sh_dbg\n"
        "  RUNTIME_OUTPUT_NAME_DEBUG rt_dbg\n"
        "  ARCHIVE_OUTPUT_NAME_DEBUG implib_dbg\n"
        "  LIBRARY_OUTPUT_DIRECTORY_DEBUG linux/lib\n"
        "  RUNTIME_OUTPUT_DIRECTORY_DEBUG win/bin\n"
        "  ARCHIVE_OUTPUT_DIRECTORY_DEBUG win/implib)\n"
        "add_executable(app src/main.c)\n"
        "set_target_properties(app PROPERTIES\n"
        "  OUTPUT_NAME base_app\n"
        "  RUNTIME_OUTPUT_NAME_DEBUG dbg_app\n"
        "  RUNTIME_OUTPUT_NAME_RELEASE \"$<IF:$<CONFIG:Release>,rel_app,bad_app>\"\n"
        "  RUNTIME_OUTPUT_DIRECTORY \"$<IF:$<PLATFORM_ID:Windows>,winbin,posixbin>\")\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    shared_id = bm_query_target_by_name(model, nob_sv_from_cstr("shared"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(core_id != BM_TARGET_ID_INVALID);
    ASSERT(shared_id != BM_TARGET_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    debug_linux.current_target_id = app_id;
    debug_linux.usage_mode = BM_QUERY_USAGE_LINK;
    debug_linux.config = nob_sv_from_cstr("Debug");
    debug_linux.platform_id = nob_sv_from_cstr("Linux");
    debug_linux.build_interface_active = true;

    release_linux = debug_linux;
    release_linux.config = nob_sv_from_cstr("Release");

    debug_windows = debug_linux;
    debug_windows.platform_id = nob_sv_from_cstr("Windows");

    ASSERT(bm_query_target_effective_artifact(model, core_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_linux, query_arena, &artifact));
    ASSERT(artifact.emits);
    ASSERT(nob_sv_eq(artifact.directory, nob_sv_from_cstr("row53_artifact_build/artifacts/dbg/lib")));
    ASSERT(nob_sv_eq(artifact.file_name, nob_sv_from_cstr("pre_archive_dbg.pkg")));
    ASSERT(nob_sv_eq(artifact.prefix, nob_sv_from_cstr("pre_")));
    ASSERT(nob_sv_eq(artifact.output_name, nob_sv_from_cstr("archive_dbg")));
    ASSERT(nob_sv_eq(artifact.suffix, nob_sv_from_cstr(".pkg")));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/artifacts/dbg/lib/pre_archive_dbg.pkg")));

    ASSERT(bm_query_target_effective_artifact(model, core_id, BM_TARGET_ARTIFACT_LINKER, &release_linux, query_arena, &artifact));
    ASSERT(nob_sv_eq(artifact.file_name, nob_sv_from_cstr("pre_archive_generic.pkg")));
    ASSERT(nob_sv_eq(artifact.directory, nob_sv_from_cstr("row53_artifact_build/artifacts/lib")));

    ASSERT(bm_query_target_effective_artifact(model, app_id, BM_TARGET_ARTIFACT_RUNTIME, &release_linux, query_arena, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/posixbin/rel_app")));
    ASSERT(nob_sv_eq(artifact.suffix, nob_sv_from_cstr("")));

    ASSERT(bm_query_target_effective_artifact(model, app_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_windows, query_arena, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/winbin/dbg_app.exe")));
    ASSERT(nob_sv_eq(artifact.suffix, nob_sv_from_cstr(".exe")));

    ASSERT(bm_query_target_effective_artifact(model, shared_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_linux, query_arena, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/linux/lib/libsh_dbg.so")));
    ASSERT(bm_query_target_effective_artifact(model, shared_id, BM_TARGET_ARTIFACT_LINKER, &debug_linux, query_arena, &linker));
    ASSERT(nob_sv_eq(linker.path, artifact.path));

    ASSERT(bm_query_target_effective_artifact(model, shared_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_windows, query_arena, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/win/bin/rt_dbg.dll")));
    ASSERT(bm_query_target_effective_artifact(model, shared_id, BM_TARGET_ARTIFACT_LINKER, &debug_windows, query_arena, &linker));
    ASSERT(nob_sv_eq(linker.path, nob_sv_from_cstr("row53_artifact_build/win/implib/implib_dbg.lib")));

    known_configs = bm_query_known_configurations(model);
    ASSERT(build_model_string_span_contains_ci(known_configs, "Debug"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "Release"));
    ASSERT(build_model_string_span_contains_ci(known_configs, "RelWithDebInfo"));

    session = bm_query_session_create(session_arena, model);
    ASSERT(session != NULL);
    stats = bm_query_session_stats(session);
    ASSERT(stats != NULL);

    ASSERT(bm_query_session_target_effective_artifact(session, app_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_linux, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/posixbin/dbg_app")));
    ASSERT(bm_query_session_target_effective_artifact(session, app_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_linux, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/posixbin/dbg_app")));
    ASSERT(bm_query_session_target_effective_artifact(session, app_id, BM_TARGET_ARTIFACT_RUNTIME, &release_linux, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/posixbin/rel_app")));
    ASSERT(bm_query_session_target_effective_artifact(session, app_id, BM_TARGET_ARTIFACT_RUNTIME, &debug_windows, &artifact));
    ASSERT(nob_sv_eq(artifact.path, nob_sv_from_cstr("row53_artifact_build/winbin/dbg_app.exe")));
    ASSERT(stats->target_artifact_hits == 1);
    ASSERT(stats->target_artifact_misses == 3);

    arena_destroy(query_arena);
    arena_destroy(session_arena);
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

TEST(build_model_source_membership_file_sets_and_source_properties_are_canonical) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id imported_id = BM_TARGET_ID_INVALID;
    Arena *scratch = arena_create(256 * 1024);
    size_t main_index = 0;
    size_t public_index = 0;
    size_t skip_index = 0;
    size_t iface_index = 0;
    size_t header_set_index = 0;
    size_t iface_header_set_index = 0;
    size_t module_index = 0;
    BM_String_Span sources_raw = {0};
    String_View property_value = {0};
    ASSERT(scratch != NULL);

    test_semantic_pipeline_config_init(&config);
    config.current_file = "source_shape_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("source_shape_src");
    config.binary_dir = nob_sv_from_cstr("source_shape_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C CXX)\n"
        "set_source_files_properties(main.c PROPERTIES LANGUAGE CXX COMPILE_DEFINITIONS MAIN_LANG_CXX=1 COMPILE_OPTIONS -Wno-unused-parameter INCLUDE_DIRECTORIES local/include)\n"
        "set_property(SOURCE main.c APPEND PROPERTY COMPILE_DEFINITIONS MAIN_LANG_APPEND=1)\n"
        "set_property(SOURCE main.c APPEND PROPERTY COMPILE_OPTIONS -Wshadow)\n"
        "set_property(SOURCE main.c APPEND PROPERTY INCLUDE_DIRECTORIES local/extra)\n"
        "set_property(SOURCE main.c PROPERTY CUSTOM_SOURCE_TAG alpha)\n"
        "add_library(core STATIC main.c public.h skip_compile.c)\n"
        "set_source_files_properties(skip_compile.c PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)\n"
        "target_sources(core INTERFACE iface.h)\n"
        "target_sources(core PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/public.hpp)\n"
        "target_sources(core INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS api FILES api/iface.hpp)\n"
        "target_sources(core PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "add_library(imported_mod STATIC IMPORTED)\n"
        "target_sources(imported_mod INTERFACE FILE_SET CXX_MODULES BASE_DIRS imported FILES imported/api.cppm)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    imported_id = bm_query_target_by_name(model, nob_sv_from_cstr("imported_mod"));
    ASSERT(core_id != BM_TARGET_ID_INVALID);
    ASSERT(imported_id != BM_TARGET_ID_INVALID);

    sources_raw = bm_query_target_sources_raw(model, core_id);
    ASSERT(sources_raw.count == 3);
    ASSERT(build_model_string_span_contains_substring(sources_raw, "main.c"));
    ASSERT(build_model_string_span_contains_substring(sources_raw, "public.h"));
    ASSERT(build_model_string_span_contains_substring(sources_raw, "skip_compile.c"));

    main_index = build_model_find_target_source_index_containing(model, core_id, "main.c");
    public_index = build_model_find_target_source_index_containing(model, core_id, "public.h");
    skip_index = build_model_find_target_source_index_containing(model, core_id, "skip_compile.c");
    iface_index = build_model_find_target_source_index_containing(model, core_id, "iface.h");
    header_set_index = build_model_find_target_source_index_containing(model, core_id, "include/public.hpp");
    iface_header_set_index = build_model_find_target_source_index_containing(model, core_id, "api/iface.hpp");
    module_index = build_model_find_target_source_index_containing(model, core_id, "modules/core.cppm");
    ASSERT(main_index < bm_query_target_source_count(model, core_id));
    ASSERT(public_index < bm_query_target_source_count(model, core_id));
    ASSERT(skip_index < bm_query_target_source_count(model, core_id));
    ASSERT(iface_index < bm_query_target_source_count(model, core_id));
    ASSERT(header_set_index < bm_query_target_source_count(model, core_id));
    ASSERT(iface_header_set_index < bm_query_target_source_count(model, core_id));
    ASSERT(module_index < bm_query_target_source_count(model, core_id));

    ASSERT(bm_query_target_source_kind(model, core_id, main_index) == BM_TARGET_SOURCE_REGULAR);
    ASSERT(bm_query_target_source_visibility(model, core_id, main_index) == BM_VISIBILITY_PRIVATE);
    ASSERT(bm_query_target_source_is_compile_input(model, core_id, main_index));
    ASSERT(!bm_query_target_source_header_file_only(model, core_id, main_index));
    ASSERT(nob_sv_eq(bm_query_target_source_language(model, core_id, main_index), nob_sv_from_cstr("CXX")));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_compile_definitions(model, core_id, main_index),
                                                 "MAIN_LANG_CXX=1"));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_compile_definitions(model, core_id, main_index),
                                                 "MAIN_LANG_APPEND=1"));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_compile_options(model, core_id, main_index),
                                                 "-Wno-unused-parameter"));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_compile_options(model, core_id, main_index),
                                                 "-Wshadow"));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_include_directories(model, core_id, main_index),
                                                 "local/include"));
    ASSERT(build_model_string_item_span_contains(bm_query_target_source_include_directories(model, core_id, main_index),
                                                 "local/extra"));
    ASSERT(build_model_string_span_contains(bm_query_target_source_raw_property_items(model,
                                                                                      core_id,
                                                                                      main_index,
                                                                                      nob_sv_from_cstr("COMPILE_DEFINITIONS")),
                                            "MAIN_LANG_CXX=1"));
    ASSERT(build_model_string_span_contains(bm_query_target_source_raw_property_items(model,
                                                                                      core_id,
                                                                                      main_index,
                                                                                      nob_sv_from_cstr("CUSTOM_SOURCE_TAG")),
                                            "alpha"));

    ASSERT(bm_query_target_source_kind(model, core_id, public_index) == BM_TARGET_SOURCE_REGULAR);
    ASSERT(bm_query_target_source_visibility(model, core_id, public_index) == BM_VISIBILITY_PRIVATE);
    ASSERT(bm_query_target_source_is_compile_input(model, core_id, public_index));

    ASSERT(bm_query_target_source_kind(model, core_id, skip_index) == BM_TARGET_SOURCE_REGULAR);
    ASSERT(bm_query_target_source_visibility(model, core_id, skip_index) == BM_VISIBILITY_PRIVATE);
    ASSERT(!bm_query_target_source_is_compile_input(model, core_id, skip_index));
    ASSERT(bm_query_target_source_header_file_only(model, core_id, skip_index));
    ASSERT(bm_query_target_source_generated(model, core_id, skip_index));

    ASSERT(bm_query_target_source_kind(model, core_id, iface_index) == BM_TARGET_SOURCE_REGULAR);
    ASSERT(bm_query_target_source_visibility(model, core_id, iface_index) == BM_VISIBILITY_INTERFACE);
    ASSERT(!bm_query_target_source_is_compile_input(model, core_id, iface_index));

    ASSERT(bm_query_target_source_kind(model, core_id, header_set_index) == BM_TARGET_SOURCE_HEADER_FILE_SET);
    ASSERT(bm_query_target_source_visibility(model, core_id, header_set_index) == BM_VISIBILITY_PUBLIC);
    ASSERT(nob_sv_eq(bm_query_target_source_file_set_name(model, core_id, header_set_index), nob_sv_from_cstr("HEADERS")));
    ASSERT(!bm_query_target_source_is_compile_input(model, core_id, header_set_index));

    ASSERT(bm_query_target_source_kind(model, core_id, iface_header_set_index) == BM_TARGET_SOURCE_HEADER_FILE_SET);
    ASSERT(bm_query_target_source_visibility(model, core_id, iface_header_set_index) == BM_VISIBILITY_INTERFACE);
    ASSERT(nob_sv_eq(bm_query_target_source_file_set_name(model, core_id, iface_header_set_index), nob_sv_from_cstr("api")));
    ASSERT(!bm_query_target_source_is_compile_input(model, core_id, iface_header_set_index));

    ASSERT(bm_query_target_source_kind(model, core_id, module_index) == BM_TARGET_SOURCE_CXX_MODULE_FILE_SET);
    ASSERT(bm_query_target_source_visibility(model, core_id, module_index) == BM_VISIBILITY_PUBLIC);
    ASSERT(nob_sv_eq(bm_query_target_source_file_set_name(model, core_id, module_index), nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(!bm_query_target_source_is_compile_input(model, core_id, module_index));

    ASSERT(bm_query_target_file_set_count(model, core_id) == 3);
    ASSERT(nob_sv_eq(bm_query_target_file_set_name(model, core_id, 0), nob_sv_from_cstr("HEADERS")));
    ASSERT(bm_query_target_file_set_kind(model, core_id, 0) == BM_TARGET_FILE_SET_HEADERS);
    ASSERT(bm_query_target_file_set_visibility(model, core_id, 0) == BM_VISIBILITY_PUBLIC);
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_base_dirs(model, core_id, 0), "include"));
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_files_raw(model, core_id, 0), "include/public.hpp"));
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_files_effective(model, core_id, 0), "include/public.hpp"));

    ASSERT(nob_sv_eq(bm_query_target_file_set_name(model, core_id, 1), nob_sv_from_cstr("api")));
    ASSERT(bm_query_target_file_set_kind(model, core_id, 1) == BM_TARGET_FILE_SET_HEADERS);
    ASSERT(bm_query_target_file_set_visibility(model, core_id, 1) == BM_VISIBILITY_INTERFACE);
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_base_dirs(model, core_id, 1), "api"));
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_files_raw(model, core_id, 1), "api/iface.hpp"));

    ASSERT(nob_sv_eq(bm_query_target_file_set_name(model, core_id, 2), nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(bm_query_target_file_set_kind(model, core_id, 2) == BM_TARGET_FILE_SET_CXX_MODULES);
    ASSERT(bm_query_target_file_set_visibility(model, core_id, 2) == BM_VISIBILITY_PUBLIC);
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_base_dirs(model, core_id, 2), "modules"));
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_files_raw(model, core_id, 2), "modules/core.cppm"));

    ASSERT(bm_query_target_file_set_count(model, imported_id) == 1);
    ASSERT(nob_sv_eq(bm_query_target_file_set_name(model, imported_id, 0), nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(bm_query_target_file_set_kind(model, imported_id, 0) == BM_TARGET_FILE_SET_CXX_MODULES);
    ASSERT(bm_query_target_file_set_visibility(model, imported_id, 0) == BM_VISIBILITY_INTERFACE);
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_base_dirs(model, imported_id, 0), "imported"));
    ASSERT(build_model_string_span_contains_substring(bm_query_target_file_set_files_raw(model, imported_id, 0), "imported/api.cppm"));

    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("SOURCES"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("main.c")));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("public.h")));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("skip_compile.c")));
    ASSERT(!build_model_sv_contains(property_value, nob_sv_from_cstr("iface.h")));

    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("INTERFACE_SOURCES"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("iface.h")));

    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("HEADER_SETS"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("HEADERS")));
    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("INTERFACE_HEADER_SETS"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("api")));
    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("HEADER_SET"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("include/public.hpp")));
    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("HEADER_SET_API"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("api/iface.hpp")));
    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("CXX_MODULE_SETS"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(bm_query_target_modeled_property_value(model, core_id, nob_sv_from_cstr("CXX_MODULE_SET"), scratch, &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(bm_query_target_modeled_property_value(model,
                                          imported_id,
                                          nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS"),
                                          scratch,
                                          &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("CXX_MODULES")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    arena_destroy(scratch);
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
    BM_Link_Item_Span link_lib_items = {0};
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
    ASSERT(build_model_link_item_equals_at(link_lib_items, 0, "iface"));
    ASSERT(build_model_link_item_equals_at(link_lib_items, 1, "m"));
    ASSERT(build_model_link_item_equals_at(link_lib_items, 2, "pthread"));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_effective_queries_terminate_interface_cycles_without_duplicate_contributions) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_String_Item_Span def_items = {0};
    BM_Link_Item_Span link_items = {0};

    ASSERT(query_arena != NULL);

    test_semantic_pipeline_config_init(&config);
    config.current_file = "cycle_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("cycle_query_src");
    config.binary_dir = nob_sv_from_cstr("cycle_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(a INTERFACE)\n"
        "add_library(b INTERFACE)\n"
        "add_library(c INTERFACE IMPORTED)\n"
        "set_target_properties(c PROPERTIES\n"
        "  INTERFACE_COMPILE_DEFINITIONS C_DEF=1\n"
        "  INTERFACE_LINK_LIBRARIES dl)\n"
        "target_compile_definitions(a INTERFACE A_DEF=1)\n"
        "target_compile_definitions(b INTERFACE B_DEF=1)\n"
        "target_link_libraries(a INTERFACE b)\n"
        "target_link_libraries(b INTERFACE a c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE a b)\n",
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

    ASSERT(bm_query_target_effective_compile_definitions_items_with_context(model,
                                                                            app_id,
                                                                            &compile_ctx,
                                                                            query_arena,
                                                                            &def_items));
    ASSERT(build_model_count_string_item_occurrences(def_items, "A_DEF=1") == 1);
    ASSERT(build_model_count_string_item_occurrences(def_items, "B_DEF=1") == 1);
    ASSERT(build_model_count_string_item_occurrences(def_items, "C_DEF=1") == 1);

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_items));
    ASSERT(build_model_count_link_item_occurrences(link_items, "a") == 1);
    ASSERT(build_model_count_link_item_occurrences(link_items, "b") == 1);
    ASSERT(build_model_count_link_item_occurrences(link_items, "c") == 1);
    ASSERT(build_model_count_link_item_occurrences(link_items, "dl") == 1);

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_usage_requirement_property_setters_promote_to_canonical_item_storage) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Directory_Id root_directory = BM_DIRECTORY_ID_INVALID;
    BM_Target_Id iface_id = BM_TARGET_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context compile_ctx = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_Link_Item_Span global_link_libs = {0};
    BM_Link_Item_Span directory_link_libs = {0};
    BM_String_Item_Span include_items = {0};
    BM_String_Item_Span compile_opts = {0};
    BM_String_Item_Span compile_features = {0};
    BM_String_Item_Span link_dirs = {0};
    BM_String_Item_Span link_opts = {0};
    BM_Link_Item_Span link_lib_items = {0};
    BM_String_Item_Span raw_compile_features = {0};
    BM_String_Item_Span raw_include_items = {0};
    BM_String_Item_Span raw_compile_options = {0};
    BM_String_Span raw_compile_options_prop = {0};
    String_View property_value = {0};

    ASSERT(query_arena != NULL);

    test_semantic_pipeline_config_init(&config);
    config.current_file = "usage_item_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("usage_item_src");
    config.binary_dir = nob_sv_from_cstr("usage_item_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "set_property(GLOBAL APPEND PROPERTY LINK_LIBRARIES global_dep)\n"
        "set_property(DIRECTORY APPEND PROPERTY LINK_LIBRARIES dir_dep)\n"
        "add_library(iface INTERFACE)\n"
        "set_target_properties(iface PROPERTIES\n"
        "  INTERFACE_INCLUDE_DIRECTORIES iface/include\n"
        "  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES iface/sys\n"
        "  INTERFACE_COMPILE_DEFINITIONS IFACE_DEF=1\n"
        "  INTERFACE_COMPILE_OPTIONS -Wall\n"
        "  INTERFACE_COMPILE_FEATURES c_std_99\n"
        "  CUSTOM_META keepme)\n"
        "set_property(TARGET iface APPEND PROPERTY INTERFACE_COMPILE_FEATURES c_std_11)\n"
        "set_property(TARGET iface APPEND PROPERTY INTERFACE_LINK_OPTIONS -Wl,--as-needed)\n"
        "set_property(TARGET iface APPEND PROPERTY INTERFACE_LINK_DIRECTORIES iface/lib)\n"
        "set_property(TARGET iface APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)\n"
        "set_property(TARGET iface APPEND_STRING PROPERTY INTERFACE_COMPILE_OPTIONS -Wraw)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE iface)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    root_directory = bm_query_root_directory(model);
    iface_id = bm_query_target_by_name(model, nob_sv_from_cstr("iface"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(root_directory != BM_DIRECTORY_ID_INVALID);
    ASSERT(iface_id != BM_TARGET_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    global_link_libs = bm_query_global_link_libraries_raw(model);
    directory_link_libs = bm_query_directory_link_libraries_raw(model, root_directory);
    ASSERT(global_link_libs.count == 1);
    ASSERT(build_model_link_item_equals_at(global_link_libs, 0, "global_dep"));
    ASSERT(directory_link_libs.count == 1);
    ASSERT(build_model_link_item_equals_at(directory_link_libs, 0, "dir_dep"));

    raw_compile_features = bm_query_target_compile_features_raw(model, iface_id);
    raw_include_items = bm_query_target_include_directories_raw(model, iface_id);
    raw_compile_options = bm_query_target_compile_options_raw(model, iface_id);
    ASSERT(raw_compile_features.count == 2);
    ASSERT(build_model_string_item_equals_at(raw_compile_features, 0, "c_std_99"));
    ASSERT(build_model_string_item_equals_at(raw_compile_features, 1, "c_std_11"));
    ASSERT(raw_include_items.count == 2);
    ASSERT(build_model_string_item_span_contains(raw_include_items, "iface/include"));
    ASSERT(build_model_string_item_span_contains(raw_include_items, "iface/sys"));
    ASSERT(build_model_string_item_has_flag(raw_include_items, "iface/sys", BM_ITEM_FLAG_SYSTEM));
    ASSERT(raw_compile_options.count == 1);
    ASSERT(build_model_string_item_equals_at(raw_compile_options, 0, "-Wall"));

    ASSERT(bm_query_target_modeled_property_value(model,
                                          iface_id,
                                          nob_sv_from_cstr("INTERFACE_SYSTEM_INCLUDE_DIRECTORIES"),
                                          query_arena,
                                          &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("iface/sys")));
    ASSERT(bm_query_target_modeled_property_value(model,
                                          iface_id,
                                          nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES"),
                                          query_arena,
                                          &property_value));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("c_std_99")));
    ASSERT(build_model_sv_contains(property_value, nob_sv_from_cstr("c_std_11")));

    raw_compile_options_prop = bm_query_target_raw_property_items(model,
                                                                  iface_id,
                                                                  nob_sv_from_cstr("INTERFACE_COMPILE_OPTIONS"));
    ASSERT(raw_compile_options_prop.count == 1);
    ASSERT(build_model_string_equals_at(raw_compile_options_prop, 0, "-Wraw"));
    ASSERT(bm_query_target_raw_property_items(model, iface_id, nob_sv_from_cstr("CUSTOM_META")).count == 1);

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
    ASSERT(build_model_string_item_span_contains(include_items, "iface/include"));
    ASSERT(build_model_string_item_span_contains(include_items, "iface/sys"));
    ASSERT(build_model_string_item_has_flag(include_items, "iface/sys", BM_ITEM_FLAG_SYSTEM));

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &compile_ctx,
                                                                        query_arena,
                                                                        &compile_opts));
    ASSERT(compile_opts.count == 1);
    ASSERT(build_model_string_item_equals_at(compile_opts, 0, "-Wall"));
    ASSERT(!build_model_string_item_span_contains(compile_opts, "-Wraw"));

    ASSERT(bm_query_target_effective_compile_features_items(model,
                                                            app_id,
                                                            &compile_ctx,
                                                            query_arena,
                                                            &compile_features));
    ASSERT(compile_features.count == 2);
    ASSERT(build_model_string_item_equals_at(compile_features, 0, "c_std_99"));
    ASSERT(build_model_string_item_equals_at(compile_features, 1, "c_std_11"));

    ASSERT(bm_query_target_effective_link_directories_items_with_context(model,
                                                                         app_id,
                                                                         &link_ctx,
                                                                         query_arena,
                                                                         &link_dirs));
    ASSERT(link_dirs.count == 1);
    ASSERT(build_model_string_item_contains_at(link_dirs, 0, "iface/lib"));

    ASSERT(bm_query_target_effective_link_options_items_with_context(model,
                                                                     app_id,
                                                                     &link_ctx,
                                                                     query_arena,
                                                                     &link_opts));
    ASSERT(link_opts.count == 1);
    ASSERT(build_model_string_item_equals_at(link_opts, 0, "-Wl,--as-needed"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_lib_items));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "iface"));
    ASSERT(build_model_link_item_span_contains(link_lib_items, "m"));

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
        "install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo RENAME DemoPkgConfig.cmake COMPONENT Development)\n"
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
                     nob_sv_from_cstr("Unspecified")));
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
    ASSERT(nob_sv_eq(bm_query_install_rule_public_header_component(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("Development")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)0) == core_id);
    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)1) == BM_INSTALL_RULE_FILE);
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("Development")));
    ASSERT(nob_sv_eq(bm_query_install_rule_rename(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("DemoPkgConfig.cmake")));

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

TEST(build_model_context_queries_support_build_local_install_prefix_target_genex_eval_and_link_literals) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(512 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context build_ctx = {0};
    BM_Query_Eval_Context export_ctx = {0};
    BM_Query_Eval_Context install_ctx = {0};
    BM_Query_Eval_Context link_ctx = {0};
    BM_String_Item_Span include_items = {0};
    BM_String_Item_Span compile_opts = {0};
    BM_Link_Item_Span link_items = {0};
    String_View resolved = {0};

    ASSERT(query_arena != NULL);
    ASSERT(build_model_write_text_file("row51_query_src/main.c", "int main(void) { return 0; }\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "row51_query_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row51_query_src");
    config.binary_dir = nob_sv_from_cstr("row51_query_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(base INTERFACE)\n"
        "set_property(TARGET base PROPERTY CUSTOM_DIR \"$<BUILD_INTERFACE:base/custom>\")\n"
        "target_include_directories(base INTERFACE\n"
        "  \"$<BUILD_LOCAL_INTERFACE:base/local>\"\n"
        "  \"$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/sdk/include>\")\n"
        "target_compile_options(base INTERFACE \"$<$<COMPILE_LANGUAGE:C>:-DBASE_C>\")\n"
        "target_link_libraries(base INTERFACE \"$<LINK_LIBRARY:WHOLE_ARCHIVE,dep>\")\n"
        "add_library(wrapper INTERFACE)\n"
        "target_include_directories(wrapper INTERFACE \"$<TARGET_PROPERTY:base,INTERFACE_INCLUDE_DIRECTORIES>\")\n"
        "target_include_directories(wrapper INTERFACE \"$<TARGET_GENEX_EVAL:base,$<TARGET_PROPERTY:base,CUSTOM_DIR>>\")\n"
        "target_compile_options(wrapper INTERFACE \"$<TARGET_PROPERTY:base,INTERFACE_COMPILE_OPTIONS>\")\n"
        "target_link_libraries(wrapper INTERFACE \"$<TARGET_PROPERTY:base,INTERFACE_LINK_LIBRARIES>\")\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE wrapper)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    build_ctx.current_target_id = app_id;
    build_ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    build_ctx.compile_language = nob_sv_from_cstr("C");
    build_ctx.build_interface_active = true;
    build_ctx.build_local_interface_active = true;
    build_ctx.install_interface_active = false;

    export_ctx = build_ctx;
    export_ctx.build_local_interface_active = false;

    install_ctx = build_ctx;
    install_ctx.build_interface_active = false;
    install_ctx.build_local_interface_active = false;
    install_ctx.install_interface_active = true;
    install_ctx.install_prefix = nob_sv_from_cstr("/opt/demo");

    link_ctx = build_ctx;
    link_ctx.usage_mode = BM_QUERY_USAGE_LINK;
    link_ctx.compile_language = nob_sv_from_cstr("");

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &build_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(build_model_string_item_span_contains(include_items, "base/local"));
    ASSERT(build_model_string_item_span_contains(include_items, "base/custom"));

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &export_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(!build_model_string_item_span_contains(include_items, "base/local"));
    ASSERT(build_model_string_item_span_contains(include_items, "base/custom"));

    ASSERT(bm_query_target_effective_include_directories_items_with_context(model,
                                                                            app_id,
                                                                            &install_ctx,
                                                                            query_arena,
                                                                            &include_items));
    ASSERT(!build_model_string_item_span_contains(include_items, "base/local"));
    ASSERT(!build_model_string_item_span_contains(include_items, "base/custom"));
    ASSERT(build_model_string_item_span_contains(include_items, "/opt/demo/sdk/include"));

    ASSERT(bm_query_target_effective_compile_options_items_with_context(model,
                                                                        app_id,
                                                                        &build_ctx,
                                                                        query_arena,
                                                                        &compile_opts));
    ASSERT(build_model_string_item_span_contains(compile_opts, "-DBASE_C"));

    ASSERT(bm_query_target_effective_link_libraries_items_with_context(model,
                                                                       app_id,
                                                                       &link_ctx,
                                                                       query_arena,
                                                                       &link_items));
    ASSERT(build_model_link_item_span_contains(link_items, "$<LINK_LIBRARY:WHOLE_ARCHIVE,dep>"));

    ASSERT(bm_query_resolve_string_with_context(model,
                                                &install_ctx,
                                                query_arena,
                                                nob_sv_from_cstr("$<INSTALL_PREFIX>/sdk/include"),
                                                &resolved));
    ASSERT(nob_sv_eq(resolved, nob_sv_from_cstr("/opt/demo/sdk/include")));

    arena_destroy(query_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_same_family_target_property_cycles_fail_deterministically) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *query_arena = arena_create(256 * 1024);
    const Build_Model *model = NULL;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Query_Eval_Context ctx = {0};
    BM_String_Item_Span include_items = {0};

    ASSERT(query_arena != NULL);
    ASSERT(build_model_write_text_file("row51_cycle_src/main.c", "int main(void) { return 0; }\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "row51_cycle_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("row51_cycle_src");
    config.binary_dir = nob_sv_from_cstr("row51_cycle_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(loop INTERFACE)\n"
        "target_include_directories(loop INTERFACE \"$<TARGET_PROPERTY:loop,INTERFACE_INCLUDE_DIRECTORIES>\")\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE loop)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    ASSERT(app_id != BM_TARGET_ID_INVALID);

    ctx.current_target_id = app_id;
    ctx.usage_mode = BM_QUERY_USAGE_COMPILE;
    ctx.compile_language = nob_sv_from_cstr("C");
    ctx.build_interface_active = true;
    ctx.build_local_interface_active = true;
    ctx.install_interface_active = false;

    ASSERT(!bm_query_target_effective_include_directories_items_with_context(model,
                                                                             app_id,
                                                                             &ctx,
                                                                             query_arena,
                                                                             &include_items));

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
                     nob_sv_from_cstr("Toolkit")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("Development")));

    ASSERT(bm_query_export_count(model) == 1);
    ASSERT(nob_sv_eq(bm_query_export_component(model, (BM_Export_Id)0),
                     nob_sv_from_cstr("Toolkit")));

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_install_queries_cover_supported_target_kinds_and_rule_families) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Directory_Id root_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id sub_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Target_Id app_id = BM_TARGET_ID_INVALID;
    BM_Target_Id core_static_id = BM_TARGET_ID_INVALID;
    BM_Target_Id core_shared_id = BM_TARGET_ID_INVALID;
    BM_Target_Id plugin_id = BM_TARGET_ID_INVALID;
    BM_Target_Id iface_id = BM_TARGET_ID_INVALID;
    BM_Export_Id demo_export_id = BM_EXPORT_ID_INVALID;
    BM_Export_Id plugin_export_id = BM_EXPORT_ID_INVALID;
    BM_Install_Rule_Id_Span static_header_rules = {0};
    BM_Install_Rule_Id_Span shared_runtime_rules = {0};
    BM_Install_Rule_Id_Span interface_header_rules = {0};

    ASSERT(build_model_write_text_file("install_graph_src/src/app.c", "int main(void) { return 0; }\n"));
    ASSERT(build_model_write_text_file("install_graph_src/src/core_static.c", "int core_static_value(void) { return 1; }\n"));
    ASSERT(build_model_write_text_file("install_graph_src/src/core_shared.c", "int core_shared_value(void) { return 2; }\n"));
    ASSERT(build_model_write_text_file("install_graph_src/include/core_static.h", "int core_static_value(void);\n"));
    ASSERT(build_model_write_text_file("install_graph_src/cmake/DemoConfig.cmake",
                                       "include(\"${CMAKE_CURRENT_LIST_DIR}/DemoTargets.cmake\")\n"));
    ASSERT(build_model_write_text_file("install_graph_src/scripts/tool.sh", "#!/bin/sh\nexit 0\n"));
    ASSERT(build_model_write_text_file("install_graph_src/assets/readme.txt", "root asset\n"));
    ASSERT(build_model_write_text_file("install_graph_src/sub/plugin.c", "int plugin_value(void) { return 3; }\n"));
    ASSERT(build_model_write_text_file("install_graph_src/sub/include/iface.h", "#define IFACE_VALUE 1\n"));
    ASSERT(build_model_write_text_file(
        "install_graph_src/sub/CMakeLists.txt",
        "add_library(plugin MODULE plugin.c)\n"
        "install(TARGETS plugin EXPORT PluginTargets LIBRARY DESTINATION lib/plugins COMPONENT PluginRuntime)\n"
        "add_library(iface INTERFACE)\n"
        "target_include_directories(iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\"\n"
        "  \"$<INSTALL_INTERFACE:include/subiface>\")\n"
        "install(TARGETS iface EXPORT PluginTargets DESTINATION share/meta COMPONENT InterfaceMeta\n"
        "  INCLUDES DESTINATION include/subiface COMPONENT InterfaceHeaders)\n"
        "install(EXPORT PluginTargets NAMESPACE Demo:: DESTINATION lib/cmake/plugin FILE PluginTargets.cmake COMPONENT PluginConfig)\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "install_graph_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("install_graph_src");
    config.binary_dir = nob_sv_from_cstr("install_graph_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_executable(app src/app.c)\n"
        "add_library(core_static STATIC src/core_static.c)\n"
        "set_target_properties(core_static PROPERTIES PUBLIC_HEADER include/core_static.h)\n"
        "add_library(core_shared SHARED src/core_shared.c)\n"
        "install(TARGETS app RUNTIME DESTINATION bin/apps COMPONENT AppRuntime)\n"
        "install(TARGETS core_static EXPORT DemoTargets\n"
        "  ARCHIVE DESTINATION lib/static COMPONENT StaticDevelopment\n"
        "  PUBLIC_HEADER DESTINATION include/static COMPONENT StaticHeaders)\n"
        "install(TARGETS core_shared EXPORT DemoTargets\n"
        "  LIBRARY DESTINATION lib/shared COMPONENT SharedRuntime\n"
        "  RUNTIME DESTINATION bin/shared COMPONENT SharedRuntime\n"
        "  ARCHIVE DESTINATION lib/import COMPONENT SharedImport\n"
        "  NAMELINK_COMPONENT SharedDev)\n"
        "install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo RENAME DemoPkgConfig.cmake COMPONENT DemoConfig)\n"
        "install(PROGRAMS scripts/tool.sh DESTINATION bin/tools RENAME demo-tool COMPONENT RuntimeTools)\n"
        "install(DIRECTORY assets DESTINATION share/root COMPONENT RuntimeTree)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT DemoConfig)\n"
        "add_subdirectory(sub)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    root_dir_id = build_model_find_directory_id(model,
                                                nob_sv_from_cstr("install_graph_src"),
                                                nob_sv_from_cstr("install_graph_build"));
    sub_dir_id = build_model_find_directory_id(model,
                                               nob_sv_from_cstr("install_graph_src/sub"),
                                               nob_sv_from_cstr("install_graph_build/sub"));
    app_id = bm_query_target_by_name(model, nob_sv_from_cstr("app"));
    core_static_id = bm_query_target_by_name(model, nob_sv_from_cstr("core_static"));
    core_shared_id = bm_query_target_by_name(model, nob_sv_from_cstr("core_shared"));
    plugin_id = bm_query_target_by_name(model, nob_sv_from_cstr("plugin"));
    iface_id = bm_query_target_by_name(model, nob_sv_from_cstr("iface"));

    ASSERT(root_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(sub_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(app_id != BM_TARGET_ID_INVALID);
    ASSERT(core_static_id != BM_TARGET_ID_INVALID);
    ASSERT(core_shared_id != BM_TARGET_ID_INVALID);
    ASSERT(plugin_id != BM_TARGET_ID_INVALID);
    ASSERT(iface_id != BM_TARGET_ID_INVALID);

    for (size_t export_index = 0; export_index < bm_query_export_count(model); ++export_index) {
        BM_Export_Id export_id = (BM_Export_Id)export_index;
        String_View name = bm_query_export_name(model, export_id);
        if (nob_sv_eq(name, nob_sv_from_cstr("DemoTargets"))) {
            demo_export_id = export_id;
        } else if (nob_sv_eq(name, nob_sv_from_cstr("PluginTargets"))) {
            plugin_export_id = export_id;
        }
    }

    ASSERT(demo_export_id != BM_EXPORT_ID_INVALID);
    ASSERT(plugin_export_id != BM_EXPORT_ID_INVALID);
    ASSERT(bm_query_install_rule_count(model) == 8);

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)0) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)0) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)0), nob_sv_from_cstr("app")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)0) == app_id);
    ASSERT(bm_query_target_kind(model, app_id) == BM_TARGET_EXECUTABLE);
    ASSERT(nob_sv_eq(bm_query_install_rule_runtime_destination(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("bin/apps")));
    ASSERT(nob_sv_eq(bm_query_install_rule_runtime_component(model, (BM_Install_Rule_Id)0),
                     nob_sv_from_cstr("AppRuntime")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)1) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)1) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("core_static")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)1) == core_static_id);
    ASSERT(bm_query_target_kind(model, core_static_id) == BM_TARGET_STATIC_LIBRARY);
    ASSERT(nob_sv_eq(bm_query_install_rule_export_name(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_destination(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("lib/static")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_component(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("StaticDevelopment")));
    ASSERT(nob_sv_eq(bm_query_install_rule_public_header_destination(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("include/static")));
    ASSERT(nob_sv_eq(bm_query_install_rule_public_header_component(model, (BM_Install_Rule_Id)1),
                     nob_sv_from_cstr("StaticHeaders")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)2) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)2) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("core_shared")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)2) == core_shared_id);
    ASSERT(bm_query_target_kind(model, core_shared_id) == BM_TARGET_SHARED_LIBRARY);
    ASSERT(nob_sv_eq(bm_query_install_rule_export_name(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_destination(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("lib/import")));
    ASSERT(nob_sv_eq(bm_query_install_rule_library_destination(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("lib/shared")));
    ASSERT(nob_sv_eq(bm_query_install_rule_runtime_destination(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("bin/shared")));
    ASSERT(nob_sv_eq(bm_query_install_rule_archive_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("SharedImport")));
    ASSERT(nob_sv_eq(bm_query_install_rule_library_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("SharedRuntime")));
    ASSERT(nob_sv_eq(bm_query_install_rule_runtime_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("SharedRuntime")));
    ASSERT(nob_sv_eq(bm_query_install_rule_namelink_component(model, (BM_Install_Rule_Id)2),
                     nob_sv_from_cstr("SharedDev")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)3) == BM_INSTALL_RULE_FILE);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)3) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)3),
                     nob_sv_from_cstr("cmake/DemoConfig.cmake")));
    ASSERT(nob_sv_eq(bm_query_install_rule_destination(model, (BM_Install_Rule_Id)3),
                     nob_sv_from_cstr("lib/cmake/demo")));
    ASSERT(nob_sv_eq(bm_query_install_rule_rename(model, (BM_Install_Rule_Id)3),
                     nob_sv_from_cstr("DemoPkgConfig.cmake")));
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)3),
                     nob_sv_from_cstr("DemoConfig")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)3) == BM_TARGET_ID_INVALID);

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)4) == BM_INSTALL_RULE_PROGRAM);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)4) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)4),
                     nob_sv_from_cstr("scripts/tool.sh")));
    ASSERT(nob_sv_eq(bm_query_install_rule_destination(model, (BM_Install_Rule_Id)4),
                     nob_sv_from_cstr("bin/tools")));
    ASSERT(nob_sv_eq(bm_query_install_rule_rename(model, (BM_Install_Rule_Id)4),
                     nob_sv_from_cstr("demo-tool")));
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)4),
                     nob_sv_from_cstr("RuntimeTools")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)5) == BM_INSTALL_RULE_DIRECTORY);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)5) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)5),
                     nob_sv_from_cstr("assets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_destination(model, (BM_Install_Rule_Id)5),
                     nob_sv_from_cstr("share/root")));
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)5),
                     nob_sv_from_cstr("RuntimeTree")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)6) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)6) == sub_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)6),
                     nob_sv_from_cstr("plugin")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)6) == plugin_id);
    ASSERT(bm_query_target_kind(model, plugin_id) == BM_TARGET_MODULE_LIBRARY);
    ASSERT(nob_sv_eq(bm_query_install_rule_export_name(model, (BM_Install_Rule_Id)6),
                     nob_sv_from_cstr("PluginTargets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_library_destination(model, (BM_Install_Rule_Id)6),
                     nob_sv_from_cstr("lib/plugins")));
    ASSERT(nob_sv_eq(bm_query_install_rule_library_component(model, (BM_Install_Rule_Id)6),
                     nob_sv_from_cstr("PluginRuntime")));

    ASSERT(bm_query_install_rule_kind(model, (BM_Install_Rule_Id)7) == BM_INSTALL_RULE_TARGET);
    ASSERT(bm_query_install_rule_owner_directory(model, (BM_Install_Rule_Id)7) == sub_dir_id);
    ASSERT(nob_sv_eq(bm_query_install_rule_item_raw(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("iface")));
    ASSERT(bm_query_install_rule_target(model, (BM_Install_Rule_Id)7) == iface_id);
    ASSERT(bm_query_target_kind(model, iface_id) == BM_TARGET_INTERFACE_LIBRARY);
    ASSERT(nob_sv_eq(bm_query_install_rule_export_name(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("PluginTargets")));
    ASSERT(nob_sv_eq(bm_query_install_rule_destination(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("share/meta")));
    ASSERT(nob_sv_eq(bm_query_install_rule_component(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("InterfaceMeta")));
    ASSERT(nob_sv_eq(bm_query_install_rule_includes_destination(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("include/subiface")));
    ASSERT(nob_sv_eq(bm_query_install_rule_includes_component(model, (BM_Install_Rule_Id)7),
                     nob_sv_from_cstr("InterfaceHeaders")));

    static_header_rules = bm_query_install_rules_for_component(model,
                                                               nob_sv_from_cstr("StaticHeaders"),
                                                               fixture.scratch_arena);
    shared_runtime_rules = bm_query_install_rules_for_component(model,
                                                                nob_sv_from_cstr("SharedRuntime"),
                                                                fixture.scratch_arena);
    interface_header_rules = bm_query_install_rules_for_component(model,
                                                                  nob_sv_from_cstr("InterfaceHeaders"),
                                                                  fixture.scratch_arena);
    ASSERT(static_header_rules.count == 1);
    ASSERT(static_header_rules.items[0] == (BM_Install_Rule_Id)1);
    ASSERT(shared_runtime_rules.count == 1);
    ASSERT(shared_runtime_rules.items[0] == (BM_Install_Rule_Id)2);
    ASSERT(interface_header_rules.count == 1);
    ASSERT(interface_header_rules.items[0] == (BM_Install_Rule_Id)7);

    ASSERT(bm_query_install_rule_for_export_target(model, demo_export_id, core_static_id) == (BM_Install_Rule_Id)1);
    ASSERT(bm_query_install_rule_for_export_target(model, demo_export_id, core_shared_id) == (BM_Install_Rule_Id)2);
    ASSERT(bm_query_install_rule_for_export_target(model, plugin_export_id, plugin_id) == (BM_Install_Rule_Id)6);
    ASSERT(bm_query_install_rule_for_export_target(model, plugin_export_id, iface_id) == (BM_Install_Rule_Id)7);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_standalone_export_queries_cover_build_tree_and_package_registry) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Directory_Id root_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id sub_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Target_Id core_id = BM_TARGET_ID_INVALID;
    BM_Target_Id helper_id = BM_TARGET_ID_INVALID;
    BM_Export_Id install_export_id = BM_EXPORT_ID_INVALID;
    BM_Export_Id targets_export_id = BM_EXPORT_ID_INVALID;
    BM_Export_Id export_set_id = BM_EXPORT_ID_INVALID;
    BM_Export_Id package_export_id = BM_EXPORT_ID_INVALID;
    BM_Target_Id_Span export_targets = {0};
    BM_Export_Id_Span component_exports = {0};
    String_View export_name = {0};

    ASSERT(build_model_write_text_file("standalone_export_src/core.c", "int core_value(void) { return 41; }\n"));
    ASSERT(build_model_write_text_file("standalone_export_src/sub/helper.c", "int helper_value(void) { return 1; }\n"));
    ASSERT(build_model_write_text_file("standalone_export_src/sub/CMakeLists.txt",
                                       "add_library(helper STATIC helper.c)\n"
                                       "install(TARGETS helper EXPORT DemoTargets ARCHIVE DESTINATION lib/sub COMPONENT Development)\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "standalone_export_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("standalone_export_src");
    config.binary_dir = nob_sv_from_cstr("standalone_export_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "project(Test LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES EXPORT_NAME api)\n"
        "add_subdirectory(sub)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib COMPONENT Development)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT Development)\n"
        "export(TARGETS core helper FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/StandaloneTargets.cmake NAMESPACE Demo::)\n"
        "export(EXPORT DemoTargets FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/InstallSetTargets.cmake NAMESPACE Demo::)\n"
        "cmake_policy(SET CMP0090 NEW)\n"
        "set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)\n"
        "export(PACKAGE DemoPkg)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    root_dir_id = build_model_find_directory_id(model,
                                                nob_sv_from_cstr("standalone_export_src"),
                                                nob_sv_from_cstr("standalone_export_build"));
    sub_dir_id = build_model_find_directory_id(model,
                                               nob_sv_from_cstr("standalone_export_src/sub"),
                                               nob_sv_from_cstr("standalone_export_build/sub"));
    core_id = bm_query_target_by_name(model, nob_sv_from_cstr("core"));
    helper_id = bm_query_target_by_name(model, nob_sv_from_cstr("helper"));
    ASSERT(root_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(sub_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(core_id != BM_TARGET_ID_INVALID);
    ASSERT(helper_id != BM_TARGET_ID_INVALID);
    ASSERT(bm_query_target_modeled_property_value(model,
                                                  core_id,
                                                  nob_sv_from_cstr("EXPORT_NAME"),
                                                  fixture.scratch_arena,
                                                  &export_name));
    ASSERT(nob_sv_eq(export_name, nob_sv_from_cstr("api")));

    ASSERT(bm_query_export_count(model) == 4);
    for (size_t i = 0; i < bm_query_export_count(model); ++i) {
        BM_Export_Id export_id = (BM_Export_Id)i;
        if (bm_query_export_kind(model, export_id) == BM_EXPORT_INSTALL) {
            install_export_id = export_id;
        } else if (bm_query_export_kind(model, export_id) == BM_EXPORT_BUILD_TREE &&
                   bm_query_export_source_kind(model, export_id) == BM_EXPORT_SOURCE_TARGETS) {
            targets_export_id = export_id;
        } else if (bm_query_export_kind(model, export_id) == BM_EXPORT_BUILD_TREE &&
                   bm_query_export_source_kind(model, export_id) == BM_EXPORT_SOURCE_EXPORT_SET) {
            export_set_id = export_id;
        } else if (bm_query_export_kind(model, export_id) == BM_EXPORT_PACKAGE_REGISTRY) {
            package_export_id = export_id;
        }
    }

    ASSERT(install_export_id != BM_EXPORT_ID_INVALID);
    ASSERT(targets_export_id != BM_EXPORT_ID_INVALID);
    ASSERT(export_set_id != BM_EXPORT_ID_INVALID);
    ASSERT(package_export_id != BM_EXPORT_ID_INVALID);

    ASSERT(bm_query_export_source_kind(model, install_export_id) == BM_EXPORT_SOURCE_INSTALL_EXPORT);
    ASSERT(bm_query_export_owner_directory(model, install_export_id) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_export_name(model, install_export_id), nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_export_namespace(model, install_export_id), nob_sv_from_cstr("Demo::")));
    ASSERT(nob_sv_eq(bm_query_export_destination(model, install_export_id), nob_sv_from_cstr("lib/cmake/demo")));
    ASSERT(nob_sv_eq(bm_query_export_file_name(model, install_export_id), nob_sv_from_cstr("DemoTargets.cmake")));
    ASSERT(nob_sv_eq(bm_query_export_component(model, install_export_id), nob_sv_from_cstr("Development")));
    ASSERT(nob_sv_eq(bm_query_export_output_file_path(model, install_export_id, fixture.scratch_arena),
                     nob_sv_from_cstr("lib/cmake/demo/DemoTargets.cmake")));
    ASSERT(bm_query_export_enabled(model, install_export_id));
    ASSERT(!bm_query_export_append(model, install_export_id));
    ASSERT(bm_query_export_cxx_modules_directory(model, install_export_id).count == 0);
    export_targets = bm_query_export_targets(model, install_export_id);
    ASSERT(export_targets.count == 2);
    ASSERT(build_model_target_id_span_contains(export_targets, core_id));
    ASSERT(build_model_target_id_span_contains(export_targets, helper_id));

    ASSERT(bm_query_export_kind(model, targets_export_id) == BM_EXPORT_BUILD_TREE);
    ASSERT(bm_query_export_owner_directory(model, targets_export_id) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_export_name(model, targets_export_id), nob_sv_from_cstr("StandaloneTargets")));
    ASSERT(nob_sv_eq(bm_query_export_namespace(model, targets_export_id), nob_sv_from_cstr("Demo::")));
    ASSERT(nob_sv_eq(bm_query_export_file_name(model, targets_export_id), nob_sv_from_cstr("StandaloneTargets.cmake")));
    ASSERT(build_model_sv_contains(bm_query_export_output_file_path(model, targets_export_id, fixture.scratch_arena),
                                   nob_sv_from_cstr("exports/StandaloneTargets.cmake")));
    ASSERT(bm_query_export_destination(model, targets_export_id).count == 0);
    ASSERT(bm_query_export_component(model, targets_export_id).count == 0);
    ASSERT(bm_query_export_enabled(model, targets_export_id));
    ASSERT(!bm_query_export_append(model, targets_export_id));
    ASSERT(bm_query_export_cxx_modules_directory(model, targets_export_id).count == 0);
    export_targets = bm_query_export_targets(model, targets_export_id);
    ASSERT(export_targets.count == 2);
    ASSERT(build_model_target_id_span_contains(export_targets, core_id));
    ASSERT(build_model_target_id_span_contains(export_targets, helper_id));

    ASSERT(bm_query_export_kind(model, export_set_id) == BM_EXPORT_BUILD_TREE);
    ASSERT(bm_query_export_owner_directory(model, export_set_id) == root_dir_id);
    ASSERT(nob_sv_eq(bm_query_export_name(model, export_set_id), nob_sv_from_cstr("DemoTargets")));
    ASSERT(nob_sv_eq(bm_query_export_file_name(model, export_set_id), nob_sv_from_cstr("InstallSetTargets.cmake")));
    ASSERT(build_model_sv_contains(bm_query_export_output_file_path(model, export_set_id, fixture.scratch_arena),
                                   nob_sv_from_cstr("exports/InstallSetTargets.cmake")));
    ASSERT(bm_query_export_destination(model, export_set_id).count == 0);
    ASSERT(bm_query_export_component(model, export_set_id).count == 0);
    ASSERT(bm_query_export_enabled(model, export_set_id));
    ASSERT(!bm_query_export_append(model, export_set_id));
    ASSERT(bm_query_export_cxx_modules_directory(model, export_set_id).count == 0);
    export_targets = bm_query_export_targets(model, export_set_id);
    ASSERT(export_targets.count == 1);
    ASSERT(build_model_target_id_span_contains(export_targets, core_id));

    ASSERT(bm_query_export_source_kind(model, package_export_id) == BM_EXPORT_SOURCE_PACKAGE);
    ASSERT(bm_query_export_owner_directory(model, package_export_id) == root_dir_id);
    ASSERT(bm_query_export_enabled(model, package_export_id));
    ASSERT(nob_sv_eq(bm_query_export_package_name(model, package_export_id), nob_sv_from_cstr("DemoPkg")));
    ASSERT(build_model_sv_contains(bm_query_export_registry_prefix(model, package_export_id),
                                   nob_sv_from_cstr("standalone_export_build")));
    ASSERT(!bm_query_export_append(model, package_export_id));
    ASSERT(bm_query_export_cxx_modules_directory(model, package_export_id).count == 0);

    component_exports = bm_query_exports_for_component(model,
                                                       nob_sv_from_cstr("Development"),
                                                       fixture.scratch_arena);
    ASSERT(component_exports.count == 1);
    ASSERT(component_exports.items[0] == install_export_id);
    ASSERT(bm_query_install_rule_for_export_target(model, install_export_id, core_id) != BM_INSTALL_RULE_ID_INVALID);
    ASSERT(bm_query_install_rule_for_export_target(model, install_export_id, helper_id) != BM_INSTALL_RULE_ID_INVALID);
    ASSERT(bm_query_target_owner_directory(model, helper_id) == sub_dir_id);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_package_find_results_freeze_query_surface_and_nested_owner) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    const Build_Model *model = NULL;
    BM_Directory_Id root_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id sub_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Package_Id module_id = BM_PACKAGE_ID_INVALID;
    BM_Package_Id config_id = BM_PACKAGE_ID_INVALID;
    BM_Package_Id missing_id = BM_PACKAGE_ID_INVALID;

    ASSERT(build_model_write_text_file("package_find_src/cmake/FindModulePkg.cmake",
                                       "set(ModulePkg_FOUND 1)\n"));
    ASSERT(build_model_write_text_file("package_find_src/sub/prefix/ConfigPkgConfig.cmake",
                                       "set(ConfigPkg_FOUND 1)\n"));
    ASSERT(build_model_write_text_file("package_find_src/sub/CMakeLists.txt",
                                       "find_package(ConfigPkg REQUIRED CONFIG PATHS prefix NO_DEFAULT_PATH)\n"));

    test_semantic_pipeline_config_init(&config);
    config.current_file = "package_find_src/CMakeLists.txt";
    config.source_dir = nob_sv_from_cstr("package_find_src");
    config.binary_dir = nob_sv_from_cstr("package_find_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &fixture,
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(Test LANGUAGES NONE)\n"
        "set(CMAKE_MODULE_PATH cmake)\n"
        "find_package(ModulePkg QUIET MODULE)\n"
        "find_package(MissingPkg QUIET CONFIG)\n"
        "add_subdirectory(sub)\n",
        &config));
    ASSERT(fixture.eval_ok);
    ASSERT(fixture.build.freeze_ok);
    ASSERT(fixture.build.model != NULL);

    model = fixture.build.model;
    root_dir_id = build_model_find_directory_id(model,
                                                nob_sv_from_cstr("package_find_src"),
                                                nob_sv_from_cstr("package_find_build"));
    sub_dir_id = build_model_find_directory_id(model,
                                               nob_sv_from_cstr("package_find_src/sub"),
                                               nob_sv_from_cstr("package_find_build/sub"));
    module_id = bm_query_package_by_name(model, nob_sv_from_cstr("ModulePkg"));
    config_id = bm_query_package_by_name(model, nob_sv_from_cstr("ConfigPkg"));
    missing_id = bm_query_package_by_name(model, nob_sv_from_cstr("MissingPkg"));

    ASSERT(root_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(sub_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(bm_query_package_count(model) == 3);
    ASSERT(module_id != BM_PACKAGE_ID_INVALID);
    ASSERT(config_id != BM_PACKAGE_ID_INVALID);
    ASSERT(missing_id != BM_PACKAGE_ID_INVALID);

    ASSERT(nob_sv_eq(bm_query_package_name(model, module_id), nob_sv_from_cstr("ModulePkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(model, module_id), nob_sv_from_cstr("MODULE")));
    ASSERT(build_model_sv_contains(bm_query_package_found_path(model, module_id),
                                   nob_sv_from_cstr("FindModulePkg.cmake")));
    ASSERT(bm_query_package_found(model, module_id));
    ASSERT(!bm_query_package_required(model, module_id));
    ASSERT(bm_query_package_quiet(model, module_id));
    ASSERT(bm_query_package_owner_directory(model, module_id) == root_dir_id);

    ASSERT(nob_sv_eq(bm_query_package_name(model, config_id), nob_sv_from_cstr("ConfigPkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(model, config_id), nob_sv_from_cstr("CONFIG")));
    ASSERT(build_model_sv_contains(bm_query_package_found_path(model, config_id),
                                   nob_sv_from_cstr("ConfigPkgConfig.cmake")));
    ASSERT(bm_query_package_found(model, config_id));
    ASSERT(bm_query_package_required(model, config_id));
    ASSERT(!bm_query_package_quiet(model, config_id));
    ASSERT(bm_query_package_owner_directory(model, config_id) == sub_dir_id);

    ASSERT(nob_sv_eq(bm_query_package_name(model, missing_id), nob_sv_from_cstr("MissingPkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(model, missing_id), nob_sv_from_cstr("CONFIG")));
    ASSERT(bm_query_package_found_path(model, missing_id).count == 0);
    ASSERT(!bm_query_package_found(model, missing_id));
    ASSERT(!bm_query_package_required(model, missing_id));
    ASSERT(bm_query_package_quiet(model, missing_id));
    ASSERT(bm_query_package_owner_directory(model, missing_id) == root_dir_id);

    test_semantic_pipeline_fixture_destroy(&fixture);
    TEST_PASS();
}

TEST(build_model_package_find_results_preserve_redirect_registry_and_provider_resolution) {
    Test_Semantic_Pipeline_Config redirect_config = {0};
    Test_Semantic_Pipeline_Config provider_config = {0};
    Test_Semantic_Pipeline_Fixture redirect_fixture = {0};
    Test_Semantic_Pipeline_Fixture provider_fixture = {0};
    const Build_Model *redirect_model = NULL;
    const Build_Model *provider_model = NULL;
    BM_Directory_Id redirect_root_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Directory_Id provider_root_dir_id = BM_DIRECTORY_ID_INVALID;
    BM_Package_Id redirect_id = BM_PACKAGE_ID_INVALID;
    BM_Package_Id registry_id = BM_PACKAGE_ID_INVALID;
    BM_Package_Id provided_id = BM_PACKAGE_ID_INVALID;

    ASSERT(build_model_write_text_file("redirect_src/CMakeLists.txt",
                                       "set(RedirectPkg_FOUND 1)\n"
                                       "add_library(redirect_pkg_target INTERFACE)\n"));
    ASSERT(build_model_write_text_file("provider_top.cmake",
                                       "macro(dep_provider method)\n"
                                       "  if(method STREQUAL \"FIND_PACKAGE\")\n"
                                       "    if(ARGV1 STREQUAL \"ProvidedPkg\")\n"
                                       "      set(ProvidedPkg_FOUND 1)\n"
                                       "      set(ProvidedPkg_CONFIG provider://ProvidedPkg)\n"
                                       "    else()\n"
                                       "      find_package(${ARGN} BYPASS_PROVIDER)\n"
                                       "    endif()\n"
                                       "  endif()\n"
                                       "endmacro()\n"
                                       "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FIND_PACKAGE)\n"));

    test_semantic_pipeline_config_init(&redirect_config);
    redirect_config.current_file = "CMakeLists.txt";
    redirect_config.source_dir = nob_sv_from_cstr(".");
    redirect_config.binary_dir = nob_sv_from_cstr("package_special_redirect_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &redirect_fixture,
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(Test LANGUAGES NONE)\n"
        "include(FetchContent)\n"
        "FetchContent_Declare(RedirectPkg SOURCE_DIR redirect_src OVERRIDE_FIND_PACKAGE)\n"
        "FetchContent_MakeAvailable(RedirectPkg)\n"
        "find_package(RedirectPkg CONFIG QUIET)\n"
        "set(ENV{HOME} \"${CMAKE_CURRENT_BINARY_DIR}/home\")\n"
        "set(ENV{USERPROFILE} \"${CMAKE_CURRENT_BINARY_DIR}/home\")\n"
        "file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/RegPkgConfig.cmake\" \"set(RegPkg_FOUND 1)\\n\")\n"
        "cmake_policy(SET CMP0090 NEW)\n"
        "set(CMAKE_EXPORT_PACKAGE_REGISTRY TRUE)\n"
        "export(PACKAGE RegPkg)\n"
        "find_package(RegPkg CONFIG QUIET)\n",
        &redirect_config));
    ASSERT(redirect_fixture.eval_ok);
    ASSERT(redirect_fixture.build.freeze_ok);
    ASSERT(redirect_fixture.build.model != NULL);

    redirect_model = redirect_fixture.build.model;
    redirect_root_dir_id = build_model_find_directory_id(redirect_model,
                                                         nob_sv_from_cstr("."),
                                                         nob_sv_from_cstr("package_special_redirect_build"));
    redirect_id = bm_query_package_by_name(redirect_model, nob_sv_from_cstr("RedirectPkg"));
    registry_id = bm_query_package_by_name(redirect_model, nob_sv_from_cstr("RegPkg"));

    ASSERT(redirect_root_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(bm_query_package_count(redirect_model) >= 2);
    ASSERT(redirect_id != BM_PACKAGE_ID_INVALID);
    ASSERT(registry_id != BM_PACKAGE_ID_INVALID);

    ASSERT(nob_sv_eq(bm_query_package_name(redirect_model, redirect_id), nob_sv_from_cstr("RedirectPkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(redirect_model, redirect_id), nob_sv_from_cstr("CONFIG")));
    ASSERT(bm_query_package_found(redirect_model, redirect_id));
    ASSERT(bm_query_package_quiet(redirect_model, redirect_id));
    ASSERT(bm_query_package_owner_directory(redirect_model, redirect_id) == redirect_root_dir_id);
    ASSERT(build_model_sv_contains(bm_query_package_found_path(redirect_model, redirect_id),
                                   nob_sv_from_cstr("RedirectPkgConfig.cmake")));

    ASSERT(nob_sv_eq(bm_query_package_name(redirect_model, registry_id), nob_sv_from_cstr("RegPkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(redirect_model, registry_id), nob_sv_from_cstr("CONFIG")));
    ASSERT(bm_query_package_found(redirect_model, registry_id));
    ASSERT(bm_query_package_quiet(redirect_model, registry_id));
    ASSERT(bm_query_package_owner_directory(redirect_model, registry_id) == redirect_root_dir_id);
    ASSERT(build_model_sv_contains(bm_query_package_found_path(redirect_model, registry_id),
                                   nob_sv_from_cstr("RegPkgConfig.cmake")));

    test_semantic_pipeline_config_init(&provider_config);
    provider_config.current_file = "CMakeLists.txt";
    provider_config.source_dir = nob_sv_from_cstr(".");
    provider_config.binary_dir = nob_sv_from_cstr("package_special_provider_build");

    ASSERT(test_semantic_pipeline_fixture_from_script(
        &provider_fixture,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES provider_top.cmake)\n"
        "project(Test LANGUAGES NONE)\n"
        "find_package(ProvidedPkg QUIET)\n",
        &provider_config));
    ASSERT(provider_fixture.eval_ok);
    ASSERT(provider_fixture.build.freeze_ok);
    ASSERT(provider_fixture.build.model != NULL);

    provider_model = provider_fixture.build.model;
    provider_root_dir_id = build_model_find_directory_id(provider_model,
                                                         nob_sv_from_cstr("."),
                                                         nob_sv_from_cstr("package_special_provider_build"));
    provided_id = bm_query_package_by_name(provider_model, nob_sv_from_cstr("ProvidedPkg"));

    ASSERT(provider_root_dir_id != BM_DIRECTORY_ID_INVALID);
    ASSERT(bm_query_package_count(provider_model) == 1);
    ASSERT(provided_id != BM_PACKAGE_ID_INVALID);
    ASSERT(nob_sv_eq(bm_query_package_name(provider_model, provided_id), nob_sv_from_cstr("ProvidedPkg")));
    ASSERT(nob_sv_eq(bm_query_package_mode(provider_model, provided_id), nob_sv_from_cstr("AUTO")));
    ASSERT(bm_query_package_found(provider_model, provided_id));
    ASSERT(bm_query_package_quiet(provider_model, provided_id));
    ASSERT(bm_query_package_owner_directory(provider_model, provided_id) == provider_root_dir_id);
    ASSERT(nob_sv_eq(bm_query_package_found_path(provider_model, provided_id),
                     nob_sv_from_cstr("provider://ProvidedPkg")));

    test_semantic_pipeline_fixture_destroy(&redirect_fixture);
    test_semantic_pipeline_fixture_destroy(&provider_fixture);
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
        "set(CPACK_ARCHIVE_FILE_NAME PackMe-archive)\n"
        "set(CPACK_ARCHIVE_FILE_EXTENSION pkg)\n"
        "set(CPACK_ARCHIVE_RUNTIME_FILE_NAME PackMe-runtime)\n"
        "set(CPACK_COMPONENTS_GROUPING IGNORE)\n"
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)\n"
        "set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)\n"
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
    ASSERT(nob_sv_eq(bm_query_cpack_package_archive_file_name(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("PackMe-archive")));
    ASSERT(nob_sv_eq(bm_query_cpack_package_archive_file_extension(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("pkg")));
    ASSERT(nob_sv_eq(bm_query_cpack_package_components_grouping(model, (BM_CPack_Package_Id)0), nob_sv_from_cstr("IGNORE")));
    ASSERT(!bm_query_cpack_package_include_toplevel_directory(model, (BM_CPack_Package_Id)0));
    ASSERT(bm_query_cpack_package_archive_component_install(model, (BM_CPack_Package_Id)0));
    ASSERT(bm_query_cpack_package_archive_name_override_count(model, (BM_CPack_Package_Id)0) == 1);
    ASSERT(nob_sv_eq(bm_query_cpack_package_archive_name_override_key(model, (BM_CPack_Package_Id)0, 0), nob_sv_from_cstr("RUNTIME")));
    ASSERT(nob_sv_eq(bm_query_cpack_package_archive_name_override_file_name(model, (BM_CPack_Package_Id)0, 0), nob_sv_from_cstr("PackMe-runtime")));

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
    test_build_model_build_step_effective_view_resolves_context_and_target_artifacts(passed, failed, skipped);
    test_build_model_custom_command_append_merges_into_original_step_and_rejects_missing_base(passed, failed, skipped);
    test_build_model_known_configurations_include_build_step_genex_fields(passed, failed, skipped);
    test_build_model_target_build_order_view_centralizes_explicit_steps_and_config_link_prereqs(passed, failed, skipped);
    test_build_model_static_library_link_cycle_is_not_an_execution_order_cycle(passed, failed, skipped);
    test_build_model_resolves_direct_and_binary_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_source_and_stripped_generated_source_producers(passed, failed, skipped);
    test_build_model_resolves_byproduct_producers_and_keeps_unresolved_file_dependencies(passed, failed, skipped);
    test_build_model_marks_generated_sources_without_producer_steps(passed, failed, skipped);
    test_build_model_freeze_rejects_duplicate_effective_producers_and_execution_cycles(passed, failed, skipped);
    test_build_model_replay_actions_freeze_query_and_preserve_order(passed, failed, skipped);
    test_build_model_replay_action_resolved_operands_use_query_context(passed, failed, skipped);
    test_build_model_replay_actions_reject_invalid_opcode_payload_shapes(passed, failed, skipped);
    test_build_model_tests_freeze_owner_working_dir_expand_lists_and_configurations(passed, failed, skipped);
    test_build_model_replay_actions_accept_c3_opcodes_and_queries(passed, failed, skipped);
    test_build_model_replay_actions_accept_c5_ctest_coverage_and_memcheck_queries(passed, failed, skipped);
    test_build_model_ctest_memcheck_preserves_registered_test_command_surface(passed, failed, skipped);
    test_build_model_ctest_local_memcheck_relative_command_surface(passed, failed, skipped);
    test_build_model_ctest_external_project_query_preserves_post_memcheck_exists_surface(passed, failed, skipped);
    test_build_model_replay_actions_reject_malformed_ordering(passed, failed, skipped);
    test_build_model_context_aware_queries_expand_usage_requirements_and_target_property_genex(passed, failed, skipped);
    test_build_model_context_queries_support_build_local_install_prefix_target_genex_eval_and_link_literals(passed, failed, skipped);
    test_build_model_same_family_target_property_cycles_fail_deterministically(passed, failed, skipped);
    test_build_model_effective_queries_follow_global_directory_and_transitive_link_library_seeds(passed, failed, skipped);
    test_build_model_platform_context_and_typed_platform_properties_are_queryable(passed, failed, skipped);
    test_build_model_imported_target_queries_resolve_configs_and_mapped_locations(passed, failed, skipped);
    test_build_model_source_effective_language_centralizes_supported_c_and_cxx_classification(passed, failed, skipped);
    test_build_model_effective_link_language_uses_config_platform_imported_mapping_and_session_context(passed, failed, skipped);
    test_build_model_imported_target_paths_already_rooted_in_source_dir_are_not_rebased_twice(passed, failed, skipped);
    test_build_model_imported_target_known_configurations_are_stable_and_deduped(passed, failed, skipped);
    test_build_model_known_configuration_catalog_surfaces_supported_row52_domains(passed, failed, skipped);
    test_build_model_known_configuration_catalog_detects_strequal_config_comparisons(passed, failed, skipped);
    test_build_model_artifact_query_resolves_output_naming_layout_by_config_and_platform(passed, failed, skipped);
    test_build_model_preserves_imported_global_across_property_orderings(passed, failed, skipped);
    test_build_model_alias_and_unknown_target_identity_queries_are_canonical(passed, failed, skipped);
    test_build_model_source_membership_file_sets_and_source_properties_are_canonical(passed, failed, skipped);
    test_build_model_query_session_reuses_effective_item_and_value_results(passed, failed, skipped);
    test_build_model_query_session_splits_effective_contexts_without_merging_semantics(passed, failed, skipped);
    test_build_model_query_session_memoizes_imported_target_resolution(passed, failed, skipped);
    test_build_model_compile_feature_catalog_and_effective_features_are_shared(passed, failed, skipped);
    test_build_model_effective_queries_dedup_and_preserve_first_occurrence(passed, failed, skipped);
    test_build_model_effective_queries_terminate_interface_cycles_without_duplicate_contributions(passed, failed, skipped);
    test_build_model_usage_requirement_property_setters_promote_to_canonical_item_storage(passed, failed, skipped);
    test_build_model_install_and_export_queries_surface_typed_metadata(passed, failed, skipped);
    test_build_model_install_queries_materialize_effective_default_components(passed, failed, skipped);
    test_build_model_install_queries_cover_supported_target_kinds_and_rule_families(passed, failed, skipped);
    test_build_model_standalone_export_queries_cover_build_tree_and_package_registry(passed, failed, skipped);
    test_build_model_package_find_results_freeze_query_surface_and_nested_owner(passed, failed, skipped);
    test_build_model_package_find_results_preserve_redirect_registry_and_provider_resolution(passed, failed, skipped);
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
