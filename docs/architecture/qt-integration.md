# Qt 6 Desktop Integration

Qt 6 is for **desktop-facing interfaces and tools only** (requirement §9).
Player-facing in-game UI (menus, inventory, equipment, dialogue, battle HUDs)
renders through SDL3 GPU — never Qt widgets.

## 1. What Uses Qt 6

| Tool | Replaces (Java) | Process |
|---|---|---|
| **Launcher** | launch scripts + `UpdaterApplication` (JavaFX updater) + GPU selection | standalone `severed-chains-launcher` |
| **Settings UI** | in-game options *plus* global prefs (window, GPU backend, device, audio device, controller config, keybinds, mod enable/disable, presets) | standalone (shares `config.dcnf` + mod dir with the game; file-watched or lock-based sync) |
| **Mod manager** | (new; Java had none — mods were dropped in `mods/`) | standalone: list/enable/disable/validate mods (Luau + native + fate), dependency graph, install from zip, per-mod license view, error reports from last run |
| **Debugger** | JavaFX `debugger` (script, gamestate, combat, smap inspectors) | **separate process** attached to a running game (see §3) |
| **Asset inspector** | (new; Java debugger had smap model view) | standalone: open `files/` assets, decode (TMD/TIM/ANM/DABA/CContainer), preview mesh/texture/anim; export PNG (PngWriter parity) |
| **fate editor/CLI** | external `script-recompiler` tooling | CLI (C++, no Qt) + optional Qt diff viewer for fate patches |
| **Save inspector** | (new) | standalone: view/migrate saves (Tag tree view), memcard import |

## 2. Engine Independence & Coexistence

- The game engine never links Qt. Qt tools link **the engine library**
  (`scengine`) for: config access, mod manifest reading, asset decoding
  (engine `ps1` decoders — no GPU device needed for decoding), Tag
  serialization, save reading.
- **Window ownership / event loops:** game window = SDL3. Qt tools are
  separate processes (no shared-window issues at all — this is the clean
  answer to "how Qt 6 and SDL3 coexist": they don't share a process).
  *Exception (evaluated, rejected):* embedding Qt in the game process for
  settings screens would mean two event loops + Qt's own exception-using
  widgets + no real benefit (all settings are data in `config.dcnf`, fully
  editable from the Qt tool and from the in-game SDL UI which keeps the
  Java-parity options menu). The in-game options menu (SDL-rendered) remains
  for parity; the Qt settings tool is the rich editor.
- Config synchronization: single writer per process; the launcher/tools and
  the game never run as the *same* instance concurrently with writes —
  guarded by a file lock (`config.lock`, O_EXCL, stale detection) + mtime
  reload in the game (the game watches `config.dcnf` mtime per sim tick,
  cheap).
- **Exception policy in Qt code** (AGENTS.md §4: exceptions permitted but
  discouraged; error-handling.md §1):
  - Qt 6 C++ uses exceptions internally (Qt 6 is built with exceptions;
    `QString`/containers throw on bad input; the C++ API documents throw
    behavior). Qt interop is precisely the case the policy **permits**:
    tool code that calls into Qt may catch, and must preserve the diagnostic
    (`what()` / `QException` text) instead of swallowing it. Ordinary tool
    logic still returns `std::expected` / status codes.
  - **Compatibility finding (documented limitation):** Qt 6 *requires*
    exceptions for some paths (e.g. `QException`-based event filtering is
    opt-out; `qThrow` in container misuse). Practical mitigations:
    1. Use the **C-compatible parts of Qt 6** where feasible
       (QML is optional; widgets are C++ — we use widgets).
    2. Catch Qt's exceptions at the *tool* boundary (a throw escaping a Qt
       slot would terminate anyway) and keep a `std::set_terminate`
       handler as the last-resort diagnostic in the Qt host. **Under
       `SC_ENABLE_EXCEPTIONS=OFF` this layer cannot be built**: a Qt throw
       reaching one of our `-fno-exceptions` TUs is undefined behaviour, so
       the Qt-backed tools are excluded from that configuration rather than
       quietly downgraded.
    3. Therefore: **isolate all Qt widget code behind a C shim** (`qtbridge`):
       a small set of `extern "C"` entry points (init UI, pump events,
       render, handle close) where the shim's TUs are compiled **with**
       exceptions enabled (Qt itself is an approved exception-using
       dependency — precompiled third-party behavior, per requirement §3
       "do not assume compiler flags applied to project code change those
       libraries"). The shim stays `extern "C"` for API-isolation and
       LGPL/dynamic-linking reasons, and it converts failures into status
       codes at the boundary. Reusable logic (models, config access, asset
       decoding) lives outside the shim and follows the normal
       `std::expected` convention.
    4. Alternative if this proves brittle: **Qt for C (qtc)** — not mature
       enough; or drop Qt widgets for tools in favor of **Qt QML** (JS-based,
       no C++ exceptions in our TUs at all; QML engine is third-party).
       **Decision: start with widgets + the C-shim boundary; fall back to QML
       for the UI layer if the shim boundary shows instability in testing.**
       Both paths keep "Qt integration code follows the no-exceptions
       policy" true for all project-owned logic.
- Qt version: 6.x LTS (6.8/6.9 generation — pick the current LTS at
  implementation time; pin in CMake). Modules used: Core, Gui, Widgets
  (+ Network only for the launcher's update check, or do updates over the
  engine's HTTP util — keep Qt lean: Core/Gui/Widgets + optional Network).
- Licensing: **LGPL v3** (or commercial). For our AGPL-3.0 project: LGPL
  dynamic linking of Qt is the compliant path (tools link Qt dynamically
  — a license obligation; no static Qt). Documented in licensing.md.

## 3. Debugger Architecture (separate process)

```
game (engine library)
  └─ DebugServer (in-engine, socket or named-pipe; local-only, no auth needed
     on localhost; token in a runtime file)
       ├─ introspection: engine state, scenes/entities (via EnTT-safe
       │   read-only queries), battle state, script VM frames (Luau debug
       │   protocol), save state, mod list, config
       └─ commands: pause/resume, frame-advance, spawn/kill entity, set flag,
           reload shaders, set render mode (TAB-mode parity)
Qt debugger process
  └─ DebugClient → panels: gamestate viewer/editor (Java
     gamestate_editor parity), script debugger (live + step), combat
     debugger, smap model viewer, entity browser
```

- The JavaFX debuggers (script_debugger, gamestate_editor, combat_debugger,
  smap_debugger, registry_selector, script_live_debugger) map 1:1 to Qt
  panels.
- The in-engine free-cam + debug keys (pause/frame-advance/wireframe/shader
  reload) remain in the game process (SDL input) — the Qt debugger
  orchestrates them over the debug socket.

## 4. Launcher Features (parity + improvement)

- Disk image detection (4 disks, region recognition — port `Unpacker.DISK_IDS`
  + OTHER_REGION_IDS), status (unpacked version vs current, mod count).
- GPU backend/device selection (lists `SDL_GetGPUDeviceNames`), audio device
  selection (SDL3 audio device enumeration — parity with `AudioThread
  .getDevices()` hot-swap), controller test + remap.
- Mod list (enable/disable — writes the enabled-set the game reads at boot;
  parity: Java enabled all mods in `mods/`, failed ones reported; native:
  same default, plus explicit disable).
- Launch game (spawns the thin executable host with the resolved config).
- Update check (GitHub releases — parity with `Updater`), staged update
  (`updater.jar.new` parity → native: staged binary swap, same semantics).

## 5. Build/Packaging Notes

- Qt is fetched per-platform via the build system's package manager policy
  (see build-system.md: vcpkg on Windows, system/brew on Linux/macOS —
  decision + pinning there).
- Qt tools are separate install targets (`severed-chains-tools`); the game
  itself ships without Qt (smaller attack surface; a game-only install works
  fully — in-game menus cover all player needs).
- Windows: Qt DLLs alongside the tool; macOS: framework bundle; Linux:
  system libqt6.

## 6. Open Questions

- Qt 6.8 vs 6.9 vs next LTS: pick at M10 implementation (roadmap) — pin
  then; the design is version-agnostic (Core/Gui/Widgets stable API).
- Whether the asset inspector needs GPU preview (mesh render) — if yes, it
  links the engine GPU device headless (SDL3 GPU supports offscreen
  rendering without a window: `SDL_CreateGPUDevice` + offscreen texture —
  documented SDL capability; no Qt rendering involved).
