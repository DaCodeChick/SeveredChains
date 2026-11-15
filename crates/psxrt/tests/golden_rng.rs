//! Golden-vector tests for the three random streams.
//!
//! Oracle: `testdata/generators/GoldenRng.java`, run on JDK 27. The two integer-LCG streams
//! are reproduced by an independent Java program; the 48-bit stream is captured from a real
//! `java.util.Random`, so none of the 72,000 expected values here were produced by the Rust
//! code under test.
//!
//! The 48-bit fixture is one continuous stream: 12,000 bounded draws, then 12,000
//! `nextFloat`, then 12,000 `nextFloat(bound)`, then 12,000 `nextDouble`, all from a single
//! generator seeded with `0x1234_5678_9ABC`. It is replayed in file order for exactly that
//! reason — a draw that consumes a different number of state words desynchronises everything
//! after it, which is the failure this file exists to catch.

use psxrt::{Lcg15, Lcg16, RandStream, Random48};

/// The stand-in for the host frame counter that the LCG16 fixture injected.
const FIXTURE_VSYNC_COUNT: i32 = 0x1234;

/// The seed the 48-bit fixture started from, before scrambling.
const FIXTURE_RANDOM48_SEED: u64 = 0x1234_5678_9ABC;

fn report(label: &str, mismatches: &[String]) {
    eprintln!(
        "{label}: {} mismatches, first up to 10:\n{}",
        mismatches.len(),
        mismatches.iter().take(10).cloned().collect::<Vec<_>>().join("\n")
    );
}

fn hex(value: &str) -> u64 {
    u64::from_str_radix(value, 16).unwrap_or_else(|err| panic!("bad hex {value:?}: {err}"))
}

fn dec(value: &str) -> i64 {
    value
        .parse::<i64>()
        .unwrap_or_else(|err| panic!("bad integer {value:?}: {err}"))
}

/// The 16-bit stream, replayed through the guarded form.
///
/// Both the plain chain and the four points where the low-twelve-bit guard fires are in the
/// same fixture, so the test drives the guarded call throughout: away from the guard the
/// guarded and unguarded forms must agree, and that equality is asserted separately below.
#[test]
fn lcg16_matches_oracle() {
    const FIXTURE: &str = include_str!("../testdata/rng_lcg16.txt");

    let mut stream = Lcg16::with_state(Lcg16::INITIAL_STATE);
    let mut rows = 0_usize;
    let mut guard_hits = 0_usize;
    let mut mismatches: Vec<String> = Vec::new();

    for line in FIXTURE.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let state_before = hex(fields.next().expect("state field"));
        let expected = fields.next().expect("output field").parse::<u64>().expect("u32");
        let guarded = fields.next().expect("guard flag") == "1";

        if stream.current_state() != state_before {
            mismatches.push(format!(
                "row {rows}: state drifted, at {:#010x}, fixture {state_before}",
                stream.current_state()
            ));
        }

        let actual = u64::from(stream.next_guarded(FIXTURE_VSYNC_COUNT));
        if actual != expected {
            mismatches.push(format!("row {rows}: output {actual}, fixture {expected}"));
        }
        if guarded {
            guard_hits += 1;
        }
        rows += 1;
    }

    if !mismatches.is_empty() {
        report("lcg16", &mismatches);
    }
    assert_eq!(rows, 12_000, "expected the whole fixture");
    assert_eq!(guard_hits, 4, "the fixture injects the frame counter four times");
    assert!(mismatches.is_empty(), "lcg16 disagrees with the oracle");
}

/// Away from the guard, the guarded and unguarded forms must produce the same value *and* the
/// same next state. The reference has two of the three call-site families without the guard; if
/// they were served by the guarded form, the divergence would only be visible at the four
/// low-twelve-bit points, which is too rare to catch anything else.
///
/// Both streams are reset to the fixture's state on every row. Running them as one continuous
/// pair would test nothing: once the guard fires the two states differ, and every later row then
/// diverges for a reason that has nothing to do with the guard.
#[test]
fn lcg16_unguarded_matches_guarded_except_at_the_guard() {
    const FIXTURE: &str = include_str!("../testdata/rng_lcg16.txt");

    let mut rows = 0_usize;
    let mut agree = 0_usize;
    let mut differ = 0_usize;

    for line in FIXTURE.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let state = hex(fields.next().expect("state field")) as i32;

        let mut plain = Lcg16::with_state(state);
        let mut guarded_stream = Lcg16::with_state(state);
        let plain_value = plain.next_raw();
        let guarded_value = guarded_stream.next_guarded(FIXTURE_VSYNC_COUNT);

        let identical =
            plain_value == guarded_value && plain.current_state() == guarded_stream.current_state();
        let guard_fires = (state as u32 & 0xfff) == 0;
        assert_eq!(
            identical, !guard_fires,
            "row {rows}: state {state:#010x} (guard fires: {guard_fires}) gave {plain_value} \
             unguarded and {guarded_value} guarded"
        );

        if identical {
            agree += 1;
        } else {
            differ += 1;
        }
        rows += 1;
    }

    assert_eq!(rows, 12_000);
    assert_eq!(differ, 4, "the guard fires four times in the fixture");
    assert_eq!(agree, 11_996);
}

/// The 15-bit stream. Output is taken from the top of the updated state, so the fixture pins
/// the shift as well as the multiplier.
#[test]
fn lcg15_matches_oracle() {
    const FIXTURE: &str = include_str!("../testdata/rng_lcg15.txt");

    let mut stream = Lcg15::with_state(Lcg15::INITIAL_STATE);
    let mut rows = 0_usize;
    let mut mismatches: Vec<String> = Vec::new();

    for line in FIXTURE.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let state_before = hex(fields.next().expect("state field"));
        let expected = fields
            .next()
            .expect("output field")
            .parse::<u32>()
            .expect("u32");

        if stream.current_state() != state_before {
            mismatches.push(format!(
                "row {rows}: state drifted, at {:#010x}, fixture {state_before}",
                stream.current_state()
            ));
        }
        let actual = stream.next_raw();
        if actual != expected {
            mismatches.push(format!("row {rows}: output {actual}, fixture {expected}"));
        }
        assert!(actual <= 0x7fff, "row {rows}: a 15-bit draw exceeded 0x7fff");
        rows += 1;
    }

    if !mismatches.is_empty() {
        report("lcg15", &mismatches);
    }
    assert_eq!(rows, 12_000, "expected the whole fixture");
    assert!(mismatches.is_empty(), "lcg15 disagrees with the oracle");
}

/// The 48-bit stream, replayed as one continuous run in file order.
///
/// Two things here are load-bearing beyond the values themselves:
///
/// * draw sizes differ (one 31-bit word, one 32-bit word, one 24-bit word, two words), so the
///   fixture pins the *state consumption* of each form, not just its output;
/// * the bounded form enters a rejection loop, so its draw count is data-dependent. A
///   single-draw implementation would match on the first value and drift after it.
#[test]
fn random48_matches_oracle() {
    const FIXTURE: &str = include_str!("../testdata/rng_random48.txt");

    let mut stream = Random48::with_seed(FIXTURE_RANDOM48_SEED);
    let mut rows = 0_usize;
    let mut mismatches: Vec<String> = Vec::new();

    for line in FIXTURE.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let kind = fields.next().expect("kind field");
        let arg = fields.next().expect("arg field");
        let value = fields.next().expect("value field");

        let (actual, in_range) = match kind {
            "i" => {
                let bound = dec(arg) as i32;
                let actual = stream
                    .next_bounded_i32(bound)
                    .unwrap_or_else(|| panic!("row {rows}: bound {bound} was rejected"));
                // The fixture writes bounded draws in decimal and every float draw as raw IEEE
                // bit patterns, which is what makes this fixture able to pin rounding.
                (actual.to_string(), (0..bound).contains(&actual))
            }
            "f" => {
                let actual = stream.next_f32();
                (format!("{:x}", actual.to_bits()), (0.0..1.0).contains(&actual))
            }
            "F" => {
                let bound = f32::from_bits(hex(arg) as u32);
                let actual = stream.next_f32_bounded(bound);
                (format!("{:x}", actual.to_bits()), actual >= 0.0 && actual < bound)
            }
            "d" => {
                let actual = stream.next_f64();
                (format!("{:x}", actual.to_bits()), (0.0..1.0).contains(&actual))
            }
            other => panic!("row {rows}: unknown draw kind {other:?}"),
        };

        if !in_range {
            mismatches.push(format!("row {rows} ({kind}): out of range: {actual}"));
        }
        if actual != value {
            mismatches.push(format!("row {rows} ({kind} {arg}): got {actual}, fixture {value}"));
        }
        rows += 1;
    }

    if !mismatches.is_empty() {
        report("random48", &mismatches);
    }
    assert_eq!(rows, 48_000, "expected the whole fixture");
    assert!(mismatches.is_empty(), "random48 disagrees with the oracle");
}

/// The replacement unbounded draw has no fixture because the reference never calls it: every
/// traced consumer uses a bounded or floating-point form. These values come from the Stafford
/// variant 4 mix that the reference's generator class installs, evaluated from the Java
/// expression twice over — once in a Java program and once in Python — and the two agree.
/// They are pinned so that removing the method later is a decision rather than an unobserved
/// regression.
///
/// The pairs are `(internal state before the draw, mixed output)`. Note that the draw feeding
/// the mix is sign-extended, which is what makes three of these six values what they are.
#[test]
fn random48_mixed_draw_is_pinned() {
    const CASES: [(u64, u32); 6] = [
        (0x0001_2345_6789, 0x7ca9_ede0),
        (0x0000_0000_0001, 0x9f4c_c0d0),
        (0x0000_ffff_ffff, 0xeded_7a23),
        (0x0005_deec_e66c, 0x972e_0455),
        (0x000f_ffff_ffff, 0x206c_4239),
        (0x0000_0001_0001, 0xe6f3_64b9),
    ];

    for (state, expected) in CASES {
        let mut stream = Random48::with_state(state);
        let actual = stream.next_u32_mixed();
        assert_eq!(
            actual, expected,
            "mixed draw from state {state:#014x}: got {actual:#010x}, expected {expected:#010x}"
        );
    }
}
