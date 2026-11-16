//! Round trip for the MRG container: `psxrt-compat` writes a table, `psxrt-import` parses and
//! extracts from it.
//!
//! Same reasoning as `bpe_round_trip.rs` — the writer is an oracle, not a packing tool, and it is
//! kept in another crate so that agreeing with the parser is evidence rather than a tautology.
//!
//! The tests here are arranged by what they could plausibly get wrong, not by which function they
//! call. The three things worth checking in a container reader are: that a window is cut at the
//! right place, that a window is cut at the right *scale*, and that an entry which does not own its
//! bytes does not quietly produce somebody else's.

use proptest::collection::vec;
use proptest::prelude::*;
use psxrt_compat::container::mrg as write;
use psxrt_import::ImportError;
use psxrt_import::container::mrg::{OffsetScale, SECTOR_BYTES, Table};

/// Build a container: a table, then a payload region with known bytes placed at given offsets.
///
/// Returns the store and the entries it was built from, so a test can assert against the source of
/// truth rather than against what the parser said.
#[must_use]
fn container(entries: &[(u32, u32)], payload: &[u8]) -> Vec<u8> {
    let mut store = write::write(entries);
    store.extend_from_slice(payload);
    store
}

/// Both crates name the same magic word, and neither is allowed to import it from the other.
#[test]
fn both_crates_agree_on_the_magic_word() {
    assert_eq!(write::MAGIC, psxrt_import::container::mrg::MAGIC);
    // Written out rather than taken from either crate: this is the value a real container starts
    // with, and if both implementations drifted together this test would still fail.
    assert_eq!(&write::write(&[])[..4], b"MRG\x1a");
}

/// The plain case: distinct, in-bounds, non-empty entries.
#[test]
fn a_written_container_yields_its_own_bytes_back() {
    const ALPHA: &[u8] = b"alpha";
    const BETA: &[u8] = b"beta-beta-beta";

    // Table of 2 entries is 8 + 16 = 24 bytes, so ALPHA starts at 24 and BETA at 29.
    let entries = [(24u32, ALPHA.len() as u32), (29u32, BETA.len() as u32)];
    let mut payload = ALPHA.to_vec();
    payload.extend_from_slice(BETA);
    let store = container(&entries, &payload);

    let table = Table::parse(&store, OffsetScale::Bytes).expect("a well-formed container parses");
    assert_eq!(table.len(), 2);
    assert_eq!(table.payload_of(&store, 0).unwrap(), ALPHA);
    assert_eq!(table.payload_of(&store, 1).unwrap(), BETA);
    assert!(
        !table.entry(0).unwrap().is_shared() && !table.entry(1).unwrap().is_shared(),
        "distinct offsets are not duplicates, and must not be treated as if they were"
    );
}

/// Parsing and extracting have to agree with each other *and* with the offsets the container was
/// built from. This is the one test that goes all the way from written bytes to file contents.
#[test]
fn extraction_of_a_written_container_is_deterministic_and_complete() {
    let files: Vec<Vec<u8>> = (0..12u32).map(|i| vec![i as u8; 3 + i as usize]).collect();

    // Lay the files out back to back behind the table, no gaps and no overlaps.
    let header = 8 + 8 * files.len();
    let mut entries = Vec::new();
    let mut payload = Vec::new();
    for file in &files {
        entries.push(((header + payload.len()) as u32, file.len() as u32));
        payload.extend_from_slice(file);
    }

    let store = container(&entries, &payload);
    let table = Table::parse(&store, OffsetScale::Bytes).unwrap();

    let extracted: Vec<Vec<u8>> = (0..table.len())
        .map(|i| table.payload_of(&store, i).unwrap().to_vec())
        .collect();
    assert_eq!(
        extracted, files,
        "extraction must reproduce the tree it was given"
    );

    // And twice over, because "deterministic" is part of the requirement: the same bytes have to
    // produce the same answer every time, or an incremental cache cannot ever be trusted.
    let again = Table::parse(&store, OffsetScale::Bytes).unwrap();
    let second: Vec<Vec<u8>> = (0..again.len())
        .map(|i| again.payload_of(&store, i).unwrap().to_vec())
        .collect();
    assert_eq!(second, files);
}

/// The two ways an entry can share, checked separately because they come apart in the reader: a
/// duplicate offset is caught in the first pass, a zero-size pointer in the second.
#[test]
fn sharing_entries_own_no_bytes_of_their_own() {
    const DATA: &[u8] = b"shared-window!";

    // A four-entry table is 8 + 32 bytes, so the payload region starts at 40 and all four offsets
    // below are real ones. Offsets that landed inside the table would still exercise the sharing
    // rules and would prove nothing about extraction.
    const AT: u32 = 40;
    let entries = [
        (AT, DATA.len() as u32), // 0 — owns the window
        (AT, DATA.len() as u32), // 1 — same offset, same size: a duplicate
        (AT, 0),                 // 2 — zero-size pointer at the same offset
        (AT, 1),                 // 3 — a one-byte file that happens to start there
    ];
    let store = container(&entries, DATA);
    let table = Table::parse(&store, OffsetScale::Bytes).unwrap();

    // Entry 0 owns it.
    assert_eq!(table.payload_of(&store, 0).unwrap(), DATA);

    // 1 and 2 share, so they produce nothing here; the bytes reach them later, as a view.
    assert_eq!(table.entry(1).unwrap().shared_from, Some(0));
    assert_eq!(table.entry(2).unwrap().shared_from, Some(0));
    assert_eq!(table.payload_of(&store, 1).unwrap(), &[] as &[u8]);
    assert_eq!(table.payload_of(&store, 2).unwrap(), &[] as &[u8]);

    // 3 is a duplicate too: sharing is decided by the offset word alone, not by offset-and-size,
    // so a genuinely different file that happens to start at the same place is swallowed. That is
    // the reference's rule, replicated. It costs nothing because the bytes still reach the entry,
    // later, as a view of the owner.
    assert_eq!(table.entry(3).unwrap().shared_from, Some(0));
}

/// A pointer whose offset matches nothing names no file at all. That is different from naming a
/// file and shorter than it, and the reader has to keep the two apart.
#[test]
fn a_pointer_with_nothing_at_its_offset_names_no_file() {
    let store = container(&[(0x7fff, 0)], b"nothing lives anywhere near here");
    let table = Table::parse(&store, OffsetScale::Bytes).unwrap();

    assert_eq!(table.entry(0).unwrap().shared_from, None);
    assert_eq!(table.entry(0).unwrap().length, 0);
    assert_eq!(table.payload_of(&store, 0).unwrap(), &[] as &[u8]);
}

/// A pointer may name an entry *above* it. Pass one resolves every sized entry before pass two
/// starts, so this is legal, and a reader that only searched downwards would silently resolve it to
/// nothing.
#[test]
fn a_pointer_may_name_a_higher_entry() {
    const DATA: &[u8] = b"late-owner";
    let entries = [
        (24u32, 0),  // 0 — pointer, resolved in pass two
        (24u32, 10), // 1 — the owner
    ];
    let store = container(&entries, DATA);
    let table = Table::parse(&store, OffsetScale::Bytes).unwrap();

    assert_eq!(table.entry(0).unwrap().shared_from, Some(1));
    assert_eq!(table.payload_of(&store, 0).unwrap(), &[] as &[u8]);
    assert_eq!(table.payload_of(&store, 1).unwrap(), DATA);
}

/// Sector-scaled containers store a *sector number* where others store a byte offset. A writer that
/// does not know the difference, or a reader that applies the wrong scale, is off by 2048 — and it
/// still looks like a valid read, which is what makes this worth its own test.
#[test]
fn a_sector_scaled_table_is_read_at_the_right_scale() {
    const DATA: &[u8] = b"sector nine";

    // One sector of padding holding the table at its front, then DATA at the start of sector 1.
    // Entry 0 names sector 1, so the two scales point at genuinely different bytes.
    let table_bytes = write::write(&[(1, DATA.len() as u32)]);
    let mut store = table_bytes.clone();
    store.resize(SECTOR_BYTES, 0x00);
    store.extend_from_slice(DATA);

    assert_eq!(
        &store[SECTOR_BYTES..SECTOR_BYTES + DATA.len()],
        DATA,
        "the fixture itself is wrong: the payload is not where the entry points"
    );

    let table = Table::parse(&table_bytes, OffsetScale::Sectors).unwrap();
    assert_eq!(
        table.entry(0).unwrap().start,
        SECTOR_BYTES,
        "a sector number was read as a byte offset"
    );
    assert_eq!(table.payload_of(&store, 0).unwrap(), DATA);

    // The same bytes read at the wrong scale give a *different, plausible* answer: byte offset 1 is
    // inside the table that was just read, and the reader would happily hand it back as a file.
    // This is the failure mode, asserted rather than described.
    let wrong = Table::parse(&table_bytes, OffsetScale::Bytes).unwrap();
    assert_eq!(wrong.entry(0).unwrap().start, 1);
    assert_ne!(
        wrong.payload_of(&store, 0).unwrap(),
        DATA,
        "the two scales must not coincide, or this test proves nothing"
    );
}

/// The scale is chosen by file name, and only for a closed set of seven names. Anything outside the
/// set is byte-scaled, including things that look close.
#[test]
fn only_the_named_containers_are_sector_scaled() {
    for name in ["DRGN.BIN", "DRGN0.BIN", "DRGN24.BIN"] {
        assert_eq!(
            OffsetScale::for_container(name),
            OffsetScale::Sectors,
            "{name} is in the sector-scaled set"
        );
    }
    for name in [
        "DRGN.BINX",
        "drgn.bin",
        "DRGN3.BIN",
        "DRGN25.BIN",
        "SUBMAP/DRGN.BIN",
        "",
    ] {
        assert_eq!(
            OffsetScale::Bytes,
            OffsetScale::for_container(name),
            "{name} is not in the set and must read as byte offsets"
        );
    }
}

/// An entry whose window runs off the end is a refusal, not a short read. A short read would be the
/// quiet version of the same bug.
#[test]
fn a_window_running_past_the_store_is_refused() {
    // Table is 16 bytes and the filler 22, so the store is 38 and the entry's 64-byte window from
    // offset 8 cannot be satisfied. The offset is deliberately absurd — it points into the table
    // that declares it — because that is the shape corrupt data actually has.
    let mut store = write::write(&[(8, 64)]);
    store.extend_from_slice(&[0x5au8; 22]);
    assert_eq!(
        store.len(),
        38,
        "the fixture's arithmetic, not an assertion about the reader"
    );
    assert!(matches!(
        Table::parse(&store, OffsetScale::Bytes)
            .unwrap()
            .payload_of(&store, 0),
        Err(ImportError::OutOfBounds {
            index: 0,
            start: 8,
            end: 72,
            available: 38
        })
    ));
}

/// A table that lies about its own length is refused before anything is allocated. The count is
/// four attacker-shaped bytes and the reference trusts it.
#[test]
fn a_table_claiming_more_bytes_than_exist_is_refused() {
    // Count of 2^29 needs 4 GiB of table; the buffer is eight bytes long.
    let mut table = write::MAGIC.to_le_bytes().to_vec();
    table.extend_from_slice(&(1u32 << 29).to_le_bytes());
    assert!(matches!(
        Table::parse(&table, OffsetScale::Bytes),
        Err(ImportError::Truncated {
            needed: _,
            available: 8
        })
    ));

    // And an absurd count: eight bytes of buffer claiming four gigabytes of table.
    let mut table = write::MAGIC.to_le_bytes().to_vec();
    table.extend_from_slice(&u32::MAX.to_le_bytes());
    assert!(Table::parse(&table, OffsetScale::Bytes).is_err());
}

/// A container that does not start with the magic word is not a container, and saying so beats
/// reading the first eight bytes as a table.
#[test]
fn a_non_container_is_named_rather_than_misread() {
    // Eight bytes that would otherwise parse as a one-entry table.
    let not_a_container = vec![0u8; 16];
    assert!(matches!(
        Table::parse(&not_a_container, OffsetScale::Bytes),
        Err(ImportError::UnrecognisedContainer { found: 0 })
    ));
    assert!(Table::parse(&[], OffsetScale::Bytes).is_err());
    assert!(Table::parse(&[0x4d, 0x52, 0x47], OffsetScale::Bytes).is_err());
}

/// A table with no entries is legal and means an empty directory, not a broken one. The reference
/// writes a directory sentinel for it rather than failing.
#[test]
fn an_empty_table_is_a_directory_and_not_damage() {
    let table = write::write(&[]);
    let parsed = Table::parse(&table, OffsetScale::Bytes).unwrap();
    assert!(parsed.is_empty());
    assert!(parsed.index_text().is_empty());
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(400))]

    /// The safety property for the whole module: no combination of table bytes, store length,
    /// offset scale, or generated entries can make the reader index outside the bytes it was given,
    /// or panic doing it. The reference gets several of these wrong by design — it reads past the
    /// end of the array and lets the exception happen — so this is the seam where that difference
    /// is paid for.
    #[test]
    fn no_table_shape_reaches_outside_its_store(
        entries in vec((any::<u32>(), any::<u32>()), 0..20),
        slack in 0usize..2048,
        sectors in any::<bool>(),
    ) {
        let table_bytes = write::write(&entries);
        let mut store = table_bytes.clone();
        store.resize(table_bytes.len() + slack, 0x5a);

        let scale = if sectors { OffsetScale::Sectors } else { OffsetScale::Bytes };
        let Ok(table) = Table::parse(&table_bytes, scale) else { return Ok(()) };

        prop_assert_eq!(table.len(), entries.len());

        for index in 0..table.len() {
            match table.payload_of(&store, index) {
                Ok(window) => {
                    // Whatever came back has to be inside the store, which is what makes the
                    // borrow legal; the assertion is here to catch a future version that returns
                    // a slice cut from somewhere else.
                    prop_assert!(window.len() <= store.len());
                }
                Err(ImportError::OutOfBounds { index: reported, .. }) => {
                    prop_assert_eq!(reported, index);
                }
                // Anything else is a bug: this property exists to prove the reader only ever
                // answers "no" for a reason it can name.
                Err(error) => {
                    let named = matches!(error, ImportError::OutOfBounds { .. });
                    prop_assert!(named, "unexplained failure for entry {index}: {error}");
                }
            }
        }

        // A position past the end is a caller error, and it stays one.
        let past_end = table.payload_of(&store, entries.len());
        let refused = matches!(past_end, Err(ImportError::NoSuchEntry { .. }));
        prop_assert!(refused, "an out-of-range index returned {past_end:?}");
    }
}
