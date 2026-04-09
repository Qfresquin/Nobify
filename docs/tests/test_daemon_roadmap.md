# Test Daemon Fast-Track Roadmap

Status: Canonical active roadmap for the Linux-only test-infrastructure
rewrite whose sole goal is to reach a fast persistent test daemon as quickly
as possible.

This document supersedes the old preserved-entrypoint constraint around
`./build/nob_test` for this program. The current baseline runner still exists
today, but this roadmap explicitly authorizes replacing it with a new
`./build/nob`-owned client/daemon architecture even if that breaks the old
test command surface between waves.

This roadmap does not redefine evaluator, Event IR, build-model, codegen, or
`nobify` product contracts. It only defines how local and CI-facing test
infrastructure should evolve to maximize development speed.

## 1. Status

As of April 8, 2026:

- the current baseline test entrypoint is still `./build/nob_test`
- `src_v2/build/nob.c` does not yet own test dispatch
- `src_v2/build/nob_test.c` still owns module selection, profiles, incremental
  compilation, workspaces, logs, and preflight behavior
- the project now wants a faster path to a persistent daemon than the old
  compatibility-first test architecture allowed
- only the `nobify` product needs portability; test infrastructure does not
- inter-wave breakage is acceptable if it shortens the path to the daemon

The target end state of this roadmap is:

`./build/nob` front door -> daemon client/supervisor -> `nob_testd` -> shared runner core -> suites

## 2. Goal

The goal of this program is to replace the current standalone test-runner
experience with a Linux/POSIX-only client+daemon architecture that makes local
feedback materially faster and more ergonomic.

This means:

- `./build/nob` becomes the only official human entrypoint for tests
- `build/nob_testd` becomes the persistent execution owner
- watch mode, impact routing, and fast local profiles become first-class
  behavior of the daemon program
- legacy `./build/nob_test` compatibility is not a release-blocking concern

This program optimizes for:

- shortest path to a useful daemon
- faster edit/build/run loops
- fewer stale-bootstrap footguns
- one clear mental model for local test execution

It does not optimize for:

- preserving the historical runner CLI at all costs
- cross-platform test-runner portability
- early polish before the daemon is useful

## 3. Frozen Decisions

The following decisions are frozen for this roadmap:

- only `nobify` product portability matters
- test infrastructure may stay Linux/POSIX-only indefinitely
- `./build/nob_test` is transitional and may be removed later
- `./build/nob` becomes the official human entrypoint for tests
- `src_v2/build/nob.c` adopts `NOB_GO_REBUILD_URSELF_PLUS(...)` and owns test
  dispatch
- `src_v2/build/nob_test.c` may remain temporarily, but only as a wrapper or
  compatibility shim once runner-core extraction begins
- the new daemon binary is `build/nob_testd`
- the transport is a UNIX domain socket under `Temp_tests/daemon/`
- the first protocol is framed `argc + argv[]` payloads over the socket, not
  JSON
- daemon requests are serialized by default in early waves
- watch mode belongs to the daemon program, not to a separate parallel
  architecture
- external helper tools are allowed only as temporary accelerators in early
  waves and are not the final architecture
- the long-term implementation center is shared runner-core code under
  `src_v2/build/`, reused by the daemon and any temporary shim
- no future wave should spend effort on making the test runner portable unless
  that work directly helps `nobify`

The target CLI contract is:

- `./build/nob test <module>`
- `./build/nob test watch <module>`
- `./build/nob test watch auto`
- `./build/nob test daemon start`
- `./build/nob test daemon stop`
- `./build/nob test daemon status`

## 4. Why This Is A Separate Program

This roadmap is separate from the structural refactor plan because it changes
the test command surface and execution ownership aggressively.

The old structural plan assumed:

- `./build/nob_test` remains the official entrypoint
- runner ergonomics are preserved through internal refactors
- command-surface stability is a hard guardrail

This daemon program intentionally rejects those assumptions in order to move
faster. It treats the existing runner as useful implementation substrate, not
as a contract that must survive intact.

## 5. Workstreams

### 5.1 Front Door And Bootstrap

- move human test entry to `./build/nob`
- eliminate stale `nob` / `nob_test` bootstrap behavior
- treat self-rebuild as part of the normal workflow

### 5.2 Runner-Core Extraction

- extract module registry, profiles, incremental compilation, workspaces,
  logs, and preflight into reusable runner-core code
- reduce `nob_test.c` to a thin wrapper while it still exists

### 5.3 Daemon Runtime

- add `build/nob_testd`
- add client/daemon transport, lifecycle, and failure recovery
- keep early daemon execution serialized and simple

### 5.4 Watch And Impact Routing

- add first-class watch mode
- add path-prefix-based module routing
- keep routing explicit and cheap before attempting smarter graphs

### 5.5 Fast Local Feedback

- add dedicated fast local profile
- cache or skip repeated local preflight work where safe
- integrate compiler cache and parallel object compilation

### 5.6 Legacy Surface Removal

- remove or hollow out `build/nob_test`
- align docs and scripts around the new mental model

## 6. Wave Plan

### T0 Contract Reset And Program Start

Goal:
- establish the daemon rewrite as a Linux-only active program with permission
  to break the current runner surface

Deliverables:
- this roadmap under `docs/tests/test_daemon_roadmap.md`
- doc updates marking `./build/nob_test` as legacy/transitional
- explicit statement that test infra portability is out of scope
- explicit statement that `./build/nob` is the target front door

Non-goals:
- no implementation
- no compatibility guarantees

Exit criteria:
- one canonical roadmap exists
- docs no longer require preserving `./build/nob_test` as the primary test
  contract for this program

### T1 Self-Rebuilding Front Door

Goal:
- remove stale-bootstrap footguns immediately

Deliverables:
- `nob.c` uses `NOB_GO_REBUILD_URSELF_PLUS(...)`
- `nob_test.c` also uses `NOB_GO_REBUILD_URSELF_PLUS(...)` while it still
  exists
- `./build/nob test ...` is added as the new supported front door
- `./build/nob` builds and invokes the current runner as a transitional step

Non-goals:
- no daemon yet
- no major runner refactor yet

Exit criteria:
- changing `nob.c` or `nob_test.c` cannot leave the common workflow on stale
  binaries
- users can run test modules through `./build/nob test ...`

### T2 Runner-Core Extraction

Goal:
- extract reusable runner logic out of the standalone runner binary so the
  daemon can own it next

Deliverables:
- shared runner-core files under `src_v2/build/`
- module registry, profile logic, incremental compilation, workspaces, logs,
  and preflight moved into runner-core
- `nob_test.c` reduced to a thin wrapper over runner-core
- `nob.c` becomes the owner of user-facing test CLI parsing

Non-goals:
- no persistent daemon yet
- no attempt to keep old internal file boundaries

Exit criteria:
- daemon code can call runner-core APIs directly
- `nob_test.c` is no longer an architectural owner

### T3 Daemon MVP

Goal:
- land a real persistent daemon as fast as possible

Deliverables:
- new `build/nob_testd`
- one daemon process per workspace/session root
- UNIX socket listener under `Temp_tests/daemon/`
- simple request protocol carrying `argc + argv[]`
- simple response protocol carrying exit status, summary text, and artifact
  paths
- `./build/nob test daemon start|stop|status`
- `./build/nob test <module>` talks to the daemon and auto-starts it if absent

Non-goals:
- no watch mode yet
- no queue sophistication yet
- no portability layer

Exit criteria:
- a client can run at least one real module through the daemon
- the common test path no longer depends on direct use of `./build/nob_test`

### T4 Watch Mode

Goal:
- make the daemon useful for daily development

Deliverables:
- `./build/nob test watch <module>`
- `./build/nob test watch auto`
- Linux-first watcher implementation using native facilities, with polling
  fallback only if needed for robustness
- debounce and rerun coalescing
- clear watch output for changed files, selected modules, build phase, run
  phase, last result, and preserved failure workspace

Non-goals:
- no perfect dependency graph
- no parallel job scheduler yet

Exit criteria:
- file saves can rerun a chosen module or auto-routed modules through the
  daemon
- external watch tools are no longer required for the normal loop

### T5 Impact Router

Goal:
- stop rerunning the wrong suites

Deliverables:
- path-prefix-to-module routing metadata stored with the module registry
- initial routing for:
  - `src_v2/evaluator`
  - `src_v2/build_model`
  - `src_v2/codegen`
  - `src_v2/build`
  - `test_v2/evaluator_diff`
  - `test_v2/evaluator_codegen_diff`
  - `test_v2/artifact_parity`
  - shared helpers under `test_v2/`
- explicit fallback policy when a path is unknown
- route reporting in watch output

Non-goals:
- no full include-graph intelligence
- no attempt at minimal perfect test selection

Exit criteria:
- `watch auto` picks predictable modules for representative file changes
- implementers do not need to guess routing behavior later

### T6 Fast Loop Performance

Goal:
- make daemon-driven local feedback materially faster than the current runner
  path

Deliverables:
- dedicated fast local profile
- cached or skippable preflight for daemon local runs
- compiler-cache integration using `ccache` or `sccache` when available
- parallel object compilation in runner-core
- daemon-side caching of tool discovery and profile validation
- failure-first logging behavior for watch mode

Non-goals:
- no weakening of CI-oriented strict profiles
- no requirement that every profile be equally fast

Exit criteria:
- watch mode defaults to the fast profile
- repeated local runs avoid fixed-cost preflight and rediscovery work
- compile time drops materially for broad changes

### T7 Legacy Surface Removal

Goal:
- remove transitional clutter once the daemon path is stable

Deliverables:
- `build/nob_test` removed or replaced by a tiny documented compatibility shim
- docs stop presenting `./build/nob_test` as primary
- architecture docs describe `nob` as client/supervisor and `nob_testd` as
  execution owner
- any temporary external watch bridge is removed from the main path

Non-goals:
- no new daemon features
- no portability work

Exit criteria:
- there is one clear public entrypoint and one clear execution owner
- stale mental models around the old runner are gone

## 7. Required Interfaces And Behavior

The implementation target for later waves is:

- `nob` owns top-level command parsing for `test`, `watch`, and `daemon`
  subcommands
- `nob_testd` owns persistent execution state
- runner-core exposes build/run operations as callable APIs instead of
  embedding them in CLI-only flow
- logs, preserved failure workspaces, and `Temp_tests` layout remain
  conceptually intact unless a later wave explicitly replaces them
- old preflight checks stay logically owned by runner-core, but local daemon
  runs may cache or skip them under the fast profile
- sanitizer, coverage, and explicit heavy suites remain supported, but the
  fast loop is optimized around normal local development rather than around
  those profiles

## 8. Evidence And Acceptance

The daemon program is only complete when it has explicit proof for:

- `nob` self-rebuild after editing `src_v2/build/nob.c`
- transitional self-rebuild for `src_v2/build/nob_test.c`
- `./build/nob test <module>` end-to-end
- daemon auto-start on first client request
- `daemon start|stop|status`
- stale socket and stale PID recovery
- watch rerun on save
- `watch auto` routing for representative file changes
- fast profile behavior not regressing strict or sanitizer profiles
- preserved failure workspaces and captured logs still working through daemon
  execution

## 9. Assumptions

- only the product `nobify` needs portability
- test infra can stay Linux/POSIX-only indefinitely
- breaking the old runner contract is acceptable
- this roadmap is active and authoritative, not archival
- the shortest path to a useful daemon is preferred over compatibility, polish,
  or portability
