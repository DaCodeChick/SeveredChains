# `fate compile` — Native Design (F1)

**Status: design only. No production C code, no CMake target, and no test wiring exists yet.**
This document is the implementation contract for the first `fate` milestone. It is written to be
self-contained: a developer (human or another model) should be able to implement `fate compile`
from this file plus the golden corpus, without re-deriving behaviour from the Java reference.

**Date:** 2026-10-27. **Scope:** `fate compile` = Fate Script source → assembly text, *only*.
`assemble`, `decompile`, `genpatch`, `patch`/`undopatch`, `strip`, and `versions` are **out of
scope** for this milestone; they must exist as subcommand names that reject with a clear "not
implemented" message, and nothing may be scaffolded for them (see §14).

**Governing decisions (all owner-ruled, 2026-10-27):**
* Placement: root `fate/` subproject, mirroring `psx/` (**D1**).
* Language: ISO **C23** for the `fate` target only; project default stays C17, with a
  configure-time probe, no silent downgrade (**D2** / spec `spc-1793133517787-4253`).
* Goldens: scratch-copy Java oracle, committed AGPL goldens (**D3**; spec
  `spc-1793129896113-12590`).
* Exit codes: deliberate deviation, `0/1/2/3` (spec `spc-1793134462715-50393`).
* Identifiers: psx-style **snake_case, no `fate_` prefix**, `UPPER_SNAKE` macros, internal
  linkage by default (spec `spc-1793133498509-334`).
* **R1 — behavioural fidelity:** match the reference's observable behaviour as closely as
  possible; the committed goldens are normative for both emitted text and diagnostic text
  (spec `spc-1793139055396-44131`).
* Error handling: typed C status codes, structured diagnostics, `exit()` only at the CLI entry
  point, deep components never call `exit()`/`abort()` for recoverable errors (spec
  `spc-1793131198334-17871`).

---

## 1. What "correct" means

The single acceptance method is **differential comparison against the committed goldens** in
`fate/testdata/golden/`. There are 19 fixtures (17 from F0 plus `p16_waitcopy` and
`p17_strescape`, added in F2), each with:

* `<id>.expected` — byte-exact assembly text the Java reference produced (may be empty).
* `<id>.diag` — the reference's exit code, whether it wrote a file, and its diagnostic lines.
* a `manifest.json` entry — the compile arguments, the expected outcome, and (for the six R1
  cases) the ruling.

**Two parity modes** (owner ruling, decision 2 of 2026-10-28). Eighteen fixtures are
`"parity": "exact"` and must match byte-for-byte. `p12_forloop` is `"parity": "deviation"`,
`"valid_source": false`: its input is not valid Fate and the reference's 152-byte output is an
ANTLR error-recovery by-product (`gosub inl[:for]` names no defined label, so it would not
assemble). For such a fixture the harness checks exit status, absence of a crash, and that the
output *differs* from the golden; a byte-identical result is reported as an obsolete pin. See
`docs/research/fate-toolchain-phase1.md` §13 and `fate/testdata/README.md`.

A native `fate compile` run is **correct for a fixture** when:

1. the emitted assembly text is **byte-identical** to `<id>.expected`, and
2. the emitted diagnostics match `<id>.diag`, and
3. the process exit code matches the contract in §9 (which is derived from, but not identical to,
   the reference's exit code).

Comparison is byte-exact by design. These are load-bearing, not cosmetic, and a normalising diff
would hide real drift:

* the file starts with a blank line (see §7.1);
* a zero-parameter op carries a trailing space (`return `, `noop `);
* scratch slots and labels are `<function>_expr_N` / `_label_N` with **double underscore**, and
  both `_expr_` and `_label_` counters run **per file**, not per function;
* variable declaration order in a function prologue is insertion order, de-duplicated by name.

The reference (Script-Recompiler `0.7.11` @ `3433e6f74bb6d562aeff0490d11e8937da437511`) is the
behavioural oracle and stays **read-only**. Behaviour must be *derived* from its observable
output and grammar, not translated from its Java source (spec `spc-1793131202962-45157`). The
Java source is cited in **this document and `docs/research/fate-toolchain-phase1.md` only** —
code comments state behaviour and carry no `.java` citations (per `docs/guidelines/cpp.md` §2).

### 1.1 The six R1 fidelity cases

These six fixtures are where "match the reference" and "add a new diagnostic" diverged. They are
ruled **match** (R1). The implementer must reproduce the reference's *observable* behaviour for
each; the golden is the acceptance test.

| id | source quirk | required observable behaviour | exit |
|---|---|---|---|
| `p8_badhex` | `var x = 0xFF;` | lexes as `0` + ident `xFF`; emits `mov 0, …`; **no** diagnostic | 0 |
| `p9_tab` | tab in source | whitespace class is `[ \n]`; emit `line L:C token recognition error at: '\t'` per tab on **stderr**; keep compiling | 0 |
| `p10_truefalse` | `if (x == true)` | report `unimplemented op true`; write **no** output file. The reference's `NullPointerException` is a defect and is **not** reproduced — exit cleanly | 3 |
| `p11_strings` | unknown scoped call | drop the statement (no placeholder); report `unknown engine call <name>`; write the rest | 3 |
| `p12_forloop` | `for ( … )` | `for` is not a keyword: parse as a call to a function named `for` with the reference's recovery outcome; report all five reference diagnostics | 3 |
| `p13_incl` | `include char_utils.fate ;` | filename token includes the trailing space; emit `#include <token>` verbatim; functions load only when the token ends exactly in `.fate` | 3 |

`p10` is the only case where a literal match is impossible: the reference crashes. We match its
*observable result* (diagnostic text, no output file) and replace the crash with a clean exit 3.

---

## 2. Architecture

### 2.1 File layout

```
fate/
  CMakeLists.txt            # adds the `fate` executable + its tests
  README.md                 # what this subproject is, how to build/test it
  src/
    main.c                  # CLI entry point; the ONLY place exit() is called
    cli.c  cli.h            # argument parsing (compile only)
    lexer.c  lexer.h        # token stream
    parser.c  parser.h      # parse tree (with recovery, §5)
    preproc.c  preproc.h    # function/include/return-count collection pass
    codegen.c  codegen.h    # scope/vars/labels + op-list emission
    meta.c  meta.h          # meta bundle loader (-m)
    diagnostics.c  diag.h   # error list + diagnostic records
    util.c  util.h          # arena/growable arrays, string helpers
  tests/
    CMakeLists.txt          # registers the differential + unit tests
    test_lexer.c            # token-stream unit tests (incl. the hazards)
    ...                     # see §12
```

**Implementation correction (F2, 2026-10-28).** The differential harness above was planned as a C
binary (`tests/test_diff.c`) because §14 of `fate-toolchain-phase1.md` forbade Python in a
*default* test. That constraint was about the oracle, not the comparison, and the owner ruled the
Python harness retained. The differential therefore lives in
**`fate/testdata/run_golden.py`**, invoked by one CTest entry (`fate.compile.golden`) registered
in `fate/CMakeLists.txt`; it reads the manifest, runs `fate compile` per fixture, and compares
bytes/diagnostics/exit codes. It never runs Java and never downloads anything.

* One executable named **`fate`**. No per-operation executables, no dispatcher framework
  (spec `spc-1793129887564-33080`).
* Production code is `.c`/`.h` only (spec `spc-1793129890273-76616`). No C++ in `fate/`.
* **Internal linkage (`static`) by default.** A file's functions are `static` unless they are the
  documented interface called from another translation unit. There is **no public library target**
  and no `fate_`-prefixed exported API (spec `spc-1793129890273-76616`).
* No third-party dependency. The meta CSVs are parsed with a small hand-written reader
  (the reference uses opencsv; we do not import it).

### 2.2 Pipeline and data flow

The pipeline mirrors the reference's `FateCompiler.compile()`, in the same four stages:

```
source string
  └─ lexer  ──► token stream
  └─ parser ──► parse tree            (recovery on error, §5)
  └─ preproc ─► functions[] , includes[], per-function return counts
  └─ codegen ─► op list ──► compiled text
```

* **Lexer** produces a flat token array. Line/column is computed per token for diagnostics and
  for the p9 tab messages.
* **Parser** builds a tree following the grammar in §5. It is a single forward pass.
* **Preprocessor** (pass 1) walks the tree to collect, in source order: the function table
  (name → params + return count), the include-name list, and the per-function return count. It
  also emits the preprocessor diagnostics (`missing return`, illegal function name, duplicate
  declaration).
* **Code generator** (pass 2) walks the tree again, maintaining the context (scopes, current
  function, op list, `_expr_`/`_label_` counters), and appends ops. It then rewrites variable
  names (prepend function name), rewrites label names (prepend prefix, empty on the CLI path),
  and renders the op list to text.

Keeping preproc and codegen as **two separate passes** is important: the codegen pass must know
every function's return count *before* it lowers calls, and it must validate includes against the
full include list. Do not fold them into one pass.

### 2.3 Memory model

* Use a single **arena** per compile (bump allocator). The parse tree, token array, op list,
  scopes, and all `FateValue`-like nodes are arena-allocated and freed by one reset at the end of
  the compile. This matches the reference's GC'd object model and keeps the C code free of manual
  `free()` on the hot path.
* Growable arrays (`util.h`) for: tokens, ops, the errors list, the per-function variable lists,
  and CSV rows.
* Strings: the source is a NUL-terminated buffer; token text is a `(ptr, len)` view into it (not
  copied). Rendered op text is built into a growable output buffer.
* **No exceptions.** Every fallible function returns a typed status (§8). The arena and
  growable arrays report `Status` codes; they do not `abort()`.

---

## 3. The lexer (precise)

The token set, rule order, and the quirks are normative. Rule order matters: **first matching
rule wins** (ANTLR semantics). Keywords and multi-char operators are listed before
`IDENTIFIER` so they win over it.

### 3.1 Rules, in order

1. `ENTRYPOINT  'entrypoint'`
2. `DEF 'def'` · `VAR 'var'` · `TRUE 'true'` · `FALSE 'false'` · `RETURN 'return'` ·
   `CONTINUE 'continue'` · `BREAK 'break'` · `IF 'if'` · `ELSE 'else'` · `WHILE 'while'` ·
   `DO 'do'` · `STOR 'stor'` · `REG 'reg'`
3. `INCLUDE 'include' ' '+` → **pushMode(INCLUDE_FILE_MODE)**. Note the literal single space is
   part of the token match; it is what consumes the space after `include`.
4. Operators, longest-first (so `==` beats `=`, `&&=` beats `&&` beats `&`, `>>=` beats `>>`
   beats `>`, etc.):
   `== != >= <= && || !` · `* / % + - & | ^ ~` · `>> <<` · `&&= ||= *= /= %= += -= &= |= ^= ~= >>= <<=`
   · `=` · `++ --`
5. Punctuation: `, ( ) [ ] { } ;` · `::` (SCOPE, must beat `:` COLON) · `:`
6. `IDENTIFIER  [a-zA-Z_][a-zA-Z0-9_]*`
7. `NUMBER  '-'? ( [0-9]+ | '0x'[a-f0-9]+ )`
8. `WHITESPACE  [ \n] → skip`
9. `LINE_COMMENT  '//' ~[\r\n]* → skip`
10. `BLOCK_COMMENT  '/*' .*? '*/' → skip`
11. `STRING  '"' ( ESC_SEQ | ~('\\'|'"') )* '"'`, where `ESC_SEQ = '\\' [n"\\]`
12. mode `INCLUDE_FILE_MODE`: `INCLUDE_FILE  ~[;\r\n]+ → popMode`

### 3.2 Normative quirks (each is pinned by a golden or a unit test)

* **Whitespace is only space and LF.** Tab (`\t`) and carriage return (`\r`) are **not** skipped.
  A tab outside a comment is a *lexer* error: emit `line <L>:<C> token recognition error at:
  '\t'` to **stderr** and continue (the reference's ANTLR default console listener does exactly
  this; the stream is stderr, measured). A CRLF file therefore mis-lexes. This is `p9`.
* **Hex literals are lowercase-only.** `NUMBER`'s hex branch is `'0x'[a-f0-9]+`. For `0xFF`, the
  hex branch matches no digits, so the token is the decimal `0`, and `xFF` is a separate
  `IDENTIFIER`. This is `p8` — it is *not* an error, it silently narrows.
* **`true`/`false` are lexed but never parsed.** No parser rule references `TRUE`/`FALSE`. The
  token reaches the codegen `value`/expression path, which reports `unimplemented op true` and
  (in the reference) crashes. We report the diagnostic and stop without writing output (§7.6,
  `p10`). Do **not** implement `true`/`false` as values.
* **Include filename is a raw token.** In `INCLUDE_FILE_MODE`, the filename is everything up to
  the next `;`, `\r`, or `\n` — **including any trailing spaces**. So `include foo.fate ;`
  produces filename `foo.fate ` (trailing space). This is `p13`. The `#include` line and the
  function-loading gate both use this raw token verbatim.
* **Strings** support only `\n`, `\"`, `\\` escapes and do **not** terminate at a newline; an
  unterminated quote swallows the rest of the file. (No golden exercises the unterminated case
  yet; see §14 open items.)

### 3.3 Token record

Each token carries: `kind`, `start_byte`, `line`, `column`, and for `NUMBER`/`IDENTIFIER`/
`STRING`/`INCLUDE_FILE` a `(text_ptr, len)` view. `line`/`column` are 1-based, `column` counting
from the start of the line (the p9 messages use 0-based column in the reference's
`line 4:0` form — match that exactly, i.e. column is 0-based in the message).

---

## 4. Parse tree and grammar

The grammar is normative and must be reproduced **exactly**, including operator precedence and
left-associativity. Top level allows **only** declarations — there are no free-floating
top-level statements.

```
body          : (entrypoint | global | function | include)*
entrypoint    : ENTRYPOINT IDENTIFIER TERM
function      : DEF IDENTIFIER identifier_list block
include       : INCLUDE INCLUDE_FILE TERM

control       : if_ | while_ | do_while
if_           : IF LPAREN expression RPAREN block (ELSE if_ | ELSE block)?
while_        : WHILE LPAREN expression RPAREN block
do_while      : DO block WHILE LPAREN expression RPAREN TERM

block         : OPENER (statement | control)* CLOSER
statement     : (declaration | assignment | postfix | augmented_assignment
               | call | return_ | CONTINUE | BREAK) TERM

declaration        : VAR (IDENTIFIER | identifier_list) (ASSIGN (expression | array_initializer))?
assignment         : (assignable | assignable_list) ASSIGN (expression | array_initializer)
augmented_assignment : assignable augmented_assignment_op expression
postfix            : assignable postfix_op
call               : IDENTIFIER (SCOPE IDENTIFIER)? expression_or_string_list
return_            : RETURN (expression | expression_list)?

global           : VAR IDENTIFIER ASSIGN (NUMBER | const_array_initializer) TERM

identifier_list   : LPAREN (IDENTIFIER (COMMA IDENTIFIER)*)? RPAREN
assignable_list   : LPAREN (assignable (COMMA assignable)*)? RPAREN
expression_list   : LPAREN (expression (COMMA expression)*)? RPAREN
expression_or_string_list : LPAREN (expression_or_string (COMMA expression_or_string)*)? RPAREN
expression_or_string : expression | STRING

expression :
    LPAREN expression RPAREN
  | (ADD | SUB | NOTC | NOTA) expression
  | expression mult_op expression
  | expression add_op expression
  | expression shift_op expression
  | expression relational_op expression
  | expression equality_op expression
  | expression ANDA expression
  | expression XORA expression
  | expression ORA expression
  | expression ANDC expression
  | expression ORC expression
  | value

value : NUMBER | call | assignable | id | array_lookup
assignable : IDENTIFIER | stor | gamevar | reg
stor        : STOR LBRACKET (expression COMMA)? expression RBRACKET
gamevar     : VAR LBRACKET expression RBRACKET (LBRACKET expression RBRACKET)?
reg         : REG LBRACKET expression RBRACKET
id          : IDENTIFIER COLON IDENTIFIER
array_lookup : IDENTIFIER LBRACKET expression RBRACKET
array_initializer   : LBRACKET expression (COMMA expression)* RBRACKET
const_array_initializer : LBRACKET (NUMBER (COMMA NUMBER)* | STRING (COMMA STRING)*) COMMA? RBRACKET

augmented_assignment_op : ANDC_ASSIGN ORC_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
                        ADD_ASSIGN SUB_ASSIGN ANDA_ASSIGN ORA_ASSIGN XORA_ASSIGN
                        NOTA_ASSIGN SHR_ASSIGN SHL_ASSIGN
postfix_op : INCR | DECR
mult_op    : MUL | DIV | MOD
add_op     : ADD | SUB
shift_op   : SHR | SHL
relational_op : GT | LT | GTE | LTE
equality_op : EQ | NEQ
```

### 4.1 Precedence (normative, top = tightest)

All binary operators are **left-associative**. Precedence is fixed by the order of the
`expression` alternatives (first listed binds tightest):

```
1  unary  ( ADD | SUB | NOTC | NOTA )
2  *  /  %
3  +  -
4  >>  <<
5  <  >  <=  >=
6  ==  !=
7  &
8  ^
9  |
10 &&
11 ||
```

**This is not C precedence.** In particular additive `+ -` binds *tighter* than shift `>> <<`
(`a + b >> c` = `(a + b) >> c`), and `&` binds tighter than `==`. Do not reach for C's
precedence table.

The parser must be built by **left-recursion elimination** (or an equivalent precedence-climbing
parser) that produces the same tree shape as ANTLR: for `e1 o1 e2 o2 e3` at equal precedence,
the tree is `(e1 o1 e2) o2 e3`, and each binary node keeps its two operands as
`expression(0)` (left) and `expression(1)` (right). This shape is load-bearing because the
codegen's expression-var allocation order (§7.4) depends on it.

### 4.2 Parse recovery (R1: calibrated best-effort)

The reference gets its error-path output from **ANTLR's default error recovery**, which is a
by-product of the generated state machine, not a designed behaviour. Reproducing ANTLR's exact
recovery in a hand-written parser is not feasible, and R1 asks us to match *as closely as
possible*, with the goldens as the acceptance test. Therefore:

* The parser is a deterministic, single-pass, recursive-descent / precedence-climbing parser
  that follows the grammar exactly on well-formed input.
* On the **first unexpected token** at any point, the parser enters **recovery**: it consumes
  tokens until it reaches a token that can *start a new statement or top-level item*
  (`def`, `var`, `include`, `entrypoint`, `if`, `while`, `do`, `return`, `continue`, `break`,
  or an identifier that could begin a `call`), or a block terminator it can resynchronise on
  (`;`, `}`, `)`). It then resumes at that boundary. No parse-error diagnostic is emitted for a
  recovered token (the reference installs no parse-error listener, so its recovery is silent).
* The three recovery goldens — `p8_badhex`, `p12_forloop`, `p13_incl` — are the acceptance test
  for this policy. **If a well-chosen general policy does not reproduce all three byte-for-byte,
  the policy must be adjusted until it does**, before any other work proceeds. The policy may
  then diverge from the reference on inputs not covered by a golden; that is an accepted,
  documented limitation (R1's "as closely as possible"), and any such divergence found later must
  be added as a new fixture + golden, not silently absorbed.

The implementer should treat the recovery policy as the single highest-risk item in this
milestone and validate it against `p8`/`p12`/`p13` first, in isolation, before building the rest
of codegen on top.

---

## 5. Preprocessor pass (pass 1)

Walks the tree top-down, in source order. Responsibilities, each pinned by a golden or unit
test:

1. **Collect functions.** For each top-level `function`:
   * name = the `IDENTIFIER` text;
   * params = the `identifier_list` identifiers, in source order;
   * return count = the arity of the **last** `return` statement in the function body, in
     document order (a `return;` is 0, `return e;` is 1, `return (a, b);` is 2).
   * diagnostics:
     * `missing return` — if the block has **no statements at all, or its last statement is not
       a `return_`**. In this check, `statement` means the grammar's `statement` alternative;
       `control` structures (`if`/`while`/`do`) are *not* statements, so a function that ends in
       an `if/else` reports `missing return` even when both branches return (`p2`), and a
       trailing `if` after a `return` does not save it.
     * `<name> is not a legal name` — if the function name equals an ASM op name
       (case-insensitive; this is the "opcode-name collision" case).
   * store `functions[name] = (name, params, return_count)`.
2. **Collect includes.** For each top-level `include`, append the raw `INCLUDE_FILE` token text
   (with any trailing space) to the include list, in source order.
3. **Resolve included functions.** See §5.1 (breadth-first, `.fate` gate, function-name-as-variable).
4. **Declaration diagnostics.** A `var` declared when the name is already in scope reports
   `variable <name> already defined in scope` (the declaration still lowers and assigns).

* A missing `-L` **directory** is a hard I/O failure before any compiling (the reference throws
  outside its `try`). Map to native exit **2** (§9).
* An include *file* that cannot be resolved (name not found in any `-L` dir, and the name does
  end in `.fate`) is `Could not resolve include <name> in include paths […]`. In the reference
  this throws *inside* the compile `try`, so **no output file is written** and the exit is 0.
  Under the native contract this is an I/O-class failure: report it as a diagnostic, write no
  output, and exit **2** (the reference's 0 is the collapse the contract refuses to reproduce).

### 5.1 Include resolution and function table

* Search **only** the `-L` include dirs, in order. Never search the including file's own
  directory. (This is a behavioural fact: compiling `queen_fury.fate` without `-L` fails.)
* Process includes **breadth-first in discovery order** (the root file's includes first, then
  each included file's own includes as that file is processed), de-duplicated by exact token
  text. An include name is loaded **only if it ends exactly in `.fate`** (the `p13` gate); any
  other name is simply not loaded (no error, no functions).
* For each loaded include: resolve the file against the `-L` dirs, parse it, and run the
  preprocessor on it. If the file cannot be read, add `Failed to load include <…>` and continue.
* **Function names are also global variables.** After collection, every function name is added
  as a length-1 variable to the global scope (insertion order is irrelevant — it is not
  observable in output). Consequence: a bare identifier equal to a function name resolves as a
  variable (`inl[:<name>]`) rather than reporting `is not defined in the current scope`. They do
  **not** appear in any function prologue (they are added before any function body is visited).

---

## 6. Meta loading (`-m`)

`compile` **requires** a meta bundle. The reference obtains meta from a network `version`; the
native tool is offline and reproducible, so it takes a **directory** instead. This is a
deliberate CLI deviation from the reference's `-v` and is the main open decision in §14.

* CLI: `-m <meta-dir>` (required for `compile`).
* The directory must contain, as the oracle's `m1` cache does: `descriptions.csv`,
  `params.csv`, `enums.csv`, and one CSV per enum class named `<ClassName>.csv`.
* What `compile` actually uses (keep the loader minimal — do not model the description/param
  text columns we never emit):
  * `descriptions.csv` → the ordered list of engine-method **names** (column 0).
  * `params.csv` → the **parameter count per method** (rows where column 0 == method name).
    A method with no matching row has 0 params.
  * `enums.csv` → the ordered list of enum **class names** (column 0).
  * each `<ClassName>.csv` → the ordered list of that class's **value strings** (column 0).
    The codegen looks up a bare identifier against *all enum values* (not classes); a match
    renders as the value string itself.
* CSV parsing: the files are simple (quoted or unquoted fields, comma-separated, one record per
  line). Implement a small reader that handles double-quoted fields with `""` escapes and
  unquoted fields; it does not need full RFC 4180. Strip a UTF-8 BOM if present.
* A missing or malformed meta file is an I/O error → exit **2** (matches the reference, which
  throws while loading meta, writes nothing, and exits 1 on a missing enum CSV).

---

## 7. Code generation (pass 2) — the normative emission rules

This is where byte-exactness lives. The rules below are derived from the reference's visitor and
verified against the goldens. Implement them exactly.

### 7.1 Overall output framing

`compile()` renders, in order:

1. for each entrypoint (in source order): `entrypoint :<name>\n`
2. one blank line: `\n`
3. for each op in the op list: `<op text>\n`

That single blank line at step 2 is why every golden starts with an empty first line. The op list
is in **source order** of top-level items; entrypoints are collected separately and always render
at the very top, before any function.

### 7.2 The op list and its item kinds

The op list is an ordered list of items. Most items are **ops** (`<name> <p1, p2, …>`), but four
kinds render specially and are interleaved in the list at their source position:

* **function prologue** (one per `function`, at the function's start):
  ```
  "\n"
  for each variable in the function's variable list, de-duplicated by name (first wins):
      "<varname>:\n"
      repeat "data <value>\n" for the variable's length    # value is "0"
  "<functionname>:"        # no trailing newline; the "\n" is added by the list renderer
  ```
  The variable list is: params first (source order), then local/`_expr_` variables in the order
  they were created during the body visit. Duplicate names (same name in sibling scopes) are
  de-duplicated, first occurrence kept.
* **label** (a generated `_label_N`): `<labelname>:`
* **include directive**: `#include <raw filename token>` (trailing space included, verbatim)
* **global** (a top-level `var`):
  ```
  "<name>:\n"
  for each value: "<type> <value>\n"        # type is "data" (or "rel")
  ```
  A scalar global emits one `data <n>` line; an array-of-numbers global emits one `data <n>`
  line per element (values verbatim from source, so `0x1`, `0xffffffff`). An **array-of-strings
  global** (no golden yet — §14) emits, for `var t = ["a", "b"];`:
  ```
  t:
  rel :t__entry_0
  rel :t__entry_1
  t__entry_0:
  data str[a]
  t__entry_1:
  data str[b]
  ```
  i.e. a `rel :<t>__entry_<i>` line per element under `t:`, then one `data str[<s>]` block per
  element label, and the global variable is flagged as a `rel` table (which changes how
  `t[i]` lookups lower — §7.3).

A plain **op** renders as `<opname> <params joined by ", ">`. With zero params this is
`<opname> ` — **the trailing space is part of the byte stream** (`return `, `noop `).

### 7.3 Value rendering

Each operand is one of these, rendered exactly:

| kind | rendering |
|---|---|
| immediate | the raw text (a number, an op/comparator name, an engine-call name) |
| variable | `inl[:<name>]` |
| `stor[i]` | `stor[<i>]` |
| `stor[a,b]` | `stor[<a>, <b>]` (comma + space) |
| `var[i]` | `var[<i>]` |
| `var[a][b]` | `var[<a>][<b>]` |
| `reg[i]` | `reg[<i>]` |
| `id[M:N]` | `id[<M:N>]` (the `MODULE:NAME` text verbatim) |
| enum value | the value string verbatim |
| string | `str[<s>]` |
| array element | `<var>[<i>]`, or for a `rel` table `inl[:<name>[:<name>[<i>]]]` |
| label ref | `inl[:<label>]` |
| function ref | `inl[:<name>]` |

**`rel` table lookup (no golden yet — §14).** A lookup `t[i]` where `t` is an array-of-strings
global lowers to `mov <i>, inl[:62]` (the index is moved into `stor[62]`) and renders as the
special form `inl[:t[:t[stor[62]]]]`. Only `stor` is supported as the index of a `rel` table.

**Literal-index rule (load-bearing).** When an index expression is not already a variable, it is
moved into a fresh `_expr_` slot and referenced indirectly. This is why `stor[5]` becomes
`mov 5, inl[:f__expr_0]` then `mov stor[inl[:f__expr_0]], …` (p5), while `reg[3]` — whose index
is used directly — stays immediate. Apply the "materialise into a slot if not already a
`FateVariable`" rule to `stor`, `var`, `gamevar` indices, and to `array_lookup` indices.

### 7.4 Expression lowering and `_expr_` allocation order (the byte-exact core)

`_expr_` slots are allocated from a **file-wide** counter starting at 0, in the exact order the
visitor evaluates the expression. For a **binary** operator the allocation order is:

1. allocate the **result** slot (`out = getExprVar()`),
2. evaluate the **left** operand,
3. evaluate the **right** operand,

then emit `mov <left>, out` and `<op> <right>, out` (the `right` operand and `out` are the same
slot, in-place). The left operand, if not already a variable, is materialised into a slot first
via the §7.3 rule. For **unary** `+ - ! ~` the pattern is a single result slot: `mov <a>, out`
then (for `-`) `neg out`, (for `!`) `cmp ==, <a>, 0, out`, (for `~`) `not out`.

This order is why `p4`'s first statement lowers to:
```
mov inl[:f_b], inl[:f__expr_2]      # left of (b*2)
mul 2, inl[:f__expr_2]
mov inl[:f_a], inl[:f__expr_1]      # left of (a + (b*2))
add inl[:f__expr_2], inl[:f__expr_1]
mov 3, inl[:f__expr_4]              # (3/4)
div 4, inl[:f__expr_4]
mov inl[:f__expr_4], inl[:f__expr_3]
mod 5, inl[:f__expr_3]
mov inl[:f__expr_1], inl[:f__expr_0]
sub inl[:f__expr_3], inl[:f__expr_0]
```
A second implementation that allocates left-to-right instead of result-first will produce the
same semantics and **fail the byte comparison**. Get the order from this section, not from
intuition.

### 7.5 Statements, control, and the `_label_` counter

`_label_N` is allocated from a **file-wide** counter starting at 0, in the order control
structures are visited. The lowering is normative:

* **`if`** (`if (e) { A } [else (if (e2){B}|{C})]`):
  1. `label L1 = getLabel()`
  2. evaluate `e` → `v`
  3. `jmp_cmp ==, 0, v, inl[:L1]`
  4. emit `A`
  5. if there is an else: `L2 = getLabel(); jmp inl[:L2]`
  6. emit `L1:`
  7. if `else if`: recursively emit the nested `if`
  8. if `else {…}`: emit the else block
  9. if there was an else: emit `L2:`
* **`while (e) { B }`**: `L1, L2 = getLabel(), getLabel()`; `L1:`; evaluate `e`→`v`;
  `jmp_cmp ==, 0, v, inl[:L2]`; emit `B`; `jmp inl[:L1]`; `L2:`.
* **`do { B } while (e);`**: `L1, L2 = getLabel(), getLabel()`; `L1:`; emit `B`; evaluate `e`→`v`;
  `jmp_cmp !=, 0, v, inl[:L2]`; `L2:`. (Note `!=`, the mirror of `while`'s `==`.)
* **`continue`** → `jmp inl[:<current loop's L1>]`; **`break`** → `jmp inl[:<current loop's L2>]`.
  Outside a loop each reports `continue is only valid inside of loop` /
  `break is only valid inside of loop`.
* **Scopes.** A function opens a **function scope** that holds its params; the function's body
  `block` then pushes a child scope, closed at the block's end, and the function scope is
  closed after the body. A `var` declared in a scope is visible to that scope and its
  descendants; the same name in two sibling scopes are distinct variables (both appear in the
  function's variable list, de-duplicated on emission).

### 7.6 Calls and returns

* **Engine call** (`MOD::name(args…)`):
  1. look up `MOD::name` in the meta method list (case-sensitive equality). If absent: report
     `unknown engine call MOD::name` and emit **nothing** (the whole statement is dropped) —
     this is `p11`.
  2. if present, arity-check against the meta param count: mismatch reports `expected <n>
     params, got <m>`.
  3. render `call <metaName>, <a1, …, ak>` — the **meta** name is param 0. (Since lookup is by
     equality, this equals the source text.) A `STRING` argument is materialised into an `_expr_`
     slot whose value is the raw string and rendered as `str[…]`.
* **ASM op call** (`mov(…)` etc., bare identifier that case-insensitively matches an op name):
  arity-check against the op's param count (mismatch → `expected <n> params, got <m>`); render
  the op. A `STRING` argument to an ASM op reports `strings cannot be passed to ASM ops`.
  **Special case: `wait`** — its first operand is decremented in place by the hardware, so the
  reference copies it to a fresh `_expr_` slot first and uses the copy. Reproduce this.
* **User-function call** (`name(args…)`):
  1. if `name` is in the function table, arity-check against its param count (mismatch →
     `expected <n> params, got <m>`).
  2. if **not** in the table, report `unknown function <name>` (but still emit the call).
  3. for each argument (source order): a `STRING` argument reports `strings cannot be passed as
     parameters`; otherwise evaluate it and emit `push <arg>`.
  4. emit `gosub inl[:<name>]`.
  * Because the reference emits `push` for each argument and the callee `pop`s in the same
    order, the net effect is arguments in source order at the call site and the callee popping
    its params in **reverse** source order (the last-declared param is popped first). Reproduce
    the callee `pop` order exactly: for params `[p1, p2, …, pn]` emit `pop pn … pop p1`.
* **Multi-assignment.** `(a, b) = <e>`: evaluate the RHS once; if it is a value list (a
  multi-return call), the *i*-th variable receives the *i*-th value — one `mov` per pair, no
  extra temporaries.
* **Array initialiser.** `var x = [e1, e2, e3];` declares a length-3 variable (its prologue
  block is `f_x:` + three `data 0` lines) and then, per element *i* in order: allocate a fresh
  `_expr_` slot `k`, emit `mov <i>, k` and `mov <ei>, x[k]`. The index is **always** materialised
  into a slot, even though it is a literal (no golden exercises this yet — §14).
* **Return.** `return;` → 0 values; `return e;` → 1; `return (a, b);` → 2. The return count of
  the *last* return in the function (from the preprocessor) is `def.returns`. Every return
  statement whose value count differs from `def.returns` reports `return expected <def.returns>
  parameter(s), got <n> instead`. Then emit `push <v>` for each value (source order) and
  `return `.
* **Using a function's result as a value.** When a call appears in an expression, allocate one
  `_expr_` slot per declared return (in order), emit `pop` for each (reverse order), and the
  expression's value is that slot (or the list of slots for multiple returns).

### 7.7 Diagnostics (the `errors` list)

Diagnostics are strings of the form `<line>: <message>`, collected in emission order, exactly as
the reference's `errors` list. The **exit-3 trigger** is: this list is non-empty. The per-tab
lexer messages (§3.2) and the `Failed to compile:`/stack-trace lines are **not** part of this
list (they go to stderr / are crash artefacts) and do not trigger exit 3 on their own.

The full set of diagnostic messages the codegen can emit, verbatim (line = the offending token's
1-based line):

```
<line>: variable "<n>" is not defined in the current scope
<line>: variable <n> already defined in scope
<line>: function "<n>" missing return
<line>: function "<n>" is not a legal name
<line>: entrypoint function <n> not found
<line>: continue is only valid inside of loop
<line>: break is only valid inside of loop
<line>: unknown operator type <t>
<line>: unknown assignment type <t>
<line>: only regular variables can be used with array initializers
<line>: unknown global initializer
<line>: unknown engine call <name>
<line>: expected <n> params, got <m>
<line>: unknown function <name>
<line>: strings cannot be passed to ASM ops
<line>: strings cannot be passed as parameters
<line>: unknown parameter type
<line>: unimplemented value <t>
<line>: unimplemented op <t>
<line>: ASM ops and engine calls cannot be used in expressions
<line>: unknown assignable <t>
<line>: return expected <n> parameter(s), got <m> instead
<line>: function "<n>" return lists are not the same size
<line>: Failed to load include <…>
Could not resolve include <name> in include paths […]        # not line-prefixed
```

Match the exact wording, spacing, and punctuation of each. The `.diag` goldens are the
authority; when in doubt, diff against them.

### 7.8 Final name rewriting

After the op list is complete:
1. **Variable names**: for each function, every variable in its list is renamed to
   `<functionName>_<varName>` (hence the double underscore on `<fn>_expr_N` and
   `<fn>_<param>`). This rename is applied to the emitted text, not to the scope.
2. **Label names**: every generated label is prefixed with the label prefix. On the CLI path the
   prefix is the **empty string**, so labels stay `_label_N`.

---

## 8. Error handling and status

* Typed status codes only; no exceptions; no `abort()`. A small `Status` enum (in `util.h` or
  `diag.h`) covers: `Ok`, `Usage`, `IO` (missing/unreadable input, unwritable output, missing
  include dir, missing/bad meta), `Compile` (one or more diagnostics), `OutOfMemory`.
* Deep components (lexer, parser, codegen, meta) return `Status` and never call `exit()`.
* Diagnostics carry file/line/column + code + message. The `errors` list (§7.7) is the
  compilation-diagnostic carrier.
* **Malformed input must never degrade into success**: a compile that produced any diagnostic
  exits 3, never 0.

---

## 9. CLI and exit codes

`fate` is one executable. For this milestone only `compile` is implemented; every other
subcommand name (`assemble`, `decompile`, `genpatch`, `patch`, `applypatch`, `undopatch`,
`strip`, `versions`) is recognised and rejected with a clear "not implemented in this build"
message on **exit 1** (usage). Unknown subcommand → usage on **exit 1**.

### 9.1 `fate compile` options

| flag | meaning | default |
|---|---|---|
| `-i <file>` / `--in <file>` | input `.fate` source (required) | — |
| `-o <file>` / `--out <file>` | output assembly text | `<input>.txt` |
| `-L <dir>` / `--libs <dir>` | include dir (repeatable) | none |
| `-m <dir>` / `--meta <dir>` | meta bundle directory (required) | — |
| `-w <dir>` / `--work-dir <dir>` | working dir for relative paths | CWD |

* **`--dump-ast`** (debug, development only): after parsing, print the parse tree (node kind +
  token text, indented) to stdout and stop before codegen. It is a developer aid for the
  recovery-policy work (§4.2); it is not part of the differential test, changes no golden, and
  its absence/behaviour is not acceptance-critical — but the flag must exist so the parser can
  be debugged against `p8`/`p12`/`p13` before codegen is built on top.

* Default output name appends `.txt` to the **full** input name: `char_utils.fate` →
  `char_utils.fate.txt` (matches the reference, measured).
* Output parent directories are created if missing (the reference does `createDirectories`).

### 9.2 Exit-code contract (owner-approved; a deliberate deviation from the reference)

| code | meaning |
|---|---|
| `0` | compiled, **no** diagnostics in the `errors` list, output written |
| `1` | usage / CLI error (bad flags, missing required flag, unknown subcommand, unimplemented subcommand) |
| `2` | I/O error: missing or unreadable input, unwritable output, missing `-L` dir, missing/bad meta, or an unresolvable `.fate` include (no output written) |
| `3` | compilation produced one or more diagnostics (output written **unless** the failure aborted emission, as in `p10`) |

The reference exits 0 for almost everything; this contract is the explicit, owner-approved break
(spec `spc-1793134462715-50393`). `exit(3)` ⇔ the `errors` list is non-empty.

---

## 10. Build: CMake, C23 probe

* `fate/CMakeLists.txt` declares `add_executable(fate …)` with `C_STANDARD 23` and
  `C_EXTENSIONS OFF` **at target scope**. The project-level `C_STANDARD 17` is untouched; adding
  `fate` must not change the C standard of any existing target (spec
  `spc-1793133517787-4253`).
* **Configure-time C23 probe** (mirrors the existing exception probes in
  `cmake/ScToolchain.cmake`): try-compile a small C23 feature (e.g. `auto` return type or a
  `_Atomic` builtin) with `-std=c23`. On failure, **fail configure with a clear
  "toolchain cannot do C23" message** — never silently downgrade to C17/C11.
* No new third-party dependency. No `find_package` beyond what the project already uses.
* The `fate` executable is added to the build **only** via `add_subdirectory(fate)` at the root,
  guarded so it is optional if the tree is consumed without it (match how `psx/` is wired).

---

## 11. Gate integration (must land with the code, not after)

Per D1, the mechanical gates must see the C tool:

* `tools/check_format.py`: add `"fate"` to `SCAN_ROOTS` and ensure `.c`/`.h` are in the scanned
  suffixes. The C code is formatted to the root `.clang-format` (Allman, 2-space, 120-col,
  `RemoveBracesLLVM`).
* `tools/check_naming.py`: add `.c` to the scanned suffixes. snake_case identifiers pass
  automatically via `is_snake_or_macro`; **camelCase in `fate/` must be flagged**. `fate` uses
  snake_case throughout (spec `spc-1793133498509-334`), so a clean result is expected — but the
  gate must actually *cover* the files, which is the point.
* **Prove the extension bites**: before declaring F1 done, plant a deliberately mis-cased
  identifier (camelCase) in a scratch copy of a `fate/` file and confirm both gates fail, then
  remove it. A gate that "passes" without actually scanning `fate/` is a silent exemption.

---

## 12. Test strategy

* **Primary acceptance: differential vs goldens.** A test compiles each of the 19 fixtures
  (using the manifest's args, with `-m` pointed at a local meta bundle and `-L` at the fixture
  dir where the manifest says so) and byte-compares the output to `<id>.expected`, compares the
  diagnostics to `<id>.diag`, and checks the exit code against §9.
* **Loud skip.** The differential test must **skip with exit 77** (CMake `SKIP_RETURN_CODE`, which
  *is* set on the test — without it CTest reads any non-zero exit as a failure) and
  a visible message when a golden is absent or the meta bundle is unavailable — never silently
  pass (spec `spc-1793134467821-81877`). The oracle itself is **never** invoked by a default
  build/test; `tools/fate_oracle.py` is the only thing that runs Java, and only on explicit
  opt-in.
* **Unit tests (supplementary, may not replace the differential test):**
  * lexer: token stream for each hazard (tab, `0xFF`, `true`, include trailing space, string
    escapes); line/column values.
  * parser: precedence table (§4.1) — in particular `a + b >> c` grouping; left-associativity;
    the three recovery fixtures produce the expected tree shape.
  * codegen: the §7.4 expression-var allocation order on `p4`; the `pop` reverse order; the
    `p8`/`p10`/`p11`/`p12`/`p13` observable behaviours.
* **CI:** a dedicated `fate-toolchain` job now exists in `.github/workflows/ci.yml` (three legs:
  macOS arm64 + Linux x86-64 GCC + Linux x86-64 Clang; the engine matrix passes
  `-DSC_ENABLE_FATE=OFF` so the two suites stay independent). Its compiler gate requires the
  `auto x = 1;` probe to compile under `-std=c23`/`-std=c2x` **and** to fail under
  `-std=c17 -Werror -pedantic-errors`, so a toolchain that merely ignores the flag cannot qualify.
  **No CI leg has executed**: the repository is not under version control and no runner is
  available, so this is written-and-locally-dry-run evidence only (the macOS step sequence was
  executed locally; runner behaviour, including whether an image has a meta bundle, is unknown).

---

## 13. Byte-level invariants checklist

Verify each of these is preserved, independently of the fixtures (they are the properties the
fixtures happen to exercise). Status as of F2: `[x]` = observed in matched output and therefore
pinned by a fixture; `[ ]` = **not yet verified**, either no fixture exercises it or the
comparison that would prove it is not implemented.

- [x] output begins with exactly one blank line before the first op
- [x] zero-parameter op = `<name> ` with a trailing space (`return ` in the p17 golden)
- [x] `_expr_` and `_label_` counters are per-file, start at 0, and interleave in visit order
      (verified per file: p3 emits `__expr_0`/`__expr_1`, p16 four slots, p17 three)
- [x] `<fn>_expr_N` and `<fn>_<param>` use a double underscore (function name + `_` + slot name)
- [x] function prologue: `\n`, then de-duplicated variable `name:`/`data 0` blocks, then `name:`
      (p15 and p17 match byte-for-byte, which exercises deduplication)
- [ ] `stor[a, b]` has a comma **and** a space; `var[a][b]` keeps two bracket groups — no fixture
      emits a `stor` operand yet
- [ ] literal indices are materialised into `_expr_` slots, not inlined (`reg[3]` stays immediate)
      — untested: no fixture indexes with a literal
- [ ] callee pops params in reverse source order — **not exercised**: no golden contains two
      consecutive `pop` lines, so the corpus cannot currently detect a wrong pop order
- [x] `#include <token>` is emitted verbatim, trailing space included (p13/p14 match)
- [x] an unknown scoped call emits nothing for that statement (p11 matches)
- [ ] the `errors` list wording matches §7.7 verbatim — the harness compares `.expected` bytes and
      exit status; **diagnostic-text comparison is not implemented** (see §14 item 5)
- [~] exit code follows §9, with exit 3 ⇔ non-empty `errors` list — exit codes are compared for
      the pinned deviation and every fixture's emitted text, but a full per-fixture exit-code
      assertion against `.diag` is not yet implemented

---

## 14. Open items and decisions for the owner

1. **`-m <meta-dir>` replaces the reference's `-v <version>`.** This is the one CLI change from
   the reference, forced by the offline/reproducible requirement. Confirm the flag name and that
   `-m` is **required** for `compile` (not optional with a bundled default). *Proposed: yes,
   required; the meta is part of the input, so it should be explicit.*
2. **Recovery policy generalisation.** §4.2 specifies a best-effort recovery calibrated to
   `p8`/`p12`/`p13`. Confirm it is acceptable that inputs outside the golden corpus may diverge
   from the reference's ANTLR by-product, with any such divergence promoted to a new
   fixture+golden when found. *Proposed: acceptable.*
3. **Unterminated-string and a few untested paths** (string as a global table, `id[…]` enum
   lookup, `rel` table emission, `wait` copy) have no golden yet. They are specified here from
   the source, but the owner should confirm we should add fixtures+goldens for them in this
   milestone (F1) or defer to a follow-up. *Proposed: add the string-escape and `wait` fixtures
   now (cheap, high value); defer the `rel`/`id` table fixtures until `assemble` needs them.*
4. **Meta CSVs stay out of the repo** (retail-derived). The tests need a meta bundle to run;
   confirm the test's default meta path (currently the local Severed-Chains bundle, overridable
   via an env var) and that its absence → loud skip 77, not a pass. *Proposed: as specified in
   §12.*
   **Status (F2):** implemented as proposed — default-on, bundle discovered from `$FATE_META_DIR`
   or local candidates, else skip 77 reported as *Skipped* and escalated to a visible
   `NOT VERIFIED` banner by the CI step. **Consequence the owner should decide on:** because the
   runners have no bundle, the three fate legs compile the tool but do not compare a single byte
   of output. Committing a small *synthetic* (non-retail) meta bundle under
   `fate/testdata/local-meta/` would make those legs actually test output; that needs an explicit
   decision about fixture licensing, so it has not been done unilaterally.
5. **Diagnostic format is still `file:line: message`** (F2). §14 of the phase-1 doc asks for
   column + stable code (e.g. `FATE-E0002`); `.diag` files also do not record the reference's
   stderr `token recognition error at: '\t'` warnings, so p9's warning is untested. Neither is
   blocking the byte-match acceptance, both are open.

---

## Appendix A — op names (the complete set from `OpType`, 55 distinct names)

A bare identifier that case-insensitively matches any of these is treated as an **ASM op call**
in a call position, and a *function* named after one is rejected (`is not a legal name`). The
complete list, verbatim (several enum constants share a name — e.g. `rewind` covers opcodes
1, 5, 6, 7, 11, 13, 14, 15 — and `byName` returns the first match, so the implementation only
cares about the name, not the opcode, for dispatch and arity):

```
yield rewind wait wait_cmp mov swap_broken memcpy and or xor andor not shl shr add sub
sub_rev incr decr neg abs mul div div_rev mod mod_rev mul_12 div_12 div_12_rev sqrt rand
sin_12 cos_12 atan2_12 call jmp jmp_cmp while jmp_table gosub return gosub_table
deallocate deallocate_other fork fork_reenter consume debug96 debug97 debug98 depth noop
push pop cmp
```

The arity each of these expects in an ASM op call (e.g. `mov(a, b)`) is the count of its
parameter entries in the reference's `OpType` table — e.g. `mov` takes 2, `cmp` takes 4,
`jmp_cmp` takes 4, `push`/`pop` take 1, `call` takes 0, `return`/`noop` take 0. The full
opcode/param-name table lives in the reference (`OpType.java`) and is reproduced here only as
far as the arity counts need it; if an arity table is required for implementation, extract it
from that file once, verbatim, rather than re-deriving it.

## Appendix B — what is deliberately NOT in this design

* `assemble` / `decompile` / `genpatch` / `patch` / `undopatch` / `strip` / `versions` — names
  only, rejected with exit 1. No scaffolding.
* Network meta fetch, the `snapshot` version, and the `./cache` directory — replaced by `-m`.
* The decompiler's comment emission (`; SUBROUTINE`, `; FORK JMP`, …) — that is `decompile`, not
  `compile`.
* Any public library target or `fate_`-prefixed API — none, until a concrete need is shown and
  approved.