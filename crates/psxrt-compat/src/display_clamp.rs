//! G3, G4, G13 — display geometry and display latency.
//!
//! These are recorded rather than knobbed. Each is a behaviour with one defensible
//! implementation, and a knob that nobody can evaluate is decoration: G4's clamp has no
//! competing behaviour on the table (the alternative would be to draw 384-wide with no
//! evidence that anything does), and G3 and G13 are descriptions of a pipeline stage that has
//! to be built before there is anything to switch.

/// The width the GPU model substitutes for a 384-wide context.
pub const CLAMPED_WIDTH: u32 = 368;
/// The only width the clamp recognises.
pub const WIDE_WIDTH: u32 = 384;
/// The baseline width every presentation mode starts from.
pub const BASE_WIDTH: u32 = 320;

/// The largest height a presentation context may use, as a multiple of the 240-line baseline.
///
/// Widening and heightening are governed by the same presentation-mode decision, so the
/// height rule is recorded next to the width rule rather than in a separate note.
pub const BASE_HEIGHT: u32 = 240;

/// Apply the wide-context clamp.
///
/// The check is an *equality* test against 384, not a threshold. That is exactly what the
/// reference does, and it matters: a threshold version would also rewrite a hypothetical
/// 400-wide context, and nothing establishes that any context is 400 wide or what should
/// happen to it. Anything that is not 384 passes through untouched, including 385 and 368
/// itself.
///
/// Both entry points that set a width — the display-mode command and the drawing-area command
/// — run the same test, on the same value, before it is used for projection as well as for
/// clipping. A caller must not clamp once and share the result between the two, because they
/// are set from different places in the frame and can disagree.
#[must_use]
pub const fn clamp_draw_width(width: u32) -> u32 {
    if width == WIDE_WIDTH { CLAMPED_WIDTH } else { width }
}

/// G4 — the wide-context width clamp.
pub const RECORD_DRAW_WIDTH: crate::PolicyRecord = crate::PolicyRecord {
    key: "G4",
    rows: &["G4"],
    topic: "wide-context display and draw width clamp",
    source: "research/psx/05-compatibility-classification.md §3.1; research/rendering/03-ui-and-text.md",
    confidence: crate::Confidence::Unresolved,
    layer: crate::Layer::Platform,
    default: "clamp applied, as the reference does",
    availability: crate::Availability::Implemented,
    knob: "",
    register: "Q-37, Q-65",
    note: "Whether the original's effective horizontal resolution behaved the same way is \
            Unresolved. Two distinct 368s exist and must not be merged: this clamp, which the \
            GPU model applies to both the display-mode and the drawing-area width, and the \
            renderer's own projection width, which is 384 for one menu mode and 368 for \
            another. 384-wide content exists in the shipped game, so the clamp is observable \
            and any change to it shifts wide presentations rather than tidying them.",
    closure: "External NTSC and PSX display documentation, plus the visual cross-check of the \
              wide contexts against the original.",
};

/// G3 — the centre-origin draw offset, the buffer arrangement, and the frame counter that
/// invalidates the transform cache.
pub const RECORD_CENTER_ORIGIN: crate::PolicyRecord = crate::PolicyRecord {
    key: "G3",
    rows: &["G3"],
    topic: "centre-origin draw offset; double buffering; the frame counter that invalidates the \
            concatenated-matrix cache",
    source: "research/psx/05-compatibility-classification.md §3.1",
    confidence: crate::Confidence::ExperimentallyVerified,
    layer: crate::Layer::Platform,
    default: "the reference's screenspace-offset mechanism is reproduced",
    availability: crate::Availability::RecordedOnly,
    knob: "",
    register: "",
    note: "Three unrelated mechanisms share this row and only the first is a coordinate \
            question. The matrix-cache half is the load-bearing one: a cache that is keyed on a \
            frame counter which the game can reset will serve stale transforms, and the \
            reference's own comment records that it rebuilt the cache when clearing it. Nothing \
            in I0 reads or invalidates a matrix cache, so the row is recorded here and built \
            where the transform pipeline lives.",
    closure: "No open question; the row is recorded so the three mechanisms are not silently \
              merged when the presentation stage is built.",
};

/// G13 — how many frames a composed image waits before it is shown.
pub const RECORD_DISPLAY_LATENCY: crate::PolicyRecord = crate::PolicyRecord {
    key: "G13",
    rows: &["G13"],
    topic: "one-frame display latency, and the full-motion-video tick cadence",
    source: "research/psx/05-compatibility-classification.md §3.1; research/rendering/01-render-engine.md §2.4; research/rendering/04-postprocessing.md §4.5",
    confidence: crate::Confidence::ExperimentallyVerified,
    layer: crate::Layer::Platform,
    default: "reproduced as part of the frame contract",
    availability: crate::Availability::RecordedOnly,
    knob: "",
    register: "",
    note: "Three separate one-frame effects stack, and a visual comparison against the original \
            has to account for all of them: an image composed in frame N is shown in frame \
            N+1; the swap happens before the per-frame draw, which adds a further frame; and \
            full-motion-video playback is driven by render ticks rather than by logic ticks, so \
            its cadence moves with the frame rate rather than with game time. Recorded here \
            because the latency is only observable once a frame loop exists.",
    closure: "No open question; any difference from the original is a presentation-mode \
              difference and is checked visually, not in I0.",
};
