# Evaluator Command Capabilities

Status: Canonical Target. This document defines the target capability metadata
model for evaluator commands.

## 1. Scope

This document covers:
- native capability metadata ownership,
- scripted command visibility,
- capability query APIs,
- relationship between capability metadata and semantic support.

It does not replace the implementation-current audit in
[evaluator_coverage_matrix.md](./evaluator_coverage_matrix.md).

## 2. Ownership Model

Capability metadata belongs to the registry/session model.

`EvalRegistry` owns metadata for native commands.

`EvalSession` owns visibility for scripted commands and may expose combined
command-exists or capability queries that merge:
- native registry metadata
- scripted command availability

Capability metadata is not owned by one active execution context.

## 3. Target Query Surface

Native query API:

```c
bool eval_registry_get_command_capability(
    const EvalRegistry *registry,
    String_View command_name,
    Command_Capability *out_capability);
```

Session query surface:

```c
bool eval_session_command_exists(const EvalSession *session,
                                 String_View command_name);
```

An implementation may also provide a combined session-level capability query,
but the architectural requirement is that capability ownership is registry or
session based, not context based.

## 4. Capability Meaning

Capability metadata is intended for:
- introspection,
- migration tracking,
- diagnostics enrichment,
- tooling and reporting.

It is not a substitute for:
- command parsing,
- semantic validation,
- compatibility policy,
- command coverage audits.

`FULL` capability metadata means the implementation claims the command is
intended to be complete for the supported baseline. It does not exempt the
command from behavioral tests.

## 5. Scripted Commands

Scripted commands are semantically "available" through session state, but they
are not registry-backed native capabilities.

Tools querying command existence may need to consider both:
- registry-native capabilities
- session-scripted declarations

The architecture allows that combined view while preserving separate ownership.

## 6. Relationship to Coverage Audits

The coverage matrix remains the implementation-current audit against CMake
3.28.

Capability metadata may differ from the audit when:
- implementation is mid-migration,
- behavior is only partially faithful,
- a command exists for dispatch or tooling reasons but is not semantically
  complete.

When they differ:
- capability metadata explains the registered command surface,
- the coverage matrix explains the audited behavior.
