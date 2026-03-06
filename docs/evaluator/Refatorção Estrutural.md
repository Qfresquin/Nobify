# Evaluator General Refactor Roadmap (Filtered + Revised)

Status: decision-complete roadmap for evaluator architecture/documentation alignment, including the next bulk-first internal refactor campaign.

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

### 2.3 Temporary Breakage Policy for Bulk Waves

- The next internal refactor campaign is explicitly allowed to enter temporarily broken intermediate states inside the working branch or local workspace while code is being moved.
- This permission exists to reduce repeated stabilization cost during large internal extractions; it does not change the target contract for the evaluator.
- Temporary breakage is acceptable only between planned checkpoints inside one active wave. It is not an acceptable final state for a completed wave.
- Each wave must close by restoring a usable evaluator baseline:
  - current evaluator targets compile again,
  - `./build/nob_v2_test test-evaluator` passes on a fresh rebuild baseline,
  - docs match the resulting boundaries closely enough to guide the next wave,
  - temporary shims/adapters are either removed or left behind intentionally with tracked follow-up.
- Public API stability still applies at wave end unless a separate RFC explicitly changes that contract.
- If a bulk wave becomes too large to re-stabilize in one pass, split it by subsystem boundary rather than by arbitrary file-count or line-count slices.

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
- Status: completed in the workspace on March 6, 2026.
- D1 completed `eval_file` decomposition into dispatcher + path/glob/rw/copy/extra/fsops/transfer units.
- D2 completed `eval_string` decomposition into dispatcher + text/regex/json/misc units.
- D3 completed `eval_target` decomposition into core/property handling + usage/linkage + `source_group()` units.
- D4 completed `eval_package` decomposition into `find_package()` core + shared `find_*` item-resolution unit.
- D5 completed `eval_flow` decomposition into shared helpers + block + `cmake_language()` + process units.
- D6 completed `eval_try_compile` decomposition into shared helpers + parse + compile exec + `try_run` units.

### Phase E — Incremental Subsystem Extraction Boundaries

- Extract internal service boundaries in steps without replacing `Evaluator_Context` as the primary runtime integration object.
- Prefer testable service seams (scope, policy, flow, diagnostics, dispatcher, file/deferred) with stable interfaces.
- Defer any full structural composition rewrite until post-stabilization and separate RFC.

### Phase F — Bulk-First Internal Service Campaign

- Goal: finish the bulk of the remaining internal structural movement quickly, even if that means tolerating temporary non-green states during the middle of the work.
- Scope: remaining large translation units, cross-cutting internal storage conventions, process/runtime side-effect seams, and repetitive handwritten command parsers that still slow down changes.
- Main rule: batch structural churn first, then do one stabilization pass per wave instead of repeatedly preserving perfect health after every micro-move.

Recommended wave order:

- F1 completed explicit `Evaluator_Context` state slices for scope/cache/bindings, runtime compatibility/report, command registry, and deferred/file queues while preserving one owning context.
- F2 completed the property store/query service boundary on March 6, 2026 by internalizing synthetic property-key composition, centralizing property definition/query/write behavior in `eval_property.c`, and simplifying the residual property core in `eval_target.c`.
- F3 completed the runtime process/environment service on March 6, 2026 by centralizing subprocess launch/capture/timeout/cwd handling in `eval_runtime_process.c`, routing `set(ENV{...})` and environment reads through one overlay service, and moving `try_compile` off open-coded output-file capture.
- F4 completed the declarative command grammar layer on March 6, 2026 by extending `eval_opt_parser` with tail/missing-value/duplicate-rule support, migrating `find_*`, `try_compile`, `try_run`, and explicit-mode `separate_arguments` to grammar specs/callbacks, and keeping `cmake_parse_arguments` on its existing keyword-table parser.
- F5 completed the remaining hotspot decomposition on March 6, 2026 by splitting property queries, runtime dependency handling, parse helpers, list helpers, path helpers, and command-line/path utilities out of `eval_target.c`, `eval_file_extra.c`, `eval_vars.c`, `eval_list.c`, `eval_cmake_path.c`, and `eval_utils.c` into adjacent internal modules.

1. **F1 State Partition Around `Evaluator_Context`**
   - Introduce explicit internal state groupings over the existing context-centric model.
   - Candidate slices:
     - scope/cache/bindings,
     - runtime compatibility/report/stop state,
     - native/user command registry,
     - file locks/deferred generation/deferred directory queues.
   - Keep one owning `Evaluator_Context`, but stop treating the full struct as the default mutation surface for every handler.

2. **F2 Property Store and Property Query Service**
   - Replace or encapsulate the current synthetic property-key convention so property reads/writes stop depending on ad hoc string-key composition spread across handlers.
   - Move property-definition lookup, inheritance walking, non-target property writes, and property query modes behind one internal service boundary.
   - Use this wave to simplify the remaining residual core in `eval_target`.

3. **F3 Runtime Process and Environment Service**
   - Consolidate subprocess execution, timeout handling, working-directory handling, stdout/stderr capture, and environment mutation/overlay under one internal runtime service.
   - Remove open-coded process side-effect handling from `execute_process`, `try_compile`, `try_run`, and environment-oriented variable flows.
   - The immediate objective is one place that owns process side effects; deeper semantic cleanup can happen later.

4. **F4 Declarative Command Grammar Layer**
   - Extend `eval_opt_parser` or add an adjacent grammar service so complex command parsers stop hand-walking token arrays whenever possible.
   - First migration targets:
     - `find_*`,
     - `try_compile`,
     - `try_run`,
     - `cmake_parse_arguments`,
     - `separate_arguments`,
     - other option-heavy handlers that still encode grammar inline.
   - Favor grammar/data-table driven parsing once the destination service is stable, even if adapters are needed during the migration.

5. **F5 Residual Hotspot Decomposition**
   - Split the remaining post-Phase-D hotspots by cohesive subdomain instead of continuing to accumulate unrelated helpers:
     - `eval_target.c`,
     - `eval_file_extra.c`,
     - `eval_utils.c`,
     - `eval_vars.c`,
     - `eval_list.c`,
     - `eval_cmake_path.c`.
   - Accept temporary helper duplication during the move, then collapse duplicates at the end of the wave.

6. **F6 Bulk Cleanup and Normalization**
   - Remove temporary forwarding layers that only existed to keep the bulk move flowing.
   - Normalize internal header ownership and helper placement.
   - Re-run the full evaluator verification baseline and align docs before declaring the campaign structurally complete.

Bulk-first execution rules for Phase F:

- Prefer moving whole concern families in one pass over a sequence of micro-refactors that each require a full stabilization cycle.
- Accept temporary duplication, compatibility adapters, or ugly intermediate wiring inside the wave if they shorten the time to the destination boundary.
- Defer naming polish, helper deduplication, and low-signal cleanup until the destination boundary has stopped moving.
- Avoid reopening already-settled roadmap decisions during the bulk campaign.
- In particular, do not reintroduce build-IR coupling or child evaluator contexts as side quests.
- Do not spend the bulk campaign on dispatcher rewrites unless a new blocker proves the current indexed dispatcher insufficient.

### Phase G — Semantic Promotion After Structural Bulk

- Once the bulk structural campaign returns to a stable green baseline, shift the main effort from moving code to increasing semantic completeness.
- Promote `PARTIAL` coverage in clusters rather than command-by-command so each wave pays one stabilization cost for one semantic family.

Recommended promotion order:

1. **G1 Property, query, and introspection cluster**
   - `get_property` and related wrappers.
   - Directory/source/target/test property query consistency.
   - Property inheritance and definition edge cases that become easier after Phase F2.

2. **G2 Advanced target and execution cluster**
   - `target_compile_features`,
   - `target_precompile_headers`,
   - `target_sources`,
   - `try_run` advanced signatures and workflows.

3. **G3 `ctest_*` cluster**
   - Treat the `ctest_*` family as one coordinated promotion effort instead of isolated commands.
   - Share metadata/runtime helpers where possible so the cluster does not keep re-encoding the same workflow logic.

4. **G4 Modern runtime/meta gaps**
   - `cmake_language` advanced surface,
   - `cmake_file_api`,
   - `cmake_host_system_information`,
   - remaining modern runtime/meta integration gaps.

5. **G5 Legacy compatibility wrappers**
   - Tackle only after the modern/core surfaces above have stabilized.
   - Prioritize wrappers that unblock real projects or reduce compatibility surprise; do not chase perfect historical parity by default.

Phase G working rule:

- Structural extraction should happen before large semantic-promotion pushes whenever both touch the same code path.
- The objective is to avoid paying the same regression-fix tax twice: once during movement and again during semantic completion.

## 4. Acceptance Conditions for This Roadmap

- The evaluator boundary remains context-centric and Event-IR oriented.
- Rejected items (#2, #6) do not reappear as active roadmap commitments.
- Partial items (#1, #3) are implemented only under stated constraints.
- Compatibility, diagnostics, and dispatcher documents remain mutually consistent.
- Temporary internal breakage is permitted only inside active bulk waves and only until the next stabilization checkpoint.
- A bulk wave is not complete until the evaluator is functional again and the current verification baseline is green.
- Structural bulk should reduce future stabilization cost; if a wave adds churn without improving future change velocity, it failed the roadmap intent.
