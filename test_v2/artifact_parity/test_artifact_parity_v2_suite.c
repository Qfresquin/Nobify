#include "test_artifact_parity_v2_support.h"

#include "test_fs.h"
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

typedef struct {
    char name[64];
    char *prev_value;
    bool had_prev_value;
} Artifact_Parity_Env_Guard;

static void artifact_parity_set_env_or_unset(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

static bool artifact_parity_env_guard_set(Artifact_Parity_Env_Guard *guard,
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
    artifact_parity_set_env_or_unset(name, value);
    return true;
}

static void artifact_parity_env_guard_cleanup(void *ctx) {
    Artifact_Parity_Env_Guard *guard = (Artifact_Parity_Env_Guard*)ctx;
    if (!guard) return;
    artifact_parity_set_env_or_unset(guard->name,
                                     guard->had_prev_value ? guard->prev_value : NULL);
    free(guard->prev_value);
    guard->prev_value = NULL;
}

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

static bool artifact_parity_run_nob_command(const Artifact_Parity_Case *case_def,
                                            const Artifact_Parity_Nob_Command *command) {
    const char *run_dir = artifact_parity_case_nob_run_dir(case_def);
    if (!case_def || !command) return false;

    switch (command->kind) {
        case ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT:
            return artifact_parity_run_binary_in_dir(run_dir, "./nob_gen", NULL, NULL);

        case ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET:
            return artifact_parity_run_binary_in_dir(run_dir, "./nob_gen", command->arg, NULL);

        case ARTIFACT_PARITY_NOB_COMMAND_CLEAN:
            if (!artifact_parity_run_binary_in_dir(run_dir, "./nob_gen", "clean", NULL)) return false;
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
            return artifact_parity_run_binary_in_dir(run_dir, "./nob_gen", "install", NULL);

        case ARTIFACT_PARITY_NOB_COMMAND_PACKAGE:
            return artifact_parity_run_binary_in_dir(run_dir, "./nob_gen", "package", NULL);
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

    if (!case_def) return false;
    if (!cwd) return false;
    source_root = artifact_parity_case_source_root(case_def);
    cmake_binary_dir = artifact_parity_case_cmake_binary_dir(case_def);
    nob_binary_dir = artifact_parity_case_nob_binary_dir(case_def);
    generated_nob_path = artifact_parity_case_generated_nob_path(case_def);
    generated_nob_bin_path = artifact_parity_case_generated_nob_bin_path(case_def);
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

    if ((case_def->phases & ARTIFACT_PARITY_PHASE_CONFIGURE) ||
        (case_def->phases & ARTIFACT_PARITY_PHASE_BUILD) ||
        (case_def->phases & ARTIFACT_PARITY_PHASE_INSTALL)) {
        if (!artifact_parity_run_cmake_configure(&s_artifact_parity_cmake,
                                                 source_root,
                                                 cmake_binary_dir)) {
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_BUILD) {
        if (!artifact_parity_run_cmake_build(&s_artifact_parity_cmake,
                                             cmake_binary_dir,
                                             case_def->cmake_build_target)) {
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_INSTALL) {
        if (!artifact_parity_run_cmake_install(&s_artifact_parity_cmake,
                                               cmake_binary_dir,
                                               "cmake_install")) {
            return false;
        }
    }
    if (case_def->phases & ARTIFACT_PARITY_PHASE_PACKAGE) {
        Artifact_Parity_Cmake_Config package_config = {0};
        char package_skip_reason[256] = {0};
        if (!artifact_parity_resolve_cmake(&package_config, true, package_skip_reason)) return false;
        if (!package_config.cpack_available) {
            nob_log(NOB_INFO,
                    "artifact parity case %s skipped package phase: %s",
                    case_def->name,
                    package_skip_reason[0] ? package_skip_reason : "cpack is unavailable");
            return true;
        }
        if (!artifact_parity_run_cmake_package(&package_config, cmake_binary_dir)) return false;
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

    for (size_t i = 0; i < case_def->nob_command_count; ++i) {
        if (!artifact_parity_run_nob_command(case_def, &case_def->nob_commands[i])) return false;
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL},
    {ARTIFACT_PARITY_NOB_COMMAND_CLEAN, NULL},
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "app"},
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
};

static const Artifact_Parity_File s_install_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityInstall VERSION 2.0 LANGUAGES C)\n"
        "add_library(core STATIC src/core.c)\n"
        "set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY artifacts/lib)\n"
        "add_executable(app src/main.c)\n"
        "target_link_libraries(app PRIVATE core)\n"
        "set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY artifacts/bin)\n"
        "install(TARGETS app DESTINATION bin)\n"
        "install(TARGETS core DESTINATION lib)\n"
        "install(FILES assets/NOTICE.txt DESTINATION share)\n",
    },
    {
        "src/core.c",
        "int core_value(void) { return 11; }\n",
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
};

static const Artifact_Parity_Nob_Command s_install_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "app"},
    {ARTIFACT_PARITY_NOB_COMMAND_INSTALL, NULL},
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
    .cmake_base_dir = "cmake_install",
    .nob_base_dir = "source/install",
    .clean_absence_relpath = NULL,
    .subject = "install_tree",
};

static const Artifact_Parity_Manifest_Request s_empty_export_package_manifest_requests[] = {
    {ARTIFACT_PARITY_DOMAIN_EXPORT_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "export_files", "exports"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_FILES, ARTIFACT_PARITY_CAPTURE_TREE, "package_files", "packages"},
    {ARTIFACT_PARITY_DOMAIN_PACKAGE_METADATA, ARTIFACT_PARITY_CAPTURE_FILE_TEXT, "package_metadata", "package_metadata.txt"},
};

static const Artifact_Parity_File s_empty_export_package_files[] = {
    {
        "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(ArtifactParityEmpty LANGUAGES C)\n",
    },
};

static const Artifact_Parity_Nob_Command s_empty_export_package_commands[] = {
    {ARTIFACT_PARITY_NOB_COMMAND_PACKAGE, NULL},
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL},
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_DEFAULT, NULL},
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "app"},
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "app"},
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
    {ARTIFACT_PARITY_NOB_COMMAND_BUILD_TARGET, "app"},
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

TEST(artifact_parity_emits_empty_export_and_package_manifest_sections) {
    if (!s_artifact_parity_cmake.available) {
        TEST_SKIP(s_artifact_parity_skip_reason[0]
                      ? s_artifact_parity_skip_reason
                      : "cmake 3.28.x is not available");
    }

    ASSERT(artifact_parity_run_case(&s_empty_export_package_case));
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

TEST(artifact_parity_skips_when_cmake_env_points_to_missing_binary) {
    Artifact_Parity_Env_Guard env_guard = {0};
    Artifact_Parity_Cmake_Config config = {0};
    char skip_reason[256] = {0};

    ASSERT(artifact_parity_env_guard_set(&env_guard,
                                         CMK2NOB_TEST_CMAKE_BIN_ENV,
                                         "./missing-cmake-for-artifact-parity"));
    TEST_DEFER(artifact_parity_env_guard_cleanup, &env_guard);

    ASSERT(artifact_parity_resolve_cmake(&config, false, skip_reason));
    ASSERT(!config.available);
    ASSERT(strstr(skip_reason, CMK2NOB_TEST_CMAKE_BIN_ENV) != NULL);
    TEST_PASS();
}

TEST(artifact_parity_skips_when_cmake_version_is_not_3_28) {
#if defined(_WIN32)
    TEST_SKIP("fake cmake version probe is POSIX-only");
#else
    Artifact_Parity_Env_Guard env_guard = {0};
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
    ASSERT(artifact_parity_env_guard_set(&env_guard,
                                         CMK2NOB_TEST_CMAKE_BIN_ENV,
                                         "fake_bin/cmake"));
    TEST_DEFER(artifact_parity_env_guard_cleanup, &env_guard);

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
    Artifact_Parity_Env_Guard env_guard = {0};
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
    ASSERT(artifact_parity_env_guard_set(&env_guard,
                                         CMK2NOB_TEST_CMAKE_BIN_ENV,
                                         "fake_pkg_bin/cmake"));
    TEST_DEFER(artifact_parity_env_guard_cleanup, &env_guard);

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
    Artifact_Parity_Env_Guard path_guard = {0};
    const char *tool_only_path_abs = nob_temp_sprintf("%s/tool_only_path", nob_get_current_dir_temp());
    ASSERT(s_artifact_parity_cmake.available);
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
    ASSERT(artifact_parity_env_guard_set(&path_guard, "PATH", tool_only_path_abs));
    TEST_DEFER(artifact_parity_env_guard_cleanup, &path_guard);
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
        test_artifact_parity_emits_empty_export_and_package_manifest_sections(passed, failed, skipped);
        test_artifact_parity_out_of_source_top_level_build_outputs_match_cmake(passed, failed, skipped);
        test_artifact_parity_out_of_source_subdirectory_build_outputs_match_cmake(passed, failed, skipped);
        test_artifact_parity_generated_source_consumer_matches_cmake(passed, failed, skipped);
        test_artifact_parity_custom_target_dependency_matches_cmake(passed, failed, skipped);
        test_artifact_parity_post_build_sidecar_matches_cmake(passed, failed, skipped);
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
