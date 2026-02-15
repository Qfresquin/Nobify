#ifndef TEST_TRANSPILER_SHARED_H_
#define TEST_TRANSPILER_SHARED_H_

#include "nob.h"
#include "lexer.h"
#include "parser.h"
#include "transpiler.h"
#include "arena.h" // <--- Necessário agora
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

// Helper para escrever arquivos temporários para testes
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

static int run_shell_command_silent(const char *cmd) {
    if (!cmd) return -1;
    return system(cmd);
}

static void remove_test_tree(const char *path) {
    if (!path || !path[0]) return;
#if defined(_WIN32)
    (void)run_shell_command_silent(nob_temp_sprintf("cmd /C if exist \"%s\" rmdir /S /Q \"%s\"", path, path));
#else
    (void)run_shell_command_silent(nob_temp_sprintf("rm -rf \"%s\"", path));
#endif
}

// Macros de teste adaptadas
#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, " FAILED: %s (line %d): %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    (*passed)++; \
} while(0)

// Helper para parsear string de entrada (Agora recebe a Arena)
static Ast_Root parse_cmake(Arena *arena, const char *input) {
    Lexer l = lexer_init(sv_from_cstr(input));
    Token_List tokens = {0};
    
    Token t = lexer_next(&l);
    while (t.kind != TOKEN_END) {
        nob_da_append(&tokens, t);
        t = lexer_next(&l);
    }
    
    // Passa a arena para o parser
    Ast_Root root = parse_tokens(arena, tokens);
    
    // Os tokens foram alocados com malloc (nob_da_append no teste), então liberamos aqui.
    // A AST fica na arena.
    free(tokens.items); 
    return root;
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

// --- Testes ---


#endif // TEST_TRANSPILER_SHARED_H_
