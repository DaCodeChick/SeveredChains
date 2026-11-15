//! The three random streams, and the contract they share.
//!
//! Three independent generators exist, and which one a roll draws from is part of the
//! observable behaviour: two of them are seeded once at start-up and never re-seeded, the
//! third is re-seeded from the host clock. A feature that draws from the wrong one changes
//! its distribution even when both are "a random number", so the streams are separate types
//! rather than one type with a parameter.
//!
//! | Stream | State | Output | Where the seed comes from |
//! |---|---|---|---|
//! | [`Lcg16`] | signed 32-bit | 16 bits, `(state / 2) & 0xffff` | constant 3; a guard can pull in the frame counter |
//! | [`Lcg15`] | 32-bit | 15 bits, `(state >> 16) & 0x7fff` | constant `0x2404_0001`, optionally re-seeded per area |
//! | [`Random48`] | 48-bit | words, bounded draws, floats | host clock, or a supplied seed for replay |
//!
//! Nothing in this module reads a clock. Where the reference reached for one, the value is an
//! argument, so a recorded run replays bit-for-bit and a headless test never depends on when
//! it ran. Choosing *what* to feed those arguments is policy and lives in `psxrt-compat`.

mod lcg15;
mod lcg16;
mod random48;

pub use lcg15::Lcg15;
pub use lcg16::Lcg16;
pub use random48::Random48;

/// The shared shape of a random stream: a deterministic stream whose whole state can be read
/// back and restored.
///
/// A stream is reproducible from its state plus the sequence of calls made on it. That is the
/// property the replay harness depends on, and the reason the state is exposed rather than
/// hidden behind an opaque generator.
pub trait RandStream {
    /// Bit width of the internal state. Used to bound the state-restoration property tests.
    const STATE_BITS: u32;

    /// Replace the whole internal state.
    fn restore_state(&mut self, state: u64);

    /// Read the internal state back, for persistence and for assertions.
    fn current_state(&self) -> u64;
}

impl RandStream for Lcg16 {
    const STATE_BITS: u32 = 32;

    fn restore_state(&mut self, state: u64) {
        self.restore_raw(state as i32);
    }

    fn current_state(&self) -> u64 {
        u64::from(self.raw_state() as u32)
    }
}

impl RandStream for Lcg15 {
    const STATE_BITS: u32 = 32;

    fn restore_state(&mut self, state: u64) {
        self.restore_raw(state as u32);
    }

    fn current_state(&self) -> u64 {
        u64::from(self.raw_state())
    }
}

impl RandStream for Random48 {
    const STATE_BITS: u32 = 48;

    fn restore_state(&mut self, state: u64) {
        self.restore_raw(state);
    }

    fn current_state(&self) -> u64 {
        self.raw_state()
    }
}
