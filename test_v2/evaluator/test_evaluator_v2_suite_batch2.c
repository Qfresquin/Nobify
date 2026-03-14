#include "test_evaluator_v2_common.h"

TEST(evaluator_cmake_language_core_subcommands_work) {
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
        "set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)\n"
        "cmake_language(CALL set CALL_OUT alpha)\n"
        "cmake_language(EVAL CODE [[set(EVAL_OUT beta)]])\n"
        "cmake_language(GET_MESSAGE_LOG_LEVEL LOG_OUT)\n"
        "add_executable(cml_probe main.c)\n"
        "target_compile_definitions(cml_probe PRIVATE CALL_OUT=${CALL_OUT} EVAL_OUT=${EVAL_OUT} LOG_OUT=${LOG_OUT})\n"
        "set(DEFER_VALUE before)\n"
        "cmake_language(DEFER ID later CALL target_compile_definitions cml_probe PRIVATE DEFER_VALUE=${DEFER_VALUE})\n"
        "cmake_language(DEFER ID cancel_me CALL set CANCELLED yes)\n"
        "cmake_language(DEFER ID_VAR AUTO_ID CALL set AUTO_HIT yes)\n"
        "cmake_language(DEFER CANCEL_CALL cancel_me ${AUTO_ID})\n"
        "cmake_language(DEFER GET_CALL_IDS IDS_OUT)\n"
        "cmake_language(DEFER GET_CALL later CALL_INFO)\n"
        "set(DEFER_VALUE after)\n"
        "return()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_call = false;
    bool saw_eval = false;
    bool saw_log = false;
    bool saw_defer = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("cml_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("CALL_OUT=alpha"))) saw_call = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("EVAL_OUT=beta"))) saw_eval = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("LOG_OUT=NOTICE"))) saw_log = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DEFER_VALUE=after"))) saw_defer = true;
    }

    ASSERT(saw_call);
    ASSERT(saw_eval);
    ASSERT(saw_log);
    ASSERT(saw_defer);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IDS_OUT")), nob_sv_from_cstr("later")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("CALL_INFO")),
                     nob_sv_from_cstr("target_compile_definitions;cml_probe;PRIVATE;DEFER_VALUE=${DEFER_VALUE}")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("CANCELLED")).count == 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("AUTO_HIT")).count == 0);
    String_View auto_id = eval_var_get(ctx, nob_sv_from_cstr("AUTO_ID"));
    ASSERT(auto_id.count > 0);
    ASSERT(auto_id.data[0] == '_');

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_models_find_package_hook) {
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

    ASSERT(nob_mkdir_if_not_exists("provider_root"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake/FallbackPkg"));
    ASSERT(nob_mkdir_if_not_exists("provider_root/lib/cmake/ClearedPkg"));
    ASSERT(nob_write_entire_file("provider_root/lib/cmake/FallbackPkg/FallbackPkgConfig.cmake",
                                 "set(FallbackPkg_FOUND 1)\n"
                                 "set(FallbackPkg_CONFIG_HIT 1)\n",
                                 strlen("set(FallbackPkg_FOUND 1)\n"
                                        "set(FallbackPkg_CONFIG_HIT 1)\n")));
    ASSERT(nob_write_entire_file("provider_root/lib/cmake/ClearedPkg/ClearedPkgConfig.cmake",
                                 "set(ClearedPkg_FOUND 1)\n"
                                 "set(ClearedPkg_CONFIG_HIT 1)\n",
                                 strlen("set(ClearedPkg_FOUND 1)\n"
                                        "set(ClearedPkg_CONFIG_HIT 1)\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(dep_provider method)\n"
        "  list(APPEND PROVIDER_LOG \"${method}:${ARGV1}\")\n"
        "  if(method STREQUAL \"FIND_PACKAGE\")\n"
        "    if(ARGV1 STREQUAL \"ProvidedPkg\")\n"
        "      set(ProvidedPkg_FOUND 1)\n"
        "      set(ProvidedPkg_CONFIG provider://ProvidedPkg)\n"
        "      set(PROVIDED_BY_PROVIDER yes)\n"
        "    else()\n"
        "      find_package(${ARGN} BYPASS_PROVIDER)\n"
        "    endif()\n"
        "  endif()\n"
        "endmacro()\n"
        "set(CMAKE_PREFIX_PATH \"${CMAKE_CURRENT_BINARY_DIR}/provider_root\")\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FIND_PACKAGE)\n"
        "find_package(ProvidedPkg QUIET)\n"
        "find_package(FallbackPkg CONFIG QUIET)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER \"\")\n"
        "find_package(ClearedPkg CONFIG QUIET)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BY_PROVIDER")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ProvidedPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ProvidedPkg_CONFIG")), nob_sv_from_cstr("provider://ProvidedPkg")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FallbackPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("FallbackPkg_CONFIG_HIT")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ClearedPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("ClearedPkg_CONFIG_HIT")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDER_LOG")),
                     nob_sv_from_cstr("FIND_PACKAGE:ProvidedPkg;FIND_PACKAGE:FallbackPkg")));
    ASSERT(ctx->command_state.dependency_provider.command_name.count == 0);
    ASSERT(!ctx->command_state.dependency_provider.supports_find_package);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_models_fetchcontent_hook) {
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

    ASSERT(nob_mkdir_if_not_exists("provided_dep_src"));
    ASSERT(nob_mkdir_if_not_exists("fetchcontent_local"));
    ASSERT(nob_mkdir_if_not_exists("fetchcontent_bypass"));
    ASSERT(nob_write_entire_file("fetchcontent_local/CMakeLists.txt",
                                 "add_library(local_from_fetch INTERFACE)\n",
                                 strlen("add_library(local_from_fetch INTERFACE)\n")));
    ASSERT(nob_write_entire_file("fetchcontent_bypass/CMakeLists.txt",
                                 "add_library(bypass_from_fetch INTERFACE)\n",
                                 strlen("add_library(bypass_from_fetch INTERFACE)\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "macro(dep_provider method)\n"
        "  list(APPEND PROVIDER_LOG \"${method}:${ARGV1}\")\n"
        "  if(method STREQUAL \"FETCHCONTENT_MAKEAVAILABLE_SERIAL\")\n"
        "    if(ARGV1 STREQUAL \"ProvidedDep\")\n"
        "      FetchContent_SetPopulated(${ARGV1}\n"
        "        SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/provided_dep_src\"\n"
        "        BINARY_DIR \"${CMAKE_CURRENT_BINARY_DIR}/provided_dep_build\")\n"
        "    elseif(ARGV1 STREQUAL \"LocalDep\")\n"
        "      FetchContent_MakeAvailable(${ARGV1})\n"
        "    endif()\n"
        "  endif()\n"
        "endmacro()\n"
        "include(FetchContent)\n"
        "FetchContent_Declare(ProvidedDep)\n"
        "FetchContent_Declare(DeclaredOnly)\n"
        "FetchContent_Declare(LocalDep\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_local\"\n"
        "  BINARY_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_local_build\")\n"
        "FetchContent_Declare(BypassDep)\n"
        "FetchContent_GetProperties(DeclaredOnly SOURCE_DIR DECLARED_SRC BINARY_DIR DECLARED_BIN POPULATED DECLARED_POPULATED)\n"
        "set(FETCHCONTENT_SOURCE_DIR_BYPASSDEP \"${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_bypass\")\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)\n"
        "FetchContent_MakeAvailable(ProvidedDep LocalDep BypassDep)\n"
        "FetchContent_GetProperties(ProvidedDep SOURCE_DIR PROVIDED_SRC BINARY_DIR PROVIDED_BIN POPULATED PROVIDED_POPULATED)\n"
        "FetchContent_GetProperties(LocalDep SOURCE_DIR LOCAL_SRC BINARY_DIR LOCAL_BIN POPULATED LOCAL_POPULATED)\n"
        "FetchContent_GetProperties(BypassDep SOURCE_DIR BYPASS_SRC BINARY_DIR BYPASS_BIN POPULATED BYPASS_POPULATED)\n"
        "FetchContent_GetProperties(ProvidedDep)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER \"\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(ctx->fetchcontent_module_loaded);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDER_LOG")),
                     nob_sv_from_cstr("FETCHCONTENT_MAKEAVAILABLE_SERIAL:ProvidedDep;FETCHCONTENT_MAKEAVAILABLE_SERIAL:LocalDep")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("LOCAL_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BYPASS_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_POPULATED")), nob_sv_from_cstr("0")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BIN")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("DECLARED_BIN")).count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_POPULATED")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_SOURCE_DIR")),
                     eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_SRC"))));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("provideddep_BINARY_DIR")),
                     eval_var_get(ctx, nob_sv_from_cstr("PROVIDED_BIN"))));
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("local_from_fetch")));
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("bypass_from_fetch")));
    ASSERT(ctx->command_state.dependency_provider.command_name.count == 0);
    ASSERT(!ctx->command_state.dependency_provider.supports_find_package);
    ASSERT(!ctx->command_state.dependency_provider.supports_fetchcontent_makeavailable_serial);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_url_population_and_redirect_override) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_url_archive_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_url_archive_root/url_src"));
    ASSERT(nob_write_entire_file("fc_url_archive_root/url_src/CMakeLists.txt",
                                 "add_library(url_from_fetch INTERFACE)\n",
                                 strlen("add_library(url_from_fetch INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_url_dep.tar", "fc_url_archive_root", "url_src"));

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
        "include(FetchContent)\n"
        "file(SHA256 fc_url_dep.tar FC_URL_HASH)\n"
        "FetchContent_Declare(UrlDep\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_url_dep.tar\"\n"
        "  URL_HASH \"SHA256=${FC_URL_HASH}\"\n"
        "  SOURCE_SUBDIR url_src\n"
        "  OVERRIDE_FIND_PACKAGE)\n"
        "FetchContent_GetProperties(UrlDep SOURCE_DIR URL_DECL_SRC BINARY_DIR URL_DECL_BIN POPULATED URL_DECL_POP)\n"
        "FetchContent_MakeAvailable(UrlDep)\n"
        "if(EXISTS \"${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/UrlDepConfig.cmake\")\n"
        "  set(REDIRECT_CFG_EXISTS 1)\n"
        "endif()\n"
        "find_package(UrlDep CONFIG QUIET)\n"
        "FetchContent_GetProperties(UrlDep POPULATED URL_POP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_POP")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("URL_POP")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("URL_DECL_BIN")).count > 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("REDIRECT_CFG_EXISTS")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("UrlDep_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("UrlDep_DIR")).count > 0);
    ASSERT(eval_target_known(ctx, nob_sv_from_cstr("url_from_fetch")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_populate_direct_url_download_no_extract_does_not_add_subdirectory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_noextract_archive_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_noextract_archive_root/noextract_src"));
    ASSERT(nob_write_entire_file("fc_noextract_archive_root/noextract_src/CMakeLists.txt",
                                 "add_library(noextract_lib INTERFACE)\n",
                                 strlen("add_library(noextract_lib INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_noextract_dep.tar", "fc_noextract_archive_root", "noextract_src"));

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
        "include(FetchContent)\n"
        "file(SHA256 fc_noextract_dep.tar FC_NOEXTRACT_HASH)\n"
        "FetchContent_Populate(NoExtractDep\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_noextract_dep.tar\"\n"
        "  URL_HASH \"SHA256=${FC_NOEXTRACT_HASH}\"\n"
        "  DOWNLOAD_NO_EXTRACT TRUE\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_noextract_src\")\n"
        "FetchContent_GetProperties(NoExtractDep SOURCE_DIR NOEXTRACT_SRC BINARY_DIR NOEXTRACT_BIN POPULATED NOEXTRACT_POP)\n"
        "if(EXISTS \"${noextractdep_SOURCE_DIR}/fc_noextract_dep.tar\")\n"
        "  set(NOEXTRACT_EXISTS 1)\n"
        "endif()\n"
        "if(TARGET noextract_lib)\n"
        "  set(NOEXTRACT_TARGET 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_POP")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_EXISTS")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_BIN")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("NOEXTRACT_TARGET")).count == 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("noextract_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_populate_saved_details_git_clones_without_add_subdirectory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(evaluator_create_fetchcontent_git_repo("fc_git_saved_repo",
                                                  "add_library(git_saved_lib INTERFACE)\n",
                                                  "v1\n",
                                                  "v1"));

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
        "include(FetchContent)\n"
        "FetchContent_Declare(GitSaved\n"
        "  GIT_REPOSITORY \"${CMAKE_CURRENT_BINARY_DIR}/fc_git_saved_repo\"\n"
        "  GIT_TAG v1\n"
        "  GIT_SHALLOW TRUE)\n"
        "FetchContent_Populate(GitSaved)\n"
        "FetchContent_GetProperties(GitSaved SOURCE_DIR GIT_SRC BINARY_DIR GIT_BIN POPULATED GIT_POP)\n"
        "file(READ \"${gitsaved_SOURCE_DIR}/version.txt\" GIT_VERSION)\n"
        "if(TARGET git_saved_lib)\n"
        "  set(GIT_TARGET 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("GIT_POP")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_BIN")).count > 0);
    ASSERT(sv_contains_sv(eval_var_get(ctx, nob_sv_from_cstr("GIT_VERSION")), nob_sv_from_cstr("v1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("GIT_TARGET")).count == 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("git_saved_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_makeavailable_try_find_package_always_prefers_package_resolution) {
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
        "include(FetchContent)\n"
        "file(MAKE_DIRECTORY fc_try_pkg/lib/cmake/TryPkg)\n"
        "file(WRITE fc_try_pkg/lib/cmake/TryPkg/TryPkgConfig.cmake [=[set(TryPkg_FOUND 1)\n"
        "set(TryPkg_FROM package)\n"
        "]=])\n"
        "file(MAKE_DIRECTORY fc_try_pkg_local)\n"
        "file(WRITE fc_try_pkg_local/CMakeLists.txt [=[add_library(try_local_lib INTERFACE)\n"
        "]=])\n"
        "set(CMAKE_PREFIX_PATH \"${CMAKE_CURRENT_BINARY_DIR}/fc_try_pkg\")\n"
        "set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)\n"
        "FetchContent_Declare(TryPkg\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_try_pkg_local\")\n"
        "FetchContent_MakeAvailable(TryPkg)\n"
        "FetchContent_GetProperties(TryPkg SOURCE_DIR TRY_SRC BINARY_DIR TRY_BIN POPULATED TRY_POP)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TRY_POP")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TryPkg_FOUND")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TryPkg_FROM")), nob_sv_from_cstr("package")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("TRY_SRC")).count > 0);
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("TRY_BIN")).count > 0);
    ASSERT(!eval_target_known(ctx, nob_sv_from_cstr("try_local_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_fetchcontent_negative_declarations_and_hash_failure_surface_diags) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("fc_bad_hash_root"));
    ASSERT(nob_mkdir_if_not_exists("fc_bad_hash_root/bad_hash_src"));
    ASSERT(nob_write_entire_file("fc_bad_hash_root/bad_hash_src/CMakeLists.txt",
                                 "add_library(bad_hash_lib INTERFACE)\n",
                                 strlen("add_library(bad_hash_lib INTERFACE)\n")));
    ASSERT(evaluator_create_tar_archive("fc_bad_hash.tar", "fc_bad_hash_root", "bad_hash_src"));

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
        "include(FetchContent)\n"
        "FetchContent_Declare(BadMixed\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  GIT_REPOSITORY \"${CMAKE_CURRENT_BINARY_DIR}/fc_git_saved_repo\")\n"
        "FetchContent_Declare(BadUnsupported SVN_REPOSITORY some_repo)\n"
        "FetchContent_Declare(BadArgs\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  FIND_PACKAGE_ARGS QUIET OVERRIDE_FIND_PACKAGE)\n"
        "FetchContent_Declare(BadGit GIT_TAG v1)\n"
        "FetchContent_Declare(BadUrl URL_HASH SHA256=1234)\n"
        "FetchContent_Populate(BadHash\n"
        "  URL \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash.tar\"\n"
        "  URL_HASH \"SHA256=0000\"\n"
        "  SOURCE_DIR \"${CMAKE_CURRENT_BINARY_DIR}/fc_bad_hash_out\")\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count >= 6);

    bool saw_mixed = false;
    bool saw_unsupported = false;
    bool saw_args_combo = false;
    bool saw_missing_git_repo = false;
    bool saw_missing_url = false;
    bool saw_hash_failure = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_DIAG) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent_Declare() may not mix URL and GIT transports"))) {
            saw_mixed = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent transport is not supported in this wave"))) {
            saw_unsupported = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent_Declare() may not combine FIND_PACKAGE_ARGS with OVERRIDE_FIND_PACKAGE"))) {
            saw_args_combo = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent GIT transport requires GIT_REPOSITORY"))) {
            saw_missing_git_repo = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("FetchContent URL transport requires URL"))) {
            saw_missing_url = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("file(DOWNLOAD) hash verification failed"))) {
            saw_hash_failure = true;
        }
    }

    ASSERT(saw_mixed);
    ASSERT(saw_unsupported);
    ASSERT(saw_args_combo);
    ASSERT(saw_missing_git_repo);
    ASSERT(saw_missing_url);
    ASSERT(saw_hash_failure);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_eval_inline_soft_error_preserves_context) {
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
        "set(BEFORE_INLINE_FILE ${CMAKE_CURRENT_LIST_FILE})\n"
        "cmake_language(EVAL CODE [[message(SEND_ERROR inline_soft)\n"
        "set(INLINE_FILE ${CMAKE_CURRENT_LIST_FILE})]])\n"
        "set(AFTER_INLINE ok)\n");

    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("BEFORE_INLINE_FILE")), nob_sv_from_cstr("CMakeLists.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("INLINE_FILE")), nob_sv_from_cstr("CMakeLists.txt")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("AFTER_INLINE")), nob_sv_from_cstr("ok")));
    ASSERT(ctx->current_file != NULL);
    ASSERT(strcmp(ctx->current_file, "CMakeLists.txt") == 0);
    ASSERT(nob_sv_eq(eval_current_list_file(ctx), nob_sv_from_cstr("CMakeLists.txt")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_language_dependency_provider_rejects_invalid_forms) {
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
        "macro(dep_provider_bad method)\n"
        "endmacro()\n"
        "function(dep_provider_scope)\n"
        "  cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS FIND_PACKAGE)\n"
        "endfunction()\n"
        "dep_provider_scope()\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER missing_provider SUPPORTED_METHODS FIND_PACKAGE)\n"
        "cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS BAD_METHOD)\n"
        "find_package(OutsidePkg BYPASS_PROVIDER QUIET)\n");

    Eval_Result run_res = evaluator_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_scope = false;
    bool saw_missing = false;
    bool saw_method = false;
    bool saw_bypass = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) must be called at file scope"))) {
            saw_scope = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) requires an existing function() or macro()"))) {
            saw_missing = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_language(SET_DEPENDENCY_PROVIDER) received an unknown method"))) {
            saw_method = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("find_package(BYPASS_PROVIDER) may only be used from inside a dependency provider"))) {
            saw_bypass = true;
        }
    }

    ASSERT(saw_scope);
    ASSERT(saw_missing);
    ASSERT(saw_method);
    ASSERT(saw_bypass);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_compile_definitions_normalizes_dash_d_items) {
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
        "add_executable(norm_defs main.c)\n"
        "target_compile_definitions(norm_defs PRIVATE -DFOO -D BAR -DBAZ=1 QUX=2)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_foo = false;
    bool saw_bar = false;
    bool saw_baz = false;
    bool saw_qux = false;
    bool saw_dash_prefixed = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("norm_defs"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_foo = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR"))) saw_bar = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAZ=1"))) saw_baz = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("QUX=2"))) saw_qux = true;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("-D"))) saw_dash_prefixed = true;
    }

    ASSERT(saw_foo);
    ASSERT(saw_bar);
    ASSERT(saw_baz);
    ASSERT(saw_qux);
    ASSERT(!saw_dash_prefixed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_target_validates_signature_and_target) {
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
        "add_custom_target(gen)\n"
        "add_custom_command(TARGET gen POST_BUILD COMMAND echo ok BYPRODUCTS ok.txt)\n"
        "add_custom_command(TARGET missing POST_BUILD COMMAND echo bad)\n"
        "add_custom_command(TARGET gen COMMAND echo bad_no_stage)\n"
        "add_custom_command(TARGET gen PRE_BUILD PRE_LINK COMMAND echo bad_multi_stage)\n"
        "add_custom_command(TARGET gen POST_BUILD DEPENDS dep1 COMMAND echo bad_depends)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    size_t custom_target_success_events = 0;
    size_t custom_target_error_events = 0;
    bool saw_missing_target = false;
    bool saw_missing_stage = false;
    bool saw_multi_stage = false;
    bool saw_unexpected_depends = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("add_custom_command"))) {
            if (ev->as.command_end.status == EVENT_COMMAND_STATUS_SUCCESS) {
                custom_target_success_events++;
            } else if (ev->as.command_end.status == EVENT_COMMAND_STATUS_ERROR) {
                custom_target_error_events++;
            }
        }
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) target was not declared"))) {
            saw_missing_target = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) requires PRE_BUILD, PRE_LINK or POST_BUILD"))) {
            saw_missing_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) accepts exactly one build stage"))) {
            saw_multi_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unexpected argument in add_custom_command()")) &&
                   nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("DEPENDS"))) {
            saw_unexpected_depends = true;
        }
    }

    ASSERT(custom_target_success_events == 1);
    ASSERT(custom_target_error_events == 4);
    ASSERT(saw_missing_target);
    ASSERT(saw_missing_stage);
    ASSERT(saw_multi_stage);
    ASSERT(saw_unexpected_depends);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_output_validates_conflicts) {
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
        "add_custom_command(OUTPUT bad_pairs.c IMPLICIT_DEPENDS C only.c CXX COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_conflict.c IMPLICIT_DEPENDS C in.c DEPFILE in.d COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_pool.c JOB_POOL pool USES_TERMINAL COMMAND gen)\n"
        "add_custom_command(OUTPUT good.c COMMAND python gen.py DEPENDS schema.idl BYPRODUCTS gen.log MAIN_DEPENDENCY schema.idl)\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_pairs_error = false;
    bool saw_conflict_error = false;
    bool saw_pool_error = false;
    bool saw_valid_output_event = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("add_custom_command")) &&
            ev->as.command_end.status == EVENT_COMMAND_STATUS_SUCCESS) {
            saw_valid_output_event = true;
        }
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("IMPLICIT_DEPENDS requires language/file pairs"))) {
            saw_pairs_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) cannot combine DEPFILE with IMPLICIT_DEPENDS"))) {
            saw_conflict_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) JOB_POOL is incompatible with USES_TERMINAL"))) {
            saw_pool_error = true;
        }
    }

    ASSERT(saw_pairs_error);
    ASSERT(saw_conflict_error);
    ASSERT(saw_pool_error);
    ASSERT(saw_valid_output_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

typedef struct {
    Event_Family family;
    const char *label;
} Event_Family_Contract_Row;

typedef struct {
    Event_Kind kind;
    Event_Family family;
    const char *label;
    uint32_t role_mask;
} Event_Kind_Contract_Row;

TEST(evaluator_event_ir_taxonomy_is_frozen) {
    static const Event_Family_Contract_Row expected_families[] = {
        {EVENT_FAMILY_TRACE, "trace"},
        {EVENT_FAMILY_DIAG, "diag"},
        {EVENT_FAMILY_DIRECTORY, "directory"},
        {EVENT_FAMILY_FLOW, "flow"},
        {EVENT_FAMILY_SCOPE, "scope"},
        {EVENT_FAMILY_POLICY, "policy"},
        {EVENT_FAMILY_VAR, "var"},
        {EVENT_FAMILY_FS, "fs"},
        {EVENT_FAMILY_PROC, "proc"},
        {EVENT_FAMILY_STRING, "string"},
        {EVENT_FAMILY_LIST, "list"},
        {EVENT_FAMILY_MATH, "math"},
        {EVENT_FAMILY_PATH, "path"},
        {EVENT_FAMILY_PROJECT, "project"},
        {EVENT_FAMILY_TARGET, "target"},
        {EVENT_FAMILY_TEST, "test"},
        {EVENT_FAMILY_INSTALL, "install"},
        {EVENT_FAMILY_CPACK, "cpack"},
        {EVENT_FAMILY_PACKAGE, "package"},
    };
    static const Event_Kind_Contract_Row expected_kinds[] = {
        {EVENT_DIAG, EVENT_FAMILY_DIAG, "diag", EVENT_ROLE_DIAGNOSTIC},
        {EVENT_COMMAND_BEGIN, EVENT_FAMILY_TRACE, "command_begin", EVENT_ROLE_TRACE},
        {EVENT_COMMAND_END, EVENT_FAMILY_TRACE, "command_end", EVENT_ROLE_TRACE},
        {EVENT_INCLUDE_BEGIN, EVENT_FAMILY_TRACE, "include_begin", EVENT_ROLE_TRACE},
        {EVENT_INCLUDE_END, EVENT_FAMILY_TRACE, "include_end", EVENT_ROLE_TRACE},
        {EVENT_ADD_SUBDIRECTORY_BEGIN, EVENT_FAMILY_TRACE, "add_subdirectory_begin", EVENT_ROLE_TRACE},
        {EVENT_ADD_SUBDIRECTORY_END, EVENT_FAMILY_TRACE, "add_subdirectory_end", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_CALL, EVENT_FAMILY_TRACE, "cmake_language_call", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_EVAL, EVENT_FAMILY_TRACE, "cmake_language_eval", EVENT_ROLE_TRACE},
        {EVENT_CMAKE_LANGUAGE_DEFER_QUEUE, EVENT_FAMILY_TRACE, "cmake_language_defer_queue", EVENT_ROLE_TRACE},
        {EVENT_DIRECTORY_ENTER, EVENT_FAMILY_DIRECTORY, "directory_enter", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_DIRECTORY_LEAVE, EVENT_FAMILY_DIRECTORY, "directory_leave", EVENT_ROLE_TRACE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "directory_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_FAMILY_DIRECTORY, "global_property_mutate", EVENT_ROLE_STATE | EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_VAR_SET, EVENT_FAMILY_VAR, "var_set", EVENT_ROLE_STATE},
        {EVENT_VAR_UNSET, EVENT_FAMILY_VAR, "var_unset", EVENT_ROLE_STATE},
        {EVENT_SCOPE_PUSH, EVENT_FAMILY_SCOPE, "scope_push", EVENT_ROLE_STATE},
        {EVENT_SCOPE_POP, EVENT_FAMILY_SCOPE, "scope_pop", EVENT_ROLE_STATE},
        {EVENT_POLICY_PUSH, EVENT_FAMILY_POLICY, "policy_push", EVENT_ROLE_STATE},
        {EVENT_POLICY_POP, EVENT_FAMILY_POLICY, "policy_pop", EVENT_ROLE_STATE},
        {EVENT_POLICY_SET, EVENT_FAMILY_POLICY, "policy_set", EVENT_ROLE_STATE},
        {EVENT_FLOW_RETURN, EVENT_FAMILY_FLOW, "flow_return", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_IF_EVAL, EVENT_FAMILY_FLOW, "flow_if_eval", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BRANCH_TAKEN, EVENT_FAMILY_FLOW, "flow_branch_taken", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_LOOP_BEGIN, EVENT_FAMILY_FLOW, "flow_loop_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_LOOP_END, EVENT_FAMILY_FLOW, "flow_loop_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BREAK, EVENT_FAMILY_FLOW, "flow_break", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_CONTINUE, EVENT_FAMILY_FLOW, "flow_continue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_DEFER_QUEUE, EVENT_FAMILY_FLOW, "flow_defer_queue", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_DEFER_FLUSH, EVENT_FAMILY_FLOW, "flow_defer_flush", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BLOCK_BEGIN, EVENT_FAMILY_FLOW, "flow_block_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_BLOCK_END, EVENT_FAMILY_FLOW, "flow_block_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_FUNCTION_BEGIN, EVENT_FAMILY_FLOW, "flow_function_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_FUNCTION_END, EVENT_FAMILY_FLOW, "flow_function_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_MACRO_BEGIN, EVENT_FAMILY_FLOW, "flow_macro_begin", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FLOW_MACRO_END, EVENT_FAMILY_FLOW, "flow_macro_end", EVENT_ROLE_TRACE | EVENT_ROLE_STATE},
        {EVENT_FS_WRITE_FILE, EVENT_FAMILY_FS, "fs_write_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_APPEND_FILE, EVENT_FAMILY_FS, "fs_append_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_READ_FILE, EVENT_FAMILY_FS, "fs_read_file", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_GLOB, EVENT_FAMILY_FS, "fs_glob", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_MKDIR, EVENT_FAMILY_FS, "fs_mkdir", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_REMOVE, EVENT_FAMILY_FS, "fs_remove", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_COPY, EVENT_FAMILY_FS, "fs_copy", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_RENAME, EVENT_FAMILY_FS, "fs_rename", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_CREATE_LINK, EVENT_FAMILY_FS, "fs_create_link", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_CHMOD, EVENT_FAMILY_FS, "fs_chmod", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_ARCHIVE_CREATE, EVENT_FAMILY_FS, "fs_archive_create", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_ARCHIVE_EXTRACT, EVENT_FAMILY_FS, "fs_archive_extract", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_TRANSFER_DOWNLOAD, EVENT_FAMILY_FS, "fs_transfer_download", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_FS_TRANSFER_UPLOAD, EVENT_FAMILY_FS, "fs_transfer_upload", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PROC_EXEC_REQUEST, EVENT_FAMILY_PROC, "proc_exec_request", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PROC_EXEC_RESULT, EVENT_FAMILY_PROC, "proc_exec_result", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_REPLACE, EVENT_FAMILY_STRING, "string_replace", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_CONFIGURE, EVENT_FAMILY_STRING, "string_configure", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_REGEX, EVENT_FAMILY_STRING, "string_regex", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_HASH, EVENT_FAMILY_STRING, "string_hash", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_STRING_TIMESTAMP, EVENT_FAMILY_STRING, "string_timestamp", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_APPEND, EVENT_FAMILY_LIST, "list_append", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_PREPEND, EVENT_FAMILY_LIST, "list_prepend", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_INSERT, EVENT_FAMILY_LIST, "list_insert", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_REMOVE, EVENT_FAMILY_LIST, "list_remove", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_TRANSFORM, EVENT_FAMILY_LIST, "list_transform", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_LIST_SORT, EVENT_FAMILY_LIST, "list_sort", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_MATH_EXPR, EVENT_FAMILY_MATH, "math_expr", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_NORMALIZE, EVENT_FAMILY_PATH, "path_normalize", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_COMPARE, EVENT_FAMILY_PATH, "path_compare", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_PATH_CONVERT, EVENT_FAMILY_PATH, "path_convert", EVENT_ROLE_RUNTIME_EFFECT},
        {EVENT_TEST_ENABLE, EVENT_FAMILY_TEST, "test_enable", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TEST_ADD, EVENT_FAMILY_TEST, "test_add", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_INSTALL_RULE_ADD, EVENT_FAMILY_INSTALL, "install_rule_add", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_INSTALL_TYPE, EVENT_FAMILY_CPACK, "cpack_add_install_type", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_COMPONENT_GROUP, EVENT_FAMILY_CPACK, "cpack_add_component_group", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_CPACK_ADD_COMPONENT, EVENT_FAMILY_CPACK, "cpack_add_component", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PACKAGE_FIND_RESULT, EVENT_FAMILY_PACKAGE, "package_find_result", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PROJECT_DECLARE, EVENT_FAMILY_PROJECT, "project_declare", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_PROJECT_MINIMUM_REQUIRED, EVENT_FAMILY_PROJECT, "project_minimum_required", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_DECLARE, EVENT_FAMILY_TARGET, "target_declare", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_ADD_SOURCE, EVENT_FAMILY_TARGET, "target_add_source", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_ADD_DEPENDENCY, EVENT_FAMILY_TARGET, "target_add_dependency", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_PROP_SET, EVENT_FAMILY_TARGET, "target_prop_set", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_LIBRARIES, EVENT_FAMILY_TARGET, "target_link_libraries", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_OPTIONS, EVENT_FAMILY_TARGET, "target_link_options", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_LINK_DIRECTORIES, EVENT_FAMILY_TARGET, "target_link_directories", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_INCLUDE_DIRECTORIES, EVENT_FAMILY_TARGET, "target_include_directories", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_COMPILE_DEFINITIONS, EVENT_FAMILY_TARGET, "target_compile_definitions", EVENT_ROLE_BUILD_SEMANTIC},
        {EVENT_TARGET_COMPILE_OPTIONS, EVENT_FAMILY_TARGET, "target_compile_options", EVENT_ROLE_BUILD_SEMANTIC},
    };

    ASSERT(EVENT_FAMILY_COUNT == NOB_ARRAY_LEN(expected_families));
    ASSERT(EVENT_KIND_COUNT == NOB_ARRAY_LEN(expected_kinds));

    for (size_t i = 0; i < NOB_ARRAY_LEN(expected_families); i++) {
        const Event_Family_Contract_Row *expected = &expected_families[i];
        ASSERT((Event_Family)i == expected->family);
        ASSERT(strcmp(event_family_name(expected->family), expected->label) == 0);
    }
    ASSERT(strcmp(event_family_name(EVENT_FAMILY_COUNT), "unknown_family") == 0);

    for (size_t i = 0; i < NOB_ARRAY_LEN(expected_kinds); i++) {
        const Event_Kind_Contract_Row *expected = &expected_kinds[i];
        const Event_Kind_Meta *meta = event_kind_meta(expected->kind);
        ASSERT((Event_Kind)i == expected->kind);
        ASSERT(meta != NULL);
        ASSERT(meta->kind == expected->kind);
        ASSERT(meta->family == expected->family);
        ASSERT(meta->role_mask == expected->role_mask);
        ASSERT(meta->default_version == 1);
        ASSERT(strcmp(meta->label, expected->label) == 0);
        ASSERT(strcmp(event_kind_name(expected->kind), expected->label) == 0);
        ASSERT(event_kind_family(expected->kind) == expected->family);
        ASSERT(event_kind_role_mask(expected->kind) == expected->role_mask);
        ASSERT(strcmp(event_family_name(expected->family), "unknown_family") != 0);

        for (uint32_t bit = 1; bit <= EVENT_ROLE_BUILD_SEMANTIC; bit <<= 1) {
            bool expected_has_role = (expected->role_mask & bit) != 0;
            ASSERT(event_kind_has_role(expected->kind, (Event_Role)bit) == expected_has_role);
        }
    }

    ASSERT(event_kind_meta(EVENT_KIND_COUNT) == NULL);
    ASSERT(strcmp(event_kind_name(EVENT_KIND_COUNT), "unknown_event") == 0);
    ASSERT(event_kind_family(EVENT_KIND_COUNT) == EVENT_FAMILY_COUNT);
    ASSERT(event_kind_role_mask(EVENT_KIND_COUNT) == 0);
    ASSERT(!event_kind_has_role(EVENT_KIND_COUNT, EVENT_ROLE_TRACE));

    TEST_PASS();
}

TEST(evaluator_event_ir_metadata_and_stream_contract) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Cmake_Event_Stream *stream = event_stream_create(arena);
    ASSERT(stream != NULL);

    const Event_Kind_Meta *command_begin_meta = event_kind_meta(EVENT_COMMAND_BEGIN);
    ASSERT(command_begin_meta != NULL);
    ASSERT(command_begin_meta->family == EVENT_FAMILY_TRACE);
    ASSERT(event_kind_has_role(EVENT_COMMAND_BEGIN, EVENT_ROLE_TRACE));
    ASSERT(!event_kind_has_role(EVENT_COMMAND_BEGIN, EVENT_ROLE_BUILD_SEMANTIC));

    const Event_Kind_Meta *dir_prop_meta = event_kind_meta(EVENT_DIRECTORY_PROPERTY_MUTATE);
    ASSERT(dir_prop_meta != NULL);
    ASSERT(dir_prop_meta->family == EVENT_FAMILY_DIRECTORY);
    ASSERT(event_kind_has_role(EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_ROLE_BUILD_SEMANTIC));
    ASSERT(event_kind_has_role(EVENT_DIRECTORY_PROPERTY_MUTATE, EVENT_ROLE_STATE));

    const Event_Kind_Meta *global_prop_meta = event_kind_meta(EVENT_GLOBAL_PROPERTY_MUTATE);
    ASSERT(global_prop_meta != NULL);
    ASSERT(global_prop_meta->family == EVENT_FAMILY_DIRECTORY);
    ASSERT(event_kind_has_role(EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_ROLE_BUILD_SEMANTIC));
    ASSERT(event_kind_has_role(EVENT_GLOBAL_PROPERTY_MUTATE, EVENT_ROLE_STATE));

    Event invalid = {0};
    invalid.h.kind = EV_UNKNOWN;
    invalid.h.origin.file_path = nob_sv_from_cstr("invalid.cmake");
    ASSERT(!event_stream_push(stream, &invalid));
    ASSERT(stream->count == 0);

    Arena *source_arena = arena_create(64 * 1024);
    ASSERT(source_arena != NULL);

    char *origin_path = arena_strndup(source_arena, "origin.cmake", strlen("origin.cmake"));
    char *command_name = arena_strndup(source_arena, "ephemeral_command", strlen("ephemeral_command"));
    ASSERT(origin_path != NULL);
    ASSERT(command_name != NULL);
    Event ev = {0};
    ev.h.kind = EVENT_COMMAND_BEGIN;
    ev.h.origin.file_path = nob_sv_from_parts(origin_path, strlen(origin_path));
    ev.h.origin.line = 7;
    ev.h.origin.col = 3;
    ev.as.command_begin.command_name = nob_sv_from_parts(command_name, strlen(command_name));
    ev.as.command_begin.dispatch_kind = EVENT_COMMAND_DISPATCH_BUILTIN;
    ev.as.command_begin.argc = 2;
    ASSERT(event_stream_push(stream, &ev));

    origin_path[0] = 'X';
    command_name[0] = 'X';

    ASSERT(stream->count == 1);
    ASSERT(stream->items[0].h.version == 1);
    ASSERT(stream->items[0].h.seq == 1);
    ASSERT(nob_sv_eq(stream->items[0].h.origin.file_path, nob_sv_from_cstr("origin.cmake")));
    ASSERT(nob_sv_eq(stream->items[0].as.command_begin.command_name, nob_sv_from_cstr("ephemeral_command")));

    char *property_name = arena_strndup(source_arena, "INCLUDE_DIRECTORIES", strlen("INCLUDE_DIRECTORIES"));
    char *item_buf = arena_strndup(source_arena, "include", strlen("include"));
    ASSERT(property_name != NULL);
    ASSERT(item_buf != NULL);
    String_View items[] = {nob_sv_from_parts(item_buf, strlen(item_buf))};
    ev = (Event){0};
    ev.h.kind = EVENT_DIRECTORY_PROPERTY_MUTATE;
    ev.h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.as.directory_property_mutate.property_name = nob_sv_from_parts(property_name, strlen(property_name));
    ev.as.directory_property_mutate.op = EVENT_PROPERTY_MUTATE_PREPEND_LIST;
    ev.as.directory_property_mutate.modifier_flags =
        EVENT_PROPERTY_MODIFIER_BEFORE | EVENT_PROPERTY_MODIFIER_SYSTEM;
    ev.as.directory_property_mutate.items = items;
    ev.as.directory_property_mutate.item_count = NOB_ARRAY_LEN(items);
    ASSERT(event_stream_push(stream, &ev));

    char *global_property_name =
        arena_strndup(source_arena, "IR_GLOBAL_PROP", strlen("IR_GLOBAL_PROP"));
    char *global_item_a = arena_strndup(source_arena, "global_a", strlen("global_a"));
    char *global_item_b = arena_strndup(source_arena, "global_b", strlen("global_b"));
    ASSERT(global_property_name != NULL);
    ASSERT(global_item_a != NULL);
    ASSERT(global_item_b != NULL);
    String_View global_items[] = {
        nob_sv_from_parts(global_item_a, strlen(global_item_a)),
        nob_sv_from_parts(global_item_b, strlen(global_item_b)),
    };
    ev = (Event){0};
    ev.h.kind = EVENT_GLOBAL_PROPERTY_MUTATE;
    ev.h.origin.file_path = nob_sv_from_cstr("CMakeLists.txt");
    ev.as.global_property_mutate.property_name =
        nob_sv_from_parts(global_property_name, strlen(global_property_name));
    ev.as.global_property_mutate.op = EVENT_PROPERTY_MUTATE_SET;
    ev.as.global_property_mutate.items = global_items;
    ev.as.global_property_mutate.item_count = NOB_ARRAY_LEN(global_items);
    ASSERT(event_stream_push(stream, &ev));

    property_name[0] = 'X';
    item_buf[0] = 'X';
    global_property_name[0] = 'X';
    global_item_a[0] = 'X';
    global_item_b[0] = 'X';
    arena_destroy(source_arena);

    ASSERT(stream->count == 3);
    ASSERT(evaluator_stream_has_monotonic_sequence(stream));
    ASSERT(nob_sv_eq(stream->items[1].as.directory_property_mutate.property_name,
                     nob_sv_from_cstr("INCLUDE_DIRECTORIES")));
    ASSERT(stream->items[1].as.directory_property_mutate.item_count == 1);
    ASSERT(nob_sv_eq(stream->items[1].as.directory_property_mutate.items[0],
                     nob_sv_from_cstr("include")));
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.property_name,
                     nob_sv_from_cstr("IR_GLOBAL_PROP")));
    ASSERT(stream->items[2].as.global_property_mutate.item_count == 2);
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.items[0],
                     nob_sv_from_cstr("global_a")));
    ASSERT(nob_sv_eq(stream->items[2].as.global_property_mutate.items[1],
                     nob_sv_from_cstr("global_b")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_directory_semantics_and_trace_surface) {
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
        "file(WRITE ir_include.cmake [=[set(IR_INCLUDED 1)\n]=])\n"
        "file(MAKE_DIRECTORY ir_subdir)\n"
        "file(WRITE ir_subdir/CMakeLists.txt [=[add_library(ir_subdir_lib STATIC sub.c)\n]=])\n"
        "add_compile_options(-Wall)\n"
        "add_compile_definitions(IR_DEF)\n"
        "add_link_options(-Wl,--as-needed)\n"
        "include_directories(BEFORE SYSTEM ir_inc_a ir_inc_b)\n"
        "link_directories(BEFORE ir_lib)\n"
        "set_property(GLOBAL PROPERTY IR_GLOBAL_PROP ir_global_a ir_global_b)\n"
        "get_property(IR_COMPILE_OPTIONS DIRECTORY PROPERTY COMPILE_OPTIONS)\n"
        "get_property(IR_COMPILE_DEFINITIONS DIRECTORY PROPERTY COMPILE_DEFINITIONS)\n"
        "get_property(IR_LINK_OPTIONS DIRECTORY PROPERTY LINK_OPTIONS)\n"
        "get_property(IR_INCLUDE_DIRECTORIES DIRECTORY PROPERTY INCLUDE_DIRECTORIES)\n"
        "get_property(IR_LINK_DIRECTORIES DIRECTORY PROPERTY LINK_DIRECTORIES)\n"
        "get_property(IR_GLOBAL_PROP_OUT GLOBAL PROPERTY IR_GLOBAL_PROP)\n"
        "include(ir_include.cmake)\n"
        "add_subdirectory(ir_subdir)\n"
        "unknown_event_ir_cmd()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    bool saw_compile_options = false;
    bool saw_compile_definitions = false;
    bool saw_link_options = false;
    bool saw_include_directories = false;
    bool saw_link_directories = false;
    bool saw_global_property = false;
    bool saw_include_begin = false;
    bool saw_include_end = false;
    bool saw_subdir_begin = false;
    bool saw_subdir_end = false;
    bool saw_directory_enter = false;
    bool saw_directory_leave = false;
    bool saw_unknown_begin = false;
    bool saw_unknown_end = false;
    size_t include_begin = (size_t)-1;
    size_t include_end = (size_t)-1;
    size_t subdir_begin = (size_t)-1;
    size_t subdir_end = (size_t)-1;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("COMPILE_OPTIONS"))) {
            saw_compile_options =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("-Wall"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
            saw_compile_definitions =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("IR_DEF"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("LINK_OPTIONS"))) {
            saw_link_options =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_APPEND_LIST &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("-Wl,--as-needed"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("INCLUDE_DIRECTORIES"))) {
            saw_include_directories =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_PREPEND_LIST &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) != 0 &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_SYSTEM) != 0 &&
                ev->as.directory_property_mutate.item_count == 2 &&
                sv_contains_sv(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("ir_inc_a")) &&
                sv_contains_sv(ev->as.directory_property_mutate.items[1], nob_sv_from_cstr("ir_inc_b"));
        }
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("LINK_DIRECTORIES"))) {
            saw_link_directories =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_PREPEND_LIST &&
                (ev->as.directory_property_mutate.modifier_flags & EVENT_PROPERTY_MODIFIER_BEFORE) != 0 &&
                ev->as.directory_property_mutate.item_count == 1 &&
                sv_contains_sv(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("ir_lib"));
        }
        if (ev->h.kind == EVENT_GLOBAL_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.global_property_mutate.property_name, nob_sv_from_cstr("IR_GLOBAL_PROP"))) {
            saw_global_property =
                ev->as.global_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.global_property_mutate.item_count == 2 &&
                nob_sv_eq(ev->as.global_property_mutate.items[0], nob_sv_from_cstr("ir_global_a")) &&
                nob_sv_eq(ev->as.global_property_mutate.items[1], nob_sv_from_cstr("ir_global_b"));
        }
        if (ev->h.kind == EVENT_INCLUDE_BEGIN &&
            sv_contains_sv(ev->as.include_begin.path, nob_sv_from_cstr("ir_include.cmake"))) {
            saw_include_begin = true;
            include_begin = i;
        }
        if (ev->h.kind == EVENT_INCLUDE_END &&
            sv_contains_sv(ev->as.include_end.path, nob_sv_from_cstr("ir_include.cmake"))) {
            saw_include_end = true;
            include_end = i;
        }
        if (ev->h.kind == EVENT_ADD_SUBDIRECTORY_BEGIN &&
            sv_contains_sv(ev->as.add_subdirectory_begin.source_dir, nob_sv_from_cstr("ir_subdir"))) {
            saw_subdir_begin = true;
            subdir_begin = i;
        }
        if (ev->h.kind == EVENT_ADD_SUBDIRECTORY_END &&
            sv_contains_sv(ev->as.add_subdirectory_end.source_dir, nob_sv_from_cstr("ir_subdir"))) {
            saw_subdir_end = true;
            subdir_end = i;
        }
        if (ev->h.kind == EVENT_DIRECTORY_ENTER) saw_directory_enter = true;
        if (ev->h.kind == EVENT_DIRECTORY_LEAVE) saw_directory_leave = true;
        if (ev->h.kind == EVENT_COMMAND_BEGIN &&
            nob_sv_eq(ev->as.command_begin.command_name, nob_sv_from_cstr("unknown_event_ir_cmd")) &&
            ev->as.command_begin.dispatch_kind == EVENT_COMMAND_DISPATCH_UNKNOWN) {
            saw_unknown_begin = true;
        }
        if (ev->h.kind == EVENT_COMMAND_END &&
            nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("unknown_event_ir_cmd")) &&
            ev->as.command_end.dispatch_kind == EVENT_COMMAND_DISPATCH_UNKNOWN &&
            ev->as.command_end.status == EVENT_COMMAND_STATUS_UNSUPPORTED) {
            saw_unknown_end = true;
        }
    }

    ASSERT(saw_compile_options);
    ASSERT(saw_compile_definitions);
    ASSERT(saw_link_options);
    ASSERT(saw_include_directories);
    ASSERT(saw_link_directories);
    ASSERT(saw_global_property);
    ASSERT(saw_include_begin);
    ASSERT(saw_include_end);
    ASSERT(saw_subdir_begin);
    ASSERT(saw_subdir_end);
    ASSERT(saw_directory_enter);
    ASSERT(saw_directory_leave);
    ASSERT(saw_unknown_begin);
    ASSERT(saw_unknown_end);
    ASSERT(include_begin != (size_t)-1);
    ASSERT(include_end != (size_t)-1);
    ASSERT(subdir_begin != (size_t)-1);
    ASSERT(subdir_end != (size_t)-1);

    size_t include_enter = (size_t)-1;
    size_t include_leave = (size_t)-1;
    for (size_t i = include_begin + 1; i < include_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_ENTER) {
            include_enter = i;
            break;
        }
    }
    for (size_t i = include_enter + 1; i < include_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_LEAVE) {
            include_leave = i;
            break;
        }
    }
    ASSERT(include_enter != (size_t)-1);
    ASSERT(include_leave != (size_t)-1);
    ASSERT(include_begin < include_enter);
    ASSERT(include_enter < include_leave);
    ASSERT(include_leave < include_end);

    size_t subdir_enter = (size_t)-1;
    size_t subdir_leave = (size_t)-1;
    for (size_t i = subdir_begin + 1; i < subdir_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_ENTER) {
            subdir_enter = i;
            break;
        }
    }
    for (size_t i = subdir_enter + 1; i < subdir_end; i++) {
        if (stream->items[i].h.kind == EVENT_DIRECTORY_LEAVE) {
            subdir_leave = i;
            break;
        }
    }
    ASSERT(subdir_enter != (size_t)-1);
    ASSERT(subdir_leave != (size_t)-1);
    ASSERT(subdir_begin < subdir_enter);
    ASSERT(subdir_enter < subdir_leave);
    ASSERT(subdir_leave < subdir_end);
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_COMPILE_OPTIONS")), nob_sv_from_cstr("-Wall")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_COMPILE_DEFINITIONS")), nob_sv_from_cstr("IR_DEF")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_LINK_OPTIONS")),
                     nob_sv_from_cstr("-Wl,--as-needed")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("IR_GLOBAL_PROP_OUT")),
                     nob_sv_from_cstr("ir_global_a;ir_global_b")));

    String_View include_dirs = eval_var_get(ctx, nob_sv_from_cstr("IR_INCLUDE_DIRECTORIES"));
    ASSERT(semicolon_list_count(include_dirs) == 2);
    ASSERT(sv_contains_sv(semicolon_list_item_at(include_dirs, 0), nob_sv_from_cstr("ir_inc_a")));
    ASSERT(sv_contains_sv(semicolon_list_item_at(include_dirs, 1), nob_sv_from_cstr("ir_inc_b")));

    String_View link_dirs = eval_var_get(ctx, nob_sv_from_cstr("IR_LINK_DIRECTORIES"));
    ASSERT(semicolon_list_count(link_dirs) == 1);
    ASSERT(sv_contains_sv(semicolon_list_item_at(link_dirs, 0), nob_sv_from_cstr("ir_lib")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_command_trace_sequences_unknown_and_error_paths) {
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
        "function(trace_fn)\n"
        "  math()\n"
        "endfunction()\n"
        "macro(trace_macro)\n"
        "  return()\n"
        "endmacro()\n"
        "unknown_trace_cmd()\n"
        "math()\n"
        "trace_fn()\n"
        "trace_macro()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    size_t unknown_begin = evaluator_find_command_begin_index(stream,
                                                              nob_sv_from_cstr("unknown_trace_cmd"),
                                                              7,
                                                              EVENT_COMMAND_DISPATCH_UNKNOWN);
    size_t unknown_diag = evaluator_find_diag_index(stream,
                                                    nob_sv_from_cstr("unknown_trace_cmd"),
                                                    7,
                                                    nob_sv_from_cstr("Unknown command"));
    size_t unknown_end = evaluator_find_command_end_index(stream,
                                                          nob_sv_from_cstr("unknown_trace_cmd"),
                                                          7,
                                                          EVENT_COMMAND_DISPATCH_UNKNOWN,
                                                          EVENT_COMMAND_STATUS_UNSUPPORTED);

    size_t math_begin = evaluator_find_command_begin_index(stream,
                                                           nob_sv_from_cstr("math"),
                                                           8,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t math_diag = evaluator_find_diag_index(stream,
                                                 nob_sv_from_cstr("math"),
                                                 8,
                                                 nob_sv_from_cstr("math() requires a subcommand"));
    size_t math_end = evaluator_find_command_end_index(stream,
                                                       nob_sv_from_cstr("math"),
                                                       8,
                                                       EVENT_COMMAND_DISPATCH_BUILTIN,
                                                       EVENT_COMMAND_STATUS_ERROR);

    size_t fn_begin = evaluator_find_command_begin_index(stream,
                                                         nob_sv_from_cstr("trace_fn"),
                                                         9,
                                                         EVENT_COMMAND_DISPATCH_FUNCTION);
    size_t fn_inner_begin = evaluator_find_command_begin_index(stream,
                                                               nob_sv_from_cstr("math"),
                                                               2,
                                                               EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t fn_inner_diag = evaluator_find_diag_index(stream,
                                                     nob_sv_from_cstr("math"),
                                                     2,
                                                     nob_sv_from_cstr("math() requires a subcommand"));
    size_t fn_inner_end = evaluator_find_command_end_index(stream,
                                                           nob_sv_from_cstr("math"),
                                                           2,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN,
                                                           EVENT_COMMAND_STATUS_ERROR);
    size_t fn_end = evaluator_find_command_end_index(stream,
                                                     nob_sv_from_cstr("trace_fn"),
                                                     9,
                                                     EVENT_COMMAND_DISPATCH_FUNCTION,
                                                     EVENT_COMMAND_STATUS_ERROR);

    size_t macro_begin = evaluator_find_command_begin_index(stream,
                                                            nob_sv_from_cstr("trace_macro"),
                                                            10,
                                                            EVENT_COMMAND_DISPATCH_MACRO);
    size_t macro_inner_begin = evaluator_find_command_begin_index(stream,
                                                                  nob_sv_from_cstr("return"),
                                                                  5,
                                                                  EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t macro_inner_diag = evaluator_find_diag_index(stream,
                                                        nob_sv_from_cstr("return"),
                                                        5,
                                                        nob_sv_from_cstr("return() cannot be used inside macro()"));
    size_t macro_inner_end = evaluator_find_command_end_index(stream,
                                                              nob_sv_from_cstr("return"),
                                                              5,
                                                              EVENT_COMMAND_DISPATCH_BUILTIN,
                                                              EVENT_COMMAND_STATUS_ERROR);
    size_t macro_end = evaluator_find_command_end_index(stream,
                                                        nob_sv_from_cstr("trace_macro"),
                                                        10,
                                                        EVENT_COMMAND_DISPATCH_MACRO,
                                                        EVENT_COMMAND_STATUS_ERROR);

    ASSERT(unknown_begin != (size_t)-1);
    ASSERT(unknown_diag != (size_t)-1);
    ASSERT(unknown_end != (size_t)-1);
    ASSERT(unknown_begin < unknown_diag);
    ASSERT(unknown_diag < unknown_end);

    ASSERT(math_begin != (size_t)-1);
    ASSERT(math_diag != (size_t)-1);
    ASSERT(math_end != (size_t)-1);
    ASSERT(math_begin < math_diag);
    ASSERT(math_diag < math_end);

    ASSERT(fn_begin != (size_t)-1);
    ASSERT(fn_inner_begin != (size_t)-1);
    ASSERT(fn_inner_diag != (size_t)-1);
    ASSERT(fn_inner_end != (size_t)-1);
    ASSERT(fn_end != (size_t)-1);
    ASSERT(fn_begin < fn_inner_begin);
    ASSERT(fn_inner_begin < fn_inner_diag);
    ASSERT(fn_inner_diag < fn_inner_end);
    ASSERT(fn_inner_end < fn_end);

    ASSERT(macro_begin != (size_t)-1);
    ASSERT(macro_inner_begin != (size_t)-1);
    ASSERT(macro_inner_diag != (size_t)-1);
    ASSERT(macro_inner_end != (size_t)-1);
    ASSERT(macro_end != (size_t)-1);
    ASSERT(macro_begin < macro_inner_begin);
    ASSERT(macro_inner_begin < macro_inner_diag);
    ASSERT(macro_inner_diag < macro_inner_end);
    ASSERT(macro_inner_end < macro_end);
    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_event_ir_command_trace_sequences_success_paths) {
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
        "function(trace_fn_ok)\n"
        "  set(FN_LOCAL 1)\n"
        "endfunction()\n"
        "macro(trace_macro_ok)\n"
        "  set(MAC_VISIBLE 1)\n"
        "endmacro()\n"
        "set(TOP_OK 1)\n"
        "trace_fn_ok()\n"
        "trace_macro_ok()\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    size_t top_begin = evaluator_find_command_begin_index(stream,
                                                          nob_sv_from_cstr("set"),
                                                          7,
                                                          EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t top_end = evaluator_find_command_end_index(stream,
                                                      nob_sv_from_cstr("set"),
                                                      7,
                                                      EVENT_COMMAND_DISPATCH_BUILTIN,
                                                      EVENT_COMMAND_STATUS_SUCCESS);
    size_t fn_begin = evaluator_find_command_begin_index(stream,
                                                         nob_sv_from_cstr("trace_fn_ok"),
                                                         8,
                                                         EVENT_COMMAND_DISPATCH_FUNCTION);
    size_t fn_inner_begin = evaluator_find_command_begin_index(stream,
                                                               nob_sv_from_cstr("set"),
                                                               2,
                                                               EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t fn_inner_end = evaluator_find_command_end_index(stream,
                                                           nob_sv_from_cstr("set"),
                                                           2,
                                                           EVENT_COMMAND_DISPATCH_BUILTIN,
                                                           EVENT_COMMAND_STATUS_SUCCESS);
    size_t fn_end = evaluator_find_command_end_index(stream,
                                                     nob_sv_from_cstr("trace_fn_ok"),
                                                     8,
                                                     EVENT_COMMAND_DISPATCH_FUNCTION,
                                                     EVENT_COMMAND_STATUS_SUCCESS);
    size_t macro_begin = evaluator_find_command_begin_index(stream,
                                                            nob_sv_from_cstr("trace_macro_ok"),
                                                            9,
                                                            EVENT_COMMAND_DISPATCH_MACRO);
    size_t macro_inner_begin = evaluator_find_command_begin_index(stream,
                                                                  nob_sv_from_cstr("set"),
                                                                  5,
                                                                  EVENT_COMMAND_DISPATCH_BUILTIN);
    size_t macro_inner_end = evaluator_find_command_end_index(stream,
                                                              nob_sv_from_cstr("set"),
                                                              5,
                                                              EVENT_COMMAND_DISPATCH_BUILTIN,
                                                              EVENT_COMMAND_STATUS_SUCCESS);
    size_t macro_end = evaluator_find_command_end_index(stream,
                                                        nob_sv_from_cstr("trace_macro_ok"),
                                                        9,
                                                        EVENT_COMMAND_DISPATCH_MACRO,
                                                        EVENT_COMMAND_STATUS_SUCCESS);

    ASSERT(top_begin != (size_t)-1);
    ASSERT(top_end != (size_t)-1);
    ASSERT(top_begin < top_end);

    ASSERT(fn_begin != (size_t)-1);
    ASSERT(fn_inner_begin != (size_t)-1);
    ASSERT(fn_inner_end != (size_t)-1);
    ASSERT(fn_end != (size_t)-1);
    ASSERT(fn_begin < fn_inner_begin);
    ASSERT(fn_inner_begin < fn_inner_end);
    ASSERT(fn_inner_end < fn_end);

    ASSERT(macro_begin != (size_t)-1);
    ASSERT(macro_inner_begin != (size_t)-1);
    ASSERT(macro_inner_end != (size_t)-1);
    ASSERT(macro_end != (size_t)-1);
    ASSERT(macro_begin < macro_inner_begin);
    ASSERT(macro_inner_begin < macro_inner_end);
    ASSERT(macro_inner_end < macro_end);

    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("TOP_OK")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_var_get(ctx, nob_sv_from_cstr("MAC_VISIBLE")), nob_sv_from_cstr("1")));
    ASSERT(eval_var_get(ctx, nob_sv_from_cstr("FN_LOCAL")).count == 0);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body) {
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
        "set(MAC_RET start)\n"
        "macro(mc_ret)\n"
        "  set(MAC_RET before)\n"
        "  return()\n"
        "  set(MAC_RET after)\n"
        "endmacro()\n"
        "mc_ret()\n"
        "add_executable(ret_macro main.c)\n"
        "target_compile_definitions(ret_macro PRIVATE MAC_RET=${MAC_RET})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_macro_return_error = false;
    bool saw_macro_ret_after = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("return() cannot be used inside macro()"))) {
            saw_macro_return_error = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_macro")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MAC_RET=after"))) {
            saw_macro_ret_after = true;
        }
    }

    ASSERT(saw_macro_return_error);
    ASSERT(saw_macro_ret_after);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate) {
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
        "set(RET_OLD root_old)\n"
        "set(RET_NEW root_new)\n"
        "cmake_policy(SET CMP0140 OLD)\n"
        "function(ret_old)\n"
        "  set(RET_OLD changed_old)\n"
        "  return(PROPAGATE RET_OLD)\n"
        "endfunction()\n"
        "ret_old()\n"
        "cmake_policy(SET CMP0140 NEW)\n"
        "function(ret_new)\n"
        "  set(RET_NEW changed_new)\n"
        "  return(PROPAGATE RET_NEW)\n"
        "endfunction()\n"
        "ret_new()\n"
        "add_executable(ret_cmp0140 main.c)\n"
        "target_compile_definitions(ret_cmp0140 PRIVATE RET_OLD=${RET_OLD} RET_NEW=${RET_NEW})\n");
    ASSERT(!eval_result_is_fatal(evaluator_run(ctx, root)));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_ret_old_root = false;
    bool saw_ret_new_changed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_OLD=root_old"))) {
            saw_ret_old_root = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_NEW=changed_new"))) {
            saw_ret_new_changed = true;
        }
    }

    ASSERT(saw_ret_old_root);
    ASSERT(saw_ret_new_changed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}


void run_evaluator_v2_batch2(int *passed, int *failed) {
    test_evaluator_cmake_language_core_subcommands_work(passed, failed);
    test_evaluator_cmake_language_dependency_provider_models_find_package_hook(passed, failed);
    test_evaluator_cmake_language_dependency_provider_models_fetchcontent_hook(passed, failed);
    test_evaluator_fetchcontent_url_population_and_redirect_override(passed, failed);
    test_evaluator_fetchcontent_populate_direct_url_download_no_extract_does_not_add_subdirectory(passed, failed);
    test_evaluator_fetchcontent_populate_saved_details_git_clones_without_add_subdirectory(passed, failed);
    test_evaluator_fetchcontent_makeavailable_try_find_package_always_prefers_package_resolution(passed, failed);
    test_evaluator_fetchcontent_negative_declarations_and_hash_failure_surface_diags(passed, failed);
    test_evaluator_cmake_language_eval_inline_soft_error_preserves_context(passed, failed);
    test_evaluator_cmake_language_dependency_provider_rejects_invalid_forms(passed, failed);
    test_evaluator_target_compile_definitions_normalizes_dash_d_items(passed, failed);
    test_evaluator_add_custom_command_target_validates_signature_and_target(passed, failed);
    test_evaluator_add_custom_command_output_validates_conflicts(passed, failed);
    test_evaluator_event_ir_taxonomy_is_frozen(passed, failed);
    test_evaluator_event_ir_metadata_and_stream_contract(passed, failed);
    test_evaluator_event_ir_directory_semantics_and_trace_surface(passed, failed);
    test_evaluator_event_ir_command_trace_sequences_unknown_and_error_paths(passed, failed);
    test_evaluator_event_ir_command_trace_sequences_success_paths(passed, failed);
    test_evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body(passed, failed);
    test_evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate(passed, failed);
}
