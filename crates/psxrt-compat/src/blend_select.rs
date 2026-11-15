//! G10, G12 — the translucency equations, and the mask gates they run behind.
//!
//! This module holds records, not knobs. The four equations have exact integer definitions and
//! are implemented in [`psxrt::blend`]; the reference's accelerated renderer reaches the same
//! four results by a different route. The difference between the two routes is small,
//! documented, and not worth a switch that nobody can evaluate without a frame to look at.
//! Recording it here is what keeps the difference from being mistaken for a bug later.
//!
//! Where a code is stored rather than what it means — the bit positions inside a page word and
//! a flag word — is [`rate_00`](crate::rate_00)'s business, and the extractors live there.

/// G10 — the four translucency equations, and the two routes to them.
pub const RECORD_EQUATIONS: crate::PolicyRecord = crate::PolicyRecord {
    key: "G10",
    rows: &["G10"],
    topic: "translucency equations and their output alpha",
    source: "research/psx/05-compatibility-classification.md §3.1; research/rendering/01-render-engine.md; gfx/shaders/standard.fsh and gfx/shaders/tmd.fsh in the reference",
    confidence: crate::Confidence::ExperimentallyVerified,
    layer: crate::Layer::Platform,
    default: "the exact integer equations, per channel",
    availability: crate::Availability::Implemented,
    knob: "",
    register: "",
    note: "The reference's accelerated path does not evaluate these equations. It reaches the \
            half case by writing a half-strength alpha and letting the host blend stage mix, \
            and it reaches the quarter case by selecting plain additive blending and applying a \
            quarter factor to the incoming colour instead. Both are approximations of the \
            equation rather than different equations, and the quarter case is the one that \
            differs numerically: scaling before adding rounds at a different point than \
            dividing does, by up to one unit per channel. The exact form is chosen because it \
            needs no assumption about what the host blend stage does, and because its result is \
            the one the platform's own painter produces. The subtractive case additionally \
            floors at zero, which a host blend configured for wrapping would not. Output alpha \
            is a separate question recorded under G12, because that is where it has effects.",
    closure: "No open question about the equations themselves; the check would be a per-pixel \
              comparison against the reference's accelerated output, which needs a renderer.",
};

/// G12 — the mask bit, the "not to masked areas" gate, and the textured-alpha gate.
pub const RECORD_MASKING: crate::PolicyRecord = crate::PolicyRecord {
    key: "G12",
    rows: &["G12"],
    topic: "mask-bit set and skip, the not-to-masked-areas gate, and the textured-alpha gate",
    source: "research/psx/05-compatibility-classification.md §3.1",
    confidence: crate::Confidence::ExperimentallyVerified,
    layer: crate::Layer::Platform,
    default: "reproduced as a two-pass structure",
    availability: crate::Availability::RecordedOnly,
    knob: "",
    register: "",
    note: "Three gates sit between a blend result and the framebuffer, and they are the reason \
            blending cannot be a single pass: a primitive may write the mask instead of the \
            colour, it may be skipped where the mask says not to draw, and a textured primitive \
            may be discarded by its own alpha. The output alpha recorded under G10 is what the \
            third gate reads, so the two rows are one mechanism observed twice, not two \
            independent facts. Recorded rather than implemented because a mask is per-pixel \
            framebuffer state and there is no framebuffer in the workspace yet.",
    closure: "No open question; the row is built with the presentation stage.",
};
