//! Channel-level blending, as the platform's painter performs it.
//!
//! Two distinct operations live here and they are not interchangeable:
//!
//! * [`BlendMode`] — the four translucency equations. Which one applies is carried in the
//!   texture-page descriptor, and "no translucency" is a separate case, not mode zero.
//! * [`modulate`] — texture modulation, which multiplies the vertex colour by the texel. It
//!   runs on a different path and can combine with a translucency step.
//!
//! Both are written on 8-bit channels with integer arithmetic. Every step of both is
//! truncating: the quarter-bright equation divides by four rather than shifting (the two are
//! the same for non-negative inputs, and the code says divide), the half equation adds before
//! dividing so an odd sum loses a unit, and modulation clamps the product to a byte after
//! shifting. A float implementation with a round-to-nearest produces different bytes at
//! hundreds of channel pairs, which is why these stay integral and are tested exhaustively.

/// One opaque colour triple, in the red-first order the reference's draw buffer uses.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Rgb8 {
    /// Red.
    pub r: u8,
    /// Green.
    pub g: u8,
    /// Blue.
    pub b: u8,
}

impl Rgb8 {
    /// Build a colour from three channels.
    #[must_use]
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }

    /// Unpack the low three bytes of a 0xBBGGRR-ordered word.
    #[must_use]
    pub const fn from_bgr(word: u32) -> Self {
        Self {
            r: (word & 0xff) as u8,
            g: ((word >> 8) & 0xff) as u8,
            b: ((word >> 16) & 0xff) as u8,
        }
    }

    /// Repack as a 0xBBGGRR-ordered word with no mask bit.
    #[must_use]
    pub const fn to_bgr(self) -> u32 {
        (self.r as u32) | ((self.g as u32) << 8) | ((self.b as u32) << 16)
    }
}

/// A translucency equation.
///
/// The order is the order the reference enumerates them in, and the numeric codes that reach
/// the encoder depend on it; reordering this enum changes rendered output.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum BlendMode {
    /// Half background plus half foreground. This is also the mode the reference reaches by
    /// a different route in its accelerated path — a half-strength alpha write rather than a
    /// distinct equation — which is why the equation has to be stated explicitly here.
    #[default]
    HalfBackgroundPlusHalfForeground,
    /// Full additive.
    BackgroundPlusForeground,
    /// Full subtractive, clamped at zero.
    BackgroundMinusForeground,
    /// Quarter-bright additive.
    BackgroundPlusQuarterForeground,
}

impl BlendMode {
    /// Every mode, in code order.
    pub const ALL: [Self; 4] = [
        Self::HalfBackgroundPlusHalfForeground,
        Self::BackgroundPlusForeground,
        Self::BackgroundMinusForeground,
        Self::BackgroundPlusQuarterForeground,
    ];

    /// The code stored in the texture-page descriptor.
    #[must_use]
    pub const fn ordinal(self) -> u32 {
        match self {
            Self::HalfBackgroundPlusHalfForeground => 0,
            Self::BackgroundPlusForeground => 1,
            Self::BackgroundMinusForeground => 2,
            Self::BackgroundPlusQuarterForeground => 3,
        }
    }

    /// Decode a stored code. Codes outside 0..3 have no meaning and are not clamped into a
    /// neighbouring mode.
    #[must_use]
    pub const fn from_ordinal(code: u32) -> Option<Self> {
        match code {
            0 => Some(Self::HalfBackgroundPlusHalfForeground),
            1 => Some(Self::BackgroundPlusForeground),
            2 => Some(Self::BackgroundMinusForeground),
            3 => Some(Self::BackgroundPlusQuarterForeground),
            _ => None,
        }
    }

    /// Blend one channel.
    #[must_use]
    pub const fn channel(self, background: u8, foreground: u8) -> u8 {
        let background = background as u32;
        let foreground = foreground as u32;
        let mixed = match self {
            // Truncating halving of the sum: an odd sum loses a unit.
            Self::HalfBackgroundPlusHalfForeground => (background + foreground) / 2,
            Self::BackgroundPlusForeground => background + foreground,
            Self::BackgroundMinusForeground => match background.checked_sub(foreground) {
                Some(difference) => difference,
                None => 0,
            },
            // The quarter is taken before the addition, and the divide truncates.
            Self::BackgroundPlusQuarterForeground => background + foreground / 4,
        };
        // Every mode saturates at a byte; subtractive additionally floors at zero.
        if mixed > 0xff {
            0xff
        } else {
            mixed as u8
        }
    }

    /// Blend three channels.
    #[must_use]
    pub const fn apply(self, background: Rgb8, foreground: Rgb8) -> Rgb8 {
        Rgb8 {
            r: self.channel(background.r, foreground.r),
            g: self.channel(background.g, foreground.g),
            b: self.channel(background.b, foreground.b),
        }
    }
}

/// Texture modulation: the vertex colour multiplied by the texel, per channel, clamped to a
/// byte.
///
/// The scale is a divide by 128 after the multiply, so a texel channel of 128 reproduces the
/// vertex channel exactly and anything above it saturates. The product is computed at full
/// width; narrowing it first would wrap.
#[must_use]
pub const fn modulate(vertex: Rgb8, texel: Rgb8) -> Rgb8 {
    Rgb8 {
        r: modulate_channel(vertex.r, texel.r),
        g: modulate_channel(vertex.g, texel.g),
        b: modulate_channel(vertex.b, texel.b),
    }
}

#[must_use]
const fn modulate_channel(vertex: u8, texel: u8) -> u8 {
    let product = ((vertex as u32) * (texel as u32)) >> 7;
    if product > 0xff {
        0xff
    } else {
        product as u8
    }
}
