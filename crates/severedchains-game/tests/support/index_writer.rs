//! The `files/` per-directory index, on the writing side.
//!
//! The cache is a directory tree. Every directory that came out of a container carries one ASCII
//! file named `mrg` with one line per table entry, and the root carries a file named `version`
//! holding `5`. This module produces the first of those. [`read`] is not a function here: the
//! reader is `psxrt_import::cache::Index`, in the other crate.
//!
//! # Why a writer for three punctuation characters is worth a file
//!
//! Because it is the second description of the format, and that is the whole point. The reader and
//! its `text()` counterpart both live in `psxrt-import`, next to each other, which makes a round
//! trip between them a check that a function agrees with its own mirror image — always true, and
//! evidence about nothing. This module is written from the format as the reference describes it
//! (`FileMap.addFile` emitting `name + '=' + target + ';' + size + '\n'`, read back by
//! `Loader.loadDirectorySync` splitting on `[=;]`) rather than from the parser's control flow, and
//! it may not import from `psxrt-import`. Where the two disagree, a test fails.
//!
//! # The format, stated once
//!
//! ```text
//! <slot>=<target>;<declared size>\n
//! ```
//!
//! * `slot` is the position inside the directory, written as a decimal number. Not a name. The
//!   unpacker converts with `Integer.toString(i)` and the reader decodes with
//!   `Integer.parseInt`, so a file that read `armour=armour;12` would throw.
//! * `target` is the position whose bytes this entry uses — the same number for the ordinary case
//!   where an entry owns its own bytes, and somebody else's for a pointer.
//! * `declared size` is the length the *table* claimed, which is zero for a pointer line even
//!   though the bytes it points at have a length.
//! * A blank target means "nothing here at all", which is not the same as a target that points
//!   somewhere. See [`Owner::Nothing`] for why this writer can say it and the reference never does.
//!
//! Keys are indices, and `Loader` resolves a pointer by walking until an entry names itself, which
//! means a chain is expressible: `1=2;0` beside `2=0;8` says slot 1 reads what slot 0 owns.
//!
//! # The one thing the two halves do not share
//!
//! The reference writes one line per resolved entry and simply omits an entry that has no bytes, so
//! its output never contains a gap marker. [`write`] is the same: it takes a dense list and emits
//! one line each, in order, with no filler for positions nobody mentioned. `Index::text()` in the
//! other crate writes `n=;0` into an empty slot instead. Both read back as "no file", so the two
//! are equivalent, and they are not byte-identical, and that is a difference someone should be able
//! to look up rather than rediscover.

/// Who owns the bytes a line describes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Owner {
    /// The entry owns its own bytes. Written as the entry's own index, which is what the unpacker's
    /// `entry.virtual() ? entry.parent() : i` ternary answers for a non-virtual entry.
    Own,
    /// The entry is a pointer to another position in the directory.
    Parent(usize),
    /// The line says nothing owns them.
    ///
    /// `Loader.loadDirectorySync` has a branch for a blank target, so the read side is real. The
    /// write side is not: `Unpacker.java:615` always passes a target, so no cache the toolchain
    /// produces has ever contained this form. It is here so a test can hand the reader its legacy
    /// shape, and it is marked as such rather than being quietly available, because a writer that
    /// can emit what a reader merely tolerates tends to start emitting it.
    Nothing,
}

/// One index line.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Entry {
    /// Whose bytes this is.
    pub owner: Owner,
    /// The length the table declared, which is what the `;` field carries. Zero for a pointer.
    pub declared_size: usize,
}

impl Entry {
    /// An entry that owns `size` bytes of its own.
    #[must_use]
    pub const fn own(declared_size: usize) -> Self {
        Self {
            owner: Owner::Own,
            declared_size,
        }
    }

    /// An entry that reads somebody else's bytes.
    #[must_use]
    pub const fn pointer(parent: usize) -> Self {
        Self {
            owner: Owner::Parent(parent),
            declared_size: 0,
        }
    }
}

/// Name of the per-directory index file, as it is spelled on disk.
pub const INDEX_FILE: &str = "mrg";

/// Name of the root marker, and the only file whose contents are compared before a cache is
/// trusted.
pub const VERSION_FILE: &str = "version";

/// The cache version this writer emits.
///
/// Duplicated from `psxrt_import::cache::VERSION` rather than imported, deliberately: this crate
/// may not depend on the other, and a shared constant would have to be smuggled in through a
/// third crate to be shared at all. What makes the duplication safe is a test, not a comment —
/// `tests/index_round_trip.rs` asserts the two strings are the same, so they can only drift
/// loudly.
pub const VERSION: &str = "5";

/// Write a whole index.
///
/// `entries` is dense: position *n* in the slice is slot *n*, and every slot gets a line. That is
/// how the reference lays them out, in the loop at `Unpacker.java:613-617` that walks the archive
/// a second time after the files have been written.
#[must_use]
pub fn write(entries: &[Entry]) -> String {
    let mut text = String::new();

    for (slot, entry) in entries.iter().enumerate() {
        let target = match entry.owner {
            Owner::Own => slot.to_string(),
            Owner::Parent(parent) => parent.to_string(),
            Owner::Nothing => String::new(),
        };
        text.push_str(&format!("{slot}={target};{}\n", entry.declared_size));
    }

    text
}

/// The contents of the root `version` file: the version on its own, with no trailing newline.
///
/// The reader does not parse this file, it compares it — a cache at any other version is discarded
/// and rebuilt. Keeping the writer's side here means the string a test feeds in and the string a
/// real run would produce are the same string.
#[must_use]
pub fn version_file() -> String {
    VERSION.to_string()
}
