# Evaluator v2 Specification

Status: Canonical Target. This document defines the target evaluator contract
for the post-refactor architecture. Current `src_v2/evaluator` code may lag
behind this document during migration.

Project priority framing:
- evaluator semantics target **CMake 3.28** first,
- historical behavior is modeled when it is required to preserve that
  observable baseline,
- Nob optimization remains downstream of evaluator semantic reconstruction.

## 1. Scope and Boundary

The evaluator consumes parser AST and mutates canonical semantic state. It may
project those committed changes to Event IR, diagnostics, and run results.

Canonical boundary:

`Ast_Root -> execution pipeline -> semantic state mutation -> Event IR/result projection`

Input:
- `Ast_Root` from `src_v2/parser`
- `EvalSession`
- `EvalExec_Request`

Output:
- `EvalRunResult`
- optional `Event_Stream` projection

The evaluator is no longer specified as one public mutable
`Evaluator_Context`. The target architecture is session-based and explicitly
separates:
- persistent semantic state,
- transient execution frames,
- command registry metadata,
- backend services,
- transactional command application.

Structural details are defined in
[evaluator_architecture_target.md](./evaluator_architecture_target.md).

## 2. Canonical Document Hierarchy

This file is the top-level evaluator contract.

Subordinate canonical target documents:
- [evaluator_architecture_target.md](./evaluator_architecture_target.md)
- [evaluator_runtime_model.md](./evaluator_runtime_model.md)
- [evaluator_execution_model.md](./evaluator_execution_model.md)
- [evaluator_dispatch.md](./evaluator_dispatch.md)
- [evaluator_variables_and_scope.md](./evaluator_variables_and_scope.md)
- [evaluator_compatibility_model.md](./evaluator_compatibility_model.md)
- [evaluator_event_ir_contract.md](./evaluator_event_ir_contract.md)
- [evaluator_diagnostics.md](./evaluator_diagnostics.md)
- [evaluator_command_capabilities.md](./evaluator_command_capabilities.md)

Implementation-current analysis remains in:
- [evaluator_coverage_matrix.md](./evaluator_coverage_matrix.md)
- [evaluator_audit_notes.md](./evaluator_audit_notes.md)
- [evaluator_expressions.md](./evaluator_expressions.md)

Analytical documents may describe the current implementation, but they cannot
redefine this target contract.

## 3. Target Public API

The target public surface is session/request based.

### 3.1 Core Types

```c
typedef struct EvalSession EvalSession;
typedef struct EvalRegistry EvalRegistry;
typedef struct EvalServices EvalServices;
typedef struct EvalExec_Request EvalExec_Request;
typedef struct EvalRunResult EvalRunResult;

typedef enum {
    EVAL_EXEC_MODE_PROJECT = 0,
    EVAL_EXEC_MODE_SCRIPT,
    EVAL_EXEC_MODE_CTEST_SCRIPT,
} Eval_Exec_Mode;
```

Target configuration types:

```c
typedef struct {
    Arena *persistent_arena;
    const EvalServices *services;
    EvalRegistry *registry; /* optional; default registry if NULL */
    Eval_Compat_Profile compat_profile;
    String_View source_root;
    String_View binary_root;
} EvalSession_Config;

typedef struct {
    Arena *scratch_arena;
    String_View source_dir;
    String_View binary_dir;
    const char *list_file;
    Eval_Exec_Mode mode;
    Event_Stream *stream; /* optional */
} EvalExec_Request;

typedef struct {
    Eval_Result result;
    Eval_Run_Report report;
    size_t emitted_event_count;
} EvalRunResult;
```

### 3.2 Core Functions

```c
EvalSession *eval_session_create(const EvalSession_Config *cfg);
void eval_session_destroy(EvalSession *session);

EvalRunResult eval_session_run(EvalSession *session,
                               const EvalExec_Request *request,
                               Ast_Root ast);

bool eval_session_set_compat_profile(EvalSession *session,
                                     Eval_Compat_Profile profile);
bool eval_session_command_exists(const EvalSession *session,
                                 String_View command_name);
```

### 3.3 Registry Functions

```c
EvalRegistry *eval_registry_create(Arena *arena);
void eval_registry_destroy(EvalRegistry *registry);

bool eval_registry_register_native_command(
    EvalRegistry *registry,
    const EvalNativeCommandDef *def);
bool eval_registry_unregister_native_command(
    EvalRegistry *registry,
    String_View command_name);
bool eval_registry_get_command_capability(
    const EvalRegistry *registry,
    String_View command_name,
    Command_Capability *out_capability);
```

### 3.4 API Break Policy

This architecture intentionally breaks the old public API.

The following are not the canonical public contract anymore:
- `Evaluator_Context`
- `Evaluator_Init`
- `evaluator_create(...)`
- `evaluator_destroy(...)`
- `evaluator_run(...)`
- `evaluator_get_run_report(...)`
- context-attached native command registration as the primary extension point

Migration shims may temporarily preserve those entry points, but they are
non-normative compatibility layers.

## 4. Runtime Guarantees

The evaluator target runtime guarantees:
- one persistent `EvalSession` owns semantic state across runs,
- one transient root execution context is created per `eval_session_run(...)`,
- nested execution creates child execution contexts over the same session,
- commands apply semantic changes transactionally,
- failed commands must not leave half-committed canonical state,
- variable publication and Event IR projection happen from committed state.

The canonical persistent state includes:
- cache and compatibility state,
- directory graph,
- property engine,
- project / target / test / install / export / package models,
- user-defined function and macro definitions,
- session-visible command namespace metadata.

## 5. Event IR Boundary

Event IR remains the canonical evaluator output contract for downstream
consumers, especially the build model.

Important target rules:
- `Event_Stream` is optional per run,
- `stream == NULL` means "do not project events for this run",
- event emission is derived from committed semantic mutations,
- Event IR is not the source of truth for evaluator state,
- the evaluator must still return a complete `EvalRunResult` even when no event
  sink is provided.

The full Event IR boundary is specified in
[evaluator_event_ir_contract.md](./evaluator_event_ir_contract.md).

## 6. Diagnostics and Reporting

Diagnostics are produced through a diagnostics service and summarized in
`EvalRunResult.report`.

Target rules:
- diagnostic codes are stable and explicit,
- severity is shaped by compatibility state and execution mode,
- `EvalRunResult.report` is the canonical execution summary for a run,
- diagnostics and Event IR may both be emitted from the same committed command
  result, but they are distinct projections.

The detailed contract is specified in
[evaluator_diagnostics.md](./evaluator_diagnostics.md).

## 7. Compatibility Model

Compatibility is session-scoped, not an ad hoc side effect of arbitrary
context mutation.

The target model separates:
- session-level compatibility profile,
- policy state,
- execution-mode flags,
- unsupported-command behavior,
- error-budget / continue-on-error decisions.

Compatibility variables exposed to scripts are projections over this typed
state, not the source of truth.

The detailed contract is specified in
[evaluator_compatibility_model.md](./evaluator_compatibility_model.md).

## 8. Non-Goals

The evaluator target architecture does not:
- embed the build model inside the evaluator,
- treat variables as the primary semantic store,
- require one mutable object to represent both session and active execution,
- require `Event_Stream` capture for every run,
- use dispatcher metadata as a substitute for semantic implementation.

## 9. Migration Note

During migration, the codebase may expose both:
- the current implementation-oriented `Evaluator_Context` API,
- the target session/request architecture described here.

When they differ, this document and
[evaluator_architecture_target.md](./evaluator_architecture_target.md) are the
normative target. Implementation-current behavior must be documented as audit
material, not as architecture direction.
