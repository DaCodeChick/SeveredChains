//! Round trip for the `files/` cache index.
//!
//! Two independent things read and write this format, which is why it needs its own test file:
//! [`Table::index_text`] writes it from a parsed container, and [`Index::parse`] reads it back on
//! the next launch, or reads one that a mod shipped, or reads one a person edited by hand. They
//! live in different modules and neither knows about the other. If they disagree, the failure shows
//! up as a game that loads the wrong file, on a machine where nobody remembers unpacking anything.
//!
//! The strong property here is not "the strings match". It is that *both* ways of answering "which
//! entry owns these bytes" give the same answer — following [`Entry::shared_from`], which the parser
//! computed while reading the table, and following the index chain, which the cache reader computes
//! while reading text. Those are different computations over different data, and they have to land
//! in the same place.

use psxrt_compat::container::mrg as write;
use psxrt_import::ImportError;
use psxrt_import::cache::{INDEX_FILE, Index, VERSION, VERSION_FILE};
use psxrt_import::container::mrg::{OffsetScale, Table};

/// A container exercising every shape an index line can take: an owner, a duplicate of it, a
/// zero-size pointer at the same offset, two plain files, and a pointer that names nothing.
///
/// A six-entry table is 56 bytes, so the payload region starts at 56 and the offsets below are
/// real ones. Offsets that landed inside the table would still "work" and would prove nothing.
fn fixture() -> (Vec<u8>, Table) {
    let mut payload = Vec::new();
    payload.extend_from_slice(b"shared-window!"); // 56..70
    payload.extend_from_slice(b"x"); //              70..71
    payload.extend_from_slice(b"own-file!"); //      71..80
    let entries = [
        (56u32, 14),  // 0 — owns 56..70
        (56u32, 14),  // 1 — duplicate of 0
        (56u32, 0),   // 2 — pointer at 56
        (70u32, 1),   // 3 — owns 70..71
        (71u32, 9),   // 4 — owns 71..80
        (9000u32, 0), // 5 — pointer into nowhere
    ];
    let store = {
        let mut store = write::write(&entries);
        store.extend_from_slice(&payload);
        assert_eq!(
            store.len(),
            80,
            "the fixture's offsets must name real bytes"
        );
        store
    };
    let table = Table::parse(&store, OffsetScale::Bytes).expect("the fixture must parse");
    (store, table)
}

/// The fixture has to be checked before it is trusted to prove anything else.
#[test]
fn the_fixture_covers_every_index_shape() {
    let (store, table) = fixture();
    assert_eq!(
        table.iter().map(|(_, e)| e.shared_from).collect::<Vec<_>>(),
        vec![None, Some(0), Some(0), None, None, None],
        "the fixture no longer contains a duplicate, a pointer, and an orphan"
    );
    assert_eq!(store.len(), 80);
}

/// Write the index out of a parsed table, read it back with the cache reader, and check the two
/// answers agree. This is the test the module exists for.
#[test]
fn the_index_and_the_table_agree_about_who_owns_what() {
    let (store, table) = fixture();

    let index = Index::parse(&table.index_text()).expect("a generated index must read back");
    assert_eq!(index.len(), table.len());

    for (index_number, entry) in table.iter() {
        let resolved = index
            .resolve(index_number)
            .unwrap_or_else(|error| panic!("slot {index_number}: {error}"));

        assert_eq!(
            resolved, entry.shared_from,
            "slot {index_number}: the table says {:?}, the index says {resolved:?}",
            entry.shared_from
        );

        // Where there is an owner, the bytes reachable through it are the bytes the entry shares.
        if let Some(owner) = resolved {
            assert_eq!(
                table.payload_of(&store, owner).unwrap(),
                &store[table.entry(owner).unwrap().start
                    ..table.entry(owner).unwrap().start + entry.length],
                "slot {index_number} resolves to an owner with the wrong contents"
            );
        }
    }
}

/// A generated index reads back as the same object, byte for byte and slot for slot.
#[test]
fn index_text_survives_a_write_read_cycle() {
    let (_, table) = fixture();
    let written = table.index_text();
    let read_back = Index::parse(&written).unwrap();

    assert_eq!(read_back.text(), written, "text and parse are not inverse");
    assert_eq!(Index::parse(&read_back.text()).unwrap(), read_back);
}

/// A cache file written by the Java toolchain has to parse without ceremony, including the shapes
/// a generated one never produces. `7=;0` in particular is real: it is how "no file here" is
/// recorded, and the reader has to treat it as an answer rather than as a short line.
#[test]
fn a_hand_written_cache_file_parses() {
    // Taken in shape from what the reference's FileMap produces: `name=target;size`, US-ASCII,
    // newline-terminated, with the name being a positional number for a container directory.
    let text = "0=0;4\n1=1;4\n2=0;0\n3=;0\n4=4;0\n";
    let index = Index::parse(text).unwrap();

    assert_eq!(index.len(), 5);
    assert_eq!(
        index.resolve(0).unwrap(),
        None,
        "an owner resolves to nothing"
    );
    assert_eq!(
        index.resolve(2).unwrap(),
        Some(0),
        "a pointer finds its owner"
    );
    assert_eq!(
        index.resolve(3).unwrap(),
        None,
        "a blank target names no file"
    );
    // Slot 4 says it points at itself with a zero size. That is an owner, not a pointer.
    assert_eq!(index.resolve(4).unwrap(), None);
}

/// Two lines that point at each other. The reference spins forever on this input; a cache file is
/// attacker-writable in any install where somebody can reach the `files/` directory.
#[test]
fn a_cyclic_cache_file_is_refused_instead_of_followed() {
    let index = Index::parse("0=1;0\n1=0;0\n").unwrap();
    assert!(matches!(
        index.resolve(0),
        Err(ImportError::IndexCycle { start: 0 })
    ));
}

/// Damage is named, with the line it came from. "Malformed input" is not actionable when a cache
/// directory has two hundred lines in it.
#[test]
fn damage_names_the_line_that_caused_it() {
    let text = "0=0;4\n1=1;4\ngarbage\n";
    match Index::parse(text) {
        Err(ImportError::MalformedIndex { line, text }) => {
            assert_eq!(line, 2);
            assert!(
                text.contains("garbage"),
                "the offending text is missing: {text}"
            );
        }
        other => panic!("expected a named malformed line, got {other:?}"),
    }
}

/// The two file names and the version are part of the format, not implementation detail. Moving
/// any of them silently invalidates every cache on disk.
#[test]
fn the_cache_identifiers_are_stable() {
    assert_eq!(VERSION, "5");
    assert_eq!(INDEX_FILE, "mrg");
    assert_eq!(VERSION_FILE, "version");
}
