# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

# Evaluator Variables and Scope

Status: Canonical Target. This document defines the target variable and scope
model for the evaluator.

## 1. Scope

This document covers:
- variable storage classes,
- lookup precedence,
- frame overlays,
- function vs macro behavior,
- propagation rules,
- cache and environment interaction,
- projections from typed state.

## 2. Core Principle

Variables are not the primary semantic source of truth.

Canonical evaluator semantics live in typed session state such as:
- directory graph,
- property engine,
- cache store,
- target/test/install/export/package models,
- compatibility and policy state.

Variables are one projection layer over that state.

## 3. Storage Classes

The target architecture distinguishes four classes of visible values:

1. frame-local normal variables
2. session cache variables
3. environment values exposed through services
4. synthetic projections derived from canonical semantic state

Normal variables are owned by execution frames.

Cache variables are owned by the session cache model.

Environment values are resolved through `EvalServices`, optionally with a
session-managed overlay when CMake semantics require mutation.

Synthetic projections include values such as:
- `CMAKE_*`
- `PROJECT_*`
- target-derived metadata
- compatibility mirrors
- query results that are intentionally exposed as variables

## 4. Lookup Precedence

Target lookup order is:

1. frame-local overlay in the active execution context
2. visible parent overlays according to the current scope rules
3. session cache, when CMake lookup semantics permit fallback
4. environment or synthetic providers when the relevant syntax requests them

Synthetic state must not be copied into ad hoc shadow variables just to make
queries work.

## 5. Function, Macro, and Block Semantics

Functions create:
- a new execution context,
- a new normal variable frame,
- local argument bindings,
- explicit propagation only through CMake-defined mechanisms.

Macros create:
- a new execution context,
- argument overlays,
- no new normal variable scope.

Blocks create:
- a child execution context,
- local overlays and policy visibility according to block options,
- controlled propagation through `PROPAGATE` semantics.

## 6. Propagation Rules

Variable propagation is explicit and bounded.

Target propagation mechanisms include:
- parent-scope writes,
- `block(PROPAGATE ...)`,
- `return(PROPAGATE ...)`,
- command-specific directory or cache mutations.

Propagation updates the intended target scope or model directly. It should not
depend on replaying textual `set(...)` calls later.

## 7. Directory and Child Execution Boundaries

Directory entry creates a child execution context tied to a `DirectoryGraph`
node.

That child context inherits:
- the directory snapshot visible at entry,
- policy visibility,
- session-backed semantic models.

It may still have its own frame-local overlays for normal variables and
arguments.

## 8. Cache and Environment Rules

Cache entries live in session state and remain visible across runs unless
cleared by the owning session.

Environment access is service-backed.

The evaluator may expose an environment overlay when commands such as
`set(ENV{...})` require it, but that overlay remains a dedicated model owned by
the runtime, not a side effect hidden in arbitrary variable maps.

## 9. Projections from Typed State

The evaluator must be able to publish variable views derived from canonical
state without making those views authoritative.

Examples:
- `project(...)` updates project state first, then publishes `PROJECT_*`
  projections
- target and directory metadata are queried from canonical models first
- compatibility variables mirror session configuration instead of defining it

This rule is what keeps property, target, install, and directory semantics
consistent across commands.
