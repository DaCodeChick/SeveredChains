//! Test-only writer oracles.
//!
//! Each module here writes bytes for a decoder that lives in `src/`, and none of them may read
//! from `src/`. That separation is the reason a round-trip test in this crate means anything: a
//! writer sitting beside its reader can be written to match the reader's bugs, and the test goes
//! green either way. `tests/architecture.rs` fails the build if a module in here grows a `use`
//! path into the crate under test, and the constants that appear twice — the container magic, the
//! cache version — are compared by name in the round-trip files rather than shared.
//!
//! Not every binary needs every writer, which is why the `pub mod` declarations below carry
//! `allow(dead_code)`: each integration test includes this whole module and uses part of it.

#[allow(dead_code)]
pub mod bpe_writer;
#[allow(dead_code)]
pub mod mrg_writer;
