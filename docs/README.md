# Nobify

**CMake Frontend. Nob Backend. Zero Runtime Dependência.**

Nobify converte projetos escritos em CMake para um build equivalente em Nob, permitindo compilar bibliotecas C/C++ **sem exigir CMake no ambiente final de build**.

O projeto parte de uma premissa simples:

> A linguagem da biblioteca deveria ser suficiente para compilá-la.
> O build não deveria exigir a instalação de outra linguagem para existir.

---

## Motivação

CMake se tornou a DSL dominante para descrição de builds no ecossistema C/C++.
Isso trouxe padronização — mas também trouxe dependência estrutural:

* Necessidade de instalar CMake
* Dependência de versão e policies
* Runtime de interpretação
* Pipeline configure → generate → build
* Geradores múltiplos

Para muitas bibliotecas, essa infraestrutura é desproporcional ao objetivo final: gerar artefatos binários.

Nobify preserva a compatibilidade com o ecossistema CMake, mas remove a obrigatoriedade do runtime CMake no ambiente de build.

---

## Proposta

CMakeLists.txt →  Nob.c

CMake é tratado como DSL de entrada.
Nob é o backend determinístico de execução.

O resultado:

* Mesmos targets
* Mesmas flags
* Mesmas dependências
* Mesmos artefatos finais

Sem exigir a execução do CMake.

---

## Escopo e Contrato de Compatibilidade

Nobify não é um clone do **CMake**.

Ele não promete:

* Paridade histórica completa
* Compatibilidade bit-a-bit
* Implementação de todos os geradores (Ninja, VS, Xcode)
* Replicação integral de policies e quirks legados

Ele promete:

> Compatibilidade funcional orientada a bibliotecas C/C++ reais.

A métrica de sucesso é objetiva:

* A biblioteca compila.
* Os artefatos equivalentes são gerados.
* As dependências são preservadas.
* O ambiente final não precisa de CMake.

---

## Cobertura Atual

* 130 comandos CMake analisados
* Cobertura completa no escopo do projeto
* Classificação interna:

  * **Suportado** – conversão completa para o objetivo
  * **Adequado** – conversão suficiente para builds reais
  * **Parcial / Não implementado** – inexistentes no escopo atual

---

## Arquitetura

Nobify é estruturado como um compilador:

* Lexer
* Parser
* Evaluator semântico
* Dispatcher de comandos
* Codegen para Nob

Essa arquitetura permite compatibilidade ampla sem acoplamento ao runtime do CMake.

---

## Filosofia Nob

Nob adota um princípio de engenharia:

> Infraestrutura deve ser mínima, explícita e determinística.

Nobify aplica esse princípio ao ecossistema CMake:

* Reaproveita a linguagem dominante
* Remove dependências desnecessárias
* Reduz complexidade do ambiente
* Mantém controle total do backend

---

## V2 Documentation Index

Read order for Build Model v2 / Transpiler v2 migration:

1. `docs/build_model_v2_contract.md` (normative source of truth)
2. `docs/build_model_v2_readiness_checklist.md` (objective go/no-go gate)
3. `docs/build_model_v2_roadmap.md` (milestones and execution order)
4. `docs/build_model_v2_transition_recommendations.md` (operational guidance)
5. `docs/transpiler_v2_spec.md` (transpiler constraints and dependencies)
6. `docs/transpiler_v2_activation_plan.md` (post-readiness activation)

Hard policy summary:

1. no functional implementation in transpiler v2 planner/codegen before Build Model v2 readiness is PASS
2. Build Model v2 readiness is the single unlock gate for transpiler v2 functional work
