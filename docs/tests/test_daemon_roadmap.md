# Test Daemon Fast-Track Roadmap

Status: Canonical active roadmap for the Linux-only test-infrastructure
rewrite whose sole goal is to reach a fast persistent test daemon as quickly
as possible.

This document supersedes the old preserved-entrypoint constraint around
`./build/nob_test` for this program. The legacy shim is now gone; this roadmap
explicitly authorized replacing it with a new `./build/nob`-owned
client/daemon architecture even when that broke the old test command surface
between waves.

This roadmap does not redefine evaluator, Event IR, build-model, codegen, or
`nobify` product contracts. It only defines how local and CI-facing test
infrastructure should evolve to maximize development speed.

Historical wave descriptions are preserved below for implementation history.
Any remaining mentions of `./build/nob_test` or `src_v2/build/nob_test.c`
inside those earlier-wave sections are historical only, not current guidance.

## 1. Status

As of April 9, 2026:

- `./build/nob test ...` is the official human-facing test entrypoint
- `build/nob_testd` already owns the daemon-backed run path
- watch mode and impact routing already exist behind `./build/nob test watch ...`
- fast profile, daemon-side preflight caching, launcher detection, and parallel
  object compilation have already landed
- T5 throughput observability and compact watch ergonomics have already landed
- T6 legacy surface removal has already landed:
  - `build/nob_test` has been removed
  - `clean` and `tidy` are fronted through `./build/nob test ...`
  - `smoke` is the only supported public aggregate label
- only the `nobify` product needs portability; test infrastructure does not
- inter-wave breakage remains acceptable if it shortens time-to-daemon

The target end state of this roadmap is:

`./build/nob` front door -> daemon client/supervisor -> reactor `nob_testd` -> shared runner core -> suites

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
- `./build/nob_test` has been removed; any remaining mentions below are
  historical wave context only
- `./build/nob` becomes the official human entrypoint for tests
- `src_v2/build/nob.c` adopts `NOB_GO_REBUILD_URSELF_PLUS(...)` and owns test
  dispatch
- `src_v2/build/nob_test.c` has been deleted; earlier wave text may still
  mention it as historical context
- the new daemon binary is `build/nob_testd`
- the transport is a pathname `AF_UNIX` socket under `Temp_tests/daemon/`
- the socket type is `SOCK_SEQPACKET`, not `SOCK_STREAM`
- the first protocol is a versioned binary envelope with opcode + status fields
  plus argv-like string payloads; it is not JSON
- the client validates the daemon peer via `SO_PEERCRED`
- the daemon runtime is a single-process, single-threaded Linux reactor
- the first reactor implementation may use raw `epoll`/`inotify`/`timerfd`/
  `signalfd`/`pidfd_open` primitives directly, or `sd-event` if that shortens
  time-to-daemon
- daemon requests are serialized by default in early waves
- watch mode belongs to the daemon program, not to a separate parallel
  architecture
- watch mode and impact routing are one program, not two separate later
  integrations
- watch reruns use debounce, coalescing, and last-write-wins cancellation
- inotify overflow or cache inconsistency triggers explicit root rescan and
  reroute instead of silent stale state
- the daemon MVP already owns fast local feedback primitives: fast profile,
  cached tool discovery, cached profile validation, and cached/skippable local
  preflight
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

### 5.2 Runner-Core And Typed Control Plane

- extract module registry, profiles, incremental compilation, workspaces,
  logs, and preflight into reusable runner-core code
- add typed request/result and module/profile identifiers instead of keeping the
  future daemon path stringly-typed
- add per-module watch-root and routing metadata in the registry itself
- reduce `nob_test.c` to a thin wrapper while it still exists

### 5.3 Daemon Runtime

- add `build/nob_testd`
- add client/daemon transport, lifecycle, cache, and failure recovery
- make the daemon a Linux reactor, not a generic remote-runner shim
- keep early daemon execution serialized and simple, but supervise children
  explicitly

### 5.4 Watch And Impact Routing

- add first-class watch mode
- add routing from explicit module-owned watch roots
- combine routing, debounce, cancellation, and overflow recovery in the same
  implementation wave
- keep routing explicit and cheap before attempting smarter graphs

### 5.5 Fast Local Feedback

- bring fast local profile and fixed-cost caching into the daemon early
- extend with compiler cache and parallel object compilation after the daemon is
  usable
- keep heavy profiles and CI-style strict runs intact

### 5.6 Legacy Surface Removal

- remove or hollow out `build/nob_test`
- align docs and scripts around the new mental model

## 6. Wave Plan

The wave descriptions below intentionally preserve the state and assumptions
that were true when each wave was defined. T6 completion supersedes any older
wave text that still refers to `./build/nob_test` as if it were current.

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
- typed control-plane structures such as request/result objects and stable
  module/profile identifiers replace daemon-facing string parsing
- the module registry grows explicit metadata for:
  - watch roots
  - default local profile
  - explicit heavy/aggregate policy
  - watch-auto eligibility
- `nob_test.c` reduced to a thin wrapper over runner-core
- `nob.c` becomes the owner of user-facing test CLI parsing

Non-goals:
- no persistent daemon yet
- no attempt to keep old internal file boundaries

Exit criteria:
- daemon code can call runner-core APIs directly
- daemon/watch work no longer depends on reusing the old CLI string parser as an
  internal API
- `nob_test.c` is no longer an architectural owner

### T3 Daemon MVP And Fast-Loop Baseline

Goal:
- land a real persistent daemon as fast as possible

Deliverables:
- new `build/nob_testd`
- one daemon process per workspace/session root
- pathname `AF_UNIX` `SOCK_SEQPACKET` listener under `Temp_tests/daemon/`
- versioned binary request/response envelope carrying operation id, status, and
  argv-like string payloads
- peer validation via `SO_PEERCRED`
- single-threaded reactor runtime implemented either with raw Linux file
  descriptors or `sd-event`
- child supervision primitives wired from day one, including process tracking,
  timeout-aware termination escalation, and stale socket cleanup
- daemon-side cache for:
  - discovered tool paths
  - profile compatibility validation
  - local preflight stamps and invalidation keys
- dedicated fast local profile available on the daemon path from the MVP wave
- `./build/nob test daemon start|stop|status`
- `./build/nob test <module>` talks to the daemon and auto-starts it if absent

Non-goals:
- no watch mode yet
- no parallel scheduler yet
- no portability layer

Exit criteria:
- a client can run at least one real module through the daemon
- repeated local daemon runs reuse cached fixed-cost validation/tooling work
- the common test path no longer depends on direct use of `./build/nob_test`

### T4 Watch Mode, Impact Routing, And Cancellation

Goal:
- make the daemon useful for daily development

Deliverables:
- `./build/nob test watch <module>`
- `./build/nob test watch auto`
- explicit module watch roots stored in the registry and consumed by `watch auto`
- Linux-first watcher implementation using `inotify`
- debounce via timer-based scheduling rather than ad hoc sleeps
- rerun coalescing plus last-write-wins cancellation for save storms
- overflow/inconsistency recovery by rescanning roots and recomputing routes
- dynamic watch registration for newly created subdirectories under watched
  roots
- clear watch output for changed files, selected modules, build phase, run
  phase, last result, and preserved failure workspace

Non-goals:
- no perfect dependency graph
- no parallel job scheduler yet
- no requirement for a cross-platform watcher fallback

Exit criteria:
- file saves can rerun a chosen module or auto-routed modules through the
  daemon
- route selection is predictable and recoverable even after rename storms or
  inotify overflow
- external watch tools are no longer required for the normal loop

### T5 Throughput And Ergonomics Acceleration

Goal:
- materially improve daemon-era throughput and watch readability once the
  control loop exists

Deliverables:
- compiler-cache integration using `ccache` or `sccache` when available
- parallel object compilation in runner-core
- streamed or failure-first logging tuned for watch usability
- better invalidation/reporting for fast-profile cache hits versus misses
- explicit fallback routing policy remains visible in watch output when a path
  is unknown or too broad
- default watch output becomes compact/failure-first, with `--verbose` as the
  inspection path for roots, routed sets, and per-rerun detail

Non-goals:
- no full include-graph intelligence
- no attempt at minimal perfect test selection

Exit criteria:
- watch mode remains readable under frequent reruns
- compile time drops materially for broad changes
- fast local feedback is measurably cheaper than the pre-daemon runner path

### T6 Legacy Surface Removal

Goal:
- remove transitional clutter once the daemon path is stable

Deliverables:
- `build/nob_test` removed
- docs stop presenting `./build/nob_test` as primary
- architecture docs describe `nob` as client/supervisor and `nob_testd` as the
  reactor execution owner
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
- daemon-facing control flow uses typed request/result structures and stable
  ids, even if the CLI remains text-oriented
- the daemon is a single-threaded Linux reactor that owns:
  - local socket accept/read/write
  - child-process supervision
  - timer-based debounce and timeout handling
  - watch event intake and overflow recovery
- logs, preserved failure workspaces, and `Temp_tests` layout remain
  conceptually intact unless a later wave explicitly replaces them
- old preflight checks stay logically owned by runner-core, but local daemon
  runs may cache or skip them under the fast profile
- module registry entries carry the watch roots and routing metadata needed by
  `watch auto`
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
- client/daemon compatibility for the versioned local protocol
- peer validation on the local UNIX socket
- watch rerun on save
- `watch auto` routing for representative file changes
- last-write-wins cancellation under save storms
- watch recovery after rename churn or inotify overflow
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
