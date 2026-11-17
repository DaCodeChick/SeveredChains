//! Import-side readers: given original-format container bytes, produce working data.
//!
//! This crate is the *reading* half of the asset pipeline. It answers two questions and nothing
//! else: "where do these bytes live?" and "what shape are they in when they arrive?". What the
//! bytes mean once decoded — a model, a texture, a sequence — is a different layer's problem (I1b),
//! and running any of it is a third layer's problem (I3).
//!
//! # What decides whether a format belongs here
//!
//! Two tests, both checked by `tests/architecture.rs` rather than asserted by this paragraph.
//!
//! **Is it a PSX thing or a game thing?** A container's framing, a codec's dictionary, an offset
//! table's scale: those are properties of how the data was written, and a second project building a
//! PSX game would want the same code. A list of *which filenames on one particular disc* behave
//! differently is not that. Game-specific knowledge — the sector-aligned container names, the
//! `files/` cache convention, the patch workflow — lives in `severedchains-game`, and a name like
//! `DRGN0.BIN` appearing in this crate's sources is a placement failure, not a style preference.
//!
//! **Is the reader's answer checkable against something that is not the reader?** A parser with no
//! independent way to be wrong is untested however many fixtures it has, because fixtures generated
//! by the parser's own model say only that the model agrees with itself. Where a format has a
//! writing side, the oracle for it is a test-only module under `tests/support/` that may not import
//! from `src/`. Where it has none, the module header says so and says what the artifact actually is
//! — `PatchList::parse`'s counterpart, for instance, is a hand-authored CSV that nothing in this
//! project emits.
//!
//! # Modules
//!
//! * [`container::bpe`] — decompression. Two paths, chosen by a marker: the BPE codec, or byte
//!   passthrough. Paired with `tests/support/bpe_writer.rs` and exercised by
//!   `tests/bpe_round_trip.rs`.
//! * [`container::mrg`] — the MRG container: an offset/size table, plus the two ways a table entry
//!   can name data it does not own. Paired with `tests/support/mrg_writer.rs` and exercised by
//!   `tests/mrg_round_trip.rs`. The *scale* a table is read at is a parameter, not a lookup; the
//!   game crate decides it.
//!
//! # Not in this milestone
//!
//! `space`, `container::iso` and `pipeline` were scoped into I1a and moved to I1b by owner decision
//! on 2026-11-16; `planning/04` §I1a records the cut and what it costs. They are absent for a reason
//! and not merely unbuilt: none of the three can be checked against anything yet, and `space` in
//! particular was specified against a 63-bit file-key model that the reference does not have. The
//! replacement model is designed in `planning/08-file-space-identity-and-read-domains.md`; read that
//! before writing any of the three, and note its §5 — four verification items are the precondition,
//! not optional reading.
//!
//! # Direction of the test edge
//!
//! `tests/architecture.rs` fails the build if a runtime crate starts depending on an import crate,
//! if an oracle under `tests/support/` grows an import path into `src/`, or if a game-specific name
//! appears in this crate's sources. The two rules above are only rules while something enforces
//! them.

#![forbid(unsafe_code)]

pub mod container;
pub mod error;

pub use error::ImportError;
