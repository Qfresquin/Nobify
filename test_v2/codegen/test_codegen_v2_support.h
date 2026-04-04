#ifndef TEST_CODEGEN_V2_SUPPORT_H_
#define TEST_CODEGEN_V2_SUPPORT_H_

#include "test_semantic_pipeline.h"
#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "build_model_query.h"
#include "diagnostics.h"
#include "nob_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *source_dir;
    const char *binary_dir;
} Codegen_Test_Config;

void codegen_test_set_repo_root(const char *repo_root);
bool codegen_render_script(const char *script,
                           const char *input_path,
                           const char *output_path,
                           Nob_String_Builder *out);
bool codegen_render_script_with_config(const char *script,
                                       const Codegen_Test_Config *config,
                                       Nob_String_Builder *out);
bool codegen_write_script(const char *script,
                          const char *input_path,
                          const char *output_path);
bool codegen_write_script_with_config(const char *script,
                                      const Codegen_Test_Config *config);
bool codegen_load_text_file_to_arena(Arena *arena, const char *path, String_View *out);
bool codegen_sv_contains(String_View sv, const char *needle);
bool codegen_compile_generated_nob(const char *generated_path, const char *output_path);
bool codegen_write_text_file(const char *path, const char *text);
bool codegen_run_binary_in_dir(const char *dir,
                               const char *binary_path,
                               const char *arg1,
                               const char *arg2);

#endif // TEST_CODEGEN_V2_SUPPORT_H_
