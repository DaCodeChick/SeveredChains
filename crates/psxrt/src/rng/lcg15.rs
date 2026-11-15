//! The 15-bit random stream: the `rand`/`srand` family.
//!
//! Output is taken from the *upper* part of the updated state: the low sixteen bits are
//! dropped and the top bit is masked off, so a draw is always in `0..0x7fff`. Nearly all of
//! the original's uses of this stream are presentational, but the stream identity still
//! matters: it is a separate sequence with its own seed, and drawing from it where the
//! original drew from the 16-bit stream changes the results.

/// The multiplier of the state update.
const MULTIPLIER: u32 = 0x41c6_4e6d;
/// The increment of the state update.
const ADDEND: u32 = 0x3039;
/// How far the updated state is shifted to reach the output.
const OUTPUT_SHIFT: u32 = 16;
/// The output mask.
const OUTPUT_MASK: u32 = 0x7fff;

/// The 15-bit-output linear congruential stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Lcg15 {
    state: u32,
}

impl Lcg15 {
    /// The state the original starts with.
    pub const INITIAL_STATE: u32 = 0x2404_0001;

    /// Create a stream at a given state.
    #[must_use]
    pub const fn with_state(state: u32) -> Self {
        Self { state }
    }

    /// The current state.
    #[must_use]
    pub const fn raw_state(&self) -> u32 {
        self.state
    }

    /// Replace the whole internal state, for replay from a saved game.
    pub fn restore_raw(&mut self, state: u32) {
        self.state = state;
    }

    /// Advance once and produce the next 15-bit value.
    #[must_use]
    pub fn next_raw(&mut self) -> u32 {
        self.state = self.state.wrapping_mul(MULTIPLIER).wrapping_add(ADDEND);
        (self.state >> OUTPUT_SHIFT) & OUTPUT_MASK
    }
}
