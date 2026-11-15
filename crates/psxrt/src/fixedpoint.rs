//! PSX fixed-point helpers: the angular unit and the two trig primitives that share it.
//!
//! ## The angular unit
//!
//! The whole angle pipeline of the original flows through "PSX degrees": 4096 units per full
//! revolution. The reference does not mask or wrap the operand — it converts the raw integer
//! to a float radian by multiplying a precomputed constant, and hands that to a float sine.
//! Anything that assumes a 0..4095 domain is therefore wrong at the call sites that pass a
//! computed or wrapped-around angle; this module takes the raw `i32` unchanged.
//!
//! ## The chain, exactly as observed
//!
//! ```text
//! rsin(theta) = (short)( sinFloat( theta * PSX_DEG_TO_RAD ) * 4096.0f )
//! ```
//!
//! Three details that a plausible reimplementation gets wrong:
//!
//! 1. **The constant is precomputed, not recomputed.** `PSX_DEG_TO_RAD` is the float product
//!    of `360.0f/4096.0f` and a separately-rounded `DEG_TO_RAD`. Multiplying by
//!    `tau/4096` in one expression gives a different rounding and different endpoint values.
//! 2. **The transcendental is evaluated at float precision.** The reference's `MathHelper.sin`
//!    returns `float`, so the sine is rounded to f32 *before* the scale multiply, and the
//!    multiply itself is a float multiply. Doing the whole chain in double lands one ulp off
//!    at the saturation boundary and then truncates to a different integer.
//! 3. **The final conversion truncates and wraps.** The narrowing to a 16-bit signed integer
//!    truncates toward zero and keeps the low 16 bits; it neither rounds nor saturates.
//!
//! The scale factor is `0x1000`, so a peak sine of 1.0 yields 4096 (`0x1000`), which is one
//! past the "peak 0xFFF" the research notes quote — the overshoot is real and only occurs
//! where the sine rounds to exactly 1.0.

/// One full revolution in PSX angular units.
pub const PSX_DEG_PER_REV: i32 = 4096;

/// `360.0f / 4096.0f`, exactly representable.
const PSX_DEG_TO_DEG: f32 = 360.0 / 4096.0;

/// Degrees to radians, evaluated in double and narrowed once, which is what the reference
/// writes: `Math.PI` is a double, so the division happens at double precision and only the
/// result loses precision. Doing the division at single precision instead rounds differently,
/// and this constant is multiplied by every angle the engine converts.
const DEG_TO_RAD: f32 = (core::f64::consts::PI / 180.0_f64) as f32;

/// Angular unit to radians. Equals `0x3ac90fdb` in IEEE-754 binary32.
///
/// Kept in two spellings on purpose: the value is asserted against the bit pattern in
/// `tests/golden_fixedpoint.rs`, so a change to how it is computed cannot slip through.
pub const PSX_DEG_TO_RAD: f32 = PSX_DEG_TO_DEG * DEG_TO_RAD;

/// The fixed-point amplitude that a sine of 1.0 would scale to.
pub const TRIG_SCALE: f32 = 4096.0;

/// Which trigonometric path a context uses.
///
/// Only one of these is implementable today. The table path reads the 456-entry sine table
/// that the original kept in ROM at `80015c80 - GTE sine table`; those bytes are not
/// available, and no algorithm for reading them is documented — only that a table exists and
/// that callers index it with `theta & 0x3ff` after adding a 512 offset for the cosine. A
/// sampler invented from that would be a guess dressed up as a mechanism, so no seam is
/// defined here; the second arm arrives with the milestone that owns the hardware-truth
/// evidence (I5), together with whatever index rule that evidence supports.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum TrigonometryModel {
    /// Evaluate the transcendental in floating point, then scale and truncate. This is what
    /// the reference does and what [`rsin`] / [`rcos`] implement.
    #[default]
    FloatExpression,
}

/// Convert a PSX angular unit to radians the way the reference does.
#[must_use]
pub fn psx_deg_to_rad(theta: i32) -> f32 {
    (theta as f32) * PSX_DEG_TO_RAD
}

/// `rsin` — the fixed-point sine of a PSX angle, amplitude 4096.
///
/// Returns values in `[-4096, 4096]`. Negative angles produce negative results; the
/// "always returns a positive value even for negative angles" comment on the reference
/// method is contradicted by its own code and by measurement, and is recorded as a
/// documentation defect, not a behaviour to preserve.
#[must_use]
pub fn rsin(theta: i32) -> i16 {
    scale_to_fixed(sine(psx_deg_to_rad(theta)))
}

/// `rcos` — the fixed-point cosine of a PSX angle, amplitude 4096.
#[must_use]
pub fn rcos(theta: i32) -> i16 {
    scale_to_fixed(cosine(psx_deg_to_rad(theta)))
}

/// Evaluate a sine at float precision.
///
/// The reference delegates to a float-returning library sine. Both candidate readings of
/// that (a double evaluation narrowed once to f32, and a native f32 evaluation) were
/// measured against the committed grid and agree at every one of its 16,385 points, so the
/// unresolved question about which one the reference resolves to cannot change an observable
/// result here. Rust's own `f64::sin` is used; a bit-exact requirement against a different
/// host library would be a change of oracle, not a change of algorithm.
#[must_use]
fn sine(radians: f32) -> f32 {
    (f64::from(radians)).sin() as f32
}

#[must_use]
fn cosine(radians: f32) -> f32 {
    (f64::from(radians)).cos() as f32
}

/// Scale by 4096 in float, then narrow to i16 by truncation and bit truncation.
#[must_use]
fn scale_to_fixed(value: f32) -> i16 {
    // Multiplying in f32 is load-bearing: see the module header.
    let scaled = value * TRIG_SCALE;
    // Java's (short) cast truncates toward zero and keeps the low 16 bits. For this domain
    // the magnitude never exceeds 4096, so a saturating cast and a truncating-and-wrapping
    // cast agree; the saturating behaviour is still the one Rust gives, and it is noted.
    scaled as i16
}
