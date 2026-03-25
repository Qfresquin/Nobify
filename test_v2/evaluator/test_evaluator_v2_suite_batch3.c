#include "test_evaluator_v2_common.h"

TEST(evaluator_list_transform_genex_strip_and_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x\")\n"
        "list(TRANSFORM L GENEX_STRIP OUTPUT_VARIABLE L_STRIPPED)\n"
        "list(TRANSFORM L APPEND \"_S\" AT 0 OUTPUT_VARIABLE L_APPENDED)\n"
        "add_executable(list_transform_ov main.c)\n"
        "target_compile_definitions(list_transform_ov PRIVATE \"L=${L}\" \"L_STRIPPED=${L_STRIPPED}\" \"L_APPENDED=${L_APPENDED}\")\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_original = false;
    bool saw_stripped = false;
    bool saw_appended = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item,
                      nob_sv_from_cstr("L=$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_original = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("L_STRIPPED=;a;x"))) {
            saw_stripped = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item,
                             nob_sv_from_cstr("L_APPENDED=$<CONFIG:Debug>_S;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_appended = true;
        }
    }

    ASSERT(saw_original);
    ASSERT(saw_stripped);
    ASSERT(saw_appended);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_transform_output_variable_requires_single_output_var) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"a;b\")\n"
        "list(TRANSFORM L TOUPPER OUTPUT_VARIABLE)\n"
        "list(TRANSFORM L TOUPPER AT 0 OUTPUT_VARIABLE OUT EXTRA)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    size_t output_arity_errors = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.cause,
                       nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"))) {
            continue;
        }
        output_arity_errors++;
    }
    ASSERT(output_arity_errors == 2);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_sort_and_transform_selector_surface_matches_documented_combinations) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(SORT_NAT \"a10;a2;A1\")\n"
        "list(SORT SORT_NAT COMPARE NATURAL CASE INSENSITIVE)\n"
        "set(SORT_BASE \"/tmp/A2.txt;/tmp/a1.txt;/tmp/b10.txt\")\n"
        "list(SORT SORT_BASE COMPARE FILE_BASENAME CASE INSENSITIVE ORDER DESCENDING)\n"
        "set(L \"  one  ;two;three;four\")\n"
        "list(TRANSFORM L STRIP AT 0 OUTPUT_VARIABLE L_AT)\n"
        "list(TRANSFORM L APPEND _X FOR 1 3 2 OUTPUT_VARIABLE L_FOR)\n"
        "list(TRANSFORM L TOUPPER REGEX \"^t\" OUTPUT_VARIABLE L_REGEX)\n"
        "list(TRANSFORM L REPLACE \"o\" \"O\" REGEX \"^t\" OUTPUT_VARIABLE L_REPLACE_REGEX)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SORT_NAT")),
                     nob_sv_from_cstr("A1;a2;a10")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SORT_BASE")),
                     nob_sv_from_cstr("/tmp/b10.txt;/tmp/A2.txt;/tmp/a1.txt")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("L")),
                     nob_sv_from_cstr("  one  ;two;three;four")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("L_AT")),
                     nob_sv_from_cstr("one;two;three;four")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("L_FOR")),
                     nob_sv_from_cstr("  one  ;two_X;three;four_X")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("L_REGEX")),
                     nob_sv_from_cstr("  one  ;TWO;THREE;four")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("L_REPLACE_REGEX")),
                     nob_sv_from_cstr("  one  ;twO;three;four")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_math_rejects_empty_and_incomplete_invocations) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "math()\n"
        "math(EXPR)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool found_empty_error = false;
    bool found_expr_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("math() requires a subcommand"))) {
            found_empty_error = true;
        }
        if (nob_sv_eq(stream->items[i].as.diag.cause,
                      nob_sv_from_cstr("math(EXPR) requires output variable and expression"))) {
            found_expr_arity_error = true;
        }
    }
    ASSERT(found_empty_error);
    ASSERT(found_expr_arity_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_target_properties_rejects_alias_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_target_properties(alias_real PROPERTIES OUTPUT_NAME x)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found_alias_error = false;
    bool emitted_prop_for_alias = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_target_properties() cannot be used on ALIAS targets"))) {
            found_alias_error = true;
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_prop_for_alias = true;
        }
    }
    ASSERT(found_alias_error);
    ASSERT(!emitted_prop_for_alias);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_executable_imported_and_alias_signatures) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(tool IMPORTED GLOBAL)\n"
        "add_executable(tool_alias ALIAS tool)\n"
        "if(TARGET tool_alias)\n"
        "  set(HAS_TOOL_ALIAS 1)\n"
        "endif()\n"
        "add_executable(alias_probe main.c)\n"
        "target_compile_definitions(alias_probe PRIVATE HAS_TOOL_ALIAS=${HAS_TOOL_ALIAS})\n"
        "add_executable(tool_alias_bad ALIAS missing_tool)\n"
        "add_executable(tool_alias2 ALIAS tool_alias)\n"
        "add_executable(tool_bad IMPORTED source.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_has_alias = false;
    bool saw_alias_missing_err = false;
    bool saw_alias_of_alias_err = false;
    bool saw_imported_sources_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("alias_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("HAS_TOOL_ALIAS=1"))) {
            saw_has_alias = true;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target does not exist"))) {
                saw_alias_missing_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_alias_of_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_executable(IMPORTED ...) does not accept source files"))) {
                saw_imported_sources_err = true;
            }
        }
    }

    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_has_alias);
    ASSERT(saw_alias_missing_err);
    ASSERT(saw_alias_of_alias_err);
    ASSERT(saw_imported_sources_err);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_library_imported_alias_and_default_type) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_SHARED_LIBS ON)\n"
        "add_library(auto_lib auto.c)\n"
        "add_library(imp_lib UNKNOWN IMPORTED GLOBAL)\n"
        "add_library(base_lib STATIC base.c)\n"
        "add_library(base_alias ALIAS base_lib)\n"
        "add_library(bad_alias ALIAS base_alias)\n"
        "add_library(bad_import IMPORTED)\n"
        "add_library(iface INTERFACE EXCLUDE_FROM_ALL)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_auto_shared = false;
    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_iface_exclude = false;
    bool saw_iface_bad_source = false;
    bool saw_bad_alias_err = false;
    bool saw_bad_import_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_DECLARE) {
            if (nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("auto_lib")) &&
                ev->as.target_declare.type == EV_TARGET_LIBRARY_SHARED) {
                saw_auto_shared = true;
            }
        }
        if (ev->h.kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("iface")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("EXCLUDE_FROM_ALL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_iface_exclude = true;
            }
        }
        if (ev->h.kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("iface")) &&
            nob_sv_eq(ev->as.target_add_source.path, nob_sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            saw_iface_bad_source = true;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_bad_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_library(IMPORTED ...) requires an explicit library type"))) {
                saw_bad_import_err = true;
            }
        }
    }

    ASSERT(saw_auto_shared);
    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_iface_exclude);
    ASSERT(!saw_iface_bad_source);
    ASSERT(saw_bad_alias_err);
    ASSERT(saw_bad_import_err);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_target_rejects_alias_and_unknown_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_property(TARGET alias_real PROPERTY OUTPUT_NAME bad_alias)\n"
        "set_property(TARGET missing_t PROPERTY OUTPUT_NAME bad_missing)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_alias_error = false;
    bool saw_missing_error = false;
    bool emitted_for_alias = false;
    bool emitted_for_missing = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) target was not declared"))) {
                saw_missing_error = true;
            }
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_for_alias = true;
        }
        if (ev->h.kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("missing_t"))) {
            emitted_for_missing = true;
        }
    }

    ASSERT(saw_alias_error);
    ASSERT(saw_missing_error);
    ASSERT(!emitted_for_alias);
    ASSERT(!emitted_for_missing);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_define_property_initializes_target_properties_from_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(MY_INIT seeded)\n"
        "define_property(GLOBAL PROPERTY NOB_G)\n"
        "define_property(TARGET PROPERTY CUSTOM_FLAG INITIALIZE_FROM_VARIABLE MY_INIT)\n"
        "define_property(TARGET PROPERTY CUSTOM_FLAG BRIEF_DOCS ignored)\n"
        "add_library(real STATIC real.c)\n"
        "set(MY_INIT second)\n"
        "add_executable(app main.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    size_t custom_flag_prop_sets = 0;
    bool saw_real_seeded = false;
    bool saw_app_second = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_PROP_SET) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("CUSTOM_FLAG"))) continue;
        custom_flag_prop_sets++;
        if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("real")) &&
            nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("seeded"))) {
            saw_real_seeded = true;
        }
        if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("app")) &&
            nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("second"))) {
            saw_app_second = true;
        }
    }

    ASSERT(custom_flag_prop_sets == 2);
    ASSERT(saw_real_seeded);
    ASSERT(saw_app_second);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_source_test_directory_clauses_parse_and_apply) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY src)\n"
        "file(WRITE src/CMakeLists.txt \"# empty\\n\")\n"
        "add_subdirectory(src)\n"
        "add_executable(src_t main.c)\n"
        "add_test(NAME smoke COMMAND src_t)\n"
        "set_property(SOURCE foo.c DIRECTORY src PROPERTY LANGUAGE C)\n"
        "set_property(SOURCE bar.c TARGET_DIRECTORY src_t PROPERTY LANGUAGE CXX)\n"
        "set_property(TEST smoke DIRECTORY . PROPERTY LABELS fast)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_source_dir_var_set = false;
    bool saw_source_target_dir_var_set = false;
    bool saw_test_dir_var_set = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_VAR_SET) continue;
        if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("C")) &&
            nob_sv_eq(ev->as.var_set.key,
                      nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::DIRECTORY::src::foo.c::LANGUAGE"))) {
            saw_source_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("CXX")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::DIRECTORY::.::bar.c::LANGUAGE"))) {
            saw_source_target_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("fast")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::DIRECTORY::.::smoke::LABELS"))) {
            saw_test_dir_var_set = true;
        }
    }

    ASSERT(saw_source_dir_var_set);
    ASSERT(saw_source_target_dir_var_set);
    ASSERT(saw_test_dir_var_set);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_allows_zero_objects_and_validates_test_lookup) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_test(NAME smoke COMMAND real)\n"
        "set_property(TARGET PROPERTY OUTPUT_NAME ignored)\n"
        "set_property(SOURCE PROPERTY LANGUAGE C)\n"
        "set_property(INSTALL PROPERTY FOO bar)\n"
        "set_property(TEST PROPERTY LABELS fast)\n"
        "set_property(CACHE PROPERTY VALUE cache_ignore)\n"
        "set_property(TEST smoke PROPERTY LABELS ok)\n"
        "set_property(TEST missing PROPERTY LABELS bad)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_test_error = false;
    bool saw_smoke_label_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("set_property(TEST ...) test was not declared in selected directory scope")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("missing"))) {
            saw_missing_test_error = true;
        }
        if (ev->h.kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::smoke::LABELS")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("ok"))) {
            saw_smoke_label_set = true;
        }
    }

    ASSERT(saw_missing_test_error);
    ASSERT(saw_smoke_label_set);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_cache_requires_existing_entry) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHED_X old CACHE STRING \"doc\")\n"
        "set_property(CACHE CACHED_X PROPERTY VALUE new_ok)\n"
        "set_property(CACHE MISSING_X PROPERTY VALUE bad)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_cache_error = false;
    bool saw_cache_value_update = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(CACHE ...) cache entry does not exist"))) {
            saw_missing_cache_error = true;
        }
        if (ev->h.kind == EV_SET_CACHE_ENTRY &&
            ev->as.var_set.target_kind == EVENT_VAR_TARGET_CACHE &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("CACHED_X")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("new_ok"))) {
            saw_cache_value_update = true;
        }
    }

    ASSERT(saw_missing_cache_error);
    ASSERT(saw_cache_value_update);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_core_queries_and_directory_wrappers) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "define_property(GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS short_doc FULL_DOCS long_doc)\n"
        "define_property(DIRECTORY PROPERTY INHERITED_DIR INHERITED BRIEF_DOCS dir_short FULL_DOCS dir_long)\n"
        "set_property(GLOBAL PROPERTY INHERITED_DIR inherited_global)\n"
        "set_directory_properties(PROPERTIES BATCH_DIR_PROP dir_value)\n"
        "set(SCOPE_VAR scope_value)\n"
        "get_property(GP_SET GLOBAL PROPERTY BATCH_DOC SET)\n"
        "get_property(GP_DEF GLOBAL PROPERTY BATCH_DOC DEFINED)\n"
        "get_property(GP_BRIEF GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS)\n"
        "get_property(GP_FULL GLOBAL PROPERTY BATCH_DOC FULL_DOCS)\n"
        "get_property(GP_MISSING GLOBAL PROPERTY UNKNOWN_BATCH_PROP)\n"
        "get_property(GP_MISSING_DOC GLOBAL PROPERTY UNKNOWN_BATCH_PROP BRIEF_DOCS)\n"
        "get_property(GP_INH DIRECTORY PROPERTY INHERITED_DIR)\n"
        "get_directory_property(GP_DIR BATCH_DIR_PROP)\n"
        "get_directory_property(GP_DEFVAR DEFINITION SCOPE_VAR)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_SET")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_DEF")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_BRIEF")), nob_sv_from_cstr("short_doc")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_FULL")), nob_sv_from_cstr("long_doc")));
    ASSERT(eval_test_var_defined(ctx, nob_sv_from_cstr("GP_MISSING")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_MISSING")), nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_MISSING_DOC")), nob_sv_from_cstr("NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_INH")), nob_sv_from_cstr("inherited_global")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_DIR")), nob_sv_from_cstr("dir_value")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("GP_DEFVAR")), nob_sv_from_cstr("scope_value")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_target_source_and_test_wrappers) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(batch_target main.c)\n"
        "set_target_properties(batch_target PROPERTIES CUSTOM_TGT hello)\n"
        "set_source_files_properties(main.c PROPERTIES SRC_FLAG yes)\n"
        "add_test(NAME batch_test COMMAND cmd)\n"
        "set_tests_properties(batch_test DIRECTORY . PROPERTIES LABELS fast)\n"
        "get_target_property(TGT_OK batch_target CUSTOM_TGT)\n"
        "get_target_property(TGT_MISS batch_target UNKNOWN_TGT)\n"
        "get_source_file_property(SRC_OK main.c SRC_FLAG)\n"
        "get_source_file_property(SRC_DIR main.c DIRECTORY . SRC_FLAG)\n"
        "get_source_file_property(SRC_TGT_DIR main.c TARGET_DIRECTORY batch_target SRC_FLAG)\n"
        "get_source_file_property(SRC_MISS main.c UNKNOWN_SRC)\n"
        "get_property(TEST_OK TEST batch_test DIRECTORY . PROPERTY LABELS)\n"
        "get_test_property(batch_test LABELS DIRECTORY . TEST_DIR_OK)\n"
        "get_test_property(batch_test UNKNOWN_TEST TEST_MISS)\n"
        "get_test_property(missing_test LABELS TEST_UNDECLARED)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TGT_OK")), nob_sv_from_cstr("hello")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TGT_MISS")), nob_sv_from_cstr("TGT_MISS-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_OK")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_DIR")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_TGT_DIR")), nob_sv_from_cstr("yes")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_MISS")), nob_sv_from_cstr("NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_OK")), nob_sv_from_cstr("fast")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_DIR_OK")), nob_sv_from_cstr("fast")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_MISS")), nob_sv_from_cstr("NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_UNDECLARED")), nob_sv_from_cstr("NOTFOUND")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_directory_property_missing_materializes_empty_string) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "get_directory_property(DIR_MISSING UNKNOWN_DIR_PROP)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(eval_test_var_defined(ctx, nob_sv_from_cstr("DIR_MISSING")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIR_MISSING")), nob_sv_from_cstr("")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_source_directory_clause_and_get_cmake_property_lists_and_special_cases) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "function(BatchFunction)\n"
        "endfunction()\n"
        "macro(batch_macro)\n"
        "endmacro()\n"
        "set(NORMAL_A one)\n"
        "set(CACHED_A two CACHE STRING \"doc\")\n"
        "set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Toolkit)\n"
        "add_library(meta_shared SHARED main.c)\n"
        "install(TARGETS meta_shared LIBRARY DESTINATION lib NAMELINK_COMPONENT Development)\n"
        "install(FILES payload.txt DESTINATION share COMPONENT Runtime)\n"
        "set_source_files_properties(main.c DIRECTORY . PROPERTIES SCOPED_SRC local)\n"
        "get_property(SRC_SCOPED SOURCE main.c DIRECTORY . PROPERTY SCOPED_SRC)\n"
        "get_cmake_property(ALL_VARS VARIABLES)\n"
        "get_cmake_property(CACHE_VARS CACHE_VARIABLES)\n"
        "get_cmake_property(ALL_COMMANDS COMMANDS)\n"
        "get_cmake_property(ALL_COMPONENTS COMPONENTS)\n"
        "get_cmake_property(ALL_MACROS MACROS)\n"
        "get_cmake_property(MISSING_PROP DOES_NOT_EXIST)\n"
        "list(FIND ALL_VARS NORMAL_A IDX_VAR)\n"
        "list(FIND CACHE_VARS CACHED_A IDX_CACHE)\n"
        "list(FIND ALL_COMMANDS install IDX_COMMAND_INSTALL)\n"
        "list(FIND ALL_COMMANDS batchfunction IDX_COMMAND_FUNCTION)\n"
        "list(FIND ALL_COMMANDS batch_macro IDX_COMMAND_MACRO)\n"
        "list(FIND ALL_COMPONENTS Toolkit IDX_COMPONENT_DEFAULT)\n"
        "list(FIND ALL_COMPONENTS Development IDX_COMPONENT_DEV)\n"
        "list(FIND ALL_COMPONENTS Runtime IDX_COMPONENT_RUNTIME)\n"
        "list(FIND ALL_MACROS batch_macro IDX_MACRO)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_SCOPED")), nob_sv_from_cstr("local")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_VAR")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_CACHE")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMMAND_INSTALL")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMMAND_FUNCTION")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMMAND_MACRO")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMPONENT_DEFAULT")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMPONENT_DEV")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_COMPONENT_RUNTIME")), nob_sv_from_cstr("-1")));
    ASSERT(!nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("IDX_MACRO")), nob_sv_from_cstr("-1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("MISSING_PROP")), nob_sv_from_cstr("NOTFOUND")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_directory_qualified_queries_accept_known_binary_dirs) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("gp_bin_src"));
    ASSERT(nob_mkdir_if_not_exists("gp_bin_build"));
    ASSERT(nob_write_entire_file("gp_bin_src/sub_file.c", "int gp_bin_sub_file;\n", 21));
    {
        const char *sub_cmake =
            "set_property(DIRECTORY PROPERTY BIN_DIR_PROP from_child)\n"
            "set_source_files_properties(sub_file.c PROPERTIES BIN_SRC_PROP from_source)\n"
            "add_test(NAME gp_bin_test COMMAND cmd)\n"
            "set_tests_properties(gp_bin_test PROPERTIES LABELS from_test)\n";
        ASSERT(nob_write_entire_file("gp_bin_src/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));
    }

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_subdirectory(gp_bin_src gp_bin_build)\n"
        "get_property(DIR_FROM_BIN DIRECTORY gp_bin_build PROPERTY BIN_DIR_PROP)\n"
        "get_property(SRC_FROM_BIN SOURCE sub_file.c DIRECTORY gp_bin_build PROPERTY BIN_SRC_PROP)\n"
        "get_property(TEST_FROM_BIN TEST gp_bin_test DIRECTORY gp_bin_build PROPERTY LABELS)\n"
        "get_source_file_property(SRC_WRAP_FROM_BIN sub_file.c DIRECTORY gp_bin_build BIN_SRC_PROP)\n"
        "get_test_property(gp_bin_test LABELS DIRECTORY gp_bin_build TEST_WRAP_FROM_BIN)\n"
        "set_property(DIRECTORY gp_bin_build PROPERTY BIN_DIR_PROP updated_dir)\n"
        "set_property(SOURCE sub_file.c DIRECTORY gp_bin_build PROPERTY BIN_SRC_PROP updated_source)\n"
        "set_property(TEST gp_bin_test DIRECTORY gp_bin_build PROPERTY LABELS updated_test)\n"
        "get_property(DIR_UPDATED DIRECTORY gp_bin_build PROPERTY BIN_DIR_PROP)\n"
        "get_property(SRC_UPDATED SOURCE sub_file.c DIRECTORY gp_bin_build PROPERTY BIN_SRC_PROP)\n"
        "get_property(TEST_UPDATED TEST gp_bin_test DIRECTORY gp_bin_build PROPERTY LABELS)\n"
        "get_source_file_property(SRC_WRAP_UPDATED sub_file.c DIRECTORY gp_bin_build BIN_SRC_PROP)\n"
        "get_test_property(gp_bin_test LABELS DIRECTORY gp_bin_build TEST_WRAP_UPDATED)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIR_FROM_BIN")),
                     nob_sv_from_cstr("from_child")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_FROM_BIN")),
                     nob_sv_from_cstr("from_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_FROM_BIN")),
                     nob_sv_from_cstr("from_test")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_WRAP_FROM_BIN")),
                     nob_sv_from_cstr("from_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_WRAP_FROM_BIN")),
                     nob_sv_from_cstr("from_test")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIR_UPDATED")),
                     nob_sv_from_cstr("updated_dir")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_UPDATED")),
                     nob_sv_from_cstr("updated_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_UPDATED")),
                     nob_sv_from_cstr("updated_test")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SRC_WRAP_UPDATED")),
                     nob_sv_from_cstr("updated_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("TEST_WRAP_UPDATED")),
                     nob_sv_from_cstr("updated_test")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_install_signatures_emit_expected_rules_and_component_inventory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("install_assets"));
    ASSERT(nob_write_entire_file("install_payload.txt", "payload\n", strlen("payload\n")));
    ASSERT(nob_write_entire_file("install_helper.sh", "#!/bin/sh\n", strlen("#!/bin/sh\n")));
    ASSERT(nob_write_entire_file("install_hook.cmake", "set(HOOK 1)\n", strlen("set(HOOK 1)\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Toolkit)\n"
        "add_library(inst_meta INTERFACE)\n"
        "add_library(inst_imported SHARED IMPORTED)\n"
        "install(TARGETS inst_meta EXPORT InstExport DESTINATION lib)\n"
        "install(FILES install_payload.txt TYPE DOC COMPONENT Docs)\n"
        "install(PROGRAMS install_helper.sh TYPE BIN)\n"
        "install(DIRECTORY install_assets DESTINATION share/assets)\n"
        "install(SCRIPT install_hook.cmake COMPONENT Runtime)\n"
        "install(CODE \"set(INSTALL_CODE_MARKER 1)\")\n"
        "install(EXPORT InstExport DESTINATION share/cmake/Inst)\n"
        "install(EXPORT_ANDROID_MK InstExport DESTINATION share/cmake/android)\n"
        "install(IMPORTED_RUNTIME_ARTIFACTS inst_imported DESTINATION bin)\n"
        "install(RUNTIME_DEPENDENCY_SET inst_deps DESTINATION lib/deps)\n"
        "get_cmake_property(INSTALL_COMPONENTS COMPONENTS)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_target = false;
    bool saw_file_type_doc = false;
    bool saw_program_type_bin = false;
    bool saw_directory = false;
    bool saw_script = false;
    bool saw_code = false;
    bool saw_export = false;
    bool saw_export_android = false;
    bool saw_imported_runtime_artifacts = false;
    bool saw_runtime_dependency_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_INSTALL_ADD_RULE) continue;
        if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_TARGET &&
            nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("inst_meta")) &&
            nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("lib"))) {
            saw_target = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_FILE &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("install_payload.txt")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("share/doc"))) {
            saw_file_type_doc = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_PROGRAM &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("install_helper.sh")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("bin"))) {
            saw_program_type_bin = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_DIRECTORY &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("install_assets")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("share/assets"))) {
            saw_directory = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_FILE &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("SCRIPT::install_hook.cmake")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr(""))) {
            saw_script = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_FILE &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("CODE::set(INSTALL_CODE_MARKER 1)")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr(""))) {
            saw_code = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_FILE &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("EXPORT::InstExport")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("share/cmake/Inst"))) {
            saw_export = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_FILE &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("EXPORT_ANDROID_MK::InstExport")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("share/cmake/android"))) {
            saw_export_android = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_TARGET &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("IMPORTED_RUNTIME_ARTIFACTS::inst_imported")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("bin"))) {
            saw_imported_runtime_artifacts = true;
        } else if (ev->as.install_add_rule.rule_type == EV_INSTALL_RULE_TARGET &&
                   nob_sv_eq(ev->as.install_add_rule.item, nob_sv_from_cstr("RUNTIME_DEPENDENCY_SET::inst_deps")) &&
                   nob_sv_eq(ev->as.install_add_rule.destination, nob_sv_from_cstr("lib/deps"))) {
            saw_runtime_dependency_set = true;
        }
    }

    ASSERT(saw_target);
    ASSERT(saw_file_type_doc);
    ASSERT(saw_program_type_bin);
    ASSERT(saw_directory);
    ASSERT(saw_script);
    ASSERT(saw_code);
    ASSERT(saw_export);
    ASSERT(saw_export_android);
    ASSERT(saw_imported_runtime_artifacts);
    ASSERT(saw_runtime_dependency_set);

    String_View install_components = eval_test_var_get(ctx, nob_sv_from_cstr("INSTALL_COMPONENTS"));
    ASSERT(sv_contains_sv(install_components, nob_sv_from_cstr("Toolkit")));
    ASSERT(sv_contains_sv(install_components, nob_sv_from_cstr("Docs")));
    ASSERT(sv_contains_sv(install_components, nob_sv_from_cstr("Runtime")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_get_property_inherited_target_and_source_queries_follow_declared_target_directory) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY g1_prop_subdir)\n"
        "file(WRITE g1_prop_subdir/CMakeLists.txt [=[\n"
        "add_library(g1_sub_tgt STATIC local.c)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_TARGET_PROP sub_target_dir)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_SOURCE_PROP sub_source_dir)\n"
        "set_source_files_properties(local.c TARGET_DIRECTORY g1_sub_tgt PROPERTIES G1_LOCAL_SOURCE_PROP scoped_local)\n"
        "]=])\n"
        "define_property(TARGET PROPERTY G1_INHERITED_TARGET_PROP INHERITED)\n"
        "define_property(SOURCE PROPERTY G1_INHERITED_SOURCE_PROP INHERITED)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_TARGET_PROP root_target_dir)\n"
        "set_property(DIRECTORY PROPERTY G1_INHERITED_SOURCE_PROP root_source_dir)\n"
        "add_subdirectory(g1_prop_subdir)\n"
        "get_target_property(G1_TGT_PROP g1_sub_tgt G1_INHERITED_TARGET_PROP)\n"
        "get_property(G1_SRC_INH SOURCE local.c TARGET_DIRECTORY g1_sub_tgt PROPERTY G1_INHERITED_SOURCE_PROP)\n"
        "get_property(G1_SRC_LOCAL SOURCE local.c TARGET_DIRECTORY g1_sub_tgt PROPERTY G1_LOCAL_SOURCE_PROP)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("G1_TGT_PROP")),
                     nob_sv_from_cstr("sub_target_dir")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("G1_SRC_INH")),
                     nob_sv_from_cstr("sub_source_dir")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("G1_SRC_LOCAL")),
                     nob_sv_from_cstr("scoped_local")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_directory_scoped_property_queries_require_known_directories) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("gp_known_dir"));
    ASSERT(nob_write_entire_file("gp_known_dir/sub_file.c", "int sub_file;\n", 14));
    {
        const char *sub_cmake =
            "set_property(DIRECTORY PROPERTY SUB_BATCH_PROP from_subdir)\n"
            "set_property(SOURCE sub_file.c PROPERTY SUB_SRC_PROP from_source)\n"
            "add_test(NAME sub_batch_test COMMAND cmd)\n"
            "set_property(TEST sub_batch_test PROPERTY LABELS from_test)\n";
        ASSERT(nob_write_entire_file("gp_known_dir/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));
    }

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set_property(DIRECTORY PROPERTY ROOT_BATCH_PROP from_root)\n"
        "add_subdirectory(gp_known_dir)\n"
        "get_directory_property(ROOT_PROP ROOT_BATCH_PROP)\n"
        "get_directory_property(SUB_PROP DIRECTORY gp_known_dir SUB_BATCH_PROP)\n"
        "get_property(SUB_DIR_PROP DIRECTORY gp_known_dir PROPERTY SUB_BATCH_PROP)\n"
        "get_property(SUB_SRC SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)\n"
        "get_property(SUB_TEST TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)\n"
        "set_property(SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP updated_source)\n"
        "get_property(SUB_SRC_UPDATED SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)\n"
        "set_property(TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS updated_test)\n"
        "get_property(SUB_TEST_UPDATED TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)\n"
        "get_directory_property(BAD_DIR DIRECTORY gp_missing_dir ROOT_BATCH_PROP)\n"
        "get_property(BAD_SCOPE DIRECTORY gp_missing_dir PROPERTY ROOT_BATCH_PROP)\n"
        "get_property(BAD_SRC SOURCE sub_file.c DIRECTORY gp_missing_dir PROPERTY SUB_SRC_PROP)\n"
        "get_property(BAD_TEST TEST sub_batch_test DIRECTORY gp_missing_dir PROPERTY LABELS)\n"
        "set_property(SOURCE sub_file.c DIRECTORY gp_missing_dir PROPERTY SUB_SRC_PROP nope)\n"
        "set_property(TEST sub_batch_test DIRECTORY gp_missing_dir PROPERTY LABELS nope)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ROOT_PROP")),
                     nob_sv_from_cstr("from_root")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_PROP")),
                     nob_sv_from_cstr("from_subdir")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_DIR_PROP")),
                     nob_sv_from_cstr("from_subdir")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_SRC")),
                     nob_sv_from_cstr("from_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_TEST")),
                     nob_sv_from_cstr("from_test")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_SRC_UPDATED")),
                     nob_sv_from_cstr("updated_source")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SUB_TEST_UPDATED")),
                     nob_sv_from_cstr("updated_test")));

    bool saw_get_dir_error = false;
    bool saw_get_scope_error = false;
    bool saw_get_source_error = false;
    bool saw_get_test_error = false;
    bool saw_set_source_error = false;
    bool saw_set_test_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("get_directory_property(DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_dir_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_scope_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(SOURCE DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_source_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("get_property(TEST DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_get_test_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("set_property(SOURCE DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_set_source_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("set_property(TEST DIRECTORY ...) directory is not known"))) {
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("gp_missing_dir")));
            saw_set_test_error = true;
        }
    }

    ASSERT(saw_get_dir_error);
    ASSERT(saw_get_scope_error);
    ASSERT(saw_get_source_error);
    ASSERT(saw_get_test_error);
    ASSERT(saw_set_source_error);
    ASSERT(saw_set_test_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_directory_property_inheritance_uses_directory_graph_parent) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("graph_parent"));
    ASSERT(nob_mkdir_if_not_exists("graph_child"));
    ASSERT(nob_write_entire_file("graph_child/main.c", "int main(void){return 0;}\n", strlen("int main(void){return 0;}\n")));
    ASSERT(nob_write_entire_file("graph_child/CMakeLists.txt",
                                 "get_property(CHILD_MARK DIRECTORY PROPERTY GRAPH_MARK)\n"
                                 "add_executable(graph_child_probe main.c)\n"
                                 "target_compile_definitions(graph_child_probe PRIVATE CHILD_MARK=${CHILD_MARK})\n",
                                 strlen("get_property(CHILD_MARK DIRECTORY PROPERTY GRAPH_MARK)\n"
                                        "add_executable(graph_child_probe main.c)\n"
                                        "target_compile_definitions(graph_child_probe PRIVATE CHILD_MARK=${CHILD_MARK})\n")));
    ASSERT(nob_write_entire_file("graph_parent/CMakeLists.txt",
                                 "set_property(DIRECTORY PROPERTY GRAPH_MARK from_parent)\n"
                                 "add_subdirectory(../graph_child graph_child_build)\n",
                                 strlen("set_property(DIRECTORY PROPERTY GRAPH_MARK from_parent)\n"
                                        "add_subdirectory(../graph_child graph_child_build)\n")));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "define_property(DIRECTORY PROPERTY GRAPH_MARK INHERITED)\n"
        "set_property(DIRECTORY PROPERTY GRAPH_MARK from_root)\n"
        "add_subdirectory(graph_parent)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_child_mark_from_parent = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("graph_child_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("CHILD_MARK=from_parent"))) {
            saw_child_mark_from_parent = true;
        }
    }

    ASSERT(saw_child_mark_from_parent);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_option_mark_as_advanced_and_include_regular_expression_follow_policies) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(OPT_OLD normal_old)\n"
        "cmake_policy(SET CMP0077 OLD)\n"
        "option(OPT_OLD \"old doc\" ON)\n"
        "set(OPT_NEW normal_new)\n"
        "cmake_policy(SET CMP0077 NEW)\n"
        "option(OPT_NEW \"new doc\" OFF)\n"
        "cmake_policy(SET CMP0102 OLD)\n"
        "mark_as_advanced(FORCE OLD_MISSING)\n"
        "cmake_policy(SET CMP0102 NEW)\n"
        "mark_as_advanced(FORCE NEW_MISSING)\n"
        "mark_as_advanced(FORCE OPT_OLD)\n"
        "mark_as_advanced(CLEAR OPT_OLD)\n"
        "include_regular_expression(^keep$ ^warn$)\n"
        "get_property(OPT_ADV CACHE OPT_OLD PROPERTY ADVANCED)\n"
        "add_executable(option_probe main.c)\n"
        "target_compile_definitions(option_probe PRIVATE OPT_OLD=${OPT_OLD} OPT_NEW=${OPT_NEW} OPT_ADV=${OPT_ADV} RX=${CMAKE_INCLUDE_REGULAR_EXPRESSION} RC=${CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OPT_OLD")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OPT_NEW")), nob_sv_from_cstr("normal_new")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OPT_ADV")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION")), nob_sv_from_cstr("^keep$")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_INCLUDE_REGULAR_EXPRESSION_COMPLAIN")),
                     nob_sv_from_cstr("^warn$")));

    bool saw_opt_old_cache = false;
    bool saw_old_missing_cache = false;
    bool saw_new_missing_cache = false;
    bool saw_opt_new_cache = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_SET_CACHE_ENTRY || ev->as.var_set.target_kind != EVENT_VAR_TARGET_CACHE) continue;
        if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OPT_OLD")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("ON"))) {
            saw_opt_old_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OLD_MISSING")) &&
                   nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr(""))) {
            saw_old_missing_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("NEW_MISSING"))) {
            saw_new_missing_cache = true;
        } else if (nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("OPT_NEW"))) {
            saw_opt_new_cache = true;
        }
    }

    ASSERT(saw_opt_old_cache);
    ASSERT(saw_old_missing_cache);
    ASSERT(!saw_new_missing_cache);
    ASSERT(!saw_opt_new_cache);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_separate_arguments_covers_program_mode_and_legacy_form) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "separate_arguments(OUT_UNIX UNIX_COMMAND [=[alpha \"two words\" three\\ four]=])\n"
        "separate_arguments(OUT_WIN WINDOWS_COMMAND [=[alpha \"two words\" C:\\\\tmp\\\\x]=])\n"
        "set(OUT_NATIVE [=[alpha \"two words\"]=])\n"
        "separate_arguments(OUT_NATIVE)\n"
        "separate_arguments(OUT_EMPTY UNIX_COMMAND)\n"
#if defined(_WIN32)
        "separate_arguments(OUT_PROGRAM_RAW NATIVE_COMMAND PROGRAM [=[cmd /C echo]=])\n"
        "separate_arguments(OUT_PROGRAM_SPLIT NATIVE_COMMAND PROGRAM SEPARATE_ARGS [=[cmd /C echo]=])\n"
        "separate_arguments(OUT_PROGRAM_MISSING NATIVE_COMMAND PROGRAM [=[nobify_missing_tool /C echo]=])\n");
#else
        "separate_arguments(OUT_PROGRAM_RAW NATIVE_COMMAND PROGRAM [=[sh -c echo]=])\n"
        "separate_arguments(OUT_PROGRAM_SPLIT NATIVE_COMMAND PROGRAM SEPARATE_ARGS [=[sh -c echo]=])\n"
        "separate_arguments(OUT_PROGRAM_MISSING NATIVE_COMMAND PROGRAM [=[nobify_missing_tool -c echo]=])\n");
#endif
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_UNIX")),
                     nob_sv_from_cstr("alpha;two words;three four")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_WIN")),
                     nob_sv_from_cstr("alpha;two words;C:\\\\tmp\\\\x")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_NATIVE")),
                     nob_sv_from_cstr("alpha;\"two;words\"")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_EMPTY")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_MISSING")),
                     nob_sv_from_cstr("")));

#if defined(_WIN32)
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_RAW")),
                           "cmd.exe; /C echo"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_SPLIT")),
                           "cmd.exe;/C;echo"));
#else
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_RAW")),
                           "/sh; -c echo"));
    ASSERT(nob_sv_end_with(eval_test_var_get(ctx, nob_sv_from_cstr("OUT_PROGRAM_SPLIT")),
                           "/sh;-c;echo"));
#endif

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_separate_arguments_rejects_invalid_option_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "separate_arguments(BAD_MISSING_MODE PROGRAM alpha)\n"
        "separate_arguments(BAD_MULTI_MODE UNIX_COMMAND WINDOWS_COMMAND alpha)\n"
        "separate_arguments(BAD_SEPARATE_ONLY UNIX_COMMAND SEPARATE_ARGS alpha)\n"
        "separate_arguments(BAD_EXTRA UNIX_COMMAND alpha beta)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_missing_mode = false;
    bool saw_multi_mode = false;
    bool saw_separate_only = false;
    bool saw_unexpected = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("separate_arguments"))) continue;

        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() missing required mode"))) {
            saw_missing_mode = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() modes are mutually exclusive"))) {
            saw_multi_mode = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments(SEPARATE_ARGS) requires PROGRAM"))) {
            saw_separate_only = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("separate_arguments() given unexpected argument(s)"))) {
            saw_unexpected = true;
        }
    }

    ASSERT(saw_missing_mode);
    ASSERT(saw_multi_mode);
    ASSERT(saw_separate_only);
    ASSERT(saw_unexpected);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_remove_definitions_updates_directory_definition_and_option_state) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_definitions(-DKEEP=1 -DREMOVE_ME=1 -Wall -D1BAD)\n"
        "remove_definitions(-DREMOVE_ME=1 -Wall -D1BAD /DUNKNOWN=1)\n"
        "get_property(DIR_DEFS DIRECTORY PROPERTY COMPILE_DEFINITIONS)\n"
        "get_property(DIR_OPTS DIRECTORY PROPERTY COMPILE_OPTIONS)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_GLOBAL_COMPILE_DEFINITIONS")),
                     nob_sv_from_cstr("KEEP=1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_GLOBAL_COMPILE_OPTIONS")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIR_DEFS")), nob_sv_from_cstr("KEEP=1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("DIR_OPTS")), nob_sv_from_cstr("")));

    bool saw_remove_defs_event = false;
    bool saw_remove_opts_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EVENT_DIRECTORY_PROPERTY_MUTATE) continue;
        if (ev->h.origin.line != 2) continue;
        if (nob_sv_eq(ev->as.directory_property_mutate.property_name,
                      nob_sv_from_cstr("COMPILE_DEFINITIONS"))) {
            saw_remove_defs_event =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.directory_property_mutate.item_count == 1 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("KEEP=1"));
        } else if (nob_sv_eq(ev->as.directory_property_mutate.property_name,
                             nob_sv_from_cstr("COMPILE_OPTIONS"))) {
            saw_remove_opts_event =
                ev->as.directory_property_mutate.op == EVENT_PROPERTY_MUTATE_SET &&
                ev->as.directory_property_mutate.item_count == 0;
        }
    }
    ASSERT(saw_remove_defs_event);
    ASSERT(saw_remove_opts_event);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_load_cache_rejects_missing_path_empty_legacy_clauses_and_incomplete_read_with_prefix) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "load_cache()\n"
        "load_cache(cache_in EXCLUDE)\n"
        "load_cache(cache_in INCLUDE_INTERNALS)\n"
        "load_cache(cache_in cache_extra)\n"
        "load_cache(cache_in READ_WITH_PREFIX LC_ONLY)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    bool saw_missing_path = false;
    bool saw_empty_exclude = false;
    bool saw_empty_include_internals = false;
    bool saw_unsupported_argument = false;
    bool saw_incomplete_prefixed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("load_cache() requires a build directory path"))) {
            saw_missing_path = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("load_cache(EXCLUDE ...) requires at least one entry"))) {
            saw_empty_exclude = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("load_cache(INCLUDE_INTERNALS ...) requires at least one entry"))) {
            saw_empty_include_internals = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("load_cache() received an unsupported argument"))) {
            saw_unsupported_argument = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("load_cache(READ_WITH_PREFIX ...) requires a prefix and at least one entry"))) {
            saw_incomplete_prefixed = true;
        }
    }
    ASSERT(saw_missing_path);
    ASSERT(saw_empty_exclude);
    ASSERT(saw_empty_include_internals);
    ASSERT(saw_unsupported_argument);
    ASSERT(saw_incomplete_prefixed);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_host_introspection_and_site_name_cover_supported_queries) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("host_sysroot"));
    ASSERT(nob_mkdir_if_not_exists("host_sysroot/etc"));
    {
        const char *os_release =
            "ID=nobify\n"
            "NAME=\"Nobify Linux\"\n"
            "PRETTY_NAME=\"Nobify Linux 1.0\"\n"
            "VERSION_ID=1.0\n";
        ASSERT(nob_write_entire_file("host_sysroot/etc/os-release", os_release, strlen(os_release)));
    }

    char site_cmd[256] = {0};
    ASSERT(evaluator_prepare_site_name_command(site_cmd, sizeof(site_cmd)));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        nob_temp_sprintf(
            "site_name(SITE_FALLBACK)\n"
            "set(CMAKE_SYSROOT \"host_sysroot\")\n"
            "cmake_host_system_information(RESULT HOST_MULTI QUERY OS_NAME HOSTNAME IS_64BIT NUMBER_OF_PHYSICAL_CORES PROCESSOR_NAME PROCESSOR_DESCRIPTION FQDN)\n"
            "cmake_host_system_information(RESULT HOST_FLAGS QUERY HAS_FPU HAS_MMX HAS_MMX_PLUS HAS_SSE HAS_SSE2 HAS_SSE_FP HAS_SSE_MMX HAS_AMD_3DNOW HAS_AMD_3DNOW_PLUS HAS_IA64 HAS_SERIAL_NUMBER)\n"
            "cmake_host_system_information(RESULT HOST_SERIAL QUERY PROCESSOR_SERIAL_NUMBER)\n"
            "cmake_host_system_information(RESULT HOST_MSYS QUERY MSYSTEM_PREFIX)\n"
            "cmake_host_system_information(RESULT DISTRO QUERY DISTRIB_INFO)\n"
            "cmake_host_system_information(RESULT DISTRO_PRETTY QUERY DISTRIB_PRETTY_NAME)\n"
            "cmake_host_system_information(RESULT DISTRO_UNKNOWN QUERY DISTRIB_DOES_NOT_EXIST)\n"
            "set(HOSTNAME \"%s\")\n"
            "site_name(SITE_CMD)\n",
            site_cmd));
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    String_View system_name = eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    String_View host_multi = eval_test_var_get(ctx, nob_sv_from_cstr("HOST_MULTI"));
    String_View host_flags = eval_test_var_get(ctx, nob_sv_from_cstr("HOST_FLAGS"));
    String_View host_serial = eval_test_var_get(ctx, nob_sv_from_cstr("HOST_SERIAL"));
    String_View host_msys = eval_test_var_get(ctx, nob_sv_from_cstr("HOST_MSYS"));
    String_View distro = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO"));
    String_View distro_pretty = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_PRETTY"));
    String_View distro_unknown = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_UNKNOWN"));
    String_View distro_id = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_ID"));
    String_View distro_name = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_NAME"));
    String_View distro_pretty_var = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_PRETTY_NAME"));
    String_View distro_version = eval_test_var_get(ctx, nob_sv_from_cstr("DISTRO_VERSION_ID"));
    String_View site_fallback = eval_test_var_get(ctx, nob_sv_from_cstr("SITE_FALLBACK"));
    String_View site_cmd_out = eval_test_var_get(ctx, nob_sv_from_cstr("SITE_CMD"));

    String_View host_multi_items[7] = {0};
    size_t host_multi_count = 0;
    {
        size_t start = 0;
        while (start <= host_multi.count && host_multi_count < NOB_ARRAY_LEN(host_multi_items)) {
            size_t end = start;
            while (end < host_multi.count && host_multi.data[end] != ';') end++;
            host_multi_items[host_multi_count++] = nob_sv_from_parts(host_multi.data + start, end - start);
            if (end == host_multi.count) break;
            start = end + 1;
        }
    }

    String_View host_flag_items[11] = {0};
    size_t host_flag_count = 0;
    {
        size_t start = 0;
        while (start <= host_flags.count && host_flag_count < NOB_ARRAY_LEN(host_flag_items)) {
            size_t end = start;
            while (end < host_flags.count && host_flags.data[end] != ';') end++;
            host_flag_items[host_flag_count++] = nob_sv_from_parts(host_flags.data + start, end - start);
            if (end == host_flags.count) break;
            start = end + 1;
        }
    }

    ASSERT(system_name.count > 0);
    ASSERT(site_fallback.count > 0);
    ASSERT(host_multi_count == NOB_ARRAY_LEN(host_multi_items));
    ASSERT(nob_sv_eq(host_multi_items[0], system_name));
    ASSERT(host_multi_items[1].count > 0);
    ASSERT(evaluator_sv_is_bool01(host_multi_items[2]));
    ASSERT(evaluator_sv_is_decimal(host_multi_items[3]));
    ASSERT(host_multi_items[4].count > 0);
    ASSERT(host_multi_items[5].count > 0);
    ASSERT(host_multi_items[6].count > 0);
    ASSERT(host_flag_count == NOB_ARRAY_LEN(host_flag_items));
    for (size_t i = 0; i < host_flag_count; i++) {
        ASSERT(evaluator_sv_is_bool01(host_flag_items[i]));
    }
    (void)host_serial;
#if defined(_WIN32)
    (void)host_msys;
#else
    ASSERT(host_msys.count == 0);
#endif
    ASSERT(nob_sv_eq(distro_pretty, nob_sv_from_cstr("Nobify Linux 1.0")));
    ASSERT(nob_sv_eq(distro_unknown, nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(distro_id, nob_sv_from_cstr("nobify")));
    ASSERT(nob_sv_eq(distro_name, nob_sv_from_cstr("Nobify Linux")));
    ASSERT(nob_sv_eq(distro_pretty_var, nob_sv_from_cstr("Nobify Linux 1.0")));
    ASSERT(nob_sv_eq(distro_version, nob_sv_from_cstr("1.0")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_ID")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_NAME")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_PRETTY_NAME")));
    ASSERT(evaluator_sv_list_contains(distro, nob_sv_from_cstr("DISTRO_VERSION_ID")));
#if defined(_WIN32)
    ASSERT(site_cmd_out.count > 0);
#else
    ASSERT(nob_sv_eq(site_cmd_out, nob_sv_from_cstr("mock-site")));
#endif

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_host_system_information_rejects_incomplete_and_unknown_queries) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_host_system_information(RESULT_ONLY)\n"
        "cmake_host_system_information(RESULT HOST_UNKNOWN QUERY NOT_A_REAL_QUERY)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_missing_clauses = false;
    bool saw_unknown_query = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("cmake_host_system_information() requires RESULT and QUERY clauses"))) {
            saw_missing_clauses = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("cmake_host_system_information() query key is not implemented yet"))) {
            saw_unknown_query = true;
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("NOT_A_REAL_QUERY")));
        }
    }

    ASSERT(saw_missing_clauses);
    ASSERT(saw_unknown_query);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HOST_UNKNOWN")), nob_sv_from_cstr("")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_build_name_and_build_command_follow_policy_gates) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0036 OLD)\n"
        "build_name(BN_OLD)\n"
        "cmake_policy(SET CMP0036 NEW)\n"
        "build_name(BN_NEW)\n"
        "set(CMAKE_GENERATOR \"Unix Makefiles\")\n"
        "cmake_policy(SET CMP0061 OLD)\n"
        "build_command(BC_OLD CONFIGURATION Debug TARGET demo PARALLEL_LEVEL 3)\n"
        "cmake_policy(SET CMP0061 NEW)\n"
        "build_command(BC_NEW legacy_make legacy_file legacy_target)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    String_View host_name = eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    String_View compiler_id = eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"));
    String_View build_name = eval_test_var_get(ctx, nob_sv_from_cstr("BN_OLD"));
    ASSERT(build_name.count == host_name.count + 1 + compiler_id.count);
    ASSERT(memcmp(build_name.data, host_name.data, host_name.count) == 0);
    ASSERT(build_name.data[host_name.count] == '-');
    ASSERT(memcmp(build_name.data + host_name.count + 1, compiler_id.data, compiler_id.count) == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BC_OLD")),
                     nob_sv_from_cstr("cmake --build . --target demo --config Debug --parallel 3 -- -i")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BC_NEW")),
                     nob_sv_from_cstr("cmake --build . --target legacy_target")));

    bool saw_cmp0036_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("build_name() is disallowed by CMP0036"))) {
            saw_cmp0036_diag = true;
            break;
        }
    }
    ASSERT(saw_cmp0036_diag);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_host_build_commands_reject_invalid_shapes_and_warn_on_ignored_project_name) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "site_name()\n"
        "build_name()\n"
        "build_command()\n"
        "build_command(BC_KEY CONFIGURATION)\n"
        "build_command(BC_TOO_MANY a b c d)\n"
        "build_command(BC_BAD TARGET demo UNKNOWN value)\n"
        "build_command(BC_WARN PROJECT_NAME demo)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);
    ASSERT(report->warning_count == 1);

    bool saw_site_name_shape = false;
    bool saw_build_name_shape = false;
    bool saw_build_command_missing = false;
    bool saw_build_command_missing_value = false;
    bool saw_build_command_too_many = false;
    bool saw_build_command_bad_arg = false;
    bool saw_project_name_warning = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause,
                          nob_sv_from_cstr("site_name() requires exactly one output variable"))) {
                saw_site_name_shape = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("build_name() requires exactly one output variable"))) {
                saw_build_name_shape = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("build_command() requires an output variable"))) {
                saw_build_command_missing = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("build_command() keyword requires a value"))) {
                saw_build_command_missing_value = true;
                ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("CONFIGURATION")));
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("build_command() received too many positional arguments"))) {
                saw_build_command_too_many = true;
            } else if (nob_sv_eq(ev->as.diag.cause,
                                 nob_sv_from_cstr("build_command() received an unsupported argument"))) {
                saw_build_command_bad_arg = true;
                ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("UNKNOWN")));
            }
        } else if (ev->as.diag.severity == EV_DIAG_WARNING &&
                   nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("build_command(PROJECT_NAME ...) is parsed but ignored by evaluator v2"))) {
            saw_project_name_warning = true;
            ASSERT(nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("demo")));
        }
    }

    ASSERT(saw_site_name_shape);
    ASSERT(saw_build_name_shape);
    ASSERT(saw_build_command_missing);
    ASSERT(saw_build_command_missing_value);
    ASSERT(saw_build_command_too_many);
    ASSERT(saw_build_command_bad_arg);
    ASSERT(saw_project_name_warning);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BC_WARN")),
                     nob_sv_from_cstr("cmake --build .")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_new_signature_accepts_no_log_and_runs) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr("try_run_no_log_bin");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_NEW COMPILE_NEW\n"
        "  SOURCE_FROM_CONTENT probe_new.c [=[#include <stdio.h>\n"
        "int main(void){putchar(78);return 2;}\n"
        "]=]\n"
        "  NO_CACHE\n"
        "  NO_LOG\n"
        "  LOG_DESCRIPTION hidden_try_run_probe\n"
        "  RUN_OUTPUT_VARIABLE RUN_NEW_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_NEW_STDOUT)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_NEW")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_NEW")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_NEW_STDOUT")), nob_sv_from_cstr("N")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_NEW_ALL")), nob_sv_from_cstr("N")));
    ASSERT(!nob_file_exists("try_run_no_log_bin/CMakeFiles/CMakeConfigureLog.yaml"));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_caches_result_vars_by_default_and_respects_no_cache) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_CACHE COMPILE_CACHE\n"
        "  SOURCE_FROM_CONTENT probe_cache.c \"int main(void){return 5;}\" )\n"
        "unset(COMPILE_CACHE)\n"
        "unset(RUN_CACHE)\n"
        "set(COMPILE_CACHE_AFTER ${COMPILE_CACHE})\n"
        "set(RUN_CACHE_AFTER ${RUN_CACHE})\n"
        "try_run(RUN_NO_CACHE COMPILE_NO_CACHE\n"
        "  SOURCE_FROM_CONTENT probe_no_cache.c \"int main(void){return 6;}\"\n"
        "  NO_CACHE)\n"
        "unset(COMPILE_NO_CACHE)\n"
        "unset(RUN_NO_CACHE)\n"
        "set(COMPILE_NO_CACHE_AFTER ${COMPILE_NO_CACHE})\n"
        "set(RUN_NO_CACHE_AFTER ${RUN_NO_CACHE})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_CACHE_AFTER")),
                     nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_CACHE_AFTER")),
                     nob_sv_from_cstr("5")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_NO_CACHE_AFTER")),
                     nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_NO_CACHE_AFTER")),
                     nob_sv_from_cstr("")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_executes_native_artifacts_and_stages_crosscompile_placeholders) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);
    if (nob_file_exists("TryRunResults.cmake")) {
        ASSERT(nob_delete_file("TryRunResults.cmake"));
    }

    const char *ok_source =
        "#include <stdio.h>\n"
        "int main(void){putchar(65);fputc(66, stderr);return 0;}\n";
    ASSERT(nob_write_entire_file("probe_ok_try_run.c", ok_source, strlen(ok_source)));
    ASSERT(nob_mkdir_if_not_exists("tc_try_run_project"));
    ASSERT(nob_write_entire_file("tc_try_run_project/CMakeLists.txt",
                                 "cmake_minimum_required(VERSION 3.20)\n"
                                 "project(TryRunProject C)\n"
                                 "add_executable(tc_try_run_project_probe main.c)\n",
                                 strlen("cmake_minimum_required(VERSION 3.20)\n"
                                        "project(TryRunProject C)\n"
                                        "add_executable(tc_try_run_project_probe main.c)\n")));
    ASSERT(nob_write_entire_file("tc_try_run_project/main.c",
                                 "#include <stdio.h>\n"
                                 "int main(void){putchar(80);fputc(81, stderr);return 0;}\n",
                                 strlen("#include <stdio.h>\n"
                                        "int main(void){putchar(80);fputc(81, stderr);return 0;}\n")));

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_OK COMPILE_OK tc_try_run_ok\n"
        "  probe_ok_try_run.c\n"
        "  NO_CACHE\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_OK_LOG\n"
        "  RUN_OUTPUT_VARIABLE RUN_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_STDERR)\n"
        "try_run(RUN_LEGACY COMPILE_LEGACY tc_try_run_legacy\n"
        "  probe_ok_try_run.c\n"
        "  NO_CACHE\n"
        "  OUTPUT_VARIABLE RUN_LEGACY_ALL)\n"
        "try_run(RUN_BAD COMPILE_BAD tc_try_run_bad\n"
        "  SOURCE_FROM_CONTENT probe_bad.c \"int main(void){return 7;}\"\n"
        "  NO_CACHE)\n"
        "try_run(RUN_FAIL COMPILE_FAIL tc_try_run_fail\n"
        "  SOURCE_FROM_CONTENT probe_fail.c \"int main(void){ this is not valid C; }\"\n"
        "  NO_CACHE\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_FAIL_LOG)\n"
        "set(CMAKE_CROSSCOMPILING ON)\n"
        "unset(CMAKE_CROSSCOMPILING_EMULATOR)\n"
        "try_run(RUN_XC COMPILE_XC\n"
        "  SOURCE_FROM_CONTENT probe_xc.c \"int main(void){return 0;}\"\n"
        "  NO_CACHE\n"
        "  RUN_OUTPUT_VARIABLE RUN_XC_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_XC_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_XC_STDERR)\n"
        "set(CMAKE_CROSSCOMPILING OFF)\n"
        "try_run(RUN_PROJECT COMPILE_PROJECT PROJECT Demo\n"
        "  SOURCE_DIR tc_try_run_project\n"
        "  BINARY_DIR tc_try_run_project_build\n"
        "  TARGET tc_try_run_project_probe\n"
        "  COMPILE_OUTPUT_VARIABLE COMPILE_PROJECT_LOG\n"
        "  RUN_OUTPUT_VARIABLE RUN_PROJECT_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_PROJECT_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_PROJECT_STDERR)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_OK")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_OK")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_STDOUT")), nob_sv_from_cstr("A")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_STDERR")), nob_sv_from_cstr("B")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_ALL")), nob_sv_from_cstr("AB")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_LEGACY")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_LEGACY")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_LEGACY_ALL")), nob_sv_from_cstr("AB")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_BAD")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_BAD")), nob_sv_from_cstr("7")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_FAIL")), nob_sv_from_cstr("FALSE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_FAIL")), nob_sv_from_cstr("")));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_FAIL_LOG")).count > 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_XC")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_XC")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN")));
    ASSERT(!eval_test_var_defined(ctx, nob_sv_from_cstr("RUN_XC_ALL")));
    ASSERT(!eval_test_var_defined(ctx, nob_sv_from_cstr("RUN_XC_STDOUT")));
    ASSERT(!eval_test_var_defined(ctx, nob_sv_from_cstr("RUN_XC_STDERR")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDOUT")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDERR")),
                     nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_PROJECT")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_STDOUT")), nob_sv_from_cstr("P")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_STDERR")), nob_sv_from_cstr("Q")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PROJECT_ALL")), nob_sv_from_cstr("PQ")));

    String_View try_run_results = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "TryRunResults.cmake", &try_run_results));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDOUT")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("RUN_XC__TRYRUN_OUTPUT_STDERR")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("PLEASE_FILL_OUT-FAILED_TO_RUN")));
    ASSERT(sv_contains_sv(try_run_results, nob_sv_from_cstr("PLEASE_FILL_OUT-NOTFOUND")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_consumes_prefilled_crosscompile_cache_answers) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);
    if (nob_file_exists("TryRunResults.cmake")) {
        ASSERT(nob_delete_file("TryRunResults.cmake"));
    }

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(RUN_PRESET 23 CACHE STRING \"\")\n"
        "set(RUN_PRESET__TRYRUN_OUTPUT preset_all CACHE STRING \"\")\n"
        "set(RUN_PRESET__TRYRUN_OUTPUT_STDOUT preset_out CACHE STRING \"\")\n"
        "set(RUN_PRESET__TRYRUN_OUTPUT_STDERR preset_err CACHE STRING \"\")\n"
        "set(CMAKE_CROSSCOMPILING ON)\n"
        "unset(CMAKE_CROSSCOMPILING_EMULATOR)\n"
        "try_run(RUN_PRESET COMPILE_PRESET\n"
        "  SOURCE_FROM_CONTENT probe_preset.c \"int main(void){return 0;}\"\n"
        "  RUN_OUTPUT_VARIABLE RUN_PRESET_ALL\n"
        "  RUN_OUTPUT_STDOUT_VARIABLE RUN_PRESET_STDOUT\n"
        "  RUN_OUTPUT_STDERR_VARIABLE RUN_PRESET_STDERR)\n"
        "unset(COMPILE_PRESET)\n"
        "set(COMPILE_PRESET_AFTER ${COMPILE_PRESET})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PRESET")),
                     nob_sv_from_cstr("23")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PRESET_ALL")),
                     nob_sv_from_cstr("preset_all")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PRESET_STDOUT")),
                     nob_sv_from_cstr("preset_out")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_PRESET_STDERR")),
                     nob_sv_from_cstr("preset_err")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_PRESET_AFTER")),
                     nob_sv_from_cstr("TRUE")));
    ASSERT(!nob_file_exists("TryRunResults.cmake"));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_uses_crosscompiling_emulator_when_available) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Nob_String_Builder script_sb = {0};
#if defined(_WIN32)
    nob_sb_append_cstr(&script_sb,
                       "set(CMAKE_CROSSCOMPILING ON)\n"
                       "set(CMAKE_CROSSCOMPILING_EMULATOR cmd /C)\n");
#else
    const char *cwd = nob_get_current_dir_temp();
    ASSERT(cwd != NULL);
    char emulator_path[4096] = {0};
    int emulator_n = snprintf(emulator_path, sizeof(emulator_path), "%s/%s", cwd, "try_run_emulator.sh");
    ASSERT(emulator_n > 0 && (size_t)emulator_n < sizeof(emulator_path));
    ASSERT(nob_write_entire_file("try_run_emulator.sh", "exec \"$@\"\n", strlen("exec \"$@\"\n")));
    nob_sb_appendf(&script_sb,
                   "set(CMAKE_CROSSCOMPILING ON)\n"
                   "set(CMAKE_CROSSCOMPILING_EMULATOR /bin/sh [=[%s]=])\n",
                   emulator_path);
#endif
    nob_sb_append_cstr(&script_sb,
                       "try_run(RUN_EMU COMPILE_EMU\n"
                       "  SOURCE_FROM_CONTENT probe_emu.c [=[#include <stdio.h>\n"
                       "int main(void){putchar(69);fputc(70, stderr);return 4;}\n"
                       "]=]\n"
                       "  NO_CACHE\n"
                       "  RUN_OUTPUT_VARIABLE RUN_EMU_ALL\n"
                       "  RUN_OUTPUT_STDOUT_VARIABLE RUN_EMU_STDOUT\n"
                       "  RUN_OUTPUT_STDERR_VARIABLE RUN_EMU_STDERR)\n");
    nob_sb_append_null(&script_sb);

    Ast_Root root = parse_cmake(temp_arena, script_sb.items);
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_EMU")), nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_EMU")), nob_sv_from_cstr("4")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_EMU_STDOUT")), nob_sv_from_cstr("E")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_EMU_STDERR")), nob_sv_from_cstr("F")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_EMU_ALL")), nob_sv_from_cstr("EF")));
    ASSERT(!eval_test_var_defined(ctx, nob_sv_from_cstr("RUN_EMU__TRYRUN_OUTPUT")));

    nob_sb_free(script_sb);
    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_sets_failed_to_run_when_process_cannot_start) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run(RUN_FAILED_START COMPILE_FAILED_START\n"
        "  SOURCE_FROM_CONTENT probe_failed_start.c \"int main(void){return 0;}\"\n"
        "  NO_CACHE\n"
        "  WORKING_DIRECTORY missing_run_dir)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_FAILED_START")),
                     nob_sv_from_cstr("TRUE")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RUN_FAILED_START")),
                     nob_sv_from_cstr("FAILED_TO_RUN")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_try_run_rejects_incomplete_argument_shapes) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "try_run()\n"
        "try_run(RUN_BAD COMPILE_BAD\n"
        "  SOURCE_FROM_CONTENT probe_bad1.c \"int main(void){return 0;}\"\n"
        "  OUTPUT_VARIABLE BAD_LEGACY_OUT)\n"
        "try_run(RUN_BAD2 COMPILE_BAD2\n"
        "  SOURCE_FROM_CONTENT probe_bad2.c \"int main(void){return 0;}\"\n"
        "  RUN_OUTPUT_VARIABLE)\n"
        "try_run(RUN_BAD3 COMPILE_BAD3 PROJECT Demo\n"
        "  BINARY_DIR tc_try_run_missing_source)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_missing_inputs = false;
    bool saw_legacy_output_restriction = false;
    bool saw_run_output_missing_value = false;
    bool saw_project_source_dir_required = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("try_run() requires run result, compile result and try_compile-style inputs"))) {
            saw_missing_inputs = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("try_run(OUTPUT_VARIABLE) is supported only by the legacy signature with an explicit binary directory"))) {
            saw_legacy_output_restriction = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("try_run(RUN_OUTPUT_VARIABLE) requires an output variable"))) {
            saw_run_output_missing_value = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("try_compile(PROJECT ...) requires SOURCE_DIR"))) {
            saw_project_source_dir_required = true;
        }
    }

    ASSERT(saw_missing_inputs);
    ASSERT(saw_legacy_output_restriction);
    ASSERT(saw_run_output_missing_value);
    ASSERT(saw_project_source_dir_required);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_exec_program_respects_cmp0153_and_legacy_wrapper_surface) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("ep_exec_dir"));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "cmake_policy(SET CMP0153 NEW)\n"
        "exec_program(cmd . ARGS [=[/C echo blocked]=] OUTPUT_VARIABLE EP_BLOCKED)\n"
        "cmake_policy(SET CMP0153 OLD)\n"
        "exec_program(cmd . ARGS [=[/C echo legacy]=] OUTPUT_VARIABLE EP_OUT RETURN_VALUE EP_RES)\n"
        "exec_program(cmd ep_exec_dir ARGS [=[/C cd]=] OUTPUT_VARIABLE EP_CWD RETURN_VALUE EP_CWD_RES)\n"
        "exec_program(cmd . ARGS [=[/C echo hello world]=] OUTPUT_VARIABLE EP_TOKEN)\n"
        "exec_program(cmd . OUTPUT_VARIABLE)\n"
        "exec_program(cmd . RETURN_VALUE)\n"
        "exec_program(cmd . BOGUS)\n";
#else
    const char *script =
        "cmake_policy(SET CMP0153 NEW)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf 'blocked'\"]=] OUTPUT_VARIABLE EP_BLOCKED)\n"
        "cmake_policy(SET CMP0153 OLD)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf 'legacy'\"]=] OUTPUT_VARIABLE EP_OUT RETURN_VALUE EP_RES)\n"
        "exec_program(/bin/sh ep_exec_dir ARGS [=[-c \"pwd\"]=] OUTPUT_VARIABLE EP_CWD RETURN_VALUE EP_CWD_RES)\n"
        "exec_program(/bin/sh . ARGS [=[-c \"printf '%s' \\\"$1\\\"\" sh \"hello world\"]=] OUTPUT_VARIABLE EP_TOKEN)\n"
        "exec_program(/bin/sh . OUTPUT_VARIABLE)\n"
        "exec_program(/bin/sh . RETURN_VALUE)\n"
        "exec_program(/bin/sh . BOGUS)\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("EP_OUT")), nob_sv_from_cstr("legacy")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("EP_RES")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("EP_CWD_RES")), nob_sv_from_cstr("0")));
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("EP_CWD")), nob_sv_from_cstr("ep_exec_dir")));
#if defined(_WIN32)
    ASSERT(sv_contains_sv(eval_test_var_get(ctx, nob_sv_from_cstr("EP_TOKEN")), nob_sv_from_cstr("hello")));
#else
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("EP_TOKEN")), nob_sv_from_cstr("hello world")));
#endif

    bool saw_cmp0153_diag = false;
    bool saw_output_diag = false;
    bool saw_return_diag = false;
    bool saw_bogus_diag = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("exec_program() is disallowed by CMP0153"))) {
            saw_cmp0153_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program(OUTPUT_VARIABLE) requires exactly one output variable"))) {
            saw_output_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program(RETURN_VALUE) requires exactly one output variable"))) {
            saw_return_diag = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("exec_program() received an unsupported argument"))) {
            saw_bogus_diag = true;
        }
    }
    ASSERT(saw_cmp0153_diag);
    ASSERT(saw_output_diag);
    ASSERT(saw_return_diag);
    ASSERT(saw_bogus_diag);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch6_metadata_commands_cover_documented_subset) {
    Arena *temp_arena = arena_create(3 * 1024 * 1024);
    Arena *event_arena = arena_create(3 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    ASSERT(nob_mkdir_if_not_exists("cache_in"));
    ASSERT(nob_mkdir_if_not_exists("asd_src"));
    ASSERT(nob_write_entire_file("cache_in/CMakeCache.txt",
                                 "FOO:STRING=alpha\n"
                                 "BAR:BOOL=ON\n"
                                 "KEEP:STRING=keep-me\n"
                                 "HIDE:INTERNAL=secret\n"
                                 "broken-line\n",
                                 strlen("FOO:STRING=alpha\nBAR:BOOL=ON\nKEEP:STRING=keep-me\nHIDE:INTERNAL=secret\nbroken-line\n")));
    ASSERT(nob_write_entire_file("asd_src/b.cpp", "int b = 0;\n", strlen("int b = 0;\n")));
    ASSERT(nob_write_entire_file("asd_src/a.c", "int a = 0;\n", strlen("int a = 0;\n")));
    ASSERT(nob_write_entire_file("asd_src/skip.txt", "x\n", 2));

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_TESTDRIVER_BEFORE_TESTMAIN \"/*before*/\")\n"
        "set(CMAKE_TESTDRIVER_AFTER_TESTMAIN \"/*after*/\")\n"
        "add_library(meta_lib INTERFACE)\n"
        "install(TARGETS meta_lib EXPORT DemoExport DESTINATION lib)\n"
        "export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo::)\n"
        "export(EXPORT DemoExport FILE meta-export.cmake NAMESPACE Demo::)\n"
        "load_cache(cache_in READ_WITH_PREFIX LC_ FOO BAR)\n"
        "load_cache(cache_in EXCLUDE BAR INCLUDE_INTERNALS HIDE)\n"
        "aux_source_directory(asd_src ASD_OUT)\n"
        "create_test_sourcelist(TEST_SRCS generated_driver.c alpha_test.c beta_test.c EXTRA_INCLUDE extra.h FUNCTION setup_hook)\n"
        "include_external_msproject(ext_proj external.vcxproj TYPE type-guid GUID proj-guid PLATFORM Win32 meta_lib)\n"
        "cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2 CACHE 2.0 CMAKEFILES 1 TOOLCHAINS 1)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_MODE")), nob_sv_from_cstr("EXPORT")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_EXPORT_LAST_NAMESPACE")), nob_sv_from_cstr("Demo::")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("LC_FOO")), nob_sv_from_cstr("alpha")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("LC_BAR")), nob_sv_from_cstr("ON")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("KEEP")), nob_sv_from_cstr("keep-me")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BAR")), nob_sv_from_cstr("")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("HIDE")), nob_sv_from_cstr("secret")));

    String_View asd_out = eval_test_var_get(ctx, nob_sv_from_cstr("ASD_OUT"));
    ASSERT(sv_contains_sv(asd_out, nob_sv_from_cstr("a.c")));
    ASSERT(sv_contains_sv(asd_out, nob_sv_from_cstr("b.cpp")));
    ASSERT(!sv_contains_sv(asd_out, nob_sv_from_cstr("skip.txt")));
    const char *a_pos = strstr(nob_temp_sv_to_cstr(asd_out), "a.c");
    const char *b_pos = strstr(nob_temp_sv_to_cstr(asd_out), "b.cpp");
    ASSERT(a_pos != NULL && b_pos != NULL && a_pos < b_pos);

    String_View test_srcs = eval_test_var_get(ctx, nob_sv_from_cstr("TEST_SRCS"));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("alpha_test.c")));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("beta_test.c")));
    ASSERT(sv_contains_sv(test_srcs, nob_sv_from_cstr("generated_driver.c")));
    String_View driver_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "generated_driver.c", &driver_text));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("extern int alpha_test(int, char**);")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("extern int beta_test(int, char**);")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("#include \"extra.h\"")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("setup_hook();")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("/*before*/")));
    ASSERT(sv_contains_sv(driver_text, nob_sv_from_cstr("/*after*/")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_MSPROJECT::ext_proj::LOCATION")),
                     nob_sv_from_cstr("external.vcxproj")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_MSPROJECT::ext_proj::DEPENDENCIES")),
                     nob_sv_from_cstr("meta_lib")));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::API_VERSION")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_FILE_API")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CODEMODEL")),
                     nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CACHE")),
                     nob_sv_from_cstr("2.0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::CMAKEFILES")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_CMAKE_FILE_API_QUERY::TOOLCHAINS")),
                     nob_sv_from_cstr("1")));

    String_View export_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "meta-export.cmake", &export_text));
    ASSERT(sv_contains_sv(export_text, nob_sv_from_cstr("set(NOBIFY_EXPORT_TARGETS \"meta_lib\")")));

    String_View file_api_query = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/query/client-nobify/query.json", &file_api_query));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"codemodel\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"cache\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"cmakeFiles\"")));
    ASSERT(sv_contains_sv(file_api_query, nob_sv_from_cstr("\"kind\": \"toolchains\"")));

    String_View file_api_index = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/index-nobify-v1.json", &file_api_index));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("codemodel-v2.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("cache-v2.0.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("cmakeFiles-v1.json")));
    ASSERT(sv_contains_sv(file_api_index, nob_sv_from_cstr("toolchains-v1.json")));

    String_View codemodel_reply = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/codemodel-v2.json", &codemodel_reply));
    ASSERT(sv_contains_sv(codemodel_reply, nob_sv_from_cstr("\"kind\": \"codemodel\"")));

    String_View cache_reply = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, ".cmake/api/v1/reply/cache-v2.0.json", &cache_reply));
    ASSERT(sv_contains_sv(cache_reply, nob_sv_from_cstr("\"kind\": \"cache\"")));

    bool saw_malformed_cache_warning = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("load_cache() skipped a malformed cache entry"))) {
            saw_malformed_cache_warning = true;
            break;
        }
    }
    ASSERT(saw_malformed_cache_warning);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch6_metadata_commands_reject_unsupported_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "export(PACKAGE Demo)\n"
        "cmake_file_api(REPLY)\n"
        "cmake_file_api(QUERY API_VERSION 2 CODEMODEL 2)\n"
        "cmake_file_api(QUERY API_VERSION 1 UNKNOWN_KIND 1)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_batch6_metadata_commands_reject_incomplete_option_values) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include_external_msproject(ext_proj external.vcxproj TYPE)\n"
        "include_external_msproject(ext_proj2 external.vcxproj PLATFORM)\n"
        "cmake_file_api(QUERY API_VERSION 1 CODEMODEL)\n"
        "export(EXPORT DemoExport NAMESPACE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    bool saw_msproject_type_error = false;
    bool saw_msproject_platform_error = false;
    bool saw_file_api_version_error = false;
    bool saw_export_namespace_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("include_external_msproject(TYPE ...) requires a GUID value"))) {
            saw_msproject_type_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("include_external_msproject(PLATFORM ...) requires a value"))) {
            saw_msproject_platform_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("cmake_file_api() requires at least one version for each object kind"))) {
            saw_file_api_version_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                             nob_sv_from_cstr("export(EXPORT ... NAMESPACE ...) requires a value"))) {
            saw_export_namespace_error = true;
        }
    }

    ASSERT(saw_msproject_type_error);
    ASSERT(saw_msproject_platform_error);
    ASSERT(saw_file_api_version_error);
    ASSERT(saw_export_namespace_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}


void run_evaluator_v2_batch3(int *passed, int *failed) {
    test_evaluator_list_transform_genex_strip_and_output_variable(passed, failed);
    test_evaluator_list_transform_output_variable_requires_single_output_var(passed, failed);
    test_evaluator_list_sort_and_transform_selector_surface_matches_documented_combinations(passed, failed);
    test_evaluator_math_rejects_empty_and_incomplete_invocations(passed, failed);
    test_evaluator_set_target_properties_rejects_alias_target(passed, failed);
    test_evaluator_add_executable_imported_and_alias_signatures(passed, failed);
    test_evaluator_add_library_imported_alias_and_default_type(passed, failed);
    test_evaluator_set_property_target_rejects_alias_and_unknown_target(passed, failed);
    test_evaluator_define_property_initializes_target_properties_from_variable(passed, failed);
    test_evaluator_set_property_source_test_directory_clauses_parse_and_apply(passed, failed);
    test_evaluator_set_property_allows_zero_objects_and_validates_test_lookup(passed, failed);
    test_evaluator_set_property_cache_requires_existing_entry(passed, failed);
    test_evaluator_get_property_core_queries_and_directory_wrappers(passed, failed);
    test_evaluator_get_property_target_source_and_test_wrappers(passed, failed);
    test_evaluator_get_directory_property_missing_materializes_empty_string(passed, failed);
    test_evaluator_get_property_source_directory_clause_and_get_cmake_property_lists_and_special_cases(passed, failed);
    test_evaluator_get_property_directory_qualified_queries_accept_known_binary_dirs(passed, failed);
    test_evaluator_install_signatures_emit_expected_rules_and_component_inventory(passed, failed);
    test_evaluator_get_property_inherited_target_and_source_queries_follow_declared_target_directory(passed, failed);
    test_evaluator_directory_scoped_property_queries_require_known_directories(passed, failed);
    test_evaluator_directory_property_inheritance_uses_directory_graph_parent(passed, failed);
    test_evaluator_option_mark_as_advanced_and_include_regular_expression_follow_policies(passed, failed);
    test_evaluator_separate_arguments_covers_program_mode_and_legacy_form(passed, failed);
    test_evaluator_separate_arguments_rejects_invalid_option_shapes(passed, failed);
    test_evaluator_remove_definitions_updates_directory_definition_and_option_state(passed, failed);
    test_evaluator_load_cache_rejects_missing_path_empty_legacy_clauses_and_incomplete_read_with_prefix(passed, failed);
    test_evaluator_host_introspection_and_site_name_cover_supported_queries(passed, failed);
    test_evaluator_host_system_information_rejects_incomplete_and_unknown_queries(passed, failed);
    test_evaluator_build_name_and_build_command_follow_policy_gates(passed, failed);
    test_evaluator_host_build_commands_reject_invalid_shapes_and_warn_on_ignored_project_name(passed, failed);
    test_evaluator_try_run_new_signature_accepts_no_log_and_runs(passed, failed);
    test_evaluator_try_run_caches_result_vars_by_default_and_respects_no_cache(passed, failed);
    test_evaluator_try_run_executes_native_artifacts_and_stages_crosscompile_placeholders(passed, failed);
    test_evaluator_try_run_consumes_prefilled_crosscompile_cache_answers(passed, failed);
    test_evaluator_try_run_uses_crosscompiling_emulator_when_available(passed, failed);
    test_evaluator_try_run_sets_failed_to_run_when_process_cannot_start(passed, failed);
    test_evaluator_try_run_rejects_incomplete_argument_shapes(passed, failed);
    test_evaluator_exec_program_respects_cmp0153_and_legacy_wrapper_surface(passed, failed);
    test_evaluator_batch6_metadata_commands_cover_documented_subset(passed, failed);
    test_evaluator_batch6_metadata_commands_reject_unsupported_forms(passed, failed);
    test_evaluator_batch6_metadata_commands_reject_incomplete_option_values(passed, failed);
}
