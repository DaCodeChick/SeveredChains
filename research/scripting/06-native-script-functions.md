# M3-06: Native Script Function Catalog

Scope: the exhaustive catalog of the engine's native script functions — every
slot in the base table and the two engine-state override tables (Battle,
Submap), joined to the `patches/meta` descriptions/params CSVs, with ROM
addresses from the `@Method` annotations.

Validation categories, per `AGENTS.md`: **source-verified** (reference code),
**format-corroborated**, **asset-validated** (none for M3).

## 1. Method and verification

The catalog was built mechanically (this document was generated from the
pinned sources, then reviewed):

1. extract every `functions[N] = …` assignment from the base static block
   (`Scus94491BpeSegment_8004.java`) and the state override methods
   (`Battle.getScriptFunctions()`, `SMap.getScriptFunctions()`);
2. resolve `this::method` spellings to `Class::method`;
3. join to `patches/meta/descriptions.csv` by index (row N = index N) and to
   `params.csv` by name;
4. recover the ROM address from the nearest `@Method(0x…L)` annotation above
   each method declaration (within 8 lines, to tolerate interleaved
   `@ScriptDescription`/`@ScriptParam` annotations).

**Verification results (source-verified):**

- **592 implemented slots** total: 86 base + 362 Battle + 144 Submap.
- **592/592 slots match `descriptions.csv` exactly** (name at the same index)
  — zero mismatches, zero missing rows. The index↔name alignment of doc 04 §3
  is fully verified for every implemented slot, not just spot checks.
- **559 slots carry a `@Method` ROM address**; **33 do not** (marked `—`
  below): base indices 890–892, 900–916, 960 (SItem good/item and render
  helpers) and Battle indices 1000–1005, 1010–1013, 1020–1021 (stat-mod and
  mod-support helpers) — these are reference-side additions or decompiler
  routines without an assigned ROM label; treat their "originality" as
  Unresolved (the descriptions read as original-game features, e.g. item
  pickup — Inferred).
- **432 indices (1024 − 592) have descriptions but no implementation in any
  table** — the reference implements a 592-slot subset of the original
  1024-slot function space. None of the 432 unimplemented indices have rows
  in `params.csv` (census: 0), so the local metadata itself marks them as
  param-less. A script calling such an index in the reference stalls
  (null function → `PAUSE_AND_REWIND`, doc 05 §6). Ranges:
  20–31, 51–95, 179–181, 183–191, 208–223, 272–287, 325–351, 373–415,
  417–418, 445–479, 510–511, 527–543, 603–604, 609, 617, 633, 652–671,
  719–735, 792–799, 808–831, 842, 855–863, 867–889, 893–895, 898–899,
  917–939, 941–959, 961–999, 1006–1009, 1014–1019, 1022–1023.

Dispatch order and stall behavior: doc 05 §6; the union-view semantics of the
metadata: doc 04 §3.

Column legend: **index** = CALL header / `call N` / descriptions.csv row;
**ROM** = `@Method` address (Ghidra label; `—` = none); **function** =
`Class::method` (identical in code and metadata); **params** = from
`params.csv` as `direction type name`, with the branch attribute in brackets
where not `none`.

## 2. Base table (86 slots — `Scus94491BpeSegment_8004`)

| index | ROM | function | params |
|---|---|---|---|
| 0 | `0x80017354L` | Scus94491BpeSegment::scriptSetIndicatorsDisabled | (in bool disabled) |
| 1 | `0x80017374L` | Scus94491BpeSegment::scriptReadIndicatorsDisabled | (out bool disabled) |
| 2 | `0x80017390L` | Scus94491BpeSegment::scriptSetGlobalFlag1 | (in flag flag, in bool value) |
| 3 | `0x800173fcL` | Scus94491BpeSegment::scriptReadGlobalFlag1 | (in flag flag, out bool value) |
| 4 | `0x80017440L` | Scus94491BpeSegment::scriptSetGlobalFlag2 | (in flag flag, in bool value) |
| 5 | `0x800174d8L` | Scus94491BpeSegment::scriptReadGlobalFlag2 | (in flag flag, out bool value) |
| 6 | `0x8001751cL` | FullScreenEffects::scriptStartFadeEffect | (in int type, in int frames) |
| 7 | `0x80017564L` | DrgnFiles::scriptWaitForFilesToLoad | — |
| 8 | `0x80017584L` | Rumble::scriptStartRumbleMode | (in int joypadIndex, in int mode (0: stop, 1: weak, 2: medium, 3: strong) |
| 9 | `0x800175b4L` | Scus94491BpeSegment::scriptSetFlag | (both int_array flags, in flag flag, in bool value) |
| 10 | `0x80017648L` | Scus94491BpeSegment::scriptReadFlag | (in int_array flags, in flag flag, out bool value) |
| 11 | `0x80017688L` | Rumble::scriptStartRumble | (in int joypadIndex, in int intensity, in int frames) |
| 16 | `0x800176c0L` | Rumble::scriptSetRumbleDampener | (in int intensity) |
| 17 | `0x800176ecL` | Rumble::scriptResetRumbleDampener | — |
| 192 | `0x80029b68L` | Text::scriptGetFreeTextboxIndex | (out int textboxIndex) |
| 193 | `0x80029bd4L` | Text::scriptInitTextbox | (in int textboxIndex, in int p1, in int x, in int y, in int width, in int height) |
| 194 | `0x80025158L` | Text::scriptSetTextboxContents | (in int index, in int type, in string text) |
| 195 | `0x80029c98L` | Text::scriptIsTextboxInitialized | (in int index, out bool initialized) |
| 196 | `0x80029cf4L` | Text::scriptGetTextboxState | (in int index, out int state) |
| 197 | `0x80029d34L` | Text::scriptGetTextboxTextState | (in int index, out int state) |
| 199 | `0x80029e8cL` | Text::scriptSetTextboxVariable | (in int index, in int value) |
| 200 | `0x800254bcL` | Text::scriptAddTextbox | (in int index, in int packedData, in int x, in int y, in int width, in int height, in string text) |
| 201 | `0x80029d6cL` | Text::scriptDeallocateTextbox | (in int index) |
| 202 | `0x80029e04L` | Text::scriptDeallocateAllTextboxes | — |
| 203 | `0x80029eccL` | Text::FUN_80029ecc | (in int index) |
| 204 | `0x80028ff8L` | Text::FUN_80028ff8 | (out int ret) |
| 205 | `0x80029f48L` | Text::scriptGetTextboxSelectionIndex | (in int index, out int selectionIndex) |
| 206 | `0x80029f80L` | Text::scriptGetTextboxElement | (in int index, out int value) |
| 207 | `0x80025718L` | Text::scriptAddSelectionTextbox | (in int index, in int minSelectionIndex, in int maxSelectionIndex) |
| 224 | `0x8001e640L` | Audio::scriptLoadMenuSounds | — |
| 225 | `0x8001e918L` | Audio::FUN_8001e918 | — |
| 227 | `0x8001eb30L` | Audio::FUN_8001eb30 | (in int unused) |
| 230 | `0x8001f450L` | Audio::scriptLoadMusicPackage | (in int musicIndex, in bool dontStartSequence) |
| 231 | `0x8001fe28L` | Audio::FUN_8001fe28 | (in int musicIndex, in int p1) |
| 232 | `0x8001ffdcL` | Audio::scriptUnloadSoundFile | (in int soundType) |
| 233 | `0x8002013cL` | Audio::scriptUnuseCharSoundFile | (in int bentIndex) |
| 234 | `0x80020230L` | Audio::scriptStopEncounterSoundEffects | (in int index) |
| 235 | `0x800202a4L` | Audio::scriptFreeEncounterSoundEffects | — |
| 236 | `0x8001ab34L` | Audio::scriptPlaySound | (in int soundFileIndex, in int soundIndex, in int unused1, in int unused2, in int initialDelay, in int repeatDelay) |
| 237 | `0x8001ab98L` | Audio::scriptStopSound | (in int soundFileIndex, in int soundIndex, in int mode) |
| 240 | `0x8001ad5cL` | Audio::scriptStopSoundsAndSequences | — |
| 241 | `0x8001adc8L` | Audio::scriptStartCurrentMusicSequence | — |
| 242 | `0x8001ae18L` | Audio::scriptToggleMusicSequencePause | — |
| 243 | `0x8001ae68L` | Audio::scriptToggleMusicSequencePause2 | — |
| 244 | `0x8001aec8L` | Audio::scriptStopCurrentMusicSequence | — |
| 248 | `0x8001b094L` | Audio::FUN_8001b094 | (in int p0) |
| 249 | `0x8001b134L` | Audio::FUN_8001b134 | (in int p0, in int p1) |
| 250 | `0x8001b13cL` | Audio::FUN_8001b13c | — |
| 251 | `0x8001b144L` | Audio::FUN_8001b144 | — |
| 252 | `0x8001b14cL` | Audio::scriptSetMainVolume | (in int left, in int right) |
| 253 | `0x8001b17cL` | Audio::scriptSetSequenceVolume | (in int volume) |
| 254 | `0x8001b208L` | Audio::scriptSetAllSoundSequenceVolumes | (in int volume) |
| 255 | `0x8001b27cL` | Audio::scriptSssqFadeIn | (in int fadeTime, in int volume) |
| 704 | `0x8001b2acL` | Audio::scriptStartSequenceAndChangeVolumeOverTime | (in int fadeTime, in int volume) |
| 705 | `0x8001b310L` | Audio::scriptSssqFadeOut | (in int fadeTime) |
| 706 | `0x8001b33cL` | Audio::scriptChangeSequenceVolumeOverTime | (in int fadeTime, in int volume) |
| 707 | `0x8001b3a0L` | Audio::scriptGetSequenceFlags | (out int flags) |
| 708 | `0x8001b0f0L` | Audio::scriptGetSssqTempoScale | (out int tempoScale) |
| 709 | `0x8001b118L` | Audio::scriptSetSssqTempoScale | (in int tempoScale) |
| 710 | `0x8001ffc0L` | Audio::scriptGetLoadedSoundFiles | (out int flags) |
| 711 | `0x8001b1ecL` | Audio::scriptGetSequenceVolume | (out int volume) |
| 714 | `0x80020060L` | Audio::scriptStopAndUnloadSequences | — |
| 864 | `0x800244c4L` | Scus94491BpeSegment::scriptGiveChestContents | (in int itemId, out int itemGiven) |
| 865 | `0x80024590L` | Scus94491BpeSegment::scriptTakeItem | (in int itemId, out int itemTaken) |
| 866 | `0x80024480L` | Scus94491BpeSegment::scriptGiveGold | (in int amount, out int out) |
| 890 | `—` | Scus94491BpeSegment::scriptReadRegistryEntryVar | (in reg registryId, in reg entryId, in int entryVar, out any value) |
| 891 | `—` | SItem::scriptInputActionPressed | (in reg inputActionId, out bool pressed) |
| 892 | `—` | SItem::scriptInputActionHeld | (in reg inputActionId, out bool held) |
| 900 | `—` | SItem::scriptGetMaxItemCount | (out int size) |
| 901 | `—` | SItem::scriptGetMaxEquipmentCount | (out int size) |
| 902 | `—` | SItem::scriptIsItemSlotUsed | (in int slot, out bool used) |
| 903 | `—` | SItem::scriptIsEquipmentSlotUsed | (in int slot, out bool used) |
| 904 | `—` | SItem::scriptGetItemSlot | (in int slot, out reg id) |
| 905 | `—` | SItem::scriptGetEquipmentSlot | (in int slot, out reg id) |
| 906 | `—` | SItem::scriptSetItemSlot | (in int slot, in reg id) |
| 907 | `—` | SItem::scriptSetEquipmentSlot | (in int slot, in reg id) |
| 908 | `—` | SItem::scriptGiveItem | (in reg id, out bool given) |
| 909 | `—` | SItem::scriptGiveEquipment | (in reg id, out bool given) |
| 910 | `—` | SItem::scriptTakeItem | (in reg id, out int taken) |
| 911 | `—` | SItem::scriptTakeEquipment | (in reg id, out int taken) |
| 912 | `—` | SItem::scriptGenerateAttackItem | (out reg id) |
| 913 | `—` | SItem::scriptGenerateRecoveryItem | (out reg id) |
| 914 | `—` | SItem::scriptHasGood | (in reg id, out bool has) |
| 915 | `—` | SItem::scriptGiveGood | (in reg id) |
| 916 | `—` | SItem::scriptTakeGood | (in reg id) |
| 960 | `—` | RenderEngine::scriptGetRenderAspectMultiplier | (out int multiplier) |

## 3. Battle state table (362 slots — `Battle.getScriptFunctions`)

Overrides are only consulted when the base slot is null (base-first merge,
doc 05 §6); all 362 slots are base-null.

| index | ROM | function | params |
|---|---|---|---|
| 32 | `0x800dabccL` | Battle::scriptResetCameraMovement | — |
| 33 | `0x800dac20L` | Battle::scriptCameraSetViewpoint | (in int mode, in int x, in int y, in int z, in int scriptIndex) |
| 34 | `0x800db034L` | Battle::scriptCameraSetRefpoint | (in int mode, in int x, in int y, in int z, in int scriptIndex) |
| 35 | `0x800db460L` | Battle::scriptCameraMoveViewpoint | (in int mode, in int x, in int y, in int z, in int ?, in int ticks, in int stepType, in int scriptIndex) |
| 36 | `0x800db574L` | Battle::scriptCameraMoveRefpoint | (in int mode, in int x, in int y, in int z, in int ?, in int ticks, in int stepType, in int scriptIndex) |
| 37 | `0x800db688L` | Battle::FUN_800db688 | (in int mode, in int x, in int y, in int z, in int initialStepZ, 8-bit fixed-point, in int finalStepZ, 8-bit fixed-point, in int stepType, in int scriptIndex) |
| 38 | `0x800db79cL` | Battle::FUN_800db79c | (in int mode, in int x, in int y, in int z, in int initialStepZ, 8-bit fixed-point, in int finalStepZ, 8-bit fixed-point, in int stepType, in int scriptIndex) |
| 39 | `0x800db8b0L` | Battle::scriptCameraAccelerateViewpoint | (in int mode, in int x, in int y, in int z, in int ticks, in int stepSmoothingMode, in int stepZ, in int stepType, in int scriptIndex) |
| 40 | `0x800db9e0L` | Battle::scriptCameraAccelerateRefpoint | (in int mode, in int x, in int y, in int z, in int ticks, in int stepSmoothingMode, in int stepZ, in int stepType, in int scriptIndex) |
| 41 | `0x800dbb10L` | Battle::scriptIsCameraMoving | (in int type, out bool isMoving) |
| 42 | `0x800dc2d8L` | Battle::scriptCalculateCameraValue | (in int type, in int mode, in int component, in int scriptIndex, out int out) |
| 43 | `0x800dbb9cL` | Battle::FUN_800dbb9c | (in int ?, in int ?) |
| 44 | `0x800dcbecL` | Battle::scriptWobbleCamera | (in int frames, in int framesUntilWobble, in int x, in int y) |
| 45 | `0x800dcb84L` | Battle::scriptStopCameraMovement | (in int type) |
| 46 | `0x800dbc2cL` | Battle::scriptSetViewportTwist | (in int twist) |
| 47 | `0x800dbc80L` | Battle::scriptStopCameraZAcceleration | (in int type) |
| 48 | `0x800dbcc8L` | Battle::scriptSetCameraProjectionPlaneDistance | (in int distance) |
| 49 | `0x800dbcfcL` | Battle::scriptGetProjectionPlaneDistance | (out int distance) |
| 50 | `0x800d8decL` | Battle::scriptMoveCameraProjectionPlane | (in int mode, in int newDistance, in int frames, in int stepZ1) |
| 128 | `0x800cb3fcL` | Battle::scriptSetBentPos | (in int bentIndex, in int x, in int y, in int z) |
| 129 | `0x800cb468L` | Battle::scriptGetBentPos | (in int bentIndex, out int x, out int y, out int z) |
| 130 | `0x800cb4c8L` | Battle::scriptSetBentRotation | (in int bentIndex, in int x, in int y, in int z) |
| 131 | `0x800cc7d8L` | Battle::FUN_800cc7d8 | (in int bentIndex) |
| 132 | `0x800cb534L` | Battle::scriptSetBentRotationY | (in int bentIndex, in int y) |
| 133 | `0x800cc948L` | Battle::loadAllCharAttackAnimations | — |
| 134 | `0x800cb578L` | Battle::scriptGetBentRotation | (in int bentIndex, out int x, out int y, out int z) |
| 135 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 136 | `0x800cb5d8L` | Battle::scriptGetMonsterStatusResistFlags | (in int bentIndex, out int flags) |
| 137 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 138 | `0x800cb618L` | Battle::scriptSetBentHidden | (in int bentIndex, in bool set) |
| 139 | `0x800cb674L` | Battle::scriptSetInterpolationEnabled | (in int bentIndex, in bool set) |
| 140 | `0x800cb6bcL` | Battle::FUN_800cb6bc | (in int bentIndex, in bool animIndex) |
| 141 | `0x800cb764L` | Battle::FUN_800cb764 | (in int unused) |
| 142 | `0x800cb76cL` | Battle::FUN_800cb76c | (in int bentIndex) |
| 143 | `0x800cb9b0L` | Battle::scriptGetLoadingBentAnimationIndex | (in int bentIndex, out int animationIndex) |
| 144 | `0x800cb9f0L` | Battle::scriptPauseAnimation | (in int bentIndex) |
| 145 | `0x800cba28L` | Battle::scriptResumeAnimation | (in int bentIndex) |
| 146 | `0x800cba60L` | Battle::scriptSetBentAnimationLoopState | (in int bentIndex, in bool loop) |
| 147 | `0x800cbabcL` | Battle::scriptAnimationHasFinished | (in int bentIndex, out bool finished) |
| 148 | `0x800cbb00L` | Battle::FUN_800cbb00 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int y, in int z) |
| 149 | `0x800cbc14L` | Battle::FUN_800cbc14 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int y, in int z) |
| 150 | `0x800cbde0L` | Battle::FUN_800cbde0 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int y, in int z) |
| 151 | `0x800cbef8L` | Battle::FUN_800cbef8 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int y, in int z) |
| 152 | `0x800cc0c8L` | Battle::FUN_800cc0c8 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int z) |
| 153 | `0x800cc1ccL` | Battle::FUN_800cc1cc | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int z) |
| 154 | `0x800cc364L` | Battle::FUN_800cc364 | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int z) |
| 155 | `0x800cc46cL` | Battle::FUN_800cc46c | (in int childBentIndex, in int parentBentIndex, in int ticks, in int x, in int z) |
| 156 | `0x800cc608L` | Battle::scriptBentLookAtBent | (in int bentIndexToTurn, in int bentIndexTarget) |
| 157 | `0x800cc698L` | Battle::FUN_800cc698 | (in int bentIndex0, in int bentIndex1, in int ticks) |
| 158 | `0x800cc784L` | Battle::FUN_800cc784 | (in int bentIndex) |
| 159 | `0x800cc8f4L` | Battle::scriptLoadAttackAnimations | (in int bentIndex) |
| 160 | `0x800cca34L` | Battle::scriptSetUpAndHandleCombatMenu | (out int selectedAction) |
| 161 | `0x800cca34L` | Battle::scriptOverrideBattleMenuActions | (in int actionCount, in reg_array actions) |
| 162 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 163 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 164 | `0x800ccb3cL` | Battle::scriptRenderDamage | (in int bentIndex, in int damage) |
| 165 | `0x800ccb70L` | Battle::scriptAddFloatingNumberForBent | (in int bentIndex, in int value) |
| 166 | `0x800ccba4L` | Battle::FUN_800ccba4 | (in int bentIndex, in bool isDragoon) |
| 167 | `0x800cccf4L` | Battle::scriptGetCharOrMonsterId | (in int bentIndex, out int id) |
| 168 | `0x800ccd34L` | Battle::scriptSetBentStat | (in int bentIndex, in int value, in legend.game.combat.bent.BattleEntityStat statIndex) |
| 169 | `0x800cce04L` | Battle::scriptGetBentStat | (in int bentIndex, in legend.game.combat.bent.BattleEntityStat statIndex, out int value) |
| 170 | `0x800ccf0cL` | Battle::scriptSetPostBattleAction | (in reg action, in int_array params) |
| 171 | `0x800ccec8L` | Battle::scriptSetBattleHudVisibility | (in bool visible) |
| 172 | `0x800ccef8L` | Battle::scriptSetPostBattleActionBossKill | — |
| 173 | `0x800ccf2cL` | Battle::scriptSetBentDeadAndDropLoot | (in int bentIndex, in bool dead) |
| 174 | `0x800cd0ecL` | Battle::scriptGetHitProperty | (in int bentIndex, in int hitNum, in int hitPropertyIndex, out int value) |
| 175 | `0x800cd078L` | Battle::scriptSetBentDead | (in int bentIndex, in bool dead) |
| 176 | `0x800cd160L` | Battle::scriptLevelUpAddition | (in int bentIndex, in bool doIt) |
| 177 | `0x800cce70L` | Battle::scriptGetBentStat2 | (in int bentIndex, in legend.game.combat.bent.BattleEntityStat statIndex, out int value) |
| 178 | `0x800ccda0L` | Battle::scriptSetBentRawStat | (in int bentIndex, in int value, in legend.game.combat.bent.BattleEntityStat statIndex) |
| 182 | `0x800cb674L` | Battle::scriptSetAnimationScale | (in int bentIndex, in int scale) |
| 226 | `0x8001e920L` | Battle::scriptLoadCharAttackSounds | (in int bentIndex, in int type) |
| 228 | `0x8001ecccL` | Battle::scriptLoadBattleCutsceneSounds | (in int cutsceneIndex) |
| 229 | `0x8001f070L` | Battle::scriptLoadMonsterAttackSounds | (in int monsterIndex) |
| 238 | `0x8001abd0L` | Battle::scriptPlayBentSound | (in int type, in int bentIndex, in int soundIndex, in int a3, in int a4, in int initialDelay, in int repeatDelay) |
| 239 | `0x8001ac48L` | Battle::scriptStopBentSound | (in int unused, in int bentIndex, in int soundIndex, in int mode) |
| 245 | `0x8001af34L` | Battle::scriptStartEncounterSounds | (in int p0) |
| 246 | `0x8001afa4L` | Battle::scriptStopEncounterSounds | (in int p0) |
| 247 | `0x8001b014L` | Battle::scriptStopEncounterSounds2 | (in int p0) |
| 320 | `0x800cc9d8L` | Battle::scriptEnableBentTextureAnimation | (in int bentIndex, in int textureIndex, in bool enabled) |
| 321 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 322 | `0x800cb84cL` | Battle::scriptSetLoadingBentAnimationIndex | (in int bentIndex, in bool animIndex) |
| 323 | `0x800cb95cL` | Battle::FUN_800cb95c | (in int bentIndex) |
| 324 | `0x8001734cL` | Scus94491BpeSegment::scriptRewindAndPause2 | — |
| 352 | `0x800cd3b4L` | Battle::scriptSetModelPartVisibility | (in int bentIndex, in int partIndex, in bool visible) |
| 353 | `0x800ee210L` | Battle::scriptCopyVram | (in int destX, in int destY, in int width, in int height, in int sourceX, in int sourceY) |
| 354 | `0x800cd468L` | Battle::scriptLoadGlobalAsset | (in int drgnIndex, in int fileIndex, out int index) |
| 355 | `0x800cd4b0L` | Battle::scriptWaitGlobalAssetAllocation | (in int index) |
| 356 | `0x800cd4f0L` | Battle::scriptDeallocateGlobalAsset | (in int index) |
| 357 | `0x800cd52cL` | Battle::scriptAddCombatant | (in int charIndexAndFlags, out int combatantIndex) |
| 358 | `0x800cd570L` | Battle::scriptDeallocateAndRemoveCombatant | (in int combatantIndex) |
| 359 | `0x800cda78L` | Battle::FUN_800cda78 | (in int bentIndex) |
| 360 | `0x800cd5b4L` | Battle::scriptAllocateBent | (in int entrypointIndex, in int combatantIndex, out int scriptIndex) |
| 361 | `0x800cd740L` | Battle::scriptLoadModelToCombatantFromGlobalAssets | (in int combatantIndex, in int index) |
| 362 | `0x800cd7a8L` | Battle::FUN_800cd7a8 | (in int p0, in int p1) |
| 363 | `0x800cd810L` | Battle::scriptLoadAnimToCombatantFromGlobalAssetsOrDeallocate | (in int combatantIndex, in int animIndex, in int index) |
| 364 | `0x800cd8a4L` | Battle::scriptLoadTextureToCombatantFromGlobalAssets | (in int combatantIndex, in int index) |
| 365 | `0x800cd9fcL` | Battle::scriptGetBentNobj | (in int bentIndex, out int count) |
| 366 | `0x800cda3cL` | Battle::scriptDeallocateCombatant | (in int bentIndex) |
| 367 | `0x800cdb18L` | Battle::scriptStopRenderingStage | — |
| 368 | `0x800cdb44L` | Battle::scriptLoadStage | (in int stageIndex) |
| 369 | `0x800cd910L` | Battle::FUN_800cd910 | (in int drgnIndex, in int fileIndex, out int queueIndex) |
| 370 | `0x800cd958L` | Battle::scriptGetCombatantIndex | (in int bentIndex, out int combatantIndex) |
| 371 | `0x800cd998L` | Battle::scriptGetBentSlot | (in int bentIndex, out int charOrBentSlot, in bool mode) |
| 372 | `0x800cdb74L` | Battle::scriptDisableCombat | — |
| 416 | `0x800e6fb4L` | Battle::scriptLoadDragoonDeffSync | (in int flagsAndIndex, in int bentIndex, in int p2, in int scriptEntrypoint) |
| 419 | `0x800e473cL` | Battle::scriptResetLights | — |
| 420 | `0x800e4788L` | Battle::scriptSetLightDirection | (in int lightIndex, in int x, in int y, in int z) |
| 421 | `0x800e47c8L` | Battle::scriptGetLightDirection | (in int lightIndex, out int x, out int y, out int z) |
| 422 | `0x800e48a8L` | Battle::FUN_800e48a8 | (in int lightIndex, in int x, in int y, in int z) |
| 423 | `0x800e48e8L` | Battle::FUN_800e48e8 | (in int lightIndex, out int x, out int y, out int z) |
| 424 | `0x800e4964L` | Battle::FUN_800e4964 | (in int lightIndex, in int index, in int x, in int y, in int z) |
| 425 | `0x800e4abcL` | Battle::FUN_800e4abc | (in int lightIndex, both int indexAndX, out int y, out int z) |
| 426 | `0x800e4c10L` | Battle::scriptSetBattleLightColour | (in int lightIndex, in int r, in int g, in int b) |
| 427 | `0x800e4c90L` | Battle::scriptGetBattleLightColour | (in int lightIndex, out int r, out int g, out int b) |
| 428 | `0x800e4d2cL` | Battle::scriptSetBattleBackgroundLightColour | (in int r, in int g, in int b) |
| 429 | `0x800e4db4L` | Battle::scriptGetBattleBackgroundLightColour | (out int r, out int g, out int b) |
| 430 | `0x800e4dfcL` | Battle::FUN_800e4dfc | (in int lightIndex) |
| 431 | `0x800e4e2cL` | Battle::FUN_800e4e2c | (in int lightIndex) |
| 432 | `0x800e4e64L` | Battle::FUN_800e4e64 | (in int lightIndex, out int out) |
| 433 | `0x800e4ea0L` | Battle::FUN_800e4ea0 | (in int lightIndex, in int x (PSX degrees), in int y (PSX degrees), in int z (PSX degrees), in int ticks) |
| 434 | `0x800e4fa0L` | Battle::FUN_800e4fa0 | (in int lightIndex, in int x rotation (PSX degrees), in int y rotation (PSX degrees), in int z rotation (PSX degrees), in int ticks) |
| 435 | `0x800e50e8L` | Battle::FUN_800e50e8 | (in int lightIndex, in int lightOrBentIndex, in int x, in int y, in int z, in int ticks) |
| 436 | `0x800e52f8L` | Battle::FUN_800e52f8 | (in int lightIndex, in int x0, in int y0, in int z0, in int x1, in int y1, in int z1) |
| 437 | `0x800e540cL` | Battle::FUN_800e540c | (in int lightIndex, in int bentIndex) |
| 438 | `0x800e54f8L` | Battle::FUN_800e54f8 | (in int lightIndex) |
| 439 | `0x800e5528L` | Battle::FUN_800e5528 | (in int lightIndex) |
| 440 | `0x800e5560L` | Battle::FUN_800e5560 | (in int lightIndex, out int value) |
| 441 | `0x800e559cL` | Battle::FUN_800e559c | (in int lightIndex, in int x, in int y, in int z, in int The number of ticks) |
| 442 | `0x800e569cL` | Battle::FUN_800e569c | (in int lightIndex, in int x0, in int y0, in int z0, in int x1, in int y1, in int z1) |
| 443 | `0x800e596cL` | Battle::scriptApplyStageAmbiance | — |
| 444 | `0x800e59d8L` | Battle::FUN_800e59d8 | (in int mode, in int_array data) |
| 480 | `0x800f95d0L` | Battle::scriptCheckPhysicalHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 481 | `0x800f2500L` | Battle::scriptPhysicalAttack | (in int attackerIndex, in int defenderIndex, out int damage, out int specialEffects) |
| 482 | `0x800f96d4L` | Battle::scriptGetBentPos2 | (in int attackerIndex, out int x, out int y, out int z) |
| 483 | `0x800f9730L` | Battle::scriptAddFloatingNumber | (in int number, in int x, in int y) |
| 484 | `0x800f95d0L` | Battle::scriptCheckPhysicalHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 485 | `0x800f95d0L` | Battle::scriptCheckPhysicalHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 486 | `0x800f95d0L` | Battle::scriptCheckPhysicalHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 487 | `0x800f43dcL` | Battle::scriptGiveSp | (in int bentIndex, in int amount, out int total) |
| 488 | `0x800f4518L` | Battle::scriptConsumeSp | (in int bentIndex, in int unused, in int amount) |
| 489 | `0x800f97d8L` | Battle::scriptInitSpellAndItemMenu | (in int bentIndex, in int menuType) |
| 490 | `0x800f4600L` | Battle::scriptGetItemOrSpellTargetingInfo | (out int selectionState, out int targetBentIndex, out int itemOrSpellId) |
| 491 | `0x800f480cL` | Battle::scriptGetItemOrSpellAttackTarget | (in int targetMode, out int targetBentIndex) |
| 492 | `0x800f2694L` | Battle::scriptDragoonMagicStatusItemAttack | (in int attackerIndex, in int defenderIndex, in int spellId, out int damage, out int specialEffects) |
| 493 | `0x800f96a8L` | Battle::scriptSetTempSpellStats | (in int bentIndex) |
| 494 | `0x800f984cL` | Battle::scriptRenderRecover | (in int bentIndex, in int amount, in int colourIndex) |
| 495 | `0x800f2838L` | Battle::scriptItemMagicAttack | (in int attackerIndex, in int defenderIndex, in int unused, out int damage, out int specialEffects) |
| 496 | `0x800f9884L` | Battle::scriptSetTempItemMagicStats | (in int bentIndex) |
| 497 | `0x800f98b0L` | Battle::scriptTakeItem | (in reg itemId, out reg itemTaken) |
| 498 | `0x800f99ecL` | Battle::scriptGiveItem | (in reg itemId, out reg itemGiven) |
| 499 | `0x800f9a50L` | Battle::scriptSetHudTargetBobj | (in int targetType, in int targetBentIndex) |
| 500 | `0x800f9b2cL` | Battle::scriptIsFloatingNumberOnScreen | (out bool onScreen) |
| 501 | `0x800f9b78L` | Battle::scriptSetDragoonSpaceElementIndex | (in int charId) |
| 502 | `0x800f9b94L` | Battle::FUN_800f9b94 | (in int p0, in int p1) |
| 503 | `0x800f9bd4L` | Battle::FUN_800f9bd4 | (in int p0) |
| 504 | `0x800f9c00L` | Battle::FUN_800f9c00 | (in int p0) |
| 505 | `0x800f9c2cL` | Battle::scriptRenderBattleHudBackground | (in int x, in int y, in int width, in int height, in int colourIndex) |
| 506 | `0x800f9cacL` | Battle::scriptSetDisabledMenuIcons | (in int iconIndicesBitset) |
| 507 | `0x800f9618L` | Battle::scriptCheckSpellOrStatusHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 508 | `0x800f9660L` | Battle::scriptCheckItemHit | (in int attackerIndex, in int defenderIndex, out bool hit) |
| 509 | `0x800f9d7cL` | Battle::scriptFinishBentTurn | (in int bentIndex) |
| 512 | `0x800ee2acL` | Battle::scriptSetBentZOffset | (in int bentIndex, in int z) |
| 513 | `0x800ee2e4L` | Battle::scriptSetBentScaleUniform | (in int bentIndex, in int scale) |
| 514 | `0x800ee324L` | Battle::scriptSetBentScale | (in int bentIndex, in int x, in int y, in int z) |
| 515 | `0x800ee384L` | Battle::scriptAttachShadowToBottomOfBentModel | (in int bentIndex) |
| 516 | `0x800ee468L` | Battle::scriptDisableBentShadow | (in int bentIndex) |
| 517 | `0x800ee49cL` | Battle::scriptSetBentShadowSize | (in int bentIndex, in int x, in int z) |
| 518 | `0x800ee4e8L` | Battle::scriptSetBentShadowOffset | (in int bentIndex, in int x, in int y, in int z) |
| 519 | `0x800ee548L` | Battle::scriptApplyScreenDarkening | (in int intensity) |
| 520 | `0x800ee384L` | Battle::scriptAttachShadowToBottomOfBentModel | (in int bentIndex) |
| 521 | `0x800ee574L` | Battle::scriptGetStageNobj | (out int count) |
| 522 | `0x800ee594L` | Battle::scriptShowStageModelPart | (in int modelPartIndex) |
| 523 | `0x800ee5c0L` | Battle::scriptHideStageModelPart | (in int modelPartIndex) |
| 524 | `0x800ee3c0L` | Battle::scriptAttachShadowToBentModelPart | (in int bentIndex, in int modelPartAttachmentIndex) |
| 525 | `0x800ee408L` | Battle::scriptUpdateBentShadowType | (in int bentIndex) |
| 526 | `0x800ee5f0L` | Battle::scriptSetStageZ | (in int z) |
| 544 | `0x801115ecL` | SEffe::scriptGetRelativePosition | (in int bobjIndex, in int parentIndex, out int x, out int y, out int z) |
| 545 | `0x80111ae4L` | SEffe::scriptSetRelativePosition | (in int bobjIndex1, in int bobjIndex2, in int x, in int y, in int z) |
| 546 | `0x80112704L` | SEffe::scriptGetRotationDifference | (in int bobjIndex1, in int bobjIndex2, out int x, out int y, out int z) |
| 547 | `0x80112770L` | SEffe::scriptSetRelativeRotation | (in int bobjIndex1, in int bobjIndex2, in int x, in int y, in int z) |
| 548 | `0x80113964L` | SEffe::scriptGetScaleRatio | (in int bobjIndex1, in int bobjIndex2, out int x, out int y, out int z) |
| 549 | `0x801139d0L` | SEffe::scriptSetRelativeScale | (in int bobjIndex1, in int bobjIndex2, in int x, in int y, in int z) |
| 550 | `0x8011452cL` | SEffe::scriptGetColourDifference | (in int bobjIndex1, in int bobjIndex2, out int r, out int g, out int b) |
| 551 | `0x80114598L` | SEffe::scriptSetRelativeColour | (in int bobjIndex1, in int bobjIndex2, in int r, in int g, in int b) |
| 552 | `0x80114e0cL` | SEffe::scriptGetGenericEffectValue | (in int effectIndex, in int genericIndex, out int value) |
| 553 | `0x80114e60L` | SEffe::scriptSetGenericEffectValue | (in int effectIndex, in int genericIndex, in int value) |
| 554 | `0x80111b60L` | SEffe::scriptTransformBobjPositionToScreenSpace | (in int bobjIndex, out int x, out int y, out int z) |
| 555 | `0x8011554cL` | SEffe::scriptRemoveEffectAttachment | (in int effectIndex, in int attachmentId) |
| 556 | `0x801155a0L` | SEffe::scriptHasEffectAttachment | (in int effectIndex, out bool hasAttachment, in int attachmentId) |
| 557 | `0x80111be8L` | SEffe::scriptWaitForPositionScalerToFinish | (in int effectIndex) |
| 558 | `0x80111c2cL` | SEffe::scriptAddPositionScalerAttachment | (in int effectIndex, in int parentIndex, in int velocityX, in int velocityY, in int velocityZ, in int accelerationX, in int accelerationY, in int accelerationZ) |
| 559 | `0x80112184L` | SEffe::scriptAddRelativePositionScalerTicks0 | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 560 | `0x80112274L` | SEffe::scriptAddRelativePositionScalerDistance0 | (in int effectIndex, in int parentIndex, in int distancePerFrame, in int x, in int y, in int z) |
| 561 | `0x80112364L` | SEffe::scriptAddPositionScalerMoveToParent | (in int effectIndex, in int parentIndex) |
| 562 | `0x801155f8L` | SEffe::FUN_801155f8 | — |
| 563 | `0x80112900L` | SEffe::scriptGetRelativeAngleBetweenBobjs | (in int bobjIndex1, in int bobjIndex2, out int x, out int z, out int y) |
| 564 | `0x8011299cL` | SEffe::scriptRotateBobjTowardsPoint | (in int bobjIndex, in int parentIndex, in int x, in int y, in int z) |
| 565 | `0x80115440L` | SEffe::scriptSetEffectApplyRotationAndScaleFlag | (in int effectIndex, in bool set) |
| 566 | `0x80111658L` | SEffe::scriptGetEffectTranslationRelativeToParent | (in int effectIndex, in int parentIndex, out int x, out int y, out int z) |
| 567 | `0x80112aa4L` | SEffe::scriptAddRotationScalerAttachment | (in int effectIndex, in int unused, in int velocityX, in int velocityY, in int velocityZ, in int accelerationX, in int accelerationY, in int accelerationZ) |
| 568 | `0x80112bf0L` | SEffe::scriptAddRotationScalerAttachmentTicks | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 569 | `0x80112e00L` | SEffe::scriptAddRotationScalerAttachmentDistance | (in int effectIndex, in int parentIndex, in int distancePerTick, in int angle) |
| 570 | `0x8011306cL` | SEffe::scriptAddRotationScalerAttachmentTowardsPointTicks | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 571 | `0x801132c8L` | SEffe::scriptAddRotationScalerAttachmentTowardsPointDistance | (in int effectIndex, in int parentIndex, in int distancePerTick, in int x, in int y, in int z) |
| 572 | `0x80115600L` | SEffe::FUN_80115600 | — |
| 573 | `0x800e9f68L` | Battle::scriptAllocateClonedModelEffect | (out int effectIndex, in int cloneId) |
| 574 | `0x80118984L` | SEffe::scriptLoadEffectModelAnimation | (in int effectIndex, in int deffPartIndex) |
| 575 | `0x80113c6cL` | SEffe::scriptAddScaleScalerAttachment | (in int effectIndex, in int velocityX, in int velocityY, in int velocityZ, in int accelerationX, in int accelerationY, in int accelerationZ) |
| 576 | `0x80114094L` | SEffe::scriptAddScaleScalerMultiplicativeAttachmentTicks | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 577 | `0x801143f8L` | SEffe::scriptAddScaleScalerMultiplicativeAttachmentDistance | (in int effectIndex, in int parentIndex, in int distancePerTick, in int x, in int y, in int z) |
| 578 | `0x80115608L` | SEffe::scriptSetScriptScript | (in int scriptIndex, in int deffScriptIndex, in int entrypoint) |
| 579 | `0x80114070L` | SEffe::scriptAddScaleScalerDifferenceAttachmentTicks | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 580 | `0x801147c8L` | SEffe::scriptAddColourScalerAttachment | (in int effectIndex, in int velocityR, in int velocityG, in int velocityB, in int accelerationR, in int accelerationG, in int accelerationB) |
| 581 | `0x80114920L` | SEffe::scriptAddConstantColourScalerAttachment | (in int effectIndex, in int parentIndex, in int ticks, in int destR, in int destG, in int destB) |
| 582 | `0x80114b00L` | SEffe::scriptAddConstantColourScalerDistance | (in int effectIndex, in int parentIndex, in int distancePerTick, in int velocityX, in int velocityY, in int velocityZ) |
| 583 | `0x80114eb4L` | SEffe::scriptRemoveGenericEffectAttachment | (in int effectIndex, in int genericIndex) |
| 584 | `0x80114f34L` | SEffe::FUN_80114f34 | — |
| 585 | `0x80115014L` | SEffe::scriptAddGenericAttachment | (in int effectIndex, in int genericIndex, in int speed, in int acceleration) |
| 586 | `0x80115058L` | SEffe::scriptAddGenericAttachmentTicks | (in int effectIndex, in int varIndex, in int value, in int ticks) |
| 587 | `0x80115168L` | SEffe::scriptAddGenericAttachmentSpeed | (in int effectIndex, in int varIndex, in int value, in int speed) |
| 588 | `0x801152b0L` | SEffe::scriptAddLifespanAttachment | (in int effectIndex, in int ticks) |
| 589 | `0x80115324L` | SEffe::scriptSetEffectErrorFlag | (in int effectIndex, in bool clear) |
| 590 | `0x80115388L` | SEffe::scriptSetEffectTranslucencySourceFlag | (in int effectIndex, in bool set) |
| 591 | `0x801153e4L` | SEffe::scriptSetEffectTranslucencyModeFlag | (in int effectIndex, in int mode) |
| 592 | `0x800e74acL` | Battle::scriptGetDeffBentParams | (out int bentIndex, out int target) |
| 593 | `0x80112398L` | SEffe::scriptGetPositionScalerAttachmentVelocity | (in int effectIndex, in int unused, out int x, out int y, out int z) |
| 594 | `0x800eb518L` | Battle::scriptAddOrUpdateTextureAnimationAttachment | (in int effectIndex, in int varIndex, in int step) |
| 595 | `0x8011549cL` | SEffe::scriptSetEffectLightingDisableFlag | (in int effectIndex, in bool set) |
| 596 | `0x801122ecL` | SEffe::scriptAddRelativePositionScalerDistance1 | (in int effectIndex, in int parentIndex, in int distancePerFrame, in int x, in int y, in int z) |
| 597 | `0x801121fcL` | SEffe::scriptAddRelativePositionScalerTicks1 | (in int effectIndex, in int parentIndex, in int ticks, in int x, in int y, in int z) |
| 598 | `0x80111cc4L` | SEffe::scriptAddOrUpdatePositionScalerAttachment | (in int effectIndex, in int parentIndex, in int velocityX, in int velocityY, in int velocityZ, in int accelerationX, in int accelerationY, in int accelerationZ) |
| 599 | `0x80111ed4L` | SEffe::scriptUpdateParabolicPositionScalerAttachment | (in int effectIndex, in int unused, in int accelerationX, in int accelerationY, in int accelerationZ) |
| 600 | `0x800e93e0L` | Battle::scriptAllocateEmptyEffectManagerChild | (out int effectIndex) |
| 601 | `0x800e96ccL` | Battle::allocateBillboardSpriteEffect | (out int effectIndex, in int flags) |
| 602 | `0x800e9854L` | Battle::FUN_800e9854 | (in int p0, in int effectIndex) |
| 605 | `0x80117eb0L` | SEffe::scriptAllocateLmbAnimation | (out int effectIndex, in int lmbFlags) |
| 606 | `0x801183c0L` | SEffe::allocateDeffTmd | (out int effectIndex, in int flags) |
| 607 | `0x800e99bcL` | Battle::FUN_800e99bc | (out int effectIndex, in int flags) |
| 608 | `0x801181a8L` | SEffe::scriptSetLmbDeffFlag | (in int effectIndex, in int flagIndex, in int deffFlag) |
| 610 | `0x800ea200L` | Battle::scriptLoadCmbAnimation | (in int effectIndex, in int flags) |
| 611 | `0x801156f8L` | SEffe::scriptAttachEffectToBobj | (in int effectIndex, in int parentBobjIndex, in int parentPartIndex) |
| 612 | `0x800ea13cL` | Battle::scriptHideEffectModelPart | (in int effectIndex, in flag part) |
| 613 | `0x800ea19cL` | Battle::scriptShowEffectModelPart | (in int effectIndex, in flag part) |
| 614 | `0x800eb84cL` | Battle::scriptAddRedEyeDragoonTransformationFlameArmorEffectAttachment | (in int effectIndex, in int textureIndex1, in int textureIndex2, in int attachmentStep) |
| 615 | `0x800eb188L` | Battle::scriptApplyTextureAnimationAttachment | (in int effectIndex, in int textureIndex) |
| 616 | `0x800eb01cL` | Battle::scriptRemoveTextureAnimationAttachment | (in int effectIndex, in int textureIndex) |
| 618 | `0x80115690L` | SEffe::scriptLoadSameScriptAndJump | (in int scriptIndex, in int address [fork_jump]) |
| 619 | `0x80118df4L` | SEffe::allocateShirleyTransformWipeEffect | (in int effectIndex) |
| 620 | `0x80111a58L` | SEffe::FUN_80111a58 | (in int effectIndex, in int modelPartIndex, out int x, out int y, out int z) |
| 621 | `0x800ea384L` | Battle::scriptGetEffectLoopCount | (in int effectIndex, out int loopCount) |
| 622 | `0x80119484L` | SEffe::allocateSpriteWithTrailEffect | (out int effectIndex, in int flags, in int colourAndScaleFlags, in int count, in int steps, in int brightnessAndScaleChange) |
| 623 | `0x800e73acL` | Battle::scriptLoadDeff | (in int flagsAndIndex, in int bentIndex, in int deffParam, in int scriptEntrypoint, in int type) |
| 624 | `0x800e6db4L` | Battle::scriptTickDeffLoadingStage | (in int loadingStage) |
| 625 | `0x800e7490L` | Battle::scriptGetDeffLoadingStage | (out int loadingStage) |
| 626 | `0x8011574cL` | SEffe::scriptGetEffectZ | (in int effectIndex, out int z) |
| 627 | `0x8011578cL` | SEffe::scriptSetEffectZ | (in int effectIndex, in int z) |
| 628 | `0x801184e4L` | SEffe::allocateDeffTmdRenderer | (in int effectIndex, in int flags, in int objIndex) |
| 629 | `0x801157d0L` | SEffe::scriptAttackEffectToBobjRelative | (in int effectIndex, in int parentBobjIndex, in int parentPartIndex) |
| 630 | `0x801127e0L` | SEffe::scriptGetEffectRotation | (in int effectIndex, in int unused, out int x, out int y, out int z) |
| 631 | `0x801181f0L` | SEffe::scriptSetLmbManagerTransformMetrics | (in int effectIndex, in int managerTransformFlags, in int subStepCountMultiplier, in int subStepCount, in int finalManagerTransformMultiplier) |
| 632 | `0x801114b8L` | SEffe::scriptAllocateBuggedEffect | (in int p0, in int p1, in int p2, in int p3) |
| 634 | `0x80115ab0L` | SEffe::scriptWaitForXaToLoad | (in int expectedStage) |
| 635 | `0x80115a94L` | SEffe::scriptGetXaLoadingStage | (out int loadingStage) |
| 636 | `0x80115a58L` | SEffe::scriptPlayXaAudio | (in int loadingStage, in int archiveIndex, in int fileIndex) |
| 637 | `0x800e7314L` | Battle::scriptLoadCutsceneDeff | (in int flagsAndIndex, in int bentIndex, in int p2, in int scriptEntrypoint) |
| 638 | `0x800e71e4L` | Battle::scriptLoadSpellOrItemDeff | (in int flagsAndIndex, in int bentIndex, in int p2, in int scriptEntrypoint) |
| 639 | `0x800e727cL` | Battle::scriptLoadEnemyOrBossDeff | (in int flagsAndIndex, in int bentIndex, in int p2, in int scriptEntrypoint) |
| 640 | `0x8011357cL` | SEffe::scriptConvertRotationYxzToXyz | (in int unused, in int yxzX, in int yxzY, in int yxzZ, out int xyzX, out int xyzY, out int xyzZ) |
| 641 | `0x80115a28L` | SEffe::scriptResetDeffManager | (in int mode) |
| 642 | `0x8011287cL` | SEffe::FUN_8011287c | (in int scriptIndex, in int partIndex, out int x, out int y, out int z) |
| 643 | `0x800e9798L` | Battle::scriptSetModelShadow | (in int bobjIndex, in int mode) |
| 644 | `0x800ea2a0L` | Battle::scriptSetBttlShadowSize | (in int bobjIndex, in int x, in int z) |
| 645 | `0x800ea30cL` | Battle::scriptSetBttlShadowOffset | (in int bobjIndex, in int x, in int y, in int z) |
| 646 | `0x801188ecL` | SEffe::scriptAllocateShadowEffect | (out int effectIndex) |
| 647 | `0x80115ad8L` | SEffe::scriptUpdateDeffManagerFlags | (in int flags) |
| 648 | `0x80115ea4L` | SEffe::scriptLoadDeffStageEffects | (in int mode) |
| 649 | `0x80115ed4L` | SEffe::scriptGetEffectTextureMetrics | (in int typeOrBobjIndex, in int textureIndex, out int u, out int v, out int w, out int h) |
| 650 | `0x801154f4L` | SEffe::scriptSetEffectNormalizeLightMatrixFlag | (in int effectIndex, in bool set) |
| 651 | `0x80116160L` | SEffe::scriptConsolidateEffectMemory | — |
| 712 | `0x8001ac88L` | Battle::scriptPlayCombatantSound | (in int type, in int charOrMonsterIndex, in int soundIndex, in int initialDelay, in int repeatDelay) |
| 713 | `0x8001acd8L` | Battle::scriptStopBentSound2 | (in int unused, in int bentIndex, in int soundIndex, in int mode) |
| 715 | `0x8001f250L` | Battle::scriptLoadCharacterAttackSounds | (in int charId) |
| 716 | `0x800203f0L` | Battle::scriptReplaceMonsterSounds | (in int monsterId) |
| 717 | `0x8001f674L` | Battle::scriptLoadCutsceneSounds | (in int cutsceneIndex, in bool dontStartSequence) |
| 718 | `0x8001f560L` | Battle::scriptLoadFinalBattleSounds | (in int battleProgress, in bool dontStartSequence) |
| 736 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 737 | `0x800cec8cL` | Battle::scriptAllocateFullScreenOverlay | (out int effectIndex, in int startR, in int startG, in int startB, in int endR, in int endG, in int endB, in int ticks) |
| 738 | `0x800cee50L` | Battle::scriptRand | (out int value, in int min, in int max) |
| 739 | `0x800ceeccL` | Battle::scriptSetWeaponTrailSegmentCount | (in int effectIndex, in int count) |
| 740 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 741 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 742 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 743 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 744 | `0x800cef00L` | Battle::scriptRenderColouredQuad | (in int r, in int g, in int b, in int translucency) |
| 745 | `0x800cf0b4L` | Battle::FUN_800cf0b4 | (in int p0, in int p1, in int p2, in int p3, in int p4) |
| 746 | `0x80102088L` | SEffe::scriptAllocateParticleEffect | (out int effectIndex, in int parentIndex, in int type, in int count, in int p4, in int p5, in int p6, in int p7, in int behaviourType) |
| 747 | `0x80102364L` | SEffe::scriptSetParticleAcceleration | (in int mode, in int effectIndex, in int accelerationX, in int accelerationY, in int accelerationZ, in int accelerationScale) |
| 748 | `0x800d3090L` | Battle::FUN_800d3090 | — |
| 749 | `0x800d0564L` | Battle::scriptAllocateProjectileHitEffect | (out int effectIndex, in int count, in int r, in int g, in int b) |
| 750 | `0x800d09b8L` | Battle::FUN_800d09b8 | — |
| 751 | `0x800d0decL` | Battle::scriptAllocateAdditionSparksEffect | (out int effectIndex, in int count, in int r, in int g, in int b, in int distance, in int ticks) |
| 752 | `0x80102608L` | SEffe::FUN_80102608 | — |
| 753 | `0x801077e8L` | SEffe::scriptAllocateAdditionOverlaysEffect | (in int attackerIndex, in int targetIndex, in int autoCompleteType, in int unused, out int effectIndex) |
| 754 | `0x801077bcL` | SEffe::scriptGetHitCompletionState | (in int unused, in int index, out int result) |
| 755 | `0x80108de8L` | SEffe::FUN_80108de8 | — |
| 756 | `0x800d19ecL` | Battle::scriptAllocateAdditionStarburstEffect | (out int effectIndex, in int parentIndex, in int rayCount, in int type) |
| 757 | `0x800d1cacL` | Battle::FUN_800d1cac | (out int effectIndex) |
| 758 | `0x800d1cf4L` | Battle::FUN_800d1cf4 | (out int effectIndex) |
| 759 | `0x801078c0L` | SEffe::scriptAlterAdditionContinuationState | (in int effectIndex, in int state) |
| 760 | `0x80108df0L` | SEffe::FUN_80108df0 | — |
| 761 | `0x800d2ff4L` | Battle::scriptAllocateGuardEffect | (out int effectIndex) |
| 762 | `0x800ce6a8L` | Battle::scriptAllocateWeaponTrailEffect | (out int effectIndex, in int parentIndex, in int dobjIndex) |
| 763 | `0x800d2734L` | Battle::scriptAllocateRadialGradientEffect | (out int effectIndex, in int count, in int type) |
| 764 | `0x800d3d74L` | Battle::scriptAllocateAdditionScript | (in int charId, in int p1) |
| 765 | `0x800cfcccL` | Battle::scriptGetBobjLocalWorldMatrixTranslation | (in int effectIndex, in int partIndex, out int x, out int y, out int z) |
| 766 | `0x800d4338L` | Battle::scriptAllocateSpTextEffect | (in int p0, both int p1) |
| 767 | `0x800d4580L` | Battle::scriptAllocateAdditionNameEffect | (in int totalSp) |
| 800 | `0x8010c378L` | SEffe::scriptAllocateLensFlareEffect | (out int effectIndex, in int bentIndex, in int x, in int y, in int z, in int deffFlags1, in int deffFlags2, in int deffFlags3, in int deffFlags4, in int deffFlags5) |
| 801 | `0x8010d1dcL` | SEffe::scriptAllocateWsDragoonTransformationFeathersEffect | (out int effectIndex, in int count, in int deffFlags) |
| 802 | `0x8010d7dcL` | SEffe::scriptAllocateGoldDragoonTransformEffect | (out int effectIndex, in int deffFlags, in int count, in int unused, in int horizontalMax, in int verticalMin, in int verticalMax, in int movementTicksMax, in int preMovementTicksMax) |
| 803 | `0x8010e04cL` | SEffe::scriptAllocateStarChildrenMeteorEffect | (out int effectIndex, in int deffFlags, in int count) |
| 804 | `0x8010edc8L` | SEffe::scriptAllocateStarChildrenImpactEffect | (out int effectIndex, in int count, in int maxStartingFrame, in int maxTranslationMagnitude) |
| 805 | `0x8010e89cL` | SEffe::scriptAllocateMoonlightStarsEffect | (out int effectIndex, in int deffFlags, in int maxToggleFrameThreshold, in int maxScale, in int count) |
| 806 | `0x8001c5fcL` | Scus94491BpeSegment::FUN_8001c5fc | — |
| 807 | `0x8001c604L` | Scus94491BpeSegment::FUN_8001c604 | — |
| 832 | `0x800d46d4L` | Battle::scriptRenderButtonPressHudElement | (in int type, in int x, in int y, in int translucency, in int brightness) |
| 833 | `0x80108df8L` | SEffe::FUN_80108df8 | (out int effectIndex) |
| 834 | `0x80102610L` | SEffe::FUN_80102610 | — |
| 835 | `0x800cfdf8L` | Battle::scriptGetBentDimension | (in int bentIndex, in int axis, out int size) |
| 836 | `0x80109158L` | SEffe::scriptAllocateRainEffect | (out int effectIndex, in int count) |
| 837 | `0x800ce9b0L` | Battle::scriptApplyWeaponTrailScaling | (in int effectIndex, in int smallScalingFactor, in int largeScalingFactor) |
| 838 | `0x801052dcL` | SEffe::scriptAllocateElectricityEffect | (out int effectIndex, in int parentIndex, in int boltAngleRangeCutoff, in int boltCount, in int maxSegmentLength, in int boltAngleStep, in int flags, in int type) |
| 839 | `0x80105604L` | SEffe::scriptGetBoltSegmentEnd | (in int effectIndex, in int boltIndex, in int segmentIndex, out int x, out int y, out int z) |
| 840 | `0x801087f8L` | SEffe::scriptAllocateDragoonAdditionScript | (in int flags, in int charId, in int ticksUntilStart, in int hudX, in int hudY) |
| 841 | `0x80105c38L` | SEffe::scriptAllocateThunderArrowEffect | (in int effectIndex, in int p1) |
| 843 | `0x80109a7cL` | SEffe::scriptAllocateScreenDistortionEffect | (out int effectIndex, in int angleStep, in int type) |
| 844 | `0x801089ccL` | SEffe::scriptGetDragoonAdditionHitsCompleted | (in int unused, out int count) |
| 845 | `0x801023f4L` | SEffe::FUN_801023f4 | — |
| 846 | `0x800cfec8L` | Battle::FUN_800cfec8 | — |
| 847 | `0x801023fcL` | SEffe::scriptGetAliveParticles | (in int effectIndex, out bool_array alive) |
| 848 | `0x8010246cL` | SEffe::scriptGetParticlePosition | (in int effectIndex, in int particleIndex, out int x, out int y, out int z) |
| 849 | `0x800cff24L` | Battle::scriptSetMtSeed | (in int seed) |
| 850 | `0x80109d30L` | SEffe::scriptAllocateVertexDifferenceAnimation | (in int effectIndex, in int differenceIndex, in int ticks, in int scale) |
| 851 | `0x8010a3fcL` | SEffe::scriptAllocateFrozenJetEffect | (out int effectIndex, in int tmdDeffIndex, in int p2, in int p3) |
| 852 | `0x800d34bcL` | Battle::scriptAllocateMonsterDeathEffect | (out int effectIndex, in int bentIndex, in int spriteIndex, in int unused) |
| 853 | `0x800d0124L` | Battle::scriptGetBobjModelPartCount | (in int bobjIndex, out int partCount) |
| 854 | `0x801079a4L` | SEffe::scriptGetAdditionOverlayActiveStatus | (in int type, out int state) |
| 896 | `0x8010a610L` | SEffe::scriptAllocateGradientRaysEffect | (out int effectIndex, in int count, in int innerY, in int midVertZ, in int outerY, in int step, in int flags, in int type) |
| 897 | `0x8010b1d8L` | SEffe::scriptAllocateScreenCaptureEffect | (out int effectIndex, in int deffFlags, in int x, in int y, in int w, in int h, in int type) |
| 1000 | `—` | Battle::scriptHasStatMod | (in reg id, in int bentId, in reg statType, out bool present) |
| 1001 | `—` | Battle::scriptAddStatMod | (in reg id, in int bentId, in reg statType, in reg statModType, in int_array statModParams) |
| 1002 | `—` | Battle::scriptRemoveStatMod | (in reg id, in int bentId, in reg statType) |
| 1003 | `—` | Battle::scriptGetStatModType | (in reg id, in int bentId, in reg statType, out reg statModType) |
| 1004 | `—` | Battle::scriptUpdateStatModParams | (in reg id, in int bentId, in reg statType, in int_array statModParams) |
| 1005 | `—` | Battle::scriptGetStatModParams | (in reg id, in int bentId, in reg statType, out int_array statModParams) |
| 1010 | `—` | Battle::scriptUseItem | (in reg itemId, in int targetBentIndex) |
| 1011 | `—` | Battle::scriptApplyEquipmentEffect | (in reg equipmentId, in int wearerBentIndex) |
| 1012 | `—` | Battle::scriptPrepareAttack | (out int waitTicks) |
| 1013 | `—` | Battle::scriptAttack | (out int type) |
| 1020 | `—` | Battle::scriptSetCombatantVramSlot | (in int combatantIndex, in int vramSlot) |
| 1021 | `—` | Battle::scriptPlayBentSoundForUsage | (in int type, in int bentIndex, in int soundIndex, in legend.game.combat.types.BattleSoundUsage usage, in int initialDelay, in int repeatDelay) |

## 4. Submap state table (144 slots — `SMap.getScriptFunctions`)

All 144 slots are base-null.

| index | ROM | function | params |
|---|---|---|---|
| 12 | `0x800d9bc0L` | SMap::scriptRestoreCharDataVitals | — |
| 13 | `0x800d9bf4L` | SMap::scriptClearStatusEffects | — |
| 14 | `0x800d9c1cL` | SMap::scriptCloneCharacterData | (in int sourceCharId, in int destCharId) |
| 15 | `0x800d9ce4L` | SMap::scriptSetCharAddition | (in int charId, in reg additionId) |
| 18 | `0x800d9d20L` | SMap::scriptGetCharAddition | (in int charId, out reg additionId) |
| 19 | `0x800d9d60L` | SMap::scriptMaxOutDartDragoon | — |
| 96 | `0x800df168L` | SMap::scriptSelfLoadSobjModelAndAnimation | (in int objectIndex) |
| 97 | `0x800df198L` | SMap::scriptSelfLoadSobjAnimation | (in int animIndex) |
| 98 | `0x800df1c8L` | SMap::scriptSelfGetSobjAnimation | (out int animIndex) |
| 99 | `0x800df1f8L` | SMap::scriptSelfToggleAnimationDisabled | (in bool disabled) |
| 100 | `0x800df228L` | SMap::scriptSelfIsAnimationFinished | (out int value) |
| 101 | `0x800df258L` | SMap::scriptSetModelPosition | (in int scriptIndex, in float x, in float y, in float z) |
| 102 | `0x800df2b8L` | SMap::scriptReadModelPosition | (in int scriptIndex, out float x, out float y, out float z) |
| 103 | `0x800df314L` | SMap::scriptSetModelRotate | (in int scriptIndex, in int x, in int y, in int z) |
| 104 | `0x800df374L` | SMap::scriptReadModelRotate | (in int scriptIndex, out int x, out int y, out int z) |
| 105 | `0x800df3d0L` | SMap::scriptSelfFacePoint | (in int x, in int y, in int z) |
| 106 | `0x800df410L` | SMap::scriptSelfSetSobjHidden | (in int value) |
| 107 | `0x800df440L` | SMap::scriptSelfMoveToPosition | (in int x, in int y, in int z, in int movementTicks) |
| 108 | `0x800df488L` | SMap::scriptSelfMoveAlongArc | (in int x, in int y, in int z, in int i_144) |
| 109 | `0x800df4d0L` | SMap::scriptCheckSelfCollision | (out int collisionPrimitiveIndex) |
| 110 | `0x800df500L` | SMap::FUN_800df500 | (out int value) |
| 111 | `0x800df530L` | SMap::scriptSelfSetIgnoreCollision | (in int value) |
| 112 | `0x800df560L` | SMap::FUN_800df560 | (in int animatedTextureIndex) |
| 113 | `0x800df5c0L` | SMap::scriptSelfEnableTextureAnimation | (in int animatedTextureIndex) |
| 114 | `0x800df590L` | SMap::FUN_800df590 | (out int value) |
| 115 | `0x800df5f0L` | SMap::scriptSelfDisableTextureAnimation | (in int animatedTextureIndex) |
| 116 | `0x800df620L` | SMap::scriptSelfGetMovementType | (out int value) |
| 117 | `0x800df650L` | SMap::scriptSelfAttachCameraToSobj | (in bool attach) |
| 118 | `0x800df680L` | SMap::scriptSelfIsCameraAttached | (out bool attached) |
| 119 | `0x800df6a4L` | SMap::scriptSetCameraPos | (in int x, in int y, in int z) |
| 120 | `0x800df788L` | SMap::scriptRotateSobj | (in int scriptIndex, in int x, in int y, in int z, in int frames) |
| 121 | `0x800df890L` | SMap::scriptRotateSobjAbsolute | (in int scriptIndex, in int x, in int y, in int z, in int frames) |
| 122 | `0x800df904L` | SMap::scriptSelfMoveAlongArc2 | (in int x, in int y, in int z, in int ticks, in int stepAccelerationY) |
| 123 | `0x800de1d0L` | SMap::scriptMovePlayer | (in int x, in int y, in int z) |
| 124 | `0x800df954L` | SMap::scriptFacePlayer | — |
| 125 | `0x800df9a8L` | SMap::FUN_800df9a8 | (in int scriptIndex, out int x0, out int y0, out int x1, out int y1) |
| 126 | `0x800dfb28L` | SMap::scriptGetCurrentSubmapId | (out int submapId) |
| 127 | `0x800dfb44L` | SMap::scriptSelfGetSobjNobj | (in int numberOfParts) |
| 198 | `0x80025218L` | SMap::scriptAddSobjTextbox | (in int index, in int submapObjectIndex, in int packedData, in int width, in int height, in string text) |
| 256 | `0x800e67d4L` | SMap::scriptMapTransition | (in int submapCut, in int submapScene) |
| 257 | `0x800e68b4L` | SMap::scriptGetCameraOffset | (out float x, out float y) |
| 258 | `0x800e6904L` | SMap::scriptSetCameraOffset | (in float x, in float y) |
| 259 | `0x800e69a4L` | SMap::scriptGetCollisionAndTransitionInfo | (out int flags, in int collisionPrimitiveIndex) |
| 260 | `0x800e69e8L` | SMap::FUN_800e69e8 | — |
| 261 | `0x800e69f0L` | SMap::scriptBlockCollisionPrimitive | (in int collisionPrimitiveIndex) |
| 262 | `0x800e6a28L` | SMap::scriptUnblockCollisionPrimitive | (in int collisionPrimitiveIndex) |
| 263 | `0x800e6a64L` | SMap::scriptSetEnvForegroundPosition | (in int x, in int y, in int overlayIndex) |
| 264 | `0x800e683cL` | SMap::scriptSetModeParamForNextCallToScriptSetCameraOffsetOrHideSubmapForegroundObject | (in int mode, in int foregroundObjectIndex, out int out) |
| 265 | `0x800e6af0L` | SMap::scriptGetSetEncountersDisabled | (in bool set, in bool apply, out int value) |
| 266 | `0x800e6aa0L` | SMap::scriptSetEnvironmentOverlayDepthModeAndZ | (in int mode, in int overlayIndex, in int z, out int out) |
| 267 | `0x800e6b64L` | SMap::scriptGetCollisionPrimitivePos | (in int collisionPrimitiveIndex, out float x, out float y, out float z) |
| 268 | `0x800e6bd8L` | SMap::FUN_800e6bd8 | — |
| 269 | `0x800e6be0L` | SMap::FUN_800e6be0 | (in int sobjIndex, out float z) |
| 270 | `0x800e6cacL` | SMap::FUN_800e6cac | (in int x, in int Y) |
| 271 | `0x800e6ce0L` | SMap::scriptStartFmv | (in int fmvIndex, in int engineStateAfterFmv, in int submapCutAfterFmv, in int submapSceneAfterFmv) |
| 288 | `0x800dfb74L` | SMap::scriptSelfHideModelPart | (in int partIndex) |
| 289 | `0x800dfba4L` | SMap::scriptSelfShowModelPart | (in int partIndex) |
| 290 | `0x800dfbd4L` | SMap::scriptSelfFaceCamera | — |
| 291 | `0x800dfc00L` | SMap::scriptScaleXyz | (in int sobjIndex, in int x, in int y, in int z) |
| 292 | `0x800dfc60L` | SMap::scriptScaleUniform | (in int sobjIndex, in int scale) |
| 293 | `0x800dfca0L` | SMap::scriptSetModelZOffset | (in int sobjIndex, in int offset) |
| 294 | `0x800dfcd8L` | SMap::scriptSelfSetSobjFlag | (in int bitIndex, in bool value) |
| 295 | `0x800dfd10L` | SMap::scriptSelfGetSobjFlag | (in int bitIndex, out bool value) |
| 296 | `0x800de334L` | SMap::scriptAddTriangleIndicator | (in int collisionPrimitiveIndex, in int indicatorType) |
| 297 | `0x800de4b4L` | SMap::scriptAddTriangleIndicators | (in int_array stream) |
| 298 | `0x800dfd8cL` | SMap::scriptShowAlertIndicator | (in int sobjIndex, in int y) |
| 299 | `0x800dfdd8L` | SMap::scriptHideAlertIndicator | (in int sobjIndex) |
| 300 | `0x800dfd48L` | SMap::scriptRemoveTriangleIndicatorType | (in int indicatorType) |
| 301 | `0x800e05c8L` | SMap::FUN_800e05c8 | (in int index, in int value) |
| 302 | `0x800e05f0L` | SMap::FUN_800e05f0 | (in int index) |
| 303 | `0x800e0614L` | SMap::scriptEnableSobjFlatLight | (in int sobjIndex, in int r, in int g, in int b) |
| 304 | `0x800e0684L` | SMap::scriptDisableSobjFlatLight | (in int sobjIndex) |
| 305 | `0x800e06c4L` | SMap::scriptSetSobjCollision1 | (in int sobjIndex, in int horizontalSize, in int verticalSize) |
| 306 | `0x800e0710L` | SMap::scriptGetSobjCollidedWith1 | (in int sobjIndex, out int collided) |
| 307 | `0x800e0894L` | SMap::scriptSetSobjCollision2 | (in int sobjIndex, in int horizontalSize, in int verticalSize, in int reach) |
| 308 | `0x800e08f4L` | SMap::scriptGetSobjCollidedWith2 | (in int sobjIndex, out int collided) |
| 309 | `0x800e0930L` | SMap::scriptSetAmbientColour | (in int sobjIndex, in int r, in int g, in int b) |
| 310 | `0x800e09a0L` | SMap::scriptResetAmbientColour | (in int sobjIndex) |
| 311 | `0x800e09e0L` | SMap::scriptEnableShadow | (in int sobjIndex) |
| 312 | `0x800e0a14L` | SMap::scriptDisableShadow | (in int sobjIndex) |
| 313 | `0x800e0a48L` | SMap::scriptSetShadowSize | (in int sobjIndex, in int x, in int z) |
| 314 | `0x800e0a94L` | SMap::scriptSetShadowOffset | (in int sobjIndex, in float x, in float y, in float z) |
| 315 | `0x800dee28L` | SMap::scriptCheckPlayerCollision | (in int deltaX, in int deltaY, in int deltaZ, out int collidee) |
| 316 | `0x800e0af4L` | SMap::scriptGetPlayerMovement | (out float x, out float y, out float z) |
| 317 | `0x800e0b34L` | SMap::scriptSwapShadowTexture | (in int type) |
| 318 | `0x800e0ba0L` | SMap::scriptSetSobjPlayerCollisionMetrics | (in int sobjIndex, in int horizontalSize, in int verticalSize, in int reach) |
| 319 | `0x800e0cb8L` | SMap::FUN_800e0cb8 | (in int sobjIndex) |
| 672 | `0x800dfe0cL` | SMap::scriptLoadSobjModelAndAnimation | (in int scriptIndex, in int objectIndex) |
| 673 | `0x800dfec8L` | SMap::scriptLoadSobjAnimation | (in int scriptIndex, in int animIndex) |
| 674 | `0x800dff68L` | SMap::scriptGetSobjAnimation | (in int scriptIndex, out int animIndex) |
| 675 | `0x800dffa4L` | SMap::scriptToggleAnimationDisabled | (in int scriptIndex, in bool disabled) |
| 676 | `0x800dffdcL` | SMap::scriptIsAnimationFinished | (in int scriptIndex, out bool finished) |
| 677 | `0x800e0018L` | SMap::scriptFacePoint | (in int scriptIndex, in float x, in float y, in float z) |
| 678 | `0x800e0094L` | SMap::scriptSetSobjHidden | (in int scriptIndex, in bool hidden) |
| 679 | `0x800de668L` | SMap::scriptSobjMoveToPosition | (in int scriptIndex, in float x, in float y, in float z, in int movementTicks) |
| 680 | `0x800de944L` | SMap::scriptSobjMoveAlongArc | (in int scriptIndex, in float x, in float y, in float z, in int ticks) |
| 681 | `0x800e00ccL` | SMap::scriptCheckSobjCollision | (in int scriptIndex, out int collisionPrimitiveIndex) |
| 682 | `0x800e0148L` | SMap::scriptGetSobjIgnoreCollision | (in int scriptIndex, out int value) |
| 683 | `0x800e01bcL` | SMap::scriptResetClutAnimations | (in int scriptIndex, in int clutAnimationIndex) |
| 684 | `0x800e0244L` | SMap::scriptEnableTextureAnimation | (in int scriptIndex, in int animatedTextureIndex) |
| 685 | `0x800e0204L` | SMap::scriptDisableClutAnimation | (in int scriptIndex, in int clutAnimationIndex) |
| 686 | `0x800e0284L` | SMap::scriptDisableTextureAnimation | (in int scriptIndex, in int animatedTextureIndex) |
| 687 | `0x800e02c0L` | SMap::scriptGetSobjMovementType | (in int scriptIndex, out int value) |
| 688 | `0x800e02fcL` | SMap::scriptAttachCameraToSobj | (in int scriptIndex, in bool attach) |
| 689 | `0x800deba0L` | SMap::scriptSobjMoveAlongArc2 | (in int scriptIndex, in float x, in float y, in float z, in int ticks, in float stepAccelerationY) |
| 690 | `0x800e03a8L` | SMap::scriptGetSobjNobj | (in int scriptIndex, out int numberOfParts) |
| 691 | `0x800e03e4L` | SMap::scriptHideModelPart | (in int scriptIndex, in int partIndex) |
| 692 | `0x800e0448L` | SMap::scriptShowModelPart | (in int scriptIndex, in int partIndex) |
| 693 | `0x800e04b4L` | SMap::scriptFaceCamera | (in int sobjIndex) |
| 694 | `0x800e0520L` | SMap::scriptSetSobjFlag | (in int sobjIndex, in int bitIndex, in bool value) |
| 695 | `0x800e057cL` | SMap::scriptGetSobjFlag | (in int sobjIndex, in int bitIndex, out bool value) |
| 696 | `0x800e074cL` | SMap::loadInterpolatedSobjAnimation | (in int sobjIndex, in int animIndex) |
| 697 | `0x800e07f0L` | SMap::loadUninterpolatedSobjAnimation | (in int sobjIndex, in int animIndex) |
| 698 | `0x800e0184L` | SMap::scriptSetSobjIgnoreCollision | (in int scriptIndex, in int value) |
| 699 | `0x800e0c40L` | SMap::scriptSetChapterTitleCardReadyToRender | (in int originX, in int originY) |
| 700 | `0x800e0c80L` | SMap::scriptGetChapterTitleCardAnimationComplete | (out int complete) |
| 701 | `0x800e0c00L` | SMap::scriptLoadChapterTitleCard | (in int titleNum) |
| 702 | `0x800e0c24L` | SMap::scriptIsChapterTitleCardLoaded | (out bool loaded) |
| 703 | `0x800e0c9cL` | SMap::scriptSetTitleCardAnimationPauseTicks | (in int ticks) |
| 768 | `0x800f2048L` | SMap::scriptSelfInitTmdDust | (in int shouldRender, in int size, in int instantiationTicks, in int maxTicks) |
| 769 | `0x800f1f9cL` | SMap::scriptInitTmdDust | (in int sobjIndex, in bool shouldRender, in int size, in int instantiationTicks, in int maxTicks) |
| 770 | `0x800f1060L` | SMap::scriptAllocateSmokePlumeEffectData | (in int_array smokeData) |
| 771 | `0x800f2264L` | SMap::scriptInitFootprints | (in int shouldRender) |
| 772 | `0x800f179cL` | SMap::scriptAddSavePoint | (in bool hasSavePoint, in float x, in float y, in float z) |
| 773 | `0x800f23ecL` | SMap::scriptInitOrthoDust | (in int mode, in int size, in int instantiationTicks, in int maxTicks) |
| 774 | `0x800f2780L` | SMap::FUN_800f2780 | — |
| 775 | `0x800f2090L` | SMap::scriptReinitializeSmokePlumeForIntermittentBursts | (in int_array smokeData) |
| 776 | `0x800f2198L` | SMap::scriptInitSnowParticleData | (in int unused, in int stepAngleMax, in int translationScaleX, in int stepXMax, in int stepYDivisor) |
| 777 | `0x800f1eb8L` | SMap::FUN_800f1eb8 | (in int p0) |
| 778 | `0x800f2618L` | SMap::scriptAddTriangleIndicators2d | (in int_array stream) |
| 779 | `0x800f1b64L` | SMap::scriptAddTriangleIndicatorsWithOffset | (in int_array stream) |
| 780 | `0x800f26c8L` | SMap::scriptAddTriangleIndicator2d | (in int indicatorType, in int screenspaceX, in int screenspaceY) |
| 781 | `0x800f1d0cL` | SMap::scriptAddTriangleIndicatorWithOffset | (in int collisionPrimitiveIndex, in int indicatorType, in float screenspaceOffsetX, in float screenspaceOffsetY) |
| 782 | `0x800f14f0L` | SMap::scriptAllocateSmokeCloudData | (in int effectState, in int instantiationTicks, in int lifecycleTicks, in float offsetBaseX, in float offsetY, in int offsetRandomX, in float totalDistance, in float size, in float sizeChange) |
| 783 | `0x800f24d8L` | SMap::scriptDeallocateSmokeCloudDataAndEffect | (in int forceDeallocation) |
| 784 | `0x800f24b0L` | SMap::scriptSetSmokeCloudEffectStateToDontTick | (in int mode) |
| 785 | `0x800f23a0L` | SMap::scriptSetFootprintsInstantiationInterval | (in int instantiationInterval) |
| 786 | `0x800f1634L` | SMap::scriptInitLawPodTrail | (in bool shouldRender, in int maxSegments, in int fadeDelay, in int fadeSteps, in int width, in int translucency, in int r, in int g, in int b) |
| 787 | `0x800f22c4L` | SMap::scriptChangeFootprintsMode | (in int footprintMode, in int instantiationTicks, in int sobjIndex) |
| 788 | `0x800f2554L` | SMap::FUN_800f2554 | (in int p0, in int p1) |
| 789 | `0x800f25a8L` | SMap::scriptDeallocateLawPodTrail | (in bool shouldDeallocate) |
| 790 | `0x800f1274L` | SMap::scriptAllocateUnusedSmokeEffectData | (in int instantiationTicks, in int lifecycleTicks, in int delayTicks, in float x, in float y, in float z, in int totalDistance, in int offsetRandomX, in int size, in int sizeChange) |
| 791 | `0x800f2780L` | SMap::scriptOpenShop | (in reg shopId) |
| 940 | `0x800dfca0L` | SMap::scriptSetSobjUsePs1Depth | (in int sobjIndex, in bool usePs1Depth) |

## 5. Observations

- **Slot families by index range** (mechanical, from the tables above):
  - 0–11: global flags/indicators/fade/rumble/waits (base);
  - 12–19: submap character-data ops (SMap) — the only submap slots in the
    low range, because 12–15/18–19 are base-null;
  - 20–31: unimplemented (described, e.g. extra camera/transition ops);
  - 32–50: battle camera (Battle);
  - 51–95: unimplemented;
  - 96–127: submap sobj/nobj manipulation (SMap) — the largest contiguous
    submap block;
  - 128–178: battle BENT position/rotation/model ops (Battle);
  - 192–227: text boxes (base + a few battle/smap entries);
  - 230–255: base audio/SFX + submap transitions (mixed base/smap);
  - 256–319: submap sobj scripting + transitions (SMap, blocks 256–271 and
    288–319);
  - 320–444: battle effects/stage/cutscene (Battle);
  - 480–651: battle combat model (Battle: attacks, stats, damage, DEFF,
    event playback);
  - 672–703: submap environment/transition (SMap);
  - 704–718: base + battle audio sequences (mixed);
  - 736–767: battle battle-flow/menu (Battle);
  - 768–791: submap misc (SMap);
  - 864–866 / 890–892 / 900–916: base chest/registry/SItem helpers (several
    unanchored);
  - 960: render aspect (base); 1000–1021: battle stat-mod/mod-support block
    (mostly unanchored); 893–959 and 961–1023: mostly unimplemented
    (described original features the reference left out).
- **Enum-typed params** (doc 04 §4): the two enum classes appear in the
  battle BENT stat getters/setters (`BattleEntityStat` — 182 values; the
  stat integer is the row index in
  `legend.game.combat.bent.BattleEntityStat.csv`) and the BENT sound ops
  (`BattleSoundUsage`: `ACTIVE_FORM`=0, `REACTION`=1).
- **Branch attributes**: the local metadata marks exactly the fork-destination
  params (`[fork_jump]`) and nothing else (no `jump`/`subroutine`/`gosub`
  values occur in the shipped CSV); the disassembler's other branch modes
  exist in code for the remote metadata (doc 04 §2.1, Q-62).
- **String params** (`type string`) occur on textbox-content ops (base text
  block) — these drive the disassembler's string-table recovery (doc 03
  §3.2).
- **`stor[7]` flag overlap**: several battle functions (BENT flag ops) read
  and write the low 16 bits of `stor[7]` — the same word that holds the
  state's callback flags in bits 16–27 (doc 05 §3.1); scripts and engine
  code share the slot by design.
- **No slot is double-implemented**: base ∩ Battle = ∅ and
  base ∩ SMap = ∅ (verified: every state slot is base-null) and
  Battle ∩ SMap = ∅ — the base-first merge never actually disambiguates in
  the shipped tables (it matters only for mods adding state functions).
- **Registry-ID params** (`reg`/`reg_array` types) concentrate in the item
  (SItem) and battle loot/equipment families — registry IDs are the mod data
  surface (cross-repo map).

## 6. Evidence index

| Claim area | Source |
|---|---|
| Table extraction | `Scus94491BpeSegment_8004.java` (static block, `getScriptFunction`, `loadEngineStateFunctions`), `Battle.java` (`getScriptFunctions`, line 735+), `SMap.java` (`getScriptFunctions`, line 749+) — mechanical extraction with 100% coverage check |
| Names/descriptions | `patches/meta/descriptions.csv` (1024 rows) |
| Params | `patches/meta/params.csv` (1538 rows), joined by name |
| ROM addresses | `@Method` annotations across `legend/**` (8-line window scan); 559/592 covered |
| Enums | `patches/meta/enums.csv` + per-enum CSVs (doc 04 §4) |
| Dispatch/stall | doc 05 §6; doc 04 §3 |

Pins: `Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade`.

## 7. Open items

- The **432 unimplemented indices**: which are original-game features the
  reference chose not to port vs. never-existing original slots cannot be
  distinguished offline (the descriptions are the only evidence of intent —
  asset/external-PSX check; M9 will hit this when porting gameplay systems
  whose scripts call them).
- The **33 unanchored slots** (`—` ROM): same originality question; their
  param metadata exists, so the toolchain fully supports them.
- Q-63/Q-64 from doc 05 (fork depth, DEALLOCATE82 original semantics) stand
  unchanged.
