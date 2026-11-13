# Severed Chains — Agent Instructions

Project: Rust-based reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).

## Current Phase

**M9 (Game Systems: Content and Rules) is in progress** (approved 2026-11-08; starting point `5cbdd65`). Everything before it — R0, R1 (including the independent xhigh audit), M2 (assets), M3 (Fate scripting, including its xhigh audit), M4 (PSX hardware), M5 (rendering), M6 (audio), M7 (saves and configuration), M8 (modding) — is **complete**. This file intentionally does not duplicate the milestone history; the authoritative progress records are:

| Record | Location |
|---|---|
| Phase-status narrative, topic tables, progress history | `research/README.md` |
| Milestone scope, sequencing, verification criteria, checkpoint logs | `research/03-research-plan.md` |
| Open questions, conflicts, resolutions, research risks | `research/04-unresolved-questions.md` |
| Per-subject technical findings | `research/{assets,scripting,psx,rendering,audio,saves,modding,game}/` (M9 deliverables in `research/game/`) |
| Independent audits | `research/audits/r0-r1-xhigh-audit.md`, `research/audits/m3-xhigh-audit.md` |

- **Latest completed checkpoint:** **M9-06** — `research/game/06-characters-and-progression.md` (working tree, commit pending): the character data model (`CharacterData2c` field map, the `partyFlags_04` word, the `set()` copy contract); the template system (`CharacterTemplate` abstract surface, `RetailCharacterTemplate` — the `applyLevelUp` Pre/Post event surface and the 6-stat level-indexed increments, `applyDragoonLevelUp` (MP +20, SP +100, the 4 template dragoon-stat rows), `checkUnlocks`, and `hasDragoon` = the spirit item in `goods_19c` (no per-character flag); the 9 retail templates + the `DartCharacterData` divine-dart subclass with its `+0x4000` status-flag bit, the divine spell list, and the suppressed transform); the stat system (unary vs vitals, the 17 registered stat-mod types, fractional stats, `StatCollection`); level progression (the per-template XP curves — the abstract default `23000 + i×1000`, Dart's DXP switch — and the level-0 creation state: every character created at dlevel 1 (one `applyDragoonLevelUp` at creation: MP +20, SP +100, the dlevel-0 dragoon-stat row applied even before owning a spirit), SP starts at 0 (only HP/MP restored)); dragoon progression (the dlevel-5 dragon spells — `DragonSpell` battle stages 71–77 — and the DEFF-load machinery: `deffManager.flags_20 |= dragoonDeffFlags_800fafec[index] << 16`, the index-46 sound special case (an empty retail slot, placeholder-registered in L4), the extra-TIM table → dir 4115+i, the `SECT/DRGN0.BIN` DEFF pairs); the 9-element system (the retail packed-element flags 0x0–0x80; the three `SimpleElement` pairs water↔fire / earth↔wind / dark↔light are all **mutually** strong + self-weak; thunder no-strong; divine boost-self-only; the per-attack-element damage rules — attacking ×150/×50, defending identity, dragoon-space ×150/×50 — applied over the equipment-inclusive attack-element list); addition progression (the retail 10×5 `CHARACTER_ADDITIONS` table + `additionOffsets_8004f5ac`, the `level × 20` XP curve, the 5-level `levelMultipliers`, the reserved rows 7/13/18/22/28); spell progression (63 registered spells — retail 0–59 + 65–67, the `SPELL32–37` placeholders, the per-character dlevel gates, the first-spell pre-unlock at creation; Albert's and Rose's lists both draw on retail 14–19 without overlap); the post-battle report end-to-end (the `PostBattleScreen` state machine; the XP pool — per-enemy `xp_194`/`gold_196`/drops from `enemyRewards_80112868` via `loadEnemyDropsAndScript` @ 0x80109808 (`EnemyRewardsEvent`, mod-mutable), accumulated in `scriptSetBentDeadAndDropLoot` @ 0x800ccf2c, `livingChars_800bc968` filled at the post-combat camera-fade start — split main = total/living (raw integer division) vs secondary = safeDiv × `SECONDARY_CHARACTER_XP_MULTIPLIER` (default 1.0; vanilla preset 0.5); the 10/frame `givePendingXp`/`givePendingDxp` loops with CONFIRM skip; the `checkForUnlocks`/`applyLevelUpActions` dispatch; the addition-level-up loop); the character lifecycle (creation, the char-swap gate `CANT_REMOVE`/`UNLOCK_PARTY`, removal + party-flag backup (the 8-bit backup is comment-only), the save round-trip — addition/spell merge retail-first, `hasTransformed` from the `scriptFlags2` bits); the settled RQ7 boundary (characters/progression is the most **native + data-driven** system in the M9 surface — the engine owns every rule, the data owns every value, the script is only a thin producer: SP grants, mastery ticks, party-flag writes, the dragoon-space set — never a rule); the RNG/determinism census (no RNG in any progression path); C1–C40. Register: Q-70 advanced (characters/progression leg); Q-82–Q-88 opened (the charId-9/"DD" identity, the reserved addition-table slots, the `SpellStats0c` undecoded fields, the retail DXP source beyond `scriptGiveSp`, the retail secondary-XP share, the `SPELL32–37` placeholder slots, the 8-bit party-flag backup — all retail-byte source-verification pending). Predecessor checkpoints: M9-05c (commit `0ced525` final incl. corrections; `80db8c8` initial) documented the addition (timing-overlay) system end-to-end — the moddable battle-action menu registry + the native `scriptSetUpAndHandleCombatMenu` bridge + the L4 registry-ID dispatch; the `AdditionOverlaysEffect44` spinner engine (window computation, borders, input, completion states) and the per-hit script driver; the SP/mastery grants with the equipment-flag asymmetry (Wargod Calling halves addition SP and suppresses the mastery tick — the earlier "odd-slot" reading was wrong); the counterattack system (the `stor[8]` arming channel, the retail tutorial/counter words `var[45][242/243]` — written only by retail scripts, so every L4 `== 2`/`== 3` gate is dead in the reference, the "burn_out" retail item 0xc3 / DEFF 4313 verified from the DRGN1 diff hunks); dragoon transformation/detransformation (party SP pooling, the budget word never initialized from SP in the L4 script — U35); the d-attack and its "daddy" dragoon-addition HUD (4-segment vs Kongol 3-segment, `scriptGetDragoonAdditionHitsCompleted` −1/0/1..4); the complete status-ailment lifecycle (two initialization paths, the tick-word model, the per-ailment tables, un-stun-on-damage, the poison DoT MAX_HP/10); the post-battle action dispatch and `PostBattleScreen`; the settled RQ7 boundary for the whole surface (the script↔native contract enumerated); RNG/timing determinism (both spinners RNG-free); U1–U35. M9-05b (commit `a8f4f22` final incl. corrections; `54b08ac` initial) documented the battle entity/stat models and the exact damage/accuracy/resource pipelines (F1–F20); M9-05a (commit `f9096f4`) documented battle entry/exit, the `battleLoadingStage` machine, timing, the native turn system, the data model, the 362-slot script-function table, stat init/formula overview, audio, the `SEffe` overview, and the 6-event mod surface (B1–B20).
- **Next authorized checkpoint:** **M9-07** — `research/game/07-inventory-and-items.md`: the goods/item/equipment model, the inventory screens and shops, the item-use and equipment-swap rules, the `goods_19c`/inventory state, and the item side of the battle reward/drop paths (05a/05b/06) — the final M9 deliverable before the bounded cross-system reconciliation pass.
- **Remaining M9 deliverables:** 07 (inventory and items), followed by a bounded cross-system reconciliation pass. Per-document requirements (data models with citations, state machines, exact formulas/constants, the script/code boundary, the retail-value source, original-vs-reference tagging) are specified in the plan's M9 verification criteria; the cross-cutting priority is the script-vs-hardcode boundary per gameplay behavior (RQ7).
- **Approval and commit boundaries:** M10 (cross-system architecture review) and all implementation work (Rust crates, code, dependencies, architectural decisions) are **not approved** — do not begin them. Each M9 checkpoint stops for approval; git commits are made by the user on request, never by the agent unilaterally.


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
