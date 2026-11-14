# Severed Chains — Agent Instructions

Project: Rust-based reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).

## Current Phase

**M10 (Cross-System Architecture Review) is in progress** (approved 2026-11-13; starting point `291ecb3`). Everything before it — R0, R1 (including the independent xhigh audit), M2 (assets), M3 (Fate scripting, including its xhigh audit), M4 (PSX hardware), M5 (rendering), M6 (audio), M7 (saves and configuration), M8 (modding), and **M9 (Game Systems: Content and Rules) including its bounded cross-system reconciliation** — is **complete** (M9 closed 2026-11-13 at `291ecb3`). This file intentionally does not duplicate the milestone history; the authoritative progress records are:

| Record | Location |
|---|---|
| Phase-status narrative, topic tables, progress history | `research/README.md` |
| Milestone scope, sequencing, verification criteria, checkpoint logs | `research/03-research-plan.md` |
| Open questions, conflicts, resolutions, research risks | `research/04-unresolved-questions.md` |
| Per-subject technical findings | `research/{assets,scripting,psx,rendering,audio,saves,modding,game}/` (M9 deliverables in `research/game/`; M10 cross-system synthesis in `research/cross/`) |
| Independent audits | `research/audits/r0-r1-xhigh-audit.md`, `research/audits/m3-xhigh-audit.md` |

- **Latest completed checkpoint:** **M9 complete** (2026-11-13, commit `291ecb3`) — all seven `research/game/` deliverables (01 Dabas, 02 title, 03 submap, 04 world map, 05a/05b/05c battle, 06 characters & progression, 07 inventory & items) checkpoint-verified, plus the bounded cross-system reconciliation (six cross-system interface pairs verified consistent; the RQ7 boundary consolidated — title/world map zero scripts, Dabas mechanism island, submap script-as-policy-layer, inventory/characters native + data-driven with a script-thin edge, battle "the engine is the law, the script is the courtroom"; 13 in-place corrections, including the 59-site `rand()` LCG census, the 148-entry equipment catalog, the `encounterAccumulator` address, and the `poison_guard` flag re-classification; **M9 gate: PASS**). Full per-checkpoint M9 summaries live in the M9 log in `research/03-research-plan.md`.
- **Currently in progress:** **M10 — Cross-System Architecture Review** — three deliverables under `research/cross/` (01 architecture review: initialization/shutdown, runtime services, shared state and ownership, concurrency, cross-system data and event flow; 02 compatibility checklist: the consolidated M1–M9 constraint set; 03 implementation-readiness notes: an assessment, explicitly not a design proposal), plus a complete re-triage of every open item in `research/04-unresolved-questions.md`, and the project-wide PSX address/offset documentation convention (below). M10 is synthesis and re-triage: no new subsystem research beyond bounded verifications needed for the re-triage, no Rust architecture, code, or dependencies.
- **Approval and commit boundaries:** all implementation work (Rust crates, code, dependencies, architectural decisions) is **not approved** — do not begin it; any step after M10 requires explicit user approval. Git commits are made by the user on request, never by the agent unilaterally.


**Research only.** Do not implement engine code, create Rust crates or a Cargo workspace, add dependencies, or make architectural decisions until the research is explicitly approved. No game disc images or extracted proprietary assets are available, downloaded, or committed: byte-level format claims rest on the reference decoders' source and must carry a validation category — **source-verified** (behavior established from reference code), **format-corroborated** (independent spec/implementation/test), or **asset-validated** (confirmed against real game bytes; none yet — record in the register instead of claiming it).

## Language Rules

- All authored content (documentation, plans, comments, code, reports) must be written in **English**.
- Never generate Chinese (or any other non-English) text in `AGENTS.md`, plans, documentation, comments, or reports.
- All generated text files must end with exactly one newline (see "File endings and auditing").

## File endings and auditing

- Every non-empty text file created or modified in this project MUST end with exactly one newline character.
- Use LF (`\n`) line endings unless an explicitly documented format requirement dictates otherwise.
- Do not introduce CRLF line endings.
- Do not add unnecessary blank lines at the end of files.
- Preserve binary files exactly; never append newlines to binary data.
- This policy applies to all applicable text files: Rust, Markdown, TOML, JSON, YAML, scripts, configuration files, and generated text.
- Audit all applicable project text files at the completion of every research or implementation milestone, using the repeatable audit procedure in `scripts/audit_file_endings.py` (detects missing final newlines, excessive trailing blank lines, and CRLF endings; distinguishes text from binary; reports violations without modifying files unless `--fix` is passed).
- Report any violations found and correct them. When correcting line endings, do not silently rewrite unrelated file content.
- Reference repositories are excluded from modification: they may be inspected but must remain read-only, and the audit never writes to them.

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
| Severed-Chains | `~/Documents/GitHub/Severed-Chains` | Primary engine and game implementation | `71ce130364ed00431d420891370d5314a4e3d4c4` (advanced from `0c08c679b` at M9-07/Phase A, 2026-11-13 — M1–M9-06 research was verified against the prior pin; the Phase-A delta census (game/07 §12.1) confirms no prior finding is invalidated) |
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

## PSX Address and Offset Documentation Conventions

Project-wide convention for documenting PSX RAM addresses and structure field offsets in human-readable annotations (established 2026-11-13, M10; applies to all newly authored material).

### Absolute PSX RAM addresses

When documenting an absolute PSX RAM address in a comment or descriptive annotation, use:

`xxxxxxxx - Description`

Example: `800c6ae8 - Submap encounter accumulator`

Requirements:
- Exactly eight hexadecimal digits for a 32-bit address.
- Lowercase hexadecimal characters.
- No `0x` prefix in the address-comment label.
- One space, a hyphen, and one space before the description.
- Use the actual verified address; never generate a plausible address from a symbol name.
- Preserve the distinction between a runtime RAM address, a ROM address, and an address in another address space.
- Do not label an address as PSX RAM unless its address-space classification is supported.

### Structure field offsets

When documenting an offset relative to a structure's base address, use:

`xx - Description`

Examples: `00 - Structure flags`, `0a - Field description`, `10 - Field description`, `1c - Structure field or boundary`

Requirements:
- Lowercase hexadecimal.
- Minimum width of two digits, zero-padded.
- Use additional digits when necessary for offsets larger than `ff`.
- No `0x` prefix in the offset-comment label.
- Offsets are relative to the containing structure, not absolute RAM addresses.
- Do not confuse hexadecimal offsets with decimal field indices.

### Inline placement

Prefer attaching the address or offset to the relevant documentation comment, field annotation, or table entry. Avoid detached lines such as `PSX RAM: 0x800c6ae8` followed separately by the actual description.

For a Rust field, the eventual convention would look like:

```rust
/// 0a - Field description.
field_name: u16,
```

For an absolute address annotation:

```rust
/// 800c6ae8 - Submap encounter accumulator.
```

These are **formatting examples only**, not permission to write implementation code during the research phase.

### Scope and exceptions

This policy governs human-readable address and offset descriptions. Do not mechanically alter:

- Executable source code or numeric literals.
- Java/Ghidra identifiers such as `encounterAccumulator_800c6ae8`.
- `@Method(...)` annotations.
- Source citations, file paths, or original identifier spellings.
- Script expressions or assembler syntax.
- Serialized formats, registry identifiers, or external interfaces.
- Existing tables where an explicitly typed `0x...` value is needed for parsing, arithmetic, or unambiguous machine-readable representation.

Maintain the full original value and address-space provenance in source references.

For existing M1–M9 documents, **do not perform a bulk formatting rewrite**. Apply the convention to newly written material and to existing passages only when they are being substantively edited for another justified reason. The purpose is consistent documentation, not changing the meaning of existing research.

## Execution Boundaries

**Allowed:** inspecting reference source, searching repository history, reading documentation and tests, analyzing algorithms and data formats, non-destructive inspection commands, writing research documentation, updating this file for research-related instructions.

**Not allowed (without explicit authorization):** modifying reference repositories, implementing Rust functionality, creating production crates, mechanically translating Java to Rust, refactoring existing code, changing build configurations, introducing implementation dependencies, making irreversible architectural decisions, presenting speculative behavior as established fact. Once Rust implementation is explicitly approved, it is additionally bound by the “Semantic Naming (Rust Rewrite)” policy below.

- If a research question cannot be resolved, record it in the unresolved-questions register and continue with independent work.
- Before declaring a phase complete: re-check the index in `research/README.md`, verify each completed topic against its evidence, list every file created or modified, and stop to request approval before starting the next research milestone.

## Semantic Naming (Rust Rewrite)

Governs all future Rust implementation work (M10 and later, once approved). The reference implementation is authoritative for observed behavior, not for naming quality: reverse-engineering artifacts in the reference source (auto-generated names, anonymous structures, memory addresses, field offsets, unidentified symbols) must not be perpetuated in the Rust rewrite.

### Semantic naming is mandatory

Every internal Rust identifier must have a meaningful, idiomatic name grounded in its observed responsibility. This applies to functions and methods; structures, enums, traits, and type aliases; structure fields and enum variants; variables, parameters, and constants; modules and internal interfaces; and state-machine components and callbacks.

Do not mechanically translate Java identifiers into Rust naming conventions. A mechanical rename does not satisfy this policy: `Struct1` → `struct_1`, or `_80001234` → `function_80001234`, is a violation, not compliance.

### Prohibited placeholder patterns

Do not introduce unidentified or auto-generated identifiers into production Rust merely because they exist in the reference source. The list below is illustrative, not an exhaustive blacklist — judge identifiers by their semantic clarity, not by whether they match a listed pattern:

1. Decompiler-generated names: `FUN_*`, `LAB_*`, `DAT_*`, `undefined*`, `local_*`, `param_*`.
2. Generic numbered types: `Struct1`, `Struct2`, `Class3`, `Type4`, or equivalent variants.
3. Address-derived names: `_80001234`, `sub_80001234`, `func_80001234`.
4. Offset-derived field names: `_04`, `_08`, `field_10`, `field_0x20`.
5. Unknown placeholders: `unk_*`, `unknown_*`, `var_*`, `data_*`, when used without meaningful context.
6. Superficially renamed placeholders: `struct_1`, `unknown_handler`, `function_80001234` — cosmetic transformations that retain the original ambiguity.

### Evidence-driven naming

Before assigning a semantic name to an unidentified symbol:

1. Examine its implementation and observable behavior.
2. Trace its relevant callers and callees.
3. Inspect the data it reads and writes.
4. Identify its side effects and state transitions.
5. Examine related structures, offsets, and access patterns.
6. Cross-reference existing research findings under `research/`.
7. Determine the narrowest behavior supported by the evidence.

Name the symbol for what it demonstrably does. Do not infer purpose solely from its address, offset, numeric suffix, or proximity to another symbol. Do not invent gameplay semantics unsupported by the available evidence.

### Structure and field reconstruction

Generic structures and offset-named fields require particular attention. For each structure:

- Determine its responsibility and ownership.
- Identify meaningful fields through access patterns and data flow.
- Preserve verified layout constraints: offsets, alignment, and sizes.
- Assign semantic names to fields wherever evidence supports them, and distinguish confirmed field meanings from provisional interpretations.
- A structure's original identifier is not evidence of its purpose.

If an offset must be preserved for binary compatibility, preserve it in the layout definition, documentation, or an appropriate assertion — not as the field name.

### Uncertain semantics

If a symbol cannot be confidently identified:

- Continue investigating before settling on a name; avoid falsely confident names.
- Use a narrowly descriptive name based on verified operations or representation.
- Clearly document the uncertainty when it affects implementation correctness.
- Record material unresolved questions using the project's established research process (`research/04-unresolved-questions.md`).
- Revisit provisional names when stronger evidence becomes available.

**Do not force an invented semantic name simply to eliminate a placeholder.** If no defensible name can be established, leave the symbol unresolved in research rather than prematurely introducing it into production Rust.

### Rust conventions

- `snake_case` for functions, methods, modules, variables, and fields.
- `PascalCase` for structs, enums, traits, and type aliases.
- `SCREAMING_SNAKE_CASE` for constants and statics.

Meaningful domain terminology is preferred over mechanical transliteration from Java. Avoid unnecessarily verbose names, but never sacrifice clarity to preserve an unidentified reference identifier.

### Source traceability

- Preserve original Java/Ghidra identifiers and addresses in research documents, and maintain source-to-Rust symbol mappings where needed.
- Include concise source references in code comments when useful; preserve evidence of uncertain interpretations.
- Do not bulk-rename research documentation merely to improve readability.

Original identifiers may appear in isolated reverse-engineering maps, compatibility definitions, diagnostic tooling, and source-reference comments where their exact identity is necessary. They must not become the default naming scheme for production code.

### Compatibility exceptions

Do not rename identifiers whose exact external representation is required for compatibility, including:

- Exported ABI symbols.
- Serialized keys and field names.
- Registry identifiers.
- Script-facing names.
- External resource identifiers.
- Binary format signatures.
- Hardware register definitions where established naming or addressing is required.

Internal Rust names may differ from external names through explicit mappings or serialization attributes. Preserve behavior, binary layout, calling conventions, and observable compatibility.

### Research versus implementation

Research documentation must retain enough original identifiers to make findings independently verifiable; production Rust code must use semantic identifiers grounded in those findings. **The reference implementation is authoritative for observed behavior, not for naming quality.**
