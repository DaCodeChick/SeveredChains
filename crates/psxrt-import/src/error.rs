//! The ways a container or codec can refuse to be read.
//!
//! One enum for the crate, and every variant carries the numbers that produced it. That matters
//! more here than in the runtime crates: this code runs while somebody is building an asset tree,
//! usually over several hundred files at once, and "malformed input" is not an actionable message
//! when you have to find which of 353 entries said it and which four bytes inside that entry were
//! wrong.
//!
//! ## Failure is the default, panic is not
//!
//! Nothing in this crate panics on bad bytes. Where the original toolchain would have died on an
//! out-of-range index or hung forever, the reader here returns the same *failure* as a value,
//! because a tool that unpacks an entire disc has to be able to report one bad file and keep
//! going.
//!
//! Where the original silently produced something, the reader here does not quietly agree:
//! [`ImportError::LengthMismatch`] is a deliberate narrowing, explained where it is raised.
//!
//! ## Why some variants are tuples and some are not
//!
//! A variant carrying one meaningful value is a tuple: `MalformedDictionary(257)` says the whole
//! thing, and naming the single field `key` buys a line of documentation that could have gone on
//! the meaning of the number. A variant carrying two or more is named-field, because
//! `OutOfBounds(40, 4, 12, 8)` is not readable at the call site and nothing about a `u32` tells
//! you which of its four positions is the sector number.
//!
//! Every tuple variant below states its positions in the same order as its type, and where two
//! positions share a type the doc names them in order rather than leaving the reader to guess.
//! [`ImportError::Truncated`] is the one tuple that carries two same-typed positions, and it is
//! the reason that rule is written down: `(512, 508)` is unreadable without saying which is which.
//!
//! The errors that are *not* about reading a container — a malformed cache index, an unresolved
//! `#include`, a missing patch source — are not here. They belong to the tooling that meets them
//! and live in `severedchains-game`.

/// A read that could not be completed.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum ImportError {
    /// The record asked for bytes that are not there.
    ///
    /// `(offset the record wanted to read up to, length of the payload it was given)`. The first
    /// is an absolute offset within the payload rather than a count of missing bytes, so the
    /// reader can point at where the record stopped making sense.
    #[error("payload ends at {1} bytes but the record reaches {0}")]
    Truncated(usize, usize),

    /// A declared decompressed size that cannot be honoured.
    ///
    /// The original sized its output buffer straight from the first word of the payload, which
    /// means a corrupt header could ask for an arbitrarily large allocation. The bound here is a
    /// native guard, not a format limit: no game file approaches it. See
    /// [`crate::container::bpe::MAX_DECOMPRESSED_BYTES`].
    ///
    /// Payload: the declared size, widened to `u64` so a value with the sign bit set stays visible
    /// as itself rather than arriving as a negative number.
    #[error("declared size {0} is outside the supported range")]
    ImplausibleSize(u64),

    /// A block header above the codec's per-block ceiling.
    ///
    /// The original treats this as an error too, and says so in its own log line; the ceiling is
    /// the codec's, not a convention this project added.
    #[error("block declares {declared} decompressed bytes, above the per-block limit of {limit}")]
    OversizedBlock {
        /// The block size the header asked for.
        declared: u32,
        /// The ceiling that was exceeded.
        limit: u32,
    },

    /// A dictionary that runs past its own table.
    ///
    /// A run length is trusted, not re-counted, so a payload can describe more dictionary entries
    /// than the 256-slot table holds. The original indexes out of its array there; this is the same
    /// failure, as a value.
    ///
    /// Payload: the table index the run tried to write.
    #[error("dictionary overruns its 256-slot table at key {0}")]
    MalformedDictionary(usize),

    /// An expansion that would not stop.
    ///
    /// A dictionary is a table of substitutions, and nothing in the format forbids one entry from
    /// naming a byte that another entry expands. The original has no cycle check and would grow its
    /// working list until it died. The budget here is a native guard; a real encoded block never
    /// gets close, so hitting it means the dictionary is circular.
    ///
    /// Payload: the expansion budget that was exhausted, not the number of steps taken.
    #[error("dictionary expansion exceeded its budget of {0} steps")]
    NonTerminatingDictionary(usize),

    /// A payload that decoded to a different length than it declared.
    ///
    /// The original allocates its output from the declared length and then fills it, so a short
    /// decode silently yields trailing zero bytes and a long one indexes past the end. Both are
    /// refusals here. This is the one place this crate knowingly does *not* replicate the original,
    /// and it does so because the replicated behaviour is the production of garbage: no original
    /// file reaches this branch, since the tool that wrote them emitted exactly the declared count.
    #[error("decoded {written} bytes where the header declared {declared}")]
    LengthMismatch {
        /// Length the payload's header declared.
        declared: usize,
        /// Length actually produced before the payload ran out.
        written: usize,
    },

    /// A container whose leading word names no format this crate can read.
    ///
    /// Payload: the word that was found, read little-endian — that is, as a container header, not
    /// as a codec marker. A payload that *begins* with these four bytes without being a container
    /// is a real case, and `tests/bpe_round_trip.rs` pins it.
    #[error("leading word {0:#010x} is not a container or codec this crate reads")]
    UnrecognisedContainer(u32),

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
    /// reading one means multiplying by 0x800. The original multiplies in a 32-bit signed int and
    /// lets it wrap. This does not: a wrapped offset still names a plausible-looking place in the
    /// file, so wrapping turns one corrupt entry into a silently wrong extraction that no later check
    /// would catch.
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
    ///
    /// Payload: the position that was asked for. Which container it was asked of is the caller's
    /// knowledge, not the table's, so it is not carried here.
    #[error("container has no entry {0}")]
    NoSuchEntry(usize),
}
