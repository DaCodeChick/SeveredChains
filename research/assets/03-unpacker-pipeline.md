# M2-03 — Unpacker Pipeline, Transformation Inventory, and Fix-Patch Catalog

Phase M2 deliverable 3 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/unpacker/Unpacker.java` (1531 lines), `Transformations.java`, `PathNode.java`, `FileData.java`/`ExpandableFileData.java`/`FileBackedFileData.java`, `LeafTransformation.java`, `BranchTransformation.java`, `Replacement.java`, `DeffArchive.java`, `SubmapPxlTransformer.java`, `CtmdTransformer.java`, `SaveCardTextureExtractor.java`, `core/audio/xa/XaTranscoder.java`. All claims **source-verified**; classification tags (bugfix / change / enable / neutral) are `Inferred` where noted. Validation categories per `AGENTS.md`: **source-verified** (reference code), **format-corroborated** (independent spec/implementation), **asset-validated** (requires real disc/asset bytes — none yet; no such claims in this document).

## 1. Pipeline stages (source-verified)

`Unpacker.unpack()` (`Unpacker.java:303-404`), in order:

1. **Version gate**: read `files/version` (integer). If `!= VERSION` (currently **5**, `Unpacker.java:71-73`, comment: "Update this any time we make a breaking change"): delete the entire `files/` tree (keeping `files/.gitignore`) and start fresh. No content-hash invalidation — cache invalidation is a **manual, global, breaking-change-only** bump. `Inferred`: any change to a transformer requires a bump, otherwise stale outputs survive.
2. **Disc acquisition**: `getIsoReaders` scans `./isos/`, validates each candidate via the PVD (M2-01 §2), waits (1 s poll, status listener, `UnpackerStoppedRuntimeException` on close) until all four disc readers exist.
3. **Tree build**: merged ISO9660 tree (M2-01 §4) → `PathNode` tree + FIFO `transformationQueue` of all file nodes. Existing files on disk are skipped (M2-01 §5).
4. **Leaf transformations** (concurrent): a virtual-thread-per-task executor; the main thread polls the queue and dispatches `transform(node, …)`; a status thread reports `transformations.getRemaining()` every 50 ms. `remaining` counts nodes: `addNode`/`addChild` increment, `transform` decrements exactly once after the first matching transformer runs. Transformation order across nodes is **non-deterministic**; per-node transformer selection is deterministic (first match in registration order).
5. **Branch transformations** (sequential over transformers): "Submap PXL converter", "Claire model fixer" — whole-tree post-passes.
6. **Replacements**: "Replace Chester texture" (repo-bundled asset).
7. **Write-out**: `files` tree flattened to a deque; virtual-thread-per-task executor writes each node to `files/<fullPath>` (skipping virtual `FileData`s, M2-01 §5); status thread every 50 ms.
8. **Version stamp**: `files/version` = `5`.
9. `finally`: `FileBackedFileData.closeAll()`.

### 1.1 Thread/ownership notes (cross-check with R1 `architecture/03`)

- Unpacking runs **before** the ROM's `main()` on the loading-screen thread (R1 lifecycle doc); the game's frame loop is not running.
- Per-disc `IsoReader` reads are serialized by a per-reader monitor (`synchronized(entry.reader())` in `readFile`).
- Transformation workers may touch shared `FileData`/`PathNode` structures only through the node they own (queue hand-off); the **global** `flags` set is a `synchronizedSet` (created in `unpack`), while each `PathNode`'s own `flags` is a plain `HashSet` — safe because a node is only read/transformed by one worker at a time (queue hand-off). `Transformations` guards its queue/counters with `synchronized` blocks on the queue.
- **Failure propagation caveat** (source-verified): `transformationThrowable` is set by a worker and checked by the dispatcher *between* polls — "won't catch everything since it's set async" (comment, `Unpacker.java:365`); the executor shutdown is re-checked. A worker exception during the branch/replacement stages propagates synchronously.

## 2. Transformation framework (source-verified)

### 2.1 Node model

`PathNode` (`PathNode.java`): immutable `fullPath`/`pathSegment`/`data`/`parent`; `children: ConcurrentHashMap<String, PathNode>` keyed by path segment; per-node `flags: Set<String>` (e.g. `"DEFF"` — set when a node came out of a DEFF archive, used by the CTMD discriminator).

### 2.2 `Transformations` (queue + tree helper)

- `addNode(fullPath, data)`: inserts/creates the path (intermediate directories as null-data nodes), enqueues the leaf, increments `remaining`. **Insert replaces any existing leaf** at that path (compute: non-final segments keep the existing node; final segment is always replaced) — re-transformed nodes are re-queued.
- `replaceNode(node, data)` = `addNode(node.fullPath, data)` (same path, new data).
- `addChild(parent, segment, data)`.
- `replaceWithFile(node, path)`: reads a repo-bundled file (`patches/replacements/…`) into the node.
- `decrementRemaining()` after each `transform`.
- `isEmpty()` ⇔ `remaining == 0` — the work queue is the termination condition.

### 2.3 `transform` (leaf stage)

`Unpacker.transform` (`Unpacker.java:536-553`): for the polled node, walk `transformers` in **registration order**; first whose `discriminator.matches(node, flags)` wins: the node is **removed from its parent's children**, the transformer runs (which re-adds the node under some path, possibly different), then `break` — exactly one transformer per dequeue. Nodes matching no transformer are dropped silently (they remain removed from the tree) — e.g. the `skipPartyPermutations` transformer matches files 3537-3592 and re-adds nothing (§4 #21). A `shouldStop` check at the top throws `UnpackerStoppedRuntimeException`.

### 2.4 Discriminator contract

The static-init comment (`Unpacker.java:84-87`) states the core invariant: **the pipeline is recursive; after a transformation the file is re-queued until no transformer applies, so every discriminator must recognize its own output and return false**, or infinite transformation loops. The de-facto "already processed" signals are: path/flag checks (one-shot `flags.contains(path)` guards for overlay extractors), magic changes (BPE→script: decompressed data no longer has the BPE marker; MRG magic consumed by `unmrg`), and data-size changes (patches change sizes, and their discriminators pin exact sizes — a patched file never re-matches).

## 3. Data model (source-verified)

- `FileData`: view over a byte array — `offset`, `size`, `virtualSize`, `realFileIndex` (M2-01 §6.2). All primitive reads are little-endian. `slice` shares the backing array (zero-copy). `checkBounds` throws `IndexOutOfBoundsException` (no soft errors).
- `ExpandableFileData`: auto-growing (×2) backing array, used by the MIDI→SSSQ dev tool only (M2-10 §5).
- `FileBackedFileData`: low-memory mode; per-file `RandomAccessFile` over `files/tmp/<path>`, all access `synchronized(file)`, close-on-exit. (`Unpacker.readFile` switches on `Config.lowMemoryUnpacker()`; the `OutOfMemoryError` catch in `unpack` flips the config and asks the user to restart — `Unpacker.java:392-399`.)

## 4. Leaf transformer registry — exhaustive inventory (source-verified)

Registration order = match order (`Unpacker.java:78-137`), numbered 1–29 exactly as registered:

| # | Name | Discriminator (match condition) | Transformation (output) | Notes / classification |
|---|---|---|---|---|
| 1 | Monster SFX | `DRGN0.BIN/<778..1289>/<idx>` subfile | re-home to `monsters/<id>/sounds/` via `battleAssetIdentifierSfx` map (`-1` → dropped) | comment: "This has to happen before MRG" — output paths must not be re-evaluated by MRG-adjacent transforms. **enable** (asset re-homing for mods) |
| 2 | Battle phase SFX | `DRGN0.BIN/<1290..1297>[/<n>\|/mrg]` | re-home to `monsters/phases/{doel,melbu,zackwell}/<phase>/<entity>` | before MRG (same reason); hardcoded entity/phase table. **enable** |
| 3 | Monster texture | `DRGN0.BIN/<2625..3136>/<idx>` | re-home to `monsters/<id>/textures/combat` | before MRG; encounter→monster table. **enable** |
| 4 | **Decompressor** | `readInt(0x4) == 0x1a455042` (BPE marker), **except** paths `SECT/DRGN0.BIN/<4031..4102>/**` | BPE-decompress in place (§5) | core; runs repeatedly for nested archives (decompressed MRG/DEFF re-queued). normalization |
| 5 | **Unmrg** | `readInt(0x0) == 0x1a47524d` | expand MRG to child nodes + `mrg` index (M2-02) | after decompress; recursive via re-queue. normalization |
| 6 | **Undeff** | `readInt(0x0) == 0x46464544` ("DEFF") | split DEFF archive into children `<i>`, set node flag `"DEFF"` | after decompress; DEFF entries re-queued (their own transforms apply). normalization |
| 7 | **Engine overlay** | path segment == `SCUS_944.91`, once (flag) | keep file + extract `lod_engine` = bytes `[0xa00, 0xa00+0x36228)` | one-shot; feeds #11 (LOD engine). **enable** + original data |
| 8 | `drgn21_402_3` | `SECT/DRGN2{1,2}.BIN/402/3` size `0xee4` | extend to `0x107c`, write `0x49` at `0x1078` | **bugfix** (§7.1) |
| 9 | `drgn21_693_0` | `SECT/DRGN21.BIN/693/0` size `0x188` | extend to `0x190`, fill `0xffff_ffff` words at `0x188/0x18c` | **bugfix** (§7.1) |
| 10 | `drgn0_142_anim` | `SECT/DRGN0.BIN/142` size `0x1f0` | inject 2 objects × 2 keyframes (12 B each) of animation for the post-Divine-Dragon Meru cutscene; `newData[0xc] = 22` (object count) | **bugfix** (§7.1) |
| 11 | **LOD engine** | path == `lod_engine` | extract `shadow.ctmd` `[0x3d0, +0x14c)`, `shadow.anim` `[0x51c, +0x28)`, `shadow.tim` `@0x544` (TIM-sized), `font.tim` `@0xb6744` (TIM-sized) | consumes #7 output (M2-04). original data exposure |
| 12 | **SMAP assets** | `OVL/SMAP.OV_`, once (flag `"SMAP"`) | keep file + extract `SUBMAP/savepoint` `[0x10694, +0x904)` and 13 TIMs at fixed offsets (`0x100b4, 0x10214, 0x10f98, 0x115d8, 0x11858, 0x11e98, 0x11f58, 0x12098, 0x122d8, 0x12518, 0x12658, 0x12798, 0x129d8`) | consumes OVL file (M2-04). original data exposure |
| 13 | Lavitz oof Dart hand | `SECT/DRGN0.BIN/5546/1` size `0x721c` | script rewrite: two in-place jumps + appended sub-function ops (hand/arm restore; Lavitz chest-plate z-offset) → size `0x7284` | **bugfix** (§7.1) |
| 14 | Magma fish `drgn0_3667_16` | `DRGN0.BIN/3667/16` size `0x538` | `patchAnimation(data, 11)`: rebuild keyframe tables for 11 objects (was fewer), replicate first-object transforms per keyframe, zero-fill last, set count `@0xc` | **bugfix** (§7.1) |
| 15 | Magma fish `drgn0_3667_17` | `DRGN0.BIN/3667/17` size `0x178` | as #14 | **bugfix** |
| 16 | Magma fish `drgn0_3750_16` | `DRGN0.BIN/3750/16` size `0x538` | as #14 | **bugfix** |
| 17 | `drgn1_343` | `SECT/DRGN1.BIN/343` size `0x3ab4` and byte `@0x3a70 == 8` | set byte `@0x3a70 = 3` (Faust battle coord2 index 8→3) | **bugfix** (§7.1) |
| 18 | Player combat sounds | `DRGN0.BIN/<752..772>/**` | re-home to `characters/<name>/sounds/combat/<leaf>` (char id map 752:0-2 dart/lavitz/shana, 753:2 rose, 754:2 haschel, 755:2 meru, 756:2 kongol, 757:2 miranda, 772:1 albert) | **enable** (character asset paths) |
| 19 | Player combat models/textures | `DRGN0.BIN/<3993..4010>/**` (`mrg` files only for even ids) | re-home to `characters/<name>/{textures,models}/combat[...]` (id = 3993+2k textures, 3994+2k models, k=0..8) | **enable** |
| 20 | Dragoon combat models/textures | `DRGN0.BIN/<4011..4030>/**` (same mrg rule) | re-home to `characters/<name>/{textures,models}/dragoon[...]` (k=0..9, id 9/10 → `divine`) | **enable** |
| 21 | **Skip party permutations** | `DRGN0.BIN/<3537..3592>/**` | **drop** (no re-add) — storage saving; 56 file ids | neutral reference enhancement (retail content intentionally not unpacked) — `Inferred` that retail uses these at runtime (they exist on disc); no runtime consumer found in decomp |
| 22 | **BTLD data extractor** | `OVL/S_BTLD.OV_`, once | keep file + extract `encounters` `= slice(0x68d8, 0x7000)` and `player_combat_script` `= slice(0x4, 0x68d4)` | feeds #26 + runtime (M2-04). original data exposure |
| 23 | **UI patcher** | `SECT/DRGN0.BIN/6666` and byte `@0x24a8 != 0` | zero 6 bytes (`0x24a8+i*0x14` and `+0x2` for i=0..2) — removes baked-in slashes in character-card fractions | **change** (cosmetic; reference renders the fraction slash itself) |
| 24 | **Portrait extractor** | `SECT/DRGN0.BIN/6665`, once | walk 3 concatenated TIMs in the save-card file; extract 9 portrait PNGs (`characters/<name>/portrait.png`), 8 spirit icons (`goods/<spirit>/icon.png`), `retail_atlas.png` | **enable** (PNG assets) |
| 25 | **CTMD converter** | node flag `DEFF`, size ≥ `0x14`, DEFF-part type byte `@3 ∈ 0..4`, container offset `@0xc` valid, `readInt(containerOffset)==0xc`, byte `@containerOffset+0xc == 0x41` (TMD `'A'`), TMD flags `& 0x2` (CTMD) | unpack CTMD → plain TMD in place, adjust DEFF/0xc container pointers (M2-05 §5) | **enable** (editable TMDs) — stored model data changes, no intended gameplay change |
| 26 | **Player script damage caps** | path == `player_combat_script`, `readInt(4) != MRG magic`, once | write `0x3b9aca00` (999999999) at `0x9f0, 0xa00, 0x82b0, 0x82c0, 0xe190, 0xe1a0` | **change** (removes player damage caps) — "cap" semantics `Inferred` from sibling patches (9999↔999999999 pairs) |
| 27 | **Enemy script damage caps** | `SECT/DRGN1.BIN/**`, once per path | scan u16-aligned; where `readUShort(i)==9999 && readUShort(i+0x10)==9999` → write int 999999999 at both, skip `i+=0x10` | **change** (removes enemy damage caps; javadoc: "Every single enemy script has damage caps build into it for some reason") |
| 28 | **Item script damage caps** | `DRGN0.BIN/<fileId>/1.*` with even `fileId` in `4140..5500`, once per path | same patcher as #27 | **change** (removes item damage caps) |
| 29 | **XA audio converter** | path ends `.XA` | XA→Opus transcode (M2-10 §4); one opus per active track | normalization + reference audio pipeline |

### Branch transformers (post-leaf) and replacements

| Name | Condition/operation | Classification |
|---|---|---|
| **Submap PXL converter** (`SubmapPxlTransformer`, branch) | for each present `DRGN2x.BIN` (x=1..4): per submap cut (asset dir `<cut+1>`, script dir `<cut+2>`, step 3 through the asset MRG): parse the 3 PXL sheet TIMs (last 3 MRG entries); per sobj read its model's texture slot (16 standard slots + 2 high-res double-width slots, `textureSlotRects`); cut the 16×128 (or 32×128) texture region from the PXL sheet → new 4bpp CLUT TIM written to `textures/<sobj>` + `textures/mrg` index; sobjs whose model is a virtual file map to `textures/<sobj> = <source sobj>;0`; missing → `"";0` + warning; **Lenus 2 face CLUT replacement** (GH#1845) for `DRGN22.BIN/980` sobj 5 (32-color CLUT written at `0x14`) | normalization (splits sheets into per-object textures) + **bugfix** (Lenus CLUT) |
| **Claire model fixer** (branch) | if `DRGN22.BIN` & `DRGN23.BIN` present: `DRGN22.BIN/863/33` ← `DRGN23.BIN/506/33`; if `DRGN24.BIN` & `DRGN23.BIN`: `DRGN24.BIN/260/33` ← `DRGN23.BIN/506/33` | **bugfix** (broken Claire face UVs on disk 2/4, fixed with disk 3's model) |
| **Replace Chester texture** (`patches/replacements/`, replacement) | `SECT/DRGN2{1,2}.BIN/260/textures/4` ← repo file `chester.tim` | **bugfix / content replacement** (face/chest swap on Bale library NPC; replacement asset shipped in the reference repo) |

## 5. BPE decompression format (source-verified)

Reference: `Unpacker.decompressDiscriminator` / `Unpacker.decompress` (`Unpacker.java:555-566`, `1402-1500`).

**Identification**: file ≥ 8 bytes and `readInt(0x4) == 0x1a455042` (LE; bytes `42 50 45 1a` = `"BPE" + 0x1a`). **Exception**: paths matching `SECT/DRGN0.BIN/<4031..4102>` are never decompressed (path-based bypass, `Unpacker.java:557-561`).

**Layout**:

```
0x00  u32  decompressed size (destination length)
0x04  u32  marker 0x1a455042
0x08  block stream, per block:
      u32  block size (decompressed bytes in block; must be <= 0x800; 0 = end of stream)
      adaptive-dictionary build section:
          key = 0; while key < 256:
            byte b:
              b >= 0x80 : key += b - 0x7f            (skip; no data bytes consumed)
              else      : for i in 0..b:
                            dictL[key] = next byte
                            if dictL[key] != key: dictR[key] = next byte
                            key++
      block data section, repeat until block bytes emitted:
          push byte; while queue non-empty: pop k
            dictL[k] == k : emit k
            else          : push dictR[k], push dictL[k]   (LIFO ⇒ dictL emitted before dictR)
      word-align archive position: offset = roundUp(offset, 4)
      next block …
```

- Dictionary: 256-entry **byte-pair** table (`dictL`/`dictR`); identity-initialized per block and re-loaded from each block's build section, so every block is self-contained.
- Errors: block size > `0x800` → `RuntimeException("Decompression error")`; marker mismatch → `RuntimeException("Attempted to decompress non-BPE segment")`.
- Coverage: the discriminator is content-based (marker at `0x4`), so it applies to every BPE-compressed file in the tree — the main EXE (`SCUS_944.91`), overlays (`OVL/*.OV_`), and most `SECT` script/data files.
- **Format corroboration**: no$psx documents a general PSX "BPE (Byte Pair Encoding)" file-compression scheme (§13.84, Psx-spx) and a game-specific `BPE_` variant (static dictionary, different header, e.g. Bust A Groove). The LoD variant's per-block self-describing dictionary and `size @0x0 / marker @0x4` header are **source-verified**; its byte-level identity with a documented SCE toolchain variant is not established (`Inferred` relationship only) — registered as an open item (M2-10 / register).

## 6. DEFF container (disk-level) (source-verified)

Reference: `DeffArchive.java` (unpack side), `DeffPart.java` (runtime part formats → M2-05 §4).

```
0x00  u32   magic 0x46464544 ("DEFF", LE)
0x04  u32   (unparsed by DeffArchive — unknown)
0x06  u16   entry count
0x08  …     (2 bytes unparsed before the entry table at 0x0c)
0x0c  8*N   entries: u32 (unparsed), u32 offset
```

- File i = bytes `[offset_i, offset_{i+1})` (last = end of archive) (`DeffArchive.java:14-30`).
- `undeff` splits into children named by index, tagging each with node flag `"DEFF"` (consumed by the CTMD discriminator) and re-queueing.
- Runtime DEFF **part** header (per-entry structure, M2-05 §4): first u32 `flags_00` with `type = flags >>> 24` (0=LMB, 1/2=animated TMD, 3=TMD, 4=sprite, 5=CMB; 7 noted as existing).

## 7. Fix-patch catalog — before/after semantics (M2 research question 6)

Tags: **[bugfix]** repairs original data/behavior; **[change]** deliberate behavior change beyond retail; **[enable]** exposes/normalizes content for tooling/mods; **[neutral]** no intended behavior change.

### 7.1 Original bug fixes

| # | Target (precondition size) | What retail does wrong | Unpack-time fix | Evidence |
|---|---|---|---|---|
| 8 | `DRGN2{1,2}.BIN/402/3` (0xee4) — Dragon's Nest plant object script | script jumps out of bounds into the next script (worked on-disc because MRG files were adjacent) | extend script to `0x107c`, `0x49` (return) at `0x1078` | javadoc `Unpacker.java:572-581` |
| 9 | `DRGN21.BIN/693/0` (0x188) — Hoax post-Kongol submap controller | on the "I still don't know…" dialog choice, script pushes an inline parameter past its end | extend to `0x190`, fill 2×`0xffffffff` (sentinel handled in `scriptReadGlobalFlag2` → returns 0) | javadoc `Unpacker.java:588-594` |
| 10 | `DRGN0.BIN/142` (0x1f0) — Meru cutscene anim (post-Divine-Dragon fight) | 2 of 22 objects missing animations (visible at top of her leg) | inject 12-B keyframes for the missing objects (data credited to "DooMMetaL"), set object count `@0xc` = 22 | javadoc `Unpacker.java:601-606` |
| 13 | `DRGN0.BIN/5546/1` (0x721c) — Lavitz "out of fight" scene script | Dart's left hand/forearm turned off for the hand-clasp close-up and never turned back on; Lavitz's damaged chest plate visible behind Albert | two in-place ops replaced by jumps to appended code: (a) re-add hand+arm, jump back; (b) z-offset the chest-plate object off-screen and back | javadoc `Unpacker.java:621-633` + patch body |
| 14-16 | `DRGN0.BIN/3667/16` (0x538), `3667/17` (0x178), `3750/16` (0x538) — magma/lava fish swim animations | "missing the last object" in the animation (3 different files for the same bug) | `patchAnimation(data, 11)`: rebuild keyframe tables for 11 objects (was fewer), replicate first-object transforms per keyframe, zero-fill last, set count `@0xc` | javadoc ×3 + `patchAnimation` (`Unpacker.java:845-862`) |
| 17 | `DRGN1.BIN/343` (0x3ab4, byte@0x3a70==8) — first Faust battle script | script accesses coord2ArrPtr index 8; model has only 6 objects | change hardcoded param 8 → 3 (the index set by `800ee3c0` at battle start) | javadoc `Unpacker.java:890-895` |
| B1 | `DRGN22.BIN/863/33`, `DRGN24.BIN/260/33` (Claire models) | broken face UVs on disk 2/4 Claire models | replace with `DRGN23.BIN/506/33` (disk 3 good model) | branch transformer javadoc (`Unpacker.java:831`) |
| B2 | `DRGN2{1,2}.BIN/260/textures/4` (Chester, Bale library) | face/chest textures swapped | replace with `patches/replacements/chester.tim` (shipped asset) | javadoc (`Unpacker.java:820-824`) |
| P1 | `DRGN22.BIN/980` sobj 5 (Lenus 2) texture (GH#1845) | busted face texture (bad CLUT) | 32-entry CLUT replacement written at TIM offset `0x14` during PXL conversion | `SubmapPxlTransformer.java:107-111` |

### 7.2 Behavior changes (reference opinions, not original behavior)

| # | Target | Change | Tag |
|---|---|---|---|
| 26 | `player_combat_script` (extracted from `S_BTLD.OV_`) | 6 cap values → 999999999 (`[0x9f0,0xa00],[0x82b0,0x82c0],[0xe190,0xe1a0]`) | **[change]** player damage caps removed |
| 27 | all `SECT/DRGN1.BIN/**` (enemy scripts) | u16 pairs `(9999 @i, 9999 @i+0x10)` → ints 999999999 | **[change]** enemy damage caps removed |
| 28 | `DRGN0.BIN/<4140..5500 even>/1.*` (item scripts) | same as #27 | **[change]** item damage caps removed |
| 23 | `DRGN0.BIN/6666` (UI data) | zero 6 slash-marker bytes (character card fractions) | **[change]** cosmetic — reference renders the fraction slash itself |
| 21 | `DRGN0.BIN/<3537..3592>/**` | files dropped (not written to `files/`) | **[neutral]** "Skip party permutations" — retail content intentionally not unpacked |

### 7.3 Content re-homing / exposure (enable; data unchanged unless noted)

Monster/character path remaps (#1, 2, 3, 18, 19, 20), overlay asset extraction (#7, 11, 12, 22), portrait PNG extraction (#24), CTMD→TMD (#25 — *data changes*: compressed→uncompressed TMD, container pointers adjusted; intended content-identical), XA→Opus (M2-10 §4 — codec change, intended quality-neutral at 48 kHz), submap PXL→per-object TIMs (branch — data reshaped, pixels preserved except Lenus CLUT).

## 8. Determinism and ordering constraints (source-verified)

- **Within a node**: transformer order is fixed (registration order); a node is transformed at most once per dequeue; re-queued outputs are re-evaluated from the top, so a later-registered transformer can still process an earlier transformer's output (that is the design — e.g. `undeff` output → `CTMD converter`; `BTLD data extractor` output → `Player script damage caps`; `Engine overlay` output → `LOD engine`).
- **Across nodes**: virtual-thread dispatch order is non-deterministic; correctness does not depend on it because (a) transformers are pure functions of `(node path, node data, flags)`; (b) cross-node interactions (branch/replacement stages) run only after *all* leaf work completes; (c) the only cross-node interaction in the leaf stage is the one-shot `flags` guards (synchronized sets).
- **Output determinism**: file *content* is deterministic for a given disc set; write order is not. `files/version` makes the tree atomic w.r.t. the running game (written last).

## 9. Error handling and recovery (source-verified)

- **Stop**: `Unpacker.stop()` sets `shouldStop`; the disc-wait loop and each `transform` check it → `UnpackerStoppedRuntimeException` (window close during loading; R1 F-09).
- **OOM**: `catch (OutOfMemoryError)` in `unpack` → enables `Config.lowMemoryUnpacker()` + `Config.save()` + status "restart the game" (next run uses `FileBackedFileData` spill to `files/tmp`).
- **Other failures**: wrapped in `UnpackerException` and rethrown → loading screen stays (no in-app recovery; R1 lifecycle doc).
- **Transformation worker failure**: `transformationThrowable` (AtomicReference) + post-shutdown recheck (§1.1).
- **Per-transformer validation failures** (content-based): throw `RuntimeException` (BPE block size, "Need to support mixed files" in CTMD, invalid DEFF type, etc.) — they abort the whole unpack.
- **Partial-unpack tolerance**: branch transformers/replacements guard on `children.containsKey(...)` ("if we're doing a partial unpack, we might not have all DRGN2x", `SubmapPxlTransformer.java:21`) — the pipeline supports running over a tree that was only partially populated (the existing-file skip in M2-01 §5 makes re-runs possible).

## 10. psx-sdk / reusability notes (proposed-layer observations, no design decisions)

- Reusable PSX-format work: BPE (if the variant maps to a documented SCE scheme), XA ADPCM decode + transcode (M2-10), ISO9660/PSX disc reading (M2-01), MDEC (M2-09), TMD/TIM (M2-05/06, standard Sony formats with LoD quirks).
- LoD-specific (game layer, not platform): the entire leaf-transformer policy (asset re-homing, damage caps, bug fixes), sobj 33-stride conventions, PXL sheets, SMAP/S_BTLD/lod_engine extraction offsets, CTMD, SSSQ, DEFF, MRG policy flags.
- Coupling risk: the unpacker's "path is identity" model (discriminators key on exact virtual paths and exact sizes) makes any future reimplementation's path/size changes breaking by construction; a spec for the *policy* (which paths are special) is as important as the format specs for compatibility.
