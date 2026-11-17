# P0-08 — File-Space Identity and Read Domains

Status: **Approved in outline 2026-11-16; no decision here is exercised by I1a.** Written 2026-11-16,
before any I1a code was written, at the owner's instruction to design the file-space model rather
than implement against an undefined one.

What followed from that instruction is worth recording precisely, because the easy reading of this
paragraph is "approved, therefore built". On 2026-11-16 the owner cut `space.rs`,
`container/iso.rs` and `pipeline.rs` out of I1a and moved all three to I1b. The design-first
requirement is therefore met in the way it was meant to be — nothing was coded against an
undefined model — but it is *not* met by having been coded at all. D-1, D-2 and D-3 remain
proposals that a milestone has now accepted in principle and none has verified. Section 5 is where
that distinction is kept honest, and the four unwritten checks there are the actual precondition
for I1b's file-space work.

Reference pin: all Java citations resolve against `Severed-Chains @ d47ac8637`, verified line by
line on 2026-11-16. No claim below is `asset-validated`; the categories used are defined in
`AGENTS.md` and every one is `source-verified` or explicitly marked as a proposal.

---

## 1. Why this document exists

`planning/04` §I1a and `planning/01` §4 (the module map) describe a file-space layer in terms that
the project has never defined:

| Claimed content | Where asserted |
|---|---|
| a 63-bit address | planning/01:112, planning/04:67, planning/04:330 |
| four kinds (ORIGINAL / DERIVED / GENERATED / DEPENDENCY) | planning/04:67 |
| four namespaces (FILE / DISC / REGION / OBJECT) | planning/04:67 |
| a 4 GiB high-bit wall with two independent guards | planning/04:67 |
| a 16 MiB full-disc fallback above 127 characters | planning/01:113, planning/04:68 |
| a `region_mask & mask == 0x03` predicate | planning/04:103 |
| `disc:` / `region:` / `object:` access paths | planning/01:112 |
| a raw-LZSS variant and a data-driven mode flag (0 raw, 1 BPE, else error) | planning/04:70, planning/04:81 |

Two facts make these unimplementable as written.

**They have no evidence anchor.** Every one of them cites `planning/07 §3.1` or `planning/05 §5.6`.
Neither section exists. `planning/07` §3 is a five-item prose list about what I0 gets right;
`planning/05` §5 is the three-tier determinism contract. Grepping the four reference repositories
for `disc:`, `DERIVED`, `GENERATED`, `DEPENDENCY` returns **zero hits** in Java source. Grepping the
whole `Severed-Chains` checkout for `disc:` returns **zero files**. `bin\` appears nowhere.

**They contradict the checklist row that is supposed to support them.** `planning/04:330` maps the
whole group to risk row 1 and CL-AST-01, which is *disc sector geometry*. Risk row 1's own text
names "**the path-as-identity cache key**" in the same clause as "the 63-bit key". Those are
different models, and the reference supports only the second one.

`planning/04:125` offers the one concrete artifact — `patches/scripts/DRGN1/254.diff` — as proof of
"the `disc:FILE:…`/`bin`/`bin\` shape, an 0x800-byte alignment rule, that some files are compressed
and some are not, and that the mode flag is data-driven". The file is a unified diff of Fate
assembly text. Its header is `--- original` / `+++ modified`. It contains no path, no alignment
rule and no mode flag. **The stated evidence for the entire file-space and decompression model does
not describe the artifact cited.** This is recorded so it is not re-quoted as a rationale later.

Had I1a been implemented as specced, the 63-bit encoding, its guards and its fallback would all
have been invented, and the round-trip test would have been generated from the invention — exactly
the failure that risk row 1 warns is invisible at test time.

---

## 2. What the reference actually does

Three separate mechanisms, all simpler than the model above, all source-verified.

### 2.1 Identity is a path string

The runtime read path is `Loader.loadFileSync(String)` → `Unpacker.ROOT.resolve(fixPath(name))` →
`Files.readAllBytes` (`Loader.java:40,49,291`). There is no numeric key anywhere on it. `fixPath`
does three things and nothing else: truncate after the last `;`, drop a leading `/` or `\`, replace
`\` with `/`.

The patch toolchain agrees. `patches/scripts.csv` names targets as `SECT/DRGN1.BIN/20` — the
container written as a directory, the entry index as the leaf. `ScriptPatcher` resolves every one of
them with `Path.resolve` against `filesDir` / `backupsDir` / `patchesDir`
(`ScriptPatcher.java:109,199,213,247`). No address, no key, no mask.

### 2.2 The `files/` cache is a directory tree plus an ASCII index

An unpacked archive becomes a directory of numbered files plus one `mrg` index
(`Unpacker.java:619`). The index is US-ASCII, one line per entry, newline terminated
(`FileMap.java:8-13`):

```
<index>=<target>;<virtualSize>
```

`target` is the entry's own index for a real entry and its parent's index for a virtual one
(`Unpacker.java:615`). `Loader.loadDirectorySync` parses it back by splitting each line on `[=;]`
and requiring exactly three fields (`Loader.java:28,174-191`). A directory with no `mrg` is read by
sorting its members numerically where the name parses as an integer, case-insensitively otherwise
(`Loader.java:254-265`).

Virtual entries are **not** stored. `writeFile` skips any node whose `realFileIndex() != -1`
(`Unpacker.java:1384`); the cache holds real bytes plus the index, and the alias is rebuilt at load
time (`Loader.java:239`).

So the cache *is* index-addressed — by `(directory, decimal index)` — and that indexing is
game-visible: `Battle.java:2945,3032` and `RetailSubmap.java:572,602,616` branch on
`hasVirtualSize()` and resolve models and textures through `realFileIndex()`. A 63-bit global key
would replace a working two-level scheme with an untested one to solve a problem nobody has.

### 2.3 Where a read is allowed to stop — the real content of §B

`planning/04:88` §B describes "the streaming path reads 4 bytes beyond the end of the payload;
legal inside an 0x800-byte-aligned buffer, illegal at an exact slice boundary". This is a real
phenomenon with a wrong diagnosis. There are two genuinely different window rules:

- **Slices are strict.** `FileData.slice` bounds-checks and `checkBounds` compares
  `offset + size > this.size()` — the *slice's own* length (`unpacker/FileData.java:57-63,723-736`). A
  read past the payload throws `IndexOutOfBoundsException`. Nothing is tolerated.
- **Aliases are not.** `FileData.virtual` builds its view with `size = real.data.length`
  (`unpacker/FileData.java:34-36`) — the whole backing array, not the logical `virtualSize`. A virtual view's
  readable window therefore extends past its own payload into whatever shares the buffer.

That is the whole of it. It is not a disc-sector alignment rule; it is a shared-buffer aliasing
rule, and it survives into game logic as `hasVirtualSize()`.

Separately, disc reads: 2352-byte raw sectors, Form 2 detected by bit `0x20` of sector byte 18,
Form 2 and `.IKI` / `.XA` files delivered as full 2352-byte sectors, everything else trimmed to
the exact requested length (`IsoReader.java:8,65,75`; `Unpacker.java:523`). Q-45 stays open — this
document does not resolve the Yellow Book bit discrepancy.

---

## 3. Decisions for approval

### D-1 — Identity is `(read domain, path)`; there is no 63-bit key

Delete the 63-bit address, the four kinds, the four namespaces, the 4 GiB wall, the 16 MiB
fallback and the `region_mask` predicate from the I1a scope. Replace them with:

- `PathKey` — a normalised path. Normalisation is exactly `fixPath`, three rules, no more.
- Directory-relative `index` addressing for archive members, carrying `(own index, owner index,
  virtual size)` — the three values the reference actually distinguishes.

`planning/04:330` risk row 1 is then satisfied by the model the reference has, and its
"path-as-identity cache key" clause becomes true instead of being contradicted by its neighbour.

A numeric key stays available to a later milestone if and when **disc reinjection** is scheduled.
Reinjection is out of I1a scope and blocked on the P-02 / R-01 asset-access decision, so nothing in
I1a–I2 needs it, and adding it now would put an unverifiable encoding on the path between a disc
image and every decoded asset.

### D-2 — Four read domains, replacing the four namespaces

`planning/04` §A requires `Disk`, `Cache`, `Tree` to share one trait while staying separate types,
and says the earlier `BTreeMap`-backed cache-as-`Space` design made the pipeline untestable without
real files. That requirement is kept and made concrete. Four domains, each with evidence:

| Domain | Window rule | Evidence |
|---|---|---|
| `Disc` | strict; sector-addressed, 2352-byte raw or trimmed to length | `IsoReader.java:63-92` |
| `Tree` | strict; the unpacked `files/` directory | `Loader.java:37,255` |
| `Slice` | strict; sub-range of a loaded archive | `unpacker/FileData.java:57-63` |
| `Alias` | **extended** to the end of the shared backing array | `unpacker/FileData.java:34-36` |

The path string never encodes which domain it belongs to, so the resolution order stays a runtime
decision, exactly as `planning/04:67` wanted — it just is not built on an integer.

### D-3 — Resolve §B in favour of strict bounds plus one explicit aliasing type

`ReadError::Overrun` (naming per §4) is returned when a strict window is crossed; it is a value,
never a panic. `Alias` is a distinct type that documents its extended window and says so in the
module header, rather than being quietly folded into `Slice`.

This is the answer to "whether the Rust reader replicates it or asserts it away is an I1a
decision, recorded either way" — recorded, replicating, with the two cases kept visibly separate.

**Unresolved, and not resolvable here:** whether a specific game read crosses an alias boundary is
an `asset-validated` question and needs real disc bytes (P-02 / R-01). It goes to the register as
**Q-99** (the register is full to Q-98) rather than being assumed either way.

### D-4 — Decompression is BPE or passthrough; there is no LZSS and no mode byte

`container::bpe` implements the one path the reference has: `readInt(0x4) == 0x1a455042` selects
BPE (`Unpacker.java:584,1408`); a file with no marker matches no transformer and is written
unchanged. Absence of the marker is the whole of "mode". No mode byte exists in the reference and
no LZSS variant does. The module header states this, so its absence reads as a decision and not as
an oversight.

`254.diff`'s observation that "some files are compressed and some are not" is true and is fully
explained by marker presence; it does not imply a mode field.

---

## 4. Naming decisions taken here

To keep the API sketches in this document from becoming the naming defect that has already been
raised twice:

- Format acronyms follow Rust PascalCase convention inside identifiers — `MrgArchive`,
  `BpeDecoder`, `ReadError::BpeMarkerMissing` — and lowercase in `snake_case` positions
  (`read_mrg`, `decode_bpe`). Mixed-case leading acronyms (`MrgArchive` → `MrgArchive` is fine;
  `MrgError`/`XaDir`-style mixed case inside a `SCREAMING_SNAKE` constant is not) are avoided.
- `mrg` as a *file name* and `MRG\x1a` as a *magic* are compatibility literals and keep their exact
  spelling (`AGENTS.md`, Compatibility exceptions).
- No identifier derived from an offset or an address (`_04`, `field_10`) appears in Rust names;
  offsets live in layout constants and assertions.

---

## 5. Verification that replaces the unbuildable tests

`planning/04:103` asks for a never-panic sweep over "192 bits of input domain" and a key round-trip.
With no key, those have no subject. The honest equivalents, all writable without real assets:

1. **Path normalisation is total and idempotent** — `fixPath` never panics on arbitrary bytes, and
   `normalise(normalise(x)) == normalise(x)`. Property test, arbitrary input.
2. **Strict windows never panic** — every `Disc` / `Tree` / `Slice` read that crosses its bound
   returns `Err(Overrun)`. Property test over offset and length drawn from and beyond the buffer.
   This is the direct successor of the never-panic criterion.
3. **Container round-trip** — build an MRG, read it, write it, compare bytes; same for the `mrg`
   index and for BPE streams (`decode → encode → compare`). **Done at I1a**, and it crosses the
   crate boundary rather than sitting inside one module: `tests/mrg_round_trip.rs`,
   `tests/index_round_trip.rs` and `tests/bpe_round_trip.rs` each drive the `psxrt-compat` writer
   against the `psxrt-import` reader. Item 3 is the only one of the five that I1a could finish, and
   it is the only one it finished.
4. **Alias window is explicit** — one test that asserts an `Alias` *can* read past the logical
   payload and that a `Slice` cannot, so the difference is pinned by a test rather than by a
   comment.
5. **Disc round-trip** — a synthetic 2352-byte-sector image built to the `IsoReader` contract, read
   back through the Rust reader, compared to `IsoReader` output. This is the oracle named in
   `planning/04:107`; it needs no real disc.

What is *lost* relative to the I1a gate as written: the 4 GiB and 16 MiB property tests (risk row
7). They cannot be written against an undefined encoding. Rather than invent one to satisfy them,
risk row 7 is marked **not applicable to I1a** here, and the row itself needs the owner's
signature because it is a change to a recorded risk disposition.

---

## 6. Documentation defects to correct

**Status 2026-11-16: all three are corrected at source.** They are kept listed here because the
point of the section is the defect class, not the three instances.

1. `planning/01` §4, `space` row, cited `planning/07 §3.1` — does not exist. **Fixed:** the row now
   states the 63-bit / four-namespace model is retired and carries no citation for it.
2. `planning/04` §4 risk row 1 cited `planning/05 §5.6` — does not exist. **Fixed earlier:** that
   row now reads "§A §3.1" and marks the 63-bit key retired. `grep` for `§5.6` in `planning/04`
   returns nothing.
3. `planning/04` §I1b, `fmv/mdec` row, attributed to `patches/scripts/DRGN1/254.diff` four
   properties the file does not contain. **Fixed:** verified against the file itself on 2026-11-16
   — it is an 85-line Fate script diff whose only changes are two `call 498, id[lod:burn_out]`
   replacements, with zero occurrences of `disc:`, `FILE:`, `bin`, `0x800`, `compress` or `mode`.
   The row now says the three properties are *unverified* and must be re-derived from real disc
   bytes, which is the correction the "pointer needs a human read" rule was meant to force.

The rule those three went through is unchanged and still holds: they were routed to
`research/citation-exceptions.md` rather than silently patched, because `citation_audit.py --apply`
is documented as untrusted, and a wrong pointer replaced by an unverified one is worse than the
wrong pointer. The fixes above replace each pointer with a statement of what was actually checked
and what returned zero, not with a substitute citation.

**A line-number caveat for readers of §2's table.** The "Where asserted" column cites `file:line`
positions as they stood when this document was written. Three of them have since moved — the
targets were edited, so the line numbers now land on unrelated text. Read the column as *which
section*, not as an address.

---

## 7. What I1a delivered, and what it deliberately did not

Built, tested and passing on 2026-11-16:

- `psxrt-import::container::mrg` — MRG read, virtual-entry resolution, duplicate-offset detection.
- `psxrt-import::container::bpe` — BPE decode; passthrough when the marker is absent.
- `psxrt-import::cache` — the `files/` tree and `mrg` index, read side.
- `psxrt-import::patch` — patch orchestration. Not a file-space consumer; it addresses files by
  path within four named trees and never needed a key, which is the strongest evidence this
  document has that D-1 is right.
- `psxrt-compat::container` — MRG writer, BPE encoder, `mrg` index writer, as test oracles. The
  round trips cross the crate boundary; three of them also carry a property test.

Not built, moved to I1b by owner decision on 2026-11-16:

- `psxrt-import::space` — the four read domains, `PathKey`, `fixPath`, the error codes.
- `psxrt-import::container::iso` — 2352-byte sector reader with the Form 1 / Form 2 split and the
  `.IKI` / `.XA` raw path.
- `psxrt-import::pipeline` — the orchestrator. It is listed here because its first draft belonged
  to the same layer, and because §5 items 2 and 5 are the precondition for both it and `iso`.

The reason all three moved is the same reason and it is not difficulty. None of them can be checked
against anything at I1a: `space` and `iso` need real disc bytes or at minimum a synthetic image
built to the `IsoReader` contract, and neither the synthetic-image harness nor the `Alias` / `Slice`
boundary existed. Writing them would have produced code whose tests only restate their own
definitions. That is precisely the failure mode §1 was written to prevent, so it was avoided rather
than argued about.

What is still needed before I1b writes them: §5 items 1, 2, 4 and 5. Item 3 is done.
