//! Texture depths and how a texel is addressed inside them.
//!
//! The platform stores four-bit and eight-bit textures as *packed inside 16-bit words*, which
//! is the reason a depth carries four numbers instead of one. To read the texel at x the
//! renderer:
//!
//! 1. divides x by the depth's `width_divisor` to find which 16-bit word holds it;
//! 2. takes `x & width_mask` to find the slot inside that word, shifts it by `index_shift` to
//!    reach the slot's bit position, and masks out the palette index;
//! 3. looks that index up in the colour line that the quad's CLUT address names.
//!
//! Fifteen-bit and twenty-four-bit modes have a divisor of one, a zero mask and a zero shift,
//! so the word *is* the colour and there is no palette step. They are kept as depths rather
//! than as a special case because callers branch on "is this a paletted mode", and that is
//! exactly `index_mask != 0`.
//!
//! ## What is deliberately not here
//!
//! A texture depth is not the whole story about where a texel came from: the page word that
//! carries it also carries the page coordinates and the translucency code, and a depth on its
//! own cannot address anything.
//!
//! The reference has **two** encoders for that word and they do not agree, which is why this is
//! written down rather than quietly resolved:
//!
//! * `MathHelper.makeTpage` builds `x / 64 | y / 256 << 4 | depth << 7`, plus `mode << 5` when
//!   a translucency mode is supplied. Nothing is masked. Because `y / 256` is a two-bit field
//!   placed at bit 4, its upper bit lands on bit 5 — the bit a supplied translucency code also
//!   occupies.
//! * `Graphics.GetTPage` (a port of the game's own routine at `0x8003b3f0`) builds
//!   `(x & 0x3ff) >> 6 | (y & 0x100) >> 4 | (y & 0x200) << 2 | mode << 5 | depth << 7`, masked
//!   to sixteen bits. Here the second page-height bit goes to bit 7 instead — where the depth
//!   code lives.
//!
//! The readers are a third opinion, and they agree with the second encoder: page height is
//! tested as `& 0b10000`, translucency is read as `>>> 5 & 0b11`, depth is read as
//! `>>> 7 & 0b11`. Under that reading a page whose second height bit is set decodes as a depth
//! two steps higher than the one written. Whether the platform lays the field out this way is
//! not established anywhere in this repository and is not claimed here; what is established is
//! that the reference contains both packings, that they differ, and that the disagreement is
//! unresolved rather than settled in favour of either. See `Q-80` in
//! `research/04-unresolved-questions.md`.
//!
//! One further trap, recorded rather than reproduced: two call sites write `>>> 7 * 0b11`,
//! which is not the expression the six comparable sites use (`>>> 7 & 0b11`) — the multiply
//! binds first, so those two shift the word clear out of the depth field. That is a defect in
//! the reference, not a behaviour with a claim on it.
//!
//! Assembling a whole page word needs VRAM coordinates and so belongs with the renderer, not
//! here. Bits 5 and 6 of a TPage word encode the same four modes as bits 28-29 of a draw
//! flag, and the decoder for both is scheduled for `psxrt::gpu::pixel` (`planning/07` F5,
//! landing at I5a) — it does not exist yet. Until it does, the record of that mapping,
//! including the fact that the "no translucency" encoding is itself a visible mode, is the
//! `rate_00` row of `planning/03-compatibility-policy.md` §5. The module this paragraph used
//! to point at was deleted at I1a; a pointer to a file that is gone is worse than none.

use crate::blend::Rgb8;

/// A texture depth, with the addressing parameters that follow from it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Bpp {
    /// Four bits per pixel: sixteen colours, four texels per 16-bit word.
    #[default]
    Bits4,
    /// Eight bits per pixel: two hundred and fifty-six colours, two texels per word.
    Bits8,
    /// Fifteen bits per pixel: a direct read where the word is already a colour.
    Bits15,
    /// Twenty-four bits per pixel: direct colour, no palette.
    Bits24,
}

impl Bpp {
    /// Every depth, in code order.
    pub const ALL: [Self; 4] = [Self::Bits4, Self::Bits8, Self::Bits15, Self::Bits24];

    /// The stored code of this depth. Codes 2 and 3 both read 16-bit words; code 3 is the
    /// no-palette mode.
    #[must_use]
    pub const fn code(self) -> u32 {
        match self {
            Self::Bits4 => 0,
            Self::Bits8 => 1,
            Self::Bits15 => 2,
            Self::Bits24 => 3,
        }
    }

    /// Decode a stored code. There is no fifth depth, so an out-of-range code is an error
    /// rather than a clamp.
    #[must_use]
    pub const fn from_code(code: u32) -> Option<Self> {
        match code {
            0 => Some(Self::Bits4),
            1 => Some(Self::Bits8),
            2 => Some(Self::Bits15),
            3 => Some(Self::Bits24),
            _ => None,
        }
    }

    /// How many texels fit in one 16-bit word along x.
    #[must_use]
    pub const fn width_divisor(self) -> u32 {
        match self {
            Self::Bits4 => 4,
            Self::Bits8 => 2,
            Self::Bits15 | Self::Bits24 => 1,
        }
    }

    /// Which slots exist inside one word.
    #[must_use]
    pub const fn width_mask(self) -> u32 {
        match self {
            Self::Bits4 => 0x3,
            Self::Bits8 => 0x1,
            Self::Bits15 | Self::Bits24 => 0,
        }
    }

    /// How far the slot has to move to reach its bit position in the word.
    #[must_use]
    pub const fn index_shift(self) -> u32 {
        match self {
            Self::Bits4 => 2,
            Self::Bits8 => 3,
            Self::Bits15 | Self::Bits24 => 0,
        }
    }

    /// The width of the extracted field. Zero means the word is a colour, not an index.
    #[must_use]
    pub const fn index_mask(self) -> u32 {
        match self {
            Self::Bits4 => 0xf,
            Self::Bits8 => 0xff,
            Self::Bits15 | Self::Bits24 => 0,
        }
    }

    /// Whether this depth reads through a palette line.
    #[must_use]
    pub const fn is_paletted(self) -> bool {
        self.index_mask() != 0
    }

    /// Extract the palette index, or the colour itself in the unpaletted modes, from the
    /// 16-bit word found at the addressed location.
    ///
    /// The shift is `slot << index_shift`, and the shift amount is taken *before* the mask,
    /// so a four-bit texel at slot 3 shifts by six, not by three.
    #[must_use]
    pub const fn index_in_word(self, word: u16, x: u32) -> Option<u32> {
        if self.index_mask() == 0 {
            None
        } else {
            let slot = x & self.width_mask();
            Some(((word as u32) >> (slot << self.index_shift())) & self.index_mask())
        }
    }
}

/// Where a texel lands once it has been resolved.
///
/// Either it named a palette entry, in which case the entry has to be read from the CLUT
/// line, or it already was a colour. Keeping these as two cases means a caller cannot
/// silently treat a raw 15-bit word as a palette index.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Texel {
    /// A palette index, already shifted out of its word, plus the word it came from.
    Paletted {
        /// The index into the CLUT line.
        index: u32,
        /// The raw 16-bit word, retained so a caller can see the other packed texels.
        word: u16,
    },
    /// A direct colour.
    Direct(Rgb8),
}
