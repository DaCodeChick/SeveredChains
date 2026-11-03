# M2-01 — ISO/CD Access, File Tree, and Runtime Resource Access

Phase M2 deliverable 1 of 9 (+1 supplementary). Evidence base: Severed-Chains @ `0c08c679b`, Dig-Dabas @ `4c1fbfebced`, no disc images available (no `asset-validated` claims in this document). Validation categories per `AGENTS.md`: **source-verified** (reference code), **format-corroborated** (independent spec/implementation), **asset-validated** (none yet).

## 1. Raw disc geometry (source-verified)

Reference: `legend/game/unpacker/IsoReader.java` (Severed-Chains).

- Raw sector size `SECTOR_SIZE = 0x930` (2352 bytes); sync pattern `SYNC_PATTER_SIZE = 12` (`IsoReader.java:8-9`).
- Sector layout used by the reader: `[0..11]` sync, `[12..23]` 12-byte CD header, `[24..]` user data:
  - `seekSector(n)` positions at `n*2352 + 12`; callers then `advance(12)` to reach user data (`Unpacker.loadRoot`, `getIsoReader`).
  - `seekSectorRaw(n)` positions at `n*2352` (used by `readSectors` for whole-sector reads).
- **Form1/Form2 detection** (`readSectors`, `IsoReader.java:63-79`): header byte at sector offset `16 + 2` (i.e. the 7th byte of the 12-byte CD header, header[6], the control/data-mode byte); the reference tests **bit `0x20`** (`(byte >>> 5) & 1`) to select Form 2 → sector user size `0x930` (2352, full raw copy) vs Form 1 → `0x800` (2048). *(source-verified; note: the Yellow Book control byte's Form-2 indicator is bit `0x04` — the reference's use of bit `0x20` is not reconciled with that bit, `Inferred` PSX-toolchain convention; registered as an open item.)* Non-raw copies start at sector offset 24.
- `readSectors(sector, length, raw)`: computes `sectorCount = ceil(length / 0x800)`, walks consecutive physical sectors, copies Form1 2048 B or Form2 2352 B (raw) per sector; non-raw output is trimmed to `length`. The same length is used for the **directory-record data length** (see §3), so ISO addressing is in 2048-byte logical blocks even though raw sectors are 2352 bytes.
- **Raw (whole-sector) reads are forced for `.IKI` and `.XA` files** (`Unpacker.readFile`, `Unpacker.java:495`): `filename.endsWith(".IKI") || filename.endsWith(".XA")`. This is what preserves FMV (`STR/*.IKI`) and XA (`XA/LODXA0n.XA`) sector streams byte-exact, including their subheaders.

Format corroboration (no$psx "PSXSPX CDROM ISO" / reverseengineering.wiki "Format:CD-ROM XA"): raw sector = 12-byte sync + header, Mode 2 Form 1 (2048 data) or Form 2 (2324 data, the last 20 of which are zero for XA video); logical block for ISO9660 = 2048 bytes. The reference's Form2 branch copies 2352 bytes raw (not 2324) — for `.IKI`/`.XA` files the extra 280 bytes are simply carried along; for normal files Form 2 sectors are truncated to 2048 at the data region. *(source-verified behavior; the 2324-vs-2352 nuance is format-corroborated.)*

## 2. Volume (disc) identification (source-verified)

Reference: `Unpacker.getIsoReader` (`Unpacker.java:407-443`), `DISK_IDS`/`OTHER_REGION_IDS`/`OTHER_REGION_NAMES` (`Unpacker.java:57-60`), `PVD_SECTOR = 16` (`Unpacker.java:62`).

- Candidate image files are anything in `./isos/` with size ≥ `(16+1)*2352` bytes.
- The **Primary Volume Descriptor (PVD) at sector 16** is validated (user data, i.e. sector start + 24):
  | offset (PVD user data) | check |
  |---|---|
  | `0x0` | byte `0x01` (primary volume descriptor) |
  | `0x1..0x5` | `"CD001"` |
  | `0x6` | byte `0x01` (version) |
  | `0x7..0x38` | 32-char field, trimmed, must equal `"PLAYSTATION"` (system identifier) |
  | `0x39..0x6A` | 32-char field, trimmed = **disc ID** (volume identifier), e.g. `"SCUS94491"` |
- Disc IDs accepted for the four LoD discs (`DISK_IDS`): `SCUS94491` (disk 1), `SCUS94584` (2), `SCUS94585` (3), `SCUS94586` (4). The region ID in the PVD therefore doubles as the disc selector; each `IsoReader` is tagged with its 0-based disk index.
- Other-region IDs (28 known: 20 `SCES` — `SCES03043..33047` grouped europe/france/germany/italy/spain — and 8 `SCPS` — `SCPS10119..10122` japan, `SCPS45461..45464` asia) are recognized and produce a per-slot "wrong region" status instead of a generic "not found" (`Unpacker.java:431-435`; `OTHER_REGION_NAMES` has 7 entries, `index/4` maps 28 IDs → 7 regions).
- The disc-wait loop: all four readers must be non-null; per missing disc the loading screen shows `unpacker.disk_help` + per-disk `unpacker.disk_status`, polling every 1 s (R1 lifecycle doc, F-09). `Unpacker.stop()` aborts with `UnpackerStoppedRuntimeException`.

Format corroboration: the PVD field offsets checked above are the standard ISO 9660 volume descriptor layout (type byte, 5-byte standard identifier `"CD001"` at offset 1, version, then 32-char system/volume identifiers) — confirmed against ISO 9660 references and PSX disc documentation (no$psx "PSXSPX CDROM ISO Volume Descriptors"; fstool `vd.rs`). PSX discs put `"PLAYSTATION"` in the system identifier and the region ID (`SCUS…`) in the volume identifier. **No divergence from the reference implementation was found.**

## 3. ISO9660 directory records (source-verified; format-corroborated)

Reference: `legend/game/unpacker/DirectoryEntry.java`, `Unpacker.loadRoot` / `populateDirectoryTree`.

- Root directory record: read from the PVD at offset `0x9c` (`Unpacker.java:460`) — the standard ISO 9660 position (156); no$psx documents it as a 34-byte record spanning PVD `09Ch..0BDh`, root block "usually 22 on PSX disks".
- Directory records (PVD root record and all directory-sector records) are parsed identically by `DirectoryEntry.fromArray(reader, data, offset)`:
  | record offset | size | field | reference read |
  |---|---|---|---|
  | `0x00` | 1 | record length (LEN_DR) | `entryLength` |
  | `0x01` | 1 | extended attribute record length | (unused) |
  | `0x02` | 4 | extent location (LBA) — both-endian 32-bit pair | little-endian half, `sector` |
  | `0x0a` | 4 | data length — both-endian 32-bit pair | little-endian half, `length` |
  | `0x12` | 7 | recording timestamp | (unused) |
  | `0x19` | 1 | file flags | `flags`; `isDirectory = (flags & 0x2) != 0` |
  | `0x1a` | 1 | file unit size | (unused) |
  | `0x1b` | 1 | interleave gap size | (unused) |
  | `0x1c` | 4 | volume sequence number (2×16 both-endian) | (unused) |
  | `0x20` | 1 | file identifier length (LEN_FI) | `nameLength` |
  | `0x21` | LEN_FI | file identifier | `name` (see below) |
- Format corroboration: this is the textbook ISO 9660 directory record (no$psx "PSXSPX CDROM ISO File and Directory Descriptors", table: `02h 8 Data Logical Block Number (2x32bit)`, `0Ah 8 Data Size (2x32bit)`, `19h 1 File Flags`, `20h 1 Length of Name`, `21h Name`). PSX discs additionally carry a 14-byte CD-XA "System Use" area after the name (owner IDs, file attributes with `IS_MODE2`/`IS_MODE2_FORM2`/`IS_INTERLEAVED`/`IS_CDDA`/`IS_DIRECTORY` bits, `"XA"` signature, XA file number) which the reference **ignores entirely**.
- Both-endian numbers: the reference always takes the **little-endian half** of each 2×32-bit (or 2×16-bit) field — valid because the LE half is stored first on PSX discs.
- Name handling (`DirectoryEntry.java:24-33`):
  - name byte `0x00` → `"."`, `0x01` → `".."`;
  - names containing `;` are truncated at the **last** `;` (ISO 9660 version suffix, e.g. `SECT;1` → `SECT`);
  - otherwise used as-is.
- Directory walk (`populateDirectoryTree`, `Unpacker.java:464-487`): seeks the directory's LBA, skips 12 (to user data), reads 2048 bytes, iterates records `sectorOffset += entryLength` until a zero length byte (sector padding/terminator). Records never cross sector boundaries in practice (no$psx: entries should not cross 0x800 boundaries; PSX kernel supports max 0x800 per directory).
- Children are stored in a `HashMap<String, DirectoryEntry>` keyed by name; `putIfAbsent` is used during the cross-disc merge (§4). Note: insertion order is directory-order (alphabetical on PSX discs), but the map is unordered — file *content* never depends on this order (the queue-based unpacker is order-insensitive; see M2-03).

## 4. Cross-disc tree merge: disc 4 is the master tree (source-verified)

Reference: `Unpacker.unpack`, `Unpacker.java:330-341`.

```
roots[4];
root = loadRoot(readers[3], null);      // disk 4 (SCUS94586) builds the master tree
for(i = 0; i < 3; i++)
    loadRoot(readers[i], root);         // disks 1-3 merge their directory trees INTO the master
```

- `loadRoot(reader, destinationTree)` reads the PVD root record of `reader`, then `populateDirectoryTree(rootDir, destinationTree)` adds **every** directory node into `destinationTree` via `children().putIfAbsent(name, entry)` — recursively.
- Consequences:
  - The **disc-4 directory structure is the master**; any directory or file *name* that appears on both a lower disc and disc 4 resolves to the **disc-4 entry** (first-come wins, disc 4 is inserted first).
  - Every `DirectoryEntry` keeps its **own** `reader` (its source disc's `IsoReader`), so when the tree is walked later each file is read from the correct physical disc regardless of which disc "owned" the name in the merged tree.
  - `putIfAbsent` means a name collision is silent — no diagnostic is emitted for shadowed entries. *(source-verified; the actual collision set on real LoD discs is not asset-validated.)*
- This answers M2 research question 1 (second half): the "root tree on disc 4 indexes discs 1–3" is a **name-merged ISO9660 directory tree**, not an explicit index structure. The merge is whole-tree: every directory level of discs 1–3 is grafted into the disc-4 skeleton.

## 5. From tree to extracted files (source-verified)

Reference: `Unpacker.populateInitialFileTree` / `readFile` / `writeFile` (`Unpacker.java:489-520`, `1375-1400`).

- The merged `DirectoryEntry` tree is walked once; each **file** node becomes a `PathNode` with:
  - `fullPath` = concatenation of directory names + file name (ISO names, already `;version`-stripped);
  - `data` = `readFile(filename, entry)`: `entry.reader().readSectors(entry.sector(), entry.length(), raw=.IKI/.XA)` — i.e. exactly `length` bytes starting at LBA `sector` (see §1), read under a per-reader monitor (one `IsoReader` per disc; reads from the same disc are serialized).
- In **low-memory mode** (`Config.lowMemoryUnpacker()`), file bytes are spilled to `files/tmp/<fullPath>` and wrapped in a `FileBackedFileData` (random-access view, shared `RandomAccessFile`); the in-memory path wraps bytes in a `FileData` (see M2-03 §3).
- **Existing files are skipped**: `if(!Files.exists(ROOT.resolve(filename)))` — an initial tree build over an already-unpacked tree only queues missing nodes. Combined with the version gate (M2-03 §1) this makes re-runs incremental only after a version match; a version bump deletes everything first.
- Final write (`writeFile`): `files/<fullPath>` (parents created); nodes whose `FileData.realFileIndex() != -1` (virtual files, §6) are **not written** — they exist only as runtime views over real files. Empty-directory sentinels (MRG archives with 0 entries) become empty directories on disk.
- Output root: `Unpacker.ROOT = ./files`; replacements root: `./patches/replacements`.

## 6. Runtime resource access (source-verified)

Reference: `legend/game/unpacker/Loader.java`, `FileData.java`, `legend/game/DrgnFiles.java`. (R1 cross-check: async completion ordering and frame-bound visibility — see §6.3 and R1 `platform/02-execution-model.md`.)

### 6.1 Virtual path resolution

- All game code loads through **virtual paths** — the same `fullPath` strings the unpacker built, e.g. `SECT/DRGN1.BIN/402/3`, `font.tim`, `XA/LODXA00.XA/5.opus`, `OVL/SMAP.OV_`.
- `Loader.resolve(fixPath(name))`: `fixPath` strips a trailing `;version` (last `;`), drops a leading `/` or `\`, and converts `\` → `/`. `Unpacker.ROOT.resolve(...)` → `files/<path>`.
- There is **no archive layer at runtime**: the "MRG" structures are materialized as real directories + a small text index at unpack time (§6.2). Runtime archive access = directory read + index parse.

### 6.2 The `mrg` index file and virtual files

- Every unpacked MRG directory contains a file named `mrg`: lines of `name=target;size` (ASCII), written by `FileMap` (`Unpacker.unmrg`, `Unpacker.java:607-620`):
  - real entry i: `i=i;<size>`
  - virtual entry i (size-0 style): `i=<parentIndex>;0`
  - duplicate-offset entry i: `i=<duplicateIndex>;<own size>`
  - missing/unmapped file (branch transformers): `i=;<0>` (blank target means "no file").
- `Loader.loadDirectorySync(dir)`:
  1. If `dir/mrg` exists: parse all lines into `virtual→real` and `virtual→size` maps; produce a `List<FileData>` **indexed by virtual index**:
     - `real == -1` → `null` at that index;
     - regular file → real `FileData` (only when `virtual == real`; non-self-mapping real entries are placeholder `null`s until virtual resolution);
     - directory → `FileData(new byte[0])` (empty);
     - missing file → `null`;
     - **virtual entries** are resolved to "the realest file" by following `fileMap` chains until `real == real`, then become `FileData.virtual(realFileData, virtualSize, realIndex)`.
  2. If no `mrg` file: list the directory, keep regular files only, sorted by numeric name (else case-insensitive string), each as a real `FileData`. (Note: subdirectories are **not** represented in this mode — no empty entries.)
- `FileData.virtual(real, virtualSize, realFileIndex)` (`FileData.java:38-40`): a view over the real file's underlying byte array with `size = realFileData.length` (readable up to the full real size) but carrying `virtualSize` and `realFileIndex`:
  - `hasVirtualSize()` (`virtualSize != 0`) is used by game code as an **existence check** for optional files (e.g. `Battle.java:2701,2944`, `RetailSubmap.java:572`);
  - `realFileIndex()` identifies the shared real file — `RetailSubmap.java:602` shares one model across 33 sobj entries via `objects.get(submapModel.realFileIndex() / 33).model` (33 = sobj stride, see M2-02 §5).
- Error/missing behavior: a missing file yields `null` in the loaded list (directory) or a thrown `RuntimeException("Failed to load file …")` for direct loads (`Loader.loadFileSync`). Async loads swallow failures: `onFileLoadingException` logs and **completes with `null`** (`Loader.java:154-157`) — see §6.3 for the frame-bound contract that makes this safe for scripts.

### 6.3 Asynchronous loading

- `Loader.loadFile(s)` / `loadDirectory` run on `Async.EXECUTOR`, a **fixed thread pool sized to available processors** (`core/Async.java:11`), returning `CompletableFuture<FileData(List)>`. A global `LOADING_COUNT` tracks in-flight requests (`Loader.getLoadingFileCount()`).
- The ROM exposes the wait contract to scripts: `DrgnFiles.scriptWaitForFilesToLoad` (decompiled ROM, `@Method(0x80017564L)`, `DrgnFiles.java:128-140`, registered as script function 7 in `Scus94491BpeSegment_8004.functions[7]`) returns `PAUSE_AND_REWIND` while any load is in flight, else `PAUSE`. Combined with the interpreter's frame-top execution (R1), a script never observes a partially-loaded resource set: it rewinds until the queue drains. This is the resolution of R1 register item **Q-43** for the script path (failure → `null` completion → the script sees an absent asset, never a torn one).
- Synchronous variants (`loadFileSync`, `loadDrgnFileSync`, `loadDrgnDirSync`) exist for startup/critical paths (e.g. `font.tim`, `shadow.*` in `Scus94491BpeSegment.gameInit`).

### 6.4 DRGN script-file addressing (M2 research question 2)

Reference: `legend/game/DrgnFiles.java` (`loadDrgnFile(s/Dir)`), ROM global `drgnBinIndex_800bc058` (`DrgnFiles.java:22`).

- The ROM addresses script/data files as `(drgnBinIndex, fileIndex)` pairs; the file index is the **MRG entry index** inside `SECT/DRGN{n}.BIN` (a directory in the extracted tree whose children are the MRG entries).
- Index remapping (`DrgnFiles.java:40` and all `loadDrgn*` overloads):
  ```
  if (drgnBinIndex >= 2) drgnBinIndex = 20 + drgnBinIndex_800bc058;
  ```
  i.e. ROM indices `0` → `DRGN0.BIN`, `1` → `DRGN1.BIN`, and any value `≥ 2` means "chapter content on the current game disc", remapped to `DRGN21.BIN`–`DRGN24.BIN` by adding 20 to the **current disc number** held in ROM global `drgnBinIndex_800bc058` (1–4).
- `drgnBinIndex_800bc058` is set by ROM code: `preload()` → 1 (`Scus94491BpeSegment.java:636`); `SMap.java:4238` → 1 (reset on submap entry); `RetailSubmap.java:205` → the per-submap `drgnIndex` (chapter content lives on the disc matching the chapter). So the "which disc" decision is data-driven from the submap record; the physical disc is always selected by name in the merged tree (§4).
- Path construction: `SECT/DRGN` + n + `.BIN/` + file(s). Directories: `SECT/DRGN{n}.BIN/<dir>` loaded via `loadDrgnDir` (MRG subdirectory → list of entries).

## 7. Concrete end-to-end trace (source-verified)

One resource, disc sector → runtime bytes, for `SECT/DRGN21.BIN/402/3` (the Dragon's Nest plant object script):

1. **Disc 2** (SCUS94584) holds the ISO directory entries: root directory (PVD `0x9c` of disc 2's PVD) → `SECT` directory → `DRGN21.BIN` file record (LBA `S`, data length `L`). All of this is grafted into the **disc-4 master tree** at unpack start (§4).
2. `populateInitialFileTree` reaches the `DRGN21.BIN` file node: `IsoReader(disk 2).readSectors(S, L, raw=false)` → `L` bytes: an **MRG** archive (magic `0x1a47524d`, M2-02 §1). The node `SECT/DRGN21.BIN` enters the transformation queue.
3. `Unpacker.transform` runs the first matching leaf transformer:
   - `decompressDiscriminator`: root-MRG path exclusion does not apply (pattern `^SECT/DRGN0\.BIN/…`), and the MRG header has no BPE marker at `0x4` → no.
   - `mrgDiscriminator`: `readInt(0x0) == MrgArchive.MAGIC` → **yes**.
4. `unmrg` expands the MRG: entry 402 is a real (or virtual) entry → child node `SECT/DRGN21.BIN/402` (itself an MRG for the submap-object file group); entry 402's entry 3 → child node `SECT/DRGN21.BIN/402/3` with the raw entry bytes (sector-aligned: `offset * 0x800` within the `DRGN21.BIN` bytes, M2-02 §3). A `mrg` index file is added to each expanded directory.
5. The child re-enters the queue; `decompressDiscriminator` now matches (`readInt(0x4) == 0x1a455042`, BPE marker, M2-03 §5) → `decompress` replaces the node's data with the BPE-decompressed script bytes.
6. `drgn21_402_3_patcherDiscriminator` matches (`fullPath == "SECT/DRGN21.BIN/402/3" && size == 0xee4`) → the original script-jump-out-of-bounds patch is applied (extended to `0x107c`, `0x49` terminator at `0x1078`) — M2-03 §7.1.
7. After all leaves, `writeFile` writes `files/SECT/DRGN21.BIN/402/3` (+ `mrg` index files at each level).
8. **Runtime**: game code calls `DrgnFiles.loadDrgnFile(2-or-≥2, "402/3")` → remapped to `SECT/DRGN21.BIN/402/3` via the current disc index → `Loader.loadFile` (async) → `files/SECT/DRGN21.BIN/402/3` → `FileData` → `ScriptFile`. If loaded as a directory (`loadDrgnDir`), the `mrg` index maps virtual indices to the on-disk entries and materializes virtual files as views.

What this trace **cannot** establish without disc images: the actual LBA `S` and length `L` of `DRGN21.BIN`, the MRG entry offsets, and which disc the original retail disc-2 directory records physically reside on relative to the disc-4 merge (the merge only guarantees name resolution, not which disc's *data* a given name came from in collision cases). All such byte-level claims are `Inferred`/`Unresolved` and registered (M2-10 / register).

## 8. Known top-level tree (source-verified name inventory)

Names observed in source (not an exhaustive tree — the full tree requires the discs):

| Name | Kind | Evidence |
|---|---|---|
| `SCUS_944.91` | file — main executable (BPE-compressed PSX EXE) | `Unpacker.engineOverlayDiscriminator`, M2-04 |
| `SECT` | directory — all game content (`DRGN0.BIN`, `DRGN1.BIN`, `DRGN21..24.BIN`, …) | pervasive in code + patterns `DRGN_BIN`, `DRGN0_FILE`, `ITEM_SCRIPT` (`Unpacker.java:65-68`) |
| `OVL` | directory — engine overlays: `SMAP.OV_`, `S_BTLD.OV_` (asset carriers) | `Unpacker.smapAssetDiscriminator`, `extractBtldDataDiscriminator` |
| `STR` | directory — FMV sector streams `*.IKI` (18 known: `DEMOH.IKI` … `ENDING2H.IKI`) | `Fmv.diskFmvs_80052d7c` (`Fmv.java:87-90`) |
| `XA` | directory — `LODXA00.XA` … `LODXA03.XA` raw XA sector streams | `Audio.playXaAudio` load path, `XaTranscoder` constants (M2-10) |
| `files/version` | unpacker metadata (written last) | `Unpacker.unpack` |

## 9. Open items for this document

- Actual per-disc directory structure, LBAs, and the real name-collision set: **asset-validated** only (register).
- Whether any LoD directory exceeds the 0x800 single-directory-sector norm (the Hot Shots Golf 2 exception noted by no$psx): unknown, low impact (the reader already loops over multi-sector directories via `entryLength` chaining within a 2048-byte sector read — **only one sector per directory is read per level**; a multi-sector directory would be truncated. `Inferred` risk, unobservable without discs). *(source-verified limitation: `populateDirectoryTree` reads exactly one 2048-byte sector per directory.)*
