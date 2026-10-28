# Script-Recompiler Repository Research

**Repository:** `~/Documents/GitHub/Script-Recompiler`
(task referred to it as "Script-Decompiler"; the actual directory is
`Script-Recompiler`)
**Commit:** `3433e6f74bb6d562aeff0490d11e8937da437511` (branch `main`,
2026-06-21, "Find functions from all includes")
**Artifact:** `org.legendofdragoon:script-recompiler:0.7.11` (matches the
version consumed by Severed-Chains) — verified.
**License:** AGPL-3.0 (verified).
**Read-only reference — do not modify.**

> Phase 1 of the standalone `fate` toolchain (`fate compile` first) deepened this into
> `docs/research/fate-toolchain-phase1.md`: CLI contract, compile pipeline, assembly text format,
> fixture and oracle inventory, and the decisions still open. Where the two documents disagree,
> the newer file wins — §1 and §6 below were corrected against it.

Resolves the Phase 0 open question on the fate pipeline internals. All
behavior below is **verified** from source unless marked otherwise.

## 1. What it is

A ~6,000-LOC Java library + CLI (`Shell`, main class
`org.legendofdragoon.scripting.Shell`) for working with the Legend of
Dragoon **story/combat script VM** bytecode: **disassembler** (raw word
array → token tree), **assembler** (token tree → raw word array),
**text translator** (token tree ↔ human text format), **unified-diff
patcher** (text-level), and a **fate compiler** (a high-level ANTLR-based
language → opcodes). It is used in two ways by Severed-Chains:

1. **At runtime**, via `legend.game.scripting.ScriptManager`
   (`GameEngine.SCRIPTS`): the runtime script VM keeps a `Disassembler`,
   `Assembler`, `Tokenizer`, `FateCompiler`, and `Meta` for on-demand
   disassembly (debugger, live script viewer) and reassembly (patching,
   mod-provided script text).
2. **At unpack time**, via `legend.game.unpacker.scripts.ScriptPatcher`
   (see §5) which applies mods'/retail script patches.

CLI commands (verified from `Shell.main`): `versions` (v), `genpatch` (g), `patch` (p, listed as
`applypatch` in the no-argument help), `undopatch` (u), `strip` (s) — these five are dispatched
before option parsing and exit 0 once they run — then `decompile` (d), `compile` (c) and
`assemble` (a) through a commons-cli `Options` block, with a `default` branch that prints the
command list and exits 1. The zero-argument help and the unknown-command help list **different**
sets, and neither is authoritative (`assemble` is missing from the first, `strip` from the
second). Non-zero exits are limited to: no arguments, option parse failure, missing input file,
malformed `-t`, unknown command word — **a compile that logs errors still exits 0**, and it writes
its output file whenever `compile` returned (`Shell.java:221-247`). The CLI's meta source is the **remote**
`https://legendofdragoon.org/scmeta/` (cached to `./cache`, resolved against the process CWD, not
against `-w`); the *game's*
`ScriptManager` uses the **local** bundled meta `patches/meta/*.csv`
(verified: `new MetaManager(null, this.patchDir).loadMeta("meta")` — URI
null = local). Both share the same CSV schema.

Dependencies: log4j, jansi, opencsv 5.9 (meta CSVs), commons-cli 1.6 (CLI),
java-diff-utils 4.12 (unified diff), ANTLR 4.13.2 (fate grammar).

## 2. Binary format (verified)

Scripts are arrays of **little-endian 32-bit words** (`State` over `byte[]`,
word = 4 bytes). Encodings (from `Assembler.assemble`):

| Entry | Encoding |
|---|---|
| Op | `opcode \| (paramCount << 8) \| (headerParam << 16)` then params, each param occupying `width` words (16-bit or 32-bit values packed) |
| LodString | UTF-16, **2 chars per word** (low char \| high char << 16), length = word count |
| PointerTable | N words, each = `(targetAddr - tableAddr) / 4` (relative, word units) |
| Entrypoint | 1 word = **absolute target address** |
| Data | raw word(s) |

The runtime op set is `legend.game.scripting.OpType` (108 ops, indices
0..103 used — `ScriptManager.scriptLog = boolean[108]`); the *tool*
`org.legendofdragoon.scripting.OpType` mirrors it with **name + param
metadata** (e.g. `WAIT(2, "wait", OpParam.in("frames"))`, `FORK(86,
"fork", ..., "stor[32] value")`). The verified catalog includes control
flow (`YIELD`, `REWIND`, `WAIT`, `WAIT_CMP[_0]`, `JMP`, `JMP_CMP`,
`JMP_TABLE`, `GOSUB[_TABLE]`, `RETURN`, `WHILE`, `FORK`, `FORK_REENTER`,
`CONSUME`, `DEALLOCATE*`), data ops (`MOV`, `MEMCPY`, `AND/OR/XOR/ANDOR`,
`NOT`, `SHL/SHR`, `ADD/SUB/SUB_REV`, `INCR/DECR`, `NEG/ABS`, `MUL/DIV/MOD`
(+ `_REV` variants), `SQRT`, `SIN_12/COS_12/ATAN2_12`, `RAND`), debug
(`NOOP_96..98`), and registry/flag ops (see the runtime `OpType` in the
reference repo for the full set incl. `SET_FLAG`-style registry ops —
cross-checked with the Phase 0 runtime VM research).

**Not a VM in this repo**: execution semantics live in the game
(`ScriptState`/`RunningScript`, documented in the Phase 0
source-architecture.md). This repo is the *tooling* half; the two share the
`OpType` definition (duplicated enum — a Java wart; the native port keeps
one definition).

## 3. Disassembler (verified)

`Disassembler.disassemble(name, bytes, extraBranches, tableLengths,
entrypointCount)`:
- Entry points: from a known count (retail scripts have 0..N entrypoints at
  word 0+) — the game passes per-script entrypoint counts (the runtime
  `ScriptFile` knows them from the MRG context; the tool takes them as a
  parameter).
- **Branch probing**: recursive forward analysis from each entrypoint with
  a per-branch **register-state tracker** (`ScriptRegisters`,
  `resolution/` package: `Register`, `RegisterSet`, `RegisterValueType`,
  `ResolvedValue`) — this is how param *types* (value vs. pointer vs.
  registry id vs. stor slot) get resolved: the disassembler tracks which
  registers hold what (e.g. a `mov` from a known registry-id source marks
  the dest as a registry id), so reassembly can round-trip typed params.
- **PointerTable detection + overrun handling** (jump tables that collide
  with data/labels get truncated with a warning — verified logic at the
  top of `assemble`-adjacent code).
- `extraBranches` + `tableLengths`: per-script hints (from patch config
  CSVs, §5) for branches the generic probing would miss.
- Output `Script` token tree: `entries[]` (per word index: `Entrypoint` |
  `Op(params)` | `LodString` | `Data` | `PointerTable`), `labels` (address →
  label names, auto `LABEL_` names reindexed in address order by the
  translator), `branches`, `subs`/`subTables`/`forkJumps` markers (emitted
  as `; SUBROUTINE` etc. comments), `warnings`, `buildStrings` (deferred
  string materialization), `labelUsageCount`.

## 4. Assembler + Translator (verified)

- **`Assembler.assemble(Script)` → `int[]`**: exact inverse of §2's
  encodings. Label refs in pointer tables resolve via `findLabelAddress`.
  LodString words, op header word, param widths — as above.
- **`Translator.translate(script, meta, stripNames, stripComments,
  lineNumbers)` → text**: one entry per line, human-readable:
  - ops rendered as `name arg1, arg2, ... ;` with **meta names** for
    registry ops (the meta CSV maps op param slots → named registry ids /
    enums / game vars — e.g. `set_reg lod:character_ids, p0` style; the
    meta files `descriptions.csv`, `params.csv`, `enums.csv` + per-enum
    class CSVs are the "function reference" for the script language).
  - labels rendered as `; :LABEL_12` lines; `reindexedLabels` stable
    ordering; comments/subroutines/fork markers as `;` lines.
  - `stripNames`/`stripComments` options produce the **canonical form**
    used for patching (only op lines, minimal decoration — the form the
    bundled `scripts.csv` diffs are written against).
- **`Tokenizer`**: parses the text format back to a `Script` (used by the
  game for mod-supplied script text and by the fate compiler's output
  path). The text format is thus a stable interchange: bytes ↔ Script ↔
  text.

## 5. Patching (verified)

`Patcher` (library): unified diff (java-diff-utils) over **stripped
disassembly lines**: `generatePatch(orig, mod)` (3 context lines),
`applyPatch(lines, patch)`, `undoPatch(patched, patch)`, `strip(meta,
file, stripCalls, eolComments, fullLineComments, blankLines)` — with
**call normalization**: `call <numeric index>` ↔ `call <function name>`
via meta (`replaceCalls`) so diffs are resilient to function reordering.

**Integration in Severed-Chains** (verified,
`legend/game/unpacker/scripts/` + `GameEngine.start`):
- After `Unpacker.unpack()`: `new ScriptPatcher("./patches", "./files",
  "./files/patches/cache", "./files/patches/backups").apply()`.
- `./patches` is a **directory shipped with the build** (built from the
  repo's `patches/` dir via build.gradle `into("patches") { from 'patches'
  }`):
  - `patches/scripts.csv` — the **patch list**: rows
    `<type>,<scriptPath>,<patchFile>,<description>` with `type` =
    `diff` | `replacement` (legacy 2-column rows without type = `diff`).
    ~hundreds of entries: per-DRGN-N per-script bug fixes (finisher
    crashes, dart checks, hardcoded char counts...) plus two **big
    rewrites**: `replacement,player_combat_script,scripts/
    player_combat_script.txt,"Major rewrites to support modding"` and
    `replacement,throw_item,scripts/throw_item.txt`.
  - `patches/scripts/*.diff` — unified diffs in canonical text form;
    `*.txt` — full **fate-language** replacement scripts (e.g.
    `player_combat_script.txt` starts with `entrypoint :ENTRYPOINT_0_MAIN`
    ... — fate format, §6); optional sibling `<name>.diff.config.csv`
    provides per-patch `extraBranches`/`tableLengths` hints.
  - `patches/libs/` — include files (fate `include` paths);
    `patches/meta/` — **local meta CSVs** (descriptions/params/enums +
    per-enum-class CSVs incl. `legend.game.combat.bent.
    BattleEntityStat.csv`, `...BattleSoundUsage.csv`) — the bundled
    version of the remote scmeta.
  - `patches/replacements/` — binary file replacements (e.g.
    `chester.tim` — a fixed retail texture).
- **Apply algorithm** (`ScriptPatcher.apply`):
  1. Load patch list; also load the *cache* list
     (`files/patches/cache/scripts.csv`).
  2. For each patch: `needsUpdate` = patch file or **any included file**
     changed since cache (CRC32 of the patch + all resolved includes,
     include dirs = `libs`, `scripts`); if changed → back up the original
     to `files/patches/backups/` (once), then:
     - **DIFF**: read the *backup* (unpatched) bytes → `SCRIPTS.
       disassemble` → `translate(stripNames=true, stripComments=true)` →
       `Patcher.applyPatch` → `SCRIPTS.assemble` → write patched bytes to
       `files/`.
     - **REPLACEMENT**: `SCRIPTS.assemble(name, fateText)` — the fate text
       is **compiled** (via `FateCompiler`, §6) and written.
  3. **Deleted patches are restored** (cache has an entry the new list
     doesn't → move backup back over the patched file, prune empty backup
     dirs).
  4. If anything changed: `files/patches/cache/` is deleted and
     re-copied from `./patches` (so the cache mirrors the applied state).
- **Important consequence (verified):** patching is **idempotent +
  reversible** and always re-patches from the *unpatched backup* — the
  game's script tree in `files/` is a *derived* artifact. The native port
  must preserve: backups, cache+restore-on-change, CRC-of-includes
  invalidation, and the "patch list is data shipped with the build/mods"
  model (mods carry their own `scripts.csv` — the reference build merges
  the shipped `patches/` dir; verify how multiple mods' patch dirs are
  merged — **open question Q-SCR-2**).

## 6. Fate compiler (verified — structure)

- ANTLR 4.13.2 grammars (`FateLexer.g4`/`FateParser.g4`): a small C-like
  language. Top level is `body : (entrypoint | global | function | include)*` — **no statements
  at top level**. Inside a function: declarations, assignment, postfix `++`/`--`, augmented
  assignment, calls, `return`, `break`, `continue`, and control `if`/`else`, `while`,
  `do { } while (…);`. No `for`. Operators: `==`, `!=`, `>`, `<`, `>=`, `<=`, `&&`, `||`, `!`,
  `+ - * / % & | ^ ~`, `<<`, `>>`, and compound assigns including `&&=`, `||=`, `~=`.
  Corrections to the first pass of this research: `true`/`false` are lexer keywords that **no
  parser rule references**, so `if (x == true)` is a parse error (do not implement them); and
  `:NAME` labels are **not** a Fate statement — labels belong to the assembly text format, not to
  the compiler's input. Two lexer hazards carry into any reimplementation: `WHITESPACE : [ \n]`
  does not skip tab or carriage return, and `NUMBER` accepts only lowercase hex (`0xFF` is a
  syntax error in source, though the assembly-text number pattern is case-insensitive).
  Full detail: `docs/research/fate-toolchain-phase1.md` §6.
- `FateCompiler` (with `FateContext`, `FateScope`, `FatePreprocessor`
  (includes), `FateFunction(Definition/Ref)`, `FateEnum`, `FateGameVar(
  Array)`, `FateLabel(Ref)`, `FateReg/RegId/Stor`, `FateValue(
  List)/Immediate`, `FateArrayVariable`, `FateLoop`, `FateCompilerVisitor`
  (ANTLR visitor → op emission)): compiles fate text → the same op stream
  the runtime executes. This is how the "Major rewrites to support
  modding" combat scripts and mod story patches are authored at a higher
  level than raw op text.
- The compiler is **not a separate tool** at runtime — `ScriptManager.
  assemble(name, text)` routes through the tokenizer/compiler (verified
  the `FateCompiler` + `Tokenizer` fields and their use).

## 7. Reusable tooling for the native port

Everything here is **portable, dependency-light logic** — a clean
candidate for the native `fate` CLI (roadmap M12) and an in-engine
service:

| Java piece | Native analog | Notes |
|---|---|---|
| `State`/word access | `psx::script::WordStream` (little-endian u32 reader/writer) | trivial |
| `tokens/*` (`Script`, `Op`, `Param`, `LodString`, `Data`, `PointerTable`, `Entrypoint`) | `psx::script` token tree (value types) | param types: value/ptr/regid/stor + width |
| `resolution/*` register tracking | same algorithm, ported | the hard part of disassembly — golden-test against Java output |
| `Disassembler`/`Assembler` | port 1:1; **byte-exact round-trip requirement** (disassemble→assemble == identity on all retail scripts) | |
| `Translator`/`Tokenizer` | canonical text codec (the patch format!) | text format is a **compat contract** — diff files must still apply |
| `Patcher` (unified diff) | implement unified diff gen/apply/undo in-repo (small; java-diff-utils semantics: 3 context lines, hunk application with fuzz) | new small in-repo component (no new dep) |
| `meta/*` (CSV) | same CSV schema, bundled under `patches/meta/` | keep the files verbatim (they're data, AGPL — fine, we're AGPL) |
| `FateCompiler` + grammars | ANTLR4 runtime is a Java lib — **native fate compiler options** (Q-SCR-1): (a) ANTLR4 C++ runtime (LGPLv2.1+/BSD — would be a new dependency requiring approval), (b) hand-rolled recursive-descent parser for the (small, known) fate grammar, (c) keep fate *text* as the mod-facing format and compile it with the Java tool at mod-build time (no). **Recommendation: (b)** — the grammar is bounded (one lexer + one parser, ~150 terminal rules), and the native port can validate against Java-compiled outputs (golden tests). | decision needed — see Q-SCR-1 |
| `Shell` CLI | `fate` CLI (versions/decompile/compile/genpatch/applypatch/undopatch/strip) | M12 |

**Determinism note:** disassembly is deterministic (no randomness); the
translator's label reindexing is order-stable (address-sorted). Round-trip
byte-identity is the acceptance bar (testing.md).

## 8. Open questions

- **Q-SCR-1** (decision): native fate compiler — ANTLR4-C++ (new dep,
  needs approval) vs. hand-rolled parser (recommended).
- **Q-SCR-2:** how do *multiple mods'* `scripts.csv`/patch files merge in
  the reference build (the shipped `patches/` is a single merged dir — is
  it built from all mods' patch contributions, or is only `lod`'s shipped
  and mods add at runtime via a different path)? Check the build pipeline /
  mod JAR layout (the bundled `turnorder` mod JAR) — verify before M10.
- **Q-SCR-3:** the per-script entrypoint counts the runtime passes to
  `disassemble` — source is `ScriptFile` (MRG context); confirm the exact
  table during the M6/M8 port.
