# M3-02: The Fate Binary Format

Scope: the byte format of LoD script binaries (the contents of `SECT/DRGN*.BIN/<index>`
files, submap/battle script blobs, and any other `ScriptFile` data), plus the
**mandatory C-01 investigation**: the runtime opcode table
(`Severed-Chains/.../scripting/OpType.java`) versus the toolchain opcode table
(`Script-Recompiler/.../scripting/OpType.java`), including parameter metadata and
error-handling behavior.

Validation categories, per `AGENTS.md`: **source-verified** (established from
reference code), **format-corroborated** (independent spec/implementation/test),
**asset-validated** (real game bytes — none available for M3; the one observed
binary in §9.1 is reference source data, not a retail disc byte stream, and is
labelled accordingly).

## 1. Container: a stream of little-endian 32-bit words

- A script is a flat array of bytes interpreted as **little-endian u32 words**.
  The runtime builds `int[] ops` from the whole buffer
  (`ScriptFile` constructor: `ByteBuffer.wrap(data).order(LITTLE_ENDIAN).asIntBuffer()`,
  source-verified); the toolchain reads words with `MathHelper.get` (LE,
  `State.wordAt`, source-verified). Byte 3 of a word is therefore the **high**
  byte (`State.paramType() = script[currentOffset + 3]`, source-verified).
- The runtime `ScriptFile` exposes `getOp(wordOffset)` and `setOp(wordOffset, value)`
  over the whole buffer — **script code is writable at runtime** (an inline
  parameter's `set()` writes through to the file data; comment in
  `ScriptInlineParam.set`: "Apparently this is possible", source-verified).
- The buffer length is always a multiple of 4 in practice (the runtime divides by 4
  without a remainder check; the toolchain's `State.hasMore()` uses
  `currentOffset / 4 < script.length / 4`, source-verified).

## 2. The entrypoint table

- The **first words** of the file are the entrypoint table: each word is a
  **byte address** (offset within the file) of an entry.
  The runtime converts to a word offset by dividing by 4:
  `ScriptFile.getEntry(index) = ops[index] / 4` (source-verified).
- **The table has no self-describing length.** The runtime caller knows which
  entrypoint to use — the index comes from game data or script parameters, not
  from the file: e.g. the native `scriptSetScriptScript` takes an `entrypoint`
  param and a DEFF script index (`deffManager_800c693c.scripts_2c[deffScriptIndex]`
  → `loadScriptFile(file, entrypoint)`, `SEffe.java:3959`, source-verified), and
  `scriptLoadSameScriptAndJump` always uses entrypoint 0. The disassembler
  **infers** the count: it walks the leading words and treats a word as an
  entrypoint only while its value is a valid code offset
  (`isValidOp`: 4-byte aligned, in `[0x4, length)`, and the word at that offset
  parses as a header, §5); the default cap is 32 (comment: "Most have 0x10, some
  have less, player_combat_script is the only one I've seen with 0x20",
  `Disassembler.getEntrypoints`, source-verified). The CLI exposes
  `--entrypoint-count` to override the cap (source-verified).
- Entrypoints are labelled `ENTRYPOINT_<i>` in disassembly and emitted as
  `entrypoint :ENTRYPOINT_<i>` lines, which the assembler resolves back to the
  label's **byte address** (`Assembler.findEntrypointAddress`, source-verified).
- Duplicate entrypoint values are preserved in `script.allEntrypoints` but deduped
  in `script.entrypoints` for branch probing (source-verified).

## 3. Instruction layout

An instruction is:

```
word 0 (opcode word):  [opcode:8][param_count:8][header_param:16]
word 1..n (params):    one or more words per parameter (§4)
```

- `opcode` = byte 0; `param_count` = byte 1; `header_param` = bytes 2–3 (u16).
  Runtime decode: `OpType.byOpcode(opCommand & 0xff)`,
  `paramCount_14 = opCommand >>> 8 & 0xff`, `opParam_18 = opCommand >>> 16`
  (`ScriptState.executeFrame`, source-verified). Assembler encode:
  `out[entryIndex] = op.type.opcode | op.params.length << 8 | op.headerParam << 16`
  (`Assembler.assemble`, source-verified).
- **param_count ≤ 10** at runtime: `if(this.context.paramCount_14 > 10)
  throw new RuntimeException("Too many parameters!")` (source-verified). The
  `RunningScript.params_20` array is sized 10 (source-verified).
- `header_param` is non-zero only for the five compare ops
  (`WAIT_CMP`, `WAIT_CMP_0`, `JMP_CMP`, `JMP_CMP_0`, `CMP`); the disassembler
  rejects any other op with a non-zero header:
  `if(type.headerParamName == null && opParam != 0) return null`
  (source-verified).

## 4. Parameter words

Each parameter is one or more words. **Byte 3 (high byte) of the first word is the
parameter type**; bytes 0–2 carry type-specific command bytes
(`cmd0 = byte 0`, `cmd1 = byte 1`, `cmd2 = byte 2`; `State.param0/1/2`,
source-verified). The packing is `type << 24 | cmd2 << 16 | cmd1 << 8 | cmd0`
(`Tokenizer.packParam`, source-verified).

The complete type table, cross-referencing the runtime parser
(`ScriptState.parseParam`, the authoritative runtime) and the toolchain enum
(`ParameterType`, with widths used by the disassembler/assembler). Source:
both files, full read (source-verified).

| type | name (toolchain) | width (words) | runtime semantics (source-verified) | textual form (source-verified) |
|---|---|---|---|---|
| 0x00 | IMMEDIATE | 1 | the word **is** the value | `0x…` (only if high byte is 0, §4.2) |
| 0x01 | NEXT_IMMEDIATE | 2 | descriptor word + **next** word is the value | large immediates (§4.2) |
| 0x02 | STORAGE | 1 | `stor[cmd0]` (this state) | `stor[N]` |
| 0x03 | OTHER_OTHER_STORAGE | 1 | `stor[stor[stor[cmd0], cmd1], cmd2]` (two hops) | `stor[stor[stor[A], B], C]` |
| 0x04 | OTHER_STORAGE_OFFSET | 1 | `stor[stor[cmd0], cmd1 + stor[cmd2]]` | `stor[stor[A], B + stor[C]]` |
| 0x05 | GAMEVAR_1 | 1 | `var[cmd0]` (global game variable) | `var[N]` |
| 0x06 | GAMEVAR_2 | 1 | `var[cmd0 + stor[cmd1]]` | `var[N + stor[M]]` |
| 0x07 | GAMEVAR_ARRAY_1 | 1 | `var[cmd0][stor[cmd1]]` | `var[N][stor[M]]` |
| 0x08 | GAMEVAR_ARRAY_2 | 1 | `var[cmd0 + stor[cmd1]][stor[cmd2]]` | `var[N + stor[M]][stor[K]]` |
| 0x09 | INLINE_1 | 1 | value = word at `opOffset + (short)word` — **signed 16-bit relative to the opcode word** | `inl[:label]` / `inl[0xADDR]` |
| 0x0a | INLINE_2 | 1 | value = word at `opOffset + (short)word + stor[cmd2]` | `inl[:label][stor[M]]` |
| 0x0b | INLINE_TABLE_1 | 1 | word at `opOffset + (short)word + W[opOffset + (short)word + stor[cmd2]]` (double indirection, inner offset includes a stor hop) | `inl[A][A][stor[M]]` (both `A`s must be equal — tokenizer rejects otherwise) |
| 0x0c | INLINE_TABLE_2 | 2 | `W[next][W[next][stor[cmd0]] + stor[cmd1]]` where `next` = the descriptor's next word (the pair's second word is the relative base) | translator emits `inl[A[A[stor[B]] + stor[C]]]` — **no tokenizer pattern: one-way** (§8) |
| 0x0d | OTHER_STORAGE | 1 | `stor[stor[cmd0], cmd1 + cmd2]` | translator emits `stor[stor[A], B + C]` — **no tokenizer pattern: one-way** (§8) |
| 0x0e | GAMEVAR_3 | 1 | `var[cmd0 + cmd1]` | `var[N + M]` |
| 0x0f | GAMEVAR_ARRAY_3 | 1 | `var[cmd0][cmd1]` | `var[N][M]` |
| 0x10 | GAMEVAR_ARRAY_4 | 1 | `var[cmd0 + stor[cmd1]][cmd2]` | `var[N + stor[M]][K]` |
| 0x11 | GAMEVAR_ARRAY_5 | 1 | runtime: `var[cmd0 + cmd1][stor[cmd2]]` — comment: "Haven't verified this, afaik it's never used" | translator emits `var[A + B][stor[C]]` — **no tokenizer pattern: one-way** (§8) |
| 0x12 | _12 | 1 | runtime: `throw new RuntimeException("Not implemented")` | none — translator **throws** ("Param type 0x12 not yet supported") |
| 0x13 | INLINE_3 | 1 | word at `opOffset + (short)word + cmd2` | translator emits plain `inl[0xABS]`, which the tokenizer re-parses as **INLINE_1 (0x09)** — value-stable, **byte-changing** (§8) |
| 0x14 | INLINE_TABLE_3 | 1 | word at `opOffset + (short)word + W[opOffset + (short)word + cmd2]` | `inl[A][inl[A + N]]` (also `inl[A + inl[A + N]]`) — stable |
| 0x15 | _15 | 2 | runtime: `W[next][W[next][stor[cmd0]] + cmd1]` (second word = relative base) | none — translator **throws** ("Param type 0x15 not yet supported") |
| 0x16 | _16 | 2 | runtime: `W[next][cmd0 + stor[cmd1]]` (second word = relative base) | none — translator **throws** ("Param type 0x16 not yet supported") |
| 0x17 | INLINE_TABLE_4 | 2 | runtime: `W[next][W[next][cmd0] + cmd1]` (second word = relative base) | translator emits `inl[A[A[B] + C]]` — **no tokenizer pattern: one-way** (§8) |
| 0x20 | REG | 1 | registry-ID slot `reg[cmd0]` of this state | `reg[N]` |
| 0x21 | ID | 1 + (len+3)/4 | registry **string ID**: word 0 = `0x21_00_LL_00` where **LL = char count in byte 2** (`id.length() << 16`); subsequent words hold 4 ASCII chars each, char *i* in byte *i* mod 4 (low byte first); the string is read lazily at `getRegistryId()` time | `id[any.string.with.colon]` (pattern `.*?:.*?`) |
| 0x22 | REG_NULL | 1 | null registry-ID parameter | `null` |
| 0x23 | REG_VAR | 1 | registry-ID slot `reg[stor[cmd0]]` | `reg[stor[N]]` |
| 0x24 | STOR_INL | 2 | descriptor + packed word: `storIndex = W[packed & 0xffff]` (a **word offset** whose *contents* are the stor index); if `cmd0 == 2`: `scriptIndex = W[packed >>> 16]` (contents = script-state index) → other state's storage | `stor[inl[:storLabel]]` / `stor[inl[:scriptLabel], inl[:storLabel]]` |
| 0x25 | GAMEVAR_INL | 2 | `varIndex = W[packed & 0xffff]`; if `cmd0 == 2`: `arrIndex = W[packed >>> 16]` → `var[varIndex][arrIndex]`, else `var[varIndex]` | `var[inl[:varLabel]]` / `var[inl[:varLabel]][inl[:arrLabel]]` |
| 0x26 | REG_INL | 2 | `regIndex = W[packed & 0xffff]`; if `cmd0 == 2`: `scriptIndex = W[packed >>> 16]` → other state's registry slot | `reg[inl[:regLabel]]` / `reg[inl[:scriptLabel], inl[:regLabel]]` |
| 0x27 | INLINE_INL | 2 | `offset = packed & 0xffff` (used directly, word offset); `index = W[packed >>> 16]`; result = word at `offset + index` | `inl[:baseLabel][inl[:indexLabel]]` |
| other | (fallthrough) | 1 | runtime: "Treated as an immediate if not a valid op" — the consumed word is the value | — |

Notes (source-verified):
- Types 0x24–0x27 read their packed word from `commandOffset` (the **next** word
  after the descriptor), i.e. their width is 2 (descriptor + packed word); the
  toolchain `ParameterType` agrees (width 2).
- Types 0x0c/0x15/0x16/0x17 also consume 2 words in **both** implementations
  (the runtime advances `commandOffset` a second time for them; the toolchain
  width table agrees).
- The runtime's per-type resolution happens **eagerly** in `parseParam` (script
  hops are dereferenced immediately; e.g. type 0x3 resolves
  `stor[stor[stor[cmd0], cmd1]]` down to a concrete other-state storage slot at
  parse time). Toolchain parameter resolution is symbolic where possible
  (`ResolvedValue`/`Register`, §6).
- The 0x2x "inl" forms let a parameter point at *another word in the file that
  itself encodes the index* — the mechanism used for label-bound parameters in
  compiled code (the compiler emits `inl[:label]` forms, and the
  0x24–0x27 forms are the "indirect index" variants seen in retail scripts;
  which of these occur in retail code is **Unresolved** without asset validation).

### 4.1 Where jumps resolve

`ScriptInlineParam` (the runtime target of all `inl` forms) resolves its value as
a **word offset into `this.state.frame().file`** — the file of the state the
parameter was created for, which may be a **different script state** (types 0x3,
0x4, 0x24, 0x25, 0x26 with script hops). `jump()` replaces the running script's
top frame with a copy of *that* file and sets the offset (source-verified). So
branches can cross from one script file into another's address space; the
"offset" is always file-relative word offsets.

### 4.2 Immediate disambiguation

- A plain immediate word is only unambiguous when its **high byte is 0**: the
  tokenizer emits values with `(value & 0xff00_0000) != 0` as `NEXT_IMMEDIATE`
  (descriptor `0x01000000` + value word) instead (source-verified).
- The collision is real: an immediate `0x02000005` would otherwise be indistinguishable
  from a `STORAGE` parameter (`stor[5]`), and `0x01000005` from a `NEXT_IMMEDIATE`
  descriptor. The runtime falls back to "the consumed word is the value" for any
  high byte that is not a recognized type, which makes **type bytes 0x01,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x13, 0x14, 0x20, 0x22, 0x23** unusable as the high byte of a plain
  immediate (they would be parsed as those parameter types), and 0x02/0x0c/0x12/
  0x15/0x16/0x17/0x21/0x24/0x25/0x26/0x27 unusable for the same reason plus
  width changes. The conservative tokenizer rule (two-word form whenever bits
  16–23 are set) sidesteps the whole class (source-verified).
- The disassembler performs the inverse: `ParameterType.byOpcode(highByte)`
  returns `IMMEDIATE` for unrecognized types (source-verified) — so a disassembly
  cannot represent a plain immediate whose high byte happens to be a valid type
  byte; such values (if any exist in retail scripts) would be misread, which is a
  latent round-trip hazard (register Q-60).

## 5. The header-validity predicate (code vs data)

`Disassembler.parseHeader(state, offset)` (source-verified) — the exact predicate
the toolchain uses to decide "is this word an instruction?":

1. `offset` within `[0, length-4]`, 4-byte aligned, and `offset >= 0x4`
   (`isValidOp`; the first word of the file can never be code because it is
   entrypoint-table space).
2. `OpType.byOpcode(byte0) != null` — i.e. the opcode is in the **toolchain**
   table (returns null for unknown; the runtime throws, §7).
3. If `CALL`: `header_param < 1024` (function index space).
4. If not `CALL`: **exact arity** — `type.params.length == param_count`.
5. If the op has no header param: `header_param == 0`.

Consequences:
- Any non-instruction word in a script file (data, strings, pointer tables) must
  *not* satisfy this predicate at the positions the disassembler scans, or the
  branch prober will misparse. Data words that do satisfy it are tolerated only
  where they are not probed (inside tables, strings, data tails).
- `CALL` is the wildcard: any param count is accepted, which is why the
  disassembler additionally consults metadata branch attributes for CALL params
  (§6.3).

## 6. The C-01 master opcode table

### 6.0 The two tables, side by side

Runtime table: `Severed-Chains/src/main/java/legend/game/scripting/OpType.java`
(57 entries, opcodes 0–103 except the gaps below). Unknown opcode →
`byOpcode` **throws** `RuntimeException("Invalid opcode %#x")`
(source-verified).

Toolchain table: `Script-Recompiler/src/main/java/org/legendofdragoon/scripting/OpType.java`
(58 entries = runtime's 57 plus `NOOP(100, "noop")`, which the runtime does not
have). Unknown opcode → `byOpcode` **returns null** (source-verified).

Gaps present in **both** tables (no entry at all): 23, 31, 37–39, 45–47, 53–55,
57–63, 69–71, 75–79, 81, 84–85, 90–95, 100 (runtime only), >103. A word with one
of these opcodes is "data" to the toolchain disassembler and a hard
`RuntimeException` if the runtime ever executes it (source-verified).

### 6.1 Per-opcode table

"Runtime behavior" = the implemented op body in `ScriptState.runOp` and its
helpers (source-verified). "Toolchain behavior" = the disassembler's
register-flow model and branch probing for that opcode
(`Disassembler.probeBranch` switch, source-verified). "Discrepancy" = differences
that matter for a Rust port or for disassembly fidelity.

| op | runtime mnemonic | toolchain mnemonic | hdr param | params (name/dir, width) | runtime behavior | toolchain model | discrepancy / notes |
|---|---|---|---|---|---|---|---|
| 0 | `yield` | `yield` | – | none | `PAUSE` (one frame, then next instruction) | not terminal, no probe | none |
| 1 | `rewind` | `rewind` | – | none | `PAUSE_AND_REWIND` (repeat this instruction every frame) | terminal (break) | none |
| 2 | `wait` | `wait` | – | `frames` (in, 1) | if p0 ≠ 0: p0−=1, `PAUSE_AND_REWIND`; else `CONTINUE` | no probe | none. N = N frames |
| 3 | `wait_cmp` | `wait_cmp` | operand | `left` (in), `right` (in) | compare true → `CONTINUE`, else `PAUSE_AND_REWIND` | no probe | none. 10 operators (§6.2) |
| 4 | `wait_cmp` (`WAIT_CMP_0`) | `wait_cmp` (`WAIT_CMP_0`) | operand | `right` (in) | as 3 with left ≡ 0 | as 3 with left ≡ 0 | `_0` form: left operand omitted, constant 0 (§6.3) |
| 5 | `rewind` (`REWIND5`) | `rewind` | – | none | **not in the runtime dispatch switch** → `IllegalArgumentException("Unknown script op")` | terminal (modelled as rewind) | **C-01 discrepancy**: named-but-unimplemented at runtime; original intent Unresolved (§6.4) |
| 6 | `rewind` (`REWIND6`) | `rewind` | – | none | as 5 | terminal | as 5 |
| 7 | `rewind` (`REWIND7`) | `rewind` | – | none | as 5 | terminal | as 5 |
| 8 | `mov` | `mov` | – | `source` (in, 1), `dest` (out, 1) | dest = source (float-aware); `CONTINUE` | copyRegister(dest ← source) | none |
| 9 | `swap_broken` | `swap_broken` | – | `sourceDest` (both, 1), `dest` (out, 1) | **not a swap**: `tmp = p0; p1 = tmp; p0 = tmp` — both params end with p0's value; `CONTINUE`. Source comment: "Pretty sure this is _supposed_ to be a swap"; log description: "Broken swap" | copyRegister(p1 ← p0) only (p0 untouched in the model) | **C-01 key opcode**: retail scripts that used 9 for swapping rely on the broken copy semantics in the original (Inferred: the original PSX VM implemented the same broken behavior — the decompiler observed it; asset-validated-pending). The toolchain models it faithfully (p1 ← p0), but its param metadata marks p0 as `both` while only reading it |
| 10 | `memcpy` | `memcpy` | – | `size` (in), `src` (in), `dest` (in) | copies p0 words p1[i]→p2[i]; **IndexOutOfBoundsException caught + warned** (documented retail OOB bug: NPC controller scripts run memcpy out of bounds when NPCs leave; GH#230/236/237/240) | no probe | reference-specific tolerance: the catch is a reference decision to preserve original observable behavior (Inferred) |
| 11 | `rewind` (`REWIND11`) | `rewind` | – | none | as 5 (unimplemented) | terminal | as 5 |
| 12 | `mov` (`MOV_0`) | `mov` (`MOV_0`) | – | `dest` (out, 1) | dest = 0; `CONTINUE` | setRegister(dest ← 0) | `_0` form of `mov` (source omitted, constant 0), §6.3 |
| 13 | `rewind` (`REWIND13`) | `rewind` | – | none | as 5 | terminal | as 5 |
| 14 | `rewind` (`REWIND14`) | `rewind` | – | none | as 5 | terminal | as 5 |
| 15 | `rewind` (`REWIND15`) | `rewind` | – | none | as 5 | terminal | as 5 |
| 16 | `and` | `and` | – | `and` (in), `operand` (both) | p1 &= p0 | merge p1 ← p1 & p0 | none |
| 17 | `or` | `or` | – | `or` (in), `operand` (both) | p1 \|= p0 | merge | none |
| 18 | `xor` | `xor` | – | `xor` (in), `operand` (both) | p1 ^= p0 | merge | none |
| 19 | `andor` | `andor` | – | `and` (in), `or` (in), `operand` (both) | p2 = (p2 & p0) \| p1 | two merges (p2 &= p0; p2 \|= p1) | none |
| 20 | `not` | `not` | – | `val` (both) | p0 = ~p0 | modify | none |
| 21 | `shl` | `shl` | – | `shift` (in), `val` (both) | p1 <<= p0 | merge | none |
| 22 | `shr` | `shr` | – | `shift` (in), `val` (both) | p1 = p1 **>> p0 (arithmetic** shift; the helper `Param.shr` is logical but the op uses `>>` directly, javadoc "right (arithmetic)") | merge with `>>` (arithmetic) | none (both arithmetic) |
| 24 | `add` | `add` | – | `amount` (in), `operand` (both) | p1 += p0 (overflow allowed) | merge | none |
| 25 | `sub` | `sub` | – | `amount` (in), `operand` (both) | p1 −= p0 | merge | none |
| 26 | `sub_rev` | `sub_rev` | – | `amount` (in), `operand` (both) | p1 = p0 − p1 (float-aware) | merge p1 ← p0 − p1 | none |
| 27 | `incr` | `incr` | – | `operand` (both) | p0 += 1 | modify | none |
| 28 | `decr` | `decr` | – | `operand` (both) | p0 −= 1 | modify | none |
| 29 | `neg` | `neg` | – | `operand` (both) | p0 = −p0 (float-aware) | modify | none |
| 30 | `abs` | `abs` | – | `operand` (both) | p0 = \|p0\| | modify | none |
| 32 | `mul` | `mul` | – | `amount` (in), `operand` (both) | p1 *= p0 (overflow allowed) | merge | none |
| 33 | `div` | `div` | – | `amount` (in), `operand` (both) | p1 /= p0, **zero divisor: p1 = (p1 ≥ 0 ? −1 : 1)**; `CONTINUE` | safeDiv: zero divisor → sign(num) with 0 → 0 | **C-01 behavioral discrepancy**: runtime zero-division result (−1/±1) differs from the toolchain's safeDiv model (+1/0/−1). The disassembler's symbolic model is therefore slightly wrong for div-by-0 paths |
| 34 | `div_rev` | `div_rev` | – | `amount` (in), `operand` (both) | p1 = safeDiv(p0, p1) (float-aware; zero → sign) | safeDiv | none (both use safeDiv) |
| 35 | `mod` | `mod` | – | `amount` (in), `operand` (both) | p1 %= p0 — **int zero divisor throws `ArithmeticException`** (propagates → script crash dump) | zero divisor → 0 | **C-01 behavioral discrepancy**: runtime crashes on int mod-by-0; toolchain models 0. Float mod-by-0 → NaN in both |
| 36 | `mod_rev` | `mod_rev` | – | `amount` (in), `operand` (both) | p1 = p0 % p1 (float-aware; int zero divisor → ArithmeticException via `%`) | zero → 0 | as 35 |
| 40 | `mul_12` | `mul_12` | – | `amount` (in), `operand` (both) | p1 = ((p1 >> 4) * (p0 >> 4)) >> 4 — 12.8 fixed-point scale, "reduces likelihood of overflow at cost of precision" (javadoc) | same formula | none |
| 41 | `div_12` | `div_12` | – | `amount` (in), `operand` (both) | if p0 = 0 → p1 = 0; else p1 = ((p1 << 4) / p0) << 8 | same (toolchain model would div-by-0 → safeDiv 0; equivalent result 0) | effectively none |
| 42 | `div_12_rev` | `div_12_rev` | – | `amount` (in), `operand` (both) | if p1 = 0 → p1 = 0; else p1 = ((p0 << 4) / p1) << 8 | same | none |
| 43 | `mod` (`MOD43`) | `mod` (`MOD43`) | – | `amount` (in), `operand` (both) | identical to 35 (same method `scriptMod`) | identical | none — duplicate encoding of 35 |
| 44 | `mod_rev` (`MOD_REV44`) | `mod_rev` (`MOD_REV44`) | – | `amount` (in), `operand` (both) | identical to 36 | identical | none — duplicate encoding of 36 |
| 48 | `sqrt` | `sqrt` | – | `val` (in), `dest` (out) | p1 = sqrt(p0) (float-aware; `Math.sqrt` on the raw bits) | merge | none. (PSX `sqrt` semantics: Inferred that the original called a float sqrt on the same storage) |
| 49 | `rand` | `rand` | – | `bound` (in), `dest` (out) | p1 = (p0 * simpleRand()) >>> 16 — **unsigned** 16-bit fold | p1 ∈ [0, bound) range register | none. `simpleRand` seed/sequence is engine state (`simpleRandSeed_8004dd44`, initial 3 — R1 audit noted `/ 2` not `>> 1`; see `research/platform/02-execution-model.md`) |
| 50 | `sin_12` | `sin_12` | – | `angle` (in), `dest` (out) | p1 = rsin(p0) — ROM rsin (PSX-degree table, 12.8 output) | (short)(sin(rad(p0)) * 0x1000) | the toolchain uses a float approximation of the table; values match the table's documented intent (format-corroborated by the PSX rsin spec: 4096 = 2π, output scaled by 0x1000) |
| 51 | `cos_12` | `cos_12` | – | `angle` (in), `dest` (out) | p1 = rcos(p0) | as 50 | as 50 |
| 52 | `atan2_12` | `atan2_12` | – | `y` (in), `x` (in), `dest` (out) | p2 = radToPsxDeg(atan2(p0, p1)) (float, with ±0 handling) | same | none |
| 56 | `call` | `call` | **index** (0–1023) | variable count | `getScriptFunction(idx)`: null → **`PAUSE_AND_REWIND` (permanent stall, retrying every frame)**; `UnsupportedOperationException` → `RuntimeException("Script subfunc N error")`; else the native's FlowControl | probes params whose metadata `branch` is jump/subroutine/fork_jump; warns `Skipping CALL … due to unknowable parameter` when a branch param doesn't resolve | the null→stall behavior is the runtime's "unimplemented native" mode (C-01: toolchain has no equivalent — unknown call names are a *compile* error, `UnknownCallException`) |
| 64 | `jmp` | `jmp` | – | `addr` (in) | jump via param (§4.1) | terminal; probes target | none |
| 65 | `jmp_cmp` | `jmp_cmp` | operator | `left` (in), `right` (in), `addr` (in) | compare true → jump to p2; always `CONTINUE` | terminal; probes **both** fall-through (next instruction) and target | none |
| 66 | `jmp_cmp` (`JMP_CMP_0`) | `jmp_cmp` (`JMP_CMP_0`) | operator | `right` (in), `addr` (in) | as 65 with left ≡ 0 | as 65 | `_0` form, §6.3 |
| 67 | `while` | `while` | – | `counter` (both), `addr` (in) | p0 −= 1; if p0 ≠ 0 → jump (do-while) | not terminal, no probe (modelled as a countdown loop) | the name "while" is a misnomer — it is a countdown-jmp; high-level Fate `while` does **not** use this op (§1 of the language doc) |
| 68 | `jmp_table` | `jmp_table` | – | `index` (in), `table` (in) | jump to `table[table[index]]` (double indirection; table words are relative word offsets, §6.5) | terminal; probes table of (sub)tables per metadata | none |
| 72 | `gosub` | `gosub` | – | `addr` (in) | push frame (offset = **next** word), jump | records sub, probes target, **clears all register knowledge** (conservative) | none |
| 73 | `return` | `return` | – | none | pop frame, continue at saved offset | terminal | none |
| 74 | `gosub_table` | `gosub_table` | – | `index` (in), `table` (in) | push frame, jump to `table[table[index]]` | as 68 + sub recording + register clear | none |
| 80 | `deallocate` | `deallocate` | – | none | `deallocateWithChildren()` (self + child chain); `PAUSE_AND_REWIND` | terminal | none |
| 82 | `deallocate` (`DEALLOCATE82`) | `deallocate` (`DEALLOCATE82`) | – | none | **identical implementation** to 80 (`deallocateWithChildren`); `PAUSE_AND_REWIND` | terminal | duplicate semantics; the toolchain's comment log says "deallocate children" for 82 vs "deallocate" for 80 — the log strings differ from the actual code (both call `deallocateWithChildren`); the runtime log description for 82 is inaccurate (source-verified discrepancy in the reference's own log text) |
| 83 | `deallocate_other` | `deallocate_other` | – | `index` (in) | deallocate state `p0` (with children); `PAUSE_AND_REWIND` if p0 is self else `CONTINUE` | no probe | none |
| 86 | `fork` | `fork` | – | `index` (in), `addr` (in), `stor[32] value` (in) | fork state `p0` (child inherits storage 8–62, isFloat, registry IDs, call stack; parent/child flags + stor[5]/[6] linkage); **child** jumps to p1; child.stor[32] = p2; `CONTINUE` | probes fork target (non-terminal) | none. Note the forked script (not the caller) is redirected |
| 87 | `fork_reenter` | `fork_reenter` | – | `index` (in), `entrypoint` (in), `stor[32] value` (in) | as 86 but child's frame offset = `file.getEntry(p1)` (entrypoint of the child's current file) | probes entrypoint (non-terminal) | none |
| 88 | `consume` | `consume` | – | none | `consumeChild()`: adopt the immediate child (child's storage 8–62/isFloat/registry IDs + call stack move into self; child chain relinked; child deallocated); continue at the child's last offset | terminal | none |
| 96 | `debug96` | `debug96` | `?` | 2× `?` (in) | **no-op** (`FUN_800172f4`: returns `CONTINUE`); the toolchain's parameter metadata (`"?", OpParam.in("?"), OpParam.in("?")`) is speculative | no probe | **C-01 discrepancy**: toolchain models 2 parameters + a header param; runtime ignores any params. Evidence of real retail usage/shape is Unresolved (register Q-61) |
| 97 | `debug97` | `debug97` | – | none | no-op; `CONTINUE` | no probe | none |
| 98 | `debug98` | `debug98` | – | 1× `?` (in) | `LOGGER.error("DEBUG: %s", p0)` — debug print of a parameter; `CONTINUE` | no probe | reference adds logging (original was presumably a no-op or ROM debug facility — Inferred) |
| 99 | `depth` | `depth` | – | `dest` (out) | p0 = call-stack depth (frame count) | no probe | none |
| 100 | — (absent) | `noop` | – | none | **not in the runtime table** → `byOpcode` throws if executed | recognized, no probe | **C-01 discrepancy**: toolchain-only opcode. The runtime would crash on a retail script containing 100; the toolchain can disassemble/assemble it. Whether retail scripts use 100 is Unresolved (register Q-61); if they do, the reference runtime has a latent bug |
| 101 | `push` | `push` | – | `val` (in) | push p0 onto the state value stack | no probe | none |
| 102 | `pop` | `pop` | – | `val` (out) | p0 = stack.pop() | no probe | none |
| 103 | `cmp` | `cmp` | comparator | `left` (in), `right` (in), `out` (out) | out = compare(a,b,op) ? 1 : 0 | no probe | only op that may use header 8/9 (§6.2) |

Gaps (no entry in either table): 23, 31, 37–39, 45–47, 53–55, 57–63, 69–71,
75–79, 81, 84–85, 90–95, >103. (Runtime-only gap: 100. Toolchain-only entry:
100.) Source-verified from both `OpType.java` files (full read).

### 6.2 The ten comparison operators (header param)

`ScriptState.scriptCompare` (source-verified; the toolchain's `buildHeaderParam`
and the tokenizer's operator map agree on 0–7):

| op | integer semantics | float semantics (both operands float) | registry-ID semantics |
|---|---|---|---|
| 0 `<=` | a ≤ b | a < b \|\| flEq(a,b) | — |
| 1 `<` | a < b | a < b ∧ ¬flEq | — |
| 2 `==` | a == b | flEq(a,b) | a is null (if b is non-registry 0) / Objects.equals (if both registry) |
| 3 `!=` | a != b | ¬flEq | a is not null / !equals |
| 4 `>` | a > b | a > b ∧ ¬flEq | — (illegal) |
| 5 `>=` | a ≥ b | a > b \|\| flEq | — (illegal) |
| 6 `&` | (a & b) ≠ 0 | falls back to integer bitwise on raw bits | — (illegal) |
| 7 `!&` | (a & b) == 0 | as 6 | — (illegal) |
| 8 `&&` | a ≠ 0 ∧ b ≠ 0 | as 8 | — (illegal; **CMP only**) |
| 9 `\|\|` | a ≠ 0 ∨ b ≠ 0 | as 9 | — (illegal; **CMP only**) |

Rules (source-verified):
- If exactly one operand is a registry-ID param and the other is a plain param
  equal to 0: only operators 2/3 are legal ("null test"); otherwise
  `IllegalArgumentException("Registry IDs can only be compared using == or !=")`.
- If both are registry IDs: only 2/3 (Objects.equals).
- Floats compared with `MathHelper.flEq` (float equality with epsilon handling;
  `research/psx/01-compatibility-boundaries.md` documents the helper).
- Any other operator value → `false` (no throw) for the standard/float paths.

### 6.3 The `_0` opcodes (4, 12, 66) — Q-15 resolution

The `_0` opcodes are **encoding-compact variants** in which the first operand is
the constant 0 and therefore not stored:

- 4 `WAIT_CMP_0` = `WAIT_CMP` (3) with `left` ≡ 0 (one fewer param word).
- 12 `MOV_0` = `MOV` (8) with `source` ≡ 0 (dest = 0; one fewer param word).
- 66 `JMP_CMP_0` = `JMP_CMP` (65) with `left` ≡ 0 (one fewer param word).

Both tables carry the `_0` forms as first-class opcodes with the same mnemonic
as their full forms. The toolchain additionally:
- gives them `commentParamNames` restoring the full param names
  (`left/right`, `source/dest`, `left/right/addr`) so log/comment text stays
  meaningful (`OpType` static block, source-verified);
- **emits them in full form** with the explicit `0x0` first operand in
  disassembly (`Translator.translate`: appends `, 0x0` / ` 0x0,` for
  `WAIT_CMP_0`/`JMP_CMP_0`/`MOV_0`, source-verified);
- **re-detects them on assembly**: `OpType.modifyOp` converts `WAIT_CMP`/`JMP_CMP`/
  `MOV` back to the `_0` form when the first parameter **resolves to constant 0**
  (source-verified). This is the round-trip anchor: binary → text → binary
  reproduces the `_0` encoding exactly, and hand-written `x 0x0, ...` lines
  normalize to the compact form (see §8).

Q-15 ("semantics of opcode 9 and specialized `_0` opcodes 4, 12, 66") is
therefore resolved: 9 = broken swap (§6.1), 4/12/66 = zero-first-operand
encodings (§6.3). Residual: which `_0` forms occur in retail scripts is
asset-validated-pending (they are in both tables, so disassembly of retail
scripts will show them directly when assets arrive).

### 6.4 The named-but-unimplemented "rewind" family (5, 6, 7, 11, 13, 14, 15)

These seven opcodes exist in both tables with mnemonic `rewind` and zero
parameters, but the **runtime dispatch has no case for them**: execution reaches
`default -> throw new IllegalArgumentException("Unknown script op " + op)`
(source-verified, `ScriptState.runOp`). The toolchain disassembler treats them as
terminal like `rewind` (source-verified).

Assessment: the decompiler team evidently recognized these opcode bytes as
rewind-family (naming them `REWIND5` etc.) but did not implement them, either
because (a) retail scripts never take these paths, or (b) the original treated
them identically to opcode 1. Both are **Unresolved** without asset validation;
a Rust port must choose a policy (crash like the reference, or treat as REWIND) —
recorded as register Q-61. Note the disassembler's register model and branch
probing assume "terminal", so a script that executes one of these at runtime in
the reference will **crash with a dump**, while the toolchain will happily
disassemble it.

### 6.5 Pointer-table semantics

- Table entries are **signed word offsets relative to the table start**
  (`Assembler`: `(destAddress - rel.address) / 0x4`; disassembler:
  `destination = tableAddress + state.wordAt(entryAddress) * 0x4`, source-verified).
- `JMP_TABLE`/`GOSUB_TABLE` are **double indirection**: `dest =
  table[table[index]]` — the table holds offsets to words that themselves hold
  offsets (the inner words are resolved by the same relative rule, source-verified
  from `scriptJumpTable`: `p1.array(p1.array(p0).get()).jump(...)`).
- Table-length determination at disassembly time (source-verified,
  `Disassembler.probeTable`/`handlePointerTable`):
  1. explicit override — patch config `table_length, <addr>, <n>` (or CLI
     `-t addr=n`);
  2. register-range — when the index parameter resolves to a numeric range,
     length = range max;
  3. heuristic — continue while the entry is positive and the running address is
     before the earliest destination, or negative and after the latest
     destination ("only allows all positive or all negative pointers (breaks a
     few tables like in Kazas garbage room) but yields much better results
     overall" — source comment);
  4. string tables (CALL params typed `string`): continue over non-op words; a
     word that "looks like an op" is accepted only if a `0xa0ff` string
     terminator is found within 300 words at its destination.
- Tables that overrun other labelled data are truncated, with a warning and label
  cleanup (`Disassembler.disassemble` table-overrun pass, source-verified).

### 6.6 Error-handling behavior: runtime exceptions vs toolchain null-returns

| Situation | Runtime (`Severed-Chains`) | Toolchain (`Script-Recompiler`) |
|---|---|---|
| Unknown opcode byte (absent from table) | `OpType.byOpcode` **throws** `RuntimeException("Invalid opcode %#x")` | `OpType.byOpcode` **returns null**; the disassembler stops the branch probe (treats position as non-code) |
| Op present in table but unimplemented (5/6/7/11/13/14/15) | `byOpcode` succeeds; `runOp` default **throws** `IllegalArgumentException` | disassembles fine (terminal) |
| `CALL` with unregistered function index | `getScriptFunction` returns null → op returns `PAUSE_AND_REWIND` → **the script stalls forever, retrying the same CALL every frame** (logged once at op execution) | `UnknownCallException` at tokenization if the *name* is unknown; numeric indices are not checked against anything at assemble time (metadata only supplies arity/branch info) |
| Native throws `UnsupportedOperationException` | wrapped: `RuntimeException("Script subfunc N error")` | n/a |
| Param count > 10 | `RuntimeException("Too many parameters!")` | the assembler accepts any count for CALL; non-CALL arity is fixed by the table, so >10 is unreachable |
| Non-registry param used where registry ID expected | `IllegalStateException` from `Param` base methods | n/a |
| mod/div by zero (int) | `ArithmeticException` from `%` (mod); DIV has the ±1 rule (§6.1) | safeDiv model: 0 → sign; the disassembler never "executes", so no crash — but the symbolic model diverges from runtime on div-by-0 (§6.1, op 33) |

This is the C-01 "runtime exception vs toolchain null-return" axis, resolved:
the two codebases use different failure modes for the same ambiguity because the
runtime must *execute* (and the original PSX VM's behavior on undefined opcodes
is not knowable — it presumably just ran the bytes as data, hence the
decompiler's "stop probing" model).

## 7. FlowControl (the op result type)

`FlowControl` enum: `CONTINUE`, `PAUSE`, `PAUSE_AND_REWIND` (source-verified).
Execution semantics in `ScriptState.executeFrame` (source-verified):

- `CONTINUE` → fetch the next instruction in the same frame.
- `PAUSE` → stop this script for the frame; **the next instruction runs next frame**
  (opOffset is committed to `commandOffset` before the pause, comment:
  "Returning 1 pauses execution until the next frame").
- `PAUSE_AND_REWIND` → stop for the frame; **the same instruction re-executes
  next frame** (opOffset is not advanced, comment: "Returning anything else
  pauses execution and repeats the same instruction next frame").

The per-frame execution loop runs `do { runOp } while(ret == CONTINUE && !paused)`
— a script with no pausing ops runs to a pause/terminal boundary within a single
frame (source-verified). This is why `WAIT`/`WAIT_CMP`/`YIELD`/`REWIND` are the
timing primitives and why high-level `while` loops need an explicit pausing op
inside the body to avoid single-frame exhaustion.

## 8. Round-trip fidelity (decompile → reassemble)

Conditions under which the toolchain reproduces the input bytes exactly
(source-verified; see also `research/scripting/03-toolchain.md` §8):

1. **Header round trip**: opcode/param-count/header are emitted verbatim
   (§3); non-CALL arity is validated against the table, so a disassembled op
   always reassembles with the same count.
2. **`_0` forms**: translator emits full form with `0x0`; `modifyOp` re-compacts
   (§6.3). Stable.
3. **Labels and inline offsets**: `inl[:label]` resolves to the same
   `(dest - opAddr) / 4 & 0xffff` on reassembly as long as the label layout is
   unchanged (adding/removing words before an inline reference shifts it — the
   reason patches are written against the stripped decompilation and recompiled
   whole).
4. **Jump tables**: `rel :label` lines re-emit `(dest - tableAddr) / 4` per entry;
   table *length* must be the same — if the heuristic that determined the length
   at disassembly time is non-deterministic for a given script, the reassembly
   can differ (this is exactly what the `table_length` config entries in
   `patches/scripts/**/*.config.csv` exist to pin; 5+ shipped configs observed,
   source-verified).
5. **Out-of-range inline pointers**: the disassembler **rewrites** an inline
   pointer whose destination lies past the end of the script to `0` with a
   warning ("Pointer at 0x%x destination is past the end of the script,
   replacing with 0", source-verified) — a recompile of such a script is
   **not** byte-identical (documented divergence).
6. **One-way and type-changing param types** (source-verified: full pattern list
   in `Tokenizer` vs full emission switch in `Translator`):

   | type | translator output | tokenizer re-parse | stability |
   |---|---|---|---|
   | 0x0c INLINE_TABLE_2 | `inl[A[A[stor[B]] + stor[C]]]` | no matching pattern → `RuntimeException("Unknown param")` | **one-way** (recompile fails) |
   | 0x0d OTHER_STORAGE | `stor[stor[A], B + C]` | no matching pattern | **one-way** |
   | 0x11 GAMEVAR_ARRAY_5 | `var[A + B][stor[C]]` | no matching pattern | **one-way** |
   | 0x17 INLINE_TABLE_4 | `inl[A[A[B] + C]]` | no matching pattern | **one-way** |
   | 0x13 INLINE_3 | plain `inl[0xABS]` | re-parsed as **0x09 INLINE_1** | value-stable, **byte-changing** (type byte 0x13 → 0x09) |
   | 0x12, 0x15, 0x16 | translator **throws** (`Param type 0x1x not yet supported`) | no pattern | **cannot decompile** at all |

   Everything else (0x00–0x0b, 0x0e–0x10, 0x14, 0x20–0x27) has a matching
   tokenizer pattern and is byte-stable. The tokenizer's label-rebinding pass
   additionally **throws** `Need to implement label bindings` for
   INLINE_TABLE_2/INLINE_3/INLINE_TABLE_4 if label-based spellings were ever
   constructed programmatically (source-verified).
   Scripts containing any of these types can still be *loaded and executed*
   (the runtime reads raw bytes); they only break the decompile→recompile
   patching flow. Whether retail scripts contain any is asset-validated-pending
   (register Q-60).
7. **Strings**: strings split across labels are emitted as multiple
   `data str[...<noterm>]` segments; reassembly reconstructs the same words
   (source-verified, `Translator` LodString branch + `LodString.fromString`).
8. **Entry points**: the entrypoint count is inferred; if the inference at
   decompile time differs from the original count, the reassembled entry table
   differs. `--entrypoint-count` pins it (source-verified).
9. **Data words**: anything not classified as op/param/table/string is emitted
    as `data 0x…` verbatim (source-verified) — byte-stable by construction.

Known non-round-trippable inputs, in short: out-of-range inlines (rewritten to
0), 0x12/0x15/0x16 params (hard failure), 0x0c/0x0d/0x11/0x17 params (one-way —
recompile fails), 0x13 params (recompiled as 0x09, bytes change), and any
script whose table-length heuristic is unstable without a config pin.

## 9. Encoding examples

Per the milestone requirement, concrete instruction encodings. Examples 9.1 is
reference source data (an 8-byte constant in the reference, **not** a retail
disc byte stream — labelled source-verified, not asset-validated). Examples 9.2–9.8
are **derived byte-for-byte from the source-verified encode/decode rules** of §3–§6
(the tokenizer/assembler/disassembler implementations); they are not claims about
any specific retail script's bytes.

### 9.1 `doNothingScript_8004f650` (observed 8-byte constant)

Raw bytes (little-endian), from `Scus94491BpeSegment_8004.java:186`:

```
offset  hex bytes            word (LE)      meaning
0x00    04 00 00 00          0x00000004     entrypoint table: entry 0 = byte address 0x04
0x04    01 00 00 00          0x00000001     opcode 0x01 REWIND, 0 params, header 0
```

Decoded fields: single entrypoint → offset 0x04; the code is one instruction,
`rewind`. Disassembly (per §2/§5 rules):

```
entrypoint :ENTRYPOINT_0
ENTRYPOINT_0:
rewind
```

Behavior: `REWIND` → `PAUSE_AND_REWIND` → the script re-executes `rewind` every
frame forever — a permanent no-op ("Do nothing") that still ticks as a live
script state. (Source-verified: byte constant + op semantics.)

### 9.2 `wait 5`

```
02 01 00 00    opcode word: op 0x02 WAIT | 1 param | header 0
05 00 00 00    param 0: IMMEDIATE 5 (high byte 0)
```

Decoded: `wait 5`. Disassembly: `wait 5 ; frames`. Behavior: frame 1 p0 5→4
rewind+pause; … frame 5 p0 1→0; frame 6 p0==0 → CONTINUE. Total: 5 paused frames.
High-level Fate `wait 5;` compiles with the copy-to-temp special case
(`mov 5, _expr_0; wait _expr_0`), so the *compiled* form of `wait 5;` is instead
`02 01 00 00` + `01 00 00 02` (stor[_expr_0]) + `08 01 00 00`… — see the language
doc §2.5 for the emission; the 8-byte form above is the hand-written
low-level spelling.

### 9.3 `mov stor[18], 0x1`

```
08 02 00 00    op 0x08 MOV | 2 params | header 0
01 00 00 00    param 0 (source): IMMEDIATE 1
12 00 00 02    param 1 (dest):   STORAGE cmd0=18 (type 0x02)
```

Disassembly: `mov 0x1, stor[18] ; source, dest`. Behavior: `stor[18] = 1`.

### 9.4 `call 322, stor[0], 0x2`

Text taken verbatim from a shipped patch (`patches/scripts/DRGN1/20.diff`,
stripped-form decompilation — an observed artifact of the patch ecosystem); bytes
derived:

```
38 02 42 01    op 0x38 CALL | 2 params | header 0x0142 (322)
00 00 00 02    param 0: STORAGE cmd0=0
02 00 00 00    param 1: IMMEDIATE 2
```

Decoded fields: opcode 56 (CALL), param_count 2, header_param 322 (function
index), params [stor[0], 2]. Disassembly: `call 322, stor[0], 0x2 ; <param
names per metadata row 322>`. Behavior: invoke native function 322 from the
global or per-state table; if unregistered → permanent `PAUSE_AND_REWIND` stall
(§6.6).

### 9.5 `wait_cmp ==, 0x0, stor[4]` (opcode 4, the `_0` form)

```
04 01 02 00    op 0x04 WAIT_CMP_0 | 1 param | header 0x0002 (operator 2 = ==)
04 00 00 02    param 0 (right): STORAGE cmd0=4
```

Disassembly: `wait_cmp ==, 0x0, stor[4] ; operand, left, right` (translator
re-expands the `_0` form). Behavior: while `stor[4] != 0` → `PAUSE_AND_REWIND`;
continues once `stor[4] == 0`. Re-assembly of that line: tokenizer builds
`WAIT_CMP(3)` with params [0, stor[4]] + header 2; `modifyOp` sees param0
resolves to constant 0 → converts to `WAIT_CMP_0(4)` dropping the 0 → identical
bytes (§6.3).

### 9.6 Two-word immediate (`NEXT_IMMEDIATE`)

`mov 0x01000005, stor[3]` — the value 0x01000005 has bits 16–23 set (0x01), so
the tokenizer must use the two-word form (a bare word would be misread as
`NEXT_IMMEDIATE` descriptor/type space):

```
08 02 00 00    op 0x08 MOV | 2 params | header 0
01 00 00 00    param 0: NEXT_IMMEDIATE descriptor (type 0x01)
05 00 00 01    param 0 continued: value 0x01000005
03 00 00 02    param 1: STORAGE cmd0=3
```

Disassembly: `mov 0x1000005, stor[3]` (the disassembler's `NEXT_IMMEDIATE` case
reads `wordAt(currentOffset + 4)`, source-verified) — stable round trip.

### 9.7 `jmp_cmp ==, 0x0, stor[5], :END` (opcode 66)

Assume `:END` is at file byte offset `opAddr + 0x40`:

```
42 02 02 00    op 0x42 JMP_CMP_0 | 2 params | header 0x0002 (==)
10 00 00 09    param 0 (addr): INLINE_1 — word 0x09000010, rel = (0x40)/4 = 0x10 words
05 00 00 02    param 1 (right): STORAGE cmd0=5
```
`jmp_cmp ==, 0x0, stor[5], inl[:LABEL_n] ; operand, left, right, addr`.
Behavior: if `0 == stor[5]` jump to END, else fall through; always CONTINUE.

### 9.8 `data str[HI]` and `data str[<line>HI]` (string encoding)

Char codes: `H` = 0x27, `I` = 0x28, terminator 0xa0ff (language doc §5). Each
word packs char0 in the low 16 bits and char1 in the high 16 bits.

`data str[HI]` (codes: H, I, term):

```
27 00 28 00    word 0: 0x00280027 — char0 = 'H' (0x27), char1 = 'I' (0x28)
ff a0 00 00    word 1: 0x0000a0ff — terminator
```

`data str[<line>HI]` (codes in source order: 0xa1ff, H, I, plus terminator):

```
ff a1 27 00    word 0: 0x0027a1ff — char0 = 0xa1ff (<line>), char1 = 'H' (0x27)
28 00 ff a0    word 1: 0xa0ff0028 — char0 = 'I' (0x28), char1 = 0xa0ff (terminator)
```

Disassembly renders `str[<line>HI]` (the `LodString.CONTROLS` reverse path,
source-verified).

## 10. Cross-references and open items

- Runtime scheduling, storage model, param classes: `research/scripting/05-runtime-interpreter.md`.
- Toolchain pipeline and patch round-trip in practice: `research/scripting/03-toolchain.md`.
- Native function indices referenced by `call`: `research/scripting/06-native-script-functions.md`.
- Q-15 resolved (§6.3, §6.1 op 9). C-01 resolved with the discrepancies preserved
  (§6.1, §6.4, §6.6): toolchain-only `NOOP(100)`, runtime-unimplemented 5/6/7/11/13/14/15,
  div-by-0 model mismatch, mod-by-0 crash, CALL-null stall vs compile error.
- Q-60 (new): whether retail scripts use param types 0x11/0x12/0x15/0x16 or
  immediates with colliding high bytes (asset-validated-pending).
- Q-61 (new): original-VM behavior of opcodes 5/6/7/11/13/14/15 and 100; retail
  usage of the `debug96/97/98` shapes (asset-validated-pending / external PSX
  evidence).
- Q-59 (from the language doc): retail string/control-code usage.

## 11. Evidence index

| Claim area | Source |
|---|---|
| Word stream / LE | `Severed-Chains/.../scripting/ScriptFile.java` (full), `Script-Recompiler/.../State.java` (full), `MathHelper.get` (both repos) |
| Entrypoint table | `ScriptFile.getEntry`, `Disassembler.getEntrypoints`, `Assembler.findEntrypointAddress`, `Shell` `--entrypoint-count` |
| Opcode word layout | `ScriptState.executeFrame` (decode), `Assembler.assemble` (encode) |
| Param types | `ScriptState.parseParam` (all 0x0–0x27 cases), `ParameterType` enum (widths), `Tokenizer.parseParam`/`packParam` |
| Immediate rule | `Tokenizer.parseParam` (`(value & 0xff00_0000) == 0`), `Disassembler.parseParamValue` (IMMEDIATE/NEXT_IMMEDIATE) |
| Header predicate | `Disassembler.parseHeader` / `isValidOp` |
| Master table | both `OpType.java` files (full read), `ScriptState.runOp` + op bodies (full read of lines 458–1522), `Disassembler.probeBranch` switch (full read) |
| `_0` forms | `OpType.modifyOp` (toolchain), `Translator.translate` (`_0` expansion), runtime op bodies `scriptCompare0`/`scriptSetZero`/`scriptConditionalJump0` |
| CMP operators | `ScriptState.scriptCompare`, `Translator.buildHeaderParam`, `Tokenizer.parseParam` operator map |
| Tables | `Assembler` PointerTable branch, `Disassembler.probeTable`/`handlePointerTable`/`probeTableOfBranches`/`probeTableOfTables`, `ScriptPatcher.getPatchConfigs` (config pinning), shipped `patches/scripts/**/*.config.csv` (5+ observed) |
| FlowControl | `FlowControl.java`, `ScriptState.executeFrame` loop + comments |
| Errors | `OpType.byOpcode` (both), `runOp` default, `scriptExecuteSubFunc`, `Patcher`/`Tokenizer` exceptions |
| Shipped diff example | `patches/scripts/DRGN1/20.diff` (text artifact; bytes derived) |

Pins: `Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511`,
`Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade`.
