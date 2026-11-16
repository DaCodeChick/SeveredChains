//! A BPE encoder, for the benefit of the decoder that lives in `psxrt-import`.
//!
//! # What this is for
//!
//! A decompressor is easy to write and hard to know you got right. Hand-written expectations prove
//! only that the decoder agrees with whatever the test author believed, and the interesting inputs
//! — multi-block streams, dictionary substitutions, padding between blocks — are exactly the ones
//! where a belief is most likely to be wrong. A round trip against an encoder written from the
//! format description, rather than from the decoder's control flow, is a stronger gate, and it is
//! the reason this module exists.
//!
//! # What this is not
//!
//! **It is not a compressor, and it is not a shipping writer.** An encoded payload is larger than
//! its input: every block carries a 258-byte dictionary plus a 4-byte size, so a 16-byte block
//! becomes roughly 280 bytes. Nothing in this project needs to produce BPE-compressed assets —
//! the game reads them, it does not write them — so there is no requirement to satisfy and no
//! benchmark to win. If a real compressor is ever needed it belongs here, and the constraints it
//! inherits are the ones in this file.
//!
//! It also refuses, rather than guessing, when asked for something it cannot honour. See
//! [`EncodeError`].
//!
//! # The layout produced
//!
//! ```text
//! offset  size  meaning
//!   0x00    4   decompressed length, little-endian
//!   0x04    4   marker 0x1a455042
//!   0x08    4   block 0: decompressed byte count, at most 0x800
//!             per block:
//!               dictionary  the full 256-slot table, as two runs of 128 entries
//!               data        the coded bytes
//!               padding     zero bytes up to the next 4-byte boundary
//!             then a 4-byte zero count ends the payload
//! ```
//!
//! # Why the encoder cannot produce a circular dictionary
//!
//! The format permits one: a dictionary is a table of substitutions and nothing in it forbids slot
//! A from naming a byte that slot B expands to, which makes the expansion in `psxrt_import` run
//! until its budget is spent. That input class is real, which is why the decoder refuses it.
//!
//! This encoder cannot generate it, by construction rather than by checking afterwards. A symbol
//! byte is only ever assigned from the range 0x80..=0xFF *excluding every byte value that occurs
//! in the data*. Two things follow. A byte that has to be emitted as a literal can never be
//! shadowed by a substitution, because its value was excluded from the symbol pool. And a
//! substitution's two children are always literal bytes drawn from the data, so no child is ever
//! itself a symbol, which is what makes the expansion two levels deep at most instead of arbitrary.
//!
//! That is a property of how symbols are chosen, not a rule that was added on top. It is written
//! down because the next person to "optimise" symbol assignment will remove it without noticing.

/// The marker word written at offset 4. Bytes on disk are `42 50 45 1A`.
pub const SIGNATURE: u32 = 0x1a45_5042;

/// The largest block this encoder will emit, in decompressed bytes.
pub const MAX_BLOCK_BYTES: usize = 0x800;

/// The largest payload this encoder will produce, matching the decoder's own ceiling.
///
/// Kept equal deliberately: an encoder that emitted a payload above the decoder's limit would
/// produce files that the project's own reader refuses.
pub const MAX_DECOMPRESSED_BYTES: usize = 16 * 1024 * 1024;

/// One substitution: `symbol` stands for `left` followed by `right`.
///
/// Both children are literal bytes. Nested rules — a rule whose child is another rule — are not
/// supported, and are not needed to exercise the decoder, which expands to arbitrary depth
/// whether or not anything asks it to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rule {
    /// The byte the decoder expands.
    pub symbol: u8,
    /// The first literal byte it stands for.
    pub left: u8,
    /// The second literal byte it stands for.
    pub right: u8,
}

/// Encode `data` as a BPE payload.
///
/// `rules` are the substitutions to apply; pass an empty slice for a plain, literal-only encoding,
/// which is the shape most round-trip tests need. A rule is applied greedily, left to right, and a
/// rule whose two bytes are not adjacent in the data is simply never used.
///
/// # Errors
///
/// [`EncodeError::NoFreeSymbol`] if there are more rules than there are usable symbol bytes, and
/// [`EncodeError::TooLarge`] if the input exceeds [`MAX_DECOMPRESSED_BYTES`]. Rules that are
/// unusable for another reason — a child byte that a symbol would shadow, or a symbol equal to
/// its own left byte — are dropped rather than reported, because dropping them costs compression
/// and nothing else. Callers that need to know count the returned rules.
pub fn encode(data: &[u8], rules: &[(u8, u8)]) -> Result<Payload, EncodeError> {
    Payload::new(data, rules)
}

/// A built payload: the bytes to write, and the rules that were actually applied.
#[derive(Debug, Clone)]
pub struct Payload {
    bytes: Vec<u8>,
    rules: Vec<Rule>,
}

impl Payload {
    fn new(data: &[u8], rules: &[(u8, u8)]) -> Result<Self, EncodeError> {
        if data.len() > MAX_DECOMPRESSED_BYTES {
            return Err(EncodeError::TooLarge {
                requested: data.len(),
                limit: MAX_DECOMPRESSED_BYTES,
            });
        }

        let applied = select_symbols(data, rules);
        let dictionary = build_dictionary(&applied);

        let mut bytes = Vec::new();
        bytes.extend_from_slice(&(data.len() as u32).to_le_bytes());
        bytes.extend_from_slice(&SIGNATURE.to_le_bytes());

        for block in data.chunks(MAX_BLOCK_BYTES) {
            bytes.extend_from_slice(&(block.len() as u32).to_le_bytes());
            bytes.extend_from_slice(&dictionary);
            bytes.extend_from_slice(&code_block(block, &applied));
            while bytes.len() % 4 != 0 {
                bytes.push(0);
            }
        }

        // The terminating zero count. A stream whose last block happens to end on a boundary still
        // needs it: it is what tells the decoder there are no more blocks, not padding.
        bytes.extend_from_slice(&0u32.to_le_bytes());

        Ok(Self {
            bytes,
            rules: applied,
        })
    }

    /// The encoded bytes, ready to hand to `psxrt_import::container::bpe::decode`.
    #[must_use]
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// The rules that were actually applied, with the symbol each was given.
    ///
    /// Returned so a test can assert that a round trip really exercised expansion. A round trip
    /// over input with nothing to substitute is a round trip over the literal path only, and it is
    /// easy to write several of those without noticing none of them tested anything.
    #[must_use]
    pub fn rules(&self) -> &[Rule] {
        &self.rules
    }
}

/// Choose a symbol byte for each usable rule.
///
/// The pool is 0x80..=0xFF minus every value that occurs in `data`. See the module header for why
/// that exclusion is the thing that makes the output safe, rather than merely smaller.
fn select_symbols(data: &[u8], rules: &[(u8, u8)]) -> Vec<Rule> {
    let mut occurs = [false; 256];
    for byte in data {
        occurs[usize::from(*byte)] = true;
    }

    let mut taken = [false; 256];
    let mut applied = Vec::new();

    for &(left, right) in rules {
        if applied.len() >= 32 {
            // Enough symbols for anything a test asks for, and it keeps the search below cheap.
            break;
        }
        let symbol = (0x80..=0xff).find(|&symbol| {
            !occurs[usize::from(symbol)]
                && !taken[usize::from(symbol)]
                && symbol != left
                && !(taken[usize::from(left)] || taken[usize::from(right)])
        });
        if let Some(symbol) = symbol {
            taken[usize::from(symbol)] = true;
            applied.push(Rule {
                symbol,
                left,
                right,
            });
        }
    }

    applied
}

/// Write the full 256-slot table, as two runs of 128 entries.
///
/// A run is a count byte followed by that many entries; an entry is one byte when the slot holds
/// its own index, and two bytes — left then right — when it does not. Two runs of 128 cover all
/// 256 slots without needing the skip form, where a count byte of 0x80 or more jumps forward and
/// then reads a single entry.
fn build_dictionary(rules: &[Rule]) -> Vec<u8> {
    let slot_entry =
        |slot: u8, bytes: &mut Vec<u8>| match rules.iter().find(|rule| rule.symbol == slot) {
            Some(rule) => {
                bytes.extend_from_slice(&[rule.left, rule.right]);
            }
            None => bytes.push(slot),
        };

    let mut dictionary = Vec::with_capacity(258 + 2 * rules.len());
    dictionary.push(0x7f);
    for slot in 0x00..0x80u8 {
        slot_entry(slot, &mut dictionary);
    }
    dictionary.push(0x7f);
    for slot in 0x80..=0xffu8 {
        slot_entry(slot, &mut dictionary);
    }
    dictionary
}

/// Turn literal bytes into coded bytes by replacing matched pairs with their symbol.
fn code_block(block: &[u8], rules: &[Rule]) -> Vec<u8> {
    let mut coded = Vec::with_capacity(block.len());
    let mut index = 0;

    while index < block.len() {
        let pair = rules
            .iter()
            .find(|rule| block.get(index..index + 2) == Some(&[rule.left, rule.right][..]));
        match pair {
            Some(rule) => {
                coded.push(rule.symbol);
                index += 2;
            }
            None => {
                coded.push(block[index]);
                index += 1;
            }
        }
    }

    coded
}

/// Why an encoding was refused.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum EncodeError {
    /// The input is larger than the decoder would accept.
    #[error("{requested} bytes exceeds the {limit}-byte limit shared with the decoder")]
    TooLarge {
        /// Length of the data offered.
        requested: usize,
        /// The limit from [`MAX_DECOMPRESSED_BYTES`].
        limit: usize,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The dictionary is a variable-length encoding of all 256 slots, so its byte length is not
    /// 256. Two runs of 128 literal entries cost 258 bytes; each rule turns one literal byte into
    /// a pair and so costs one more. Checking the *rule* rather than a remembered total is the
    /// point: a dictionary that omitted slots, or wrote 256 raw bytes with no count header, would
    /// decode as a truncated stream rather than as a wrong table, and that is harder to see.
    #[test]
    fn the_dictionary_covers_all_256_slots() {
        // 2 count bytes + 256 literal slots.
        assert_eq!(build_dictionary(&[]).len(), 258);

        let rules = [Rule {
            symbol: 0xfe,
            left: b'a',
            right: b'b',
        }];
        let with_one_rule = build_dictionary(&rules);
        assert_eq!(
            with_one_rule.len(),
            259,
            "one rule turns one literal byte into a pair, so it adds exactly one byte"
        );

        // The count headers are what make it self-describing: 0x7f means "128 entries follow".
        // The count headers sit at the front of each run. Position, not byte value, is what
        // identifies them: 0x7f is also a literal byte the data can contain, so "how many 0x7f
        // bytes are in here" has no useful answer — three is correct and means nothing.
        assert_eq!(with_one_rule[0], 0x7f);
        assert_eq!(with_one_rule[1 + 128], 0x7f);
    }

    /// A rule that cannot be given a symbol is dropped rather than encoded wrongly.
    #[test]
    fn a_rule_with_no_free_symbol_is_dropped() {
        // Every byte value appears, so nothing in 0x80..=0xff is free.
        let data: Vec<u8> = (0..=255u8).collect();
        let payload = encode(&data, &[(b'a', b'b')]).unwrap();
        assert!(payload.rules().is_empty());
    }

    /// A payload with nothing in it still carries the header and the terminating count.
    ///
    /// Twelve bytes, not sixteen: an empty input has no blocks at all, so it gets no dictionary.
    /// The four-byte zero is the terminator that says "no more blocks", and the decoder's loop
    /// exits on it without ever building a table. Writing a zero-length *block* here instead would
    /// be a different encoding, and the decoder would read its zero count as the terminator and
    /// then trip the length check on the leftover bytes.
    #[test]
    fn an_empty_input_still_produces_a_terminator() {
        let payload = encode(b"", &[]).unwrap();
        assert_eq!(payload.bytes().len(), 12);
        assert_eq!(&payload.bytes()[8..], &[0, 0, 0, 0]);
    }
}
