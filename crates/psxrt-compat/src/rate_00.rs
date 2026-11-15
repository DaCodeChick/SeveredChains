//! G11 — what the translucency code zero means, and how "no translucency" is spelled.
//!
//! Two things are easy to conflate and the conflation is silent: the *code* that selects an
//! equation, and the *absence* of an equation. In the reference these are not two values of
//! one field. Absence is carried elsewhere — a primitive is flagged as translucent or it is
//! not, and where a mode is stored in a signed slot the value minus one means "none" — while
//! code zero is a real equation, half-plus-half.
//!
//! That distinction is user-visible. Reading code zero as "opaque" would flatten the highlight
//! backgrounds in shipped menus, which carry code zero, and would do so without any error
//! anywhere.

use psxrt::BlendMode;

/// The value the reference stores in a signed slot when a primitive has no translucency at all.
///
/// It is a sentinel, not a fifth entry in the table. A `u8` field or a `& 0x3` shortcut cannot
/// represent it, which is why [`translucency`] takes a signed value and returns an option.
pub const NO_TRANSLUCENCY: i32 = -1;

/// G11 — the translucency-code-to-equation table, and the fact that code zero is not "off".
pub const RECORD: crate::PolicyRecord = crate::PolicyRecord {
    key: "G11",
    rows: &["G11"],
    topic: "translucency rate zero means half-plus-half",
    source: "research/psx/05-compatibility-classification.md §3.1; research/rendering/01-render-engine.md §10; research/rendering/02-tmd-pipeline.md; research/rendering/03-ui-and-text.md §3.2",
    confidence: crate::Confidence::Unresolved,
    layer: crate::Layer::Platform,
    default: "the reference's four-entry table, code zero = half-plus-half",
    availability: crate::Availability::Implemented,
    knob: "",
    register: "Q-65",
    note: "Three independent consumers of this table were read: the GPU-model triangle path, \
            the modern mesh split, and the user-interface glyph path. All three read code zero \
            as half-plus-half, and code zero does occur in shipped content — the menu \
            highlight backgrounds use it — so the mapping is on screen in the reference rather \
            than a corner of the encoding. What is NOT established is that the platform's own \
            rate encoding reads this way: a four-entry table with this ordering is one \
            documented reading among more than one, and no external GPU documentation has been \
            consulted. The table is therefore recorded as Unresolved and reproduced because it \
            is the only behaviour that has been observed, not because it is believed correct. \
            Two further details that a shader file alone would mislead about: the quarter \
            equation is reached with a plain additive blend plus a colour scale rather than \
            with its own blend, and the half equation is reached with a half-strength alpha \
            rather than with its own blend, so the accelerated path has no distinct state \
            corresponding to either code.",
    closure: "External documentation of the platform's rate-bit encoding, plus a retail asset \
              census of which codes actually appear. Either would let the table be corrected \
              rather than merely reproduced.",
};

/// Decode a stored translucency code.
///
/// [`NO_TRANSLUCENCY`] decodes to [`None`], and so does anything outside the table. Those two
/// answers mean different things — one is "draw without blending", the other is a malformed
/// word — and the caller is the place that knows which. Collapsing them into a default mode is
/// how a menu panel quietly loses its transparency.
#[must_use]
pub const fn translucency(code: i32) -> Option<BlendMode> {
    if code >= 0 {
        BlendMode::from_ordinal(code as u32)
    } else {
        None
    }
}

/// Bit position of the translucency code inside a texture-page word.
pub const PAGE_TRANSLUCENCY_SHIFT: u32 = 5;
/// Bit position of the translucency code inside a primitive's flag word.
pub const FLAG_TRANSLUCENCY_SHIFT: u32 = 28;

/// Read the translucency code out of a texture-page word.
///
/// Bits 5 and 6 carry the code; the page coordinates and the texture depth sit in the same
/// word, so extracting the code is a separate step from decoding it. The depth occupies bit 7,
/// which is why the mask is two bits wide and not three.
#[must_use]
pub const fn page_translucency(page_word: u32) -> Option<BlendMode> {
    translucency(((page_word >> PAGE_TRANSLUCENCY_SHIFT) & 0x3) as i32)
}

/// Read the translucency code out of a primitive's flag word.
///
/// The same four-entry table at a different bit position, which is why it needs its own name:
/// reading the page bits where the flag bits belong yields a plausible-looking wrong mode for
/// three quarters of inputs.
#[must_use]
pub const fn flag_translucency(flag_word: u32) -> Option<BlendMode> {
    translucency(((flag_word >> FLAG_TRANSLUCENCY_SHIFT) & 0x3) as i32)
}
