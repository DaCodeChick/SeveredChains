//! Legend of Dragoon knowledge, kept out of the crates that are supposed to be reusable.
//!
//! This crate exists because "PSX engine" and "this game" are two different things, and the
//! workspace only stays readable while something keeps them apart. `psxrt` is a platform's
//! arithmetic; `psxrt-import` is a set of file formats. Neither of them knows that a game called
//! Legend of Dragoon exists. Everything in here knows it, and that is the whole admission test:
//! *if a second game used the same engine, would it want this file too?* If yes, it is in the
//! wrong crate.
//!
//! Concretely, that means this crate owns facts of the form "in *this* game, *this* name means
//! *this*", and the code that acts on them:
//!
//! * [`container::mrg`] — which container names count their offsets in sectors rather than bytes.
//!   The parser takes a scale; it does not look up a filename, because a filename is not a property
//!   of the MRG format. It is a property of one disc.
//! * [`tooling::cache`] — the `files/` cache index: the ASCII layout, the pointer walk, and the
//!   version marker that decides whether a whole cache tree is thrown away.
//! * [`tooling::patch`] — the `patches/scripts.csv` walk, its CRC-32 gate and its backup
//!   discipline, in front of a toolchain that does not exist yet (I4).
//!
//! # What this crate is not
//!
//! It is not a home for "generic enough" code that happened to be written while working on this
//! game, and it is not a staging area to be drained later. A module placed here because nothing
//! else would take it will stay here. The test is the one above, applied to the file, not to the
//! milestone that added it.
//!
//! Gameplay rules, compatibility policy and the engine's own state will also land here when they
//! get written — as `rules/`, beside this, not inside it. What none of them will get is a
//! placeholder: a type with no producer and no caller is not a seam, it is a shape that makes an
//! unfinished area look finished.
//!
//! # Direction of the dependency edge
//!
//! This crate depends on `psxrt-import` for one thing: [`psxrt_import::container::mrg::OffsetScale`],
//! the scale that [`container::mrg`] selects. That edge points the right way — a game crate may
//! read a format crate, never the reverse — and `psxrt-import` has no dependency on anything here.
//! The test-only oracle writers under `tests/support/` are likewise forbidden from importing a
//! reader; see `tests/architecture.rs`, which fails the build if either edge appears.

#![forbid(unsafe_code)]

pub mod container;
pub mod tooling;
