# Severed Chains Native — Application Foundation + Resource Layer

Native C++23 reimplementation of *The Legend of Dragoon: Severed Chains*.
Authoritative specs: `AGENTS.md` + `docs/` (guidelines are mandatory).

Implemented so far: the application foundation of `docs/architecture/roadmap.md`
M1 (lifecycle, window, config, timing, diagnostics) and the front of M6 —
resource discovery, ownership and validation over an extracted game tree
(`lod::res`), with the `psx` library's BPE / MRG / DEFF / TIM modules behind it —
a **C17 core** (`psx::psx`, stable C ABI) plus header-only **C++23 wrappers**
(`psx::cpp`) that the engine uses.
No rendering, audio or gameplay yet; no game data ships here (players supply
their own legally obtained data).

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure   # incl. repo.naming (naming lint) + repo.format (style gate)
./build/bin/lodsc --ticks 60        # headless lifecycle demo, exits 0
```

`psx` is a standalone-capable subproject whose only dependency is the libc allocator —
it builds, tests and exports itself on its own:

```sh
cmake -S psx -B build-psx -G Ninja && cmake --build build-psx && ctest --test-dir build-psx
# old-vs-new equivalence against the pre-migration implementation (psx-c-api.md 10):
cmake -S psx -B build-psx -G Ninja -DPSX_ENABLE_DIFFERENTIAL_TEST=ON
```

Its suite has four consumers: the C++23 wrapper runner (`sc_tests_psx`), a **pure C17**
program (`sc_psx_c_consumer`), an exported-symbol audit (`psx.abi.symbols`) and the
opt-in differential corpus (`psx.differential`).

Build options: `SC_ENABLE_TESTS` (ON), `SC_ENABLE_EXCEPTIONS` (ON),
`SC_WERROR` (OFF; CI sets ON), `SC_ENABLE_ASAN/UBSAN/TSAN`,
`SC_DEPS_FETCHCONTENT` (OFF; builds SDL3 3.4.18 from source — what the Windows
legs and the source-deps Linux leg do),
`BUILD_SHARED_LIBS` (unset). `psx` alone: `PSX_SHARED` (defaults to
`BUILD_SHARED_LIBS`, shared when unset), `PSX_ENABLE_TESTS` (ON),
`PSX_ENABLE_INSTALL` (ON), `PSX_ENABLE_DIFFERENTIAL_TEST` (OFF). Configure fails fast if the toolchain lacks
C++23 `std::expected`/`std::print`, or if probe-verified policies do not
match their flags.

## Continuous integration

`.github/workflows/ci.yml` runs seventeen build-and-test legs plus a `repo-gates`
job for the naming and formatting lints. Legs are ordered by verification reach,
Apple Silicon first: 4 × macOS arm64 (Metal host, both exception policies + the
differential corpus + a sanitizer lane), 8 × Linux x86_64 (GCC and Clang, both
policies, + SDL3 from source, + differential, + a sanitizer lane that also runs leak
detection, which macOS cannot do), 3 × Linux arm64 on native ARM runners, and 2 ×
Windows MSVC. SDL3 comes from `libsdl3-dev` / `brew sdl3` when the package is >= 3.2,
otherwise from the pinned source build; a too-old package fails the configure
rather than silently building against an unverified version. Linux toolchains are
probed, not assumed: a leg labelled `clang` fails loudly if no working clang is
found instead of falling back to GCC. Every leg builds with `-DSC_WERROR=ON`, runs
`ctest`, and re-tests `psx` as a standalone project. Reproduce one leg:

```sh
# exceptions-off, warnings-as-errors (what ci.yml builds on every OS)
cmake -B build-exc-off -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSC_ENABLE_EXCEPTIONS=OFF -DSC_WERROR=ON
cmake --build build-exc-off && ctest --test-dir build-exc-off

# SDL3 from source instead of a system package
cmake -B build-src -G Ninja -DSC_DEPS_FETCHCONTENT=ON

# the Apple-Silicon legs, exactly as ci.yml configures them on macos-15
cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSC_WERROR=ON -DSC_ENABLE_EXCEPTIONS=ON -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-mac && ctest --test-dir build-mac

# the sanitizer lane (ci.yml runs this on macOS arm64 and both Linux flavours)
cmake -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSC_WERROR=ON -DSC_ENABLE_ASAN=ON -DSC_ENABLE_UBSAN=ON
cmake --build build-san
# leak detection is unsupported on macOS and makes ASan abort, hence 0 here;
# the Linux legs run with detect_leaks=1
cd build-san && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --output-on-failure
```

## Run

```sh
./build/bin/lodsc [options]
  --ticks <N>        sim ticks before exiting; 0 = run until quit
                     (default: 0 with --windowed, otherwise 1)
  --tick-hz <F>      fixed sim rate (default: config tick_hz)
  --windowed         real video driver (default: headless = SDL dummy driver)
  --headless         force the dummy driver (overrides --windowed)
  --width <N> / --height <N>   window size (overrides config)
  --config <P>       config file (default: <configDir>/config.txt)
  --crash-dir <D>    crash-log directory (default: <dataDir>/crashes)
  --no-config-write  never create/rewrite the config file
  --resources <D>    resource root (default: resolved resource dir)
  --validate-assets  discover + validate the resource tree, then exit
  --inspect <P>      validate one root-relative resource and exit (repeatable)
  --help             usage

asset exit codes: 0 ok, 4 root unusable, 5 a resource failed to validate
```

`--validate-assets` / `--inspect` need no window, audio device or game data: they
report what a tree contains and whether each file parses, e.g.
`SUBMAP/x.tim: TIM 4bpp, image 4x2 (16 B), 3 B trailing, clut 4 entries (8 B)`.

Runtime directories resolve as: `SC_RESOURCE_DIR` / `SC_CONFIG_DIR` /
`SC_DATA_DIR` environment overrides → bundle-relative (`Contents/Resources`) →
platform user-data dir → executable dir. A resource root can also be pinned in
`config.txt` (`resource_dir /path/to/tree`), and `--resources` overrides both. The config file is `key value` text
(`#` comments) and is created on first run; malformed lines fall back to their
default per key, and an unreadable file degrades to defaults with a warning —
bad configuration never blocks startup.

## Layout

```
CMakeLists.txt          root: C++23, subdirs
cmake/                  ScToolchain (warnings, exception/RTTI policy,
                        sanitizer flags, configure-time probes), ScDeps (SDL3),
                        ScPackaging (install)
psx/                    reusable PSX library — C17 core + C++23 wrappers, libc-only
                        deps, standalone: include/psx/*.h (C ABI: export, version,
                        error, bytes, bpe, tim, archive) + include/psx/cpp/*.hpp
                        (psx:: wrappers: std::expected, spans, RAII handles),
                        src/*.c + src/reader.h (checked LE),
                        migration/ (frozen pre-C implementation + differential corpus)
src/engine/             scengine (namespace lod): status, log, diagnostics,
                        frame events, fs/paths (+ bounded binary reads), config,
                        platform (SDL3 window), app lifecycle (ordered init +
                        rollback, clamped fixed-step loop with quit/resize/focus
                        handling), resources (root validation, RAII blobs, path
                        confinement, discovery, psx-backed validation)
src/game/               scgame (namespace game): GameApp
src/host/               lodsc — thin host: CLI parsing + wiring only
tests/                  framework/ (LOD_* harness) + engine/ (CTest) + repo/
                        (static checks: naming lint);
                        psx/tests/ has its own PSX_* harness (extractable) plus the
                        pure-C consumer and the ABI symbol audit
docs/research/psx-formats.md   byte layouts + provenance for implemented formats
docs/architecture/resource-management.md  roots, ownership, validation policy
```

## Policy notes (pre-beta)

- **Exceptions**: permitted but discouraged; `std::expected<T, Status>` is
  the primary error path (AGENTS.md §4). `SC_ENABLE_EXCEPTIONS=OFF` applies
  `-fno-exceptions` and a configure-time probe verifies `throw` then fails to
  compile; the whole tree (including the terminate handler, which is guarded)
  builds under both policies. `psx` and the plugin SDK are exception-free
  **always** — for `psx` structurally (the core is C17; its C++ wrapper/test targets link
  `sc::exceptions_off` + their own probe), for the SDK by flag — because there it is a
  portability/ABI property, not a preference.
- **RTTI**: disabled on public-API targets (`psx` C++ surface) per AGENTS.md.
- **`psx` ABI**: exported symbols are audited (`psx.abi.symbols`) — the core must export
  exactly the documented `psx_*` functions and reference no C++ runtime symbol; error-code
  numbers and struct layouts are frozen behind `PSX_ABI_VERSION` (psx-c-api.md §6, §9).
- **Versioning / compatibility**: none until beta — no project version,
  version report, or compat shims. Reintroduce with the plugin SDK (M5).
- **Naming**: per the AGENTS.md table — PascalCase types/functions/enum
  values, camelCase variables and parameters. **Acronyms are upper case** in
  types, functions, constants and enum values (`CLIOptions`, `Status::IOOpen`,
  `DecompressBPE`), and inside variables (`deviceGPU`); the single exception is
  a variable whose name *starts* with the acronym, which stays camelCase
  (`gpuDevice`, `idTable`). Truncations are not acronyms (`SDLWindowPtr`).
  `python3 tools/check_naming.py` enforces this on code **and** on the names the
  docs prescribe, and runs as the CTest entry `repo.naming`.
- **Formatting**: `.clang-format` is authoritative — run
  `clang-format -i <touched files>` (CI checks the whole tree). Single-statement
  `if`/`else`/`for`/`while` bodies carry **no braces**; `python3 tools/check_format.py`
  enforces the whole file as the CTest entry `repo.format`.
