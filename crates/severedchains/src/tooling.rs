//! The asset-pipeline conventions this engine's toolchain happens to use.
//!
//! These are not formats and they are not reusable facilities. A cache index and a
//! `scripts.csv` walk are things *this* pipeline does; another project building a PSX game would
//! write a database and a manifest and throw both files away. They live here, and not in the
//! format crate, because leaving them there meant `psxrt-import` documented one game's conventions
//! as though they were PSX's.
//!
//! Each module keeps its own error type — [`cache::CacheError`] and [`patch::PatchError`] — because
//! the two fail for unrelated reasons, and a reader who gets `IndexCycle` and `MissingSource` from
//! the same enum has to work out which subsystem they fell out of before they can act on either.
//!
//! The round-trip oracles for these formats are test-only modules under `tests/support/`, and a
//! support module may not import the reader it is paired with. That is enforced, not hoped for:
//! see `tests/architecture.rs`.

pub mod cache;
pub mod patch;
