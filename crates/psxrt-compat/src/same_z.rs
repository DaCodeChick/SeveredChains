//! G6 — how primitives at the same depth order inside one ordering-table slot.
//!
//! The ordering table has one list per depth slot, and the list has to be walked in *some*
//! order. Which order is not determined by the depth: two primitives with equal depth draw in
//! insertion order or in insertion-reverse order depending on this choice, and for opaque
//! geometry that is visible whenever they overlap.
//!
//! The reference's GPU model walks its slot lists back-to-front, which makes the *last*
//! inserted primitive of a slot draw first — the `Lifo` arm. Its modern renderer's equal-depth
//! tie-break is a separate matter and is described under
//! [Reference-internal tie-breaks](#reference-internal-tie-breaks); it is not this knob.

/// The draw order within a single ordering-table slot.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SameZOrder {
    /// Insertion-reverse: the last command pushed into a slot is the first one drawn. This is
    /// what the reference's GPU model does, and it is load-bearing for one shipped effect
    /// (see [`RECORD`]).
    #[default]
    Lifo,
    /// Insertion order: the first command pushed into a slot is the first one drawn. This is
    /// the plausible original behaviour and is the reason the knob exists.
    Fifo,
}

impl SameZOrder {
    /// This project's ledger entry for the row.
    pub const RECORD: crate::PolicyRecord = crate::PolicyRecord {
        key: "G6",
        rows: &["G6"],
        topic: "same-depth primitive order within an ordering-table slot",
        source: "research/psx/05-compatibility-classification.md §3.1; research/rendering/01-render-engine.md §11",
        confidence: crate::Confidence::Unresolved,
        layer: crate::Layer::Platform,
        default: "Lifo (the reference's observed walk); Fifo selectable",
        availability: crate::Availability::RecordedOnly,
        knob: "same_z_order",
        register: "Q-65, Q-66, Q-71",
        note: "Queue order (Fifo) is the layer-1 attribution and is only Inferred: no external \
              PSX GPU documentation has been consulted. The reference's slot walk is Lifo, and \
              one shipped effect depends on it — the battle texture-animation circular scroll \
              reads as wrong under Fifo, so flipping this knob is a visual change, not a \
              neutral one. The reference's modern 2D pool also tie-breaks Lifo and its 3D pool \
              FIFO at equal depth; that split is layer-4 engine behaviour and is held fixed \
              regardless of this knob.",
        closure: "Original-PSX observation or external PSX GPU documentation of same-slot \
                  ordering; or a retail-TMD census of same-depth overlaps.",
    };

    /// Where a command that was pushed at `insertion_index` into a slot holding `count`
    /// commands lands in the draw sequence.
    ///
    /// Zero is drawn first. The function is total and undefined inputs are rejected by
    /// returning [`None`], because an ordering mistake here is a rendering artefact that is
    /// extremely hard to trace back to its cause.
    #[must_use]
    pub const fn draw_position(self, insertion_index: usize, count: usize) -> Option<usize> {
        if insertion_index >= count {
            return None;
        }
        match self {
            Self::Lifo => Some(count - 1 - insertion_index),
            Self::Fifo => Some(insertion_index),
        }
    }
}
