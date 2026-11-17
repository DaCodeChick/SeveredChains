//! Adversarial input: arbitrary bytes fed to every reader in `psxrt-import`.
//!
//! The round-trip files (`bpe_round_trip.rs`, `mrg_round_trip.rs`, `index_round_trip.rs`) start
//! from well-formed data and damage it in named ways. That proves a reader notices the specific
//! damages somebody thought of. It does not prove the reader is safe against bytes nobody thought
//! of, which is the actual situation for a tool whose input is a disc image somebody else burned.
//!
//! Two properties are asserted about unpredictable input:
//!
//! * **No panic.** Every reader here returns `Result`, and that is the entire point: a corrupt
//!   container is a *reportable* condition. A panic in a reader turns "this disc has three bad
//!   archives" into "the tool died on page one", and it is the one way a `Result`-typed function
//!   can still be wrong. Out-of-range slicing is a panic in Rust, so an unchecked slice inside a
//!   decoder is exactly what this file hunts for.
//! * **No silent short read.** Where a read succeeds it must hand back the amount of data the
//!   record asked for, and where a pointer walk succeeds it must land where the data says. The
//!   reference's own slice step checks nothing, so a shorter answer is the realistic way a port
//!   goes wrong quietly: it does not crash, it hands the next layer a plausible-looking buffer
//!   that is missing its tail.
//!
//! A failure here means one of two things and they need different responses: a panic is a defect in
//! the reader, a wrong length or wrong owner is a defect in the *model* of the format. Neither may
//! be fixed by weakening the assertion or by excluding the input that found it.
//!
//! # What this file is not
//!
//! It is a second net, not the contract. Whether the fuzz reaches a given state depends on the
//! seed, so anything that genuinely must stay fixed is *also* pinned by a named test — the
//! length-mismatch path that these four cases reach at odds is asserted by hand in
//! `bpe_round_trip.rs`. Use this file to find a case worth promoting to one, not as a substitute
//! for having one.
//!
//! The oracle in `arbitrary_index_text_terminates_and_agrees` walks the pointer graph with a
//! visited set while `Index::resolve` uses a bounded counter, on purpose: a property test that
//! reimplements the mechanism under test passes forever whatever it says. Each of the two
//! properties above was checked for falsifiability by removing the guard it protects and
//! confirming this file fails; see `planning/05` §2.1 for the three mutations and what they
//! surfaced.

use proptest::collection::vec;
use proptest::prelude::*;
use psxrt_compat::container::bpe as write;
use psxrt_import::cache::Index;
use psxrt_import::container::bpe::{self, Codec};
use psxrt_import::container::mrg::{OffsetScale, Table};

/// The container magic, for tables that need to get past the "is this a container" check.
const MAGIC: u32 = 0x1a47_524d;

/// Feed arbitrary bytes to the codec decoder and require a total, self-consistent answer.
///
/// `decode` is the only function in the workspace that slices at an offset partly computed from
/// the bytes themselves, so it is where an out-of-range slice would live.
fn codec_is_total(payload: &[u8]) {
    // Which path was taken is knowable independently of the result, from the marker at offset 4.
    // A payload shorter than the 8-byte header cannot select BPE, so the indexing below is safe.
    let path = bpe::codec_for(payload);

    // Refusal is the expected answer for most of what proptest generates, and it is a correct
    // answer, so only the success case carries an obligation. Nothing is asserted about *which*
    // error: the specific errors are pinned by the named tests in bpe_round_trip.rs, and
    // restating them here would only slow the fuzz down.
    let Ok(out) = bpe::decode(payload) else {
        return;
    };

    match path {
        // Passthrough is byte-for-byte: nothing is decompressed, so nothing may change. If this
        // ever fails, a payload was treated as coded when its header said otherwise.
        Codec::Passthrough => {
            assert_eq!(out.as_slice(), payload, "passthrough altered bytes");
        }
        Codec::Bpe => {
            let declared =
                u32::from_le_bytes(payload[..4].try_into().expect("BPE path implies 8 bytes"))
                    as usize;
            assert_eq!(
                out.len(),
                declared,
                "a decode reported success with {} bytes but the header promised {declared}",
                out.len()
            );
        }
    }
}

/// Every table shape against a store of a different size than the table claims.
///
/// The point is `payload_of`: it turns a table entry into a range. A malformed container must not
/// be able to make that read outside the store, and it must not be able to read *part* of the way
/// and call that success.
fn table_is_total(table: &[u8], store: &[u8], sectors: bool) {
    let scale = if sectors {
        OffsetScale::Sectors
    } else {
        OffsetScale::Bytes
    };

    let Ok(parsed) = Table::parse(table, scale) else {
        return;
    };

    // The entry count comes from the header word and the parse must honour it exactly. Getting
    // this wrong in either direction is visible: too few entries silently drops files, too many
    // reads past the table.
    let declared = u32::from_le_bytes(table[4..8].try_into().unwrap()) as usize;
    assert_eq!(
        parsed.len(),
        declared,
        "the parser produced {} entries but the header declared {declared}",
        parsed.len()
    );

    for index in 0..parsed.len() {
        let Some(entry) = parsed.entry(index) else {
            continue;
        };

        match parsed.payload_of(store, index) {
            Ok(window) => {
                // An entry that owns bytes must yield exactly the number of bytes it asked for.
                // This is the assertion that catches a quiet short read. Sharing and zero-length
                // entries legitimately produce nothing, and `mrg_round_trip.rs` pins their exact
                // behaviour by name, so excluding them here is not excluding a case.
                if !entry.is_shared() && entry.length > 0 {
                    assert_eq!(
                        window.len(),
                        entry.length,
                        "entry {index} asked for {} bytes and was handed {}",
                        entry.length,
                        window.len()
                    );
                }
                // Whatever came back has to be a real run of the store. Cheap to state, and it is
                // the property that would fail if a window were ever assembled from two places, or
                // from memory outside the container.
                assert!(
                    store.windows(window.len()).any(|run| run == window),
                    "entry {index} yielded bytes that are not a contiguous run of the store"
                );
            }
            // A refusal is allowed and expected for windows that run off the end. What must not
            // happen is an index-shaped error for an entry that is in range, because that is how a
            // misaligned table would look: the answer is "no such entry" while the entry exists,
            // and the file it names is silently dropped.
            Err(error) => {
                assert!(
                    !matches!(error, psxrt_import::ImportError::NoSuchEntry { .. }),
                    "entry {index} exists but was reported as no such entry"
                );
            }
        }
    }
}

/// Walk a pointer chain with a visited set.
///
/// Deliberately a *different mechanism* from `Index::resolve`, which decides "cycle" by running
/// out of iterations. The two agree only if that bound is exactly right, so comparing them tests
/// the bound itself: too small and real chains get reported as cycles, too large and a cyclic
/// index hangs instead of answering. A test that reimplemented the same bound would prove nothing.
///
/// `Err(())` means the chain revisited a slot, which is a cycle and must be *reported* by the
/// reader rather than answered with no data.
fn walk(target_of: &[Option<usize>], start: usize) -> Result<Option<usize>, ()> {
    let Some(mut current) = target_of[start] else {
        return Ok(None);
    };
    if current == start {
        return Ok(None);
    }

    let mut seen = vec![false; target_of.len()];
    loop {
        if seen[current] {
            return Err(());
        }
        seen[current] = true;

        match target_of[current] {
            // The chain ran out into a slot with no line: no owner, and not damage either, since
            // gaps are legal in this format.
            None => return Ok(None),
            // A slot naming itself owns its own bytes, so it is the answer.
            Some(next) if next == current => return Ok(Some(current)),
            Some(next) => current = next,
        }
    }
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(4096))]

    /// Random bytes, none of which came from the encoder.
    #[test]
    fn random_bytes_never_panic_the_codec(payload in vec(any::<u8>(), 0..600)) {
        codec_is_total(&payload);
    }

    /// A correct payload cut at an arbitrary point. Truncation is the realistic corruption, and
    /// it is the one the round-trip file cannot generate, because its `one_block()` helper pads.
    #[test]
    fn truncated_payloads_never_panic(
        data in vec(any::<u8>(), 1..900),
        split in any::<f64>(),
    ) {
        let whole = write::encode(&data, &[]).expect("the encoder accepts a legal input");
        let at = (split.clamp(0.0, 1.0) * whole.bytes().len() as f64) as usize;
        codec_is_total(&whole.bytes()[..at]);
    }

    /// A correct payload with single bytes replaced. This reaches states the encoder cannot
    /// produce: dictionary entries that expand into each other, run counts over a short block, a
    /// declared length that contradicts the body.
    #[test]
    fn replaced_bytes_never_panic(
        data in vec(any::<u8>(), 1..500),
        index in any::<usize>(),
        replacement in any::<u8>(),
    ) {
        let mut damaged = write::encode(&data, &[])
            .expect("the encoder accepts a legal input")
            .bytes()
            .to_vec();
        let at = index % damaged.len();
        damaged[at] = replacement;
        codec_is_total(&damaged);
    }

    /// The same with a two-byte pair replaced, so a dictionary *entry* can be rewritten and not
    /// only a count byte. A rewritten entry is the case that can make the decoder expand forever,
    /// or read a length out of the dictionary itself.
    #[test]
    fn rewritten_dictionary_entries_never_panic(
        data in vec(any::<u8>(), 1..500),
        at in any::<usize>(),
        pair in any::<(u8, u8)>(),
    ) {
        let whole = write::encode(&data, &[]).expect("the encoder accepts a legal input");
        let mut damaged = whole.bytes().to_vec();
        // Dictionary entries live at or past offset 12, inside the block.
        if damaged.len() < 20 {
            return Ok(());
        }
        let at = 12 + (at % (damaged.len() - 20));
        damaged[at] = pair.0;
        damaged[at + 1] = pair.1;
        codec_is_total(&damaged);
    }
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(2048))]

    /// Random table bytes. The magic and a plausible count are spliced in so the parse gets past
    /// the two header checks and reaches the entry loop, which is the part under test. Those two
    /// refusals are already pinned by name in `mrg_round_trip.rs`.
    #[test]
    fn random_tables_never_panic(
        mut table in vec(any::<u8>(), 16..200),
        store_len in 0usize..512,
        sectors in any::<bool>(),
    ) {
        table[0..4].copy_from_slice(&MAGIC.to_le_bytes());
        let room = (table.len() - 8) / 8;
        table[4..8].copy_from_slice(&(room as u32).to_le_bytes());

        let store: Vec<u8> = (0..store_len).map(|i| (i % 251) as u8).collect();
        table_is_total(&table, &store, sectors);
    }

    /// Structurally valid tables with random entry windows: better coverage of the range logic,
    /// because the header is correct, so a failure can only come from a window.
    #[test]
    fn random_entry_windows_never_panic(
        entries in vec((any::<u32>(), any::<u32>()), 0..24),
        store_len in 0usize..1024,
        sectors in any::<bool>(),
    ) {
        let mut table = MAGIC.to_le_bytes().to_vec();
        let count = u32::try_from(entries.len()).unwrap() * 8;
        table.extend_from_slice(&count.to_le_bytes());
        for (start, length) in &entries {
            table.extend_from_slice(&start.to_le_bytes());
            table.extend_from_slice(&length.to_le_bytes());
        }

        let store: Vec<u8> = (0..store_len).map(|i| (i % 251) as u8).collect();
        table_is_total(&table, &store, sectors);
    }
}

// Arbitrary index text into the cache reader.
//
// `Index::resolve` walks pointer chains, so its failure mode is a chain that never ends rather
// than a bad slice. The reference walks with `while (map.get(real) != real)` and no limit, so a
// generated index containing a cycle is the input that would hang a faithful port. Termination is
// asserted by running the walk, and the answer is compared against `walk`, which uses a different
// cycle-detection mechanism.
proptest! {
    #![proptest_config(ProptestConfig::with_cases(4096))]

    #[test]
    fn arbitrary_index_text_terminates_and_agrees(
        lines in vec((any::<usize>(), any::<usize>(), any::<usize>(), any::<bool>()), 0..12),
    ) {
        const SLOTS: usize = 40;

        let mut text = String::new();
        let mut pairs: Vec<(usize, Option<usize>)> = Vec::new();
        for (slot, target, size, blank) in &lines {
            // `;` is the separator, not `:`. `FileMap.addFile` emits `name=target;size\n`, so a
            // generator using `:` produces genuinely malformed lines and the reader was right to
            // refuse them — which is exactly what it did when this file first used a colon.
            text.push_str(&format!(
                "{}={};{}\n",
                slot % SLOTS,
                if *blank { String::new() } else { (target % SLOTS).to_string() },
                size % 300,
            ));
            pairs.push((slot % SLOTS, if *blank { None } else { Some(target % SLOTS) }));
        }

        let index = Index::parse(&text)
            .map_err(|error| TestCaseError::fail(format!("{text}\nrefused: {error}")))?;

        // A later line naming the same slot replaces the earlier one, so the graph is assembled
        // from the text rather than assumed from the generated list.
        let mut target_of = vec![None; SLOTS];
        for (slot, target) in &pairs {
            target_of[*slot] = *target;
        }

        for slot in 0..SLOTS {
            let expected = walk(&target_of, slot);
            let got = index.resolve(slot);

            match expected {
                Err(()) => {
                    // A cycle has to be *reported*. Returning `None` would also terminate, but it
                    // would terminate by quietly producing no data for a file that has bytes, and
                    // that is the silent-wrong-answer failure this file exists to catch.
                    prop_assert!(
                        got.is_err(),
                        "slot {slot} sits in a cycle but resolve() returned {got:?}\n{text}"
                    );
                }
                Ok(want) => {
                    let got = got.map_err(|error| {
                        TestCaseError::fail(format!(
                            "slot {slot} is acyclic but resolve() failed: {error}\n{text}"
                        ))
                    })?;
                    prop_assert_eq!(
                        got, want,
                        "slot {} resolved differently than an independent walk\n{}",
                        slot, text
                    );
                    if let Some(owner) = want {
                        prop_assert!(owner < SLOTS, "owner {owner} is outside the index");
                        prop_assert_ne!(owner, slot, "a slot may not be its own owner");
                    }
                }
            }
        }
    }
}
