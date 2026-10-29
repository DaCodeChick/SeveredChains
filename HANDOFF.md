# HANDOFF — session transfer (2026-10-28)

Working tree: `/Users/schala/Documents/GitHub/SeveredChains` (C++23 engine + `psx` C17 library + the new
`fate` C23 toolchain). **The tree IS under version control** — `.git` exists, branch `main`, two
commits (`44d1b0d` Initial commit, 2026-10-28 18:19; `6d6ffd1` Add native engine foundation and
Fate compiler, 18:27), remote `https://github.com/DaCodeChick/SeveredChains.git`. Notes elsewhere
in this file and in `progress.md` said "no `.git`"; that was stale within minutes of being written
and it cost a faster fixture recovery (see §7). **Check `git rev-parse --git-dir` before concluding
anything about version control here.** Nothing from this session is committed.

**Path note:** the `/tmp/falone` and `/tmp/rootb` build directories named below are
ephemeral and were **gone** at the start of this session (a stale CMake cache pointing at the
old `/Users/schala/Documents/severedchains` path also made the first configure fail). Re-create
the build directory rather than trusting a remembered one, and never reuse a cache from a
different workspace path.

This file is a *transfer of state*, not a spec. Authoritative detail lives in:

- `AGENTS.md` — project rules (read it; it is short and binding).
- `docs/research/fate-toolchain-phase1.md` — reference analysis, §13 = what differential testing caught.
- `docs/research/fate-compile-design.md` — the implementation contract for `fate compile`.
- `progress.md` — chronological log, newest row = 2026-10-28.

---

## 1. Where things actually stand

**`fate compile` (Fate Script → assembly text) works and is differentially tested.**

- 19 fixtures: 18 byte-identical to oracle-generated goldens, 1 pinned deviation (`p12_forloop`).
- The harness now checks **bytes, diagnostics and exit status** (it used to check bytes only).
  Measured after that change: 18/19 byte-exact + 1 pinned deviation, 17 diagnostics exact, 2
  printed as NOT VERIFIED (`p10` crash-line filter, `p12` unpinned), 19/19 exit statuses.
- `ctest` = **30/30**, and `fate.compile.golden` **actually runs** here (this machine has a meta
  bundle). `30` includes the fate entry; `-DSC_ENABLE_FATE=OFF` gives 29/29 with fate absent.
  **That count predates the 2026-10-28 harness/`main.c` edits**: after them only the fate-only
  build and `run_golden.py` were re-run (thermal budget), so `ctest` has to be re-run before
  anyone repeats "30/30". The golden entry runs the same command line, so the harness result
  above is real evidence; the untested part is the CMake/CTest wrapper, not the comparison.
- Naming gate clean, format gate clean (clang-format 23.1, 80 files), build has **zero warnings**
  under `-Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -pedantic`.
- Everything verified on **macOS/arm64 only**. No CI leg has ever executed. Do not upgrade that
  fact into "cross-platform verified" anywhere.

**Not done:** `assemble`, `decompile`, `strip`, `genpatch`, `patch`/`undopatch`, `versions`
(deliberately deferred — no scaffolding exists, by decision, not by omission).

---

## 2. Commands that work right now

```bash
# fate standalone (fast, no SDL/engine deps) — re-create the dir if it is missing
cmake -S fate -B /tmp/fb -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build /tmp/fb

# whole project
cmake -S . -B /tmp/rootb -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build /tmp/rootb
ctest --test-dir /tmp/rootb            # 30/30 (last verified before this session's edits)

# the differential directly (this is the acceptance test)
python3 fate/testdata/run_golden.py /tmp/fb/fate fate/testdata

# the two repo gates
python3 tools/check_naming.py
python3 tools/check_format.py

# run one fixture by hand (always `-o` onto a scratch path, never onto a fixture)
/tmp/fb/fate compile -m ~/Documents/GitHub/Severed-Chains/patches/meta \
  fate/testdata/fixtures/p15_argcount.fate -o /tmp/x.txt
```

Skip-path checks (all three were executed this session; re-run them if you touch the harness):

```bash
python3 fate/testdata/run_golden.py /nonexistent/fate fate/testdata      # -> 77 "no fate binary"
env -i PATH=/usr/bin:/bin HOME=/tmp/fakehome \
  python3 fate/testdata/run_golden.py /tmp/falone/fate fate/testdata     # -> 77 "no metadata bundle"
```

Regenerating goldens (opt-in only — **never** part of a default build/test):

```bash
python3 tools/fate_oracle.py status | build | gen | check
```

It builds the Java reference in `~/.cache/sc-fate-oracle/src` (scratch copy). **Never build or
write inside `~/Documents/GitHub/Script-Recompiler`** — read-only reference repo by project rule.

---

## 3. File map

`fate/src/` (7 `.c` + 9 `.h`, all C; one executable target `fate`, no library target):

| File | Responsibility |
|---|---|
| `util.c/.h` | arena allocator, growable byte buffer, `Str` (non-NUL-terminated slice), `Status` enum |
| `lexer.c/.h` | hand-written scanner; whitespace class is `[ \n]` **exactly**; hex digits lowercase only |
| `parse.c/.h`, `ast.h` | recursive-descent parser + AST, includes the ANTLR-recovery calibration |
| `model.c/.h`, `fate.h` | `Diag`, `Include`, `Func`, `GlobalVar`, `TopItem`, `Source` + helpers |
| `meta.c/.h` | loads `params.csv` / `enums.csv` / per-enum-class CSVs from `-m <dir>` |
| `optable.h` | op table **generated from `OpType.java` enum declarations** (55 names) |
| `emit.c/.h` | scope handling, scratch allocation, op-list emission, `entrypoint`, prologue/epilogue |
| `main.c` | CLI + the **only** place `exit()` is called |

`fate/testdata/` = `fixtures/` (21 `.fate`: 19 with goldens + 2 CLI-contract sources),
`golden/` (19 `.expected` + 19 `.diag`),
`manifest.json`, `run_golden.py` (the harness), `README.md`.
`tools/fate_oracle.py` = oracle driver. `.github/workflows/ci.yml` = 20 legs (17 engine + 3 fate).

---

## 4. Behavioural contract (read before "fixing" something that is intentional)

**Exit codes** `0` clean / `1` usage / `2` I/O / `3` compiled-with-diagnostics. This is a
*deliberate deviation* from the reference, which exits 0 even when it logs errors. `p15` exits 3
here and `exit=0` in `p15_argcount.diag` — that is correct, not a bug.

**`-L` paths are validated before the source is read** (decision 1, 2026-10-28): a
non-directory `-L` prints `no such include directory: <path>`, exits `2`, writes nothing. Before
that it decayed into `Failed to load include` + `unknown function` at exit `3`, which the contract
forbids. More than 32 `-L` entries exits `1` (it used to be silently ignored).

**An unresolvable `.fate` include is fatal** (decision 4, 2026-10-28): exit `2`, nothing written,
and a pre-existing file at the output path is *removed* so a failing run cannot package yesterday's
assembly. An output path that names the input is refused before any file is opened. This replaces
the earlier collect-a-diagnostic-and-continue behaviour, which wrote partial output and exited `3`.
The gate is still "token ends exactly in `.fate`": `include other.txt;` and the trailing-space form
stay verbatim `#include` lines. Measured reference behaviour and the citation chain are in design
doc §9.2; the five pins are `c4`/`c5`/`c7`/`c8`/`c9` (`c9` is the include that sits *between* two
valid `def`s, so it is the only pin where the failure happens after real statements are parsed).

**Two tables in `manifest.json`.** `fixtures` (19 oracle comparisons) and `cli_contract` (9
native checks, `c1`–`c9`, no golden — reported on their own line as *not derived from the oracle*,
because in the two families they cover the reference deliberately behaves differently: it exits 1
on an unreadable `-L`, and exits 0 on an unresolvable include). `c3_valid_include_control` and
`c6_trailing_space_gate` exist so that a rule which aborted on *every* include could not read as
green; `c8` compiles a **copy** of a fixture, because it pins "no failing run may damage its own
input".

**Two parity modes** in `manifest.json`: `"match"` (18) needs byte equality — that is the default
for valid source; `"deviation"` (`p12`) needs exit 3 + no crash + output *differs*. Mismatches on
**valid** source are defects. The `p12` pin is **open, not accepted**: see §5.6.

**Two diagnostic modes**: a fixture's stderr is compared against its `.diag` (order-insensitive,
multiset-strict, reference crash lines filtered out), unless the entry says `"diag": "unpinned"`,
which is `p12` and means *not compared, but reported*. There is no mode that passes in silence.

**Output invariants** (all observable, all currently matched): blank first line, trailing space on
zero-parameter ops, `_expr_`/`_label_` counters run **per file**, `str[]` string slots, `wait`
operand cloning.

**Ruled, do not "correct":** `0xFF` lexes as `0` + ident `xFF`; `true` is `unimplemented op true`
(no output file, exit 3 — the reference's NPE is a defect and is *not* reproduced); `:NAME` labels
are not compiler input; `for` is not in the grammar; string arguments become
`inl[:f__expr_N]` slots, not inline strings.

---

## 5. Open work, in priority order

### 5.1 Diagnostic parity — DONE 2026-10-28, verify before trusting

This was the section that said "the documented acceptance method (design doc §12) is
'byte-compare output **and compare the diagnostics**', and only the first half is
implemented". It is now implemented, on both sides of the pair:

- **The tool emits what the reference emits.** Function-level diagnostics lost their
  missing `N: ` line prefix; the per-tab lexer warnings are emitted again; the `p13`
  include path now reproduces the `unknown function` diagnostic (the trailing space in
  `include char_utils.fate ;` is load-bearing — the reference's `name.endsWith(".fate")`
  test sees it, so the file is *not* loaded, and the native tool has to keep that quirk
  to stay byte-compatible).
- **`run_golden.py` checks it.** Three axes per fixture: bytes, diagnostics, exit status.
  Reference crash lines (`Failed to compile:` + `java.*` + stack frames) are filtered from
  the expectation per R1, which is what lets `p10` be compared with no per-fixture hack —
  the `compile aborted` banner the tool used to print was removed because the reference
  prints no such line, so keeping it was an *added* divergence, not a matched one.
- **The checks were proven to bite**, not just to pass: a stubbed compiler that prints
  nothing, and one that prints one invented message, each produce failures naming the
  fixture, the expected line and the actual line.

Two things were re-ruled here on 2026-10-28 (decision 2), and the *shape* of the answer matters:

- Diagnostic **semantics** (which conditions are diagnosed, in what order, on which carrier) are
  settled by the oracle. Diagnostic **format** (codes, columns) is a native enhancement that may
  only be added as an additive machine-readable channel — JSON report, `file:line:col:` prefix,
  `--format=llvm` — never by rewriting the golden's default plain text.
- **Never fabricate a source location to satisfy a template.** The reference has column data
  available (`Token.getCharPositionInLine()`) and uses only `getLine()`; the lexer's own messages
  carry real columns, which is why `p9` can print `line 3:5 …` honestly.
- So the "stable codes / columns" half of `spc-1793131198334-17871` is no longer an unmeetable
  parity target — it is a feature with a place to be pinned (`cli_contract`). Nobody should invent
  an `FATE-E0001` string and call it parity.

`p12_forloop`'s missing diagnostic pair stays `"diag": "unpinned"`, and §5.6 records why it is not
"accepted" either.

### 5.2 CI is written but has never run (blocked, needs a decision)

`.github/workflows/ci.yml` has 20 legs. **No run of it has ever been observed**, and that is not
the same as "it cannot run": `.git` exists with a remote configured, so a push would trigger it.
Whether `origin` has been fetched/pushed before, and whether any Actions run exists, was NOT
verified in this session — check before citing CI as evidence either way. Locally I only dry-ran
the step scripts (YAML parses, 16 `run:` blocks pass `bash -n`, the macOS fate sequence ran
end-to-end). **Ask the user**: push and let CI run, or explicitly accept local-only evidence.
Spec `spc-1793042266346-69099` sits at `partial` for exactly this reason — do not mark it met
without a real run.

### 5.3 CI fate legs currently compare zero bytes

The meta CSVs are not committed (retail-derived). Runners have no `Severed-Chains` checkout, so
the 3 `fate-toolchain` legs build the tool and **skip** the golden test (printed as a visible
`NOT VERIFIED` banner, not as coverage). Committing a small **synthetic, non-retail** meta bundle
under `fate/testdata/local-meta/` would make them real. That is a licensing/scope call —
**do not do it unilaterally.**

### 5.4 Nothing from these two sessions is committed

`git status` is 11 modified tracked paths + 1 untracked new fixture
(`fate/testdata/fixtures/c9_midstream.fate`), no commit made. `c1_noincdir.fate` is *not* new — it
has been tracked since `dd58586`; only its header comment changed. HEAD contains the **pre-damage**
fixtures, so `git checkout -- fate/testdata/fixtures` would have restored the corpus in one
command. The manual
restore from the `/tmp` snapshot happens to be byte-identical to HEAD (empty `git diff` against
HEAD for all 19), which is the integrity proof — and also luck, not process. Committing is the
user's call; no destructive git operation may be used to "fix" mistakes.

### 5.5 Then F3

CLI surface: `--help`, argument validation, "missing required arg" messages, and keeping
`exit()`/`abort()` out of every file except `main.c` (that last still holds: they appear only in
`main.c`).

### 5.6 Two open issues, both found while checking, neither fixed unilaterally

1. **Parse recovery rule 2 is not local.** A malformed statement swallows the *rest of the
   function body*, including valid statements. Reproducer: `foo(a); for(var i = 0; i < 3; i++)
   { bar(a); } baz(a); return;` emits `gosub inl[:foo]` + `incr undefined` and stops — `bar(a)`,
   `baz(a)`, `return` are gone, and the reported `function "f" missing return` is a consequence of
   the eaten `return`. `for(...)` is invalid Fate so no fixture hits it, and `for` is not a keyword
   (`for(a);` lowers like `foo(a);`; `var for = 1;` is an ordinary variable), but that is evidence
   about *probes*, not a proof that valid source cannot reach the path. **Therefore
   `p12_forloop` stays an open issue and must not be described as conformant** (decision 3).
   Closing it needs either local recovery re-measured against the oracle, or an oracle-generated
   golden pinned as a permanent divergence.
2. **Unresolvable `.fate` include: CLOSED 2026-10-28 — abort, exit 2, nothing written.** It used
   to give `Failed to load include …` + `11: unknown function helper`, exit 3, output written. The
   reference was then measured with the built oracle instead of read off the source: it also stops
   and writes nothing (`Include.resolve` is called outside the read `try`, the `catch (final
   Throwable t)` at `Shell.java:236` swallows the exception, and `Files.writeString` never runs),
   but reports **exit 0**. Design doc §9.2 and §7.7 carry the measured output. Pinned as
   `c4`–`c8`; the trailing-space gate that `p13` depends on is untouched and still verified by
   `c6_trailing_space_gate`.
   **What is NOT closed:** whether an *unreadable but resolvable* include (a `.fate` file that
   exists and cannot be read) should keep the reference's collect-and-continue shape. That path is
   unexercised — no fixture, no probe, no measurement — and the reference's `Failed to load include
   <exception>` message for it is unverified. Do not "align" it without measuring first.

---

## 6. Decisions already taken — do not re-litigate

1. **R1 (2026-10-27):** match Script-Recompiler's observable behaviour; goldens are normative.
2. **R1 refinement (2026-10-28):** `p12_forloop` is *invalid source*; its golden is ANTLR
   error-recovery output (`gosub inl[:for]` would not assemble). Pinned as `deviation`. The
   0/1/2/3 exit contract is **not** overridden.
3. Golden test defaults **ON** (`FATE_ENABLE_GOLDEN_TEST=ON`); a zero-test default build was
   rejected as coverage.
4. Fate CI = **dedicated job**, not bolted onto the engine matrix; compiler choice made by a
   **feature probe** with a non-vacuity guard (must fail under `-std=c17 -Werror
   -pedantic-errors`).
5. Python harness (`run_golden.py`) is the retained acceptance harness; the old `test_diff.c`
   plan is amended in the docs — specs that still cite it are stale.
6. assemble/decompile/strip/genpatch/versions stay deferred.
7. C style in `fate/` = psx-style snake_case, **no** `fate_` prefix. Until 2026-10-28 this was
   enforced by nothing: `check_naming.py` scanned only `.h/.hpp/.cpp/.cc`, and its acronym rule
   cannot see camelCase anyway. It now scans production C and has a grammar check for it (§5.5).
   The naming spec's "documented + reviewed, not lint-enforced" line described an arrangement
   that had already failed, which is why it was replaced rather than obeyed.
8. Project default stays C17; only the `fate` target is C23. Measured fact: **all 7 fate `.c`
   files currently compile clean under `-std=c17`** — nothing requires C23 yet; the probe guards
   the declared standard for future use.
9. **Ruling of 2026-10-28 (three decisions, implemented or filed this session).** (a) A missing or
   non-directory `-L` path is an I/O error: exit `2`, message names the path, no output; the
   reference's exit `1` is *not* reproduced, because R1 covers diagnostics and emitted text, not
   the exit contract. (b) Diagnostic codes and compile-diagnostic columns stay **native
   enhancements in an additive channel**; the oracle governs semantics only; no fabricated source
   locations. (c) A pinned deviation is allowed only after checking that it does not alter
   valid-source semantics — and for `p12` that check **did not establish** equivalence, so the
   issue is open (§5.6). Nothing in this ruling licenses expanding scope: implementing a
   `for`-loop desugaring, matching ANTLR recovery in general, and adding `FATE-E000x` strings are
   explicitly *not* authorised by it.
10. **Ruling of 2026-10-28 (fatal include, implemented after measurement).** An unresolvable
    `.fate` include aborts the compile: exit `2`, one message naming the missing file and the unit
    that asked for it, no output written, and a pre-existing regular file at the output path is
    removed. An output path that is also the input is refused with exit `2` before any file is
    opened. The `.fate`-suffix gate is unchanged, so `include other.txt;` and `include x.fate ;`
    are unaffected. The reference was measured first (built oracle, six probes — design doc §9.2):
    it also writes nothing in every one of those cases, and the only thing not reproduced is its
    exit status (`0` for an unresolved include, `1` for an unreadable `-L`), which the contract
    treats as a failure class to be raised, not matched. Pins are `c4`–`c9` in `cli_contract`.

---

## 7. Costly gotchas (each one is a bug that was actually found)

- `Str` is **not** NUL-terminated. Passing `.p` to `%s` prints the slice plus the rest of the
  file. Three sites did this.
- `memcpy(data, "str[", 5)` copied the literal's NUL into the buffer. Byte-exact comparison is the
  only reason that was caught.
- `wait` cloning matches on **opcode 2**, not the name — name matching also catches `wait_cmp`
  (opcodes 3/4), which takes no copy.
- `_expr_`/`_label_` counters are per **file**, not per function (one `FateContext` per file).
- The fixture corpus can be wrong: `p17` was first written with 1 argument against a
  3-parameter function. Fix the fixture, not the compiler, when that happens.
- `SKIP_RETURN_CODE 77` on the test is **required**; without it CTest reads a skip as a failure.
- `run_golden.py` needs `from __future__ import annotations` — the system `python3` here is 3.9
  and `X | None` in a signature raises `TypeError` (which looked like a test failure, not a skip).
- `fate/` is a `check_format.py` SCAN_ROOTS entry and a naming-gate C root. Don't let edits to
  `fate/` drift out of them. Both gates now genuinely read C: `check_naming.py` checks `.c` and
  non-`psx/cpp` `.h` files for the C identifier grammar, so a PascalCase type or camelCase
  function in `fate/src` is a build failure rather than something a reviewer has to notice.
- No RTTI/exceptions crossing boundaries does not apply to `fate` (pure C), but the gates do.
- **Argument-position `N` in a probe script is not a stable way to address a file.** On
  2026-10-28 a stub used `cat > "$6"` to fake compiler output; for the 17 fixtures with no
  extra CLI args `$6` was the *source*, so 18 fixture files were truncated to one byte and
  the suite would still have gone green. `run_golden.py` now hashes every fixture, `.expected`
  and `.diag` before the sweep and fails naming anything that changed. Use `"$@"` matching or
  an explicit `--`-terminated form if you need a stub; and run stubs against a **copy** of
  `testdata`, never the tree.
- **Check for `.git` before assuming there is none.** The repo has existed since 2026-10-28 18:19
  with the fixtures committed, so the one-command recovery above was available the whole time. A
  stale "no version control" note is not a harmless note — it hides the cheapest safety net.

---

## 8. Environment facts

- Reference repo: `~/Documents/GitHub/Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511`
  (read-only, verified unmodified). Oracle scratch: `~/.cache/sc-fate-oracle/src`.
- Meta bundle (not committed): `~/Documents/GitHub/Severed-Chains/patches/meta/` — 5 CSVs. Also
  discoverable via `$FATE_META_DIR`.
- Toolchains present locally: Apple clang 17 (`/usr/bin/cc`), Homebrew GCC 16. Both pass the C23
  probe; both correctly reject `auto x = 1;` under `-std=c17 -Werror -pedantic-errors`.
- Python 3.9.6 (`/usr/bin/python3`) and a newer `python3` on PATH — test harness changes on 3.9.
- **Thermal constraint:** the user's machine throttles. Prefer the cheapest sufficient check
  (`/tmp/falone` fate-only builds take seconds; the full project build is the expensive one).
  Don't start daemons or install toolchains without asking.

---

## 9. Suggested first moves for the next session

1. **Commit the verified state** (the user's call). Twelve paths differ from `dd58586`: eleven
   modified tracked files — `main.c`, `util.c`, `util.h`, `run_golden.py`, `manifest.json`,
   `c1_noincdir.fate` (header comment only), `README.md`, `HANDOFF.md`, `testing.md`,
   `fate-compile-design.md`, `progress.md` — plus the new untracked `c9_midstream.fate`. Together
   they cover the fatal-include
   abort, the two new guards, the six new `cli_contract` entries and the doc corrections.
   Re-verified after all of it: `run_golden.py` exits 0 (18/19 + 1 deviation, 9/9 CLI), both repo
   gates clean, whole-project CTest **30/30** from a fresh configure at `/tmp/scb`. That 30/30 is
   now post-edit evidence; the in-tree `build/` directory cannot be used for it (stale cached
   source path, see `testing.md`). Pushing to `origin` is what would make CI run for the first
   time, and that is a separate decision from committing.
2. **Ask the user about 5.2 / 5.3 / 5.4** (git + CI execution + synthetic meta) and about the two
   issues in **§5.6** — the `p12` recovery non-locality is still open, and the include question
   that was open beside it is now closed. Do not build anything that assumes an answer.
3. Whatever you change: re-run `run_golden.py`, both gates, and `ctest` (expect 30/30). A
   change that makes 19/19 match is *suspicious*, not good — it means the p12 pin went stale.
   The harness now also fails if any input file changes during a run, so that check needs no
   human memory anymore.
4. **Never run a probe against the real `testdata` tree**, and never let a stub write to a
   positional argument that might be a source file (see §7). `cli_contract` puts `-o` inside a
   private temporary directory for exactly this reason; hand-run probes must do the same.