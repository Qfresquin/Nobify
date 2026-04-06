#ifndef TEST_SEMANTIC_PIPELINE_H_
#define TEST_SEMANTIC_PIPELINE_H_

#include "arena.h"
#include "build_model_builder.h"
#include "build_model_freeze.h"
#include "build_model_validate.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"

typedef struct {
    const char *current_file;
    String_View source_dir;
    String_View binary_dir;
    bool enable_export_host_effects;
    bool override_enable_export_host_effects;
    size_t parse_arena_size;
    size_t scratch_arena_size;
    size_t event_arena_size;
    size_t builder_arena_size;
    size_t validate_arena_size;
    size_t model_arena_size;
} Test_Semantic_Pipeline_Config;

typedef struct {
    Diag_Sink *sink;
    const Build_Model_Draft *draft;
    const Build_Model *model;
    bool builder_ok;
    bool validate_ok;
    bool freeze_ok;
} Test_Semantic_Pipeline_Build_Result;

typedef struct {
    Arena *parse_arena;
    Arena *scratch_arena;
    Arena *event_arena;
    Arena *builder_arena;
    Arena *validate_arena;
    Arena *model_arena;
    Ast_Root ast;
    Event_Stream *stream;
    Event_Stream *build_stream;
    EvalRunResult eval_run;
    bool eval_ok;
    Test_Semantic_Pipeline_Build_Result build;
    String_View source_dir;
    String_View binary_dir;
    const char *current_file;
} Test_Semantic_Pipeline_Fixture;

void test_semantic_pipeline_config_init(Test_Semantic_Pipeline_Config *config);
Ast_Root test_semantic_pipeline_parse_cmake(Arena *arena, const char *script);
Event_Stream *test_semantic_pipeline_wrap_stream_with_root(Arena *arena,
                                                           const Event_Stream *stream,
                                                           const char *current_file,
                                                           String_View source_dir,
                                                           String_View binary_dir);
bool test_semantic_pipeline_build_model_from_stream(
    Arena *builder_arena,
    Arena *validate_arena,
    Arena *model_arena,
    const Event_Stream *stream,
    Test_Semantic_Pipeline_Build_Result *out);
bool test_semantic_pipeline_fixture_from_script(Test_Semantic_Pipeline_Fixture *out,
                                                const char *script,
                                                const Test_Semantic_Pipeline_Config *config);
void test_semantic_pipeline_fixture_destroy(Test_Semantic_Pipeline_Fixture *fixture);

#endif // TEST_SEMANTIC_PIPELINE_H_
