#include "ctest_coverage_utils.h"
#include "build_model.h"

#include "cmake_path_utils.h"
#include "sys_utils.h"

static void ctest_cov_json_append_escaped(String_Builder *sb, String_View s) {
    if (!sb) return;
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '\\') sb_append_cstr(sb, "\\\\");
        else if (c == '"') sb_append_cstr(sb, "\\\"");
        else if (c == '\n') sb_append_cstr(sb, "\\n");
        else if (c == '\r') sb_append_cstr(sb, "\\r");
        else if (c == '\t') sb_append_cstr(sb, "\\t");
        else if (c < 0x20) sb_append_cstr(sb, "?");
        else sb_append(sb, (char)c);
    }
}

static bool ctest_cov_is_artifact(String_View name) {
    String_View ext = cmk_path_extension(name);
    return nob_sv_eq(ext, sv_from_cstr(".gcda")) ||
           nob_sv_eq(ext, sv_from_cstr(".gcno")) ||
           nob_sv_eq(ext, sv_from_cstr(".gcov")) ||
           nob_sv_eq(ext, sv_from_cstr(".GCDA")) ||
           nob_sv_eq(ext, sv_from_cstr(".GCNO")) ||
           nob_sv_eq(ext, sv_from_cstr(".GCOV"));
}

static bool ctest_cov_collect_recursive(Arena *arena, String_View dir, String_List *files) {
    if (!arena || dir.count == 0 || !files) return false;

    Nob_File_Paths children = {0};
    if (!sys_read_dir(dir, &children)) return false;

    for (size_t i = 0; i < children.count; i++) {
        String_View name = sv_from_cstr(children.items[i]);
        if (nob_sv_eq(name, sv_from_cstr(".")) || nob_sv_eq(name, sv_from_cstr(".."))) continue;

        String_View child = cmk_path_join(arena, dir, name);
        Nob_File_Type t = sys_get_file_type(child);
        if (t == NOB_FILE_DIRECTORY) {
            (void)ctest_cov_collect_recursive(arena, child, files);
            continue;
        }
        if (t == NOB_FILE_REGULAR && ctest_cov_is_artifact(name)) {
            string_list_add(files, arena, child);
        }
    }

    nob_da_free(children);
    return true;
}

bool ctest_coverage_collect_gcov_bundle(Arena *arena,
                                        String_View source_dir,
                                        String_View build_dir,
                                        String_View gcov_command,
                                        const String_List *gcov_options,
                                        String_View tarball_path,
                                        String_View tarball_compression,
                                        bool delete_after,
                                        String_View *out_data_json_path,
                                        String_View *out_labels_json_path,
                                        String_View *out_coverage_xml_path,
                                        size_t *out_file_count) {
    if (!arena || source_dir.count == 0 || build_dir.count == 0 || tarball_path.count == 0) return false;

    String_List files = {0};
    string_list_init(&files);
    (void)ctest_cov_collect_recursive(arena, build_dir, &files);

    String_View coverage_dir = cmk_path_join(arena, build_dir, sv_from_cstr("Testing/CoverageInfo"));
    String_View data_json_path = cmk_path_join(arena, coverage_dir, sv_from_cstr("data.json"));
    String_View labels_json_path = cmk_path_join(arena, coverage_dir, sv_from_cstr("Labels.json"));
    String_View coverage_xml_path = cmk_path_join(arena, coverage_dir, sv_from_cstr("Coverage.xml"));

    if (!sys_ensure_parent_dirs(arena, cmk_path_join(arena, coverage_dir, sv_from_cstr(".keep"))) ||
        !sys_mkdir(coverage_dir)) {
        return false;
    }

    String_Builder data_json = {0};
    sb_append_cstr(&data_json, "{\n");
    sb_append_cstr(&data_json, "  \"format\": \"cmk2nob-cdash-gcov-v1\",\n");
    sb_append_cstr(&data_json, "  \"source\": \"");
    ctest_cov_json_append_escaped(&data_json, source_dir);
    sb_append_cstr(&data_json, "\",\n  \"build\": \"");
    ctest_cov_json_append_escaped(&data_json, build_dir);
    sb_append_cstr(&data_json, "\",\n  \"gcov_command\": \"");
    ctest_cov_json_append_escaped(&data_json, gcov_command);
    sb_append_cstr(&data_json, "\",\n  \"gcov_options\": [");
    if (gcov_options) {
        for (size_t i = 0; i < gcov_options->count; i++) {
            if (i > 0) sb_append_cstr(&data_json, ", ");
            sb_append(&data_json, '"');
            ctest_cov_json_append_escaped(&data_json, gcov_options->items[i]);
            sb_append(&data_json, '"');
        }
    }
    sb_append_cstr(&data_json, "],\n  \"files\": [");
    for (size_t i = 0; i < files.count; i++) {
        if (i > 0) sb_append_cstr(&data_json, ", ");
        sb_append(&data_json, '"');
        ctest_cov_json_append_escaped(&data_json, files.items[i]);
        sb_append(&data_json, '"');
    }
    sb_append_cstr(&data_json, "]\n}\n");

    if (!sys_write_file_bytes(data_json_path, data_json.items ? data_json.items : "", data_json.count)) {
        nob_sb_free(data_json);
        return false;
    }
    nob_sb_free(data_json);

    if (!sys_write_file_bytes(labels_json_path, "{}\n", 3)) return false;

    String_Builder coverage_xml = {0};
    sb_append_cstr(&coverage_xml, "<Site BuildName=\"cmk2nob\" Name=\"cmk2nob\">\n");
    sb_append_cstr(&coverage_xml, "  <Coverage>\n");
    sb_append_cstr(&coverage_xml, "    <CoverageLog>\n");
    for (size_t i = 0; i < files.count; i++) {
        sb_append_cstr(&coverage_xml, "      <File>");
        ctest_cov_json_append_escaped(&coverage_xml, files.items[i]);
        sb_append_cstr(&coverage_xml, "</File>\n");
    }
    sb_append_cstr(&coverage_xml, "    </CoverageLog>\n");
    sb_append_cstr(&coverage_xml, "  </Coverage>\n");
    sb_append_cstr(&coverage_xml, "</Site>\n");

    if (!sys_write_file_bytes(coverage_xml_path, coverage_xml.items ? coverage_xml.items : "", coverage_xml.count)) {
        nob_sb_free(coverage_xml);
        return false;
    }
    nob_sb_free(coverage_xml);

    if (!sys_ensure_parent_dirs(arena, tarball_path)) return false;

    String_Builder tar_manifest = {0};
    sb_append_cstr(&tar_manifest, "# cmk2nob-cdash-gcov-bundle-v1\n");
    sb_append_cstr(&tar_manifest, "source=");
    sb_append_buf(&tar_manifest, source_dir.data, source_dir.count);
    sb_append_cstr(&tar_manifest, "\nbuild=");
    sb_append_buf(&tar_manifest, build_dir.data, build_dir.count);
    sb_append_cstr(&tar_manifest, "\ncompression=");
    sb_append_buf(&tar_manifest, tarball_compression.data, tarball_compression.count);
    sb_append_cstr(&tar_manifest, "\ngcov_command=");
    sb_append_buf(&tar_manifest, gcov_command.data, gcov_command.count);
    sb_append_cstr(&tar_manifest, "\nmetadata=data.json;Labels.json;Coverage.xml\n");
    for (size_t i = 0; i < files.count; i++) {
        sb_append_cstr(&tar_manifest, "file=");
        sb_append_buf(&tar_manifest, files.items[i].data, files.items[i].count);
        sb_append(&tar_manifest, '\n');
    }

    if (!sys_write_file_bytes(tarball_path, tar_manifest.items ? tar_manifest.items : "", tar_manifest.count)) {
        nob_sb_free(tar_manifest);
        return false;
    }
    nob_sb_free(tar_manifest);

    if (delete_after) {
        for (size_t i = 0; i < files.count; i++) {
            (void)sys_delete_file(files.items[i]);
        }
    }

    if (out_data_json_path) *out_data_json_path = data_json_path;
    if (out_labels_json_path) *out_labels_json_path = labels_json_path;
    if (out_coverage_xml_path) *out_coverage_xml_path = coverage_xml_path;
    if (out_file_count) *out_file_count = files.count;
    return true;
}
