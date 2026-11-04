# M3 Independent xhigh Audit

**Status: COMPLETE (2026-11-04).** This document was first committed as a
checkpoint (`a0b3733`) with findings established but unremediated; the
remediation pass then applied every §3 worklist item in place across
`research/scripting/01–06`, synchronized the governance files (register,
index, plan, `AGENTS.md`), and finalized this report (§7). Per the audit
mandate, the exhaustive 592-function behavioral analysis remains **deferred
to the M9 subsystem milestones** (a representative sample was behaviorally
verified, §4.1) — an intentional scope note, not a gap.

**Final gate: PASS** (see §7.5).

- Date: 2026-11-04
- Audited baseline: `DaCodeChick/SeveredChains` @
  `197b4305fe15205363e3ebd4bc3ba0026cf7bdb4` ("Complete M3 Fate scripting
  research"), working tree clean at audit start.
- Audit posture: M3 conclusions treated as claims to be tested against the
  pinned reference sources; no M3 work redone wholesale; no M4, no Rust, no
  engine modification, no architectural decisions.

## 1. Method and evidence protocol

Evidence classes used in this audit:

1. **source-verified (re-verified)** — re-read directly from the pinned
   reference sources during this audit (path:line cited per finding).
2. **experimentally verified** — the Script-Recompiler toolchain was built
   **offline** (`./gradlew jar --offline`, JDK 25, dependency cache only; no
   network fetches) and executed in `/tmp/fate_exp` against synthetic scripts
   and hand-crafted binaries. Shell metadata was served from a **local** copy
   of `patches/meta` placed at `./cache/local` (the `loadMeta("<version>")`
   cache path performs no HTTP when the version directory exists — verified
   in `MetaManager.loadMeta`). Results label **reference-toolchain behavior,
   not retail-asset behavior**: no retail script bytes or disc images exist,
   so **no asset-validated claims are made anywhere in this audit**.
3. **M3-inherited** — verified during M3 and not re-examined this session
   (flagged where it matters).
4. **Unresolved** — recorded for the register; no claim made.

Repositories confirmed unchanged at audit start and end of each session
segment (all four clean at pinned revisions):

| repo | revision |
|---|---|
| Severed-Chains | `0c08c679b6b54e167b00851f1f1458d4dea3eade` |
| Script-Recompiler | `3433e6f74bb6d562aeff0490d11e8937da437511` |
| Mod-Loader | `c692134907a070a8b2c7db7b2b779010bc16deb9` |
| Dig-Dabas | `4c1fbfebced123d43282b6e3b08286336b6b05f0` |

(The offline build wrote only gitignored `build/` artifacts inside
Script-Recompiler; `git status` remains clean there.)

## 2. Findings register

Severity: **H**igh (round-trip/scope claims a Rust port or patch author would
act on wrongly), **M**edium (mechanism/coverage claims materially inaccurate),
**L**ow (wording/precision), **I**nfo.
Status: all findings **pending remediation** in the M3 documents (worklist in
§3); none applied in this checkpoint.

### High

- **H-1 — 0x24–0x27 params are one-way, not byte-stable.**
  Claim (doc 02 §8; doc 03 §10 item 3): everything outside the enumerated
  hazard set "(0x00–0x0b, 0x0e–0x10, 0x14, 0x20–0x27) has a matching
  tokenizer pattern and is byte-stable".
  Evidence: `Translator.buildParam` emits the **absolute-address** forms
  `stor[inl[0xADDR]]`, `var[inl[0xADDR]]`, `reg[inl[0xADDR]]`,
  `inl[0xADDR][inl[0xADDR]]` for STOR_INL/GAMEVAR_INL/REG_INL/INLINE_INL
  (`Script-Recompiler/.../Translator.java`, `buildParam` no-label switch,
  STOR_INL/GAMEVAR_INL/REG_INL/INLINE_INL cases); the tokenizer's
  `INL_SUBPATTERN` (`Tokenizer.java`) is `inl\[\s*:(\w+)\s*?]` — label form
  **only**. Regex tested mechanically: `stor[inl[0x234]]`,
  `stor[inl[0x10], inl[0x20]]`, `var[inl[0x234]]`,
  `inl[0x10][inl[0x20]]` all **fail** to match; `stor[inl[:L]]`-style forms
  match. **Experimentally confirmed**: a hand-crafted binary with a 0x24
  param disassembles to `mov stor[inl[0x10]], 0x7`; re-assembly fails with
  `RuntimeException: Unknown param stor[inl[0x10]]`. The doc 02 §4
  text-form column for 0x24–0x27 documents only the tokenizer *input*
  grammar (label forms), conflating it with what the translator *emits*.
  Mitigating observation: none of the 328 shipped `.diff` files contains any
  `inl[0x…]`/`inl[:…]` 0x24–0x27 spelling or `cmp &&`/`cmp ||` (mechanical
  grep), so the in-game patch pipeline has never hit this wall — but any
  future patch of a script containing these types cannot assemble.
  Classification: **incorrect** (doc 02 §8 stability claim), **incomplete**
  (doc 02 §4 text-form column, doc 03 §10).

- **H-2 — CMP header 8/9 (`&&`/`||`) is a one-way round-trip hazard,
  undocumented; doc 01 states the opposite.**
  Claim (doc 01 §3.3): "`&&` `||` map to 8–9. The last two are only legal
  for `CMP` (the translator renders header 8/9 on the other ops as
  `Unknown CMP operator`)".
  Evidence: `Translator.buildHeaderParam` has cases 0–7 for **all five**
  compare ops and `default -> "Unknown CMP operator " + op.headerParam` —
  including `CMP` itself. **Experimentally confirmed**: a hand-crafted
  `cmp` with header 8 disassembles to
  `cmp Unknown CMP operator 8, 0x1, 0x2, 0x203`; re-assembly fails with
  `RuntimeException: Unknown param Unknown CMP operator 8`. The hazard is
  reachable by design: high-level Fate lowers `&&`/`||` (and `&&=`/`||=`) to
  `CMP` header 8/9 (`FateCompilerVisitor` ANDC/ORC cases,
  `fate.addOp(new FateOp(OpType.CMP, new FateImmediate("&&"), a, b, out))`
  ~line 843; augmented-assignment cases ~line 229). So any retail script
  using `&&`/`||` cannot round-trip through decompile→recompile. The
  tokenizer *does* accept `&&`/`||` (hand-written assembly works), so this
  is purely a translator-emission gap. Also missing from the doc 02 §8
  hazard summary and doc 03 §10 conditions.
  Classification: **incorrect** (doc 01 §3.3), **incomplete** (doc 02 §8,
  doc 03 §10).

### Medium

- **M-1 — Doc 02 §6.0 entry counts and gap list.**
  Claim: "Runtime table: (57 entries…)", "Toolchain table: (58 entries =
  runtime's 57 plus NOOP(100))", gap list "23, 31, 37–39, 45–47, 53–55,
  57–63, 69–71, 75–79, 81, 84–85, 90–95, 100 (runtime only), >103".
  Evidence (mechanical parse of both `OpType.java` enums): runtime = **67**
  entries, toolchain = **68**; all 67 shared opcodes have identical
  mnemonics (re-confirmed during remediation — the checkpoint's "(66)" was a
  transcription slip);
  toolchain-only = {100}; **both tables are also missing opcode 89** (the
  gap list omits it in §6.0 and in the §6.1 closing note). `byOpcode`
  behavior re-verified: runtime throws
  `RuntimeException("Invalid opcode %#x")` (`OpType.java:76-84`), toolchain
  returns null (`OpType.java:85-93`).
  Classification: **incorrect** (counts), **incomplete** (op 89).

- **M-2 — Doc 05 §2 auto-allocation is round-robin, not lowest-first.**
  Claim: "the per-frame reset makes every allocation take the **lowest free
  index ≥ 9** — deterministic, low-index-first".
  Evidence: `ScriptManager.findFreeScriptState`
  (`ScriptManager.java:246-277`) first does `upperBound++` (wrapping ≥108 →
  9), then scans `upperBound..107`, then `9..upperBound−1`. With the
  per-tick reset (`upperBound = 9`, line 213), the **first allocation of a
  frame starts scanning at slot 10**; slot 9 is returned only when 10–107
  are all occupied. Successive allocations within a frame continue
  round-robin from the last allocation point. The Q-42 core conclusion
  stands — slots 0–8 are hard-reserved (scan space is 9–107) — but the
  mechanism sentence (doc 05 §2, doc 05 §14, and the register Q-42 entry)
  must read "round-robin over 9–107 from the last allocation point; after
  the per-tick reset the first frame allocation starts at 10, so slot 9 is
  the fallback when 10–107 are full".
  Classification: **incorrect** (mechanism), core reservation claim
  confirmed.

- **M-3 — Doc 04 §3 Submap slot ranges are wrong.**
  Claim: "SMap: exploration slots including 8–20, 32–48, 208–286, …".
  Evidence (mechanical extraction of `SMap.getScriptFunctions()`): the 144
  SMap slots occupy ranges **12–15, 18–19, 96–127, 198, 256–271, 288–319,
  672–703, 768–791, 940**. In particular **32–48 contains zero SMap slots**
  (32–48 are Battle camera slots). The Battle side of the same sentence
  ("32–50 and 128–178") is verified complete. `descriptions.csv` rows
  32–41 = Battle camera slots verified (incl. `FUN_800db688`/`FUN_800db79c`
  at rows 37/38).
  Classification: **incorrect**.

- **M-4 — Doc 05 §7.1: script tickers are gated by `paused || stopped`.**
  Claim (§7.1 pseudocode): "executeScriptTickers(ticked) # ALWAYS: per-state
  tick() then tempTick()".
  Evidence: `ScriptManager.executeScriptTickers` (`ScriptManager.java:365`)
  begins `if(this.paused || this.stopped) return;`. Renderers are gated by
  `stopped` only (doc correct there); doc 05 §7.3's "when not paused/stopped"
  is correct — §7.1's "ALWAYS" comment contradicts it. Also verified:
  tickers run as two separate loops (all `tick()` then all `tempTick()`)
  under the `hasExecuted() || isScriptFrame` gate with per-state
  dump-and-throw (GH#1530 comment present).
  Classification: **incorrect** (§7.1).

### Low

- **L-1 — Doc 02 §9.6 NEXT_IMMEDIATE descriptor bytes.** Doc shows
  `01 00 00 00`; the actual descriptor word is `0x01000000` → LE
  `00 00 00 01` (`Tokenizer.packParam(ParameterType.NEXT_IMMEDIATE)` =
  `type.opcode << 24`, all cmd bytes 0; runtime `parseParam` reads
  `type = op >>> 24`). **Experimentally verified** (assembled bytes at the
  descriptor position: `00 00 00 01`; value word `05 00 00 01` as
  documented). §4.2's "descriptor `0x01000000`" is correct.
- **L-2 — Doc 02 §9.7 JMP_CMP_0 param order.** Doc lists param 0 = INLINE
  (addr), param 1 = STORAGE; actual order is **param 0 = STORAGE (right),
  param 1 = INLINE (addr)** — toolchain `OpParam.in("right"),
  OpParam.in("addr")` and runtime `scriptConditionalJump0`
  (`ScriptState.java:1294-1301`: `scriptCompare(ZERO, params_20[0], header)`
  then `params_20[1].jump(...)`). **Experimentally verified** (assembled
  bytes: `05 00 00 02` then `fe ff 00 09` for a −2-word relative). The doc's
  inline word `10 00 00 09` is a valid encoding of rel = +0x10 words; only
  the ordering (and the implied layout) is wrong.
- **L-3 — Doc 02 §9.8 string char codes off by one.** Doc: "H = 0x27, I =
  0x28". Actual (`LodString` charset, verified both directions):
  **H = 0x26, I = 0x27** (0x28 = J). Corrected bytes (experimentally
  verified): `data str[HI]` → `26 00 27 00` (word `0x00270026`) +
  `ff a0 00 00` (word `0x0000a0ff`); `data str[<line>HI]` → `ff a1 26 00`
  (word `0x0026a1ff` — code 0xa1ff in the low 16, H in the high 16) +
  `27 00 ff a0` (word `0xa0ff0027`). (The checkpoint's parenthetical for the
  `<line>` word was a byte-order transcription slip — the raw assembled bytes
  and `LodString.fromString` confirm `ff a1 26 00` / `0x0026a1ff`; the code
  list is the source order [0xa1ff, H, I, 0xa0ff], packed two codes per
  word.) Doc 01 §5 charset table
  (0x26=H, 0x27=I, 0x28=J) is correct; its control table re-verified in full
  (line 0xa1ff, button 0xa200, multibox 0xa3ff, speed 0xa500|n, pause
  0xa600|n, colour 0xa700|n, var 0xa800|n, xoffset 0xad00|n, yoffset
  0xae00|n, sauto 0xb000|n, element 0xb100|n, arrow 0xb200|n, action
  0xc000|len; terminator 0xa0ff, `<noterm>` suppression at
  `LodString.fromString`).
- **L-4 — Doc 02 §4.2/§9.6 "bits 16–23" wording.** The tokenizer rule is
  `(value & 0xff00_0000) == 0` — bits **24–31** (the high byte). §9.6's
  "0x01000005 has bits 16–23 set" is wrong (bit 24 is set). Replace with
  "high byte (bits 24–31)".
- **L-5 — Doc 02 §6.2 "raw bits".** Float operands of ops 6/7/8/9 compare
  via `.get()` = the storage **int mirror** (`Math.round` of the float for
  float slots, `ScriptState.setStor` 219-231), not raw float bits. Reword
  to "integer mirror".
- **L-6 — Doc 04 §6 failure row 1.** If the local `meta` version dir is
  missing, `loadMeta("meta")` proceeds to `getVersions()` with a **null base
  URI** (ScriptManager passes `null`, verified
  `ScriptManager.meta()`) → `NullPointerException`, which is *not* caught by
  `meta()`'s `IOException | CsvException | NoSuchVersionException` catch.
  Missing CSVs inside an existing dir do throw `NoSuchFileException`
  (IOException). Split the row.
- **L-7 — Doc 03 §9 usage-footer bullet is garbled.** Actual: the no-args
  footer (`Shell.java:50`) lists `[v]ersions, [d]ecompile, [c]ompile,
  [g]enpatch, [a]pplypatch, [u]ndopatch, [s]trip` — where "applypatch" is
  not a real command (aliases are `p`/`patch`); the switch-default footer
  (`Shell.java:267`) lists `[v]ersions, [d]ecompile, [c]ompile,
  [a]ssemble, [g]enpatch, [p]atch, [u]ndopatch` — missing `s`/`strip`.
  Replace the vague sentence with these two exact observations. (Doc 03 §9
  command table itself is verified correct, including that standalone
  `p`/`patch` applies a diff text→text only, without reassembly.)
- **L-8 — Doc 05 §2/§13: `Battle.java:4490` is the non-player BENT branch.**
  It uses `SCRIPTS.findFreeScriptState()` (auto-allocated), not an
  entity-data slot; only `Battle.java:2167` (player BENT) uses the
  entity-data `scriptSlot`. Reword "player BENT script slots: 2167 / 4490".
- **L-9 — Doc 05 §12 "per-op @Method ROM addresses".** Exactly **three**
  op bodies lack a `@Method` anchor: `scriptPush` (op 101,
  `ScriptState.java:1430`), `scriptPop` (op 102, :1435), `scriptCmp` (op
  103, :1440). All other dispatched op bodies carry anchors. State this
  precisely (also note `scriptCompare` @ 0x8001670c is the WAIT_CMP body,
  distinct from unanchored `scriptCmp` which is the CMP 103 body:
  `p2 = compare ? 1 : 0` via `Param.set(boolean)` → `set(val ? 1 : 0)`,
  always CONTINUE).
- **L-10 — Doc 01 §2.4 token names.** The logical operators lower via the
  **ANDC** (`&&`) / **ORC** (`||`) grammar tokens (ANDA/XORA are the
  bitwise `&`/`^` cases and lower to AND/XOR ops). Correct the
  "(ANDA/ORA/ANDC/ORC cases)" parenthetical.
- **L-11 — Doc 06 missing catalog facts (two).**
  (a) 592 slots resolve to **578 unique implementations**; 4 alias groups
  register one method under multiple indices:
  `Scus94491BpeSegment::scriptRewindAndPause2` @ 135, 137, 162, 163, 321,
  324; `Battle::scriptCheckPhysicalHit` @ 480, 484, 485, 486;
  `Battle::scriptAttachShadowToBottomOfBentModel` @ 515, 520;
  `Battle::FUN_800d3090` @ 736, 740, 741, 742, 743, 748. (census
  reproduced this audit).
  (b) **44 implemented methods have no `params.csv` row** (the catalog's
  params column for them is empty; params.csv has 1538 rows / 534 unique
  method names). Also verified: params.csv branch column contains only
  `none` (1537) and `fork_jump` (1) — the doc 06 "local branch metadata uses
  only fork_jump" claim is confirmed.
- **L-12 — Doc 03 §3.2 item 5 (string recovery) needs a precondition.**
  The disassembler only materializes `LodString` entries for word runs that
  were *identified* (CALL params typed `string` in metadata, pointer-table
  destinations, inline string destinations — `buildStrings`/`StringInfo`
  mechanism); unreferenced char data stays `data` words. **Experimentally
  confirmed**: two `data str[…]` lines with nothing pointing at them
  disassemble to plain `data 0x…` lines (byte-stable, but no string
  rendering).
- **L-13 — Doc 02 §4 width column for 0x15/0x16.** The runtime consumes
  **two** words for 0x15/0x16/0x17 (`parseParam` cases 636-652 all do
  `commandOffset_0c++` after reading the descriptor, base = the second
  word) — the doc's "2" matches the runtime — but the toolchain
  `ParameterType` declares widths `_15(0x15, 1)`, `_16(0x16, 1)`,
  `INLINE_TABLE_4(0x17, 2)`; i.e. the disassembler would consume only one
  word for 0x15/0x16 (the base word would become an orphan entry).
  Unobservable in practice because the translator throws on 0x15/0x16
  first — annotate the column rather than change the value.
- **L-14 — Doc 02 §8: 0x21 ID round-trip needs a colon caveat.** The
  tokenizer ID pattern is `.*?:.*?` (documented in §4's 0x21 row); IDs
  without a colon cannot be re-parsed → one-way for those. One-line note in
  the §8 summary.

### Informational

- **I-1 — Disassembler symbolic-tracking limit.** `Disassembler.parseParamValue`
  has the OTHER_* storage cases **commented out (TODO)** — the disassembler
  returns unresolved for 0x03/0x04/0x0d params (register model never tracks
  cross-state storage). Not a doc error (docs don't claim otherwise) but
  relevant to round-trip reasoning.
- **I-2 — Translator 0x0d format quirk.** The `OTHER_STORAGE` emission
  computes its `B + C` argument through a Java-precedence garble
  (`b >> 8 & 0xff + b >> 16 & 0xff`); the emitted text is one-way anyway
  (no matching tokenizer pattern), so no behavioral consequence.
- **I-3 — safeDiv javadoc claim.** Toolchain `MathHelper.safeDiv` javadoc
  says "Handles /0 like the PS1" — a toolchain-side assertion of original
  semantics; no PSX evidence in the repos. The runtime/toolchain div-by-0
  divergence (doc 02 op 33 row) is re-verified precisely: runtime
  `scriptDivide` (1100-1114) → `p1 = (p1 ≥ 0 ? −1 : 1)` (0 → −1);
  `safeDiv` → `Integer.compare(0, num)` (0 → 0).
- **I-4 — Commented-out Battle assignments.** Verified at
  `Battle.java:880/988/1002/1099`: `// functions[445] = Temp::FUN_800ca734;`,
  `// functions[603] = Temp::FUN_800ca648;`,
  `// functions[617] = Temp::FUN_800caae4;`,
  `// functions[842] = Temp::FUN_800c6968;` — explaining why a naive
  regex census counts 366 Battle assignments vs 362 active.
- **I-5 — `ScriptPatcher` include-CRC regex tolerates patch-added lines:**
  `^\s*\+?#?include\s+(.+?)\s*;?\s*$` (matches `+include …` from diff-added
  lines and `#include …`). Verified; not doc-specific.
- **I-6 — Disassembler out-of-range branch targets are probed gracefully**
  (observed in experiments: "Probing branch 0x48" past EOF, no crash).

## 3. Remediation worklist — ALL ITEMS APPLIED (2026-11-04)

Each row below was applied in place during the remediation pass; the M3
documents now carry the corrected text (most with an inline "corrected by
the M3 audit" annotation). Dispositions: §7.1.

| doc | change | finding |
|---|---|---|
| 02 §6.0 / §6.1 | entry counts 57→67, 58→68; add 89 to both gap lists; note op 89 is a gap in both tables | M-1 |
| 02 §4 | 0x24–0x27 text-form column: distinguish tokenizer input (label form) from translator emission (absolute form); 0x15/0x16 width annotation | H-1, L-13 |
| 02 §4.2 + §9.6 | "bits 16–23" → "high byte (bits 24–31)" | L-4 |
| 02 §6.2 | "raw bits" → "integer mirror" | L-5 |
| 02 §8 | add 0x24–0x27 and CMP header 8/9 to the hazard summary; 0x21 colon caveat | H-1, H-2, L-14 |
| 02 §9.6 | descriptor bytes `00 00 00 01` | L-1 |
| 02 §9.7 | param order: STORAGE then INLINE | L-2 |
| 02 §9.8 | H=0x26/I=0x27 + corrected bytes for both strings | L-3 |
| 01 §3.3 | translator renders "Unknown CMP operator" for **all** compare ops including CMP; `cmp &&/||` is a one-way decompile hazard | H-2 |
| 01 §2.4 | ANDC/ORC token names | L-10 |
| 03 §3.2 item 5 | string-recovery precondition | L-12 |
| 03 §8 / §10 | extend one-way set (0x24–0x27, CMP header 8/9) | H-1, H-2 |
| 03 §9 | precise footer wording | L-7 |
| 04 §3 | SMap ranges: 12–15, 18–19, 96–127, 198, 256–271, 288–319, 672–703, 768–791, 940 | M-3 |
| 04 §6 | split missing-dir (NPE, uncaught) vs missing-CSV (IOException) rows | L-6 |
| 05 §2 / §14 | round-robin allocation correction (slot 10 start after reset; slot 9 fallback) | M-2 |
| 05 §7.1 | tickers gated by `paused \|\| stopped` (not "ALWAYS") | M-4 |
| 05 §2 / §13 | Battle.java:4490 non-player/auto-allocated wording | L-8 |
| 05 §12 | 3 unanchored op bodies (101/102/103) stated precisely | L-9 |
| 06 | alias groups (578 unique) + 44 methods without params rows + behavioral-verification status note | L-11 |
| register 04 | Q-18 hazard-set extension; Q-60 add 0x24–0x27; Q-42 mechanism wording; Q-61 note op 89 | M-2, H-1, H-2, M-1 |
| README | M3 findings summary items 3 and 7 (round-trip hazards; 578 unique implementations) | H-1, H-2, L-11 |
| AGENTS / plan | audit doc reference once remediated | — |

## 4. Verification results by priority area

### 4.1 Priority A — native-function catalog (census reproduced)

Reproduced from pinned sources with comment-aware extraction of
`functions[N] = …`:

- **592 implemented slots** = 86 base (`Scus94491BpeSegment_8004`) + 362
  Battle + 144 Submap; **432 unimplemented** of the 1024 described;
  descriptions.csv 1024 rows; params.csv 1538 rows / 534 unique method names.
- **592/592 slot names match descriptions.csv exactly** (zero mismatches) —
  M3 claim confirmed.
- **559/592 carry `@Method` ROM addresses; 33 unanchored** — the 33-index
  list matches doc 06 exactly using the documented 8-line annotation window
  (a 15-line window produces a false positive at index 1021 — method
  note).
- **New (L-11):** 592 slots = 578 unique implementations; 4 alias groups
  (see finding).
- **0** unimplemented indices have params.csv rows; **44** implemented
  methods have no params.csv row.
- Commented-out Battle assignments 445/603/617/842 verified (I-4).
- params.csv branch values: `none` × 1537, `fork_jump` × 1 — doc 06 claim
  confirmed.

**Spot-verification (names + anchors + bodies), this audit:**

| slot | documented | verified at |
|---|---|---|
| 0 | Scus94491BpeSegment::scriptSetIndicatorsDisabled @ 0x80017354 | Scus94491BpeSegment.java:269 ✓ |
| 7 | DrgnFiles::scriptWaitForFilesToLoad @ 0x80017564 | DrgnFiles.java:130 ✓ (body: loading>0 → PAUSE_AND_REWIND else PAUSE) |
| 192 | Text::scriptGetFreeTextboxIndex @ 0x80029b68 | Text.java:1943 ✓ (body: scans 8 textbox pairs, out = i or −1) |
| 236 | Audio::scriptPlaySound @ 0x8001ab34 | Audio.java:333 ✓ (body: sound-file switch 0/4/8/9/10; delays × tickMultiplier) |
| 705 | Audio::scriptSssqFadeOut @ 0x8001b310 | Audio.java:533 ✓ |
| 865 | Scus94491BpeSegment::scriptTakeItem @ 0x80024590 | Scus94491BpeSegment.java:479 ✓ (body: equipment vs item split at 0xc0; out 0 = success, 0xff = failure) |
| 891 | SItem::scriptInputActionPressed (no anchor) | SItem.java:248 ✓ (body: registry-ID input action → PLATFORM.isActionPressed) |
| 901 | SItem::scriptGetMaxEquipmentCount (no anchor) | SItem.java:272 ✓ |
| 960 | RenderEngine::scriptGetRenderAspectMultiplier (no anchor) | RenderEngine.java:1476 ✓ |
| 32 (Battle) | Battle::scriptResetCameraMovement @ 0x800dabcc | Battle.java:5374 ✓ |

Behavioral contracts **not** individually audited for the remaining slots —
**deferred to M9** per the checkpoint mandate (gate therefore
CONDITIONAL, §6).

### 4.2 Priority B — C-01 independent reconciliation

- Table counts/gaps/mnemonics: see M-1 (counts and op 89 corrected;
  otherwise the C-01 "drift with preserved discrepancies" resolution holds).
- Master-table op bodies re-verified against `ScriptState` (source:lines):
  1 rewind; 2 wait (906-914); 3 wait_cmp / 4 wait_cmp_0 (`scriptCompare0`
  916-918: `compare(ZERO, p0, header) ? CONTINUE : PAUSE_AND_REWIND`);
  8 mov; 9 `FUN_80016790` broken swap (934-948; source comment "Pretty sure
  this is _supposed_ to be a swap"; log description at
  `ScriptManager.java:94`); 10 memcpy catch+warn (retail OOB bug);
  12 mov_0 (`scriptSetZero` 966-970); 16-22 bit ops incl. SHR arithmetic
  (`scriptShiftRightArithmetic` 1020: `p1 >> p0`; `Param.shr` is logical
  `>>>`); 24-30 add/sub/sub_rev/incr/decr/neg/abs; 32 mul; 33 div
  (zero-division guard 1100-1114); 34 div_rev (`scriptDivide2` 1118-1126,
  safeDiv float-aware); 35/43 mod (same method, `case MOD, MOD43` 752);
  36/44 mod_rev (753); 40 mul_12 (1146-1149); 41 div_12 (1153-1162:
  p0==0 → 0 else `((p1 << 4) / p0) << 8`); 42 div_12_rev (1166-1175);
  48 sqrt (1181-1190: float branch computes on the **int mirror** — see
  L-5); 49 rand (1194-1198: `(p0 * simpleRand()) >>> 16`); 50-52
  rsin/rcos/atan2; 56 call (`scriptExecuteSubFunc` 1219-1231: null →
  PAUSE_AND_REWIND stall; UnsupportedOperationException →
  `RuntimeException("Script subfunc N error")`); 64-67 jmp / jmp_cmp /
  jmp_cmp_0 (`scriptConditionalJump0` 1294-1301) / while (decr; jump if
  ≠ 0); 68/74 jmp_table / gosub_table (double indirection
  `table[table[p0]]` 1320-1323 / 1349-1352); 72 gosub (frame offset = next
  word); 73 return; 80/82 both `deallocateWithChildren()` → PAUSE_AND_REWIND
  (1360-1370); 83 deallocate_other (1372-1376: self → PAUSE_AND_REWIND else
  CONTINUE); 86/87 fork / fork_reenter (1379-1403: child jumps; stor[32]
  payload; reenter via child's file entrypoint); 88 consume
  (`scriptConsumeChild` 1406-1409: `commandOffset = consumeChild()`);
  96 debug96 = `FUN_800172f4` no-op (1408-1410); 97 no-op; 98
  `FUN_80017304` (1419-1422: `LOGGER.error("DEBUG: %s", p0)`); 99 depth
  (1425-1428: `callStack.size()`); 101-103 push/pop/cmp (1430-1443; see
  L-9); 100 toolchain-only (runtime `byOpcode` throws).
- Compare-operator table (§6.2) verified in full against
  `scriptCompare` (800-858): integer/float/registry-ID columns all match
  (float: `<=` = `a<b || flEq`; `<` = `a<b && !flEq`; `==` = `flEq`;
  `!=` = `!flEq`; `>` = `a>b && !flEq`; `>=` = `a>b || flEq`; `flEq`
  epsilon 1e-5 strict-`; mixed null-test and registry-registry rules
  throw `IllegalArgumentException` for ops other than 2/3; unknown op →
  `false`).
- Parameter-type table (§4) verified against `parseParam` (535-717):
  0x01 (inline next word), 0x02, 0x03/0x04 (cross-state hops through
  `manager.getState`), 0x05-0x0f, 0x12 throws, 0x13 (base opOffset,
  `(short)word + cmd2`), 0x14, 0x15/0x16/0x17 (second-word base; see
  L-13), 0x20-0x23, 0x21 (cmd2 = char count; consumes `1 + (len+3)/4`;
  lazy read at `getRegistryId()`), 0x24-0x27 (descriptor + packed word;
  low 16 = word index, high 16 when count==2 = script-state index),
  fallback inline-self for unrecognized types.
- Header-validity predicate (§5) verified against `parseHeader`
  (606-636) + `isValidOp` (638-648): all five conditions accurate,
  including the CALL-wildcard arity and the `headerParamName == null`
  zero-header rule.
- `_0` forms / `modifyOp` (toolchain 139-153): WAIT_CMP/JMP_CMP/MOV with
  first param resolving to constant 0 → `_0` variant — verified.
- Doc 02 §6.1 op-82 discrepancy row (log text "deallocate children" vs
  both-ops-deallocate-self+children) re-verified against
  `ScriptManager.java:169-171`.

### 4.3 Priority C — encoding examples (all eight classified)

Experimental protocol: synthetic low-level Fate files assembled with the
built toolchain (`Shell a -v local`), disassembled (`Shell d -v local`),
and re-assembled; hand-crafted binaries for hazard cases. Results in
§2/§4; per-example classification:

| example | classification | result |
|---|---|---|
| 9.1 doNothingScript constant | source-verified (M3; `Scus94491BpeSegment_8004.java:186`; consistent with `Battle.allocateLighting` loading it at Battle.java:6525) | doc correct |
| 9.2 `wait 5` | **experimentally byte-verified** | doc correct |
| 9.3 `mov stor[18], 0x1` | **experimentally byte-verified** | doc correct |
| 9.4 `call 322, stor[0], 0x2` | **experimentally byte-verified**; call 322 = `Battle::scriptSetLoadingBentAnimationIndex` per local meta | doc correct |
| 9.5 `wait_cmp ==, 0x0, stor[4]` | **experimentally byte-verified** incl. `_0` re-expansion on decompile | doc correct |
| 9.6 two-word immediate | doc bytes **incorrect** (descriptor); corrected bytes experimentally verified | L-1, L-4 |
| 9.7 `jmp_cmp` _0 form | doc param order **incorrect**; corrected order experimentally verified | L-2 |
| 9.8 strings | doc char codes/bytes **incorrect**; corrected bytes experimentally verified + byte-identical round trip | L-3 |

Additional experimental results:

- **True round trip**: exp1 (wait/mov/call/next-immediate/wait_cmp_0/
  jmp_cmp_0 with label + entrypoint) → assemble → disassemble →
  re-assemble = **byte-identical**.
- **One-way hazards reproduced** (new verification class —
  "experimentally confirmed failure"): hazA (0x24 STOR_INL) → re-assembly
  throws `Unknown param stor[inl[0x10]]`; hazB (CMP header 8) → re-assembly
  throws `Unknown param Unknown CMP operator 8`.
- **String recovery is identification-driven** (L-12): unreferenced char
  words stay `data`; round trip still byte-stable.
- Entrypoint/opcode-word layout re-verified in the assembler
  (`Assembler.java`: entrypoint byte address via `findLabelAddress`;
  `opcode | params.length << 8 | headerParam << 16`; LodString packing
  char0 low 16 / char1 high 16; pointer tables `(dest − rel.address)/4`).
- No retail-asset verification of any kind (none available).

### 4.4 Priority D — interpreter and timing

- `executeFrame` (458-533): doc 05 §4 pseudocode matches line-for-line
  (ticks++, PRE/POST events, `FLAG_FILE_NOT_SET | FLAG_CHILD_SCRIPT |
  paused` gate, `OpType.byOpcode(opCommand & 0xff)`, paramCount > 10 →
  `RuntimeException("Too many parameters!")`, `commandOffset++` before
  eager param parse, opOffset committed only on CONTINUE/PAUSE,
  `Arrays.fill(params_20, null)`, loop `while (ret == CONTINUE &&
  !paused)`, `frame().offset = opOffset`).
- `scriptExecuteSubFunc` (1219-1231) verified (doc cites 1218-1232 —
  cosmetic one-line offset; may tighten).
- `ScriptManager`: param pool types (196-197); `willTick` (200-202);
  `tick()` (204-217) pseudocode verified; `stop/start/pause/resume`
  javadoc verified; `clear` (251-253); `allocateScriptState` (313-328:
  stor 1..62 = −1, stor[0] = index, stor[7] flag word) verified;
  `findFreeScriptState` → M-2; `executeScriptFrame` (338-363:
  `paused || stopped` gate, `paramPool.reset()`, all 108 slots in index
  order, dump + `RuntimeException("An error occurred while ticking script
  N")`) verified; `executeScriptTickers` → M-4; `executeScriptRenderers`
  (stopped-only gate) verified; `meta()` (null base URI, local `meta`
  dir) verified.
- Storage model: `setStor(int)`/`setStor(float)` (219-231),
  `getStor/getStorFloat/isStorFloat` (233-243) verified (float → int
  mirror `Math.round`).
- Fork (362-393) / consume (394-430) / deallocate family (319-360)
  verified in full — doc 05 §8 accurate (child = "Forked " + name;
  child flags = parent flags | CHILD; storage 8–62 copied, isFloat and
  registry IDs full-copy; stor[5]/[6] head-insertion chain; call stack
  copied in order; consume: no-child → `RuntimeException("Null command")`,
  grandchild relink, identity move, child deallocated, returns
  `frame().offset`).
- `dump()` (1445-1486) verified: file/op/byte offset, op param, params,
  all 63 stor slots float-aware, non-null registry IDs, call stack,
  ±5-line disassembly window via in-game `disassemble` +
  `Translator.translate(…, false, false, true)` matched by hex line
  prefix.
- Cadence cross-references verified: `EngineStates.java:71`
  (`setFramesPerTick(1)` on every transition); `SMap.java:4088-4089`
  (`vsyncMode = 1` + `setFramesPerTick(2)` in INIT_0 → 30 Hz submap
  scripts); `Menus.java:67/101` (stop/start); `SMap.java:4019` pause with
  resumes at 4168/4246/4316/4324/4354/4379; explicit slots
  `SMap.java:3263` (0), `Battle.java:6524` (1), `Battle.java:9308` (5),
  `Battle.java:2167` (player BENT).
- Param classes: `Param.set(boolean)` (10-12), `shr`/`shra` (190-196),
  `div`/`mod` float-aware (114-144), `ScriptStorageParam.get()` = int
  mirror (21-23) verified.
- Findings: M-2, M-4, L-8, L-9 (see register).

### 4.5 Priority E — metadata and toolchain

- `Patcher` (full read): strip pipeline (EOL cut when `indexOf(';') > 0`;
  full-line when `== 0`; `CALL_MATCHER ^call\s+(.+?)\s*(?:,.*|$)` with
  exact-name → index replacement; trim; blank-line removal), unified diff
  (3-line context, "original"/"modified" headers), apply/undo via difflib,
  `PatchFailedException` — doc 03 §7 verified.
- `Shell`: command set and option flags verified (d: `-b/-t/-C/-N/-l/-e`,
  `-v` default `snapshot`; c/a: `-L` libs; s: `-C/-E/-F/-B` all default
  off → full strip); base URI hardcoded
  `https://legendofdragoon.org/scmeta/`, cache `./cache` — doc 04 §4
  verified; L-7 footer wording.
- `Meta`/`MetaManager` (full read): `getVersions` = `versions.php` first
  row; `snapshot` always remote; named version = local-if-dir-exists
  (no freshness check) else membership check (`NoSuchVersionException`)
  then fetch + cache write (all files incl. enum CSVs); non-200 →
  `RuntimeException("Failed to download meta …")`; local load reads
  descriptions/params/enums + `<class>.csv`; join by method name — doc 04
  §3/§4 verified; L-6 failure row.
- `ScriptPatcher` (full read): include dirs `libs`/`scripts`;
  `needsUpdate` (CRC32 over patch + config + transitive includes, missing
  → null entry); `apply()` (patch new/changed, rethrow
  `PatchFailedException`, restore deleted from backups, delete-empty-dirs
  up to backups root, cache mirror = delete + copy on change);
  `patchFile` (backup-if-missing with replacement quirk; config `branch`
  hex / `table_length` hex→count / else `IllegalArgumentException`;
  decompile **backup bytes** with `translate(…, true, true, false)`;
  `Patcher.applyPatch`; `SCRIPTS.assemble`; any failure →
  `RuntimeException("Failed to patch …")`) — doc 03 §7.3 verified.
- `FateCompilerVisitor` (relevant sections): ASM-op path (arity check,
  string-rejection error); `wait` special case (`mov arg, _expr_k; wait
  _expr_k`, comment explains in-place decrement) — doc 01 §2.5 / doc 02
  §9.2 verified; engine-call path (meta name lookup, "unknown engine
  call" error); `&&`/`||`/`&&=`/`||=` → CMP header 8/9 — H-2 / L-10.
- `LodString` (full read): charset both directions, controls, terminator,
  `<noterm>` — L-3 / doc 01 §5 verified.
- `MathHelper` (toolchain): `safeDiv` (I-3), `flEq` (1e-5), PSX-degree
  helpers.

## 5. Updated round-trip hazard master set (as remediated)

Byte-stable round trip (binary → text → binary) holds only when:

1. entrypoint count inference matches (or is pinned);
2. no inline pointer is out of range (rewritten to 0 with warning);
3. no param uses a **one-way** type — 0x0c, 0x0d, 0x11, 0x17, **0x24,
   0x25, 0x26, 0x27** (reparse failure: `Unknown param`) — or a
   **hard-failing** type — 0x12, 0x15, 0x16 (translator throws); 0x13
   recompiles as 0x09 (value-stable, byte-changing); 0x21 IDs must contain
   a colon;
4. no compare op carries **header 8/9** (translator emits
   `Unknown CMP operator N` — reparse failure);
5. jump-table lengths stable (config pins for ambiguous scripts);
6. labels referenced by name only (tokenizer rebinds from final layout).

## 6. Remaining work (final state, 2026-11-04)

1. **Remediation** of the M3 documents per the §3 worklist — **DONE** (all
   items applied; dispositions in §7.1).
2. **Governance sync** — **DONE** (register C-01/Q-18/Q-42/Q-60/Q-61
   wording, README index + M3 findings summary, plan M3 status, AGENTS
   current phase).
3. **Final audit sections** — **DONE** (this §7).
4. **File-ending audit and final commit** — **DONE** (see §7.4; the final
   commit records the remediation; this checkpoint commit `a0b3733`
   precedes it).
5. **592-slot behavioral contracts** — **deferred to M9 subsystem
   milestones** (M4 where timing-adjacent) per the mandate — the catalog
   remains name/anchor/param-verified (592/592) with the §4.1 spot sample;
   documented as such in `scripting/06` §1 and §7.

The original PSX behavior qualification (no asset-validated claims; `@Method`
labels as anchors only; original-VM parity Unresolved) holds throughout.

## 7. Remediation record and final verification

### 7.1 Disposition of every finding (26 total: 2 High, 4 Medium, 14 Low,
6 Informational)

| finding | disposition | applied in |
|---|---|---|
| H-1 (0x24–0x27 one-way) | **Corrected** | 02 §4 rows 0x24–0x27; 02 §8 item 6 table + shortlist; 01 §4 param table (rows 0x21/0x24–0x27); 03 §5 (immediate/operator lines); 03 §8.2 (patch constraint); 03 §10 item 3 |
| H-2 (CMP header 8/9 one-way) | **Corrected** | 02 §6.1 row 103; 02 §6.2 (intro + rows 6/7/8/9); 02 §8 new item 10 + shortlist; 01 §2.4 (hazard note); 01 §3.3; 03 §5 (operator line); 03 §8.2; 03 §10 new item 4 |
| M-1 (entry counts / op 89) | **Corrected** | 02 §6.0 (67/68, 89 in gap lists); 02 §6.1 closing note; register C-01; README item 1 |
| M-2 (allocation round-robin) | **Corrected** | 05 §2; 05 §14; register Q-42; README item 6 |
| M-3 (SMap ranges) | **Corrected** | 04 §3 (exact 144-slot range set) |
| M-4 (ticker gating) | **Corrected** | 05 §7.1 (pseudocode gates for tickers and renderers) |
| L-1 (NEXT_IMMEDIATE bytes) | **Corrected** | 02 §9.6 (`00 00 00 01`) |
| L-2 (JMP_CMP_0 param order) | **Corrected** | 02 §9.7 (STORAGE then INLINE) |
| L-3 (H/I char codes) | **Corrected** | 02 §9.8 (0x26/0x27 + corrected byte layout); 01 §5 was already correct |
| L-4 (bits 16–23 wording) | **Corrected** | 02 §9.6; 01 §4 table + §6 note; 03 §5 (high byte / bits 24–31) |
| L-5 ("raw bits" → int mirror) | **Corrected** | 02 §6.2 rows 6/7; 02 §6.1 row 48 (sqrt) |
| L-6 (missing-meta failure path) | **Corrected** | 04 §6 (split: missing dir → uncaught NPE; missing CSV → IOException) |
| L-7 (CLI footers) | **Corrected** | 03 §9 (exact two-footer wording) |
| L-8 (Battle.java:4490) | **Corrected** | 05 §2 explicit-slot list; 05 §13 evidence index |
| L-9 (unanchored op bodies) | **Corrected** | 05 §12 (3 unanchored bodies named: 101/102/103) |
| L-10 (ANDA/ORA → ANDC/ORC) | **Corrected** | 01 §2.4 |
| L-11 (catalog: 578 unique / aliases / 44 no-params) | **Corrected** | 06 §1 (M3-audit block), 06 §6 evidence index, 06 §7 open items, README item 7 |
| L-12 (string-recovery precondition) | **Corrected** | 03 §3.2 item 5 |
| L-13 (0x15/0x16 width) | **Clarified** | 02 §4 (runtime width 2 annotated; toolchain declares 1) |
| L-14 (0x21 colon caveat) | **Corrected** | 02 §8 (0x21 row); 01 §4 ID row; 03 §10 item 3 |
| I-1 – I-6 | **Recorded** (no doc error; retained here as the historical record; I-4 referenced from 06 §1) | — |

Totals: **19 corrected, 1 clarified, 6 informational-recorded** — no finding
deferred and none left unresolved by remediation.

### 7.2 Final verification coverage (by priority area)

- **C-01 (Priority B)**: independently re-verified — 67/68 entry counts,
  all 67 shared opcodes with identical mnemonics, toolchain-only {100},
  op-89 gap in both tables, `byOpcode` throw-vs-null split. The C-01
  resolution (drift, discrepancies preserved) stands; the reconciliation is
  now count-correct (register C-01, README item 1).
- **Encoding examples (Priority C)**: all eight classified — 9.1
  source-verified (reference constant); 9.2–9.5 **experimentally
  byte-verified**; 9.6/9.7/9.8 **corrected and experimentally verified**
  (incl. byte-identical reassembly round trips for exp1 and exp2). The
  source-derived / tested / asset-validated distinction is preserved in
  `scripting/02` §9's intro; no retail-asset validation exists (none
  available).
- **Interpreter/timing (Priority D)**: `executeFrame`, `tick`,
  `executeScriptFrame`/`Tickers`/`Renderers`, allocation, fork/consume/
  deallocate, dump, cadence — all verified; mechanism corrections M-2 and
  M-4 applied in 05 §2/§7.1/§14.
- **Native catalog (Priority A)**: census reproduced exactly (592 =
  86+362+144; 592/592 names; 559 anchored; 432 unimplemented; 0
  unimplemented with params rows); new facts documented (578 unique
  implementations, 4 alias groups, 44 methods without params rows, 4
  commented-out Battle assignments); 10-slot anchor spot check plus 5
  behavioral body reads this audit. Exhaustive behavioral contracts:
  **deferred to M9/M4** (§7.3).
- **Toolchain/meta (Priority E)**: Patcher, Shell, Meta/MetaManager,
  ScriptPatcher, FateCompilerVisitor, LodString verified in full; doc 03/04
  corrections applied (L-6, L-7, L-12 plus the H-1/H-2 extensions).

### 7.3 Native-function coverage limitation (explicit)

- **Mechanically verified for all 592 slots**: index, name (joined to
  `descriptions.csv`), ROM anchor, params (joined to `params.csv`),
  alias/implementation identity (578 unique).
- **Behaviorally inspected**: a limited sample (M3 spot rows 0/7/192/236/
  705/865/891/901/960/32; the audit re-verified 10 slots with anchors and
  read 5 bodies: 7, 192, 236, 865, 891).
- **Deferred**: full per-slot behavioral contracts for the 578 unique
  implementations → M9 subsystem research (M4 where timing-adjacent).
  Recorded in `scripting/06` §1 and §7. Not a blocker for M3 acceptance —
  the catalog's dispatch/metadata claims are complete as documented.

### 7.4 Remaining research risks and final checks

1. **Retail-asset validation**: no M3 or audit claim is `asset-validated`;
   Q-59 (string/control usage), Q-60 (one-way types and high-byte
   immediates — now incl. 0x24–0x27 and header 8/9), Q-61 (original-VM
   behavior of unimplemented/absent opcodes — now incl. 89, 100) remain open
   on asset/external-PSX evidence.
2. **Original-VM parity** (Q-17 advanced; Q-63, Q-64 open): reference
   behavior documented; PSX-original parity Unresolved where the reference
   diverges (div-by-0, DEALLOCATE82, fork depth).
3. **Remote scmeta content** (Q-62): uncheckable offline; local
   `patches/meta` is the in-game authority regardless.
4. **Behavioral depth of the 578 native implementations**: deferred to
   M9/M4 (§7.3) — M9 must re-verify each function it ports.
5. **Final checks (2026-11-04)**: all four reference repositories verified
   clean at pinned revisions (the audit's offline build left only
   gitignored `build/` artifacts in Script-Recompiler); no engine code,
   Rust code, or unrelated files touched; `scripts/audit_file_endings.py`
   run at finalization with zero violations (fixed on first pass only where
   this audit's own files lacked the final newline).

### 7.5 Final M3 audit gate

**PASS.**

Basis: every actionable finding (2 High, 4 Medium, 14 Low) is remediated in
place with inline audit annotations; the corrected claims are consistent
across all six M3 documents, the register, the index, the plan, and
`AGENTS.md`; the two High findings rest on reproduced experimental failures,
not just source reading; the binary examples carry explicit
source-derived / tested / asset-validated classification; the catalog's
verification depth is explicitly scoped; remaining limitations are
registered (Q-59–Q-64) and assigned to later milestones (M4/M9 + assets).
No correction is incomplete, and no M3 claim now contradicts the pinned
reference sources or the offline toolchain experiments.
