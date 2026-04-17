# Historical

Superseded by the active `CMake 3.8 parity total -> Nob` documentation reset.
Not canonical.

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

The core execution-owner rewrite is now landed. The main remaining pain is
operational rather than structural: daemon state, recovery, lifecycle, and
watch session ownership now need explicit hardening ahead of any further
throughput or scheduler expansion.

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
- `./build/nob test daemon restart`
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

### 5.7 Operational Ergonomics, Recovery, And Introspection

- make daemon lifecycle and active-work state visible and explainable through
  CLI and protocol surfaces
- replace opaque `busy`/recovery behavior with structured control-plane
  outcomes and explicit admission policy
- remove manual stop/clean/status choreography from the common local workflow

### 5.8 Detached Watch Sessions And Session Ownership

- move long-lived watch ownership fully into the daemon instead of the active
  client connection
- let watch sessions survive transient client disconnects by default
- surface detached watch state and lifecycle through `status` and daemon
  control paths

### 5.9 Case-Scoped Debug UX

- add case-level filtering to the human-facing front door so developers can
  rerun one failing test case inside a heavy module without paying the full
  module cost every time
- emit a compact end-of-run failure summary that names the failing cases and
  their key locations instead of forcing log-grep as the primary diagnosis path
- keep this as a runner-core and daemon control-plane feature rather than a
  suite-local ad hoc convention

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

### T7 Operational Hardening And Introspection

Goal:
- make the daemon predictable to operate during normal day-to-day development

Deliverables:
- enriched `daemon status` output covering effective state, active work,
  module/profile, run duration, attached client state, preserved logs or
  workspace paths, pending cancel/drain state, and cache hit/miss reasons
- explicit admission policy:
  - foreground run versus another foreground run rejects with a structured
    error instead of only free-form text
  - watch reruns use documented replace-running behavior
  - the daemon reports which policy was applied for the active request
- `daemon stop` becomes drain-aware instead of failing solely because the
  daemon is currently `busy`
- `daemon stop --force` becomes the official immediate-cancel path with
  timeout-aware escalation
- `./build/nob test daemon restart` becomes an official lifecycle path
- `./build/nob test clean` stops requiring manual pre-stop coordination:
  - default `clean` coordinates daemon drain/stop first
  - `clean --force` performs immediate daemon teardown before cleanup
- the binary protocol gains stable error codes plus optional metadata for the
  active request so text messages stop being the only control-plane surface
- stale socket/PID recovery and kill-escalation outcomes become visible through
  `status` and structured errors instead of remaining purely internal behavior

Non-goals:
- no general parallel scheduler yet
- no detached watch sessions yet
- no portability work

Exit criteria:
- `busy` is explainable and recoverable from the CLI
- `status` shows what the daemon is actually doing
- `stop` and `clean` work from both idle and busy states
- watch replace/cancel behavior is explicit instead of implicit

### T8 Detached Watch Sessions

Goal:
- decouple watch session lifetime from the client connection that started it

Deliverables:
- watch sessions become daemon-owned state instead of attached-client state
- terminal or client disconnect no longer stops watch by default
- `status` and daemon lifecycle surfaces report and control detached watch
  sessions explicitly

Non-goals:
- no multi-user sharing model
- no rich attach UX in this wave

Exit criteria:
- watch survives client disconnect
- detached watch ownership and state are visible and controllable through the
  daemon surface

### T9 Case Filters And Failure Summaries

Goal:
- make heavy daemon-fronted modules materially cheaper to debug when only one
  or two cases are failing

Deliverables:
- `./build/nob test <module> --case <case-name>` becomes an official
  front-door path for modules that expose case names through the runner-core
- the typed request/result path grows a canonical optional case-filter field
  instead of treating case selection as suite-local argv parsing
- runner-core and official suites may honor the case filter to skip unrelated
  cases inside the selected module binary
- failing module runs end with a compact summary block listing, at minimum:
  - failing case name
  - source file and line when available
  - preserved failure workspace when available
- the failure summary is emitted as the canonical first diagnostic surface for
  module-level failures so grep is no longer required just to identify the
  failing case
- daemon/client result reporting preserves the compact summary in both normal
  foreground runs and watch-triggered reruns

Non-goals:
- no arbitrary regex/query language for selecting cases
- no per-case parallel scheduler in this wave
- no replacement of full logs or preserved workspaces; this wave only improves
  the first failure-discovery step

Exit criteria:
- a known failing `codegen` or similarly heavy module case can be rerun through
  `./build/nob test <module> --case <case-name>`
- a failing module run ends with a compact machine-readable or consistently
  parseable human summary of failing cases
- developers no longer need an extra full-module rerun plus ad hoc `rg` just
  to learn which case failed

## 7. Required Interfaces And Behavior

The implementation target for later waves is:

- `nob` owns top-level command parsing for `test`, `watch`, and `daemon`
  subcommands
- `nob_testd` owns persistent execution state
- runner-core exposes build/run operations as callable APIs instead of
  embedding them in CLI-only flow
- daemon-facing control flow uses typed request/result structures and stable
  ids, even if the CLI remains text-oriented
- typed requests may carry an optional case filter for case-aware modules
- the control plane eventually exposes structured daemon states including
  `idle`, `busy`, `watching`, `draining`, and `stopping`
- the control plane eventually exposes stable error codes and optional active
  request metadata rather than relying on free-form text alone
- daemon lifecycle eventually includes `start`, `stop`, `stop --force`,
  `restart`, and `status`
- the daemon is a single-threaded Linux reactor that owns:
  - local socket accept/read/write
  - child-process supervision
  - timer-based debounce and timeout handling
  - watch event intake and overflow recovery
- `clean` eventually coordinates with daemon lifecycle instead of requiring
  users to manually stop the daemon first
- logs, preserved failure workspaces, and `Temp_tests` layout remain
  conceptually intact unless a later wave explicitly replaces them
- old preflight checks stay logically owned by runner-core, but local daemon
  runs may cache or skip them under the fast profile
- module registry entries carry the watch roots and routing metadata needed by
  `watch auto`
- detached watch session ownership eventually belongs to daemon state rather
  than to the lifetime of a single foreground client
- sanitizer, coverage, and explicit heavy suites remain supported, but the
  fast loop is optimized around normal local development rather than around
  those profiles
- module failures eventually surface a compact failing-case summary in addition
  to full logs and preserved workspaces

## 8. Evidence And Acceptance

The daemon program is only complete when it has explicit proof for:

- `nob` self-rebuild after editing `src_v2/build/nob.c`
- transitional self-rebuild for `src_v2/build/nob_test.c`
- `./build/nob test <module>` end-to-end
- daemon auto-start on first client request
- `daemon start|stop|restart|status`
- enriched `daemon status` for an active run, including active-work metadata
- stale socket and stale PID recovery
- client/daemon compatibility for the versioned local protocol
- peer validation on the local UNIX socket
- structured `busy` and recovery errors instead of text-only rejection
- `daemon stop` while work is active
- `daemon stop --force` against a stuck worker
- `./build/nob test clean` from both idle and busy daemon states
- `./build/nob test <module> --case <case-name>` for at least one heavy module
- compact end-of-run failure summary for module failures, including failing
  case identity and source location when available
- watch rerun on save
- `watch auto` routing for representative file changes
- last-write-wins cancellation under save storms
- explicit watch replace-running reporting under save storms
- watch recovery after rename churn or inotify overflow
- detached watch persistence across client disconnect in the T8 wave
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
