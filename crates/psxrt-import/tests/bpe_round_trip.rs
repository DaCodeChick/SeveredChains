//! Round trip for the BPE codec: `psxrt-compat` writes a payload, `psxrt-import` reads it back.
//!
//! # Why this crosses a crate boundary
//!
//! The encoder is a test oracle, not a shipping writer — see its module header. Its whole job is to
//! be an independent witness that the decoder is right, and that only holds while the two are
//! written against the format rather than against each other. `psxrt-import` depends on
//! `psxrt-compat` under `[dev-dependencies]` and never in production; a `dev-dependency` cycle back
//! the other way is rejected by the build, so the encoder cannot quietly start calling the decoder.
//!
//! # What a green run here does *not* prove
//!
//! It proves the decoder inverts the encoder. It does not prove either matches the original tool
//! that produced the real game data, and it is worth not confusing those: no original BPE-compressed
//! asset has been checked against this decoder yet. That is item P-02 in `research/04` and it needs
//! a disc image. Until it runs, the honest claim is "self-consistent and derived from the reference's
//! `Unpacker.decompress`", which is what the module doc claims too.
//!
//! The properties below are run over generated input. TD-19 asks for a named, pinned PRNG rather
//! than whatever the `rand` crate happens to default to, and proptest's default engine is
//! `rand_chacha`'s ChaCha with a fixed start seed and a persisted failure file, so that is what
//! these use.

use proptest::collection::vec;
use proptest::prelude::*;
use psxrt_compat::container::bpe;
use psxrt_import::ImportError;
use psxrt_import::container::bpe as codec;

/// A payload that decodes back to what went in, and a marker that says so.
#[track_caller]
fn assert_round_trips(data: &[u8], rules: &[(u8, u8)]) {
    let payload = bpe::encode(data, rules).expect("encoder refused a legal input");

    assert_eq!(
        codec::codec_for(payload.bytes()),
        codec::Codec::Bpe,
        "an encoded payload must present the marker, or the reader will hand back compressed bytes"
    );

    match codec::decode(payload.bytes()) {
        Ok(decoded) => assert_eq!(
            decoded,
            data,
            "{} bytes in, {} bytes back ({} rules)",
            data.len(),
            decoded.len(),
            payload.rules().len()
        ),
        Err(error) => panic!(
            "{} bytes in, error back: {error} ({} rules)",
            data.len(),
            payload.rules().len()
        ),
    }
}

/// The empty case has to work, because a container entry is allowed to be zero length and the
/// decoder has to distinguish that from damage.
#[test]
fn empty_input_round_trips() {
    assert_round_trips(b"", &[]);
}

/// Every block boundary, and the bytes either side of it. A single-block payload and a
/// multi-block payload differ in one 4-byte count and a whole dictionary, which is exactly
/// where an off-by-something hides.
#[test]
fn every_block_boundary_round_trips() {
    let data: Vec<u8> = (0..4104u32).map(|i| (i % 251) as u8).collect();

    for length in [
        0, 1, 2, 3, 4, 0x7ff, 0x800, 0x801, 0xfff, 0x1000, 0x1001, 0x1002,
    ] {
        assert_round_trips(&data[..length], &[]);
    }
}

/// A payload with two blocks really has two blocks, and the second one is read from the right
/// place. That is only observable if the first block's padding is present and computed on the
/// absolute offset, which is the detail most likely to be dropped and still survive a test that
/// only ever encodes one block.
#[test]
fn a_payload_spanning_blocks_pads_on_the_absolute_offset() {
    let data = vec![0x5au8; 0x801];
    let payload = bpe::encode(&data, &[]).unwrap();
    let bytes = payload.bytes();

    // First block: 4 count + 258 dictionary + 0x800 coded, starting at offset 8. The padding that
    // follows is what puts the *next* count on a word boundary.
    let first_block_end: usize = 8 + 4 + 258 + 0x800;
    let second_count = first_block_end.div_ceil(4) * 4;

    assert_eq!(
        first_block_end % 4,
        2,
        "this test only means something if the first block does not already end aligned"
    );
    assert_eq!(
        &bytes[second_count..second_count + 4],
        &[1, 0, 0, 0],
        "the second block's count is not where the padding rule puts it"
    );

    // And the decoder lands there too: 0x801 bytes in, 0x801 bytes back.
    assert_eq!(codec::decode(bytes).unwrap(), data);
}

/// Rules that find nothing to substitute are not a test of the substitution path. This one builds
/// input the codec *must* act on, and fails if it did not.
#[test]
fn a_rule_that_applies_is_actually_applied() {
    let data = b"ABABABABABABABABABABABABABABABAB".to_vec();
    let payload = bpe::encode(&data, &[(b'A', b'B')]).unwrap();

    assert_eq!(
        payload.rules().len(),
        1,
        "the rule was dropped, so this proved nothing"
    );

    // 32 bytes down to 16 coded bytes, plus the dictionary. If the rule had been skipped the
    // payload would be 32 bytes of data instead of 16.
    let literal = bpe::encode(&data, &[]).unwrap();
    assert!(
        payload.bytes().len() < literal.bytes().len(),
        "encoded {:#?} is not smaller than literal",
        payload.bytes().len()
    );

    assert_round_trips(&data, &[(b'A', b'B')]);
}

/// Overlapping runs: `AAA` with a rule on `AA`. The first two bytes are consumed as a pair and the
/// third is a literal, which is the case where a decoder that scans pairs wrong still looks right
/// on even-length input.
#[test]
fn a_run_of_three_with_a_two_byte_rule_round_trips() {
    for length in 2..=12 {
        let data = vec![b'a'; length];
        assert_round_trips(&data, &[(b'a', b'a')]);
    }
}

/// Every byte value present, so the encoder has nowhere to put a symbol. It must fall back to a
/// literal encoding that still decodes, rather than emitting a dictionary it cannot honour.
#[test]
fn a_payload_with_no_free_symbol_byte_still_round_trips() {
    let data: Vec<u8> = (0..=255u16).map(|i| i as u8).cycle().take(2048).collect();
    assert_round_trips(&data, &[(0x00, 0x01), (0xfe, 0xff), (b'q', b'q')]);
}

/// A rule naming bytes that never occur adjacent is simply unused. The payload is then a plain
/// literal encoding, and it still has to decode.
#[test]
fn a_rule_with_nothing_to_match_is_harmless() {
    assert_round_trips(b"the quick brown fox", &[(0x00, 0xff)]);
}

/// Payloads that cross the 0x800 boundary *and* substitute, which is the combination none of the
/// single-purpose tests above reach.
#[test]
fn substitution_across_block_boundaries_round_trips() {
    let data: Vec<u8> = b"XY".repeat(2000);
    assert_round_trips(&data, &[(b'X', b'Y')]);

    let mut data = vec![0u8; 3072];
    for (i, byte) in data.iter_mut().enumerate() {
        *byte = if i % 3 == 0 { 0x11 } else { 0x22 };
    }
    assert_round_trips(&data, &[(0x11, 0x22), (0x22, 0x11)]);
}

/// The declared length at offset 0 is the size of the *output*. Corrupting it is the cheapest way
/// to steer a reader into a wrong allocation, so both directions are refused rather than absorbed.
#[test]
fn a_wrong_declared_length_is_refused_in_both_directions() {
    let data = vec![0x42u8; 0x900];
    let payload = bpe::encode(&data, &[]).unwrap();
    let bytes = payload.bytes();

    for tamper in [0x00u32, 1, 0x1000_0000, 0xffff_ffff] {
        let mut damaged = bytes.to_vec();
        damaged[..4].copy_from_slice(&tamper.to_le_bytes());
        assert_eq!(
            codec::codec_for(&damaged),
            codec::Codec::Bpe,
            "tampering with the length must not change the classification"
        );
        assert!(
            codec::decode(&damaged).is_err(),
            "declared length {tamper:#010x} was accepted"
        );
    }
}

/// A payload without the marker is not damaged, it is uncompressed. Getting this wrong drops every
/// uncompressed file in the layout.
#[test]
fn an_unmarked_payload_is_returned_verbatim() {
    for data in [
        b"".to_vec(),
        vec![0x00],
        vec![0xff; 4],
        b"DRGN".to_vec(),
        vec![0x4d, 0x52, 0x47, 0x1a, 0x02, 0x00, 0x00, 0x00],
        vec![0x53, 0x45, 0x41, 0x44, 0x53],
    ] {
        assert_eq!(
            codec::codec_for(&data),
            codec::Codec::Passthrough,
            "{data:02x?} was classified as compressed"
        );
        assert_eq!(codec::decode(&data).unwrap(), data);
    }
}

/// The last case is the one worth keeping: a payload that *begins* with a magic number but has no
/// marker at offset 4. A reader that sniffs offset 0 calls that compressed and reads a header as
/// data.
#[test]
fn a_container_header_is_not_a_codec_header() {
    let container = psxrt_compat::container::mrg::write(&[(0, 0)]);
    assert_eq!(
        codec::codec_for(&container),
        codec::Codec::Passthrough,
        "leading word {:#010x} is a container magic, not the codec marker",
        u32::from_le_bytes(container[..4].try_into().unwrap())
    );
}

/// 0x7f does two jobs in this format and the encoder has to keep them apart.
///
/// It is the count byte for a run of 128 dictionary entries *and* an ordinary literal byte that
/// data can contain. The two never meet — the dictionary region and the data region are read in
/// sequence and never interleave — but that is exactly the kind of thing a later "simplification"
/// breaks, so the payload here carries thirty literal 0x7f bytes and still substitutes.
#[test]
fn a_literal_0x7f_does_not_collide_with_the_run_count_byte() {
    let mut data = vec![0x7fu8; 30];
    data.extend_from_slice(&[0x10, 0x11, 0x10, 0x11]);

    let payload =
        bpe::encode(&data, &[(0x10, 0x11)]).expect("0x7f is the count byte, not a symbol");
    assert_eq!(payload.rules().len(), 1, "the rule was silently dropped");
    assert_eq!(codec::decode(payload.bytes()).unwrap(), data);
}

// ---------------------------------------------------------------------------
// Decoder-side guards, written against the format rather than against the encoder
// ---------------------------------------------------------------------------

// These three cannot be produced by `psxrt-compat`, which is why they are hand-assembled here. A
// round trip cannot reach them: the encoder is built so that it does not emit circular or overlong
// dictionaries. They are tested anyway because the inputs are not hypothetical — a cache directory
// is a plain text file anybody can edit, and a corrupt image produces arbitrary bytes. A reader
// whose only coverage is its own writer has no error paths under test at all.
///
/// Two `0xff` headers do it: each means "jump forward by 128 and read one entry", and the second
/// lands at 257. The reference dies here with an `ArrayIndexOutOfBoundsException`.
#[test]
fn a_dictionary_that_overruns_its_table_is_refused() {
    let mut payload = Vec::new();
    payload.extend_from_slice(&4u32.to_le_bytes()); // declared length
    payload.extend_from_slice(&0x1a45_5042u32.to_le_bytes());
    payload.extend_from_slice(&4u32.to_le_bytes()); // block of 4 decompressed bytes
    payload.extend_from_slice(&[0xff, 0x80, 0xff]); // dictionary: 129 + 128 = 257
    payload.extend_from_slice(b"abcd");

    assert!(matches!(
        codec::decode(&payload),
        Err(ImportError::MalformedDictionary { key: 257 })
    ));
}

/// A block header above the codec's ceiling.
#[test]
fn an_oversized_block_is_refused_rather_than_read() {
    let mut payload = Vec::new();
    payload.extend_from_slice(&0x1000u32.to_le_bytes());
    payload.extend_from_slice(&0x1a45_5042u32.to_le_bytes());
    payload.extend_from_slice(&0x801u32.to_le_bytes());

    assert!(matches!(
        codec::decode(&payload),
        Err(ImportError::OversizedBlock {
            declared: 0x801,
            limit: 0x800
        })
    ));
}

/// A header that asks for more output than the reader will allocate.
///
/// The declared length is the first word of the file and it is trusted by the format, which is
/// why the bound exists at all: without it, eight attacker-chosen bytes choose the size of an
/// allocation.
#[test]
fn an_implausible_declared_length_is_refused() {
    let mut payload = Vec::new();
    payload.extend_from_slice(&u32::MAX.to_le_bytes());
    payload.extend_from_slice(&0x1a45_5042u32.to_le_bytes());
    payload.extend_from_slice(&4u32.to_le_bytes());

    assert!(matches!(
        codec::decode(&payload),
        Err(ImportError::ImplausibleSize { .. })
    ));
}

/// A circular dictionary: two slots that expand into each other.
///
/// `0x80 -> (0x81, 0x81)` and `0x81 -> (0x80, 0x80)`, so every unwind leaves the pending list one
/// byte longer than it started and it never drains. Written by hand because the encoder cannot
/// produce it — its symbol pool excludes every byte present in the data, which is what makes this
/// class unreachable *by construction* — and worth testing anyway, because that unreachability is a
/// property of the writer and not of the format. This reader also reads files it did not write.
#[test]
fn a_circular_dictionary_terminates_as_an_error() {
    let payload = one_block(16, &[0x80], &swapped_dictionary());
    assert!(matches!(
        codec::decode(&payload),
        Err(ImportError::NonTerminatingDictionary { .. })
    ));
}

/// A payload that stops in the middle of a record, and one that over-produces.
///
/// The distinction being protected here is between "short" and "wrong": a truncated payload must
/// not quietly decode to fewer bytes, and a payload that produces more than it declared must not
/// quietly decode to a prefix.
#[test]
fn length_that_does_not_match_the_header_is_refused() {
    let dictionary = all_literal_dictionary();

    // Case one: a block that promises eight coded bytes and offers three, with nothing after them.
    // Assembled by hand rather than through `one_block` because a *truncated* payload is by
    // definition missing its tail; padding it and adding a terminator would turn the truncation
    // into a length mismatch, which is a different refusal.
    let mut short = header(8);
    short.extend_from_slice(&8u32.to_le_bytes());
    short.extend_from_slice(&all_literal_dictionary());
    short.extend_from_slice(b"abc");
    assert!(matches!(
        codec::decode(&short),
        Err(ImportError::Truncated { .. })
    ));

    // Case two: four coded bytes against two declared. The third has nowhere to go.
    let long = one_block(2, b"abcd", &dictionary);
    assert!(matches!(
        codec::decode(&long),
        Err(ImportError::LengthMismatch {
            declared: 2,
            written: 3
        })
    ));

    // The control, so the two above are not just "everything errors": the same four bytes with a
    // truthful header decode to themselves.
    let ok = one_block(4, b"abcd", &dictionary);
    assert_eq!(codec::decode(&ok).unwrap(), b"abcd");
}

/// A complete 256-slot table in which slots `0x80` and `0x81` expand into each other.
///
/// Two runs of 128 entries, which is the shape the encoder uses. A count byte of `0x80` or above is
/// *not* a longer run: it jumps forward and then reads exactly one entry. So "one run of 256" is
/// not expressible, and writing `0xff` at the front produces a dictionary that overruns its own
/// table instead of one that fills it — which is the other test, not this one.
///
/// The table has to be complete. A partial one is a truncated payload, and the truncation is
/// detected first, which would hide the cycle this fixture exists to expose.
fn swapped_dictionary() -> Vec<u8> {
    let mut dictionary = vec![0x7f];
    dictionary.extend_from_slice(&(0u32..128).map(|slot| slot as u8).collect::<Vec<u8>>());

    dictionary.push(0x7f);
    dictionary.extend_from_slice(&[0x81, 0x81]);
    dictionary.extend_from_slice(&[0x80, 0x80]);
    dictionary.extend_from_slice(&(130u32..256).map(|slot| slot as u8).collect::<Vec<u8>>());

    assert_eq!(
        dictionary.len(),
        260,
        "two count bytes, 254 literals, two pairs"
    );
    dictionary
}

/// Assemble a complete single-block payload by hand.
///
/// The trailing padding and the terminating zero count are added here because they are part of the
/// format. A hand-built payload that leaves them out fails for a reason unrelated to whatever the
/// test was about, which is the worst kind of green-to-red flake.
fn one_block(declared: usize, coded: &[u8], dictionary: &[u8]) -> Vec<u8> {
    let mut payload = header(declared as u32);
    payload.extend_from_slice(&(coded.len() as u32).to_le_bytes());
    payload.extend_from_slice(dictionary);
    payload.extend_from_slice(coded);
    while payload.len() % 4 != 0 {
        payload.push(0);
    }
    payload.extend_from_slice(&0u32.to_le_bytes());
    payload
}

/// A payload header: declared length, then the marker.
fn header(declared: u32) -> Vec<u8> {
    let mut payload = declared.to_le_bytes().to_vec();
    payload.extend_from_slice(&0x1a45_5042u32.to_le_bytes());
    payload
}

/// A complete 256-slot table with no substitutions: two runs of 128 literal entries.
fn all_literal_dictionary() -> Vec<u8> {
    let mut dictionary = vec![0x7f];
    dictionary.extend_from_slice(&(0u32..128).map(|slot| slot as u8).collect::<Vec<u8>>());
    dictionary.push(0x7f);
    dictionary.extend_from_slice(&(128u32..256).map(|slot| slot as u8).collect::<Vec<u8>>());
    dictionary
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    /// The property the module exists for. Generated input reaches shapes worth more than anything
    /// hand-written here: payloads long enough for several blocks, rules dropped for want of a free
    /// symbol, and data that happens to contain bytes in the symbol range.
    #[test]
    fn decode_undoes_encode(
        data in vec(any::<u8>(), 0..2500),
        rules in vec((any::<u8>(), any::<u8>()), 0..6),
    ) {
        let payload = bpe::encode(&data, &rules).unwrap();
        prop_assert_eq!(codec::codec_for(payload.bytes()), codec::Codec::Bpe);
        prop_assert_eq!(codec::decode(payload.bytes())?, data);
    }
}
