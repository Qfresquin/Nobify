#if defined(_WIN32)
#if defined(_MSC_VER) && !defined(__clang__)
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "cl.exe", nob_temp_sprintf("/Fe:%s", (binary_path)), \
    (source_path), "lexer.c", "parser.c", "transpiler.c", "arena.c", "build_model.c", "diagnostics.c", "sys_utils.c", "toolchain_driver.c", "math_parser.c", "genex_evaluator.c", "ds_adapter.c"
#elif defined(__clang__)
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "clang", "-x", "c", "-o", (binary_path), \
    (source_path), "lexer.c", "parser.c", "transpiler.c", "arena.c", "build_model.c", "diagnostics.c", "sys_utils.c", "toolchain_driver.c", "math_parser.c", "genex_evaluator.c", "ds_adapter.c"
#else
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "gcc", "-x", "c", "-o", (binary_path), \
    (source_path), "lexer.c", "parser.c", "transpiler.c", "arena.c", "build_model.c", "diagnostics.c", "sys_utils.c", "toolchain_driver.c", "math_parser.c", "genex_evaluator.c", "ds_adapter.c"
#endif
#else
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "cc", "-x", "c", "-o", (binary_path), \
    (source_path), "lexer.c", "parser.c", "transpiler.c", "arena.c", "build_model.c", "diagnostics.c", "sys_utils.c", "toolchain_driver.c", "math_parser.c", "genex_evaluator.c", "ds_adapter.c"
#endif

#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION
// Inclusão dos módulos do projeto
#include "arena.h"
#include "arena_dyn.h"
#include "lexer.h"
#include "parser.h"
#include "transpiler.h"
#include "diagnostics.h"

// Append defensivo na lista de tokens usando a arena.
static bool main_token_list_append(Arena *arena, Token_List *list, Token item) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(*list->items), list->count + 1)) {
        return false;
    }
    list->items[list->count++] = item;
    return true;
}

int main(int argc, char **argv) {
    // 1. Sistema de Rebuild Próprio do NOB (Bootstrapping)
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv,
        "lexer.c", "parser.c", "transpiler.c", "arena.c", "build_model.c", "diagnostics.c", "sys_utils.c", "toolchain_driver.c", "math_parser.c", "genex_evaluator.c", "ds_adapter.c");

    bool strict_mode = false;
    bool continue_on_fatal_error = false;
    bool write_output_on_error = false;
    const char *input_path = "test.txt";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            strict_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--continue-on-fatal-error") == 0) {
            continue_on_fatal_error = true;
            continue;
        }
        if (strcmp(argv[i], "--write-output-on-error") == 0) {
            write_output_on_error = true;
            continue;
        }
        if (argv[i][0] == '-') {
            nob_log(NOB_ERROR, "Opcao desconhecida: %s", argv[i]);
            return 1;
        }
        input_path = argv[i];
    }

    diag_reset();
    diag_set_strict(strict_mode);
    diag_telemetry_reset();
    transpiler_set_continue_on_fatal_error(continue_on_fatal_error);

    const char *output_filename = "nob_generated.c";
    
    nob_log(NOB_INFO, "--- Iniciando Conversao: %s -> %s%s ---",
            input_path, output_filename, strict_mode ? " (strict)" : "");
    if (continue_on_fatal_error) {
        nob_log(NOB_INFO, "Modo compatibilidade ativo: continuando apos message(FATAL_ERROR)");
    }

    // 2. Inicialização da Arena Global
    // Aloca 4MB iniciais (cresce automaticamente se necessário).
    // Esta arena vai segurar: Conteúdo do arquivo, Tokens e AST.
    Arena *global_arena = arena_create(4 * 1024 * 1024);
    if (!global_arena) {
        nob_log(NOB_ERROR, "Falha fatal: Nao foi possivel criar a Arena de memoria.");
        return 1;
    }

    // 3. Leitura do Arquivo
    // Lemos usando nob_sb e copiamos para a arena para garantir persistência
    Nob_String_Builder sb_input = {0};
    if (!nob_read_entire_file(input_path, &sb_input)) {
        nob_log(NOB_ERROR, "Falha ao ler o arquivo de entrada: %s", input_path);
        arena_destroy(global_arena);
        return 1;
    }
    
    // Copia o conteúdo para a arena e libera o buffer temporário do nob
    char *content_cstr = arena_strndup(global_arena, sb_input.items, sb_input.count);
    if (!content_cstr) {
        nob_log(NOB_ERROR, "Falha de memoria ao copiar conteudo de entrada para arena");
        nob_sb_free(sb_input);
        arena_destroy(global_arena);
        return 1;
    }
    String_View content = nob_sv_from_parts(content_cstr, sb_input.count);
    nob_sb_free(sb_input);

    // 4. Tokenização (Lexer)
    nob_log(NOB_INFO, "Etapa 1: Tokenizacao...");
    Lexer l = lexer_init(content);
    Token_List tokens = {0};
    
    Token t = lexer_next(&l);
    while (t.kind != TOKEN_END) {
        if (t.kind == TOKEN_INVALID) {
            nob_log(NOB_ERROR, "Erro de Sintaxe na linha %zu, col %zu: Token Invalido", t.line, t.col);
            // Em produção, poderíamos continuar para achar mais erros, mas aqui paramos.
            arena_destroy(global_arena);
            return 1;
        }
        // Armazena o token na Arena
        if (!main_token_list_append(global_arena, &tokens, t)) {
            nob_log(NOB_ERROR, "Falha de memoria ao armazenar tokens");
            arena_destroy(global_arena);
            return 1;
        }
        t = lexer_next(&l);
    }
    nob_log(NOB_INFO, "  -> Gerados %zu tokens.", tokens.count);

    // 5. Análise Sintática (Parser)
    // A função parse_tokens agora recebe a arena para alocar os nós da AST
    nob_log(NOB_INFO, "Etapa 2: Parsing (Analise Sintatica)...");
    Ast_Root root = parse_tokens(global_arena, tokens);
    nob_log(NOB_INFO, "  -> AST gerada com %zu nos raiz.", root.count);
    if (diag_has_errors()) {
        diag_telemetry_emit_summary();
        (void)diag_telemetry_write_report("cmk2nob_unsupported_commands.log", input_path);
        nob_log(NOB_ERROR, "Falha no parsing: %zu erro(s), %zu warning(s)",
                diag_error_count(), diag_warning_count());
        arena_destroy(global_arena);
        return 1;
    }
    
    // Debug opcional: imprimir a árvore
    // print_ast(root, 0);

    // 6. Transpilação / Avaliação
    // O transpiler cria sua própria arena interna para o contexto de avaliação,
    // mas usa a AST que está na global_arena.
    nob_log(NOB_INFO, "Etapa 3: Transpilacao e Geracao de Codigo...");
    Nob_String_Builder sb_output = {0};
    
    transpile_datree_with_input_path(root, &sb_output, input_path);
    if (sb_output.count == 0 || sb_output.items == NULL) {
        nob_log(NOB_ERROR, "Transpilacao falhou: nenhuma saida foi gerada");
        arena_destroy(global_arena);
        nob_sb_free(sb_output);
        return 1;
    }
    if (diag_has_errors()) {
        diag_telemetry_emit_summary();
        (void)diag_telemetry_write_report("cmk2nob_unsupported_commands.log", input_path);
        if (!write_output_on_error) {
            nob_log(NOB_ERROR, "Transpilacao falhou por diagnosticos: %zu erro(s), %zu warning(s)",
                    diag_error_count(), diag_warning_count());
            arena_destroy(global_arena);
            nob_sb_free(sb_output);
            return 1;
        }
        nob_log(NOB_WARNING,
                "Transpilacao com diagnosticos: escrevendo saida por --write-output-on-error (%zu erro(s), %zu warning(s))",
                diag_error_count(), diag_warning_count());
    }

    // 7. Salvar Resultado
    if (!nob_write_entire_file(output_filename, sb_output.items, sb_output.count)) {
        nob_log(NOB_ERROR, "Falha ao gravar arquivo de saida: %s", output_filename);
        arena_destroy(global_arena);
        nob_sb_free(sb_output);
        return 1;
    }

    // 8. Limpeza e Finalização
    // Liberar a arena destrói TUDO (Tokens, AST, Strings copiadas) de uma vez.
    arena_destroy(global_arena);
    nob_sb_free(sb_output);

    nob_log(NOB_INFO, "Sucesso!");
    diag_telemetry_emit_summary();
    (void)diag_telemetry_write_report("cmk2nob_unsupported_commands.log", input_path);
    if (diag_has_warnings()) {
        nob_log(NOB_WARNING, "Concluido com %zu warning(s)", diag_warning_count());
    }
    nob_log(NOB_INFO, "Arquivo gerado: %s", output_filename);
    nob_log(NOB_INFO, "Para compilar o projeto gerado, use:");
    nob_log(NOB_INFO, "  cc %s -o nob && ./nob", output_filename);

    return 0;
}
