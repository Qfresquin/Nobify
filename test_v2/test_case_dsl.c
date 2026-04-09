#include "test_case_dsl.h"

#include "test_snapshot_support.h"

#include <string.h>

static bool test_case_dsl_sv_has_prefix(String_View sv, const char *prefix) {
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    if (!prefix || sv.count < prefix_len) return false;
    return memcmp(sv.data, prefix, prefix_len) == 0;
}

static String_View test_case_dsl_copy_sv(Arena *arena, String_View value) {
    char *copy = NULL;
    if (!arena) return nob_sv_from_cstr("");
    copy = arena_strndup(arena, value.data ? value.data : "", value.count);
    if (!copy) return nob_sv_from_cstr("");
    return nob_sv_from_parts(copy, value.count);
}

bool test_case_dsl_split_scoped_path(String_View raw,
                                     Test_Case_Dsl_Path_Scope default_scope,
                                     Test_Case_Dsl_Path_Scope *out_scope,
                                     String_View *out_relpath) {
    if (!out_scope || !out_relpath) return false;

    if (test_case_dsl_sv_has_prefix(raw, "source/")) {
        *out_scope = TEST_CASE_DSL_PATH_SCOPE_SOURCE;
        *out_relpath = nob_sv_from_parts(raw.data + 7, raw.count - 7);
        return true;
    }
    if (test_case_dsl_sv_has_prefix(raw, "build/")) {
        *out_scope = TEST_CASE_DSL_PATH_SCOPE_BUILD;
        *out_relpath = nob_sv_from_parts(raw.data + 6, raw.count - 6);
        return true;
    }

    *out_scope = default_scope;
    *out_relpath = raw;
    return true;
}

static bool test_case_dsl_parse_scoped_path_entry(
    Arena *arena,
    String_View raw,
    Test_Case_Dsl_Path_Scope default_scope,
    Test_Case_Dsl_Path_Entry *out_entry) {
    Test_Case_Dsl_Path_Scope scope = TEST_CASE_DSL_PATH_SCOPE_SOURCE;
    String_View relpath = {0};

    if (!arena || !out_entry) return false;
    if (!test_case_dsl_split_scoped_path(raw, default_scope, &scope, &relpath)) return false;

    *out_entry = (Test_Case_Dsl_Path_Entry){
        .scope = scope,
        .relpath = test_case_dsl_copy_sv(arena, relpath),
    };
    return out_entry->relpath.data != NULL;
}

static bool test_case_dsl_parse_layout(String_View line,
                                       Test_Case_Dsl_Project_Layout *out_layout) {
    if (!out_layout) return false;
    if (nob_sv_eq(line, nob_sv_from_cstr("BODY_ONLY_PROJECT"))) {
        *out_layout = TEST_CASE_DSL_LAYOUT_BODY_ONLY_PROJECT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("RAW_CMAKELISTS"))) {
        *out_layout = TEST_CASE_DSL_LAYOUT_RAW_CMAKELISTS;
        return true;
    }
    return false;
}

static bool test_case_dsl_parse_mode(String_View line, Test_Case_Dsl_Mode *out_mode) {
    if (!out_mode) return false;
    if (nob_sv_eq(line, nob_sv_from_cstr("PROJECT"))) {
        *out_mode = TEST_CASE_DSL_MODE_PROJECT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("SCRIPT"))) {
        *out_mode = TEST_CASE_DSL_MODE_SCRIPT;
        return true;
    }
    return false;
}

static bool test_case_dsl_parse_query(Arena *arena,
                                      String_View line,
                                      Test_Case_Dsl_Query *out_query) {
    String_View rest = line;
    String_View first = {0};
    String_View second = {0};
    const char *space = NULL;

    if (!arena || !out_query) return false;
    *out_query = (Test_Case_Dsl_Query){0};

    if (nob_sv_eq(line, nob_sv_from_cstr("#@@QUERY STDOUT"))) {
        out_query->kind = TEST_CASE_DSL_QUERY_STDOUT;
        return true;
    }
    if (nob_sv_eq(line, nob_sv_from_cstr("#@@QUERY STDERR"))) {
        out_query->kind = TEST_CASE_DSL_QUERY_STDERR;
        return true;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY VAR "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_VAR;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY CACHE_DEFINED "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_CACHE_DEFINED;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_EXISTS "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_TARGET_EXISTS;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_PROP "))) {
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        first = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        second = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (first.count == 0 || second.count == 0) return false;
        out_query->kind = TEST_CASE_DSL_QUERY_TARGET_PROP;
        out_query->arg0 = test_case_dsl_copy_sv(arena, first);
        out_query->arg1 = test_case_dsl_copy_sv(arena, second);
        return out_query->arg0.data != NULL && out_query->arg1.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_EXISTS "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_FILE_EXISTS;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_TEXT "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_FILE_TEXT;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_SHA256 "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_FILE_SHA256;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TREE "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_TREE;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY CMAKE_PROP "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_CMAKE_PROP;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY GLOBAL_PROP "))) {
        out_query->kind = TEST_CASE_DSL_QUERY_GLOBAL_PROP;
        out_query->arg0 = test_case_dsl_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY DIR_PROP "))) {
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        first = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        second = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (first.count == 0 || second.count == 0) return false;
        out_query->kind = TEST_CASE_DSL_QUERY_DIR_PROP;
        out_query->arg0 = test_case_dsl_copy_sv(arena, first);
        out_query->arg1 = test_case_dsl_copy_sv(arena, second);
        return out_query->arg0.data != NULL && out_query->arg1.data != NULL;
    }

    return false;
}

static bool test_case_dsl_parse_env_op(Arena *arena,
                                       String_View line,
                                       Test_Case_Dsl_Env_Op *out_op) {
    String_View rest = line;
    String_View name = {0};
    String_View value = {0};
    const char *eq = NULL;
    const char *space = NULL;

    if (!arena || !out_op) return false;
    *out_op = (Test_Case_Dsl_Env_Op){0};

    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV_UNSET "))) {
        if (rest.count == 0) return false;
        out_op->kind = TEST_CASE_DSL_ENV_UNSET;
        out_op->name = test_case_dsl_copy_sv(arena, rest);
        return out_op->name.data != NULL;
    }

    rest = line;
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV_PATH "))) {
        Test_Case_Dsl_Path_Scope scope = TEST_CASE_DSL_PATH_SCOPE_SOURCE;
        String_View relpath = {0};
        space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        name = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        value = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (name.count == 0 || value.count == 0) return false;
        if (!test_case_dsl_split_scoped_path(value,
                                             TEST_CASE_DSL_PATH_SCOPE_SOURCE,
                                             &scope,
                                             &relpath)) {
            return false;
        }
        out_op->kind = TEST_CASE_DSL_ENV_SET_PATH;
        out_op->name = test_case_dsl_copy_sv(arena, name);
        out_op->path_scope = scope;
        out_op->path_relpath = test_case_dsl_copy_sv(arena, relpath);
        return out_op->name.data != NULL && out_op->path_relpath.data != NULL;
    }

    rest = line;
    if (!nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@ENV "))) return false;
    eq = memchr(rest.data, '=', rest.count);
    if (!eq) return false;
    name = nob_sv_from_parts(rest.data, (size_t)(eq - rest.data));
    value = nob_sv_from_parts(eq + 1, rest.count - (size_t)(eq - rest.data) - 1);
    if (name.count == 0) return false;

    out_op->kind = TEST_CASE_DSL_ENV_SET;
    out_op->name = test_case_dsl_copy_sv(arena, name);
    out_op->value = test_case_dsl_copy_sv(arena, value);
    return out_op->name.data != NULL && out_op->value.data != NULL;
}

static bool test_case_dsl_parse_cache_init(Arena *arena,
                                           String_View line,
                                           Test_Case_Dsl_Cache_Init *out_init) {
    String_View rest = line;
    String_View lhs = {0};
    String_View name = {0};
    String_View type = {0};
    String_View value = {0};
    const char *eq = NULL;
    const char *colon = NULL;

    if (!arena || !out_init) return false;
    *out_init = (Test_Case_Dsl_Cache_Init){0};

    if (!nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@CACHE_INIT "))) return false;
    eq = memchr(rest.data, '=', rest.count);
    if (!eq) return false;
    lhs = nob_sv_from_parts(rest.data, (size_t)(eq - rest.data));
    value = nob_sv_from_parts(eq + 1, rest.count - (size_t)(eq - rest.data) - 1);
    colon = memchr(lhs.data, ':', lhs.count);
    if (!colon) return false;
    name = nob_sv_from_parts(lhs.data, (size_t)(colon - lhs.data));
    type = nob_sv_from_parts(colon + 1, lhs.count - (size_t)(colon - lhs.data) - 1);
    if (name.count == 0 || type.count == 0) return false;

    out_init->name = test_case_dsl_copy_sv(arena, name);
    out_init->type = test_case_dsl_copy_sv(arena, type);
    out_init->value = test_case_dsl_copy_sv(arena, value);
    return out_init->name.data != NULL &&
           out_init->type.data != NULL &&
           out_init->value.data != NULL;
}

bool test_case_dsl_parse_case(Arena *arena,
                              Test_Case_Pack_Entry entry,
                              Test_Case_Dsl_Case *out_case) {
    Nob_String_Builder body = {0};
    bool have_outcome = false;
    bool have_layout = false;
    bool have_mode = false;
    size_t pos = 0;

    if (!arena || !out_case) return false;
    *out_case = (Test_Case_Dsl_Case){
        .mode = TEST_CASE_DSL_MODE_PROJECT,
        .layout = TEST_CASE_DSL_LAYOUT_BODY_ONLY_PROJECT,
    };
    out_case->name = test_case_dsl_copy_sv(arena, entry.name);
    if (!out_case->name.data) return false;

    while (pos < entry.script.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < entry.script.count && entry.script.data[line_end] != '\n') line_end++;
        pos = line_end < entry.script.count ? line_end + 1 : line_end;

        String_View raw_line = nob_sv_from_parts(entry.script.data + line_start, line_end - line_start);
        String_View line = test_case_pack_trim_cr(raw_line);

        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@OUTCOME "))) {
            if (have_outcome) {
                nob_sb_free(body);
                return false;
            }
            if (nob_sv_eq(line, nob_sv_from_cstr("SUCCESS"))) {
                out_case->expected_outcome = TEST_CASE_DSL_EXPECT_SUCCESS;
            } else if (nob_sv_eq(line, nob_sv_from_cstr("ERROR"))) {
                out_case->expected_outcome = TEST_CASE_DSL_EXPECT_ERROR;
            } else {
                nob_sb_free(body);
                return false;
            }
            have_outcome = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@MODE "))) {
            if (have_mode || !test_case_dsl_parse_mode(line, &out_case->mode)) {
                nob_sb_free(body);
                return false;
            }
            if (out_case->mode == TEST_CASE_DSL_MODE_SCRIPT && have_layout) {
                nob_sb_free(body);
                return false;
            }
            have_mode = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@PROJECT_LAYOUT "))) {
            if (have_layout || !test_case_dsl_parse_layout(line, &out_case->layout)) {
                nob_sb_free(body);
                return false;
            }
            if (out_case->mode == TEST_CASE_DSL_MODE_SCRIPT) {
                nob_sb_free(body);
                return false;
            }
            have_layout = true;
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE "))) {
            Test_Case_Dsl_Path_Entry file = {0};
            if (!test_case_dsl_parse_scoped_path_entry(arena,
                                                       line,
                                                       TEST_CASE_DSL_PATH_SCOPE_SOURCE,
                                                       &file) ||
                !arena_arr_push(arena, out_case->files, file)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@DIR "))) {
            Test_Case_Dsl_Path_Entry dir = {0};
            if (!test_case_dsl_parse_scoped_path_entry(arena,
                                                       line,
                                                       TEST_CASE_DSL_PATH_SCOPE_SOURCE,
                                                       &dir) ||
                !arena_arr_push(arena, out_case->dirs, dir)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE_TEXT "))) {
            Test_Case_Dsl_Text_Fixture text_file = {0};
            Nob_String_Builder text = {0};
            Test_Case_Dsl_Path_Entry path_entry = {0};
            bool found_end = false;

            if (!test_case_dsl_parse_scoped_path_entry(arena,
                                                       line,
                                                       TEST_CASE_DSL_PATH_SCOPE_SOURCE,
                                                       &path_entry)) {
                nob_sb_free(text);
                nob_sb_free(body);
                return false;
            }

            while (pos < entry.script.count) {
                size_t text_line_start = pos;
                size_t text_line_end = pos;
                while (text_line_end < entry.script.count &&
                       entry.script.data[text_line_end] != '\n') {
                    text_line_end++;
                }
                pos = text_line_end < entry.script.count ? text_line_end + 1 : text_line_end;

                String_View text_raw = nob_sv_from_parts(entry.script.data + text_line_start,
                                                         text_line_end - text_line_start);
                String_View text_line = test_case_pack_trim_cr(text_raw);
                if (nob_sv_eq(text_line, nob_sv_from_cstr("#@@END_FILE_TEXT"))) {
                    found_end = true;
                    break;
                }
                if (test_case_dsl_sv_has_prefix(text_line, "#@@")) {
                    nob_sb_free(text);
                    nob_sb_free(body);
                    return false;
                }
                nob_sb_append_buf(&text, text_raw.data, text_raw.count);
                nob_sb_append(&text, '\n');
            }

            if (!found_end) {
                nob_sb_free(text);
                nob_sb_free(body);
                return false;
            }

            text_file.scope = path_entry.scope;
            text_file.relpath = path_entry.relpath;
            text_file.text = test_case_dsl_copy_sv(arena,
                                                   nob_sv_from_parts(text.items ? text.items : "",
                                                                     text.count));
            nob_sb_free(text);
            if (!text_file.text.data ||
                !arena_arr_push(arena, out_case->text_files, text_file)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (test_case_dsl_sv_has_prefix(line, "#@@ENV ") ||
            test_case_dsl_sv_has_prefix(line, "#@@ENV_UNSET ") ||
            test_case_dsl_sv_has_prefix(line, "#@@ENV_PATH ")) {
            Test_Case_Dsl_Env_Op op = {0};
            if (!test_case_dsl_parse_env_op(arena, line, &op) ||
                !arena_arr_push(arena, out_case->env_ops, op)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (test_case_dsl_sv_has_prefix(line, "#@@CACHE_INIT ")) {
            Test_Case_Dsl_Cache_Init init = {0};
            if (!test_case_dsl_parse_cache_init(arena, line, &init) ||
                !arena_arr_push(arena, out_case->cache_inits, init)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (test_case_dsl_sv_has_prefix(line, "#@@QUERY ")) {
            Test_Case_Dsl_Query query = {0};
            if (!test_case_dsl_parse_query(arena, line, &query) ||
                !arena_arr_push(arena, out_case->queries, query)) {
                nob_sb_free(body);
                return false;
            }
            continue;
        }

        line = test_case_pack_trim_cr(raw_line);
        if (test_case_dsl_sv_has_prefix(line, "#@@")) {
            nob_sb_free(body);
            return false;
        }

        nob_sb_append_buf(&body, raw_line.data, raw_line.count);
        nob_sb_append(&body, '\n');
    }

    if (!have_outcome) {
        nob_sb_free(body);
        return false;
    }

    if (out_case->mode == TEST_CASE_DSL_MODE_SCRIPT) {
        for (size_t i = 0; i < arena_arr_len(out_case->files); ++i) {
            if (out_case->files[i].scope == TEST_CASE_DSL_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->dirs); ++i) {
            if (out_case->dirs[i].scope == TEST_CASE_DSL_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->text_files); ++i) {
            if (out_case->text_files[i].scope == TEST_CASE_DSL_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
        for (size_t i = 0; i < arena_arr_len(out_case->env_ops); ++i) {
            if (out_case->env_ops[i].kind == TEST_CASE_DSL_ENV_SET_PATH &&
                out_case->env_ops[i].path_scope == TEST_CASE_DSL_PATH_SCOPE_BUILD) {
                nob_sb_free(body);
                return false;
            }
        }
    }

    out_case->body = test_case_dsl_copy_sv(arena,
                                           nob_sv_from_parts(body.items ? body.items : "",
                                                             body.count));
    nob_sb_free(body);
    return out_case->body.data != NULL;
}

bool test_case_dsl_case_exists_in_pack(Arena *arena,
                                       const char *case_pack_path,
                                       const char *case_name) {
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;
    if (!arena || !case_pack_path || !case_name) return false;
    if (!test_snapshot_load_text_file_to_arena(arena, case_pack_path, &content) ||
        !test_snapshot_parse_case_pack_to_arena(arena, content, &entries)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        if (nob_sv_eq(entries[i].name, nob_sv_from_cstr(case_name))) return true;
    }
    return false;
}

bool test_case_dsl_load_case_from_pack(Arena *arena,
                                       const char *case_pack_path,
                                       const char *case_name,
                                       Test_Case_Dsl_Case *out_case) {
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;
    if (!arena || !case_pack_path || !case_name || !out_case) return false;
    if (!test_snapshot_load_text_file_to_arena(arena, case_pack_path, &content) ||
        !test_snapshot_parse_case_pack_to_arena(arena, content, &entries)) {
        return false;
    }
    for (size_t i = 0; i < arena_arr_len(entries); ++i) {
        if (!nob_sv_eq(entries[i].name, nob_sv_from_cstr(case_name))) continue;
        return test_case_dsl_parse_case(arena, entries[i], out_case);
    }
    return false;
}
