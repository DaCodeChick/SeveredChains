# Error Handling — std::expected-First Strategy

Requirement (AGENTS.md §4, revised 2026-10): C++ exceptions are **permitted
but discouraged** — each use needs a concrete justification (third-party
interop, fatal-logic boundaries) and never serves as control flow in
project code. Recoverable failures use explicit error codes /
`std::expected`; RAII cleanup on all paths; third-party exception behavior
documented, not assumed away. Project targets compile with exceptions
**enabled** by default (`SC_ENABLE_EXCEPTIONS=ON`).

## 1. Compilation Enforcement

- Exception policy is explicit and switchable (`SC_ENABLE_EXCEPTIONS`,
  default **ON**). When OFF, CMake applies to **every project-owned
  target**:
  - Clang/GCC: `-fno-exceptions`
  - MSVC: `/EHs-c-` (no exceptions, but SEH allowed)
- A configure-time probe verifies the *effective* compiler setting matches
  the selected policy: with the policy OFF, a `throw 1;` probe must fail to
  compile (regression guard against silently dropped flags).
- **Exception-free by contract** (independent of the switch, and separately
  probe-verified): `psx` (reusable, extractable, portable — psx-library.md §5)
  and the native plugin API/SDK (native-api.md §1.6). Being exception-free
  there is an ABI/portability property, not a style preference; everything else
  follows the permitted-but-discouraged default.
  For `psx` this is now structural: the core is **C17**, which has no exception
  mechanism at all, and the header-only C++23 wrappers plus their test runner are
  compiled `-fno-exceptions` / `-fno-rtti` so the whole surface keeps the property
  (psx-c-api.md §7). The probe that guards the project-wide policy therefore cannot
  weaken it, and the C++ wrapper layer is what keeps engine call sites unchanged.
- Approved third-party libs with their own exception behavior (Qt 6;
  possibly none others in the approved stack — Luau is exceptions-free C++;
  EnTT/GLM/SDL3/Luau are no-exceptions-compatible) are documented in
  dependencies.md with the exact policy per lib.
- **Terminate safety:** `std::set_terminate` handler installed at app start
  (logs, writes crash log, exits non-zero) — a safeguard so any uncaught
  throw (ours or third-party) yields a diagnosable crash, not a silent
  death. This handler is *not* an exception mechanism — it's a last-resort
  diagnostic.

## 2. Result Types (core convention)

```cpp
namespace lod {
// Status code — stable, documented, safe across the plugin ABI (§ native-api)
enum class Status : int32_t {
  Ok = 0,
  // generic
  Error,            // unspecified (message on context)
  InvalidArgument,
  NotFound,         // file/asset/handle not found
  NotSupported,     // malformed-format-but-recognized / missing feature
  OutOfMemory,
  Timeout,
  AlreadyExists,
  WouldBlock,
  // domain (ranges, per area)
  // 100s: io/asset   200s: decode   300s: gpu   400s: audio
  // 500s: save       600s: mod      700s: script 800s: plugin
  ...
};

// Result<T> — small, non-throwing, movable/copyable
template <class T>
class Result {
 public:
  // construction from T (success) or Status (failure)
  // value() -> const T& (UB on failure — call hasValue() first; document)
  // error() -> Status
  // map/mapError combinators (constexpr-friendly)
  bool hasValue() const;
  Status error() const;
  T& value(); const T& value() const;
  ...
};
using StatusResult = Result<std::monostate>;   // fire-and-forget ops
```

- **Prefer `std::expected`?** Requirement names it as acceptable; we adopt
  `std::expected<T, Status>` from C++23 directly **for internal engine code**
  (it's in `<expected>`, no dependencies, and `std::unexpected` +
  `std::unexpect` is clean), and use our own tiny `Result<T>` **only where we
  need pre-C++23-ish brevity or the plugin ABI docs need a simpler type**.
  Decision: **use `std::expected` everywhere internal** (the requirement
  explicitly allows it); `Result<T>` exists as a documented alias when a
  non-standard type is preferable (e.g. no `<expected>` on an old toolchain).
  Status codes are identical.
- **API boundaries** (subsystem→subsystem): every fallible function returns
  `std::expected<T, Status>` or `Status`; constructors that can fail return
  `std::expected<T, Status>` (no throwing constructors, ever).
- **I/O:** all file/asset operations return `expected<AssetData, Status>`;
  no `std::filesystem` exceptions (use the non-throwing overloads; catch
  `filesystem_error` is a `try` — FORBIDDEN: use
  `std::filesystem::is_status_error_code`-style checks via the
  non-throwing `status()` APIs, and `ec` overloads where available).
- **SDL3** already uses return codes/`SDL_GetError()` — map 1:1 to `Status`
  (an SDL error-mapping layer, unit-tested).
- **Luau VM:** protected calls with out-params (see luau-api §1) — no
  exceptions; compile diagnostics returned as structured values.
- **Plugins (C++23 API):** the same `lod::Status` enum (native-api.md §1.6) —
  plugin methods return `Status`/`std::expected<T, Status>`; no exceptions
  cross the boundary (the plugin side always compiles exception-free, and the
  engine's host layer converts anything a plugin could raise into a `Status`
  before returning —
  enforced by the SDK toolchain file). Status values are freely editable
  pre-beta; freeze discipline attaches when the plugin API ships
  (native-api.md §9).

## 3. RAII & Cleanup Discipline

- Every owned resource has an RAII type with a **value-initialization
  "null" state** that is safe to destroy (e.g. `GpuTexture{}` = no resource;
  `deinit()` on a null object is a no-op). This makes partial-init failure
  paths trivial: destroy everything, return the error.
- **Composition pattern** for multi-step init:
  ```cpp
  auto dev = make_expected<GpuDevice>(...);
  if (!dev) return dev.error();
  auto ctx = make_expected<RenderContext>(*dev);
  if (!ctx) return ctx.error();   // dev destroyed here (RAII), then return
  ```
- Shutdown = deterministic reverse teardown; each `deinit()` is idempotent
  and returns `Status` (logged on failure, never fatal to the process —
  matching the Java finally-block teardown).
- **Async work:** tasks return `expected` via a completion handle
  (`Task<T>`: `poll() -> expected<T, Status>` when ready); no exceptions can
  escape a task (task boundary wraps the callable in a no-throw harness that
  captures a forced `Status{Error}` if our own code misbehaves — via the
  terminate handler, since we can't catch).
- **Audio thread / synth:** fixed-size ring buffers; an error during a tick
  (e.g. underrun) is *logged and dropped* (audio must never block the game)
  — mirroring Java's lenient audio behavior.

## 4. Error Context & Diagnostics

- `ErrorContext` (thread-local, scoped): current operation string
  (`"load asset SECT/DRGN0.BIN/119/67"`, `"create pipeline tmd:opaque"`);
  failures log `status + context + SDL_GetError()/system message` at the
  failing site (a `LOD_FAIL(ctx, status, "message")` macro — no exceptions,
  just `log::error(...); return status;`).
- **Asset errors are first-class** (requirement §12: "explicit error results
  for malformed, missing, or unsupported assets"): each decoder returns
  specific codes (`Status::DecodeHeader`, `DecodeSizeMismatch`,
  `DecodeUnsupportedBPP`, ...) + the offset at which parsing failed; the
  asset manager logs a structured table on unpack (parity: Java threw with
  file + reason).
- **Crash path** (verified parity with `Main.java`): unhandled top-level
  failure → crash log with timestamp, loaded mods, failed
  mods, last error context, optional recovery save (version/commit fields
  arrive with product versioning).

### 4.1 Format decoders (`psx`) across the language boundary

The decoders report malformed input at *format* granularity (24 distinct codes: BPE block
size, TIM rect extent, MRG entry out of range, DEFF offsets bad, …), because that is what
a player-facing "your asset is corrupt" message and a parity bug report both need. The
engine's own `Status` is deliberately coarser — a 5-value `Decode*` family — because
callers branch on recovery strategy, not on byte offsets.

The boundary is therefore explicit and one-directional:

| Layer | Vocabulary | Why |
|---|---|---|
| C core (`psx`) | `psx_error_t` — stable numbers in per-domain ranges (psx-c-api.md §4) | ABI-stable, usable from C, no dependency on engine enums |
| C++ wrapper (`psx::cpp`) | `psx::Error` (`static_assert`-mirrored) inside `std::expected<T, psx::Error>` | Preserves precision for callers that want it |
| Engine (`lod::res`) | `lod::Status::Decode*` returned, precise name logged | Coarse control flow, precise diagnostics |

Concretely, a decode failure logs the path, the precise `psx_error_name` and the mapped
`Status`, and returns the mapped `Status` — so the precise code is never lost, only
projected. Decoders never return `lod::Status`, and the engine never inspects a raw
`psx_error_t` number in a branch: it uses `psx::Error`.

Rules that keep this honest:

- No decoder allocates an error object, formats a message, or logs. Names and messages are
  static strings in the core; formatting is the caller's job (`std::format`).
- `PSX_ERROR_INVALID_ARGUMENT` (a caller bug) is distinguishable from every
  malformed-data code, so a test can tell "the fuzzers found a bug" from "the test wrote a
  bad call".
- Adding a code means adding a name, a message, a range predicate (if new domain), and a
  row in the C consumer test — the numeric value never changes afterwards.

## 5. Failure-Semantics Table (where behavior is chosen, not accidental)

| Situation | Behavior |
|---|---|
| Missing disc during unpack | wait + status UI (parity), skippable → clean exit code 2 |
| Malformed asset (decode error) | log + skip that asset (game continues; scene shows missing) — unless it's a boot-critical asset (title UI) → show error overlay, exit 3 |
| Missing mod dependency | mod skipped, notification (parity `missingMods`) |
| Mod script compile error | mod marked errored, its hooks disabled, content kept (Java parity: `onModError`) |
| Plugin init failure | plugin failed + reported, game continues (parity failed-mod UX) |
| GPU device init failure | enumerate failures per backend, show diagnostic, exit 4 (no silent fallback to software) |
| GPU runtime error (pipeline create) | log + disable affected pipeline + fallback pipeline where one exists (e.g. no PS1-depth → modern depth, documented) |
| Audio device failure | retry/backoff, then mute + notification (never crash) |
| Save file corrupt | save slot shown invalid (parity `InvalidSavedGame`), not deleted |
| Config corrupt | back up + recreate defaults (parity `Config` load failure → defaults) |
| Luau hook timeout | warn once, disable hook for that mod |
| Plugin crash (segfault) | process dies — crash handler + log; documented limitation (native-api §4) |

## 6. Testing Hooks

- Every `Status` code is reachable by a unit test (code-coverage on the
  error paths — see testing.md).
- Fault-injection seams: IO layer has a `FaultInjector` test double (inject
  errors by path), GPU layer has a headless device for CI.
- ABI fuzz: plugin table calls with garbage args (see testing.md).
