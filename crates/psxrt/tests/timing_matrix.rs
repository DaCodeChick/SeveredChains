//! The frame-cadence matrix: every vsync count against every legal speed multiplier, replayed
//! against an oracle that transcribes the reference's own expressions.
//!
//! The cadence field is read twice per frame loop, by two formulas that treat it differently.
//! One clamps it at one; the other takes it raw. Neither is adjusted for the speed multiplier
//! in the same way, and neither is checked for divisibility. Because the same field is writable
//! from a script variable and from a debugger control, values outside the three the unmodded
//! game uses are reachable, so the whole matrix is pinned rather than the three rows.
//!
//! The oracle lives in `testdata/generators/GoldenTiming.java` and is a transcription of
//! `Scus94491BpeSegment.gameLoop` and `Menus.java:218`, not a reimplementation of this crate.

use psxrt::{
    BASE_RATE_HZ, FrameCadence, GameClock, MAX_SPEED_MULTIPLIER, MIN_SPEED_MULTIPLIER,
    ScriptTickGate,
};

/// Oracle rows, generated outside the Rust build.
const CADENCE_MATRIX: &str = include_str!("../testdata/timing_matrix.txt");

/// Oracle rows for the script gate, same generator.
const GATE_MATRIX: &str = include_str!("../testdata/script_gate.txt");

fn parse_matrix(text: &str) -> Vec<(i32, u32, u32, i32)> {
    text.lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .map(|line| {
            let mut fields = line.split_whitespace();
            let mut next = |what: &str| {
                fields
                    .next()
                    .unwrap_or_else(|| panic!("missing {what} in {line:?}"))
            };
            let vsync = next("vsync count").parse::<i32>().expect("vsync count");
            let speed = next("speed").parse::<u32>().expect("speed");
            let hz = next("rate").parse::<u32>().expect("rate");
            let increment = next("clock increment")
                .parse::<i32>()
                .expect("clock increment");
            (vsync, speed, hz, increment)
        })
        .collect()
}

#[test]
fn every_cadence_row_matches_the_oracle() {
    let rows = parse_matrix(CADENCE_MATRIX);
    assert_eq!(rows.len(), 22 * 16, "22 vsync counts against 16 speeds");

    let mut mismatches: Vec<String> = Vec::new();
    for (vsync, speed, expected_rate, expected_increment) in &rows {
        let cadence = FrameCadence::new(*vsync, *speed)
            .unwrap_or_else(|| panic!("cadence {vsync}/{speed} was rejected"));

        if cadence.frame_rate_hz() != *expected_rate {
            mismatches.push(format!(
                "vsync {vsync} speed {speed}: rate {}, oracle says {expected_rate}",
                cadence.frame_rate_hz()
            ));
        }
        if cadence.clock_increment_sixtieths() != *expected_increment {
            mismatches.push(format!(
                "vsync {vsync} speed {speed}: increment {}, oracle says {expected_increment}",
                cadence.clock_increment_sixtieths()
            ));
        }
    }
    assert!(mismatches.is_empty(), "{mismatches:?}");
}

/// The two consumers of the cadence field must disagree wherever the field is outside 1..=60.
///
/// This is the point of keeping the raw count in the struct. If the rate were stored and the
/// increment derived from it, every row below would be wrong in a way that looks like a
/// rounding choice.
#[test]
fn the_rate_clamps_the_count_and_the_clock_increment_does_not() {
    let rows = parse_matrix(CADENCE_MATRIX);
    let mut clampings = 0_usize;
    let mut zero_rates = 0_usize;
    let mut frozen_clocks = 0_usize;

    for (vsync, speed, expected_rate, expected_increment) in &rows {
        let cadence = FrameCadence::new(*vsync, *speed).unwrap();
        let divisor = u32::try_from((*vsync).max(1)).unwrap();

        if *vsync < 1 {
            clampings += 1;
            // Below one, the rate uses one and the clock uses the raw value: a full-speed frame
            // loop around a clock that stands still, or runs backwards.
            assert_eq!(cadence.frame_rate_hz(), BASE_RATE_HZ * speed);
            assert_eq!(cadence.clock_increment_sixtieths(), *vsync);
            if *vsync == 0 {
                frozen_clocks += 1;
            }
        } else if *vsync > 60 {
            // Above sixty the truncating divide reaches zero. Reported as zero rather than
            // lifted to one hertz, so the platform boundary sees a zero rate.
            assert_eq!(cadence.frame_rate_hz(), 0, "vsync {vsync} speed {speed}");
            zero_rates += 1;
        } else {
            assert_eq!(cadence.rate_divisor(), divisor);
            assert_eq!(cadence.frame_rate_hz(), *expected_rate);
        }

        assert_eq!(cadence.clock_increment_sixtieths(), *expected_increment);
    }

    assert_eq!(clampings, 3 * 16, "three counts below one, at every speed");
    assert_eq!(zero_rates, 3 * 16, "three counts above sixty (61, 70, 120), at every speed");
    assert_eq!(frozen_clocks, 16);
}

/// Where a divisor does not divide sixty, the truncation happens before the speed is applied,
/// so the multiplier cannot rescue it.
#[test]
fn a_non_divisor_truncates_before_the_speed_is_applied() {
    for vsync in 1..=60_u32 {
        for speed in MIN_SPEED_MULTIPLIER..=MAX_SPEED_MULTIPLIER {
            let cadence = FrameCadence::new(vsync.cast_signed(), speed).unwrap();
            let expected = (60 / vsync) * speed;
            assert_eq!(
                cadence.frame_rate_hz(),
                expected,
                "vsync {vsync} speed {speed}: 60 / {vsync} * {speed}"
            );
        }
    }
    // The consequence, stated once: seven of the sixty divisors reach 60 Hz and the rest fall
    // away in steps. 61 and beyond reach nothing at all.
    assert_eq!(FrameCadence::with_vsync_count(7).frame_rate_hz(), 8);
    assert_eq!(FrameCadence::with_vsync_count(7).frame_rate_hz() * 16, 128);
    assert_eq!(FrameCadence::with_vsync_count(8).frame_rate_hz(), 7);
}

#[test]
fn speed_is_accepted_only_over_the_configured_range() {
    for speed in [0, 17, 32, u32::MAX] {
        assert_eq!(FrameCadence::new(2, speed), None, "speed {speed}");
    }
    for speed in MIN_SPEED_MULTIPLIER..=MAX_SPEED_MULTIPLIER {
        assert!(FrameCadence::new(2, speed).is_some(), "speed {speed}");
    }
    assert_eq!(MIN_SPEED_MULTIPLIER, 1);
    assert_eq!(MAX_SPEED_MULTIPLIER, 16);
}

#[test]
fn the_four_named_counts_are_the_ones_the_reference_installs() {
    assert_eq!(FrameCadence::DEFAULT_VSYNC_COUNT, 1);
    assert_eq!(FrameCadence::GAMEPLAY_VSYNC_COUNT, 2);
    assert_eq!(FrameCadence::HIGH_SPEED_VSYNC_COUNT, 1);
    assert_eq!(FrameCadence::BATTLE_EVENT_VSYNC_COUNT, 3);

    // Rates, for the record: the default and the high-speed count are the same number, and the
    // gameplay count is the one that halves the loop.
    assert_eq!(FrameCadence::with_vsync_count(2).frame_rate_hz(), 30);
    assert_eq!(FrameCadence::with_vsync_count(1).frame_rate_hz(), 60);
    assert_eq!(FrameCadence::with_vsync_count(3).frame_rate_hz(), 20);
}

/// The script gate, replayed against the reference's modulo.
///
/// The reference keeps one global frame counter and tests it against `3 - vsync`, so a state
/// that raises the frame rate keeps its logic pace by raising the divisor with it. The gate is
/// modelled as a cycling counter because that is equivalent to the modulo for a counter that
/// starts at zero and rises by one, which is what the reference's counter does between state
/// transitions.
#[test]
fn the_script_gate_reproduces_the_reference_modulo() {
    for (vsync, divisor) in [(1_i32, 2_u32), (2, 1), (3, 1)] {
        let expected_bits = GATE_MATRIX
            .lines()
            .find(|line| line.starts_with(&format!("{divisor} ")))
            .unwrap_or_else(|| panic!("no gate row for divisor {divisor}"))
            .split_whitespace()
            .nth(1)
            .expect("bitmap")
            .to_string();

        // Cross-check the fixture against the modulo it was generated from, so a stale fixture
        // fails here rather than quietly redefining the gate.
        assert_eq!(
            expected_bits
                .chars()
                .enumerate()
                .filter(|(_, bit)| *bit == '1')
                .count(),
            48 / usize::try_from(divisor).unwrap(),
            "fixture for divisor {divisor}"
        );

        let mut bits = String::new();
        let mut gate = ScriptTickGate::every(divisor);
        for _frame in 0..48 {
            bits.push(if gate.ticks_now() { '1' } else { '0' });
            gate.advance();
        }
        assert_eq!(bits, expected_bits, "gate at vsync {vsync} (divisor {divisor})");

        // And the cycle length is exact over a longer run, not only across the fixture window.
        let mut ticks = 0_usize;
        let mut gate = ScriptTickGate::every(divisor);
        for _frame in 0..600 {
            if gate.ticks_now() {
                ticks += 1;
            }
            gate.advance();
        }
        assert_eq!(ticks, 600 / usize::try_from(divisor).unwrap());
    }
}

#[test]
fn a_gate_below_one_frame_ticks_every_frame() {
    // A zero divisor would never satisfy the tick test, and the reference's modulo would fault
    // on it. Treated as one, which is what the reference's own `Math.max(1, …)` does.
    let gate = ScriptTickGate::every(0);
    assert_eq!(gate.frames_per_tick(), 1);
    assert!(gate.ticks_now());

    let mut gate = gate;
    for _ in 0..10 {
        assert!(gate.ticks_now());
        assert!(gate.advance());
    }
}

/// What the two separate formulas add up to, measured over a whole second.
///
/// The frame rate alone does not say how fast a game plays. Three of the rows below differ in
/// frame rate and are identical in how fast the game runs, which is the design: raising the
/// picture rate while raising the script gate keeps logic at its usual pace, and the raw clock
/// increment keeps game time at real speed. A port that derived the clock step from the frame
/// rate instead of the raw count would break the smooth-but-slow rows, and a port that forgot
/// the gate would get double-speed gameplay the moment a state raised its rate.
#[test]
fn rate_and_gate_and_clock_together_set_how_fast_the_game_plays() {
    // (label, vsync count, speed, frames per logic tick, expected logic ticks in one second,
    //  expected sixtieths of game time in one second)
    let cases = [
        ("gameplay", 2_i32, 1_u32, 1_u32, 30_u32, 60_i64),
        ("high speed", 1, 1, 2, 30, 60),
        ("battle event", 3, 1, 1, 20, 60),
        ("gameplay at 4x", 2, 4, 1, 120, 240),
        // Reachable from a script variable, and included because everything keeps running while
        // the clock stands still.
        ("frozen", 0, 1, 1, 60, 0),
    ];

    let mut second = Vec::new();
    for (label, vsync, speed, frames_per_tick, expected_logic, expected_time) in cases {
        let cadence = FrameCadence::new(vsync, speed).unwrap();
        let rate = cadence.frame_rate_hz();
        assert!(rate > 0, "{label}: the frame loop has no rate");

        let mut gate = ScriptTickGate::every(frames_per_tick);
        let mut clock = GameClock::new();

        // One second of real time, frame by frame. The gate advances once per frame loop
        // iteration, which is what the reference's per-iteration counter increment means.
        let mut logic_ticks = 0_u32;
        for _frame in 0..usize::try_from(rate).unwrap() {
            if gate.ticks_now() {
                logic_ticks += 1;
            }
            gate.advance();
            clock.advance_by(cadence);
        }

        assert_eq!(
            logic_ticks,
            expected_logic,
            "{label}: logic ticks in one second at {rate} Hz with a gate of {frames_per_tick}"
        );
        assert_eq!(
            clock.sixtieths(),
            expected_time,
            "{label}: expected {expected_time} sixtieths of game time, got {}",
            clock.sixtieths()
        );
        second.push((label, rate, logic_ticks, clock.sixtieths()));
    }

    // Gameplay and high speed run the game at the same speed; only the picture differs. Pinned
    // as a pair because it is the property the whole three-field design exists to preserve.
    let gameplay = &second[0];
    let high_speed = &second[1];
    assert_ne!(gameplay.1, high_speed.1, "the frame rates differ");
    assert_eq!(gameplay.2, high_speed.2, "…and the logic pace does not");
    assert_eq!(gameplay.3, high_speed.3, "…nor does game time");
}

/// Game time is a counter, so a long run must not drift, and a negative increment must apply.
#[test]
fn the_clock_is_a_counter_not_a_measurement() {
    let mut clock = GameClock::new();
    for _ in 0..100_000 {
        clock.advance_by(FrameCadence::with_vsync_count(2));
    }
    assert_eq!(clock.sixtieths(), 200_000);

    // The reference adds the raw field with `+=`, so a negative field walks the clock backwards
    // rather than being ignored.
    let mut clock = GameClock::with_sixtieths(1_000);
    clock.advance_by(FrameCadence::with_vsync_count(-3));
    assert_eq!(clock.sixtieths(), 997);

    // A restored clock keeps its reading rather than being re-based on the host.
    let restored = GameClock::with_sixtieths(-5);
    assert_eq!(restored.sixtieths(), -5);
    assert_eq!(GameClock::default(), GameClock::new());
}
