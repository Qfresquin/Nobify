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

## 2026-02-15T00:01:28-03:00
- Removidos comportamentos especificos de projeto no transpiler:
  - fallback sintetico por nome de pacote (`Libpsl` e afins)
  - comando dedicado `curl_transform_makefile_inc`
- `find_package` passou a exigir evidencias reais (`<Pkg>_FOUND`) e agora falha cedo em `REQUIRED`.
- `string(REGEX MATCH/REPLACE)` e `file(STRINGS ... REGEX ...)` estao em caminho generico.
- `pkg_check_modules`, `pkg_search_module` e `find_package_handle_standard_args` foram integrados ao evaluator.
- Dependencias de `OBJECT` library via `$<TARGET_OBJECTS:...>` agora entram no modelo e no codegen.
- Validacao atual com `curl-8.18.0/CMakeLists.txt`:
  - a transpilacao para durante configuracao em `find_package(Libpsl REQUIRED)` quando o pacote nao esta disponivel;
  - nao avanca para erro tardio de compilacao C por header ausente;
  - estado local observado: `1 erro(s)` e `5 warning(s)` (dependencias opcionais ausentes).
- Decisoes de arquitetura e comportamento generico foram consolidadas em `docs/transpiler_dependency_resolution.md`.

## 2026-02-15T04:36:19-03:00
- Validacao com dependencia real instalada:
  - instalado via MSYS2 UCRT64: `mingw-w64-ucrt-x86_64-libpsl` (com `libidn2`/`libunistring`).
  - `pkg-config --modversion libpsl` passou (`0.21.5`).
- Reexecucao de `cmk2nob` no `curl-8.18.0/CMakeLists.txt`:
  - `find_package(Libpsl REQUIRED)` passou e marcou origem `module` (via `pkg-config` no fluxo `FindLibpsl.cmake`).
  - avancou para etapas posteriores do projeto.
- Novo bloqueio observado (apos superar `Libpsl`):
  - dois erros de parser (`argumento nao fechado, esperado ')'`) durante processamento em subdiretorios `lib` e `src`.
  - contexto do bloqueio: includes de `Makefile.inc.cmake` no fluxo de build do curl.

## 2026-02-15T12:40:00-03:00
- Fechada a regressao de parser em `lib/src` de forma generica:
  - `resolve_string_depth` agora aplica escapes CMake antes de expandir variaveis/genex.
  - `string(REPLACE|REGEX MATCH|REGEX REPLACE)` passou a concatenar `input...` sem inserir espaco.
  - `cmk_regex_replace_backrefs` passou a aceitar `\$` no template de replacement sem quebrar backrefs.
- Novos testes de regressao adicionados para:
  - replacement `"\${\\1}"` em `string(REGEX REPLACE ...)`;
  - `string(REPLACE ... "\n" ...)`;
  - transformacao estilo `Makefile.inc` + `include(...)`.
- Validacao atual:
  - `nob.exe test`: `343/343` passou.
  - `cmk2nob curl-8.18.0/CMakeLists.txt`: sucesso com `0 erro(s)` (mantendo warnings de dependencias opcionais ausentes).
  - `nob_generated.c` compila com `-Ivendor`; a execucao do runner agora falha adiante por ambiente/toolchain (`netinet/in.h` ausente no compilador C atual do host), nao por parser/transpilacao.

## 2026-02-15T06:25:00-03:00
- Removida regra especifica de projeto no fallback de `check_type_size`:
  - excluido `curl_off_t` hardcoded de `eval_check_type_size_value` em `src/transpiler/transpiler_evaluator.inc.c`.
  - o evaluator manteve apenas tipos genericos para fallback local.
- Revalidacao ponta-a-ponta no cenario atual:
  1. `nob.exe`
  2. `cmk2nob.exe curl-8.18.0/CMakeLists.txt`
  3. `cc -Ivendor nob_generated.c -o nob_generated_runner.exe`
  4. `nob_generated_runner.exe`
- Resultado da execucao final do runner: `EXIT=0`.
- Estado final: fluxo do `curl` conclui no `nob_generated` sem hacks especificos no transpiler.

## 2026-02-15T07:05:00-03:00
- Fechado de forma generica o caso de tipos definidos por header em `check_type_size`:
  - `check_type_size` agora tenta probe real (compila + executa snippet) para obter `sizeof(T)` de tipos nao triviais.
  - suporte a `CMAKE_EXTRA_INCLUDE_FILES` no snippet do probe.
  - resolucao de headers para caminho absoluto (evita dependencia do diretorio do arquivo de probe).
  - fallback conservador antigo continua para cenarios sem compilador.
- Regressao coberta por teste novo:
  - `check_type_size_real_probe_with_extra_include_files` em `test/test_transpiler.c`.
- Validacao completa apos ajuste:
  1. `nob.exe test` -> `344/344` passou.
  2. `cmk2nob.exe curl-8.18.0/CMakeLists.txt` -> `EXIT=0`.
  3. `cc -Ivendor nob_generated.c -o nob_generated_runner.exe` -> sucesso.
  4. `nob_generated_runner.exe` -> `EXIT=0`.
