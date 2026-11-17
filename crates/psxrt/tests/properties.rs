//! Property tests over the primitives' whole input domains.
//!
//! The golden and exhaustive suites pin *values*. This file pins *invariants* — statements that
//! hold for every input in a domain, checked against a bound stated independently of the code.
//! That distinction is what makes them worth having next to the exhaustive sweeps: a sweep that
//! covers all 65,536 channel pairs proves the arithmetic is what it was pinned to, and nothing
//! about what happens if the arithmetic is touched.
//!
//! Three of the properties below are deliberately written as cross-checks between two call
//! forms that look interchangeable at a call site (`next_bits(16)` against the top half of
//! `next_bits(32)`; the guarded and unguarded 16-bit draws; the cycling script gate against the
//! reference's modulo). Those are the refactors that silently move a stream, and none of them
//! shows up in a pinned-value test that keeps calling the same form.
//!
//! The uniformity screen is the one property whose expected value comes from the requirement
//! rather than from any implementation, so it carries its own discrimination check: the biased
//! alternative is computed alongside it and has to fall outside the band, or the band would be
//! too wide to mean anything.

use proptest::collection::vec;
use proptest::prelude::*;
use psxrt::{
    BlendMode, Bpp, FrameCadence, GameClock, Lcg15, Lcg16, RandStream, Random48, Rgb8,
    ScriptTickGate,
};

// ---------------------------------------------------------------------------
// Random streams: replay, state domains, and call-form cross-checks
// ---------------------------------------------------------------------------

/// One step of a call script. The point of scripting the calls rather than calling one method
/// repeatedly is that the *sequence* has to replay, including the multi-draw forms.
#[derive(Clone, Copy, Debug)]
enum Draw {
    Bits(u32),
    U32,
    Mixed,
    Bounded(i32),
    F32,
    F32Bounded(f32),
    F64,
    F64Bounded(f64),
}

fn run_draw(op: Draw, stream: &mut Random48) -> u64 {
    match op {
        Draw::Bits(bits) => u64::from(stream.next_bits(bits)),
        Draw::U32 => u64::from(stream.next_u32()),
        Draw::Mixed => u64::from(stream.next_u32_mixed()),
        // A non-positive bound is a caller error, not a draw; recorded as such rather than
        // drawn from.
        Draw::Bounded(bound) => stream
            .next_bounded_i32(bound)
            .map_or(u64::MAX, |value| u64::from(value as u32)),
        Draw::F32 => u64::from(stream.next_f32().to_bits()),
        Draw::F32Bounded(bound) => u64::from(stream.next_f32_bounded(bound).to_bits()),
        Draw::F64 => stream.next_f64().to_bits(),
        Draw::F64Bounded(bound) => stream.next_f64_bounded(bound).to_bits(),
    }
}

fn draw_script() -> impl Strategy<Value = Draw> {
    prop_oneof![
        (1u32..=48).prop_map(Draw::Bits),
        Just(Draw::U32),
        Just(Draw::Mixed),
        any::<i32>().prop_map(Draw::Bounded),
        Just(Draw::F32),
        any::<f32>().prop_map(Draw::F32Bounded),
        Just(Draw::F64),
        any::<f64>().prop_map(Draw::F64Bounded),
    ]
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    /// A stream must be reproducible from its state plus the calls made on it — that is the
    /// contract the replay harness is built on. Nothing here may keep a counter, a cached
    /// value or a discard flag outside the reported state, and this is what fails if one
    /// appears.
    #[test]
    fn random48_replays_from_a_snapshot_of_its_own_state(
        state in any::<u64>(),
        ops in vec(draw_script(), 2..40usize),
    ) {
        let split = ops.len() / 2;
        let mut stream = Random48::with_state(state);
        for op in &ops[..split] {
            run_draw(*op, &mut stream);
        }
        let snapshot = stream.current_state();
        let mut expected = Vec::new();
        for op in &ops[split..] {
            expected.push(run_draw(*op, &mut stream));
        }

        let mut replay = Random48::with_state(snapshot);
        let mut actual = Vec::new();
        for op in &ops[split..] {
            actual.push(run_draw(*op, &mut replay));
        }
        prop_assert_eq!(actual, expected, "replay from the snapshot diverged");
        prop_assert_eq!(replay.current_state(), stream.current_state());
    }

    /// The seed is masked to 48 bits, so two states that differ only above bit 48 are the same
    /// stream. A generator that let the whole 64-bit word into the state would be a different
    /// generator, and nothing else here would notice.
    #[test]
    fn states_that_differ_only_above_the_state_width_are_the_same_stream(
        low in any::<u64>(),
        high in any::<u16>(),
    ) {
        let ops: Vec<Draw> = vec![Draw::U32, Draw::F32, Draw::F64, Draw::Bounded(7), Draw::Mixed];
        let masked = (low & ((1u64 << 48) - 1)) | (u64::from(high) << 48);

        let mut a = Random48::with_state(masked);
        let mut b = Random48::with_state(masked & ((1u64 << 48) - 1));
        prop_assert_eq!(a.current_state(), b.current_state(), "state is not masked to 48 bits");

        for op in &ops {
            prop_assert_eq!(run_draw(*op, &mut a), run_draw(*op, &mut b), "masked state drew differently");
        }
    }

    /// Bit width is not decoration. `next_bits(16)` and the top half of a `next_bits(32)` draw
    /// advance the state the same single step and read the same bits, so they must agree; a
    /// 24-bit draw and a 32-bit draw do *not* read the same bits, and the third assertion pins
    /// where they part company. This is the pair that a "these two are the same" refactor
    /// confuses.
    #[test]
    fn a_wider_draw_and_a_narrower_draw_at_the_same_state_agree_only_where_the_bits_overlap(
        state in any::<u64>(),
    ) {
        let mut narrow = Random48::with_state(state);
        let sixteen = narrow.next_bits(16);
        let state_after_narrow = narrow.current_state();

        let mut wide = Random48::with_state(state);
        let thirty_two = wide.next_bits(32);
        let state_after_wide = wide.current_state();

        prop_assert_eq!(state_after_narrow, state_after_wide, "both draws are one state step");
        prop_assert_eq!(u64::from(sixteen), u64::from(thirty_two >> 16), "16 bits are the top 16 of 32");

        let mut quarter = Random48::with_state(state);
        let twenty_four = quarter.next_bits(24);
        prop_assert_eq!(
            u64::from(twenty_four >> 8),
            u64::from(sixteen),
            "24 bits contain the same top 16"
        );

        // And a float draw is one 24-bit draw, not a 32-bit one narrowed: the same state step,
        // different bits.
        let mut unit = Random48::with_state(state);
        let unit = unit.next_f32();
        prop_assert!((0.0..1.0).contains(&unit), "unit float draw left [0, 1): {}", unit);
        let mut unit_check = Random48::with_state(state);
        let from_bits = (unit_check.next_bits(24) as f32) * (1.0 / 16_777_216.0);
        prop_assert_eq!(unit.to_bits(), from_bits.to_bits(), "the float draw is not the 24-bit path");
    }

    /// The double-precision draw takes *two* state steps. Asserted against a two-step
    /// reconstruction so that collapsing it to one step — the other half of the same refactor —
    /// is caught.
    #[test]
    fn a_double_draw_consumes_two_state_steps(state in any::<u64>()) {
        let mut pair = Random48::with_state(state);
        let high = u64::from(pair.next_bits(26));
        let low = u64::from(pair.next_bits(27));
        let reconstructed = ((high << 27) + low) as f64 * (1.0 / 9_007_199_254_740_992.0);

        let mut single = Random48::with_state(state);
        let drawn = single.next_f64();
        prop_assert_eq!(drawn.to_bits(), reconstructed.to_bits());
        prop_assert_eq!(single.current_state(), pair.current_state(), "the double draw is not two steps");
        prop_assert!((0.0..1.0).contains(&drawn), "unit double draw left [0, 1): {}", drawn);
    }

    /// Bounded draws. Range and termination, over arbitrary states and arbitrary bounds,
    /// including the bounds that make the rejection loop iterate.
    #[test]
    fn a_bounded_draw_stays_in_its_range(state in any::<u64>(), bound in any::<i32>(), draws in 1usize..12) {
        let mut stream = Random48::with_state(state);
        for _ in 0..draws {
            let drawn = stream.next_bounded_i32(bound);
            if bound <= 0 {
                prop_assert_eq!(drawn, None, "a non-positive bound must not draw");
            } else {
                let value = drawn.expect("a positive bound must draw");
                prop_assert!(
                    (0..bound).contains(&value),
                    "bound {} produced {}", bound, value
                );
            }
        }
    }

    /// Bounded draws at a power-of-two bound take the shortcut, which for a power of two is
    /// exactly a shift. Stated as the shift, because that is the equation the reference writes,
    /// and a modulo or a float path would agree on most inputs and differ on the rest.
    #[test]
    fn a_power_of_two_bound_scales_one_draw_rather_than_rejecting(state in any::<u64>()) {
        for power in 1..16u32 {
            let bound = 1_i32 << power;
            let mut direct = Random48::with_state(state);
            let drawn = direct.next_bounded_i32(bound).expect("positive bound");

            let mut scaled = Random48::with_state(state);
            let draw = scaled.next_bits(31);
            let expected = (((i64::from(bound) * i64::from(draw)) >> 31) as i32) & (bound - 1);
            prop_assert_eq!(drawn, expected, "bound {}", bound);
            prop_assert_eq!(
                scaled.current_state(),
                direct.current_state(),
                "bound {} consumed a different number of draws", bound
            );
        }
    }

    /// The 16-bit stream's output is the low 16 bits of a signed divide by two, taken after the
    /// update, and the divide is not an arithmetic shift.
    ///
    /// The two forms agree on every even state and disagree on the odd negative ones, so the
    /// second half of this test requires the difference to actually occur in the domain rather
    /// than asserting it everywhere. Stated that way because "divide by two is a shift" is true
    /// for the overwhelming majority of inputs and silently wrong for the rest.
    #[test]
    fn lcg16_output_is_sixteen_bits_and_the_divide_is_not_a_shift(state in any::<i32>()) {
        let mut stream = Lcg16::with_state(state);
        let first = stream.next_raw();

        let updated = state.wrapping_mul(9).wrapping_add(0x3711);
        let divided = ((updated / 2) as u32 & 0xffff) as u16;
        let shifted = ((updated >> 1) as u32 & 0xffff) as u16;
        prop_assert_eq!(first, divided, "state {}", state);

        if updated < 0 && updated % 2 != 0 {
            prop_assert_ne!(divided, shifted, "an odd negative state separates the two forms");
        } else {
            prop_assert_eq!(divided, shifted, "elsewhere the two forms agree");
        }
    }

    /// Off the guard, the frame counter is not read at all. This is a cross-check between two
    /// call forms at the same state, which is the shape the reference's three call-site families
    /// actually have.
    #[test]
    fn the_lcg16_guard_only_engages_on_a_state_whose_low_twelve_bits_are_zero(
        state in any::<i32>(),
        frame_count in any::<i32>(),
    ) {
        let guarded = Lcg16::with_state(state).next_guarded(frame_count);
        let unguarded = Lcg16::with_state(state).next_raw();
        if state & 0xfff != 0 {
            prop_assert_eq!(guarded, unguarded, "guard engaged away from the guard");
        } else {
            // On the guard, the counter replaces the state; the ordinary update then multiplies
            // by nine again. Checked against a stream started at the replacement value, which is
            // a different construction rather than a re-run of the same expression.
            let replaced = Lcg16::with_state(frame_count.wrapping_mul(9)).next_raw();
            prop_assert_eq!(guarded, replaced, "the guard did not replace the state");
        }
    }

    /// The 15-bit stream never emits a 16-bit value. Cheap, and it is the invariant a change to
    /// the shift or the mask breaks first.
    #[test]
    fn lcg15_output_stays_inside_fifteen_bits(state in any::<u32>(), draws in 1usize..16) {
        let mut stream = Lcg15::with_state(state);
        for _ in 0..draws {
            prop_assert!(stream.next_raw() <= 0x7fff, "state {:x}", stream.raw_state());
        }
    }

    #[test]
    fn lcg_streams_replay_from_a_snapshot_of_their_own_state(
        lcg16_state in any::<i32>(),
        lcg15_state in any::<u32>(),
        frame_counts in vec(any::<i32>(), 2..32usize),
    ) {
        let split = frame_counts.len() / 2;

        let mut stream = Lcg16::with_state(lcg16_state);
        for count in &frame_counts[..split] {
            let _ = stream.next_guarded(*count);
        }
        let snapshot = stream.current_state();
        let expected: Vec<u16> = frame_counts[split..].iter().map(|count| stream.next_guarded(*count)).collect();
        let mut replay = Lcg16::with_state(0);
        replay.restore_state(snapshot);
        let actual: Vec<u16> = frame_counts[split..].iter().map(|count| replay.next_guarded(*count)).collect();
        prop_assert_eq!(actual, expected, "lcg16 replay diverged");

        let mut stream = Lcg15::with_state(lcg15_state);
        for _ in 0..split {
            let _ = stream.next_raw();
        }
        let snapshot = stream.current_state();
        let expected: Vec<u32> = (split..frame_counts.len()).map(|_| stream.next_raw()).collect();
        let mut replay = Lcg15::with_state(0);
        replay.restore_state(snapshot);
        let actual: Vec<u32> = (split..frame_counts.len()).map(|_| replay.next_raw()).collect();
        prop_assert_eq!(actual, expected, "lcg15 replay diverged");
    }
}

// ---------------------------------------------------------------------------
// Uniformity: the one screen whose expected value comes from the requirement
// ---------------------------------------------------------------------------

/// Split `draws` samples into `bands` equal decades of the bound and count them.
fn histogram<const BANDS: usize>(
    stream: &mut Random48,
    bound: i32,
    draws: usize,
) -> [usize; BANDS] {
    let mut counts = [0_usize; BANDS];
    let width = i64::from(bound) / BANDS as i64;
    for _ in 0..draws {
        let value = stream.next_bounded_i32(bound).expect("positive bound");
        let band = usize::try_from(i64::from(value) / width)
            .unwrap()
            .min(BANDS - 1);
        counts[band] += 1;
    }
    counts
}

/// A rejection sampler is used *because* plain modulo is not uniform, and at a large bound the
/// difference is not academic: most of the range stops being reachable at all.
///
/// The band is derived from the requirement — a uniform source spreads `DRAWS` samples over
/// `BANDS` decades within a few standard deviations — not from any measured output. The second
/// half of the test computes the biased alternative over the same-sized sample and requires it to
/// fall outside that band, because a band nothing can fail is not a check.
#[test]
fn a_bounded_draw_distributes_over_its_whole_range_where_a_modulo_would_not() {
    const BANDS: usize = 8;
    const DRAWS: usize = 200_000;
    // Above 2^30, so a `draw % bound` sampler can only reach the first one-and-a-bit decades.
    let bound = 1_500_000_000_i32;
    let ideal = DRAWS / BANDS;
    // ±10 % is roughly 150 standard deviations for a fair sampler, and far tighter than the
    // distortion a modulo sampler produces at this bound.
    let tolerance = ideal / 10;

    // The screen must pass for the real sampler at a spread of seeds.
    for seed in 0..8u64 {
        let counts = histogram::<BANDS>(&mut Random48::with_state(seed | 1), bound, DRAWS);
        for (band, count) in counts.iter().enumerate() {
            assert!(
                count.abs_diff(ideal) <= tolerance,
                "seed {seed}, band {band}: {count} samples, expected about {ideal}"
            );
        }
    }

    // …and fail for the alternative it exists to rule out.
    let mut biased = [0_usize; BANDS];
    let mut stream = Random48::with_state(1);
    let width = i64::from(bound) / BANDS as i64;
    for _ in 0..DRAWS {
        let value = stream.next_bits(31) as i32 % bound;
        biased[usize::try_from(i64::from(value) / width).unwrap()] += 1;
    }
    assert!(
        biased.iter().any(|count| count.abs_diff(ideal) > tolerance),
        "the band is too wide to detect the bias it is here to detect: {biased:?}"
    );
}

// ---------------------------------------------------------------------------
// Blend: saturation, truncation order, monotonicity
// ---------------------------------------------------------------------------

proptest! {
    #![proptest_config(ProptestConfig::with_cases(1024))]

    /// Each equation, stated in the saturating arithmetic of the description rather than in the
    /// code's own steps. The saturation and the truncation are the two places a float blend
    /// diverges, so they are the parts written out again here.
    #[test]
    fn blend_channels_saturate_at_a_byte_and_truncate_rather_than_round(
        background in any::<u8>(),
        foreground in any::<u8>(),
    ) {
        use BlendMode::*;
        for mode in BlendMode::ALL {
            let result = mode.channel(background, foreground);
            let expected = match mode {
                HalfBackgroundPlusHalfForeground => (u32::from(background) + u32::from(foreground)) / 2,
                BackgroundPlusForeground => u32::from(background) + u32::from(foreground),
                BackgroundMinusForeground => u32::from(background) - u32::from(foreground).min(u32::from(background)),
                BackgroundPlusQuarterForeground => u32::from(background) + u32::from(foreground) / 4,
            };
            prop_assert_eq!(u32::from(result), expected.min(255), "{:?}", mode);
        }
    }

    /// Monotonicity, in both arguments where the equation has it. This is the property that
    /// makes a saturation bug visible: a wrap would make a larger input produce a smaller
    /// output, which the sweep checks only pair by pair.
    #[test]
    fn additive_blends_never_get_darker_as_the_foreground_gets_brighter(
        background in any::<u8>(),
        low in any::<u8>(),
        high in any::<u8>(),
    ) {
        let (low, high) = (low.min(high), low.max(high));
        for mode in [
            BlendMode::BackgroundPlusForeground,
            BlendMode::BackgroundPlusQuarterForeground,
            BlendMode::HalfBackgroundPlusHalfForeground,
        ] {
            prop_assert!(
                mode.channel(background, high) >= mode.channel(background, low),
                "{:?} darkened as the foreground rose from {} to {}", mode, low, high
            );
        }
        // Subtractive runs the other way, and floors at zero rather than wrapping.
        prop_assert!(
            BlendMode::BackgroundMinusForeground.channel(background, high)
                <= BlendMode::BackgroundMinusForeground.channel(background, low)
        );
        prop_assert!(BlendMode::BackgroundMinusForeground.channel(background, high) <= background);
    }

    /// Modulation: a texel of 128 is a pass-through and everything above it saturates. The
    /// pass-through is the reason the scale is a divide by 128 and not by 255, so it is worth a
    /// property of its own.
    #[test]
    fn modulation_passes_through_at_a_texel_of_128_and_saturates_above_it(
        r in any::<u8>(),
        g in any::<u8>(),
        b in any::<u8>(),
        texel in any::<u8>(),
    ) {
        use psxrt::modulate;
        let vertex = Rgb8::new(r, g, b);
        let result = modulate(vertex, Rgb8::new(texel, texel, texel));
        for (channel, out) in [(r, result.r), (g, result.g), (b, result.b)] {
            let expected = (u32::from(channel) * u32::from(texel)) >> 7;
            prop_assert_eq!(u32::from(out), expected.min(255));
        }
        if texel == 128 {
            prop_assert_eq!(result, vertex, "a texel of 128 must reproduce the vertex colour");
        }
        if (u32::from(r) * u32::from(texel)) >> 7 > 255 {
            prop_assert_eq!(result.r, 255, "a product above a byte must saturate, not wrap");
        }
        // Monotone in the texel, up to the saturation point.
        prop_assert!(modulate(vertex, Rgb8::new(255, 0, 0)).r >= modulate(vertex, Rgb8::new(64, 0, 0)).r);
    }

    /// Colour-word round trip, over arbitrary words: the packing is byte order, not bit order,
    /// and the top byte carries the mask bit that the painter reads separately.
    #[test]
    fn a_colour_word_round_trips_when_its_top_byte_is_empty(word in any::<u32>()) {
        let colour = Rgb8::from_bgr(word);
        prop_assert_eq!(colour.to_bgr() >> 24, 0, "the mask byte is not part of the colour");
        prop_assert_eq!(Rgb8::from_bgr(colour.to_bgr()), colour);
    }
}

// ---------------------------------------------------------------------------
// Pixel depth: in-domain round trip, and the domain boundary
// ---------------------------------------------------------------------------

proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    /// Pack then address, over arbitrary in-domain lines.
    ///
    /// The indices are generated inside the depth's field, which is the condition the round trip
    /// needs. Below this test, a second one pins what happens outside it: a five-bit index at a
    /// four-bit depth does not come back, and that is the packing working, not failing.
    ///
    /// `pack_line` states the layout independently of `Bpp`'s four parameters, so a change to any
    /// of them has to disagree with something else that was written down.
    #[test]
    fn a_packed_line_reads_back_through_its_own_depth(
        line4 in vec(0..16_u8, 1..128usize),
        line8 in vec(any::<u8>(), 1..128usize),
    ) {
        let failures = [
            round_trip_failure(Bpp::Bits4, &line4),
            round_trip_failure(Bpp::Bits8, &line8),
        ]
        .into_iter()
        .flatten()
        .collect::<Vec<_>>();
        prop_assert!(failures.is_empty(), "{failures:?}");
    }

    /// Nothing the addressing accepts can read outside its own 16-bit word, whatever the slot.
    #[test]
    fn addressing_stays_inside_the_word(word in any::<u16>(), slot in 0..8u32) {
        for depth in Bpp::ALL {
            if let Some(read) = depth.index_in_word(word, slot) {
                prop_assert!(read <= depth.index_mask(), "{:?} read {}", depth, read);
            }
        }
    }
}

/// Pack a line in the layout the depth uses, then read every texel back through the depth's
/// addressing. Returns the first disagreement, if any.
///
/// The packer takes the texel width from the depth by rule (`16 / texels per word`) rather than
/// from `Bpp`'s `index_shift`, so the two sides of the round trip are written independently.
fn round_trip_failure(depth: Bpp, indices: &[u8]) -> Option<String> {
    let per_word = match depth {
        Bpp::Bits4 => 4,
        Bpp::Bits8 => 2,
        Bpp::Bits15 | Bpp::Bits24 => return None,
    };
    let words = pack_line(depth, indices);
    for (x, index) in indices.iter().enumerate() {
        let word = words[x / per_word];
        let read = depth.index_in_word(word, u32::try_from(x).unwrap());
        if read != Some(u32::from(*index)) {
            return Some(format!(
                "{depth:?} at x {x} in word {word:#06x}: packed {index}, read {read:?}"
            ));
        }
    }
    None
}

fn pack_line(depth: Bpp, indices: &[u8]) -> Vec<u16> {
    let per_word = match depth {
        Bpp::Bits4 => 4,
        Bpp::Bits8 => 2,
        Bpp::Bits15 | Bpp::Bits24 => return Vec::new(),
    };
    let width = 16 / per_word;
    indices
        .chunks(per_word)
        .map(|chunk| {
            chunk.iter().enumerate().fold(0_u32, |word, (slot, index)| {
                word | (u32::from(*index) & ((1 << width) - 1)) << (slot * width)
            }) as u16
        })
        .collect()
}

// ---------------------------------------------------------------------------
// Timing: completeness of the cadence matrix, and the gate's equivalence
// ---------------------------------------------------------------------------

proptest! {
    #![proptest_config(ProptestConfig::with_cases(1024))]

    /// The two consumers of the cadence field, over the whole domain rather than the rows the
    /// unmodded game happens to use. Both formulas are transcribed from the reference, so this
    /// is a differential check and not a restatement of the code.
    #[test]
    fn both_cadence_formulas_hold_for_any_vsync_count_and_speed(
        vsync in -128_i32..128,
        speed in 1..=16_u32,
    ) {
        let cadence = FrameCadence::new(vsync, speed).unwrap();

        let frames = u32::try_from(vsync.max(1)).unwrap();
        prop_assert_eq!(cadence.rate_divisor(), frames, "the rate clamps at one");
        prop_assert_eq!(cadence.frame_rate_hz(), 60 / frames * speed, "60 / frames * speed");

        // …and the clock does not: it takes the raw field, unclamped and unscaled.
        prop_assert_eq!(cadence.clock_increment_sixtieths(), vsync, "the clock takes the raw field");
    }

    /// Game time is whatever was added to it. Checked against a running sum rather than against
    /// the clock, so the property is about accumulation, not about the type.
    #[test]
    fn the_clock_is_the_sum_of_its_increments(increments in vec(-2000_i32..2000, 1..200usize)) {
        let mut clock = GameClock::new();
        let mut total = 0_i64;
        for step in &increments {
            clock.advance(*step);
            total = total.wrapping_add(i64::from(*step));
        }
        prop_assert_eq!(clock.sixtieths(), total);
    }

    /// The gate is a cycling counter; the reference's gate is a global counter under a modulo.
    /// They agree, and this checks one against the other over divisors the game never installs,
    /// so the agreement is a fact about the model rather than a coincidence of three values.
    #[test]
    fn the_script_gate_agrees_with_a_modulo_over_a_global_counter(
        divisor in 1_u32..9,
        frames in 1usize..400,
    ) {
        let mut gate = ScriptTickGate::every(divisor);
        let pace = usize::try_from(divisor).expect("the divisor fits");
        // The reference's counter *is* the frame index: it counts frame-loop iterations and is
        // not reset mid-game, so `frame` is the second, independent implementation of the test.
        for frame in 0..frames {
            prop_assert_eq!(
                gate.ticks_now(),
                frame % pace == 0,
                "frame {}, divisor {}", frame, divisor
            );
            gate.advance();
        }
    }
}

/// The gate has to keep its pace when the frame rate rises, which is the reason it exists.
/// Checked at the rates the 60/30/20 Hz contexts reach, against a logic-tick count derived from
/// the requirement.
#[test]
fn raising_the_frame_rate_with_the_gate_keeps_the_logic_pace() {
    for (vsync, speed, divisor) in [(2, 1, 1), (1, 1, 2), (3, 1, 1), (2, 4, 1), (1, 2, 2)] {
        let cadence = FrameCadence::new(vsync, speed).unwrap();
        let rate = usize::try_from(cadence.frame_rate_hz()).unwrap();
        let mut gate = ScriptTickGate::every(divisor);
        let mut ticks = 0_usize;
        for _frame in 0..rate {
            if gate.ticks_now() {
                ticks += 1;
            }
            gate.advance();
        }
        assert_eq!(
            ticks,
            rate / usize::try_from(divisor).unwrap(),
            "vsync {vsync}"
        );
    }
}

/// A texel is a value, not a lookup: two slots of one word must stay distinguishable after
/// addressing, over arbitrary words.
#[test]
fn addressing_two_slots_of_one_word_keeps_their_values_apart() {
    for depth in [Bpp::Bits4, Bpp::Bits8] {
        let per_word = usize::try_from(depth.width_divisor()).unwrap();
        let width = 16 / per_word;
        for word in 0_u16..=u16::MAX {
            let slots: Vec<u32> = (0..per_word)
                .map(|slot| (u32::from(word) >> (slot * width)) & ((1 << width) - 1))
                .collect();
            let addressed: Vec<Option<u32>> = (0..per_word)
                .map(|slot| depth.index_in_word(word, u32::try_from(slot).unwrap()))
                .collect();
            for (slot, value) in slots.into_iter().enumerate() {
                assert_eq!(
                    addressed[slot],
                    Some(value),
                    "{depth:?} slot {slot} in word {word:#06x}"
                );
            }
        }
    }
}
/// Outside the field the packing is lossy, and that has to stay true: a five-bit index at a
/// four-bit depth has nowhere to go, so it comes back as its low four bits. Pinned because "the
/// round trip holds" means nothing unless the domain it holds over is also pinned.
#[test]
fn an_index_wider_than_the_field_does_not_survive_the_packing() {
    let indices = [16_u8, 17, 31, 32, 255];
    let words = pack_line(Bpp::Bits4, &indices);
    let read_back: Vec<Option<u32>> = indices
        .iter()
        .enumerate()
        .map(|(x, _)| {
            let word = words[x / 4];
            Bpp::Bits4.index_in_word(word, u32::try_from(x).unwrap())
        })
        .collect();
    let expected: Vec<Option<u32>> = indices
        .iter()
        .map(|index| Some(u32::from(*index) & 0xf))
        .collect();
    assert_eq!(read_back, expected, "a four-bit field keeps four bits");

    // And at eight bits the same sweep *is* in domain, which is what makes the four-bit case a
    // statement about width rather than about round tripping in general.
    let eight = vec![200_u8, 201, 202, 203];
    assert!(round_trip_failure(Bpp::Bits8, &eight).is_none());
}
