#include "../evaluator/test_evaluator_v2_support.h"
#include "test_fs.h"

#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char *family_label;
    const char *case_pack_path;
} Diff_Case_Pack;

static const Diff_Case_Pack s_diff_case_packs[] = {
    {"target_usage", "test_v2/evaluator_diff/cases/target_usage_seed_cases.cmake"},
    {"list", "test_v2/evaluator_diff/cases/list_seed_cases.cmake"},
    {"var_commands", "test_v2/evaluator_diff/cases/var_commands_seed_cases.cmake"},
};

typedef enum {
    DIFF_EXPECT_SUCCESS = 0,
    DIFF_EXPECT_ERROR,
} Diff_Expected_Outcome;

typedef enum {
    DIFF_QUERY_VAR = 0,
    DIFF_QUERY_CACHE_DEFINED,
    DIFF_QUERY_TARGET_EXISTS,
    DIFF_QUERY_TARGET_PROP,
    DIFF_QUERY_FILE_EXISTS,
} Diff_Query_Kind;

typedef struct {
    Diff_Query_Kind kind;
    String_View arg0;
    String_View arg1;
} Diff_Query;

typedef struct {
    String_View relpath;
} Diff_Path_Entry;

typedef struct {
    String_View name;
    String_View body;
    Diff_Expected_Outcome expected_outcome;
    Diff_Path_Entry *files;
    Diff_Path_Entry *dirs;
    Diff_Query *queries;
} Diff_Case;

typedef struct {
    char cmake_bin[_TINYDIR_PATH_MAX];
    char cmake_version[64];
    bool available;
} Diff_Cmake_Config;

typedef struct {
    Diff_Expected_Outcome outcome;
    String_View snapshot;
    Eval_Run_Report report;
    bool have_report;
} Diff_Evaluator_Run;

typedef struct {
    Diff_Expected_Outcome outcome;
    bool command_started;
    String_View snapshot;
    String_View stdout_text;
    String_View stderr_text;
} Diff_Cmake_Run;

static bool diff_build_qualified_case_name(const char *family_label,
                                           String_View case_name,
                                           char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!family_label || !out) return false;
    n = snprintf(out,
                 _TINYDIR_PATH_MAX,
                 "%s::%.*s",
                 family_label,
                 (int)case_name.count,
                 case_name.data ? case_name.data : "");
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool diff_sv_has_prefix(String_View value, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    return value.count >= prefix_len && memcmp(value.data, prefix, prefix_len) == 0;
}

static bool diff_sv_contains(String_View value, String_View needle) {
    if (needle.count == 0 || value.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= value.count; i++) {
        if (memcmp(value.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static String_View diff_copy_sv(Arena *arena, String_View value) {
    char *copy = NULL;
    if (!arena) return nob_sv_from_cstr("");
    copy = arena_strndup(arena, value.data ? value.data : "", value.count);
    if (!copy) return nob_sv_from_cstr("");
    return nob_sv_from_parts(copy, value.count);
}

static bool diff_ensure_dir_chain(const char *path) {
    char buffer[_TINYDIR_PATH_MAX] = {0};
    size_t len = 0;
    size_t start = 0;

    if (!path || path[0] == '\0') return true;
    len = strlen(path);
    if (len + 1 > sizeof(buffer)) return false;

    memcpy(buffer, path, len + 1);
#if defined(_WIN32)
    if (len >= 2 && buffer[1] == ':') start = 2;
#else
    if (buffer[0] == '/') start = 1;
#endif
    for (size_t i = start + 1; i < len; i++) {
        if (buffer[i] != '/' && buffer[i] != '\\') continue;
        buffer[i] = '\0';
        if (buffer[0] != '\0' && !nob_mkdir_if_not_exists(buffer)) return false;
        buffer[i] = '/';
    }
    return nob_mkdir_if_not_exists(buffer);
}

static bool diff_ensure_parent_dir(const char *path) {
    size_t temp_mark = nob_temp_save();
    const char *dir = nob_temp_dir_name(path);
    bool ok = diff_ensure_dir_chain(dir);
    nob_temp_rewind(temp_mark);
    return ok;
}

static bool diff_write_entire_file(const char *path, const char *data) {
    size_t size = data ? strlen(data) : 0;
    if (!diff_ensure_parent_dir(path)) return false;
    return nob_write_entire_file(path, data ? data : "", size);
}

static bool diff_make_empty_file(const char *path) {
    return diff_write_entire_file(path, "");
}

static bool diff_path_is_executable(const char *path) {
    if (!path || path[0] == '\0') return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

static bool diff_copy_string(const char *src, char out[_TINYDIR_PATH_MAX]) {
    int n = 0;
    if (!src || !out) return false;
    n = snprintf(out, _TINYDIR_PATH_MAX, "%s", src);
    return n >= 0 && n < _TINYDIR_PATH_MAX;
}

static bool diff_read_text_file(Arena *arena, const char *path, String_View *out) {
    if (out) *out = nob_sv_from_cstr("");
    if (!arena || !path || !out) return false;
    if (!test_ws_host_path_exists(path)) return true;
    return evaluator_load_text_file_to_arena(arena, path, out);
}

static void diff_sanitize_name(String_View name, char out[128]) {
    size_t wi = 0;
    if (!out) return;
    memset(out, 0, 128);
    if (name.count == 0) {
        memcpy(out, "case", 5);
        return;
    }

    for (size_t i = 0; i < name.count && wi + 1 < 128; i++) {
        unsigned char ch = (unsigned char)name.data[i];
        out[wi++] = (char)(isalnum(ch) ? ch : '_');
    }
    out[wi] = '\0';
    if (wi == 0) memcpy(out, "case", 5);
}

static bool diff_append_bracket_quoted(Nob_String_Builder *sb, String_View value) {
    size_t eq_count = 0;
    if (!sb) return false;

    for (;;) {
        char closer[16] = {0};
        size_t closer_len = 0;

        closer[closer_len++] = ']';
        for (size_t i = 0; i < eq_count; i++) closer[closer_len++] = '=';
        closer[closer_len++] = ']';
        closer[closer_len] = '\0';

        if (diff_sv_contains(value, nob_sv_from_cstr(closer))) {
            eq_count++;
            if (eq_count > 8) return false;
            continue;
        }

        nob_sb_append(&sb[0], '[');
        for (size_t i = 0; i < eq_count; i++) nob_sb_append(&sb[0], '=');
        nob_sb_append(&sb[0], '[');
        nob_sb_append_buf(&sb[0], value.data, value.count);
        nob_sb_append(&sb[0], ']');
        for (size_t i = 0; i < eq_count; i++) nob_sb_append(&sb[0], '=');
        nob_sb_append(&sb[0], ']');
        return true;
    }
}

static bool diff_append_line_expr(Nob_String_Builder *sb,
                                  size_t index,
                                  String_View prefix) {
    if (!sb) return false;
    nob_sb_append_cstr(sb, nob_temp_sprintf("set(_NOB_DIFF_LINE_%zu ", index));
    if (!diff_append_bracket_quoted(sb, prefix)) return false;
    nob_sb_append_cstr(sb, ")\n");
    nob_sb_append_cstr(sb,
                       nob_temp_sprintf("string(APPEND _NOB_DIFF_LINE_%zu \"${_NOB_DIFF_VAL_%zu}\" ",
                                        index,
                                        index));
    nob_sb_append_cstr(sb, "\"${_NOB_DIFF_NL}\")\n");
    nob_sb_append_cstr(sb,
                       nob_temp_sprintf("file(APPEND \"${_NOB_DIFF_SNAPSHOT_PATH}\" \"${_NOB_DIFF_LINE_%zu}\")\n",
                                        index));
    return true;
}

static bool diff_parse_query(Arena *arena, String_View line, Diff_Query *out_query) {
    String_View rest = line;
    String_View first = {0};
    String_View second = {0};
    if (!arena || !out_query) return false;
    *out_query = (Diff_Query){0};

    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY VAR "))) {
        out_query->kind = DIFF_QUERY_VAR;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY CACHE_DEFINED "))) {
        out_query->kind = DIFF_QUERY_CACHE_DEFINED;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_EXISTS "))) {
        out_query->kind = DIFF_QUERY_TARGET_EXISTS;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY TARGET_PROP "))) {
        const char *space = memchr(rest.data, ' ', rest.count);
        if (!space) return false;
        first = nob_sv_from_parts(rest.data, (size_t)(space - rest.data));
        second = nob_sv_from_parts(space + 1, rest.count - (size_t)(space - rest.data) - 1);
        if (first.count == 0 || second.count == 0) return false;
        out_query->kind = DIFF_QUERY_TARGET_PROP;
        out_query->arg0 = diff_copy_sv(arena, first);
        out_query->arg1 = diff_copy_sv(arena, second);
        return out_query->arg0.data != NULL && out_query->arg1.data != NULL;
    }
    if (nob_sv_chop_prefix(&rest, nob_sv_from_cstr("#@@QUERY FILE_EXISTS "))) {
        out_query->kind = DIFF_QUERY_FILE_EXISTS;
        out_query->arg0 = diff_copy_sv(arena, rest);
        return out_query->arg0.data != NULL;
    }

    return false;
}

static bool diff_parse_case(Arena *arena,
                            Test_Case_Pack_Entry entry,
                            Diff_Case *out_case) {
    Nob_String_Builder body = {0};
    bool have_outcome = false;
    size_t pos = 0;

    if (!arena || !out_case) return false;
    *out_case = (Diff_Case){0};
    out_case->name = diff_copy_sv(arena, entry.name);

    while (pos < entry.script.count) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < entry.script.count && entry.script.data[line_end] != '\n') line_end++;
        pos = line_end < entry.script.count ? line_end + 1 : line_end;

        String_View raw_line = nob_sv_from_parts(entry.script.data + line_start, line_end - line_start);
        String_View line = test_case_pack_trim_cr(raw_line);

        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@OUTCOME "))) {
            if (have_outcome) return false;
            if (nob_sv_eq(line, nob_sv_from_cstr("SUCCESS"))) out_case->expected_outcome = DIFF_EXPECT_SUCCESS;
            else if (nob_sv_eq(line, nob_sv_from_cstr("ERROR"))) out_case->expected_outcome = DIFF_EXPECT_ERROR;
            else return false;
            have_outcome = true;
            continue;
        }
        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@FILE "))) {
            Diff_Path_Entry file = { .relpath = diff_copy_sv(arena, line) };
            if (file.relpath.data == NULL || !arena_arr_push(arena, out_case->files, file)) return false;
            continue;
        }
        line = test_case_pack_trim_cr(raw_line);
        if (nob_sv_chop_prefix(&line, nob_sv_from_cstr("#@@DIR "))) {
            Diff_Path_Entry dir = { .relpath = diff_copy_sv(arena, line) };
            if (dir.relpath.data == NULL || !arena_arr_push(arena, out_case->dirs, dir)) return false;
            continue;
        }
        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@QUERY ")) {
            Diff_Query query = {0};
            if (!diff_parse_query(arena, line, &query) || !arena_arr_push(arena, out_case->queries, query)) {
                return false;
            }
            continue;
        }
        line = test_case_pack_trim_cr(raw_line);
        if (diff_sv_has_prefix(line, "#@@")) return false;

        nob_sb_append_buf(&body, raw_line.data, raw_line.count);
        nob_sb_append(&body, '\n');
    }

    if (!have_outcome) {
        nob_sb_free(body);
        return false;
    }

    out_case->body = diff_copy_sv(arena, nob_sv_from_parts(body.items ? body.items : "", body.count));
    nob_sb_free(body);
    return out_case->body.data != NULL;
}

static bool diff_query_path_scope(String_View path,
                                  String_View *out_scope_var,
                                  String_View *out_suffix) {
    if (!out_scope_var || !out_suffix) return false;

    if (diff_sv_has_prefix(path, "source/")) {
        *out_scope_var = nob_sv_from_cstr("${CMAKE_SOURCE_DIR}");
        *out_suffix = nob_sv_from_parts(path.data + 7, path.count - 7);
        return true;
    }
    if (diff_sv_has_prefix(path, "build/")) {
        *out_scope_var = nob_sv_from_cstr("${CMAKE_BINARY_DIR}");
        *out_suffix = nob_sv_from_parts(path.data + 6, path.count - 6);
        return true;
    }

    *out_scope_var = nob_sv_from_cstr("${CMAKE_BINARY_DIR}");
    *out_suffix = path;
    return true;
}

static bool diff_build_probe_block(Arena *arena,
                                   const Diff_Case *diff_case,
                                   Nob_String_Builder *out) {
    if (!arena || !diff_case || !out) return false;

    nob_sb_append_cstr(out, "\n# --- nobify differential probe ---\n");
    nob_sb_append_cstr(out, "set(_NOB_DIFF_SNAPSHOT_PATH \"${CMAKE_BINARY_DIR}/diff_snapshot.txt\")\n");
    nob_sb_append_cstr(out, "set(_NOB_DIFF_NL \"\n\")\n");
    nob_sb_append_cstr(out, "file(WRITE \"${_NOB_DIFF_SNAPSHOT_PATH}\" \"OUTCOME=SUCCESS${_NOB_DIFF_NL}\")\n");

    for (size_t i = 0; i < arena_arr_len(diff_case->queries); i++) {
        const Diff_Query *query = &diff_case->queries[i];
        switch (query->kind) {
            case DIFF_QUERY_VAR: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("VAR:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("if(DEFINED %.*s)\n",
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"${%.*s}\")\n",
                                                    i,
                                                    (int)query->arg0.count,
                                                    query->arg0.data));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__UNDEFINED__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_CACHE_DEFINED: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("CACHE_DEFINED:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("get_property(_NOB_DIFF_VAL_%zu CACHE ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, " PROPERTY VALUE SET)\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("if(_NOB_DIFF_VAL_%zu)\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_TARGET_EXISTS: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("TARGET_EXISTS:%.*s=", (int)query->arg0.count, query->arg0.data));
                nob_sb_append_cstr(out, "if(TARGET ");
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_TARGET_PROP: {
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("TARGET_PROP:%.*s:%.*s=",
                                     (int)query->arg0.count,
                                     query->arg0.data,
                                     (int)query->arg1.count,
                                     query->arg1.data));
                nob_sb_append_cstr(out, "if(TARGET ");
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  get_target_property(_NOB_DIFF_VAL_%zu ", i));
                if (!diff_append_bracket_quoted(out, query->arg0)) return false;
                nob_sb_append_cstr(out, " ");
                if (!diff_append_bracket_quoted(out, query->arg1)) return false;
                nob_sb_append_cstr(out, ")\n");
                nob_sb_append_cstr(out,
                                   nob_temp_sprintf("  if(\"${_NOB_DIFF_VAL_%zu}\" STREQUAL \"_NOB_DIFF_VAL_%zu-NOTFOUND\")\n",
                                                    i,
                                                    i));
                nob_sb_append_cstr(out, nob_temp_sprintf("    set(_NOB_DIFF_VAL_%zu \"__UNSET__\")\n", i));
                nob_sb_append_cstr(out, "  endif()\n");
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"__MISSING_TARGET__\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }

            case DIFF_QUERY_FILE_EXISTS: {
                String_View scope_var = {0};
                String_View suffix = {0};
                String_View prefix = nob_sv_from_cstr(
                    nob_temp_sprintf("FILE_EXISTS:%.*s=", (int)query->arg0.count, query->arg0.data));
                if (!diff_query_path_scope(query->arg0, &scope_var, &suffix)) return false;

                nob_sb_append_cstr(out, "set(_NOB_DIFF_PATH_");
                nob_sb_append_cstr(out, nob_temp_sprintf("%zu \"", i));
                nob_sb_append_cstr(out, scope_var.data);
                if (suffix.count > 0) {
                    nob_sb_append_cstr(out, "/");
                    nob_sb_append_buf(out, suffix.data, suffix.count);
                }
                nob_sb_append_cstr(out, "\")\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("if(EXISTS \"${_NOB_DIFF_PATH_%zu}\")\n", i));
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"1\")\n", i));
                nob_sb_append_cstr(out, "else()\n");
                nob_sb_append_cstr(out, nob_temp_sprintf("  set(_NOB_DIFF_VAL_%zu \"0\")\n", i));
                nob_sb_append_cstr(out, "endif()\n");
                if (!diff_append_line_expr(out, i, prefix)) return false;
                break;
            }
        }
    }

    return true;
}

static bool diff_generate_cmakelists(Arena *arena,
                                     const Diff_Case *diff_case,
                                     const char *cmakelists_path,
                                     String_View *out_script) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (out_script) *out_script = nob_sv_from_cstr("");
    if (!arena || !diff_case || !cmakelists_path || !out_script) return false;

    nob_sb_append_cstr(&sb, "cmake_minimum_required(VERSION 3.28)\n");
    nob_sb_append_cstr(&sb, "project(DiffCase LANGUAGES C CXX)\n");
    nob_sb_append_buf(&sb, diff_case->body.data, diff_case->body.count);
    if (diff_case->body.count == 0 || diff_case->body.data[diff_case->body.count - 1] != '\n') {
        nob_sb_append(&sb, '\n');
    }
    if (!diff_build_probe_block(arena, diff_case, &sb)) goto defer;

    nob_sb_append(&sb, '\0');
    if (!diff_write_entire_file(cmakelists_path, sb.items ? sb.items : "")) goto defer;
    *out_script = diff_copy_sv(arena, nob_sv_from_cstr(sb.items ? sb.items : ""));
    ok = out_script->data != NULL;

defer:
    nob_sb_free(sb);
    return ok;
}

static bool diff_prepare_source_fixture(const Diff_Case *diff_case, const char *source_dir) {
    if (!diff_case || !source_dir) return false;

    for (size_t i = 0; i < arena_arr_len(diff_case->dirs); i++) {
        char path[_TINYDIR_PATH_MAX] = {0};
        if (!test_fs_join_path(source_dir, nob_temp_sv_to_cstr(diff_case->dirs[i].relpath), path)) return false;
        if (!diff_ensure_dir_chain(path)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(diff_case->files); i++) {
        char path[_TINYDIR_PATH_MAX] = {0};
        if (!test_fs_join_path(source_dir, nob_temp_sv_to_cstr(diff_case->files[i].relpath), path)) return false;
        if (!diff_make_empty_file(path)) return false;
    }

    return true;
}

static Diff_Expected_Outcome diff_evaluator_outcome_from_result(Eval_Result result,
                                                                const Eval_Run_Report *report) {
    if (eval_result_is_fatal(result)) return DIFF_EXPECT_ERROR;
    if (!report) return DIFF_EXPECT_ERROR;
    return report->error_count == 0 ? DIFF_EXPECT_SUCCESS : DIFF_EXPECT_ERROR;
}

static bool diff_run_evaluator_case(Arena *arena,
                                    const char *cmakelists_path,
                                    const char *source_dir,
                                    const char *binary_dir,
                                    Diff_Evaluator_Run *out_run) {
    Arena *temp_arena = NULL;
    Arena *event_arena = NULL;
    Cmake_Event_Stream *stream = NULL;
    Eval_Test_Init init = {0};
    Eval_Test_Runtime *ctx = NULL;
    String_View script = {0};
    Ast_Root root = {0};

    if (out_run) *out_run = (Diff_Evaluator_Run){0};
    if (!arena || !cmakelists_path || !source_dir || !binary_dir || !out_run) return false;

    temp_arena = arena_create(2 * 1024 * 1024);
    event_arena = arena_create(2 * 1024 * 1024);
    if (!temp_arena || !event_arena) goto defer;

    stream = event_stream_create(event_arena);
    if (!stream) goto defer;

    if (!evaluator_load_text_file_to_arena(temp_arena, cmakelists_path, &script)) goto defer;
    root = parse_cmake(temp_arena, nob_temp_sv_to_cstr(script));

    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(source_dir);
    init.binary_dir = nob_sv_from_cstr(binary_dir);
    init.current_file = cmakelists_path;
    init.exec_mode = EVAL_EXEC_MODE_PROJECT;

    ctx = eval_test_create(&init);
    if (!ctx) goto defer;

    Eval_Result result = eval_test_run(ctx, root);
    const Eval_Run_Report *report = eval_test_report(ctx);
    out_run->outcome = diff_evaluator_outcome_from_result(result, report);
    if (report) {
        out_run->report = *report;
        out_run->have_report = true;
    }

    if (out_run->outcome == DIFF_EXPECT_SUCCESS) {
        char snapshot_path[_TINYDIR_PATH_MAX] = {0};
        if (!test_fs_join_path(binary_dir, "diff_snapshot.txt", snapshot_path)) goto defer;
        if (!diff_read_text_file(arena, snapshot_path, &out_run->snapshot)) goto defer;
        out_run->snapshot = evaluator_normalize_newlines_to_arena(arena, out_run->snapshot);
    }

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    return true;

defer:
    if (ctx) eval_test_destroy(ctx);
    if (temp_arena) arena_destroy(temp_arena);
    if (event_arena) arena_destroy(event_arena);
    return false;
}

static bool diff_run_cmake_case(Arena *arena,
                                const Diff_Cmake_Config *config,
                                const char *source_dir,
                                const char *binary_dir,
                                Diff_Cmake_Run *out_run) {
    Nob_Cmd cmd = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    char snapshot_path[_TINYDIR_PATH_MAX] = {0};
    bool ok = false;

    if (out_run) *out_run = (Diff_Cmake_Run){0};
    if (!arena || !config || !config->available || !source_dir || !binary_dir || !out_run) return false;

    if (!test_fs_join_path(".", "cmake_stdout.txt", stdout_path) ||
        !test_fs_join_path(".", "cmake_stderr.txt", stderr_path) ||
        !test_fs_join_path(binary_dir, "diff_snapshot.txt", snapshot_path)) {
        return false;
    }

    nob_cmd_append(&cmd, config->cmake_bin, "-S", source_dir, "-B", binary_dir);
    ok = nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path);
    nob_cmd_free(cmd);

    out_run->command_started = true;
    out_run->outcome = ok ? DIFF_EXPECT_SUCCESS : DIFF_EXPECT_ERROR;

    if (!diff_read_text_file(arena, stdout_path, &out_run->stdout_text) ||
        !diff_read_text_file(arena, stderr_path, &out_run->stderr_text)) {
        return false;
    }
    out_run->stdout_text = evaluator_normalize_newlines_to_arena(arena, out_run->stdout_text);
    out_run->stderr_text = evaluator_normalize_newlines_to_arena(arena, out_run->stderr_text);

    if (ok) {
        if (!diff_read_text_file(arena, snapshot_path, &out_run->snapshot)) return false;
        out_run->snapshot = evaluator_normalize_newlines_to_arena(arena, out_run->snapshot);
    }

    return true;
}

static bool diff_copy_path_if_exists(const char *src, const char *dst) {
    Test_Fs_Path_Info info = {0};
    if (!src || !dst) return false;
    if (!test_fs_get_path_info(src, &info)) return false;
    if (!info.exists) return true;
    if (!diff_ensure_parent_dir(dst)) return false;
    if (info.is_dir) return test_fs_copy_tree(src, dst);
    return nob_copy_file(src, dst);
}

static bool diff_preserve_failure_artifacts(String_View case_name) {
    char cwd[_TINYDIR_PATH_MAX] = {0};
    char case_root[_TINYDIR_PATH_MAX] = {0};
    char cases_root[_TINYDIR_PATH_MAX] = {0};
    char suite_work[_TINYDIR_PATH_MAX] = {0};
    char failures_root[_TINYDIR_PATH_MAX] = {0};
    char failure_dir[_TINYDIR_PATH_MAX] = {0};
    char sanitized[128] = {0};
    static const char *k_entries[] = {
        "source",
        "build_eval",
        "build_cmake",
        "cmake_stdout.txt",
        "cmake_stderr.txt",
        "evaluator_snapshot.txt",
        "cmake_snapshot.txt",
        "case_summary.txt",
    };

    if (!test_fs_save_current_dir(cwd)) return false;
    if (!diff_copy_string(nob_temp_dir_name(cwd), case_root)) return false;
    if (!diff_copy_string(nob_temp_dir_name(case_root), cases_root)) return false;
    if (!diff_copy_string(nob_temp_dir_name(cases_root), suite_work)) return false;

    diff_sanitize_name(case_name, sanitized);
    if (!test_fs_join_path(suite_work, "__diff_failures", failures_root)) return false;
    if (!test_fs_join_path(failures_root, sanitized, failure_dir)) return false;
    if (!diff_ensure_dir_chain(failure_dir)) return false;
    if (!test_fs_remove_tree(failure_dir)) return false;
    if (!nob_mkdir_if_not_exists(failure_dir)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(k_entries); i++) {
        char src[_TINYDIR_PATH_MAX] = {0};
        char dst[_TINYDIR_PATH_MAX] = {0};
        if (!test_fs_join_path(cwd, k_entries[i], src) ||
            !test_fs_join_path(failure_dir, k_entries[i], dst) ||
            !diff_copy_path_if_exists(src, dst)) {
            return false;
        }
    }

    nob_log(NOB_ERROR, "preserved differential mismatch artifacts at %s", failure_dir);
    return true;
}

static bool diff_write_case_summary(const Diff_Cmake_Config *config,
                                    const Diff_Case_Pack *case_pack,
                                    const Diff_Case *diff_case,
                                    const char *qualified_case_name,
                                    const Diff_Evaluator_Run *eval_run,
                                    const Diff_Cmake_Run *cmake_run,
                                    const char *path) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (!config || !case_pack || !diff_case || !qualified_case_name || !eval_run || !cmake_run || !path) {
        return false;
    }

    nob_sb_append_cstr(&sb, nob_temp_sprintf("family=%s\n", case_pack->family_label));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("case_pack=%s\n", case_pack->case_pack_path));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("qualified_case=%s\n", qualified_case_name));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("case=%.*s\n", (int)diff_case->name.count, diff_case->name.data));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("expected=%s\n",
                                        diff_case->expected_outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("cmake_bin=%s\n", config->cmake_bin));
    nob_sb_append_cstr(&sb, nob_temp_sprintf("cmake_version=%s\n", config->cmake_version));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("evaluator_outcome=%s\n",
                                        eval_run->outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    nob_sb_append_cstr(&sb,
                       nob_temp_sprintf("cmake_outcome=%s\n",
                                        cmake_run->outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR"));
    if (eval_run->have_report) {
        nob_sb_append_cstr(&sb,
                           nob_temp_sprintf("evaluator_report=warnings:%zu errors:%zu unsupported:%zu overall:%d\n",
                                            eval_run->report.warning_count,
                                            eval_run->report.error_count,
                                            eval_run->report.unsupported_count,
                                            (int)eval_run->report.overall_status));
    }

    nob_sb_append(&sb, '\0');
    ok = diff_write_entire_file(path, sb.items ? sb.items : "");
    nob_sb_free(sb);
    return ok;
}

static bool diff_record_snapshots(const Diff_Evaluator_Run *eval_run,
                                  const Diff_Cmake_Run *cmake_run) {
    bool ok = true;
    ok = ok && diff_write_entire_file("evaluator_snapshot.txt",
                                      eval_run->snapshot.data ? nob_temp_sv_to_cstr(eval_run->snapshot) : "");
    ok = ok && diff_write_entire_file("cmake_snapshot.txt",
                                      cmake_run->snapshot.data ? nob_temp_sv_to_cstr(cmake_run->snapshot) : "");
    return ok;
}

static bool diff_case_matches(const Diff_Case *diff_case,
                              const Diff_Evaluator_Run *eval_run,
                              const Diff_Cmake_Run *cmake_run) {
    if (!diff_case || !eval_run || !cmake_run) return false;
    if (eval_run->outcome != diff_case->expected_outcome) return false;
    if (cmake_run->outcome != diff_case->expected_outcome) return false;
    if (diff_case->expected_outcome == DIFF_EXPECT_ERROR) return true;
    return nob_sv_eq(eval_run->snapshot, cmake_run->snapshot);
}

static bool diff_resolve_cmake(Diff_Cmake_Config *out_config,
                               char skip_reason[256]) {
    Nob_Cmd cmd = {0};
    char stdout_path[_TINYDIR_PATH_MAX] = {0};
    char stderr_path[_TINYDIR_PATH_MAX] = {0};
    String_View version_text = {0};
    Arena *arena = NULL;
    const char *env_path = NULL;
    bool found = false;

    if (out_config) *out_config = (Diff_Cmake_Config){0};
    if (skip_reason) skip_reason[0] = '\0';
    if (!out_config || !skip_reason) return false;

    env_path = getenv(CMK2NOB_TEST_CMAKE_BIN_ENV);
    if (env_path && env_path[0] != '\0') {
        if (strchr(env_path, '/') || strchr(env_path, '\\')) {
            found = diff_path_is_executable(env_path) && diff_copy_string(env_path, out_config->cmake_bin);
        } else {
            found = test_ws_host_program_in_path(env_path, out_config->cmake_bin);
        }
        if (!found) {
            snprintf(skip_reason, 256, "%s does not point to an executable", CMK2NOB_TEST_CMAKE_BIN_ENV);
            return true;
        }
    } else if (!test_ws_host_program_in_path("cmake", out_config->cmake_bin)) {
        snprintf(skip_reason, 256, "cmake not found in PATH");
        return true;
    }

    if (!test_fs_join_path(".", "__cmake_version_stdout.txt", stdout_path) ||
        !test_fs_join_path(".", "__cmake_version_stderr.txt", stderr_path)) {
        return false;
    }

    nob_cmd_append(&cmd, out_config->cmake_bin, "--version");
    if (!nob_cmd_run(&cmd, .stdout_path = stdout_path, .stderr_path = stderr_path)) {
        nob_cmd_free(cmd);
        snprintf(skip_reason, 256, "failed to execute cmake --version");
        return true;
    }
    nob_cmd_free(cmd);

    arena = arena_create(64 * 1024);
    if (!arena) return false;
    if (!diff_read_text_file(arena, stdout_path, &version_text)) {
        arena_destroy(arena);
        return false;
    }
    version_text = evaluator_normalize_newlines_to_arena(arena, version_text);

    if (!diff_sv_has_prefix(version_text, "cmake version 3.28.")) {
        size_t line_end = 0;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        snprintf(skip_reason,
                 256,
                 "requires CMake 3.28.x, found %.*s",
                 (int)line_end,
                 version_text.data);
        arena_destroy(arena);
        return true;
    }

    {
        const char *prefix = "cmake version ";
        size_t prefix_len = strlen(prefix);
        size_t line_end = prefix_len;
        while (line_end < version_text.count && version_text.data[line_end] != '\n') line_end++;
        size_t version_len = line_end - prefix_len;
        if (version_len >= sizeof(out_config->cmake_version)) version_len = sizeof(out_config->cmake_version) - 1;
        memcpy(out_config->cmake_version, version_text.data + prefix_len, version_len);
        out_config->cmake_version[version_len] = '\0';
    }

    out_config->available = true;
    arena_destroy(arena);
    return true;
}

static void run_diff_case(const Diff_Cmake_Config *config,
                          const Diff_Case_Pack *case_pack,
                          const Diff_Case *diff_case,
                          int *passed,
                          int *failed,
                          int *skipped) {
    Test_Case_Workspace ws = {0};
    Arena *arena = NULL;
    char case_cwd[_TINYDIR_PATH_MAX] = {0};
    char source_dir[_TINYDIR_PATH_MAX] = {0};
    char build_eval_dir[_TINYDIR_PATH_MAX] = {0};
    char build_cmake_dir[_TINYDIR_PATH_MAX] = {0};
    char cmakelists_path[_TINYDIR_PATH_MAX] = {0};
    char qualified_case_name[_TINYDIR_PATH_MAX] = {0};
    Diff_Evaluator_Run eval_run = {0};
    Diff_Cmake_Run cmake_run = {0};
    bool ok = false;

    if (!config || !case_pack || !diff_case || !passed || !failed || !skipped) return;
    (void)skipped;

    if (!diff_build_qualified_case_name(case_pack->family_label, diff_case->name, qualified_case_name) ||
        !test_ws_case_enter(&ws, qualified_case_name)) {
        test_v2_emit_failure_message(__func__, 0, "could not enter isolated differential test workspace");
        nob_log(NOB_ERROR, "FAILED: %s: could not enter isolated differential test workspace", qualified_case_name);
        (*failed)++;
        return;
    }

    arena = arena_create(512 * 1024);
    if (!arena) goto fail;

    if (!test_fs_save_current_dir(case_cwd) ||
        !test_fs_join_path(case_cwd, "source", source_dir) ||
        !test_fs_join_path(case_cwd, "build_eval", build_eval_dir) ||
        !test_fs_join_path(case_cwd, "build_cmake", build_cmake_dir) ||
        !test_fs_join_path(source_dir, "CMakeLists.txt", cmakelists_path)) {
        goto fail;
    }

    if (!nob_mkdir_if_not_exists(source_dir) ||
        !nob_mkdir_if_not_exists(build_eval_dir) ||
        !nob_mkdir_if_not_exists(build_cmake_dir) ||
        !diff_prepare_source_fixture(diff_case, source_dir) ||
        !diff_generate_cmakelists(arena, diff_case, cmakelists_path, &(String_View){0}) ||
        !diff_run_evaluator_case(arena, cmakelists_path, source_dir, build_eval_dir, &eval_run) ||
        !diff_run_cmake_case(arena, config, source_dir, build_cmake_dir, &cmake_run) ||
        !diff_record_snapshots(&eval_run, &cmake_run) ||
        !diff_write_case_summary(config,
                                 case_pack,
                                 diff_case,
                                 qualified_case_name,
                                 &eval_run,
                                 &cmake_run,
                                 "case_summary.txt")) {
        goto fail;
    }

    ok = diff_case_matches(diff_case, &eval_run, &cmake_run);
    if (!ok) {
        nob_log(NOB_ERROR,
                "differential mismatch in case %s (expected=%s evaluator=%s cmake=%s)",
                qualified_case_name,
                diff_case->expected_outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR",
                eval_run.outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR",
                cmake_run.outcome == DIFF_EXPECT_SUCCESS ? "SUCCESS" : "ERROR");
        if (eval_run.outcome == DIFF_EXPECT_SUCCESS && cmake_run.outcome == DIFF_EXPECT_SUCCESS) {
            nob_log(NOB_ERROR,
                    "snapshot mismatch in case %s\n--- evaluator ---\n%.*s--- cmake ---\n%.*s",
                    qualified_case_name,
                    (int)eval_run.snapshot.count,
                    eval_run.snapshot.data ? eval_run.snapshot.data : "",
                    (int)cmake_run.snapshot.count,
                    cmake_run.snapshot.data ? cmake_run.snapshot.data : "");
        }
        (void)diff_preserve_failure_artifacts(nob_sv_from_cstr(qualified_case_name));
        (*failed)++;
    } else {
        (*passed)++;
    }

    arena_destroy(arena);
    if (!test_ws_case_leave(&ws)) {
        nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated differential test workspace",
                qualified_case_name);
        (*failed)++;
    }
    return;

fail:
    nob_log(NOB_ERROR, "FAILED: %s: differential harness error", qualified_case_name);
    (void)diff_preserve_failure_artifacts(nob_sv_from_cstr(qualified_case_name));
    if (arena) arena_destroy(arena);
    (*failed)++;
    if (!test_ws_case_leave(&ws)) {
        nob_log(NOB_ERROR, "FAILED: %s: could not cleanup isolated differential test workspace",
                qualified_case_name);
        (*failed)++;
    }
}

static void run_evaluator_diff_case_pack(const Diff_Cmake_Config *config,
                                         const Diff_Case_Pack *case_pack,
                                         int *passed,
                                         int *failed,
                                         int *skipped) {
    Arena *arena = NULL;
    String_View content = {0};
    Test_Case_Pack_Entry *entries = NULL;

    if (!config || !case_pack || !passed || !failed || !skipped) return;
    (void)skipped;

    arena = arena_create(512 * 1024);
    if (!arena) {
        (*failed)++;
        return;
    }

    if (!evaluator_load_text_file_to_arena(arena, case_pack->case_pack_path, &content) ||
        !test_case_pack_parse(arena, content, &entries)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to parse %s", case_pack->case_pack_path);
        arena_destroy(arena);
        (*failed)++;
        return;
    }

    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        Diff_Case diff_case = {0};
        if (!diff_parse_case(arena, entries[i], &diff_case)) {
            nob_log(NOB_ERROR,
                    "evaluator diff suite: failed to parse metadata for family %s case %.*s",
                    case_pack->family_label,
                    (int)entries[i].name.count,
                    entries[i].name.data);
            (*failed)++;
            continue;
        }
        run_diff_case(config, case_pack, &diff_case, passed, failed, skipped);
    }

    arena_destroy(arena);
}

void run_evaluator_diff_v2_tests(int *passed, int *failed, int *skipped) {
    Test_Workspace ws = {0};
    Diff_Cmake_Config cmake = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    char skip_reason[256] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator_diff");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    if (!diff_resolve_cmake(&cmake, skip_reason)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to resolve cmake runtime");
        if (failed) (*failed)++;
    } else if (!cmake.available) {
        nob_log(NOB_INFO, "SKIPPED: evaluator diff suite: %s", skip_reason);
        if (skipped) (*skipped)++;
    } else {
        for (size_t i = 0; i < NOB_ARRAY_LEN(s_diff_case_packs); i++) {
            run_evaluator_diff_case_pack(&cmake, &s_diff_case_packs[i], passed, failed, skipped);
        }
    }

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator diff suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
