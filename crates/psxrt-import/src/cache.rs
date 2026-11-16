//! The `files/` cache: what the toolchain writes out once, so that neither it nor the game has to
//! unpack a container again.
//!
//! # Shape
//!
//! The cache is a plain directory tree. Each directory that came out of a container carries one
//! ASCII file named `mrg` describing it, and the root carries a file named `version` holding the
//! single character `5`. There is no database, no manifest, and no binary index.
//!
//! An `mrg` file has one line per entry:
//!
//! ```text
//! <slot>=<target>;<size>
//! ```
//!
//! `slot` is the position inside the directory. `target` is the position whose bytes this entry
//! uses, and it equals `slot` for the ordinary case where an entry owns its own bytes. `size` is
//! the length the *table* declared, which for a pointer entry is zero even though the bytes it
//! points at have a length — see [`crate::container::mrg::Entry::index_length`] for why those are
//! kept apart. A blank target, `7=;0`, means "nothing here at all", which is different from a
//! target that points somewhere.
//!
//! # What this module is
//!
//! The index codec: [`Index::parse`] to read one, [`Index::text`] to write one, and
//! [`Index::resolve`] to answer "which entry actually owns the bytes for slot *n*". Those three are
//! the whole of it, and they are here rather than inside `container::mrg` because the index is a
//! format that outlives the parser — the toolchain writes it, the game reads it back on the next
//! launch, and a mod can ship one. Any change to it is a compatibility event, so it gets its own
//! file and its own tests.
//!
//! # What is deliberately not here
//!
//! Tree assembly, the transform registry, the incremental skip, and the whole-tree wipe below
//! version 5 are all specified in `planning/04` under this milestone, and none of them are built
//! yet. They need a *node* to hang off, and the node model is the open question in `planning/08`
//! §D-1: whether a file is addressed by path or by the 63-bit key that earlier drafts described.
//! Writing the orchestrator before that is answered would mean inventing the thing the design
//! document exists to decide. The cache codec above does not depend on the answer, which is why it
//! is built and they are not.

use crate::error::ImportError;

/// The version string this build reads and writes.
///
/// A cache at a different version is not upgraded — the whole tree is discarded and rebuilt.
/// Keeping the number here rather than inline means the discard path and the write path cannot
/// disagree about what "current" means.
pub const VERSION: &str = "5";

/// Name of the per-directory index file.
pub const INDEX_FILE: &str = "mrg";

/// Name of the root version file.
pub const VERSION_FILE: &str = "version";

/// One line of an index.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Slot {
    /// Which slot's bytes this one uses, or `None` when the line said nothing does.
    pub target: Option<usize>,
    /// The length the table declared. Zero for a pointer line, and not necessarily the number of
    /// bytes reachable.
    pub declared_size: usize,
}

impl Slot {
    /// Whether this slot names itself, which is the ordinary case of an entry owning its own bytes.
    ///
    /// Takes the index because a slot does not know its own position; "is this a pointer?" is only
    /// answerable relative to the line it came from.
    #[must_use]
    pub const fn is_own(&self, index: usize) -> bool {
        matches!(self.target, Some(target) if target == index)
    }
}

/// A whole `mrg` file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Index {
    slots: Vec<Option<Slot>>,
}

impl Index {
    /// Read index text.
    ///
    /// Lines are `slot=target;size`. A blank target means no file. Anything that is not exactly
    /// those three fields is reported with its line number, because an index that half-parses is
    /// worse than one that does not parse at all: a short line silently misaligns everything after
    /// it in a reader that counts positions instead of trusting the slot number.
    ///
    /// Gaps are allowed. A file that describes slot 7 and never mentions 0..6 yields a table with
    /// empty slots there, which is what the text says.
    ///
    /// That is a deliberate divergence, and the reason is worth the space. The reference keeps a
    /// `List` alongside its map and *appends* to it while iterating the map's entry set, then in a
    /// second pass indexes that list by slot number — `files.get(real)`, `files.set(virtual, …)`.
    /// Two independent things have to hold for that to give the right answer: the iteration order
    /// has to happen to be ascending, and the slot numbers have to be dense. Neither is enforced,
    /// and the first is a hash-table property. Sparse or out-of-order index text therefore shifts
    /// entries onto the wrong files, silently. Reading by declared slot cannot produce that class of
    /// wrong answer, at the cost of agreeing with the reference only where the reference is not
    /// accidentally wrong — which, for a cache the toolchain wrote itself, is everywhere.
    ///
    /// The tests below assert the Rust behaviour, not the reference's, and say so. This is a known
    /// and deliberate divergence rather than an unverified one; a reference-derived golden fixture
    /// would need the Java toolchain's own positional path to be run over adversarial input, which
    /// is not a thing this project currently has a harness for.
    pub fn parse(text: &str) -> Result<Self, ImportError> {
        let mut slots: Vec<Option<Slot>> = Vec::new();

        for (number, line) in text.lines().enumerate() {
            if line.is_empty() {
                continue;
            }

            let damage = |what: &str| ImportError::MalformedIndex {
                line: number,
                text: format!("{what}: {line}"),
            };

            let (slot_text, rest) = line
                .split_once('=')
                .ok_or_else(|| damage("no `=` separator"))?;
            let (target_text, size_text) = rest
                .split_once(';')
                .ok_or_else(|| damage("no `;` separator"))?;

            let slot: usize = slot_text
                .trim()
                .parse()
                .map_err(|_| damage("slot is not a number"))?;

            let declared_size: usize = size_text
                .trim()
                .parse()
                .map_err(|_| damage("size is not a number"))?;

            // A blank target is a legal line meaning "no file here", and it is how the reference
            // records an entry that names a place nothing owns.
            let target = if target_text.trim().is_empty() {
                None
            } else {
                let target: usize = target_text
                    .trim()
                    .parse()
                    .map_err(|_| damage("target is not a number"))?;
                Some(target)
            };

            if slot >= slots.len() {
                slots.resize(slot + 1, None);
            }
            slots[slot] = Some(Slot {
                target,
                declared_size,
            });
        }

        Ok(Self { slots })
    }

    /// Write the index text back out.
    ///
    /// Empty slots are written as `slot=;0`. The reference's writer omits them, because it writes
    /// one line per *resolved entry* and an omitted entry simply does not appear; both read back as
    /// "no file", so the two are equivalent, but they are not byte-identical. Byte-identity with a
    /// cache produced by the Java toolchain is not the goal — that the same cache reads the same in
    /// both implementations is, and `tests/cache_round_trip.rs` asserts that property.
    #[must_use]
    pub fn text(&self) -> String {
        let mut text = String::new();
        for (slot, entry) in self.slots.iter().enumerate() {
            match entry {
                Some(entry) => {
                    let target = entry
                        .target
                        .map_or(String::new(), |target| target.to_string());
                    text.push_str(&format!("{slot}={target};{}\n", entry.declared_size));
                }
                None => text.push_str(&format!("{slot}=;0\n")),
            }
        }
        text
    }

    /// How many slots the index describes.
    #[must_use]
    pub fn len(&self) -> usize {
        self.slots.len()
    }

    /// Whether the index describes nothing.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.slots.is_empty()
    }

    /// One slot, exactly as written.
    #[must_use]
    pub fn slot(&self, index: usize) -> Option<Slot> {
        self.slots.get(index).copied().flatten()
    }

    /// Which slot owns the bytes for slot `index`, following a chain of pointers if there is one.
    ///
    /// Returns `None` for a slot that names nothing, and for a slot that names itself. A chain is
    /// followed at most `len` times, and a chain that has not ended by then is reported as a cycle,
    /// because it cannot have: with *n* slots, a chain taking *n* links has visited something twice.
    ///
    /// The bound is not decoration. The reference follows the chain with `while (map.get(real) !=
    /// real)` and no limit, so an index whose lines point at each other — `5=6;0` beside `6=5;0` —
    /// spins forever. A corrupt or hand-edited cache file reaches that state without any help from
    /// the game, which is what makes it a defect rather than a curiosity.
    pub fn resolve(&self, index: usize) -> Result<Option<usize>, ImportError> {
        let Some(mut current) = self.slot(index).and_then(|slot| slot.target) else {
            return Ok(None);
        };

        if current == index {
            return Ok(None);
        }

        for _ in 0..self.slots.len() {
            match self.slot(current).and_then(|slot| slot.target) {
                None => return Ok(None),
                Some(next) if next == current => return Ok(Some(current)),
                Some(next) => current = next,
            }
        }

        Err(ImportError::IndexCycle { start: index })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The ordinary case: every slot owns its own bytes.
    #[test]
    fn a_plain_index_resolves_every_slot_to_itself() {
        let index = Index::parse("0=0;4\n1=1;4\n2=2;4\n").unwrap();
        assert_eq!(index.len(), 3);
        for slot in 0..3 {
            assert_eq!(index.resolve(slot).unwrap(), None);
            assert!(index.slot(slot).unwrap().is_own(slot));
        }
        assert_eq!(index.text(), "0=0;4\n1=1;4\n2=2;4\n");
    }

    /// A pointer line names somebody else, and `resolve` finds the owner.
    #[test]
    fn a_pointer_resolves_to_its_owner() {
        let index = Index::parse("0=0;8\n1=0;0\n").unwrap();
        assert_eq!(index.resolve(1).unwrap(), Some(0));
    }

    /// A chain of two pointers still has to end somewhere, and the reference follows it all the way.
    #[test]
    fn a_chain_of_pointers_is_followed_to_the_end() {
        let index = Index::parse("0=0;8\n1=2;0\n2=0;0\n").unwrap();
        assert_eq!(index.resolve(1).unwrap(), Some(0));
    }

    /// Two lines that point at each other never end. The reference loops; this reports.
    #[test]
    fn a_cyclic_index_is_reported_rather_than_followed() {
        let index = Index::parse("5=6;0\n6=5;0\n").unwrap();
        assert!(matches!(
            index.resolve(5),
            Err(ImportError::IndexCycle { start: 5 })
        ));
        assert!(matches!(
            index.resolve(6),
            Err(ImportError::IndexCycle { start: 6 })
        ));
    }

    /// A blank target is a real answer meaning "nothing here".
    #[test]
    fn a_blank_target_means_no_file_rather_than_a_bad_line() {
        let index = Index::parse("7=;0\n").unwrap();
        assert_eq!(index.len(), 8);
        assert_eq!(index.slot(7).unwrap().target, None);
        assert_eq!(index.resolve(7).unwrap(), None);
        // The gap below it reads as empty, not as damaged.
        assert_eq!(index.slot(0), None);
    }

    /// A pointer into a gap resolves to nothing rather than to whatever the gap's neighbour is.
    #[test]
    fn a_pointer_into_a_gap_resolves_to_nothing() {
        let index = Index::parse("0=0;8\n3=1;0\n").unwrap();
        assert_eq!(index.resolve(3).unwrap(), None);
    }

    /// Anything that is not three fields is refused, naming the line.
    #[test]
    fn a_line_that_is_not_three_fields_is_refused() {
        for text in [
            "0\n",
            "0=0\n",
            "0;0\n",
            "zero=0;0\n",
            "0=0;eight\n",
            "0=x;0\n",
        ] {
            assert!(Index::parse(text).is_err(), "should have refused {text:?}");
        }
    }

    /// Writing what was parsed gives back the same thing, gaps included.
    #[test]
    fn text_and_parse_are_inverse() {
        let text = "0=0;8\n1=;0\n2=0;0\n3=3;16\n";
        let index = Index::parse(text).unwrap();
        assert_eq!(index.text(), text);
        assert_eq!(Index::parse(&index.text()).unwrap(), index);
    }

    /// An empty file is an empty index, not a malformed one.
    #[test]
    fn an_empty_index_is_empty() {
        let index = Index::parse("").unwrap();
        assert!(index.is_empty());
        assert!(index.text().is_empty());
    }
}
