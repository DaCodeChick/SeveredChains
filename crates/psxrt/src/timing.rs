//! Frame cadence and game time.
//!
//! Two clocks are in play and they are easy to conflate.
//!
//! * The **frame loop** runs at a rate derived from a divisor of 60. The divisor is the
//!   vsync count — how many sixtieths of a second one frame-loop iteration lasts — and a
//!   speed multiplier is applied on top of the resulting rate.
//! * **Game time** is a counter in units of one sixtieth of a second. It advances by the
//!   *raw* vsync count once per frame-loop iteration, for the states that advance time at all.
//!
//! Those two consumers read the same field differently and the difference is observable. The
//! rate clamps the count at one (`Math.max(1, …)`); the game-time increment does not clamp at
//! all, is not checked for divisibility, and is not adjusted for the speed multiplier. A count
//! of zero therefore yields a full-speed frame loop whose game clock stands still, and a count
//! above sixty yields a zero hertz frame loop. Both are reachable: the same field is writable
//! from a script variable and from a debugger spin control, so the domain is wider than the
//! three values the unmodded game happens to use.
//!
//! A third quantity, the script gate, keeps logic at its usual pace when the frame loop is
//! running faster: a state can raise the frame rate to sixty and hold the script engine to
//! every second frame. Getting the composition wrong here is how a port ends up with
//! double-speed gameplay or half-speed animation, which is why the whole matrix is pinned.
//!
//! ## Why nothing here reads a clock
//!
//! The reference derives its frame counter from the *nominal* rate it has just configured, not
//! from measured elapsed time, and it derives game time from the vsync count, not from the
//! rate. Both are pure functions of the cadence, so the whole timing picture is reproducible
//! from the cadence alone and this crate needs no clock, no thread sleeping and no platform
//! timer. A zero hertz rate is returned as zero rather than clamped to one hertz: the
//! reference's platform layer divides a nanosecond budget by the rate and faults on zero, and
//! surfacing the zero keeps that failure visible at the boundary instead of converting it into
//! a silent one-hertz stutter somewhere else.

/// The rate that every divisor is taken against: one tick is one sixtieth of a second.
pub const BASE_RATE_HZ: u32 = 60;

/// The lowest speed multiplier the configuration accepts.
pub const MIN_SPEED_MULTIPLIER: u32 = 1;
/// The highest speed multiplier the configuration accepts.
pub const MAX_SPEED_MULTIPLIER: u32 = 16;

/// How fast the frame loop runs, and how fast game time advances inside it.
///
/// Constructed from a vsync count and a speed rather than from a duration, so that the value a
/// test asserts is the value the original would have computed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FrameCadence {
    /// The raw vsync count, unclamped. The two formulas below clamp it differently, so the
    /// field keeps the unclamped value and each formula applies its own rule.
    vsync_count: i32,
    speed: u32,
}

impl FrameCadence {
    /// The count the timing field starts at, before the game configures it.
    pub const DEFAULT_VSYNC_COUNT: i32 = 1;
    /// The count installed during start-up and used by most contexts: one game tick per two
    /// sixtieths, that is 30 Hz.
    pub const GAMEPLAY_VSYNC_COUNT: i32 = 2;
    /// The count used when a context wants a full 60 Hz tick.
    pub const HIGH_SPEED_VSYNC_COUNT: i32 = 1;
    /// The count two battle sequences raise, giving 20 Hz.
    pub const BATTLE_EVENT_VSYNC_COUNT: i32 = 3;

    /// Build a cadence, rejecting a speed outside the range the configuration accepts.
    ///
    /// The vsync count is accepted unmodified, including zero and negative values, because the
    /// game-time formula uses it raw. Clamping happens inside each formula.
    #[must_use]
    pub const fn new(vsync_count: i32, speed: u32) -> Option<Self> {
        if speed < MIN_SPEED_MULTIPLIER || speed > MAX_SPEED_MULTIPLIER {
            return None;
        }
        Some(Self { vsync_count, speed })
    }

    /// Build the cadence a context uses, at unit speed.
    #[must_use]
    pub const fn with_vsync_count(vsync_count: i32) -> Self {
        Self {
            vsync_count,
            speed: MIN_SPEED_MULTIPLIER,
        }
    }

    /// The raw vsync count.
    #[must_use]
    pub const fn vsync_count(&self) -> i32 {
        self.vsync_count
    }

    /// The divisor the frame-rate formula actually uses: the raw count, raised to one.
    #[must_use]
    pub const fn rate_divisor(&self) -> u32 {
        if self.vsync_count < 1 {
            1
        } else {
            self.vsync_count as u32
        }
    }

    /// Frames of the frame loop per second.
    ///
    /// This is the value the reference hands to both the frame limiter and the input sampler,
    /// so a mismatch here desynchronises input from logic as well as from video.
    ///
    /// The arithmetic is `60 / divisor * speed` with each step truncating, in that order: a
    /// divisor that does not divide sixty truncates before the speed is applied, which is why
    /// the multiplier cannot rescue it. Returns zero for a divisor above sixty. Zero is not a
    /// rate; see the module header for why it is returned rather than clamped.
    #[must_use]
    pub const fn frame_rate_hz(&self) -> u32 {
        BASE_RATE_HZ / self.rate_divisor() * self.speed
    }

    /// How much game time one frame-loop iteration consumes, in sixtieths of a second.
    ///
    /// This is the raw vsync count, not a derivation from [`Self::frame_rate_hz`]. The
    /// reference adds the field directly to the timestamp, so a divisor above sixty still
    /// advances game time while the frame loop around it has no usable rate, and a count of
    /// zero freezes the clock without slowing anything down. The speed multiplier is absent
    /// from this path: running the frame loop faster makes *more* ticks happen, it does not
    /// make each tick cover more game time.
    #[must_use]
    pub const fn clock_increment_sixtieths(&self) -> i32 {
        self.vsync_count
    }
}

/// The game-time counter.
///
/// Deliberately a counter and not a clock: the value is whatever the frame loop has added to
/// it, which is what makes a recorded run replay identically.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct GameClock {
    sixtieths: i64,
}

impl GameClock {
    /// A clock at zero.
    #[must_use]
    pub const fn new() -> Self {
        Self { sixtieths: 0 }
    }

    /// A clock at a given reading, for restoring a saved game.
    #[must_use]
    pub const fn with_sixtieths(sixtieths: i64) -> Self {
        Self { sixtieths }
    }

    /// The current reading, in units of one sixtieth of a second.
    #[must_use]
    pub const fn sixtieths(&self) -> i64 {
        self.sixtieths
    }

    /// Advance by an explicit amount.
    ///
    /// The amount is a parameter rather than a measured interval precisely because the
    /// reference's amount comes from the cadence, not from the host clock. A negative amount
    /// is allowed because the reference's `+=` would apply one.
    pub fn advance(&mut self, sixtieths: i32) {
        self.sixtieths = self.sixtieths.wrapping_add(i64::from(sixtieths));
    }

    /// Advance by this cadence's increment, whatever that is — including zero.
    pub fn advance_by(&mut self, cadence: FrameCadence) {
        self.advance(cadence.clock_increment_sixtieths());
    }
}

/// The script gate: how many frame-loop iterations pass between two logic ticks.
///
/// A state that raises the frame rate keeps its logic pace by raising this count with it, so
/// the pair (rate, count) rather than the rate alone determines how fast the game plays.
/// The counter starts at zero, which means "tick now".
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ScriptTickGate {
    frames_per_tick: u32,
    counter: u32,
}

impl ScriptTickGate {
    /// The count every state starts with, and the count a transition resets to.
    pub const DEFAULT_FRAMES_PER_TICK: u32 = 1;
    /// The only other count the reference installs, for the high-speed submap path.
    pub const HIGH_SPEED_FRAMES_PER_TICK: u32 = 2;

    /// A gate that ticks on every frame.
    #[must_use]
    pub const fn always() -> Self {
        Self {
            frames_per_tick: Self::DEFAULT_FRAMES_PER_TICK,
            counter: 0,
        }
    }

    /// A gate that ticks once every `frames_per_tick` frames.
    ///
    /// A count below one is treated as one: a zero count would never satisfy the tick test and
    /// the reference's modulo would fault.
    #[must_use]
    pub const fn every(frames_per_tick: u32) -> Self {
        Self {
            frames_per_tick: if frames_per_tick < 1 {
                1
            } else {
                frames_per_tick
            },
            counter: 0,
        }
    }

    /// Whether logic runs this frame.
    #[must_use]
    pub const fn ticks_now(&self) -> bool {
        self.counter == 0
    }

    /// Move to the next frame and report whether logic ran on the one just entered.
    ///
    /// The counter cycles, so a gate configured for every second frame ticks on frames 0, 2,
    /// 4 and so on, indefinitely.
    pub fn advance(&mut self) -> bool {
        self.counter = (self.counter + 1) % self.frames_per_tick;
        self.counter == 0
    }

    /// The configured count.
    #[must_use]
    pub const fn frames_per_tick(&self) -> u32 {
        self.frames_per_tick
    }
}
