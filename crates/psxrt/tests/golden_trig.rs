//! Golden-vector tests for the PSX fixed-point trig primitives.
//!
//! Oracle: `testdata/generators/GoldenTrig.java`, run on JDK 21. It reproduces the reference
//! chain
//!
//! ```text
//! rad          = theta * PSX_DEG_TO_RAD                     (float multiply)
//! rsin(theta)  = (short)( (float)Math.sin(rad) * 4096.0f )  (float multiply, truncating narrow)
//! rcos(theta)  = (short)( (float)Math.cos(rad) * 4096.0f )
//! ```
//!
//! over every `theta` in `-8192..=8192`. Nothing in this file computes an expected value from
//! the code under test; the 32,770 expected values were produced before the Rust primitives
//! existed.

use psxrt::{PSX_DEG_TO_RAD, rcos, rsin};

/// The expected bit pattern of the precomputed angle constant.
const PSX_DEG_TO_RAD_BITS: u32 = 0x3ac9_0fdb;

fn first_mismatches(label: &str, mismatches: &[String]) {
    eprintln!(
        "{label}: {} mismatches, first up to 10:\n{}",
        mismatches.len(),
        mismatches.iter().take(10).cloned().collect::<Vec<_>>().join("\n")
    );
}

/// The constant must be the precomputed product, not a recomputation.
///
/// Both spellings of the reference expression (`(float)(Math.PI / 180.0)` and a native f32
/// division) happen to round to the same `DEG_TO_RAD`, and therefore to the same
/// `PSX_DEG_TO_RAD`; the point of pinning the bit pattern is that a future "simplification"
/// into a single `tau / 4096` expression cannot pass unnoticed.
#[test]
fn angle_constant_bit_pattern() {
    assert_eq!(
        PSX_DEG_TO_RAD.to_bits(),
        PSX_DEG_TO_RAD_BITS,
        "PSX_DEG_TO_RAD changed bit pattern: now {:#010x}",
        PSX_DEG_TO_RAD.to_bits()
    );
}

/// Every point of the committed grid, both primitives.
///
/// This is the row that matters for the "float, not double" claim in the module header: a
/// chain evaluated entirely in double precision diverges near the saturation boundary, and
/// those divergences are inside this grid rather than beyond it.
#[test]
fn trig_grid_matches_oracle() {
    const GRID: &str = include_str!("../testdata/trig_grid.txt");

    let mut checked = 0_usize;
    let mut mismatches: Vec<String> = Vec::new();

    for line in GRID.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut fields = line.split_whitespace();
        let theta: i32 = fields.next().expect("theta field").parse().expect("theta integer");
        let expected_sin: i16 = fields
            .next()
            .expect("rsin field")
            .parse()
            .expect("rsin integer");
        let expected_cos: i16 = fields
            .next()
            .expect("rcos field")
            .parse()
            .expect("rcos integer");

        let actual_sin = rsin(theta);
        if actual_sin != expected_sin {
            mismatches.push(format!("rsin({theta}) = {actual_sin}, oracle {expected_sin}"));
        }
        let actual_cos = rcos(theta);
        if actual_cos != expected_cos {
            mismatches.push(format!("rcos({theta}) = {actual_cos}, oracle {expected_cos}"));
        }
        checked += 1;
    }

    if !mismatches.is_empty() {
        first_mismatches("trig grid", &mismatches);
    }
    assert_eq!(checked, 16_385, "expected the whole -8192..=8192 grid");
    assert!(
        mismatches.is_empty(),
        "{} of {} sampled values disagree with the oracle",
        mismatches.len(),
        checked * 2
    );
}

/// The oracle also recorded where `Math` and `StrictMath` disagree, so that the choice between
/// them is answered by data rather than by assumption. It recorded none.
///
/// Re-running this assertion after a fixture refresh is what keeps the "immaterial"
/// classification honest: if a future grid or platform produces divergences, they appear here
/// as rows to reconcile instead of as a silent change of oracle.
#[test]
fn math_and_strictmath_agree_over_the_grid() {
    const DIVERGENCES: &str = include_str!("../testdata/trig_strictmath_divergences.txt");

    let rows: Vec<&str> = DIVERGENCES
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect();

    if let Some(row) = rows.first() {
        // Should the fixture ever gain rows, they are angles where the two candidate readings
        // of the reference's float sine differ. Each is a genuine open case, so the test fails
        // rather than picking one, and it names how many there are.
        let theta: i32 = row.parse().expect("divergence row is a theta");
        panic!(
            "oracle flags {} Math/StrictMath divergence(s), first at theta {theta}; \
             reconcile before use",
            rows.len()
        );
    }
}

/// Saturation and sign behaviour, stated separately from the grid so that a change in the grid
/// range cannot quietly drop the coverage.
///
/// The peak is 4096, one past the "peak 0xFFF" the research notes quote, and it is real: the
/// sine rounds to exactly 1.0 at that angle and the scale multiply happens after the
/// narrowing to float.
#[test]
fn peak_amplitude_and_sign() {
    assert_eq!(rsin(1024), 4096, "quarter turn, sine rounds to exactly 1.0");
    assert_eq!(rsin(3072), -4096, "three-quarter turn");
    assert_eq!(rcos(0), 4096);
    assert_eq!(rcos(2048), -4096, "half turn");
    assert_eq!(rsin(0), 0);
    assert_eq!(rsin(-1024), -4096, "negative angles stay negative");
    assert_eq!(rsin(4096), rsin(0), "the unit wraps by periodicity of the sine");
}
