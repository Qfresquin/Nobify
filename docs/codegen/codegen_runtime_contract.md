# Codegen Runtime Contract (Normative)

## 1. Role

This document defines the canonical runtime contract of the generated `nob.c`
program.

It covers:

- CLI shape
- phase semantics
- auto-configure behavior
- helper vocabulary
- tool-resolution policy
- rejection policy
- legibility requirements for generated code

It does not redefine evaluator, Event IR, or build-model semantics. Codegen
continues to consume only the frozen/query-facing `Build_Model`.

## 2. Product Boundary

The generated backend is a downstream executor of canonical build-model state.

It is not:

- a general evaluator replay engine
- a second semantic source of truth
- a code generator that reverse-engineers behavior from raw Event IR

The preserved boundary is:

`AST -> evaluator -> Event IR -> build_model -> codegen -> generated Nob runtime`

## 2.1 Data Flow

`build_model query -> helper and phase emission -> generated nob.c execution`

The generated runtime executes model-owned phase behavior; it does not
reconstruct evaluator semantics from raw execution traces.

## 3. CLI Surface

The canonical generated CLI is:

- `configure`
- `build [targets...]`
- `test [tests...] [--config <name>]`
- `install [--prefix <path>] [--component <name>]`
- `export`
- `package [--generator <name>] [--output-dir <path>]`
- `clean`

Invocation without a subcommand means:

- `configure + build`

The backend may keep compatibility with historical direct-target invocation, but
the documented product contract is phase-oriented.

## 4. Phase Semantics

The runtime owns these phases:

- `configure`
  executes replay actions frozen with `BM_REPLAY_PHASE_CONFIGURE`
- `build`
  executes normal target/build-step work after configure ownership is
  satisfied
- `test`
  ensures configure ownership, auto-builds selected registered-test targets
  when required, then executes the filtered test plan and any supported
  test-driver replay
- `install`
  executes install rules after configure ownership is satisfied
- `export`
  executes standalone export generation after configure ownership is satisfied
- `package`
  executes package-plan behavior after configure ownership is satisfied
- `clean`
  removes backend-owned staging and emitted outputs according to the generated
  cleanup contract

The runtime must not infer phase order from helper names. Phase ownership comes
from the frozen build model.

## 5. Auto-Configure Rule

The canonical `C2` through `C5` rule is:

- `build`, `test`, `install`, `export`, and `package` must ensure that required
  configure-phase actions are satisfied before their own phase proceeds

The default product behavior is:

- if configure work is pending, later phases trigger `configure` first
- explicit `configure` remains available so users can run configuration-only
  workflows
- configure freshness is tracked by backend-owned per-config stamps under
  `.nob/`

This rule applies equally when invocation begins with the default no-subcommand
entry.

## 5.1 Test Contract

The product-facing `test` command is distinct from scripted `ctest_test`
replay:

- `test` uses the frozen build-model test domain
- it filters by explicit names and optional `--config`
- it resolves first-token executable target names to runtime artifact paths
- it auto-builds those resolved targets before execution
- it runs commands from the frozen effective working directory

`ctest_test` remains a test-driver replay step:

- it also consumes the frozen build-model test domain
- it does not auto-build beyond prior explicit `ctest_build` replay
- in `C3`, the narrow local dashboard subset is supported
- in `C5`, that local-only subset additionally includes
  `ctest_coverage` and `ctest_memcheck` replay without adding new CLI
  subcommands

## 6. Helper Vocabulary

Generated helpers are part of the product surface and must stay understandable.

Required rules:

- helper names must map clearly to their responsibility
- phase helpers and action helpers must be grouped by behavior, not emitted as
  opaque unstructured code
- backend-specific utilities such as filesystem, process, archive, install, and
  test helpers may be emitted only when required by the frozen model
- helper generation must reflect the query-visible model, not hidden evaluator
  knowledge

Typical helper families include:

- filesystem helpers
- process execution helpers
- tool-resolution helpers
- phase dispatch helpers
- install/export/package helpers
- replay-action helpers

## 7. Tool Resolution

Generated runtime tool resolution remains explicit and layered.

Canonical precedence:

- environment override when documented for that tool
- embedded absolute path captured during generation when available
- bare tool lookup by name on the host

This rule already applies to `cmake`, `cpack`, `gzip`, and `xz` and extends to
new helper families only through documented additions.

Current documented additions include:

- `NOB_TAR_BIN` override, else bare `tar`, for configure replay archive
  extraction and archive-create helpers

## 8. Rejection Policy

Unsupported behavior in the generated backend must fail explicitly.

Required properties:

- stable user-visible diagnostic
- deterministic failure point
- rejection derived from canonical build-model or codegen validation, not from
  undefined runtime behavior
- variant-level rejection preferred over blanket family-level rejection once a
  family enters the closure program

The generated backend must not silently skip unsupported replay actions.

For `C3` through `C5`, explicit reject coverage includes at least:

- generic process replay such as `execute_process()` and `exec_program()`
- probe replay such as `try_compile()` and `try_run()`
- unsupported `ctest_*` dashboard/script/network variants such as
  `ctest_submit`, `ctest_upload`, `ctest_update`, `ctest_run_script`, and
  `ctest_read_custom_files`
- unsupported `FetchContent` provider, VCS, remote, and custom-command flows

## 9. Legibility Requirement

Legibility is a product requirement.

The generated `nob.c` must remain:

- auditable by a human
- structurally organized by phase and helper family
- understandable without reading evaluator internals

The backend may be large, but it must not collapse into:

- one opaque dispatcher blob
- an interpreter for raw Event IR
- generated code whose only verification path is black-box execution

## 10. Relationship To Build Model

Codegen consumes:

- existing target/build-step/install/export/package queries
- replay-domain queries defined by `build_model_replay.md`

Codegen does not consume:

- evaluator-private state
- raw Event IR
- ad hoc semantic reconstruction at render time

This keeps the frozen build model as the sole semantic representation read by
the generated backend.

## 11. Public Contract

The stable public contract of this document is:

- generated CLI command surface
- phase semantics and auto-configure rule
- helper vocabulary and tool-resolution precedence
- explicit rejection requirements and legibility expectations

## 12. Non-goals

- turning generated `nob.c` into a general evaluator replay engine
- consuming evaluator-private state or raw Event IR in codegen/runtime
- hiding unsupported behavior through silent runtime skips

## 13. Evidence

Expected evidence includes:

- codegen suite coverage for generated runtime command behavior
- closure-harness classification updates when support status changes
- explicit parity evidence for supported artifact-producing flows
