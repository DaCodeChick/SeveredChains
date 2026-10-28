# HANDOFF — session transfer (2026-10-28)

Working tree: `/Users/schala/Documents/severedchains` (C++23 engine + `psx` C17 library + the new
`fate` C23 toolchain). **Not under version control** — no `.git`. Backups live in
`~/.cache/sc-backup-*`.

This file is a *transfer of state*, not a spec. Authoritative detail lives in:

- `AGENTS.md` — project rules (read it; it is short and binding).
- `docs/research/fate-toolchain-phase1.md` — reference analysis, §13 = what differential testing caught.
- `docs/research/fate-compile-design.md` — the implementation contract for `fate compile`.
- `progress.md` — chronological log, newest row = 2026-10-28.

---

## 1. Where things actually stand

**`fate compile` (Fate Script → assembly text) works and is differentially tested.**

- 19 fixtures: 18 byte-identical to oracle-generated goldens, 1 pinned deviation (`p12_forloop`).
- `ctest` = **30/30**, and `fate.compile.golden` **actually runs** here (this machine has a meta
  bundle). `30` includes the fate entry; `-DSC_ENABLE_FATE=OFF` gives 29/29 with fate absent.
- Naming gate clean, format gate clean (clang-format 23.1, 80 files), build has **zero warnings**
  under `-Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -pedantic`.
- Everything verified on **macOS/arm64 only**. No CI leg has ever executed. Do not upgrade that
  fact into "cross-platform verified" anywhere.

**Not done:** `assemble`, `decompile`, `strip`, `genpatch`, `patch`/`undopatch`, `versions`
(deliberately deferred — no scaffolding exists, by decision, not by omission).

---

## 2. Commands that work right now

```bash
# fate standalone (fast, no SDL/engine deps)
cmake -S fate -B /tmp/falone -G Ninja && cmake --build /tmp/falone

# whole project
cmake -S . -B /tmp/rootb -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build /tmp/rootb
ctest --test-dir /tmp/rootb            # 30/30

# the differential directly (this is the acceptance test)
python3 fate/testdata/run_golden.py /tmp/falone/fate fate/testdata

# the two repo gates
python3 tools/check_naming.py
python3 tools/check_format.py

# run one fixture by hand
/tmp/falone/fate compile -m ~/Documents/GitHub/Severed-Chains/patches/meta \
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

**Two parity modes** in `manifest.json`: `"exact"` (18) needs byte equality; `"deviation"` (`p12`)
needs exit 3 + no crash + output *differs*. Mismatches on **valid** source are defects.

**Output invariants** (all observable, all currently matched): blank first line, trailing space on
zero-parameter ops, `_expr_`/`_label_` counters run **per file**, `str[]` string slots, `wait`
operand cloning.

**Ruled, do not "correct":** `0xFF` lexes as `0` + ident `xFF`; `true` is `unimplemented op true`
(no output file, exit 3 — the reference's NPE is a defect and is *not* reproduced); `:NAME` labels
are not compiler input; `for` is not in the grammar; string arguments become
`inl[:f__expr_N]` slots, not inline strings.

---

## 5. Open work, in priority order

### 5.1 Diagnostic text is not compared — and one case is genuinely wrong ⚠️

The documented acceptance method (design doc §12) is "byte-compare output **and compare the
diagnostics to `<id>.diag`**". Only the first half is implemented. Consequence, reproduced today:

```bash
/tmp/falone/fate compile -m ~/Documents/GitHub/Severed-Chains/patches/meta \
  fate/testdata/fixtures/p9_tab.fate -o /tmp/y.txt 2>/tmp/err.txt
wc -c < /tmp/err.txt    # 0  <-- we print nothing
cat fate/testdata/golden/p9_tab.diag
# exit=0
# produced_output=yes
# line 4:0 token recognition error at: '\t'
# line 5:0 token recognition error at: '\t'
```

`p9_tab.fate` contains 2 tabs; the reference emits 2 messages; we emit none and exit 0. R1 ruling
+ the manifest note for p9 explicitly require the message. So this is an **unimplemented R1
clause masked by a green suite**. Two sub-tasks: (a) implement lexer-warning emission,
(b) make the harness compare `.diag` content (it currently checks exit code only).
Also unimplemented: **column numbers** and stable diagnostic codes — the reference prints
`line 4:0 …` (line:column), we print `7: message` (line only). Specs: `spc-1793131198334-17871`
(error handling, **open**), `spc-1793139055396-44131` (R1, **open**).

### 5.2 CI is written but has never run (blocked, needs a decision)

`.github/workflows/ci.yml` has 20 legs. I cannot execute it: no git repo, no runner. Locally I
only dry-ran the step scripts (YAML parses, 16 `run:` blocks pass `bash -n`, the macOS fate
sequence ran end-to-end). **Ask the user**: init a repo and let CI run, or explicitly accept
local-only evidence. Spec `spc-1793042266346-69099` sits at `partial` for exactly this reason —
do not mark it met without a real run.

### 5.3 CI fate legs currently compare zero bytes

The meta CSVs are not committed (retail-derived). Runners have no `Severed-Chains` checkout, so
the 3 `fate-toolchain` legs build the tool and **skip** the golden test (printed as a visible
`NOT VERIFIED` banner, not as coverage). Committing a small **synthetic, non-retail** meta bundle
under `fate/testdata/local-meta/` would make them real. That is a licensing/scope call —
**do not do it unilaterally.**

### 5.4 Nothing is committed

No git repo. Everything since the C17 migration exists only in the working tree plus
`~/.cache/sc-backup-*`. If the user wants history, that needs an explicit decision (and no
destructive git operations to "fix" mistakes).

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

1. Decide + implement **5.1** (warning emission + `.diag` comparison). It is the only place where
   the suite is currently green about something that is not true.
2. Ask the user about **5.2 / 5.3 / 5.4** (git + CI execution + synthetic meta) before building
   anything that assumes an answer.
3. Whatever you change: re-run `run_golden.py`, both gates, and `ctest` (expect 30/30). A change
   that makes 19/19 match is *suspicious*, not good — it means the p12 pin went stale.