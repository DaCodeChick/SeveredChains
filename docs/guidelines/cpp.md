# C++23 Coding Guidelines (mandatory)

**Status:** mandatory for all project-owned C++ (engine, `psx` library, game,
tools, plugins, tests). AGENTS.md references this document as required
reading before any C++ implementation work.

This project is **modern C++23**. Legacy C/C++ idioms — the printf family,
`std::va_list`, C strings as API types, manual memory management,
macro-based flow control — are **prohibited** in project code except at the
narrow, documented interop boundaries in §9. Formatting (indentation,
braces, line width) is governed by the root `.clang-format` (LLVM baseline,
2-space indent, **Allman braces**, **no braces on single-statement control
bodies** — §6.1, 120 columns); this document governs
*design and idiom*, not layout.

Rules are marked **MUST** (violations are defects), **SHOULD** (deviation
needs a documented justification in the code), or **AVOID** (use the modern
alternative).

---

## 1. Language baseline

- **C++23** (`CMAKE_CXX_STANDARD 23`, no compiler extensions).
- **C17** for the `psx` core only (`C_STANDARD 17`, `C_EXTENSIONS OFF`) — a deliberate
  ABI/portability decision, not a stylistic one: the core is a shared C library with
  no C++ runtime and no third-party dependency, and C++23 reaches it through the
  header-only `psx::cpp` wrappers (§9.2, psx-c-api.md). Everything else in this
  document governs project-owned C++; C sources are governed by psx-c-api.md §2-§5.
- **Exceptions permitted, but discouraged** across project targets. Prefer `std::expected` and explicit error codes for recoverable failures (§4). Exceptions may be used when they provide a clear architectural benefit, particularly when integrating third-party libraries that already throw. Do not use exceptions for normal control flow, routine validation, resource lookups, or expected failure conditions. Catch exceptions at appropriate subsystem or application boundaries, preserving diagnostic information and ensuring safe recovery or termination. Never allow exceptions to propagate across C ABI boundaries or incompatible interfaces. Destructors must not throw. Avoid blanket `catch (...)` handlers that silently swallow failures. Do not disable exception support ad hoc per target: the policy is global and switchable (`SC_ENABLE_EXCEPTIONS`, error-handling.md §1) and project code must compile under both settings; the only targets compiled exception-free *unconditionally* are the documented portable / public-API boundaries (`psx`, the plugin SDK). Any deliberate use of exceptions in project-owned code must be justified by a concrete advantage over `std::expected`.
- **RTTI disabled on public API targets** (`psx`, engine public API,
  `sc-sdk`). No `dynamic_cast` in any public header. Engine-internal code
  may use RTTI only where it does not appear in a public header (AGENTS.md).
- Warnings: `-Wall -Wextra -Werror` (Clang/GCC), `/W4 /permissive- /WX`
  (MSVC). Warnings are bugs; do not suppress with `#pragma` / `-Wno-*`
  unless a targeted, commented, justified single-line suppression documents
  a false positive.
- `static_assert` for invariants; `[[assume]]` sparingly (optimizer hints
  that the compiler cannot verify, with a comment).

## 2. Standard library: preferred facilities

Use the standard library before writing custom infrastructure. Preferred
facilities by job:

| Job | Use | Not |
|---|---|---|
| Formatted text / diagnostics | `std::format`, `std::print`, `std::println`, `std::format_string` | `printf`/`snprintf`/`sprintf`, `std::va_list`, `std::ostringstream` for simple formatting |
| Owned text | `std::string` (or `std::string` members) | `char[]`, `char*`, buffers |
| Borrowed text | `std::string_view` | `const char*` (C interop boundary only, §9) |
| Owned byte sequences | `std::vector<std::byte>` / `std::vector<uint8_t>` | `malloc`/`free`, `new[]`/`delete[]` |
| Non-owning byte view (decoders, I/O) | `std::span<const uint8_t>` | raw pointer + length pair, `const uint8_t*` + `size_t` as API |
| Fixed-size buffers | `std::array<T, N>` | C arrays, `T data[N]` members |
| Paths and files | `std::filesystem` | C strings for paths, `fopen` in C++ code |
| Result of a fallible operation | `std::expected<T, E>` (§4) | out-parameters, error code + value pair, `bool` return for data ops |
| Optional value | `std::optional<T>` | sentinel values, `bool hasX + T x` pairs |
| Closed set of values | `enum class` (§6) | unscoped enums, magic integers, `bool` overloading |
| Tagged union of states | `std::variant` (rarely) | tagged unions, char-discriminated structs |
| Iteration and transformation | ranges (`std::ranges`, `std::views`) where they improve readability | hand-rolled index loops for what `std::ranges::copy`/`transform` do |
| Bit-level work (PSX fixed-point, formats) | `std::bit` (`std::bit_cast`, `std::popcount`, …), fixed-width integers (`uint8_t`…`uint64_t`) | `reinterpret_cast`, union type-punning, char bit twiddling on non-POD |
| Endianness-aware binary loads | explicit little-endian helpers built on `std::span` + `std::bit_cast`/byte assembly (the `psx` library provides these) | pointer casts of unaligned data |
| Timers (app code) | `std::chrono` (steady clock for durations, system clock for wall time); SDL tick functions only where SDL event timing demands it | `time()`, `clock()` |
| Threading (M5+) | `std::jthread`, `std::mutex`, `std::condition_variable`, `std::atomic` | C11 threads, `volatile` for synchronization, platform thread APIs directly |
| Diagnostics call site | non-template logging entry points only (accurate `std::source_location`); the formatted template path deliberately omits it (Clang reports the template body line) | manual `__FILE__`/`__LINE__` plumbing in every call |
| Compile-time only code | `constexpr` / `consteval` functions | preprocessor "computation" |

- **MUST** prefer standard algorithms over hand-rolled loops where the
  algorithm is the intent (`std::find`, `std::ranges::count`, …).
- **SHOULD** keep range pipelines short and named; if a `std::views` chain
  needs a comment to explain itself, split it into named steps.
- **AVOID** `std::any` (no type safety at the call site) — `std::variant`
  for known sets.

## 3. Type-safe logging and diagnostics

- **MUST** use `std::format` / `std::print` / `std::println` for all
  formatted output. **The printf family is prohibited** in project C++:
  no `printf`, `snprintf`, `sprintf`, `fprintf`, `std::va_list`, and no
  `const char* format`-style APIs in project headers.
- **MUST** use *literal* format strings for formatted logging. The engine
  logger's formatted entry point takes `std::format_string<Args...>`, which
  the compiler only accepts for string literals — this is the type safety:
  a mismatched format string is a **compile error**, and (with exceptions
  disabled) a runtime format error would otherwise terminate the process.
  For a dynamic string, format it first with `std::format` (literal format)
  and pass the result as a value.
- The process logger (`lod::Logger`) is the one diagnostic sink:
  leveled (`Trace`/`Debug`/`Info`/`Warning`/`Error`), tagged (subsystem
  "marker"), timestamped. Log **through the logger**, never `std::print`
  from subsystem code (the host `main` may `std::print` for CLI output such
  as `--version`/`--help`). Line format:
  `[   1234ms] INFO  [marker] message`. Call-site capture via
  `std::source_location` is deliberately not on the formatted template path
  — Clang reports the template body's line there, not the call site; it
  remains available on non-template logging entry points where it is
  accurate.
- Logging is thread-safe (internal mutex). Format **before** locking.
- Diagnostic I/O failure (full terminal, closed pipe) under
  is a terminate condition — an accepted, documented last
  resort for diagnostics, not a control-flow mechanism.

Example:

```cpp
util::Logger::Instance().Log(util::LogLevel::Info, "window",
    "created at {}x{} on monitor {}", width, height, monitorName);
```

## 4. Errors: `std::expected` preferred, exceptions permitted

- **MUST** prefer `std::expected<T, Status>` (or `Status` for operations without a return value) for recoverable failures. Exceptions are permitted when they provide a demonstrable architectural benefit, particularly when integrating third-party libraries that already throw. Do not introduce exceptions merely to avoid explicit error propagation. Fallible constructors should use factories returning `std::expected<T, Status>` where practical; throwing constructors are permitted when necessary and must maintain exception safety (error-handling.md §2).
- **MUST** mark `std::expected` return values `[[nodiscard]]`. Ignoring an error result is a defect.
- Error *codes* are typed (`lod::Status` engine-wide; `psx::Error` in the `psx` library) with documented domain ranges and a `ToString`/`NameOf` function per enum (diagnostics). **AVOID** returning `false` where an `expected`/`Status` carries the *why*; `bool` returns are acceptable only for state queries (`IsInitialized()`) and simple predicates.
- **AVOID** out-parameters for errors, and C-style "error code + out value" pairs — the primary exception is a C interop boundary (§9).
- **MUST NOT** use exceptions for normal control flow, routine validation, resource lookups, or expected failure conditions. Do not allow exceptions to cross C ABI boundaries or interfaces that prohibit exception propagation. Catch exceptions at appropriate boundaries and translate them into typed errors where required. Never silently swallow exceptions.
- **MUST** preserve exception safety and RAII guarantees on every error path. A fallible sequence must either roll back, preserve a documented valid state, or avoid committing partial state. Destructors must not propagate exceptions. Prefer RAII over manual cleanup or unwind code.
- **SHOULD** provide at least the basic exception-safety guarantee for operations that may throw, and the strong guarantee where practical. Do not introduce exception-based error handling into a subsystem without considering its existing ownership, recovery, and error-propagation contracts.
- The `psx` library uses `std::expected<T, psx::Error>` with decoder-specific error codes (malformed-class granularity per testing.md §2.11). Preserve this interface unless a documented architectural reason justifies changing it.

## 5. RAII, ownership, lifetimes, value semantics

- **MUST** express ownership with the type system:
  - *Unique ownership*: value members, `std::unique_ptr<T>` (heap when the
    lifetime/identity genuinely requires it — polymorphic subsystems,
    cycles broken intentionally).
  - *Shared ownership*: `std::shared_ptr<T>` **only** with a documented
    reason (a real multi-owner lifetime, e.g. a resource used by several
    subsystems with independent teardown). Default to unique.
  - *Non-owning access*: references (always valid), `std::span`,
    `std::string_view`, or a raw `const T*` / `T*` **strictly as
    non-owning observation**.
- **MUST NOT** own through a raw pointer: no raw owning pointers, no
  `delete`, no `new` outside `std::make_unique`/`std::unique_ptr` factories,
  no `malloc`/`free`, no `new[]`/`delete[]` in engine/game code. (The `psx`
  library's decoders return owned IR by value — `std::vector`-based — never
  raw allocations.)
- **C resource ownership**: a C-owned resource (e.g. an SDL3 handle) is
  owned by exactly one RAII class, held in a `std::unique_ptr` **supplied
  with an explicit custom deleter** (a named namespace-scope lambda —
  `decltype` names its type in the `unique_ptr` alias — or a small struct),
  and released idempotently in `Destroy()` + the destructor. `std::make_unique`
  is used for **complete** types; SDL3 types are **opaque C types**, so
  `make_unique` cannot apply — the handle returned by the C create
  function is owned via `reset()` (or direct construction) with the custom
  deleter. Deleter/alias names follow the acronym rule
  (`SDLWindowDeleter`, never `SdlWindowDeleter`). The raw pointer may be <!-- naming-allow: a counter-example of the acronym rule -->
  exposed read-only where a later layer needs it (M2 GPU) — documented as
  non-owning, and the escape hatch is named `Handle()`.
- **Value semantics by default**: pass small/cheap types by value
  (integers, enums, `string_view`, `Status`); pass by `const T&` when large
  or lazily evaluated; take `T&&` / use `std::move` when the callee owns.
  Copies are fine unless a measurement says otherwise — no speculative
  moves.
- Copy constructors/assignment: default (`= default`) or deleted. Non-
  copyable types (resources, mutexes, the logger) delete copy and document.
- Lifetimes: a borrowed reference/`string_view`/`span` **MUST NOT** outlive
  its referent; a function must never return a view into a temporary.
- **AVOID** global mutable state; the logger's process instance and
  compile-time tables (`const`/`constexpr`) are the sanctioned exceptions.
- **AVOID** inheritance for code reuse (composition). Inheritance is for
  the interface patterns the architecture documents (plugin API frozen
  classes, the test framework's registration), never for ad-hoc "mixins".

## 6. Strong typing and const correctness

- **MUST** use `enum class` with an explicit underlying type for every
  closed set (states, actions, error codes, formats). Unscoped enums are
  prohibited. Every public enum has a `[[nodiscard]] std::string_view
  NameOf(...)` (or `ToString`) for diagnostics. The one exception is
  documentation-by-declaration in **C** (§9.2): the `psx` core has no `enum class`,
  so `psx_error_t` is a `typedef enum` that declares the code set alongside its
  type — C++ code still consumes `psx::Error`, never that unscoped enum.
- **MUST** use `constexpr` for compile-time constants and `consteval` for
  compile-time-only computation. Integer constants are named
  (`WindowMinWidth`), never bare literals in logic.
- **MUST** mark query functions, conversions, and error-returning functions
  `[[nodiscard]]`.
- **MUST** default member functions to `const`; drop `const` only to
  mutate state. `mutable` only for internal caches with a comment.
- **MUST** use fixed-width integers (`std::uint8_t` … `std::uint64_t`,
  `std::int32_t`, …) where the width is meaningful (PSX formats, save
  blobs, ABI-visible values); `std::size_t` for sizes/counts; `int32_t` for
  screen coordinates/counts that can be negative. **AVOID** plain `int`
  at API boundaries where the width matters (allowed for loop indices and
  local bookkeeping).
- **AVOID** overloading on integer types; prefer named strong types
  (structs or `enum class`) so a `frame` is not interchangeable with a
  `voxelCount`.
- **MUST NOT** use `reinterpret_cast`. POD reinterpretation uses
  `std::bit_cast` (both sides trivially copyable, same size —
  `static_assert` it). Pointer arithmetic is confined to binary-format
  decoding helpers (§9.2).
- No implicit narrowing: let the compiler reject it; write the explicit
  `static_cast` only when the domain is provably in range (comment when
  non-obvious).

### 6.1 Control-statement braces

- **MUST NOT** brace a single-statement `if`/`else`/`for`/`while` body:

  ```cpp
  if (index >= storage_.size())
    return std::unexpected(Error::MRGEntryMissing);
  ```

  A reviewer then reads indentation as structure, which is the point of writing it
  down; the two-statement form is what braces are for.
- Braces **stay** where removing them would change meaning or read badly, and
  `RemoveBracesLLVM` encodes exactly those exceptions: a body that is itself a
  braced-less `if` (dangling-else hazard), a body that declares a variable (the
  scope is the point), multi-statement bodies, and function/lambda bodies.
- **Enforcement:** `.clang-format` sets `RemoveBracesLLVM: true`, so `clang-format`
  removes the braces (§12), and `python3 tools/check_format.py` — CTest entry
  `repo.format` — fails the suite on drift. On a host without `clang-format`, or with
  one older than 17 (which cannot see `RemoveBracesLLVM`), that script says so on its
  first line and runs its own structural scan for this rule instead, so an
  under-tooled runner reports what it could not verify rather than passing silently.

### 6.2 Public data

- **MUST NOT** give a type that exposes behaviour public mutable state. Where correctness
  depends on a relation between members — an entry array plus the count that describes it,
  a handle plus what it owns — the members are private, trailing-underscore, and reached
  through queries.
- Pure aggregates may keep public data (a decoded value returned by value, a field-for-field
  translation of a C struct, a coordinate pair), **with a comment saying so** and naming
  what it mirrors. The test is "could a caller put this object in a state its own methods
  then misread?", not "is it a `struct`?".
- Worked example in `psx`: `tim::Rect`, `tim::Image`, `archive::MRGEntry` and
  `archive::DEFFEntry` are decoded results and stay aggregate; `MRGTable` and `DEFFTable`
  are not — their entry array and virtual count together are one fact, so both are private
  behind `Entries()` / `At()` / `VirtualCount()` / `View()`, and `psx/tests/test_wrapper.cpp`
  pins that with a `static_assert` so re-publicising a field fails the build.

### 6.3 Header inclusion style

- Headers use **`#pragma once` on line 1**, not `#ifndef`/`#define` include guards. This is a
  whole-tree convention, not a per-directory choice: 29 of the 38 headers already used it —
  including every public `psx` C17 header, which is the exported ABI surface — and the 9 that
  did not were all in `fate`, which had not picked it up. `fate/src` was converted on
  2026-10-29; nothing in the tree uses a guard now.
- `#pragma once` is supported by all three target toolchains (it is a documented MSVC
  extension that Clang and GCC both implement), and it is quiet under the strictest C flag set
  the project uses — **measured on this host with clang only**: `-std=c17 -Wall -Wextra
  -Wshadow -Wconversion -Werror -pedantic-errors`, and again under `-std=c23`. The MSVC side
  is documented-and-expected, not measured here; the known caveat is path-identity for headers
  reached over network shares or symlinks, which does not apply to an in-tree `src/` include
  graph. Anyone who wants that leg covered should say so rather than assume it is tested.
- It is not cosmetic here. The `fate` headers really are re-included: a TU that includes
  `model.h` twice — once directly, once through `ast.h` — compiles clean with the pragma and
  yields 21 redefinition errors (`redefinition of 'status_t'` first) with protection stripped.
  Anyone tempted to drop protection entirely should run that comparison first.
- `tools/check_format.py` fails on a header that uses an include guard or carries no
  protection at all. It does not consult clang-format, so it still runs when clang-format is
  missing or older than 17, and it is reported separately from formatting offenders because
  the remedy is an edit, not `clang-format -i`.

## 7. Naming and documentation

- Naming follows the AGENTS.md table exactly: **library** namespaces only —
  one namespace per library, *no* per-module or per-class namespaces (the
  engine is `lod`, the game library `game`, the PSX library `psx`, the
  test framework `lod::test` to separate the test library from the engine;
  the `App` class has no namespace of its own); **PascalCase** for types,
  functions, *and methods* (free functions and member functions alike),
  constants (PascalCase, **no** `k` prefix), and enum values — with acronyms
  upper-cased per the policy below; camelCase for *scoped* variables and
  parameters; PascalCase for public data members; `camelCase_` for private or
  protected member state;
  UPPER_SNAKE_CASE for macros; snake_case files. C-linkage discovery
  symbols: PascalCase with the `LoD` prefix (`LoDPluginCreateV1`). String
  *ids* stay lowercase.
  The `psx` **C core** is the documented exception to the PascalCase grammar — a C ABI has
  no namespaces, so it uses `psx_snake_case` functions, `psx_…_t` types and
  `PSX_UPPER_SNAKE` macros (psx-c-api.md §2); its C++ wrappers use the PascalCase grammar
  above, which is why the same operation appears as `psx_bpe_decompress` and
  `psx::bpe::DecompressBPE`. `tools/check_naming.py` knows both grammars. Third-party names (SDL_*, luau_*, Qt) are preserved
  verbatim.
- **Named constants inside the C grammar are `UPPER_SNAKE_CASE`, not `snake_case`.** Object-like
  macros and file-scope read-only tables are named constants — `#define OP_NAME_COUNT`,
  `static const op_entry_t OPERATORS[]` — and a lower-case spelling reads as a variable.
  Function-local `const` is *not* covered and stays camelCase: most of those are read-only
  temporaries whose value is computed at run time, and spelling cannot distinguish them from
  constants, so the gate keys on file scope (a declaration at column 0) rather than guessing.
  Both the macro rule and the table rule are enforced by `tools/check_naming.py`; before
  2026-10-29 neither was, which is how `op_name_count`, `op_names`, `keywords` and
  `operators` sat in `fate/src` against a rule this section already stated.
- **Prefixes by domain.** C++ code identifiers (namespaces, types, C++
  macros, C-linkage symbols, C++ file names) use `LoD`/`LOD`/`lod` — The
  Legend of Dragoon is what the code implements. The build system (CMake
  files, target names, CMake variables/options) uses the project prefix
  `Sc`/`SC`/`sc` — Severed Chains is the *project*. Product: the app
  executable is `lodsc` (LoD: Severed Chains); the window title and version
  strings carry the product name. The `psx` library is prefix-free (no
  `Sc`/`SC`/`sc` *and* no `LoD`/`LOD`/`lod` — it is an independent
  identity).
- **Acronyms are upper case in every non-trivial identifier** (AGENTS.md
  naming table). Types, functions/methods, constants and enum values are
  PascalCase from the first character, so an acronym there is always upper case:
  `EngineAPI`, `SDLPlatform`, `XADecoder`, `DecompressBPE`, `CLIOptions`,
  `Status::IOOpen`, `ToFPMicros()`, `T::ID` — never `SdlPlatform`, <!-- naming-allow: the counter-example half of this rule -->
  `XaDecoder`, `DecompressBpe`, `CliOptions`, `IoOpen`, `ToFpMicros`, `T::Id`, <!-- naming-allow: the counter-example half of this rule -->
  or `Sc_enable_`. In **variables** the acronym is upper case everywhere except
  when it *leads* the name, because variables are camelCase: `deviceGPU`,
  `tableID`, `rawXABlock` but `gpuDevice`, `idTable`, `cliOptions`. Two-letter
  acronyms are covered by the same rule (`IO`, `FP`, `ID`).
- **Constants and enum values are PascalCase positions too**, including
  function-scope named constants: `MaxSector`, `XADir`, `SDLMinWindowSize`,
  `BPEMagic`, `MRGMagic`, `TIMBPP`, `Status::IOOpen`. A `constexpr` that names a
  limit is a constant, not a local variable, so it never takes the variable form
  (`MaxSector` is right; the camelCase spelling of the same name is the violation
  this rule exists to catch).
- **The audit is a tool, not a memory exercise:**
  `python3 tools/check_naming.py` (CTest `repo.naming`) scans project code and the
  identifiers docs prescribe, and must be clean before work is reported complete.
  It splits identifiers into camel/Pascal humps, so `Time`/`Timer` never trip the
  `TIM` rule and snake_case names (files, test groups, string ids) are outside the
  rule's scope. Deliberate quotations and rule counter-examples are marked
  `<!-- naming-allow: reason -->`.
- **Truncations are not acronyms.** `Ptr`, `Buf`, `Msg`, `Env`, `Ctx`, `Args`
  are clipped words and stay mixed-case (`SDLWindowPtr`, `MsgLevel`); prefer the
  full word in public APIs.
  **Member names do not repeat the enclosing class name** (`VersionString()`,
  not `SdlVersionString()`) — the class context is implicit. <!-- naming-allow: a counter-example of the no-repeat-class-name rule -->
- **Data members are not variables** (AGENTS.md naming section). The camelCase
  rule covers scoped variables and parameters only. Public data members are
  PascalCase (`TickHz`, `ResourceDir`, `MinAPIVersion` — native-api.md §2's
  `PluginInfo` is written to this), private and protected member state is
  camelCase with a trailing underscore (`simFrame_`, `crashDir_`). Two things are
  deliberately outside that rule: a `constexpr` member is a *constant*, so it keeps
  the PascalCase constant position (`NoSource`) without an underscore, and a
  function-pointer member is invoked (`tc.fn(ok)`), so it follows the function
  convention. Third-party struct members are preserved verbatim (`event.key`,
  `event.gaxis.axis`) like any other third-party name.
  `tools/check_naming.py` walks a scope stack with the access specifier, so it
  applies the right half of the rule per record — including classes that re-open
  `public:` — and skips anything seen while a parenthesis is open (a wrapped
  parameter list is arguments, not fields; that case was found and fixed, not
  assumed).
- **MUST**: Doxygen-compatible comments on **all public project-owned C++
  APIs** (AGENTS.md mandate) — purpose, `@param` (in/out, ownership),
  `@return` (and the failure cases with their error codes), ownership and
  thread-safety notes for anything shared or long-lived. Internal helpers
  get comments where the *why* is non-obvious.
- Comments explain *why*, never what the code already says. **Code
  comments do not cite the Java implementation** — no `.java` file
  references, no "parity: `X.java:NN`" notes; state the behavior directly.
  Java source citations belong in `docs/` (research methodology).
- `ReflowComments: false` in `.clang-format` exists to keep Doxygen blocks
  intact — do not edit around it.

## 8. Prohibited and discouraged patterns

| Legacy pattern | Prohibited because | Use instead |
|---|---|---|
| `printf`/`snprintf`/`sprintf`, `std::va_list` | untyped formatting, runtime failures | `std::format` / `std::print` (§3) |
| `const char*` / `char*` in C++ APIs | no length, no null guarantee, encoding drift | `std::string_view` (borrow), `std::string` (own) |
| `new`/`delete`, `malloc`/`free` | manual lifetimes leak on error paths | values, `std::unique_ptr`, `std::vector` (§5) |
| raw owning pointers / double ownership | aliasing, double-free | unique/shared pointers, RAII wrappers |
| C-style casts (`(T)x`) | hides conversions | `static_cast`, `std::bit_cast`, `const_cast` (never in new code except §9) |
| `reinterpret_cast`, union type-punning | UB-adjacent, ABI fragile | `std::bit_cast` on PODs, structured byte helpers |
| unscoped enums, magic integers | namespace pollution, no exhaustiveness | `enum class` + `switch` with `NameOf` |
| macros for flow control / "functions" | no type checking, no debuggability | `constexpr`/templates; the sanctioned exceptions are §8.1 |
| variadic preprocessor macros | same | variadic templates |
| `goto` | unstructured jumps | loops, `break`/`continue`, early returns |
| `volatile` for synchronization | not atomic; data-race UB | `std::atomic`, mutexes |
| out-parameter error codes (C++ side) | error easily ignored | `std::expected` (§4) |
| hand-rolled string buffers | the STL is the buffer | `std::string`, `std::format` |
| C arrays / `sizeof`-driven loops in C++ | no bounds, no value semantics | `std::array`, `std::span`, ranges |

### 8.1 Sanctioned macro exceptions

Macros are permitted **only** for:
1. **Interop with C headers** that require them (rare; e.g. `extern "C"`
   discovery symbols).
2. **Bounded registration sugar in the test framework** (`LOD_TEST`,
   `LOD_CHECK`, `LOD_REQUIRE` — and the equivalent `PSX_*` set in the
   self-contained psx harness) — static-initializer registration has no
   macro-free spelling that stays readable; the macros do no flow control
   and no side effects beyond registration.
3. Build-system visibility (CMake) — not C++ macros.

Anything else: rewrite as a `constexpr`/template function.

## 9. Interop exceptions (where C is genuinely required)

These are the **only** places C-style types may appear. At each boundary,
convert to modern types **immediately** (one function deep) and do not let
C types leak across the layer into public engine/game APIs.

### 9.1 SDL3 (C API — the platform layer)

- `SDL_*` types (`SDL_Window*`, `SDL_Event`, `SDL_Keycode`, `SDL_InitFlags`)
  are permitted in the engine's SDL interop layer (and the host `main` for CLI hints).
- C strings returned by SDL (`SDL_GetError()`, version strings) are
  converted to `std::string` at the point of use — never stored as
  `const char*` in project state.
- `SDL_Window` is owned by the `Window` RAII class as a `std::unique_ptr`
  with a custom deleter (`SDLWindowDeleter` — §5; `make_unique` cannot be
  used for opaque C types). Raw `SDL_Window*` is exposed via `Handle()`
  only where a later layer (M2 GPU) requires it, documented as a
  non-owning escape hatch.
- Above the platform layer, APIs take/return C++ types (our own structs,
  `std::string`, `std::expected`), not SDL types — except the documented
  GPU-layer boundary (M2+), where `SDL_GPU*` handles are wrapped by RAII
  resource classes (rendering-sdl3-gpu.md).

### 9.2 The `psx` library (C17 core + C++23 wrappers)

Low-level byte manipulation is **expected and correct** here — this is where the
"C-like" work legitimately lives, and it is written in **C17**, not C++23
(psx-c-api.md is authoritative; the rules below are what a C++ contributor must not
"improve" away):

- **Core (`psx/src/*.c`, `psx/include/psx/*.h`)**: C17 with compiler extensions off.
  Fixed-width integers everywhere; explicit little-endian reads through the internal
  bounds-checked reader; every declared length/count/rect validated against the real
  buffer before use. Byte views are `psx_bytes_t` / `psx_writable_bytes_t` descriptors;
  results that reference input bytes alias the caller's buffer and never copy.
  Fallible calls return `psx_error_t` and write through out-parameters — there is no
  `std::expected` in C, and inventing a C++ type here would break the ABI.
  Allocation happens only inside an explicit `_table_parse`, paired with `_destroy`.
  `tools/check_naming.py` accepts the C grammar here (`psx_snake_case` functions,
  `psx_…_t` types, `PSX_UPPER_SNAKE` macros).
- **Wrappers (`psx/include/psx/cpp/*.hpp`)**: header-only C++23 that converts
  `std::span<const uint8_t>` ↔ `psx_bytes_t` and `psx_error_t` ↔
  `std::expected<T, psx::Error>`. A wrapper is **one C call plus conversions** — no
  reimplementation, no extra validation, no caching. They own what they allocate
  (`std::vector` results, move-only `*TableHandle`s) and are built `-fno-exceptions`
  `-fno-rtti`, so the C++ face keeps the core's exception-free contract.
- **Engine/game code consumes `psx::cpp`**, never the C surface directly, and maps
  `psx::Error` → `lod::Status::Decode*` at its own boundary while logging the precise
  name (error-handling.md §4.1).
- GTE fixed-point conventions (when `psx::gte` arrives) use casts with the exact reference
  shifts, documented per operation **in `docs/research/`** — code comments state behaviour,
  not Java provenance (§2).
- No SDL, no Qt, no C++ runtime, no third-party — the boundary that keeps this layer
  portable (psx-library.md §1, psx-c-api.md §9).

### 9.3 Native plugin API

C++23 interfaces across the boundary; the single C-linkage discovery symbol
(`LoDPluginCreateV<N>`) is the only C (native-api.md). No C ABI.

### 9.4 Qt (desktop tools, M11+)

Qt's C++ API is used directly; the documented `qtbridge` exception-behavior
boundary is the only place our no-exceptions policy meets Qt's throwing
C++ (qt-integration.md §2).

## 10. Low-level work is not prohibited

§8 targets *unnecessary* legacy patterns in a modern codebase. Legitimate
low-level programming **remains fully in scope** and is used deliberately:

- PSX format decoding: byte-level parsing, fixed-point arithmetic, run
  lengths, adaptive byte-pair tables (`psx::bpe`), GTE integer math
  (`psx::gte`) — written with `std::span`/bit-cast/fixed-width ints,
  golden-tested byte-exact (testing.md §2.11).
- Software rasterizer (`psx::gpu`): per-pixel loops, scanline conversion,
  fixed-point interpolation — performance-critical, measured, documented.
- Platform interop: SDL3, and where a later milestone demands it,
  platform APIs — behind RAII and the modern C++ surface.
- Optimization: `constexpr` tables, branch prediction hints, vectorization
  (autovec where possible, intrinsics only with a measurement justification
  and a portable fallback).

The rule is *modern C++ as the default idiom; low-level where the work
requires it — never legacy by habit*.

## 11. Compile-time rigor and quality gates

- `-Werror` on all project sources (build-system.md §2). A new warning is a
  build break to be fixed, not suppressed.
- Configure-time **compiler policy probes** (build-system.md §2): a `throw`
  TU must fail to compile; a `dynamic_cast` TU must fail on public API
  targets. CI re-runs these on every compiler.
- **Doxygen** runs over all public headers (target `sc-api-docs`); missing
  or malformed doc comments on public symbols are a CI defect.
- Formatted with the root `.clang-format` before review; **do not**
  mass-reformat files you did not touch.

## 12. Quick audit checklist (per file)

- [ ] No `throw`/`try`/`catch`; errors via `std::expected`/`Status`,
      `[[nodiscard]]` on error returns.
- [ ] No printf family, no `va_list`; all formatted output via
      `std::format`/`std::print` with literal format strings.
- [ ] No raw owning pointers, no `new`/`delete`/`malloc`/`free`;
      RAII at every resource boundary (C resources wrapped immediately).
- [ ] No `const char*`/`char*` in project APIs (C strings converted at the
      interop boundary).
- [ ] `enum class` for all value sets; `constexpr` constants;
      `[[nodiscard]]` on queries; member functions `const` by default.
- [ ] No `reinterpret_cast`; `std::bit_cast` for POD views; fixed-width
      ints where width matters.
- [ ] No macros outside §8.1; no `goto`; no `volatile` synchronization.
- [ ] Doxygen on every public symbol; `@param`/`@return`/error/
      thread-safety notes; porting comments cite the reference source.
- [ ] No public mutable state behind behaviour; aggregates that keep public data say why
      and what they mirror (§6.2).
- [ ] `clang-format` applied; Allman braces and **no braces on single-statement
      bodies** per `.clang-format` (§6.1); `tools/check_format.py` clean.
- [ ] New headers open with `#pragma once` on line 1 — no `#ifndef` guard (§6.3); named
      constants and file-scope read-only tables are `UPPER_SNAKE_CASE` (§7); both are gate-
      enforced, so the check fails rather than the review catching it later.