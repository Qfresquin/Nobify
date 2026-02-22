#ifndef TEST_EVALUATOR_V2_SHARED_H_
#define TEST_EVALUATOR_V2_SHARED_H_

#include "test_v2_assert.h"
#include "test_evaluator_v2_snapshot.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) {
        return false;
    }
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_cmake(Arena *arena, const char *input) {
    Lexer l = lexer_init(nob_sv_from_cstr(input));
    Token_List tokens = {0};

    for (;;) {
        Token t = lexer_next(&l);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &tokens, t)) return (Ast_Root){0};
    }

    return parse_tokens(arena, tokens);
}

static int run_shell_command_silent(const char *cmd) {
    if (!cmd) return -1;
    return system(cmd);
}

static void write_test_file(const char *path, const char *content) {
    if (!nob_write_entire_file(path, content, strlen(content))) {
        printf("    ! Failed to write test file: %s\n", path);
    }
}

static void remove_test_dir(const char *path) {
#if defined(_WIN32)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void remove_test_tree(const char *path) {
    if (!path || !path[0]) return;
#if defined(_WIN32)
    (void)run_shell_command_silent(nob_temp_sprintf("cmd /C if exist \"%s\" rmdir /S /Q \"%s\"", path, path));
#else
    (void)run_shell_command_silent(nob_temp_sprintf("rm -rf \"%s\"", path));
#endif
}

static int count_occurrences(const char *haystack, const char *needle) {
    if (!haystack || !needle || needle[0] == '\0') return 0;
    int count = 0;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

static String_View sv_dirname_temp(Arena *arena, const char *path) {
    if (!arena || !path || !path[0]) return nob_sv_from_cstr(".");

    String_View p = nob_sv_from_cstr(path);
    for (size_t i = p.count; i-- > 0;) {
        char c = p.data[i];
        if (c == '/' || c == '\\') {
            if (i == 0) return nob_sv_from_cstr("/");
            char *dir = arena_strndup(arena, p.data, i);
            if (!dir) return nob_sv_from_cstr(".");
            return nob_sv_from_cstr(dir);
        }
    }

    return nob_sv_from_cstr(".");
}

static bool evaluator_snapshot_from_ast(Ast_Root root,
                                        const char *input_path,
                                        Nob_String_Builder *sb) {
    if (!sb) return false;

    Arena *temp_arena = arena_create(8 * 1024 * 1024);
    Arena *event_arena = arena_create(8 * 1024 * 1024);
    if (!temp_arena || !event_arena) {
        if (temp_arena) arena_destroy(temp_arena);
        if (event_arena) arena_destroy(event_arena);
        return false;
    }

    Cmake_Event_Stream stream = {0};
    String_View source_dir = sv_dirname_temp(temp_arena, input_path ? input_path : "CMakeLists.txt");

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = &stream;
    init.source_dir = source_dir;
    init.binary_dir = source_dir;
    init.current_file = input_path ? input_path : "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    bool ok = ctx != NULL;
    if (ctx) {
        ok = evaluator_run(ctx, root);
    }

    String_View snapshot = {0};
    if (!evaluator_render_snapshot_to_arena(event_arena,
                                            &stream,
                                            nob_get_current_dir_temp(),
                                            &snapshot)) {
        ok = false;
    } else {
        nob_sb_append_buf(sb, snapshot.data, snapshot.count);
    }

    arena_destroy(event_arena);
    arena_destroy(temp_arena);
    return ok;
}

static void transpile_datree(Ast_Root root, Nob_String_Builder *sb) {
    (void)evaluator_snapshot_from_ast(root, "CMakeLists.txt", sb);
}

static void transpile_datree_with_input_path(Ast_Root root, Nob_String_Builder *sb, const char *input_path) {
    (void)evaluator_snapshot_from_ast(root, input_path, sb);
}

typedef struct {
    bool continue_on_fatal_error;
} Transpiler_Run_Options;

static void transpile_datree_ex(Ast_Root root,
                                Nob_String_Builder *sb,
                                const Transpiler_Run_Options *options) {
    (void)options;
    (void)evaluator_snapshot_from_ast(root, "CMakeLists.txt", sb);
}

static bool assert_evaluator_golden(const char *input_path,
                                    const char *expected_path,
                                    const char *input_file_for_origin) {
    Arena *arena = arena_create(2 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
    bool ok = true;

    if (!evaluator_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Ast_Root root = parse_cmake(arena, input.data);
    Nob_String_Builder sb = {0};
    transpile_datree_with_input_path(root, &sb, input_file_for_origin ? input_file_for_origin : input_path);

    String_View actual = nob_sv_from_parts(sb.items, sb.count);
    String_View actual_norm = evaluator_normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        nob_sb_free(sb);
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        nob_sb_free(sb);
        goto done;
    }

    String_View expected_norm = evaluator_normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(actual_norm, expected_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

    nob_sb_free(sb);

done:
    arena_destroy(arena);
    return ok;
}

#endif // TEST_EVALUATOR_V2_SHARED_H_
