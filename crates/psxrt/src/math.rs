//! Runtime mathematics: the numeric primitives a gameplay or rendering path calls, rather than
//! the numeric helpers an asset importer calls.
//!
//! The split is by *who calls it*. A decoder that turns packed bytes into an image is a
//! `psxrt-import` concern and does not live here; a sine that a movement routine evaluates sixty
//! times a second does. Both could be described as "maths", which is why the crate once had a
//! single module called `fixedpoint` holding one and none of the other — the name described no
//! type in it, and its most-used entry point was the one thing in it that was not fixed-point.
//!
//! Nothing here selects a compatibility behaviour. Where two candidates for a routine exist, the
//! module documents the disagreement and implements the one the evidence supports; the knob, when
//! one is needed, arrives with the subsystem that has to choose.

pub mod trig;
