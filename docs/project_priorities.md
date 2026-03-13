# Project Priorities

Status: Canonical project-level direction for Nobify documentation.

## 1. Priority Order

Nobify follows this priority order:

1. **Primary: CMake 3.28 semantic compatibility**
   - The official comparison target is CMake 3.28.
   - Success means reproducing the CMake 3.28 behavior that affects real
     projects, generated artifacts, dependency structure, and script-visible
     outcomes.

2. **Secondary: historical CMake behavior**
   - Historical policies, wrappers, and legacy quirks matter when they are
     needed to keep real projects compatible with the CMake 3.28 baseline.
   - Historical parity is important, but it does not outrank the CMake 3.28
     compatibility target.

3. **Tertiary: Nob backend optimization**
   - Optimization work happens after semantic parity is stable enough to trust
     the reconstructed build model.
   - Optimizations must preserve validated CMake 3.28 behavior.

## 2. What "Compatibility" Means Here

Compatibility in this project is primarily about observable build semantics:

- command and control-flow behavior,
- variables, cache, properties, and policy-visible outcomes,
- generated files and declared targets,
- dependency and usage-requirement reconstruction,
- script-visible results that influence later evaluation.

This project is not trying to be a multi-version clone of every historical
CMake release at the same priority level. The baseline is one version first:
CMake 3.28.

## 3. Architectural Consequences

The current v2 architecture exists to support that priority order:

- `lexer` and `parser` recover syntax into AST,
- `evaluator` recovers CMake 3.28 semantics into canonical Event IR,
- `build_model` reconstructs a stable semantic model,
- `codegen` targets Nob,
- future optimization work should act on the stable model, not bypass semantic
  recovery inside the evaluator.

In short:

`CMake 3.28 parity -> stable semantic model -> Nob optimization`

## 4. Documentation Guidance

When writing or updating docs in this repository:

- state clearly when a contract targets CMake 3.28 behavior,
- label historical behavior as secondary unless it is required for that target,
- avoid phrasing that suggests "good enough for some libraries" is the top
  project goal,
- describe optimization work as downstream of semantic correctness, not as a
  competing priority.
