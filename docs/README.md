# Nobify

**CMake Frontend. Nob Backend. Zero Runtime Dependencies.**

> "The language of the library should be sufficient to compile it. The build shouldn't require installing a second language just to exist."

Nobify converts projects written in CMake into a standalone **Nob** build system (C source), allowing you to compile C/C++ libraries **without requiring CMake in the final build environment**.

---

## The Philosophy: Recreational Programming

This project is, at its core, a **Recreational Programming** endeavor.

I started Nobify with a simple frustration: to compile a medium-to-large C/C++ project today, you practically have to learn two programming languages—C++ and CMake. CMake is powerful and standardized, but it is also bloated. It introduces a massive structural dependency (the CMake runtime, version policies, generators) just to invoke a compiler.

I am building this to prove a point to myself: **We can do better.**

The scope of replicating CMake is immense for a single person. I know that. But I am having fun.

## Current State: The "Magic" vs. The Craft

### Version 1 (Legacy)
The initial version of Nobify works. It has successfully compiled complex projects like **libcurl** and handles small header-only libraries well. The Lexer and Parser are solid.

However, the Transpiler and Build Model in v1 were built with a lot of "AI assistance" and trial-and-error. While it works, the code behaves like magic—I don't fully understand or own the logic behind it. It is a complex workaround rather than a system.

### Version 2 (In Progress)
I am currently rewriting the core **Transpiler** and **Build Model** from scratch (v2).

**Why?**
I don't want magic. I want engineering.
I am aiming for a clean, deterministic, and well-thought-out architecture (likely ~7000 lines of focused C code) that I can maintain, understand, and be proud of.

*   **Goal:** Strict separation of concerns (AST -> Event Stream -> Build Model -> Codegen).
*   **Status:** The v2 architecture is currently being specified and implemented. You can read the rigorous engineering contracts in the `docs/` folder.

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

## Project Structure & Roadmap

If you are interested in the architecture of the rewrite, check the documentation indices:

*   **[Build Model v2 Contract](docs/build_model_v2_contract.md):** The normative source of truth for the new engine.
*   **[Readiness Checklist](docs/build_model_v2_readiness_checklist.md):** The objective gates for v2.
*   **[Transpiler v2 Spec](docs/transpiler_v2_spec.md):** How we turn the model into code.

## Disclaimer

Nobify is not a 1:1 clone of CMake. It does not promise to support every legacy policy or historical quirk of the last 20 years.

**It promises functional compatibility for real-world libraries.**
If the library compiles, the artifacts are correct, and dependencies are preserved, Nobify has succeeded.

---

*Est. 2026. Built with hate for bloat and love for C.*