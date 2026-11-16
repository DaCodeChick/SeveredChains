//! The `files/` cache index, checked across the crate boundary.
//!
//! Every test here writes with `psxrt_compat::container::index` and reads with
//! `psxrt_import::cache::Index`. That crossing is the reason the writer exists: inside
//! `psxrt-import`, `Index::parse` and `Index::text` are two views of one understanding, and a
//! round trip between them can only ever confirm that the author meant the same thing twice. A
//! second, independently written writer that lands on the same bytes is a check that the
//! understanding matches the format.
//!
//! The three shapes below are the whole vocabulary: an entry that owns its bytes, an entry that
//! points at somebody else's, and a line that says nothing owns them. Everything a real MRG
//! directory produces is some arrangement of the first two.

use proptest::prelude::*;
use psxrt_compat::container::index::{self, Entry, Owner};
use psxrt_import::cache::{self, Index};

/// The cache version is compared, not parsed: any other value means the whole tree is discarded.
/// Two crates holding that constant need a reason to hold the same one.
#[test]
fn the_two_crates_agree_on_what_a_current_cache_is() {
    assert_eq!(
        index::VERSION,
        cache::VERSION,
        "a writer emitting one version while the reader rejects anything but another is a cache \
         that is thrown away on every launch"
    );
    assert_eq!(index::INDEX_FILE, cache::INDEX_FILE);
    assert_eq!(index::VERSION_FILE, cache::VERSION_FILE);
    assert_eq!(index::version_file(), cache::VERSION);
}

#[test]
fn a_plain_directory_writes_and_reads_as_twelve_bytes_of_nobody_owns_anything() {
    let written = index::write(&[Entry::own(12), Entry::own(12), Entry::own(12)]);

    assert_eq!(written, "0=0;12\n1=1;12\n2=2;12\n");

    let read = Index::parse(&written).expect("three well-formed lines");
    assert_eq!(read.len(), 3);
    for slot in 0..3 {
        let slot_text = read.slot(slot).expect("written");
        assert!(
            slot_text.is_own(slot),
            "slot {slot} should own its own bytes"
        );
        assert_eq!(slot_text.declared_size, 12);
        assert_eq!(
            read.resolve(slot).unwrap(),
            None,
            "a self-owning entry resolves to nobody"
        );
    }
}

/// The writer's `Owner::Own` and the reader's `is_own` are two descriptions of one idea, and this
/// is where they meet.
#[test]
fn a_pointer_written_here_resolves_there() {
    // Slot 1 reads slot 0's bytes. `Unpacker.java:615` writes exactly this when `entry.virtual()`.
    let written = index::write(&[Entry::own(8), Entry::pointer(0)]);
    assert_eq!(written, "0=0;8\n1=0;0\n");

    let read = Index::parse(&written).expect("parses");
    assert_eq!(
        read.resolve(1).unwrap(),
        Some(0),
        "the pointer reaches its owner"
    );
    assert_eq!(
        read.slot(1).unwrap().declared_size,
        0,
        "a pointer declares nothing, and that zero is not the length of what it reaches"
    );
}

/// Chains are expressible, because a target is an index and not a flag. Nothing in the reference
/// forbids `1` pointing at `2` which points at `0`, and `Loader` walks until an entry names itself.
#[test]
fn a_two_link_chain_is_written_here_and_walked_there() {
    let written = index::write(&[Entry::own(8), Entry::pointer(2), Entry::pointer(0)]);

    let read = Index::parse(&written).expect("parses");
    assert_eq!(
        read.resolve(1).unwrap(),
        Some(0),
        "the walk has to end at the entry that owns the bytes, not at the first hop"
    );
}

/// The reference's chain walk is `while (map.get(real) != real)` with no bound, so a pair of lines
/// that name each other spins forever. The writer can produce that pair; the reader refuses it.
#[test]
fn a_cycle_is_written_here_and_refused_there() {
    // Slot 0 names slot 1 and slot 1 names slot 0. Nothing owns anything; the walk would visit
    // both forever.
    let written = index::write(&[Entry::pointer(1), Entry::pointer(0)]);
    assert_eq!(written, "0=1;0\n1=0;0\n");

    let read =
        Index::parse(&written).expect("the text is well-formed even though it means nothing");
    let error = read
        .resolve(0)
        .expect_err("0 names 1 which names 0, and neither is an owner");
    assert!(
        error.to_string().contains("cycle"),
        "an unbounded walk is a hang, and a hang reported as a value is the whole point of \
         bounding it: {error}"
    );
}

/// A blank target is readable and has never been writable.
///
/// `Loader.loadDirectorySync` has a branch for it; `Unpacker.java:615` has no path that produces
/// it. The writer supports the shape only so this test can hand the reader something it is
/// specified to accept, and the assertion pins that the two readings of "no file" — a blank target
/// and an absent line — land in the same place.
#[test]
fn the_blank_form_the_reader_tolerates_has_never_been_emitted() {
    let written = index::write(&[
        Entry::own(4),
        Entry {
            owner: Owner::Nothing,
            declared_size: 0,
        },
    ]);

    assert_eq!(written, "0=0;4\n1=;0\n");

    let read = Index::parse(&written).expect("a blank target is legal");
    assert_eq!(read.slot(1).unwrap().target, None);
    assert_eq!(read.resolve(1).unwrap(), None);
    assert_eq!(
        read.resolve(0).unwrap(),
        None,
        "and the two are not confusable"
    );
}

#[test]
fn a_malformed_line_is_reported_with_its_line_number() {
    // The reference throws a bare RuntimeException with the line text; the Rust reader names the
    // position too, which is the one improvement that costs nothing at the read boundary.
    let error = Index::parse("0=0;4\nbogus\n").expect_err("two fields is not three");
    let text = error.to_string();
    assert!(text.contains("line 1"), "{text}");
    assert!(text.contains("bogus"), "{text}");
}

/// The gap difference, pinned.
///
/// The reference omits an entry with no bytes; `Index::text` writes `n=;0` into the hole. Both
/// read back as "no file", so the divergence is only observable in the bytes — which is exactly why
/// it needs a test rather than a paragraph.
#[test]
fn a_hole_is_spelled_differently_but_means_the_same_thing() {
    let dense = index::write(&[Entry::own(4)]);
    assert_eq!(
        dense,
        Index::parse(&dense).unwrap().text(),
        "a dense index has to round-trip byte-for-byte, or the two writers do not agree on \
         the only case they both claim to handle"
    );

    // Now the case where they part company. The reference would omit slot 1 entirely; the reader's
    // own writer spells the hole `1=;0`. Same meaning, different bytes.
    let with_a_hole = "0=0;4\n1=;0\n";
    let a = Index::parse(&dense).unwrap();
    let b = Index::parse(with_a_hole).unwrap();

    assert_eq!(a.len(), 1, "the writer's one line describes one slot");
    assert_eq!(
        b.len(),
        2,
        "the second line names a slot the writer would have skipped"
    );
    assert_eq!(a.resolve(0).unwrap(), b.resolve(0).unwrap());
    assert_eq!(
        b.resolve(1).unwrap(),
        None,
        "and the extra line resolves to nothing, as intended"
    );
}

// ---------------------------------------------------------------------------------------------
// The property half.
// ---------------------------------------------------------------------------------------------

// Any dense index the writer can produce must read back as exactly what was written, and must
// re-emit byte-identically. The second clause is the interesting one: it says the two writers
// agree on every input in this class, not just on the three examples above.
proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    #[test]
    fn every_dense_index_round_trips(entries in prop::collection::vec(0..4usize, 0..24)) {
        let entries: Vec<Entry> = entries
            .iter()
            .enumerate()
            .map(|(slot, target)| match target {
                0 => Entry::own(7),
                1 => Entry::pointer(slot),
                // A pointer at an earlier slot, which is the only shape a real archive produces,
                // plus a blank, which the reader tolerates and the reference never writes.
                2 if slot > 0 => Entry::pointer(slot - 1),
                3 => Entry { owner: Owner::Nothing, declared_size: 0 },
                _ => Entry::own(0),
            })
            .collect();

        let written = index::write(&entries);
        let read = Index::parse(&written)
            .unwrap_or_else(|error| panic!("refused to read what the writer emitted: {error}\n{written}"));

        prop_assert_eq!(read.len(), entries.len(), "one line per entry, no filler\n{}", written);

        for (slot, entry) in entries.iter().enumerate() {
            let read = read.slot(slot).expect("every slot was written");
            let expected = match entry.owner {
                Owner::Own => Some(slot),
                Owner::Parent(parent) => Some(parent),
                Owner::Nothing => None,
            };
            prop_assert_eq!(read.target, expected);
            prop_assert_eq!(read.declared_size, entry.declared_size);
        }

        prop_assert_eq!(&read.text(), &written, "dense text must be byte-identical both ways");
    }
}

// Cycles are constructible from arbitrary input, and the reader must answer with a value rather
// than by not answering. The bound on `Index::resolve` is what makes this a termination property
// instead of a hope. The test asserts the walk returns, not what it returns: a chain that points
// at itself has no owner, and neither does a cycle, and both are legal input.
proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    #[test]
    fn resolving_terminates_for_any_arrangement_of_pointers(
        targets in prop::collection::vec(prop::option::weighted(0.75, 0..20usize), 1..20)
    ) {
        let text: String = targets.iter().enumerate().map(|(slot, target)| match target {
            None => format!("{slot}=;0\n"),
            Some(0) => format!("{slot}={slot};0\n"),
            Some(target) => format!("{slot}={target};0\n"),
        }).collect();

        let read = Index::parse(&text).expect("the shape is well-formed by construction");

        for slot in 0..read.len() {
            // No assertion about the *answer* — a chain that points at itself has no owner, and a
            // cycle has no owner either. The assertion is that this line is reached at all.
            let _ = read.resolve(slot);
        }
    }
}
