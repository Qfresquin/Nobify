#ifndef EVALUATOR_H_
#define EVALUATOR_H_

// Evaluator v2
// - Input:  Ast_Root (parser.h)
// - Output: Cmake_Event_Stream (append-only)
// - Strict boundary: does NOT include or depend on build_model.h

#include <stddef.h>
#include <stdbool.h>

#include "nob.h"          // String_View
#include "arena.h"        // Arena
#include "parser.h"       // Ast_Root, Node, Args
#include "../transpiler/event_ir.h"

// -----------------------------------------------------------------------------
// Evaluator API
// -----------------------------------------------------------------------------

typedef struct Evaluator_Context Evaluator_Context;

typedef struct {
    Arena *arena;        // temporary expansions (rewound frequently)
    Arena *event_arena;  // persistent strings + event stream storage
    Cmake_Event_Stream *stream;

    String_View source_dir; // CMAKE_SOURCE_DIR
    String_View binary_dir; // CMAKE_BINARY_DIR

    const char *current_file; // for origin fallback
} Evaluator_Init;

Evaluator_Context *evaluator_create(const Evaluator_Init *init);
void evaluator_destroy(Evaluator_Context *ctx);

bool evaluator_run(Evaluator_Context *ctx, Ast_Root ast);

#endif // EVALUATOR_H_
