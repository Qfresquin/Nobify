<proposed_plan>
# Redefinição do Event IR como Contrato Principal e Rebaixamento do Legado para Obsoleto

## Objetivo

Mudar o eixo do projeto para que:

- o `Event IR` novo seja o único contrato principal de execução semântica
- o `Event IR` atual seja rebaixado para código obsoleto
- o `build_model` atual seja rebaixado para código obsoleto
- o `src`/fluxo legado deixe de ser a referência arquitetural
- nada no contrato novo seja chamado de “v3”; o nome correto passa a ser apenas `Event IR`

A decisão explícita é:
- o contrato atual de [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h) deixa de ser “o futuro”
- ele vira legado/obsoleto
- o novo contrato passa a ocupar o nome canônico de `event_ir`
- o builder atual também deixa de ser parte ativa da linha principal

## Mudança de Nomenclatura e Direção

## 1. Não usar “v3”

O novo contrato não deve nascer como:
- `event_ir_v3.h`
- `event_ir_v3.c`

Isso só perpetua a ideia de remendo incremental.

O desenho correto é:

- o **novo** contrato fica com o nome canônico:
  - [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h)
  - [`src_v2/transpiler/event_ir.c`](/home/pedro/Nobify/src_v2/transpiler/event_ir.c)

- o **contrato atual** é movido para obsoleto, por exemplo:
  - [`src_obsolete/transpiler/event_ir_legacy.h`](/home/pedro/Nobify/src_obsolete/transpiler/event_ir_legacy.h)
  - [`src_obsolete/transpiler/event_ir_legacy.c`](/home/pedro/Nobify/src_obsolete/transpiler/event_ir_legacy.c)

## 2. Rebaixar `build_model` atual para obsoleto

O `build_model` atual não deve ser mantido como parte viva da arquitetura principal enquanto o IR muda de natureza.

Mover para obsoleto, por exemplo:

- [`src_obsolete/build_model/build_model.c`](/home/pedro/Nobify/src_obsolete/build_model/build_model.c)
- [`src_obsolete/build_model/build_model_builder.c`](/home/pedro/Nobify/src_obsolete/build_model/build_model_builder.c)
- [`src_obsolete/build_model/build_model_validate.c`](/home/pedro/Nobify/src_obsolete/build_model/build_model_validate.c)
- [`src_obsolete/build_model/build_model_freeze.c`](/home/pedro/Nobify/src_obsolete/build_model/build_model_freeze.c)
- [`src_obsolete/build_model/build_model_query.c`](/home/pedro/Nobify/src_obsolete/build_model/build_model_query.c)
- headers associados

Se você não quiser mover fisicamente de imediato, a alternativa temporária é:
- mantê-los em `src_v2`, mas marcados como obsoletos e fora do build
- ainda assim, arquiteturalmente, eles deixam de ser “o sistema atual”

## 3. Rebaixar o fluxo legado para `src_obsolete`

Você mencionou “para `src` obsoleto”.
A leitura correta disso é:

- criar uma área explícita de legado, por exemplo:
  - `src_obsolete/`

Nela entram:
- IR estrutural antigo
- builder antigo
- quaisquer adaptadores que existirem só para compatibilidade histórica

Isso deixa claro que:
- `src_v2` continua sendo a linha ativa
- mas o contrato antigo sai da linha ativa
- o novo `event_ir` assume o nome principal dentro de `src_v2`

## Nova Arquitetura Alvo

## 1. O novo Event IR passa a ser a fonte de verdade

O evaluator deve emitir o novo `Event IR` completo da linguagem.

Ele passa a modelar:

- trace de execução
- controle de fluxo
- scopes
- policies
- variáveis/cache
- operações semânticas
- filesystem/process
- efeitos estruturais de build

O `Event IR` deixa de ser “IR do build model” e passa a ser:
- IR semântico primário da execução de CMake

## 2. O build model deixa de ser contrato de referência

O `build_model` futuro:
- será refeito depois
- será derivado do novo `Event IR`
- não dita mais o formato do IR

Isso inverte a dependência atual:
- antes: evaluator emitia pensando no builder
- agora: builder futuro será um consumidor do IR semântico

## 3. O IR estrutural antigo não deve contaminar o novo desenho

Ao mover o legado para obsoleto:
- não carregar enums antigos por compatibilidade
- não manter payloads antigos “só por enquanto”
- não misturar evento estrutural legado com semântico novo no mesmo header

Se for preciso consultar o desenho antigo:
- consulta-se o código em `src_obsolete`
- não se mistura no contrato novo

## Nova Taxonomia do Event IR (Canônico)

O novo [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h) deve ser redesenhado por famílias semânticas.

Famílias propostas:

1. `TRACE`
- começo/fim de comando
- começo/fim de bloco
- rastreio de execução

2. `DIAG`
- warning
- error
- unsupported
- compatibility notices

3. `FLOW`
- branch
- loop
- break
- continue
- return
- defer queue/flush

4. `SCOPE`
- push/pop de escopos
- troca de contexto de include/subdir/function/macro/block

5. `POLICY`
- push/pop/set
- status efetivo
- aplicação de versão

6. `VAR`
- set/unset normal
- cache
- env
- macro bindings
- watchers

7. `FS`
- write/read/copy/remove/mkdir/glob/chmod/rename/link/archive/transfer

8. `PROC`
- execução externa e resultado

9. `STRING`
- operações de `string(...)`

10. `LIST`
- operações de `list(...)`

11. `MATH`
- `math(EXPR ...)`

12. `PATH`
- `cmake_path(...)` e equivalentes semânticos

13. `PROJECT`
- `project`, `cmake_minimum_required`, `enable_language`

14. `TARGET`
- declaração e mutações de target

15. `TEST`
- `enable_testing`, `add_test`, CTest

16. `INSTALL`
- regras de install

17. `CPACK`
- metadata de CPack

18. `PACKAGE`
- `find_package`, `find_*`

19. `META`
- `include`, `add_subdirectory`, `cmake_language`, `export`, file-api, etc.

## Struct Base do Novo Event IR

## 1. Header comum

O novo `event_ir.h` deve definir um cabeçalho base unificado para todos os eventos.

Proposta:

```c
typedef enum {
    EVENT_FAMILY_TRACE = 0,
    EVENT_FAMILY_DIAG,
    EVENT_FAMILY_FLOW,
    EVENT_FAMILY_SCOPE,
    EVENT_FAMILY_POLICY,
    EVENT_FAMILY_VAR,
    EVENT_FAMILY_FS,
    EVENT_FAMILY_PROC,
    EVENT_FAMILY_STRING,
    EVENT_FAMILY_LIST,
    EVENT_FAMILY_MATH,
    EVENT_FAMILY_PATH,
    EVENT_FAMILY_PROJECT,
    EVENT_FAMILY_TARGET,
    EVENT_FAMILY_TEST,
    EVENT_FAMILY_INSTALL,
    EVENT_FAMILY_CPACK,
    EVENT_FAMILY_PACKAGE,
    EVENT_FAMILY_META,
} Event_Family;
```

```c
typedef struct {
    String_View file_path;
    size_t line;
    size_t col;
} Event_Origin;
```

```c
typedef struct {
    Event_Family family;
    uint16_t kind;
    uint16_t version;
    uint32_t flags;
    uint64_t seq;
    uint32_t scope_depth;
    uint32_t policy_depth;
    Event_Origin origin;
} Event_Header;
```

## 2. Evento principal

```c
typedef struct {
    Event_Header h;
    union {
        Event_Trace_Command_Begin trace_command_begin;
        Event_Diag diag;
        Event_Var_Set var_set;
        Event_Scope_Push scope_push;
        Event_Policy_Set policy_set;
        Event_Flow_Return flow_return;
        Event_Fs_Copy fs_copy;
        Event_Target_Declare target_declare;
        ...
    } as;
} Event;
```

## 3. Stream

```c
typedef struct {
    Event *items;
} Event_Stream;
```

Com:
- `NULL` = stream vazio
- `arena_arr_len(stream->items)` como tamanho

## Estratégia de Payloads

## 1. O payload novo deve ser semântico

O payload deve representar:
- intenção
- efeito lógico
- estado relevante

Não deve ser uma simples cópia do payload estrutural antigo.

Exemplo:
- `Event_Var_Set { key, value, scope_kind, scope_depth }`
- `Event_Flow_Return { has_propagate, propagate_vars, return_context }`
- `Event_Fs_Copy { sources, destination, mode_flags }`

## 2. Preservar rastreabilidade sem poluir tudo

Quando necessário, use eventos de `TRACE` para carregar:
- `command_name`
- `resolved_args`
- maybe subcommand/mode

Mas não duplicar isso em todos os eventos de efeito.

## 3. Estratégia híbrida recomendada

Para cada comando relevante:
- emitir um evento de `TRACE` de entrada/saída
- emitir eventos semânticos normalizados de efeito

Exemplo:
- `TRACE_COMMAND_BEGIN`
- `VAR_SET`
- `TRACE_COMMAND_END`

Ou:
- `TRACE_COMMAND_BEGIN`
- `FS_COPY`
- `TRACE_COMMAND_END`

Isso preserva:
- legibilidade
- auditabilidade
- possibilidade de derivação

## Coverage Matrix Inicial: Command -> Event IR

## 1. Documento novo

Criar:

- [`docs/evaluator/event_ir_coverage_matrix.md`](/home/pedro/Nobify/docs/evaluator/event_ir_coverage_matrix.md)

Esse documento vira a fonte de controle da migração.

## 2. Colunas mínimas

- comando
- módulo atual
- status do handler
- emissão atual
- família alvo no novo IR
- eventos alvo
- prioridade
- notas de semântica
- impacto downstream

## 3. Seed inicial

### Alta prioridade semântica
- `set`
- `unset`
- `if`
- `foreach`
- `while`
- `return`
- `block`
- `function`
- `macro`
- `include`
- `add_subdirectory`
- `cmake_policy`
- `cmake_minimum_required`

### Alta prioridade estrutural
- `project`
- `add_executable`
- `add_library`
- `target_*`
- `add_custom_command`
- `add_test`
- `install`
- `find_package`

### Alta prioridade operacional
- `file(...)`
- `execute_process`
- `cmake_language`

### Média
- `string`
- `list`
- `math`
- `cmake_path`

## Ordem de Migração por Família

Como você aceita quebrar o builder atual, a ordem pode priorizar o novo contrato, não compatibilidade.

## Fase 1: Infra do novo Event IR
- reescrever [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h)
- reescrever [`src_v2/transpiler/event_ir.c`](/home/pedro/Nobify/src_v2/transpiler/event_ir.c)
- implementar stream, dump, kind-name e header comum

## Fase 2: Trace + Diag
- migrar o evaluator para emitir:
  - `TRACE_COMMAND_BEGIN`
  - `TRACE_COMMAND_END`
  - `DIAG`
- isso já dá visibilidade da execução

## Fase 3: Scope + Policy + Var
- migrar:
  - scope push/pop
  - policy push/pop/set
  - set/unset/cache/env
- isso cria a base de estado

## Fase 4: Flow
- migrar:
  - if/elseif/else
  - foreach/while
  - break/continue/return
  - defer

## Fase 5: Meta context
- migrar:
  - include
  - add_subdirectory
  - cmake_language
  - include guards
  - subdir context changes

## Fase 6: FS + Proc
- migrar:
  - `file(...)`
  - `execute_process`
  - host/process operations

## Fase 7: String + List + Math + Path
- migrar os comandos semânticos utilitários

## Fase 8: Structural build effects
- migrar:
  - project
  - target
  - test
  - install
  - cpack
  - package
- aqui nasce o substituto futuro do builder estrutural

## Rebaixamento do Legado para Obsoleto

## 1. O que mover para `src_obsolete`

Mover ou marcar como obsoleto:

- IR estrutural atual:
  - [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h) legado
  - [`src_v2/transpiler/event_ir.c`](/home/pedro/Nobify/src_v2/transpiler/event_ir.c) legado

- build model atual:
  - [`src_v2/build_model/build_model.c`](/home/pedro/Nobify/src_v2/build_model/build_model.c)
  - [`src_v2/build_model/build_model_builder.c`](/home/pedro/Nobify/src_v2/build_model/build_model_builder.c)
  - [`src_v2/build_model/build_model_validate.c`](/home/pedro/Nobify/src_v2/build_model/build_model_validate.c)
  - [`src_v2/build_model/build_model_freeze.c`](/home/pedro/Nobify/src_v2/build_model/build_model_freeze.c)
  - [`src_v2/build_model/build_model_query.c`](/home/pedro/Nobify/src_v2/build_model/build_model_query.c)

## 2. Como lidar com o build

Duas opções válidas:

1. mover fisicamente para `src_obsolete/` e tirar do build
2. manter em `src_v2`, mas:
- renomear como `_legacy`
- remover do build ativo
- documentar como obsoleto

Como você quer sinal arquitetural forte, a recomendação melhor é:
- mover para `src_obsolete/`

## 3. Consequência prática

Até o novo consumidor existir:
- o build principal pode compilar sem o builder atual
- ou o alvo principal temporário pode ser apenas “parser + evaluator + event ir dump”

Isso é aceitável dentro do escopo que você definiu.

## Impacto Esperado no Projeto

## 1. `eval_dispatcher` e `evaluator`

Eles passam a mirar o novo `Event IR` como contrato primário.
O dispatcher continua, mas os handlers deixam de pensar em “o que o builder precisa” e passam a pensar em:
- que semântica aconteceu
- que eventos precisam ser emitidos

## 2. `build_model`

O `build_model` atual deixa de ser parte da linha principal.
No futuro, ele será:
- refeito
- ou substituído por um derivador do novo IR

## 3. `docs`

A documentação precisa ser atualizada para refletir:
- o IR atual é obsoleto
- o novo `Event IR` é a referência
- a matriz de cobertura passa a ser o controle da migração

## Critérios de Aceite da Primeira Etapa

A primeira etapa estará correta quando:

- o `event_ir` atual tiver sido rebaixado para obsoleto
- o novo `event_ir` canônico existir com:
  - taxonomia por família
  - header comum
  - stream novo
- o `build_model` atual estiver fora da linha principal
- houver matriz de cobertura inicial
- o evaluator puder emitir ao menos:
  - `TRACE`
  - `DIAG`
  - `VAR`
  - `SCOPE`
  - `POLICY`
  - `FLOW_RETURN`

## Próximo Passo Imediato

O próximo passo correto é:

1. criar a área obsoleta:
- [`src_obsolete/`](/home/pedro/Nobify/src_obsolete)

2. mover ou congelar o legado:
- `event_ir` atual
- `build_model` atual

3. reescrever o `event_ir` canônico em:
- [`src_v2/transpiler/event_ir.h`](/home/pedro/Nobify/src_v2/transpiler/event_ir.h)
- [`src_v2/transpiler/event_ir.c`](/home/pedro/Nobify/src_v2/transpiler/event_ir.c)

4. criar:
- [`docs/evaluator/event_ir_coverage_matrix.md`](/home/pedro/Nobify/docs/evaluator/event_ir_coverage_matrix.md)

Essa é a forma correta de aumentar o escopo de verdade sem transformar o IR novo em “mais uma versão acumulada” do legado.
</proposed_plan>