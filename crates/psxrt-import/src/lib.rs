//! Import-side readers: given original-format container bytes, produce working data.
//!
//! This crate is the *reading* half of the asset pipeline. It answers two questions and nothing
//! else: "where do these bytes live?" and "what shape are they in when they arrive?". What the
//! bytes mean once decoded — a model, a texture, a sequence — is a different layer's problem
//! (I1b), and running any of it is a third layer's problem (I3).
//!
//! # What lives here, and what does not
//!
//! Two rules decide placement, and both are checked rather than asserted.
//!
//! **Readers only.** A module in this crate decodes. The matching encoder for anything here
//! lives in `psxrt-compat`, because emitting original-format bytes is a decision about how to
//! write the world down, while reading them is a fact about how they were written. Keeping the
//! pair in separate crates is what makes a round-trip test mean something: an encoder that sits
//! beside its decoder can be written to match the decoder's bugs, and the test goes green
//! either way. `psxrt-compat` is therefore a `dev-dependency` of this crate and never a
//! production one.
//!
//! **No invented formats.** Every format here is reproduced from the reference implementation,
//! and where the reference does something odd the oddity is either replicated or the divergence
//! is declared in the module that owns it. A module whose header says "there is no second
//! decoder here" is stating that it checked, not that it ran out of time.
//!
//! # Modules
//!
//! * [`container::bpe`] — decompression. Two paths, chosen by a marker: the BPE codec, or byte
//!   passthrough. Both are paired with an encoder in `psxrt-compat` and exercised by
//!   `tests/bpe_round_trip.rs`.
//! * [`container::mrg`] — the MRG container: an offset/size table, plus the two ways a table
//!   entry can name data it does not own. Paired with a writer in `psxrt-compat` and exercised
//!   by `tests/mrg_round_trip.rs`.
//! * [`cache`] — the `files/` cache index codec, and the bounded pointer walk that reads one.
//!   Two writers feed it: [`container::mrg::Table::index_text`] produces the same text from a
//!   parsed table, and `psxrt_compat::container::index` produces it from an independently
//!   written description of the format. `tests/cache_round_trip.rs` checks the first pairing,
//!   `tests/index_round_trip.rs` the second — the second is the one that could actually fail.
//! * [`patch`] — the patch orchestration half: the `scripts.csv` walk, the CRC32 gate over each
//!   entry's `#include` closure, the one-time backup and the restore-on-removal, in front of an
//!   injected [`patch::Toolchain`] that is a recording stub until I4.
//! * `space`, `container::iso`, `pipeline` — **not in this milestone.** All three were scoped
//!   into I1a and moved to I1b by owner decision on 2026-11-16; `planning/04` §I1a records the
//!   cut and what it costs. They are absent for a reason and not merely unbuilt: none of the
//!   three can be checked against anything yet, and `space` in particular was specified against
//!   a 63-bit file-key model that the reference does not have. The replacement model is
//!   designed in `planning/08-file-space-identity-and-read-domains.md`; read that before
//!   writing any of the three, and note its §5 — four verification items are the precondition,
//!   not optional reading.
//!
//! # Direction of the test edge
//!
//! `tests/dependency_direction.rs` fails the build if a runtime crate starts depending on
//! `psxrt-compat`, or if a `psxrt` source file names `psxrt_compat`. The rule above is only a
//! rule while something enforces it.

#![forbid(unsafe_code)]

pub mod cache;
pub mod container;
pub mod error;
pub mod patch;

pub use error::ImportError;
