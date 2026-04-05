#include "nob_codegen_internal.h"

static bool cg_package_has_generator(CG_Context *ctx, const char *generator_name) {
    if (!ctx || !generator_name) return false;
    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_String_Span generators =
            bm_query_cpack_package_generators(ctx->model, (BM_CPack_Package_Id)package_index);
        for (size_t i = 0; i < generators.count; ++i) {
            if (nob_sv_eq(generators.items[i], nob_sv_from_cstr(generator_name))) return true;
        }
    }
    return false;
}

static bool cg_package_output_dir(CG_Context *ctx,
                                  BM_CPack_Package_Id id,
                                  String_View *out) {
    String_View output_dir = {0};
    String_View absolute = {0};
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    output_dir = bm_query_cpack_package_output_directory(ctx->model, id, ctx->scratch);
    if (output_dir.count == 0) return true;
    if (!cg_absolute_from_cwd(ctx, output_dir, &absolute)) return false;
    return cg_relative_path_to_arena(ctx->scratch, ctx->emit_dir_abs, absolute, out);
}

static bool cg_emit_package_plan_tables(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;

    nob_sb_append_cstr(out,
        "typedef struct {\n"
        "    const char *package_name;\n"
        "    const char *package_version;\n"
        "    const char *package_file_name;\n"
        "    const char *default_output_dir;\n"
        "    const char *staging_root;\n"
        "    const char *payload_root;\n"
        "    const char *metadata_output_path;\n"
        "    bool include_toplevel_directory;\n"
        "    const char **generators;\n"
        "    size_t generator_count;\n"
        "} Nob_Package_Plan;\n\n"
        "typedef struct {\n"
        "    const Nob_Package_Plan *plan;\n"
        "    const char *config;\n"
        "    const char *selected_generator;\n"
        "    const char *effective_output_dir;\n"
        "    const char *staging_root;\n"
        "    const char *payload_root;\n"
        "    const char *metadata_output_path;\n"
        "} Nob_Package_Request;\n\n");

    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_CPack_Package_Id id = (BM_CPack_Package_Id)package_index;
        BM_String_Span generators = bm_query_cpack_package_generators(ctx->model, id);
        nob_sb_append_cstr(out, "static const char *g_package_generators_");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", package_index));
        nob_sb_append_cstr(out, "[] = {");
        for (size_t i = 0; i < generators.count; ++i) {
            if (i > 0) nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, generators.items[i])) return false;
        }
        nob_sb_append_cstr(out, "};\n");
    }
    nob_sb_append_cstr(out, "\nstatic const Nob_Package_Plan g_package_plans[] = {\n");
    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_CPack_Package_Id id = (BM_CPack_Package_Id)package_index;
        String_View output_dir = {0};
        String_View staging_root = {0};
        String_View payload_root = {0};
        String_View metadata_path = {0};
        BM_String_Span generators = bm_query_cpack_package_generators(ctx->model, id);
        if (!cg_package_output_dir(ctx, id, &output_dir) ||
            !cg_rebase_from_binary_root(ctx,
                                        nob_sv_from_cstr(nob_temp_sprintf(".nob/package/plan%zu/staging", package_index)),
                                        &staging_root) ||
            !cg_rebase_from_binary_root(ctx,
                                        nob_sv_from_cstr(nob_temp_sprintf(".nob/package/plan%zu/payload", package_index)),
                                        &payload_root) ||
            !cg_rebase_from_binary_root(ctx,
                                        nob_sv_from_cstr(nob_temp_sprintf(".nob/package/plan%zu/package_metadata.txt", package_index)),
                                        &metadata_path)) {
            return false;
        }

        nob_sb_append_cstr(out, "    {\n");
        nob_sb_append_cstr(out, "        .package_name = ");
        if (!cg_sb_append_c_string(out, bm_query_cpack_package_name(ctx->model, id))) return false;
        nob_sb_append_cstr(out, ",\n        .package_version = ");
        if (!cg_sb_append_c_string(out, bm_query_cpack_package_version(ctx->model, id))) return false;
        nob_sb_append_cstr(out, ",\n        .package_file_name = ");
        if (!cg_sb_append_c_string(out, bm_query_cpack_package_file_name(ctx->model, id))) return false;
        nob_sb_append_cstr(out, ",\n        .default_output_dir = ");
        if (!cg_sb_append_c_string(out, output_dir)) return false;
        nob_sb_append_cstr(out, ",\n        .staging_root = ");
        if (!cg_sb_append_c_string(out, staging_root)) return false;
        nob_sb_append_cstr(out, ",\n        .payload_root = ");
        if (!cg_sb_append_c_string(out, payload_root)) return false;
        nob_sb_append_cstr(out, ",\n        .metadata_output_path = ");
        if (!cg_sb_append_c_string(out, metadata_path)) return false;
        nob_sb_append_cstr(out, ",\n        .include_toplevel_directory = ");
        nob_sb_append_cstr(out, bm_query_cpack_package_include_toplevel_directory(ctx->model, id) ? "true" : "false");
        nob_sb_append_cstr(out, ",\n        .generators = g_package_generators_");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", package_index));
        nob_sb_append_cstr(out, ",\n        .generator_count = ");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", generators.count));
        nob_sb_append_cstr(out, "\n    },\n");
    }
    nob_sb_append_cstr(out,
        "};\n\n"
        "static const size_t g_package_plan_count = sizeof(g_package_plans) / sizeof(g_package_plans[0]);\n\n");
    return true;
}

bool cg_emit_package_function(CG_Context *ctx, Nob_String_Builder *out) {
    bool has_tgz = false;
    bool has_txz = false;
    bool has_zip = false;
    if (!ctx || !out) return false;

    if (bm_query_cpack_package_count(ctx->model) == 0) {
        nob_sb_append_cstr(out,
            "static bool package_all(const char *selected_generator, const char *output_dir_override) {\n"
            "    (void)selected_generator;\n"
            "    (void)output_dir_override;\n"
            "    return true;\n"
            "}\n\n");
        return true;
    }

    has_tgz = cg_package_has_generator(ctx, "TGZ");
    has_txz = cg_package_has_generator(ctx, "TXZ");
    has_zip = cg_package_has_generator(ctx, "ZIP");

    if (!cg_emit_package_plan_tables(ctx, out)) return false;

    nob_sb_append_cstr(out,
        "static int package_string_ptr_compare(const void *lhs, const void *rhs) {\n"
        "    const char *const *a = (const char *const *)lhs;\n"
        "    const char *const *b = (const char *const *)rhs;\n"
        "    if (!*a && !*b) return 0;\n"
        "    if (!*a) return -1;\n"
        "    if (!*b) return 1;\n"
        "    return strcmp(*a, *b);\n"
        "}\n\n"
        "static const char *package_generator_extension(const char *generator) {\n"
        "    if (!generator) return \"\";\n"
        "    if (strcmp(generator, \"TGZ\") == 0) return \".tar.gz\";\n"
        "    if (strcmp(generator, \"TXZ\") == 0) return \".tar.xz\";\n"
        "    if (strcmp(generator, \"ZIP\") == 0) return \".zip\";\n"
        "    return \"\";\n"
        "}\n\n"
        "static bool package_generator_supported(const char *generator) {\n"
        "    return generator && (strcmp(generator, \"TGZ\") == 0 || strcmp(generator, \"TXZ\") == 0 || strcmp(generator, \"ZIP\") == 0);\n"
        "}\n\n"
        "static const char *package_join_path(const char *lhs, const char *rhs) {\n"
        "    if (!lhs || lhs[0] == '\\0') return rhs;\n"
        "    if (!rhs || rhs[0] == '\\0') return lhs;\n"
        "    return nob_temp_sprintf(\"%s/%s\", lhs, rhs);\n"
        "}\n\n"
        "static const char *package_make_archive_name(const Nob_Package_Request *request, const char *generator) {\n"
        "    return nob_temp_sprintf(\"%s%s\", request->plan->package_file_name, package_generator_extension(generator));\n"
        "}\n\n"
        "static const char *package_make_archive_path(const Nob_Package_Request *request, const char *generator) {\n"
        "    return package_join_path(request->effective_output_dir, package_make_archive_name(request, generator));\n"
        "}\n\n"
        "static bool package_request_init(const Nob_Package_Plan *plan,\n"
        "                                 const char *selected_generator,\n"
        "                                 const char *output_dir_override,\n"
        "                                 Nob_Package_Request *out) {\n"
        "    if (!plan || !out) return false;\n"
        "    memset(out, 0, sizeof(*out));\n"
        "    out->plan = plan;\n"
        "    out->config = g_build_config;\n"
        "    out->selected_generator = selected_generator;\n"
        "    out->effective_output_dir = (output_dir_override && output_dir_override[0] != '\\0')\n"
        "        ? output_dir_override\n"
        "        : plan->default_output_dir;\n"
        "    out->staging_root = plan->staging_root;\n"
        "    out->payload_root = plan->payload_root;\n"
        "    out->metadata_output_path = plan->metadata_output_path;\n"
        "    return true;\n"
        "}\n\n"
        "static bool package_collect_sorted_children(const char *dir_path, Nob_File_Paths *children) {\n"
        "    if (!children) return false;\n"
        "    memset(children, 0, sizeof(*children));\n"
        "    if (!nob_read_entire_dir(dir_path, children)) return false;\n"
        "    if (children->count > 1) {\n"
        "        qsort(children->items, children->count, sizeof(children->items[0]), package_string_ptr_compare);\n"
        "    }\n"
        "    return true;\n"
        "}\n\n"
        "static bool package_copy_directory_contents(const char *src_dir, const char *dst_dir) {\n"
        "    Nob_File_Paths children = {0};\n"
        "    bool ok = false;\n"
        "    if (!src_dir || !dst_dir) return false;\n"
        "    if (!ensure_dir(dst_dir)) return false;\n"
        "    if (!package_collect_sorted_children(src_dir, &children)) return false;\n"
        "    ok = true;\n"
        "    for (size_t i = 0; i < children.count; ++i) {\n"
        "        const char *name = children.items[i];\n"
        "        const char *src_path = package_join_path(src_dir, name);\n"
        "        const char *dst_path = package_join_path(dst_dir, name);\n"
        "        struct stat st = {0};\n"
        "        if (!name || strcmp(name, \".\") == 0 || strcmp(name, \"..\") == 0) continue;\n"
        "        if (stat(src_path, &st) != 0) {\n"
        "            nob_log(NOB_ERROR, \"package: failed to stat %s: %s\", src_path, strerror(errno));\n"
        "            ok = false;\n"
        "            break;\n"
        "        }\n"
        "        if (S_ISDIR(st.st_mode)) {\n"
        "            if (!install_copy_directory(src_path, dst_path)) {\n"
        "                ok = false;\n"
        "                break;\n"
        "            }\n"
        "        } else if (S_ISREG(st.st_mode)) {\n"
        "            if (!install_copy_file(src_path, dst_path)) {\n"
        "                ok = false;\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    nob_da_free(children);\n"
        "    return ok;\n"
        "}\n\n"
        "static bool package_sync_payload(const Nob_Package_Request *request) {\n"
        "    const char *top_level_dir = NULL;\n"
        "    if (!request || !request->plan) return false;\n"
        "    if (!remove_path_recursive(request->staging_root) || !remove_path_recursive(request->payload_root)) return false;\n"
        "    if (!ensure_dir(request->staging_root) || !ensure_dir(request->payload_root)) return false;\n"
        "    if (!install_all(request->staging_root, NULL)) return false;\n"
        "    if (request->plan->include_toplevel_directory) {\n"
        "        top_level_dir = package_join_path(request->payload_root, request->plan->package_file_name);\n"
        "        return install_copy_directory(request->staging_root, top_level_dir);\n"
        "    }\n"
        "    return package_copy_directory_contents(request->staging_root, request->payload_root);\n"
        "}\n\n"
        "static bool package_metadata_append(const Nob_Package_Request *request,\n"
        "                                    const char *generator,\n"
        "                                    const char *archive_path) {\n"
        "    FILE *fp = NULL;\n"
        "    struct stat st = {0};\n"
        "    if (!request || !generator || !archive_path) return false;\n"
        "    if (!ensure_parent_dir(request->metadata_output_path)) return false;\n"
        "    fp = fopen(request->metadata_output_path, \"ab\");\n"
        "    if (!fp) {\n"
        "        nob_log(NOB_ERROR, \"package: failed to open metadata file %s: %s\", request->metadata_output_path, strerror(errno));\n"
        "        return false;\n"
        "    }\n"
        "    if (stat(archive_path, &st) != 0) {\n"
        "        nob_log(NOB_ERROR, \"package: failed to stat %s: %s\", archive_path, strerror(errno));\n"
        "        fclose(fp);\n"
        "        return false;\n"
        "    }\n"
        "    fprintf(fp,\n"
        "            \"generator=%s\\nfile=%s\\nsize=%lld\\npackage=%s\\nversion=%s\\ninclude_toplevel=%d\\n\\n\",\n"
        "            generator,\n"
        "            archive_path,\n"
        "            (long long)st.st_size,\n"
        "            request->plan->package_name ? request->plan->package_name : \"\",\n"
        "            request->plan->package_version ? request->plan->package_version : \"\",\n"
        "            request->plan->include_toplevel_directory ? 1 : 0);\n"
        "    fclose(fp);\n"
        "    return true;\n"
        "}\n\n");

    if (has_tgz || has_txz) {
        nob_sb_append_cstr(out,
            "static bool package_tar_write_octal(char *dst, size_t dst_size, unsigned long long value) {\n"
            "    char buf[32] = {0};\n"
            "    size_t len = 0;\n"
            "    if (!dst || dst_size < 2) return false;\n"
            "    memset(dst, '0', dst_size);\n"
            "    dst[dst_size - 1] = '\\0';\n"
            "    if (dst_size >= 2) dst[dst_size - 2] = ' ';\n"
            "    snprintf(buf, sizeof(buf), \"%llo\", value);\n"
            "    len = strlen(buf);\n"
            "    if (len + 2 > dst_size) return false;\n"
            "    memcpy(dst + (dst_size - 2 - len), buf, len);\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_tar_fill_name_prefix(const char *path, char name[100], char prefix[155]) {\n"
            "    const char *slash = NULL;\n"
            "    size_t path_len = path ? strlen(path) : 0;\n"
            "    memset(name, 0, 100);\n"
            "    memset(prefix, 0, 155);\n"
            "    if (path_len == 0) return false;\n"
            "    if (path_len <= 100) {\n"
            "        memcpy(name, path, path_len);\n"
            "        return true;\n"
            "    }\n"
            "    slash = strrchr(path, '/');\n"
            "    if (!slash) return false;\n"
            "    if ((size_t)(slash - path) > 155) return false;\n"
            "    if (strlen(slash + 1) > 100) return false;\n"
            "    memcpy(prefix, path, (size_t)(slash - path));\n"
            "    memcpy(name, slash + 1, strlen(slash + 1));\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_tar_write_header(FILE *fp,\n"
            "                                     const char *relpath,\n"
            "                                     char typeflag,\n"
            "                                     unsigned long long size,\n"
            "                                     unsigned mode) {\n"
            "    unsigned char header[512] = {0};\n"
            "    unsigned checksum = 0;\n"
            "    if (!fp || !relpath || relpath[0] == '\\0') return false;\n"
            "    if (!package_tar_fill_name_prefix(relpath, (char *)&header[0], (char *)&header[345])) {\n"
            "        nob_log(NOB_ERROR, \"package: tar path too long: %s\", relpath);\n"
            "        return false;\n"
            "    }\n"
            "    if (!package_tar_write_octal((char *)&header[100], 8, mode & 0777) ||\n"
            "        !package_tar_write_octal((char *)&header[108], 8, 0) ||\n"
            "        !package_tar_write_octal((char *)&header[116], 8, 0) ||\n"
            "        !package_tar_write_octal((char *)&header[124], 12, size) ||\n"
            "        !package_tar_write_octal((char *)&header[136], 12, 0)) {\n"
            "        return false;\n"
            "    }\n"
            "    memset(&header[148], ' ', 8);\n"
            "    header[156] = (unsigned char)typeflag;\n"
            "    memcpy(&header[257], \"ustar\", 5);\n"
            "    memcpy(&header[263], \"00\", 2);\n"
            "    for (size_t i = 0; i < sizeof(header); ++i) checksum += header[i];\n"
            "    if (!package_tar_write_octal((char *)&header[148], 8, checksum)) return false;\n"
            "    return fwrite(header, 1, sizeof(header), fp) == sizeof(header);\n"
            "}\n\n"
            "static bool package_tar_copy_file(FILE *fp, const char *path) {\n"
            "    FILE *src = NULL;\n"
            "    char buf[8192];\n"
            "    bool ok = false;\n"
            "    src = fopen(path, \"rb\");\n"
            "    if (!src) {\n"
            "        nob_log(NOB_ERROR, \"package: failed to open %s: %s\", path, strerror(errno));\n"
            "        return false;\n"
            "    }\n"
            "    ok = true;\n"
            "    while (!feof(src)) {\n"
            "        size_t n = fread(buf, 1, sizeof(buf), src);\n"
            "        if (n > 0 && fwrite(buf, 1, n, fp) != n) {\n"
            "            ok = false;\n"
            "            break;\n"
            "        }\n"
            "        if (ferror(src)) {\n"
            "            ok = false;\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    fclose(src);\n"
            "    return ok;\n"
            "}\n\n"
            "static bool package_tar_write_padding(FILE *fp, unsigned long long size) {\n"
            "    static const unsigned char zeros[512] = {0};\n"
            "    unsigned long long rem = size % 512ull;\n"
            "    if (rem == 0) return true;\n"
            "    return fwrite(zeros, 1, (size_t)(512ull - rem), fp) == (size_t)(512ull - rem);\n"
            "}\n\n"
            "static bool package_tar_write_tree_entry(FILE *fp, const char *abs_path, const char *relpath) {\n"
            "    struct stat st = {0};\n"
            "    Nob_File_Paths children = {0};\n"
            "    bool ok = false;\n"
            "    if (!fp || !abs_path || !relpath) return false;\n"
            "    if (stat(abs_path, &st) != 0) {\n"
            "        nob_log(NOB_ERROR, \"package: failed to stat %s: %s\", abs_path, strerror(errno));\n"
            "        return false;\n"
            "    }\n"
            "    if (S_ISDIR(st.st_mode)) {\n"
            "        const char *dir_rel = nob_temp_sprintf(\"%s/\", relpath);\n"
            "        if (!package_tar_write_header(fp, dir_rel, '5', 0, (unsigned)st.st_mode)) return false;\n"
            "        if (!package_collect_sorted_children(abs_path, &children)) return false;\n"
            "        ok = true;\n"
            "        for (size_t i = 0; i < children.count; ++i) {\n"
            "            const char *name = children.items[i];\n"
            "            const char *child_abs = package_join_path(abs_path, name);\n"
            "            const char *child_rel = package_join_path(relpath, name);\n"
            "            if (!name || strcmp(name, \".\") == 0 || strcmp(name, \"..\") == 0) continue;\n"
            "            if (!package_tar_write_tree_entry(fp, child_abs, child_rel)) {\n"
            "                ok = false;\n"
            "                break;\n"
            "            }\n"
            "        }\n"
            "        nob_da_free(children);\n"
            "        return ok;\n"
            "    }\n"
            "    if (!S_ISREG(st.st_mode)) return true;\n"
            "    if (!package_tar_write_header(fp, relpath, '0', (unsigned long long)st.st_size, (unsigned)st.st_mode) ||\n"
            "        !package_tar_copy_file(fp, abs_path) ||\n"
            "        !package_tar_write_padding(fp, (unsigned long long)st.st_size)) {\n"
            "        return false;\n"
            "    }\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_write_tar_tree(const char *root_dir, const char *tar_path) {\n"
            "    Nob_File_Paths children = {0};\n"
            "    FILE *fp = NULL;\n"
            "    bool ok = false;\n"
            "    if (!root_dir || !tar_path) return false;\n"
            "    if (!ensure_parent_dir(tar_path)) return false;\n"
            "    fp = fopen(tar_path, \"wb\");\n"
            "    if (!fp) {\n"
            "        nob_log(NOB_ERROR, \"package: failed to open tar file %s: %s\", tar_path, strerror(errno));\n"
            "        return false;\n"
            "    }\n"
            "    if (!package_collect_sorted_children(root_dir, &children)) {\n"
            "        fclose(fp);\n"
            "        return false;\n"
            "    }\n"
            "    ok = true;\n"
            "    for (size_t i = 0; i < children.count; ++i) {\n"
            "        const char *name = children.items[i];\n"
            "        const char *child_abs = package_join_path(root_dir, name);\n"
            "        if (!name || strcmp(name, \".\") == 0 || strcmp(name, \"..\") == 0) continue;\n"
            "        if (!package_tar_write_tree_entry(fp, child_abs, name)) {\n"
            "            ok = false;\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    if (ok) {\n"
            "        static const unsigned char zeros[1024] = {0};\n"
            "        ok = fwrite(zeros, 1, sizeof(zeros), fp) == sizeof(zeros);\n"
            "    }\n"
            "    nob_da_free(children);\n"
            "    fclose(fp);\n"
            "    return ok;\n"
            "}\n\n");
    }

    if (has_zip) {
        nob_sb_append_cstr(out,
            "typedef struct {\n"
            "    const char *name;\n"
            "    uint32_t crc32;\n"
            "    uint32_t size;\n"
            "    uint32_t local_offset;\n"
            "    uint16_t mode;\n"
            "    bool directory;\n"
            "} Nob_Zip_Entry;\n\n"
            "static uint32_t package_zip_crc32(const unsigned char *data, size_t size) {\n"
            "    uint32_t crc = 0xFFFFFFFFu;\n"
            "    for (size_t i = 0; i < size; ++i) {\n"
            "        crc ^= (uint32_t)data[i];\n"
            "        for (int bit = 0; bit < 8; ++bit) {\n"
            "            uint32_t mask = (uint32_t)-(int)(crc & 1u);\n"
            "            crc = (crc >> 1) ^ (0xEDB88320u & mask);\n"
            "        }\n"
            "    }\n"
            "    return ~crc;\n"
            "}\n\n"
            "static bool package_zip_append_u16(Nob_String_Builder *sb, uint16_t value) {\n"
            "    unsigned char buf[2] = {(unsigned char)(value & 0xFFu), (unsigned char)((value >> 8) & 0xFFu)};\n"
            "    if (!sb) return false;\n"
            "    nob_sb_append_buf(sb, (const char *)buf, sizeof(buf));\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_zip_append_u32(Nob_String_Builder *sb, uint32_t value) {\n"
            "    unsigned char buf[4] = {\n"
            "        (unsigned char)(value & 0xFFu),\n"
            "        (unsigned char)((value >> 8) & 0xFFu),\n"
            "        (unsigned char)((value >> 16) & 0xFFu),\n"
            "        (unsigned char)((value >> 24) & 0xFFu),\n"
            "    };\n"
            "    if (!sb) return false;\n"
            "    nob_sb_append_buf(sb, (const char *)buf, sizeof(buf));\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_zip_write_file_entry(FILE *fp,\n"
            "                                         const char *name,\n"
            "                                         const unsigned char *data,\n"
            "                                         size_t size,\n"
            "                                         uint32_t crc32,\n"
            "                                         uint16_t mode,\n"
            "                                         uint32_t local_offset,\n"
            "                                         Nob_String_Builder *central) {\n"
            "    Nob_String_Builder header = {0};\n"
            "    size_t name_len = name ? strlen(name) : 0;\n"
            "    if (!fp || !name || !central) return false;\n"
            "    if (!package_zip_append_u32(&header, 0x04034B50u) ||\n"
            "        !package_zip_append_u16(&header, 20) ||\n"
            "        !package_zip_append_u16(&header, 0) ||\n"
            "        !package_zip_append_u16(&header, 0) ||\n"
            "        !package_zip_append_u16(&header, 0) ||\n"
            "        !package_zip_append_u16(&header, 0) ||\n"
            "        !package_zip_append_u32(&header, crc32) ||\n"
            "        !package_zip_append_u32(&header, (uint32_t)size) ||\n"
            "        !package_zip_append_u32(&header, (uint32_t)size) ||\n"
            "        !package_zip_append_u16(&header, (uint16_t)name_len) ||\n"
            "        !package_zip_append_u16(&header, 0)) {\n"
            "        nob_sb_free(header);\n"
            "        return false;\n"
            "    }\n"
            "    if (fwrite(header.items, 1, header.count, fp) != header.count ||\n"
            "        fwrite(name, 1, name_len, fp) != name_len ||\n"
            "        (size > 0 && fwrite(data, 1, size, fp) != size)) {\n"
            "        nob_sb_free(header);\n"
            "        return false;\n"
            "    }\n"
            "    nob_sb_free(header);\n"
            "    if (!package_zip_append_u32(central, 0x02014B50u) ||\n"
            "        !package_zip_append_u16(central, 20) ||\n"
            "        !package_zip_append_u16(central, 20) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u32(central, crc32) ||\n"
            "        !package_zip_append_u32(central, (uint32_t)size) ||\n"
            "        !package_zip_append_u32(central, (uint32_t)size) ||\n"
            "        !package_zip_append_u16(central, (uint16_t)name_len) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u16(central, 0) ||\n"
            "        !package_zip_append_u32(central, ((uint32_t)(mode & 0xFFFFu)) << 16) ||\n"
            "        !package_zip_append_u32(central, local_offset)) {\n"
            "        return false;\n"
            "    }\n"
            "    nob_sb_append_buf(central, name, name_len);\n"
            "    return true;\n"
            "}\n\n"
            "static bool package_zip_add_tree_entry(FILE *fp,\n"
            "                                       const char *abs_path,\n"
            "                                       const char *relpath,\n"
            "                                       Nob_String_Builder *central,\n"
            "                                       size_t *entry_count) {\n"
            "    struct stat st = {0};\n"
            "    Nob_File_Paths children = {0};\n"
            "    bool ok = false;\n"
            "    long offset = 0;\n"
            "    if (!fp || !abs_path || !relpath || !central || !entry_count) return false;\n"
            "    if (stat(abs_path, &st) != 0) {\n"
            "        nob_log(NOB_ERROR, \"package: failed to stat %s: %s\", abs_path, strerror(errno));\n"
            "        return false;\n"
            "    }\n"
            "    offset = ftell(fp);\n"
            "    if (offset < 0) return false;\n"
            "    if (S_ISDIR(st.st_mode)) {\n"
            "        const char *dir_name = nob_temp_sprintf(\"%s/\", relpath);\n"
            "        if (!package_zip_write_file_entry(fp, dir_name, NULL, 0, 0, (uint16_t)st.st_mode, (uint32_t)offset, central)) return false;\n"
            "        ++(*entry_count);\n"
            "        if (!package_collect_sorted_children(abs_path, &children)) return false;\n"
            "        ok = true;\n"
            "        for (size_t i = 0; i < children.count; ++i) {\n"
            "            const char *name = children.items[i];\n"
            "            const char *child_abs = package_join_path(abs_path, name);\n"
            "            const char *child_rel = package_join_path(relpath, name);\n"
            "            if (!name || strcmp(name, \".\") == 0 || strcmp(name, \"..\") == 0) continue;\n"
            "            if (!package_zip_add_tree_entry(fp, child_abs, child_rel, central, entry_count)) {\n"
            "                ok = false;\n"
            "                break;\n"
            "            }\n"
            "        }\n"
            "        nob_da_free(children);\n"
            "        return ok;\n"
            "    }\n"
            "    if (!S_ISREG(st.st_mode)) return true;\n"
            "    {\n"
            "        Nob_String_Builder file_data = {0};\n"
            "        uint32_t crc32 = 0;\n"
            "        if (!nob_read_entire_file(abs_path, &file_data)) return false;\n"
            "        crc32 = package_zip_crc32((const unsigned char *)(file_data.items ? file_data.items : \"\"), file_data.count);\n"
            "        ok = package_zip_write_file_entry(fp,\n"
            "                                          relpath,\n"
            "                                          (const unsigned char *)(file_data.items ? file_data.items : \"\"),\n"
            "                                          file_data.count,\n"
            "                                          crc32,\n"
            "                                          (uint16_t)st.st_mode,\n"
            "                                          (uint32_t)offset,\n"
            "                                          central);\n"
            "        if (ok) ++(*entry_count);\n"
            "        nob_sb_free(file_data);\n"
            "        return ok;\n"
            "    }\n"
            "}\n\n"
            "static bool package_write_zip_tree(const char *root_dir, const char *zip_path) {\n"
            "    Nob_File_Paths children = {0};\n"
            "    Nob_String_Builder central = {0};\n"
            "    FILE *fp = NULL;\n"
            "    bool ok = false;\n"
            "    long central_offset = 0;\n"
            "    size_t entry_count = 0;\n"
            "    if (!root_dir || !zip_path) return false;\n"
            "    if (!ensure_parent_dir(zip_path)) return false;\n"
            "    fp = fopen(zip_path, \"wb\");\n"
            "    if (!fp) {\n"
            "        nob_log(NOB_ERROR, \"package: failed to open zip file %s: %s\", zip_path, strerror(errno));\n"
            "        return false;\n"
            "    }\n"
            "    if (!package_collect_sorted_children(root_dir, &children)) {\n"
            "        fclose(fp);\n"
            "        return false;\n"
            "    }\n"
            "    ok = true;\n"
            "    for (size_t i = 0; i < children.count; ++i) {\n"
            "        const char *name = children.items[i];\n"
            "        const char *child_abs = package_join_path(root_dir, name);\n"
            "        if (!name || strcmp(name, \".\") == 0 || strcmp(name, \"..\") == 0) continue;\n"
            "        if (!package_zip_add_tree_entry(fp, child_abs, name, &central, &entry_count)) {\n"
            "            ok = false;\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    central_offset = ftell(fp);\n"
            "    if (ok && central_offset >= 0 && fwrite(central.items, 1, central.count, fp) == central.count) {\n"
            "        Nob_String_Builder end = {0};\n"
            "        ok = package_zip_append_u32(&end, 0x06054B50u) &&\n"
            "             package_zip_append_u16(&end, 0) &&\n"
            "             package_zip_append_u16(&end, 0) &&\n"
            "             package_zip_append_u16(&end, (uint16_t)entry_count) &&\n"
            "             package_zip_append_u16(&end, (uint16_t)entry_count) &&\n"
            "             package_zip_append_u32(&end, (uint32_t)central.count) &&\n"
            "             package_zip_append_u32(&end, (uint32_t)central_offset) &&\n"
            "             package_zip_append_u16(&end, 0) &&\n"
            "             fwrite(end.items, 1, end.count, fp) == end.count;\n"
            "        nob_sb_free(end);\n"
            "    } else {\n"
            "        ok = false;\n"
            "    }\n"
            "    nob_da_free(children);\n"
            "    nob_sb_free(central);\n"
            "    fclose(fp);\n"
            "    return ok;\n"
            "}\n\n");
    }

    nob_sb_append_cstr(out,
        "static bool package_generate_archive(const Nob_Package_Request *request, const char *generator) {\n"
        "    const char *archive_path = NULL;\n"
        "    if (!request || !request->plan || !generator) return false;\n"
        "    archive_path = package_make_archive_path(request, generator);\n"
        "    if (!ensure_dir(request->effective_output_dir)) return false;\n");

    if (has_tgz) {
        nob_sb_append_cstr(out,
            "    if (strcmp(generator, \"TGZ\") == 0) {\n"
            "        const char *tar_path = package_join_path(request->staging_root, \"payload.tar\");\n"
            "        const char *compressed_tmp = nob_temp_sprintf(\"%s.gz\", tar_path);\n"
            "        Nob_Cmd cmd = {0};\n"
            "        bool ok = false;\n"
            "        if (!remove_path_recursive(tar_path) || !remove_path_recursive(compressed_tmp)) return false;\n"
            "        if (!package_write_tar_tree(request->payload_root, tar_path)) return false;\n"
            "        nob_cmd_append(&cmd, resolve_gzip_bin(), \"-n\", \"-f\", \"-k\", tar_path);\n"
            "        ok = nob_cmd_run(&cmd);\n"
            "        nob_cmd_free(cmd);\n"
            "        if (!ok) return false;\n"
            "        if (!install_copy_file(compressed_tmp, archive_path)) return false;\n"
            "        if (!remove_path_recursive(compressed_tmp) || !remove_path_recursive(tar_path)) return false;\n"
            "        return package_metadata_append(request, generator, archive_path);\n"
            "    }\n");
    }
    if (has_txz) {
        nob_sb_append_cstr(out,
            "    if (strcmp(generator, \"TXZ\") == 0) {\n"
            "        const char *tar_path = package_join_path(request->staging_root, \"payload.tar\");\n"
            "        const char *compressed_tmp = nob_temp_sprintf(\"%s.xz\", tar_path);\n"
            "        Nob_Cmd cmd = {0};\n"
            "        bool ok = false;\n"
            "        if (!remove_path_recursive(tar_path) || !remove_path_recursive(compressed_tmp)) return false;\n"
            "        if (!package_write_tar_tree(request->payload_root, tar_path)) return false;\n"
            "        nob_cmd_append(&cmd, resolve_xz_bin(), \"-f\", \"-k\", tar_path);\n"
            "        ok = nob_cmd_run(&cmd);\n"
            "        nob_cmd_free(cmd);\n"
            "        if (!ok) return false;\n"
            "        if (!install_copy_file(compressed_tmp, archive_path)) return false;\n"
            "        if (!remove_path_recursive(compressed_tmp) || !remove_path_recursive(tar_path)) return false;\n"
            "        return package_metadata_append(request, generator, archive_path);\n"
            "    }\n");
    }
    if (has_zip) {
        nob_sb_append_cstr(out,
            "    if (strcmp(generator, \"ZIP\") == 0) {\n"
            "        if (!package_write_zip_tree(request->payload_root, archive_path)) return false;\n"
            "        return package_metadata_append(request, generator, archive_path);\n"
            "    }\n");
    }
    nob_sb_append_cstr(out,
        "    nob_log(NOB_ERROR, \"package: unsupported generator '%s'\", generator);\n"
        "    return false;\n"
        "}\n\n"
        "static bool package_run_plan(const Nob_Package_Plan *plan,\n"
        "                             const char *selected_generator,\n"
        "                             const char *output_dir_override,\n"
        "                             bool *executed) {\n"
        "    Nob_Package_Request request = {0};\n"
        "    if (!plan) return false;\n"
        "    if (!package_request_init(plan, selected_generator, output_dir_override, &request)) return false;\n"
        "    if (!remove_path_recursive(request.metadata_output_path)) return false;\n"
        "    if (!package_sync_payload(&request)) return false;\n"
        "    for (size_t i = 0; i < plan->generator_count; ++i) {\n"
        "        const char *generator = plan->generators[i];\n"
        "        if (selected_generator && selected_generator[0] != '\\0' && strcmp(selected_generator, generator) != 0) {\n"
        "            continue;\n"
        "        }\n"
        "        if (!package_generate_archive(&request, generator)) return false;\n"
        "        if (executed) *executed = true;\n"
        "    }\n"
        "    return true;\n"
        "}\n\n"
        "static bool package_all(const char *selected_generator, const char *output_dir_override) {\n"
        "    bool executed = false;\n"
        "    if (selected_generator && selected_generator[0] != '\\0' && !package_generator_supported(selected_generator)) {\n"
        "        nob_log(NOB_ERROR, \"package: unsupported generator '%s'\", selected_generator);\n"
        "        return false;\n"
        "    }\n"
        "    if (g_package_plan_count == 0) return true;\n"
        "    for (size_t i = 0; i < g_package_plan_count; ++i) {\n"
        "        if (!package_run_plan(&g_package_plans[i], selected_generator, output_dir_override, &executed)) return false;\n"
        "    }\n"
        "    if (selected_generator && selected_generator[0] != '\\0' && !executed) {\n"
        "        nob_log(NOB_ERROR, \"package: generator '%s' is not configured in this build\", selected_generator);\n"
        "        return false;\n"
        "    }\n"
        "    return true;\n"
        "}\n\n");

    return true;
}
