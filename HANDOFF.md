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

`fate/testdata/` = `fixtures/` (19 `.fate`), `golden/` (19 `.expected` + 19 `.diag`),
`manifest.json`, `run_golden.py` (the harness), `README.md`.
`tools/fate_oracle.py` = oracle driver. `.github/workflows/ci.yml` = 20 legs (17 engine + 3 fate).

---

## 4. Behavioural contract (read before "fixing" something that is intentional)

**Exit codes** `0` clean / `1` usage / `2` I/O / `3` compiled-with-diagnostics. This is a
*deliberate deviation* from the reference, which exits 0 even when it logs errors. `p15` exits 3
here and `exit=0` in `p15_argcount.diag` — that is correct, not a bug.

**Two parity modes** in `manifest.json`: `"match"` (18) needs byte equality; `"deviation"` (`p12`)
needs exit 3 + no crash + output *differs*. Mismatches on **valid** source are defects.

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

Still open here, and deliberately not "fixed": `p12_forloop`'s missing pair (`return lists
are not the same size`, `unknown function for`) exists in the reference only because ANTLR
recovers `for(...) {` as a call to a function named `for` — pinned as `"diag": "unpinned"`
and printed as NOT VERIFIED. The reference emits **no diagnostic codes** anywhere in the
19 `.diag` files, so the "stable diagnostic codes" half of `spc-1793131198334-17871` has
nothing to match against; inventing our own would be a new divergence under R1. Specs
`spc-1793131198334-17871` and `spc-1793139055396-44131` can move from *open* once the
column/code question is ruled on, not before.

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

### 5.4 Nothing from this session is committed

The repository exists (see the header), but everything from this session — the four parity fixes,
the harness rewrite, the manifest changes and the restored fixture corpus — is uncommitted
working-tree state (9 modified files, no new ones). HEAD contains the **pre-damage** fixtures, so
`git checkout -- fate/testdata/fixtures` would have restored the corpus in one command. The manual
restore from the `/tmp` snapshot happens to be byte-identical to HEAD (empty `git diff` against
HEAD for all 19), which is the integrity proof — and also luck, not process. Committing is the
user's call; no destructive git operation may be used to "fix" mistakes.

### 5.5 Then F3

CLI surface: `--help`, argument validation, "missing required arg" messages, and keeping
`exit()`/`abort()` out of every file except `main.c`.

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
7. C style in `fate/` = psx-style snake_case, **no** `fate_` prefix (exempt from
   `check_naming.py`'s acronym rule via `is_snake_or_macro`).
8. Project default stays C17; only the `fate` target is C23. Measured fact: **all 7 fate `.c`
   files currently compile clean under `-std=c17`** — nothing requires C23 yet; the probe guards
   the declared standard for future use.

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
  `fate/` drift out of them.
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

1. **Commit the verified state** (the user's call). Nine modified files sit uncommitted, including
   the four parity fixes and the harness rewrite. Before committing, re-run both gates and `ctest`
   at the root — the 30/30 figure is pre-edit evidence. Pushing to `origin` is what would make CI
   run for the first time, and that is a separate decision from committing.
2. **Ask the user about 5.2 / 5.3 / 5.4** (git + CI execution + synthetic meta) before
   building anything that assumes an answer.
3. Whatever you change: re-run `run_golden.py`, both gates, and `ctest` (expect 30/30). A
   change that makes 19/19 match is *suspicious*, not good — it means the p12 pin went stale.
   The harness now also fails if any input file changes during a run, so that check needs no
   human memory anymore.