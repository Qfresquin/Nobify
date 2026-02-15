#include "nob.h"
#include "build_model.h"
#include "math_parser.h"
#include "genex_evaluator.h"
#include "sys_utils.h"
#include "toolchain_driver.h"
#include "cmake_path_utils.h"
#include "cmake_regex_utils.h"
#include "cmake_glob_utils.h"
#include "find_search_utils.h"
#include "cmake_meta_io.h"
#include "ctest_coverage_utils.h"

#include <stdio.h>
#include <string.h>

#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, " FAILED: %s (line %d): %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    (*passed)++; \
} while(0)

TEST(math_parser_precedence_unary_and_errors) {
    long long value = 0;

    ASSERT(math_eval_i64(sv_from_cstr("1 + 2 * 3"), &value) == MATH_EVAL_OK);
    ASSERT(value == 7);

    ASSERT(math_eval_i64(sv_from_cstr("-(2 + 3)"), &value) == MATH_EVAL_OK);
    ASSERT(value == -5);

    ASSERT(math_eval_i64(sv_from_cstr("7 / 0"), &value) == MATH_EVAL_DIV_ZERO);
    ASSERT(math_eval_i64(sv_from_cstr("1 +"), &value) == MATH_EVAL_INVALID_EXPR);

    TEST_PASS();
}

TEST(genex_evaluator_basic_paths) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Build_Model *model = build_model_create(arena);
    ASSERT(model != NULL);
    build_model_set_default_config(model, sv_from_cstr("Debug"));
    model->is_windows = false;
    model->is_apple = false;
    model->is_linux = true;
    model->is_unix = true;

    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    ASSERT(app != NULL);
    build_target_set_property(app, arena, sv_from_cstr("OUTPUT_NAME"), sv_from_cstr("myapp"));

    Genex_Eval_Context gx = {0};
    gx.arena = arena;
    gx.model = model;
    gx.default_config = model->default_config;

    ASSERT(nob_sv_eq(genex_evaluate(&gx, sv_from_cstr("CONFIG")), sv_from_cstr("Debug")));
    ASSERT(nob_sv_eq(genex_evaluate(&gx, sv_from_cstr("IF:1,YES,NO")), sv_from_cstr("YES")));
    ASSERT(nob_sv_eq(genex_evaluate(&gx, sv_from_cstr("TARGET_PROPERTY:app,OUTPUT_NAME")), sv_from_cstr("myapp")));
    ASSERT(nob_sv_eq(genex_evaluate(&gx, sv_from_cstr("PLATFORM_ID")), sv_from_cstr("Linux")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(sys_utils_directory_copy_delete_and_file_download) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    String_View root = sv_from_cstr("temp_phase2_sys");
    (void)sys_delete_path_recursive(arena, root);
    ASSERT(nob_mkdir_if_not_exists(nob_temp_sv_to_cstr(root)));

    String_View nested_file = sv_from_cstr("temp_phase2_sys/a/b/c.txt");
    ASSERT(sys_ensure_parent_dirs(arena, nested_file));
    ASSERT(sys_write_file(nested_file, sv_from_cstr("hello")));
    String_View read_back = sys_read_file(arena, nested_file);
    ASSERT(read_back.data != NULL);
    ASSERT(nob_sv_eq(read_back, sv_from_cstr("hello")));

    ASSERT(nob_mkdir_if_not_exists("temp_phase2_sys/srcdir"));
    ASSERT(nob_mkdir_if_not_exists("temp_phase2_sys/srcdir/sub"));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_sys/srcdir/sub/one.txt"), sv_from_cstr("one")));

    ASSERT(sys_copy_entry_to_destination(arena,
                                         sv_from_cstr("temp_phase2_sys/srcdir"),
                                         sv_from_cstr("temp_phase2_sys/dst")));
    ASSERT(nob_file_exists("temp_phase2_sys/dst/srcdir/sub/one.txt"));

    String_View log_msg = sv_from_cstr("");
    ASSERT(sys_download_to_path(arena,
                                sv_from_cstr("file://temp_phase2_sys/srcdir/sub/one.txt"),
                                sv_from_cstr("temp_phase2_sys/download/out.txt"),
                                &log_msg));
    ASSERT(nob_file_exists("temp_phase2_sys/download/out.txt"));

    ASSERT(sys_delete_path_recursive(arena, sv_from_cstr("temp_phase2_sys/dst")));
    ASSERT(!nob_file_exists("temp_phase2_sys/dst/srcdir/sub/one.txt"));

    (void)sys_delete_path_recursive(arena, root);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(toolchain_driver_compiler_probe_and_try_run) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    Toolchain_Driver drv = {0};
    drv.arena = arena;
    drv.build_dir = sv_from_cstr(".");
    drv.timeout_ms = 5000;
    drv.debug = false;

    String_View compiler = toolchain_default_c_compiler();
    bool available = toolchain_compiler_available(&drv, compiler);

    bool used_probe = false;
    bool compiles = toolchain_probe_check_c_source_compiles(&drv,
                                                            sv_from_cstr("int main(void){return 0;}"),
                                                            &used_probe);

    if (!available) {
        ASSERT(used_probe == false);
        ASSERT(compiles == false);
        arena_destroy(arena);
        TEST_PASS();
        return;
    }

    ASSERT(used_probe == true);
    ASSERT(compiles == true);

    String_View src_path = sv_from_cstr("temp_phase2_toolchain_ok.c");
#if defined(_WIN32)
    String_View out_path = sv_from_cstr("temp_phase2_toolchain_ok.exe");
#else
    String_View out_path = sv_from_cstr("temp_phase2_toolchain_ok");
#endif

    ASSERT(sys_write_file(src_path,
                          sv_from_cstr("#include <stdio.h>\nint main(void){ puts(\"phase2_toolchain_ok\"); return 0; }\n")));

    String_List defs = {0};
    String_List opts = {0};
    String_List libs = {0};
    string_list_init(&defs);
    string_list_init(&opts);
    string_list_init(&libs);

    Toolchain_Compile_Request req = {0};
    req.compiler = compiler;
    req.src_path = src_path;
    req.out_path = out_path;
    req.compile_definitions = &defs;
    req.link_options = &opts;
    req.link_libraries = &libs;

    Toolchain_Compile_Result compile_out = {0};
    int run_rc = 1;
    String_View run_output = sv_from_cstr("");
    ASSERT(toolchain_try_run(&drv, &req, NULL, &compile_out, &run_rc, &run_output));
    ASSERT(compile_out.ok == true);
    ASSERT(run_rc == 0);
    ASSERT(strstr(nob_temp_sv_to_cstr(run_output), "phase2_toolchain_ok") != NULL);

    bool used_bad_probe = false;
    bool bad_compiles = toolchain_probe_check_c_source_compiles(&drv,
                                                                sv_from_cstr("int main(void){ this is broken; }"),
                                                                &used_bad_probe);
    ASSERT(used_bad_probe == true);
    ASSERT(bad_compiles == false);

    if (nob_file_exists(nob_temp_sv_to_cstr(src_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(src_path));
    if (nob_file_exists(nob_temp_sv_to_cstr(out_path))) (void)nob_delete_file(nob_temp_sv_to_cstr(out_path));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_path_utils_basic) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    String_View normalized = cmk_path_normalize(arena, sv_from_cstr("a/./b/../c"));
    ASSERT(nob_sv_eq(normalized, sv_from_cstr("a/c")));

    String_View rel = cmk_path_relativize(arena, sv_from_cstr("root/sub/file.txt"), sv_from_cstr("root"));
    ASSERT(nob_sv_eq(rel, sv_from_cstr("sub/file.txt")));

    bool supported = false;
    String_View comp = cmk_path_get_component(arena, sv_from_cstr("dir/name.ext"), sv_from_cstr("FILENAME"), &supported);
    ASSERT(supported == true);
    ASSERT(nob_sv_eq(comp, sv_from_cstr("name.ext")));

    comp = cmk_path_get_component(arena, sv_from_cstr("dir/name.ext"), sv_from_cstr("STEM"), &supported);
    ASSERT(supported == true);
    ASSERT(nob_sv_eq(comp, sv_from_cstr("name")));

    comp = cmk_path_get_component(arena, sv_from_cstr("dir/name.ext"), sv_from_cstr("EXTENSION"), &supported);
    ASSERT(supported == true);
    ASSERT(nob_sv_eq(comp, sv_from_cstr(".ext")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_regex_glob_find_utils_basic) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    String_View replaced = cmk_regex_replace_backrefs(arena,
                                                      sv_from_cstr("([0-9]+\\.[0-9]+\\.[0-9]+).+"),
                                                      sv_from_cstr("version=1.2.3-beta"),
                                                      sv_from_cstr("\\1"));
    ASSERT(nob_sv_eq(replaced, sv_from_cstr("1.2.3")));

    ASSERT(cmk_glob_match(sv_from_cstr("*.txt"), sv_from_cstr("a.txt")) == true);
    ASSERT(cmk_glob_match(sv_from_cstr("*.txt"), sv_from_cstr("a.c")) == false);

    (void)sys_delete_path_recursive(arena, sv_from_cstr("temp_phase2_glob_mod"));
    ASSERT(sys_mkdir(sv_from_cstr("temp_phase2_glob_mod")));
    ASSERT(sys_mkdir(sv_from_cstr("temp_phase2_glob_mod/sub")));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_glob_mod/a.txt"), sv_from_cstr("a")));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_glob_mod/b.c"), sv_from_cstr("b")));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_glob_mod/sub/c.txt"), sv_from_cstr("c")));

    String_Builder list = {0};
    bool first = true;
    ASSERT(cmk_glob_collect_recursive(arena,
                                      sv_from_cstr("temp_phase2_glob_mod"),
                                      sv_from_cstr(""),
                                      sv_from_cstr("*.txt"),
                                      false,
                                      sv_from_cstr(""),
                                      &list,
                                      &first));
    ASSERT(strstr(list.items ? list.items : "", "a.txt") != NULL);
    ASSERT(strstr(list.items ? list.items : "", "c.txt") != NULL);
    nob_sb_free(list);

    String_List dirs = {0};
    String_List names = {0};
    string_list_init(&dirs);
    string_list_init(&names);
    string_list_add(&dirs, arena, sv_from_cstr("temp_phase2_glob_mod"));
    string_list_add(&names, arena, sv_from_cstr("a.txt"));
    String_View found = sv_from_cstr("");
    ASSERT(find_search_candidates(arena, &dirs, NULL, &names, &found));
    ASSERT(strstr(nob_temp_sv_to_cstr(found), "a.txt") != NULL);

    (void)sys_delete_path_recursive(arena, sv_from_cstr("temp_phase2_glob_mod"));
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cmake_meta_and_ctest_coverage_utils_basic) {
    Arena *arena = arena_create(1024 * 1024);
    ASSERT(arena != NULL);

    (void)sys_delete_path_recursive(arena, sv_from_cstr("temp_phase2_meta"));
    ASSERT(sys_mkdir(sv_from_cstr("temp_phase2_meta")));

    String_View query_root = sv_from_cstr("temp_phase2_meta/.cmake/api/v1/query");
    ASSERT(cmk_meta_emit_file_api_query(arena, query_root, sv_from_cstr("codemodel"), sv_from_cstr("1")));
    ASSERT(sys_file_exists(sv_from_cstr("temp_phase2_meta/.cmake/api/v1/query/codemodel-v1.json")));

    String_List targets = {0};
    string_list_init(&targets);
    string_list_add(&targets, arena, sv_from_cstr("app"));
    ASSERT(cmk_meta_export_write_targets_file(arena,
                                              sv_from_cstr("temp_phase2_meta/export/targets.cmake"),
                                              sv_from_cstr("Demo::"),
                                              sv_from_cstr("TARGETS"),
                                              sv_from_cstr(""),
                                              &targets,
                                              false));
    ASSERT(sys_file_exists(sv_from_cstr("temp_phase2_meta/export/targets.cmake")));

    ASSERT(cmk_meta_export_register_package(arena,
                                            sv_from_cstr("temp_phase2_meta/registry"),
                                            sv_from_cstr("DemoPkg"),
                                            sv_from_cstr("DemoPkg_DIR"),
                                            sv_from_cstr("temp_phase2_meta/install")));
    ASSERT(sys_file_exists(sv_from_cstr("temp_phase2_meta/registry/DemoPkg.cmake")));

    ASSERT(sys_mkdir(sv_from_cstr("temp_phase2_meta/cov")));
    ASSERT(sys_mkdir(sv_from_cstr("temp_phase2_meta/cov/build")));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_meta/cov/build/a.gcda"), sv_from_cstr("x")));
    ASSERT(sys_write_file(sv_from_cstr("temp_phase2_meta/cov/build/b.gcno"), sv_from_cstr("y")));

    String_List gcov_options = {0};
    string_list_init(&gcov_options);
    string_list_add(&gcov_options, arena, sv_from_cstr("--preserve-paths"));

    String_View data_json_path = sv_from_cstr("");
    String_View labels_json_path = sv_from_cstr("");
    String_View coverage_xml_path = sv_from_cstr("");
    size_t file_count = 0;
    ASSERT(ctest_coverage_collect_gcov_bundle(arena,
                                              sv_from_cstr("temp_phase2_meta/cov/src"),
                                              sv_from_cstr("temp_phase2_meta/cov/build"),
                                              sv_from_cstr("gcov"),
                                              &gcov_options,
                                              sv_from_cstr("temp_phase2_meta/cov/bundle.tar"),
                                              sv_from_cstr("FROM_EXT"),
                                              false,
                                              &data_json_path,
                                              &labels_json_path,
                                              &coverage_xml_path,
                                              &file_count));
    ASSERT(file_count >= 2);
    ASSERT(sys_file_exists(data_json_path));
    ASSERT(sys_file_exists(labels_json_path));
    ASSERT(sys_file_exists(coverage_xml_path));
    ASSERT(sys_file_exists(sv_from_cstr("temp_phase2_meta/cov/bundle.tar")));

    (void)sys_delete_path_recursive(arena, sv_from_cstr("temp_phase2_meta"));
    arena_destroy(arena);
    TEST_PASS();
}

void run_phase2_module_tests(int *passed, int *failed) {
    test_math_parser_precedence_unary_and_errors(passed, failed);
    test_genex_evaluator_basic_paths(passed, failed);
    test_sys_utils_directory_copy_delete_and_file_download(passed, failed);
    test_toolchain_driver_compiler_probe_and_try_run(passed, failed);
    test_cmake_path_utils_basic(passed, failed);
    test_cmake_regex_glob_find_utils_basic(passed, failed);
    test_cmake_meta_and_ctest_coverage_utils_basic(passed, failed);
}
