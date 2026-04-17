# Historical

Superseded by the active `CMake 3.28 parity total -> Nob` documentation reset.
Not canonical.

# Build Model Closure Matrix

Status: Primary major-work closure matrix for the downstream `Build_Model`.

This is the matrix to use when the goal is:

> after implementing all non-boundary rows here, only minor fixes, bug work,
> and corpus-specific tightening should remain for the `Build_Model`.

Unlike the command-by-command
[build_model_coverage_matrix.md](./build_model_coverage_matrix.md), this
document is not an ownership audit. It is a structural closure checklist for
the big semantic slices that usually force real code changes.

If this matrix still has many `partial` or `open` rows, the project is not
close to "OpenCV should probably just work" even if many individual command
rows look green in the command matrix.

## Overview

- Scope: `Event_Stream -> Build_Model -> Query -> codegen`
- Goal: identify the remaining major build-model work needed before downstream
  artifact parity stops requiring architectural changes
- Companion audit:
  [build_model_coverage_matrix.md](./build_model_coverage_matrix.md)
- Current supported subset claim:
  [../codegen/generated_backend_supported_subset.md](../codegen/generated_backend_supported_subset.md)

## Status Taxonomy

`Status`
- `closed`: the build-model architecture for this slice is strong enough that
  remaining work should be minor fixes, not new core modeling
- `partial`: the slice exists and works for part of the supported/product path,
  but wider semantics still likely require meaningful code changes
- `open`: the current build-model shape is not yet strong enough to treat the
  remaining work as minor
- `boundary`: intentionally outside the current product claim, but kept visible
  because full CMake-like parity would eventually need a decision here

`Proof`
- `strong`: explicit build-model, pipeline, codegen, and/or real-project proof
  already exercises this slice as a first-class downstream concern
- `moderate`: the slice is exercised indirectly or only for a narrower subset
- `weak`: ownership exists, but downstream proof is still thin
- `none`: no serious downstream proof should be assumed from this matrix

Interpretation rule:
- This is the matrix that answers "how much major build-model work is left?"
- A row marked `closed` is much closer to "don't redesign this again."
- A row marked `partial` or `open` means significant code changes are still a
  normal expectation.

## Closure Matrix

| Capability Slice | Primary Domains | Status | Why Large Work May Still Remain | Finish Condition | Proof |
|---|---|---|---|---|---|
| Directory graph, nesting, and owner capture | `project`, `directory` | `closed` | Mostly tightening and bug work remain, not new architecture. | Nested directory ownership, parentage, and inherited context no longer require new storage design. | `strong` |
| Target declaration, kind identity, alias/imported identity | `target` | `closed` | Target identity is now canonical: `UNKNOWN_LIBRARY` is distinct from utility targets, imported/alias globality is first-class, and alias kind queries resolve to the effective target family. | New target-family work fits into the existing target record shape without structural redesign. | `strong` |
| Source membership, file sets, and source-file properties | `target`, `build_step` | `closed` | Source membership is now modeled canonically: regular sources, `INTERFACE_SOURCES`, typed file sets, generated status, producer linkage, and source-local compile metadata no longer depend on raw property carry-through. | New source/file-set parity work fits the existing typed source and file-set query surface without new core model shapes. | `strong` |
| Usage-requirement raw item model | `directory`, `target` | `closed` | Usage requirements are now canonical across direct events and supported property setters: directory/global `LINK_LIBRARIES`, target include/define/option/feature/link families, and `SYSTEM` provenance no longer depend on the raw property bag as their primary downstream source. | Include dirs, defs, opts, link libs, link dirs, link opts, compile features, flags, and provenance fit the stable item model without new families; remaining work is limited to transitive semantics and intentional raw-property boundaries such as custom keys and `APPEND_STRING`. | `strong` |
| Effective propagation and transitive query semantics | `target`, `directory`, `query` | `closed` | Effective queries now seed closure transitively from global, directory, and target link-library families, resolve aliases/imported targets during traversal, and respect compile-vs-link plus build/install context at edge-selection time. | Usage-requirement propagation for the supported subset fits the existing query-time closure model without new storage or API shape; remaining work belongs to adjacent slices such as broader genex/config expansion, not a redesign of effective query semantics. | `strong` |
| Generator-expression-carrying downstream preservation | `target`, `directory`, `install`, `export`, `replay` | `open` | This is one of the biggest sources of "looks implemented row-by-row, still fails on real projects." | Artifact-relevant genex context is preserved or resolved downstream strongly enough that codegen does not need evaluator-private escape hatches. | `weak` |
| Config, language, and platform split | `target`, `query`, `replay` | `partial` | Some context-sensitive querying exists, but broad multi-config/platform closure is still a common major-work source. | Per-config, per-language, and per-platform artifact decisions stop forcing new model/query design. | `moderate` |
| Output naming, prefixes/suffixes, and artifact path resolution | `target`, `query` | `partial` | Core output fields exist, but project-scale artifact naming/layout can still expose missing shape. | Executable/library/archive/runtime file resolution is stable enough that new projects mostly hit bugs, not schema gaps. | `moderate` |
| Custom command and custom target graph | `build_step` | `partial` | This slice is central for large projects and still tends to uncover ordering/byproduct/detail gaps. | Outputs, byproducts, deps, commands, and ordering semantics are carried without frequent structural additions. | `moderate` |
| Explicit dependency graph and ordering closure | `target`, `build_step` | `partial` | Explicit deps exist, but full build ordering confidence still depends on adjacent slices being stronger. | New dependency-related parity failures rarely require new graph entity kinds or relation storage. | `moderate` |
| Imported targets, package results, and imported artifact resolution | `package`, `target`, `query` | `partial` | Current real-project proof is narrow compared with the general imported-target universe. | Imported files, linker files, link languages, and package metadata are stable enough for broader package ecosystems. | `moderate` |
| Install graph and install-time target resolution | `install`, `target` | `partial` | The core path works, but the full install surface is broader than the currently proven subset. | New install parity issues are mostly rule bugs, not missing install-model structure. | `strong` |
| Export graph and downstream package regeneration | `export`, `package`, `target` | `partial` | Export is proven for a supported subset, but broader regeneration/import ecosystems remain larger than the current claim. | Export/import round-tripping no longer requires new build-model entity families for common package styles. | `moderate` |
| CPack and package planning | `cpack`, `package` | `partial` | Archive/package support exists, but packaging remains much wider than the current proven slice. | Common package-plan metadata fits the model without recurring schema growth. | `weak` |
| Configure replay for deterministic filesystem and local host effects | `replay` | `partial` | The landed subset is useful, but the full configure-effect universe is still much larger. | Most artifact-relevant configure effects are represented canonically; remaining cases are small or intentional boundaries. | `moderate` |
| Process, probe, and explicit reject ownership | `replay` | `partial` | Ownership exists, but positive runtime semantics are still incomplete for several families. | Unsupported host/process/probe cases are either fully modeled for execution or fully bounded without schema churn. | `weak` |
| Test domain, test properties, and target-command resolution | `test`, `target`, `query` | `partial` | Core test ownership exists, but broader test-property and execution semantics can still surface new needs. | Test discovery and execution-relevant metadata stop forcing new model fields in common project cases. | `moderate` |
| Local CTest replay and staged testing artifacts | `replay`, `test` | `partial` | Local-only support exists, but the wider CTest surface remains much larger. | Local test-driver flows no longer require new replay payload shapes for common usage. | `moderate` |
| Dependency materialization and FetchContent closure | `replay`, `package` | `open` | Real-world dependency workflows still exceed the current local-only subset by a large margin. | Most dependency materialization paths needed by real projects fit stable replay/query shapes. | `weak` |
| Language enablement, toolchain, and cross-compilation-visible state | `project`, `target`, `query`, `replay` | `open` | Large C/C++ projects regularly stress this area, and the current supported claim does not close it. | Toolchain- and language-driven artifact decisions no longer require new build-model semantics. | `weak` |
| Real-project corpus breadth | `all downstream domains` | `open` | The current proven corpus is still small compared with the ecosystem size. | Broader real-project proof stops revealing major new build-model entity/query gaps. | `moderate` |

## Notes

- This matrix is the one to use for "are we almost done with major build-model
  changes?"
- The command-level matrix can stay green-ish while this matrix is still far
  from closed, because real projects fail on interactions between slices, not
  only on isolated command ownership.
- For a complex project such as OpenCV, the risky rows are not just one command
  like `add_library()`. They are the interaction-heavy slices:
  generator-expression preservation, multi-config/platform split, source/file
  metadata, custom-command graph, imported-target resolution, dependency
  materialization, and toolchain/language behavior.
- The current supported real-project claim remains the narrower corpus listed
  in [generated_backend_supported_subset.md](../codegen/generated_backend_supported_subset.md).

## Related Docs

- [Build model coverage matrix](./build_model_coverage_matrix.md)
- [Build model architecture](./build_model_architecture.md)
- [Build model query](./build_model_query.md)
- [Build model replay domain](./build_model_replay.md)
- [Generated backend supported subset](../codegen/generated_backend_supported_subset.md)
