# Asset Pipeline — Original Data → Native Loader

Reference commit: `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`
Paths relative to `~/Documents/GitHub/Severed-Chains`.

## 1. Source of Truth: Player-Provided Disc Images

- 4 PS1 discs (US: SCUS94491–SCUS94494; EU/JP/Asia IDs supported) as ISO or BIN,
  placed by the player in `isos/`. **Never bundled or redistributed.**
- The Java unpacker polls for all 4 (1s interval, status + skip UI), then merges
  the ISO9660 directory trees — root from disk 4, others added into the same tree
  (`Unpacker.getIsoReaders`, `loadRoot`, `IsoReader`, PVD at sector 16).
- The PS1 disc virtual filesystem surfaces as `SECT/DRGNn.BIN/dirnum/filenum`
  (the classic "DRGN" binary archive layout), plus root-level `DRGN0.BIN`..
  `DRGN4.BIN` and `OVL/` overlays.

## 2. Extraction Pipeline (verified, `legend.game.unpacker`)

Flow (`Unpacker.unpack`):
1. Version check: `files/version` vs `VERSION = 5`; mismatch → wipe `files/` and
   re-unpack. (File-backed low-memory mode on OOM: `FileBackedFileData`,
   `Config.enableLowMemoryUnpacker`.)
2. Build `PathNode` tree of all disc files.
3. **Leaf transformations** (parallel, Java 25 virtual threads; each node may
   spawn children): ordered `transformers` list — discriminated by content:
   - **BPE decompress**: files with u32 at offset 4 == `0x1a455042`
     ("BPE" segments; the main EXE uses the same codec). Format (verified in
     `Unpacker.decompress`): header `[0]=decompressed size, [4]=magic`; blocks of
     ≤0x800 bytes each preceded by u32 decompressed-size (0 = end); per-block
     256-entry adaptive byte-pair dictionary (leftch, rightch when leftch != key);
     iterative substitution decompression. Self-contained codec, ~100 lines —
     port to C++ with a vectorized inner loop. **Reusable PS1 tech.**
   - **MRG unpack**: magic `0x1a47524d`; u32 count at 0x4; 8-byte
     `{offset,size}` entries; "virtual" entries (size==0) are slices of a real
     entry (offset points inside the parent); DRGN*.BIN roots use 0x800
     (2048-byte) sector alignment. Emits an `mrg` file map (JSON) recording
     virtual→parent mapping.
   - **DEFF unpack**: magic `0x46464544` ("DEFF"), u16 count at 0x6, 8-byte entry
     table (size field at +4); entries are contiguous slices.
   - **SMAP asset extraction**: `OVL/SMAP.OV_` overlay → fixed-offset extraction of
     submap shared assets into `SUBMAP/` (shadow/alert/arrow/savepoint/dust/smoke
     TIMs, savepoint data at 0x10694) — hardcoded offsets, documented.
   - **Engine overlay extraction**: other `OVL/*` overlays split into sections.
   - **Per-file patchers**: ~10 exact-path+size-gated binary patches (verified
     examples: `drgn0_5546_1` — fixes Dart's missing hand in the OoF close-up by
     rewriting script ops; `drgn21_693_0`, `drgn0_142` animation patches;
     `drgn21_402_3`). These are content-correctness fixes the original had.
4. **Branch transformations** (per-directory) and **replacements**
   (`patches/replacements/`, e.g. `chester.tim` — a community replacement model).
5. Write tree to `files/` (parallel), then `files/version = 5`.
6. **Script patching** (separate pass after unpack): `ScriptPatcher` applies
   `patches/` (the external `script-recompiler` library) — story scripts
   disassembled to "fate" text, patched, reassembled; cache `files/patches/cache`,
   backups `files/patches/backups`; meta tables in `patches/meta/*.csv`.
7. **XA audio transcoding**: `XaTranscoder` decodes PS1 XA ADPCM (4-step filter,
   zigzag tables — `legend.core.spus.XaAdpcm` / `XaTranscoder`), resamples with
   80-tap cubic interpolation, encodes to **Opus** (960-sample frames, pre-skip
   312 samples to hide XA filter warmup) → `files/XA/LODXA0n.XA/m.opus`.
   **Modernization decision in the reference: CD audio is pre-transcoded at
   unpack time; runtime streams Opus.** (Runtime XA decode exists too —
   `XaPlayer` — but the shipped flow uses the Opus cache.)

Runtime loading (`Loader`): `loadFileSync` / async `loadFile` (CompletableFuture
on the `Async` pool) from `files/`; `DrgnFiles` caches DRGN bin indices; models
load asynchronously (`TmdObjLoader`, `Addition.loadAnimations`,
`Models.loadModelAndAnimation`).

## 3. Asset Formats (verified from source)

Byte-level layouts with per-field provenance for the formats implemented so far
(BPE, MRG, DEFF, TIM) — including where this project deliberately deviates from
the original readers — are in [psx-formats.md](psx-formats.md).

| Format | Class(es) | Notes |
|---|---|---|
| TMD | `tmd.Tmd`, `TmdHeader`, `TmdObjTable1c` | 8-byte header (flags: 0x1 fixp unsupported, 0x2 CTMD), nobj, 0x1c-byte obj tables, per-primitive PS1 GP0-encoded vertex data (uv, clut, tpage, colour, normal idx, vertex idx) |
| CTMD | `unpacker.Ctmd`, `CtmdTransformer` | Scaled TMD variant (per-obj scale at +0x18), converted to TMD at unpack; battle/cutscene models |
| TIM | `tim.Tim` | 12-byte header (rect + flags: bpp[2:0], hasClut[3]), clut rect @0xc, image rect, packed pixel data; 4/8/15bpp |
| DABA | `core.gpu.VramTextureLoader`, `VramTextureSingle/Stitched` | VRAM-page textures positioned at absolute 1024×512 coordinates; tpage = 64×256 sub-regions |
| ANM | `types.AnmFile/AnmSequence/AnmSpriteGroup/AnmSpriteMetrics14/Keyframe0c` | Sprite-group animation: 8-byte sequence entries, 14-byte sprite metrics, keyframes; used for character cutscene animations |
| CContainer | `types.CContainer`, `CContainerClutAnimations` | Sprite container: TmdWithId + CLUT animation table + subfile (frame data, 7-byte frames by default) |
| MCQ | `types.McqHeader`, `McqBuilder` | Menu quad files (UI geometry) |
| SShd | `sound.Sshd` | SPU sound file: magic "SShd" @0xc, 28 subfile offsets (voice/ADPCM data, volume ramps, patch lists, embedded SSSQ) |
| SSSq | `sound.Sssq` | SPU sequence (note events over a soundbank) |
| Mid (modding) | `unpacker.midi.*` | **Tooling only**: MIDI ↔ SSSQ conversion + soundfont export for music mods |
| XA | `core.audio.xa.XaTranscoder`, `core.spus.XaAdpcm` | CD audio; decoded → resampled → Opus at unpack |
| CMB/DEFF (combat) | `combat.deff.Cmb`, `RegisterDeffsEvent` | Combat data (monster AI/behaviour scripts) — see combat research |
| Story scripts | `scripting.*` (OpType VM), external `script-recompiler` | Binary event VM over `SECT/DRGN0.BIN/xxxx/1xxx`; "fate" text disassembly/patching |
| Save (SC) | `saves.*`, `Tag` format, `Save60` (DABA) | See §5 |
| NEWROOT.RDT | `wmap.*` | World map data (root navigation) |
| FMV | `game.fmv.Fmv`, `VideoPlayer` (ffmpeg/javacv) | Intro video: reference ships `gfx/intro.mp4` (bundled, not from disc); `Fmv.playCurrentFmv` |

## 4. Native Pipeline Design (proposal)

**Keep the three-way separation (explicit requirement):**
1. **Source** — player's `isos/` (read-only, never modified).
2. **Extraction cache** — deterministic `files/` equivalent (`data/` in native),
   versioned by extraction format version + disc hashes; rebuildable anytime.
3. **Mod overrides** — per-mod asset directories applied on top at load time
   (path-shimmed loader: `mod1 → mod2 → base` priority).

**Stages:**
- `disc` (ISO9660 reader — C++, no external dep needed for plain ISO9660 +
  Rock Ridge not required; PS1 ISOs are plain ISO9660 with Joliet off)
- `archive` (BPE, MRG, DEFF decoders — small, pure, unit-tested)
- `decode` (TMD/CTMD/TIM/DABA/ANM/CContainer/MCQ/SShd/SSSq/XA → engine IR)
- `convert` (tim→texture upload layout; tmd→mesh IR; xa→opus — Opus codec lib
  **requires approval** (or: skip Opus and decode XA at runtime; see §6 risks))
- `cache` (deterministic on-disk cache with content hashing)
- `load` (async loader with refcounted handles; explicit error results for
  missing/malformed/unsupported files — no exceptions)

**IR choices:** decoded assets become engine-owned value types
(`Mesh`, `TextureData`, `AnimationClip`, `SoundBank`) uploaded to GPU/audio on
first use; original byte layout is never exposed past the decoder.

## 5. Save Data (verified)

- Tag format (`legend.core.tags`): tagged binary tree — Map/List containers +
  String/Int/Long/Float/Bool/Raw/RegistryId/Enum leaves; custom
  serialize/deserialize into `FileData` (offset-based). This is the SC save
  format (`*.dcnf` config, save cards in `saves/`).
- Versioning: `SaveVersion` V8→V9→V10 with `SaveMigrator`; `V10Serializer`
  (current), plus `LegacySerializer`/`RetailSerializer` for original-format saves.
- `MemcardSavedGame`: imports **original PS1 memory-card saves** (real `.mcr`/
  DABA save cards, see `convert_memcard.sh` in repo root).
- Per-save `ConfigCollection` (settings are stored in the save!) + global
  `config.dcnf` (`ConfigStorageLocation.GLOBAL`); presets (`ConfigPresetManager`).
- `WriteSaveDataEvent`/`ReadSaveDataEvent` — mods attach arbitrary `ListTag modData`
  to saves; `DabasManager` keeps a DABA `Save60` (original save card structure)
  embedded in SC saves so mods can read/write original-layout data.
- Crash-recovery saves from `lastSavableEngineState` (`Main.java`).
- **Native:** port the Tag serializer exactly (binary-compatible with V10 saves is
  the safe default; document any deviations). Save compatibility is a first-class
  feature (users keep saves across the port).

## 6. Risks & Open Questions

1. **Opus at runtime**: the reference transcodes XA→Opus at unpack (LWJGL opus).
   Native options: (a) libopus (BSD) — **requires approval**; (b) keep XA and
   decode at runtime (we already port XaAdpcm; streaming from file at 48kHz needs
   the same resampling — doable, more runtime CPU); (c) transcode to a neutral
   PCM/WAV cache at unpack (simplest, larger files, zero new deps) and stream
   through SDL3 audio. **Recommendation: (c) for milestone 6, revisit (a) if
   file size matters** — decision to confirm with user.
2. Hardcoded SMAP overlay offsets: verified but brittle; keep the same offsets
   (fidelity) and add size validation with clear errors.
3. BPE decompressor performance: the Java implementation is byte-at-a-time;
   native should be fast (it runs on the main EXE, ~tens of MB).
4. `patches/` tooling (fate compiler, CSV-driven patches) depends on the external
   `script-recompiler` repo (not in this repo) — needs separate investigation or
   a scoped reimplementation for modding parity (Milestone 10+).
5. FMV: `intro.mp4` is a bundled asset (human-made, not LoD copyright — verify
   licensing with user before reusing the file itself). Native video decoding
   approach TBD (SDL3 has no video decode; options: ffmpeg (LGPL, needs
   approval), or ship the intro as a texture-sequence, or drop to a static
   fallback in v1).
6. Which region discs drive behavior (US-only today; EU/JP/Asia IDs are
   recognized but content differences unverified) — document as a known gap.