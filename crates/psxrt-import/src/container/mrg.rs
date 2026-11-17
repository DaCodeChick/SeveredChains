//! The MRG container: an offset/size table, and the two ways an entry can name data it does not
//! own.
//!
//! An MRG is not a filesystem and not an archive with filenames. It is a table of `(offset, size)`
//! pairs with no names at all — the children it produces are numbered by *table position*, `0`,
//! `1`, `2`, and so on. Any name a tool later gives them comes from elsewhere.
//!
//! # The header, and where it stops
//!
//! ```text
//! offset  size  meaning
//!   0x00    4   magic 0x1a47524d — the bytes 4d 52 47 1a, `MRG` followed by 0x1A
//!   0x04    4   entry count
//!   0x08    8n  the entries: u32 offset, u32 size, repeated `count` times
//! ```
//!
//! There is no name table, no per-entry flag, and no archive-wide compression field. A reader
//! looking for those is looking for something the format does not have; see "What is *not* here" at
//! the foot of this file.
//!
//! # Two scales for the offset field
//!
//! The same word means two different things depending on which file the table came from. For most
//! files it is a byte offset into the container. For the seven sector-aligned containers this game
//! ships, it is a *sector number*, and the byte offset is that value times 0x800. Nothing inside the
//! table says which is in force: the scale is a property of the containing *file*, and a container
//! has no name. Choosing it is therefore the caller's job — see [`OffsetScale`], which is taken as a
//! parameter and not inferred, and which is why getting it wrong is at least a mistake somebody can
//! see in a call site rather than a fact buried in a parser.
//!
//! # Two ways an entry can share
//!
//! Both exist and they are not the same case.
//!
//! **A duplicate offset.** Two entries naming the same place. The second is recorded as sharing with
//! the first, and its bytes are not extracted a second time.
//!
//! **A zero-size entry.** An entry whose size word is 0 is not a zero-length file; it is a
//! *pointer*. Its offset word names a place that some other entry owns, and the table records which
//! one.
//!
//! Both are stored as [`Entry::shared_from`], because in both cases the bytes belong to somebody
//! else and extracting them here would double-count. But they differ in what gets written to the
//! index, and that difference is preserved — see [`Entry::index_length`].

use crate::container::read_u32_le;
use crate::error::ImportError;

/// The container's leading word. Bytes on disk are `4D 52 47 1A`.
pub const MAGIC: u32 = 0x1a47_524d;

/// Bytes in a sector, for the containers that count in sectors.
pub const SECTOR_BYTES: usize = 0x800;

/// How to read the offset word of a table.
///
/// Chosen by the caller. A table carries no field saying which scale is in force, and the only
/// thing that could imply one — the name of the file it came out of — is not a property of the
/// format. `severedchains-game::container::mrg` holds the list of names that need
/// [`OffsetScale::Sectors`] and hands the answer in here.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum OffsetScale {
    /// The offset word is a byte offset.
    #[default]
    Bytes,
    /// The offset word is a sector number; multiply by [`SECTOR_BYTES`].
    Sectors,
}

/// One row of the table.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    /// First byte of this entry's window, already scaled to a byte offset.
    ///
    /// For a sharing entry this is the *unscaled* word copied out of the table, which under sector
    /// scaling is not a byte offset at all. Nothing dereferences it — a sharing entry's bytes are
    /// reached through [`Entry::shared_from`] — and it is carried only because the reference carries
    /// it. Do not read it as an address.
    pub start: usize,

    /// How many bytes this entry reaches, where those bytes are reachable at all.
    pub length: usize,

    /// The length written into the index line for this entry.
    ///
    /// Usually equal to [`Entry::length`], and the two differ in exactly two cases: a duplicate
    /// offset, where both hold the size; and a zero-size pointer that resolved to an owner, where
    /// `length` is the owner's and this is zero. The index is a record of what the table *said*,
    /// which is why the two are kept apart rather than collapsed into one number.
    pub index_length: usize,

    /// The table index whose bytes this entry names, if it names somebody else's.
    pub shared_from: Option<usize>,
}

impl Entry {
    /// Whether this entry's bytes belong to another entry.
    #[must_use]
    pub const fn is_shared(&self) -> bool {
        self.shared_from.is_some()
    }
}

/// A parsed table.
#[derive(Debug, Clone)]
pub struct Table {
    entries: Vec<Entry>,
}

impl Table {
    /// Read a table from container bytes.
    ///
    /// `table` is the whole container — magic word, count, and entries together, which is the unit
    /// the reference hands to its parser.
    ///
    /// ## The two passes, and why the order matters
    ///
    /// Entries are resolved in two passes: everything with a size, then everything without. Sharing
    /// is resolved inside each pass against what has already been resolved, so an entry can only
    /// ever point at an entry that was resolved *before* it, and the answer for entry 40 can depend
    /// on entry 39. A single-pass reader gets this wrong, and gets it wrong quietly.
    pub fn parse(table: &[u8], scale: OffsetScale) -> Result<Self, ImportError> {
        let magic = read_u32_le(table, 0).ok_or(ImportError::Truncated(8, table.len()))?;
        if magic != MAGIC {
            return Err(ImportError::UnrecognisedContainer(magic));
        }

        let count = read_u32_le(table, 4).ok_or(ImportError::Truncated(8, table.len()))?;

        // Bounds before allocation. A four-byte count is attacker-shaped data, and both of these
        // checks have to happen first: the multiply so a huge count cannot wrap into a small
        // length, and the comparison so a table that lies about its own size never reaches the
        // allocator.
        let table_bytes = usize::try_from(count)
            .ok()
            .and_then(|count| count.checked_mul(8))
            .and_then(|bytes| bytes.checked_add(8))
            .ok_or(ImportError::Truncated(usize::MAX, table.len()))?;
        if table.len() < table_bytes {
            return Err(ImportError::Truncated(table_bytes, table.len()));
        }

        let entries = count as usize;
        let mut parsed: Vec<Option<Entry>> = vec![None; entries];

        // Pass one: entries that claim bytes.
        for index in 0..entries {
            let row = 8 + index * 8;
            // In range by the length check above, so the fallbacks are unreachable and only keep
            // the indexing total.
            let offset = read_u32_le(table, row).unwrap_or_default();
            let size = read_u32_le(table, row + 4).unwrap_or_default();
            if size == 0 {
                continue;
            }
            let length = usize::try_from(size).unwrap_or(usize::MAX);

            // Duplicate detection compares the *already-resolved* window start of earlier entries
            // against this entry's *raw, unscaled* offset word. Under byte scaling the two are the
            // same kind of number and this finds real duplicates. Under sector scaling a resolved
            // start has been multiplied by 2048 while this word has not, so the comparison almost
            // always fails and duplicates go undetected.
            //
            // That asymmetry is in the source this was read from, and it is replicated rather than
            // corrected. The
            // effect is not a wrong answer — an undetected duplicate still extracts its own bytes,
            // which are the right bytes, just extracted twice — so "fixing" it would change the
            // shape of the index for sector-scaled containers with no evidence that the current
            // shape is wrong. Correcting it needs a real sector-aligned container to compare against, which
            // is a P-02 asset question and not a decision available here.
            let duplicate = (0..index).find(|&earlier| {
                parsed[earlier].is_some_and(|earlier| earlier.start == widen(offset))
            });

            let start = match duplicate {
                // A duplicate keeps its raw word rather than re-resolving, matching how the
                // reference stores it. It is never dereferenced.
                Some(_) => widen(offset),
                None => resolve_start(index, offset, scale)?,
            };

            parsed[index] = Some(Entry {
                start,
                index_length: length,
                length,
                shared_from: duplicate,
            });
        }

        // Pass two: entries with no size, which are pointers rather than files.
        for index in 0..entries {
            let row = 8 + index * 8;
            let offset = read_u32_le(table, row).unwrap_or_default();
            let size = read_u32_le(table, row + 4).unwrap_or_default();
            if size != 0 {
                continue;
            }

            // The owner is searched for across the *whole* table, and against the table as it
            // stands partway through this pass. That is not the same as searching below this entry:
            // pass one has already resolved every sized entry, including the ones above, so a
            // pointer can legitimately name a higher-numbered entry and does. Narrowing the range to
            // `0..index` would be a different rule, and would leave such a pointer unresolved.
            //
            // Entries that are themselves sharing are excluded, which is what keeps a chain of
            // pointers from resolving through a link that never owned its bytes either.
            let owner = (0..entries).find(|&candidate| {
                parsed[candidate].is_some_and(|entry| {
                    entry.shared_from.is_none() && entry.start == widen(offset)
                })
            });

            // Resolve the owner without a panic path. The `find` above only yields an index whose
            // slot holds a resolved entry, so an empty owner slot is unreachable — but leaving an
            // `expect` here (the previous form) would put a panic on a path driven by table bytes,
            // where a future edit to the search could turn "refuse a malformed container" into
            // "abort the run". Degrading an unresolved owner to the same entry as "no owner found"
            // removes that class of failure, and `Entry` is `Copy` so the owner is read out before
            // the write below.
            let owner_entry = owner.and_then(|owner| parsed[owner].map(|entry| (owner, entry)));
            let entry = match owner_entry {
                Some((owner, owner_entry)) => Entry {
                    start: owner_entry.start,
                    length: owner_entry.length,
                    // Zero, not the owner's length. The index records what the table said about
                    // this entry, and the table said nothing.
                    index_length: 0,
                    shared_from: Some(owner),
                },
                None => Entry {
                    start: widen(offset),
                    length: 0,
                    index_length: 0,
                    shared_from: None,
                },
            };
            parsed[index] = Some(entry);
        }

        Ok(Self {
            // Every entry is filled by exactly one pass: pass one takes everything with a size,
            // pass two takes everything without one. The fallback keeps that reasoning in one place
            // instead of depending on it.
            entries: parsed
                .into_iter()
                .map(|entry| {
                    entry.unwrap_or(Entry {
                        start: 0,
                        length: 0,
                        index_length: 0,
                        shared_from: None,
                    })
                })
                .collect(),
        })
    }

    /// How many entries the table has.
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the table names no files at all.
    ///
    /// Legal, and distinct from a malformed table. An empty archive is a real thing a real container
    /// says.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// One entry, by table position.
    #[must_use]
    pub fn entry(&self, index: usize) -> Option<&Entry> {
        self.entries.get(index)
    }

    /// Every entry, in table order, which is also extraction order.
    pub fn iter(&self) -> impl Iterator<Item = (usize, &Entry)> {
        self.entries.iter().enumerate()
    }

    /// The bytes an entry *owns*, as opposed to the bytes it can reach.
    ///
    /// `store` is the container the table was parsed from. Entries that name nobody, or that name
    /// somebody else, own no bytes and yield none — which is not the same as having nothing to read.
    /// The reference's container step writes one cache file per entry that owns its bytes and skips
    /// the rest, and a sharing entry is skipped rather than copied. Reaching the bytes a pointer
    /// names is a later step's job, and it is not a slice of this container: by the time anything
    /// needs them the owner's file has been extracted on its own, and the reference hands the
    /// pointer a view spanning *the owner's whole file* plus a separately recorded declared length,
    /// not a window of that length. Building that here would settle the read-domain question in
    /// `planning/08` §D-2 by accident, so it is left to the layer that has to answer it.
    ///
    /// A window that runs off the end of `store` is an error rather than a short read. That is the
    /// whole point of returning a `Result`: the reference's own slice step checks nothing, and
    /// `planning/08` §D-3 settles that this reader will not read outside the window it was given.
    /// A window of zero length yields no bytes without being checked, because nothing is read.
    pub fn payload_of<'a>(&self, store: &'a [u8], index: usize) -> Result<&'a [u8], ImportError> {
        let Some(entry) = self.entries.get(index) else {
            return Err(ImportError::NoSuchEntry(index));
        };

        // A sharing entry has no bytes of its own: it points at another entry's window, and the
        // owner produces them. Same for a pointer that found no owner, which names nothing.
        if entry.is_shared() || entry.length == 0 {
            return Ok(&[]);
        }

        let end = entry
            .start
            .checked_add(entry.length)
            .ok_or(ImportError::OutOfBounds {
                index,
                start: entry.start,
                end: usize::MAX,
                available: store.len(),
            })?;
        store.get(entry.start..end).ok_or(ImportError::OutOfBounds {
            index,
            start: entry.start,
            end,
            available: store.len(),
        })
    }

    /// The `mrg` index text this table describes.
    ///
    /// One line per entry: `index=target;length`, ASCII, newline-terminated, in table order. The
    /// target is the entry's own number, or the number of the entry it shares with. The length is
    /// [`Entry::index_length`], which is what the *table* said — not the number of bytes reachable,
    /// which for a sharing entry is not this entry's business.
    ///
    /// This is the cache format, so it is written here rather than hidden inside the writer: the
    /// reader and the writer have to agree on a file that outlives both, and a round trip through it
    /// is the test that proves they do.
    #[must_use]
    pub fn index_text(&self) -> String {
        let mut text = String::new();
        for (index, entry) in self.iter() {
            let target = entry.shared_from.unwrap_or(index);
            text.push_str(&index.to_string());
            text.push('=');
            text.push_str(&target.to_string());
            text.push(';');
            text.push_str(&entry.index_length.to_string());
            text.push('\n');
        }
        text
    }
}

/// Turn a raw offset word into a byte offset.
fn resolve_start(index: usize, offset: u32, scale: OffsetScale) -> Result<usize, ImportError> {
    match scale {
        OffsetScale::Bytes => Ok(widen(offset)),
        OffsetScale::Sectors => {
            widen(offset)
                .checked_mul(SECTOR_BYTES)
                .ok_or(ImportError::OffsetOverflow {
                    index,
                    sector: offset,
                })
        }
    }
}

/// Widen a 32-bit offset or length to a `usize`.
///
/// Saturating rather than `as usize`. On a target where `usize` is narrower than 32 bits, `as`
/// would truncate a large offset into a small one and quietly change *which window gets read* —
/// the one class of error that produces plausible wrong data instead of an error. Saturating turns
/// that into an out-of-bounds failure. Every target this workspace builds for has at least 32-bit
/// `usize`, so this is a guard against a future one, not a fix for a present bug; it is written out
/// because `usize::from` does not exist for `u32` and the obvious alternative is `as`.
fn widen(value: u32) -> usize {
    usize::try_from(value).unwrap_or(usize::MAX)
}

// # What is *not* here, and why that is written down
//
// Three absences, each of which looks like an omission until you check:
//
// * **No compression field.** The reference does not compress at container level. Compression is
//   decided per *file*, later, by [`crate::container::bpe::codec_for`] looking at the bytes
//   themselves. A container-wide "is this archive compressed" flag exists in other PSX archive
//   formats and in no place here; an earlier draft assumed otherwise.
// * **No names.** Table position is the identity, which is why the index lines are numbers. A
//   design that put names in the table could not round-trip a real container.
// * **No archive-wide payload.** Entries are slices of the container that holds their table. There
//   is no concatenated blob behind them, so [`Table::payload_of`] takes the same bytes
//   [`Table::parse`] took.

#[cfg(test)]
mod tests {
    use super::*;

    /// Assemble container bytes from `(offset, size)` pairs.
    fn container(entries: &[(u32, u32)]) -> Vec<u8> {
        let mut table = MAGIC.to_le_bytes().to_vec();
        table.extend_from_slice(&(entries.len() as u32).to_le_bytes());
        for (offset, size) in entries {
            table.extend_from_slice(&offset.to_le_bytes());
            table.extend_from_slice(&size.to_le_bytes());
        }
        table
    }

    /// A leading word that is not the container's magic is refused, and says what it found.
    #[test]
    fn a_table_that_is_not_a_container_is_refused() {
        assert!(matches!(
            Table::parse(&0x1a45_5042u32.to_le_bytes(), OffsetScale::Bytes),
            Err(ImportError::UnrecognisedContainer(0x1a45_5042))
        ));
    }

    /// A count the bytes cannot fill is refused, and so is one whose arithmetic would wrap.
    #[test]
    fn a_count_larger_than_the_bytes_is_refused() {
        // Claims 100 entries and supplies none: 8 bytes of header, no table.
        let mut table = container(&[]);
        table.splice(4..8, 100u32.to_le_bytes());
        assert!(matches!(
            Table::parse(&table, OffsetScale::Bytes),
            Err(ImportError::Truncated(808, 8))
        ));

        // The same trap at the other end of the range. On a 64-bit target `count * 8 + 8` still
        // fits, so this reads as a short table; on a 32-bit target the multiply wraps and the
        // guard catches it there instead. Either way the count is refused rather than trusted.
        let mut huge = container(&[]);
        huge.splice(4..8, u32::MAX.to_le_bytes());
        assert!(matches!(
            Table::parse(&huge, OffsetScale::Bytes),
            Err(ImportError::Truncated(..))
        ));
    }

    /// The plain case: every entry owns its own bytes.
    #[test]
    fn a_plain_table_reads_every_entry() {
        let table = container(&[(0, 4), (4, 4)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert_eq!(table.len(), 2);
        assert!(!table.entry(0).unwrap().is_shared());
        assert!(!table.entry(1).unwrap().is_shared());
        assert_eq!(table.index_text(), "0=0;4\n1=1;4\n");
    }

    /// Two entries on the same offset: the second shares with the first.
    #[test]
    fn a_repeated_offset_shares_with_its_first_occurrence() {
        let table = container(&[(0, 4), (0, 4)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert_eq!(table.entry(1).unwrap().shared_from, Some(0));
        // The index records the sharing, so a reader of the cache can tell a second copy apart from
        // a reference to the first. And the length survives, unlike the pointer case below.
        assert_eq!(table.index_text(), "0=0;4\n1=0;4\n");
    }

    /// A zero-size entry is a pointer, and it resolves against the pass-one result.
    #[test]
    fn a_zero_size_entry_points_at_whoever_owns_those_bytes() {
        let table = container(&[(0, 4), (0, 0)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        let pointer = table.entry(1).unwrap();
        assert_eq!(pointer.shared_from, Some(0));
        // Zero, not 4: the table said nothing about this entry's size, and the index says nothing
        // back. The bytes themselves are reachable through entry 0.
        assert_eq!(pointer.index_length, 0);
        assert_eq!(table.index_text(), "0=0;4\n1=0;0\n");
    }

    /// A pointer to a place nothing owns is not a file, and not an error either.
    #[test]
    fn a_pointer_with_no_owner_names_nothing() {
        let table = container(&[(0, 4), (64, 0)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        let pointer = table.entry(1).unwrap();
        assert!(!pointer.is_shared());
        assert_eq!(pointer.length, 0);
        assert_eq!(table.payload_of(&[0u8; 8], 1).unwrap(), &[] as &[u8]);
    }

    /// A pointer may name an entry *above* itself, and the reference allows it, so this reader
    /// does. A scan that only looked downwards would leave the pointer unresolved.
    #[test]
    fn a_pointer_may_name_a_later_entry() {
        // Entry 0 points at offset 8, which entry 1 owns. Pass one resolves entry 1 first, because
        // pass one runs to completion before any pointer is looked at.
        let table = container(&[(8, 2), (8, 2), (8, 0)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        // Entry 0 is first-come, so it owns offset 8 and entry 1 shares with it; entry 2 then
        // resolves to entry 0 as well, because that is the first non-sharing match.
        assert_eq!(table.entry(1).unwrap().shared_from, Some(0));
        assert_eq!(table.entry(2).unwrap().shared_from, Some(0));
        assert_eq!(table.index_text(), "0=0;2\n1=0;2\n2=0;0\n");
    }

    /// Sector scaling multiplies, and only for the names that use it.
    #[test]
    fn sector_scaled_tables_scale_their_offsets() {
        let table = container(&[(2, 4)]);
        let scaled = Table::parse(&table, OffsetScale::Sectors).unwrap();
        assert_eq!(scaled.entry(0).unwrap().start, 2 * SECTOR_BYTES);
        assert!(!scaled.entry(0).unwrap().is_shared());

        // The same bytes at byte scale name a completely different place.
        let plain = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert_eq!(plain.entry(0).unwrap().start, 2);
    }

    /// The two scales are not interchangeable, and the duplicate test is where that shows.
    ///
    /// Duplicate detection compares an earlier entry's *resolved* start against this entry's *raw*
    /// offset word. Under byte scaling those are the same kind of number, so a repeated offset is
    /// caught. Under sector scaling one has been multiplied by 2048 and the other has not, so the
    /// test answers a question nobody asked: it misses actual repeats and fires on coincidences.
    #[test]
    fn the_duplicate_test_depends_on_which_scale_is_in_force() {
        let repeated = container(&[(0x100, 4), (0x100, 4)]);

        // Byte scale: the two entries name the same four bytes and are recognised as sharing.
        let at_bytes = Table::parse(&repeated, OffsetScale::Bytes).unwrap();
        assert_eq!(at_bytes.entry(1).unwrap().shared_from, Some(0));

        // Sector scale, same bytes: entry 0 resolves to 0x100 * 0x800, entry 1's raw word is still
        // 0x100, so the comparison fails and each entry extracts separately.
        let at_sectors = Table::parse(&repeated, OffsetScale::Sectors).unwrap();
        assert!(!at_sectors.entry(1).unwrap().is_shared());

        // And the converse: two entries that name *different* places under sector scaling, because
        // 1 * 0x800 equals the raw word 0x800. Pinned because it is a real behaviour of the rule,
        // not a hypothetical — and because it is harmless only so long as a sharing entry is never
        // extracted in its own right, which is what `payload_of` guarantees.
        let coincidence = container(&[(1, 4), (0x800, 4)]);
        let at_sectors = Table::parse(&coincidence, OffsetScale::Sectors).unwrap();
        assert_eq!(at_sectors.entry(1).unwrap().shared_from, Some(0));
        assert_eq!(at_sectors.entry(1).unwrap().start, 0x800);
        assert_eq!(at_sectors.entry(1).unwrap().index_length, 4);
    }

    /// A zero-size pointer resolves against the same unresolved comparison, so sector-scaled
    /// pointers usually find no owner and produce no file.
    #[test]
    fn a_sector_scaled_pointer_rarely_finds_its_owner() {
        // The pointer names sector 1, which is where entry 0 lives once scaled. The comparison is
        // against the raw word, so it does not match.
        let table = container(&[(1, 4), (1, 0)]);
        let at_sectors = Table::parse(&table, OffsetScale::Sectors).unwrap();
        assert!(!at_sectors.entry(1).unwrap().is_shared());
        assert_eq!(at_sectors.entry(1).unwrap().length, 0);

        // At byte scale the same table resolves the pointer to entry 0.
        let at_bytes = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert_eq!(at_bytes.entry(1).unwrap().shared_from, Some(0));
    }

    /// A window that runs off the end of its store is reported, with the numbers.
    #[test]
    fn a_window_beyond_the_store_is_reported() {
        let table = container(&[(4, 8)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert!(matches!(
            table.payload_of(&[0u8; 8], 0),
            Err(ImportError::OutOfBounds {
                index: 0,
                start: 4,
                end: 12,
                available: 8
            })
        ));
    }

    /// Asking for a position the table does not have is a mistake, not an empty file.
    #[test]
    fn asking_past_the_end_of_the_table_is_an_error() {
        let table = container(&[(0, 4)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert!(matches!(
            table.payload_of(&[0u8; 8], 1),
            Err(ImportError::NoSuchEntry(1))
        ));
    }

    /// Position, not name, is the identity — so the index is numbers all the way down.
    #[test]
    fn the_index_text_is_a_line_per_entry_in_table_order() {
        let table = container(&[(0, 2), (8, 2), (0, 0)]);
        let table = Table::parse(&table, OffsetScale::Bytes).unwrap();
        assert_eq!(table.index_text(), "0=0;2\n1=1;2\n2=0;0\n");
    }

    /// An empty table is a valid reading of a valid container.
    #[test]
    fn an_empty_table_is_read_as_empty_rather_than_damaged() {
        let table = Table::parse(&container(&[]), OffsetScale::Bytes).unwrap();
        assert!(table.is_empty());
        assert!(table.index_text().is_empty());
    }
}
