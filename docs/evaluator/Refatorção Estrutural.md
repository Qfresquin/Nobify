# Evaluator General Refactor Roadmap (Filtered + Revised)

Status: decision-complete roadmap for evaluator architecture/documentation alignment.

## 1. Decision Log

| Idea | Title | Status | Decision |
| --- | --- | --- | --- |
| #1 | Subsystems with clear contracts | Accepted with Constraints | Extract internal modules incrementally while keeping `Evaluator_Context` as the integration boundary. No immediate full subsystem object graph/composition rewrite. |
| #2 | Build IR inside evaluator | Rejected | Do not introduce `BuildModelBuilder` in evaluator scope now. Keep evaluator boundary focused on AST -> Event IR. |
| #3 | Metadata-active command dispatch | Accepted with Constraints | Add indexed/hash-backed lookup for runtime native command registry. Keep capability metadata primarily descriptive/introspection-oriented, not the primary runtime policy engine. |
| #4 | Diagnostic system based on explicit codes | Accepted | Move to deterministic diagnostic code usage at emission points, with stable mapping to severity/class/hint semantics. |
| #5 | Domain-based split of large evaluator files | Accepted | Split high-churn large translation units by cohesive domains (string/target/file/etc.) with shared internal headers. |
| #6 | Child context isolation for nested execution | Rejected | Do not adopt child evaluator contexts for `include()`/nested execution now. Keep shared context model and explicit scope/flow controls. |
| #7 | Consistent compatibility refresh model | Accepted | Centralize compatibility refresh timing so decisions are based on one deterministic per-command-cycle refresh boundary. |

## 2. Constraints and Non-Goals

### 2.1 Accepted-with-Constraints Boundaries

- **#1 Subsystem split**: first step is internal boundary extraction (scope/policy/flow/diagnostic/file/deferred services) over the existing context model.
- **#3 Dispatch improvements**: index-based lookup improves runtime performance and determinism for command existence checks, while capability metadata remains an introspection/reporting contract first.

### 2.2 Non-Goals (Out of Scope)

- **#2 Build IR in evaluator** is out of scope for this roadmap.
- **#6 Child context execution model** is out of scope for this roadmap.

## 3. Implementation Phases

### Phase A — Diagnostic Code System Contract

- Define explicit diagnostic-code usage as mandatory at emission points.
- Keep deterministic mapping from code -> default severity -> error class -> hint contract.
- Align evaluator diagnostics docs and run-report semantics with code-first diagnostics.

### Phase B — Dispatcher Index + Lookup Contract

- Introduce indexed/hash-backed lookup over runtime native command registry.
- Keep dispatch order and unknown-command behavior explicit and unchanged unless documented separately.
- Preserve public capability API behavior while reducing O(N) lookup pressure in hot paths.

### Phase C — Compatibility Refresh Centralization

- Define one canonical compatibility refresh point per command evaluation cycle.
- Ensure policy decisions (unsupported/error-budget/continue-on-error) read a consistent refreshed state.
- Document fallback semantics for compatibility variable mutations that occur between command boundaries.

### Phase D — Domain File Decomposition

- Split oversized modules into domain-focused units (for example string/target/file families).
- Add shared internal headers where needed for internal contracts and helper reuse.
- Keep behavioral contracts unchanged while reducing maintenance and review surface area.

### Phase E — Incremental Subsystem Extraction Boundaries

- Extract internal service boundaries in steps without replacing `Evaluator_Context` as the primary runtime integration object.
- Prefer testable service seams (scope, policy, flow, diagnostics, dispatcher, file/deferred) with stable interfaces.
- Defer any full structural composition rewrite until post-stabilization and separate RFC.

## 4. Acceptance Conditions for This Roadmap

- The evaluator boundary remains context-centric and Event-IR oriented.
- Rejected items (#2, #6) do not reappear as active roadmap commitments.
- Partial items (#1, #3) are implemented only under stated constraints.
- Compatibility, diagnostics, and dispatcher documents remain mutually consistent.
