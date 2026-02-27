Com base na arquitetura **v2** definida para resolver os conflitos de responsabilidade e memória, esta é a Pipeline de Execução e a estrutura de arquivos C correspondente.

### 1. A Pipeline de Execução (Fluxo de Dados)

O fluxo agora é estritamente **linear**. Não há volta. O `Evaluator` não toca no `Build Model`, e o `Codegen` não toca na `AST`.

```
    A(CMakeLists.txt)
    A -->|Lexer/Parser|         B(AST - Abstract Syntax Tree)
    B -->|Evaluator|            C(Event Stream)
    C -->|Builder|              D(Builder State - Mutável)
    D -->|Validator & Freeze|   E(Build Model - Imutável)
    E -->|Codegen|              F(nob.c)
```

1.  **Parsing:** Converte texto em árvore de nós (`NODE_COMMAND`, `NODE_IF`).
2.  **Evaluation (A "CPU"):**
    *   Resolve variáveis (`${VAR}`).
    *   Executa controle de fluxo (`if`, `foreach`, `macro`).
    *   **Saída:** Uma lista linear de eventos *resolvidos* (ex: `EV_TARGET_ADD_SOURCE`).
3.  **Build Construction:** O Builder consome a lista de eventos e monta as estruturas (Targets, Props).
4.  **Freeze:** Valida integridade (ciclos, targets faltando), faz **Deep Copy** para uma arena final e tranca o modelo para leitura.
5.  **Codegen:** Itera sobre o modelo travado e escreve C puro.

---

### 2. Arquivos C (Responsabilidades)

Aqui estão os arquivos que você deve criar/manter para implementar essa pipeline, separados por módulo.

#### Módulo 1: Parser (Entrada)
*Mantido da v1, mas desacoplado.*
*   `src/parser/parser.c`: Lê o arquivo e gera a AST.
*   `src/parser/lexer.c`: Quebra o texto em tokens.

#### Módulo 2: Evaluator (O "Motor" - Antigo Transpiler)
*Aqui morre o `transpiler_evaluator.inc.c` gigante. Ele é quebrado em partes lógicas.*
*   `src/evaluator/evaluator.c`: O loop principal. Percorre a AST, gerencia a pilha de chamadas (`macros`/`functions`) e o escopo de variáveis (`set`/`unset`).
*   `src/evaluator/eval_dispatcher.c`: A tabela de "de-para". Mapeia strings de comando (`"add_executable"`) para funções que **emitem eventos**.
    *   *Ex:* A função `handle_add_executable` aqui não cria o target; ela emite o evento `EV_TARGET_DECLARE`.
*   `src/evaluator/eval_expr.c`: Avalia lógica booleana de `if()` e expansão de strings `${}`.

#### Módulo 3: Event IR (A Fronteira)
*A nova camada de contrato.*
*   `src/transpiler/event_ir.c`: Funções para criar, armazenar e iterar sobre o `Cmake_Event_Stream`. Define as structs dos eventos.

#### Módulo 4: Build Model (O Banco de Dados)
*Separado em Escrita (Builder) e Leitura (Model).*
*   `src/build_model/build_model_builder.c`:
    *   Implementa `build_model_apply_event()`.
    *   Recebe um evento cru e atualiza o estado temporário.
*   `src/build_model/build_model_validate.c`:
    *   Verifica ciclos, dependências fantasmas e regras de visibilidade.
*   `src/build_model/build_model_freeze.c`:
    *   Implementa `build_model_freeze()`.
    *   Cria a versão final (`Build_Model`) fazendo **Deep Copy** de tudo que está no Builder para a arena final, garantindo que o modelo sobreviva à destruição do Evaluator.
*   `src/build_model/build_model_query.c`: (Opcional) Getters otimizados para o Codegen ler o modelo.

#### Módulo 5: Codegen (A Saída)
*   `src/codegen/nob_codegen.c`:
    *   Recebe `const Build_Model*`.
    *   Escreve o `nob.c`.
    *   Não tem lógica de decisão complexa, apenas formatação de strings baseada nos dados do modelo.

#### Módulo 6: Driver (Orquestração)
*   `src/nobify.c` (Main):
    *   Chama `parser` -> `evaluator` -> `builder` -> `freeze` -> `codegen` na ordem correta.
    *   Gerencia as Arenas de memória (limpa a arena do Evaluator/Events após o Freeze para economizar RAM).