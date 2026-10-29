# `psx/testdata` — committed differential fixtures

Files here are **test inputs plus expected outputs**, not game data. Anything
derived from a retail disc is excluded from this directory by design (see
"Adding a fixture" below); the project never distributes proprietary content.

Unlike `fate/testdata/`, this tree is **not** frozen-read-only against probes.
No test writes here. `test_xa.cpp` only reads `xa_adpcm.bin`.

## `xa_adpcm.bin`

XA 4-bit ADPCM differential fixture: 17 sectors, each pairing an input sector
with the PCM that the Java reference produced for it. 217 782 bytes.

A test that cannot read this file **fails**, it does not skip. The input is
synthetic, so its absence means a broken checkout rather than an optional asset
that happens to be missing. This is the deliberate difference from the
`SC_PSX_ASSET_DIR` path in `docs/architecture/testing.md` §2.2, where a missing
retail bundle is expected and skips with a visible message. `PSX_XA_FIXTURE` is
therefore always defined in `psx/tests/CMakeLists.txt`, while
`SC_PSX_ASSET_DIR` stays optional.

### Container

Little-endian throughout.

```text
"PSXXAFIX"        8 bytes  magic
u32 version                currently 1
u32 count                  number of cases  (17)
per case:
  u8  name_len
  bytes name               case name, no NUL
  u8  coding               coding-info byte passed to the decoder
  u32 sector_len           input sector length in bytes
  u32 pcm_len              **expected PCM length in BYTES**, not in samples
  bytes sector             input
  bytes pcm                expected output
```

`pcm_len` is a byte count. Reading it as a sample count halves the payload and
silently compares against the wrong window; `test_xa.cpp` treats it as bytes and
`ToSamples()` does the `/2`. Stereo is 8064 bytes (4032 samples), mono is 16128
bytes (8064 samples, written twice because mono duplicates to L=R).

All 17 cases use a full 2352-byte sector. Short-input rejection is **not**
represented here — it is pinned by hand-built undersized arrays inside
`test_xa.cpp`. For reference, the shortest sector the decoder accepts is 2328:
24 bytes of sync/header skipped plus 18 × 128-byte portions; the trailing 24
bytes of a real sector are EDC/ECC that neither the reference nor this port
reads.

### Case families

| Family | What it pins |
|---|---|
| `silence*` | zero nibbles — proves the decoder is not inventing signal |
| `badshift13/14/15` | `shift = 12 - (b & 0x0F)` going negative, i.e. Java's `<< (shift & 31)` |
| `saturate_f0/f1/f2/f3` | clamp rails at each of the four reachable filter settings |
| `random_stereo*`, `random_mono*` | long runs of nonzero nibbles; the mono cases also pin L=R duplication |

Aggregate coverage of the 17 cases: 88 704 expected samples, 63 094 of them
non-zero (71.1%), 17 375 clamp-rail hits; shift values -3..12 and all four
reachable filter settings appear. Chained runs (5 stereo, 3 mono sectors)
carry predictor state across sector boundaries. `test_xa.cpp` additionally
counts how many cases change output under carried state, so that a future
regeneration that quietly drops all state-sensitive cases fails rather than
passing vacuously.

### Regenerating

```sh
python3 psx/tools/xa_oracle.py ~/Documents/GitHub/Severed-Chains psx/testdata/xa_adpcm.bin
```

Requires JDK 21+ and a `fastutil` jar. The script compiles the reference
`XaAdpcm.java` in a scratch directory against a `ShortArrayList` shim and runs
it — the expected PCM is produced by the oracle, never hand-written. The
reference repository is only ever read.

Regenerate only when `XaAdpcm.java` actually changes, and re-check the diff:
every changed byte is a claim that the reference changed its behaviour.
