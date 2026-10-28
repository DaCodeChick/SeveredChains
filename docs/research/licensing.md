# Licensing Research

## 1. Reference Project License (verified)

- `~/Documents/GitHub/Severed-Chains/LICENSE` = full **GNU AGPL-3.0** text
  (661 lines, standard FSF wording). Identical file exists in this native
  repo's initial commit (verified byte-identical header; same size).
- `CREDITS`/`credits.txt` attribute the work (Monoxide lead, community
  contributors, third-party art: arks.itch.io controller icons).
- README states: "This is not an emulator, but assembly code disassembled
  and rewritten in Java. AI was not used..."; assets are user-provided
  (discs), no official code/assets in the repo.
- Third-party deps with their own licenses (from build.gradle): LWJGL
  (Zlib), JOML (MIT), JavaFX (GPL v2 w/ classpath exception), log4j (Apache
  2.0), JSON (MIT), Gson (Apache 2.0), fastutil (Dual AL/LGPL), JNA (LGPL
  2.1/Apache dual), discord-sdk4j (see its repo), javacv/ffmpeg (LGPL/GPL
  mix), opencsv (Apache 2.0), java-diff-utils (Apache 2.0), commons-io
  (Apache 2.0), slugify (MIT), JSR-305 (Apache 2.0), stb (public domain),
  plus the org.legendofdragoon jitpack libs (Mod-Loader `c692134`,
  Script-Recompiler `3433e6f`, Dig-Dabas `4c1fbfe` — **all three inspected
  read-only in the Phase 0 follow-up; all AGPL-3.0, verified** — see
  docs/research/mod-loader.md, script-recompiler.md, dig-dabas.md).

## 2. Status of This Project

- **Independent derivative implementation.** The code is being rewritten
  from scratch in C++23; we do not copy Java source. However:
  - It **derives from** the same underlying work (a reverse engineering of
    the same game, with the same design of enhancements/modding) and,
    practically, from the reference project's *behavioral specification*.
  - AGPL-3.0's copyleft attaches to the *work* (the Java implementation).
    A clean-room-style rewrite that uses the reference only as a
    behavioral reference does not automatically inherit AGPL — **but this
    project has explicitly chosen to keep the AGPL-3.0 LICENSE file in the
    native repo** (it's in the initial commit), which we read as the
    project owner's intent to release the native project under AGPL-3.0 as
    well (consistent with "independent derivative" + the ecosystem's
    expectations). **This intent should be confirmed with the project
    owner** (documented as a legal-review item, §6).
  - The original *Legend of Dragoon* game is Square/Sega IP. The project
    (like the Java original) distributes **no game data and no game code
    copied from the ROM** — only user-supplied data is used. The
    disassembly-derived *knowledge* (formats, formulas, timings) is the
    same category as the Java project's position; no new legal exposure is
    created by the rewrite beyond what the Java project already asserts.
    (This is the same posture as the Java project's README; not legal
    advice — see §6.)

## 3. AGPL-3.0 Obligations for the Native Project (if released under AGPL)

1. **Source availability:** the complete corresponding source of the
   program (engine, game, tools, plugins we distribute) must be offered to
   users of the binary (or via network access if SaaS — not our case; we
   ship desktop binaries) under AGPL or a compatible license.
2. **Preserve licenses/copyright notices** (our LICENSE, third-party
   notices).
3. **State changes** (if we modify dependencies' source) — we don't plan
   to.
4. **Copyleft:** derivative works of *our* code must be AGPL. Users who
   link our AGPL engine into their own mod tools/plugins: **plugins are
   separate works** — the C++23 plugin API boundary (native-api.md) is
   designed so that a mod's plugin is a standalone program communicating
   with the engine through a stable, documented, versioned interface
   (AGPL §4/§5 analysis: the boundary must be "at arm's length" — the
   frozen interface classes + toolchain contract + opaque engine objects
   satisfy this for the intended use; document this analysis in the plugin
   docs).
5. **Attribution:** preserve the credits; the native README should credit
   Severed Chains (Java) as the behavioral reference and list the original
   project's authors (parity with `credits.txt`).
6. **Distribution:** no "no distribution" restriction; but **game data
   must never be bundled** (proprietary Square/Sega assets) — same rule as
   the Java project; the launcher must require user-provided discs.

## 4. Dependency Licensing (native project)

| Dependency | License | Compliance notes |
|---|---|---|
| SDL3 | zlib | permissive; keep the copyright notice in dists |
| GLM | MIT | keep notice |
| EnTT | MIT | keep notice |
| Luau | MIT | keep notice |
| Qt 6 | **LGPL v3** (or commercial) | **dynamic linking only** for the Qt-using tools (no static linking, no "static Qt" binaries without an exception); keep Qt's LICENSE files in the tools' distribution; state that the tools may be relinked against other Qt versions (LGPL static-link exception is not needed because we dynamic-link). The *game* doesn't link Qt at all (cleanest possible posture) |
| SDL3_image | zlib (same as SDL3) | permissive; keep the copyright notice in dists |
| glslang/SPIRV-Cross/DXC | MIT (build-time) | notices in repo (we link nothing at runtime) |
| libopus (if approved) | BSD | keep notice |
| libcurl (if approved) | MIT + OpenSSL (Apache/SSLeay) or platform TLS | keep notices; OpenSSL dual license fine |
| ffmpeg/libav (if approved) | LGPL (avoid GPL filters/components) | dynamic link; avoid GPL-enabled builds |
| gamecontrollerdb.txt (SDL) | same as SDL (zlib) | fine to ship |

**Qt-specific (documented limitation, per requirement §3):** Qt 6's C++
API uses exceptions internally; our no-exceptions policy is applied to
*project-owned* code (see qt-integration.md §2) — Qt's own compiled objects
retain their exception behavior; this is a documented third-party behavior,
not a policy violation.

## 5. Mod Licensing

- **Do not assume mods share our license** (requirement §16): each mod
  package carries its own `license` field (SPDX id) in `mod.toml`
  (native-api.md manifest; luau-api.md package format). The game/mod
  manager displays it; the AGPL copyleft does *not* reach mod code that
  lives outside our program (separate works, arm's-length API — same
  analysis as §3.4 for native plugins; Luau mods are data/scripts loaded at
  runtime — also separate works).
- Bundled/first-party mods (`lod` content module, sample plugin) are ours
  → AGPL (or chosen project license) + attribution.
- Community ports of existing Java mods: each mod author chooses their
  license; our format must support *any* license metadata (it does).
- **Asset overrides** in mods: the author is responsible for their assets'
  licensing (original LoD assets = Square/Sega — fan asset redistribution
  is the author's legal exposure, same as in the Java project; we provide
  no tooling that bundles assets into our distribution).

## 6. Items Requiring Legal Review / Owner Confirmation

1. **Confirm the native project's release license** = AGPL-3.0 (the LICENSE
   file is already present; confirm intent) or a different choice (e.g.
   GPL-3.0) — this document's obligations analysis assumes AGPL-3.0.
2. Confirm the "independent derivative / behavioral reference only"
   posture is acceptable to the reference project's maintainers (good
   faith: they may want attribution language; `credits.txt` parity is
   planned).
3. ~~The org.legendofdragoon jitpack libraries' licenses (mod-loader,
   script-recompiler, mogul-dabas) — verify before reusing *their code or
   data tables*~~ — **RESOLVED (Phase 0 follow-up)**: all three source
   repositories inspected (commits above); all AGPL-3.0. Our clean
   reimplementation inside this AGPL project + attribution is the copyleft-
   compliant posture. Residual note: the reference build's bundled
   `patches/` corpus + meta CSVs are *data* — when the native port ships
   equivalent data, keep attribution to the source project (credits).
4. Proprietary game data: reconfirm the user-supplied-disc model for all
   platforms (including any online update path — the updater must never
   fetch game data).
5. FMV/intro.mp4: the bundled intro video's provenance (human-made per
   credits: Tyler Frenette intro) — verify the license grants reuse in the
   native project before shipping it.
6. Qt LGPL: our dynamic-link posture is standard, but have counsel review
   the qtbridge C-shim (exceptions-enabled TUs) for any LGPL static-link
   appearance (it's dynamic; expected clean).
7. Trademarks: "The Legend of Dragoon" / "Severed Chains" names — keep the
   descriptive-use posture of the original project (fan project naming);
   no new exposure, but flag for the owner.

**Not legal advice.** The above is engineering-side documentation of the
obligations and open items; final sign-off is a legal-review action.
