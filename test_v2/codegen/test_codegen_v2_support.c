#include "test_codegen_v2_support.h"

static char s_codegen_repo_root[_TINYDIR_PATH_MAX] = {0};

static bool codegen_run_binary(const char *binary_path, const char *arg1, const char *arg2) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!binary_path) return false;
    nob_cmd_append(&cmd, binary_path);
    if (arg1) nob_cmd_append(&cmd, arg1);
    if (arg2) nob_cmd_append(&cmd, arg2);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

void codegen_test_set_repo_root(const char *repo_root) {
    snprintf(s_codegen_repo_root, sizeof(s_codegen_repo_root), "%s", repo_root ? repo_root : "");
}

bool codegen_render_script(const char *script,
                           const char *input_path,
                           const char *output_path,
                           Nob_String_Builder *out) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    bool ok = false;
    if (!codegen_arena) return false;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    test_semantic_pipeline_config_init(&config);
    config.current_file = input_path ? input_path : "CMakeLists.txt";

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &config);
    if (!ok || !fixture.build.freeze_ok || !fixture.build.model || diag_has_errors()) {
        arena_destroy(codegen_arena);
        test_semantic_pipeline_fixture_destroy(&fixture);
        return false;
    }

    Nob_Codegen_Options opts = {
        .input_path = nob_sv_from_cstr(input_path),
        .output_path = nob_sv_from_cstr(output_path),
    };
    ok = nob_codegen_render(fixture.build.model, codegen_arena, &opts, out);

    arena_destroy(codegen_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    return ok;
}

bool codegen_write_script(const char *script,
                          const char *input_path,
                          const char *output_path) {
    Test_Semantic_Pipeline_Config config = {0};
    Test_Semantic_Pipeline_Fixture fixture = {0};
    Arena *codegen_arena = arena_create(8 * 1024 * 1024);
    bool ok = false;
    if (!codegen_arena) return false;

    diag_reset();
    diag_set_strict(false);
    diag_telemetry_reset();

    test_semantic_pipeline_config_init(&config);
    config.current_file = input_path ? input_path : "CMakeLists.txt";

    ok = test_semantic_pipeline_fixture_from_script(&fixture, script, &config);
    if (!ok || !fixture.build.freeze_ok || !fixture.build.model || diag_has_errors()) {
        arena_destroy(codegen_arena);
        test_semantic_pipeline_fixture_destroy(&fixture);
        return false;
    }

    Nob_Codegen_Options opts = {
        .input_path = nob_sv_from_cstr(input_path),
        .output_path = nob_sv_from_cstr(output_path),
    };
    ok = nob_codegen_write_file(fixture.build.model, codegen_arena, &opts);

    arena_destroy(codegen_arena);
    test_semantic_pipeline_fixture_destroy(&fixture);
    return ok;
}

bool codegen_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !path || !out) return false;
    if (!nob_read_entire_file(path, &sb)) return false;
    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

bool codegen_sv_contains(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

bool codegen_compile_generated_nob(const char *generated_path, const char *output_path) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (s_codegen_repo_root[0] == '\0' || !generated_path || !output_path) return false;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd,
                   "-D_GNU_SOURCE",
                   "-std=c11",
                   "-Wall",
                   "-Wextra",
                   nob_temp_sprintf("-I%s/vendor", s_codegen_repo_root),
                   "-o",
                   output_path,
                   generated_path);
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

bool codegen_write_text_file(const char *path, const char *text) {
    const char *dir = NULL;
    if (!path || !text) return false;
    dir = nob_temp_dir_name(path);
    if (dir && strcmp(dir, ".") != 0 && !nob_mkdir_if_not_exists(dir)) return false;
    return nob_write_entire_file(path, text, strlen(text));
}

bool codegen_run_binary_in_dir(const char *dir,
                               const char *binary_path,
                               const char *arg1,
                               const char *arg2) {
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    const char *cwd = nob_get_current_dir_temp();
    bool ok = false;
    if (!dir || !binary_path || !cwd) return false;
    if (strlen(cwd) + 1 > sizeof(prev_cwd)) return false;
    memcpy(prev_cwd, cwd, strlen(cwd) + 1);
    if (!nob_set_current_dir(dir)) return false;
    ok = codegen_run_binary(binary_path, arg1, arg2);
    if (!nob_set_current_dir(prev_cwd)) return false;
    return ok;
}
