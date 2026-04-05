#include "test_codegen_v2_common.h"
#include "test_fs.h"

#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

typedef struct {
    char name[64];
    char *prev_value;
    bool had_prev_value;
} Codegen_Env_Guard;

static void codegen_set_env_or_unset(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

static bool codegen_env_guard_set(Codegen_Env_Guard *guard,
                                  const char *name,
                                  const char *value) {
    const char *prev = NULL;
    if (!guard || !name) return false;
    memset(guard, 0, sizeof(*guard));
    if (snprintf(guard->name, sizeof(guard->name), "%s", name) >= (int)sizeof(guard->name)) {
        return false;
    }
    prev = getenv(name);
    guard->had_prev_value = prev != NULL;
    if (guard->had_prev_value) {
        guard->prev_value = strdup(prev);
        if (!guard->prev_value) return false;
    }
    codegen_set_env_or_unset(name, value);
    return true;
}

static bool codegen_env_guard_begin(Codegen_Env_Guard **out_guard,
                                    const char *name,
                                    const char *value) {
    Codegen_Env_Guard *guard = NULL;
    if (!out_guard) return false;
    *out_guard = NULL;
    guard = (Codegen_Env_Guard*)calloc(1, sizeof(*guard));
    if (!guard) return false;
    if (!codegen_env_guard_set(guard, name, value)) {
        free(guard->prev_value);
        free(guard);
        return false;
    }
    *out_guard = guard;
    return true;
}

static void codegen_env_guard_cleanup(void *ctx) {
    Codegen_Env_Guard *guard = (Codegen_Env_Guard*)ctx;
    if (!guard) return;
    codegen_set_env_or_unset(guard->name,
                             guard->had_prev_value ? guard->prev_value : NULL);
    free(guard->prev_value);
    free(guard);
}

static size_t codegen_count_substr(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    size_t count = 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return 0;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) != 0) continue;
        count++;
    }
    return count;
}

static bool codegen_run_argv_in_dir(const char *dir,
                                    const char *const *argv,
                                    size_t argc) {
    Nob_Cmd cmd = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !argv || argc == 0 || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    for (size_t i = 0; i < argc; ++i) {
        if (!argv[i]) continue;
        nob_cmd_append(&cmd, argv[i]);
    }
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}

static bool codegen_build_static_archive(const char *dir,
                                         const char *compiler,
                                         const char *source_path,
                                         const char *archive_path) {
    const char *obj_path = nob_temp_sprintf("%s.obj.o", archive_path);
    const char *compile_argv[] = {compiler, "-c", source_path, "-o", obj_path};
    const char *archive_argv[] = {"ar", "rcs", archive_path, obj_path};
    if (!dir || !compiler || !source_path || !archive_path) return false;
    return codegen_run_argv_in_dir(dir, compile_argv, NOB_ARRAY_LEN(compile_argv)) &&
           codegen_run_argv_in_dir(dir, archive_argv, NOB_ARRAY_LEN(archive_argv));
}

static bool codegen_build_shared_library(const char *dir,
                                         const char *compiler,
                                         const char *source_path,
                                         const char *shared_path) {
    const char *argv[] = {compiler, "-shared", "-fPIC", source_path, "-o", shared_path};
    if (!dir || !compiler || !source_path || !shared_path) return false;
    return codegen_run_argv_in_dir(dir, argv, NOB_ARRAY_LEN(argv));
}

#if !defined(_WIN32)
static bool codegen_make_tool_only_path_dir(const char *dir) {
    static const char *k_tools[] = {"cc", "c++", "as", "ld", "ar", "mkdir", "rm"};
    char tool_path[_TINYDIR_PATH_MAX] = {0};
    char tool_link[_TINYDIR_PATH_MAX] = {0};
    if (!dir) return false;
    if (!nob_mkdir_if_not_exists(dir)) return false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_tools); ++i) {
        if (!test_ws_host_program_in_path(k_tools[i], tool_path)) return false;
        if (!test_fs_join_path(dir, k_tools[i], tool_link)) return false;
        (void)unlink(tool_link);
        if (symlink(tool_path, tool_link) != 0) return false;
    }
    return true;
}
#endif

TEST(codegen_write_file_rebases_source_and_binary_roots_for_out_of_source_nob) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    Codegen_Test_Config config = {
        .input_path = "rebase_src/CMakeLists.txt",
        .output_path = "rebase_src/generated/nob.c",
        .source_dir = "rebase_src",
        .binary_dir = "rebase_build",
    };
    ASSERT(arena != NULL);

    ASSERT(codegen_write_text_file("rebase_src/src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(
        "project(Test C)\n"
        "add_executable(app src/main.c)\n",
        &config));

    ASSERT(codegen_load_text_file_to_arena(arena, "rebase_src/generated/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "../src/main.c"));
    ASSERT(codegen_sv_contains(generated, "../../rebase_build/app"));
    ASSERT(codegen_sv_contains(generated, "../../rebase_build/.nob/obj"));
    ASSERT(codegen_compile_generated_nob("rebase_src/generated/nob.c", "rebase_src/generated/nob_gen"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_default_out_of_source_top_level_targets_build_in_binary_root) {
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "default_src/CMakeLists.txt",
        .output_path = "default_src/generated/default/nob.c",
        .source_dir = "default_src",
        .binary_dir = "default_build",
    };

    ASSERT(codegen_write_text_file("default_src/core.c", "int core_value(void) { return 7; }\n"));
    ASSERT(codegen_write_text_file("default_src/shared.c", "int shared_value(void) { return 9; }\n"));
    ASSERT(codegen_write_text_file("default_src/plugin.c", "int plugin_value(void) { return 11; }\n"));
    ASSERT(codegen_write_text_file(
        "default_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 7 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("default_src/generated/default/nob.c",
                                         "default_src/generated/default/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("default_src/generated/default", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("default_build/app"));
    ASSERT(test_ws_host_path_exists("default_build/libcore.a"));
    ASSERT(test_ws_host_path_exists("default_build/libshared.so"));
    ASSERT(test_ws_host_path_exists("default_build/libplugin.so"));
    ASSERT(!test_ws_host_path_exists("default_src/app"));
    ASSERT(!test_ws_host_path_exists("default_src/libcore.a"));
    ASSERT(!test_ws_host_path_exists("default_src/libshared.so"));
    ASSERT(!test_ws_host_path_exists("default_src/libplugin.so"));
    TEST_PASS();
}

TEST(codegen_default_out_of_source_subdirectory_uses_owner_binary_dirs) {
    Test_Fs_Path_Info app_dir_info = {0};
    const char *root_script =
        "project(Test C)\n"
        "add_subdirectory(lib)\n"
        "add_subdirectory(app)\n";
    Codegen_Test_Config config = {
        .input_path = "subdir_src/CMakeLists.txt",
        .output_path = "subdir_src/generated/subdirs/nob.c",
        .source_dir = "subdir_src",
        .binary_dir = "subdir_build",
    };

    ASSERT(codegen_write_text_file(
        "subdir_src/lib/CMakeLists.txt",
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"));
    ASSERT(codegen_write_text_file(
        "subdir_src/app/CMakeLists.txt",
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/core.c", "int core_value(void) { return 3; }\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/shared.c", "int shared_value(void) { return 5; }\n"));
    ASSERT(codegen_write_text_file("subdir_src/lib/plugin.c", "int plugin_value(void) { return 7; }\n"));
    ASSERT(codegen_write_text_file(
        "subdir_src/app/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 3 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(root_script, &config));
    ASSERT(codegen_compile_generated_nob("subdir_src/generated/subdirs/nob.c",
                                         "subdir_src/generated/subdirs/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("subdir_src/generated/subdirs", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libcore.a"));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libshared.so"));
    ASSERT(test_ws_host_path_exists("subdir_build/lib/libplugin.so"));
    ASSERT(test_ws_host_path_exists("subdir_build/app/app"));
    ASSERT(!test_ws_host_path_exists("subdir_build/libcore.a"));
    ASSERT(test_fs_get_path_info("subdir_build/app", &app_dir_info));
    ASSERT(app_dir_info.exists);
    ASSERT(app_dir_info.is_dir);
    TEST_PASS();
}

TEST(codegen_explicit_output_directories_shape_out_of_source_artifacts) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME corex ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(shared SHARED shared.c)\n"
        "set_target_properties(shared PROPERTIES OUTPUT_NAME sharedx LIBRARY_OUTPUT_DIRECTORY artifacts/shlib)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "explicit_src/CMakeLists.txt",
        .output_path = "explicit_src/generated/explicit/nob.c",
        .source_dir = "explicit_src",
        .binary_dir = "explicit_build",
    };
    ASSERT(arena != NULL);

    ASSERT(codegen_write_text_file("explicit_src/core.c", "int core_value(void) { return 13; }\n"));
    ASSERT(codegen_write_text_file("explicit_src/shared.c", "int shared_value(void) { return 17; }\n"));
    ASSERT(codegen_write_text_file("explicit_src/plugin.c", "int plugin_value(void) { return 19; }\n"));
    ASSERT(codegen_write_text_file(
        "explicit_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 13 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_load_text_file_to_arena(arena, "explicit_src/generated/explicit/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "\"-shared\""));
    ASSERT(codegen_sv_contains(generated, "\"-fPIC\""));
    ASSERT(codegen_sv_contains(generated, "../../../explicit_build/artifacts/bin/runner"));
    ASSERT(codegen_compile_generated_nob("explicit_src/generated/explicit/nob.c",
                                         "explicit_src/generated/explicit/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("explicit_src/generated/explicit", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/bin/runner"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/lib/libcorex.a"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/shlib/libsharedx.so"));
    ASSERT(test_ws_host_path_exists("explicit_build/artifacts/modules/libplugin.so"));
    ASSERT(!test_ws_host_path_exists("explicit_src/artifacts"));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_cxx_static_dependency_uses_cxx_driver_for_link_out_of_source) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C CXX)\n"
        "add_library(core STATIC core.cpp)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "cxx_src/CMakeLists.txt",
        .output_path = "cxx_src/generated/cxx/nob.c",
        .source_dir = "cxx_src",
        .binary_dir = "cxx_build",
    };
    ASSERT(arena != NULL);
    ASSERT(codegen_write_text_file(
        "cxx_src/core.cpp",
        "#include <string>\n"
        "extern \"C\" int core_value(void) {\n"
        "    static std::string text = \"seven\";\n"
        "    return (int)text.size();\n"
        "}\n"));
    ASSERT(codegen_write_text_file(
        "cxx_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 5 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_load_text_file_to_arena(arena, "cxx_src/generated/cxx/nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&cc_cmd, true);"));
    ASSERT(codegen_sv_contains(generated, "append_toolchain_cmd(&link_cmd, true);"));
    ASSERT(codegen_compile_generated_nob("cxx_src/generated/cxx/nob.c", "cxx_src/generated/cxx/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("cxx_src/generated/cxx", "./nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("cxx_build/app"));
    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_clean_removes_out_of_source_outputs_but_preserves_binary_root) {
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "add_library(shared SHARED shared.c)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n";
    Codegen_Test_Config config = {
        .input_path = "clean_src/CMakeLists.txt",
        .output_path = "clean_src/generated/clean/nob.c",
        .source_dir = "clean_src",
        .binary_dir = "clean_build",
    };

    ASSERT(codegen_write_text_file("clean_src/core.c", "int core_value(void) { return 21; }\n"));
    ASSERT(codegen_write_text_file("clean_src/shared.c", "int shared_value(void) { return 22; }\n"));
    ASSERT(codegen_write_text_file("clean_src/plugin.c", "int plugin_value(void) { return 23; }\n"));
    ASSERT(codegen_write_text_file(
        "clean_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 21 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("clean_src/generated/clean/nob.c",
                                         "clean_src/generated/clean/nob_gen"));
    ASSERT(codegen_run_binary_in_dir("clean_src/generated/clean", "./nob_gen", NULL, NULL));
    ASSERT(test_ws_host_path_exists("clean_build/.nob"));
    ASSERT(test_ws_host_path_exists("clean_build/app"));
    ASSERT(codegen_run_binary_in_dir("clean_src/generated/clean", "./nob_gen", "clean", NULL));
    ASSERT(test_ws_host_path_exists("clean_build"));
    ASSERT(!test_ws_host_path_exists("clean_build/.nob"));
    ASSERT(!test_ws_host_path_exists("clean_build/app"));
    ASSERT(!test_ws_host_path_exists("clean_build/libcore.a"));
    ASSERT(!test_ws_host_path_exists("clean_build/libshared.so"));
    ASSERT(!test_ws_host_path_exists("clean_build/libplugin.so"));
    TEST_PASS();
}

TEST(codegen_ignores_cxx_modules_file_set_metadata_in_compile_inputs) {
    Nob_String_Builder sb = {0};
    ASSERT(codegen_render_script(
        "project(Test CXX)\n"
        "add_library(core STATIC core.cpp)\n"
        "target_sources(core PUBLIC FILE_SET CXX_MODULES BASE_DIRS modules FILES modules/core.cppm)\n"
        "add_executable(app main.cpp)\n"
        "target_link_libraries(app PRIVATE core)\n",
        "CMakeLists.txt",
        "nob.c",
        &sb));

    char *output = nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : "");
    ASSERT(strstr(output, "core.cpp") != NULL);
    ASSERT(strstr(output, "main.cpp") != NULL);
    ASSERT(strstr(output, "core.cppm") == NULL);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_builds_generated_source_from_output_rule_step) {
    const char *script =
        "project(Test C)\n"
        "add_custom_command(\n"
        "  OUTPUT generated/generated.c generated/generated.h\n"
        "  COMMAND sh -c \"mkdir -p gen_build/generated && cp gen_src/template_generated.c gen_build/generated/generated.c && cp gen_src/template_generated.h gen_build/generated/generated.h && printf generated > gen_build/generated/generated.log\"\n"
        "  DEPENDS template_generated.c template_generated.h\n"
        "  BYPRODUCTS generated/generated.log)\n"
        "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c)\n"
        "target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)\n";
    Codegen_Test_Config config = {
        .input_path = "gen_src/CMakeLists.txt",
        .output_path = "generated_graph_nob.c",
        .source_dir = "gen_src",
        .binary_dir = "gen_build",
    };

    ASSERT(codegen_write_text_file(
        "gen_src/template_generated.c",
        "#include \"generated.h\"\n"
        "int generated_value(void) { return GENERATED_VALUE; }\n"));
    ASSERT(codegen_write_text_file(
        "gen_src/template_generated.h",
        "#define GENERATED_VALUE 29\n"));
    ASSERT(codegen_write_text_file(
        "gen_src/main.c",
        "int generated_value(void);\n"
        "int main(void) { return generated_value() == 29 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("generated_graph_nob.c", "generated_graph_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./generated_graph_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("gen_build/app"));
    ASSERT(test_ws_host_path_exists("gen_build/generated/generated.c"));
    ASSERT(test_ws_host_path_exists("gen_build/generated/generated.h"));
    ASSERT(test_ws_host_path_exists("gen_build/generated/generated.log"));
    TEST_PASS();
}

TEST(codegen_renders_multi_command_steps_with_deduped_rebuild_inputs) {
    String_View generated = {0};
    Nob_String_Builder sb = {0};
    Codegen_Test_Config config = {
        .input_path = "dedupe_src/CMakeLists.txt",
        .output_path = "dedupe_nob.c",
        .source_dir = "dedupe_src",
        .binary_dir = "dedupe_build",
    };
    ASSERT(codegen_render_script_with_config(
        "project(Test C)\n"
        "add_custom_command(\n"
        "  OUTPUT generated/out.c\n"
        "  COMMAND echo first\n"
        "  COMMAND echo second\n"
        "  DEPENDS dedupe_input.idl dedupe_input.idl)\n",
        &config,
        &sb));
    generated = nob_sv_from_parts(sb.items ? sb.items : "", sb.count);
    ASSERT(codegen_count_substr(generated, "Nob_Cmd step_cmd = {0};") == 2);
    ASSERT(codegen_count_substr(generated, "dedupe_src/dedupe_input.idl") == 1);
    nob_sb_free(sb);
    TEST_PASS();
}

TEST(codegen_uses_embedded_cmake_for_runtime_steps_without_cmake_on_path) {
#if defined(_WIN32)
    TEST_SKIP("tool-only PATH probe is POSIX-only");
#else
    Codegen_Env_Guard *path_guard = NULL;
    const char *script =
        "project(Test C)\n"
        "add_custom_command(\n"
        "  OUTPUT generated/generated.c generated/generated.h\n"
        "  COMMAND cmake -E make_directory cmake_embed_build/generated\n"
        "  COMMAND cmake -E copy_if_different cmake_embed_src/template_generated.c cmake_embed_build/generated/generated.c\n"
        "  COMMAND cmake -E copy_if_different cmake_embed_src/template_generated.h cmake_embed_build/generated/generated.h)\n"
        "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c)\n"
        "target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)\n";
    Codegen_Test_Config config = {
        .input_path = "cmake_embed_src/CMakeLists.txt",
        .output_path = "cmake_embed_nob.c",
        .source_dir = "cmake_embed_src",
        .binary_dir = "cmake_embed_build",
    };

    ASSERT(codegen_write_text_file(
        "cmake_embed_src/template_generated.c",
        "#include \"generated.h\"\n"
        "int generated_value(void) { return GENERATED_VALUE; }\n"));
    ASSERT(codegen_write_text_file(
        "cmake_embed_src/template_generated.h",
        "#define GENERATED_VALUE 31\n"));
    ASSERT(codegen_write_text_file(
        "cmake_embed_src/main.c",
        "int generated_value(void);\n"
        "int main(void) { return generated_value() == 31 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("cmake_embed_nob.c", "cmake_embed_nob_gen"));
    ASSERT(codegen_make_tool_only_path_dir("tool_only_path"));
    ASSERT(codegen_env_guard_begin(&path_guard, "PATH", "tool_only_path"));
    TEST_DEFER(codegen_env_guard_cleanup, path_guard);
    ASSERT(codegen_run_binary_in_dir(".", "./cmake_embed_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("cmake_embed_build/app"));
    ASSERT(test_ws_host_path_exists("cmake_embed_build/generated/generated.c"));
    ASSERT(test_ws_host_path_exists("cmake_embed_build/generated/generated.h"));
    TEST_PASS();
#endif
}

TEST(codegen_custom_target_dependency_runs_and_clean_removes_step_stamps) {
    const char *script =
        "project(Test C)\n"
        "add_custom_target(prepare COMMAND sh -c \"mkdir -p ct_build/generated && printf ready > ct_build/generated/prepared.txt\")\n"
        "add_executable(app main.c)\n"
        "add_dependencies(app prepare)\n";
    Codegen_Test_Config config = {
        .input_path = "ct_src/CMakeLists.txt",
        .output_path = "custom_target_nob.c",
        .source_dir = "ct_src",
        .binary_dir = "ct_build",
    };

    ASSERT(codegen_write_text_file("ct_src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("custom_target_nob.c", "custom_target_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./custom_target_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("ct_build/app"));
    ASSERT(test_ws_host_path_exists("ct_build/generated/prepared.txt"));
    ASSERT(test_ws_host_path_exists("ct_build/.nob/steps"));
    ASSERT(codegen_run_binary_in_dir(".", "./custom_target_nob_gen", "clean", NULL));
    ASSERT(test_ws_host_path_exists("ct_build"));
    ASSERT(!test_ws_host_path_exists("ct_build/.nob/steps"));
    TEST_PASS();
}

TEST(codegen_target_hooks_run_at_pre_link_and_post_build_boundaries) {
    const char *script =
        "project(Test C)\n"
        "add_executable(app main.c)\n"
        "add_custom_command(TARGET app PRE_LINK\n"
        "  COMMAND sh -c \"test ! -e hook_build/app && mkdir -p hook_build/hooks && printf pre > hook_build/hooks/pre.txt\"\n"
        "  BYPRODUCTS hooks/pre.txt)\n"
        "add_custom_command(TARGET app POST_BUILD\n"
        "  COMMAND sh -c \"test -e hook_build/app && mkdir -p hook_build/hooks && printf post > hook_build/hooks/post.txt\"\n"
        "  BYPRODUCTS hooks/post.txt)\n";
    Codegen_Test_Config config = {
        .input_path = "hook_src/CMakeLists.txt",
        .output_path = "hook_nob.c",
        .source_dir = "hook_src",
        .binary_dir = "hook_build",
    };

    ASSERT(codegen_write_text_file("hook_src/main.c", "int main(void) { return 0; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("hook_nob.c", "hook_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./hook_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("hook_build/app"));
    ASSERT(test_ws_host_path_exists("hook_build/hooks/pre.txt"));
    ASSERT(test_ws_host_path_exists("hook_build/hooks/post.txt"));
    TEST_PASS();
}

TEST(codegen_mixed_c_and_cxx_compile_contexts_apply_language_options_and_standard_flags) {
    Arena *arena = arena_create(512 * 1024);
    String_View generated = {0};
    const char *script =
        "project(Test C CXX)\n"
        "add_executable(app main.c main.cpp)\n"
        "target_compile_options(app PRIVATE\n"
        "  \"$<$<COMPILE_LANGUAGE:C>:-DC_ONLY>\"\n"
        "  \"$<$<COMPILE_LANGUAGE:CXX>:-DCXX_ONLY>\")\n"
        "target_compile_features(app PRIVATE c_std_99 cxx_std_14)\n"
        "set_target_properties(app PROPERTIES\n"
        "  C_EXTENSIONS OFF\n"
        "  CXX_EXTENSIONS OFF\n"
        "  RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "lang_cfg_src/CMakeLists.txt",
        .output_path = "lang_cfg_nob.c",
        .source_dir = "lang_cfg_src",
        .binary_dir = "lang_cfg_build",
    };
    ASSERT(arena != NULL);

    ASSERT(codegen_write_text_file(
        "lang_cfg_src/main.c",
        "#ifndef C_ONLY\n"
        "#error C_ONLY missing\n"
        "#endif\n"
        "int c_part(int * restrict value) { return *value; }\n"));
    ASSERT(codegen_write_text_file(
        "lang_cfg_src/main.cpp",
        "#ifndef CXX_ONLY\n"
        "#error CXX_ONLY missing\n"
        "#endif\n"
        "extern \"C\" int c_part(int *value);\n"
        "int main() {\n"
        "    auto next = [](auto value) { return value + 1; };\n"
        "    int v = 41;\n"
        "    return next(c_part(&v)) == 42 ? 0 : 1;\n"
        "}\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_load_text_file_to_arena(arena, "lang_cfg_nob.c", &generated));
    ASSERT(codegen_sv_contains(generated, "\"-std=c99\""));
    ASSERT(codegen_sv_contains(generated, "\"-std=c++14\""));
    ASSERT(codegen_compile_generated_nob("lang_cfg_nob.c", "lang_cfg_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./lang_cfg_nob_gen", "app", NULL));
    ASSERT(codegen_run_binary_in_dir(".", "lang_cfg_build/artifacts/bin/app", NULL, NULL));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(codegen_imported_static_unknown_and_interface_targets_build_and_run) {
    const char *script =
        "project(Test C)\n"
        "add_library(ext_static STATIC IMPORTED)\n"
        "set_target_properties(ext_static PROPERTIES\n"
        "  IMPORTED_LOCATION imports/libext_static.a\n"
        "  IMPORTED_LINK_INTERFACE_LANGUAGES \"CXX\")\n"
        "add_library(ext_unknown UNKNOWN IMPORTED)\n"
        "set_target_properties(ext_unknown PROPERTIES IMPORTED_LOCATION imports/libext_unknown.a)\n"
        "add_library(ext_iface INTERFACE IMPORTED)\n"
        "set_target_properties(ext_iface PROPERTIES\n"
        "  INTERFACE_INCLUDE_DIRECTORIES imports/include\n"
        "  INTERFACE_COMPILE_DEFINITIONS IFACE_EXPECT=23)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ext_static ext_unknown ext_iface)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "import_mix_src/CMakeLists.txt",
        .output_path = "import_mix_nob.c",
        .source_dir = "import_mix_src",
        .binary_dir = "import_mix_build",
    };

    ASSERT(codegen_write_text_file(
        "import_mix_src/imports/ext_static.cpp",
        "#include <string>\n"
        "extern \"C\" int ext_static(void) {\n"
        "    static std::string value = \"xyz\";\n"
        "    return (int)value.size();\n"
        "}\n"));
    ASSERT(codegen_write_text_file(
        "import_mix_src/imports/ext_unknown.c",
        "int ext_unknown(void) { return 11; }\n"));
    ASSERT(codegen_write_text_file(
        "import_mix_src/imports/include/iface.h",
        "#ifndef IFACE_EXPECT\n"
        "#error IFACE_EXPECT missing\n"
        "#endif\n"
        "#if IFACE_EXPECT != 23\n"
        "#error IFACE_EXPECT mismatch\n"
        "#endif\n"));
    ASSERT(codegen_write_text_file(
        "import_mix_src/main.c",
        "#include \"iface.h\"\n"
        "int ext_static(void);\n"
        "int ext_unknown(void);\n"
        "int main(void) { return (ext_static() + ext_unknown()) == 14 ? 0 : 1; }\n"));
    ASSERT(codegen_build_static_archive("import_mix_src", "c++", "imports/ext_static.cpp", "imports/libext_static.a"));
    ASSERT(codegen_build_static_archive("import_mix_src", "cc", "imports/ext_unknown.c", "imports/libext_unknown.a"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("import_mix_nob.c", "import_mix_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./import_mix_nob_gen", "app", NULL));
    ASSERT(codegen_run_binary_in_dir(".", "import_mix_build/artifacts/bin/app", NULL, NULL));
    TEST_PASS();
}

TEST(codegen_imported_shared_target_links_successfully) {
    const char *script =
        "project(Test C)\n"
        "add_library(ext_shared SHARED IMPORTED)\n"
        "set_target_properties(ext_shared PROPERTIES IMPORTED_LOCATION imports/libext_shared.so)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ext_shared)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "import_shared_src/CMakeLists.txt",
        .output_path = "import_shared_nob.c",
        .source_dir = "import_shared_src",
        .binary_dir = "import_shared_build",
    };

    ASSERT(codegen_write_text_file(
        "import_shared_src/imports/ext_shared.c",
        "int ext_shared(void) { return 29; }\n"));
    ASSERT(codegen_write_text_file(
        "import_shared_src/main.c",
        "int ext_shared(void);\n"
        "int main(void) { return ext_shared() == 29 ? 0 : 1; }\n"));
    ASSERT(codegen_build_shared_library("import_shared_src", "cc", "imports/ext_shared.c", "imports/libext_shared.so"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("import_shared_nob.c", "import_shared_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./import_shared_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("import_shared_build/artifacts/bin/app"));
    TEST_PASS();
}

TEST(codegen_debug_and_optimized_link_items_follow_generated_config) {
    const char *debug_build_argv[] = {"--config", "Debug", "app"};
    const char *script =
        "project(Test C)\n"
        "add_library(opt STATIC IMPORTED)\n"
        "set_target_properties(opt PROPERTIES IMPORTED_LOCATION imports/libopt.a)\n"
        "add_library(dbg STATIC IMPORTED)\n"
        "set_target_properties(dbg PROPERTIES IMPORTED_LOCATION imports/libdbg.a)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE optimized opt debug dbg)\n"
        "target_compile_definitions(app PRIVATE\n"
        "  \"$<$<CONFIG:Debug>:EXPECTED_VALUE=23>\"\n"
        "  \"$<$<NOT:$<CONFIG:Debug>>:EXPECTED_VALUE=17>\")\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "import_cfg_src/CMakeLists.txt",
        .output_path = "import_cfg_nob.c",
        .source_dir = "import_cfg_src",
        .binary_dir = "import_cfg_build",
    };

    ASSERT(codegen_write_text_file(
        "import_cfg_src/imports/opt.c",
        "int selected_value(void) { return 17; }\n"));
    ASSERT(codegen_write_text_file(
        "import_cfg_src/imports/dbg.c",
        "int selected_value(void) { return 23; }\n"));
    ASSERT(codegen_write_text_file(
        "import_cfg_src/main.c",
        "#ifndef EXPECTED_VALUE\n"
        "#error EXPECTED_VALUE missing\n"
        "#endif\n"
        "int selected_value(void);\n"
        "int main(void) { return selected_value() == EXPECTED_VALUE ? 0 : 1; }\n"));
    ASSERT(codegen_build_static_archive("import_cfg_src", "cc", "imports/opt.c", "imports/libopt.a"));
    ASSERT(codegen_build_static_archive("import_cfg_src", "cc", "imports/dbg.c", "imports/libdbg.a"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("import_cfg_nob.c", "import_cfg_nob_gen"));

    ASSERT(codegen_run_binary_in_dir(".", "./import_cfg_nob_gen", "app", NULL));
    ASSERT(codegen_run_binary_in_dir(".", "import_cfg_build/artifacts/bin/app", NULL, NULL));
    ASSERT(codegen_run_binary_in_dir(".", "./import_cfg_nob_gen", "clean", NULL));
    ASSERT(codegen_run_binary_in_dir_argv(".", "./import_cfg_nob_gen", debug_build_argv, NOB_ARRAY_LEN(debug_build_argv)));
    ASSERT(codegen_run_binary_in_dir(".", "import_cfg_build/artifacts/bin/app", NULL, NULL));
    TEST_PASS();
}

TEST(codegen_build_steps_resolve_target_file_and_target_linker_file_genex) {
    const char *script =
        "project(Test C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_custom_command(TARGET core POST_BUILD\n"
        "  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/artifacts/copies\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:core> ${CMAKE_CURRENT_BINARY_DIR}/artifacts/copies/core-file.a\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_LINKER_FILE:core> ${CMAKE_CURRENT_BINARY_DIR}/artifacts/copies/core-linker.a\n"
        "  BYPRODUCTS artifacts/copies/core-file.a artifacts/copies/core-linker.a)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n";
    Codegen_Test_Config config = {
        .input_path = "target_file_src/CMakeLists.txt",
        .output_path = "target_file_nob.c",
        .source_dir = "target_file_src",
        .binary_dir = "target_file_build",
    };

    ASSERT(codegen_write_text_file("target_file_src/core.c", "int core_value(void) { return 5; }\n"));
    ASSERT(codegen_write_text_file(
        "target_file_src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 5 ? 0 : 1; }\n"));
    ASSERT(codegen_write_script_with_config(script, &config));
    ASSERT(codegen_compile_generated_nob("target_file_nob.c", "target_file_nob_gen"));
    ASSERT(codegen_run_binary_in_dir(".", "./target_file_nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("target_file_build/artifacts/lib/libcore.a"));
    ASSERT(test_ws_host_path_exists("target_file_build/artifacts/copies/core-file.a"));
    ASSERT(test_ws_host_path_exists("target_file_build/artifacts/copies/core-linker.a"));
    TEST_PASS();
}

void run_codegen_v2_build_tests(int *passed, int *failed, int *skipped) {
    test_codegen_write_file_rebases_source_and_binary_roots_for_out_of_source_nob(passed, failed, skipped);
    test_codegen_default_out_of_source_top_level_targets_build_in_binary_root(passed, failed, skipped);
    test_codegen_default_out_of_source_subdirectory_uses_owner_binary_dirs(passed, failed, skipped);
    test_codegen_explicit_output_directories_shape_out_of_source_artifacts(passed, failed, skipped);
    test_codegen_cxx_static_dependency_uses_cxx_driver_for_link_out_of_source(passed, failed, skipped);
    test_codegen_clean_removes_out_of_source_outputs_but_preserves_binary_root(passed, failed, skipped);
    test_codegen_ignores_cxx_modules_file_set_metadata_in_compile_inputs(passed, failed, skipped);
    test_codegen_builds_generated_source_from_output_rule_step(passed, failed, skipped);
    test_codegen_renders_multi_command_steps_with_deduped_rebuild_inputs(passed, failed, skipped);
    test_codegen_uses_embedded_cmake_for_runtime_steps_without_cmake_on_path(passed, failed, skipped);
    test_codegen_custom_target_dependency_runs_and_clean_removes_step_stamps(passed, failed, skipped);
    test_codegen_target_hooks_run_at_pre_link_and_post_build_boundaries(passed, failed, skipped);
    test_codegen_mixed_c_and_cxx_compile_contexts_apply_language_options_and_standard_flags(passed, failed, skipped);
    test_codegen_imported_static_unknown_and_interface_targets_build_and_run(passed, failed, skipped);
    test_codegen_imported_shared_target_links_successfully(passed, failed, skipped);
    test_codegen_debug_and_optimized_link_items_follow_generated_config(passed, failed, skipped);
    test_codegen_build_steps_resolve_target_file_and_target_linker_file_genex(passed, failed, skipped);
}
