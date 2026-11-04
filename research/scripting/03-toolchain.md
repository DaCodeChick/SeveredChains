# M3-03: The Fate Toolchain

Scope: the complete tooling around the Fate language and binary format —
high-level compiler, disassembler, translator, tokenizer, assembler, the
unified-diff patch layer, the standalone CLI (`Shell`), and the in-game patch
pipeline (`ScriptPatcher`) — including the round-trip and patching guarantees.

Validation categories, per `AGENTS.md`: **source-verified** (established from
reference code), **format-corroborated** (independent spec/implementation/test),
**asset-validated** (real game bytes — none available for M3; the shipped
patch files under `Severed-Chains/patches/` are observed artifacts of the
reference ecosystem, not retail disc bytes).

## 1. The three text tiers

```
high-level Fate source (.fate)
   │  FateCompiler (ANTLR parse → preprocess → FateCompilerVisitor lower)
   ▼
low-level Fate text (canonical, patchable)
   │  Tokenizer ──────────────► Assembler
   ▼                                              ▼
Script token tree ◄──────────────────────── byte stream (LE u32 words)
   ▲  Disassembler ────────────────────────────┘
   │  Translator (canonical text, labels reindexed, _0 expanded)
```

- **High-level Fate** is a human programming language (functions, `if`/`while`/
  `do-while`, expressions, typed-ish globals). It is *not* round-trippable:
  the lowering is lossy (control flow becomes `jmp_cmp`/`jmp`, temporaries get
  allocator names).
- **Low-level Fate text** is one line per word-instruction, data word, string,
  pointer table, label, or entrypoint. It is the **canonical patchable form**:
  patches (`.diff` files) are unified diffs over this text.
- **Bytes** are the on-disk format documented in
  `research/scripting/02-fate-binary-format.md`.
- The **runtime reuses the same classes** (`FateCompiler`, `Tokenizer`,
  `Assembler`, `Disassembler`, `Translator`) through `ScriptManager` — the
  in-game patch pipeline is not a separate reimplementation
  (`ScriptManager` field initializers + `ScriptPatcher`, source-verified).

Metadata (method names, param names, branch attributes, enums) flows into the
compiler (engine-call resolution), translator (names, comments), tokenizer
(arity, call-index mapping), and disassembler (table of tables probing).
See `research/scripting/04-meta-system.md`.

## 2. The high-level compiler (`FateCompiler`)

### 2.1 API and pipeline

`FateCompiler(meta).compile(includePaths, source, errors)` (source-verified,
full read):

1. **Parse** — ANTLR v4: `FateLexer`/`FateParser` (generated from
   `src/main/antlr/.../FateLexer.g4`, `FateParser.g4`), entry rule `body()`.
   Parse errors surface via the ANTLR error listener into `errors`.
2. **Preprocess** — `FatePreprocessor` (tree visitor) over the main tree:
   - `visitInclude` queues each `#include "file";` for later resolution;
   - `visitFunction` validates each function:
     - the last statement of the block **must be a `return`**
       ("function \"name\" missing return");
     - the function name **must not collide with an opcode mnemonic**
       (`OpType.byName(name) != null` → "function \"name\" is not a legal
       name");
     - parameter names are collected into a `FateFunctionDefinition`.
3. **Resolve includes** — `getFunctionsFromIncludes`: pops the include queue
   (recursively), **only `.fate` files are accepted** (`include.endsWith(".fate")`);
   each is parsed and preprocessed, merging its function definitions into the
   shared map. A `processedIncludes` set makes include cycles safe (a file is
   processed at most once). **Include files contribute only function
   definitions** — any non-function content (statements, globals) in an include
   is discarded (only the main tree is later visited by the lowering visitor).
4. **Lower** — `FateCompilerVisitor(meta, fate, errors, functions)` visits the
   main tree and emits low-level ops into a `FateContext`.
5. **Name and emit** — `fate.updateVariableNames()` (global variables get
   their storage allocation names), `fate.updateLabelNames(labelPrefix)`
   (deterministic label names, prefix used when compiling included fragments),
   `fate.compile()` → the low-level text:

   ```
   entrypoint :ENTRYPOINT_0
   <blank line>
   <flat sequence of low-level lines: labels, ops, data, str, rel>
   ```

   (`FateContext.compile`, source-verified.)

Errors are **accumulated** in the caller-supplied `errors` list rather than
thrown (the CLI logs them after a failed run), though hard failures (bad
include, unknown call) still throw.

### 2.2 Grammar surface

The ANTLR grammar (source-verified, full read of both `.g4` files):

- `body : (entrypoint | global | function | include)*`
- `entrypoint : ENTRYPOINT IDENTIFIER TERM;`
- `global`: `var X = <expression | array | string array>;`
- `function : DEF IDENTIFIER identifier_list block;`
- statements: `declaration`, `assignment`, `postfix` (`x++`),
  `augmented_assignment` (`+=`, `-=`, …), `call`, `return`, `continue`,
  `break`;
- control: `if (expr) block (else if | else)?`, `while (expr) block`,
  `do block while (expr);`;
- operands: `IDENTIFIER` (local/function param), `stor[N]` /
  `stor[N, M]` (other state), `var[N]` / `var[N][M]` (game variables),
  `reg[N]` (registry-ID slot), `id[Pkg.Name]` (registry string ID),
  `array_lookup` for global arrays;
- `call : IDENTIFIER (SCOPE IDENTIFIER)? expression_or_string_list;` — the
  `Scope` form is an **engine call** (`pkg::name`), the bare form is either an
  opcode or a user function.

Full language semantics, lowering rules, and string encoding are in
`research/scripting/01-fate-language.md`.

### 2.3 Lowering (what the visitor does)

- **Entry points** become `entrypoint :LABEL` lines; the first op of the file
  is where entrypoint 0 lands (labels are allocated by the visitor).
- **Globals**: `var X = 123;` → `data` words; `var X = [1,2,3];` → one `data`
  word per element; string arrays → relative pointer table (`rel :label`) +
  `str[...]` data; the global gets the storage slot of the first data word
  (the runtime addresses globals through the entrypoint table's data region —
  see the language doc §2.4).
- **Functions are inlined**: `def fn(a, b)` becomes a label, a prologue that
  `POP`s each parameter in **reverse** order, the body, and the return op;
  the caller emits `PUSH`es of arguments left-to-right, `gosub :fn`, then
  `POP`s of the return values. All `return` statements in a function must have
  the **same number of values** (validated: "return lists must be the same
  size"). Locals are `data` words emitted before the function's label.
- **`if`/`while`/`do-while`** lower to `jmp_cmp` (with the condition operator in
  the header) + `jmp` + labels. There is **no implicit pause** in loop bodies —
  a `while` without a pausing op or pausing engine call runs to completion in
  a single frame (language doc §2.5; runtime loop semantics in
  `02-fate-binary-format.md` §7).
- **Calls**: if the name matches `OpType.byName`, the op is emitted directly;
  `wait` is special-cased (argument copied to a temp first, because the runtime
  decrements its operand in place); `pkg::name` resolves the engine call via
  metadata (name → index + param metadata) and emits `call <index>, ...`;
  anything else is a user function (`gosub` form).
- **Expressions** allocate `_expr_N` temporaries (stor slots) per binary/unary
  op; `&&`/`||` lower to `CMP` with header operators 8/9 (**both sides are
  evaluated — no short-circuit**); `!expr` lowers to `cmp ==, expr, 0, temp`;
  `~expr` to bitwise `not`.

## 3. The disassembler (`Disassembler`)

### 3.1 API

`disassemble(String name, byte[] bytes, List<Integer> extraBranches,
Map<Integer, Integer> tableLengths)` plus an overload with an explicit
`entrypointCount` (the CLI's `-e` flag; the runtime `ScriptManager` uses the
overload with `extraBranches`/`tableLengths` from patch configs,
source-verified).

### 3.2 Pipeline

1. **Entrypoint table inference** (§2 of the binary-format doc): walk leading
   words while the value is a valid code offset; default cap 32
   ("Most have 0x10, some have less, player_combat_script is the only one
   I've seen with 0x20" — source comment).
2. **Branch probing**: from each entrypoint (and each `extraBranches` address,
   which forces decompilation of a branch the heuristic might miss — the
   `branch <hex>` config lines), follow control flow:
   - `parseHeader` predicate (binary-format doc §5): aligned offset ≥ 0x4,
     toolchain table lookup (null → stop), CALL header < 1024, exact arity,
     zero header for non-compare ops.
   - Per-opcode control-flow model in `probeBranch` (binary-format doc §6.1,
     toolchain column): terminals (`JMP`, `JMP_CMP`/`_0` (both branches
     probed), `JMP_TABLE`, `REWIND`, `RETURN`, `DEALLOCATE`, `DEALLOCATE82`,
     `CONSUME`), subroutines (`GOSUB`, `GOSUB_TABLE` — recorded and register
     knowledge cleared), calls (`CALL` — params with metadata `branch`
     attribute of jump/subroutine/fork_jump are probed; an unresolvable branch
     param logs `Skipping CALL … due to unknowable parameter`), forks (`FORK`).
   - **Symbolic register tracking**: each stor slot holds a `Register`
     (exact value / numeric range / unknown); each op updates the model with
     the same formulas the interpreter uses (`mov` copy, `add`/`sub`/`mul`/
     `div` merge with `safeDiv`, `mod` with zero→0, `rand` → range `[0, bound)`,
     `sin_12`/`cos_12` → `(short)(sin/cos * 0x1000)`, `while` → decrement, etc.).
     This is what lets table-length inference and CALL probing work symbolically.
     (The model diverges from the runtime on div-by-0 — binary-format doc §6.1,
     op 33.)
3. **Label generation**: probed targets that are not already entrypoints or
   table entries become `LABEL_` names; labels at the same address share a
   name.
4. **Pointer-table recovery** (`handlePointerTable`/`probeTable`/
   `probeTableOfBranches`/`probeTableOfTables`):
   - explicit length override from `tableLengths` (config `table_length`);
   - register-range: if the index param resolves to a range, length = max;
   - heuristic: extend while entries are consistently signed and running
     address stays on the expected side of the destinations ("only allows all
     positive or all negative pointers (breaks a few tables like in Kazas
     garbage room) but yields much better results overall" — source comment);
   - string tables (CALL params typed `string` in metadata): extend over
     non-op words; a word that parses as an op is accepted only if a `0xa0ff`
     terminator is found within 300 words at its destination;
   - tables that overrun other labelled data are truncated with a warning and
     the orphaned labels are cleaned up.
5. **String recovery**: `0xa0ff`-terminated runs of char words *identified as
   strings* become `LodString` entries; identification is
   metadata/pointer-driven (CALL params typed `string`, pointer-table
   destinations, inline string destinations — the `StringInfo`/`buildStrings`
   mechanism), so unreferenced character data stays `data 0x…` words
   (byte-stable, but with no string rendering). A string whose word range
   crosses a label is split into multiple `str[...<noterm>]` segments (so
   reassembly reproduces the same bytes) (source-verified; experimentally
   confirmed by the M3 audit).
6. **Out-of-range inline rewrite**: an inline pointer whose destination lies
   past the end of the script is rewritten to `0` with a warning
   ("Pointer at 0x%x destination is past the end of the script, replacing
   with 0") — a documented, intended divergence (§8 of the binary-format doc).
7. **Everything else** becomes `data 0x…` words verbatim.

The output is a `Script` token tree (entrypoints, ops with params, labels,
data, strings, pointer tables).

## 4. The translator (`Translator`)

`translate(Script script, Meta meta, boolean stripNames, boolean
stripComments, boolean lineNumbers)` (source-verified, full read):

- **Label reindexing**: all `LABEL_` labels are **sorted by destination
  address** and renumbered `LABEL_0, LABEL_1, …` in that order. This makes the
  text deterministic regardless of probe order (important for stable diffs).
  Non-`LABEL_` names (entrypoints, user labels from the compiler) keep their
  names.
- **`_0` expansion**: `WAIT_CMP_0`/`MOV_0`/`JMP_CMP_0` are emitted in full
  form with the explicit constant 0 (binary-format doc §6.3).
- **`stripNames`**: engine call names are replaced by their **numeric index**
  in the metadata method table — this produces the `call 322, …` spelling
  seen in shipped diffs. (The Patcher's `strip` does the same replacement on
  text; here it happens during emission.)
- **Comments**: subroutine/table metadata is emitted as `;` comments
  (disabled with `stripComments` — the runtime patcher always strips comments,
  §6.1).
- **Line numbers** (optional): each line prefixed with its file offset.
- **One-way param types**: translating a 0x12/0x15/0x16 param **throws**
  (`Param type 0x1x not yet supported`) — such scripts cannot be decompiled
  to text at all (binary-format doc §8).

The runtime patcher calls it with `stripNames=true, stripComments=true,
lineNumbers=false` — i.e. the **stripped canonical form** is what patches
apply against (source-verified, `ScriptPatcher.decompile`).

## 5. The tokenizer (`Tokenizer`)

`tokenize(String name, List<Path> includePaths, String source)` (source-verified,
full read):

- Line grammar (regexes, all case-insensitive):
  - `#include "path";` — inlines another low-level text file (recursion is
    cycle-safe via an `includedScripts` set);
  - `entrypoint :LABEL` — entrypoint table entries (emitted at word offsets
    `i * 4`);
  - `LABEL:` — label definitions (word-boundary aligned);
  - `data 0x…` / `data str[...]` — data words and LodStrings;
  - `rel :label` — pointer table entries (relative word offsets from the
    table start);
  - `<mnemonic> [params]` — instructions (mnemonic lookup in the toolchain
    `OpType` table; unknown → `UnknownOpException`).
- **Parameters** (the full pattern set, source-verified — every pattern in
  `Tokenizer` lines 35–75):
  - numbers: `0x…`/decimal; plain immediates only when the **high byte
    (bits 24–31) is 0**, otherwise auto-expanded to `NEXT_IMMEDIATE`
    (binary-format doc §4.2);
  - operators for compare headers: `<= < == != > >= & !& && ||`
    (all ten are **accepted by the tokenizer**; the translator does **not**
    emit `&&`/`||` — it renders header 8/9 as `Unknown CMP operator N` on
    all compare ops, making decompiled `cmp &&`/`cmp ||` one-way; see the
    binary-format doc §6.2 and §8 item 10);
  - `stor[N]`, `stor[stor[stor[A], B], C]`, `stor[stor[A], B + stor[C]]`,
    `var[N]`, `var[N + stor[M]]`, `var[N][stor[M]]`, `var[N + stor[M]][stor[K]]`,
    `var[N + M]`, `var[N][M]`, `var[N + stor[M]][K]`,
    `inl[:label]`, `inl[0xADDR]`, `inl[:label][stor[M]]`, `inl[A][A][stor[M]]`,
    `inl[A][inl[A + N]]`, `inl[A + inl[A + N]]`, `reg[N]`, `reg[stor[N]]`,
    `id[a.b.c]`, `null`, and the 0x24–0x27 inl-index forms
    (`stor[inl[:x]]`, `stor[inl[:s], inl[:x]]`, `var[inl[:a]]`,
    `var[inl[:a]][inl[:b]]`, `reg[inl[:x]]`, `reg[inl[:s], inl[:x]]`,
    `inl[:a][inl[:b]]`);
  - **missing spellings**: 0x0c, 0x0d, 0x11, 0x12, 0x15, 0x16, 0x17 have no
    pattern (binary-format doc §8 table).
- **`call` handling**: `pkg::name` (CALL_PATTERN) resolves the name against
  metadata: unknown → `UnknownCallException`; the resolved **index** becomes
  the op header; param count/arity is checked against the metadata param list.
  Bare `call N, …` (numeric) is also accepted (the stripped form).
- **Label rebinding pass** (second pass over all ops): label-typed params get
  their relative offsets filled in:
  `rawValues[0] |= (labelAddress - opAddress) / 4 & 0xffff` for
  INLINE_1/2/TABLE_1/TABLE_3; label rebinding for INLINE_TABLE_2/INLINE_3/
  INLINE_TABLE_4 **throws** `Need to implement label bindings` (they can only
  be spelled with absolute addresses, which the translator emits — but the
  translator's 0x0c/0x17 spellings are not re-parsable anyway).
- **`modifyOp`**: after parsing, `WAIT_CMP`/`JMP_CMP`/`MOV` whose first param
  **resolves to constant 0** are converted to the `_0` opcodes, dropping the
  param — re-creating the compact retail encoding (binary-format doc §6.3).

## 6. The assembler (`Assembler`)

`assemble(Script script)` → `int[]` (the runtime and CLI write it as
little-endian words; `Shell.intsToBytes` uses
`ByteBuffer.order(LITTLE_ENDIAN).asIntBuffer()`, source-verified):

- Output word `i` per script entry:
  - `Entrypoint` → the **byte address** of the referenced label
    (`findEntrypointAddress`);
  - `Data` → the raw word;
  - `LodString` → two chars per word (char *i* in the low 16 bits, char *i+1*
    in the high 16 bits), terminator included unless `<noterm>`;
  - `PointerTable` → `(destAddress - tableStartAddress) / 4` per entry
    (signed word offsets — the exact inverse of the disassembler's
    `tableAddress + wordAt * 4`);
  - `Op` → `opcode | paramCount << 8 | header << 16`, followed by each param's
    raw words (after label rebinding and `modifyOp` compaction).
- There is no relocation pass beyond label rebinding: the text must already
  reference labels, not absolute positions (absolute `inl[0xADDR]` spellings
  work only when the layout is unchanged).

## 7. The patch layer (`Patcher`)

All methods static; the diff engine is `java-diff-utils` (`difflib`)
(source-verified, full read).

### 7.1 `strip` — canonicalization

`strip(meta, lines, stripCalls, stripEolComments, stripFullLineComments,
stripBlankLines)` applies, in order:

1. **EOL comments**: cut at the first `;` (only if present past column 0);
2. **Full-line comments**: lines starting with `;` become empty;
3. **Call naming**: any line matching `^call\s+(.+?)\s*(?:,.*|$)` whose call
   name equals a metadata method name is rewritten to the method's **numeric
   index** (position in the 1024-row method table);
4. **Trim + blank removal**: every line is trimmed; blank lines are dropped.

The internal `strip(meta, lines)` used for diff generation enables **all four**
transformations unconditionally. This canonical form is the coordinate system
of every `.diff` file.

### 7.2 `generatePatch` / `applyPatch` / `undoPatch`

- `generatePatch(meta, originalFile, modifiedFile)`: strip **both** inputs
  (all four transformations), then `DiffUtils.diff` + unified diff with
  **3 lines of context**, headers `--- original` / `+++ modified`, every line
  terminated with `\n`.
- `applyPatch(originalLines, patchLines)`: parse the unified diff and apply it
  to the (unstripped) lines. Note the patch's line coordinates are in the
  **stripped** coordinate system; the standard usage (both standalone and
  in-game) always applies against freshly stripped decompilation, so the
  coordinates match (source-verified from both call sites, §8).
- `undoPatch`: the exact inverse (`DiffUtils.unpatch`).
- Failure to apply (context mismatch) → `PatchFailedException`.

### 7.3 Shipped patch artifacts (observed)

- `patches/scripts.csv` — 353 rows, all in the new 4-column format
  `type,sourceFile,patchFile,description`, e.g.
  `diff,SECT/DRGN1.BIN/20,scripts/DRGN1/20.diff,"Lizardman deallocate leaked
  script state"` and
  `replacement,player_combat_script,scripts/player_combat_script.txt,"Major
  rewrites to support modding"`. Note script source paths can be **nested**
  (`SECT/DRGN23.BIN/348/1`, `/348/2`, `/348/3` are three distinct files —
  the DRGN container's virtual tree, M2 doc 02).
- Diffs are unified diffs over stripped decompilation with **numeric call
  indices** and reindexed `LABEL_n` names, e.g. `patches/scripts/DRGN23/348/1.diff`:
  `+jmp_cmp ==, stor[8], 0xf1, inl[:LABEL_30]` inserted after a `0xf0` check
  (the Flanvel teleporter fix adds a second destination branch).
- Per-patch configs: `<patch>.config.csv` (e.g. `DRGN22/798/1.config.csv`)
  with `table_length, <hex address>, <decimal count>` rows (4 rows in that
  file: `17cc=1`, `1e60=8`, `6594=35`, `6620=35`) — these pin the disassembler's
  table-length heuristic so decompilation (and therefore patch coordinates)
  are deterministic for that script.

## 8. The runtime patch pipeline (`ScriptPatcher`)

Wiring (source-verified, full reads of `ScriptPatcher`, `ScriptPatchList`,
`PatchType`, `ScriptManager`):

- `ScriptPatcher(patchesDir, filesDir, cacheDir, backupsDir)`; include dirs
  are `libs` and `scripts` (resolved against the patch base path) — the same
  dirs the standalone tokenizer uses for `#include`.
- The decompilation/translation/assembly primitives come from the
  **ScriptManager** (singletons): `SCRIPTS.disassemble(...)`,
  `SCRIPTS.meta()`, `SCRIPTS.assemble(...)` — with the **local**
  `patches/meta` metadata (doc 04), so in-game patching is fully offline.

### 8.1 Patch list (`scripts.csv`)

`ScriptPatchList.load(filesDir, csv)` per row (source-verified):

- **Legacy 2-column** row (`source,patch`): detected when
  `filesDir.resolve(row[0])` exists → `ScriptPatch(DIFF, source, patch)`.
- **New-format row** (`type,source,patch[,description]`):
  `ScriptPatch(Type.valueOf(row[0].toUpperCase()), row[1], row[2])` — types
  `DIFF` and `REPLACEMENT` (`PatchType`).
- The list is a **map keyed by source file** — one patch entry per script
  (multiple fixes for one script are separate files in the nested tree, §7.3,
  or multiple `.diff` files listed under different source paths).
- Missing/invalid CSV → logged warning + **empty list** (no patches applied,
  game boots unpatched).

### 8.2 The apply state machine (`apply()`)

1. Load the **cache** list from `cacheDir/scripts.csv` (a mirror of the patch
   dir from the last successful run).
2. For each current patch: `needsUpdate` = no cached entry, or **CRC32
   mismatch** of the patch file, its config file (null when absent), or any
   transitively `#include`d file (includes resolved against `libs`/`scripts`,
   CRC'd recursively) between cache and patches dir.
3. Changed patches are applied:
   - **Backup**: the untouched file from `filesDir` is copied to `backupsDir`
     (skipped if a backup already exists; for `REPLACEMENT` of a missing file
     the backup is skipped).
   - **`DIFF`**:
     a. read the patch text + its `<name>.config.csv` (`branch <hex>` →
        extra branch addresses; `table_length <hex> <count>` → table pins;
        unknown key → `IllegalArgumentException`);
     b. **decompile the backup** (the pristine file) with those branch/table
        hints → stripped canonical text (`stripNames=true,
        stripComments=true`);
     c. `Patcher.applyPatch(strippedLines, patchLines)`;
     d. `SCRIPTS.assemble` the patched text → write bytes over the file in
        `filesDir`.
   - **`REPLACEMENT`**: read the full script text, assemble, write.
   - Failure to apply the diff → `PatchFailedException` (propagates — the
     unpacker aborts); failure to assemble → wrapped `RuntimeException`.
4. **Restore**: cached patches that disappeared from the list get their
   backups moved back over the current file (empty backup dirs pruned).
5. **Cache update**: if anything changed, `cacheDir` is **deleted and the
   whole patches dir copied over it** — the cache is a structural mirror
   (delete+copy, not incremental).

Consequences (source-verified):

- Patching is **idempotent**: it always starts from the backup (pristine),
  never from the already-patched file; re-running after a no-op change
  (CRCs equal) skips the work entirely.
- The **backup tree is the only pristine copy** after the first run;
  `restoreFile` depends on it.
- Determinism of the patch coordinate system depends on: label reindexing
  (deterministic, §4), table-length pins (config), branch hints (config),
  and the disassembler's heuristics otherwise. That is why the shipped
  configs exist for the hard scripts.
- Assembly of the patched text can fail for the one-way param-type/header
  reasons in the binary-format doc §8 (e.g. a patch touching a script
  containing 0x0c/0x0d/0x11/0x17 or **0x24–0x27** params, 0x12/0x15/0x16
  anywhere in it, or a compare op with **header 8/9**) — an observed
  constraint on what can be patched today (verified by the M3 audit: none of
  the 328 shipped `.diff` files contains any of these spellings — they would
  have hit the same wall).

## 9. The standalone CLI (`Shell`)

`org.legendofdragoon.scripting.Shell` (source-verified, full read). Metadata
comes from the **remote** service `https://legendofdragoon.org/scmeta/` with a
local cache in `./cache` (doc 04); commands:

| command | flags | action |
|---|---|---|
| `v`/`versions` | — | list available meta versions from the server |
| `d`/`decompile` | `-i in`, `-o out`, `-v version` (default `snapshot`), `-w workdir`, `-b branch` (hex, repeatable), `-t table-length` (`addr=count`, hex=decimal, repeatable), `-C` no comments, `-N` no names, `-l` line numbers, `-e entrypoint-count` | bytes → stripped/pretty low-level text (Disassembler + Translator) |
| `c`/`compile` | `-i`, `-o`, `-v`, `-w`, `-L libs` (repeatable) | high-level Fate → low-level Fate text (FateCompiler) |
| `a`/`assemble` | `-i`, `-o`, `-v`, `-w`, `-L libs` | low-level text → bytes (Tokenizer + Assembler) |
| `g`/`genpatch` | `-a original`, `-b modified`, `-o out`, `-v`, `-w` | unified diff of two stripped decompilations |
| `p`/`patch` | `-a original`, `-b patch`, `-o out` | apply a diff to a decompilation (text) |
| `u`/`undopatch` | `-a patched`, `-b patch`, `-o out` | inverse of the above |
| `s`/`strip` | `-i`, `-o`, `-v`, `-C` ignore calls, `-E` ignore EOL comments, `-F` ignore full-line comments, `-B` ignore blank lines (all off by default → full strip) | canonicalize a decompilation |

- Default output for `d`/`c` is `<input>.txt`.
- `decompile` writes the Translator's text with the flag-driven
  `stripNames`/`stripComments` (the CLI's `d` uses the raw flag values, unlike
  the runtime patcher which forces strip-names + strip-comments).
- Exit codes: 0 on success, 1 on parse/usage error or missing input.
- The CLI's two usage footers are cosmetically stale (source-verified,
  `Shell.java`): the **no-args** footer (`Shell.java:50`) lists
  `[v]ersions, [d]ecompile, [c]ompile, [g]enpatch, [a]pplypatch, [u]ndopatch,
  [s]trip` — where "applypatch" is not a real command (the real alias is
  `p`/`patch`); the **switch-default** footer (`Shell.java:267`) lists
  `[v]ersions, [d]ecompile, [c]ompile, [a]ssemble, [g]enpatch, [p]atch,
  [u]ndopatch` — missing `s`/`strip`.

## 10. Round-trip and patching guarantees (summary)

Byte-stable round trip (binary → text → binary) holds when:

1. the entrypoint count inference matches the original (or is pinned);
2. no inline pointer is out of range (the disassembler rewrites those to 0);
3. no param uses a one-way/throwing type: 0x0c, 0x0d, 0x11, 0x17 → reparse
   failure; **0x24–0x27 → reparse failure** (the translator emits the
   absolute inline forms `stor/var/reg/inl[inl[0xADDR]]` for which the
   tokenizer has no pattern — label forms only; experimentally confirmed by
   the M3 audit); 0x12/0x15/0x16 → decompile failure; 0x13 → silently
   re-emitted as 0x09, changing the type byte but not the value; 0x21 → the
   registry-ID must contain a colon to reparse;
4. no compare op (CMP/WAIT_CMP/JMP_CMP and the `_0` families) carries header
   8/9 (`&&`/`||`) — the translator renders it as `Unknown CMP operator N`
   even on CMP itself, so the reassembly fails (experimentally confirmed by
   the M3 audit);
5. jump-table lengths are stable (heuristics are deterministic for a fixed
   input, but ambiguous scripts need `table_length` pins — shipped configs);
6. labels are only referenced by name (the tokenizer rebinds all inl offsets
   from the final layout).

Patch guarantees:

- A patch is a diff over **stripped, label-reindexed, call-indexed** text;
  the coordinates are stable across decompiles *of the same bytes* (label
  reindexing is order-deterministic; table pins remove the main heuristic
  freedom).
- Applying a patch to the canonical decompilation of the pristine file,
  then assembling, is the **only supported flow** (both the CLI workflow and
  the runtime pipeline); applying to a non-canonical text is unsupported
  (coordinates may be off).
- `undopatch` is exact (difflib inverse) when the patch applied cleanly.
- The in-game pipeline is idempotent and self-healing (backup → restore on
  removal), with CRC-based change detection that follows `#include` closure.

## 11. Evidence index

| Claim area | Source |
|---|---|
| Compiler pipeline | `Script-Recompiler/.../compiler/FateCompiler.java` (full), `FatePreprocessor.java` (full), `FateContext.compile` (lines 124–137), ANTLR `.g4` files (full) |
| Lowering | `FateCompilerVisitor.java` (full), language doc 01 |
| Disassembler | `Disassembler.java` (full read in M3 investigation: `disassemble`, `getEntrypoints`, `parseHeader`, `probeBranch`, `probeTable*`, `handlePointerTable`, register model) |
| Translator | `Translator.java` (full: label sort/reindex, `_0` expansion, stripNames, comments, param emission switch) |
| Tokenizer | `Tokenizer.java` (full: patterns lines 35–75, `tokenize`, `parseParam` family, label rebinding lines 186–199, `modifyOp` via `OpType`) |
| Assembler | `Assembler.java` (full: entry dispatch, LodString packing, PointerTable offsets, op encoding), `Shell.intsToBytes` |
| Patcher | `Patcher.java` (full: strip pipeline, `replaceCalls`, difflib usage) |
| CLI | `Shell.java` (full: all commands/flags) |
| Runtime pipeline | `Severed-Chains/.../unpacker/scripts/ScriptPatcher.java` (full), `ScriptPatchList.java` (full), `PatchType.java`, `ScriptManager` (fields + `meta()`/`disassemble`/`assemble`), `patches/scripts.csv` (353 rows), `patches/scripts/DRGN*/**` (diffs + 10 config CSVs observed) |

Pins: `Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511`,
`Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade`.

## 12. Open items

- Q-16 (`strip` semantics) — **closed**: §7.1 documents the exact four
  transformations; the standalone `s` command and the Patcher's internal strip
  are the same function with flag controls.
- Q-18 (round-trip stability conditions) — **closed** with the formal
  conditions in §10 (and binary-format doc §8): the remaining unknown is
  purely asset-side (which retail scripts hit the one-way types — Q-60).
- Q-62 (new): the remote `scmeta` service content/versioning cannot be
  inspected offline; only the URL, cache behavior, and `snapshot` semantics
  are source-verified (doc 04). Its live state is external evidence and out of
  the M3 evidence base.
