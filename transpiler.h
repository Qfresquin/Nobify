#ifndef TRANSPILER_H_
#define TRANSPILER_H_

#include "parser.h"
#include "arena.h"
#include "build_model.h"

// ============================================================================
// CONTEXTO DE AVALIAÇÃO
// ============================================================================

// Escopo de variáveis (armazenamento temporário durante avaliação)
typedef struct {
    struct {
        String_View *keys;
        String_View *values;
        size_t count;
        size_t capacity;
    } vars;
} Eval_Scope;

typedef struct {
    String_View name;
    String_View *params;
    size_t param_count;
    Node_List body;
} Eval_Macro;

typedef struct {
    String_View name;
    String_View *params;
    size_t param_count;
    Node_List body;
} Eval_Function;

typedef struct {
    String_View *keys;
    String_View *values;
    bool *was_set;
    size_t count;
} Eval_Check_State_Frame;

// Contexto de avaliação (substitui o antigo Transpiler_Context)
typedef struct {
    Arena *arena;           // Arena para todas as alocações
    Build_Model *model;     // Modelo do build sendo construído
    
    // Escopos de variáveis
    Eval_Scope *scopes;
    size_t scope_count;
    size_t scope_capacity;
    
    // Diretórios atuais (para resolução de caminhos)
    String_View current_source_dir;
    String_View current_binary_dir;
    String_View current_list_dir;
    
    // Estado da avaliação
    bool skip_evaluation;  // Para IFs falsos
    bool in_function_call;
    size_t loop_depth;
    bool break_requested;
    bool continue_requested;
    bool return_requested;
    bool continue_on_fatal_error;
    
    // Pilha de chamadas (para recursion)
    struct {
        String_View *names;
        size_t count;
        size_t capacity;
    } call_stack;
    struct {
        String_View *paths;
        size_t count;
        size_t capacity;
    } include_stack;

    struct {
        Eval_Macro *items;
        size_t count;
        size_t capacity;
    } macros;

    struct {
        Eval_Function *items;
        size_t count;
        size_t capacity;
    } functions;
    struct {
        Eval_Check_State_Frame *items;
        size_t count;
        size_t capacity;
    } check_state_stack;
} Evaluator_Context;

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================

// Transpila uma AST para código C (nob.h)
void transpile_datree(Ast_Root root, String_Builder *sb);
void transpile_datree_with_input_path(Ast_Root root, String_Builder *sb, const char *input_path);
void transpiler_set_continue_on_fatal_error(bool enabled);

// Funções auxiliares para testes e debug
void define_cache_var_build_model(Build_Model *model, const char *key, const char *val);
void dump_evaluation_context(const Evaluator_Context *ctx);

#endif // TRANSPILER_H_


