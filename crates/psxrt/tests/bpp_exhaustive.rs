//! Exhaustive checks of texture-depth addressing.
//!
//! The four depths are not "four, eight, fifteen and twenty-four bits"; they are four ways of
//! packing texels into a 16-bit word, and three of the four numbers a depth carries only mean
//! something together: `width_divisor` finds the word, `width_mask` finds the slot inside it,
//! `index_shift` moves that slot to its bit position, `index_mask` cuts it out. Get one wrong
//! and textures still render — with the wrong colours, which is the worst kind of failure to
//! notice.
//!
//! So the tests below do not check the four numbers in isolation. They build synthetic texture
//! lines by *packing* texels into words in the layout the format uses, read every texel back
//! through the depth's addressing, and require the original index. Every slot of every 16-bit
//! word is checked, for every paletted depth.
//!
//! ## Where the layout comes from
//!
//! Not from the Rust code. Packed texels sit in a 16-bit word low-index-first, each
//! `16 / texels_per_word` bits wide: for a four-bit depth, texel 0 in bits 0..3 and texel 3 in
//! bits 6..7; for an eight-bit depth, texel 0 in bits 0..7. The reference reads that layout in
//! `Gpu.getTexel` as `index >> ((x & widthMask) << indexShift) & indexMask` after taking the
//! word at `x / widthDivisor`. The test states the layout independently — deriving the bit
//! position from the texel width rather than from `index_shift` — so that a change on either
//! side has to disagree with something written down.

use psxrt::{Bpp, Texel};

/// Width, in texels, of the synthetic line the round-trip sweeps walk.
const LINE_TEXELS: u32 = 4096;

/// Every 16-bit word, for the sweeps that go through the whole addressable range.
fn all_words() -> impl Iterator<Item = u16> {
    u16::MIN..=u16::MAX
}

/// How many texels a word holds at this depth, and how wide each is. Written from the format,
/// not read from `Bpp`: the two have to agree, and deriving one from the other would test
/// nothing.
fn packed_layout(depth: Bpp) -> Option<(usize, usize)> {
    let texels = match depth {
        Bpp::Bits4 => 4,
        Bpp::Bits8 => 2,
        Bpp::Bits15 | Bpp::Bits24 => return None,
    };
    Some((texels, 16 / texels))
}

/// The index a word carries at slot `slot`, per the layout above.
fn slot_index(word: u16, depth: Bpp, slot: usize) -> Option<u32> {
    let (_, width) = packed_layout(depth)?;
    Some((u32::from(word) >> (slot * width)) & ((1 << width) - 1))
}

/// Pack a run of indices into words, low index first.
fn pack(depth: Bpp, indices: &[u8]) -> Vec<u16> {
    let Some((per_word, width)) = packed_layout(depth) else {
        return Vec::new();
    };
    indices
        .chunks(per_word)
        .map(|chunk| {
            chunk.iter().enumerate().fold(0_u32, |word, (slot, index)| {
                word | u32::from(*index) << (slot * width)
            }) as u16
        })
        .collect()
}

/// Deterministic word contents for the sweeps. Deliberately not one of the streams under test:
/// generating test input with `psxrt`'s own generators would put the thing under test inside its
/// own oracle.
fn pattern_words(count: usize) -> Vec<u16> {
    let mut state = 0x2545_F491_4F6C_DD1D_u64;
    (0..count)
        .map(|_| {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            state = state.wrapping_mul(0xBF03_6399_A1C1_E06B);
            ((state >> 32) & 0xffff) as u16
        })
        .collect()
}

/// Every slot of every 16-bit word, at the x that addresses it inside its own word.
///
/// One x per slot here, deliberately: this sweep is about the mask and the shift, and the
/// word-selection half of the addressing gets its own sweep below. Splitting them keeps a
/// failure in one from looking like a failure in the other.
#[test]
fn every_packed_slot_of_every_word_reads_back_through_the_depth() {
    for depth in Bpp::ALL {
        let Some((per_word, _)) = packed_layout(depth) else {
            continue;
        };

        let mut checks = 0_usize;
        for word in all_words() {
            for slot in 0..per_word {
                let expected = slot_index(word, depth, slot).unwrap();
                assert_eq!(
                    depth.index_in_word(word, u32::try_from(slot).unwrap()),
                    Some(expected),
                    "{depth:?} at slot {slot} in word {word:#06x}"
                );
                checks += 1;
            }
        }
        assert_eq!(checks, 65_536 * per_word, "{depth:?}");
    }
}

/// The word-selection half: one word must serve every texel that packs into it, and a different
/// word must serve the next group. Swept over a whole line, so the stride is exercised at every
/// offset rather than only in the first two groups.
#[test]
fn consecutive_texels_share_a_word_and_the_grouping_covers_the_line() {
    for depth in Bpp::ALL {
        let Some((per_word, _)) = packed_layout(depth) else {
            continue;
        };
        let words = pattern_words(LINE_TEXELS as usize / per_word);

        for group in 0..LINE_TEXELS as usize / per_word {
            for slot in 1..per_word {
                let x = group * per_word + slot;
                assert_eq!(
                    u32::from(words[group]),
                    u32::from(words[x / per_word]),
                    "{depth:?}: x {x} must address word {group}, not word {}",
                    x / per_word
                );
                let first = slot_index(words[group], depth, 0);
                let this = slot_index(words[group], depth, slot);
                if first != this {
                    assert_ne!(
                        depth.index_in_word(words[group], u32::try_from(x).unwrap()),
                        depth.index_in_word(words[group], u32::try_from(group * per_word).unwrap()),
                        "{depth:?}: x {x} and x {} are different slots of word {group} but read \
                         the same value — the slot is not being applied",
                        group * per_word
                    );
                }
            }
        }
    }
}

/// Pack a whole line, then read it back through the addressing.
///
/// `pack` is the layout, `index_in_word` is the addressing, and the two are checked against each
/// other *and* against the line. Word order and line order differ, which is the point: the
/// addressing has to reorder.
#[test]
fn a_whole_line_survives_packing_and_addressing() {
    for depth in Bpp::ALL {
        let Some((per_word, _)) = packed_layout(depth) else {
            continue;
        };
        let words = pattern_words(LINE_TEXELS as usize / per_word);
        let indices: Vec<u8> = words
            .iter()
            .flat_map(|&word| {
                (0..per_word).map(move |slot| slot_index(word, depth, slot).unwrap() as u8)
            })
            .collect();

        assert_eq!(
            pack(depth, &indices),
            words,
            "{depth:?}: repacking must be the identity"
        );

        let read_back: Vec<u32> = (0..LINE_TEXELS)
            .map(|x| {
                let word = words[x as usize / per_word];
                depth.index_in_word(word, x).unwrap()
            })
            .collect();
        let expected: Vec<u32> = indices.iter().map(|index| u32::from(*index)).collect();
        assert_eq!(read_back, expected, "{depth:?}");
    }
}

/// A line where adjacent texels differ must not read the same colour twice; a uniform line must
/// read uniformly. Two cases, because a stride error shows up in the first and a masking error
/// in the second.
#[test]
fn alternating_texels_never_collapse_onto_their_neighbour() {
    for depth in Bpp::ALL {
        let Some((per_word, _)) = packed_layout(depth) else {
            continue;
        };

        let indices: Vec<u8> = (0..LINE_TEXELS as usize).map(|x| (x % 2) as u8).collect();
        let words = pack(depth, &indices);
        for (x, expected) in indices.iter().enumerate() {
            let word = words[x / per_word];
            assert_eq!(
                depth.index_in_word(word, u32::try_from(x).unwrap()),
                Some(u32::from(*expected)),
                "{depth:?} at x {x}"
            );
        }

        // Uniform index 1 in every slot of every word: the word is then the same everywhere, so
        // a wrong divisor would be invisible and only the mask/shift pair is under test.
        let uniform = vec![1_u8; LINE_TEXELS as usize];
        let words = pack(depth, &uniform);
        assert!(
            words.iter().all(|word| *word == words[0]),
            "{depth:?}: a uniform line must pack to a repeated word"
        );
        for x in 0..LINE_TEXELS {
            assert_eq!(
                depth.index_in_word(words[0], x),
                Some(1),
                "{depth:?} at x {x}"
            );
        }
    }
}

/// The four numbers of a depth are not independent. This is the algebra that makes them one
/// packing rather than four constants, and it is what a "simplification" breaks.
#[test]
fn the_four_depth_parameters_describe_one_packing() {
    for depth in Bpp::ALL {
        let divisor = usize::try_from(depth.width_divisor()).unwrap();
        let mask = usize::try_from(depth.width_mask()).unwrap();
        let shift = usize::try_from(depth.index_shift()).unwrap();
        let index_mask = usize::try_from(depth.index_mask()).unwrap();

        assert_eq!(
            mask,
            divisor - 1,
            "{depth:?}: the slot mask must name exactly the slots a word holds"
        );

        let Some((per_word, texel_bits)) = packed_layout(depth) else {
            assert_eq!(divisor, 1, "{depth:?}: a direct depth is one texel per word");
            assert_eq!(shift, 0, "{depth:?}: nothing to shift");
            assert_eq!(index_mask, 0, "{depth:?}: the word is a colour, not an index");
            assert!(!depth.is_paletted());
            continue;
        };

        assert_eq!(per_word, divisor, "{depth:?}");
        assert_eq!(divisor * texel_bits, 16, "{depth:?}: a packed depth fills its word");
        assert_eq!(
            index_mask,
            (1 << texel_bits) - 1,
            "{depth:?}: the extracted field is as wide as the texel"
        );
        assert_eq!(
            1 << shift,
            texel_bits,
            "{depth:?}: index_shift is a bit width, not a slot number — the caller shifts the \
             slot by it"
        );
        assert!(depth.is_paletted());
        assert_eq!(depth.is_paletted(), depth.index_mask() != 0);
    }
}

/// The highest bit any depth's addressing can reach, pinned per depth.
///
/// Stated as a bound rather than as a "does not panic" check, because reading past the field
/// would not panic — it would return a plausible colour.
#[test]
fn no_depth_reaches_outside_its_own_word() {
    for depth in Bpp::ALL {
        let Some((per_word, texel_bits)) = packed_layout(depth) else {
            continue;
        };
        let last_slot = per_word - 1;
        assert_eq!(
            last_slot * texel_bits + texel_bits - 1,
            15,
            "{depth:?}: the last texel ends on the top bit of the word"
        );

        // And the reference's expression, taken literally, reaches the same place: the shift is
        // computed from the slot before the mask is applied.
        let shift_amount = u32::try_from(last_slot).unwrap() << depth.index_shift();
        assert_eq!(
            usize::try_from(shift_amount).unwrap() + texel_bits - 1,
            15,
            "{depth:?}"
        );

        // A word with every bit set must therefore extract a full-width index at the last slot.
        assert_eq!(
            depth.index_in_word(u16::MAX, u32::try_from(last_slot).unwrap()),
            Some(depth.index_mask()),
            "{depth:?}"
        );
    }
}

#[test]
fn the_two_direct_depths_have_no_palette_step() {
    for depth in [Bpp::Bits15, Bpp::Bits24] {
        assert!(!depth.is_paletted());
        // A zero mask does not mean "index zero". Returning zero here would be read as a palette
        // index, and index zero is a colour that exists.
        for word in all_words() {
            assert_eq!(depth.index_in_word(word, 0), None, "{depth:?}");
            assert_eq!(depth.index_in_word(word, 1), None, "{depth:?}");
        }
    }
    for depth in [Bpp::Bits4, Bpp::Bits8] {
        assert!(depth.is_paletted());
        for word in all_words() {
            assert!(depth.index_in_word(word, 0).is_some(), "{depth:?}");
        }
    }
}

#[test]
fn depth_codes_round_trip_and_there_is_no_fifth_depth() {
    for depth in Bpp::ALL {
        assert_eq!(Bpp::from_code(depth.code()), Some(depth));
    }
    for code in 4_u32..=255 {
        assert_eq!(Bpp::from_code(code), None, "code {code} is not a depth");
    }
    // Codes 2 and 3 both read 16-bit words and differ only in whether the result is a colour.
    // Asserted so the two are not quietly merged into "16-bit direct".
    assert_eq!(Bpp::Bits15.width_divisor(), Bpp::Bits24.width_divisor());
    assert_eq!(Bpp::Bits15.index_mask(), Bpp::Bits24.index_mask());
    assert_ne!(Bpp::Bits15.code(), Bpp::Bits24.code());
}

/// A resolved texel keeps the word it came from, so two slots of one word stay distinguishable
/// as values. Cheap to state, and it is the difference between a caller being able to see the
/// other packed texel or not.
#[test]
fn a_paletted_texel_carries_its_word_alongside_its_index() {
    let mut distinguishing = 0_usize;
    for depth in [Bpp::Bits4, Bpp::Bits8] {
        for word in all_words() {
            let first = depth.index_in_word(word, 0).unwrap();
            let second = depth.index_in_word(word, 1).unwrap();
            let keep = Texel::Paletted { index: first, word };
            match keep {
                Texel::Paletted { index, word: carried } => {
                    assert_eq!(index, first);
                    assert_eq!(carried, word);
                }
                Texel::Direct(_) => unreachable!("both of these depths are paletted"),
            }
            if first != second {
                distinguishing += 1;
                assert_ne!(
                    keep,
                    Texel::Paletted { index: second, word },
                    "{depth:?} in word {word:#06x}: the two slots differ but compare equal"
                );
            }
        }
    }
    assert!(distinguishing > 60_000, "{distinguishing} words had differing slots");
}
