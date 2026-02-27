#include "test_transpiler_shared.h"

TEST(configure_file_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *in_file = "temp_configure_in.txt";
    const char *out_file = "temp_configure_out.txt";
    write_test_file(in_file, "@PROJECT_NAME@");

    const char *input =
        "project(Test)\n"
        "configure_file(temp_configure_in.txt temp_configure_out.txt @ONLY)\n"
        "file(READ temp_configure_out.txt CFG_NAME)\n"
        "add_executable(app_${CFG_NAME} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_Test") != NULL);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(out_file, &file_out));
    ASSERT(strstr(file_out.items, "Test") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(in_file);
    nob_delete_file(out_file);
    TEST_PASS();
}

TEST(file_write_append_read_basic) {
    Arena *arena = arena_create(1024 * 1024);
    const char *tmp_file = "temp_file_cmd.txt";
    const char *input =
        "file(WRITE temp_file_cmd.txt hello)\n"
        "file(APPEND temp_file_cmd.txt _world)\n"
        "file(READ temp_file_cmd.txt CONTENT)\n"
        "add_executable(app_${CONTENT} main.c)";

    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_hello_world") != NULL);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(tmp_file, &file_out));
    ASSERT(strstr(file_out.items, "hello_world") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(tmp_file);
    TEST_PASS();
}

TEST(file_copy_rename_remove_download_basic) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_file_ops");
    nob_mkdir_if_not_exists("temp_file_ops");
    nob_mkdir_if_not_exists("temp_file_ops/srcdir");
    nob_mkdir_if_not_exists("temp_file_ops/srcdir/nested");
    write_test_file("temp_file_ops/src.txt", "copy_me");
    write_test_file("temp_file_ops/srcdir/nested/n1.txt", "nested_one");

    const char *input =
        "project(Test)\n"
        "file(COPY temp_file_ops/src.txt temp_file_ops/srcdir DESTINATION temp_file_ops/out)\n"
        "file(RENAME temp_file_ops/out/src.txt temp_file_ops/out/renamed.txt RESULT RENAME_RC)\n"
        "file(REMOVE temp_file_ops/out/renamed.txt)\n"
        "file(DOWNLOAD file://temp_file_ops/srcdir/nested/n1.txt temp_file_ops/out/downloaded.txt STATUS DL_STATUS LOG DL_LOG)\n"
        "add_executable(app_${RENAME_RC} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "cmd_app_0") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("file") == 0);

    ASSERT(nob_file_exists("temp_file_ops/out/downloaded.txt"));
    ASSERT(!nob_file_exists("temp_file_ops/out/renamed.txt"));
    ASSERT(nob_file_exists("temp_file_ops/out/srcdir/nested/n1.txt"));

    Nob_String_Builder downloaded = {0};
    ASSERT(nob_read_entire_file("temp_file_ops/out/downloaded.txt", &downloaded));
    ASSERT(strstr(downloaded.items, "nested_one") != NULL);

    nob_sb_free(downloaded);
    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_file_ops");
    TEST_PASS();
}

TEST(file_glob_recurse_relative_and_list_directories_off) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_glob_ops");
    nob_mkdir_if_not_exists("temp_glob_ops");
    nob_mkdir_if_not_exists("temp_glob_ops/src");
    nob_mkdir_if_not_exists("temp_glob_ops/src/sub");
    write_test_file("temp_glob_ops/src/a.txt", "A");
    write_test_file("temp_glob_ops/src/sub/b.txt", "B");

    const char *input =
        "project(Test)\n"
        "file(GLOB_RECURSE GLOB_LIST RELATIVE temp_glob_ops LIST_DIRECTORIES OFF temp_glob_ops/src/*)\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"GLOB_${GLOB_LIST}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "src/a.txt") != NULL);
    ASSERT(strstr(output, "src/sub/b.txt") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("file") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_glob_ops");
    TEST_PASS();
}

TEST(make_directory_creates_requested_directories) {
    Arena *arena = arena_create(1024 * 1024);
    remove_test_tree("temp_make_dir");
    const char *input =
        "project(Test)\n"
        "make_directory(temp_make_dir/a temp_make_dir/b/c)\n"
        "add_executable(app main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    ASSERT(nob_file_exists("temp_make_dir/a"));
    ASSERT(nob_file_exists("temp_make_dir/b/c"));
    ASSERT(diag_telemetry_unsupported_count_for("make_directory") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    remove_test_tree("temp_make_dir");
    TEST_PASS();
}

TEST(remove_legacy_command_removes_list_items) {
    Arena *arena = arena_create(1024 * 1024);
    const char *input =
        "project(Test)\n"
        "set(MYLIST one;two;three;two)\n"
        "remove(MYLIST two missing)\n"
        "list(LENGTH MYLIST MYLEN)\n"
        "string(REPLACE \";\" \"_\" MYFLAT \"${MYLIST}\")\n"
        "add_executable(app main.c)\n"
        "target_compile_definitions(app PRIVATE \"LEN_${MYLEN}\" \"LIST_${MYFLAT}\")";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "-DLEN_2") != NULL);
    ASSERT(strstr(output, "-DLIST_one_three") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("remove") == 0);
    ASSERT(diag_has_errors() == false);

    nob_sb_free(sb);
    arena_destroy(arena);
    TEST_PASS();
}

TEST(write_file_legacy_write_and_append) {
    Arena *arena = arena_create(1024 * 1024);
    const char *tmp_file = "temp_write_file_cmd.txt";
    const char *input =
        "project(Test)\n"
        "write_file(temp_write_file_cmd.txt hello)\n"
        "write_file(temp_write_file_cmd.txt _world APPEND)\n"
        "file(READ temp_write_file_cmd.txt CONTENT)\n"
        "add_executable(app_${CONTENT} main.c)";

    diag_reset();
    diag_telemetry_reset();
    Ast_Root root = parse_cmake(arena, input);
    Nob_String_Builder sb = {0};
    transpile_datree(root, &sb);

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items);
    ASSERT(strstr(output, "Nob_Cmd cmd_app_hello_world") != NULL);
    ASSERT(diag_telemetry_unsupported_count_for("write_file") == 0);
    ASSERT(diag_has_errors() == false);

    Nob_String_Builder file_out = {0};
    ASSERT(nob_read_entire_file(tmp_file, &file_out));
    ASSERT(strstr(file_out.items, "hello_world") != NULL);

    nob_sb_free(file_out);
    nob_sb_free(sb);
    arena_destroy(arena);
    nob_delete_file(tmp_file);
    TEST_PASS();
}

void run_transpiler_suite_file_io(int *passed, int *failed) {
    test_configure_file_basic(passed, failed);
    test_file_write_append_read_basic(passed, failed);
    test_file_copy_rename_remove_download_basic(passed, failed);
    test_file_glob_recurse_relative_and_list_directories_off(passed, failed);
    test_make_directory_creates_requested_directories(passed, failed);
    test_remove_legacy_command_removes_list_items(passed, failed);
    test_write_file_legacy_write_and_append(passed, failed);
}
