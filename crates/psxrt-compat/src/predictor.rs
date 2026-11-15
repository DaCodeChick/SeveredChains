//! X1, X4, X6, X7 — the compressed audio stream model.
//!
//! Three of these four rows describe a format or a signal path and have nothing to choose. The
//! fourth, X6, is a genuine policy: a piece of state that outlives the file it belongs to.
//!
//! # The one knob, and why it is worth having
//!
//! The predictor state that a compressed audio block decodes against is held in one place and
//! is never reset when a new file starts. The consequence is that roughly three quarters of a
//! millisecond at the head of every file decodes against the previous file's state instead of
//! against silence.
//!
//! That is either a bug or a deliberate continuity trick and nothing in the repository can say
//! which, so it cannot be resolved here — but it can be *named*, and the knob means the day a
//! real asset is available to listen to, the question is a setting and an A/B comparison rather
//! than an archaeology project. The default mirrors the reference, because the impact is bounded
//! and small and because mirroring an observed behaviour is the safer of two unknowable choices.

/// Whether the block predictor carries state across a file boundary.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PredictorReset {
    /// One predictor for the lifetime of the process, shared by every file. What the reference
    /// does, and the default.
    #[default]
    Never,
    /// A fresh predictor at the start of each file.
    PerFile,
}

/// The most predictor state that can leak across one file boundary, in decoded samples.
///
/// The tail of a file and the head of the next one overlap by at most this much, which is why
/// the difference between the two settings is a transient rather than an audible artefact. At
/// the source rate this is a little under three quarters of a millisecond.
pub const MAX_BOUNDARY_LEAK_SAMPLES: usize = 28;

/// The rate the compressed audio streams are authored at, before any host resampling.
pub const SOURCE_RATE_HZ: u32 = 37_800;

/// Bytes in one stream header: the sync word, the header, and the subheader.
pub const HEADER_BYTES: usize = 24;
/// The byte within the header that carries the coding information.
pub const CODING_INFO_OFFSET: usize = 19;
/// Compressed portions per sector, per lane.
pub const PORTIONS_PER_SECTOR: usize = 18;
/// Bytes in one compressed portion.
pub const PORTION_BYTES: usize = 128;
/// Decoded samples produced per sector, per lane.
pub const SAMPLES_PER_SECTOR_LANE: usize = 2_016;

impl PredictorReset {
    /// X1 — the header and portion geometry of a stream.
    pub const RECORD_HEADER: crate::PolicyRecord = crate::PolicyRecord {
        key: "X1",
        rows: &["X1"],
        topic: "stream header and portion geometry",
        source: "research/psx/05-compatibility-classification.md §3.4; research/psx/04-xa-adpcm.md",
        confidence: crate::Confidence::FormatCorroborated,
        layer: crate::Layer::Platform,
        default: "the documented geometry, unchanged",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "Q-23",
        note: "The geometry is corroborated from two directions — the reference decoder's own \
                constants, and the arithmetic that a source rate of 37,800 together with the \
                disc's sector cadence has to produce for the playback rate to come out right. \
                What is not established is the sync-byte sequence, which is external to both \
                the game and the reference. Recorded here so that the constants above are read \
                as corroborated rather than as guessed, and so the sync bytes are not mistaken \
                for part of the same confidence class.",
        closure: "External documentation of the sector sync bytes.",
    };

    /// X4 — how full-motion-video audio reaches the output.
    pub const RECORD_FMV_GAIN: crate::PolicyRecord = crate::PolicyRecord {
        key: "X4",
        rows: &["X4"],
        topic: "native-rate decode, the two-stage gain, and backpressure",
        source: "research/psx/05-compatibility-classification.md §3.4; research/audio/03-xa-opus-audio.md",
        confidence: crate::Confidence::ExperimentallyVerified,
        layer: crate::Layer::Platform,
        default: "native-rate decode, the documented gain chain, drop when the buffer is full",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "Q-48, Q-79",
        note: "Three separate decisions are bundled in this row and only two are platform \
                behaviour. Decoding at the authored rate rather than resampling, and the gain \
                applied as a truncating halving followed by a float multiply of two independent \
                volume settings, are both observed and both reproduce exactly. Dropping samples \
                when the buffer fills is neither: it is what the reference does because it has \
                to do something, and it is recorded as layer-4 behaviour. The unresolved part \
                is the rate flag: a stream marked at half the rate would play at twice the \
                speed under this model, and whether any shipped stream is so marked is an asset \
                question.",
        closure: "A retail asset census of rate flags; the backpressure behaviour needs no \
                  closure because it is not claimed to be original.",
    };

    /// X6 — the shared predictor. This is the knob.
    pub const RECORD_PREDICTOR: crate::PolicyRecord = crate::PolicyRecord {
        key: "X6",
        rows: &["X6"],
        topic: "predictor state shared across file boundaries",
        source: "research/psx/05-compatibility-classification.md §3.4; research/audio/03-xa-opus-audio.md",
        confidence: crate::Confidence::Unresolved,
        layer: crate::Layer::ReferenceImplementation,
        default: "Never — the reference's behaviour, with the alternative one setting away",
        availability: crate::Availability::RecordedOnly,
        knob: "predictor_reset",
        register: "Q-69",
        note: "Undecidable from inside the repository: a shared predictor is either an oversight \
                or a deliberate attempt to keep music continuous across a file seam, and the \
                code does not say. The impact is bounded at about three quarters of a \
                millisecond per boundary, on a single consumer thread, so it is not a race and \
                not a glitch — it is a slightly wrong first block. Mirrored because the impact \
                is small, because a per-file reset would be an unevidenced improvement, and \
                because the knob costs nothing. Note that a single stream per file is the \
                normal case for this content, so the number of boundaries is small.",
        closure: "Listening to the first block of a real file under both settings, against the \
                  original.",
    };

    /// X7 — how a mono stream is laid out, and how many streams a file has.
    pub const RECORD_MONO_LANES: crate::PolicyRecord = crate::PolicyRecord {
        key: "X7",
        rows: &["X7"],
        topic: "mono streams as two lanes, and the number of streams per file",
        source: "research/psx/05-compatibility-classification.md §3.4; research/audio/03-xa-opus-audio.md",
        confidence: crate::Confidence::ExperimentallyVerified,
        layer: crate::Layer::Platform,
        default: "two lanes for mono, one stream per video file",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "Q-69",
        note: "A mono stream is not one lane: it occupies two, with the second carrying its own \
                predictor and the first duplicated. Whether a file carries one stream or several \
                is a separate rule that differs between the two kinds of audio content the game \
                has, and getting it wrong shifts every subsequent file's predictor — which is \
                the mechanism above. The two rows are one story and are kept adjacent for that \
                reason.",
        closure: "A retail asset census of lane counts and stream counts.",
    };
}
