# P0-05 — Compatibility Policy Architecture (Revised)

Status: **Revised (2026-11-15).** Supersedes the framework half of the 2026-11-14 draft ("P0-03 — Compatibility Decisions"). The ten **CD records themselves are unchanged and still binding** — the observed reference behavior, the options, the proposed defaults, the blocks, and the closure evidence all stand as written. What changes is *where a policy lives, how it is expressed, and how it is tested*. The CD dispositions affected are named in §7.

The I0 record described `psxrt-compat` as "policy framework — all defaults per CD-01..CD-07; no policy activation (I5)". That framework is **retired as a framework**: four of its eight policy modules stay (with their consumer named for the first time), four split, and the collected ledger plus the policy struct go away. This document explains why, and gives the disposition table plus the migration path.

## 1. What was wrong with the I0 framework

The I0 shape was: one module per policy; each policy enum carries one or more `pub const …: PolicyRecord` values (16 of them, in `LEDGER`); four of those policies also appear as fields of a `#[derive(Default)] Copy` struct `CompatPolicy` with a `reference_fidelity()` constructor. There is no lookup function, no `Feature` vocabulary, and no runtime dispatch of any kind — a consumer would have to receive a `CompatPolicy` and match on its fields by hand.

Four defects. Each is checked against the shipped source, and the first draft of this section mis-stated three of them, so the corrections are recorded here rather than quietly applied: the first draft described a `HashMap` lookup keyed by a `PolicyId`, an `Availability::Disabled` variant, and a 35-entry ledger. **None of those exist** — `Availability` has two variants (`Implemented`, `RecordedOnly`), `LEDGER` has 16, and the only map in the crate is a `BTreeMap` inside a test fixture parser. A criticism of a design that does not match the code is worse than no criticism, because it invites a fix to nothing.

1. **A record had no path to a consumer, and the design never named one.** `CompatPolicy` is read by exactly one test (`policy_ledger.rs`). That is not a bug in I0 — no renderer, no SPU model, and no ordering table existed yet to read it. It is a defect in the *shape*: the only way a subsystem could consume a policy was to take a `CompatPolicy` parameter, which puts a `psxrt-compat` dependency underneath `psxrt`, and `planning/01` §5 rule 2 forbids that. So the framework was unreachable *by construction*, not merely unused. What had to be decided before I5 was **who asks, and with what arguments** (ADR-010's backdoors), not how to widen the table.
2. **`Availability` describes the default, so it cannot express "both alternatives exist".** `Implemented` means *the default's mechanism exists and is exercised by tests*; `RecordedOnly` means nothing branches on the entry. There is no state for "the original behaviour is also buildable, and something selects between them" — which is the only state where a compatibility flag does anything. Compounding it, availability hangs off the *record*, while the knob hangs off `CompatPolicy`, and the two surfaces are unrelated by design (16 records, 4 fields — the crate doc says "the difference is the point", and for documentation it is). The consequence is that 12 records state a divergence with no way to select either side, and the test suite verifies *record integrity*, never *policy effect*.
3. **Three modules carried data-format facts next to policy.** `predictor.rs` holds seven XA layout constants (`SOURCE_RATE_HZ`, `HEADER_BYTES`, `CODING_INFO_OFFSET`, `PORTIONS_PER_SECTOR`, `PORTION_BYTES`, `SAMPLES_PER_SECTOR_LANE`, `MAX_BOUNDARY_LEAK_SAMPLES`); `rate_00.rs` holds a flag-word decoder (`translucency`, `page_translucency`, `flag_translucency`, the two shift constants, `NO_TRANSLUCENCY`); `display_clamp.rs` holds four display dimensions and `clamp_draw_width`. A file that is half format header and half policy cannot be tested as either, and it makes the *format* look optional: the XA sector geometry is not a thing anyone toggles. These move (ADR-005's Kind-B rule, §5).
4. **Structurally: one flat table over a 135-row checklist with no idea which of three different things a row was.** Several rows were never policies (§3), several are not compatibility at all (§2), and `battle_speed`-shaped rows would have sat beside GTE-precision-shaped rows if they had been added. The table had no notion of *who sets a knob and when*, which is the only thing that separates a player option from a compatibility override.

## 2. The three-level configuration model (ADR-004)

Three levels. The distinction is **who sets it and when**, not how important it is.

| Level | Name | Set by | Set when | Where defined | May a save file set it? |
| --- | --- | --- | --- | --- | --- |
| **L1** | engine configuration | the player, or a preset | before a campaign, and mid-campaign | `severedchains-sdk` `config` | yes — config is serialized with the save (CL-SAV-12) |
| **L2** | presentation / audio profile | the player, as a **whole profile** | at startup, or by an explicit mid-game profile change that reinitializes the affected subsystems | `severedchains` engine config, consumed by the backend | no |
| **L3** | compatibility override | the developer, or an explicit `psxrt-compat` opt-in | build, or startup before the subsystem initializes | `psxrt-compat` | no |

**The routing rule, in one line: "is this something the player configures?" → L1. "Does it change the rendering model?" → L2. "Is it a compatibility concern?" → L3.**

That is the minimum separation this project needs. It is not three configuration systems. It is **one configuration model with three levels**, addressed by one `Feature` vocabulary with explicit layers (ADR-004) — a `bool` is insufficient because a flag cannot tell the reader whose behavior it is.

Three consequences that are easy to get wrong, and were wrong before:

- **A player-facing option is not a compatibility policy.** `battle_speed` is L1 engine configuration (it is one of the reference's 88 registered config entries, and `Config.load()` clamps it). `renderer_profile` is L2. Neither is a `psxrt-compat` module.
- **L2 is selected as a whole profile**, because the presentation mode cannot be decided per-`TmdFormat` (ADR-007): the two presentation paths consume different geometry and different CLUT read paths, so a mode change forces different fixtures and different verification.
- **L1 lives in the SDK, not in `psxrt-compat`.** The reference's config surface is 88 registered entries — `CoreMod` 80, `LodConfig` 7, `TurnOrderConfigs` 1 (`research/saves/02-config.md` §2.1, item D3, source-verified at reference revision `0c08c679b`). No evidence has been collected that any of them selects a GTE precision, an ordering-table mode, or an SPU model, and no other module defines a config registrar. So the L1/L3 separation here is a **design choice, not a reproduction**. If a later census finds a config entry that does select a model, §3's routing rule decides where it goes — this sentence does not.

## 3. Checklist disposition rule — three kinds of row, plus a fourth (ADR-005)

A checklist row is one of four things. Classify it before deciding where it goes.

### Kind A — a real compatibility policy
Two or more implementations exist (or could), and choosing between them changes observable behavior.

> Examples: same-z order (LIFO/FIFO), GTE precision (float/integer/exact), predictor reset (never/per-file), the sweep envelope (gap/envelope), voice allocation (bound 20 / bound 8), blend selection (exact equations / B+F/4 decomposition).

→ `psxrt-compat`, as a policy module that **selects**, plus an implementation in `psxrt` for each alternative.

### Kind B — a hardware-derived data layout, not a compatibility decision
The thing is a *format*. There is no "modern alternative" that produces the same bytes.

> Examples: the TIM header layout, the 24-byte XA header (X1), the 8-byte vertex stride, the 4-bit/8-bit pixel index read, the SSHD container, the ANIM records.

→ `psxrt` (a representation, e.g. `gpu::clut`, `spu::xa_adpcm`) and/or `psxrt-import` (a decoder). **Do not create a `psxrt-compat` module for a format.** A `compat::xa` module is a category error that survived in the earlier drafts.

### Kind C — a runtime service
A long-lived piece of machinery with state, threads, or a device.

> Examples: `XaPlayer`, `VideoPlayer`, the FMV demux loop, the two audio threads, the frame pacer.

→ the engine layer. **`Any AudioPlayer in psxrt is explicitly out of scope`** (planning/07 §4).

### Kind D — neither
> Example: `files/` and `state_data/` are **runtime storage**. They are not a compatibility policy. The `CompatFlags::files_format` / `.state_format` flags were always wrong; what they were groping at is a *format version* (`VERSION = 5`) with a wipe-below rule, which is a storage concern owned by the cache.

### Two further facts that constrain every dispatch

1. **Dispatch point.** **`Gpu` dispatches translucency at draw-command execution time; `TmdFormat` dispatches at decode time.** These are genuinely different points. One is a single enum carrying a TPage mode **plus** a flag-word mode; the other is a four-flag `TmdFormat`. They must not be forced into a shared `TmdMode`, and they must not be forced into a single "modern" name.
2. **`TmdFormat` is not a compatibility-policy type, even if a future policy references it.** It is a format discriminator in `psxrt-import`, **plus** a decode-time selection; and the modern path must also work when TMD is never involved at all.

## 4. The four-level record template (ADR-005)

Every Kind-A policy carries five things. **The template carries the divergence documentation. It does not carry the implementation.**

| Field | Meaning | Why it is load-bearing |
| --- | --- | --- |
| `evidence` | the checklist row id + the confidence, as a **dual tag** — reference confidence **and** original confidence | the two are different questions; "the reference does X" does not imply "the original does X" |
| `layer` | `Psx` / `LodSpecific` / `ReferenceSpecific` | names whose behavior this is; the compatibility-exception list keys off this |
| `default` | which alternative ships when nothing is opted in | must be a real alternative that works, not a `None` sentinel |
| `knob` | the selector type and its alternatives | every alternative is unit-testable **without a GPU or audio device** |
| `divergence` | what is expected to differ, at what magnitude, and **the register id that would close it** | without the pointer, the divergence is folklore |

**Rejection criterion, stated so it cannot drift:** *a compatibility table that only records "Java did it this way" does not count as a compatibility layer.* A record whose `layer` is empty is not finished. A record whose `evidence` is `SourceVerified` on both halves is not finished — that tag says nothing.

**Retention.** Of the 16 records in the I0 `LEDGER`, the **information** is retained and the **table** is not. Evidence, layer, default, divergence note, and register pointer each move to a home appropriate to their kind (a policy module, a backend profile, a format module's doc header, a traceability row). `research/cross/02` remains the source of truth for the 135 rows; this document records only what each kind becomes.

## 5. Migration — module by module

Current `psxrt-compat` → destination. This is the disposition of **all eight policy modules plus the framework in `lib.rs` that collects them**.

| Current | What it actually contains | Destination | Why |
| --- | --- | --- | --- |
| `same_z.rs` | `SameZOrder` (+ `draw_position`, which computes an insertion index) + the G6 record | **stays**, as a policy selector consumed by `psxrt::gpu`'s ordering-table replay | Kind A. The two orderings are two real implementations. `draw_position` moves with the selector — it *is* the selection expressed as arithmetic |
| `blend_select.rs` | two `PolicyRecord` consts only; the equation primitives already live in `psxrt::blend` | **stays** as the policy record + the selection; the equations stay where they are | Kind A over a Kind B. Recording a layout is not a module's job |
| `display_clamp.rs` | `clamp_draw_width`, four dimension constants, + three records (draw width, center origin, display latency) | **split**: the clamp and the origin are Kind B (a layout fact) → `psxrt::gpu`; the *profile* that turns clamping on is Kind A → a `PresentationProfile` discriminant at L2 | the function is a layout fact with one correct answer |
| `rate_00.rs` | `translucency` / `page_translucency` / `flag_translucency`, `NO_TRANSLUCENCY`, two shift constants + the G11 record | **split**: the decoding → `psxrt::gpu` (it decodes a flag word, it does not choose a policy); the "no translucency is real translucency" divergence → a `DIVERGENCE.md` entry | Kind B with a Kind-A tail |
| `gte_precision.rs` | `GtePrecision` (+ two scale constants) | **stays**, consumed by `psxrt::gte` | Kind A. The integer camera program is mandatory (T4) and has no clean modern counterpart |
| `predictor.rs` | `PredictorReset`, `MAX_BOUNDARY_LEAK_SAMPLES`, **and six XA format constants** (`SOURCE_RATE_HZ`, `HEADER_BYTES`, `CODING_INFO_OFFSET`, `PORTIONS_PER_SECTOR`, `PORTION_BYTES`, `SAMPLES_PER_SECTOR_LANE`) | **split**: the format constants → `psxrt::spu::xa_adpcm` (Kind B); `PredictorReset` → the policy; the leak bound → the divergence note | a policy module must not double as a format header |
| `sweep.rs` | `SweepEnvelope` + the S7 record | **stays**, consumed by `psxrt::spu` | Kind A, and the one CD whose default deliberately diverges from the reference |
| `voice_alloc.rs` | `VoiceCapacity` (a runtime data structure with methods) + `Phase` + the allocation chain | **split**: `VoiceCapacity` and `Phase` → `psxrt::spu` (Kind C); the allocation *policy* stays in `psxrt-compat` | it is a structure, not a policy |
| `lib.rs` | `Confidence`, `Layer`, `Availability`, `PolicyRecord`, `CompatPolicy`, `LEDGER` | a `Feature` vocabulary is **introduced** by `planning/01` §3 — it is not carried over from I0, which has no flag type at all; `CompatPolicy` retired (see §6); `LEDGER` decomposed (see §4) | a runtime crate must not depend on a compatibility crate, and a four-field struct cannot carry 16 records |

**The replacement model:** `severedchains` holds **one `EngineConfig`** with **three layers**; backend families are distinguished by **profile enums**, not by feature flags; and **individual algorithm switches are handled inside their owning module**. The earlier "single ledger with string-`or`-enum-style id keys" and the earlier "a feature flag per module" are both retired.

## 6. `CompatPolicy`, `PolicyId`, and the ledger — what replaces them

- **`CompatPolicy` (the four-field `Copy` struct + `reference_fidelity()`): retired as a *shape*, kept as an idea.** Its idea — that the shipped default set and the reference-faithful set differ in exactly one field, and that a test holds that statement true — is the best thing in the I0 crate and moves to the per-subsystem matrix fixtures. What is retired is expressing the whole policy surface as one struct a consumer must receive wholesale.
- **Replace with a `CompatFlags` bitset** of **semantically named flags**, resolved at startup, with no per-read hash lookup:

  ```
  DISABLE_ORDERING_TABLE   · LEGACY_3D_QUEUING · GTE_FIXED_POINT
  SWEEP_ENVELOPE · PREDICTOR_PER_FILE · BLEND_DECOMPOSED · …
  ```

  A flag that has no implementation behind it must not appear.
- **`Feature` is a vocabulary of three parts** — the layer it belongs to, its default, and whether it may be user-configured. **It is not a universal string-keyed flag bag**, and it does not need `Default` derived for convenience's sake.
- **`LEDGER` decomposition:** the 16 records regroup by *consumer*, not by directory: **7** rendering/presentation (same-z, the three display-clamp records, rate-00, the two blend records), **2** geometry transform (the two GTE records), **7** audio (the two sweep records, voice allocation, the four predictor records). Each group's records move to the module that will consume them or to `DIVERGENCE.md`. **These are record groups, not modules.** No `crates/psxrt/src/display/` directory is created; the grouping exists so that redistribution can be audited — every record must land somewhere named — not so that directories can be made.
- **`REMAP.md` is not one global file.** See planning/01 §6.5.

## 7. Where the ten CDs land

| CD | Row | Kind | Level | Outcome of the revision |
| --- | --- | --- | --- | --- |
| CD-01 | G6 same-z | A | **L2 profile** (the presentation mode selects it) + **L3 override** | the selector stays in `psxrt-compat`; the consumer is `psxrt::gpu`'s OT replay, and a modern-path equivalent must also exist, or the flag is a flag and not a model |
| CD-02 | R4/Q-70 RNG seed | A | **L1 + dev** | **no change to the disposition.** The seed source stays a compatibility decision, *not* a released `CompatFlags` entry; the dev `--seed` override stays behind the dev feature |
| CD-03 | T2 GTE precision | A | **L3**, with a **mandatory integer program** | the integer submap camera program is **built in `psxrt::gte`** because it has no clean modern equivalent. `psxrt-compat` records the *option* — the module does not own the algorithm. The "modern default" principle **does not override** the T4 obligation |
| CD-04 | G4 the 384→368 clamp | **B, with an A tail** | L2 profile | the clamp itself moves to `psxrt::gpu`; the profile that enables it is L2. The two-distinct-368s note is preserved verbatim |
| CD-05 | G11 rate-00 | **B, with an A tail** | L3 | rate-00 → `B+F/2` stays **a compatibility escape hatch**: no clean modern default exists, so the mapping is implemented in the runtime and recorded in `DIVERGENCE.md`. The flag name should reflect that it selects a *decode model*, not "emulation" |
| CD-06 | S7 sweep | A | L3 | unchanged: envelope implemented, default **on**, the reference gap not preserved |
| CD-07 | X6 predictor | A + format constants | L3 | `PredictorReset` stays; the six format constants move to `psxrt::spu::xa_adpcm` |
| CD-08 | C-01 dual opcode tables | not a policy — a **documentation obligation** | — | moves to `severedchains-sdk` `fate::binary`; the discrepancy stays preserved as data, with the per-opcode retail verification left open and asset-gated |
| CD-09 | Q-74 SFX tick quantization | A + Kind C | L3 policy **over** a runtime mechanism | the 8.0 ms SFX tick and the sample-accurate BGM are **runtime mechanisms**; `psxrt-compat` decides *which clock a given reference layer uses* |
| CD-10 | Q-80 memcard shape | **B** (a file layout) | — | not a policy. It is a Kind-B format with an unresolved provenance question |

**Six items are deliberately *not* CD rows.** The modern presentation and audio defaults listed at `planning/04` I5b are numbered **MP-01…MP-06** (scene-queue ordering, the three logical layers, the HDR chain, explicit alpha and blend, the mask surface, the audio bus). They are model defaults of the modern path, not compatibility decisions, and giving them CD numbers would imply each one needs an original-behavior citation before it can be built. It does not. Where one of them has to *coexist* with a compatibility escape hatch (MP-04's explicit blend alongside the rate-00 mapping, MP-06's bus alongside the sweep envelope), the coexistence is stated in the I5b gate record.

## 8. Test backdoors (ADR-010)

**Compatibility policy modules remain testable without running the game, and without a GPU or audio device.** A module is still allowed to expose a way to apply the override directly to a context or a fixture.

Two backdoors, both **non-vtable**, both **test-only or tooling-only**:

1. a **direct predicate** taking the plain inputs the decision needs (no device, no engine context);
2. a **pure function** that applies the policy to a buffer or a fixture — e.g. the module transforms a `u32` buffer, or runs over a synthetic `files/` tree.

Both are permitted **even when they do not match the production dispatch path**, because their purpose is verification. They stay inside the module and are **documented as test-only**. A test backdoor is not a runtime service, must not become a hidden runtime plugin interface, and must not be a public API "just in case". The `PolicyEnv` concept from the earlier draft is allowed to return, **restricted to the tooling and test surfaces** — it is not a runtime dependency.

This is the explicit answer to "the compatibility layer shall be a non-public compatibility layer". Non-public means not *published to players*, not *unreachable from tests*.

## 9. Migration path

1. **I1a** — create `psxrt::compat` holding the `Feature` vocabulary, the `GameConfig` shape, and the `CompatFlags` bitset. Repoint `psxrt-compat`'s records onto it. **`psxrt-compat` keeps existing as a crate** and keeps depending on `psxrt`. `policy_ledger.rs` is rewritten to assert the new shape (the three-level routing, and that no L1 entry can address an L3 id).
2. **I1b** — move the Kind-B items: `gpu::clut` and `spu::xa_adpcm` receive the relocated constants and layout functions. `psxrt-import` begins consuming them (the first legitimate import→runtime edge).
3. **I3** — `gpu` (the command vocabulary and the VRAM store) and `audio::{device,transport}` land with the modern default already in place, so that the I5a policy activation is an *addition*, never a prerequisite for a working game.
4. **I5a** — the Kind-A policies activate against their model, each with its matrix fixture (planning/05 §5.3).

**What is not done:** no register item is closed by this revision. The ten CDs keep their research statuses (`Open`, `Conflict`) and their closure evidence verbatim; the two dual-layer rows and CD-08's conflict status are preserved rather than resolved. Blast radius is unchanged — each CD blocks only its subsystem's *done* declaration, and the gate structure means a policy flip is a flip, not a rework.
