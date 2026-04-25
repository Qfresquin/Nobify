# Build Model Closure Matrix

Status: Primary major-work closure matrix for the downstream `Build_Model`.

This is the matrix to use when the goal is:

> after implementing all non-boundary rows here, only minor fixes, bug work,
> and corpus-specific tightening should remain for the `Build_Model`.


## Overview

- Scope: `Event_Stream -> Build_Model -> Query -> codegen`
- Goal: identify the remaining major build-model work needed before downstream
  artifact parity stops requiring architectural changes

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
| Generator-expression-carrying downstream preservation | `target`, `directory`, `install`, `export`, `replay` | `closed` | The remaining work is now operator growth and adjacent-row breadth, not another redesign of how downstream keeps genex context alive. | Supported target/directory usage, modeled install fields including `RENAME`, export path/content emission, and replay operands resolve or preserve genex through build-model query/context so codegen no longer depends on evaluator-private shortcuts for this slice. | `strong` |
| Config, language, and platform split | `target`, `query`, `replay` | `closed` | Config catalog, compile-language, platform, imported mapping, effective link-language, and replay operand context now close through frozen model/query data with integrated parity proof. Remaining gaps belong to adjacent output-layout, build-step, or toolchain/cross-compilation rows. | New supported per-config, per-language, and per-platform artifact decisions fit the existing frozen records plus `BM_Query_Eval_Context`/query APIs without new model shape. | `strong` |
| Output naming, prefixes/suffixes, and artifact path resolution | `target`, `query` | `closed` | Artifact naming/layout is now modeled as typed target data and resolved through `bm_query_target_effective_artifact` plus session context; codegen only rebases and emits paths. Remaining gaps such as PDB, SONAME, framework/bundle layout, versioned symlinks, RPATH, toolchain/cross-compilation, object dirs, and generator multi-config auto-subdirs are adjacent rows or explicit boundaries. | Runtime/linker artifact resolution for the supported local/imported target subset fits the existing frozen records plus `BM_Query_Eval_Context`/artifact query APIs without backend-local reconstruction of output name, prefix/suffix, or output directory. | `strong` |
| Custom command and custom target graph | `build_step` | `closed` | Build steps now have an effective query surface for outputs, byproducts, file/target/producer deps, command argv, working directory, depfile, and comment; APPEND merges into the original output rule and codegen consumes query results instead of re-evaluating build-step strings. Remaining gaps such as depfile parsing, IMPLICIT_DEPENDS scanning, cross-compiling emulators, shell state composition, and unsupported target-file genex families are explicit adjacent boundaries. | Supported custom command/target graph semantics fit the build-step record plus effective query APIs without backend-local command/path/order reconstruction. | `strong` |
| Explicit dependency graph and ordering closure | `target`, `build_step` | `closed` | Target/build-step ordering is now emitted from the build-model build-order query plus build-step effective views: explicit deps, interface/imported transitive deps, generated-source producers, custom target steps, hooks, and config-aware link prerequisites are centralized before codegen. `OBJECT_DEPENDS`, depfile parsing, `IMPLICIT_DEPENDS`, and compiler scanner semantics remain explicit adjacent boundaries. | Supported target/build-step ordering changes fit the build-order query and existing records without backend-local graph reconstruction. | `strong` |
| Imported targets, package results, and imported artifact resolution | `package`, `target`, `query` | `closed` | Imported locations and link-language resolution are already queryable; package-result metadata is now frozen and queried directly across module/config/missing plus redirect, registry, and provider paths; corpus consumers now assert manifest-declared imported target identity instead of relying only on build success. | New package/imported-target work for the supported subset fits the existing package, imported-artifact, and query surfaces without build-model schema expansion. | `strong` |
| Install graph and install-time target resolution | `install`, `target` | `closed` | Install rules were already typed/queryable; direct build-model coverage now freezes and queries mixed install graphs across executable/static/shared/module/interface targets plus file/program/directory rules; pipeline goldens now expose per-rule install graph evidence; synthetic parity now proves install/export parity for the supported POSIX/Linux target kinds; unsupported OBJECT/UTILITY/UNKNOWN and SCRIPT/CODE/EXPORT_ANDROID_MK installs are explicit reject boundaries. | The supported install slice fits the existing install/export query surface without build-model schema expansion; remaining gaps are deliberate backend boundaries rather than missing install-graph structure. | `strong` |
| Export graph and downstream package regeneration | `export`, `package`, `target` | `closed` | Export/install/package rules were already typed and queryable; direct build-model coverage now freezes and queries install exports, build-tree `export(TARGETS)`, build-tree `export(EXPORT)`, package registry records, component lookup, export-set association, nested ownership, and `EXPORT_NAME`; pipeline goldens now expose first-class export graph evidence and target identities; synthetic parity now proves downstream consumers for build-tree exports, installed config packages, and package registry regeneration; unsupported export families such as `APPEND`, `CXX_MODULES_DIRECTORY`, `PACKAGE_INFO`, and `SETUP` are explicit reject boundaries. | The supported POSIX/Linux export round-trip subset fits the existing export/install/package/query surfaces without build-model schema expansion; remaining export ecosystem gaps are deliberate backend boundaries rather than missing export-graph structure. | `strong` |
| CPack and package planning | `cpack`, `package` | `closed` | CPack archive planning now flows through canonical package records, component metadata, grouping policy, archive name/extension overrides, and explicit `CPACK_PROJECT_CONFIG_FILE` boundary handling; remaining work is generator expansion rather than package-plan redesign. | Supported archive package planning for `TGZ`, `TXZ`, and `ZIP`, including component archives, fits the existing model/query/codegen surface without recurring schema growth. | `strong` |
| Configure replay for deterministic filesystem and local host effects | `replay` | `closed` | Deterministic local configure filesystem and host-effect operations now replay through canonical build-model actions; the remaining remote/process/probe-like cases are explicit adjacent boundaries. | New deterministic local filesystem replay work fits the existing replay opcode, operand, query, and codegen helper surface without backend-local state reconstruction. | `strong` |
| Process, probe, and explicit reject ownership | `replay` | `partial` | Ownership exists, but positive runtime semantics are still incomplete for several families. | Unsupported host/process/probe cases are either fully modeled for execution or fully bounded without schema churn. | `weak` |
| Test domain, test properties, and target-command resolution | `test`, `target`, `query` | `partial` | Core test ownership exists, but broader test-property and execution semantics can still surface new needs. | Test discovery and execution-relevant metadata stop forcing new model fields in common project cases. | `moderate` |
| Local CTest replay and staged testing artifacts | `replay`, `test` | `closed` | The local CTest test-driver surface is now closed; remaining CTest work is explicit remote, VCS, dashboard upload/submit, and child-script boundary coverage rather than new local replay architecture. | Local `ctest_empty_binary_directory`, `ctest_start`, `ctest_configure`, `ctest_build`, `ctest_test`, `ctest_sleep`, `ctest_coverage`, and `ctest_memcheck` flows fit the existing replay payload/query shapes without schema churn. | `strong` |
| Dependency materialization and FetchContent closure | `replay`, `package` | `closed` | The deterministic local FetchContent materialization surface is now closed; remaining dependency work is explicit remote, VCS, provider, custom download/patch/update, and broader dependency-manager boundary coverage rather than new local replay architecture. | Local `FetchContent` `SOURCE_DIR`, local archive `URL`, `FetchContent_MakeAvailable`, and `FetchContent_Populate` flows fit the existing dependency-materialization replay payload/query shapes without schema churn. | `strong` |
| Language enablement, toolchain, and cross-compilation-visible state | `project`, `target`, `query`, `replay` | `open` | Large C/C++ projects regularly stress this area, and the current supported claim does not close it. | Toolchain- and language-driven artifact decisions no longer require new build-model semantics. | `weak` |
| Real-project corpus breadth | `all downstream domains` | `open` | The current proven corpus is still small compared with the ecosystem size. | Broader real-project proof stops revealing major new build-model entity/query gaps. | `moderate` |
