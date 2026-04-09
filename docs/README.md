# Nobify

**CMake Frontend. Nob Backend. Zero Runtime Dependencies.**

> "The language of the library should be sufficient to compile it. The build shouldn't require installing a second language just to exist."

Nobify converts projects written in CMake into a standalone **Nob** build system (C source), allowing you to compile C/C++ libraries **without requiring CMake in the final build environment**.

---

## The Philosophy: Recreational Programming

This project is, at its core, a **Recreational Programming** endeavor.

I started Nobify with a simple frustration: to compile a medium-to-large C/C++ project today, you practically have to learn two programming languages—C++ and CMake. CMake is powerful and standardized, but it is also bloated. It introduces a massive structural dependency (the CMake runtime, version policies, generators) just to invoke a compiler.

I am building this to prove a point to myself: **I can do better.**

The scope of replicating CMake is immense for a single person. I know that. But I am having fun.

## Current State: The "Magic" vs. The Craft

### Version 1 (Legacy)
The initial version of Nobify works. It has successfully compiled complex projects like **libcurl** and handles small header-only libraries well. The Lexer and Parser are solid.

However, the Transpiler and Build Model in v1 were built with a lot of "IA slop" and trial-and-error. While it works, the code behaves like magic—I don't fully understand or own the logic behind it. It is a complex workaround rather than a system.

### Version 2 (In Progress)
I am currently rewriting the core **Transpiler** and **Build Model** from scratch (v2).

**Why?**
I don't want magic. I want engineering.
I am aiming for a clean, deterministic, and well-thought-out architecture (likely ~7000 lines of focused C code in one file at most :) that I can maintain, understand, and be proud of.

*   **Goal:** Strict separation of concerns (AST -> Event Stream -> Build Model -> Codegen).
*   **Status:** The v2 architecture is currently being specified and implemented. You can read the rigorous engineering contracts in the `docs/` folder.

## Priority Order

The current project direction is:

1. **Primary:** achieve semantic compatibility with **CMake 3.28**.
2. **Secondary:** preserve historical CMake behavior when it is needed to keep
   real projects compatible with that 3.28 baseline.
3. **Tertiary:** optimize the generated **Nob** backend once semantic parity is
   trustworthy.

The canonical project-level statement of that order lives in
[`project_priorities.md`](./project_priorities.md).

---

## How It Works

Nobify treats CMake as an input DSL and Nob (C) as the execution backend.

1.  **Lexer/Parser:** Reads `CMakeLists.txt` and builds an AST.
2.  **Build Model (The Brain):** Evaluates variables, targets, and dependencies without executing CMake.
3.  **Transpiler:** Generates a `nob.c` file.

**The Result:**
*   You get a `nob.c` file.
*   You run it with a C compiler.
*   Your project builds.
*   **No CMake installation required.**

---

## Reference Specs

The implementation-level contracts for v2 live in focused docs under `docs/`.

- `docs/project_priorities.md`: canonical project direction and priority order.
- `docs/cmake_artifact_parity_roadmap.md`: historical parity summary plus
  handoff to the active closure program.
- `docs/evaluator_codegen_closure_roadmap.md`: canonical post-`P8` multi-wave
  closure roadmap that coordinates the remaining
  `evaluator -> Event IR -> build_model -> codegen` gap.
- `docs/evaluator/`: active evaluator documentation rewrite.
- `docs/build_model/`: canonical build-model docs, including the replay-domain
  contract used by codegen.
- `docs/codegen/`: canonical generated-backend runtime contract and CLI
  documentation.
- `docs/transpiler/`: Event IR boundary documentation between evaluator and
  build model.
- `docs/diagnostics/`: shared diagnostic logging, counters, and telemetry contract.
- `docs/lexer/`: lexer tokenization and source-position contract.
- `docs/parser/`: parser AST, grammar, and recovery contract.
- `docs/arena/`: arena allocator and `arena_dyn.h` memory helper contract.
- `docs/tests/`: test architecture baseline, suite taxonomy, and structural refactor roadmap for the v2 test stack.
  This area now also owns the explicit `evaluator -> codegen` diff harness
  contract under `docs/tests/evaluator_codegen_diff.md`, while the build-model
  and codegen directories own the normative downstream and runtime contracts
  that the harness proves. The active Linux-only daemon rewrite for test
  ergonomics also lives here under `docs/tests/test_daemon_roadmap.md`.
- `docs/archive/`: historical migration records and delivered detailed wave logs.

---

## Disclaimer

Nobify now treats **CMake 3.28** as its primary semantic baseline.

It is not trying to give every historical CMake release equal priority.
Older policies, wrappers, and quirks matter, but they are a **secondary**
compatibility target behind the CMake 3.28 baseline.

Backend-specific optimization is also important, but it comes **after**
semantic correctness and parity.

---

*Est. 2026. Built with hate for bloat and love for C.*
