//! S3 — how a note finds a voice when every voice is busy.
//!
//! This is a record, not a knob. The behaviour is established rather than assumed: the
//! allocation routine exists in the reference, its four steps can be read, and its two capacity
//! numbers appear in two initialisation paths. Nothing here is a preference.
//!
//! # Why it is recorded at all
//!
//! Because the first reading of this subsystem, written before the routine was read, said the
//! opposite. An earlier document described voice selection as one-to-one and static, with no
//! stealing. It was wrong, and the correction is worth keeping: a static reading of a dynamic
//! allocator does not merely describe fewer behaviours, it describes a *different* audible
//! outcome, because the difference between "no voice available, drop the note" and "no voice
//! available, interrupt whichever one a rule picks" is hearable as a missing note rather than as
//! a replaced one.

/// How many voices a note may take over, before notes start being dropped instead.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VoiceCapacity {
    /// The capacity in force after the audio processor is brought up.
    pub after_initial_setup: u32,
    /// The capacity in force after the sound system has been configured, which is lower.
    pub after_sound_setup: u32,
}

impl Default for VoiceCapacity {
    fn default() -> Self {
        Self {
            after_initial_setup: 20,
            after_sound_setup: 8,
        }
    }
}

impl VoiceCapacity {
    /// S3 — the selection rule and the two capacities.
    pub const RECORD: crate::PolicyRecord = crate::PolicyRecord {
        key: "S3",
        rows: &["S3"],
        topic: "voice selection when all voices are busy",
        source: "research/psx/05-compatibility-classification.md §3.3; research/psx/03-spu.md §1 and §7",
        confidence: crate::Confidence::ExperimentallyVerified,
        layer: crate::Layer::GameContent,
        default: "the reference's four-step rule, with its two capacities",
        availability: crate::Availability::RecordedOnly,
        knob: "",
        register: "Q-68",
        note: "Selection is dynamic and runs in four steps: a voice already playing the same \
                note is restarted; otherwise a bounded priority takeover is attempted; otherwise \
                a round-robin scan looks for a free voice; otherwise a finished or unused voice \
                is used. When the bound is exhausted the note is dropped rather than \
                redirected, and that is the observable part. The bound itself changes during \
                start-up, from twenty down to eight, and both numbers are load-bearing: a model \
                that keeps twenty gives audible voices to notes the reference discards. This \
                row supersedes an earlier description of the same subsystem as one-to-one and \
                static — that reading predates the routine being read, and it is kept here \
                rather than deleted so the correction cannot silently reverse.",
        closure: "The four steps are read and settled. What remains open is whether the \
                original behaved the same way, and whether shipped content ever exhausts the \
                bound — both asset and documentation questions.",
    };

    /// The capacity in force at a given point of start-up.
    #[must_use]
    pub const fn bound(self, phase: Phase) -> u32 {
        match phase {
            Phase::AfterInitialSetup => self.after_initial_setup,
            Phase::AfterSoundSetup => self.after_sound_setup,
        }
    }
}

/// The point in start-up that the capacity depends on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Phase {
    /// The audio processor has been brought up; the higher capacity is in force.
    AfterInitialSetup,
    /// The sound system has been configured; the lower capacity is in force from here on.
    AfterSoundSetup,
}
