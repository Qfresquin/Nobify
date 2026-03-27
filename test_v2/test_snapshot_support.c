#include "test_snapshot_support.h"

#include "test_workspace.h"

static void test_snapshot_log_default_mismatch(const char *subject_path,
                                               const char *expected_path,
                                               String_View expected_norm,
                                               String_View actual_norm) {
    nob_log(NOB_ERROR, "golden mismatch for %s", subject_path);
    nob_log(NOB_ERROR,
            "--- expected (%s) ---\n%.*s",
            expected_path,
            (int)expected_norm.count,
            expected_norm.data);
    nob_log(NOB_ERROR,
            "--- actual ---\n%.*s",
            (int)actual_norm.count,
            actual_norm.data);
}

bool test_snapshot_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    Nob_String_Builder sb = {0};

    if (!arena || !path || !out) return false;
    if (!nob_read_entire_file(path, &sb)) return false;

    if (sb.count == 0) {
        nob_sb_free(sb);
        *out = nob_sv_from_cstr("");
        return true;
    }

    char *text = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

String_View test_snapshot_normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

bool test_snapshot_parse_case_pack_to_arena(Arena *arena,
                                            String_View content,
                                            Test_Case_Pack_Entry **out_items) {
    return test_case_pack_parse(arena, content, out_items);
}

void test_snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

bool test_snapshot_assert_golden_output(
    Arena *arena,
    const char *subject_path,
    const char *expected_path,
    String_View actual,
    Test_Snapshot_Golden_Mismatch_Fn on_mismatch,
    void *userdata) {
    String_View expected = {0};
    String_View actual_norm = {0};
    String_View expected_norm = {0};

    if (!arena || !subject_path || !expected_path) return false;

    actual_norm = test_snapshot_normalize_newlines_to_arena(arena, actual);

    if (test_ws_should_update_golden()) {
        if (!test_ws_update_golden_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            return false;
        }
        return true;
    }

    if (!test_snapshot_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        return false;
    }

    expected_norm = test_snapshot_normalize_newlines_to_arena(arena, expected);
    if (nob_sv_eq(expected_norm, actual_norm)) return true;

    if (on_mismatch) {
        on_mismatch(subject_path, expected_path, expected_norm, actual_norm, userdata);
    } else {
        test_snapshot_log_default_mismatch(subject_path, expected_path, expected_norm, actual_norm);
    }

    return false;
}
