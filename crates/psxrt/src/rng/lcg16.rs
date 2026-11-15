//! The 16-bit random stream: the `simpleRand` family.
//!
//! State is a *signed* 32-bit integer and the output takes the low 16 bits of `state / 2`
//! after the update. Three details are easy to get wrong and each one changes the stream:
//!
//! * the divide is an arithmetic divide by two, which for a negative state truncates toward
//!   zero, so it is not a right shift;
//! * one of the three call-site families first tests whether the low twelve bits of the state
//!   are zero and, if so, replaces the state with the frame counter — the counter is not mixed
//!   into the state, it discards it. That test is a clock dependency in the reference, and here
//!   the counter is an argument;
//! * the replacement is `frame counter * 9`, and the ordinary update then multiplies by nine
//!   again, so a guarded draw advances the stream by a factor of eighty-one.

/// The multiplier of the state update.
const MULTIPLIER: i32 = 9;
/// The increment of the state update.
const ADDEND: i32 = 0x3711;
/// A state whose low twelve bits are zero triggers the re-seed guard.
const GUARD_MASK: i32 = 0xfff;

/// The 16-bit-output linear congruential stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Lcg16 {
    state: i32,
}

impl Lcg16 {
    /// The state the original starts with.
    pub const INITIAL_STATE: i32 = 3;

    /// Create a stream at a given state.
    #[must_use]
    pub const fn with_state(state: i32) -> Self {
        Self { state }
    }

    /// The current state, for persistence and assertions.
    #[must_use]
    pub const fn raw_state(&self) -> i32 {
        self.state
    }

    /// Replace the whole internal state, for replay from a saved game.
    pub fn restore_raw(&mut self, state: i32) {
        self.state = state;
    }

    /// Advance once and produce the next 16-bit value, with no re-seed guard.
    ///
    /// This is the shape used by the two call-site families that have no guard.
    #[must_use]
    pub fn next_raw(&mut self) -> u16 {
        self.state = self.update(self.state);
        self.output(self.state)
    }

    /// Advance once with the low-twelve-bit guard applied, taking the frame counter as an
    /// argument rather than reading it from a clock.
    ///
    /// When the state's low twelve bits are zero, the counter *replaces* the state — it is not
    /// mixed in. And it replaces it with `frame_count * 9`, after which the ordinary update
    /// multiplies by nine again, so a guarded draw advances the stream by a factor of eighty-one
    /// rather than nine. Both halves are load-bearing: the guard is the only place the frame
    /// counter reaches this stream, and a single multiply there gives a different sequence from
    /// the first guarded draw onwards.
    #[must_use]
    pub fn next_guarded(&mut self, frame_count: i32) -> u16 {
        let base = if (self.state & GUARD_MASK) == 0 {
            frame_count.wrapping_mul(MULTIPLIER)
        } else {
            self.state
        };
        self.state = self.update(base);
        self.output(self.state)
    }

    fn update(&self, base: i32) -> i32 {
        base.wrapping_mul(MULTIPLIER).wrapping_add(ADDEND)
    }

    fn output(&self, state: i32) -> u16 {
        // Divide first, in the signed domain, then keep the low sixteen bits of the result.
        ((state / 2) as u32 & 0xffff) as u16
    }
}
