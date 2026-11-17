//! Which MRG tables on this disc count their offsets in sectors, and how a caller says so.
//!
//! [`psxrt_import::container::mrg::Table::parse`] takes an
//! [`OffsetScale`][psxrt_import::container::mrg::OffsetScale] and does not look at filenames. That
//! is not an oversight — an MRG table carries no name and no flag saying how its offsets are to be
//! read, so any reader that *infers* the scale is not reading the format, it is reciting a list
//! that happens to be true of one particular game. This module is that list, held where it can be
//! argued about.
//!
//! # The seven names
//!
//! Seven containers on the Legend of Dragoon discs store their offset column as a *sector number*
//! rather than a byte offset. Nothing about them is detectable from their contents: a table read at
//! the wrong scale does not fail, it returns entries 2048 times too early, and every one of them
//! still lands inside the file. That is why the set is named rather than derived, and why it is
//! matched against a single path segment.
//!
//! # The failure this is guarding
//!
//! Two, and they are different failures.
//!
//! **A name outside the set that is in fact sector-numbered.** It reads at byte scale and every
//! entry lands wrong. This is why the set is closed and named: a rule like "names beginning `DRGN`
//! are sector-aligned" would silently pick up `DRGN2.BIN`, which is not.
//!
//! **A path rather than a segment.** The lookup takes one path segment. Handing it
//! `cdrom:\\SYSTEM\\DRGN0.BIN` or a host path with directories in it answers *byte* scale, which
//! is wrong and quiet. Callers pass a basename; [`offset_scale`] does not try to guess where the
//! name came from, and a caller that strips its own separators has to decide whether `./DRGN0.BIN`
//! and `DRGN0.BIN` are the same container — which, since the answer changes every offset in the
//! table, is a decision worth a line of its own rather than a `split` in a parser.

use psxrt_import::container::mrg::OffsetScale;

/// The container names whose tables count offsets in sectors rather than bytes.
///
/// Closed and enumerated on purpose. A file outside this set that happened to be sector-numbered
/// would be read as byte offsets, and every entry would land 2048 times too early; a file inside it
/// read at byte scale fails the same way in the other direction. Neither is detectable from the
/// table, so the only honest form for this knowledge is a list someone can check against a disc.
pub const SECTOR_ALIGNED_NAMES: [&str; 7] = [
    "DRGN.BIN",
    "DRGN0.BIN",
    "DRGN1.BIN",
    "DRGN21.BIN",
    "DRGN22.BIN",
    "DRGN23.BIN",
    "DRGN24.BIN",
];

/// The offset scale that applies to a container called `name`.
///
/// `name` is one path segment with no separators — a basename, not a path. See the module header
/// for why that distinction is not cosmetic.
///
/// This is the injection point the format crate deliberately leaves open: `psxrt-import` will parse
/// a table at whatever scale it is handed, and this is the function that decides which scale *this
/// game's* discs need.
#[must_use]
pub fn offset_scale(name: &str) -> OffsetScale {
    if SECTOR_ALIGNED_NAMES.contains(&name) {
        OffsetScale::Sectors
    } else {
        OffsetScale::Bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The name set is closed, and a near miss reads at byte scale.
    ///
    /// These four assertions are the whole point of the module. `DRGN2.BIN` exists on the same disc
    /// as `DRGN1.BIN` and is not sector-aligned, which is what makes "starts with DRGN" the wrong
    /// rule and a named set the right one.
    #[test]
    fn only_the_named_containers_count_in_sectors() {
        for name in SECTOR_ALIGNED_NAMES {
            assert_eq!(
                offset_scale(name),
                OffsetScale::Sectors,
                "{name} is in the set and must read at sector scale"
            );
        }

        assert_eq!(offset_scale("DRGN2.BIN"), OffsetScale::Bytes);
        assert_eq!(offset_scale("DRGN0.BINX"), OffsetScale::Bytes);
        assert_eq!(offset_scale("SUBMAP"), OffsetScale::Bytes);
    }

    /// A full path is not a segment, and answering `Bytes` for it is the documented behaviour
    /// rather than an accident. Pinned so that a future refactor which "helpfully" splits the path
    /// here has to change a stated contract and not just a line of code.
    #[test]
    fn a_path_is_not_a_segment() {
        assert_eq!(
            offset_scale(r"cdrom:\SYSTEM\DRGN0.BIN"),
            OffsetScale::Bytes,
            "callers strip the path; this module does not"
        );
    }
}
