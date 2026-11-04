# M3-04: The Meta System

Scope: the metadata that names and annotates the engine's native script
functions and their parameters — the CSV files, the `Meta`/`MetaManager`
loading machinery, the remote `scmeta` service, the enum tables, and the
**authority question** (Q-19): remote metadata vs the local `patches/meta`
copy shipped with the runtime.

Validation categories, per `AGENTS.md`: **source-verified** (reference code),
**format-corroborated**, **asset-validated** (none for M3; the local CSVs under
`Severed-Chains/patches/meta/` are observed reference artifacts).

## 1. What metadata is

`org.legendofdragoon.scripting.meta.Meta` (source-verified, full read):

- `ScriptMethod[] methods` — the **1024-slot native function table as data**:
  each entry has `name` (e.g. `Scus94491BpeSegment::scriptSetGlobalFlag1`),
  `description` (human text), and `ScriptParam[] params`.
- `Map<String, String[]> enums` — fully-qualified class name → ordered value
  list (e.g. `legend.game.combat.bent.BattleEntityStat` → 182 values).

The **position** of a method in the array is its **call index**: the `CALL`
opcode's header param, the `call N`/`call Class::name` spelling in low-level
Fate, and the slot in the runtime's function table are all the same number
(§5).

Consumers:

| component | uses |
|---|---|
| `FateCompilerVisitor` | high-level `pkg::name` calls → index + param metadata; enum identifiers → values |
| `Tokenizer` | call name → index + arity check; enum-typed call params → value-name → index |
| `Translator` | `stripNames`: index → `Class::name` (and back for pretty output); comments |
| `Patcher.strip` | call name → numeric index in diffs (canonical form) |
| `Disassembler` | CALL branch attributes (`jump`/`subroutine`/`fork_jump`/`gosub`), `string` param type (string tables), arity warnings |
| `ScriptManager` | owns the `Meta` instance used by all of the above in-game |

## 2. The CSV files

All CSVs are parsed with OpenCSV; every field is double-quoted; **no header
rows**. The local reference set (`Severed-Chains/patches/meta/`, observed):

| file | rows | content |
|---|---|---|
| `descriptions.csv` | 1024 | one row per call index: `"Class::method","description"` |
| `params.csv` | 1538 | one row per parameter of a method (many rows per method) |
| `enums.csv` | 2 | one row per enum class: `"legend.game.combat.types.BattleSoundUsage"`, `"legend.game.combat.bent.BattleEntityStat"` |
| `legend.game.combat.types.BattleSoundUsage.csv` | 2 | `ACTIVE_FORM`, `REACTION` |
| `legend.game.combat.bent.BattleEntityStat.csv` | 182 | `LEVEL`, `DLEVEL`, `CURRENT_HP`, `CURRENT_SP`, `CURRENT_MP`, `STATUS`, … (one value per line) |

Column layouts (source-verified from `Meta.loadMeta` + `MetaManager.loadMeta*`):

- `descriptions.csv`: col 0 = method name (`SimpleClass::method` — **no
  package**, matching the low-level `call` name grammar), col 1 = description.
- `params.csv`: col 0 = method name (joined to the description row by exact
  string equality), col 1 = **direction**, col 2 = **type**, col 3 = **name**,
  col 4 = **description**, col 5 = **branch**.
- `enums.csv`: col 0 = fully-qualified enum class name.
- `<class>.csv`: col 0 = one enum value per row; **row order = the runtime
  integer value** of the enum (§4).

### 2.1 Value domains observed locally (source-verified by census of the local CSVs)

- `direction`: `in`, `out`, `both`
- `type`: `any`, `bool`, `bool_array`, `flag`, `float`, `int`, `int_array`,
  `reg`, `reg_array`, `string`, or an enum class name (the two above)
- `branch`: `none`, `fork_jump` (the toolchain's disassembler additionally
  understands `jump`, `subroutine`, and `gosub` — none of which occur in the
  local CSV; they would come from the remote service, Q-62)

## 3. Index alignment: CSV row = call index = table slot

The runtime dispatch (source-verified, `Scus94491BpeSegment_8004.java`):

- a base table `Function<RunningScript, FlowControl>[] functions =
  new Function[1024]` is filled in a static block with **86 non-null slots**
  (census), wrapped into the unmodifiable list
  `scriptSubFunctions_8004e29c` (nulls preserved);
- engine states supply override tables: `Battle.getScriptFunctions()` and
  `SMap.getScriptFunctions()` each build a **fresh 1024 array** containing
  only their own slots (Battle: camera/BENT/menu/stage slots including 32–50
  and 128–178 — full range set re-verified by the M3 audit; SMap: 144
  exploration slots at exactly **12–15, 18–19, 96–127, 198, 256–271,
  288–319, 672–703, 768–791, 940** — corrected by the M3 audit; the earlier
  draft's "8–20, 32–48, 208–286" was wrong, and 32–48 are actually Battle
  camera slots);
  `EngineState.getScriptFunctions()` defaults to **null** for the other
  states;
- `loadEngineStateFunctions(state)` installs the state's list (or null) into
  `engineStateFunctions_8004e29c`;
- `getScriptFunction(index)` resolves **base first, then state list** — base
  entries win on conflict; state entries fill base-null slots.

`descriptions.csv` describes this **union view**:

- for base slots (86 of them), the row names the base function;
- for state-only slots, the row names the state-specific function (observed:
  rows 32–41 = `Battle::scriptResetCameraMovement` … `Battle::scriptIsCameraMoving`
  = exactly the battle camera slots; rows 128+ = BENT position/rotation
  functions).

Spot-verification of the row↔slot alignment (source-verified — extended in
doc 06 to a full 592/592 mechanical join with zero mismatches):

| index | descriptions.csv name | runtime slot |
|---|---|---|
| 0 | `Scus94491BpeSegment::scriptSetIndicatorsDisabled` | `functions[0]` = same |
| 7 | `DrgnFiles::scriptWaitForFilesToLoad` | `functions[7]` = same |
| 192 | `Text::scriptGetFreeTextboxIndex` | `functions[192]` = same |
| 236 | `Audio::scriptPlaySound` | `functions[236]` = same |
| 705 | `Audio::scriptSssqFadeOut` | base slot (audio) |
| 865 | `Scus94491BpeSegment::scriptTakeItem` | base slot |
| 891 | `SItem::scriptInputActionPressed` | base slot |
| 901 | `SItem::scriptGetMaxEquipmentCount` | base slot |
| 960 | `RenderEngine::scriptGetRenderAspectMultiplier` | `functions[960]` = same |

(Indexes are 0-based call indices, i.e. `descriptions.csv` row N+1.)
The alignment holds for every checked row; a systematic full-table diff of all
1024 rows against the base+state tables is the remaining mechanical check
(recorded as a follow-up in doc 06 — the union view makes a naive 1:1 check
state-dependent for the ~600 state-only slots).

Consequences:

- `CALL` headers are validated **< 1024** by the disassembler's header
  predicate (binary-format doc §5) — the table size is a format invariant.
- A call index with **no function in the current state** (base null, state
  null) → `getScriptFunction` returns null → the runtime op returns
  `PAUSE_AND_REWIND` → **permanent stall** (binary-format doc §6.6). This is
  how a script calling a state-appropriate index in the wrong state misbehaves.
- The `@ScriptDescription`/`@ScriptParam` annotations on the native functions
  are the **source of the CSVs** (the annotations carry name/description/
  direction/type/branch/param-name text matching the CSV content — e.g.
  `SEffe.scriptSetScriptScript`'s annotations match a `params.csv` row
  pattern). The CSVs are a generated/exported view of the annotations; the
  runtime does **not** read the annotations at runtime — it reads the tables
  directly (Inferred: the generation step is not in the reference source;
  the consistency is observed, the tooling that writes the CSVs lives outside
  these repositories).

## 4. Enum tables

Two consumers, both source-verified:

1. **Low-level tokenizer** (`Tokenizer.parseParam`): for a `CALL` at
   `headerParam`, if the metadata param at position *i* has a `type` that is a
   known enum class, the param text is matched **case-insensitively** against
   the class's value list and emitted as an `IMMEDIATE` with value = **the
   value's row index**. Unknown value → `RuntimeException("Unknown <class>
   value <name>")`. So low-level Fate writes enum params as names
   (`call …, CURRENT_HP`) and the assembler emits the integer.
2. **High-level compiler** (`FateCompilerVisitor.visitAssignable`): a bare
   identifier that equals any enum value in any enum table becomes a
   `FateEnum` — emitted as the **name** in low-level text, so the chain
   high-level → low-level → bytes goes through the same name→index resolution
   at assembly time.

The runtime integer is therefore the **row index in `<class>.csv`**:
`LEVEL`=0, `DLEVEL`=1, `CURRENT_HP`=2, … for `BattleEntityStat` (182 values);
`ACTIVE_FORM`=0, `REACTION`=1 for `BattleSoundUsage`. The runtime code that
consumes those integers switches on them directly (e.g. BENT stat getters —
doc 06).

## 5. `MetaManager` and the two metadata worlds

### 5.1 Remote service (standalone toolchain)

`MetaManager(URI baseUri, Path cacheDir)`; the CLI uses
`baseUri = https://legendofdragoon.org/scmeta/`, `cacheDir = ./cache`
(source-verified, `Shell.java`). Layout under `baseUri`:

- `versions.php` → CSV whose **first row** is the version list
  (`getVersions`);
- `<version>/descriptions.csv`, `<version>/params.csv`, `<version>/enums.csv`,
  `<version>/<class>.csv`.

`loadMeta(version)` (source-verified):

- **`snapshot`**: always fetched from the server
  (`<base>snapshot/`), **never cached** — the moving "latest" view;
- **named version**: if `cacheDir/<version>` exists → load **entirely from
  cache** (no server contact); otherwise the version must appear in
  `versions.php` (else `NoSuchVersionException`), then all CSVs are fetched
  from `<base><version>/` and **written to the cache** as they download
  (`requestCsv(url, cache)` writes each file to
  `cacheDir/<version>/<file>`).

HTTP: plain `HttpURLConnection` GET; non-200 → `RuntimeException("Failed to
download meta …")`. There is no checksumming, no delta update, no lock — the
cache is a last-pull mirror.

### 5.2 Local metadata (in-game runtime) — Q-19 resolved

`ScriptManager.meta()` (source-verified, full read):

```java
this.meta = new MetaManager(null, this.patchDir).loadMeta("meta");
```

- `baseUri = null` (no server ever contacted — the URI would NPE if any
  fetch path were taken);
- `version = "meta"` → not `snapshot`; `versionDir =
  patchDir.resolve("meta")` = **`patches/meta`**, which exists → loaded via
  the **local-only** `loadMeta(Path)` path: reads
  `descriptions.csv`, `params.csv`, `enums.csv`, and the per-enum CSVs from
  the local directory. No network code executes.

**Q-19 (remote `scmeta` vs local `patches/meta`) is resolved**: the
authoritative runtime metadata is the **local** `patches/meta` CSVs shipped
in the reference repository; the remote `scmeta` service exists exclusively
for the standalone `Shell` toolchain. The two are separate copies that must
be kept in sync manually; the reference game never validates or updates its
local copy against the server. Which copy is "more current" for a given
version cannot be established offline (Q-62).

### 5.3 Implications

- The in-game patch pipeline (decompile → strip → diff → assemble) runs
  **fully offline** on the local CSVs (doc 03 §8).
- A standalone decompile with `--version snapshot` can produce text whose
  call *names* (via the remote meta) differ from what the in-game pipeline
  would emit (local meta), if the two CSV sets have drifted. Patches are
  generated and applied within one meta world in practice (both the shipped
  diffs and the runtime pipeline use name→index, and indices are stable
  across the two worlds as long as row order is unchanged).
- Row **order** is the load-bearing invariant: any reordering of
  `descriptions.csv` rows changes call indices for every downstream artifact
  (diffs, recompiled scripts). The shipped local CSV preserves the runtime
  table order (§3).

## 6. Failure modes

| situation | behavior (source-verified) |
|---|---|
| missing local `patches/meta` **directory** | `loadMeta("meta")` proceeds down the remote path with a **null base URI** (`ScriptManager` passes `null`) → `getVersions()` throws **`NullPointerException`**, which `meta()`'s catch (`IOException \| CsvException \| NoSuchVersionException`) does **not** catch → `ScriptManager` construction fails (corrected by the M3 audit — the M3 text mischaracterized this as an IOException) |
| local `meta` dir present but a **CSV file missing** | `loadMeta("meta")` throws `NoSuchFileException` (IOException) → `ScriptManager` construction fails → game init fails (the directory is shipped, so this is a repo-integrity issue) |
| `params.csv` row for a missing method | simply contributes no params (join by name; absent = zero params) — silent |
| `descriptions.csv` row with no `params.csv` rows | method with zero params — a `CALL` with params still executes (runtime doesn't check arity; the disassembler warns "Call passed more params than expected" and a commented-out throw remains in the source) |
| call name not in meta (tokenizer) | `UnknownCallException` (compile-time failure of the assemble) |
| enum value not in the class list | `RuntimeException("Unknown <class> value <name>")` |
| enum class in params.csv but no `<class>.csv` | local `loadMeta` throws IOException (file missing); remote fetch 404 → RuntimeException |
| non-200 on remote CSV | `RuntimeException("Failed to download meta …")` |
| unknown version (named, uncached) | `NoSuchVersionException` |
| CALL index ≥ 1024 in a script | disassembler header predicate rejects the word (treated as data); runtime executes it and stalls (null function → PAUSE_AND_REWIND) — binary-format doc §6.6 |

## 7. Authority summary

- **Behavioral truth**: the runtime function tables (base 86 slots + state
  override lists) — code, not metadata.
- **Naming truth**: `descriptions.csv` (the union view, §3) + the
  `@ScriptDescription`/`@ScriptParam` annotations that mirror it.
- **Semantics of names** (direction/type/branch/enum membership): the
  `params.csv` + enum CSVs; the disassembler's symbolic models depend on
  `branch`/`string` correctness.
- **Versioning**: local CSVs = unversioned (whatever the repo ships); remote
  = named versions + `snapshot`. The reference game is pinned to the local
  copy; the CLI defaults to `snapshot`.

## 8. Evidence index

| Claim area | Source |
|---|---|
| `Meta` structure, join logic | `Script-Recompiler/.../meta/Meta.java` (full read) |
| `MetaManager` remote/local/cache/snapshot | `Script-Recompiler/.../meta/MetaManager.java` (full read) |
| CLI meta wiring | `Script-Recompiler/.../Shell.java` (full read: URI, `./cache`, default `snapshot`) |
| Local CSV format/content | `Severed-Chains/patches/meta/*.csv` (all five files inspected; row counts 1024/1538/2/2/182; value censuses) |
| Index alignment | `Scus94491BpeSegment_8004` static block (86 base slots), `getScriptFunction` (base-first merge), `loadEngineStateFunctions`, `Battle.getScriptFunctions` (735+), `SMap.getScriptFunctions` (749+), spot rows 0/7/32–41/237/705/865/891/893/901/960 |
| Runtime authority (Q-19) | `ScriptManager.meta()` (full read) |
| Enum consumers | `Tokenizer.parseParam` (enum branch, lines 401–412), `FateCompilerVisitor.visitAssignable` (lines 877–883), `FateEnum` |
| Disassembler metadata use | `Disassembler.java` lines 160–200, 245–265 (branch/string/arity) |
| Annotation→CSV mirror | `SEffe.scriptSetScriptScript` `@ScriptDescription`/`@ScriptParam` vs `params.csv` pattern |

Pins: `Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511`,
`Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade`.

## 9. Open items

- **Q-19 — closed** (§5.2): runtime metadata is the local `patches/meta`
  CSVs; remote `scmeta` is standalone-tooling-only.
- **Q-62** (new): the live content/versioning of
  `https://legendofdragoon.org/scmeta/` is external evidence; the local CSVs
  may lag or lead it. Whether any shipped diff depends on remote-only names
  (e.g. `jump`/`subroutine` branch values) is uncheckable offline.
- Full 1024-row alignment audit of `descriptions.csv` against the base +
  state tables: spot-verified (§3); the mechanical full diff is deferred to
  doc 06's catalog (where per-index owners are tabulated).
