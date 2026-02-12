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

Não é sobre substituir o CMake.
É sobre desacoplar o backend do ecossistema que ele domina.

---

## Quando Usar

Use Nobify se:

* Você quer compilar bibliotecas C/C++ existentes
* Deseja reduzir dependências no ambiente de build
* Quer controle total do backend
* Prefere builds determinísticos e diretos

Use CMake diretamente se:

* Você precisa de suporte completo a todos os geradores
* Depende de policies históricas específicas
* Precisa de integração profunda com CDash/CPack completos
* Requer compatibilidade universal irrestrita

---

## Conclusão

CMake é amplamente adotado.
Isso não significa que ele precise ser obrigatório em todo ambiente de compilação.

Nobify mantém a compatibilidade com a linguagem.

Nob mantém o controle do backend.

Menos camadas.
Menos dependências.
Mesma biblioteca.

---
