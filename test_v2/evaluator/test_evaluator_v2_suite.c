#include "test_v2_assert.h"

#include "test_evaluator_v2_shared.h"

typedef struct {
    String_View name;
    String_View script;
} Evaluator_Case;

typedef struct {
    Evaluator_Case *items;
    size_t count;
    size_t capacity;
} Evaluator_Case_List;

static bool evaluator_case_list_append(Arena *arena, Evaluator_Case_List *list, Evaluator_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Evaluator_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Evaluator_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!evaluator_case_list_append(arena, out, (Evaluator_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) return false;
        }
    }

    return out->count > 0;
}

static bool render_evaluator_case_snapshot_to_sb(Arena *arena,
                                                  Evaluator_Case eval_case,
                                                  Nob_String_Builder *out_sb) {
    diag_reset();
    Ast_Root root = parse_cmake(arena, eval_case.script.data);

    Nob_String_Builder case_sb = {0};
    (void)evaluator_snapshot_from_ast(root, "CMakeLists.txt", &case_sb);
    if (case_sb.count == 0) {
        nob_sb_free(case_sb);
        return false;
    }

    nob_sb_append_buf(out_sb, case_sb.items, case_sb.count);
    nob_sb_free(case_sb);
    return true;
}

static bool render_evaluator_casepack_snapshot_to_arena(Arena *arena,
                                                         Evaluator_Case_List cases,
                                                         String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE evaluator\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_evaluator_case_snapshot_to_sb(arena, cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(4 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;

    if (!evaluator_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Evaluator_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 24) {
        nob_log(NOB_ERROR, "golden: unexpected evaluator case count: got=%zu expected=24", cases.count);
        ok = false;
        goto done;
    }

    if (!render_evaluator_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = evaluator_normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = evaluator_normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(actual_norm, expected_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    arena_destroy(arena);
    return ok;
}

static const char *EVALUATOR_GOLDEN_DIR = "test_v2/evaluator/golden";

TEST(evaluator_golden_all_cases) {
    ASSERT(assert_evaluator_golden_casepack(
        nob_temp_sprintf("%s/evaluator_all.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/evaluator_all.txt", EVALUATOR_GOLDEN_DIR)));
    TEST_PASS();
}

void run_evaluator_golden_tests(int *passed, int *failed) {
    test_evaluator_golden_all_cases(passed, failed);
}
