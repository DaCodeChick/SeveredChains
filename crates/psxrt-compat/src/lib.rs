//! Compatibility policy: the places where "what the reference did" and "what the original
//! hardware did" are different questions, and where the answer is not settled by evidence held
//! in this repository.
//!
//! # Why this crate exists
//!
//! [`psxrt`](../psxrt/index.html) holds mechanisms — arithmetic and streams whose result is
//! fully determined by their inputs. This crate holds the choices about which behaviour to
//! reproduce.
//!
//! The split is not cosmetic. A mechanism that carries its own knob can only ever be tested
//! against whichever setting the test happened to build it with, and the choice disappears from
//! the reader's view: what remains visible is a branch, not an open question. Moving the choices
//! here means each one has to be named, defaulted, sourced, and given a closure condition before
//! anything can use it, and it means a subsystem can be built against its default today and
//! flipped later without a redesign.
//!
//! # The shape of a policy
//!
//! Every entry in [`LEDGER`] carries:
//!
//! * the compatibility-checklist rows it answers to, so a reader can find the evidence;
//! * the confidence class of the *original* behaviour — never better than `Inferred` for the
//!   entries here, because a `Verified` original would make this a fact rather than a policy;
//! * which layer the observation belongs to: the platform, the game's content, or the reference
//!   implementation's own conventions;
//! * the default this project ships, and whether the mechanism behind it exists yet;
//! * the knob, if there is one, and the register item whose evidence would close the question.
//!
//! Two invariants are checked by `tests/policy_ledger.rs` rather than asserted in prose: every
//! record states a divergence and a closure condition, and no record claims that anything has
//! been checked against retail bytes, because none has.
//!
//! # What is deliberately not a knob
//!
//! Things the reference did that are merely unspecified — an unused field, a dead branch, a
//! capacity that changes during start-up — are not knobs. Turning them into knobs would imply a
//! choice where there is nothing to choose between, and every fake knob spends testing effort
//! that a real one needs. The [`CompatPolicy`] struct therefore has four fields and the ledger
//! has sixteen entries; the difference is the point.
//!
//! # What is deliberately not closed
//!
//! None of these entries is resolved. Where the original's behaviour is unknown, the default is
//! the reference's behaviour and the divergence is written down — with one exception, explained
//! in [`sweep`](mod@sweep), where the reference has an unfinished implementation rather than a
//! behaviour. Recording a default is not the same as answering the question, and the
//! `register` field on each record points at the open question that still has to be answered.

pub mod blend_select;
pub mod display_clamp;
pub mod gte_precision;
pub mod predictor;
pub mod rate_00;
pub mod same_z;
pub mod sweep;
pub mod voice_alloc;

pub use gte_precision::GtePrecision;
pub use predictor::PredictorReset;
pub use same_z::SameZOrder;
pub use sweep::SweepEnvelope;
pub use voice_alloc::{Phase as VoiceAllocationPhase, VoiceCapacity};

/// How firmly the *original* behaviour behind a policy is established.
///
/// This describes evidence, not importance. `Inferred`, `Unresolved` and `Proposed` are
/// legitimate states to ship in: the default is then a documented choice rather than a
/// discovered fact, and a knob exists so the choice can be reversed when evidence lands.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Confidence {
    /// Established from reference source, reference tests, or a documented format.
    SourceVerified,
    /// Independently corroborated by a second specification or implementation.
    FormatCorroborated,
    /// Established by running something and measuring the result.
    ExperimentallyVerified,
    /// Supported by evidence but not conclusively — the most a platform attribution reaches
    /// without external documentation or retail bytes.
    Inferred,
    /// The evidence is missing or contradicts itself.
    Unresolved,
    /// A design decision that makes no claim about the original at all.
    Proposed,
}

/// Which of the three layers of truth an observation belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Layer {
    /// Original platform behaviour, independent of any implementation of it.
    Platform,
    /// Specific to this game's content or scripting.
    GameContent,
    /// Specific to the reference implementation — its fixes, workarounds, conventions, or
    /// unfinished code. Reproducing one of these is a choice, never an obligation.
    ReferenceImplementation,
}

/// Whether the mechanism a policy selects between is present.
///
/// A policy can be recorded before its alternative is buildable. Saying so explicitly keeps a
/// knob from reading like a tested feature.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Availability {
    /// The default's mechanism exists and is exercised by tests.
    Implemented,
    /// Recorded so the question is not lost; nothing branches on it yet.
    RecordedOnly,
}

/// One entry of the compatibility ledger.
#[derive(Debug, Clone, Copy)]
pub struct PolicyRecord {
    /// The identifier this project uses for the policy.
    pub key: &'static str,
    /// The compatibility-checklist rows this entry answers.
    pub rows: &'static [&'static str],
    /// Short name of the behaviour.
    pub topic: &'static str,
    /// Where the evidence lives, by document and section.
    pub source: &'static str,
    /// Confidence in the *original* behaviour, not in the reference's behaviour.
    pub confidence: Confidence,
    /// Which layer the observation belongs to.
    pub layer: Layer,
    /// What this project ships.
    pub default: &'static str,
    /// Whether the mechanism exists.
    pub availability: Availability,
    /// The knob name, or an empty string when there is none.
    pub knob: &'static str,
    /// The open-question register items that would close this, or an empty string.
    pub register: &'static str,
    /// The divergence itself, stated so a reader can act on it.
    pub note: &'static str,
    /// What evidence would resolve the question.
    pub closure: &'static str,
}

/// Every recorded policy, in checklist order.
///
/// The list exists so the ledger can be audited mechanically and so a reader can see the whole
/// open surface at once instead of discovering it one subsystem at a time. It is the record; the
/// fields of [`CompatPolicy`] are the uses made of it.
pub const LEDGER: &[PolicyRecord] = &[
    same_z::SameZOrder::RECORD,
    display_clamp::RECORD_DRAW_WIDTH,
    display_clamp::RECORD_CENTER_ORIGIN,
    display_clamp::RECORD_DISPLAY_LATENCY,
    rate_00::RECORD,
    blend_select::RECORD_EQUATIONS,
    blend_select::RECORD_MASKING,
    gte_precision::GtePrecision::RECORD_ALGORITHMS,
    gte_precision::GtePrecision::RECORD_PRECISION,
    sweep::SweepEnvelope::RECORD_EDGE_WORDS,
    sweep::SweepEnvelope::RECORD_SWEEP,
    voice_alloc::VoiceCapacity::RECORD,
    predictor::PredictorReset::RECORD_HEADER,
    predictor::PredictorReset::RECORD_FMV_GAIN,
    predictor::PredictorReset::RECORD_PREDICTOR,
    predictor::PredictorReset::RECORD_MONO_LANES,
];

/// The knobs, at the defaults this project ships.
///
/// A policy that is recorded but not knobbed deliberately has no field here: its behaviour is
/// fixed at the default and its ledger record explains why.
///
/// These are build-time and diagnostic surfaces. They are not part of the release configuration
/// surface, which is the game's own saved-settings census, and nothing here is reachable from a
/// saved game or from a mod.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct CompatPolicy {
    /// G6 — how same-depth primitives order within one ordering-table slot.
    pub same_z_order: SameZOrder,
    /// T2 — the arithmetic the geometry transform runs at.
    pub gte: GtePrecision,
    /// X6 — whether the audio predictor carries state across a file boundary.
    pub predictor_reset: PredictorReset,
    /// S7 — whether a note that asks for a volume ramp gets one.
    pub sweep: SweepEnvelope,
}

impl CompatPolicy {
    /// Every knob set to the reference implementation's observed behaviour.
    ///
    /// Exactly one entry differs from [`CompatPolicy::default`]: the sweep envelope, where the
    /// reference has an unfinished implementation rather than a behaviour to observe. Everything
    /// else is already reference-faithful at its default. `tests/policy_ledger.rs` compares the
    /// two configurations field by field so that this statement cannot quietly become false.
    #[must_use]
    pub const fn reference_fidelity() -> Self {
        Self {
            same_z_order: SameZOrder::Lifo,
            gte: GtePrecision::Float,
            predictor_reset: PredictorReset::Never,
            sweep: SweepEnvelope::FullVolume,
        }
    }
}
