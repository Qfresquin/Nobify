#include "nob_codegen_internal.h"

static bool cg_build_export_output_file_abs(CG_Context *ctx,
                                            BM_Export_Id export_id,
                                            String_View config,
                                            String_View *out) {
    BM_Query_Eval_Context qctx = {0};
    String_View path = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    qctx = cg_make_query_ctx(ctx, BM_TARGET_ID_INVALID, BM_QUERY_USAGE_COMPILE, config, nob_sv_from_cstr(""));
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = false;
    if (!cg_resolve_model_string_with_query_ctx(ctx,
                                                &qctx,
                                                bm_query_export_output_file_path(ctx->model, export_id, ctx->scratch),
                                                &path)) {
        return false;
    }
    if (path.count == 0) return true;
    if (cg_path_is_abs(path)) return cg_normalize_path_to_arena(ctx->scratch, path, out);
    return cg_absolute_from_cwd(ctx, path, out);
}

static bool cg_build_tree_resolve_owner_path_abs(CG_Context *ctx,
                                                 BM_Directory_Id owner_dir,
                                                 String_View value,
                                                 String_View *out) {
    String_View source_dir = {0};
    String_View binary_dir = {0};
    String_View source_abs = {0};
    String_View binary_abs = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    if (value.count == 0) return true;
    if (cg_path_is_abs(value)) return cg_normalize_path_to_arena(ctx->scratch, value, out);

    source_dir = bm_query_directory_source_dir(ctx->model, owner_dir);
    binary_dir = bm_query_directory_binary_dir(ctx->model, owner_dir);
    if (!cg_absolute_from_base(ctx, source_dir, value, &source_abs) ||
        !cg_absolute_from_base(ctx, binary_dir, value, &binary_abs)) {
        return false;
    }

    if (nob_file_exists(nob_temp_sv_to_cstr(source_abs))) {
        *out = source_abs;
        return true;
    }
    if (nob_file_exists(nob_temp_sv_to_cstr(binary_abs))) {
        *out = binary_abs;
        return true;
    }

    *out = source_abs;
    return true;
}

static bool cg_export_collect_build_direct_property_values(CG_Context *ctx,
                                                           BM_Target_Id target_id,
                                                           String_View config,
                                                           BM_Query_Usage_Mode usage_mode,
                                                           const char *property_name,
                                                           String_View **out) {
    String_View raw = {0};
    String_View resolved = {0};
    String_View *values = NULL;
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   usage_mode,
                                                   config,
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = false;
    if (!bm_query_target_modeled_property_value(ctx->model,
                                                target_id,
                                                nob_sv_from_cstr(property_name),
                                                ctx->scratch,
                                                &raw) ||
        raw.count == 0) {
        return true;
    }
    if (!cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &values)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(values); ++i) {
        if (values[i].count == 0) continue;
        if (!cg_collect_unique_path(ctx->scratch, out, values[i])) return false;
    }
    return true;
}

static bool cg_export_collect_build_interface_includes(CG_Context *ctx,
                                                       BM_Target_Id target_id,
                                                       String_View config,
                                                       bool want_system,
                                                       String_View **out) {
    String_View raw = {0};
    String_View resolved = {0};
    String_View *includes = NULL;
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   BM_QUERY_USAGE_COMPILE,
                                                   config,
                                                   nob_sv_from_cstr(""));
    BM_Directory_Id owner_dir = BM_DIRECTORY_ID_INVALID;
    const char *property_name = want_system ?
        "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES" :
        "INTERFACE_INCLUDE_DIRECTORIES";
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = false;
    owner_dir = bm_query_target_owner_directory(ctx->model, target_id);
    if (!bm_query_target_modeled_property_value(ctx->model,
                                                target_id,
                                                nob_sv_from_cstr(property_name),
                                                ctx->scratch,
                                                &raw) ||
        raw.count == 0) {
        return true;
    }
    if (!cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &includes)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(includes); ++i) {
        String_View item = includes[i];
        String_View absolute = {0};
        if (item.count == 0) continue;
        if (!cg_build_tree_resolve_owner_path_abs(ctx, owner_dir, item, &absolute)) return false;
        if (!cg_collect_unique_path(ctx->scratch, out, absolute)) return false;
    }
    return true;
}

static bool cg_export_collect_build_link_libraries(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   BM_Target_Id target_id,
                                                   String_View export_namespace,
                                                   String_View config,
                                                   String_View **out) {
    String_View raw = {0};
    String_View resolved = {0};
    String_View *libs = NULL;
    BM_Target_Id_Span exported_targets = bm_query_export_targets(ctx->model, export_id);
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx,
                                                   target_id,
                                                   BM_QUERY_USAGE_LINK,
                                                   config,
                                                   nob_sv_from_cstr(""));
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = false;
    qctx.install_interface_active = false;
    if (!bm_query_target_modeled_property_value(ctx->model,
                                                target_id,
                                                nob_sv_from_cstr("INTERFACE_LINK_LIBRARIES"),
                                                ctx->scratch,
                                                &raw) ||
        raw.count == 0) {
        return true;
    }
    if (!cg_resolve_model_string_with_query_ctx(ctx, &qctx, raw, &resolved) ||
        !cg_split_top_level_list(ctx->scratch, resolved, &libs)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(libs); ++i) {
        CG_Resolved_Target_Ref dep = {0};
        String_View value = libs[i];
        if (value.count == 0) continue;
        if (cg_resolve_target_ref(ctx, &qctx, value, &dep)) {
            String_View exported_name = {0};
            if (cg_export_target_in_span(exported_targets, dep.target_id)) {
                if (!cg_target_exported_name(ctx, dep.target_id, export_namespace, &exported_name) ||
                    !cg_collect_unique_path(ctx->scratch, out, exported_name)) {
                    return false;
                }
                continue;
            }
        }
        if (!cg_collect_unique_path(ctx->scratch, out, value)) return false;
    }
    return true;
}

static bool cg_export_emit_build_tree_target_properties(CG_Context *ctx,
                                                        BM_Export_Id export_id,
                                                        BM_Target_Id target_id,
                                                        String_View exported_name,
                                                        String_View export_namespace,
                                                        String_View config,
                                                        void *userdata,
                                                        Nob_String_Builder *sb) {
    const CG_Target_Info *info = cg_target_info(ctx, target_id);
    String_View runtime_abs = {0};
    String_View link_languages_joined = {0};
    String_View includes_joined = {0};
    String_View system_includes_joined = {0};
    String_View compile_defs_joined = {0};
    String_View compile_opts_joined = {0};
    String_View compile_features_joined = {0};
    String_View link_opts_joined = {0};
    String_View link_dirs_joined = {0};
    String_View link_libs_joined = {0};
    BM_Target_Artifact_View artifact = {0};
    String_View *include_items = NULL;
    String_View *system_include_items = NULL;
    String_View *compile_defs = NULL;
    String_View *compile_opts = NULL;
    String_View *compile_features = NULL;
    String_View *link_opts = NULL;
    String_View *link_dirs = NULL;
    String_View *link_libs = NULL;
    (void)userdata;
    if (!ctx || !info || !sb) return false;

    if (!cg_export_collect_build_interface_includes(ctx, target_id, config, false, &include_items) ||
        !cg_export_collect_build_interface_includes(ctx, target_id, config, true, &system_include_items) ||
        !cg_export_collect_build_direct_property_values(ctx, target_id, config, BM_QUERY_USAGE_COMPILE, "INTERFACE_COMPILE_DEFINITIONS", &compile_defs) ||
        !cg_export_collect_build_direct_property_values(ctx, target_id, config, BM_QUERY_USAGE_COMPILE, "INTERFACE_COMPILE_OPTIONS", &compile_opts) ||
        !cg_export_collect_build_direct_property_values(ctx, target_id, config, BM_QUERY_USAGE_COMPILE, "INTERFACE_COMPILE_FEATURES", &compile_features) ||
        !cg_export_collect_build_direct_property_values(ctx, target_id, config, BM_QUERY_USAGE_LINK, "INTERFACE_LINK_OPTIONS", &link_opts) ||
        !cg_export_collect_build_direct_property_values(ctx, target_id, config, BM_QUERY_USAGE_LINK, "INTERFACE_LINK_DIRECTORIES", &link_dirs) ||
        !cg_export_collect_build_link_libraries(ctx, export_id, target_id, export_namespace, config, &link_libs) ||
        !cg_collect_target_link_languages(ctx, target_id, &link_languages_joined) ||
        !cg_join_sv_list(ctx->scratch, include_items, &includes_joined) ||
        !cg_join_sv_list(ctx->scratch, system_include_items, &system_includes_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_defs, &compile_defs_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_opts, &compile_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, compile_features, &compile_features_joined) ||
        !cg_join_sv_list(ctx->scratch, link_opts, &link_opts_joined) ||
        !cg_join_sv_list(ctx->scratch, link_dirs, &link_dirs_joined) ||
        !cg_join_sv_list(ctx->scratch, link_libs, &link_libs_joined)) {
        return false;
    }

    if (info->emits_artifact) {
        if (!cg_target_artifact_for_config_or_empty(info, BM_TARGET_ARTIFACT_RUNTIME, config, &artifact) ||
            !cg_absolute_from_emit(ctx, artifact.path, &runtime_abs)) {
            return false;
        }
    }
    nob_sb_append_cstr(sb, "# Import target \"");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, "\" for configuration \"\"\n");
    nob_sb_append_cstr(sb, "set_property(TARGET ");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)\n");
    nob_sb_append_cstr(sb, "set_target_properties(");
    if (!cg_cmake_append_escaped(sb, exported_name)) return false;
    nob_sb_append_cstr(sb, " PROPERTIES\n");
    if (includes_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_INCLUDE_DIRECTORIES \"");
        if (!cg_cmake_append_escaped(sb, includes_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (system_includes_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES \"");
        if (!cg_cmake_append_escaped(sb, system_includes_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (compile_defs_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_DEFINITIONS \"");
        if (!cg_cmake_append_escaped(sb, compile_defs_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (compile_opts_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_OPTIONS \"");
        if (!cg_cmake_append_escaped(sb, compile_opts_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (compile_features_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_COMPILE_FEATURES \"");
        if (!cg_cmake_append_escaped(sb, compile_features_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (link_opts_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_LINK_OPTIONS \"");
        if (!cg_cmake_append_escaped(sb, link_opts_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (link_dirs_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_LINK_DIRECTORIES \"");
        if (!cg_cmake_append_escaped(sb, link_dirs_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (link_libs_joined.count > 0) {
        nob_sb_append_cstr(sb, "  INTERFACE_LINK_LIBRARIES \"");
        if (!cg_cmake_append_escaped(sb, link_libs_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (link_languages_joined.count > 0) {
        nob_sb_append_cstr(sb, "  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG \"");
        if (!cg_cmake_append_escaped(sb, link_languages_joined)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    if (runtime_abs.count > 0) {
        nob_sb_append_cstr(sb, "  IMPORTED_LOCATION_NOCONFIG \"");
        if (!cg_cmake_append_escaped(sb, runtime_abs)) return false;
        nob_sb_append_cstr(sb, "\"\n");
    }
    nob_sb_append_cstr(sb, "  )\n\n");
    return true;
}

static bool cg_build_buildtree_export_file_contents(CG_Context *ctx,
                                                    BM_Export_Id export_id,
                                                    String_View config,
                                                    String_View *out) {
    if (!ctx || !out) return false;
    return cg_build_cmake_targets_file_contents(ctx,
                                                export_id,
                                                config,
                                                false,
                                                false,
                                                cg_export_emit_build_tree_target_properties,
                                                NULL,
                                                out);
}

static bool cg_emit_package_registry_helpers(Nob_String_Builder *out) {
    if (!out) return false;
    nob_sb_append_cstr(out,
        "static void export_registry_md5_store_be32(unsigned char *p, unsigned int v) {\n"
        "    p[0] = (unsigned char)((v >> 24) & 0xFFu);\n"
        "    p[1] = (unsigned char)((v >> 16) & 0xFFu);\n"
        "    p[2] = (unsigned char)((v >> 8) & 0xFFu);\n"
        "    p[3] = (unsigned char)(v & 0xFFu);\n"
        "}\n\n"
        "static unsigned int export_registry_md5_rotl(unsigned int v, unsigned int n) {\n"
        "    return (v << n) | (v >> (32u - n));\n"
        "}\n\n"
        "static void export_registry_md5_process_block(unsigned int state[4], const unsigned char block[64]) {\n"
        "    static const unsigned int K[64] = {\n"
        "        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,\n"
        "        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,\n"
        "        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,\n"
        "        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,\n"
        "        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,\n"
        "        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,\n"
        "        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,\n"
        "        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u\n"
        "    };\n"
        "    static const unsigned int S[64] = {\n"
        "        7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,\n"
        "        5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u,\n"
        "        4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,\n"
        "        6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u\n"
        "    };\n"
        "    unsigned int M[16] = {0};\n"
        "    unsigned int a = state[0];\n"
        "    unsigned int b = state[1];\n"
        "    unsigned int c = state[2];\n"
        "    unsigned int d = state[3];\n"
        "    for (size_t i = 0; i < 16; ++i) {\n"
        "        M[i] = (unsigned int)block[i * 4 + 0] | ((unsigned int)block[i * 4 + 1] << 8) |\n"
        "               ((unsigned int)block[i * 4 + 2] << 16) | ((unsigned int)block[i * 4 + 3] << 24);\n"
        "    }\n"
        "    for (size_t i = 0; i < 64; ++i) {\n"
        "        unsigned int f = 0u;\n"
        "        unsigned int g = 0u;\n"
        "        if (i < 16) {\n"
        "            f = (b & c) | (~b & d);\n"
        "            g = (unsigned int)i;\n"
        "        } else if (i < 32) {\n"
        "            f = (d & b) | (~d & c);\n"
        "            g = (5u * (unsigned int)i + 1u) & 15u;\n"
        "        } else if (i < 48) {\n"
        "            f = b ^ c ^ d;\n"
        "            g = (3u * (unsigned int)i + 5u) & 15u;\n"
        "        } else {\n"
        "            f = c ^ (b | ~d);\n"
        "            g = (7u * (unsigned int)i) & 15u;\n"
        "        }\n"
        "        {\n"
        "            unsigned int temp = d;\n"
        "            d = c;\n"
        "            c = b;\n"
        "            b = b + export_registry_md5_rotl(a + f + K[i] + M[g], S[i]);\n"
        "            a = temp;\n"
        "        }\n"
        "    }\n"
        "    state[0] += a;\n"
        "    state[1] += b;\n"
        "    state[2] += c;\n"
        "    state[3] += d;\n"
        "}\n\n"
        "static void export_registry_md5_compute(const unsigned char *msg, size_t len, unsigned char out_digest[16]) {\n"
        "    unsigned int state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};\n"
        "    unsigned char tail[128] = {0};\n"
        "    size_t full_blocks = len / 64u;\n"
        "    size_t rem = len % 64u;\n"
        "    unsigned long long bits = (unsigned long long)len * 8ull;\n"
        "    for (size_t i = 0; i < full_blocks; ++i) {\n"
        "        export_registry_md5_process_block(state, msg + (i * 64u));\n"
        "    }\n"
        "    if (rem > 0) memcpy(tail, msg + (full_blocks * 64u), rem);\n"
        "    tail[rem] = 0x80u;\n"
        "    if (rem >= 56u) {\n"
        "        export_registry_md5_store_be32(tail + 120u, (unsigned int)((bits >> 32) & 0xffffffffu));\n"
        "        export_registry_md5_store_be32(tail + 124u, (unsigned int)(bits & 0xffffffffu));\n"
        "        export_registry_md5_process_block(state, tail);\n"
        "        memset(tail, 0, 64u);\n"
        "    }\n"
        "    tail[56] = (unsigned char)(bits & 0xffu);\n"
        "    tail[57] = (unsigned char)((bits >> 8) & 0xffu);\n"
        "    tail[58] = (unsigned char)((bits >> 16) & 0xffu);\n"
        "    tail[59] = (unsigned char)((bits >> 24) & 0xffu);\n"
        "    tail[60] = (unsigned char)((bits >> 32) & 0xffu);\n"
        "    tail[61] = (unsigned char)((bits >> 40) & 0xffu);\n"
        "    tail[62] = (unsigned char)((bits >> 48) & 0xffu);\n"
        "    tail[63] = (unsigned char)((bits >> 56) & 0xffu);\n"
        "    export_registry_md5_process_block(state, tail);\n"
        "    for (size_t i = 0; i < 4; ++i) {\n"
        "        out_digest[i * 4 + 0] = (unsigned char)(state[i] & 0xffu);\n"
        "        out_digest[i * 4 + 1] = (unsigned char)((state[i] >> 8) & 0xffu);\n"
        "        out_digest[i * 4 + 2] = (unsigned char)((state[i] >> 16) & 0xffu);\n"
        "        out_digest[i * 4 + 3] = (unsigned char)((state[i] >> 24) & 0xffu);\n"
        "    }\n"
        "}\n\n"
        "static bool export_registry_md5_hex(const char *input, char out_hex[33]) {\n"
        "    static const char *lut = \"0123456789abcdef\";\n"
        "    unsigned char digest[16] = {0};\n"
        "    if (!input || !out_hex) return false;\n"
        "    export_registry_md5_compute((const unsigned char*)input, strlen(input), digest);\n"
        "    for (size_t i = 0; i < 16; ++i) {\n"
        "        out_hex[i * 2 + 0] = lut[(digest[i] >> 4) & 0x0Fu];\n"
        "        out_hex[i * 2 + 1] = lut[digest[i] & 0x0Fu];\n"
        "    }\n"
        "    out_hex[32] = '\\0';\n"
        "    return true;\n"
        "}\n\n"
        "static const char *resolve_home_dir(void) {\n"
        "    const char *home = getenv(\"HOME\");\n"
        "#if defined(_WIN32)\n"
        "    if (!home || home[0] == '\\0') home = getenv(\"USERPROFILE\");\n"
        "#endif\n"
        "    return home;\n"
        "}\n\n"
        "static char *export_registry_heap_join2(const char *lhs, const char *rhs, const char *sep) {\n"
        "    size_t lhs_len = lhs ? strlen(lhs) : 0u;\n"
        "    size_t rhs_len = rhs ? strlen(rhs) : 0u;\n"
        "    size_t sep_len = sep ? strlen(sep) : 0u;\n"
        "    char *joined = (char*)malloc(lhs_len + sep_len + rhs_len + 1u);\n"
        "    if (!joined) return NULL;\n"
        "    if (lhs_len > 0) memcpy(joined, lhs, lhs_len);\n"
        "    if (sep_len > 0) memcpy(joined + lhs_len, sep, sep_len);\n"
        "    if (rhs_len > 0) memcpy(joined + lhs_len + sep_len, rhs, rhs_len);\n"
        "    joined[lhs_len + sep_len + rhs_len] = '\\0';\n"
        "    return joined;\n"
        "}\n\n"
        "static bool export_package_registry_add(const char *package_name, const char *prefix) {\n"
        "    char entry_name[33] = {0};\n"
        "    const char *home = NULL;\n"
        "    char *package_dir = NULL;\n"
        "    char *entry_path = NULL;\n"
        "    char *contents = NULL;\n"
        "    bool ok = false;\n"
        "    if (!package_name || package_name[0] == '\\0' || !prefix || prefix[0] == '\\0') return false;\n"
        "    home = resolve_home_dir();\n"
        "    if (!home || home[0] == '\\0') return true;\n"
        "    if (!export_registry_md5_hex(prefix, entry_name)) return false;\n"
        "    package_dir = export_registry_heap_join2(home, \".cmake/packages\", \"/\");\n"
        "    if (!package_dir) return false;\n"
        "    entry_path = export_registry_heap_join2(package_dir, package_name, \"/\");\n"
        "    free(package_dir);\n"
        "    package_dir = entry_path;\n"
        "    entry_path = NULL;\n"
        "    if (!package_dir || !ensure_dir(package_dir)) goto cleanup;\n"
        "    entry_path = export_registry_heap_join2(package_dir, entry_name, \"/\");\n"
        "    contents = export_registry_heap_join2(prefix, \"\\n\", \"\");\n"
        "    if (!entry_path || !contents) goto cleanup;\n"
        "    ok = nob_write_entire_file(entry_path, contents, strlen(contents));\n"
        "cleanup:\n"
        "    free(contents);\n"
        "    free(entry_path);\n"
        "    free(package_dir);\n"
        "    return ok;\n"
        "}\n\n");
    return true;
}

bool cg_emit_export_function(CG_Context *ctx, Nob_String_Builder *out) {
    size_t build_export_count = 0;
    size_t package_export_count = 0;
    if (!ctx || !out) return false;

    for (size_t i = 0; i < bm_query_export_count(ctx->model); ++i) {
        switch (bm_query_export_kind(ctx->model, (BM_Export_Id)i)) {
            case BM_EXPORT_BUILD_TREE: build_export_count++; break;
            case BM_EXPORT_PACKAGE_REGISTRY: package_export_count++; break;
            case BM_EXPORT_INSTALL: break;
        }
    }

    if (package_export_count > 0 && !cg_emit_package_registry_helpers(out)) return false;

    nob_sb_append_cstr(out, "static bool export_all(void) {\n");
    if (build_export_count == 0 && package_export_count == 0) {
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    for (size_t export_index = 0; export_index < bm_query_export_count(ctx->model); ++export_index) {
        BM_Export_Id export_id = (BM_Export_Id)export_index;
        BM_Export_Kind kind = bm_query_export_kind(ctx->model, export_id);

        if (kind == BM_EXPORT_BUILD_TREE) {
            nob_sb_append_cstr(out, "    {\n");
            for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
                String_View config = branch < arena_arr_len(ctx->known_configs)
                    ? ctx->known_configs[branch]
                    : nob_sv_from_cstr("");
                String_View export_text = {0};
                String_View output_abs = {0};
                if (!cg_build_buildtree_export_file_contents(ctx, export_id, config, &export_text) ||
                    !cg_build_export_output_file_abs(ctx, export_id, config, &output_abs) ||
                    !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                    return false;
                }
                nob_sb_append_cstr(out, "        const char *output_path = ");
                if (!cg_sb_append_c_string(out, output_abs)) return false;
                nob_sb_append_cstr(out, ";\n");
                nob_sb_append_cstr(out, "        if (!ensure_parent_dir(output_path)) return false;\n");
                nob_sb_append_cstr(out, "        if (!nob_write_entire_file(output_path, ");
                if (!cg_sb_append_c_string(out, export_text)) return false;
                nob_sb_append_cstr(out, ", strlen(");
                if (!cg_sb_append_c_string(out, export_text)) return false;
                nob_sb_append_cstr(out, "))) return false;\n");
            }
            if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
            nob_sb_append_cstr(out, "    }\n");
            continue;
        }

        if (kind == BM_EXPORT_PACKAGE_REGISTRY) {
            if (!bm_query_export_enabled(ctx->model, export_id)) continue;
            nob_sb_append_cstr(out, "    if (!export_package_registry_add(");
            if (!cg_sb_append_c_string(out, bm_query_export_package_name(ctx->model, export_id))) return false;
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, bm_query_export_registry_prefix(ctx->model, export_id))) return false;
            nob_sb_append_cstr(out, ")) return false;\n");
        }
    }

    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}
