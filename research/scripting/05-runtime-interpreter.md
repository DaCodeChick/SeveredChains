# M3-05: The Runtime Interpreter

Scope: the in-game script engine — `ScriptManager` (allocation, scheduling,
toolchain host), `ScriptState` (the per-script VM context), the frame loop,
the `Param` class hierarchy, storage and registry-ID models, native dispatch,
fork/consume/deallocate lifecycle, timing, and error handling.

Validation categories, per `AGENTS.md`: **source-verified** (reference code),
**format-corroborated**, **asset-validated** (none for M3). The ROM
`@Method(0x…)` annotations are Ghidra decompiler labels carried into the
reference source; they anchor "this is the ported original routine" but are
not independently verified against PSX hardware (see Q-17).

## 1. Component map

| class | role |
|---|---|
| `ScriptManager` | owns the 108-slot state array, the param pool, the frames-per-tick cadence, stop/pause, and the single `Meta` + toolchain instances (compiler/tokenizer/disassembler/assembler) used by the in-game patch pipeline and crash dumps |
| `ScriptState<T>` | one script instance: index, name, `innerStruct_00` (the `ScriptedObject` it drives, nullable), storage, registry IDs, value stack, call stack, ticker/renderer/destructor/temp-ticker callbacks, flags |
| `RunningScript<T>` | the per-instruction decode window held by a state: `scriptState_04`, `opOffset_08` (committed instruction), `commandOffset_0c` (word being decoded), `opIndex_10` (OpType), `paramCount_14`, `opParam_18` (header), `params_20` (≤10 live params) |
| `ScriptFile` | the byte buffer as `int[] ops` (LE), `getEntry(i) = ops[i] / 4`, `getOp`/`setOp` — writable |
| `ScriptStackFrame` | `(file, offset)` — one call-stack frame; `copy()` for forks/returns |
| `FlowControl` | `CONTINUE` / `PAUSE` / `PAUSE_AND_REWIND` |
| `Param` hierarchy | parameter value objects (§5) |

The manager is a process-wide singleton (`GameEngine.SCRIPTS`), constructed
once with the patch include dirs and patch dir.

## 2. State allocation and reserved slots

`ScriptManager` holds `scriptStatePtrArr_800bc1c0 = new ScriptState[108]`.

- **Auto allocation** (`allocateScriptState(name, type)` →
  `findFreeScriptState()`): the method **pre-increments** `upperBound` (wrap
  ≥108 → 9) then scans `upperBound..107`, then `9..upperBound−1`, and never
  touches 0–8; `upperBound` is **reset to 9 at the end of every `tick()`**. So:
  - **slots 0–8 are hard-reserved** — `findFreeScriptState` can only return
    9–107;
  - allocation is **round-robin over 9–107 from the last allocation point**;
    because of the per-tick reset, the **first allocation of a frame starts
    scanning at slot 10**, and **slot 9 is returned only when slots 10–107
    are all occupied** (corrected by the M3 audit — the M3 text's
    "lowest-free-first from slot 9" was inaccurate). This resolves **Q-42**:
    slots 0–8 are hard-reserved (the scan space is 9–107) and `upperBound` is
    a round-robin allocation pointer, not a lowest-first mechanism.
- **Explicit allocation** (`allocateScriptState(index, name, type)`) can place
  a state in any slot, including the reserved ones. Observed explicit users
  (source-verified):
  - index **0** = "Submap controller" (`SMap.java:3263`) — the main submap
    script;
  - index **1** = "Lighting controller" (`Battle.java:6524`);
  - index **5** = "DRGN1.401" (`Battle.java:9308`) — the battle script;
  - player BENT script slot: `Battle.java:2167` uses entity-data
    `scriptSlot`/`scriptIndex`; `Battle.java:4490` (the **non-player** BENT
    branch) instead calls `SCRIPTS.findFreeScriptState()` — auto-allocated
    (corrected by the M3 audit).
- Exhaustion → `RuntimeException("Ran out of script states")`.
- `getState(index)`: `index == -1` → null; otherwise the slot (possibly null).
- `allocateScriptState` initialization (source-verified):
  `stor[i] = -1` for `i = 1..62`; `stor[0] = index`;
  `stor[7] = FLAG_1_0000 | FLAG_FILE_NOT_SET | FLAG_TICKER_NOT_SET |
  FLAG_RENDERER_NOT_SET | FLAG_DESTRUCTOR_NOT_SET`; posts the `ALLOCATED`
  lifecycle event.
- `deallocate(index)` sets the slot to null (called from
  `ScriptState.deallocate()`, which first runs the destructor unless
  `FLAG_DESTRUCTOR_NOT_SET` or `FLAG_CHILD_SCRIPT`, then posts
  `POST_DEALLOCATE`).

## 3. The state data model

### 3.1 Storage

- `storage_44 = int[63]`, `storagef_44 = float[63]`, `isFloat = boolean[63]`
  (`STORAGE_COUNT = 63`). Every slot has **both** an int and a float
  representation plus a mode bit:
  - `setStor(int)`: writes both (`storagef` = the int), clears the float flag;
  - `setStor(float)`: writes the float, writes `Math.round(val)` into the int
    mirror, sets the float flag.
  - Ops consult `isFloat` to pick float-aware arithmetic
    (`Param.isFloat` and friends; the math ops in the binary-format doc §6.1).
- Documented slot conventions (source comment, `storage_44` javadoc —
  source-verified, with the caveat that some are Inferred from usage):
  - **0**: own script index;
  - **5**: parent script index (set by fork);
  - **6**: child script index (head of the child chain; set by fork,
    cleared by deallocateChildren);
  - **7**: the flags word (below);
  - **8**: battle menu options bitset (0x01 attack … 0x80 special);
  - **9**: "is dragoon";
  - **32**: fork payload (written by the `FORK`/`FORK_REENTER` ops);
  - flags bits 0–15 of stor[7] are repurposed in battle as BENT flags
    (0x1, 0x2 dragoon, 0x4 monster, …, 0x800 no-script, …) — scripts read
    entity state through the same slot.
- **Flags** (stor[7] bits 16–27, source-verified constants + javadoc gating
  rules):
  - `FLAG_FILE_NOT_SET` (0x2_0000) — no script loaded; the VM frame loop is
    skipped entirely;
  - `FLAG_TICKER_NOT_SET` / `FLAG_RENDERER_NOT_SET` / `FLAG_DESTRUCTOR_NOT_SET`
    — the respective callback is null;
  - `FLAG_CHILD_SCRIPT` — this state is a fork child; **child states do not
    execute the VM frame, ticker, renderer, or destructor themselves** — they
    are driven through the parent (forked ops jump the *child's* frame;
    consume adopts it);
  - `FLAG_PARENT_SCRIPT` — has a child;
  - `FLAG_TEMP_TICKER_SET` (0x400_0000, "backwards" — set means *present*) —
    temp ticker active;
  - `FLAG_1_0000`, `FLAG_40_0000`–`FLAG_200_0000` — undocumented (reserved).
- **Registry IDs**: `registryIds = RegistryId[100]`
  (`REGISTRY_ID_COUNT = 100`) — the `reg[N]` parameter space; `RegistryId` is
  the Mod-Loader registry handle (mod data, registries — see
  `research/02-cross-repository-map.md`).

### 3.2 Callbacks

- `ticker_04` (`BiConsumer<ScriptState, T>`), `renderer_08`,
  `destructor_0c`, `tempTicker_10` (`TempTicker` — one-shot: when its `run`
  returns true it is cleared).
- Gating (source-verified): ticker runs when `!TICKER_NOT_SET && !CHILD` and
  the manager's frame gate (§7); renderer when `!RENDERER_NOT_SET && !CHILD`
  and not *stopped* (renderers keep running while *paused*); destructor when
  `!DESTRUCTOR_NOT_SET && !CHILD`.
- `innerStruct_00` is the scripted object (`ScriptedObject` interface:
  `getPosition()` + debug rendering; the manager exposes
  `getObject(index, type)`).

### 3.1.1 Value stack and call stack

- `stack = Deque<Integer>` (value stack for `PUSH`/`POP`; the high-level
  function calling convention in the language doc).
- `callStack = LinkedList<ScriptStackFrame>`, **top = current frame**
  (`frame() = peek()`); `pushFrame`/`popFrame` for `GOSUB`/`RETURN`;
  `replaceFrame` swaps the top (used by `ScriptInlineParam.jump`);
  `DEPTH` op reports `callStack.size()`.

## 4. The VM frame loop (`ScriptState.executeFrame`)

Exact semantics (source-verified, full read of lines 458–533):

```
executeFrame():
  ticks++
  PRE_SCRIPT_VM_TICK event
  if !(FLAG_FILE_NOT_SET or FLAG_CHILD_SCRIPT) && !paused:
    commandOffset = frame().offset
    opOffset      = commandOffset
    do:
      opCommand    = file.getOp(commandOffset)
      opIndex      = OpType.byOpcode(opCommand & 0xff)      // throws on unknown
      paramCount   = (opCommand >>> 8) & 0xff
      opParam      = opCommand >>> 16
      if paramCount > 10: throw RuntimeException("Too many parameters!")
      commandOffset++
      for i in 0..paramCount: params[i] = parseParam()      // eager, §5
      ret = runOp(opIndex, context)                         // the op body
      if ret in {CONTINUE, PAUSE}: opOffset = commandOffset // commit *next*
      // PAUSE_AND_REWIND: opOffset stays on the current instruction
      params cleared
    while ret == CONTINUE && !paused
    frame().offset = opOffset
  POST_SCRIPT_VM_TICK event
```

- **CONTINUE**: fetch the next instruction in the same frame, immediately.
- **PAUSE**: stop for the frame; the committed `opOffset` points at the
  **next** instruction, so next frame resumes after the pausing op
  (the ROM comment in the reference: "Returning 1 pauses execution until the
  next frame").
- **PAUSE_AND_REWIND**: stop for the frame; `opOffset` points at the
  **same** instruction, so it re-executes next frame
  ("Returning anything else pauses execution and repeats the same
  instruction next frame").
- A single frame therefore runs a script until its first pausing/terminal op
  — `while` loops without a pausing op complete within one frame
  (language doc §2.5).
- The per-instruction `parseParam` is **eager**: script hops
  (`stor[stor[…]]`, 0x24–0x27 forms) are dereferenced at decode time, not at
  op execution (binary-format doc §4).
- `ticks` (the "has this state ever executed a frame" counter) drives the
  scheduler's `hasExecuted()` gate (§7).
- `paused` is a per-state flag (`pause()`/`resume()`), separate from the
  manager-level pause.

## 5. The `Param` hierarchy (runtime)

All live params are drawn from the manager's `QueuePool<Param>` (two
registered types at construction: `ScriptInlineParam`, `ScriptStorageParam`);
`executeScriptFrame` calls `paramPool.reset()` at the start of each VM frame —
**params are per-instruction and recycled**; nothing may hold a reference
across frames.

| class | value model | notes (source-verified) |
|---|---|---|
| `Param` (abstract) | `get()`/`set(int)`/`array(int)` + float + registry overloads; `isFloat()`, `isRegistryId()`, `getRegistryId()` | base math ops (add/sub/mul/div/mod/not/neg/incr/decr/and/or/xor/shl/**shr — logical** `>>>`/`shr` — arithmetic `>>`); int div/mod-by-zero throw `ArithmeticException` (the DIV op guards itself, §binary-format 6.1) |
| `ScriptTempParam` | immutable scalar; `ZERO` constant | used by the `_0` compare ops and as scratch |
| `ScriptInlineParam` | `(state, wordOffset)` into **that state's current frame file**; `get()` = the word; `set(int)` **writes the word into the script file** ("Apparently this is possible" — source comment); `array(i)` = same state, offset + i; `jump()` = replace the running top frame with a copy of *this* state's file at this offset | the target of all `inl` forms (§binary-format 4.1); cross-file jumps happen because the state may not be the running one |
| `ScriptStorageParam` | `(state, storageIndex)` | the target of `stor` forms; float-aware get/set |
| `GameVarParam` | `var[N]` → a reference-specific switch mapping the global game-variable index to an engine global (the ~100+ documented game variables, R1 `research/platform/02-execution-model.md` §5 / state docs) | |
| `GameVarArrayParam` | `var[N][M]` → engine global arrays | |
| `ScriptStateRegistryIdParam` | `(state, slot)` → `state.registryIds[slot]` | `reg[N]` |
| `ScriptStateVarRegistryIdParam` | `(state, storIndex)` → `registryIds[stor[storIndex]]` | `reg[stor[N]]` |
| `ScriptInlineRegistryIdParam` | reads the ID **string** at a file offset (length in the param) at `getRegistryId()` time; immutable | `id[...]` |
| `ScriptStateNullRegistryIdParam` | null registry ID | `null` |

Registry-ID params throw `IllegalStateException` from the int/float accessors
("can only store registry IDs") — mixing them with integer ops is a hard
error (binary-format doc §6.6).

## 6. Native dispatch (`CALL`)

`scriptExecuteSubFunc` (source-verified):

```
function = getScriptFunction(header)        // base table first, then state list
if function == null: return PAUSE_AND_REWIND // permanent stall, retried every frame
try:    return function.apply(context)
catch UnsupportedOperationException: throw RuntimeException("Script subfunc <n> error")
```

- `getScriptFunction` = `scriptSubFunctions_8004e29c.get(i)` (base, 86
  non-null slots) else `engineStateFunctions_8004e29c.get(i)` (state
  overrides); doc 04 §3.
- The **null → stall** behavior is how unimplemented/unregistered natives
  surface: the script rewinds the same CALL every frame forever (and the
  disassembler's `probeBranch` never models it — the toolchain treats unknown
  calls as a *compile* error instead, binary-format doc §6.6).
- With per-index `scriptLog[i]` enabled, each executed op is logged with the
  `scriptFunctionDescriptions` map's formatted description (the map carries
  the human-readable per-op semantics, e.g. `SWAP_BROKEN` →
  `"tmp = …; *p1 = tmp; *p0 = tmp; // Broken swap"`).

## 7. Scheduling and timing

### 7.1 The manager tick (every render frame)

`ScriptManager.tick()` (source-verified):

```
ticked = false
if currentTicks == 0:           # once per framesPerTick render frames
  executeScriptFrame()          # one VM frame for ALL states (0..107 in order)
  ticked = true
executeScriptTickers(ticked)    # gated: return if paused || stopped;
                                # then per-state tick() and tempTick() in two
                                # passes, each for states with hasExecuted() || ticked
upperBound = 9                  # allocation pointer reset (reserved slots, §2)
executeScriptRenderers(ticked)  # gated: return if stopped (paused still renders)
currentTicks = (currentTicks + 1) % framesPerTick
```

- `executeScriptFrame` skips when `paused || stopped`; iterates **all 108
  slots in index order**; per-state try/catch → on any throwable:
  `state.dump()` then `RuntimeException("An error occurred while ticking
  script N")` — **one crashing script aborts the whole frame's script
  execution** and propagates up to the game loop (the game crashes; the dump
  in the log is the recovery tool).
- `executeScriptTickers`: per state, when `hasExecuted() || isScriptFrame`
  (the `isScriptFrame` flag lets effects allocate child effects that tick in
  the same frame — source comment referencing GH#1530), calls `state.tick()`
  then `state.tempTick()`; same dump-and-throw on error.
- `executeScriptRenderers`: gated only by `stopped` (a **paused** engine still
  renders scripted objects); same gate as tickers; same dump-and-throw.
- `stop()`/`start()` (javadoc: "Stops the script engine (will not render)") —
  used by the menu system (`Menus.java:67`/`101`); `pause()`/`resume()`
  ("stops ticks but still renders") — used by submap transitions
  (`SMap.java:4019` with `resume()` at the transition endpoints
  4168–4379).
- `willTick()` exposes `currentTicks == 0` (other systems query the cadence).

### 7.2 Script cadence per context (cross-referenced with R1)

- `loadGameStateOverlay` resets `framesPerTick = 1` on **every** engine-state
  transition (`EngineStates.java:71`);
- submap `INIT_0` sets `framesPerTick = 2` together with `vsyncMode = 1`
  (60 Hz rendering) (`SMap.java:4089`) → **submap scripts tick at 30 Hz**;
- no other context changes the cadence → battle scripts tick at the render
  rate (20 Hz), title/game-over/wmap-menu scripts at 30 Hz, credits at
  60 Hz, wmap exploration at 60 Hz (R1 per-context frame rates,
  `research/platform/02-execution-model.md` §5).
- Note the asymmetry: **submap exploration is the only context that halves
  the script rate** (scripts at 30 Hz while the world renders at 60 Hz);
  world-map exploration runs scripts at the full 60 Hz.

### 7.3 Script-ticker vs VM-frame distinction

The VM advances **once per `framesPerTick` render frames**, but the
`ticker_04`/`renderer_08`/`tempTicker` callbacks run **every render frame**
(when not paused/stopped) — game systems can thus run per-frame logic
attached to a script state (e.g. effect updates) at the full render rate
while the script itself steps slower.

## 8. Fork / consume / deallocate lifecycle

All three operate through the stor[5]/stor[6] parent/child chain
(source-verified, full reads):

- **`FORK` (86)** `fork index, addr, val`: `state(index).fork()` then the
  **child's** frame jumps to `addr`; child `stor[32] = val`; parent
  `CONTINUE`s. Fork mechanics:
  - child = a fresh auto-allocated state (index ≥ 9), named `"Forked " + name`;
  - child `stor[7] |= FLAG_CHILD_SCRIPT`; parent `setFlag(FLAG_PARENT_SCRIPT)`;
  - **storage 8–62 copied** parent→child (`storage_44`, `storagef_44`,
    `isFloat` — the whole array, and **registry IDs copied in full**;
    slots 0–7 are *not* copied — the child's own index/parent/child/flags are
    set explicitly);
  - child `stor[5] = parent index`; the child is **inserted at the head** of
    the parent's child chain (parent's old `stor[6]` becomes the child's
    `stor[6]`, parent `stor[6] = child index`);
  - **call stack copied** (each frame `copy()`ed, in order).
- **`FORK_REENTER` (87)** `fork index, entrypoint, val`: as `FORK` but the
  child's frame offset is set to `file.getEntry(entrypoint)` (the child's
  *current* file — the file must already be loaded there).
- **`CONSUME` (88)**: `consumeChild()` — the state adopts its immediate
  child's identity:
  - if the child itself is a parent, the chain is relinked (self inherits
    `FLAG_PARENT_SCRIPT`, the grandchild's `stor[5]` repointed to self);
    otherwise the child pointer is cleared and `FLAG_PARENT_SCRIPT` dropped;
  - storage 8–62, `isFloat`, registry IDs, and the **entire call stack**
    move child→self; the child is deallocated;
  - the op's continuation point is the **child's last frame offset**
    (`return this.frame().offset` after the move).
- **`DEALLOCATE` (80)** and **`DEALLOCATE82` (82)**: both call
  `deallocateWithChildren()` (children first, walking the stor[6] chain and
  pre-capturing each child's own next pointer, then self) →
  `PAUSE_AND_REWIND` (the state ceases to exist; the paused frame never
  resumes). The log description for 82 ("deallocate children") understates —
  self is included (source-verified discrepancy, binary-format doc §6.1).
- **`DEALLOCATE_OTHER` (83)** `index`: deallocate (with children) the state
  at `stor[index]`… precisely `manager.getState(param).deallocateWithChildren()`;
  returns `PAUSE_AND_REWIND` if that was self, `CONTINUE` otherwise.
- **`loadScriptFile(script, entrypoint)`**: clears the call stack, pushes
  `(script, script.getEntry(entrypointIndex))`, clears `FLAG_FILE_NOT_SET`;
  `null` clears the file and sets the flag (the state becomes inert).
- Child states are **invisible to the VM loop** (the `FLAG_CHILD_SCRIPT`
  guard in `executeFrame` and the ticker/renderer gates) — they only run
  through fork/consume operations on the parent, or after being consumed
  into a parent state.

## 9. Crash dump

`ScriptState.dump()` (source-verified): logs the file name, op mnemonic,
byte offset, header param, all 10-or-fewer live params, **all 63 storage
slots** (float-aware), all non-null registry IDs, and — via the in-game
toolchain (`manager.disassemble` + `Translator.translate(..., lineNumbers=true)`)
— a **±5-line disassembly window around the crashing offset**. The dump is
emitted before the fatal `RuntimeException` propagates.

## 10. Lifecycle events (mod interface)

`ScriptLifecycleEvent` (single reusable event object per state) is posted at:
`ALLOCATED`, `PRE_SCRIPT_VM_TICK`/`POST_SCRIPT_VM_TICK` (per frame),
`PRE_TICK_CALLBACK`/`POST_TICK_CALLBACK` (per ticker run),
`PRE_RENDER_CALLBACK`/`POST_RENDER_CALLBACK` (per render callback),
`PRE_DEALLOCATE`/`POST_DEALLOCATE`. These flow through the Mod-Loader event
bus (cross-repo map) — mods observe script lifecycle without touching the VM.

## 11. Error-handling summary

| failure | behavior (source-verified) |
|---|---|
| unknown opcode byte | `OpType.byOpcode` throws → caught by `executeScriptFrame`'s try/catch → `dump()` → `RuntimeException` → game crash |
| unimplemented op (5/6/7/11/13/14/15) | `runOp` default throws `IllegalArgumentException` → same path |
| param count > 10 | `RuntimeException("Too many parameters!")` → same path |
| param type 0x12 | `RuntimeException("Not implemented")` in `parseParam` → same path |
| int div/mod by zero (raw) | `ArithmeticException` (DIV op guards; MOD does not) → same path |
| registry param used as int | `IllegalStateException` → same path |
| `consume` with no child | `RuntimeException("Null command")` |
| `CALL` with null function | **no crash** — `PAUSE_AND_REWIND` stall (silent, logs only) |
| native throws `UnsupportedOperationException` | wrapped `RuntimeException("Script subfunc N error")` |
| `memcpy` out of bounds (retail bug) | caught in the op, logged as a warning, execution continues (reference-specific tolerance, binary-format doc §6.1 op 10) |
| `fork` with no free slot | `RuntimeException("Ran out of script states")` → crash path |

## 12. Reference-specific vs original behavior (Q-17 status)

Source-verified reference behaviors with no established original-game
corroboration (recorded, not asserted as original):

- the crash dump with live disassembly (tooling; the original had none);
- the `memcpy` OOB catch-and-warn (ported around a documented retail script
  bug, GH#230/236/237/240);
- the `paramCount > 10` guard (the original's decode likely had no check —
  Inferred);
- the null-function stall as an explicit `PAUSE_AND_REWIND` (the original
  presumably just executed the (unimplemented) routine — Unresolved);
- per-op `@Method(0x…)` ROM addresses on the op bodies (Ghidra labels;
  consistent with a 1:1 port of the decompiled routines). Precisely **three**
  dispatched op bodies carry **no** anchor: `scriptPush` (op 101,
  `ScriptState.java:1430`), `scriptPop` (op 102, :1435) and `scriptCmp`
  (op 103, :1440) — the CMP-body `scriptCompare` @ 0x8001670c is distinct
  from the unanchored `scriptCmp`; all other op bodies are anchored
  (re-verified by the M3 audit). The port's faithfulness — e.g. exact float
  handling, the `shr` logical-vs-arithmetic choice in helpers vs ops — is
  where a Rust port must decide (M4/M9 territory).

The VM's observable contract (opcode/param/header layout, storage model,
fork/consume semantics, flow control) is fully pinned by the source above and
is the target specification for any reimplementation; which parts of it match
the PSX original is the standing Q-17 (partially advanced here; the original
VM source is not in the reference repositories).

## 13. Evidence index

| Claim area | Source |
|---|---|
| Manager scheduling | `ScriptManager.java` (full read: `tick`, `executeScriptFrame`, `executeScriptTickers`, `executeScriptRenderers`, `findFreeScriptState`, `allocateScriptState`, `meta`, stop/pause, paramPool init) |
| State model | `ScriptState.java` (full reads: flags/javadoc lines 40–300, `loadScriptFile`, `deallocate*`, `fork`, `consumeChild`, `executeFrame` 458–533, op bodies 855–1450, `dump` 1445–1500) |
| Param classes | `Param.java`, `ScriptInlineParam.java` (full), `ScriptTempParam.java`, `ScriptStorageParam.java`, `GameVarParam.java`, `GameVarArrayParam.java`, `ScriptState*RegistryIdParam.java`, `ScriptInlineRegistryIdParam.java` (full reads) |
| Native dispatch | `ScriptState.scriptExecuteSubFunc` (1218–1232), `Scus94491BpeSegment_8004.getScriptFunction` (190–198), `loadEngineStateFunctions` (200–209) |
| Cadence | `EngineStates.java:71`, `SMap.java:4089`, `Menus.java:67/101`, `SMap.java:4019/4168..4379`, R1 frame-rate table |
| Explicit slots | `SMap.java:3263`, `Battle.java:6524/9308/2167`, `Battle.java:4490` (non-player BENT — auto-allocated, corrected by the M3 audit), `ScriptState.java:363` (fork) |
| Lifecycle events | `ScriptLifecycleEvent.java`, `postLifecycleEvent` call sites |

Pins: `Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade`.

## 14. Open items

- **Q-42 — closed** (§2): slots 0–8 are reserved (auto-allocation never
  returns them; explicit allocation owns them); allocation is round-robin
  over 9–107 from the last allocation point, and the per-tick
  `upperBound = 9` reset makes the first allocation of a frame start at
  slot 10, with slot 9 as the fallback when 10–107 are full (wording
  corrected by the M3 audit — it is not lowest-free-first).
- **Q-17 — advanced** (§12): the reference VM's contract is fully pinned;
  original-VM parity remains unverifiable without the PSX binary (the
  `@Method` addresses are the only anchor).
- Q-63 (new): whether the original allowed fork chains deeper than the
  reference's (the stor[6] chain is unbounded in the reference; no depth
  check found) — behavioral equivalence question for M9.
- Q-64 (new): the exact original semantics of `DEALLOCATE82` (self included,
  per the reference; the toolchain's "deallocate children" log text suggests
  the decompiler team was unsure) — asset/external-PSX check.
