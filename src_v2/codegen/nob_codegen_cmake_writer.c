#include "nob_codegen_internal.h"

bool cg_target_export_name(CG_Context *ctx, BM_Target_Id id, String_View *out) {
    String_View export_name = {0};
    if (!ctx || !out) return false;
    *out = bm_query_target_name(ctx->model, id);
    if (!bm_query_target_modeled_property_value(ctx->model,
                                                id,
                                                nob_sv_from_cstr("EXPORT_NAME"),
                                                ctx->scratch,
                                                &export_name)) {
        return false;
    }
    if (export_name.count > 0) *out = export_name;
    return true;
}

bool cg_target_exported_name(CG_Context *ctx,
                             BM_Target_Id id,
                             String_View export_namespace,
                             String_View *out) {
    Nob_String_Builder sb = {0};
    String_View export_name = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (!cg_target_export_name(ctx, id, &export_name)) return false;
    nob_sb_append_buf(&sb, export_namespace.data ? export_namespace.data : "", export_namespace.count);
    nob_sb_append_buf(&sb, export_name.data ? export_name.data : "", export_name.count);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_cmake_append_escaped(Nob_String_Builder *sb, String_View value) {
    if (!sb) return false;
    for (size_t i = 0; i < value.count; ++i) {
        char c = value.data ? value.data[i] : '\0';
        if (c == '\\') {
            nob_sb_append_cstr(sb, "/");
        } else if (c == '"') {
            nob_sb_append_cstr(sb, "\\\"");
        } else {
            nob_sb_append_buf(sb, &c, 1);
        }
    }
    return true;
}

bool cg_join_sv_list(Arena *scratch, String_View *items, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;
    *out = nob_sv_from_cstr("");
    for (size_t i = 0; i < arena_arr_len(items); ++i) {
        if (items[i].count == 0) continue;
        if (sb.count > 0) nob_sb_append_cstr(&sb, ";");
        nob_sb_append_buf(&sb, items[i].data ? items[i].data : "", items[i].count);
    }
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_export_noconfig_file_name(CG_Context *ctx,
                                  BM_Export_Id export_id,
                                  String_View *out) {
    String_View file_name = {0};
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    size_t stem_len = 0;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    file_name = bm_query_export_file_name(ctx->model, export_id);
    if (file_name.count == 0) return true;

    stem_len = file_name.count;
    if (file_name.count >= strlen(".cmake") &&
        strncmp(file_name.data + file_name.count - strlen(".cmake"), ".cmake", strlen(".cmake")) == 0) {
        stem_len -= strlen(".cmake");
    }

    nob_sb_append_buf(&sb, file_name.data ? file_name.data : "", stem_len);
    nob_sb_append_cstr(&sb, "-noconfig");
    nob_sb_append_buf(&sb,
                      file_name.data ? file_name.data + stem_len : "",
                      file_name.count > stem_len ? file_name.count - stem_len : 0);
    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

bool cg_export_noconfig_output_file_path(CG_Context *ctx,
                                         BM_Export_Id export_id,
                                         String_View *out) {
    String_View destination = {0};
    String_View file_name = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    destination = bm_query_export_destination(ctx->model, export_id);
    if (!cg_export_noconfig_file_name(ctx, export_id, &file_name)) return false;
    if (destination.count == 0) {
        *out = file_name;
        return true;
    }
    return cg_join_paths_to_arena(ctx->scratch, destination, file_name, out);
}

bool cg_export_target_in_span(BM_Target_Id_Span span, BM_Target_Id id) {
    for (size_t i = 0; i < span.count; ++i) {
        if (span.items[i] == id) return true;
    }
    return false;
}

bool cg_emit_cmake_imported_target_declaration(BM_Target_Kind kind,
                                               String_View exported_name,
                                               Nob_String_Builder *sb) {
    const char *cmake_kind = "UNKNOWN";
    if (!sb) return false;

    switch (kind) {
        case BM_TARGET_EXECUTABLE: cmake_kind = "EXECUTABLE"; break;
        case BM_TARGET_STATIC_LIBRARY: cmake_kind = "STATIC"; break;
        case BM_TARGET_SHARED_LIBRARY: cmake_kind = "SHARED"; break;
        case BM_TARGET_MODULE_LIBRARY: cmake_kind = "MODULE"; break;
        case BM_TARGET_INTERFACE_LIBRARY: cmake_kind = "INTERFACE"; break;
        default:
            nob_log(NOB_ERROR,
                    "codegen: unsupported exported target kind for '%.*s'",
                    (int)exported_name.count,
                    exported_name.data ? exported_name.data : "");
            return false;
    }

    if (kind == BM_TARGET_EXECUTABLE) {
        nob_sb_append_cstr(sb, "add_executable(");
    } else {
        nob_sb_append_cstr(sb, "add_library(");
    }
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " ");
    nob_sb_append_cstr(sb, cmake_kind);
    nob_sb_append_cstr(sb, " IMPORTED)\n");
    return true;
}

static bool cg_emit_cmake_import_prelude(CG_Context *ctx,
                                         BM_Export_Id export_id,
                                         bool install_style,
                                         Nob_String_Builder *sb) {
    BM_Target_Id_Span targets = {0};
    String_View export_namespace = {0};
    String_View destination = {0};
    String_View path_cursor = {0};
    size_t parent_hops = 0;
    if (!ctx || !sb) return false;

    targets = bm_query_export_targets(ctx->model, export_id);
    export_namespace = bm_query_export_namespace(ctx->model, export_id);
    destination = bm_query_export_destination(ctx->model, export_id);

    nob_sb_append_cstr(sb,
        "# Generated by CMake\n\n"
        "if(\"${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}\" LESS 2.8)\n"
        "   message(FATAL_ERROR \"CMake >= 2.8.0 required\")\n"
        "endif()\n"
        "if(CMAKE_VERSION VERSION_LESS \"2.8.3\")\n"
        "   message(FATAL_ERROR \"CMake >= 2.8.3 required\")\n"
        "endif()\n"
        "cmake_policy(PUSH)\n"
        "cmake_policy(VERSION 2.8.3...3.26)\n"
        "#----------------------------------------------------------------\n"
        "# Generated CMake target import file.\n"
        "#----------------------------------------------------------------\n\n"
        "# Commands may need to know the format version.\n"
        "set(CMAKE_IMPORT_FILE_VERSION 1)\n\n"
        "# Protect against multiple inclusion, which would fail when already imported targets are added once more.\n"
        "set(_cmake_targets_defined \"\")\n"
        "set(_cmake_targets_not_defined \"\")\n"
        "set(_cmake_expected_targets \"\")\n"
        "foreach(_cmake_expected_target IN ITEMS");
    for (size_t i = 0; i < targets.count; ++i) {
        String_View exported_name = {0};
        nob_sb_append_cstr(sb, " ");
        if (!cg_target_exported_name(ctx, targets.items[i], export_namespace, &exported_name) ||
            !cg_cmake_append_escaped(sb, exported_name)) {
            return false;
        }
    }
    nob_sb_append_cstr(sb,
        ")\n"
        "  list(APPEND _cmake_expected_targets \"${_cmake_expected_target}\")\n"
        "  if(TARGET \"${_cmake_expected_target}\")\n"
        "    list(APPEND _cmake_targets_defined \"${_cmake_expected_target}\")\n"
        "  else()\n"
        "    list(APPEND _cmake_targets_not_defined \"${_cmake_expected_target}\")\n"
        "  endif()\n"
        "endforeach()\n"
        "unset(_cmake_expected_target)\n"
        "if(_cmake_targets_defined STREQUAL _cmake_expected_targets)\n"
        "  unset(_cmake_targets_defined)\n"
        "  unset(_cmake_targets_not_defined)\n"
        "  unset(_cmake_expected_targets)\n"
        "  unset(CMAKE_IMPORT_FILE_VERSION)\n"
        "  cmake_policy(POP)\n"
        "  return()\n"
        "endif()\n"
        "if(NOT _cmake_targets_defined STREQUAL \"\")\n"
        "  string(REPLACE \";\" \", \" _cmake_targets_defined_text \"${_cmake_targets_defined}\")\n"
        "  string(REPLACE \";\" \", \" _cmake_targets_not_defined_text \"${_cmake_targets_not_defined}\")\n"
        "  message(FATAL_ERROR \"Some (but not all) targets in this export set were already defined.\\nTargets Defined: ${_cmake_targets_defined_text}\\nTargets not yet defined: ${_cmake_targets_not_defined_text}\\n\")\n"
        "endif()\n"
        "unset(_cmake_targets_defined)\n"
        "unset(_cmake_targets_not_defined)\n"
        "unset(_cmake_expected_targets)\n\n");

    if (!install_style) {
        nob_sb_append_cstr(sb, "\n");
        return true;
    }

    nob_sb_append_cstr(sb, "\n# Compute the installation prefix relative to this file.\n");
    nob_sb_append_cstr(sb, "get_filename_component(_IMPORT_PREFIX \"${CMAKE_CURRENT_LIST_FILE}\" PATH)\n");
    path_cursor = destination;
    while (path_cursor.count > 0) {
        size_t slash = path_cursor.count;
        while (slash > 0 && path_cursor.data[slash - 1] != '/') slash--;
        parent_hops++;
        if (slash == 0) break;
        path_cursor.count = slash - 1;
    }
    for (size_t hop = 0; hop < parent_hops; ++hop) {
        nob_sb_append_cstr(sb,
            "get_filename_component(_IMPORT_PREFIX \"${_IMPORT_PREFIX}\" PATH)\n");
    }
    nob_sb_append_cstr(sb,
        "if(_IMPORT_PREFIX STREQUAL \"/\")\n"
        "  set(_IMPORT_PREFIX \"\")\n"
        "endif()\n\n");
    return true;
}

static bool cg_emit_cmake_import_footer(bool install_style,
                                        bool include_noconfig,
                                        String_View noconfig_name,
                                        Nob_String_Builder *sb) {
    if (!sb) return false;

    if (install_style && include_noconfig) {
        nob_sb_append_cstr(sb,
            "# Load information for each installed configuration.\n"
            "file(GLOB _cmake_config_files \"${CMAKE_CURRENT_LIST_DIR}/");
        if (!cg_cmake_append_escaped(sb, noconfig_name)) return false;
        nob_sb_append_cstr(sb,
            "\")\n"
            "foreach(_cmake_config_file IN LISTS _cmake_config_files)\n"
            "  include(\"${_cmake_config_file}\")\n"
            "endforeach()\n"
            "unset(_cmake_config_file)\n"
            "unset(_cmake_config_files)\n\n"
            "# Cleanup temporary variables.\n"
            "set(_IMPORT_PREFIX)\n\n"
            "# Loop over all imported files and verify that they actually exist\n"
            "foreach(_cmake_target IN LISTS _cmake_import_check_targets)\n"
            "  foreach(_cmake_file IN LISTS \"_cmake_import_check_files_for_${_cmake_target}\")\n"
            "    if(NOT EXISTS \"${_cmake_file}\")\n"
            "      message(FATAL_ERROR \"The imported target \\\"${_cmake_target}\\\" references the file\n"
            "   \\\"${_cmake_file}\\\"\n"
            "but this file does not exist.  Possible reasons include:\n"
            "* The file was deleted, renamed, or moved to another location.\n"
            "* An install or uninstall procedure did not complete successfully.\n"
            "* The installation package was faulty and contained\n"
            "   \\\"${CMAKE_CURRENT_LIST_FILE}\\\"\n"
            "but not all the files it references.\n"
            "\")\n"
            "    endif()\n"
            "  endforeach()\n"
            "  unset(_cmake_file)\n"
            "  unset(\"_cmake_import_check_files_for_${_cmake_target}\")\n"
            "endforeach()\n"
            "unset(_cmake_target)\n"
            "unset(_cmake_import_check_targets)\n\n");
    }

    nob_sb_append_cstr(sb,
        "# This file does not depend on other imported targets which have\n"
        "# been exported from the same project but in a separate export set.\n\n"
        "# Commands beyond this point should not need to know the version.\n"
        "set(CMAKE_IMPORT_FILE_VERSION)\n");
    if (install_style) {
        nob_sb_append_cstr(sb, "cmake_policy(POP)\n");
    } else {
        nob_sb_append_cstr(sb, "cmake_policy(POP)\n");
    }
    return true;
}

static bool cg_emit_cmake_import_noconfig_header(Nob_String_Builder *sb) {
    if (!sb) return false;
    nob_sb_append_cstr(sb,
        "#----------------------------------------------------------------\n"
        "# Generated CMake target import file.\n"
        "#----------------------------------------------------------------\n\n"
        "# Commands may need to know the format version.\n"
        "set(CMAKE_IMPORT_FILE_VERSION 1)\n\n");
    return true;
}

static bool cg_emit_cmake_import_noconfig_footer(Nob_String_Builder *sb) {
    if (!sb) return false;
    nob_sb_append_cstr(sb,
        "# Commands beyond this point should not need to know the version.\n"
        "set(CMAKE_IMPORT_FILE_VERSION)\n");
    return true;
}

static bool cg_collect_target_link_languages(CG_Context *ctx,
                                             BM_Target_Id target_id,
                                             String_View *out) {
    String_View *languages = NULL;
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");

    for (size_t i = 0; i < bm_query_target_source_count(ctx->model, target_id); ++i) {
        String_View language = nob_sv_trim(bm_query_target_source_language(ctx->model, target_id, i));
        if (language.count == 0 || !bm_query_target_source_is_compile_input(ctx->model, target_id, i)) continue;
        if (!cg_collect_unique_path(ctx->scratch, &languages, language)) return false;
    }

    if (arena_arr_len(languages) == 0) {
        for (size_t i = 0; i < bm_query_target_source_count(ctx->model, target_id); ++i) {
            CG_Source_Lang lang = CG_SOURCE_LANG_C;
            String_View source_language = nob_sv_trim(bm_query_target_source_language(ctx->model, target_id, i));
            String_View source_path = bm_query_target_source_effective(ctx->model, target_id, i);
            if (!bm_query_target_source_is_compile_input(ctx->model, target_id, i)) continue;
            if (source_language.count > 0) {
                if (!cg_parse_source_language(source_language, &lang)) continue;
            } else {
                if (cg_is_header_like(source_path) || !cg_classify_source_lang(source_path, &lang)) continue;
            }
            if (!cg_collect_unique_path(ctx->scratch, &languages, cg_compile_language_sv(lang))) return false;
        }
    }

    return cg_join_sv_list(ctx->scratch, languages, out);
}

bool cg_build_cmake_targets_noconfig_file_contents(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   String_View config,
                                                   CG_CMake_Target_Properties_Emitter emit_properties,
                                                   void *userdata,
                                                   String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!ctx || !emit_properties || !out) return false;
    *out = nob_sv_from_cstr("");

    if (!cg_emit_cmake_import_noconfig_header(&sb)) return false;
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        String_View exported_name = {0};
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name) ||
            !emit_properties(ctx,
                             export_id,
                             target_id,
                             exported_name,
                             export_namespace,
                             config,
                             userdata,
                             &sb)) {
            nob_sb_free(sb);
            return false;
        }
    }

    if (!cg_emit_cmake_import_noconfig_footer(&sb)) {
        nob_sb_free(sb);
        return false;
    }

    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool cg_build_cmake_targets_file_contents(CG_Context *ctx,
                                          BM_Export_Id export_id,
                                          String_View config,
                                          bool install_style,
                                          bool include_noconfig,
                                          CG_CMake_Target_Properties_Emitter emit_properties,
                                          void *userdata,
                                          String_View *out) {
    BM_Target_Id_Span targets = bm_query_export_targets(ctx->model, export_id);
    String_View export_namespace = bm_query_export_namespace(ctx->model, export_id);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    String_View noconfig_name = {0};
    if (!ctx || !emit_properties || !out) return false;
    *out = nob_sv_from_cstr("");

    if (include_noconfig && !cg_export_noconfig_file_name(ctx, export_id, &noconfig_name)) return false;

    if (!cg_emit_cmake_import_prelude(ctx, export_id, install_style, &sb)) return false;
    for (size_t i = 0; i < targets.count; ++i) {
        BM_Target_Id target_id = targets.items[i];
        BM_Target_Kind kind = bm_query_target_kind(ctx->model, target_id);
        String_View exported_name = {0};
        if (!cg_target_exported_name(ctx, target_id, export_namespace, &exported_name)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "# Create imported target ");
        if (!cg_cmake_append_escaped(&sb, exported_name)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "\n");
        if (!cg_emit_cmake_imported_target_declaration(kind, exported_name, &sb)) {
            nob_sb_free(sb);
            return false;
        }
        nob_sb_append_cstr(&sb, "\n");
        if (!emit_properties(ctx,
                             export_id,
                             target_id,
                             exported_name,
                             export_namespace,
                             config,
                             userdata,
                             &sb)) {
            nob_sb_free(sb);
            return false;
        }
    }

    if (include_noconfig) {
        String_View pattern = nob_sv_from_cstr("");
        Nob_String_Builder pattern_sb = {0};
        char *pattern_copy = NULL;
        size_t stem_len = noconfig_name.count;
        if (noconfig_name.count >= strlen("-noconfig.cmake") &&
            strncmp(noconfig_name.data + noconfig_name.count - strlen("-noconfig.cmake"),
                    "-noconfig.cmake",
                    strlen("-noconfig.cmake")) == 0) {
            stem_len -= strlen("-noconfig.cmake");
        }
        nob_sb_append_buf(&pattern_sb,
                          noconfig_name.data ? noconfig_name.data : "",
                          stem_len);
        nob_sb_append_cstr(&pattern_sb, "-*.cmake");
        pattern_copy = arena_strndup(ctx->scratch, pattern_sb.items ? pattern_sb.items : "", pattern_sb.count);
        nob_sb_free(pattern_sb);
        if (!pattern_copy) {
            nob_sb_free(sb);
            return false;
        }
        pattern = nob_sv_from_cstr(pattern_copy);
        if (!cg_emit_cmake_import_footer(install_style, true, pattern, &sb)) {
            nob_sb_free(sb);
            return false;
        }
    } else if (!cg_emit_cmake_import_footer(install_style, false, noconfig_name, &sb)) {
        nob_sb_free(sb);
        return false;
    }

    copy = arena_strndup(ctx->scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}
