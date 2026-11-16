//! Toolchain-side writers, and the recorded divergences from reference behaviour.
//!
//! # What this crate is for
//!
//! It contains code whose job is to *produce* bytes in an original or reference-derived format,
//! and to do so for the benefit of a reader being tested. Right now that is one module,
//! [`container`], with three writers: a BPE encoder, an MRG container writer, and a `files/`
//! cache index writer. Each has its round trip in `psxrt-import/tests/`.
//!
//! The reason a writer lives next to a compatibility question rather than beside the reader it
//! serves is that writing a format forces a decision the reader never has to make. Reading BPE
//! means following one algorithm; *writing* it means choosing a block size, choosing which
//! dictionary slots to spend, and deciding whether to reproduce the reference's quirks or emit
//! clean bytes. Those are choices, they are visible in the output, and they are the kind of thing
//! that needs to be named rather than absorbed into a function body.
//!
//! # What this crate is not
//!
//! It is not a runtime dependency, and it must not become one.
//!
//! The I0 shape of this crate was a policy framework: a `LEDGER` of compatibility records, a
//! `Confidence`/`Layer`/`Availability` taxonomy, and a `CompatPolicy` struct of four knobs. All of
//! it was removed at I1a, for two reasons that are worth keeping rather than quietly repeating.
//!
//! First, nothing read it. A knob with no caller is a field, not a decision — the four fields of
//! `CompatPolicy` selected between behaviours that no subsystem implemented yet, so the framework
//! was describing choices that had not been made about code that did not exist. Second, a
//! mechanism that carries its own knob can only be tested against whichever setting the test
//! happened to build it with, which hides the choice rather than exposing it. The records
//! themselves are not lost: they live in `planning/03-compatibility-policy.md` and in the
//! open-question register in `research/04-unresolved-questions.md`, which is where a reader looks
//! for an open question. A Rust mirror of a markdown table, unread by anything, only offered a
//! second place for the two to disagree.
//!
//! When a real subsystem needs a real knob, it will be introduced with the subsystem, at the
//! point where a test can set it one way and the game can observe the other. Nothing here has to
//! be pre-built for that to work.
//!
//! # The dependency rule
//!
//! Nothing in the runtime depends on this crate, in production or otherwise. The `psxrt-compat →
//! psxrt` edge was deleted at I1a, so a platform mechanism can no longer be reached through a
//! compatibility flag, and this crate starts with zero project dependencies.
//!
//! The only edge into this crate is a `dev-dependency`: `psxrt-import` pulls it in for
//! round-trip tests, so a decoder is checked against its own inverse rather than against
//! hand-written expectations. That is a test fixture, not an architectural edge, and it is the
//! only one permitted. Both halves of the rule — no runtime dependency, and no `use psxrt_compat`
//! anywhere in `psxrt` — are checked by `tests/dependency_direction.rs` in the workspace root
//! rather than asserted in this comment.

pub mod container;
