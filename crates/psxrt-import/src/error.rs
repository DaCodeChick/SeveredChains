//! The ways a container or codec can refuse to be read.
//!
//! One enum for the whole crate, and every variant carries the numbers that produced it. That
//! matters more here than in the runtime crates: this code runs while somebody is building an
//! asset tree, usually over several hundred files at once, and "malformed input" is not an
//! actionable message when you have to find which of 353 entries said it and which four bytes
//! inside that entry were wrong.
//!
//! ## Failure is the default, panic is not
//!
//! Nothing in this crate panics on bad bytes. Where the reference would have thrown
//! `ArrayIndexOutOfBoundsException` or hung forever, the Rust reader returns the same *failure*
//! as a value, because a tool that unpacks an entire disc must be able to report one bad file
//! and keep going — which is also what the reference's own pipeline wants to do (a transform
//! that fails skips its node rather than aborting the run).
//!
//! Where the reference *silently* produced something, the Rust reader does not quietly agree:
//! `LengthMismatch` is a deliberate narrowing, explained where it is raised.

/// A read that could not be completed.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum ImportError {
    /// The record asked for bytes that are not there.
    ///
    /// `needed` is an absolute length within the payload, not a count of missing bytes, so the
    /// reader can point at the offset where the record stopped making sense.
    #[error("payload ends at {available} bytes but the record reaches {needed}")]
    Truncated {
        /// Offset the record wanted to read up to.
        needed: usize,
        /// Length of the payload it was given.
        available: usize,
    },

    /// A declared decompressed size that cannot be honoured.
    ///
    /// The reference sized its output buffer straight from the first word of the payload, which
    /// means a corrupt header could ask for an arbitrarily large allocation. The bound here is a
    /// native guard, not a format limit: no game file approaches it. See
    /// `container::bpe::MAX_DECOMPRESSED_BYTES`.
    #[error("declared size {declared} is outside the supported range")]
    ImplausibleSize {
        /// The declared size, widened so a value with the sign bit set stays visible as itself.
        declared: u64,
    },

    /// A block header above the codec's per-block ceiling.
    ///
    /// The reference treats this as an error too, and says so in its own log line; the ceiling
    /// is the codec's, not a convention this project added.
    #[error("block declares {declared} decompressed bytes, above the per-block limit of {limit}")]
    OversizedBlock {
        /// The block size the header asked for.
        declared: u32,
        /// The ceiling that was exceeded.
        limit: u32,
    },

    /// A dictionary that runs past its own table.
    ///
    /// A run length is trusted, not re-counted, so a payload can describe more dictionary
    /// entries than the 256-slot table holds. The reference indexes out of its array there;
    /// this is the same failure, as a value.
    #[error("dictionary overruns its 256-slot table at key {key}")]
    MalformedDictionary {
        /// The table index the run tried to write.
        key: usize,
    },

    /// An expansion that would not stop.
    ///
    /// A dictionary is a table of substitutions, and nothing in the format forbids one entry
    /// from naming a byte that another entry expands. The reference has no cycle check and
    /// would grow its working list until it died. The budget here is a native guard; a real
    /// encoded block never gets close, so hitting it means the dictionary is circular.
    #[error("dictionary expansion exceeded its budget of {budget} steps")]
    NonTerminatingDictionary {
        /// The expansion budget that was exhausted.
        budget: usize,
    },

    /// A payload that decoded to a different length than it declared.
    ///
    /// The reference allocates its output from the declared length and then fills it, so a short
    /// decode silently yields trailing zero bytes and a long one indexes past the end. Both are
    /// refusals here. This is the one place this crate knowingly does *not* replicate the
    /// reference, and it does so because the replicated behaviour is the production of garbage:
    /// no original file reaches this branch, since the tool that wrote them emitted exactly the
    /// declared count.
    #[error("decoded {written} bytes where the header declared {declared}")]
    LengthMismatch {
        /// Length the payload's header declared.
        declared: usize,
        /// Length actually produced before the payload ran out.
        written: usize,
    },

    /// A container whose leading word names no format this crate can read.
    #[error("leading word {found:#010x} is not a container or codec this crate reads")]
    UnrecognisedContainer {
        /// The word that was found, read little-endian, as the reference reads it.
        found: u32,
    },

    /// A table entry naming bytes outside the store that holds them.
    #[error("entry {index} names bytes {start}..{end}, outside the {available}-byte store")]
    OutOfBounds {
        /// Index of the offending table entry.
        index: usize,
        /// First byte the entry names.
        start: usize,
        /// One past the last byte the entry names.
        end: usize,
        /// Size of the backing store the entry was resolved against.
        available: usize,
    },

    /// A sector-numbered entry whose byte offset does not fit.
    ///
    /// Sector-aligned tables store a sector *number* where other tables store a byte offset, so
    /// reading one means multiplying by 0x800. The reference multiplies in a 32-bit signed int and
    /// lets it wrap. This does not: a wrapped offset still names a plausible-looking place in the
    /// file, so wrapping turns one corrupt entry into a silently wrong extraction that no later
    /// check would catch.
    ///
    /// On a 64-bit target a `u32 * 0x800` product cannot wrap, so this arm is a portability guard
    /// rather than a live path, and it has no test for that reason. The bounds that *do* bite in
    /// production are caught by [`Self::OutOfBounds`] when the window is read.
    #[error(
        "entry {index} stores sector {sector:#x}, which does not fit a byte offset when scaled by 0x800"
    )]
    OffsetOverflow {
        /// Index of the offending table entry.
        index: usize,
        /// The raw, unscaled field as it was read.
        sector: u32,
    },

    /// A table position the container does not have.
    ///
    /// Distinct from an entry that names no bytes: an out-of-range position is the *caller* having
    /// lost track, and returning empty for it would let a loop that counted wrong quietly produce
    /// empty files.
    #[error("container has no entry {index}")]
    NoSuchEntry {
        /// The position that was asked for.
        index: usize,
    },

    /// A cache index line that is not the three fields `slot=target;size`.
    ///
    /// Reported with the line number and the text, because an index is a hand-inspectable ASCII
    /// file and the person reading this is usually diffing it against a working one.
    #[error("index line {line} is malformed ({text})")]
    MalformedIndex {
        /// Zero-based line number within the index.
        line: usize,
        /// What was wrong, followed by the offending text.
        text: String,
    },

    /// An `#include` that names nothing.
    ///
    /// Reported with the file that asked for it, because an include is resolved against `libs/`
    /// and `scripts/` inside whichever tree is being hashed, and "not in either" is a fact about
    /// that tree rather than about the line. The reference throws `IncludeFailedException` and
    /// takes the whole run down with it.
    #[error("could not resolve include {text} (asked for by {from})")]
    IncludeFailed {
        /// The file whose text contained the directive.
        from: String,
        /// The target as it was written.
        text: String,
    },

    /// A file the patch phase needed and could not find.
    ///
    /// Carries the tree-relative name rather than a resolved path, because the two trees that
    /// could have held it are both named by [`Root`](crate::patch::Root) and the reader has to
    /// work out which one is lying.
    #[error("no file at {text}")]
    MissingSource {
        /// The path that was expected to exist.
        text: String,
    },

    /// A patch body whose name has no extension, so its config sibling cannot be named.
    #[error("patch body {text} has no extension, so its .config.csv cannot be named")]
    PatchConfigName {
        /// The patch body's name.
        text: String,
    },

    /// A cache index whose pointers point at each other, so no slot owns the bytes.
    ///
    /// The reference follows the chain with an unbounded `while` loop and hangs. Returning here
    /// instead is the difference between one bad cache file and a hung tool.
    #[error("index slot {start} is part of a pointer cycle")]
    IndexCycle {
        /// The slot the walk started from.
        start: usize,
    },
}
