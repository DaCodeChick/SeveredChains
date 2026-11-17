//! Test-only oracles for `severedchains-game`.
//!
//! The same rule as `psxrt-import/tests/support`, and for the same reason: an oracle that borrows
//! its idea of the format from the code under test cannot disagree with it, and a disagreement is
//! the only thing a round-trip test could have proved. `tests/architecture.rs` enforces it.
//!
//! * [`index_writer`] — writes the `files/` per-directory index. Paired with
//!   `severedchains_game::tooling::cache::Index` and exercised by `tests/index_round_trip.rs`.

pub mod index_writer;
