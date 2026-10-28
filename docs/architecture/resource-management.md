# Resource Management — Discovery, Ownership, Validation

**Scope (Milestone 3):** the engine layer that finds, owns and validates game
resources on disk — `lod::res` (`src/engine/include/lod/resources.h`,
`src/engine/src/resources.cpp`), plus the `psx` decoders it validates through
(psx-library.md §2.1) and the bounded file access in `lod::fs`.

This is deliberately *not* the asset pipeline (no decoding to GPU/audio formats,
no loading for a scene) and *not* the mod system. It is the layer those features
will sit on, and it is the layer that can already answer, honestly and
diagnostically: *"is this an extracted game tree, what is in it, and is each file
structurally valid?"*

## 1. What is on disk

The tree being read is the **extraction cache** the pipeline builds from the
player's discs — never the discs themselves (research/asset-pipeline.md §4 keeps
source, cache and mod overrides as three separate things).

```text
<root>/version                    extraction format version ("5")
<root>/SECT/DRGNn.BIN/<dir>/<file>  DRGN archive contents
<root>/DRGNn.BIN                    root MRG containers (sector-aligned offsets)
<root>/SUBMAP/*.tim                 submap assets pulled out of OVL/SMAP.OV_
<root>/OVL/*.OV_                    engine overlays (DEFF containers among them)
<root>/XA/…                         CD audio (transcoded; not read here)
```

**Root recognition.** A root is recognized when it carries `SECT/`, the marker
every runtime asset path hangs off. `version` is read and reported (its value is
the extraction format version), and `SUBMAP/`, `OVL/`, `XA/` are reported as
optional. Nothing is *required* beyond `SECT/`: a partially unpacked tree is a
real state an operator can be in, so `RootInfo` reports flags and human-readable
notes rather than collapsing to a single boolean.

**`ValidateRoot` is total.** "Absent" and "not a directory" are answers about the
filesystem, not I/O failures, so they come back as a populated `RootInfo`
(`exists == false`, plus notes). Only a directory that cannot be *listed* returns
an error (`Status::IORead`). Whether a root is usable is a *policy* decision, and
policy lives in `ResourceManager::Open`, which rejects a non-game root with
`Status::NotFound` after logging the notes.

## 2. Ownership and lifecycle

```text
ResourceManager::Load(rel)  ->  ResourceBlob (owns bytes)  ->  psx parser views
        (nothing cached)             (move-only, RAII)          (borrow the blob)
```

- **`ResourceBlob` is the only owner.** Move-only; `Bytes()` hands out a span
  into its own buffer; `Release()` frees immediately and the destructor does the
  same, so release is deterministic and testable.
- **The manager keeps nothing.** No cache, no handle table, no refcounts, no
  generation counters. Acquisition is a bounded read; releasing the blob releases
  the memory. Repeated acquisition is therefore cheap and predictable, and there
  is no lifetime question about spans a psx parser returned: they are tied to a
  blob the caller still holds.
- **Validation does not retain data.** `Inspect()` loads, parses, produces a
  plain-data `Inspection` summary and releases the buffer, so validating a whole
  tree needs one file's memory at a time (verified by the CLI pass over a tree
  containing a 2 MiB-scale container).

Deliberately absent, with the trigger for adding each:

| Not built | Why | Add it when |
|---|---|---|
| Asset cache | No consumer yet; a cache without a measured reload pattern is a memory leak with a nice name | The renderer/audio layers request the same resource repeatedly |
| Async loading | Nothing to overlap yet (no frame budget to protect) | First real load-on-demand path (M6+) |
| Refcounted handles / weak handles | `ResourceBlob`'s single-owner model needs none | A cache exists and multiple owners become real |
| Mod-override stacking (mod → mod → base) | Mod loading is a separate milestone; a search path with one root is the same code with one element | Mod loader lands (mod-loader.md) |
| XA / audio / texture *decoding* | Out of this milestone's scope | M5 (audio) / M4+ (rendering) |

## 3. Path confinement

Resource paths come from game data, archives and (later) mods, so they are
untrusted input:

- `Load()` rejects empty paths, absolute paths, and any path containing a `..`
  component **before** touching the filesystem (`Status::InvalidArgument`), which
  also means no symlink is ever resolved during the rejection check.
- On Windows a non-empty `root_name()` (drive/UNC) is rejected too, because
  `is_absolute()` does not cover every rooted form there.
- The manager joins the caller's path onto its root; it never accepts a second
  root argument.

`Scan()` walks with `recursive_directory_iterator` without following directory
symlinks, so a link loop cannot wedge discovery; permission-denied entries are
skipped rather than aborting the walk.

## 4. Bounded I/O

`lod::fs` gained two binary reads with *different* contracts, and the difference
is the point:

| Function | Ceiling semantics | Used by |
|---|---|---|
| `ReadFileBytes(path, maxBytes)` | An oversized file is **rejected** (`Status::IOTooLarge`); the size is checked before allocating and again from the stream | `Load()` (per-file ceiling, default 64 MiB) |
| `ReadFilePrefix(path, maxBytes)` | The read is **truncated** to `maxBytes`; a short file is not an error | `Scan()` header sniffing (16 bytes) |

A single "read at most N" helper would have made either behaviour wrong: header
detection must not fail on a 4 MiB asset, and asset loading must not silently
hand back a truncated file that a decoder then reports as "corrupt".

Short reads (file shrank, special file) are `Status::IORead`, never a partial
buffer: a truncated asset is indistinguishable from a malformed one downstream.

## 5. Content kinds and validation

`DetectKind` classifies by header only — BPE by the u32 at 0x4, MRG/DEFF/TIM by
the u32 at 0x0 — mirroring how the extraction pipeline picks its transformer.
`ResourceKind` is `Raw | BPE | MRG | DEFF | TIM`; `KindName` gives the lowercase
label used in logs and CLI output.

`Inspect` / `InspectBytes` run the matching `psx` parser and produce a summary
that states parsed facts:

```text
TIM 4bpp, image 4x2 (16 B), 3 B trailing, clut 4 entries (8 B)
MRG 1 entries (0 virtual, 1 readable, 4 B payload, sector-aligned offsets)
DEFF 2 subfiles (2 readable, 14 B payload)
BPE 540 B -> 9 B
raw 22 B (no supported header)
```

Rules that keep those claims honest:

- **Sector alignment is a property of the path, not the bytes.** Only a
  root-level `DRGN(?:0|1|2[1234])?.BIN` first component selects sector-number
  offsets (research/psx-formats.md §2); the same bytes under `OVL/` are parsed
  with byte offsets. The match is case-insensitive because the tree also lives on
  case-insensitive volumes.
- **An entry pointing outside its container is a failure,** not a warning:
  `Inspect` reports `Status::DecodeCorrupt` for it.
- **Headerless data inspects successfully as `raw`,** reporting size only. A
  parser that claimed structure it cannot read would be worse than no parser.
- **A BPE segment declaring more than the inspect limit is reported, not
  inflated** (`"… (not decompressed: over the N B inspect limit)"`), so
  validation cannot be turned into an allocation attack.
- **`psx::Error` maps to coarse `Status::Decode*`** (`DecodeHeader`,
  `DecodeTruncated`, `DecodeSizeMismatch`, `DecodeUnsupported`, `DecodeCorrupt`)
  while the specific decoder code is logged next to it: subsystem boundaries
  carry the coarse code, humans get the precise cause.
- **I/O failure is never dressed up as a decode failure.** Missing file,
  directory-instead-of-file and oversize keep their own codes
  (`NotFound`, `NotFound`, `IOTooLarge`).

## 6. Diagnostics

Every failure path logs through the caller-supplied `Logger*` (nullable — the
manager is usable without one, which keeps it testable and embeddable):

```text
WARN [resources] TIM parse failed for 'SUBMAP/depth.tim' (52 B): TIMBPP -> DecodeUnsupported
WARN [resources] resource not found: /path/root/SUBMAP/never-written.tim
WARN [resources] rejected resource path (must be relative and stay inside the root): ../../etc/hosts
INFO [resources] inspected 'DRGN0.BIN': MRG 1 entries (0 virtual, 1 readable, 4 B payload, sector-aligned offsets)
```

Path, size, the decoder's own error name, and the mapped engine status: enough to
find the file and the reason without a debugger. `InspectBytes` is also available
purely in memory, which is the seam tools and future golden tests use.

## 7. Runtime surface (host)

`src/host/src/main.cpp` is the composition root; the asset commands are host-side
reporting over engine services (no engine logic in the host):

| Flag | Behaviour |
|---|---|
| `--resources <dir>` | Resource root, overriding everything else |

Resource-root precedence is the same order as every other setting:
`--resources` → `config.txt` `resource_dir` → resolved default
(`lod::fs::ResolvePaths`, which itself honours `SC_RESOURCE_DIR`). The source
that won is logged. Asset commands never create or rewrite the config file —
they are read-only by contract.
| `--validate-assets` | Validate the root, scan it, validate every file, print per-kind counts and failures, then exit |
| `--inspect <path>` | Validate one root-relative resource and print its summary (repeatable) |

Exit codes: `0` ok, `4` root unusable, `5` at least one resource failed to
validate. Report lines go to stdout (flushed at section boundaries so they keep
order against the logger's stderr output); failures go to stderr.

No renderer, audio device or game subsystem is initialized for these commands —
they are pure filesystem operations, which is what makes them usable in CI and on
a machine with no playable data.

## 8. Verification

| Area | Cases | Where |
|---|---|---|
| BPE decode/encode/rejects | 9 | `psx/tests/test_bpe.cpp` |
| TIM parse/validation + authentic asset (opt-in) | 11 | `psx/tests/test_tim.cpp` |
| MRG/DEFF tables, virtual entries, sector scaling, rejects | 10 | `psx/tests/test_archive.cpp` |
| Library smoke + error-name stability | 2 | `psx/tests/test_smoke.cpp` |
| C++ wrapper contract (delegation, span aliasing, move-only RAII, error propagation) | 8 | `psx/tests/test_wrapper.cpp` |
| C header usable from C++ under `extern "C"`, layout/linkage stability | 1 | `psx/tests/test_c_linkage.cpp` |
| Pure C17 consumer: every entry point, every error code, NULL rejection, malformed contract | 261 checks | `psx/tests/c_consumer/main.c` |
| Exported-symbol audit (39 exports, no C++ symbol in or out) | — | `psx/tests/abi_symbols.cmake` |
| Old-vs-new equivalence corpus (opt-in) | 4 | `psx/migration/differential/` |
| Kind detection, root validation, ownership, path confinement, I/O taxonomy, scan, inspect, decode mapping, diagnostics | 15 | `tests/engine/test_resources.cpp` |
| Bounded binary reads | 4 | `tests/engine/test_fs.cpp` |

`psx` also builds and tests standalone (`cmake -S psx -B <dir>`), which is the
enforcement of its independence rule, and the whole suite runs clean under
ASan/UBSan and under both exception policies. The decoders behind the engine's
validation are the C17 core reached through `psx::cpp` — see psx-c-api.md and
`docs/reports/psx-c17-migration.md`.

## 9. Open questions

- **No retail game data locally.** Fixtures are synthetic (and, for TIM, one
  authentic non-retail asset). The layouts are verified against the reference
  readers; a retail `DRGN0.BIN` pass is still owed (research/psx-formats.md §6).
- **`version` mismatch handling.** The pipeline wipes and re-extracts on a
  mismatch; the reader only reports the value. Deciding what the *runtime* should
  do (refuse? warn? trigger re-extraction?) belongs to the extraction milestone.
- **Where `ResourceDir` should point by default** once a native extraction cache
  exists (fs::Paths already separates config/data/resource; the cache layout is
  not decided).
- **Large-container streaming.** `MRGEntryData` slices a resident buffer. Reading
  a sub-range of a multi-megabyte container without materializing it is a real
  future need (the reference has a file-backed low-memory unpacker for the same
  reason) and is the likely first change to this ownership model.
