# Mudancas de ultima hora - compatibilidade com libcurl

## 2026-02-14T22:25:03-03:00
- Corrigido `NOB_GO_REBUILD_URSELF_PLUS` em `src/app/main.c` para monitorar apenas arquivos-fonte (`CMK2NOB_REBUILD_SOURCES`).
- Removido o uso incorreto de `CMK2NOB_REBUILD_INCLUDE_FLAGS` nessa chamada, que fazia o runtime tentar abrir `-Ivendor` como arquivo e abortar antes de ler o `CMakeLists.txt`.

## 2026-02-14T22:26:54-03:00
- Adicionado fallback de `find_package(Libpsl)` em `src/transpiler/transpiler_dispatcher.inc.c`, mapeando `Libpsl -> psl`.
- Incluida versao padrao inferida para `Libpsl` (`0.21.0`) para manter variaveis `<Pkg>_VERSION` consistentes no evaluator.
- Objetivo: destravar o fluxo padrao do `CMakeLists.txt` do curl, onde `CURL_USE_LIBPSL` e `ON` por padrao e `find_package(Libpsl REQUIRED)` e executado.

## 2026-02-14T22:29:39-03:00
- Validacao concluida com `./cmk2nob.exe ./curl-8.18.0/CMakeLists.txt`.
- Resultado: transpilacao completa com sucesso (`Sucesso!`) e geracao de `nob_generated.c`.
- Estado atual de diagnosticos no cenario local: `0 erro(s)` e `6 warning(s)` (pacotes opcionais nao encontrados: `Perl`, `Brotli`, `Zstd`, `NGHTTP2`, `Libidn2`, `Libssh2`).

## 2026-02-14T22:37:10-03:00
- Corrigido `set_property(DIRECTORY ...)` em `src/transpiler/transpiler_evaluator.inc.c` para aplicar propriedades de diretorio suportadas (como `INCLUDE_DIRECTORIES`) no modelo.
- Adicionado comando dedicado `curl_transform_makefile_inc` em `src/transpiler/transpiler_dispatcher.inc.c` para transformar `Makefile.inc` em sintaxe CMake (`set(...)`) de forma compativel com o fluxo do curl.
- A transformacao dedicada tambem converte referencias `$(VAR)` e `@VAR@` para `${VAR}` e preserva continuacoes de linha com `\`.

## 2026-02-14T22:39:20-03:00
- Corrigido expansao de listas de fontes separadas por `;` nos comandos `add_library`, `add_executable` e `target_sources` em `src/transpiler/transpiler_evaluator.inc.c` (antes viravam um unico argumento gigante para o compilador).
- Normalizado `current_source_dir/current_binary_dir/current_list_dir` para separador de caminho estavel em `src/transpiler/transpiler.c`, evitando paths com `\` sem escape no `nob_generated.c`.

## 2026-02-14T22:39:48-03:00
- Atualizada a adicao de fontes no evaluator para resolver caminhos relativos em relacao ao `CMAKE_CURRENT_SOURCE_DIR` e normalizar os caminhos antes de inserir no `Build_Model`.
- Efeito esperado: o `nob_generated` passa a compilar fontes de subdiretorios (ex.: `lib/*.c`, `src/*.c`) sem depender do diretório atual de execucao.

## 2026-02-14T22:41:36-03:00
- Estendido o processamento de `configure_file` em `src/transpiler/transpiler_evaluator.inc.c` para suportar diretivas de template `#cmakedefine` e `#cmakedefine01`.
- Comportamento novo:
  - `#cmakedefine01 VAR` -> `#define VAR 1` ou `#define VAR 0`
  - `#cmakedefine VAR ...` -> `#define VAR ...` quando verdadeiro, senao `/* #undef VAR */`
- Mantida expansao de variaveis `@VAR@` e `${VAR}` durante a renderizacao.

## 2026-02-14T22:43:34-03:00
- Ajustado o expand de variaveis de `configure_file` para desserializar escapes comuns (`\\\"`, `\\\\`, `\\n`, `\\r`, `\\t`) ao injetar valores.
- `check_type_size` agora tambem exporta `<OUT_VAR>_CODE` (ex.: `SIZEOF_LONG_CODE`) com o trecho de pre-processador esperado por templates como `curl_config-cmake.h.in`.

## 2026-02-14T22:44:57-03:00
- Nova verificacao ponta-a-ponta executada:
  1. `cmk2nob` gerou `nob_generated.c`
  2. `nob_generated.c` compilou
  3. execucao do runner iniciou a compilacao da libcurl
- Estado atual do build da libcurl: **falha em dependencia externa ausente** (`libpsl.h` nao encontrado no ambiente local), no arquivo `lib/psl.h` durante a compilacao de `lib/altsvc.c`.
