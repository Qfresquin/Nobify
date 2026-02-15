# Resolucao de dependencias no transpiler CMake

## Objetivo
Este documento descreve a semantica generica de resolucao de dependencias no transpiler, sem acoplamento a projetos especificos.

## `find_package`
- A resolucao segue os modos `MODULE`, `CONFIG` e `NO_MODULE/CONFIG`.
- A ordem respeita `CMAKE_FIND_PACKAGE_PREFER_CONFIG` quando nem `MODULE` nem `CONFIG` sao forcados.
- Em modo `MODULE`, o transpiler busca `Find<Package>.cmake` em:
  - `CMAKE_MODULE_PATH`
  - `HINTS`
  - `PATHS`
  - `CMAKE_ROOT/Modules`
- Em modo `CONFIG`, o transpiler busca `<Pkg>Config.cmake` e `<pkg>-config.cmake` em:
  - `<Pkg>_DIR`
  - `HINTS`
  - `PATHS`
  - `CMAKE_PREFIX_PATH`
  - sufixos comuns (`cmake`, `lib/cmake`, `share/cmake`, `PATH_SUFFIXES`)

## Criterio de pacote encontrado
- O pacote so e considerado encontrado quando variaveis reais de resultado foram definidas apos a carga do modulo/config:
  - `<Pkg>_FOUND`
  - variante upper-case (`<PKG>_FOUND`)
- Nao existe mais fallback sintetico de sucesso por inferencia de nome.
- Nao existe tabela de aliases hardcoded por projeto (pacotes/tipos especificos de um unico projeto).

## Semantica de erro
- `find_package(... REQUIRED)`:
  - gera erro de diagnostico
  - interrompe a avaliacao subsequente (comportamento equivalente a falha de configuracao)
- `QUIET`:
  - suprime logs informativos/avisos de ausencia
- `OPTIONAL`:
  - mantem o fluxo sem erro fatal

## Propagacao para imported targets
Quando o pacote e encontrado, os metadados sao propagados para target importado (`<Pkg>::<Pkg>`) e componentes (`<Pkg>::<Component>`), usando variaveis reais:
- `<Pkg>_LIBRARIES`
- `<Pkg>_INCLUDE_DIRS` / `<Pkg>_INCLUDE_DIR`
- `<Pkg>_CFLAGS`
- `<Pkg>_LDFLAGS`
- `<Pkg>_LINK_DIRECTORIES` / `<Pkg>_LIBRARY_DIRS`

## `pkg-config` generico
Comandos suportados:
- `pkg_check_modules(...)`
- `pkg_search_module(...)`
- `cmake_pkg_config(...)`

Comportamento:
- Usa `PKG_CONFIG_EXECUTABLE` quando definido.
- Caso contrario, tenta `pkg-config` e `pkgconf`.
- Ausencia de ferramenta nao implica sucesso.
- Preenche variaveis padrao (`_FOUND`, `_VERSION`, `_LIBRARIES`, `_INCLUDE_DIRS`, `_CFLAGS`, `_LDFLAGS`, `_LIBRARY_DIRS`, `_MODULE_NAME`).

## `find_package_handle_standard_args`
- Suporte para `REQUIRED_VARS`, `VERSION_VAR` e `HANDLE_COMPONENTS`.
- Atualiza `<Pkg>_FOUND` (e variante upper-case) com base em evidencias reais.

## Regex e leitura de arquivo
- `string(REGEX MATCH ...)` e `string(REGEX REPLACE ...)` sao genericos.
- `file(STRINGS ... REGEX ...)` suportado com filtros de tamanho e limite de contagem.

## Semantica de `string(...)` e escapes
- Em argumentos quoted, o evaluator aplica escapes CMake antes da expansao:
  - `\$` -> `$` literal (sem disparar `${...}`)
  - `\\` -> `\`
  - `\n`, `\r`, `\t`, `\;`, `\"`
  - continuacao de linha com `\` + quebra de linha e consumida
- Em `string(REPLACE ...)`, `string(REGEX MATCH ...)` e `string(REGEX REPLACE ...)`, multiplos argumentos `input...` sao concatenados sem espaco automatico.
- Em `string(REGEX REPLACE ...)`, templates de replacement aceitam:
  - backrefs (`\1..\9`)
  - `\$` para `$` literal.
- Esse comportamento evita geracao de CMake invalido em fluxos que transformam arquivos texto e depois fazem `include(...)` do resultado.

## `check_type_size`
- O transpiler tenta probe real (compila + executa) para determinar `sizeof(T)` quando compilador esta disponivel.
- O snippet do probe respeita `CMAKE_EXTRA_INCLUDE_FILES`.
- Caminhos de headers extras sao resolvidos para absolutos quando possivel para evitar dependencia de diretorio relativo do arquivo de probe.
- Sem compilador disponivel, permanece fallback conservador para tipos basicos conhecidos.

## `OBJECT` libraries e `$<TARGET_OBJECTS:...>`
- Dependencias de objetos sao registradas no modelo.
- Ordenacao topologica considera dependencias de objetos.
- Codegen injeta objetos de targets `OBJECT` nos consumidores corretamente.

## Diagnostico de origem
Ao resolver pacote, o transpiler registra origem da resolucao:
- `module`
- `config`
- `cache`
- `none` (nao encontrado)
