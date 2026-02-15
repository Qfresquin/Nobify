# Transpiler v2 Spec (Contrato de Compatibilidade e Execucao)

## 1. Objetivo
- Definir o contrato tecnico da reescrita ampla do transpiler/build model com cutover unico.
- Garantir paridade funcional mensuravel com o engine legado antes de promover v2.

## 2. Escopo
- Dentro do escopo:
  - Pipeline v2: frontend -> semantic IR -> planner -> executor/effects -> codegen.
  - Compatibilidade com comportamento historico do projeto em corpus oficial e corpus real.
  - Gate de diff automatizado entre legado e v2 durante toda a migracao.
- Fora do escopo (neste momento):
  - Remocao imediata do engine legado.
  - Breaking changes em API publica sem wrapper de compatibilidade.

## 3. Entrypoints e tipos canonicos
- Entrypoint legado (mantido durante migracao):
  - `transpile_datree_ex(Ast_Root, String_Builder*, const Transpiler_Run_Options*)`
- Entrypoint v2:
  - `transpile_datree_v2(Ast_Root, String_Builder*, const Transpiler_Run_Options*, const Transpiler_Compat_Profile*)`
- Tipos v2 iniciais:
  - `Transpiler_Compat_Profile`
  - `Cmake_IR_Program`
  - `Cmake_IR_Command`
  - `Cmake_IR_Effect`

## 4. Politica de compatibilidade
- Default:
  - `Transpiler_Compat_Profile.kind = TRANSPILER_COMPAT_PROFILE_CMAKE_3_X`
  - `allow_behavior_drift = false`
- Mudancas comportamentais:
  - Permitidas somente quando:
    1. documentadas em `docs/plano_migracao_incremental.md` (secao de mudancas aceitas),
    2. cobertas por teste de regressao,
    3. aprovadas no gate da fase.

## 5. Catalogo inicial por criticidade (Tier)
- Tier-1 (core de valor):
  - `project`, `set`, `if/foreach/function/macro`
  - `add_executable`, `add_library`, `target_*` essenciais
  - `include`, `find_*`, `find_package` base
  - `install`, `add_test`, `enable_testing` essenciais
- Tier-2:
  - `try_compile`, `try_run`, checks/probes usuais
  - `custom_command`, generator expressions comuns
  - `ctest`/`cmake_file_api`/instrumentation de uso frequente
- Tier-3:
  - domínios avançados e edge cases (cpack amplo, paths exoticos, fluxos legados raros)

## 6. Baseline congelado para comparacao
- Artefato principal: `nob_generated.c`
- Metricas obrigatorias:
  - paridade de saida (diff textual e/ou estrutural)
  - taxa de diagnosticos divergentes
  - tempo de transpilacao
  - regressao funcional na suite completa

## 7. Gate de diff automatizado (fase inicial)
- Teste diferencial ativo:
  - `test/test_transpiler_v2_diff.c`
- Regras:
  - v2 deve produzir mesma saida do legado nos casos canarios definidos.
  - qualquer divergencia reprova o gate.

## 8. Checklist de release gate (pre-cutover)
- [ ] Suite completa verde.
- [ ] Gate diferencial legado-v2 verde no corpus oficial.
- [ ] Gate diferencial legado-v2 verde no corpus real.
- [ ] Regressao arquitetural verde.
- [ ] Performance <= +15% (ou justificativa aprovada e documentada).
- [ ] Sem blockers abertos Tier-1 e Tier-2.
- [ ] Lista de divergencias aceitas consolidada e assinada.

## 9. Plano de rollback pos-cutover
- Janela de rollback: 1 ciclo de release.
- Estrategia:
  - manter engine legado compilavel durante a janela.
  - fallback operacional para legado em caso de regressao critica.
  - congelar novas mudancas sem teste diferencial ate estabilizacao.

