#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"
#include "test_host_fixture_support.h"
#include "test_v2_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static Artifact_Parity_Cmake_Config s_artifact_parity_cmake = {0};
static char s_artifact_parity_skip_reason[256] = {0};
static char s_artifact_parity_nobify_bin[_TINYDIR_PATH_MAX] = {0};
static char s_artifact_parity_nobify_error[256] = {0};

#if !defined(_WIN32)
static bool artifact_parity_make_tool_only_path_dir(const char *dir) {
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

static const char *artifact_parity_case_source_root(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->source_root && case_def->source_root[0] != '\0'
        ? case_def->source_root
        : "source";
}

static const char *artifact_parity_case_cmake_binary_dir(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->cmake_binary_dir && case_def->cmake_binary_dir[0] != '\0'
        ? case_def->cmake_binary_dir
        : "cmake_build";
}

static const char *artifact_parity_case_nob_binary_dir(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->nob_binary_dir && case_def->nob_binary_dir[0] != '\0'
        ? case_def->nob_binary_dir
        : "source";
}

static const char *artifact_parity_case_generated_nob_path(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->generated_nob_path && case_def->generated_nob_path[0] != '\0'
        ? case_def->generated_nob_path
        : "source/nob.c";
}

static const char *artifact_parity_case_nob_run_dir(const Artifact_Parity_Case *case_def) {
    const char *generated_nob_path = artifact_parity_case_generated_nob_path(case_def);
    return case_def && case_def->nob_run_dir && case_def->nob_run_dir[0] != '\0'
        ? case_def->nob_run_dir
        : nob_temp_dir_name(generated_nob_path);
}

static const char *artifact_parity_case_generated_nob_bin_path(const Artifact_Parity_Case *case_def) {
    return nob_temp_sprintf("%s/nob_gen", artifact_parity_case_nob_run_dir(case_def));
}

static const char *artifact_parity_case_cmake_install_prefix(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->cmake_install_prefix && case_def->cmake_install_prefix[0] != '\0'
        ? case_def->cmake_install_prefix
        : "cmake_install";
}

static const char *artifact_parity_case_cmake_install_component(const Artifact_Parity_Case *case_def) {
    return case_def ? case_def->cmake_install_component : NULL;
}

static const char *artifact_parity_case_nob_install_prefix(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->nob_install_prefix && case_def->nob_install_prefix[0] != '\0'
        ? case_def->nob_install_prefix
        : "source/install";
}

static const char *artifact_parity_case_nob_install_component(const Artifact_Parity_Case *case_def) {
    return case_def ? case_def->nob_install_component : NULL;
}

static const char *artifact_parity_case_cmake_home_dir(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->cmake_home_dir && case_def->cmake_home_dir[0] != '\0'
        ? case_def->cmake_home_dir
        : NULL;
}

static const char *artifact_parity_case_nob_home_dir(const Artifact_Parity_Case *case_def) {
    return case_def && case_def->nob_home_dir && case_def->nob_home_dir[0] != '\0'
        ? case_def->nob_home_dir
        : NULL;
}

static bool artifact_parity_run_cmd_in_dir(const char *dir, Nob_Cmd *cmd) {
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !cmd || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);

    if (!nob_set_current_dir(dir)) return false;
    ok = nob_cmd_run(cmd);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}

static bool artifact_parity_build_archive_in_dir(const char *dir,
                                                 const char *compiler,
                                                 const char *source_relpath,
                                                 const char *output_relpath) {
    Nob_Cmd compile_cmd = {0};
    Nob_Cmd archive_cmd = {0};
    const char *object_relpath = nob_temp_sprintf("%s.obj.o", output_relpath);
    if (!dir || !compiler || !source_relpath || !output_relpath) return false;

    nob_cmd_append(&compile_cmd, compiler, "-c", source_relpath, "-o", object_relpath);
    if (!artifact_parity_run_cmd_in_dir(dir, &compile_cmd)) {
        nob_cmd_free(compile_cmd);
        return false;
    }
    nob_cmd_free(compile_cmd);

    nob_cmd_append(&archive_cmd, "ar", "rcs", output_relpath, object_relpath);
    if (!artifact_parity_run_cmd_in_dir(dir, &archive_cmd)) {
        nob_cmd_free(archive_cmd);
        return false;
    }
    nob_cmd_free(archive_cmd);

    return true;
}

static bool artifact_parity_prepare_install_case(const Artifact_Parity_Case *case_def) {
    const char *source_root = artifact_parity_case_source_root(case_def);
    if (!source_root) return false;
    return artifact_parity_write_executable_file(nob_temp_sprintf("%s/scripts/run-helper.sh", source_root),
                                                 "#!/bin/sh\n"
                                                 "exit 0\n");
}

static bool artifact_parity_make_abs_path(const char *relpath,
                                          char out_path[_TINYDIR_PATH_MAX]) {
    const char *cwd = nob_get_current_dir_temp();
    int n = 0;
    if (!relpath || !out_path || !cwd) return false;
    n = snprintf(out_path, _TINYDIR_PATH_MAX, "%s/%s", cwd, relpath);
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool artifact_parity_write_consumer_project(const char *source_dir,
                                                   const char *cmakelists_text,
                                                   const char *main_text) {
    if (!source_dir || !cmakelists_text || !main_text) return false;
    if (!nob_mkdir_if_not_exists(source_dir)) return false;
    if (!artifact_parity_write_text_file(nob_temp_sprintf("%s/CMakeLists.txt", source_dir), cmakelists_text)) {
        return false;
    }
    return artifact_parity_write_text_file(nob_temp_sprintf("%s/main.c", source_dir), main_text);
}

static bool artifact_parity_build_consumer_project(const char *source_dir,
                                                   const char *binary_dir,
                                                   const char *home_dir) {
    Test_Host_Env_Guard home_guard = {0};
    bool home_guard_active = false;
    if (!source_dir || !binary_dir) return false;
    if (home_dir && home_dir[0] != '\0') {
        if (!nob_mkdir_if_not_exists(home_dir) ||
            !test_host_env_guard_begin(&home_guard, "HOME", home_dir)) {
            return false;
        }
        home_guard_active = true;
    }

    if (!artifact_parity_run_cmake_configure(&s_artifact_parity_cmake, source_dir, binary_dir, NULL) ||
        !artifact_parity_run_cmake_build(&s_artifact_parity_cmake, binary_dir, "consumer")) {
        if (home_guard_active) test_host_env_guard_cleanup(&home_guard);
        return false;
    }

    if (home_guard_active) test_host_env_guard_cleanup(&home_guard);
    return artifact_parity_run_binary_in_dir(binary_dir, "./consumer", NULL, NULL);
}

static bool artifact_parity_verify_export_include_consumer(const char *export_relpath,
                                                           const char *consumer_source_dir,
                                                           const char *consumer_binary_dir,
                                                           const char *home_dir) {
    char export_abs[_TINYDIR_PATH_MAX] = {0};
    if (!export_relpath || !consumer_source_dir || !consumer_binary_dir) return false;
    if (!artifact_parity_make_abs_path(export_relpath, export_abs)) return false;
    return artifact_parity_write_consumer_project(
               consumer_source_dir,
               nob_temp_sprintf("cmake_minimum_required(VERSION 3.28)\n"
                                "project(ExportConsumer LANGUAGES C)\n"
                                "include(\"%s\")\n"
                                "add_executable(consumer main.c)\n"
                                "target_link_libraries(consumer PRIVATE Demo::core)\n",
                                export_abs),
               "int main(void) { return 0; }\n") &&
           artifact_parity_build_consumer_project(consumer_source_dir, consumer_binary_dir, home_dir);
}

static bool artifact_parity_verify_package_registry_consumer(const char *consumer_source_dir,
                                                             const char *consumer_binary_dir,
                                                             const char *home_dir) {
    return artifact_parity_write_consumer_project(
               consumer_source_dir,
               "cmake_minimum_required(VERSION 3.28)\n"
               "project(PackageRegistryConsumer LANGUAGES C)\n"
               "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
               "find_package(DemoPkg CONFIG REQUIRED)\n"
               "add_executable(consumer main.c)\n"
               "target_link_libraries(consumer PRIVATE DemoPkg::core)\n",
               "int main(void) { return 0; }\n") &&
           artifact_parity_build_consumer_project(consumer_source_dir, consumer_binary_dir, home_dir);
}

static bool artifact_parity_verify_export_targets_case(const Artifact_Parity_Case *case_def) {
    (void)case_def;
    return artifact_parity_verify_export_include_consumer("cmake_build/exports/CoreTargets.cmake",
                                                          "consumer_export_targets_cmake",
                                                          "consumer_export_targets_cmake_build",
                                                          NULL) &&
           artifact_parity_verify_export_include_consumer("nob_build/exports/CoreTargets.cmake",
                                                          "consumer_export_targets_nob",
                                                          "consumer_export_targets_nob_build",
                                                          NULL);
}

static bool artifact_parity_verify_export_export_set_case(const Artifact_Parity_Case *case_def) {
    (void)case_def;
    return artifact_parity_verify_export_include_consumer("cmake_build/exports/DemoTargets.cmake",
                                                          "consumer_export_set_cmake",
                                                          "consumer_export_set_cmake_build",
                                                          NULL) &&
           artifact_parity_verify_export_include_consumer("nob_build/exports/DemoTargets.cmake",
                                                          "consumer_export_set_nob",
                                                          "consumer_export_set_nob_build",
                                                          NULL);
}

static bool artifact_parity_verify_export_package_case(const Artifact_Parity_Case *case_def) {
    return artifact_parity_verify_package_registry_consumer("consumer_export_pkg_cmake",
                                                            "consumer_export_pkg_cmake_build",
                                                            artifact_parity_case_cmake_home_dir(case_def)) &&
           artifact_parity_verify_package_registry_consumer("consumer_export_pkg_nob",
                                                            "consumer_export_pkg_nob_build",
                                                            artifact_parity_case_nob_home_dir(case_def));
}

static bool artifact_parity_run_nob_command(const Artifact_Parity_Case *case_def,
                                            const Artifact_Parity_Nob_Command *command) {
    const char *run_dir = artifact_parity_case_nob_run_dir(case_def);
    const char *argv[8] = {0};
    size_t argc = 0;
    if (!case_def || !command) return false;

    if (command->config && command->config[0] != '\0') {
        argv[argc++] = "--config";
        argv[argc++] = command->config;
    }

    switch (command->kind) {
        case ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT:
            return artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc);

        case ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET:
            argv[argc++] = command->arg;
            return artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc);

        case ARTIFACT_PARITY_NOB_COMMAND_CLEAN:
            argv[argc++] = "clean";
            if (!artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc)) return false;
            if (case_def->clean_absence_relpath && case_def->clean_absence_relpath[0] != '\0') {
                if (test_ws_host_path_exists(case_def->clean_absence_relpath)) {
                    nob_log(NOB_ERROR,
                            "artifact parity: clean did not remove %s",
                            case_def->clean_absence_relpath);
                    return false;
                }
            }
            return true;

        case ARTIFACT_PARITY_NOB_COMMAND_INSTALL:
            argv[argc++] = "install";
            if (artifact_parity_case_nob_install_prefix(case_def)[0] != '\0') {
                argv[argc++] = "--prefix";
                argv[argc++] = artifact_parity_case_nob_install_prefix(case_def);
            }
            if (artifact_parity_case_nob_install_component(case_def) &&
                artifact_parity_case_nob_install_component(case_def)[0] != '\0') {
                argv[argc++] = "--component";
                argv[argc++] = artifact_parity_case_nob_install_component(case_def);
            }
            return artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc);

        case ARTIFACT_PARITY_NOB_COMMAND_PACKAGE:
            argv[argc++] = "package";
            return artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc);

        case ARTIFACT_PARITY_NOB_COMMAND_EXPORT:
            argv[argc++] = "export";
            return artifact_parity_run_binary_in_dir_argv(run_dir, "./nob_gen", argv, argc);
    }

    return false;
}

static bool artifact_parity_run_case(const Artifact_Parity_Case *case_def) {
    Arena *arena = NULL;
    String_View cmake_manifest = {0};
    String_View nob_manifest = {0};
    char input_abs[_TINYDIR_PATH_MAX] = {0};
    char output_abs[_TINYDIR_PATH_MAX] = {0};
    char source_root_abs[_TINYDIR_PATH_MAX] = {0};
    char nob_binary_root_abs[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    const char *source_root = NULL;
    const char *cmake_binary_dir = NULL;
    const char *nob_binary_dir = NULL;
    const char *generated_nob_path = NULL;
    const char *generated_nob_bin_path = NULL;
    const char *cmake_install_prefix = NULL;
    const char *cmake_home_dir = NULL;
    const char *nob_home_dir = NULL;
    Test_Host_Env_Guard cmake_home_guard = {0};
    Test_Host_Env_Guard nob_home_guard = {0};
    bool cmake_home_guard_active = false;
    bool nob_home_guard_active = false;

    if (!case_def) return false;
    if (!cwd) return false;
    source_root = artifact_parity_case_source_root(case_def);
    cmake_binary_dir = artifact_parity_case_cmake_binary_dir(case_def);
    nob_binary_dir = artifact_parity_case_nob_binary_dir(case_def);
    generated_nob_path = artifact_parity_case_generated_nob_path(case_def);
    generated_nob_bin_path = artifact_parity_case_generated_nob_bin_path(case_def);
    cmake_install_prefix = artifact_parity_case_cmake_install_prefix(case_def);
    cmake_home_dir = artifact_parity_case_cmake_home_dir(case_def);
    nob_home_dir = artifact_parity_case_nob_home_dir(case_def);
    if (s_artifact_parity_nobify_bin[0] == '\0') {
        nob_log(NOB_ERROR,
                "artifact parity suite: missing nobify tool: %s",
                s_artifact_parity_nobify_error[0]
                    ? s_artifact_parity_nobify_error
                    : "runner did not provide a nobify binary");
        return false;
    }
    if (!s_artifact_parity_cmake.available) {
        nob_log(NOB_ERROR,
                "artifact parity suite: cmake unavailable for %s: %s",
                case_def->name,
                s_artifact_parity_skip_reason[0]
                    ? s_artifact_parity_skip_reason
                    : "cmake 3.28.x is not available");
        return false;
    }

    if (!artifact_parity_materialize_files(source_root, case_def->files, case_def->file_count)) return false;
    if (case_def->prepare && !case_def->prepare(case_def)) return false;

    if (cmake_home_dir && cmake_home_dir[0] != '\0') {
        if (!nob_mkdir_if_not_exists(cmake_home_dir) ||
            !test_host_env_guard_begin(&cmake_home_guard, "HOME", cmake_home_dir)) {
            return false;
        }
        cmake_home_guard_active = true;
    }

    if ((case_def->phases & ARTIFACT_PARITY_PHASE_CONFIGURE) ||
        (case_def->phases & ARTIFACT_PARITY_PHASE_BUILD) ||
        (case_def->phases & ARTIFACT_PARITY_PHASE_INSTALL) ||
        (case_def->phases & ARTIFACT_PARITY_PHASE_EXPORT)) {
        if (!artifact_parity_run_cmake_configure(&s_artifact_parity_cmake,
                                                 source_root,
                                                 cmake_binary_dir,
                                                 case_def->cmake_build_type)) {
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_BUILD) {
        if (!artifact_parity_run_cmake_build(&s_artifact_parity_cmake,
                                             cmake_binary_dir,
                                             case_def->cmake_build_target)) {
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_INSTALL) {
        if (!artifact_parity_run_cmake_install(&s_artifact_parity_cmake,
                                               cmake_binary_dir,
                                               cmake_install_prefix,
                                               artifact_parity_case_cmake_install_component(case_def))) {
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_PACKAGE) {
        Artifact_Parity_Cmake_Config package_config = {0};
        char package_skip_reason[256] = {0};
        if (!artifact_parity_resolve_cmake(&package_config, true, package_skip_reason)) {
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return false;
        }
        if (!package_config.cpack_available) {
            nob_log(NOB_INFO,
                    "artifact parity case %s skipped package phase: %s",
                    case_def->name,
                    package_skip_reason[0] ? package_skip_reason : "cpack is unavailable");
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return true;
        }
        if (!artifact_parity_run_cmake_package(&package_config, cmake_binary_dir)) {
            if (cmake_home_guard_active) test_host_env_guard_cleanup(&cmake_home_guard);
            return false;
        }
    }
    if (cmake_home_guard_active) {
        test_host_env_guard_cleanup(&cmake_home_guard);
        cmake_home_guard_active = false;
    }

    if (snprintf(input_abs, sizeof(input_abs), "%s/%s/CMakeLists.txt", cwd, source_root) >= (int)sizeof(input_abs) ||
        snprintf(output_abs, sizeof(output_abs), "%s/%s", cwd, generated_nob_path) >= (int)sizeof(output_abs) ||
        snprintf(source_root_abs, sizeof(source_root_abs), "%s/%s", cwd, source_root) >= (int)sizeof(source_root_abs) ||
        snprintf(nob_binary_root_abs, sizeof(nob_binary_root_abs), "%s/%s", cwd, nob_binary_dir) >= (int)sizeof(nob_binary_root_abs)) {
        return false;
    }

    if (!artifact_parity_run_nobify(s_artifact_parity_nobify_bin,
                                    input_abs,
                                    output_abs,
                                    case_def->source_root ? source_root_abs : NULL,
                                    case_def->nob_binary_dir ? nob_binary_root_abs : NULL)) {
        return false;
    }
    if (!artifact_parity_compile_generated_nob(generated_nob_path, generated_nob_bin_path)) return false;

    if (nob_home_dir && nob_home_dir[0] != '\0') {
        if (!nob_mkdir_if_not_exists(nob_home_dir) ||
            !test_host_env_guard_begin(&nob_home_guard, "HOME", nob_home_dir)) {
            return false;
        }
        nob_home_guard_active = true;
    }

    for (size_t i = 0; i < case_def->nob_command_count; ++i) {
        if (!artifact_parity_run_nob_command(case_def, &case_def->nob_commands[i])) {
            if (nob_home_guard_active) test_host_env_guard_cleanup(&nob_home_guard);
            return false;
        }
    }
    if (nob_home_guard_active) {
        test_host_env_guard_cleanup(&nob_home_guard);
        nob_home_guard_active = false;
    }

    if (case_def->verify && !case_def->verify(case_def)) {
        return false;
    }

    arena = arena_create(4 * 1024 * 1024);
    if (!arena) return false;

    if (!artifact_parity_capture_manifest(arena,
                                          case_def->cmake_base_dir,
                                          case_def->manifest_requests,
                                          case_def->manifest_request_count,
                                          &cmake_manifest) ||
        !artifact_parity_capture_manifest(arena,
                                          case_def->nob_base_dir,
                                          case_def->manifest_requests,
                                          case_def->manifest_request_count,
                                          &nob_manifest) ||
        !artifact_parity_assert_equal_manifests(arena,
                                                case_def->subject ? case_def->subject : case_def->name,
                                                cmake_manifest,
                                                nob_manifest)) {
        arena_destroy(arena);
        return false;
    }

    arena_destroy(arena);
    return true;
}

static const Artifact_Parity_Manifest_Request s_build_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "generated_tree", "generated"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "generated_meta_text", "generated/meta.txt"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_FILE_SHA256, "generated_meta_sha256", "generated/meta.txt"},
};

static const Artifact_Parity_File s_build_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParity VERSION 1.2.3 LANGUAGES C)\n"
        "configure_file(meta.txt.in generated/meta.txt @ONLY)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_executable(app src/main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "meta.txt.in",
        "PROJECT=@PROJECT_NAME@\n"
        "VERSION=@PROJECT_VERSION@\n",
    },
    {
        "src/core.c",
        "int core_value(void) { return 7; }\n",
    },
    {
        "src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 7 ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_build_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL, NULL},
    {ARTIFACT_PARITY_NOB_COMMAND_CLEAN, NULL, NULL},
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_build_case = {
    .name = "build_and_generated",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_build_files,
    .file_count = NOB_ARRAY_LEN(s_build_files),
    .nob_commands = s_build_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_build_commands),
    .manifest_requests = s_build_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_build_manifest_requests),
    .cmake_build_target = "app",
    .cmake_base_dir = "cmake_build",
    .nob_base_dir = "source",
    .clean_absence_relpath = "source/artifacts/bin/app",
    .subject = "build_and_generated",
};

static const Artifact_Parity_Manifest_Request s_install_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_TREE, "install_tree", ""},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "install_notice_text", "share/NOTICE.txt"},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_SHA256, "install_notice_sha256", "share/NOTICE.txt"},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "install_demo_config_text", "lib/cmake/demo/DemoConfig.cmake"},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "install_demo_targets_text", "lib/cmake/demo/DemoTargets.cmake"},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "install_demo_targets_noconfig_text", "lib/cmake/demo/DemoTargets-noconfig.cmake"},
};

static const Artifact_Parity_File s_install_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityInstall VERSION 2.0 LANGUAGES C)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib PUBLIC_HEADER include/core.h)\n"
        "target_include_directories(core PUBLIC\n"
        "  \"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\"\n"
        "  \"$<INSTALL_INTERFACE:include/demo>\")\n"
        "add_executable(app src/main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n"
        "install(TARGETS app DESTINATION bin COMPONENT Runtime)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo COMPONENT Development)\n"
        "install(FILES assets/NOTICE.txt DESTINATION share COMPONENT Runtime)\n"
        "install(PROGRAMS scripts/run-helper.sh DESTINATION bin COMPONENT Runtime)\n"
        "install(DIRECTORY docs/ DESTINATION share/doc COMPONENT Runtime)\n"
        "install(DIRECTORY bundle DESTINATION share/tree COMPONENT Runtime)\n"
        "install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo COMPONENT Development)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT Development)\n",
    },
    {
        "src/core.c",
        "int core_value(void) { return 11; }\n",
    },
    {
        "include/core.h",
        "int core_value(void);\n",
    },
    {
        "src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 11 ? 0 : 1; }\n",
    },
    {
        "assets/NOTICE.txt",
        "Artifact parity install notice\n",
    },
    {
        "scripts/run-helper.sh",
        "#!/bin/sh\n"
        "exit 0\n",
    },
    {
        "docs/guide.txt",
        "Runtime docs\n",
    },
    {
        "bundle/readme.txt",
        "Bundled tree\n",
    },
    {
        "cmake/DemoConfig.cmake",
        "include(\"${CMAKE_CURRENT_LIST_DIR}/DemoTargets.cmake\")\n",
    },
};

static const Artifact_Parity_Nob_Command s_install_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
    {ARTIFACT_PARITY_NOB_COMMAND_INSTALL, NULL, NULL},
};

static const Artifact_Parity_Case s_install_case = {
    .name = "install_tree",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE |
              ARTIFACT_PARITY_PHASE_BUILD |
              ARTIFACT_PARITY_PHASE_INSTALL,
    .files = s_install_files,
    .file_count = NOB_ARRAY_LEN(s_install_files),
    .nob_commands = s_install_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_install_commands),
    .manifest_requests = s_install_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_install_manifest_requests),
    .cmake_build_target = "app",
    .cmake_install_prefix = "cmake_install_full",
    .nob_install_prefix = "source/custom_install",
    .cmake_base_dir = "cmake_install_full",
    .nob_base_dir = "source/custom_install",
    .clean_absence_relpath = NULL,
    .subject = "install_tree",
    .prepare = artifact_parity_prepare_install_case,
};

static const Artifact_Parity_Manifest_Request s_install_component_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_TREE, "install_tree", ""},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "install_demo_targets_text", "lib/cmake/demo/DemoTargets.cmake"},
};

static const Artifact_Parity_File s_install_component_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityInstallComponents VERSION 2.0 LANGUAGES C)\n"
        "set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Toolkit)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib PUBLIC_HEADER include/core.h)\n"
        "target_include_directories(core PUBLIC \"$<INSTALL_INTERFACE:include/demo>\")\n"
        "add_executable(app src/main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n"
        "install(TARGETS app DESTINATION bin)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo COMPONENT Development)\n"
        "install(FILES assets/runtime.txt DESTINATION share/runtime COMPONENT Runtime)\n"
        "install(FILES assets/toolkit.txt DESTINATION share/toolkit)\n"
        "install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo COMPONENT Development)\n"
        "install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT Development)\n",
    },
    {
        "src/core.c",
        "int core_value(void) { return 21; }\n",
    },
    {
        "include/core.h",
        "int core_value(void);\n",
    },
    {
        "src/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 21 ? 0 : 1; }\n",
    },
    {
        "assets/runtime.txt",
        "Runtime component\n",
    },
    {
        "assets/toolkit.txt",
        "Toolkit component\n",
    },
    {
        "cmake/DemoConfig.cmake",
        "include(\"${CMAKE_CURRENT_LIST_DIR}/DemoTargets.cmake\")\n",
    },
};

static const Artifact_Parity_Nob_Command s_install_component_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
    {ARTIFACT_PARITY_NOB_COMMAND_INSTALL, NULL, NULL},
};

static const Artifact_Parity_Case s_install_component_development_case = {
    .name = "install_component_development",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE |
              ARTIFACT_PARITY_PHASE_BUILD |
              ARTIFACT_PARITY_PHASE_INSTALL,
    .files = s_install_component_files,
    .file_count = NOB_ARRAY_LEN(s_install_component_files),
    .nob_commands = s_install_component_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_install_component_commands),
    .manifest_requests = s_install_component_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_install_component_manifest_requests),
    .source_root = "install_component_dev_source",
    .cmake_binary_dir = "install_component_dev_cmake_build",
    .nob_binary_dir = "install_component_dev_nob_build",
    .generated_nob_path = "install_component_dev_source/nob.c",
    .nob_run_dir = "install_component_dev_source",
    .cmake_build_target = "app",
    .cmake_install_prefix = "install_component_dev_cmake_prefix",
    .cmake_install_component = "Development",
    .nob_install_prefix = "install_component_dev_source/install_dev",
    .nob_install_component = "Development",
    .cmake_base_dir = "install_component_dev_cmake_prefix",
    .nob_base_dir = "install_component_dev_source/install_dev",
    .clean_absence_relpath = NULL,
    .subject = "install_component_development",
};

static const Artifact_Parity_Manifest_Request s_install_component_default_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_TREE, "install_tree", ""},
    {ARTIFACT_PARITY_DOMAIN_INSTALL_TREE, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "toolkit_text", "share/toolkit/toolkit.txt"},
};

static const Artifact_Parity_Case s_install_component_default_case = {
    .name = "install_component_default_toolkit",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE |
              ARTIFACT_PARITY_PHASE_BUILD |
              ARTIFACT_PARITY_PHASE_INSTALL,
    .files = s_install_component_files,
    .file_count = NOB_ARRAY_LEN(s_install_component_files),
    .nob_commands = s_install_component_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_install_component_commands),
    .manifest_requests = s_install_component_default_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_install_component_default_manifest_requests),
    .source_root = "install_component_default_source",
    .cmake_binary_dir = "install_component_default_cmake_build",
    .nob_binary_dir = "install_component_default_nob_build",
    .generated_nob_path = "install_component_default_source/nob.c",
    .nob_run_dir = "install_component_default_source",
    .cmake_build_target = "app",
    .cmake_install_prefix = "install_component_default_cmake_prefix",
    .cmake_install_component = "Toolkit",
    .nob_install_prefix = "install_component_default_source/install_toolkit",
    .nob_install_component = "Toolkit",
    .cmake_base_dir = "install_component_default_cmake_prefix",
    .nob_base_dir = "install_component_default_source/install_toolkit",
    .clean_absence_relpath = NULL,
    .subject = "install_component_default_toolkit",
};

static const Artifact_Parity_Manifest_Request s_empty_export_package_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "export_files", "exports"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "package_files", "packages"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "package_metadata", "packages"},
};

static const Artifact_Parity_File s_empty_export_package_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityEmpty LANGUAGES C)\n",
    },
};

static const Artifact_Parity_Nob_Command s_empty_export_package_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_PACKAGE, NULL, NULL},
};

static const Artifact_Parity_Case s_empty_export_package_case = {
    .name = "empty_export_package_domains",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE,
    .files = s_empty_export_package_files,
    .file_count = NOB_ARRAY_LEN(s_empty_export_package_files),
    .nob_commands = s_empty_export_package_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_empty_export_package_commands),
    .manifest_requests = s_empty_export_package_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_empty_export_package_manifest_requests),
    .cmake_build_target = NULL,
    .cmake_base_dir = "cmake_build",
    .nob_base_dir = "source",
    .clean_absence_relpath = NULL,
    .subject = "empty_export_package_domains",
};

static const Artifact_Parity_Manifest_Request s_export_targets_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "export_tree", "exports"},
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "core_targets_text", "exports/CoreTargets.cmake"},
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "core_targets_noconfig_text", "exports/CoreTargets-noconfig.cmake"},
};

static const Artifact_Parity_File s_export_targets_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityExportTargets LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "export(TARGETS core FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/CoreTargets.cmake NAMESPACE Demo::)\n",
    },
    {
        "core.c",
        "int core_value(void) { return 71; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_export_targets_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "core"},
    {ARTIFACT_PARITY_NOB_COMMAND_EXPORT, NULL, NULL},
};

static const Artifact_Parity_Case s_export_targets_case = {
    .name = "export_targets_build_tree",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD | ARTIFACT_PARITY_PHASE_EXPORT,
    .files = s_export_targets_files,
    .file_count = NOB_ARRAY_LEN(s_export_targets_files),
    .nob_commands = s_export_targets_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_export_targets_commands),
    .manifest_requests = s_export_targets_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_export_targets_manifest_requests),
    .source_root = "export_targets_source",
    .cmake_binary_dir = "cmake_build",
    .nob_binary_dir = "nob_build",
    .generated_nob_path = "export_targets_source/nob.c",
    .nob_run_dir = "export_targets_source",
    .cmake_build_target = "core",
    .cmake_base_dir = "cmake_build",
    .nob_base_dir = "nob_build",
    .subject = "export_targets_build_tree",
    .verify = artifact_parity_verify_export_targets_case,
};

static const Artifact_Parity_Manifest_Request s_export_set_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "export_tree", "exports"},
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "demo_targets_text", "exports/DemoTargets.cmake"},
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "demo_targets_noconfig_text", "exports/DemoTargets-noconfig.cmake"},
};

static const Artifact_Parity_File s_export_set_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityExportSet LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "install(TARGETS core EXPORT DemoTargets DESTINATION lib)\n"
        "export(EXPORT DemoTargets FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/DemoTargets.cmake NAMESPACE Demo::)\n",
    },
    {
        "core.c",
        "int core_value(void) { return 73; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_export_set_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "core"},
    {ARTIFACT_PARITY_NOB_COMMAND_EXPORT, NULL, NULL},
};

static const Artifact_Parity_Case s_export_set_case = {
    .name = "export_export_set_build_tree",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD | ARTIFACT_PARITY_PHASE_EXPORT,
    .files = s_export_set_files,
    .file_count = NOB_ARRAY_LEN(s_export_set_files),
    .nob_commands = s_export_set_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_export_set_commands),
    .manifest_requests = s_export_set_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_export_set_manifest_requests),
    .source_root = "export_set_source",
    .cmake_binary_dir = "cmake_build",
    .nob_binary_dir = "nob_build",
    .generated_nob_path = "export_set_source/nob.c",
    .nob_run_dir = "export_set_source",
    .cmake_build_target = "core",
    .cmake_base_dir = "cmake_build",
    .nob_base_dir = "nob_build",
    .subject = "export_export_set_build_tree",
    .verify = artifact_parity_verify_export_export_set_case,
};

static const Artifact_Parity_Manifest_Request s_export_package_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "registry_tree", ".cmake/packages/DemoPkg"},
};

static const Artifact_Parity_File s_export_package_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityExportPackage LANGUAGES C)\n"
        "file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/DemoPkgConfig.cmake [=[add_library(DemoPkg::core INTERFACE IMPORTED)\n]=])\n"
        "cmake_policy(SET CMP0090 NEW)\n"
        "set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)\n"
        "export(PACKAGE DemoPkg)\n",
    },
};

static const Artifact_Parity_Nob_Command s_export_package_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_EXPORT, NULL, NULL},
};

static const Artifact_Parity_Case s_export_package_case = {
    .name = "export_package_registry",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_EXPORT,
    .files = s_export_package_files,
    .file_count = NOB_ARRAY_LEN(s_export_package_files),
    .nob_commands = s_export_package_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_export_package_commands),
    .manifest_requests = s_export_package_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_export_package_manifest_requests),
    .source_root = "export_package_source",
    .cmake_binary_dir = "export_package_build",
    .nob_binary_dir = "export_package_build",
    .generated_nob_path = "export_package_source/nob.c",
    .nob_run_dir = "export_package_source",
    .cmake_home_dir = "export_pkg_cmake_home",
    .nob_home_dir = "export_pkg_nob_home",
    .cmake_base_dir = "export_pkg_cmake_home",
    .nob_base_dir = "export_pkg_nob_home",
    .subject = "export_package_registry",
    .verify = artifact_parity_verify_export_package_case,
};

static const Artifact_Parity_Manifest_Request s_package_archive_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "package_files", "packages"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "package_metadata", "packages"},
};

static const Artifact_Parity_File s_package_tgz_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityPackageTGZ LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo)\n"
        "install(FILES README.txt DESTINATION share/demo)\n"
        "install(DIRECTORY assets/ DESTINATION share/assets)\n"
        "install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)\n"
        "set(CPACK_GENERATOR \"TGZ\")\n"
        "set(CPACK_PACKAGE_NAME \"DemoPkg\")\n"
        "set(CPACK_PACKAGE_VERSION \"1.2.3\")\n"
        "set(CPACK_PACKAGE_FILE_NAME \"demo-pkg-tgz\")\n"
        "set(CPACK_PACKAGE_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}/packages\")\n"
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)\n"
        "include(CPack)\n",
    },
    {"core.c", "int core_value(void) { return 11; }\n"},
    {"include/core.h", "#define CORE_VALUE 11\n"},
    {"README.txt", "package tgz readme\n"},
    {"assets/data.txt", "asset tgz\n"},
};

static const Artifact_Parity_File s_package_txz_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityPackageTXZ LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo)\n"
        "install(FILES README.txt DESTINATION share/demo)\n"
        "install(DIRECTORY assets/ DESTINATION share/assets)\n"
        "install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)\n"
        "set(CPACK_GENERATOR \"TXZ\")\n"
        "set(CPACK_PACKAGE_NAME \"DemoPkg\")\n"
        "set(CPACK_PACKAGE_VERSION \"1.2.3\")\n"
        "set(CPACK_PACKAGE_FILE_NAME \"demo-pkg-txz\")\n"
        "set(CPACK_PACKAGE_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}/packages\")\n"
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)\n"
        "include(CPack)\n",
    },
    {"core.c", "int core_value(void) { return 13; }\n"},
    {"include/core.h", "#define CORE_VALUE 13\n"},
    {"README.txt", "package txz readme\n"},
    {"assets/data.txt", "asset txz\n"},
};

static const Artifact_Parity_File s_package_zip_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityPackageZIP LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)\n"
        "install(TARGETS core EXPORT DemoTargets ARCHIVE DESTINATION lib PUBLIC_HEADER DESTINATION include/demo)\n"
        "install(FILES README.txt DESTINATION share/demo)\n"
        "install(DIRECTORY assets/ DESTINATION share/assets)\n"
        "install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)\n"
        "set(CPACK_GENERATOR \"ZIP\")\n"
        "set(CPACK_PACKAGE_NAME \"DemoPkg\")\n"
        "set(CPACK_PACKAGE_VERSION \"1.2.3\")\n"
        "set(CPACK_PACKAGE_FILE_NAME \"demo-pkg-zip\")\n"
        "set(CPACK_PACKAGE_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}/packages\")\n"
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)\n"
        "include(CPack)\n",
    },
    {"core.c", "int core_value(void) { return 17; }\n"},
    {"include/core.h", "#define CORE_VALUE 17\n"},
    {"README.txt", "package zip readme\n"},
    {"assets/data.txt", "asset zip\n"},
};

static const Artifact_Parity_Nob_Command s_package_archive_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "core"},
    {ARTIFACT_PARITY_NOB_COMMAND_PACKAGE, NULL, NULL},
};

static const Artifact_Parity_Case s_package_tgz_case = {
    .name = "package_tgz_archive",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD | ARTIFACT_PARITY_PHASE_PACKAGE,
    .files = s_package_tgz_files,
    .file_count = NOB_ARRAY_LEN(s_package_tgz_files),
    .nob_commands = s_package_archive_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_package_archive_commands),
    .manifest_requests = s_package_archive_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_package_archive_manifest_requests),
    .source_root = "package_tgz_source",
    .cmake_binary_dir = "package_tgz_cmake_build",
    .nob_binary_dir = "package_tgz_nob_build",
    .generated_nob_path = "package_tgz_source/nob.c",
    .nob_run_dir = "package_tgz_source",
    .cmake_build_target = "core",
    .cmake_base_dir = "package_tgz_cmake_build",
    .nob_base_dir = "package_tgz_nob_build",
    .subject = "package_tgz_archive",
};

static const Artifact_Parity_Case s_package_txz_case = {
    .name = "package_txz_archive",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD | ARTIFACT_PARITY_PHASE_PACKAGE,
    .files = s_package_txz_files,
    .file_count = NOB_ARRAY_LEN(s_package_txz_files),
    .nob_commands = s_package_archive_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_package_archive_commands),
    .manifest_requests = s_package_archive_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_package_archive_manifest_requests),
    .source_root = "package_txz_source",
    .cmake_binary_dir = "package_txz_cmake_build",
    .nob_binary_dir = "package_txz_nob_build",
    .generated_nob_path = "package_txz_source/nob.c",
    .nob_run_dir = "package_txz_source",
    .cmake_build_target = "core",
    .cmake_base_dir = "package_txz_cmake_build",
    .nob_base_dir = "package_txz_nob_build",
    .subject = "package_txz_archive",
};

static const Artifact_Parity_Case s_package_zip_case = {
    .name = "package_zip_archive",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD | ARTIFACT_PARITY_PHASE_PACKAGE,
    .files = s_package_zip_files,
    .file_count = NOB_ARRAY_LEN(s_package_zip_files),
    .nob_commands = s_package_archive_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_package_archive_commands),
    .manifest_requests = s_package_archive_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_package_archive_manifest_requests),
    .source_root = "package_zip_source",
    .cmake_binary_dir = "package_zip_cmake_build",
    .nob_binary_dir = "package_zip_nob_build",
    .generated_nob_path = "package_zip_source/nob.c",
    .nob_run_dir = "package_zip_source",
    .cmake_build_target = "core",
    .cmake_base_dir = "package_zip_cmake_build",
    .nob_base_dir = "package_zip_nob_build",
    .subject = "package_zip_archive",
};

static const Artifact_Parity_Manifest_Request s_out_of_source_top_level_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
};

static const Artifact_Parity_File s_out_of_source_top_level_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP1 LANGUAGES C)\n"
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES OUTPUT_NAME corex ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(shared SHARED shared.c)\n"
        "set_target_properties(shared PROPERTIES OUTPUT_NAME sharedx LIBRARY_OUTPUT_DIRECTORY artifacts/shlib)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "core.c",
        "int core_value(void) { return 31; }\n",
    },
    {
        "shared.c",
        "int shared_value(void) { return 37; }\n",
    },
    {
        "plugin.c",
        "int plugin_value(void) { return 41; }\n",
    },
    {
        "main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 31 ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_out_of_source_top_level_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL, NULL},
};

static const Artifact_Parity_Case s_out_of_source_top_level_case = {
    .name = "out_of_source_top_level_artifacts",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_out_of_source_top_level_files,
    .file_count = NOB_ARRAY_LEN(s_out_of_source_top_level_files),
    .nob_commands = s_out_of_source_top_level_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_out_of_source_top_level_commands),
    .manifest_requests = s_out_of_source_top_level_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_out_of_source_top_level_manifest_requests),
    .source_root = "p1_top_source",
    .cmake_binary_dir = "p1_top_cmake_build",
    .nob_binary_dir = "p1_top_nob_build",
    .generated_nob_path = "p1_top_source/nob.c",
    .nob_run_dir = "p1_top_source",
    .cmake_build_target = NULL,
    .cmake_base_dir = "p1_top_cmake_build",
    .nob_base_dir = "p1_top_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "out_of_source_top_level_artifacts",
};

static const Artifact_Parity_Manifest_Request s_out_of_source_subdir_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "lib_artifacts", "lib/artifacts"},
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "app_artifacts", "app/artifacts"},
};

static const Artifact_Parity_File s_out_of_source_subdir_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP1Subdirs LANGUAGES C)\n"
        "add_subdirectory(lib)\n"
        "add_subdirectory(app)\n",
    },
    {
        "lib/CMakeLists.txt",
        "add_library(core STATIC core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_library(shared SHARED shared.c)\n"
        "set_target_properties(shared PROPERTIES OUTPUT_NAME sharedx LIBRARY_OUTPUT_DIRECTORY artifacts/shlib)\n"
        "add_library(plugin MODULE plugin.c)\n"
        "set_target_properties(plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY artifacts/modules)\n",
    },
    {
        "app/CMakeLists.txt",
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES OUTPUT_NAME runner RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "lib/core.c",
        "int core_value(void) { return 43; }\n",
    },
    {
        "lib/shared.c",
        "int shared_value(void) { return 47; }\n",
    },
    {
        "lib/plugin.c",
        "int plugin_value(void) { return 53; }\n",
    },
    {
        "app/main.c",
        "int core_value(void);\n"
        "int main(void) { return core_value() == 43 ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_out_of_source_subdir_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL, NULL},
};

static const Artifact_Parity_Case s_out_of_source_subdir_case = {
    .name = "out_of_source_subdirectory_binary_dirs",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_out_of_source_subdir_files,
    .file_count = NOB_ARRAY_LEN(s_out_of_source_subdir_files),
    .nob_commands = s_out_of_source_subdir_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_out_of_source_subdir_commands),
    .manifest_requests = s_out_of_source_subdir_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_out_of_source_subdir_manifest_requests),
    .source_root = "p1_sub_source",
    .cmake_binary_dir = "p1_sub_cmake_build",
    .nob_binary_dir = "p1_sub_nob_build",
    .generated_nob_path = "p1_sub_source/nob.c",
    .nob_run_dir = "p1_sub_source",
    .cmake_build_target = NULL,
    .cmake_base_dir = "p1_sub_cmake_build",
    .nob_base_dir = "p1_sub_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "out_of_source_subdirectory_binary_dirs",
};

static const Artifact_Parity_Manifest_Request s_p2_generated_source_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "generated_tree", "generated"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_FILE_SHA256, "generated_c_sha256", "generated/generated.c"},
};

static const Artifact_Parity_File s_p2_generated_source_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP2Generated LANGUAGES C)\n"
        "add_custom_command(\n"
        "  OUTPUT generated/generated.c generated/generated.h\n"
        "  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/src/template_generated.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/src/template_generated.h ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.h\n"
        "  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.log\n"
        "  DEPENDS src/template_generated.c src/template_generated.h\n"
        "  BYPRODUCTS generated/generated.log)\n"
        "add_executable(app src/main.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c)\n"
        "target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "src/template_generated.c",
        "#include \"generated.h\"\n"
        "int generated_value(void) { return GENERATED_VALUE; }\n",
    },
    {
        "src/template_generated.h",
        "#define GENERATED_VALUE 61\n",
    },
    {
        "src/main.c",
        "int generated_value(void);\n"
        "int main(void) { return generated_value() == 61 ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_p2_generated_source_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_p2_generated_source_case = {
    .name = "p2_generated_source_consumer",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p2_generated_source_files,
    .file_count = NOB_ARRAY_LEN(s_p2_generated_source_files),
    .nob_commands = s_p2_generated_source_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p2_generated_source_commands),
    .manifest_requests = s_p2_generated_source_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p2_generated_source_manifest_requests),
    .source_root = "p2_gen_source",
    .cmake_binary_dir = "p2_gen_cmake_build",
    .nob_binary_dir = "p2_gen_nob_build",
    .generated_nob_path = "p2_gen_nob.c",
    .nob_run_dir = ".",
    .cmake_build_target = "app",
    .cmake_base_dir = "p2_gen_cmake_build",
    .nob_base_dir = "p2_gen_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "p2_generated_source_consumer",
};

static const Artifact_Parity_Manifest_Request s_p2_custom_target_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "generated_tree", "generated"},
    {ARTIFACT_PARITY_DOMAIN_GENERATED_FILES, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "prepared_text", "generated/prepared.txt"},
};

static const Artifact_Parity_File s_p2_custom_target_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP2CustomTarget LANGUAGES C)\n"
        "add_custom_target(prepare\n"
        "  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/src/prepared.txt.in ${CMAKE_CURRENT_BINARY_DIR}/generated/prepared.txt)\n"
        "add_executable(app src/main.c)\n"
        "add_dependencies(app prepare)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "src/prepared.txt.in",
        "ready",
    },
    {
        "src/main.c",
        "int main(void) { return 0; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_p2_custom_target_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_p2_custom_target_case = {
    .name = "p2_custom_target_dependency",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p2_custom_target_files,
    .file_count = NOB_ARRAY_LEN(s_p2_custom_target_files),
    .nob_commands = s_p2_custom_target_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p2_custom_target_commands),
    .manifest_requests = s_p2_custom_target_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p2_custom_target_manifest_requests),
    .source_root = "p2_custom_source",
    .cmake_binary_dir = "p2_custom_cmake_build",
    .nob_binary_dir = "p2_custom_nob_build",
    .generated_nob_path = "p2_custom_nob.c",
    .nob_run_dir = ".",
    .cmake_build_target = "app",
    .cmake_base_dir = "p2_custom_cmake_build",
    .nob_base_dir = "p2_custom_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "p2_custom_target_dependency",
};

static const Artifact_Parity_Manifest_Request s_p2_post_build_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "post_text", "artifacts/sidecar/post.txt"},
};

static const Artifact_Parity_File s_p2_post_build_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP2PostBuild LANGUAGES C)\n"
        "add_executable(app src/main.c)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n"
        "add_custom_command(TARGET app POST_BUILD\n"
        "  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/artifacts/sidecar\n"
        "  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/src/post.txt.in ${CMAKE_CURRENT_BINARY_DIR}/artifacts/sidecar/post.txt\n"
        "  BYPRODUCTS artifacts/sidecar/post.txt)\n",
    },
    {
        "src/post.txt.in",
        "post",
    },
    {
        "src/main.c",
        "int main(void) { return 0; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_p2_post_build_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_p2_post_build_case = {
    .name = "p2_post_build_sidecar",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p2_post_build_files,
    .file_count = NOB_ARRAY_LEN(s_p2_post_build_files),
    .nob_commands = s_p2_post_build_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p2_post_build_commands),
    .manifest_requests = s_p2_post_build_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p2_post_build_manifest_requests),
    .source_root = "p2_post_source",
    .cmake_binary_dir = "p2_post_cmake_build",
    .nob_binary_dir = "p2_post_nob_build",
    .generated_nob_path = "p2_post_nob.c",
    .nob_run_dir = ".",
    .cmake_build_target = "app",
    .cmake_base_dir = "p2_post_cmake_build",
    .nob_base_dir = "p2_post_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "p2_post_build_sidecar",
};

static bool artifact_parity_prepare_p3_imported_case(const Artifact_Parity_Case *case_def) {
    const char *source_root = artifact_parity_case_source_root(case_def);
    return artifact_parity_build_archive_in_dir(source_root,
                                                "cc",
                                                "imports/ext_static.c",
                                                "imports/libext_static.a") &&
           artifact_parity_build_archive_in_dir(source_root,
                                                "cc",
                                                "imports/ext_unknown.c",
                                                "imports/libext_unknown.a");
}

static bool artifact_parity_prepare_p3_debug_config_case(const Artifact_Parity_Case *case_def) {
    const char *source_root = artifact_parity_case_source_root(case_def);
    return artifact_parity_build_archive_in_dir(source_root,
                                                "cc",
                                                "imports/opt.c",
                                                "imports/libopt.a") &&
           artifact_parity_build_archive_in_dir(source_root,
                                                "cc",
                                                "imports/dbg.c",
                                                "imports/libdbg.a");
}

static const Artifact_Parity_Manifest_Request s_p3_build_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_BUILD_OUTPUTS, ARTIFACT_PARITY_CAPTURE_TREE, "build_outputs", "artifacts"},
};

static const Artifact_Parity_File s_p3_imported_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP3Imported LANGUAGES C)\n"
        "add_library(ext_static STATIC IMPORTED)\n"
        "set_target_properties(ext_static PROPERTIES IMPORTED_LOCATION imports/libext_static.a)\n"
        "add_library(ext_unknown UNKNOWN IMPORTED)\n"
        "set_target_properties(ext_unknown PROPERTIES IMPORTED_LOCATION imports/libext_unknown.a)\n"
        "add_library(ext_iface INTERFACE IMPORTED)\n"
        "set_target_properties(ext_iface PROPERTIES\n"
        "  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/imports/include\n"
        "  INTERFACE_COMPILE_DEFINITIONS IFACE_EXPECT=23)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE ext_static ext_unknown ext_iface)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "imports/ext_static.c",
        "int ext_static(void) { return 3; }\n",
    },
    {
        "imports/ext_unknown.c",
        "int ext_unknown(void) { return 11; }\n",
    },
    {
        "imports/include/iface.h",
        "#ifndef IFACE_EXPECT\n"
        "#error IFACE_EXPECT missing\n"
        "#endif\n"
        "#if IFACE_EXPECT != 23\n"
        "#error IFACE_EXPECT mismatch\n"
        "#endif\n",
    },
    {
        "main.c",
        "#include \"iface.h\"\n"
        "int ext_static(void);\n"
        "int ext_unknown(void);\n"
        "int main(void) { return (ext_static() + ext_unknown()) == 14 ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_p3_imported_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_p3_imported_case = {
    .name = "p3_imported_prebuilt_libraries",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p3_imported_files,
    .file_count = NOB_ARRAY_LEN(s_p3_imported_files),
    .nob_commands = s_p3_imported_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p3_imported_commands),
    .manifest_requests = s_p3_build_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p3_build_manifest_requests),
    .source_root = "p3_imported_source",
    .cmake_binary_dir = "p3_imported_source",
    .nob_binary_dir = "p3_imported_source",
    .generated_nob_path = "p3_imported_source/nob.c",
    .nob_run_dir = "p3_imported_source",
    .cmake_build_target = "app",
    .cmake_base_dir = "p3_imported_source",
    .nob_base_dir = "p3_imported_source",
    .clean_absence_relpath = NULL,
    .subject = "p3_imported_prebuilt_libraries",
    .prepare = artifact_parity_prepare_p3_imported_case,
};

static const Artifact_Parity_File s_p3_debug_config_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP3Config LANGUAGES C)\n"
        "add_library(opt STATIC IMPORTED)\n"
        "set_target_properties(opt PROPERTIES IMPORTED_LOCATION imports/libopt.a)\n"
        "add_library(dbg STATIC IMPORTED)\n"
        "set_target_properties(dbg PROPERTIES IMPORTED_LOCATION imports/libdbg.a)\n"
        "add_executable(app main.c)\n"
        "target_link_libraries(app PRIVATE optimized opt debug dbg)\n"
        "target_compile_definitions(app PRIVATE\n"
        "  \"$<$<CONFIG:Debug>:EXPECTED_VALUE=23>\"\n"
        "  \"$<$<CONFIG:Debug>:SELECTED_FN=selected_value_23>\"\n"
        "  \"$<$<NOT:$<CONFIG:Debug>>:EXPECTED_VALUE=17>\"\n"
        "  \"$<$<NOT:$<CONFIG:Debug>>:SELECTED_FN=selected_value_17>\")\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "imports/opt.c",
        "int selected_value_17(void) { return 17; }\n",
    },
    {
        "imports/dbg.c",
        "int selected_value_23(void) { return 23; }\n",
    },
    {
        "main.c",
        "#ifndef EXPECTED_VALUE\n"
        "#error EXPECTED_VALUE missing\n"
        "#endif\n"
        "#ifndef SELECTED_FN\n"
        "#error SELECTED_FN missing\n"
        "#endif\n"
        "int SELECTED_FN(void);\n"
        "int main(void) { return SELECTED_FN() == EXPECTED_VALUE ? 0 : 1; }\n",
    },
};

static const Artifact_Parity_Nob_Command s_p3_debug_config_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "Debug", "app"},
};

static const Artifact_Parity_Case s_p3_debug_config_case = {
    .name = "p3_debug_config_link_selection",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p3_debug_config_files,
    .file_count = NOB_ARRAY_LEN(s_p3_debug_config_files),
    .nob_commands = s_p3_debug_config_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p3_debug_config_commands),
    .manifest_requests = s_p3_build_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p3_build_manifest_requests),
    .source_root = "p3_config_source",
    .cmake_binary_dir = "p3_config_source",
    .nob_binary_dir = "p3_config_source",
    .generated_nob_path = "p3_config_source/nob.c",
    .nob_run_dir = "p3_config_source",
    .cmake_build_target = "app",
    .cmake_build_type = "Debug",
    .cmake_base_dir = "p3_config_source",
    .nob_base_dir = "p3_config_source",
    .clean_absence_relpath = NULL,
    .subject = "p3_debug_config_link_selection",
    .prepare = artifact_parity_prepare_p3_debug_config_case,
};

static const Artifact_Parity_File s_p3_usage_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityP3Usage LANGUAGES C)\n"
        "add_library(iface INTERFACE)\n"
        "set_target_properties(iface PROPERTIES CUSTOM_TAG USAGE_EXPECT=29)\n"
        "target_include_directories(iface INTERFACE\n"
        "  \"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/iface_build/include>\"\n"
        "  \"$<INSTALL_INTERFACE:iface_install/include>\")\n"
        "target_link_libraries(iface INTERFACE \"$<LINK_ONLY:m>\")\n"
        "add_executable(app main.c)\n"
        "target_compile_options(app PRIVATE -fno-builtin)\n"
        "target_link_libraries(app PRIVATE iface)\n"
        "target_compile_definitions(app PRIVATE \"$<TARGET_PROPERTY:iface,CUSTOM_TAG>\")\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n",
    },
    {
        "iface_build/include/iface.h",
        "#ifndef USAGE_EXPECT\n"
        "#error USAGE_EXPECT missing\n"
        "#endif\n"
        "#if USAGE_EXPECT != 29\n"
        "#error USAGE_EXPECT mismatch\n"
        "#endif\n",
    },
    {
        "main.c",
        "#include \"iface.h\"\n"
        "#include <math.h>\n"
        "int main(void) {\n"
        "    volatile double value = 0.0;\n"
        "    return cos(value) == 1.0 ? 0 : 1;\n"
        "}\n",
    },
};

static const Artifact_Parity_Nob_Command s_p3_usage_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, NULL, "app"},
};

static const Artifact_Parity_Case s_p3_usage_case = {
    .name = "p3_usage_requirement_propagation",
    .phases = ARTIFACT_PARITY_PHASE_CONFIGURE | ARTIFACT_PARITY_PHASE_BUILD,
    .files = s_p3_usage_files,
    .file_count = NOB_ARRAY_LEN(s_p3_usage_files),
    .nob_commands = s_p3_usage_commands,
    .nob_command_count = NOB_ARRAY_LEN(s_p3_usage_commands),
    .manifest_requests = s_p3_build_manifest_requests,
    .manifest_request_count = NOB_ARRAY_LEN(s_p3_build_manifest_requests),
    .source_root = "p3_usage_source",
    .cmake_binary_dir = "p3_usage_cmake_build",
    .nob_binary_dir = "p3_usage_nob_build",
    .generated_nob_path = "p3_usage_nob.c",
    .nob_run_dir = ".",
    .cmake_build_target = "app",
    .cmake_base_dir = "p3_usage_cmake_build",
    .nob_base_dir = "p3_usage_nob_build",
    .clean_absence_relpath = NULL,
    .subject = "p3_usage_requirement_propagation",
};

TEST(artifact_parity_build_and_generated_manifest_matches_cmake_via_nobify) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_build_case));
    TEST_PASS();
}

TEST(artifact_parity_install_tree_matches_cmake_for_files_and_targets) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_install_case));
    TEST_PASS();
}

TEST(artifact_parity_install_component_development_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_install_component_development_case));
    TEST_PASS();
}

TEST(artifact_parity_install_component_default_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_install_component_default_case));
    TEST_PASS();
}

TEST(artifact_parity_emits_empty_export_and_package_manifest_sections) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_empty_export_package_case));
    TEST_PASS();
}

TEST(artifact_parity_export_targets_build_tree_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_export_targets_case));
    TEST_PASS();
}

TEST(artifact_parity_export_export_set_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_export_set_case));
    TEST_PASS();
}

TEST(artifact_parity_export_package_registry_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_export_package_case));
    TEST_PASS();
}

TEST(artifact_parity_package_tgz_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_package_tgz_case));
    TEST_PASS();
}

TEST(artifact_parity_package_txz_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_package_txz_case));
    TEST_PASS();
}

TEST(artifact_parity_package_zip_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_package_zip_case));
    TEST_PASS();
}

TEST(artifact_parity_out_of_source_top_level_build_outputs_match_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_out_of_source_top_level_case));
    TEST_PASS();
}

TEST(artifact_parity_out_of_source_subdirectory_build_outputs_match_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_out_of_source_subdir_case));
    TEST_PASS();
}

TEST(artifact_parity_generated_source_consumer_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p2_generated_source_case));
    TEST_PASS();
}

TEST(artifact_parity_custom_target_dependency_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p2_custom_target_case));
    TEST_PASS();
}

TEST(artifact_parity_post_build_sidecar_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p2_post_build_case));
    TEST_PASS();
}

TEST(artifact_parity_imported_prebuilt_libraries_match_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p3_imported_case));
    TEST_PASS();
}

TEST(artifact_parity_debug_config_link_selection_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p3_debug_config_case));
    TEST_PASS();
}

TEST(artifact_parity_usage_requirement_propagation_matches_cmake) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_p3_usage_case));
    TEST_PASS();
}

TEST(artifact_parity_skips_when_cmake_env_points_to_missing_binary) {
    Test_Host_Env_Guard *env_guard = NULL;
    Artifact_Parity_Cmake_Config config = {0};
    char skip_reason[256] = {0};

    ASSERT(test_host_env_guard_begin_heap(&env_guard,
                                          CMK2NOB_TEST_CMAKE_BIN_ENV,
                                          "./missing-cmake-for-artifact-parity"));
    TEST_DEFER(test_host_env_guard_cleanup, env_guard);

    ASSERT(artifact_parity_resolve_cmake(&config, false, skip_reason));
    ASSERT(!config.available);
    ASSERT(strstr(skip_reason, CMK2NOB_TEST_CMAKE_BIN_ENV) != NULL);
    TEST_PASS();
}

TEST(artifact_parity_skips_when_cmake_version_is_not_3_28) {
#if defined(_WIN32)
    TEST_SKIP("fake cmake version probe is POSIX-only");
#else
    Test_Host_Env_Guard *env_guard = NULL;
    Artifact_Parity_Cmake_Config config = {0};
    char skip_reason[256] = {0};

    ASSERT(artifact_parity_write_executable_file(
        "fake_bin/cmake",
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.27.9\\n'\n"
        "  exit 0\n"
        "fi\n"
        "exit 0\n"));
    ASSERT(test_host_env_guard_begin_heap(&env_guard,
                                          CMK2NOB_TEST_CMAKE_BIN_ENV,
                                          "fake_bin/cmake"));
    TEST_DEFER(test_host_env_guard_cleanup, env_guard);

    ASSERT(artifact_parity_resolve_cmake(&config, false, skip_reason));
    ASSERT(!config.available);
    ASSERT(strstr(skip_reason, "3.28") != NULL);
    TEST_PASS();
#endif
}

TEST(artifact_parity_skips_when_cpack_is_missing_for_package_phase) {
#if defined(_WIN32)
    TEST_SKIP("fake cpack sibling probe is POSIX-only");
#else
    Test_Host_Env_Guard *env_guard = NULL;
    Artifact_Parity_Cmake_Config config = {0};
    char skip_reason[256] = {0};

    ASSERT(artifact_parity_write_executable_file(
        "fake_pkg_bin/cmake",
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--version\" ]; then\n"
        "  printf 'cmake version 3.28.6\\n'\n"
        "  exit 0\n"
        "fi\n"
        "exit 0\n"));
    ASSERT(test_host_env_guard_begin_heap(&env_guard,
                                          CMK2NOB_TEST_CMAKE_BIN_ENV,
                                          "fake_pkg_bin/cmake"));
    TEST_DEFER(test_host_env_guard_cleanup, env_guard);

    ASSERT(artifact_parity_resolve_cmake(&config, true, skip_reason));
    ASSERT(config.available);
    ASSERT(!config.cpack_available);
    ASSERT(strstr(skip_reason, "cpack") != NULL);
    TEST_PASS();
#endif
}

TEST(artifact_parity_manifest_mismatch_reporting_returns_false) {
    Arena *arena = arena_create(64 * 1024);
    ASSERT(arena != NULL);
    ASSERT(!artifact_parity_assert_equal_manifests(arena,
                                                   "mismatch_probe",
                                                   nob_sv_from_cstr("cmake-manifest\n"),
                                                   nob_sv_from_cstr("nob-manifest\n")));
    arena_destroy(arena);
    TEST_PASS();
}

TEST(artifact_parity_generated_nob_uses_embedded_cmake_without_path_injection) {
#if defined(_WIN32)
    TEST_SKIP("tool-only PATH probe is POSIX-only");
#else
    Test_Host_Env_Guard *path_guard = NULL;
    const char *tool_only_path_abs = nob_temp_sprintf("%s/tool_only_path", nob_get_current_dir_temp());
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake not available for embedded runtime probe");
    }
    ASSERT(s_artifact_parity_nobify_bin[0] != '\0');
    ASSERT(artifact_parity_materialize_files("source",
                                             (Artifact_Parity_File[]){
                                                 {
                                                     "CMakeLists.txt",
                                                     "cmake_minimum_required(VERSION 3.28)\n"
                                                     "project(EmbeddedCMake LANGUAGES C)\n"
                                                     "add_custom_command(\n"
                                                     "  OUTPUT generated/generated.c generated/generated.h\n"
                                                     "  COMMAND cmake -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated\n"
                                                     "  COMMAND cmake -E copy_if_different template_generated.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c\n"
                                                     "  COMMAND cmake -E copy_if_different template_generated.h ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.h)\n"
                                                     "add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c)\n"
                                                     "target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)\n",
                                                 },
                                                 {
                                                     "template_generated.c",
                                                     "#include \"generated.h\"\n"
                                                     "int generated_value(void) { return GENERATED_VALUE; }\n",
                                                 },
                                                 {
                                                     "template_generated.h",
                                                     "#define GENERATED_VALUE 37\n",
                                                 },
                                                 {
                                                     "main.c",
                                                     "int generated_value(void);\n"
                                                     "int main(void) { return generated_value() == 37 ? 0 : 1; }\n",
                                                 },
                                             },
                                             4));
    ASSERT(artifact_parity_run_nobify(s_artifact_parity_nobify_bin,
                                      nob_temp_sprintf("%s/source/CMakeLists.txt", nob_get_current_dir_temp()),
                                      nob_temp_sprintf("%s/source/nob.c", nob_get_current_dir_temp()),
                                      nob_temp_sprintf("%s/source", nob_get_current_dir_temp()),
                                      nob_temp_sprintf("%s/nob_build", nob_get_current_dir_temp())));
    ASSERT(artifact_parity_compile_generated_nob("source/nob.c", "source/nob_gen"));
    ASSERT(artifact_parity_make_tool_only_path_dir("tool_only_path"));
    ASSERT(test_host_env_guard_begin_heap(&path_guard, "PATH", tool_only_path_abs));
    TEST_DEFER(test_host_env_guard_cleanup, path_guard);
    ASSERT(artifact_parity_run_binary_in_dir("source", "./nob_gen", "app", NULL));
    ASSERT(test_ws_host_path_exists("nob_build/app"));
    ASSERT(test_ws_host_path_exists("nob_build/generated/generated.c"));
    ASSERT(test_ws_host_path_exists("nob_build/generated/generated.h"));
    TEST_PASS();
#endif
}

void run_artifact_parity_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char repo_root[_TINYDIR_PATH_MAX] = {0};
    const char *repo_root_env = getenv(CMK2NOB_TEST_REPO_ROOT_ENV);
    bool prepared = test_ws_prepare(&ws, "artifact-parity");
    bool entered = false;

    s_artifact_parity_cmake = (Artifact_Parity_Cmake_Config){0};
    s_artifact_parity_skip_reason[0] = '\0';
    s_artifact_parity_nobify_bin[0] = '\0';
    s_artifact_parity_nobify_error[0] = '\0';

    if (!prepared) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to enter isolated workspace");
        (void)test_ws_cleanup(&ws);
        if (failed) (*failed)++;
        return;
    }

    snprintf(repo_root, sizeof(repo_root), "%s", repo_root_env ? repo_root_env : "");
    artifact_parity_test_set_repo_root(repo_root);

    if (!artifact_parity_resolve_cmake(&s_artifact_parity_cmake,
                                       false,
                                       s_artifact_parity_skip_reason)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to resolve cmake");
        if (failed) (*failed)++;
    }

    if (!artifact_parity_resolve_nobify_bin(s_artifact_parity_nobify_bin,
                                            s_artifact_parity_nobify_error)) {
        nob_log(NOB_ERROR, "artifact parity suite: %s", s_artifact_parity_nobify_error);
        if (failed) (*failed)++;
    } else {
        test_artifact_parity_build_and_generated_manifest_matches_cmake_via_nobify(passed, failed, skipped);
        test_artifact_parity_install_tree_matches_cmake_for_files_and_targets(passed, failed, skipped);
        test_artifact_parity_install_component_development_matches_cmake(passed, failed, skipped);
        test_artifact_parity_install_component_default_matches_cmake(passed, failed, skipped);
        test_artifact_parity_emits_empty_export_and_package_manifest_sections(passed, failed, skipped);
        test_artifact_parity_export_targets_build_tree_matches_cmake(passed, failed, skipped);
        test_artifact_parity_export_export_set_matches_cmake(passed, failed, skipped);
        test_artifact_parity_export_package_registry_matches_cmake(passed, failed, skipped);
        test_artifact_parity_package_tgz_matches_cmake(passed, failed, skipped);
        test_artifact_parity_package_txz_matches_cmake(passed, failed, skipped);
        test_artifact_parity_package_zip_matches_cmake(passed, failed, skipped);
        test_artifact_parity_out_of_source_top_level_build_outputs_match_cmake(passed, failed, skipped);
        test_artifact_parity_out_of_source_subdirectory_build_outputs_match_cmake(passed, failed, skipped);
        test_artifact_parity_generated_source_consumer_matches_cmake(passed, failed, skipped);
        test_artifact_parity_custom_target_dependency_matches_cmake(passed, failed, skipped);
        test_artifact_parity_post_build_sidecar_matches_cmake(passed, failed, skipped);
        test_artifact_parity_imported_prebuilt_libraries_match_cmake(passed, failed, skipped);
        test_artifact_parity_debug_config_link_selection_matches_cmake(passed, failed, skipped);
        test_artifact_parity_usage_requirement_propagation_matches_cmake(passed, failed, skipped);
    }

    test_artifact_parity_skips_when_cmake_env_points_to_missing_binary(passed, failed, skipped);
    test_artifact_parity_skips_when_cmake_version_is_not_3_28(passed, failed, skipped);
    test_artifact_parity_skips_when_cpack_is_missing_for_package_phase(passed, failed, skipped);
    test_artifact_parity_manifest_mismatch_reporting_returns_false(passed, failed, skipped);
    test_artifact_parity_generated_nob_uses_embedded_cmake_without_path_injection(passed, failed, skipped);

    if (!test_ws_leave(prev_cwd)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to restore cwd");
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "artifact parity suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
