# Plano de Migracao Incremental Sem Parar Feature Work

## 1. Contexto e objetivos
- Objetivo principal: reduzir acoplamento e codigo redundante sem interromper entrega de features.
- Cadencia oficial: blocos quinzenais.
- Regra de compatibilidade: pequenas mudancas comportamentais controladas sao permitidas, desde que documentadas e cobertas por testes.
- Documento de governanca: este arquivo e a referencia unica para status, gates e riscos da migracao.

### Status de implementacao (rodada atual)
- [x] Q1 (concluido): `Transpiler_Run_Options` + entrada `transpile_datree_ex` + eliminacao de dependencia global no fluxo principal + gate arquitetural em teste para bloquear regressao.
- [x] Q2 (concluido): rebuild de `INTERFACE_*` consolidado em fluxo generico unico + parse de listas `;` unificado no caminho de link libs + aplicacao centralizada por `eval_apply_target_property`.
- [x] Q3 (foundation): introducao de `Command_Spec` com validacao de aridade no dispatcher.
- [x] Q4 (parcial): camada `transpiler_effects` criada e integrada em `execute_process`, `exec_program`, fluxo `pkg-config` e probes/toolchain.
- [x] Q5 (parcial): `build_model_types.h` extraido de `build_model.h` e headers utilitarios do transpiler migrados para dependencia por tipos.
- [ ] Q6: split completo da suite `test/test_transpiler.c` em suites tematicas.

### Objetivos tecnicos
- Eliminar dependencias de estado global mutavel no fluxo principal do transpiler.
- Generalizar trechos redundantes no evaluator (especialmente `INTERFACE_*`).
- Tornar o dispatcher orientado a contrato declarativo (`Command_Spec`).
- Diminuir superficie publica e vazamento de detalhes internos no `Build_Model`.
- Reduzir duplicacao na suite de testes e melhorar isolamento por dominio.

## 2. Baseline tecnico (metricas atuais)
- `src/transpiler/transpiler_evaluator.inc.c`: 10.928 linhas.
- `src/transpiler/transpiler_dispatcher.inc.c`: 1.353 linhas.
- `src/transpiler/transpiler_codegen.inc.c`: 1.876 linhas.
- `src/build_model/build_model.h`: 743 linhas.
- `test/test_transpiler.c`: 6.202 linhas.
- Estado global identificado: `src/transpiler/transpiler.c` e `src/diagnostics/diagnostics.c`.
- Mutacao direta de internals no evaluator ainda presente.

### Baseline de qualidade
- Suite funcional existente deve permanecer verde a cada bloco.
- Performance de transpilacao: degradacao maxima aceitavel de 15% no corpus base sem justificativa.

## 3. Estrategia de execucao em 2 trilhas
- Trilha A (Feature): desenvolvimento normal de funcionalidades.
- Trilha B (Migracao): reducao de redundancia e acoplamento por blocos quinzenais.

### Regras operacionais
- Toda feature nova deve usar as novas abstracoes ao tocar area ja migrada.
- Mudancas de maior risco devem entrar com switch de runtime/fallback temporario.
- PRs de migracao devem ser pequenos e verticalizados (sem escopo aberto).
- Todo PR de migracao precisa incluir: testes, risco, rollback e impacto de compatibilidade.

## 4. Plano por bloco quinzenal

### Q1 - Fundamentos de seguranca de migracao
**Objetivo**
- Criar costura arquitetural sem quebrar fluxo atual.

**Implementacao**
1. Introduzir `Transpiler_Run_Options`.
2. Remover dependencia do estado global `g_continue_on_fatal_error` no caminho principal via injecao de contexto.
3. Introduzir facades no `Build_Model` para evitar novas mutacoes diretas de internals.
4. Consolidar checklist de migracao neste documento.

**Entregaveis**
1. Novas APIs compilando com compatibilidade retroativa.
2. Comportamento default sem regressao funcional.

**Gate**
1. Suite existente verde.
2. Nenhum novo acesso direto a campo interno em evaluator.

### Q2 - Reducao de redundancia no evaluator
**Objetivo**
- Eliminar padroes repetidos na manipulacao de propriedades `INTERFACE_*`.

**Implementacao**
1. Extrair rotina generica de sync/rebuild de propriedades derivadas de target.
2. Unificar parse de listas separadas por `;` e regras append/replace em funcoes unicas.
3. Remover blocos duplicados de reset/rebuild manual.

**Entregaveis**
1. API unica para atualizar propriedades derivadas.
2. Reducao mensuravel de linhas no evaluator.

**Gate**
1. Cobertura para `set_property` e `target_*` preservada.
2. Sem reintroducao de duplicacoes antigas.

### Q3 - Dispatcher declarativo e modular
**Objetivo**
- Reduzir boilerplate e padronizar validacao de comando.

**Implementacao**
1. Definir `Command_Spec` com handler, validacao de aridade, flags e politica de diagnostico.
2. Migrar dispatch para specs declarativas.
3. Separar handlers por dominio (`commands_target`, `commands_find`, `commands_file`, `commands_control`).

**Entregaveis**
1. Dispatcher mais simples e orientado por metadata.
2. Handlers separados por dominio funcional.

**Gate**
1. Roteamento de comandos sem perda funcional.
2. Diagnosticos equivalentes ou melhores.

### Q4 - Isolamento de efeitos (I/O, subprocess, probe)
**Objetivo**
- Separar semantica de comando da execucao de efeitos.

**Implementacao**
1. Introduzir camada de planejamento (`Effect_Request`) e execucao (`Effect_Result`).
2. Reusar `sys_utils` e `toolchain_driver` por interface estavel.
3. Padronizar erro, timeout e telemetria de efeitos.

**Entregaveis**
1. Comandos criticos passando por interface unica.
2. Menos logica procedural repetida no evaluator.

**Gate**
1. Testes de timeout/capture/erro consistentes.
2. Sem regressao de comportamento principal.

### Q5 - Enxugamento de API publica do Build Model
**Objetivo**
- Reduzir superficie publica e leaky abstractions.

**Implementacao**
1. Segregar headers de dominio e utilitarios genericos.
2. Padronizar ownership de string dentro do modelo.
3. Deprecar APIs verbosas redundantes com wrappers temporarios.

**Entregaveis**
1. `build_model.h` menor e mais estavel.
2. Contratos de API claros por dominio.

**Gate**
1. Consumidores internos migrados.
2. Wrappers de compatibilidade documentados.

### Q6 - Reorganizacao de testes e estabilizacao final
**Objetivo**
- Reduzir redundancia de teste e consolidar criterios de release.

**Implementacao**
1. Quebrar `test/test_transpiler.c` em suites tematicas.
2. Adotar harness/fixtures compartilhados para setup comum.
3. Adicionar regressao diferencial com corpus real (ex.: curl).

**Entregaveis**
1. Suites menores e mais legiveis.
2. Pipeline com gates funcionais e arquiteturais.

**Gate**
1. Paridade funcional preservada.
2. Regressao detectada cedo por suites segmentadas.

## 5. Mudancas de API/interfaces/tipos
1. Novo tipo `Transpiler_Run_Options`.
2. Nova entrada recomendada: `transpile_datree_ex(..., const Transpiler_Run_Options*)`.
3. `transpiler_set_continue_on_fatal_error` marcado como legado e removido ao final da migracao.
4. Novo contrato `Command_Spec` para dispatcher declarativo.
5. Novas APIs de encapsulamento no `Build_Model` para atualizar propriedades derivadas sem acesso direto a campos.
6. Interface de efeitos (`Effect_Request`/`Effect_Result`) para subprocess, filesystem e probes.

## 6. Plano de testes e criterios de aceite

### Regressao funcional
- Rodar suite completa em cada bloco.
- Cobrir cenarios: `find_*`, `find_package`, `check_*`, `try_compile`, `try_run`, `file`, `include`, `configure_file`.

### Regressao arquitetural
- Busca automatizada proibindo novos acessos diretos a internals de target/model no evaluator.
- Busca automatizada proibindo novos estados globais mutaveis.

### Diferencial de saida
- Comparar `nob_generated.c` em corpus base para detectar mudancas nao intencionais.

### Falhas e resiliencia
- Timeout de processo.
- Falha de compilador.
- Path invalido.
- Include circular.
- Comando nao suportado.

### Performance
- Tempo de transpilacao nao pode piorar acima de 15% no corpus base sem justificativa documentada.

## 7. Riscos e mitigacao
1. Risco: divergencia de comportamento em edge cases.
- Mitigacao: permitir pequenas mudancas apenas com registro explicito + teste de regressao.

2. Risco: PRs grandes e dificeis de revisar.
- Mitigacao: limitar escopo por bloco e manter PRs pequenos.

3. Risco: conflito com feature work.
- Mitigacao: execucao em duas trilhas + wrappers temporarios de compatibilidade.

## 8. Checklist de rollout

### Checklist por PR de migracao
- [ ] Escopo do PR mapeado para um bloco quinzenal.
- [ ] Sem regressao funcional nos testes relevantes.
- [ ] Mudanca de API documentada (quando aplicavel).
- [ ] Impacto de compatibilidade registrado.
- [ ] Plano de rollback descrito.
- [ ] Sem novo estado global mutavel.
- [ ] Sem novo acesso direto a internals de `Build_Model`/`Build_Target` no evaluator.

### Checklist de fechamento por bloco
- [ ] Entregaveis do bloco concluidos.
- [ ] Gates do bloco atendidos.
- [ ] Metricas atualizadas neste arquivo.
- [ ] Riscos residuais atualizados.

## 9. Registro de mudancas comportamentais aceitas

> Regra: toda mudanca comportamental aceita deve entrar com data, justificativa, impacto e cobertura de teste.

### Template de registro
- Data:
- Bloco:
- Mudanca:
- Justificativa tecnica:
- Impacto funcional:
- Testes adicionados/ajustados:
- Rollback:

### Entradas
- (vazio no momento)

## Metas objetivas de reducao de codigo
1. `transpiler_evaluator.inc.c`: de 10.928 para <= 7.500 ate Q4 e <= 5.500 ate Q6.
2. `test/test_transpiler.c`: de 6.202 para <= 2.500 ate Q6.
3. Zerar padrao duplicado de rebuild de `INTERFACE_*` para 1 implementacao generica.
4. Reduzir `build_model.h` em pelo menos 25% com separacao de responsabilidades.
