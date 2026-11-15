//! Backend-agnostic PSX platform/runtime mechanisms.
//!
//! This crate holds *mechanisms*, not policy. A mechanism is a piece of arithmetic or a
//! stateful stream whose behaviour is fully determined by its inputs; nothing here reads a
//! host clock, touches a GPU, or knows what a battle is. Where retail hardware and the
//! reference implementation disagree, or where a choice has to be made about which behaviour
//! to reproduce, that choice is a policy toggle and lives in `psxrt-compat`. This crate
//! exposes the mechanisms those toggles select between, and never a knob to switch them.
//!
//! Two rules hold throughout and are checked by tests:
//!
//! * **No host-clock or wall-time dependency.** Anything the reference read from a clock or a
//!   frame counter is an explicit parameter here, so a headless run and a recorded run produce
//!   the same numbers.
//! * **Nothing is invented.** Where the reference did something odd — a signed division that
//!   is not a shift, a rounding step it never performed, a mask that contradicted its own
//!   documentation — the oddity is reproduced or the divergence is declared, and either way it
//!   is pinned by a golden fixture generated from an independent oracle under
//!   `testdata/generators/`.

pub mod blend;
pub mod bpp;
pub mod fixedpoint;
pub mod rng;
pub mod timing;

pub use blend::{BlendMode, Rgb8, modulate};
pub use bpp::{Bpp, Texel};
pub use fixedpoint::{PSX_DEG_PER_REV, PSX_DEG_TO_RAD, TRIG_SCALE, psx_deg_to_rad, rcos, rsin};
pub use rng::{Lcg15, Lcg16, RandStream, Random48};
pub use timing::{
    BASE_RATE_HZ, FrameCadence, GameClock, MAX_SPEED_MULTIPLIER, MIN_SPEED_MULTIPLIER,
    ScriptTickGate,
};
