//! S2, S7 — the volume-sweep envelope, and the granularity of a key-on.
//!
//! # The one knob, and why its default is not the reference's behaviour
//!
//! This is the only entry in the ledger where the shipped default deliberately differs from
//! what the reference does, so it is worth being precise about why.
//!
//! Everywhere else, the choice is between the reference's observed behaviour and a guess about
//! the original, and the reference wins by default because it is at least one observed point.
//! Here the reference has no behaviour to observe. Its own source carries a note saying the
//! envelope is not implemented, and everything that reaches full volume does so because of
//! that note. Copying an absent feature is not fidelity to the original; it is fidelity to an
//! unfinished file, and this project has a second unfinished implementation to compare against
//! if it wants one.
//!
//! The reason a knob still exists: the shape of the envelope is not established either. What is
//! established is that the game writes ramp instructions and encodes them in the same byte as
//! note volume, which is a strange thing to do with a byte nothing reads. So the default applies
//! an envelope, the shape is marked as a proposal rather than a finding, and the knob allows a
//! side-by-side comparison for whoever eventually gets the evidence to settle it. If a census of
//! shipped content shows that no note ever carries ramp bits, the whole entry is unobservable
//! and the knob guards nothing.
//!
//! # The second row
//!
//! Key-on and key-off are collected over a frame and applied at the first sample of the next
//! one. The reference's own readings disagree about how long that frame is, which is a live
//! question outside this crate; what is settled, and what is recorded here, is that the
//! quantisation exists at all. Sub-frame timing differences between the reference and the
//! original are not a divergence to reproduce — the reference is not the original either.

/// Whether a note that asks for a volume ramp gets one.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SweepEnvelope {
/// Apply an envelope. The shape is a proposal, not an established curve; what is established is
/// that a ramp is not silence and is not full volume.
    #[default]
    Applied,
/// Play at full volume for the whole note, which is what the reference does.
    FullVolume,
}

impl SweepEnvelope {
    /// S2 — how a key-on reaches the speaker, and how coarse it is.
    pub const RECORD_EDGE_WORDS: crate::PolicyRecord = crate::PolicyRecord {
        key: "S2",
        rows: &["S2"],
        topic: "key-on and key-off collected as per-channel words and applied once per tick",
        source: "research/psx/05-compatibility-classification.md §3.3",
        confidence: crate::Confidence::ExperimentallyVerified,
        layer: crate::Layer::Platform,
        default: "collect, snapshot, clear, apply at the tick's first sample",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "Q-22, Q-68",
        note: "The collection protocol is the observable contract and it is reproduced: requests \
                accumulate into per-channel words during a frame, the words are snapshotted once, \
                the accumulator is cleared, and the snapshot takes effect at the first sample slot \
                of the tick. Everything finer than that — where in the slot a real channel would \
                actually have started — differs from the original and is not being reproduced \
                deliberately; the reference's own sub-tick timing is its own invention. Recorded \
                so that the quantisation is read as a contract and its resolution as an \
                approximation, rather than both being called the same thing.",
        closure: "Original-engine documentation for the sub-tick behaviour; the contract half \
                needs no closure.",
    };

    /// S7 — the envelope. This is the knob, and the deliberate divergence.
    pub const RECORD_SWEEP: crate::PolicyRecord = crate::PolicyRecord {
        key: "S7",
        rows: &["S7"],
        topic: "volume ramp on notes that request one",
        source: "research/psx/05-compatibility-classification.md §3.3; research/psx/03-spu.md",
        confidence: crate::Confidence::Unresolved,
        layer: crate::Layer::Platform,
        default: "Applied — an envelope is produced",
        availability: crate::Availability::RecordedOnly,
        knob: "sweep_envelope",
        register: "Q-68",
        note: "The reference does not implement this and says so in its own source. That is a \
                gap, not a behaviour, and the compatibility rule this project follows is that a \
                reference gap may be closed rather than preserved. Two things make the default \
                safe to set now: the change is invisible to any note that does not request a \
                ramp, and whether shipped content requests one is an open asset question that \
                does not depend on this choice. What is explicitly NOT claimed is the shape of \
                the envelope — that stays a proposal until the ramp semantics are documented or \
                measured, and the knob exists so the two settings can be compared against real \
                content when they are.",
        closure: "A census of ramp-bit usage in shipped content, plus documentation of the ramp \
                semantics. Together these would settle both whether the envelope is observable \
                and what shape it has.",
    };
}
