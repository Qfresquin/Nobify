#include "../nob.h"
#include "../build_model.h"
#include "../math_parser.h"
#include "../genex_evaluator.h"
#include "../sys_utils.h"
#include "../toolchain_driver.h"

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

void run_phase2_module_tests(int *passed, int *failed) {
    test_math_parser_precedence_unary_and_errors(passed, failed);
    test_genex_evaluator_basic_paths(passed, failed);
    test_sys_utils_directory_copy_delete_and_file_download(passed, failed);
    test_toolchain_driver_compiler_probe_and_try_run(passed, failed);
}
