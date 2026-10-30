# PSX Binary Formats — Byte-Level Layouts and Provenance

Research record for the formats implemented in the `psx` library during Milestone 3
(`psx::bpe`, `psx::archive` = MRG/DEFF, `psx::tim`). This is where the Java source
provenance lives: `docs/guidelines/cpp.md` §2 keeps `.java` citations out of code
comments, so each decoder's header states the layout and points here for the
evidence behind it.

- **Reference repository:** `~/Documents/GitHub/Severed-Chains` (read-only),
  commit `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`.
- **Method:** byte layouts read out of the reference readers, then cross-checked
  against real data where real data was available locally. Verified behaviour is
  separated from deviations; anything not checkable is listed under
  [Open questions](#6-open-questions-and-gaps).
- **No game data is distributed with this repository** (AGENTS.md). The only
  authentic PSX asset available locally is `patches/replacements/chester.tim`
  inside the reference repository, which is a community replacement TIM rather
  than retail data; it is used as an opt-in test fixture via `SC_PSX_ASSET_DIR`.

## 1. BPE — adaptive byte-pair codec

**Reference:** `legend/game/unpacker/Unpacker.java:1406-1520` (`decompress`);
discriminator at `Unpacker.java:584`; block-size ceiling at
`Unpacker.java:1429`; word-alignment of the block cursor at
`Unpacker.java:1505-1508`.

```text
0x0  u32  decompressed size (total bytes the blocks must produce)
0x4  u32  magic 0x1a455042 ("BPE\x1a")
0x8  u32  block 0 decompressed size (0 = end of stream)
     256-entry dictionary (left byte per key, right byte for non-identity keys)
     block data (codes, expanded through the dictionary)
     padding to a 4-byte boundary
     u32  block 1 decompressed size ... repeat
```

Behaviour verified from the reference:

| Behaviour | Reference |
|---|---|
| Magic is a u32 at 0x4, compared against `0x1a455042` | `Unpacker.java:1408-1410` |
| Content sniff for the transformer is "at least 8 bytes and the magic matches" | `Unpacker.java:584` |
| A block's decompressed size is read before the block; `0` ends the stream | `Unpacker.java:1424-1430` |
| A block declaring more than `0x800` decompressed bytes is rejected | `Unpacker.java:1431-1434` |
| Dictionary starts as 256 identity keys with an all-zero right table | `Unpacker.java:1438-1444` |
| Dictionary is adaptive: a leading byte ≥ `0x80` skips keys, otherwise it counts sequential `{left,right}` pairs; a pair is stored only when `left != key` | `Unpacker.java:1446-1481` |
| A code resolves when `left[code] == code`; otherwise it expands to `left`+`right` on a LIFO list and is re-resolved, so one code can stand for an arbitrarily long run | `Unpacker.java:1483-1506` |
| The block cursor is word-aligned after each block | `Unpacker.java:1508-1511` |

**Deviations in the C++ port** (`psx/src/bpe.cpp`):

1. **Size mismatch is an error.** The reference allocates its output from the
   declared size (`Unpacker.java:1414`) and never compares that against the bytes
   the blocks actually produced (`Unpacker.java:1519`): a short stream silently
   returns zero padding at the tail, and a stream that inflates past the declared
   size writes out of bounds. The port returns `Error::BPESizeMismatch` instead,
   because padding is indistinguishable from real trailing zero bytes
   downstream.
2. **Every bounds check is explicit.** The reference trusts the block/dictionary
   lengths once the magic matched; the port reads through a bounds-checked
   little-endian cursor, so malformed input can only produce an `Error`.
3. **Allocation is bounded before it happens.** The u32 declared size is
   untrusted, so `DecompressBPE(data, maxOutputBytes)` refuses a declared size
   above the caller's ceiling (`Error::BPEOutputTooLarge`) and detects expansion
   that would exceed it (`Error::BPEExpansionOverflow`).
4. **`CompressBPE` is tooling, not parity.** The reference has no packer. It
   exists so round-trip properties can be tested and so asset tooling can build
   fixtures; it emits identity dictionaries only.

## 2. MRG — index-only container

**Reference:** `legend/game/unpacker/MrgArchive.java` (whole file), applied from
`Unpacker.java:592-600`.

```text
0x0  u32   magic 0x1a47524d ("MRG\x1a")
0x4  u32   entry count
0x8  u32   entry[i].offset   (at 0x8 + i * 8)
     u32   entry[i].size     (at 0x8 + i * 8 + 4)
```

| Behaviour | Reference |
|---|---|
| Magic at 0x0; count at 0x4; 8-byte `{offset,size}` entries at `0x8 + i * 8` | `MrgArchive.java:6`, `MrgArchive.java:11`, `MrgArchive.java:16-19` |
| `size == 0` marks a *virtual* entry: its offset names a real entry's offset and it resolves to that entry's range | `MrgArchive.java:39-52` |
| Two real entries with the same offset collapse onto the first one | `MrgArchive.java:21-26` |
| For root `DRGN*.BIN` archives the offsets are **sector numbers** and are multiplied by `0x800` | `MrgArchive.java:29-33`, applied from `Unpacker.java:596` |
| The path pattern that selects sector alignment is `^DRGN(?:0\|1\|2[1234])?.BIN$`, matched against the **path segment**, not the full path | `Unpacker.java:53`, `Unpacker.java:596` |
| An MRG with count 0 is emitted as an empty directory, not as an archive with no files | `Unpacker.java:598-600` |

Notes carried into the port (`psx/src/archive.cpp`):

- Sector scaling is a **caller-supplied flag** (`ParseMRG(data, sectorAligned)`),
  because the flag comes from the container's *path*, which `psx` does not know.
  The engine decides it from the first path component
  (`lod::res::ResourceManager::Inspect`, `src/engine/src/resources.cpp`).
- Entry ranges are not validated during parsing (a table is just numbers);
  `MrgEntryData()` is the checked slice, and it is what reports an entry that
  claims bytes the container does not have (`Error::MRGEntryOutOfRange`).
- A virtual entry whose parent lookup fails keeps `size == 0` and `parent == -1`
  rather than being dropped, so entry indices stay aligned with the on-disc
  table (the reference indexes entries by number at runtime).

## 3. DEFF — contiguous subfile container

**Reference:** `legend/game/unpacker/DeffArchive.java:14-19`, applied from
`Unpacker.java:623`.

```text
0x0  u32   magic 0x46464544 ("DEFF")
0x6  u16   subfile count
0x8  u32   subfile[i].start  (at 8 + i * 8 + 4)
```

| Behaviour | Reference |
|---|---|
| Magic at 0x0 (checked by the transformer, not the archive class); the count is a **u16** at 0x6 | `Unpacker.java:623`, `DeffArchive.java:21-23` |
| Subfile starts live at `+4` inside each 8-byte table slot | `DeffArchive.java:14-16` |
| Subfiles are contiguous: subfile *i* spans `[start(i), start(i+1))` | `DeffArchive.java:26-29` |
| The last subfile ends at the container size (an extra pointer is appended for exactly this) | `DeffArchive.java:12-18` |
| A DEFF with count 0 is emitted as an empty directory, like MRG | `Unpacker.java:629-631` |

Port additions: the start table must be monotonic and inside the container
(`Error::DEFFOffsetsBad`), and the table must fit (`Error::DEFFTableTruncated`).
The reference trusts both, which turns a corrupt container into an
`IndexOutOfBounds` deep inside a loader instead of a reported format error.

## 4. TIM — texture image

**Reference:** `legend/game/tim/Tim.java` (whole file); rect reads go through
`FileData.readRect`, which uses `readShort` (`FileData.java:624-627`), so rects
are **signed** i16. Colour-depth codes come from `legend/core/gpu/Bpp.java`
(`BITS_4, BITS_8, BITS_15, BITS_24`, selected by `flags & 7`).

```text
0x0  u32        id, 0x10 for every TIM
0x4  u32        flags: colour depth = flags & 7, CLUT present = flags & 8
0x8  u32        CLUT section size   [only when the CLUT flag is set]
0xc  i16[4]     CLUT rect x, y, w, h
0x14 CLUT data  (clutSectionSize - 12 bytes)
     ...        image section, at 0x8 + clutSectionSize (or 0x8 without a CLUT)
        u32     image section size
        i16[4]  image rect x, y, w, h
        u8[]    image data (imageSectionSize - 12 bytes)
```

**Measured evidence** — `patches/replacements/chester.tim` in the reference
repository, 4640 bytes:

| Field | Measured |
|---|---|
| id at 0x0 | `0x10` |
| flags at 0x4 | `8` → depth 0 (4 bpp), CLUT present |
| CLUT section size at 0x8 | `0x20c` |
| CLUT rect at 0xc | `(0, 112, 16, 16)` → 256 entries |
| CLUT data | `0x20c - 0xc = 0x200` bytes = 256 × 2 at 0x14 |
| image section offset | `0x8 + 0x20c = 0x214` |
| image section size / rect | `0x1000`, rect `(0, 0, 16, 112)` at 0x218 |
| image data | `0x1000 - 0xc = 0xff4` bytes at 0x220 |
| trailing bytes after the declared image section | 12 |

Those exact numbers are asserted by
`psx/tests/test_tim.cpp:authentic_asset_matches_measured_layout`, which runs only
when `SC_PSX_ASSET_DIR` points at a directory containing `chester.tim`; without
the variable the case reports `SKIP` and still passes (no game data ships here).
The 4bpp extent rule (`w * 2 * h` bytes for a 16-bit texel at an indexed depth)
holds for this asset: `16 * 2 * 112 = 0xe00 ≤ 0xff4`.

**Deviations in the C++ port** (`psx/src/tim.cpp`):

1. **Full u32 CLUT section size.** `Tim.java` reads it with `readUShort(0x8)`
   when locating the image section, which silently truncates a section size
   above `0xffff`. The port uses the full u32.
2. **Validation instead of trust.** Depth codes outside `0..3`, sections running
   past the buffer, section sizes smaller than their own 12-byte header,
   non-positive rects, and rects that need more bytes than their section declares
   are all errors here; the reference reader accepts them and fails later (or
   renders garbage).
3. **Trailing bytes are reported, not rejected** (`trailingBytes`), matching the
   reference's behaviour on `chester.tim`.
4. **24 bpp extent is not enforced**, because the reference does not define the
   pitch for it; documented rather than guessed.

## 5. Little-endian reads

`MathHelper.java:111` builds var handles with
`MethodHandles.byteArrayViewVarHandle(int[].class, ByteOrder.nativeOrder())`, i.e.
the reference reads in *host* order and is correct only because every supported
target is little-endian. The core's internal reader (`psx/src/reader.h`,
`psx_reader_t` + `psx_reader_u8/u16/i16/u32/fits/from`) is explicitly little-endian
instead of inheriting the host's order, which matters for big-endian hosts and
for byte-exact golden tests. It is an implementation detail, not an exported API
(psx-c-api.md §1).

## 6. XA ADPCM (implemented 2026-10-29)

Source of truth is `legend/core/spu/XaAdpcm.java` (the decoder). The
`legend/core/audio/xa/XaTranscoder.java` file that shares the name is an offline
Opus encoder and is not a hardware model.

Four things were measured rather than assumed, because each one is a place where
"obvious PS1 behaviour" and "what the reference does" part company:

1. **The sector slice is the reference's, not the hardware's.** `XaAdpcm.java:89-90`
   walks 18 portions of 128 bytes, reading control bytes at `4 + blk*2 (+1)` and
   sample bytes at `16 + blk + i*4`. The documented CD-ROM XA Mode 2 Form 1 block
   is normally described as an 8-byte header over 120 data bytes. The port
   reproduces the reference and is exact against it; whether the reference matches
   real hardware is **not established** and is an open question (§7), not a
   resolved fact.
2. **`filter` can only be 0..3.** `XaAdpcm.java:164` computes
   `(b & 0x30) >> 4`. Both coefficient tables (`:22-23`) carry five entries, so
   index 4 — `122` / `-60` — is unreachable dead data. The port keeps a
   four-entry table, which is equivalent for every value the mask can produce.
3. **`shift` can go negative.** `XaAdpcm.java:163` computes `12 - (b & 0x0F)`,
   so a nibble-shift field of 13..15 yields -1..-3, and Java's `<<` on a negative
   count uses `count & 31`. The port reproduces this through an explicit
   `uint32_t` shift instead of relying on C's undefined behaviour, and the fixture
   pins it with three badshift cases.
4. **The prediction divide truncates.** `(+32)/64` in Java integer arithmetic
   rounds toward zero, which is *not* an arithmetic `>> 6` for negative
   predictions. The port uses truncating division to match.

Points 3 and 4 are load-bearing, not theoretical: replacing truncating division
with `>> 6` changes the output (`want -3694, got -3695`), as does dropping the
`+32`. Both are covered by mutations (§7 of testing.md's non-vacuity rule).

**The 8-bit flag is a deliberate behaviour difference.** The reference computes
`is8BitPerSample` (`:84`) and then never uses it — an 8-bit sector is decoded as
4-bit nibbles, producing noise. The port returns
`PSX_ERROR_XA_UNSUPPORTED_CODING`. This is a divergence from the oracle, chosen
because silently emitting noise is worse than refusing; it is pinned by a test
rather than passed off as agreement.

**State is per-stream, not process-wide.** The reference holds
`oldL/olderL/oldR/olderR` in `static` fields. The port puts them in a
caller-owned `psx_xa_stream_t`. For a single stream the output is bit-identical;
the difference is that two sequences can no longer corrupt each other.

### 6.1 The committed differential fixture

`psx/testdata/xa_adpcm.bin` is 17 synthetic sectors, each carrying its coding
byte and the reference's own PCM output. `psx/tools/xa_oracle.py` regenerates it
by compiling and running the real `XaAdpcm.java` (needs a JDK 21+ and a fastutil
jar; the committed `.bin` needs neither). Container is
`"PSXXAFIX" + u32 version + u32 count +` per case
`[u8 name_len, name, u8 coding, u32 sector_len, u32 pcm_len, sector, pcm]`,
little-endian, with `pcm_len` a **byte** count (a sample-count reading would
halve the payload and compare the wrong window). Full layout in
`psx/testdata/README.md`.

This is the **first committed** fixture in `psx/`, and it is deliberately a
different class from the optional authentic-asset checks in testing.md §2.2: the
input is synthetic, so it is always available, and `test_xa.cpp` **fails** when
the file cannot be read instead of skipping. A skip would have been a way to
report a pass that was not earned. `SC_PSX_ASSET_DIR` keeps its skip-with-
visible-message behaviour because there the absence of data is expected.

Coverage of the 17 cases, counted from the committed file: 88 704 expected
samples, 63 094 non-zero (71.1%), 17 375 clamp-rail hits; shift values -3..12
and all four reachable filter settings. Plus chained stereo (5 sectors) and mono
(3 sectors) runs that carry predictor state across sector boundaries. An earlier
draft of this section quoted 74.9% / 16858 from the scratch generator before the
fixture was narrowed to 17 cases; the figures above are the committed ones.

## 7. Open questions and gaps

- **No retail game data available locally.** BPE/MRG/DEFF layouts are verified
  against the reference readers and against hand-built fixtures; they are not yet
  verified against a retail `DRGN0.BIN`. Re-run
  `SC_PSX_ASSET_DIR`-style checks once the player supplies `isos/`.
- **MRG `mrg` JSON sidecar.** The reference writes an `mrg` file map recording
  virtual→parent relationships (used by its low-memory path). The port resolves
  virtual entries from the table itself, so no sidecar is produced; revisit if
  lazy loading needs it.
- **DEFF subfiles are only sliced, not interpreted.** CMB/DEFF semantics are
  combat research (asset-pipeline.md §3) and out of scope here.
- **TIM `trailingBytes`** is usually padding, but its meaning (palette animation
  data? alignment?) is not established by the reference. Reported as a count.
- **DABA, TMD, ANM, SShd, SSSq, ISO 9660** remain unimplemented
  (psx-library.md §2 tracks them); nothing in this document describes them.
- **XA block layout is verified against the reference only.** §6 item 1. If a
  retail `DRGN0.BIN` is supplied, re-run the decode against a real XA track and
  compare; agreement there would promote the layout from "reproduces the Java
  decoder" to "reproduces the hardware". Until then the 128-byte portion stride
  is a compatibility choice, not a hardware fact.
- **No real XA data has been decoded.** The 17 fixture sectors are synthetic.
  They cannot contain an 8-bit-coded sector, so the deliberate 8-bit divergence
  (§6) has no observed instance in real data — it is a guard, not a fix for a
  seen case.
- **XA is tested single-threaded and single-stream.** The per-stream state change
  (§6) removes the reference's cross-sequence hazard but nothing yet exercises
  concurrent streams.
- **Sector byte positions are reproduced from the reference, not from hardware
  documentation** (see §8.4). The 128-byte portion stride (§6 item 1) and the
  17/18/19 header offsets share that limitation: they are traceable to Java
  call sites, not to a measured drive.
- **ISO 9660 parsing is unimplemented.** Its blocking question (where sector
  data starts) turned out to be answerable from source and is settled in §8.2;
  what is missing is the parser and real-image goldens, not a measurement.

## 8. CD-ROM sector layer (implemented 2026-10-29)

`psx::cdrom` (`psx/include/psx/cdrom.h`, `psx/src/cdrom.c`, wrappers in
`psx/include/psx/cpp/cdrom.hpp`) is the **sector geometry and header decode**
half of the `psx::cdrom` row in psx-library.md §2. The ISO 9660 parsing half is
still open (§7). Reference sources, all at commit 3433e6f:
`legend/game/unpacker/IsoReader.java`, `legend/game/unpacker/Unpacker.java`,
`legend/game/fmv/SectorHeader.java`, `legend/game/fmv/VideoSector.java`,
`legend/game/fmv/Fmv.java`, `legend/core/spu/XaAdpcm.java`.

### 8.1 Verified from reference source

| Fact | Evidence |
|---|---|
| Raw sector = 2352 bytes; sync pattern = 12 bytes | `IsoReader.SECTOR_SIZE = 2352`, `IsoReader.SYNC_PATTER_SIZE = 12` |
| User data starts at byte 24 | `readSectors` arraycopies from index 24; `XaAdpcm.java:89` walks 18 x 128 = 2304 sample bytes inside the 2324-byte region at 24 (that region is Form 2's 2328 minus a 4-byte EDC); `Unpacker.getIsoReader` and `loadRoot` seek to `sector * 2352 + 12`, advance 12 more, then expect descriptor type `1` as the first byte they read |
| Form is bit 5 of byte 18 and it selects 2048 vs 2328 | `readSectors`: `sectorSize = raw \|\| ((sectorData[16 + 2] >>> 5) & 1) != 0 ? 0x930 : 0x800`, copy length `Math.min(sectorSize, ...)` from index 24; `SectorHeader.Submode.getForm` reads `data[18] >>> 5 & 1` |
| Sector count uses a 2048 divisor even when the transfer is raw | `readSectors`: `sectorCount = (length + 0x7ff) / 0x800` with `data = new byte[sectorSize * sectorCount]` and `dataRead += sectorSize`, so a raw read of a 2049-byte file moves 2 x 2352 bytes |
| Submode bits 7 / 6 / 5 / 4 / 3-1 / 0 and coding bits 4 / 2 / 0, at bytes 18 and 19 | `SectorHeader.Submode` (`isEof` 0x80, `isRealtime` 0x40, `getForm` bit 5, `isTrigger` 0x10, `getType` 0x0E, `isEndAudio` 0x01) and `SectorHeader.Coding` (`getBitsPerSample` 0x10, `getSampleRate` 0x4, `isStereo` 0x1) |
| Type patterns: 2 = VIDEO, 4 = AUDIO, 8 = DATA; anything else throws | `SectorHeader.Submode.getType` default branch |
| FMV playback consumes whole 2352-byte sectors, not extracted payloads | `Fmv.play` allocates `new byte[2352]` and passes the same buffer to `SectorHeader`, to `VideoSector`, and to `XaAdpcm.decode(data, data[19])` |

The 2048-divisor row is reproduced **deliberately**. `psx_sector_count_for_bytes`
keeps the quirk so that a caller can tell "matches the reference" from
"matches the hardware" instead of silently getting the wrong transfer size.

### 8.2 Corrected during implementation (previously recorded as a gap)

An earlier note in this tranche said the reference carried two conflicting
definitions of "start of sector data" — 12 (`IsoReader.seekSector`) versus 24
(`readSectors`) — and that picking between them required a real disc image.
That was wrong, and the wrong version had already reached a header comment.
`seekSector` stops after the 12-byte sync, and every caller then skips the rest
of the header itself: `Unpacker.getIsoReader`, `loadRoot` and
`populateDirectoryTree` all advance `SYNC_PATTER_SIZE` (or a literal 12) before
reading. Every path in the reference lands on byte 24.

This is recorded rather than deleted because a stale "these two disagree" note
invites someone to "fix" the 24-byte boundary later, and because the correction
changed what is actually missing: not a measurement, but the ISO 9660 parser
and real-image goldens.

### 8.3 Deliberately not in `psx::cdrom`

- **The FMV mux.** `VideoSector` wraps the same 2352-byte buffer as
  `SectorHeader`. It reads a 32-byte chunk header from the region at bytes
  24..55 — chunk number 28, chunk count 30, frame number 32 (4 bytes), demuxed
  size 36, frame width 40, frame height 42, uncompressed 32-byte block count 44
  — and copies **one 2016-byte chunk from absolute byte 56** per sector into a
  frame buffer of `2016 * sectorCount`. A frame is 5 sectors at normal speed,
  10 at double (`Fmv.play`, "(150\|75) sectors per second / 15 frames per
  second"), sectors are read with a 2352-byte stride
  (`fileData.read(sector * data.length, ...)` where `data.length == 2352`),
  `demuxedSize` comes from the frame's first DATA sector, VIDEO sectors feed
  the MDEC path and AUDIO sectors feed `XaAdpcm`.
  This is the mod's own container for the PC remaster, not PS1 sector
  semantics, so AGENTS.md's separation rule puts it with the video path in the
  engine/game layer. The layout above is recorded here so that path starts from
  measurements instead of re-deriving them.
- **XA interleave groups.** `XaTranscoder.interleaveMode` (4 or 16, chosen from
  the mod's filename) describes how the mod's files pack audio and video
  sectors. Same reason, same destination.
- **ISO 9660 name/directory/path-table parsing.** Reusable PSX technology, so
  it belongs in `psx::cdrom`; it is unbuilt because a hand-built volume
  descriptor proves only self-consistency, and the reference's own PVD check
  (descriptor type, `"CD001"`, version byte, then a 32-byte volume-space
  identifier read from logical offset 7 and trimmed, which is how
  `"PLAYSTATION"` matches) is a behaviour to pin against real data, not to copy
  blind.

### 8.4 Open questions specific to the sector layer

- **Byte positions versus hardware.** Offsets 17/18/19 are the reference's, and
  only 18 and 19 are corroborated by a second call site (the Form 2 test in
  `readSectors`, the coding byte in `XaAdpcm.decode(data, data[19])`). Whether
  they match a real Mode 2 sector layout is **not established**; the port
  reproduces the reference. Do not "correct" them from a hardware description
  without an image to measure against.
- **Byte 15 / `getMode()`.** Declared once, called nowhere in the reference, so
  the port exposes no mode field. If a real image shows byte 15 carrying a mode
  byte that something needs, that is new work with evidence, not a gap to fill
  by guesswork.
- **No real sector has been parsed.** All 12 sector-layer cases and the C
  consumer checks run on synthetic sectors built from the source above. They
  pin bit arithmetic and bounds behaviour; they do not certify any retail disc.
- **Path tables.** The reference walks directory records only. Whether ISO 9660
  path-table support is needed for the LoD discs is unresolved until an image is
  available.
