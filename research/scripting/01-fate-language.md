# M3-01: The Fate Language

Scope: the scripting language used by *The Legend of Dragoon* (PSX, SCUS94491) in this
ecosystem. "Fate" denotes two related textual languages that exist as a
compile-to-assembly pipeline:

1. **High-level Fate** — a C-like language defined by an ANTLR grammar, lowered by
   `FateCompilerVisitor` into low-level Fate.
2. **Low-level Fate** (assembly) — a line-oriented format consumed by the
   `Tokenizer` and emitted by the `Disassembler`/`Translator`. It is the canonical
   textual form of a script binary; patches are written against it.

All claims in this document are **source-verified** against the pinned reference
repositories (see §8) unless labelled otherwise. Nothing here is **asset-validated**:
no retail script bytes were available during this investigation, so language
features are established from the compiler/disassembler sources, not from observed
retail scripts.

Validation categories used in this document, per `AGENTS.md`:
- **source-verified** — behavior established from reference source code.
- **format-corroborated** — corroborated by an independent spec, implementation, or test.
- **asset-validated** — confirmed against real game bytes (none for M3 so far).

## 1. Architecture of the language toolchain

```
high-level Fate (.fate)                low-level Fate (assembly)        binary
+----------------------+     +-----------------------------+     +------------------+
| FateLexer /          |     | Translator (disasm output)  |     | ScriptFile       |
| FateParser (ANTLR)   | --> | Tokenizer                   | --> | (little-endian   |
| FateCompilerVisitor  |     | (text -> tokens)            |     |  u32 word stream)|
+----------------------+     +-------------+---------------+     +------------------+
                                  ^    |                                ^    |
                                  |    v                                |    v
                                  + Disassembler (binary -> tokens) <---+ Assembler
```

- High-level Fate is parsed by generated ANTLR v4 classes from
  `FateLexer.g4` / `FateParser.g4` (`Script-Recompiler/src/main/antlr/.../compiler/`,
  source-verified). `FateCompiler.parse` runs `parser.body()` over a char stream
  (`FateCompiler.java`, `parse`, source-verified).
- `FateCompiler.compile(includePaths, source, labelPrefix, errors)` performs:
  ANTLR parse → `FatePreprocessor` (collects function definitions and `include`
  directives) → recursive resolution of included `.fate` files
  (`getFunctionsFromIncludes`) → `FateCompilerVisitor` code generation →
  `FateContext.updateVariableNames()` (function-local variable names are prefixed
  with the function name: `var.name = this.name + '_' + var.name`,
  `FateFunction.updateVariableNames`, source-verified) → `updateLabelNames(labelPrefix)`
  → `fate.compile()` which serializes the op list to low-level Fate text
  (source-verified).
- The low-level text is then tokenized (`Tokenizer.tokenize`) and assembled
  (`Assembler.assemble`) into the binary word stream (source-verified).
- The decompiler direction is `Disassembler.disassemble` (binary → token `Script`)
  → `Translator.translate` (token `Script` → low-level text) (source-verified).
- The game runtime loads low-level Fate through the same `Tokenizer`/`Assembler`
  (`ScriptManager.assemble`, `Script-Recompiler` classes reused in-game;
  `Severed-Chains/src/main/java/legend/game/scripting/ScriptManager.java:452-468`,
  source-verified) and compiles high-level Fate through the same
  `FateCompiler` (`ScriptManager.compile`, lines 439-451, source-verified).

The intermediate serialization format produced by `FateContext` is exactly the
low-level Fate line format: each `FateOp` serializes as
`<mnemonic> <param1>, <param2>, ...` (`FateOp.toString`, source-verified). Note
that header parameters (see §4.3) are carried as the first textual parameter in
this intermediate form and are extracted by the `Tokenizer`.

## 2. High-level Fate

### 2.1 File structure

Grammar (`FateParser.g4`, source-verified):

```
body : (entrypoint | global | function | include)* ;
entrypoint : ENTRYPOINT IDENTIFIER TERM ;        // entrypoint <fn>;
function : DEF IDENTIFIER identifier_list block ;// def name(a, b) { ... }
include : INCLUDE INCLUDE_FILE TERM ;            // include file.fate;
global : VAR IDENTIFIER ASSIGN (NUMBER | const_array_initializer) TERM ;
```

- A file is a sequence of top-level items: `entrypoint <name>;`, `def <name>(...) { ... }`,
  `var <NAME> = ...;` globals, and `include <file>;`.
- `entrypoint <fn>;` requires `<fn>` to be a defined `def` function; otherwise the
  compiler records `entrypoint function <fn> not found`
  (`FateCompilerVisitor.visitEntrypoint`, source-verified). The entry name is added
  to `FateContext.entrypoints` and later emitted as a low-level `entrypoint :label`
  line.
- `include` is processed in two stages: the preprocessor records the file (and
  recursively collects its function definitions so cross-file calls resolve), and
  the visitor emits a low-level `#include <file>` line at that position
  (`visitInclude`, source-verified). The `INCLUDE` lexer token uses an ANTLR push-mode
  to capture the file name up to `;` or end of line (`FateLexer.g4`
  `INCLUDE_FILE : ~[;\r\n]+ -> popMode`, source-verified).

### 2.2 Variables, declarations, assignments

```
statement : (declaration | assignment | postfix | augmented_assignment | call
             | return_ | CONTINUE | BREAK) TERM ;
declaration : VAR (IDENTIFIER | identifier_list) (ASSIGN (expression | array_initializer))? ;
assignment : (assignable | assignable_list) ASSIGN (expression | array_initializer) ;
array_initializer : LBRACKET expression (COMMA expression)* RBRACKET ;
```

- Local variables are introduced with `var x;`, `var x = <expr>;`,
  `var (a, b) = ...;`, or `var x = [e1, e2, ...];`. Redeclaring a name in scope is a
  compile error (`variable <name> already defined in scope`,
  `FateCompilerVisitor.visitDeclaration`, source-verified).
- `var x = [e1, ..., en];` (array initializer) creates a variable of **length n**
  and emits a loop: for each element i, `mov i, _expr_k; mov e_i, x[_expr_k]`
  (`emitArrayAssignment`, source-verified). The variable is therefore a block of n
  data words, and references to it are inline-array references (`inl[:label]` +
  word offset). List assignment `var (a,b) = [e1,e2]` applies this per variable
  (source-verified).
- Plain declaration `var x = e;` and assignment `x = e;` lower to
  `mov <e>, <dest>` — the expression is evaluated once; list assignment
  `(a, b) = e;` where `e` is a single value assigns the same value to both;
  where `e` is a multi-value expression (a function call's return value list) the
  values are zipped (`emitAssignment`, source-verified).
- Array initializers may only be assigned to plain variables; using
  `stor[...]`/`var[...]`/`reg[...]` targets is an error
  (`only regular variables can be used with array initializers`,
  `FateCompilerVisitor.visitAssignment`, source-verified).
- **Globals** (top-level `var NAME = ...;`) lower to `data` entries (not code):
  - `var X = 123;` → `X: data 123` (a single data word labelled `X`).
  - `var X = [1, 2, 3];` → `X: data 1` / `data 2` / `data 3` (label on the first
    word; the variable has length 3).
  - `var X = ["a", "b"];` → a **relative pointer table**: `X: rel :X__entry_0` /
    `rel :X__entry_1`, followed by `X__entry_0: data str[a]`,
    `X__entry_1: data str[b]` (the variable is marked `isRel = true`,
    `visitGlobal`, source-verified). Array lookups into a rel global store the
    index into `stor[62]` and index the table through it
    (`FateCompilerVisitor.visitExpression`, `array_lookup` branch,
    source-verified).

### 2.3 Control flow

```
if_ : IF LPAREN expression RPAREN block (ELSE if_ | ELSE block)? ;
while_ : WHILE LPAREN expression RPAREN block ;
do_while : DO block WHILE LPAREN expression RPAREN TERM ;
```

Code generation (`FateCompilerVisitor`, source-verified):

- `if (c) { B }` →
  ```
  jmp_cmp ==, 0, <c>, _label_i     ; false -> skip
  <B>
  _label_i:
  ```
- `if (c) { B1 } else { B2 }` adds an unconditional `jmp` over the else block after
  B1; else-if chains are handled by recursing on `ELSE if_` (the emitted skip label
  of the outer if becomes the head of the inner if).
- `while (c) { B }` →
  ```
  _label_i:
  jmp_cmp ==, 0, <c>, _label_j     ; false -> exit
  <B>
  jmp _label_i
  _label_j:
  ```
- `do { B } while (c);` →
  ```
  _label_i:
  <B>
  jmp_cmp !=, 0, <c>, _label_j     ; false -> exit
  _label_j:
  ```
- `continue;` → `jmp` to the current loop's start label; `break;` → `jmp` to the
  loop's end label. Both are errors outside a loop (`continue is only valid inside
  of loop` / `break is only valid inside of loop`, `visitStatement`,
  source-verified).
- Generated labels are named `_label_<n>` sequentially (`FateContext.getLabel`,
  source-verified).

Important: **there is no `WAIT`-based loop construct and no implicit per-iteration
pause.** A high-level `while`/`do-while` loop body executes entirely within one
script tick unless the body itself contains a pausing op (a `wait`, a `yield`
called as an ASM op, or an engine call that returns `PAUSE*`). See
`research/scripting/05-runtime-interpreter.md` §4 for FlowControl semantics.

### 2.4 Expressions

Precedence (lowest to highest, from `FateParser.g4` alternatives order,
source-verified):

```
||  &&  ^  |  &  == !=  < > <= >=  << >>  + -  * / %  unary (+ - ! ~)  value
```

- Every binary and unary operation is lowered through a fresh temporary variable
  `_expr_<n>` (`FateCompilerVisitor.getExprVar`, `exprVarIndex` shared per
  compilation, source-verified). For `a OP b` the emission is
  `mov <a>, _expr_n; <op> <b>, _expr_n` (for arithmetic/bitwise ops) or
  `cmp <sym>, <a>, <b>, _expr_n` (for comparisons and `&&`/`||`).
- Evaluation order is left-to-right as written: the left operand is fully
  evaluated (and moved to the temp) before the right operand is evaluated
  (source-verified, from the emission order in each case).
- **`&&` and `||` are not short-circuit.** They lower to a single `CMP` op with
  header operators 8 (`&&`) / 9 (`||`); both operands are evaluated
  (`visitExpression`, `ANDA/ORA/ANDC/ORC` cases; the `CMP` op's 10 operators are
  defined in `research/scripting/02-fate-binary-format.md` §6, source-verified).
- `!expr` lowers to `cmp ==, <expr>, 0, _expr_n` (not a bitwise NOT), while `~expr`
  lowers to `not _expr_n` after `mov` (source-verified).
- Multi-value results: a function call used in an expression returns a
  `FateValueList`; list assignment and `return (a, b);` consume the list. A
  single-value context with a multi-value call is a compile error
  (source-verified from `visitExpression`'s call branch and `visitAssignment`).
- ASM ops and engine calls cannot appear inside expressions: `ASM ops and engine
  calls cannot be used in expressions` (the call branch returns null and the error
  is recorded, source-verified).

### 2.5 Calls

```
call : IDENTIFIER (SCOPE IDENTIFIER)? expression_or_string_list ;
```

Three call forms, dispatched in `FateCompilerVisitor.visitCall` (source-verified):

1. **ASM op** — if the name matches a low-level mnemonic (`OpType.byName`,
   case-insensitive), the call emits that op directly with exactly
   `opType.params.length` parameters (arity mismatch is an error:
   `expected N params, got M`). Strings cannot be passed to ASM ops.
   Special case: `wait(n)` first copies its argument — `mov n, _expr_k; wait
   _expr_k` — because the runtime `WAIT` decrements its parameter register in
   place, so reusing the same storage slot across `wait` calls would corrupt it
   (comment in source, source-verified).
2. **Engine call** — `pkg::name(args...)` (the `SCOPE` form). The name is looked
   up in the metadata method table (`meta.methods`, order = row order of
   `descriptions.csv`, see `research/scripting/04-meta-system.md`); the call lowers
   to `call pkg::name, <args...>` (the name is kept in the intermediate form and
   resolved to the numeric index by the `Tokenizer`; unknown name → `unknown
   engine call`). Arity is checked against the metadata parameter list. String
   arguments are materialized as temporary variables holding `str[...]` values
   (source-verified).
3. **Function call** — `fn(args...)`. Arguments are evaluated left-to-right and
   `PUSH`ed onto the script stack; then `gosub :fn` is emitted. The return values
   are popped back in reverse order into temporaries: for a function returning k
   values the call site emits k `pop _expr_n` ops after the `gosub`
   (source-verified). Strings cannot be passed as function parameters.

`return (a, b, ...);` pushes the values (left-to-right) then emits `return`. All
returns of a function must have the same arity, otherwise `function "name" return
lists are not the same size` (source-verified). Functions have no explicit return
type; a `return;` with no values is allowed (and then all returns must be
empty).

### 2.6 Operands

```
assignable : IDENTIFIER | stor | gamevar | reg ;
stor : STOR LBRACKET (expression COMMA)? expression RBRACKET ;  // stor[i] or stor[script, i]
gamevar : VAR LBRACKET expression RBRACKET (LBRACKET expression RBRACKET)? ; // var[i][j]
reg : REG LBRACKET expression RBRACKET ;                        // reg[i]
id : IDENTIFIER COLON IDENTIFIER ;                              // enum: EnumName:VALUE
array_lookup : IDENTIFIER LBRACKET expression RBRACKET ;
```

- `stor[i]` and `stor[script, i]` refer to script-state storage (see
  `research/scripting/05-runtime-interpreter.md` §5).
- `var[i]` / `var[i][j]` refer to the global "game variable" space implemented by
  the runtime as a dispatch over named engine globals
  (`GameVarParam` / `GameVarArrayParam`, source-verified; see
  `research/scripting/05-runtime-interpreter.md` §7).
- `reg[i]` refers to a script-state registry-ID slot (mod registry objects
  attached to the script, source-verified).
- `EnumName:VALUE` (the `id` rule) resolves to the numeric index of `VALUE` within
  the enum defined in metadata (`FateRegId` carries the `Name:VALUE` string; the
  `Tokenizer` resolves it to an immediate via `meta.enums`, source-verified).
- `NAME[i]` is an array lookup into a local/global variable (word offset into the
  variable's data block) — source-verified.

### 2.7 Literals and strings

- Numbers: decimal or `0x` hex, optionally signed (`NUMBER : '-'?([0-9]+|'0x'[a-f0-9]+)`,
  source-verified).
- Strings: double-quoted with escapes `\\`, `\"`, `\n` (`STRING` rule,
  source-verified). In high-level Fate strings may only appear as engine-call
  arguments (materialized as `str[...]` data temporaries, §2.5) and in const array
  globals (§2.2). They are encoded into the binary with the LoD text charset and
  control codes documented in §5.

### 2.8 Comments and whitespace

- `//` line comments, `/* */` block comments, skipped by the lexer
  (source-verified). Whitespace = spaces and newlines (tabs are **not** in the
  whitespace set — `WHITESPACE : [ \n] -> skip`, source-verified).

### 2.9 Examples (high-level → low-level)

The following show representative lowerings. The exact byte encodings of the
resulting low-level lines are worked out in `research/scripting/02-fate-binary-format.md`
§9 (encoding examples).

```
def check_flag(f) {
  if (var[5] >= 100) {
    return 1;
  }
  return 0;
}
entrypoint check_flag;
```

lowers to (labels renumbered; `var[5]` is the gold variable):

```
check_flag_f:
data 0
check_flag:
pop check_flag_f
_label_0:
cmp >=, var[5], 100, _expr_0
jmp_cmp ==, 0, _expr_0, _label_1
push 1
return
_label_1:
push 0
return
entrypoint :check_flag
```

(source-verified structure: function prologue emits the local data block then the
function label; `pop check_flag_f` pops the single argument — arguments are popped
in reverse order at function entry, `FateFunction.toString` +
`FateCompilerVisitor.visitFunction`, source-verified.)

## 3. Low-level Fate (assembly)

### 3.1 Line grammar

The `Tokenizer` consumes lines (comments after `;` removed, blank lines dropped,
leading address prefixes `^[a-f0-9]+\s+` allowed and ignored,
`Tokenizer.splitSource`/`LINE_PATTERN`, source-verified):

| Line | Token produced | Notes |
|---|---|---|
| `entrypoint :label` | `Entrypoint` | one per entrypoint; collected **first**, in file order; entrypoint table addresses are `0x0, 0x4, 0x8, ...` (source-verified, `tokenize` entrypoint pre-pass) |
| `label:` | label binding | `\w+:`; duplicate → `DuplicateLabelException` (source-verified) |
| `data 0x1234` | `Data` | raw u32 word, hex or decimal (source-verified) |
| `data str[HI<line>YOU]` | `LodString` | charset + control encoding, §5; `<noterm>` variant omits the terminator (source-verified) |
| `rel :label` | `PointerTable` (1 entry) | one line per table entry; consecutive `rel` lines form a jump/pointer table (source-verified) |
| `<mnemonic> <params...>` | `Op` | mnemonic matched case-insensitively against the toolchain `OpType.byName` (source-verified) |
| `#include file` | spliced text | resolved against include dirs; `.fate` files are run through `FateCompiler` first with the file name (sans extension) as label prefix (source-verified) |

An op line with no parameters is valid for zero-parameter ops. Anything that
matches no form is an error: `Invalid line "<line>"` (source-verified).

### 3.2 Parameter syntax (complete set)

`Tokenizer.parseParam` (source-verified) accepts, per parameter:

| Syntax | ParameterType | Encoded as (word(s)) |
|---|---|---|
| `0xNNNNNNNN` (value with top byte 0) | `IMMEDIATE` (0x0) | one word = the value |
| number with bits 16–23 nonzero | `NEXT_IMMEDIATE` (0x1) | descriptor word `0x01000000` + value word |
| `stor[N]` | `STORAGE` (0x2) | `0x02_00_00_NN` |
| `stor[stor[A], B]` (OTHER_STORAGE) | not tokenizable in shipped form | see §4 of the binary-format doc (type 0xd; translator emits the odd `stor[stor[A], B + ...]` form) |
| `stor[stor[stor[A], B], C]` | `OTHER_OTHER_STORAGE` (0x3) | `0x03_CC_BB_AA` |
| `stor[stor[A], B + stor[C]]` | `OTHER_STORAGE_OFFSET` (0x4) | `0x04_CC_BB_AA` |
| `var[N]` | `GAMEVAR_1` (0x5) | `0x05_00_00_NN` |
| `var[N + stor[M]]` | `GAMEVAR_2` (0x6) | `0x06_MM_00_NN` |
| `var[N][stor[M]]` | `GAMEVAR_ARRAY_1` (0x7) | `0x07_MM_00_NN` |
| `var[N + stor[M]][stor[K]]` | `GAMEVAR_ARRAY_2` (0x8) | `0x08_KK_MM_NN` |
| `inl[0xADDR]` / `inl[:label]` | `INLINE_1` (0x9) | `0x09_00_00_RR` where `RR = (dest - opAddr) / 4 & 0xffff` (signed 16-bit relative to the **op** address) |
| `inl[0xADDR[stor[M]]]` / `inl[:label[stor[M]]]` | `INLINE_2` (0xa) | `0x0A_MM_00_RR` |
| `inl[0xADDR[0xADDR[stor[M]]]]` (both addresses must match) | `INLINE_TABLE_1` (0xb) | `0x0B_MM_00_RR` |
| `inl[0xADDR[0xADDR[stor[A]] + stor[B]]]` | `INLINE_TABLE_2` (0xc) | 2 words (descriptor form, bound at assembly via resolver) |
| `var[N + M]` | `GAMEVAR_3` (0xe) | `0x0E_00_MM_NN` |
| `var[N][M]` | `GAMEVAR_ARRAY_3` (0xf) | `0x0F_MM_00_NN` |
| `var[N + stor[M]][K]` | `GAMEVAR_ARRAY_4` (0x10) | `0x10_KK_MM_NN` |
| `inl[0xADDR[0xADDR + 0xNN]]` (addresses must match) | `INLINE_TABLE_3` (0x14) | `0x14_NN_00_RR` |
| `inl[0xADDR[0xADDR + 0xNN]]` (INLINE_3 spelling) | `INLINE_3` (0x13) | `0x13_NN_00_RR` (see binary doc §4 for the runtime distinction) |
| `reg[N]` | `REG` (0x20) | `0x20_00_00_NN` |
| `reg[stor[N]]` | `REG_VAR` (0x23) | `0x23_00_00_NN` |
| `null` | `REG_NULL` (0x22) | `0x22000000` |
| `id[package.Class]` | `ID` (0x21) | 1 + (len+3)/4 words: header `0x21_LL_00_00` (L = string length) + packed 4 chars/word, low byte first |
| `stor[inl[0xA]]`, `stor[inl[0xB], inl[0xA]]` | `STOR_INL` (0x24) | 2 words; word1 low 16 = word offset of stor index, high 16 = word offset of script index (if 2-arg) |
| `var[inl[0xA]]`, `var[inl[0xB]][inl[0xA]]` | `GAMEVAR_INL` (0x25) | 2 words, same packing |
| `reg[inl[0xA]]`, `reg[inl[0xB], inl[0xA]]` | `REG_INL` (0x26) | 2 words, same packing |
| `inl[inl[0xB], inl[0xA]]` | `INLINE_INL` (0x27) | 2 words, same packing |
| `pkg::name` (CALL parameter) | `IMMEDIATE` | metadata method index |
| enum value (CALL parameter whose meta type is an enum class) | `IMMEDIATE` | enum index; unknown value → error (source-verified) |

Notes (source-verified):
- Parameter splitting is comma-based with `[`/`]` nesting (a comma inside brackets
  does not split) — `splitParameters` (source-verified).
- `inl[...]` addresses may be given as absolute hex file addresses; the tokenizer
  converts them to relative word offsets from the op (`(value - opAddress) / 0x4`),
  and label forms are bound after the whole script is tokenized
  (`address - op.address) / 0x4 & 0xffff`) (source-verified).
- Types 0x12, 0x15, 0x16, 0x11 (`GAMEVAR_ARRAY_5`), and the "INLINE_4/5/7"
  positions are marked as unsupported in the tokenizer (`// INLINE_4`,
  `// GAMEVAR_ARRAY_5`, `// _12`, etc.) — the tokenizers has no syntax for them,
  and the `Translator` throws `Param type 0x12/0x15/0x16 not yet supported` if a
  disassembled script contains them (source-verified). See
  `research/scripting/02-fate-binary-format.md` §8 for the round-trip consequences.

### 3.3 Header parameters and the `_0` op forms

- Five ops carry a 16-bit **header parameter** in the opcode word (bytes 2–3):
  `WAIT_CMP` (operand), `JMP_CMP` (operator), `CMP` (comparator), and the
  `_0` variants `WAIT_CMP_0`/`JMP_CMP_0` (operator). In the textual form the
  header is the **first parameter** for these ops; the tokenizer detects
  `headerParamName != null`, takes the first parameter as the header (it emits no
  parameter word), and stores the rest (source-verified, `tokenizeLine`/
  `parseParams`).
- Comparison header tokens: `<=` `<` `==` `!=` `>` `>=` `&` `!&` map to 0–7;
  `&&` `||` map to 8–9. The last two are only legal for `CMP` (the translator
  renders header 8/9 on the other ops as `Unknown CMP operator`, source-verified).
- The `_0` opcodes (4 `wait_cmp`, 12 `mov`, 66 `jmp_cmp`) are **short forms** in
  which the first operand is the constant 0 and is not stored:
  - The translator emits them in full form — `wait_cmp ==, 0x0, ...`,
    `mov 0x0, dest`, `jmp_cmp ==, 0x0, ...` (source-verified, `translate`).
  - The tokenizer, after building an `Op`, calls `OpType.modifyOp`, which
    re-detects the leading `0x0` (only if it resolved to constant 0) and converts
    the op back to the `_0` form with one fewer parameter (source-verified,
    `OpType.modifyOp`). This is what makes the textual round trip byte-stable
    (see `research/scripting/02-fate-binary-format.md` §8).

### 3.4 Calls in low-level Fate

`call 322, stor[0], 0x2` — the first field after the mnemonic is the numeric
engine-function index (16 bits; must be < 1024, enforced by the disassembler's
header parser, source-verified). The translator renders it as the metadata method
name when `stripNames` is false, or the number when true (source-verified).
Parameter count is **variable** for `CALL` (the only op whose parameter count is
not fixed by the opcode table — `Op` constructor:
`params = new Param[type == OpType.CALL ? paramCount : type.params.length]`,
source-verified).

## 4. Function mechanics (low-level)

- A `def`'s local variables become **data words placed immediately before the
  function label**, one word per variable (arrays: one word per element), each
  initialized to the variable's value (default `0`):
  `FateFunction.toString` emits `<varname>:\ndata <value>` × length, then the
  `name:` label (source-verified).
- Arguments are received on the script value stack: the function prologue emits
  one `pop <param>` per parameter, **in reverse parameter order**
  (`FateCompilerVisitor.visitFunction`, source-verified).
- Calls are `push <args...>; gosub :fn`; returns are `push <vals...>; return`.
  The value stack is the script state's `Deque<Integer>` (see runtime doc §4).
- The entrypoint table (start of the binary) contains **byte addresses** of the
  entry labels; the runtime converts to word offsets by dividing by 4
  (`ScriptFile.getEntry`, source-verified — see binary-format doc §3).

## 5. Text strings inside scripts

Scripts that display dialogue/text embed strings as data (via `data str[...]` or
engine calls with string parameters). The encoding (`LodString`,
`Script-Recompiler/.../tokens/LodString.java`, source-verified):

- **Two characters per 32-bit word**: char 0 in the low 16 bits, char 1 in the
  high 16 bits; an odd-length final char leaves the high half 0
  (source-verified, `Assembler` LodString branch + `fillString`).
- **Terminator**: `0xa0ff`. The `<noterm>` control suppresses it (used when the
  translator splits one string across labels).
- **Character set** (87 glyphs, 0x00–0x56):

| code | char | code | char | code | char | code | char |
|---|---|---|---|---|---|---|---|
| 0x00 | space | 0x15 | `0` | 0x20 | `B` | 0x39 | `a` |
| 0x01 | `,` | 0x16 | `1` | 0x21 | `C` | 0x3a | `b` |
| 0x02 | `.` | 0x17 | `2` | 0x22 | `D` | 0x3b | `c` |
| 0x03 | `·` | 0x18 | `3` | 0x23 | `E` | 0x3c | `d` |
| 0x04 | `:` | 0x19 | `4` | 0x24 | `F` | 0x3d | `e` |
| 0x05 | `?` | 0x1a | `5` | 0x25 | `G` | 0x3e | `f` |
| 0x06 | `!` | 0x1b | `6` | 0x26 | `H` | 0x3f | `g` |
| 0x07 | `_` | 0x1c | `7` | 0x27 | `I` | 0x40 | `h` |
| 0x08 | `/` | 0x1d | `8` | 0x28 | `J` | 0x41 | `i` |
| 0x09 | `'` | 0x1e | `9` | 0x29 | `K` | 0x42 | `j` |
| 0x0a | `"` | 0x1f | `A` | 0x2a | `L` | 0x43 | `k` |
| 0x0b | `(` | 0x30 | `R` | 0x2b | `M` | 0x44 | `l` |
| 0x0c | `)` | 0x31 | `S` | 0x2c | `N` | 0x45 | `m` |
| 0x0d | `-` | 0x32 | `T` | 0x2d | `O` | 0x46 | `n` |
| 0x0e | `` ` `` | 0x33 | `U` | 0x2e | `P` | 0x47 | `o` |
| 0x0f | `%` | 0x34 | `V` | 0x2f | `Q` | 0x48 | `p` |
| 0x10 | `&` | 0x35 | `W` | 0x39..0x52 | `a`..`z` | 0x49..0x52 | `q`..`z` |
| 0x11 | `*` | 0x36 | `X` | | | | |
| 0x12 | `@` | 0x37 | `Y` | 0x53 | `[` | 0x56 | `□` (white box glyph) |
| 0x13 | `+` | 0x38 | `Z` | 0x54 | `]` | | |
| 0x14 | `~` | | | 0x55 | `;` | | |

(any code outside the set renders as `<chr=0xNN>` with a warning, source-verified)

- **Control codes** (upper byte 0xa0 or above; `<name>` / `<name=param>` syntax in
  `str[...]` literals, source-verified):

| code | name | parameter | meaning (per name) |
|---|---|---|---|
| 0xa1ff | `line` | none | line break |
| 0xa200 | `button` | none | prompt for button press |
| 0xa3ff | `multibox` | none | multi-line box |
| 0xa500\|n | `speed` | numeric 8-bit | text display speed |
| 0xa600\|n | `pause` | numeric 8-bit | pause duration |
| 0xa700\|n | `colour` | numeric 8-bit | text colour |
| 0xa800\|n | `var` | numeric 8-bit | substitute variable value |
| 0xad00\|n | `xoffset` | numeric 8-bit | horizontal offset |
| 0xae00\|n | `yoffset` | numeric 8-bit | vertical offset |
| 0xb000\|n | `sauto` | numeric 8-bit | (auto-scroll style flag) |
| 0xb100\|n | `element` | numeric 8-bit | element/attribute marker |
| 0xb200\|n | `arrow` | numeric 8-bit | arrow indicator |
| 0xc000\|len | `action` | len bytes of raw text follow (packed 2/word) |

  (source-verified from the `CONTROLS` enum; the semantic interpretations of the
  numeric controls beyond their names are **Inferred** from the names and the
  runtime text renderer — the renderer-side consumption is documented in
  `research/platform/02-execution-model.md`'s textbox section and is the subject
  of the M9 text-system topic.)

This resolves the M2 open item "original retail text/string encoding inside DRGN
scripts" at the **format** level (source-verified from the toolchain). What the
renderer does with each control at runtime is layer-(a) behavior that the
reference reproduces in `legend.game.Text`; byte-level confirmation against retail
scripts remains **asset-validated-pending** (register Q-59, see
`research/04-unresolved-questions.md`).

## 6. Language design observations (Proposed-layer notes for a Rust rewrite)

These are observations, not facts about the original:

- The language is a deliberately thin layer over a 4-byte-word instruction set;
  the "assembler" is a single pass over lines with global label resolution, so
  forward references (labels, jump tables) are supported naturally
  (source-verified).
- Register allocation is absent by design: every expression gets its own
  temporary variable (`_expr_N`), and function locals are data words referenced
  through `inl[]`. This keeps the textual form one instruction per line and makes
  line-based diffing stable — which is what the patch system relies on
  (source-verified; see `research/scripting/03-toolchain.md` §8).
- `&&`/`||` are full-evaluation binary ops (a single `CMP`), not short-circuit
  control flow; scripts that rely on side-effect-free operands are unaffected,
  but a Rust port of the *high-level* language must preserve this to keep
  compiled output compatible (source-verified).
- The immediate-encoding rule (values with bits 16–23 set must use the
  two-word `NEXT_IMMEDIATE` form) is a binary-level constraint that leaks into
  the textual language via the tokenizer (source-verified; binary doc §4.2).

## 7. Evidence index (this document)

| Claim area | Source |
|---|---|
| Grammar (high-level) | `Script-Recompiler/src/main/antlr/.../FateLexer.g4`, `FateParser.g4` (full files) |
| Compile pipeline | `Script-Recompiler/src/main/java/org/legendofdragoon/scripting/compiler/FateCompiler.java` (`compile`, `parse`, `getFunctionsFromIncludes`) |
| Codegen rules | `.../compiler/FateCompilerVisitor.java` (`visitEntrypoint` 59-67, `visitFunction` 71-103, `visitIf_` 110-142, `visitWhile_` 144-161, `visitDo_while` 163-179, `visitStatement` 194-221, `visitAugmented_assignment` 223-272, `visitAssignment` 278-315, `visitDeclaration` 317-347, `emitAssignment` 349-364, `emitArrayAssignment` 366-380, `visitGlobal` 382-420, `visitCall` 422-529, `visitReturn_` 531-561, `visitExpression` 588-858, `visitAssignable` 875-918) |
| Function serialization | `.../compiler/FateFunction.java` (`toString`), `FateContext.java` (`getLabel`, `startFunction`) |
| Intermediate op text | `.../compiler/FateOp.java` (`toString`) |
| Low-level line grammar | `Script-Recompiler/src/main/java/org/legendofdragoon/scripting/Tokenizer.java` (`LINE_PATTERN`, `LABEL_PATTERN`, `tokenize` 96-244, `tokenizeLine` 252-300, `parseParams` 302-321, `parseParam` 323-610, `splitParameters` 612-645, `packParam` 655-670) |
| Header params / `_0` forms | `Tokenizer.tokenizeLine` (header extraction), `OpType.java` (toolchain: `headerParamName`, `modifyOp`), `Translator.translate` (`_0` re-expansion, `buildHeaderParam`) |
| String encoding | `.../tokens/LodString.java` (charset switch, `fromString`, `CONTROLS`), `Assembler.assemble` (LodString packing), `Disassembler.fillString` |
| Variable stack / calls | `FateCompilerVisitor.visitCall` (PUSH/GOSUB), `visitFunction` (POP), runtime `ScriptState.scriptPush/scriptPop` (see runtime doc) |
| Game reuse of toolchain | `Severed-Chains/.../scripting/ScriptManager.java:423-468` (`meta`, `compile`, `assemble`, `disassemble`), `GameEngine.java:99` (SCRIPTS construction with `patches/libs`, `patches/scripts` include dirs) |

## 8. Repository and revision pins

- `Script-Recompiler` @ `3433e6f74bb6d562aeff0490d11e8937da437511` (read-only).
- `Severed-Chains` @ `0c08c679b6b54e167b00851f1f1458d4dea3eade` (read-only).

## 9. Open items

- Q-59 (new): retail confirmation of which charset/control codes actually occur in
  shipped DRGN scripts, and of the runtime renderer's consumption of each numeric
  control (format is source-verified; byte usage is not asset-validated).
- Q-15 / C-01 (opcode 9 and the `_0` forms) — resolved in
  `research/scripting/02-fate-binary-format.md` §7.
- Parameter types 0x11/0x12/0x15/0x16 have no textual syntax and the translator
  rejects 0x12/0x15/0x16; whether retail scripts use them is **Unresolved**
  (register Q-60).
