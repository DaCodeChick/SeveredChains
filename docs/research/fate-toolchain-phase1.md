# Fate Toolchain — Phase 1: Reference Analysis (`fate compile`)

**Status: Phase 1 analysis complete, decisions answered, F0 (the oracle) executed, and the F1
native design delivered — all on 2026-10-27.** No production C code and no CMake target exist
yet; what exists is the reproducible oracle harness (`tools/fate_oracle.py`), 17 committed
goldens, and the implementation contract (`docs/research/fate-compile-design.md`). The next step
is implementation (F1 code), which is deliberately deferred to a separate work session.

**Date:** 2026-10-27. **Host:** macOS 25.6.0, Apple M4 Pro, arm64.
**Compilers present:** Apple clang 21 (default `cc`), Homebrew clang 23.1.4 (`/opt/homebrew/opt/llvm/bin/clang`).
**Java tooling present:** Gradle 9.8.2, JDK 27. No Gradle dependency cache, no
`Script-Recompiler/build/` directory.

**Reference:** `~/Documents/GitHub/Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511`
(`main`, "Find functions from all includes"), artifact `org.legendofdragoon:script-recompiler:0.7.11`,
AGPL-3.0. **Read-only — not modified by this analysis** (all inspection was `cat`/`grep`/`head`,
plus reads of `~/Documents/GitHub/lloyd_mod`, also read-only).

**Evidence legend** — every claim below is tagged:
`V` verified from reference source or reference data · `A` assumption, must be tested ·
`U` unknown, documented gap (not guessed).

---

## 0. Decisions before Phase 2 — all answered 2026-10-27

| | Question | Answer | Evidence / note |
|---|---|---|---|
| **D1** | Where does `fate` live? | **(a) root `fate/` subproject** | mirrors `psx/`; both gates extended to cover it before any C file lands (§12 consequences) |
| **D2** | C standard | **project default stays C17; the `fate` target is C23**, with a configure-time probe, no silent downgrade | probe design unchanged from below |
| **D3** | Where do goldens come from? | **(A) scratch-copy Java oracle**, built outside both read-only repos | built and run; 17 goldens generated (§12) |
| **D4** | Exit codes | **deviate deliberately**: `0` clean, `1` usage, `2` I/O, `3` compiled-with-diagnostics | reference behaviour below re-measured against a real run; the deviation is now justified by evidence, not by reading alone |
| **D5** | Identifier style | **psx-style snake_case, no `fate_` prefix** | supersedes the PascalCase recommendation below; `check_naming.py` passes snake_case automatically, and `fate` is a host-side tool, not a C ABI |
| — | May AGPL-derived goldens be committed? | **yes** | owner ruling, same date; retail-derived `.txt` disassembly stays out of the repo and in a local lane |
| **R1** | The six `parity: decide` cases (p8 `0xFF`, p9 tab, p10 `true`, p11 unknown scoped call, p12 `for`, p13 malformed include) | **match the reference's observable behaviour as closely as possible** | owner ruling 2026-10-27: "stay as much as possible to Script-Recompiler's behavior". The committed goldens are normative for both emitted text and diagnostic text. The single deliberate deviation is p10's `NullPointerException`, which is a reference defect and is not reproduced (the C tool reports `unimplemented op true`, writes no output, and exits 3). The approved 0/1/2/3 exit-code contract (D4) is **not** overridden by this ruling: exit 3 corresponds to the reference's internal `errors` list being non-empty, and the per-tab lexer messages on p9 are not `errors`-list entries, so p9 exits 0 like the reference |

The subsections below keep the original analysis because two of the answers changed it: D4's
evidence was incomplete (see the correction) and D5's recommendation was reversed on the merits.

### D1 — Where does `fate` live, and does the format gate see it?

| Option | Format gate coverage | Notes |
|---|---|---|
| **(a) `fate/` root subproject** (recommended) | needs `SCAN_ROOTS += "fate"` | mirrors `psx/`: self-contained, own `CMakeLists.txt`, own tests; does not read as "engine code" |
| (b) `tools/fate/` | needs `SCAN_ROOTS += "tools"` | `tools/` currently holds repo-lint Python only |
| (c) `src/tools/fate/` | already covered | but sits inside the engine tree, which contradicts "not a reusable engine library" |

Facts that drive this (`V`): `tools/check_format.py` uses `SCAN_ROOTS = ("src", "tests", "psx")`;
`tools/check_naming.py` scans from `.` but only `.h/.hpp/.cpp/.cc` — **`.c` files are not scanned
today** in either gate. Whichever option is chosen, both gates need a one-line extension plus
`.c`/`.h` inclusion, otherwise the first C tool in the repo lands outside the mechanical checks
the rest of the tree is held to.

### D2 — C standard: per-target C23, project default stays C17

`CMakeLists.txt` sets `CXX_STANDARD 23` / `C_STANDARD 17` project-wide with `C_EXTENSIONS OFF`.
Proposal: keep the project default at C17 and give the `fate` target `C_STANDARD 23` explicitly,
with a configure-time probe (same pattern as the existing exception probes) that reports
"toolchain cannot do C23" instead of silently downgrading. Verified locally (`V`): clang 23.1.4
accepts `-std=c23` and compiles a C23 `auto` declaration. Unverified (`U`): GCC 13 (`-std=c2x`
exists, `-std=c23` naming and feature coverage untested), MSVC (no C23 mode; there is also no C
CI leg today, so this costs nothing now and everything later).

### D3 — Where do the differential goldens come from?

There are **no existing compiler-output goldens anywhere** (§7). Options:

* **(A) Build the Java reference once in a scratch copy** (recommended). Copy
  `Script-Recompiler` to a scratch directory — *not* into the reference checkout, which must stay
  read-only and would otherwise gain a `build/` directory — and run
  `gradle run --args="compile …"` there. Needs network access for six Maven artefacts plus
  `org.antlr:antlr4:4.13.2`; the grammar is generated at build time, so there is no way to run
  the compiler without ANTLR. `sourceCompatibility = JavaVersion.VERSION_25` vs JDK 27 is
  expected to work but is `A`, not `V`.
* (B) Hand-written goldens. Rejected: we would be codifying our own guess as the oracle, which is
  exactly what spec `spc-1793129896113-12590` forbids.
* (C) Self-consistency only (compile → assemble → decompile → compare to itself). Rejected as the
  primary method: it proves internal agreement, never agreement with the reference.

Recommendation: (A), used as a **local oracle harness** kept out of the build, plus committed
goldens for *synthetic* fixtures only. Whether goldens derived from the two real `.fate` files, or
from the ten `.txt` disassembly files, may be committed is a licensing question (§9), not a
technical one — the `.txt` files describe retail script bytes.

**Answered, and partly overtaken by fact.** The harness was built and run (§12). Two premises in
the paragraph above turned out to need correction: the six Maven artefacts plus ANTLR were fetched
without trouble, and the meta bundle does *not* require the network — a named (non-`snapshot`)
meta version reads a local cache directory, so goldens are generated offline against the bundled
meta. The licensing question was answered separately (goldens may be committed); the ten `.txt`
files were left out of the repo.

### D4 — Exit codes: match the reference, or deviate?

Verified (`V`) reference behaviour (`Shell.java:221-247`): `compile` catches `Throwable`, logs
`"Failed to compile: "`, and **still writes the output file if compilation returned**; the
`errors` list is only logged afterwards; nothing sets a non-zero status, so a compile that
resolved no includes, parsed nothing, or threw mid-way still exits **0**. Non-zero exits exist
only for: no arguments (line 51), option parse failure (123), missing input file (161), malformed
`-t` argument (197/207), and an unknown command word (268).

**Corrected by the F0 runs (`V`).** "Still writes the output file" applies only when
`compile()` *returns*. Three distinct shapes were measured, and only the last two matter for the
contract:

1. compile returns → file written, exit 0, diagnostics logged (p11, p12, p13, p14, p15);
2. compile throws (`IncludeFailedException` for an unresolved include, NPE for `true`) → the write
   is *inside* the `try`, so **no file is written at all**, and the exit code is still 0 (run B,
   p10);
3. the failure happens *outside* the `try` — `readIncludeDirs` throwing on a missing `-L`
   directory, called at `Shell.java:227` — which escapes `main` as an uncaught exception and
   gives a **real exit 1**.

So the reference has exactly one non-zero compile-path exit, and it belongs to a case a user
cannot distinguish from case 2 by looking at the output. That strengthens the case for the typed
contract: `2` covers "input/output error" (unresolvable include dir, unwritable output) and `3`
covers "compiled with diagnostics", and neither collapses back into success.

Spec `spc-1793131198334-17871` requires typed statuses with only the CLI entry point producing
exit codes, and "malformed input must never degrade into a success result". Proposal: **match the
reference byte-for-byte on assembly text, and deviate deliberately on exit status** —
`0` success without diagnostics, `1` usage error, `2` input/output error, `3` compilation produced
diagnostics. This is a compatibility break against any script that currently pipes the Java tool
and trusts its exit code, so it needs an explicit ruling.

### D5 — Identifier style inside the C tool

`check_naming.py` exempts identifiers that contain `_` and are all-lower or all-upper
(`is_snake_or_macro`), i.e. `psx`-style snake_case C names pass automatically; camelCase names do
not get a free pass (`parseTimClut` would be flagged, `parse_tim_clut` would not). So both styles
are gate-compatible, and this is a taste/architecture decision, not a lint one. Original
recommendation was PascalCase for functions and types.

**Answered: psx-style snake_case, no `fate_` prefix.** Decided by the owner on 2026-10-27, which
overrides the recommendation above. Consequences to honour when the code lands: types `snake_case_t`,
constants and macros `UPPER_SNAKE`, internal linkage by default, and snake_case *only* inside `fate/`
— the C++ naming table in `AGENTS.md` still governs `src/`, `tests/` and `psx/`.

---

## 1. What `fate compile` is, precisely

Four separable stages exist in the reference, and `fate` must keep them separate:

```
Fate Script source (.fate)
      │  compile      — FateCompiler: ANTLR parse → preprocessor → visitor → text
      ▼
assembly text ("canonical" text form)
      │  assemble     — Tokenizer (text → Script token tree) + Assembler (tree → int[] words)
      ▼
binary script (little-endian 32-bit words)
      │  decompile    — Disassembler (words → tree, with register tracking) + Translator (tree → text)
      ▼
assembly text
```

`compile` **ends at text**. Producing bytes from text is `assemble`; producing text from bytes is
`decompile`. The reference wires them together only inside `assemble`, which runs the Tokenizer
(and, for `#include x.fate` lines, the compiler) and then the assembler.

**Consequence for the native design (`V`):** compile output is *not self-contained*. A compiled
`.fate` keeps `#include char_utils.fate` as a literal output line and emits calls to functions
defined in the include (`gosub inl[:removeAllCharsFromParty]`) instead of inlining them. Text→
binary resolution therefore happens at `assemble` time, and any "compile straight to binary"
shortcut would be a different tool, not this one.

The composition is not hypothetical — it is what the dormant patch `scripts/DRGN22/843/1.diff`
does in both patch trees (`V`): it inserts `#include queen_fury.fate` into a script's assembly
text and replaces three `gosub inl[:LABEL_73]` lines with
`push stor[stor[stor[0], 23], 31]` plus `gosub inl[:updateParty]`, i.e. hand-written assembly
calling a function that only exists once `queen_fury.fate` has been compiled, passing one argument
by `push`. For that text to assemble, the assembler has to resolve the include, compile it, and
bind `inl[:updateParty]` to the compiled function's label. That file also carries the only real
nested-storage operand seen (`stor[stor[stor[0], 23], 31]`), which is why two-index `stor` is in
scope for `assemble` even though no `.fate` fixture uses it.

## 2. Reference toolchain shape (`V`)

Single CLI entry point `org.legendofdragoon.scripting.Shell` (one process, subcommand dispatch),
backed by: `Tokenizer` (text → tree), `Assembler` (tree → words), `Disassembler` (words → tree),
`Translator` (tree → text), `Patcher` (unified diff over text), `FateCompiler` (+ `FateContext`,
`FateScope`, `FatePreprocessor`, `FateCompilerVisitor`, value/param classes), `MetaManager`/`Meta`
(CSV registry/enum/name metadata), `State` (word access), `resolution/*` (register tracking used
by the disassembler).

Dependencies: log4j 2.24.3, jansi 2.4.1, opencsv 5.9, commons-cli 1.6.0, java-diff-utils 4.12,
ANTLR 4.13.2 (`build.gradle:26-33`). Java 25 source/target. **No `src/test` — the reference has no
test suite at all**, which is why "goldens" have to be generated rather than taken from it.

The same classes are used inside the game at runtime (`ScriptManager` keeps a `Disassembler`,
`Assembler`, `Tokenizer`, `FateCompiler` and `Meta`), so the tool and the runtime share one
definition of the text format. The native design must keep that property: the same text codec for
the CLI and for any future in-engine use, or the two will drift.

## 3. CLI contract, as it actually is (`V`)

Pre-dispatch commands (handled before any option parsing, each exits 0 after running):
`v`/`versions`, `g`/`genpatch`, `p`/`patch` (shown as `[a]pplypatch` in the no-args help),
`u`/`undopatch`, `s`/`strip`. Everything else goes through a commons-cli `Options` block whose
`switch` handles `d`/`decompile`, `c`/`compile`, `a`/`assemble`, and prints the command list with
exit 1 for anything else — so the help string shown on zero arguments and the one shown on an
unknown command are **different lists**, and neither is authoritative.

Options for the options-path commands:

| Flag | Meaning | Notes relevant to `fate` |
|---|---|---|
| `-i/--in` (required) | input file | resolved relative to `-w` if relative |
| `-o/--out` | output file | **default is `<input>.txt`**, so `compile queen_fury.fate` writes `queen_fury.fate.txt` |
| `-w/--working-directory` | base dir for relative in/out | default: process CWD |
| `-v/--version` | meta version | default `snapshot`, fetched from `https://legendofdragoon.org/scmeta/`; cache dir is `./cache`, **relative to CWD, not to `-w`** |
| `-L/--libs` (repeatable) | include dirs for `#include` | a non-existent dir raises `IncludeFailedException` **before** the try block → hard failure, stack trace |
| `-b`, `-t`, `-C`, `-N`, `-l`, `-e` | decompile only | forced branches, table lengths (`addr=len`, hex), no comments, no friendly names, address prefixes, entrypoint count |

Two facts that change our CLI design:

1. **Meta is loaded before the command switch** (line 129-130), for every options-path command
   including `compile`. A compile run with the default `snapshot` version makes a network request.
   The offline path is `./cache/<version>/*.csv` (a local `MetaManager` was verified earlier to be
   what the game uses). `fate compile` therefore needs a meta source; without one, engine-call
   names (`SMap::scriptScaleUniform`) and their arity/types cannot be resolved.
2. **Include resolution ignores the including file's directory.** `Include.resolve` searches only
   the `-L` list; if that list is empty or matches nothing, it throws. So compiling
   `queen_fury.fate` (which contains `include char_utils.fate;`) **without** `-L <dir>` is a hard
   failure, not a warning — and `-L libs` alone is not enough if the run directory differs. Our
   CLI must decide: reproduce this exactly, or add the input file's directory as an implicit
   search path (a behavioural deviation; §0 D4-adjacent).

## 4. The compile stage, step by step (`V`, `FateCompiler.java:31-50`)

1. `parse(source)` — ANTLR `FateLexer` + `FateParser`, entry rule `body`, over the whole file.
2. `FatePreprocessor` pass over the tree: collects `include` names into a queue and `def`
   signatures into a `Map<String, FateFunctionDefinition>`; emits the "function … missing return"
   and "function … is not a legal name" diagnostics (the second triggers when a `def` name matches
   an opcode name, i.e. the language's function/call disambiguation depends on the opcode table).
3. `getFunctionsFromIncludes` (lines 59-83): breadth-first over the include queue, deduplicated by
   the include *string*; only names ending in `.fate` are resolved and parsed; each resolved file
   contributes its own `def`s and its own nested includes; an unreadable file becomes a diagnostic
   (`"Failed to load include …"`) and processing continues, while an **unresolvable** file throws
   and aborts the whole compile.
4. A fresh `FateContext` + `FateCompilerVisitor` walk the **same** tree a second time to emit ops.
   Two passes over one tree is what makes mutual recursion and forward calls work: every signature
   from the file and its include closure exists before any body is compiled.
5. `updateVariableNames()` (per-function prefixes) and `updateLabelNames(labelPrefix)`.
6. `fate.compile()` renders the text.

`labelPrefix` is `""` on the CLI path (the 3-arg `compile` overload, line 27-29 → 4-arg with `""`)
and is the include file's base name on the runtime path (`Tokenizer.java:124-125` passes the
`#include` filename minus `.fate`, so labels inside `char_utils.fate` become
`char_utils_label_N`). **`fate compile` must therefore emit unprefixed `_label_N`** and match the
`fate compile` path, not the Tokenizer path; the prefixed form belongs to `assemble`, where an
included file is compiled inline.

Output assembly (`FateContext.compile()`, lines 124-138): `entrypoint :NAME` lines in source
order, then **one blank line**, then one line per op. A `entrypoint NAME;` statement in a `.fate`
file names a *function* to be treated as an entry point, and errors with
`"<line>: entrypoint function NAME not found"` if no such function exists — it is not a free-form
statement and it does not appear inside function bodies.

## 5. The assembly text that `compile` must reproduce (`V`, measured against the F0 goldens)

* Op line: `opName + ' ' + join(", ", params)` (`FateOp.toString`, lines 26-28). Consequence:
  **a zero-parameter op keeps a trailing space** (`noop `, `return `). Byte-exact comparison will
  care. `V` — confirmed byte-by-byte: `p1_min.expected` is exactly `\n\nf:\nreturn \n`, and
  `char_utils.expected` contains 15 such lines.
* Function prologue (`FateFunction.toString`, lines 69-86) is itself an entry in the op list: a
  leading `\n`, then for each unique variable name a `NAME:` line followed by `data <value>` lines
  (one per array element), then the function's own `NAME:` label with no trailing newline (the
  caller appends `\n`).
* Compiler-generated symbols appear verbatim in the text: `_label_N` (`FateContext.java:76`) and
  `_expr_N` scratch temporaries (`FateCompilerVisitor.java:40`). Their numbering and placement are
  part of the format. **Measured shape:** the emitted name is `<function>_<scratch>`, so a scratch
  slot inside `updateParty` reads `updateParty__expr_0` — double underscore, because the `_expr_`
  prefix already starts with one. `_expr_` and `_label_` counters run **per file**, not per
  function: in `queen_fury.expected`, `updateParty` uses `_expr_0`–`_expr_19` and `addChar`
  continues at `_expr_20`, and likewise `_label_0`–`_label_3`.
* Parameter renderings to support: `stor[N]`, `stor[a,b]`, `var[N]`, `var[N][M]`, `reg[N]`,
  `id[NAME]`, `inl[:LABEL]`, `inl[0x…]`, `str[…]`, `[a, b, c]` value lists. **Measured** (`V`,
  p5): a literal index is *not* inlined — `stor[5]` becomes `mov 5, inl[:f__expr_0]` then
  `mov stor[inl[:f__expr_0]], …`, i.e. the address is computed into a scratch slot and referenced
  indirectly, while `reg[3]` stays an immediate. Two-index forms render as `stor[a, b]` with a
  comma and a space; `var[a][b]` keeps the two-bracket form.
* Calls to user-defined functions become `push <arg>…` followed by `gosub inl[:funcName]`;
  engine calls become `call <Meta name>, <args…>` — the method name rides in as parameter 0, and
  the Tokenizer recovers it with a `^call\s+(.+?)\s*(?:,.*|$)` matcher. **Measured** (`V`): the
  `push` order is reverse of the source argument order, the callee's `pop` order is the same
  reverse order, and an *unknown* bare function name still emits `gosub inl[:name]` alongside the
  `unknown function` diagnostic (p6). A call to an unknown **scoped** name (`SMap::unknownCall`)
  emits nothing at all — the statement is dropped — and also reports `unknown engine call` (p11).
* Comment lines the translator emits (`; SUBROUTINE`, `; SUBROUTINE TABLE`, `; FORK JMP`,
  `; WARNING: …`) belong to `decompile`, not `compile`. `//` comments in *source* disappear
  entirely (lexer `-> skip`), which is why `char_utils.fate`'s 26 comment lines leave no trace in
  its 373-line golden.

## 6. Fate Script language surface, from the grammars (`V`)

Top level is `body : (entrypoint | global | function | include)*` — **there are no statements at
top level and no free-floating code**; a `.fate` file is a set of declarations. Inside a function,
`block` accepts `statement | control`, all terminated by `;`.

Statements: declaration, assignment, postfix (`++`/`--`), augmented assignment, call, `return`
(optional expression or expression list), `continue`, `break`. Control: `if`/`else` (with
`else if`), `while`, `do { } while (…);`. No `for` exists in the grammar.

Expressions: parenthesised, unary `+ - ! ~`, `* / %`, `+ -`, `>> <<`, `< > <= >=`, `== !=`,
`&`, `^`, `|`, `&&`, `||`. Values: numbers, calls, `stor[…]`/`var[…][…]`/`reg[…]`,
`id` references (`MODULE:NAME`), array lookups. Globals: `var NAME = <number | [ … ]>;`.

Hazards found while reading the lexer, all of them easy to get wrong and all testable. **All six
were then run through the oracle**, and two of the readings below were wrong; the corrections are
inline because the difference between "parse error" and "silently wrong output" changes what the
native tool has to do.

* `WHITESPACE : [ \n] -> skip` — **tab and carriage return are not skipped**. A tab anywhere
  outside a comment is a lexer error, and a CRLF file mis-lexes. The two `.fate` fixtures are
  LF-only and tab-free (`V`), so this stays invisible until someone writes a fixture in an editor
  that inserts tabs. *Measured (p9):* each tab produces `token recognition error at: '\t'`, and
  compilation **continues anyway**, emitting code as if the tab were whitespace, exit 0.
* `NUMBER : '-'?([0-9]+|'0x'[a-f0-9]+)` — hex requires a lowercase `x` and lowercase digits.
  The prediction that `0xFF` is "a syntax error" was **wrong**: it lexes as the number `0` followed
  by the identifier `xFF`, and the reference emits `mov 0, inl[:f_x]` for `var x = 0xFF;` with
  **no diagnostic at all** (p8). Silent miscompilation, not a rejection. The *assembly text*
  number pattern is separately case-insensitive, so the uppercase form is legal in `.txt` and in
  hand-written `.fate` it loses bits without complaint.
* `TRUE`/`FALSE` exist as lexer keywords but **no parser rule references them**. The prediction
  that `if (x == true)` is a *parse* error was **wrong**: it reaches the emitter and reports
  `unimplemented op true`, then throws a `NullPointerException`, so no output is written and the
  exit code is still 0 (p10). Practical reading stands: do not implement them — but the native tool
  must classify this as a diagnostic (exit `3`), not as a crash.
* `INCLUDE : 'include' ' '+ -> pushMode(INCLUDE_FILE_MODE)` consumes one-or-more spaces and then
  the rest of the line up to `;` as the file name, so `#include` (text form) and `include …;`
  (source form) are different mechanisms reached by different code paths. *Measured (p13):* the
  whitespace rule is load-bearing — `include char_utils.fate ;` captures the trailing space into
  the filename token, the include then resolves but its functions are **not** found
  (`unknown function charIsInActiveParty`), whereas the same file written without the space
  resolves normally. Whitespace inside a construct is therefore a semantic difference, not cosmetic.
* Strings are `'"' (ESC_SEQ | ~('\\'|'"'))* '"'` with escapes limited to `\n`, `\"`, `\\`, and
  they do not stop at a newline: an unterminated quote silently swallows the remainder of the file.
  *Measured (p11):* a string argument itself compiles; what fails is the unknown *call* around it.
  An unterminated string is still untested (`U`).
* `&&=` and `||=` exist in the lexer and in `augmented_assignment_op`. *Measured (p4):* they lower
  to `cmp &&, <lhs>, <rhs>, <lhs>` / `cmp ||, …` writing back into the left-hand slot — no
  temporary, no diagnostic. The whole operator set of `p4_expr.fate` compiles clean, so the
  lowering table is now data in the golden rather than an open question.

## 7. Fixture and oracle inventory (`V`)

**Fate sources — two unique files.** `char_utils.fate` (187 lines) and `queen_fury.fate`
(73 lines), each present twice (`~/Documents/GitHub/lloyd_mod/patches/libs/` and
`~/Documents/GitHub/Severed-Chains/patches/libs/`) with identical content. Do not count them as
four fixtures. ASCII, LF, no tabs.

They cover, between them: function definitions with parameters, calls to user functions (both a
same-file call and a cross-file call resolved through the include), engine calls with `stor[…]`
and `var[…][…]` arguments, `if`/`else if`, `while`, `break`, `return` with and without a value,
scalar and array globals, augmented assignment `*= &= |= >>=`, bitwise `&`, comparisons
`!= < >`, `||`, postfix `++`, `var[115 + charIndex]`-style indexed addresses, `var[6][20]`
two-index game variables, `include`, `//` comments.

They do **not** cover: `do…while`, `continue`, `entrypoint`, string literals or strings as call
arguments, `reg[…]`, `id[NAME]` enum references, two-index `stor[a,b]`, `&&`, `&&=`, `||=`, `~=`,
`!`, `~`, `^`, `/`, `%`, `--`, `true`/`false`, multi-assign (`(a, b) = …`), and every error case.
A differential corpus has to be written; the real fixtures alone are not enough.

**Assembly text — ten files, none of them compiler output.** `libs/`: `char_utils.txt`,
`lookup_tables.txt`, `shadow.txt`, `throw_item_effects.txt`, `widescreen.full.txt`,
`widescreen.txt`; `scripts/`: `lookup_tables.txt`, `player_combat_script.txt`,
`throw_item_effects.txt`, `throw_item.txt`. The libs/scripts pairs are *not* duplicates
(distinct checksums). Zero occurrences of `_expr_` or `_label_` in any of them; labels are
hand-authored upper-case names (`CHAR_IS_IN_ACTIVE_PARTY:`), and `char_utils.txt` implements the
same six helpers as `char_utils.fate` with a different internal structure.

**This corrects a premise in `docs/architecture/testing.md` §2.12**, which currently asks for
"compile the reference `patches/scripts/*.txt` fate files → byte-identical to the Java
FateCompiler output". Those files are already assembly text: `player_combat_script.txt` begins
with `entrypoint :ENTRYPOINT_0_MAIN`, and none of the ten files can be parsed as Fate source (top
level allows only `entrypoint`/`global`/`function`/`include`, and `entrypoint` needs
`IDENTIFIER ';'`). Feeding them to `compile` is not a test we can run; they belong to `assemble`
and `decompile`.

**Patch corpus:** 296 `.diff` files plus `scripts.csv` (288 rows: 2 `replacement` entries pointing
at `.txt` assembly, 286 `diff` entries — so ten `.diff` files on disk are referenced by nothing).
`widescreen.txt` documents a real reference invocation in its own header:
`s -i widescreen.full.txt -o widescreen.txt` (strip).

**The unlisted ten are exactly the ones that include a `.fate` file** — `DRGN22`
834, 837, 840, 843, 846, 849, 852, 855, 858 and 969, verified by checking every `.diff` against
both `scripts.csv` files — while the three diffs that `#include char_utils.txt` *are* listed.
Read carefully, that says: the assembly-include route is live, the Fate-library route is dormant
in both patch trees, and `char_utils.txt` is the hand-maintained assembly twin of
`char_utils.fate` rather than its output — consistent with that file's own header comment ("if you
make any changes to this file, it has to be manually recompiled to char_utils.txt") and with the
absence of any compiler-generated symbol in it. **`U`:** whether those dormant diffs ever
assembled correctly; they are the natural first target for the oracle once it exists, because they
are the only real data that exercises compile-plus-assemble together.

**Meta:** `patches/meta/{descriptions.csv,params.csv,enums.csv}` plus per-enum CSVs. The two
engine calls used by `queen_fury.fate` are present in the local `descriptions.csv` and
`params.csv` (`V`), so a local meta set is sufficient to compile that file offline once an oracle
exists.

**Goldens: zero.** No `.fate` → assembly-text reference output exists anywhere in the three repos,
and the reference has no test suite to generate one.

## 8. Differential testing design (proposal)

1. **Oracle harness, outside the build.** A script that builds the Java reference in a scratch
   directory and runs `compile` over a fixture list, writing `*.expected` next to the fixtures.
   Not part of the default build; needs an explicit flag plus network on first use.
2. **A CTest entry per fixture**, or one entry that iterates a manifest, that runs the built
   `fate compile` and compares against `*.expected` byte-for-byte. **Skipped with a visible
   message when the goldens are absent, never silently passed** — the same rule `repo.format`
   follows when `clang-format` is missing. Otherwise the suite would report green while testing
   nothing.
3. **Two comparison modes, deliberately.** `exact` for oracle-generated goldens; `structural`
   (whitespace-normalised, comment-stripped) only for the hand-written `.txt` files, which are
   useful as *inputs* to `assemble`/`decompile` and as semantic cross-checks, never as compile
   goldens.
4. **Negative fixtures first-class.** Unresolvable include, missing include dir, `def` name that
   collides with an opcode, function without a return, wrong argument count, unknown engine call,
   `0xFF` literal, tab in source, unterminated string. Each pins a diagnostic code and a non-zero
   exit — that is where a hand-written compiler usually leaks.
5. **Sequencing.** Until an oracle exists, everything written is unverifiable by construction.
   This argues for building the harness *before* the compiler, not after.

## 9. Constraints inherited from this project

* **Licensing (`V` on the facts, `U` on the conclusion).** Script-Recompiler is AGPL-3.0 and is a
  read-only behavioural reference; this project keeps an AGPL-3.0 `LICENSE` and describes itself as
  an independent derivative reimplementation. The working rule for `fate` is *derive behaviour, do
  not translate Java* — the grammars, the output format and the diagnostics list are facts to
  match; the visitor code is not material to copy. Two open items for the project owner: whether
  oracle-generated goldens may be committed, and whether the ten `.txt` disassembly files (derived
  from retail script bytes) may enter the repository at all under "never distribute game data".
* **Single-host verification.** Only macOS/arm64 is locally verifiable. A hand-written recursive
  descent parser for this grammar is genuinely feasible on one machine *because* the oracle can,
  in principle, be run locally — which is why D3 matters more than D1/D2.
* **Q-SCR-1** (ANTLR4-C++ vs hand-written parser) is recorded in `docs/research/script-recompiler.md`
  with a recommendation toward a hand-written parser. This analysis does not change that
  recommendation, and it adds a fact in its favour: the grammar is one lexer + one parser with a
  bounded token set, and its lexer quirks (tab handling, hex case, dead `true`/`false`) are things
  a hand-written scanner can reproduce exactly, which a generated parser inheriting a *different*
  runtime version cannot promise.
* **Roadmap conflict, to be resolved rather than ignored.** Roadmap M6 owns the script pipeline
  inside `psx`/engine and M12 owns a script CLI. A standalone C23 `fate` is neither. The
  resolution proposed here: `fate` becomes the host-side reference implementation, and whatever
  the engine eventually needs is a *consumer* of the same text codec, decided when a consumer
  actually exists. Until that is agreed, no `psx::script` code should be written.
* **No speculative infrastructure.** No `assemble`/`decompile`/`strip`/`genpatch` scaffolding
  before `compile` works and is measured. No public library target. No `fate_` prefixes. No global
  mutable compiler state.

## 10. Proposed milestones (for approval, not started)

* **F0 — Oracle (no C code). DONE 2026-10-27, acceptance met.** Scratch-copy build of the
  reference, 17 fixtures compiled end to end, 17 goldens committed. See §12.
* **F1 — Lexer + parser + AST** for the exact grammar in §6, with a `--dump-ast` debug mode.
  *Design delivered 2026-10-27:* `docs/research/fate-compile-design.md` is the implementation
  contract (lexer rules, grammar + precedence table, calibrated recovery policy, two-pass
  preproc/codegen split, every emission rule, meta loading, CLI, CMake/C23 probe, gates, tests).
  *Acceptance:* parse-only tests over both real fixtures and the synthetic corpus; every §6
  hazard has a test — note the F0 correction: tab and `0xFF` do **not** fail the reference, they
  compile (with a stderr warning for tab, silently for `0xFF`); `true` produces a diagnostic and
  no output. The three recovery fixtures (`p8`, `p12`, `p13`) must be byte-exact first, in
  isolation, before codegen is built on top.
* **F2 — Semantic pass + text emission**: scope handling, function table over the include closure,
  scratch-variable allocation, prologue/epilogue emission, `entrypoint` lines, the trailing-space
  quirk. *Acceptance:* byte-exact match against F0 goldens; every diagnostics entry in §3/§4 has a
  fixture. **DONE 2026-10-28, acceptance met with two documented gaps.** Result: 18 of 19 fixtures
  are byte-identical to their goldens, 1 is a pinned deviation (§13). The two gaps are diagnostic
  *text* comparison (the harness compares emitted assembly bytes and exit status, not the
  reference's diagnostic wording) and `entrypoint` emission, which no fixture reaches. Defects
  found and fixed by differential testing are listed in §13 — none of them were visible by
  reading the reference.
* **F3 — CLI surface**: `fate compile` only, plus `--help`; the other subcommands stay unclaimed
  and reject with a clear message. *Acceptance:* exit-code contract from D4 implemented and tested;
  no `exit()`/`abort()` anywhere below the entry point.
* **F4 — Gate integration**: `SCAN_ROOTS` extension, `.c` added to the naming gate, C23 probe, one
  CI leg that runs the C tool tests and *skips* (loudly) when goldens are absent.
  **DONE 2026-10-28, with a revision:** the CI lane is not one leg but a dedicated
  `fate-toolchain` job of three (macOS arm64, Linux x86-64 GCC, Linux x86-64 Clang), gated by a
  C23 feature probe with a non-vacuity guard. See §13.

## 11. Verified / assumed / unknown

**Verified** (Phase 1, from source and data): the pipeline stage boundaries; the CLI table in §3
including default output naming, the include-dir failure, and the "exits 0 even when compilation
failed" behaviour; the six-step compile pipeline and the two include-failure classes;
`labelPrefix` being empty on the CLI path; the language surface and the lexer hazards in §6; the
fixture inventory, the two-file duplication, the checksum differences between the `libs/` and
`scripts/` pairs, the absence of `_expr_`/`_label_` in all ten `.txt` files; the absence of any
test suite or golden in the reference; the local meta coverage of `queen_fury.fate`; the presence
of Gradle 9.8.2 and JDK 27 and the absence of a dependency cache; that no file under
`Script-Recompiler` was modified.

**Promoted from assumed to verified by the F0 runs** (`V`, §12): a `def` in an included file *is*
callable from the includer, with correct arity and `gosub` emission — but only when the filename
token is exact, and the failure mode when it is not is a missing symbol rather than a missing file;
repeated oracle runs are byte-identical, so no HashMap iteration order was observable on this
corpus; zero-parameter ops do emit the trailing space; Gradle 9.8.2 + JDK 27 build the Java 25
target including ANTLR grammar generation, first try, offline for dependencies already in the cache
and with a 1m17s cold fetch otherwise.

**Still assumed, must be tested:** that iteration order is irrelevant in *general* — demonstrated
on 19 small fixtures, not proven; that the two real fixtures exercise the emitter paths a real
mod needs (they do not reach `entrypoint`, strings, or `id[NAME]`).

**Remaining unknowns, documented rather than guessed:** whether the game's `ScriptManager` path
ever produces output the CLI path cannot reproduce; whether the shipped `patches/` directory is
merged from multiple mods (Q-SCR-2); the per-script entrypoint counts (Q-SCR-3); and whether an
unterminated string really swallows the rest of the file. The six cases that F0 had left as
`parity: decide` are no longer unknowns — the owner ruled them **match** on 2026-10-27 (R1, §0),
so they are now normative targets pinned by their goldens rather than open questions. What R1
does *not* resolve is a design concern for Phase 2, not a behavioural one: the exact parse-recovery
algorithm that must be used to reproduce the reference's p8/p12/p13 output (the reference gets there
via ANTLR's default error recovery, which is a by-product, not a designed behaviour) — the goldens
are the acceptance test for whatever algorithm is chosen.

---

## 12. F0 executed — the oracle runs, and 17 goldens exist (2026-10-27)

**What was built.** `tools/fate_oracle.py` (Python, stdlib only, ~200 lines) drives the Java
reference in `~/.cache/sc-fate-oracle/`: `status` / `build` / `gen` / `check`. It copies the
reference into that scratch tree and builds there — `~/Documents/GitHub/Script-Recompiler` was
never written to, and `build.gradle` was not read-modified or copied into this repo. Nothing in
the default build or test run touches it.

**Environment evidence.** Gradle 9.8.2 + JDK 27 built the `JavaVersion.VERSION_25` target with the
ANTLR generation step on the first attempt: 1m17s from a cold dependency cache, 8s warm, no
toolchain flags added. Maven Central was reachable (200, 0.48s) and fetched the six artefacts plus
ANTLR 4.13.2 — so option (A) in D3 was feasible, which was the point of testing it before writing
a parser.

**The offline meta path, which D3 had wrong.** The reference hard-codes
`https://legendofdragoon.org/scmeta/` with a local cache at `./cache`, and `loadMeta("snapshot")`
*always* re-fetches — but any **named** version reads `./cache/<version>/` if it exists. The
harness therefore seeds `cache/m1/` from the local Severed Chains meta bundle and runs
`-v m1`, which is reproducible and needs no network. Two facts follow, both verified: emitted text
contains engine-call friendly names taken from that meta, so the meta is part of every golden's
provenance; and every CSV named by `enums.csv` must be present, because a missing one throws in
`MetaManager.loadCsvFile` before any compiling happens, writes no output, and exits 1.

**Determinism.** Three runs of `queen_fury.fate` produced identical SHA-1s, and `check` reproduces
all 17 goldens byte-for-byte from a clean scratch tree. The HashMap-order question is therefore not
a blocker, though it remains a demonstration on a small corpus rather than a proof.

**Corpus.** `fate/testdata/`: 2 real fixtures (committed verbatim) + 17 synthesized probes
(`p1`–`p17`) written to cover what the real fixtures cannot — `do…while`, `continue`, all operators
including `&&=`/`||=`, two-index `stor`, `var[…]`/`reg[…]`, unknown engine call, string argument,
`for`, `0xFF`, tab indentation, `true`, missing return, wrong argument count, and a
deliberately malformed include; `p16`/`p17` (WAIT operand cloning, string escapes) were added in
F2. Each fixture has a `.expected` (byte-exact reference output), a
`.diag` (exit code, whether a file was written, reported diagnostics), and a `manifest.json` entry
holding its arguments, expected outcome, and parity ruling.

**What the probes bought.** Four of the five things F0 was for came back with answers, and two of
them contradicted the reading in §6 — which is the argument for measuring before implementing. The
two most consequential: `0xFF` is not rejected, it is silently narrowed; and every reference
failure path still exits 0, which is exactly the collapse the approved exit-code contract refuses
to reproduce. Both of those would have become *design decisions* in the absence of a ruling; the
owner answered them on 2026-10-27 with R1 (match the reference as closely as possible), so the
native tool now has a single normative target — the goldens — rather than a choice per case.

**Consequences for the native design**, to be resolved in Phase 2:

1. Byte-exactness is a real constraint, not a formality: leading blank line, trailing space on
   zero-parameter ops, per-file `_expr_`/`_label_` counters, and per-file scratch numbering together
   mean the expression-lowering order is observable. Two implementations can be semantically
   identical and still fail the comparison.
2. Scratch-slot allocation and label counters must therefore be *specified* in the design, not left
   to whatever falls out of a tree walk — the goldens pin them.
3. The six `parity: decide` probes are ruled **match** (R1, §0, 2026-10-27): the C tool reproduces
   the reference's observable behaviour — emitted text and diagnostic text — as closely as
   possible, and the goldens are normative. The one case where a literal match is impossible is
   p10: the reference's `NullPointerException` is a defect, not a behaviour, so the C tool reports
   `unimplemented op true`, writes no output file (matching the reference, whose write sits inside
   the `try` that the NPE aborts), and exits 3 under the approved contract instead of the
   reference's 0. The other five match the reference *including* the exit code where the reference
  's `errors` list is empty (p8, p9) or non-empty (p11, p12, p13 → exit 3). The parse-recovery
   algorithm behind p8/p12/p13 is a Phase 2 design concern; the goldens are its acceptance test.
4. Gate scope: `SCAN_ROOTS += "fate"` and `.c` in the naming gate, per D1 — with a planted
   mis-cased identifier to prove the extension actually bites, not just that it does not error.
5. `fate/testdata/golden/` is committed; the meta bundle is not. A CI leg, if one is ever added,
   must skip loudly (exit 77) rather than pass when the oracle cannot run.

**Where these landed.** The F1 design of 2026-10-27 (`docs/research/fate-compile-design.md`) is
the implementation contract and resolves each item: (1) → design §7.4 + §13 (the allocation
order and the byte invariants are spelled out, not left to a tree walk); (2) → design §7.4
(scratch/label allocation is *specified*, goldens pin it); (3) → ruled R1, design §1.1; (4) →
design §11 (gate scope + planted-violation proof); (5) → design §12 (loud skip 77, oracle never
in the default build). What the design deliberately leaves open for the owner: the `-m` meta
flag (design §14.1), the recovery-policy generalisation (§14.2), and which untested paths get
fixtures now vs later (§14.3).

---

## 13. F2 result (2026-10-28): what the differential actually caught

Eighteen of nineteen fixtures now match their goldens byte-for-byte; `p12_forloop` is a pinned
deviation. The value of the oracle-first sequencing shows up entirely in the defect list — every
item below was invisible in a reading of the reference and only appeared when output was compared.

1. **WAIT operand cloning** (`p3`, `p16`). The reference clones the operand of `wait` into a fresh
   scratch slot because `wait` decrements its operand. Matching on the *display name* `wait` also
   catches `wait_cmp` (opcodes 3/4), which takes no copy — the check is on opcode 2, which is why
   the op table is data (`optable.h`) rather than a name lookup.
2. **String arguments to engine calls** (`p17`). A string parameter becomes a `_expr_` slot whose
   prologue value is `data str[<body>]`, emitted in the call as `inl[:f__expr_N]`. This first
   required the string literal to reach the emitter as data rather than as an integer expression.
3. **Embedded NUL in that slot** — `memcpy(data, "str[", 5)` copied the string literal's
   terminator into the buffer. Found only because the golden is compared as bytes: a
   line-oriented or trimmed comparison would have hidden a 0 byte at offset 4.
4. **Fixture arity errors are silent.** `p17` was first written with one parameter against a
   three-parameter function; it produced `arg count` diagnostics rather than a crash, so a
   hand-written test asserting "compiles" would have passed. Fixed the fixture, not the compiler.
5. **`p12_forloop` is not valid Fate.** Its golden is an ANTLR error-recovery artifact whose first
   statement, `gosub inl[:for]`, names no defined label and would not assemble. Reproducing that
   byte sequence would mean emulating a parser bug, and any grammar-level fix would then produce a
   *different* wrong answer. It is therefore recorded as `"parity": "deviation"` with
   `"valid_source": false`, and the harness checks exit status 3, absence of a crash, and that the
   output differs from the golden. Mismatches on **valid** source remain defects: four were fixed
   this round (quoted-string handling in `p14`/`p15`, the two WAIT/string cases).

**Tooling facts measured, not assumed.** The `fate` sources compile clean under `-std=c17`, so
nothing currently *requires* C23; the C23 probe in CI guards the declared standard rather than a
current dependency. The probe was checked on both toolchain families available locally — Apple
clang 17 and Homebrew GCC 16 each accept `auto x = 1;` under `-std=c23`/`-std=c2x` and reject it
under `-std=c17 -Werror -pedantic-errors`, which is what makes the gate non-vacuous. `p17`'s
fixture also corrected a mistake in this document's own earlier reading: string parameters are not
"passed as strings", they are passed as `inl[:…]` scratch slots.

---

### Citation index

Reference paths are relative to `~/Documents/GitHub/Script-Recompiler`:
`src/main/java/org/legendofdragoon/scripting/Shell.java:49-51,55-56,95-112,127-163,165-264,273-289,489-493`;
`Tokenizer.java:124-125`; `Include.java:8-29`; `IncludeFailedException.java`;
`compiler/FateCompiler.java:27-83`; `compiler/FateContext.java:76,104-138`;
`compiler/FateOp.java:26-28`; `compiler/FateFunction.java:69-86`;
`compiler/FateCompilerVisitor.java:40,59-68`; `compiler/FateLexer.g4`; `compiler/FateParser.g4`;
`build.gradle:16,26-33,79`.

Project files consulted: `AGENTS.md`, `docs/guidelines/cpp.md`, `docs/architecture/testing.md`
§2.12, `docs/architecture/roadmap.md` (M6, M12), `docs/research/script-recompiler.md`
(§1, §4, §7, §8), `docs/research/licensing.md`, `CMakeLists.txt`, `tools/check_format.py`,
`tools/check_naming.py`, `tests/CMakeLists.txt`, `tests/repo/CMakeLists.txt`.

Fixture paths: `~/Documents/GitHub/lloyd_mod/patches/libs/*.fate`,
`~/Documents/GitHub/lloyd_mod/patches/libs/*.txt`, `~/Documents/GitHub/lloyd_mod/patches/scripts/*.txt`,
`~/Documents/GitHub/lloyd_mod/patches/scripts.csv`, `~/Documents/GitHub/lloyd_mod/patches/meta/*.csv`.