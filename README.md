# Severed Chains — Rust workspace

Reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).
Planning records live in [`planning/`](planning/), the research they rest on in
[`research/`](research/). This file covers the two things a person sitting down at the code needs
immediately: how to run the tests, and how the golden test data gets to a test.

## Crates in the workspace

Three crates exist. This table is the tree as it stands after I1a, not the target layout.

| Crate | What it holds today | Depends on |
|---|---|---|
| `psxrt` | PSX platform mechanisms: fixed-point and trig, the three RNG streams, texture-depth addressing, blend arithmetic, frame cadence | nothing |
| `psxrt-compat` | Three writers, all of them test oracles: a BPE encoder, an MRG container writer, a `files/` cache index writer | nothing |
| `psxrt-import` | Import-side readers: the MRG container, the BPE decoder and its passthrough, the `files/` cache index, and the patch orchestration layer | `thiserror`; and for tests only, `psxrt-compat` and `proptest` |

**The dependency rule, and why it is a test.** No crate whose purpose is running the game may
depend on `psxrt-compat`. The `psxrt-compat → psxrt` edge that existed at I0 is gone, deleted with
the nine policy files the 2026-11-15 revision retired, so the crate restarts with zero project
dependencies. The only edge into it is a `dev-dependency`, and its direction is enforced twice:
cargo rejects a package-level dependency cycle outright, and
`crates/psxrt-import/tests/dependency_direction.rs` fails if any runtime crate names
`psxrt_compat` in a code path. A toolchain crate that depends on nothing cannot be dragged down to
a device; one that a runtime crate depends on can.

`psxrt-compat` is still not a place to put a runtime switch, and it is not on the boot path. What
it holds is now the thing its name asks for — original-format output — rather than a scaffold
describing that intention.

The other three names in the approved layout (`severedchains`, `severedchains-sdk`,
`severedchains-game`) are not created yet; nothing is stubbed for them.

## Running the tests

```
make test              # prepare golden test data, then cargo test --workspace
make check             # the same, plus the file-ending audit — this is the milestone gate
make test-prepare      # data only
make check-fixtures    # verify the data without copying anything
make clippy            # lints over library and test code
make citations         # re-check research-code citations (opt-in; needs the reference repos)
```

A bare `cargo test --workspace` also passes: no test needs a file at run time that a fixture
copy would have supplied. The copy step exists for the run-time readers described below, and
`make test` runs it so the two paths cannot drift.

## Test data contract

### Two kinds of test data, and only one of them is in this repository

**Golden fixtures** are committed. Each one holds output from an *independent oracle* — a Java
program that transcribes the reference expression, or a second implementation written for the
purpose. A test that computed its expected values from the Rust code under test would prove
nothing, so the expectation comes from outside the crate and the comparison is a differential
check.

**Real ROM data** is not committed, not downloaded, and not present. Later milestones need
`DRGN1.BIN`-class files for on-disk checks; when those arrive they are supplied by the operator
out of tree, and the test code will skip cleanly when the data is absent rather than fail. The
reason is a project constraint, not an oversight: no proprietary asset may enter this repository.
Everything committed under a `testdata/` directory is therefore machine-generated from a
published algorithm, never extracted from a disc.

### Two paths, and why both exist

| Role | Path | Read by |
|---|---|---|
| canonical | `crates/<crate>/testdata/` | `include_str!("../testdata/<file>")` — compiled into the test binary |
| target | `target/testdata/<crate>/` (or `$CARGO_TARGET_DIR/testdata/<crate>/`) | a test that needs a *path* at run time, for data too large to embed or read from disk |

Keeping one canonical copy and one copied-to-target copy only works if the copy is automated and
checked, which is what `scripts/prepare_test_fixtures.sh` is for. It copies by reading each file's
bytes and rewriting them at the destination (so a symlinked fixture is copied as its content, and a
symlink can never hide a difference), then verifies the canonical files **and** the copies against
`crates/<crate>/testdata/fixtures.manifest`. `--check` verifies without copying; `--write`
regenerates the manifest and is deliberately a separate, explicit act, so that refreshing a
fixture is something a person decided to do rather than something a test run did quietly.

### Verification, three layers

1. `crates/psxrt/tests/fixtures.rs` pins the inventory (names, count, byte size, FNV-1a
   fingerprint) in test code, checks the on-disk files against it, checks the *embedded* bytes
   against it, and checks that any target copy present is byte-identical to the canonical file.
   This is what makes "the test compares against the oracle output" a statement about the binary
   that actually ran.
2. `scripts/prepare_test_fixtures.sh --check` is the same check from outside, and is what a
   clean checkout or CI runs before building.
3. The golden tests themselves fail loudly if a fixture changes underneath them — but they fail
   with a value mismatch, which reads like a code bug. Layers 1 and 2 exist so that a changed
   fixture is reported as a changed fixture.

### Refreshing a fixture

Regenerate from the oracle under `crates/psxrt/testdata/generators/`, then confirm the new
output is byte-identical to the committed file before touching anything. If it is not identical,
the oracle or the implementation drifted, and that is the finding; the fix is the finding, not
the refresh. Only after that, `scripts/prepare_test_fixtures.sh --write` and update `PINNED` in
`crates/psxrt/tests/fixtures.rs`. Never regenerate in order to make a failing test pass.

To regenerate (JDK 21 or newer; single-file source launch, run from `crates/psxrt/testdata/generators`):

```
java GoldenRng.java ..            # writes rng_random48.txt, rng_lcg16.txt, rng_lcg15.txt
java GoldenTrig.java ..           # writes trig_grid.txt, trig_strictmath_divergences.txt
java GoldenTiming.java ../timing_matrix.txt ../script_gate.txt
java Random48Probe.java           # probe, not a generator: prints comparison counts
```

The generators write into the directory (or the two files) you name; compare the result against
the committed file before replacing it. `Random48Probe.java` is not a generator — it is the
standalone probe that established the 48-bit generator's identity against `java.util.Random`,
kept so the identity can be re-established on a different JDK.

## Test data inventory

Seven fixtures, all in `crates/psxrt/testdata/`, all consumed by `psxrt` tests.
`psxrt-compat` has none: its one test file reads two research documents for citation
traceability, which is not test data in that sense. **Expected total: 7. Expected in `psxrt`: 7.
Expected in `psxrt-compat`: 0.** Pinned in `fixtures.rs` (`PINNED`, and a count assertion) and
in `fixtures.manifest`. A later milestone may give an export module a fixture — a TIM written from
a known image, a CTMD re-encoded from a known model — and that would be the crate's first real
test data; the census assertion is the place that expectation lives, so it changes with the
milestone rather than silently.

| File | Rows | Consumed by | Oracle behind it |
|---|---|---|---|
| `rng_random48.txt` | 48,000 draws | `tests/golden_rng.rs` | `generators/GoldenRng.java`, one continuous stream replayed in file order, so state consumption is pinned too |
| `rng_lcg16.txt` | 12,000 steps | `tests/golden_rng.rs` (two tests) | `generators/GoldenRng.java`, with the reseed flag recorded per step |
| `rng_lcg15.txt` | 12,000 steps | `tests/golden_rng.rs` | `generators/GoldenRng.java` |
| `trig_grid.txt` | 16,385 angles (θ = −8192…8192) | `tests/golden_trig.rs` | `generators/GoldenTrig.java` |
| `trig_strictmath_divergences.txt` | header only | `tests/golden_trig.rs` | `generators/GoldenTrig.java` — the empty set is the finding: `Math` and `StrictMath` agree across the whole grid |
| `timing_matrix.txt` | 352 rows (22 vsync × 16 speed) | `tests/timing_matrix.rs` | `generators/GoldenTiming.java`, which transcribes the reference's clamp, its `60 / frames * speed` rate, and its unclamped clock increment |
| `script_gate.txt` | 3 divisors | `tests/timing_matrix.rs` | `generators/GoldenTiming.java` |

Beyond the fixtures, three test files carry expectations derived from the reference's own
expressions rather than read from a file. `tests/blend_exhaustive.rs` pins five 64-bit FNV-1a
digests — one per blend mode (there are four) plus one for the `T × C >>> 7` modulation — each
computed once outside this repository from the Java expressions, and the 9,918 pairs at which the
modulation saturates. `tests/bpp_exhaustive.rs` sweeps every word and every slot at each paletted
depth against the reference's packing expressions. `tests/timing_matrix.rs` replays its two
fixtures *and* restates the reference's clamp, truncation and composition expressions in code.
Those expectations live in the test file because they are small, and the independence is the
point: a digest computed by the code it checks would be worth nothing.

### Fixtures shared between crates

None today: `psxrt-compat` has no fixture-reading test, and `crates/psxrt-compat/testdata/` does
not exist. `tests/fixtures.rs::the_fixture_counts_are_the_expected_ones` pins that, so the
numbers above are checked rather than remembered.

The rule for when later milestones add data is explicit, because the roadmap already schedules
fixture data in two crates for one and the same mechanism: the ADPCM core. The worked example in
`research/psx/04-xa-adpcm.md` §5 feeds the codec in `psxrt` (rows X2/X3, and the S5 block goldens,
which planning/06 says share that codec core) and it also feeds `psxrt-import`'s XA extraction and
transcode fixtures (I1). **That is a shared oracle, not a shared file** — each crate gets its own
fixture from its own generator, because the two sides need different shapes of the same example.
Fixtures therefore stay in per-crate directories with per-crate manifests, which is what the
prepare script iterates over, and the inventory test fails if a file appears where no manifest
entry expects one. If a fixture is ever deliberately shared, it is declared shared in both
manifests and a test then checks the two copies against one fingerprint — sharing is decided,
never inherited from a filename.

## Conventions worth knowing before editing

- Every text file ends with exactly one LF newline; no CRLF anywhere. `make audit-endings`
  checks this and `make check` fails on a violation. `scripts/audit_file_endings.py --fix`
  corrects line endings only, and never touches binary files.
- The reference repositories under `~/Documents/GitHub/` are read-only reference material. Tests
  must not read them; a fixture that needs reference data has it copied in, generated, and
  fingerprinted.
- Names follow the semantic-naming policy in `AGENTS.md`. A decompiler-style identifier in
  production Rust is a policy violation even when it is spelled in Rust style.
- The two audit scans `planning/05` §8.2/§8.3 ask for — the placeholder-name scan and the
  original-identifier map — are run by hand at I0, not by a tool. The commands, their current
  results, and the map itself live in `crates/psxrt/src/REMAP.md`; an original identifier cited in
  a comment needs a line there. A mechanical scanner is an open item, recorded in the I0 gate
  record in `planning/05` §6.
- `crates/psxrt-compat/tests/policy_ledger.rs` embeds the two research documents its ledger cites
  (`research/cross/02-compatibility-checklist.md` and `research/04-unresolved-questions.md`) and
  fails if a citation stops resolving, so a policy cannot quietly outlive its evidence. That job
  survives the ledger's retirement in two pieces: the same citation resolution, taken over the whole
  corpus by `scripts/citation_audit.py`, and a dependency-direction guard that fails if any runtime
  crate names `psxrt-compat` — see `planning/03` §6 and `planning/07` §4/F10.
- Line-number citations in `research/` name a reference revision. **925 of 1 330 were verified
  byte-identical against the reference at `d47ac8637` on 2026-11-15; 154 are open** and itemised in
  `research/citation-exceptions.md` — check there before trusting a quoted line. `make citations`
  re-runs that check (add `SEVERED_CHAINS_REPOS=/path/to/parent`), and it is deliberately **not** part
  of `make check`: with no reference checkout configured it exits 0 having done nothing, so a reference
  checkout can never become a build or test dependency (TD-25). Its `--apply` mode rewrites relocated
  pointers and is **not trusted** — see the register §3.
