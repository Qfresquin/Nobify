#include "test_evaluator_v2_common.h"

TEST(evaluator_load_cache_supports_multi_path_legacy_mode_and_prefix_unset) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_multi_a"));
    ASSERT(nob_mkdir_if_not_exists("cache_multi_b"));
    ASSERT(nob_mkdir_if_not_exists("cache_prefix_empty"));
    ASSERT(nob_write_entire_file("cache_multi_a/CMakeCache.txt",
                                 "FIRST:STRING=one\n"
                                 "SHARED:STRING=from-a\n"
                                 "DROP:STRING=drop-a\n"
                                 "HIDE_A:INTERNAL=secret-a\n",
                                 strlen("FIRST:STRING=one\n"
                                        "SHARED:STRING=from-a\n"
                                        "DROP:STRING=drop-a\n"
                                        "HIDE_A:INTERNAL=secret-a\n")));
    ASSERT(nob_write_entire_file("cache_multi_b/CMakeCache.txt",
                                 "SECOND:BOOL=ON\n"
                                 "SHARED:STRING=from-b\n"
                                 "DROP:STRING=drop-b\n"
                                 "HIDE_B:INTERNAL=secret-b\n",
                                 strlen("SECOND:BOOL=ON\n"
                                        "SHARED:STRING=from-b\n"
                                        "DROP:STRING=drop-b\n"
                                        "HIDE_B:INTERNAL=secret-b\n")));
    ASSERT(nob_write_entire_file("cache_prefix_empty/CMakeCache.txt",
                                 "EMPTY:STRING=\n"
                                 "KEEP:STRING=keep-prefix\n",
                                 strlen("EMPTY:STRING=\n"
                                        "KEEP:STRING=keep-prefix\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(PFX_EMPTY sentinel)\n"
        "load_cache(cache_multi_a cache_multi_b INCLUDE_INTERNALS HIDE_A HIDE_B EXCLUDE DROP)\n"
        "load_cache(cache_prefix_empty READ_WITH_PREFIX PFX_ EMPTY KEEP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FIRST")), nob_sv_from_cstr("one")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SECOND")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SHARED")), nob_sv_from_cstr("from-b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("HIDE_A")), nob_sv_from_cstr("secret-a")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("HIDE_B")), nob_sv_from_cstr("secret-b")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("DROP")));

    ASSERT(!eval_var_defined_visible(ctx, nob_sv_from_cstr("PFX_EMPTY")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PFX_KEEP")),
                     nob_sv_from_cstr("keep-prefix")));
    ASSERT(!eval_cache_defined(ctx, nob_sv_from_cstr("PFX_KEEP")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("modules"));
    ASSERT(nob_mkdir_if_not_exists("include"));
    ASSERT(nob_write_entire_file("meta_impl.cpp", "int meta_impl = 0;\n", strlen("int meta_impl = 0;\n")));
    ASSERT(nob_write_entire_file("modules/core.cppm", "export module core;\n", strlen("export module core;\n")));
    ASSERT(nob_write_entire_file("include/meta.hpp", "#pragma once\n", strlen("#pragma once\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(meta_lib STATIC meta_impl.cpp)\n"
        "target_sources(meta_lib PUBLIC FILE_SET mods TYPE CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_include_directories(meta_lib PUBLIC include)\n"
        "target_compile_definitions(meta_lib PUBLIC META_DEF=1)\n"
        "target_compile_options(meta_lib PUBLIC -Wall)\n"
        "target_compile_features(meta_lib PUBLIC cxx_std_20)\n"
        "target_link_libraries(meta_lib PUBLIC dep::lib)\n"
        "set_target_properties(meta_lib PROPERTIES EXPORT_NAME meta-export-name CXX_EXTENSIONS OFF)\n"
        "install(TARGETS meta_lib EXPORT DemoExport FILE_SET mods DESTINATION include/modules)\n"
        "export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo:: CXX_MODULES_DIRECTORY cxx-modules)\n"
        "export(EXPORT DemoExport NAMESPACE Demo:: CXX_MODULES_DIRECTORY export-modules)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_DIRECTORY")),
                     nob_sv_from_cstr("export-modules")));
    ASSERT(nob_sv_eq(eval_var_get(ctx,
                                  nob_sv_from_cstr("NOBIFY_EXPORT_LAST_CXX_MODULES_NAME")),
                     nob_sv_from_cstr("DemoExport")));

    String_View export_targets = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "meta-targets.cmake", &export_targets));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULES_DIRECTORY \"cxx-modules\")")));
    ASSERT(sv_contains_sv(export_targets,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules/cxx-modules-cc9f26f1f4e6.cmake\")")));

    String_View targets_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6.cmake",
                                             &targets_trampoline));
    ASSERT(sv_contains_sv(targets_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-cc9f26f1f4e6-noconfig.cmake\")")));

    String_View targets_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/cxx-modules-cc9f26f1f4e6-noconfig.cmake",
                                             &targets_config));
    ASSERT(sv_contains_sv(targets_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    String_View target_modules = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "cxx-modules/target-meta-export-name-noconfig.cmake",
                                             &target_modules));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_TARGET \"Demo::meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_EXPORT_NAME \"meta-export-name\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SETS \"mods\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_SET_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_DIRS_MODS \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("modules")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_INCLUDE_DIRECTORIES \"")));
    ASSERT(sv_contains_sv(target_modules, nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_DEFINITIONS \"META_DEF=1\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_OPTIONS \"-Wall\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_COMPILE_FEATURES \"cxx_std_20\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_LINK_LIBRARIES \"dep::lib\")")));
    ASSERT(sv_contains_sv(target_modules,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_CXX_MODULE_CXX_EXTENSIONS \"OFF\")")));

    String_View export_default = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "DemoExport.cmake", &export_default));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("set(NOBIFY_EXPORT_NAME \"DemoExport\")")));
    ASSERT(sv_contains_sv(export_default,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/export-modules/cxx-modules-DemoExport.cmake\")")));

    String_View export_trampoline = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport.cmake",
                                             &export_trampoline));
    ASSERT(sv_contains_sv(export_trampoline,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/cxx-modules-DemoExport-noconfig.cmake\")")));

    String_View export_config = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena,
                                             "export-modules/cxx-modules-DemoExport-noconfig.cmake",
                                             &export_config));
    ASSERT(sv_contains_sv(export_config,
                          nob_sv_from_cstr("include(\"${CMAKE_CURRENT_LIST_DIR}/target-meta-export-name-noconfig.cmake\")")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_export_rejects_invalid_extension_and_alias_targets) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real INTERFACE)\n"
        "add_library(alias_real ALIAS real)\n"
        "export(TARGETS real FILE bad-export.txt)\n"
        "export(TARGETS alias_real FILE alias-export.cmake)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_bad_extension = false;
    bool saw_alias_reject = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(... FILE ...) requires a filename ending in .cmake"))) {
            saw_bad_extension = true;
        }
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("export(TARGETS ...) may not export ALIAS targets"))) {
            saw_alias_reject = true;
        }
    }
    ASSERT(saw_bad_extension);
    ASSERT(saw_alias_reject);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_models_metadata_and_safe_local_effects) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ctest_bin"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe"));
    ASSERT(nob_mkdir_if_not_exists("ctest_bin/wipe/sub"));
    ASSERT(nob_mkdir_if_not_exists("ctest_src"));
    ASSERT(nob_mkdir_if_not_exists("ctest_custom"));
    ASSERT(nob_write_entire_file("ctest_bin/a.txt", "A\n", strlen("A\n")));
    ASSERT(nob_write_entire_file("ctest_bin/b.txt", "B\n", strlen("B\n")));
    ASSERT(nob_write_entire_file("ctest_bin/notes.txt", "NOTES\n", strlen("NOTES\n")));
    ASSERT(nob_write_entire_file("ctest_bin/wipe/sub/junk.txt", "junk\n", strlen("junk\n")));
    ASSERT(nob_write_entire_file("ctest_custom/CTestCustom.cmake",
                                 "set(CTEST_CUSTOM_LOADED yes)\n",
                                 strlen("set(CTEST_CUSTOM_LOADED yes)\n")));
    ASSERT(nob_write_entire_file("ctest_script.cmake",
                                 "set(CTEST_SCRIPT_LOADED 1)\n",
                                 strlen("set(CTEST_SCRIPT_LOADED 1)\n")));
    ASSERT(nob_write_entire_file("ctest_script_child.cmake",
                                 "set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                 "function(ctest_child_only_fn)\n"
                                 "endfunction()\n"
                                 "set(CTEST_TRACK ChildTrack)\n",
                                 strlen("set(CTEST_SCRIPT_CHILD_ONLY 1)\n"
                                        "function(ctest_child_only_fn)\n"
                                        "endfunction()\n"
                                        "set(CTEST_TRACK ChildTrack)\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR ctest_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR ctest_bin)\n"
        "ctest_start(Experimental ctest_src . TRACK Nightly APPEND)\n"
        "ctest_configure(RETURN_VALUE CFG_RV CAPTURE_CMAKE_ERROR CFG_CE QUIET)\n"
        "ctest_build(TARGET all NUMBER_ERRORS BUILD_ERRS NUMBER_WARNINGS BUILD_WARNS RETURN_VALUE BUILD_RV CAPTURE_CMAKE_ERROR BUILD_CE APPEND)\n"
        "ctest_test(RETURN_VALUE TEST_RV CAPTURE_CMAKE_ERROR TEST_CE PARALLEL_LEVEL 2 SCHEDULE_RANDOM)\n"
        "ctest_coverage(LABELS core ui RETURN_VALUE COV_RV CAPTURE_CMAKE_ERROR COV_CE)\n"
        "ctest_memcheck(RETURN_VALUE MEM_RV CAPTURE_CMAKE_ERROR MEM_CE DEFECT_COUNT MEM_DEFECTS SCHEDULE_RANDOM)\n"
        "ctest_update(RETURN_VALUE UPD_RV CAPTURE_CMAKE_ERROR UPD_CE QUIET)\n"
        "ctest_submit(PARTS Start Build Test FILES notes.txt RETURN_VALUE SUB_RV CAPTURE_CMAKE_ERROR SUB_CE)\n"
        "ctest_upload(FILES a.txt b.txt CAPTURE_CMAKE_ERROR UPLOAD_CE)\n"
        "ctest_empty_binary_directory(wipe)\n"
        "ctest_read_custom_files(ctest_custom)\n"
        "ctest_run_script(ctest_script.cmake RETURN_VALUE SCRIPT_RV)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_child.cmake RETURN_VALUE SCRIPT_CHILD_RV)\n"
        "if(DEFINED CTEST_SCRIPT_CHILD_ONLY)\n"
        "  set(CHILD_VAR_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_VAR_LEAK 0)\n"
        "endif()\n"
        "if(COMMAND ctest_child_only_fn)\n"
        "  set(CHILD_FN_LEAK 1)\n"
        "else()\n"
        "  set(CHILD_FN_LEAK 0)\n"
        "endif()\n"
        "ctest_sleep(0.25)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_LAST_COMMAND")),
                     nob_sv_from_cstr("ctest_sleep")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TRACK")),
                     nob_sv_from_cstr("Nightly")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_MODEL")),
                     nob_sv_from_cstr("Experimental")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_TRACK")),
                     nob_sv_from_cstr("Nightly")));
    String_View ctest_tag = eval_var_get(ctx, nob_sv_from_cstr("CTEST_TAG"));
    ASSERT(ctest_tag.count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::PARTS")),
                     nob_sv_from_cstr("Start;Build;Test")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_empty_binary_directory::STATUS")),
                     nob_sv_from_cstr("CLEARED")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CFG_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CFG_CE")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BUILD_ERRS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BUILD_WARNS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("MEM_DEFECTS")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_CHILD_RV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_VAR_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_FN_LEAK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_CUSTOM_LOADED")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_LOADED")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SCRIPT_CHILD_ONLY")).count == 0);
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("CTEST_SOURCE_DIRECTORY")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("CTEST_BINARY_DIRECTORY")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_configure::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_build::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_test::RESOLVED_BUILD")),
                          nob_sv_from_cstr("ctest_bin")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_update::RESOLVED_SOURCE")),
                          nob_sv_from_cstr("ctest_src")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST_SESSION::TAG")), ctest_tag));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE")),
                          nob_sv_from_cstr("ctest_bin/Testing/TAG")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/notes.txt")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/a.txt")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::RESOLVED_FILES")),
                          nob_sv_from_cstr("ctest_bin/b.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::EXECUTION_MODE")),
                     nob_sv_from_cstr("NEW_PROCESS")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_run_script::RESOLVED_SCRIPTS")),
                          nob_sv_from_cstr("ctest_script_child.cmake")));

    String_View tag_file = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_start::TAG_FILE"));
    char *tag_file_c = arena_strndup(temp_arena, tag_file.data, tag_file.count);
    ASSERT(tag_file_c != NULL);
    ASSERT(nob_file_exists(tag_file_c));

    Nob_String_Builder tag_sb = {0};
    ASSERT(nob_read_entire_file(tag_file_c, &tag_sb));
    ASSERT(strstr(tag_sb.items, "Experimental") != NULL);
    ASSERT(strstr(tag_sb.items, tag_file_c) == NULL);
    ASSERT(memmem(tag_sb.items, tag_sb.count, ctest_tag.data, ctest_tag.count) != NULL);
    nob_sb_free(tag_sb);

    String_View submit_manifest = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_submit::MANIFEST"));
    char *submit_manifest_c = arena_strndup(temp_arena, submit_manifest.data, submit_manifest.count);
    ASSERT(submit_manifest_c != NULL);
    ASSERT(nob_file_exists(submit_manifest_c));

    Nob_String_Builder submit_sb = {0};
    ASSERT(nob_read_entire_file(submit_manifest_c, &submit_sb));
    ASSERT(strstr(submit_sb.items, "COMMAND=ctest_submit") != NULL);
    ASSERT(strstr(submit_sb.items, "PARTS=Start;Build;Test") != NULL);
    ASSERT(strstr(submit_sb.items, "FILES=") != NULL);
    ASSERT(strstr(submit_sb.items, "notes.txt") != NULL);
    nob_sb_free(submit_sb);

    String_View upload_manifest = eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_CTEST::ctest_upload::MANIFEST"));
    char *upload_manifest_c = arena_strndup(temp_arena, upload_manifest.data, upload_manifest.count);
    ASSERT(upload_manifest_c != NULL);
    ASSERT(nob_file_exists(upload_manifest_c));

    Nob_String_Builder upload_sb = {0};
    ASSERT(nob_read_entire_file(upload_manifest_c, &upload_sb));
    ASSERT(strstr(upload_sb.items, "COMMAND=ctest_upload") != NULL);
    ASSERT(strstr(upload_sb.items, "a.txt") != NULL);
    ASSERT(strstr(upload_sb.items, "b.txt") != NULL);
    nob_sb_free(upload_sb);

    ASSERT(!nob_file_exists("ctest_bin/wipe/sub/junk.txt"));
    ASSERT(nob_file_exists("ctest_bin/wipe"));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_ctest_family_rejects_invalid_and_unsupported_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("safe_bin"));
    ASSERT(nob_write_entire_file("ctest_script_bad.cmake",
                                 "set(UNUSED 1)\n",
                                 strlen("set(UNUSED 1)\n")));

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_BINARY_DIR safe_bin)\n"
        "set(CMAKE_CURRENT_BINARY_DIR safe_bin)\n"
        "ctest_empty_binary_directory(../outside)\n"
        "ctest_run_script(NEW_PROCESS ctest_script_bad.cmake RETURN_VALUE SCRIPT_BAD_RV)\n"
        "ctest_sleep(1 2)\n"
        "ctest_build(BUILD)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("SCRIPT_BAD_RV")), nob_sv_from_cstr("0")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("UNUSED")).count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_register_and_model_compat_paths) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(legacy_iface INTERFACE)\n"
        "export_library_dependencies(legacy_deps.cmake)\n"
        "make_directory(legacy_dir/sub)\n"
        "write_file(legacy_dir/sub/out.txt alpha beta)\n"
        "install_files(share .txt first.txt second.txt)\n"
        "install_programs(bin tool.sh)\n"
        "install_targets(lib legacy_iface)\n"
        "load_command(legacy_cmd ./module)\n"
        "output_required_files(input.c output.txt)\n"
        "set(LEGACY_LIST a;b;c;b)\n"
        "remove(LEGACY_LIST b)\n"
        "qt_wrap_cpp(LegacyLib LEGACY_MOCS foo.hpp bar.hpp)\n"
        "qt_wrap_ui(LegacyLib LEGACY_UI_HDRS LEGACY_UI_SRCS dialog.ui)\n"
        "subdir_depends(src dep1 dep2)\n"
        "subdirs(dir_a dir_b)\n"
        "use_mangled_mesa(mesa out prefix)\n"
        "utility_source(CACHE_EXE /bin/tool generated.c)\n"
        "variable_requires(TESTVAR OUTVAR NEED1 NEED2)\n"
        "variable_watch(WATCH_ME watch-cmd)\n"
        "set(WATCH_ME touched)\n"
        "unset(WATCH_ME)\n"
        "fltk_wrap_ui(FltkLib main.fl)\n"
        "write_file(legacy_dir/sub/appended.txt one)\n"
        "write_file(legacy_dir/sub/appended.txt two APPEND)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_file_exists("legacy_dir/sub"));
    ASSERT(nob_file_exists("legacy_dir/sub/out.txt"));
    String_View out_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/out.txt", &out_txt));
    ASSERT(nob_sv_eq(out_txt, nob_sv_from_cstr("alphabeta")));

    String_View appended_txt = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "legacy_dir/sub/appended.txt", &appended_txt));
    ASSERT(nob_sv_eq(appended_txt, nob_sv_from_cstr("onetwo")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_MOCS")), nob_sv_from_cstr("moc_foo.cxx;moc_bar.cxx")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_HDRS")), nob_sv_from_cstr("ui_dialog.h")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LEGACY_UI_SRCS")), nob_sv_from_cstr("ui_dialog.cxx")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FltkLib_FLTK_UI_SRCS")),
                     nob_sv_from_cstr("fluid_main.cxx;fluid_main.h")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::export_library_dependencies::ARGS")),
                     nob_sv_from_cstr("legacy_deps.cmake")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::load_command::ARGS")),
                     nob_sv_from_cstr("legacy_cmd;./module")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_LEGACY::subdirs::ARGS")),
                     nob_sv_from_cstr("dir_a;dir_b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VAR")),
                     nob_sv_from_cstr("WATCH_ME")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_ACTION")),
                     nob_sv_from_cstr("UNSET")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_VALUE")),
                     nob_sv_from_cstr("touched")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOBIFY_VARIABLE_WATCH_LAST_COMMAND")),
                     nob_sv_from_cstr("watch-cmd")));

    size_t install_rule_count = 0;
    bool saw_unknown_command = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_INSTALL_ADD_RULE) install_rule_count++;
        if (ev->h.kind == EV_DIAGNOSTIC && nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command"))) {
            saw_unknown_command = true;
        }
    }
    ASSERT(install_rule_count >= 4);
    ASSERT(!saw_unknown_command);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch8_legacy_commands_reject_invalid_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "make_directory()\n"
        "write_file()\n"
        "remove(ONLY_VAR)\n"
        "variable_watch(A B C)\n"
        "qt_wrap_cpp(LegacyLib ONLY_OUT)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "add_library(imported_mod STATIC IMPORTED)\n"
        "add_executable(app app.c)\n"
        "target_sources(real PRIVATE priv.c PUBLIC pub.h INTERFACE iface.h)\n"
        "target_sources(real PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/public.hpp include/detail.hpp)\n"
        "target_sources(real INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS api FILES api/iface.hpp)\n"
        "target_sources(real PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "target_sources(imported_mod INTERFACE FILE_SET CXX_MODULES BASE_DIRS imported FILES imported/api.cppm)\n"
        "target_compile_features(real PRIVATE cxx_std_20 PUBLIC cxx_std_17 INTERFACE c_std_11)\n"
        "target_precompile_headers(real PRIVATE pch.h PUBLIC pch_pub.h INTERFACE <vector>)\n"
        "target_precompile_headers(app REUSE_FROM real)\n"
        "get_target_property(REAL_SOURCES real SOURCES)\n"
        "get_target_property(REAL_IFACE_SOURCES real INTERFACE_SOURCES)\n"
        "get_target_property(REAL_HEADER_SETS real HEADER_SETS)\n"
        "get_target_property(REAL_INTERFACE_HEADER_SETS real INTERFACE_HEADER_SETS)\n"
        "get_target_property(REAL_HEADER_SET real HEADER_SET)\n"
        "get_target_property(REAL_HEADER_DIRS real HEADER_DIRS)\n"
        "get_target_property(REAL_HEADER_SET_API real HEADER_SET_API)\n"
        "get_target_property(REAL_HEADER_DIRS_API real HEADER_DIRS_API)\n"
        "get_target_property(REAL_CXX_MODULE_SETS real CXX_MODULE_SETS)\n"
        "get_target_property(REAL_CXX_MODULE_SET real CXX_MODULE_SET)\n"
        "get_target_property(REAL_CXX_MODULE_DIRS real CXX_MODULE_DIRS)\n"
        "get_target_property(IMPORTED_IFACE_CXX_MODULE_SETS imported_mod INTERFACE_CXX_MODULE_SETS)\n"
        "get_target_property(IMPORTED_CXX_MODULE_SET imported_mod CXX_MODULE_SET)\n"
        "get_target_property(IMPORTED_CXX_MODULE_DIRS imported_mod CXX_MODULE_DIRS)\n"
        "get_target_property(REAL_COMPILE_FEATURES real COMPILE_FEATURES)\n"
        "get_target_property(REAL_IFACE_COMPILE_FEATURES real INTERFACE_COMPILE_FEATURES)\n"
        "get_target_property(REAL_PCH real PRECOMPILE_HEADERS)\n"
        "get_target_property(REAL_IFACE_PCH real INTERFACE_PRECOMPILE_HEADERS)\n"
        "get_target_property(APP_REUSE app PRECOMPILE_HEADERS_REUSE_FROM)\n"
        "target_sources(real bad.c another.c)\n"
        "target_sources(real INTERFACE FILE_SET ifacemods TYPE CXX_MODULES FILES iface_bad.cppm)\n"
        "target_sources(real PUBLIC FILE_SET custom_modules FILES missing_type.cppm)\n"
        "target_compile_features(alias_real PRIVATE bad_feature)\n"
        "target_precompile_headers(missing_pch PRIVATE missing.h)\n"
        "target_sources(missing_src PRIVATE bad.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_priv_source = false;
    bool saw_pub_source = false;
    bool saw_module_source_event = false;
    bool saw_iface_prop = false;
    bool saw_header_set_prop = false;
    bool saw_interface_header_sets_prop = false;
    bool saw_cxx_module_sets_prop = false;
    bool saw_cxx_module_set_prop = false;
    bool saw_cxx_module_dirs_prop = false;
    bool saw_imported_cxx_module_sets_prop = false;
    bool saw_compile_feature_local = false;
    bool saw_compile_feature_iface = false;
    bool saw_pch_local = false;
    bool saw_pch_iface = false;
    bool saw_reuse_from = false;
    bool saw_reuse_dep = false;
    bool saw_visibility_error = false;
    bool saw_cxx_module_interface_error = false;
    bool saw_cxx_module_missing_type_error = false;
    bool saw_alias_error = false;
    bool saw_missing_pch_error = false;
    bool saw_missing_src_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("real"))) {
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("priv.c"))) saw_priv_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("pub.h"))) saw_pub_source = true;
            if (sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("core.cppm"))) saw_module_source_event = true;
            ASSERT(!sv_contains_sv(ev->as.target_add_source.path, nob_sv_from_cstr("iface.h")));
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("real"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_SOURCES")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("iface.h"))) {
                saw_iface_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("HEADER_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("include/public.hpp"))) {
                saw_header_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_HEADER_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("api"))) {
                saw_interface_header_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_cxx_module_sets_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_SET")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules/core.cppm"))) {
                saw_cxx_module_set_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CXX_MODULE_DIRS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("modules"))) {
                saw_cxx_module_dirs_prop = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("cxx_std_20"))) {
                saw_compile_feature_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_COMPILE_FEATURES")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("c_std_11"))) {
                saw_compile_feature_iface = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS")) &&
                sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch.h"))) {
                saw_pch_local = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_PRECOMPILE_HEADERS")) &&
                (sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("vector")) ||
                 sv_contains_sv(ev->as.target_prop_set.value, nob_sv_from_cstr("pch_pub.h")))) {
                saw_pch_iface = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imported_mod"))) {
            if (nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("INTERFACE_CXX_MODULE_SETS")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("CXX_MODULES"))) {
                saw_imported_cxx_module_sets_prop = true;
            }
        } else if (ev->h.kind == EV_TARGET_PROP_SET &&
                   nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("PRECOMPILE_HEADERS_REUSE_FROM")) &&
                   nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("real"))) {
            saw_reuse_from = true;
        } else if (ev->h.kind == EV_TARGET_ADD_DEPENDENCY &&
                   nob_sv_eq(ev->as.target_add_dependency.target_name, nob_sv_from_cstr("app")) &&
                   nob_sv_eq(ev->as.target_add_dependency.dependency_name, nob_sv_from_cstr("real"))) {
            saw_reuse_dep = true;
        } else if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause,
                          nob_sv_from_cstr("target command requires PUBLIC, PRIVATE or INTERFACE before items"))) {
                saw_visibility_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET TYPE CXX_MODULES) may not use INTERFACE scope on non-IMPORTED targets"))) {
                saw_cxx_module_interface_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources(FILE_SET ...) requires TYPE for non-default file-set names"))) {
                saw_cxx_module_missing_type_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_compile_features() cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_precompile_headers() target was not declared"))) {
                saw_missing_pch_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("target_sources() target was not declared"))) {
                saw_missing_src_error = true;
            }
        }
    }

    ASSERT(saw_priv_source);
    ASSERT(saw_pub_source);
    ASSERT(!saw_module_source_event);
    ASSERT(saw_iface_prop);
    ASSERT(saw_header_set_prop);
    ASSERT(saw_interface_header_sets_prop);
    ASSERT(saw_cxx_module_sets_prop);
    ASSERT(saw_cxx_module_set_prop);
    ASSERT(saw_cxx_module_dirs_prop);
    ASSERT(saw_imported_cxx_module_sets_prop);
    ASSERT(saw_compile_feature_local);
    ASSERT(saw_compile_feature_iface);
    ASSERT(saw_pch_local);
    ASSERT(saw_pch_iface);
    ASSERT(saw_reuse_from);
    ASSERT(saw_reuse_dep);
    ASSERT(saw_visibility_error);
    ASSERT(saw_cxx_module_interface_error);
    ASSERT(saw_cxx_module_missing_type_error);
    ASSERT(saw_alias_error);
    ASSERT(saw_missing_pch_error);
    ASSERT(saw_missing_src_error);

    String_View real_sources = eval_var_get(ctx, nob_sv_from_cstr("REAL_SOURCES"));
    ASSERT(semicolon_list_count(real_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 0), nob_sv_from_cstr("priv.c")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_sources, 1), nob_sv_from_cstr("pub.h")));

    String_View real_iface_sources = eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_SOURCES"));
    ASSERT(semicolon_list_count(real_iface_sources) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 0), nob_sv_from_cstr("pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_sources, 1), nob_sv_from_cstr("iface.h")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_INTERFACE_HEADER_SETS")),
                     nob_sv_from_cstr("HEADERS;api")));

    String_View real_header_set = eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET"));
    ASSERT(semicolon_list_count(real_header_set) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 0), nob_sv_from_cstr("include/public.hpp")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_header_set, 1), nob_sv_from_cstr("include/detail.hpp")));

    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS")),
                          nob_sv_from_cstr("include")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_SET_API")),
                          nob_sv_from_cstr("api/iface.hpp")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_HEADER_DIRS_API")),
                          nob_sv_from_cstr("api")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_SET")),
                          nob_sv_from_cstr("modules/core.cppm")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("REAL_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("modules")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_IFACE_CXX_MODULE_SETS")),
                     nob_sv_from_cstr("CXX_MODULES")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_SET")),
                          nob_sv_from_cstr("imported/api.cppm")));
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("IMPORTED_CXX_MODULE_DIRS")),
                          nob_sv_from_cstr("imported")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_20;cxx_std_17")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_COMPILE_FEATURES")),
                     nob_sv_from_cstr("cxx_std_17;c_std_11")));

    String_View real_pch = eval_var_get(ctx, nob_sv_from_cstr("REAL_PCH"));
    ASSERT(semicolon_list_count(real_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 0), nob_sv_from_cstr("pch.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_pch, 1), nob_sv_from_cstr("pch_pub.h")));

    String_View real_iface_pch = eval_var_get(ctx, nob_sv_from_cstr("REAL_IFACE_PCH"));
    ASSERT(semicolon_list_count(real_iface_pch) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 0), nob_sv_from_cstr("pch_pub.h")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(real_iface_pch, 1), nob_sv_from_cstr("vector")));

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("APP_REUSE")), nob_sv_from_cstr("real")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_source_group_supports_files_tree_and_regex_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "source_group(\"Root Files\" FILES main.c util.c REGULAR_EXPRESSION [=[.*\\.(c|h)$]=])\n"
        "source_group(TREE src PREFIX Generated FILES src/a.c src/sub/b.c)\n"
        "source_group(Texts [=[.*\\.txt$]=])\n"
        "source_group(TREE src FILES ../outside.c)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_main_group = false;
    bool saw_util_group = false;
    bool saw_tree_root = false;
    bool saw_tree_sub = false;
    bool saw_c_regex = false;
    bool saw_c_regex_name = false;
    bool saw_txt_regex = false;
    bool saw_txt_regex_name = false;
    bool saw_tree_outside_error = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_VAR_SET) {
            if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("main.c")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_main_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("util.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_util_group = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/a.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated"))) {
                saw_tree_root = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_FILE::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("src/sub/b.c")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Generated\\sub"))) {
                saw_tree_sub = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.(c|h)$"))) {
                saw_c_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Root Files"))) {
                saw_c_regex_name = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       !sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(".*\\.txt$"))) {
                saw_txt_regex = true;
            } else if (sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_SOURCE_GROUP_REGEX::")) &&
                       sv_contains_sv(ev->as.var_set.key, nob_sv_from_cstr("::NAME")) &&
                       nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("Texts"))) {
                saw_txt_regex_name = true;
            }
        } else if (ev->h.kind == EV_DIAGNOSTIC &&
                   ev->as.diag.severity == EV_DIAG_ERROR &&
                   nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("source_group(TREE ...) file is outside the declared tree root"))) {
            saw_tree_outside_error = true;
        }
    }

    ASSERT(saw_main_group);
    ASSERT(saw_util_group);
    ASSERT(saw_tree_root);
    ASSERT(saw_tree_sub);
    ASSERT(saw_c_regex);
    ASSERT(saw_c_regex_name);
    ASSERT(saw_txt_regex);
    ASSERT(saw_txt_regex_name);
    ASSERT(saw_tree_outside_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_mode_severity_mapping) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(NOTICE n)\n"
        "message(STATUS s)\n"
        "message(VERBOSE v)\n"
        "message(DEBUG d)\n"
        "message(TRACE t)\n"
        "message(WARNING w)\n"
        "message(AUTHOR_WARNING aw)\n"
        "message(DEPRECATION dep)\n"
        "message(SEND_ERROR se)\n"
        "message(CHECK_START probe)\n"
        "message(CHECK_PASS ok)\n"
        "message(CHECK_START probe2)\n"
        "message(CHECK_FAIL fail)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 3);
    ASSERT(report->error_count == 1);

    size_t warning_diag_count = 0;
    size_t error_diag_count = 0;
    bool saw_check_pass_cause = false;
    bool saw_check_fail_cause = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_WARNING) warning_diag_count++;
        if (ev->as.diag.severity == EV_DIAG_ERROR) error_diag_count++;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe - ok"))) saw_check_pass_cause = true;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe2 - fail"))) saw_check_fail_cause = true;
    }

    ASSERT(warning_diag_count == 3);
    ASSERT(error_diag_count == 1);
    ASSERT(!saw_check_pass_cause);
    ASSERT(!saw_check_fail_cause);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_check_pass_without_start_is_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "message(CHECK_PASS done)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"))) {
            found = true;
            break;
        }
    }
    ASSERT(found);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_deprecation_respects_control_variables) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_WARN_DEPRECATED FALSE)\n"
        "message(DEPRECATION hidden)\n"
        "set(CMAKE_WARN_DEPRECATED TRUE)\n"
        "message(DEPRECATION shown)\n"
        "set(CMAKE_ERROR_DEPRECATED TRUE)\n"
        "message(DEPRECATION err)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    bool saw_hidden = false;
    bool saw_shown_warn = false;
    bool saw_err_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("hidden"))) saw_hidden = true;
        if (ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("shown"))) {
            saw_shown_warn = true;
        }
        if (ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err"))) {
            saw_err_error = true;
        }
    }
    ASSERT(!saw_hidden);
    ASSERT(saw_shown_warn);
    ASSERT(saw_err_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_configure_log_persists_yaml_file) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(CHECK_START feature-probe)\n"
        "message(CONFIGURE_LOG probe-start)\n"
        "message(CHECK_PASS yes)\n"
        "message(CONFIGURE_LOG probe-end)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    String_View log_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "./CMakeFiles/CMakeConfigureLog.yaml", &log_text));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("kind: \"message-v1\"")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-start")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-end")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("feature-probe")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_and_unset_env_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(ENV{NOBIFY_ENV_A} valueA)\n"
        "set(ENV{NOBIFY_ENV_A})\n"
        "set(ENV{NOBIFY_ENV_B} valueB ignored)\n"
        "add_executable(env_forms main.c)\n"
        "target_compile_definitions(env_forms PRIVATE A=$ENV{NOBIFY_ENV_A} B=$ENV{NOBIFY_ENV_B})\n"
        "unset(ENV{NOBIFY_ENV_B})\n"
        "target_compile_definitions(env_forms PRIVATE B2=$ENV{NOBIFY_ENV_B})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_extra_args_warn = false;
    bool saw_a_empty = false;
    bool saw_b_value = false;
    bool saw_b2_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"))) {
            saw_extra_args_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("A="))) saw_a_empty = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B=valueB"))) saw_b_value = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B2="))) saw_b2_empty = true;
    }
    ASSERT(saw_extra_args_warn);
    ASSERT(saw_a_empty);
    ASSERT(saw_b_value);
    ASSERT(saw_b2_empty);

    const char *env_b = getenv("NOBIFY_ENV_B");
    ASSERT(env_b == NULL);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_process_env_service_overlays_execute_process_and_timestamp) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND cmd /C \"echo %NOBIFY_ENV_CHILD_F3%\" "
        "OUTPUT_VARIABLE CHILD_B OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#else
    const char *script =
        "set(ENV{NOBIFY_ENV_CHILD_F3} valueB)\n"
        "execute_process(COMMAND /bin/sh -c \"printf '%s' $NOBIFY_ENV_CHILD_F3\" "
        "OUTPUT_VARIABLE CHILD_B)\n"
        "set(ENV{SOURCE_DATE_EPOCH} 946684800)\n"
        "string(TIMESTAMP ENV_TS \"%Y\" UTC)\n"
        "unset(ENV{NOBIFY_ENV_CHILD_F3})\n"
        "unset(ENV{SOURCE_DATE_EPOCH})\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CHILD_B")), nob_sv_from_cstr("valueB")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ENV_TS")), nob_sv_from_cstr("2000")));
    ASSERT(!eval_has_env(ctx, "NOBIFY_ENV_CHILD_F3"));
    ASSERT(getenv("NOBIFY_ENV_CHILD_F3") == NULL);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(parse_direct)\n"
        "  cmake_parse_arguments(ARG \"OPT;FAST\" \"DEST\" \"TARGETS;CONFIGS\" ${ARGN})\n"
        "  list(GET ARG_TARGETS 0 ARG_T0)\n"
        "  list(GET ARG_TARGETS 1 ARG_T1)\n"
        "endmacro()\n"
        "function(parse_argv)\n"
        "  cmake_parse_arguments(PARSE_ARGV 1 FN \"FLAG\" \"ONE\" \"MULTI;MULTI\")\n"
        "  add_executable(parse_argv_t main.c)\n"
        "  list(GET FN_MULTI 0 FN_M0)\n"
        "  list(GET FN_MULTI 1 FN_M1)\n"
        "  if(DEFINED FN_ONE)\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=1)\n"
        "  else()\n"
        "    target_compile_definitions(parse_argv_t PRIVATE ONE_DEFINED=0)\n"
        "  endif()\n"
        "  target_compile_definitions(parse_argv_t PRIVATE FLAG=${FN_FLAG} M0=${FN_M0} M1=${FN_M1} UNPARSED=${FN_UNPARSED_ARGUMENTS})\n"
        "endfunction()\n"
        "parse_direct(OPT EXTRA DEST bin TARGETS a b CONFIGS)\n"
        "parse_argv(skip FLAG TAIL ONE \"\" MULTI alpha beta)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 1);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_OPT")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_FAST")), nob_sv_from_cstr("FALSE")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_DEST")), nob_sv_from_cstr("bin")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_TARGETS")), nob_sv_from_cstr("a;b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_T0")), nob_sv_from_cstr("a")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_T1")), nob_sv_from_cstr("b")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_UNPARSED_ARGUMENTS")), nob_sv_from_cstr("EXTRA")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ARG_KEYWORDS_MISSING_VALUES")), nob_sv_from_cstr("CONFIGS")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("ARG_CONFIGS")).count == 0);

    bool saw_dup_warn = false;
    bool saw_flag = false;
    bool saw_one_defined_old = false;
    bool saw_m0 = false;
    bool saw_m1 = false;
    bool saw_unparsed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("cmake_parse_arguments() keyword appears more than once across keyword lists"))) {
            saw_dup_warn = true;
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("parse_argv_t"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FLAG=TRUE"))) saw_flag = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ONE_DEFINED=0"))) saw_one_defined_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M0=alpha"))) saw_m0 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("M1=beta"))) saw_m1 = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("UNPARSED=TAIL"))) saw_unparsed = true;
    }

    ASSERT(saw_dup_warn);
    ASSERT(saw_flag);
    ASSERT(saw_one_defined_old);
    ASSERT(saw_m0);
    ASSERT(saw_m1);
    ASSERT(saw_unparsed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_unset_env_rejects_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "unset(ENV{NOBIFY_ENV_OPT} CACHE)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("unset(ENV{...}) does not accept options"))) {
            saw_error = true;
            break;
        }
    }
    ASSERT(saw_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_cmp0126_old_and_new_semantics) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHE_OLD local_old)\n"
        "set(CACHE_OLD cache_old CACHE STRING \"doc\")\n"
        "add_executable(cache_old_t main.c)\n"
        "target_compile_definitions(cache_old_t PRIVATE OLD_CA=${CACHE_OLD})\n"
        "cmake_policy(SET CMP0126 NEW)\n"
        "set(CACHE_NEW local_new)\n"
        "set(CACHE_NEW cache_new CACHE STRING \"doc\" FORCE)\n"
        "add_executable(cache_new_t main.c)\n"
        "target_compile_definitions(cache_new_t PRIVATE NEW_CB=${CACHE_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_binding_from_cache = false;
    bool saw_new_binding_from_local = false;
    bool saw_cache_old_set = false;
    bool saw_cache_new_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY && ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE) {
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_OLD")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_old"))) {
                saw_cache_old_set = true;
            }
            if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_NEW")) &&
                nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_new"))) {
                saw_cache_new_set = true;
            }
        }
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_CA=cache_old"))) {
            saw_old_binding_from_cache = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_CB=local_new"))) {
            saw_new_binding_from_local = true;
        }
    }
    ASSERT(saw_cache_old_set);
    ASSERT(saw_cache_new_set);
    ASSERT(saw_old_binding_from_cache);
    ASSERT(saw_new_binding_from_local);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_policy_version_defaults_cmp0126_to_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CACHE_VER local_ver)\n"
        "set(CACHE_VER cache_ver CACHE STRING \"doc\")\n"
        "add_executable(cache_ver_t main.c)\n"
        "target_compile_definitions(cache_ver_t PRIVATE VER=${CACHE_VER})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_cache_ver_set = false;
    bool saw_local_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHE_VER")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("cache_ver"))) {
            saw_cache_ver_set = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VER=local_ver"))) {
            saw_local_binding = true;
        }
    }
    ASSERT(saw_cache_ver_set);
    ASSERT(saw_local_binding);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}


void run_evaluator_v2_batch4(int *passed, int *failed) {
    test_evaluator_load_cache_supports_multi_path_legacy_mode_and_prefix_unset(passed, failed);
    test_evaluator_export_cxx_modules_directory_writes_sidecars_and_default_export_file(passed, failed);
    test_evaluator_export_rejects_invalid_extension_and_alias_targets(passed, failed);
    test_evaluator_ctest_family_models_metadata_and_safe_local_effects(passed, failed);
    test_evaluator_ctest_family_rejects_invalid_and_unsupported_forms(passed, failed);
    test_evaluator_batch8_legacy_commands_register_and_model_compat_paths(passed, failed);
    test_evaluator_batch8_legacy_commands_reject_invalid_forms(passed, failed);
    test_evaluator_target_sources_compile_features_and_precompile_headers_model_usage_requirements(passed, failed);
    test_evaluator_source_group_supports_files_tree_and_regex_forms(passed, failed);
    test_evaluator_message_mode_severity_mapping(passed, failed);
    test_evaluator_message_check_pass_without_start_is_error(passed, failed);
    test_evaluator_message_deprecation_respects_control_variables(passed, failed);
    test_evaluator_message_configure_log_persists_yaml_file(passed, failed);
    test_evaluator_set_and_unset_env_forms(passed, failed);
    test_evaluator_process_env_service_overlays_execute_process_and_timestamp(passed, failed);
    test_evaluator_cmake_parse_arguments_supports_direct_and_parse_argv_forms(passed, failed);
    test_evaluator_unset_env_rejects_options(passed, failed);
    test_evaluator_set_cache_cmp0126_old_and_new_semantics(passed, failed);
    test_evaluator_set_cache_policy_version_defaults_cmp0126_to_new(passed, failed);
}
