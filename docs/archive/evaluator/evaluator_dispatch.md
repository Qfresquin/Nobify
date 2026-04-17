# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Evaluator Dispatch

Status: Canonical Target. This document defines how command lookup and routing
fit into the target execution pipeline.

## 1. Scope

This document covers:
- command namespace ownership,
- native vs scripted command lookup,
- registry responsibilities,
- capability lookup,
- unknown-command behavior.

Dispatch is one stage of execution. It is not the architectural center of the
evaluator.

## 2. Command Namespace Model

The target command namespace is split between:
- `EvalRegistry` for native commands and their metadata
- `EvalSession` for scripted commands such as user-defined functions and macros

Structural AST nodes are not resolved through the command registry.

`NODE_COMMAND` execution resolves against both command namespaces using a
stable lookup order.

## 3. Lookup Order

For command nodes, canonical lookup order is:

1. native command lookup in `EvalRegistry`
2. scripted command lookup in `EvalSession`
3. unknown-command handling through diagnostics and compatibility policy

The exact matching rules are:
- command names are normalized the same way for native and scripted lookup,
- registry lookup is deterministic,
- capability metadata is available even when a command is unsupported,
- unknown-command behavior is shaped by compatibility state, not by ad hoc
  handler decisions.

## 4. Registry Responsibilities

`EvalRegistry` owns:
- built-in native command definitions,
- externally registered native commands,
- capability metadata,
- dispatch handlers for native commands,
- registry-level command existence queries.

The registry is the canonical metadata source for native commands.

It is not responsible for:
- storing user-defined functions and macros,
- owning session semantic state,
- deciding per-run directory or policy context.

## 5. Scripted Commands

Scripted commands are session state.

The target architecture requires the session to track:
- user-defined `function()` declarations,
- user-defined `macro()` declarations,
- shadowing rules between scripted and native commands,
- origin metadata for diagnostics and introspection.

Invocation of a scripted command creates an execution context appropriate to
its kind:
- functions create a new normal variable frame,
- macros create argument overlays without a new normal variable scope.

## 6. Capability Queries

Capability metadata belongs to the registry/session model, not to one mutable
execution context.

Target query surfaces:
- registry-level native capability lookup,
- session-level command-exists query,
- optional combined session-level capability lookup for tools that want one
  answer over native plus scripted namespaces.

Capability metadata supports:
- introspection,
- diagnostics,
- migration tracking,
- tooling/reporting.

It does not replace semantic implementation or dynamic validation.

## 7. Unknown Commands

Unknown-command behavior must be deterministic and compatibility-shaped.

The target architecture requires:
- one stable diagnostic code family for unknown commands,
- consistent command begin/end tracing when a stream is present,
- no silent semantic mutation for commands that failed lookup,
- no requirement that dispatch internals know about build-model consumers.

## 8. Relationship to the Pipeline

Dispatch is stage 1 of the command pipeline:
- it resolves the command provider,
- selects the request parser,
- hands execution to validation and mutation stages.

Dispatch must not:
- mutate canonical semantic state directly,
- open-code compatibility decisions that belong elsewhere,
- bypass transaction commit rules.
