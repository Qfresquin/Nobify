# Evaluator Dispatcher v2 (Annex)

Status: Normative annex for command routing in `src_v2/evaluator/eval_dispatcher.c`.

## 1. Role

`eval_dispatcher` is the command routing layer between AST command nodes and evaluator command handlers.

Primary responsibilities:
- Match command name to built-in handler.
- Route unknown names to user-defined `function()`/`macro()` when available.
- Apply unsupported-command policy for unresolved commands.
- Expose command capability metadata via `eval_command_caps`.

## 2. Dispatch Mechanism

### 2.1 Built-in Table

Built-ins are stored as static `Command_Entry` entries:
- Key: command name (case-insensitive matching)
- Value: `Cmd_Handler`

Lookup strategy in current implementation is linear scan over the dispatch array.

### 2.2 Handler Signature

```c
bool (*Cmd_Handler)(Evaluator_Context *ctx, const Node *node)
```

Handlers return `false` only for fatal/internal stop conditions (for example OOM/stop state). Semantic failures are usually converted to diagnostics.

### 2.3 Registered Built-ins (Current)

Current built-ins include (non-exhaustive grouping):
- Flow/scope: `block`, `endblock`, `break`, `continue`, `return`
- Variables/stdlib: `set`, `unset`, `list`, `string`, `math`, `message`
- Project/targets: `project`, `add_executable`, `add_library`, target property/link/include commands
- Directory/include: `add_subdirectory`, `include`, `include_guard`, `include_directories`, `link_directories`, `link_libraries`
- Compatibility/project controls: `cmake_minimum_required`, `cmake_policy`, `cmake_path`
- Filesystem and packaging: `file`, `find_package`, `install`, CPack subset
- Tests/custom/other: `enable_testing`, `add_test`, `add_custom_target`, `add_custom_command`, `try_compile`

For authoritative command-level compatibility, see `evaluator_v2_coverage_status.md` and `eval_command_caps.c`.

## 3. Unknown Command Resolution

When no built-in command matches:

1. Evaluator checks user command registry (`function`/`macro`).
2. If found, invokes user command body:
- `function`: new variable scope
- `macro`: macro-frame binding model in caller scope
3. If still unresolved, emits diagnostic with severity decided by unsupported policy:
- `WARN` => warning
- `ERROR` => error
- `NOOP_WARN` => warning with explicit no-op hint

Unknown command handling is policy-driven and deterministic.

## 4. Capability Contract

Dispatcher capability API:
- `eval_dispatcher_get_command_capability(String_View name, Command_Capability *out_capability)`

Backing registry: `src_v2/evaluator/eval_command_caps.c`.

For unknown commands:
- API returns `false`
- `out_capability` is filled as `MISSING + NOOP_WARN`

This keeps client behavior deterministic even for unknown symbols.

## 5. Error and Diagnostic Contract

Dispatcher and handlers use centralized diagnostic emission (`eval_emit_diag`) so diagnostics carry:
- severity
- component
- command
- classification metadata (`code`, `error_class`)

Dispatcher itself does not reclassify diagnostics.

## 6. Generator Expression Policy

Dispatcher does not evaluate generator expressions (`$<...>`).
Arguments containing genex text are passed as literals to downstream event payloads.

## 7. Implemented Divergences

Current intentional divergences include:
- Unsupported built-ins are policy-managed instead of hard fail by default.
- User-defined command invocation is integrated directly in unknown-command fallback path.
- Capability API provides explicit `FULL|PARTIAL|MISSING` metadata independent of runtime success in a specific script.

## 8. Roadmap (Not Yet Implemented)

Roadmap, not current behavior:
- Alternative lookup structure (for example hashed lookup) if command table growth requires it.
- Extended command-surface parity with additional CMake legacy commands.
- Additional diagnostics for ambiguous function/macro override scenarios.
