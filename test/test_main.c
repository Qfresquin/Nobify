#define NOB_IMPLEMENTATION
#include "nob.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


// nob não possui um sistema de delete recursivo de diretórios, então implementamos um aqui para limpar a sandbox dos testes.
#if defined(_WIN32)
    #include <windows.h> // RemoveDirectoryA, DeleteFileA, GetFileAttributesA
#else
    #include <unistd.h>  // rmdir
    #include <errno.h>
    #include <sys/stat.h>
#endif

// Forward declarations dos testes
void run_lexer_tests(int *passed, int *failed);
void run_parser_tests(int *passed, int *failed);
void run_transpiler_tests(int *passed, int *failed);
void run_arena_tests(int *passed, int *failed);
void run_build_model_tests(int *passed, int *failed);
void run_phase2_module_tests(int *passed, int *failed);
void run_logic_model_tests(int *passed, int *failed);
void run_transpiler_v2_diff_tests(int *passed, int *failed);

typedef void (*Test_Suite_Fn)(int *passed, int *failed);

static bool arg_has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

typedef struct {
    char *path;           // heap-owned
    Nob_File_Type type;
} Walk_Item;

typedef struct {
    Walk_Item *items;
    size_t count;
    size_t cap;
} Walk_List;

static void walk_list_push(Walk_List *wl, const char *path, Nob_File_Type type) {
    if (wl->count == wl->cap) {
        wl->cap = wl->cap ? wl->cap * 2 : 256;
        wl->items = (Walk_Item*)realloc(wl->items, wl->cap * sizeof(*wl->items));
        NOB_ASSERT(wl->items != NULL);
    }
    size_t n = strlen(path);
    char *dup = (char*)malloc(n + 1);
    NOB_ASSERT(dup != NULL);
    memcpy(dup, path, n + 1);
    wl->items[wl->count++] = (Walk_Item){ .path = dup, .type = type };
}

static bool walk_collect(Nob_Walk_Entry e) {
    Walk_List *wl = (Walk_List*)e.data;
    walk_list_push(wl, e.path, e.type);
    return true;
}

static bool delete_empty_dir(const char *path) {
#if defined(_WIN32)
    if (!RemoveDirectoryA(path)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "Could not remove directory %s: %s", path, nob_win32_error_message(err));
        return false;
    }
    return true;
#else
    if (rmdir(path) < 0) {
        if (errno == ENOENT) return true;
        nob_log(NOB_ERROR, "Could not remove directory %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif
}



static bool delete_file_ok_if_missing(const char *path) {
#if defined(_WIN32)
    if (!DeleteFileA(path)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        nob_log(NOB_ERROR, "Could not delete file %s: %s", path, nob_win32_error_message(err));
        return false;
    }
    return true;
#else
    if (remove(path) < 0) {
        if (errno == ENOENT) return true;
        nob_log(NOB_ERROR, "Could not delete file %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif
}

static bool path_exists_and_is_dir(const char *path, bool *is_dir_out) {
    if (is_dir_out) *is_dir_out = false;
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (is_dir_out) *is_dir_out = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
#else
    struct stat st;
    if (lstat(path, &st) < 0) return false;
    if (is_dir_out) *is_dir_out = S_ISDIR(st.st_mode);
    return true;
#endif
}

static void try_remove_root_sandbox(bool keep_temp) {
    if (keep_temp) return;

    bool is_dir = false;
    if (!path_exists_and_is_dir("temp_sandbox", &is_dir) || !is_dir) {
        return;
    }

    // Tenta remover (vai funcionar se estiver vazia)
    delete_empty_dir("temp_sandbox");
}


static bool delete_tree_if_exists(const char *root) {
    bool is_dir = false;
    if (!path_exists_and_is_dir(root, &is_dir)) {
        // não existe => ok
        return true;
    }

    if (!is_dir) {
        return delete_file_ok_if_missing(root);
    }

    Walk_List wl = {0};
    bool ok = nob_walk_dir(root, walk_collect, .data = &wl);
    if (!ok) {
        nob_log(NOB_ERROR, "delete_tree: walk failed for %s", root);
    }

    // Delete em ordem reversa: arquivos primeiro, dirs depois
    for (size_t i = wl.count; i-- > 0;) {
        Walk_Item it = wl.items[i];
        bool step_ok = true;

        if (it.type == NOB_FILE_DIRECTORY) {
            step_ok = delete_empty_dir(it.path);
        } else {
            step_ok = delete_file_ok_if_missing(it.path);
        }

        free(it.path);
        if (!step_ok) ok = false;
    }

    free(wl.items);
    return ok;
}

static void run_suite_in_sandbox(const char *suite_name,
                                 Test_Suite_Fn fn,
                                 int *passed, int *failed,
                                 bool keep_temp) {
    // Salva cwd em buffer estável (não depende do temp allocator)
    const char *cwd_tmp = nob_get_current_dir_temp();
    char cwd[4096] = {0};

    if (!cwd_tmp) {
        nob_log(NOB_ERROR, "could not get current dir; running in-place");
        fn(passed, failed);
        return;
    }
    strncpy(cwd, cwd_tmp, sizeof(cwd) - 1);

    (void)nob_mkdir_if_not_exists("temp_sandbox");

    // IMPORTANT: não usar nob_temp_sprintf aqui (pode ser sobrescrito por logs/erros do nob)
    char sandbox[4096] = {0};
#if defined(_WIN32)
    // Backslash reduz atrito com APIs Win32
    _snprintf(sandbox, sizeof(sandbox) - 1, "temp_sandbox\\%s", suite_name);
#else
    snprintf(sandbox, sizeof(sandbox) - 1, "temp_sandbox/%s", suite_name);
#endif

    if (!keep_temp) (void)delete_tree_if_exists(sandbox);
    (void)nob_mkdir_if_not_exists(sandbox);

    if (!nob_set_current_dir(sandbox)) {
        nob_log(NOB_ERROR, "could not chdir into sandbox: %s; running in-place", sandbox);
        fn(passed, failed);
        (void)nob_set_current_dir(cwd);
        return;
    }

    fn(passed, failed);

    (void)nob_set_current_dir(cwd);

    if (!keep_temp) (void)delete_tree_if_exists(sandbox);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    bool keep_temp = arg_has_flag(argc, argv, "--keep-temp");

    nob_log(NOB_INFO, "------------------------------------------");
    nob_log(NOB_INFO, "-   CMake2Nob Test Suite Runner          -");
    nob_log(NOB_INFO, "------------------------------------------");

    // Arena tests
    nob_log(NOB_INFO, "-> Running Arena Tests...");
    int arena_passed = 0, arena_failed = 0;
    run_suite_in_sandbox("arena", run_arena_tests, &arena_passed, &arena_failed, keep_temp);
    nob_log(NOB_INFO, "Arena Results: Passed: %d | Failed: %d", arena_passed, arena_failed);

    // Lexer tests
    nob_log(NOB_INFO, "-> Running Lexer Tests...");
    int lexer_passed = 0, lexer_failed = 0;
    run_suite_in_sandbox("lexer", run_lexer_tests, &lexer_passed, &lexer_failed, keep_temp);
    nob_log(NOB_INFO, "Lexer Results: Passed: %d | Failed: %d", lexer_passed, lexer_failed);

    // Parser tests
    nob_log(NOB_INFO, "-> Running Parser Tests...");
    int parser_passed = 0, parser_failed = 0;
    run_suite_in_sandbox("parser", run_parser_tests, &parser_passed, &parser_failed, keep_temp);
    nob_log(NOB_INFO, "Parser Results: Passed: %d | Failed: %d", parser_passed, parser_failed);

    // Build Model tests
    nob_log(NOB_INFO, "-> Running Build Model Tests...");
    int model_passed = 0, model_failed = 0;
    run_suite_in_sandbox("build_model", run_build_model_tests, &model_passed, &model_failed, keep_temp);
    nob_log(NOB_INFO, "Model Results: Passed: %d | Failed: %d", model_passed, model_failed);

    // Phase 2 module tests
    nob_log(NOB_INFO, "-> Running Phase 2 Module Tests...");
    int phase2_passed = 0, phase2_failed = 0;
    run_suite_in_sandbox("phase2_modules", run_phase2_module_tests, &phase2_passed, &phase2_failed, keep_temp);
    nob_log(NOB_INFO, "Phase 2 Modules Results: Passed: %d | Failed: %d", phase2_passed, phase2_failed);

    // Logic model tests (base da Fase 3)
    nob_log(NOB_INFO, "-> Running Logic Model Tests...");
    int logic_passed = 0, logic_failed = 0;
    run_suite_in_sandbox("logic_model", run_logic_model_tests, &logic_passed, &logic_failed, keep_temp);
    nob_log(NOB_INFO, "Logic Model Results: Passed: %d | Failed: %d", logic_passed, logic_failed);

    // Transpiler tests
    nob_log(NOB_INFO, "-> Running Transpiler Tests...");
    int transpiler_passed = 0, transpiler_failed = 0;
    run_suite_in_sandbox("transpiler", run_transpiler_tests, &transpiler_passed, &transpiler_failed, keep_temp);
    nob_log(NOB_INFO, "Transpiler Results: Passed: %d | Failed: %d", transpiler_passed, transpiler_failed);

    // Differential legacy vs v2 tests (compatibility gate during migration)
    nob_log(NOB_INFO, "-> Running Transpiler V2 Diff Tests...");
    int transpiler_v2_diff_passed = 0, transpiler_v2_diff_failed = 0;
    run_suite_in_sandbox("transpiler_v2_diff", run_transpiler_v2_diff_tests, &transpiler_v2_diff_passed, &transpiler_v2_diff_failed, keep_temp);
    nob_log(NOB_INFO, "Transpiler V2 Diff Results: Passed: %d | Failed: %d", transpiler_v2_diff_passed, transpiler_v2_diff_failed);

    int total_passed = arena_passed + lexer_passed + parser_passed + model_passed + phase2_passed + logic_passed + transpiler_passed + transpiler_v2_diff_passed;
    int total_failed = arena_failed + lexer_failed + parser_failed + model_failed + phase2_failed + logic_failed + transpiler_failed + transpiler_v2_diff_failed;

    nob_log(NOB_INFO, "------------------------------------------");
    nob_log(NOB_INFO, "-         Test Results Summary           -");
    nob_log(NOB_INFO, "------------------------------------------");

    nob_log(NOB_INFO, "Total Tests: %d", total_passed + total_failed);
    nob_log(NOB_INFO, "Passed:      %d", total_passed);

    if (total_failed > 0) {
        nob_log(NOB_ERROR, "Failed:      %d", total_failed);
        nob_log(NOB_ERROR, "Some tests failed!");
        try_remove_root_sandbox(keep_temp);
        return 1;
    }

    nob_log(NOB_INFO, "Failed:      0");
    nob_log(NOB_INFO, "All tests passed!");
    try_remove_root_sandbox(keep_temp);
    return 0;
}
