//! Decompression: one codec, plus the case where there is no codec.
//!
//! # The two paths, and why there are only two
//!
//! A payload is either BPE-compressed or it is not, and "is not" is a *successful* read that
//! hands back the same bytes. That is the whole of decompression in the reference, and it is
//! what this module implements.
//!
//! There is **no LZSS path and no mode byte**, and that absence is a finding rather than a gap.
//! An earlier draft of the roadmap specified three data-driven modes ("0 = raw, 1 = BPE, other →
//! error") and a raw-LZSS variant beside them. Neither exists. Checked for, against the reference
//! at pin `d47ac8637`:
//!
//! * Decompression is gated on one test — `readInt(0x4) == 0x1a455042`, the four bytes `42 50 45
//!   1A`, which spell `BPE` followed by 0x1A. No byte anywhere in that path is read as a mode
//!   selector.
//! * `LZSS`, `LZ77`, `lzss`, and any mode or compression-kind identifier return nothing in the
//!   unpacker's read path. The only compression the reference contains is this BPE codec, and its
//!   one sibling is a *separate* tool that re-encodes sequences by a different scheme and is not
//!   reachable from here (`CL-AST-12`, and `LZ-1` in `research/04-unresolved-questions.md`).
//!
//! Building the three-mode version would have meant writing a dispatcher for two branches that
//! have no input, and every branch a reader cannot rule out is a branch they have to test. If
//! real bytes later turn up with a second scheme, this is where it belongs, and [`Codec`] is
//! where the decision would be recorded — but the decision is not made in anticipation of an input
//! nobody has.
//!
//! # Where the decision is made
//!
//! The reference decides *before* it knows what the payload is. The gate over the decompression
//! step is a discriminator that matches on path shape and on the leading words of the data, and it
//! runs once per node, ahead of the transformers that turn a payload into a model or a texture. So:
//! container and codec questions are answered by position and marker, and content identity is
//! established afterwards, from the decoded bytes. A reader that tried to decide "is this a TMD"
//! before decompressing would be reading compressed bytes as if they were a header.
//!
//! One consequence worth naming: the marker lives at offset 4, not offset 0, and offset 0 holds the
//! decompressed length. A payload that begins with what looks like a magic number is not
//! necessarily compressed.
//!
//! # The format, in the order the reader meets it
//!
//! ```text
//! offset  size  meaning
//!   0x00    4   decompressed length, little-endian — the size of the output
//!   0x04    4   marker 0x1a455042 → BPE; anything else → not compressed
//!   0x08    4   first block: decompressed byte count, at most 0x800
//!             per block, in this order:
//!               dictionary  up to 256 substitution slots (see Dictionary::build)
//!               data        the coded bytes for this block
//!               padding     zero bytes up to the next 4-byte boundary
//!             then the next block's count; a count of 0 ends the payload
//! ```
//!
//! Block boundaries are 4-byte aligned and the alignment is computed on the *absolute* offset,
//! which is why the padding exists: the codec pads so that the following count lands on a word
//! boundary. A reader that skips the padding reads every block after the first from the wrong
//! offset.

use crate::container::read_u32_le;
use crate::error::ImportError;

/// The marker word at offset 4 that says "this payload is BPE-compressed".
///
/// Bytes on disk are `42 50 45 1A`. Read little-endian that is this value, which is how the
/// reference compares it — it compares words, not the string, so do not "simplify" this into a
/// byte-string prefix test without re-deriving the byte order.
pub const SIGNATURE: u32 = 0x1a45_5042;

/// The largest decompressed size this reader will produce.
///
/// A native guard, not a format limit. The reference sizes its output from the first word of the
/// payload without questioning it, so a corrupt header can ask for anything up to 4 GiB. No game
/// file approaches this bound; the point is that the *reader* cannot be steered into an allocation
/// by the bytes it is reading.
pub const MAX_DECOMPRESSED_BYTES: usize = 16 * 1024 * 1024;

/// How many expansions one block may perform before it is called circular.
///
/// A native guard. See [`ImportError::NonTerminatingDictionary`].
const EXPANSION_BUDGET: usize = 1 << 20;

/// The codec's per-block ceiling, in decompressed bytes.
const MAX_BLOCK_BYTES: u32 = 0x800;

/// Number of substitution slots in a block dictionary.
const DICTIONARY_SLOTS: usize = 0x100;

/// Length of the fixed header: declared-size word plus marker word.
const HEADER_BYTES: usize = 8;

/// What a payload says it is, before anything is decoded.
///
/// Two variants, and they are the only two. A third would need an input.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Codec {
    /// The marker is present: the payload is a BPE stream and must be expanded.
    Bpe,
    /// The marker is absent: the payload *is* the data.
    ///
    /// This is not "unknown" and not a failure. Uncompressed files are ordinary in the original
    /// layout, and a reader that treats an absent marker as an error drops them.
    Passthrough,
}

/// Classify a payload.
///
/// The rule is the reference's: at least 8 bytes long, and the word at offset 4 is the marker.
/// Length is part of the test — a 4-byte payload cannot carry the marker, and reading past the end
/// of the payload to look for it is the over-read that `planning/08` §D-3 refuses to inherit.
#[must_use]
pub fn codec_for(payload: &[u8]) -> Codec {
    if payload.len() >= HEADER_BYTES && read_u32_le(payload, 4) == Some(SIGNATURE) {
        Codec::Bpe
    } else {
        Codec::Passthrough
    }
}

/// Expand a payload, or copy it if it was never compressed.
///
/// The returned bytes are the file's contents, ready for whatever reads them next. Whether any of
/// them came from the region just past the payload's end is the question `planning/08` §D-3 closes
/// for this module: this function reads *inside* `payload` only, and a marker whose declared length
/// cannot be satisfied is an error rather than a shorter answer.
pub fn decode(payload: &[u8]) -> Result<Vec<u8>, ImportError> {
    match codec_for(payload) {
        Codec::Passthrough => Ok(payload.to_vec()),
        Codec::Bpe => decode_bpe(payload),
    }
}

/// The BPE path.
///
/// The shape of this function follows the reference's `Unpacker.decompress`. The points where it
/// deliberately differ are listed under "Where this stops copying" at the foot of this file rather
/// than buried in the control flow.
fn decode_bpe(payload: &[u8]) -> Result<Vec<u8>, ImportError> {
    // Offset 0 is the length of the *output*, not of the input. Read it as unsigned: the reference
    // reads it as a Java int, so a value with the top bit set is negative there and the allocation
    // throws. Widening keeps that case visible as a number instead of as a sign.
    let declared = read_u32_le(payload, 0).ok_or(ImportError::Truncated {
        needed: 4,
        available: payload.len(),
    })?;

    if declared as usize > MAX_DECOMPRESSED_BYTES {
        return Err(ImportError::ImplausibleSize {
            declared: u64::from(declared),
        });
    }
    let declared = declared as usize;

    let mut output = vec![0u8; declared];
    let mut written = 0usize;
    let mut dictionary = Dictionary::default();

    // The first block count sits at 0x08, past the header. A stream with no blocks is legal and
    // means "nothing to expand"; the count of zero is what ends the loop.
    let mut cursor = HEADER_BYTES;
    let mut block_bytes = read_u32_le(payload, cursor).ok_or(ImportError::Truncated {
        needed: cursor + 4,
        available: payload.len(),
    })?;
    cursor += 4;

    while block_bytes != 0 {
        if block_bytes > MAX_BLOCK_BYTES {
            return Err(ImportError::OversizedBlock {
                declared: block_bytes,
                limit: MAX_BLOCK_BYTES,
            });
        }
        let mut remaining = block_bytes as usize;

        dictionary.build(payload, &mut cursor)?;

        // Each round reads one coded byte and unwinds it. The unwind list is a stack: a rule
        // pushes its right byte then its left byte so the left byte is unwound first, which is the
        // order the original bytes went in.
        let mut unwind: Vec<u8> = Vec::new();
        let mut expansions = 0usize;

        while remaining > 0 {
            unwind.clear();
            let coded = *payload.get(cursor).ok_or(ImportError::Truncated {
                needed: cursor + 1,
                available: payload.len(),
            })?;
            cursor += 1;
            unwind.push(coded);

            while let Some(symbol) = unwind.pop() {
                expansions += 1;
                if expansions > EXPANSION_BUDGET {
                    return Err(ImportError::NonTerminatingDictionary {
                        budget: EXPANSION_BUDGET,
                    });
                }

                if dictionary.is_literal(symbol) {
                    // A symbol that maps to itself is a byte, not a rule. Output is written only
                    // here, which is what makes `written` a truthful count of produced bytes.
                    if written == declared {
                        return Err(ImportError::LengthMismatch {
                            declared,
                            written: written + 1,
                        });
                    }
                    output[written] = symbol;
                    written += 1;
                    remaining -= 1;
                } else {
                    let slot = usize::from(symbol);
                    unwind.push(dictionary.right[slot]);
                    unwind.push(dictionary.left[slot]);
                }
            }
        }

        // Word-align for the next count, on the absolute offset, as in the reference. The padding
        // belongs to the payload, so a producer that leaves it out desynchronises every block
        // after the first — which is why the round-trip test compares whole payloads.
        cursor = align_up(cursor);
        block_bytes = read_u32_le(payload, cursor).ok_or(ImportError::Truncated {
            needed: cursor + 4,
            available: payload.len(),
        })?;
        cursor += 4;
    }

    if written != declared {
        return Err(ImportError::LengthMismatch { declared, written });
    }

    Ok(output)
}

/// Round up to the next 4-byte boundary.
fn align_up(offset: usize) -> usize {
    offset + (4 - offset % 4) % 4
}

/// One block's substitution table.
///
/// Two parallel arrays, which is how the reference stores it and is also the honest shape: a slot
/// is either a literal byte or a pair, and the pair's two halves are what get pushed onto the
/// unwind stack. `left[i] == i` means "slot `i` is a literal", so the two tables are not
/// independent — but `right` still has to be cleared, because a slot that stops being a pair must
/// not keep the previous block's partner.
#[derive(Clone)]
struct Dictionary {
    left: [u8; DICTIONARY_SLOTS],
    right: [u8; DICTIONARY_SLOTS],
}

impl Default for Dictionary {
    fn default() -> Self {
        Self {
            left: [0u8; DICTIONARY_SLOTS],
            right: [0u8; DICTIONARY_SLOTS],
        }
    }
}

impl Dictionary {
    /// Reset to "every byte is itself", then read this block's table.
    ///
    /// The dictionary is rebuilt per block, so a payload's blocks are independent: a table read for
    /// block 1 has no effect on block 2. That is the property that makes block-level extraction
    /// possible later, and it is why the reset happens inside the block loop rather than outside it.
    fn build(&mut self, payload: &[u8], cursor: &mut usize) -> Result<(), ImportError> {
        for (slot, cell) in self.left.iter_mut().enumerate() {
            *cell = slot as u8;
        }
        self.right.fill(0);

        let mut key = 0usize;
        while key < DICTIONARY_SLOTS {
            let header = payload
                .get(*cursor)
                .copied()
                .ok_or(ImportError::Truncated {
                    needed: *cursor + 1,
                    available: payload.len(),
                })?;
            *cursor += 1;

            // A header of 0x80 or more is not a count. It moves `key` forward by
            // `header - 0x7f`, and then exactly one entry is read at the new position. Below 0x80
            // it is a count, and `header + 1` entries are read at the current position.
            //
            // The consequence, which is easy to miss and is the reason this branch is spelled out:
            // a skip does not skip on its own. It skips *and then reads*. There is no encoding for
            // "jump forward and read nothing", so a producer that uses a skip spends one
            // dictionary entry on whatever lands at its destination.
            let entries = if header >= 0x80 {
                key = key + usize::from(header) - 0x7f;
                1
            } else {
                usize::from(header) + 1
            };

            for _ in 0..entries {
                if key >= DICTIONARY_SLOTS {
                    // Only reachable through a header that overshoots: a large skip, or a count
                    // started near the top of the table. The reference indexes out of its 256-byte
                    // array here and dies with an ArrayIndexOutOfBoundsException.
                    return Err(ImportError::MalformedDictionary { key });
                }

                let left = payload
                    .get(*cursor)
                    .copied()
                    .ok_or(ImportError::Truncated {
                        needed: *cursor + 1,
                        available: payload.len(),
                    })?;
                *cursor += 1;
                self.left[key] = left;

                if usize::from(left) != key {
                    // Not a literal, so this slot is a pair and its partner byte follows. The test
                    // is against the *index*, not against a flag: the table is its own tag byte,
                    // and that is only unambiguous because a literal's partner is never read.
                    let right = payload
                        .get(*cursor)
                        .copied()
                        .ok_or(ImportError::Truncated {
                            needed: *cursor + 1,
                            available: payload.len(),
                        })?;
                    *cursor += 1;
                    self.right[key] = right;
                }

                key += 1;
            }
        }

        Ok(())
    }

    /// Whether `symbol` stands for itself.
    fn is_literal(&self, symbol: u8) -> bool {
        symbol == self.left[usize::from(symbol)]
    }
}

// # Where this stops copying
//
// Three places, all deliberate, all reached only by input that a real file does not produce.
//
// 1. **A declared length is refused, not attempted.** The reference allocates from it directly.
// 2. **A dictionary that will not terminate is refused.** The reference has no bound and would
//    grow its working list until the process died. A cycle is constructible by hand — the table
//    forbids no `slot → slot' → slot` pair — so this is a real input class, not a hypothetical.
// 3. **A length disagreement is refused.** The reference allocates the declared length and fills
//    it, so a short decode silently yields trailing zero bytes and a long one indexes past the
//    end. Both are reported here, because "decoded to a different size than it claimed" is
//    information the caller needs and a truncated answer destroys it.
//
// None of the three changes the result for a payload that a correct producer wrote.

#[cfg(test)]
mod tests {
    use super::*;

    /// A payload with the marker present is BPE, whatever else it says.
    #[test]
    fn classifies_by_the_word_at_offset_four() {
        let compressed = [
            0x00, 0x00, 0x00, 0x00, 0x42, 0x50, 0x45, 0x1a, 0x00, 0x00, 0x00, 0x00,
        ];
        assert_eq!(codec_for(&compressed), Codec::Bpe);

        // The same word, sitting at offset 0 where a magic number would normally be. Position is
        // what the reference tests, so position is what this tests.
        let misaligned = [0x42, 0x50, 0x45, 0x1a, 0x00, 0x00, 0x00, 0x00];
        assert_eq!(codec_for(&misaligned), Codec::Passthrough);
    }

    /// Too short to carry a marker is passthrough, and the bytes survive intact.
    #[test]
    fn short_payloads_pass_through_rather_than_being_read_past_their_end() {
        let long = [0x42u8, 0x50, 0x45, 0x1a, 0x42, 0x50, 0x45, 0x1a];
        for len in 0..HEADER_BYTES {
            let short = &long[..len];
            assert_eq!(codec_for(short), Codec::Passthrough);
            assert_eq!(decode(short).unwrap(), short.to_vec());
        }
    }

    /// A marker word that is merely *similar* is not a marker.
    #[test]
    fn a_payload_that_only_looks_compressed_is_copied_verbatim() {
        // The signature bytes at offset 0, and the same four bytes reversed at offset 4.
        let payload = [0x42, 0x50, 0x45, 0x1a, 0x1a, 0x50, 0x45, 0x42];
        assert_eq!(codec_for(&payload), Codec::Passthrough);
        assert_eq!(decode(&payload).unwrap(), payload.to_vec());
    }

    /// The smallest legal BPE stream: marker, zero declared bytes, one zero block count.
    #[test]
    fn an_empty_stream_decodes_to_nothing() {
        let payload = [
            0x00, 0x00, 0x00, 0x00, 0x42, 0x50, 0x45, 0x1a, 0x00, 0x00, 0x00, 0x00,
        ];
        assert_eq!(decode(&payload).unwrap(), Vec::new());
    }

    /// A declared length that cannot be honoured is refused before it is attempted.
    #[test]
    fn an_implausible_declared_length_is_refused_before_it_is_allocated() {
        let mut payload = vec![0xff, 0xff, 0xff, 0xff];
        payload.extend_from_slice(&[0x42, 0x50, 0x45, 0x1a, 0x00, 0x00, 0x00, 0x00]);
        assert!(matches!(
            decode(&payload),
            Err(ImportError::ImplausibleSize {
                declared: 4_294_967_295
            })
        ));
    }

    /// The per-block ceiling is inclusive: 0x800 is a block, anything above is a bad header.
    #[test]
    fn a_block_above_the_ceiling_is_refused() {
        let mut payload = vec![0x04, 0x00, 0x00, 0x00];
        payload.extend_from_slice(&SIGNATURE.to_le_bytes());
        payload.extend_from_slice(&0x10_0000u32.to_le_bytes());
        assert!(matches!(
            decode(&payload),
            Err(ImportError::OversizedBlock {
                declared: 0x10_0000,
                limit: 0x800
            })
        ));
    }

    /// A dictionary run that overshoots its own table is a refusal, not an out-of-bounds write.
    #[test]
    fn a_dictionary_run_past_the_table_is_refused() {
        // Skip to slot 0x80 and consume one literal there, then ask for 128 more slots from 0x81.
        // 0x81 + 128 runs off the end of a 256-slot table; the reference walks out of its array.
        let mut payload = vec![0x04, 0x00, 0x00, 0x00];
        payload.extend_from_slice(&SIGNATURE.to_le_bytes());
        payload.extend_from_slice(&0x04u32.to_le_bytes());
        payload.push(0xff); // skip: key 0 → 0x80, then read one entry
        payload.push(0x80); // that entry: slot 0x80 holds 0x80, a literal
        payload.push(0x7f); // 128 entries from slot 0x81 — fourteen too many
        payload.extend(0..=0xffu8);
        assert!(matches!(
            decode(&payload),
            Err(ImportError::MalformedDictionary { key }) if key == DICTIONARY_SLOTS
        ));
    }

    /// A table can be written so a symbol expands to itself. That is a cycle, and the reader has
    /// to notice rather than follow it.
    #[test]
    fn a_circular_dictionary_is_refused_instead_of_being_followed_forever() {
        // Slot 0x90 → [0x91, 0x00] and slot 0x91 → [0x90, 0x00]. The coded byte expands forever.
        let mut dictionary = Vec::new();
        for range in [0x00u8..=0x7f, 0x80..=0xff] {
            dictionary.push(0x7f);
            for key in range {
                match key {
                    0x90 => dictionary.extend_from_slice(&[0x91, 0x00]),
                    0x91 => dictionary.extend_from_slice(&[0x90, 0x00]),
                    _ => dictionary.push(key),
                }
            }
        }

        let mut payload = vec![0x04, 0x00, 0x00, 0x00];
        payload.extend_from_slice(&SIGNATURE.to_le_bytes());
        payload.extend_from_slice(&0x04u32.to_le_bytes());
        payload.extend_from_slice(&dictionary);
        payload.push(0x90);
        assert!(matches!(
            decode(&payload),
            Err(ImportError::NonTerminatingDictionary { .. })
        ));
    }

    /// A stream that decodes to less than it declared is damage, not a smaller success.
    #[test]
    fn a_stream_that_decodes_short_is_refused() {
        // Declares 8 bytes but contains one block of 4. Every byte inside the payload is
        // well-formed, so a reader that only checks "did the blocks parse" would report success
        // with four zero bytes appended. This is the case that guard exists for.
        let mut payload = vec![0x08, 0x00, 0x00, 0x00];
        payload.extend_from_slice(&SIGNATURE.to_le_bytes());
        payload.extend_from_slice(&0x04u32.to_le_bytes());
        payload.extend_from_slice(&literal_dictionary());
        payload.extend_from_slice(b"ABCD");
        while payload.len() % 4 != 0 {
            payload.push(0x00);
        }
        payload.extend_from_slice(&0u32.to_le_bytes());
        assert!(matches!(
            decode(&payload),
            Err(ImportError::LengthMismatch {
                declared: 8,
                written: 4
            })
        ));
    }

    /// Reading stops at the payload. A header that reaches past the end meets a refusal.
    #[test]
    fn a_truncated_stream_is_refused_at_the_offset_that_broke() {
        let payload = [0x04, 0x00, 0x00, 0x00, 0x42, 0x50, 0x45, 0x1a, 0x04, 0x00];
        assert!(matches!(
            decode(&payload),
            Err(ImportError::Truncated {
                needed: 12,
                available: 10
            })
        ));
    }

    /// A whole literal-only stream, built to the layout in the header comment, decodes exactly.
    ///
    /// With every slot holding its own index, each coded byte *is* its own output byte, so the
    /// payload doubles as its own expectation — which is what makes it useful as a decoder pin. The
    /// round trip against a real encoder is `tests/bpe_round_trip.rs`; this builds streams directly
    /// because a correct producer cannot be asked to emit deliberate damage.
    #[test]
    fn a_hand_built_stream_decodes_block_by_block() {
        let mut payload = vec![0x04, 0x00, 0x00, 0x00];
        payload.extend_from_slice(&SIGNATURE.to_le_bytes());
        for block in [b"AB".as_slice(), b"CD".as_slice()] {
            payload.extend_from_slice(&(block.len() as u32).to_le_bytes());
            payload.extend_from_slice(&literal_dictionary());
            payload.extend_from_slice(block);
            while payload.len() % 4 != 0 {
                payload.push(0x00);
            }
        }
        payload.extend_from_slice(&0u32.to_le_bytes());

        assert_eq!(decode(&payload).unwrap(), b"ABCD".to_vec());
    }

    /// A dictionary of pure literals: two runs of 128, each self-describing its own length.
    fn literal_dictionary() -> Vec<u8> {
        let mut dictionary = vec![0x7fu8];
        dictionary.extend(0x00..0x80u8);
        dictionary.push(0x7f);
        dictionary.extend(0x80..=0xffu8);
        dictionary
    }
}
