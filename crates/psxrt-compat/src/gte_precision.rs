//! T1, T2 — the arithmetic the geometry transform runs at.
//!
//! # Why this is a knob and not a constant
//!
//! The reference evaluates its transform in floating point. That is not the same as the
//! platform evaluating it in fixed point, and the difference is not zero: two of the transform
//! operations quantise their result and the floating-point path does not quantise the same way.
//! The error is small and bounded — one screen pixel, one colour step — but it is *systematic*
//! rather than random, which means it accumulates along any chain of transforms instead of
//! averaging out.
//!
//! Both readings are recorded and only one is buildable today. The fixed-point arm needs the
//! per-operation quantisation rules to be established from external documentation before there
//! is an implementation to switch to; writing one now would be inventing a rounding rule and
//! labelling it hardware. So the arm is declared, marked as not implemented, and the knob
//! exists so that the day the rules are established the change is a policy flip rather than a
//! redesign.
//!
//! # The part that is not optional
//!
//! One code path in the game is known to do integer arithmetic deliberately: the submap
//! reference view builds its camera from integer deltas, an integer square root, and a
//! normalisation by a shifted multiply. That is not a precision preference, it is the shape of
//! the original's code, and it has an in-source comment saying so. Whatever this knob is set
//! to, that path uses integer maths. A global floating-point default does not license the
//! submap camera to use floats.

/// The arithmetic a geometry transform runs at.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GtePrecision {
    /// Floating point throughout, with the bounded per-operation divergences that implies.
    /// This is what the reference does.
    #[default]
    Float,
    /// Fixed point, per operation. No implementation exists, and none can be written from the
    /// evidence available: which operations quantise, at what format, and in what order is
    /// exactly the unanswered question. Declared so the question stays visible.
    FixedPoint,
}

impl GtePrecision {
    /// The quantisation format of the screen-projection step, floating-point path.
    ///
    /// Eleven fractional bits, truncated. Recorded because the divergence this produces is
    /// measured in whole screen pixels and a one-pixel difference is otherwise invisible in a
    /// screenshot comparison and obvious in a recorded trace.
    pub const PROJECTION_FRACTIONAL_BITS: u32 = 10;

    /// The quantisation format of the lighting step, floating-point path.
    ///
    /// Twelve fractional bits, applied per lighting stage rather than once at the end.
    pub const LIGHTING_FRACTIONAL_BITS: u32 = 12;

    /// T1 — the subset of the transform instruction set that is modelled, and the part that is
    /// not.
    pub const RECORD_ALGORITHMS: crate::PolicyRecord = crate::PolicyRecord {
        key: "T1",
        rows: &["T1"],
        topic: "the modelled subset of the transform instruction set",
        source: "research/psx/05-compatibility-classification.md §3.2; research/psx/02-gte.md",
        confidence: crate::Confidence::ExperimentallyVerified,
        layer: crate::Layer::Platform,
        default: "the used subset only",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "",
        note: "A register-model subset is modelled — the two interpolation queues, the error \
                and flag registers, the projection test, saturation and overflow flags. The \
                unused tail is absent by design rather than by omission: nothing in the game or \
                the reference calls it, and a modelled instruction with no caller cannot be \
                checked against anything. Recorded so that 'we do not implement those' is a \
                decision on the record rather than a gap someone rediscovers.",
        closure: "No open question while no caller exists for the unmodelled tail.",
    };

    /// T2 — floating point versus fixed point.
    pub const RECORD_PRECISION: crate::PolicyRecord = crate::PolicyRecord {
        key: "T2",
        rows: &["T2"],
        topic: "floating-point transform versus the platform's fixed-point transform",
        source: "research/psx/05-compatibility-classification.md §3.2; research/psx/02-gte.md §3",
        confidence: crate::Confidence::Inferred,
        layer: crate::Layer::Platform,
        default: "Float, with the divergence table recorded here and the bounded error measured",
        availability: crate::Availability::Implemented,
        knob: "gte_precision",
        register: "Q-20, Q-67",
        note: "The floating-point path diverges from the fixed-point one at two operations, in \
                a way that is small per call and not small per chain. Projection truncates at \
                eleven-and-ten and lighting quantises per stage at twelve-and-twelve; the \
                reference does neither. Separately, the animation matrix path quantises its \
                accumulated transform to sixteen-and-twelve, and that is the dominant \
                divergence in the whole project because it compounds over a long animation \
                rather than resetting per frame. The fixed-point arm is declared but not \
                built: the per-operation rules are the open question, and guessing them would \
                replace a documented divergence with an undocumented one. See the module header \
                for the one path where integer maths is mandatory regardless of this setting.",
        closure: "External transform documentation, plus a per-camera-path audit of where the \
                  original used integer arithmetic. The submap reference view is the precedent \
                  for what that audit looks like.",
    };
}
