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

- **Latest completed checkpoint:** **M9-07** (commit `efc48f9`) — `research/game/07-inventory-and-items.md`: the three inventory containers + gold (`items_2e9` stack `Inventory`, cap = `INVENTORY_SIZE_CONFIG` default 32; `equipment_1e8` `List`, cap 255; `goods_19c` unique-`Good` set, unlimited; `gold_94` clamp 99,999,999 @ `SItem.addGold` 0x8002363c; the static battle-scoped `itemOverflow`/`equipmentOverflow` cleared at battle start); the data models (the `InventoryEntry` price model — buy = price×2 / sell = price, `Good` 0/0; `Inventory` give/take/merge/overflow — atomic-FAIL give with the temp-copy overflow simulation, `takeFromSlot` with cross-stack shortfall; `ItemStack` size+durability, merge-with-wear transfer; `Item` defaults — `maxStackSize` = `ITEM_STACK_SIZE` config, default 1 = vanilla no-stacking matching the retail save format; `Equipment` retail 0x1c layout — `flags_00` 0x4 no-discard / 0x40 instakill / 0x80 resist-instakill, the slot, the element/resist/immunity sets, the per-hit MP/SP ×4, the HP/MP/SP multipliers, the flat stat block 0x0f–0x1b; `Good`/`GoodsInventory` — the retail 32-bit bitset pack/unpack that leaves mod goods in place); the retail ID mapping (the 192-boundary equipment/item split, `ITEM_IDS` 64 / `EQUIPMENT_IDS` 192 / `GOODS_IDS` 37 / `SHOP_IDS` 66 (39 named + 27 `empty_shop`), consumers = the retail serializers + `scriptGiveChestContents` @ 0x800244c4 / `scriptTakeItem` @ 0x80024590 / game var 125); the operations and drop pipeline (the `SItem` primitives with retail addresses — `takeItemFromSlot` @ 0x800232dc, `takeEquipment` @ 0x800233d8, `giveEquipment` @ 0x80023484 (FAIL/TRUNCATE/default), `giveItems(List<EnemyDrop>)` @ 0x80023544, `setInventoryFromDisplay` @ 0x800239e0, `equipItem` @ 0x80103a5c; the `EnemyDrop` overflow callbacks from `scriptSetBentDeadAndDropLoot` (05b C12); the battle-end repeat-item return @ `Battle:8386–8393`; the post-battle `PostBattleScreen` → `TooManyItemsScreen` hand-off; the **silent field-give loss** (no field overflow buffer); the Dabas `giveReward` three-way dispatch); the 54-item catalog and effect families (the 3-stage battle pipeline — `scriptUseItem` (table slot 1010) → DEFF registry-by-item-ID + the `throw_item` L4 replacement-script frame pushed onto the acting bent's script stack, entrypoints 32–44, the `stor[28]`=target / `stor[30]`=user / `stor[8]`=colour words; the 24 attack items with the 0/0x8/0x10/0x40 multipliers (application is the retail DEFF); the recovery items' percentage-of-***target***-max model + the `0x800` marker bit; the purifiers' whole-`status_10` clear; the buff items' `0xb4–0xd3` power/temp-word writes with the 3-turns-other / 4-turns-self rule and the `PlayerBattleEntity`-only per-hit words `0xcc–0xd3`; the DEFF-less shields; the random balls over the 25-attack / 7-recovery L4 pools via the battle MT `seed_800fa754`; the repeat/protected items + the `RepeatItemReturnEvent` gate + the ∞ glyph; the utility items — the charm potion's cross-system `encounterAccumulator_800c6ae8` reset, angels' prayer (statMod-0 resurrection), pandemonium (all-allies), smoke ball (no-target escape), sachet, the total-vanishing `specialEffectFlag_14 & 0x80` resist check, the 4 cause-status items (bits 0x4/0x10/0x80/0x8, entrypoint 44)); the equipment system (the `CanEquip` DENY/FORCE/NORMAL two-stage check + the `EquipmentTypes` 23-tag string intersection with the 9 character tag sets; the `onEquip`/`onUnequip` permanent FRACTIONAL (hp/mp multipliers) + UNARY (flat speed) stat mods; the four special equipment classes — Wargod Calling `additionExtra_474.flag_00 |= 0x2`, Ultimate Wargod `|= 0x6` (0x2+0x4 mastery suppression), Destroyer Mace `|= 0x1`, Detonate Arrow's `prepareAttack` (DEFF 0xc1, `item_d4` = detonate_rock, `attack() = DEFF`); the 149-entry catalog with the flag assignments); the UI state machines (the `MenuStack` render-bottom→top / input-top→bottom model + the `WhichMenu` unload contract; the message-box family — MessageBox / Cycle / Choice / Quantity, the latter three new in the Phase-A update; `UseItemScreen` — the 5-row list, the `0x4000` dim gate for `!canBeUsedNow`, the ALL-target apply-to-all loop, the retail geometry @ 0x800fc8c0/0x800fc8dc; `ItemListScreen` — the equipped-item inlining `0x3000|charId` (0x1000 skip-on-write-back + 0x2000 no-discard) + the L4 sort/discard; `EquipmentScreen` — the equip-swap (`equipItem` → `takeEquipment` → `giveEquipment(previous)` → `addHp/addMp(charId, 0)` re-clamp) + the L4 sort (alpha/power/icon) / filter (slot cycle) / unequip; `TooManyItemsScreen` — the swap/discard states, the non-discardable-equipment discard refusal, pushed only from the post-battle report; `GoodsScreen` — the two-pane viewer; the battle `ItemListMenu` — the `CombatItem02` same-item merge @ 0x800f83c8, the `getTargetingInfo` contract (`params[1]` = target or −1, `params[2]` = the item registry ID)); the shop system (the 41 registered retail shops over the 66-entry `SHOP_IDS` (39 named)/var-125 model — `SItem.shopId_8007a3b4`, the submap entry points `SMap:4050/5272`; the `ShopScreen` state machine INIT_0→…→UNLOAD_19 with the per-entry extension selection (`ItemShopExtension`/`EquipmentShopExtension`/`GoodShopExtension` priority-ordered); the buy transactions — item per-unit give + per-unit debit with the `maxQty = min(cap − size, gold/price)` quantity box, equipment 255-cap + character-portrait sub-flow + displaced-item re-equip on overflow + no-gold-spend on failure, goods single; the sell transactions — the equipment/items choice, the non-discardable refusal, the quantity removal with the **sell-N-pays-once** L4 quirk, the `ShopSellPriceEvent` mod-mutable price; the post-battle MERCHANT (action value 5, 05c) → the shop opens in the **submap** via the script-written shop ID, not as a battle screen); persistence (M7-01 cross-ref — the `0x19C` goods bitset / `0x1E8` equipment / `0x2E9` item fields, the no-stacking round-trip, the force-give on load); the RQ7 boundary (inventory is **native + data-driven with a script-thin edge** — like 06's progression: the engine owns every rule (stacking, overflow, price, eligibility, the heal arithmetic, the shop transactions), the data owns every value (the 54/149/37/41 catalogs, the tag sets, the pools), the script is a thin caller (give/take/chest/shop-ID/MERCHANT) and a presenter (the item DEFFs + the `throw_item` replacement scripts); the one genuinely script-owned value on the surface is the **MT seed** via `scriptSetMtSeed`); the RNG/determinism census (three distinct generators — `simpleRand()` 16-bit LCG @ 0x800133ac, `rand()` 15-bit LCG @ `:588` (seeded by `srand` @ 0x8002d270), the static battle MT `seed_800fa754` (seeded by `scriptSetMtSeed` @ 0x800cff24 = `seed ^ 0x75bd924`, consumed by `scriptRand` @ 0x800cee50) — the surface is fully deterministic given the seeds; no item value/price/eligibility depends on RNG); the Phase-A reference update census (baseline advanced `0c08c679b` → `71ce130`: 3 commits, 18 files, +614/−32, 5 new — entirely L4 UI: the cycle/choice/quantity message boxes, the quantity buy/sell/discard flows, the equipment sort/filter/unequip, the ∞ glyph, the `INVENTORY_SIZE_CONFIG` wiring; no `@Method` address or retail-data changes; no prior finding invalidated); I1–I38. Register: Q-89–Q-98 opened (the retail item-use scripts + DEFF-side multiplier application, the `0x800` recovery-marker bit, the instakill equipment flags' consumer, the retail shop prices + the `shopType_00` role, the retail multi-item sell, the unused retail ID slots, the `equipableFlags_03` word, the `GoodsSource` semantics, the `rand()` LCG seed source, the MT seed's retail script caller, the `useInMenu` @ 0x80022d88 address sharing — all retail-byte source-verification pending). Predecessor checkpoint: M9-06 (commit `728241c`) documented characters and progression (C1–C40, Q-82–Q-88); M9-05c (commit `0ced525` final incl. corrections; `80db8c8` initial) documented the addition (timing-overlay) system end-to-end — the moddable battle-action menu registry + the native `scriptSetUpAndHandleCombatMenu` bridge + the L4 registry-ID dispatch; the `AdditionOverlaysEffect44` spinner engine (window computation, borders, input, completion states) and the per-hit script driver; the SP/mastery grants with the equipment-flag asymmetry (Wargod Calling halves addition SP and suppresses the mastery tick — the earlier "odd-slot" reading was wrong); the counterattack system (the `stor[8]` arming channel, the retail tutorial/counter words `var[45][242/243]` — written only by retail scripts, so every L4 `== 2`/`== 3` gate is dead in the reference, the "burn_out" retail item 0xc3 / DEFF 4313 verified from the DRGN1 diff hunks); dragoon transformation/detransformation (party SP pooling, the budget word never initialized from SP in the L4 script — U35); the d-attack and its "daddy" dragoon-addition HUD (4-segment vs Kongol 3-segment, `scriptGetDragoonAdditionHitsCompleted` −1/0/1..4); the complete status-ailment lifecycle (two initialization paths, the tick-word model, the per-ailment tables, un-stun-on-damage, the poison DoT MAX_HP/10); the post-battle action dispatch and `PostBattleScreen`; the settled RQ7 boundary for the whole surface (the script↔native contract enumerated); RNG/timing determinism (both spinners RNG-free); U1–U35. M9-05b (commit `a8f4f22` final incl. corrections; `54b08ac` initial) documented the battle entity/stat models and the exact damage/accuracy/resource pipelines (F1–F20); M9-05a (commit `f9096f4`) documented battle entry/exit, the `battleLoadingStage` machine, timing, the native turn system, the data model, the 362-slot script-function table, stat init/formula overview, audio, the `SEffe` overview, and the 6-event mod surface (B1–B20).
- **Next authorized checkpoint:** **the M9 bounded cross-system reconciliation pass** — a bounded review of the seven M9 documents (01–07) against each other and against M1–M8 for cross-document consistency (finding cross-references, symbol addresses, constants, and the RQ7 boundary statements), fixing inconsistencies in place; the last M9 gate before M9 completion and the M10 approval request. All seven M9 documents (01–07) are delivered.
- **Remaining M9 deliverables:** the bounded cross-system reconciliation pass (see next checkpoint). Per-document requirements (data models with citations, state machines, exact formulas/constants, the script/code boundary, the retail-value source, original-vs-reference tagging) were specified in the plan's M9 verification criteria; the cross-cutting priority was the script-vs-hardcode boundary per gameplay behavior (RQ7).
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
