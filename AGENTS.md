# Severed Chains — Agent Instructions

Project: Rust-based reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).

## Current Phase

Phase R0 (research foundation) is in progress. **Research only.** Do not implement engine code, create Rust crates or a Cargo workspace, add dependencies, or make architectural decisions until the research is explicitly approved.

## Language Rules

- All authored content (documentation, plans, comments, code, reports) must be written in **English**.
- Never generate Chinese (or any other non-English) text in `AGENTS.md`, plans, documentation, comments, or reports.
- All generated text files must end with a newline.

## Research Principles

1. **Research first.** Understand the underlying architecture and behavior of the original game and the reference implementations before proposing any design.
2. **Language-agnostic analysis.** Research subsystems, algorithms, data formats, state machines, binary layouts, timing, and observable behavior — not Java code. Do not map Java classes to Rust types one-to-one, and do not preserve Java design decisions (inheritance, exceptions, collections, class loading, framework idioms) without a demonstrated architectural reason.
3. **Evidence-based conclusions.** Every significant finding must be traceable to evidence: repository, revision, source file path, symbol (function/type/constant), line range where practical, observed behavior, and a confidence classification.
4. **Confidence classification** for every non-trivial finding:
   - `Verified` — directly supported by source code, tests, or documented formats.
   - `Inferred` — supported by evidence but not conclusively established.
   - `Unresolved` — insufficient or conflicting evidence.
   - `Proposed` — a future design decision, not an established fact.
   Never present an `Inferred` finding as `Verified`.
5. **Discrepancies are data.** When reference repositories disagree, or when reference behavior may differ from original game behavior, document the discrepancy and investigate it. Do not silently pick one interpretation.
6. **Preserve source references** in all research documentation. Do not copy large sections of source code into documents; reference it instead.
7. **Separate the layers of truth.** Distinguish clearly between:
   - (a) original game / PSX hardware behavior,
   - (b) reference-implementation-specific behavior (fixes, enhancements, workarounds, approximations),
   - (c) future design proposals for the Rust implementation.

## Reference Repositories (READ-ONLY)

| Repository | Path | Role | Revision |
|---|---|---|---|
| Severed-Chains | `~/Documents/GitHub/Severed-Chains` | Primary engine and game implementation | `0c08c679b6b54e167b00851f1f1458d4dea3eade` |
| Script-Recompiler | `~/Documents/GitHub/Script-Recompiler` | Fate script tooling (decompiler, compiler, patcher) | `3433e6f74bb6d562aeff0490d11e8937da437511` |
| Mod-Loader | `~/Documents/GitHub/Mod-Loader` | Mod loading, registries, events, i18n | `c692134907a070a8b2c7db7b2b779010bc16deb9` |
| Dig-Dabas | `~/Documents/GitHub/Dig-Dabas` | Dabas minigame reference implementation | `4c1fbfebced123d43282b6e3b08286336b6b05f0` |

- Treat these repositories as **reference material only**. Never modify them: no file changes, no new branches, no build configuration changes, no writing into the repository trees.
- Non-destructive inspection commands (read, grep, git log/diff/show) are allowed.

## Documentation

- All research documentation lives in `research/` in this workspace, organized by technical subject with descriptive filenames.
- `research/README.md` is the **central research index**: topics, investigation status, completed documents, relevant repositories, unresolved questions, known conflicts, and dependencies between topics. Keep it current as research progresses.
- The register of unresolved questions and research risks lives in `research/04-unresolved-questions.md`.
- Do not create empty documentation files to populate a directory structure. Create a document only when its content has actually been investigated.
- Written entirely in English, independent of the implementation language, and understandable without access to the original research conversation.

## Execution Boundaries

**Allowed:** inspecting reference source, searching repository history, reading documentation and tests, analyzing algorithms and data formats, non-destructive inspection commands, writing research documentation, updating this file for research-related instructions.

**Not allowed (without explicit authorization):** modifying reference repositories, implementing Rust functionality, creating production crates, mechanically translating Java to Rust, refactoring existing code, changing build configurations, introducing implementation dependencies, making irreversible architectural decisions, presenting speculative behavior as established fact.

- If a research question cannot be resolved, record it in the unresolved-questions register and continue with independent work.
- Before declaring a phase complete: re-check the index in `research/README.md`, verify each completed topic against its evidence, list every file created or modified, and stop to request approval before starting the next research milestone.
