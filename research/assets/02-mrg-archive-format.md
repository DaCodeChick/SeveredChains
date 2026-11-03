# M2-02 — MRG Archive Format

Phase M2 deliverable 2 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/unpacker/MrgArchive.java` (decoder), `Unpacker.unmrg` (expansion), `Loader.loadDirectorySync` (runtime re-assembly), `FileMap.java` (index writer). No disc images: all claims **source-verified** unless noted; nothing here is `asset-validated`. Validation categories per `AGENTS.md`: **source-verified** (reference code), **format-corroborated** (independent spec/implementation), **asset-validated** (requires real disc/asset bytes — none yet).

## 1. Container layout (source-verified)

```
offset  size  field
0x000   4     magic  = 0x1a47524d  (little-endian int; bytes "MRG" 0x1a)
0x004   4     count  = N (number of entries)
0x008   8*N   entry table: for i in 0..N-1:
              0x008 + i*8 + 0x0    u32  offset
              0x008 + i*8 + 0x4    u32  size        (0 marks a virtual entry)
```

- All fields little-endian (reference reads via `FileData.readInt`, which is LE).
- Discriminator: `data.size() >= 8 && readInt(0x0) == 0x1a47524d` (`Unpacker.mrgDiscriminator`, `Unpacker.java:646-648`).
- The MRG container is **LoD-specific** (no external PSX-format corroboration found; the name/structure do not match the generic no$psx PSX archive catalog). Treated as game data format, layer "LoD".
- `MrgArchive` constructor (`MrgArchive.java:11-56`):
  - Pass 1 (real entries): for each i with `size != 0`:
    - **duplicate detection**: if an earlier real entry j < i has the same `offset`, entry i is recorded as virtual over j: `Entry(offset, size, virtualSize=size, parent=j)`.
    - otherwise: `offset` is interpreted per the `sectorAligned` flag (§3) → `Entry(offset, size)`.
  - Pass 2 (virtual entries): for each i with `size == 0`: `offset` is a **byte offset into a parent file**; the parent is found by scanning real entries for a matching offset (`getRealEntryByOffset`). If found: `Entry(parent.offset, parent.size, virtualSize=0, parent=parentIndex)`. If **not found**: the degenerate `Entry(offset, 0)` (non-virtual, zero size) is kept — a size-0 file in the tree.
- `Entry` record (`MrgArchive.java:92-97`): `(offset, size, virtualSize, parent)`; `virtual() ⇔ parent != -1`.

### Entry semantics table

| kind | table `size` | table `offset` means | produced `Entry` |
|---|---|---|---|
| real | `> 0` | archive-relative (×0x800 if sector-aligned, §3) | `(off, size, size, -1)` |
| duplicate of earlier real | `> 0` | same as earlier entry's offset | `(earlier.off, size, size, earlierIndex)` |
| virtual (size-0) | `= 0` | byte offset into the parent file | `(parent.off, parent.size, 0, parentIndex)` |
| orphan virtual | `= 0` | offset matches no real entry | `(offset, 0, 0, -1)` (size-0 file) |

## 2. Expansion (`unmrg`) — archive to tree (source-verified)

Reference: `Unpacker.unmrg` (`Unpacker.java:650-671`).

- `new MrgArchive(node.data, sectorAligned = DRGN_BIN.matcher(node.pathSegment).matches())` — the **path-segment name** decides sector alignment: any node named `DRGN0.BIN`…`DRGN4.BIN` (regex `^DRGN(?:0|1|2[1234])?.BIN$`, `Unpacker.java:66`) gets sector-aligned offsets; everything else uses byte offsets.
- Real (non-virtual) entries become **child nodes** named by their **entry index** (`Integer.toString(i)`), with data `node.data.slice(entry.offset(), entry.size())`.
- A text **`mrg` index file** is always added as a child named `mrg` (`FileMap`):
  - line format `name=target;size\n` (ASCII, `FileMap.addFile`, `FileMap.java:11-14`);
  - real entry i: `i=i;<size>`;
  - virtual entry i: `i=<parentIndex>;<virtualSize>` (virtualSize = 0 for size-0 virtuals, own size for duplicates);
  - `unmrg` also handles `count == 0` → an `EMPTY_DIRECTORY_SENTINEL` node (empty directory on disk).
- Virtual entries are **not written as separate child files**; the index line + the real parent file is the whole representation. At runtime, `Loader.loadDirectorySync` rebuilds them as `FileData.virtual(...)` views (M2-01 §6.2).
- **Ordering note:** children are inserted in entry-index order (`addChild` per entry), but `PathNode.children` is a `ConcurrentHashMap` — order is not preserved in the tree; the `mrg` index is the only authoritative order.

## 3. Sector-aligned vs byte offsets (source-verified)

- For `DRGN*.BIN` top-level files (the disc-level game archives): entry `offset` is a **sector count**, actual byte offset = `offset * 0x800` (`MrgArchive.java:33-37`).
- For everything else (e.g. `XA/LODXA0n.XA`, `OVL/*.OV_` if MRG-structured, submap MRGs inside `DRGN2x.BIN`): `offset` is a plain **byte offset** into the archive bytes.
- Consequence: sub-archives (MRGs inside MRG entries, e.g. `DRGN21.BIN/402`) are never sector-aligned — their offsets are relative to the sliced parent entry bytes.

## 4. Runtime re-assembly (source-verified)

Reference: `Loader.loadDirectorySync` (`Loader.java:176-262`), M2-01 §6.2 for full semantics. Key points relevant to format compatibility:

- The `mrg` index is the **only** runtime artifact of the MRG structure; the archive container itself does not exist after unpacking.
- Virtual resolution follows `target` chains to the "realest" index (`while(fileMap.get(real) != real)`) — chain length > 1 is possible in principle (duplicate-of-duplicate); the code handles arbitrary depth.
- `virtualSize` in the index is authoritative for `hasVirtualSize()` existence checks; the runtime `FileData` for a virtual file is a view of the real file's full byte range with `realFileIndex` set.

## 5. Structural conventions observed in LoD MRG contents (source-verified)

These are *content* conventions of particular LoD MRGs, not container properties:

1. **Submap cut assets** (`SECT/DRGN2x.BIN/<cut+1>` MRGs, consumed by `SubmapPxlTransformer`, `SubmapPxlTransformer.java:17-40`):
   - entries grouped in strides of **33** per submap object (sobj): entry `sobj*33` = model (CContainer/TMD), entries `sobj*33+1 .. +32` = 32 animation files;
   - the **last 3 entries** of the MRG are three **PXL sheets** (large TIM texture sheets, M2-06 §5);
   - companion **script MRG** (`<cut+2>`) ends with a 4- or 8-byte file: 8 bytes = two i32 high-res texture sobj indices (each stored −1); 4 bytes (size 0x4) = no high-res (`getHighResTextureIndices`, `SubmapPxlTransformer.java:51-64`).
2. **Battle assets** (`DRGN2x.BIN/<dir>` MRGs, dev tool `Finderator.java:22-44` + `RetailSubmap.java:602`): 33 entries per sobj (1 model + 32 anims), model at `sobj*33`, anims at `+1..+32`; `realFileIndex()/33` recovers the owning sobj for model sharing.
3. **Battle character asset files** (M2-03 §7.4): `DRGN0.BIN/752..757, 772` (player combat SFX), `3993..4030` (player/dragoon combat models+textures), `2625..3136` (monster textures), `778..1289` (monster SFX), `1290..1297` (phase SFX), `3537..3592` (party permutations — dropped), item scripts `DRGN0.BIN/<4140..5500 even>/1.*`, enemy scripts `DRGN1.BIN/*`.

## 6. Edge cases and assumptions

- **Zero-count MRG** → empty directory (sentinel). *(source-verified.)*
- **Orphan virtual entries** (offset matches no real entry) become size-0 non-virtual files; the runtime would then see an empty `FileData` at that index (and the index line `i=<offset>;0`… actually the orphan is *non-virtual* in `MrgArchive`, so `unmrg` writes it as a **real child file of size 0** *and* an index line `i=i;0`). *(source-verified; whether retail MRGs contain orphans is asset-validated only.)*
- `getRealEntryByOffset` matches on the **stored** offset field of real entries (post sector-alignment scaling), so virtual offsets are byte offsets into the *scaled* coordinate space — i.e. into the parent's slice as expanded. *(source-verified by construction order: pass 1 stores scaled offsets before pass 2 resolves parents.)*
- Duplicate detection only compares against **real** entries; a duplicate-of-a-virtual is impossible (virtuals have size 0 and are skipped in pass 1).
- No checksums, no version field, no compression inside MRG. Entries may be in any order; the index is by position, not name.
- Maximum entry count is bounded by the file size (4 + 8N ≤ size); the reference does not validate `count` against remaining bytes beyond per-entry `FileData` bounds checks (an oversized `count` would throw `IndexOutOfBoundsException` at `readInt` — `Inferred` robustness note, no defensive check found).

## 7. psx-sdk / reusability notes (proposed-layer observations, no design decisions)

- The container is a trivial positional archive; a future implementation needs only: LE u32 magic/count, 16-byte entry stride, the sector-alignment flag per containing file, duplicate/virtual semantics, and the text index convention (which is a *reference-invention* for runtime, not an original format).
- The 33-stride sobj layout, PXL tail, and high-res index file are **LoD game-data conventions**, not format features — they belong to the game layer, not a reusable PSX crate (see M2-10 §7 for the reusable-vs-specific split).
