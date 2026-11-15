# Original-identifier map — `psxrt`

This is the reverse-engineering map for the crate (the audit obligation it satisfies is
`planning/05-verification-strategy.md` §8.3). It exists because production Rust must not carry
decompiler or reference artefacts as identifiers, while its comments still have to cite them so a
reader can verify a claim. Every original identifier that appears in a source-reference comment
anywhere in this crate is listed here with the Rust item that now does the work and the research
document that established it.

Scope note: this is one map for the whole crate rather than one per module. The reason is size —
ten entries across three modules — not a preference for centralisation. When a module grows its own
map, the rule stays the same: an identifier in a comment without a line here is a gate failure, not
a documentation gap.

Reference paths are relative to `~/Documents/GitHub/Severed-Chains` at the pinned revision
`71ce130364ed00431d420891370d5314a4e3d4c4`. Reference names are kept here in their original
spelling on purpose: they are what a reader greps for.

## Fixed-point trig (`fixedpoint`)

| Original | Where it appears in the reference | Rust item that carries the behaviour | Established in |
|---|---|---|---|
| `rsin(int theta)` | `src/main/java/legend/game/Scus94491BpeSegment.java:250` | `fixedpoint::rsin` — kept because it names the operation (fixed-point sine, amplitude 4096) rather than an address or an offset, and because keeping it leaves the reference's call sites greppable from the Rust side. Whether the name is the game's own or a decompiler label is not established here | `research/psx/02-gte.md`, `research/scripting/01-fate-language.md` (the `SIN_12` op body) |
| `rcos(int theta)` | `src/main/java/legend/game/Scus94491BpeSegment.java:256` | `fixedpoint::rcos` — same reasoning | as above |
| `psxDegToRad(int/float)` | `src/main/java/legend/core/MathHelper.java:190,194` | `fixedpoint::psx_deg_to_rad`, with the constant `PSX_DEG_TO_RAD` pinned at its float bit pattern | `research/psx/02-gte.md` |
| `MathHelper.sin` / `MathHelper.cos` | `src/main/java/legend/core/MathHelper.java` | `fixedpoint::sine` / `fixedpoint::cosine` (crate-private), plus `scale_to_fixed` for the `(short)` cast — the float stage the reference evaluates at single precision before narrowing | `research/psx/02-gte.md` |
| `MathHelper.makeTpage(...)` and `Graphics.GetTPage(...)` | `src/main/java/legend/core/MathHelper.java` (used from `QuadBuilder`/`PolyBuilder`/`McqBuilder`) and the `GetTPage` port | **no Rust item.** `crates/psxrt/src/bpp.rs` documents that the reference carries two page-word encoders and that they disagree (the second page-height bit lands on the translucency bit in one and on the depth code in the other), and stops there: the crate models the depth field, not the page word. The packing itself is I5's, with the ordering table it feeds | `research/psx/01-gpu-vram-ordering.md`, `research/rendering/02-tmd-pipeline.md` |

## Random streams (`rng`)

| Original | Where it appears in the reference | Rust item that carries the behaviour | Established in |
|---|---|---|---|
| `legend.core.Random` | `src/main/java/legend/core/Random.java` | `rng::random48::Random48` plus the `rng::RandStream` trait | `research/cross/01-architecture-review.md` (the RNG inventory), the P0 battle-RNG correction in `planning/README.md` §2 |
| `Random.mix32(long z)` | `src/main/java/legend/core/Random.java:8` | **not ported as a name and not reachable as behaviour**: it is a `nextInt()` override that no consumer calls. `rng::random48::Random48::next_u32_mixed` exists only because the *battle* stream's observable output depends on it when a caller asks for a 32-bit draw; the sign-extension of its argument is pinned by the golden fixture | same; the dead-code finding is the P0 correction |
| `simpleRand()` | `src/main/java/legend/game/Scus94491BpeSegment.java:225` | `rng::lcg16::Lcg16::next_raw` (and `next_guarded` for the vsync-sourced reseed branch), exposed through `rng::RandStream` | `research/psx/02-gte.md`, `research/game/05b-battle-entities-and-formulas.md` |

## Pixel formats (`bpp`)

| Original | Where it appears in the reference | Rust item that carries the behaviour | Established in |
|---|---|---|---|
| `Bpp` (the class, `Bpp.BITS_4` … `BITS_24`) | `src/main/java/legend/core/gpu/Bpp.java` | `bpp::Bpp` — the enum keeps the reference's word because "Bpp" is the format's own term for bits-per-pixel, and `code`/`from_code` carry the wire values | `research/assets/06-tim-texture-format.md`, `research/psx/01-gpu-vram-ordering.md` |
| `Graphics.GetTPage`'s source address `0x8003b3f0`, the sine table address `80015c80` | cited in `bpp.rs` and `fixedpoint.rs` module headers | addresses, not identifiers: kept in the comments with their address-space classification, and neither is implemented | `research/psx/01-gpu-vram-ordering.md`, `research/psx/02-gte.md` |

## How to check it by hand until the scanner exists

The two scans that `planning/05` §8.2/§8.3 require are not automated yet (no scanner was in I0's
scope; it is listed as open in the I0 gate record). Until then:

```
# placeholder-pattern classes (AGENTS.md list) — expect no hits in production identifiers
grep -rniE '\b(fun_|LAB_|DAT_|undefined[0-9]?|local_[0-9]|param_[0-9]|unk_|unknown_|_[0-9a-f]{6,}|field_[0-9a-f]+|struct[0-9])' \
  crates/*/src crates/*/tests --include='*.rs'
# original identifiers used in comments — every hit needs a line above
grep -rnoE '\b[A-Z][a-z0-9]+[A-Z][A-Za-z0-9]+\b' crates/psxrt/src
```

The first command's only hit today is the English word "undefined" inside a doc comment. The
second lists the crate's own type names alongside the two reference class names above; the check
is that each *cited original* has a row, not that the pattern is empty.
