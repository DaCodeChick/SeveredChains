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

## 6. Open questions and gaps

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
- **DABA, TMD, ANM, SShd, SSSq, XA, ISO 9660** remain unimplemented
  (psx-library.md §2 tracks them); nothing in this document describes them.
