//! Exhaustive checks of the two channel-level colour operations, over every possible input.
//!
//! Both operations are cheap and both have a clamp, which makes them exactly the kind of code
//! where sampling finds nothing: the interesting inputs are scattered across the whole square,
//! not clustered at the corners. So these tests walk all 65,536 channel pairs of each operation
//! for every mode, rather than picking examples.
//!
//! There are two independent checks per sweep.
//!
//! The first compares each result against an expression typed out from
//! `legend/core/gpu/Gpu.java` — `handleTranslucence` for the four translucency equations,
//! `applyBlending` for modulation. That catches a mistranslation of an equation.
//!
//! The second compares a 64-bit FNV-1a digest of the whole sweep against a constant computed
//! once, outside this repository, from the Java expressions. The digest is here because the
//! first check compares the Rust code against something written in Rust: if a reading of the
//! Java were wrong in the same way in both places, the first check would pass anyway. The
//! digest cannot be wrong that way, and it pins all 65,536 values rather than their pattern.
//!
//! To reproduce a digest, redo the sweep in any language over `(background, foreground)` pairs
//! in that order — background ascending in the outer loop, foreground ascending in the inner —
//! feeding one byte per pair into FNV-1a 64 from `0xcbf29ce484222325` with prime
//! `0x100_0000_01b3`. The `census` test pins the counts that make the sweep worth doing.

use psxrt::{BlendMode, Rgb8, modulate};

const FNV_OFFSET: u64 = 0xcbf2_9ce4_8422_2325;
const FNV_PRIME: u64 = 0x100_0000_01b3;

fn digest_update(state: &mut u64, byte: u8) {
    *state ^= u64::from(byte);
    *state = state.wrapping_mul(FNV_PRIME);
}

/// Every channel pair: background ascending outside, foreground ascending inside.
fn channel_pairs() -> impl Iterator<Item = (u8, u8)> {
    (0_u16..256).flat_map(move |b| (0_u16..256).map(move |f| (b as u8, f as u8)))
}

/// The four equations as the reference writes them, transcribed from
/// `Gpu.handleTranslucence`. `Math.min(0xff, …)` and `Math.max(0, …)` are Java's own clamps;
/// the quarter-bright mode divides rather than shifts, and integer division truncates.
///
/// The subtractive case is written as a wrapping subtraction so that the "underflow" branch is
/// visible as a value below zero rather than as a large positive one; the clamp then applies to
/// the signed result, which is what `Math.max(0, …)` does.
fn reference_equation(mode: BlendMode, background: u8, foreground: u8) -> i32 {
    let background = i32::from(background);
    let foreground = i32::from(foreground);
    match mode {
        BlendMode::HalfBackgroundPlusHalfForeground => (background + foreground) / 2,
        BlendMode::BackgroundPlusForeground => background + foreground,
        BlendMode::BackgroundMinusForeground => background - foreground,
        BlendMode::BackgroundPlusQuarterForeground => background + foreground / 4,
    }
}

/// Digests of each mode's full sweep, computed from the Java expressions outside this
/// repository.
const MODE_DIGESTS: [(BlendMode, u64); 4] = [
    (
        BlendMode::HalfBackgroundPlusHalfForeground,
        0xcbd9_f7a8_6de1_7925,
    ),
    (BlendMode::BackgroundPlusForeground, 0x5427_29fc_66b2_3fa5),
    (BlendMode::BackgroundMinusForeground, 0x085b_3cc2_9720_52a5),
    (
        BlendMode::BackgroundPlusQuarterForeground,
        0xb3c0_faa7_1169_cc25,
    ),
];

#[test]
fn every_channel_pair_matches_the_reference_equation_in_every_mode() {
    for (mode, expected_digest) in MODE_DIGESTS {
        let mut state = FNV_OFFSET;
        let mut mismatches: Vec<String> = Vec::new();

        for (background, foreground) in channel_pairs() {
            let actual = mode.channel(background, foreground);
            let expected = reference_equation(mode, background, foreground).clamp(0, 255) as u8;
            if actual != expected && mismatches.len() < 10 {
                mismatches.push(format!(
                    "{background} with {foreground}: got {actual}, reference says {expected}"
                ));
            }
            digest_update(&mut state, actual);
        }

        assert!(
            mismatches.is_empty(),
            "mode {} disagrees with the transcribed equation at {mismatches:?}",
            mode.ordinal()
        );
        assert_eq!(
            state, expected_digest,
            "mode {} does not match the digest computed from the Java expressions",
            mode.ordinal()
        );
    }
}

/// How often each mode leaves the byte range, and how often the half mode truncates. These are
/// the counts the sweep exists to cover: if a mode never saturated, its clamp would be dead
/// code, and if the half mode never saw an odd sum, the difference between dividing and
/// shifting would never have shown up.
#[test]
fn the_sweeps_actually_exercise_the_clamps_and_the_truncation() {
    let pairs: Vec<(u8, u8)> = channel_pairs().collect();
    assert_eq!(pairs.len(), 65_536);

    let addive_over = pairs
        .iter()
        .filter(|&&(b, f)| u32::from(b) + u32::from(f) > 255)
        .count();
    let subtractive_under = pairs
        .iter()
        .filter(|&&(b, f)| i32::from(b) - i32::from(f) < 0)
        .count();
    let quarter_over = pairs
        .iter()
        .filter(|&&(b, f)| u32::from(b) + u32::from(f) / 4 > 255)
        .count();
    let odd_sum = pairs
        .iter()
        .filter(|&&(b, f)| (u16::from(b) + u16::from(f)) % 2 == 1)
        .count();

    assert_eq!(addive_over, 32_640, "additive pairs that overflow a byte");
    assert_eq!(subtractive_under, 32_640, "pairs where subtraction goes negative");
    assert_eq!(quarter_over, 8_064, "quarter-bright pairs that overflow a byte");
    assert_eq!(odd_sum, 32_768, "exactly half the pairs have an odd sum");
}

/// The half mode must truncate, and the quarter mode must take the quarter before adding.
///
/// A right shift and a division by two agree on non-negative sums, so this pins the shape of the
/// expression rather than its value. It matters because both spellings appear in the reference
/// for what looks like the same thing, and only one of them is used here.
#[test]
fn halving_truncates_and_the_quarter_is_taken_before_the_addition() {
    for (background, foreground) in channel_pairs() {
        let sum = u32::from(background) + u32::from(foreground);
        let half = BlendMode::HalfBackgroundPlusHalfForeground.channel(background, foreground);
        assert_eq!(
            u32::from(half),
            sum / 2,
            "({background} + {foreground}) / 2"
        );

        let quarter = BlendMode::BackgroundPlusQuarterForeground.channel(background, foreground);
        assert_eq!(
            u32::from(quarter),
            (u32::from(background) + u32::from(foreground) / 4).min(255),
            "{background} + {foreground} / 4"
        );
    }
}

/// A handful of hand-checked values, so that a failure above has a small reproducible case next
/// to it.
#[test]
fn pinned_channel_vectors() {
    let cases: [(BlendMode, u8, u8, u8); 12] = [
        (BlendMode::HalfBackgroundPlusHalfForeground, 255, 255, 255),
        (BlendMode::HalfBackgroundPlusHalfForeground, 255, 1, 128),
        (BlendMode::HalfBackgroundPlusHalfForeground, 0, 255, 127),
        (BlendMode::HalfBackgroundPlusHalfForeground, 128, 127, 127),
        (BlendMode::BackgroundPlusForeground, 200, 200, 255),
        (BlendMode::BackgroundPlusForeground, 128, 127, 255),
        (BlendMode::BackgroundPlusForeground, 100, 100, 200),
        (BlendMode::BackgroundMinusForeground, 255, 1, 254),
        (BlendMode::BackgroundMinusForeground, 128, 127, 1),
        (BlendMode::BackgroundMinusForeground, 1, 254, 0),
        (BlendMode::BackgroundPlusQuarterForeground, 0, 255, 63),
        (BlendMode::BackgroundPlusQuarterForeground, 1, 254, 64),
    ];

    for (mode, background, foreground, expected) in cases {
        assert_eq!(
            mode.channel(background, foreground),
            expected,
            "mode {}, {background} with {foreground}",
            mode.ordinal()
        );
    }
}

#[test]
fn blending_three_channels_matches_blending_one() {
    // The three-channel form is only safe if it is the per-channel form with no carry between
    // channels. A packed implementation would carry at 255; this is what would catch it.
    for (background, foreground) in channel_pairs() {
        for mode in BlendMode::ALL {
            let mixed = mode.channel(background, foreground);
            let triple = mode.apply(
                Rgb8::new(background, background, background),
                Rgb8::new(foreground, foreground, foreground),
            );
            assert_eq!(triple, Rgb8::new(mixed, mixed, mixed));
        }
    }
}

/// Digest of the full modulation sweep, computed from `Gpu.applyBlending` outside this
/// repository.
const MODULATE_DIGEST: u64 = 0x21b9_9d90_582d_467e;

#[test]
fn modulation_over_every_vertex_and_texel_pair() {
    let mut state = FNV_OFFSET;
    let mut mismatches: Vec<String> = Vec::new();
    let mut saturated = 0_usize;

    for (vertex, texel) in channel_pairs() {
        // `Math.min((c * t) >>> 7, 0xff)` — the product is taken at full width, then shifted,
        // then clamped. Computing the product in eight bits would wrap instead of saturate, and
        // a float `/ 128.0` with round-to-nearest would differ wherever the product is odd.
        let product = u32::from(vertex) * u32::from(texel);
        let expected = ((product >> 7).min(255)) as u8;
        let actual = modulate(Rgb8::new(vertex, 0, 0), Rgb8::new(texel, 0, 0)).r;
        if actual != expected && mismatches.len() < 10 {
            mismatches.push(format!("{vertex} * {texel}: {actual} vs {expected}"));
        }
        if product >= 256 * 128 {
            saturated += 1;
        }
        digest_update(&mut state, actual);
    }

    assert!(mismatches.is_empty(), "modulation: {mismatches:?}");
    assert_eq!(state, MODULATE_DIGEST, "modulation digest");
    assert_eq!(
        saturated, 9_918,
        "pairs where modulation saturates — a clamp that never fires would be a silent \
         difference from the reference"
    );
}

/// A texel channel of exactly 128 reproduces the vertex channel, because the scale divides by
/// 128. Anything from 129 up saturates a full-brightness vertex, and the largest product of
/// them all is what the clamp exists for.
#[test]
fn modulation_identity_at_128_and_saturation_above_it() {
    for vertex in 0_u8..=255 {
        assert_eq!(
            modulate(Rgb8::new(vertex, 0, 0), Rgb8::new(128, 0, 0)).r,
            vertex,
            "a texel of 128 must reproduce the vertex exactly"
        );
        for texel in 129_u8..=255 {
            assert_eq!(
                modulate(Rgb8::new(255, 0, 0), Rgb8::new(texel, 0, 0)).r,
                255,
                "255 * {texel} must saturate, not wrap"
            );
        }
    }
}

/// The one case where narrowing the product to eight bits would not merely lose precision but
/// produce a completely different colour. Kept as a named regression against a plausible
/// "simplification".
#[test]
fn a_narrowed_product_wraps_and_is_not_equivalent() {
    let vertex = 255_u32;
    let texel = 255_u32;
    let wide = (vertex * texel) >> 7;
    let narrowed = ((vertex * texel) as u8) >> 7;
    assert_eq!(wide, 508);
    assert_eq!(narrowed, 0, "65025 truncated to a byte is 1, and 1 >> 7 is 0");
    assert_eq!(
        modulate(Rgb8::new(vertex as u8, 0, 0), Rgb8::new(texel as u8, 0, 0)).r,
        255
    );
}

/// Byte order, pinned rather than assumed.
///
/// Packing and unpacking are inverses, so a round-trip test alone would pass on a swapped
/// order. What is pinned here is which byte is which: the reference's draw buffer holds
/// blue-high (`0xBBGGRR`), and the blend equations read the red channel out of the low byte. A
/// swap would be invisible to `apply` and visible in the rendered frame.
#[test]
fn colour_words_are_blue_high() {
    let colour = Rgb8::new(0x11, 0x22, 0x33);
    assert_eq!(colour.to_bgr(), 0x0033_2211);
    assert_eq!(Rgb8::from_bgr(0x0033_2211), colour);

    for (low, high) in channel_pairs() {
        let middle = low ^ high;
        let colour = Rgb8::new(low, middle, high);
        assert_eq!(Rgb8::from_bgr(colour.to_bgr()), colour);
        assert_eq!(Rgb8::from_bgr(colour.to_bgr()).g, middle);
    }
}
