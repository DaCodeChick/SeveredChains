//! The 48-bit stream: the generator the battle rules draw from.
//!
//! This is a 48-bit linear congruential generator, not a Mersenne Twister. The "MT" wording
//! that appears on the re-seed entry point in the reference is a comment, not a description
//! of the algorithm; the class behind it derives from the platform's 48-bit generator and
//! only replaces the *unbounded* 32-bit draw. Every consumer that was traced uses a bounded
//! or floating-point draw, which bypasses that replacement entirely — so the replacement is
//! reproduced here because it is observable through the type, and is documented as
//! unreachable from the traced call sites rather than treated as live behaviour.
//!
//! ## Why the draw *forms* are pinned, not just the seed
//!
//! Three of the battle's random behaviours pick a value from a range, and the way a range
//! draw consumes generator output determines which underlying draws it makes:
//!
//! * a power-of-two bound takes a shortcut that uses one draw and scales it;
//! * any other bound enters a rejection loop that may consume several draws before accepting;
//! * a floating-point range draw takes one unit draw, multiplies by the bound in float, and
//!   substitutes the largest representable value below the bound when the product reaches it.
//!
//! Replacing any of these with the "obvious" equivalent — modulo instead of rejection, a
//! double-precision product, a plain multiply without the bound check — shifts the stream
//! and changes which item, which turn order, which particle appears. Pinning this stream
//! means the *call sequence* including these internal extra draws has to match, which is
//! exactly the invariant the replay harness depends on.

/// The multiplier of the state update.
const MULTIPLIER: u64 = 0x0005_deec_e66d;
/// The increment of the state update.
const ADDEND: u64 = 0xb;
/// A 48-bit mask; the state never grows past it.
const STATE_MASK: u64 = (1 << 48) - 1;
/// `2^-24`, the scale that turns a 24-bit draw into a value in `[0, 1)`.
const FLOAT_UNIT: f32 = 1.0 / 16_777_216.0;
/// `2^-53`, the scale for the 53-bit double-precision draw.
const DOUBLE_UNIT: f64 = 1.0 / 9_007_199_254_740_992.0;

/// The 48-bit generator, with the bounded and floating-point draw forms the game uses.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Random48 {
    state: u64,
}

impl Random48 {
    /// Create a stream from an external seed. The seed is scrambled the way the platform
    /// generator scrambles it, which is *not* the same as using the seed as the state.
    #[must_use]
    pub fn with_seed(seed: u64) -> Self {
        Self {
            state: Self::scramble(seed),
        }
    }

    /// Create a stream at a given internal state, for replay from a saved game.
    #[must_use]
    pub const fn with_state(state: u64) -> Self {
        Self { state }
    }

    /// The internal 48-bit state.
    #[must_use]
    pub const fn raw_state(&self) -> u64 {
        self.state & STATE_MASK
    }

    /// Replace the whole internal state, for replay from a saved game.
    pub fn restore_raw(&mut self, state: u64) {
        self.state = state & STATE_MASK;
    }

    /// Seed scrambling: xor with the multiplier, then mask to 48 bits.
    const fn scramble(seed: u64) -> u64 {
        (seed ^ MULTIPLIER) & STATE_MASK
    }

    /// Advance the state and return the top `bits` bits of the new state.
    ///
    /// Every other draw is built from this. Two draws that look interchangeable at the call
    /// site are not, if they ask for a different number of bits: a 24-bit draw advances the
    /// state once, a 32-bit draw also advances it once but takes different bits, and a
    /// double-precision draw advances it twice.
    #[must_use]
    pub fn next_bits(&mut self, bits: u32) -> u32 {
        self.state = self
            .state
            .wrapping_mul(MULTIPLIER)
            .wrapping_add(ADDEND)
            & STATE_MASK;
        (self.state >> (48 - bits)) as u32
    }

    /// A raw 32-bit draw, unprefixed by any mixing.
    #[must_use]
    pub fn next_u32(&mut self) -> u32 {
        self.next_bits(32)
    }

    /// The replacement unbounded draw that the game's generator class introduces.
    ///
    /// Kept because the type's identity includes it and a mod could reach it, but no traced
    /// call site does: every battle and effect draw is bounded or floating-point. A test pins
    /// its values so that removing it later is a decision, not an accident.
    #[must_use]
    pub fn next_u32_mixed(&mut self) -> u32 {
        // The reference hands the 32-bit draw to a method whose parameter is a 64-bit signed
        // integer, so a draw with its top bit set arrives sign-extended, and about half of all
        // draws have the top bit set. Zero-extending here is a different function, not an
        // equivalent spelling of the same one, which is why the widening goes through `i32`.
        let mixed = (self.next_bits(32) as i32) as u64;
        let mixed = (mixed ^ (mixed >> 33)).wrapping_mul(0x62a9_d9ed_7997_05f5);
        let mixed = (mixed ^ (mixed >> 28)).wrapping_mul(0xcb24_d0a5_c88c_35b3);
        (mixed >> 32) as u32
    }

    /// A draw in `0..bound`, or `None` for a non-positive bound.
    ///
    /// Powers of two take a single-draw shortcut; anything else enters the rejection loop,
    /// which is what keeps the distribution even. The loop's continuation test is written in
    /// wrapping arithmetic on purpose — the reference writes it as an overflowing integer
    /// expression and the wrap is load-bearing for large bounds, so a checked or saturating
    /// version would accept in a place where the reference rejects.
    #[must_use]
    pub fn next_bounded_i32(&mut self, bound: i32) -> Option<i32> {
        if bound <= 0 {
            return None;
        }
        let mask = bound - 1;
        let first = self.next_bits(31);

        if bound & mask == 0 {
            // Power of two: one draw, scaled down by the widening multiply.
            let scaled = (i64::from(bound) * i64::from(first)) >> 31;
            return Some(scaled as i32);
        }

        // Reject the over-represented part of the range. The seed draw and the remainder are
        // both non-negative, so the only term that can wrap is the `+ mask`.
        let mut draw = first;
        loop {
            let current = draw as i32;
            let candidate = current % bound;
            if current
                .wrapping_sub(candidate)
                .wrapping_add(mask)
                .is_negative()
            {
                draw = self.next_bits(31);
            } else {
                return Some(candidate);
            }
        }
    }

    /// A float in `[0, 1)`, built from one 24-bit draw.
    #[must_use]
    pub fn next_f32(&mut self) -> f32 {
        (self.next_bits(24) as f32) * FLOAT_UNIT
    }

    /// A float in `[0, bound)`.
    ///
    /// The product is taken in float and then clamped: a float draw can round up to exactly
    /// 1.0 before scaling, so the scaling step is checked and the largest value strictly
    /// below the bound is substituted when the product reaches it. A naive `unit * bound`
    /// therefore differs from this at those points.
    #[must_use]
    pub fn next_f32_bounded(&mut self, bound: f32) -> f32 {
        let scaled = self.next_f32() * bound;
        if scaled >= bound {
            next_down_f32(bound)
        } else {
            scaled
        }
    }

    /// A double in `[0, 1)`, built from *two* draws (26 bits then 27 bits).
    #[must_use]
    pub fn next_f64(&mut self) -> f64 {
        let high = u64::from(self.next_bits(26));
        let low = u64::from(self.next_bits(27));
        // The sum is below 2^53 so it is exactly representable, and scaling by 2^-53 is
        // exact; this is the same value the reference's division produces.
        ((high << 27) + low) as f64 * DOUBLE_UNIT
    }

    /// A double in `[0, bound)`, with the same clamp as the float form.
    #[must_use]
    pub fn next_f64_bounded(&mut self, bound: f64) -> f64 {
        let scaled = self.next_f64() * bound;
        if scaled >= bound {
            next_down_f64(bound)
        } else {
            scaled
        }
    }
}

/// The largest `f32` strictly below `value`.
fn next_down_f32(value: f32) -> f32 {
    if value == f32::INFINITY {
        f32::INFINITY
    } else if value == 0.0 || value == f32::NEG_INFINITY {
        value
    } else if value > 0.0 {
        f32::from_bits(value.to_bits() - 1)
    } else {
        f32::from_bits(value.to_bits() + 1)
    }
}

/// The largest `f64` strictly below `value`.
fn next_down_f64(value: f64) -> f64 {
    if value == f64::INFINITY {
        f64::INFINITY
    } else if value == 0.0 || value == f64::NEG_INFINITY {
        value
    } else if value > 0.0 {
        f64::from_bits(value.to_bits() - 1)
    } else {
        f64::from_bits(value.to_bits() + 1)
    }
}
