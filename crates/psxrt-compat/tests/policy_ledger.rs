//! Mechanical checks over the compatibility ledger.
//!
//! The ledger in [`psxrt_compat`] exists so that an open question stays visible instead of
//! turning into a silent branch. That only holds if the records stay honest, and prose cannot
//! hold. Three things are therefore verified here rather than asserted in a doc comment:
//!
//! * every record states an actual divergence and an actual way to close it, so that "recorded"
//!   does not quietly come to mean "noted and dropped";
//! * every citation resolves — checklist rows exist in `research/cross/02`, questions exist in
//!   `research/04`, and documents named as evidence exist in the tree. A citation that no longer
//!   points at anything is worse than no citation, because it reads like one;
//! * nothing claims a validation this project has not performed. There are no retail bytes in
//!   this workspace, so no record may be written as though any had been inspected, and no entry
//!   may carry a source-verified claim about the original hardware, which would turn a policy
//!   into a fact.
//!
//! Together these are the ledger's equivalent of a build: change a record without keeping its
//! evidence, and something here fails.

use std::path::{Path, PathBuf};

use psxrt_compat::{CompatPolicy, Confidence, GtePrecision, Layer, LEDGER, PolicyRecord,
                   PredictorReset, SameZOrder, SweepEnvelope};

/// The consolidated compatibility checklist that the ledger's row citations point into.
const CHECKLIST: &str = include_str!("../../../research/cross/02-compatibility-checklist.md");

/// The open-question register that the ledger's `register` citations point into.
const REGISTER: &str = include_str!("../../../research/04-unresolved-questions.md");

fn record(key: &str) -> &'static PolicyRecord {
    LEDGER
        .iter()
        .find(|entry| entry.key == key)
        .unwrap_or_else(|| panic!("no ledger record for {key}"))
}

/// The workspace root, for resolving document citations written as repository-relative paths.
fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("the crate lives two levels below the workspace root")
        .to_path_buf()
}

// ---------------------------------------------------------------------------
// A record has to say something
// ---------------------------------------------------------------------------

/// Every record states a divergence and a closure condition, in enough words to act on.
///
/// The length floors are not a quality judgement; they are what it takes to say "the reference
/// does A, the platform plausibly did B, and here is what would settle it" in one sentence each.
/// A record that drops to a bare label has stopped carrying information, and the ledger's whole
/// purpose is that a later reader inherits the question rather than rediscovering it.
#[test]
fn every_record_states_a_divergence_and_a_way_to_close_it() {
    let mut thin = Vec::new();
    let mut uncloseable = Vec::new();

    for entry in LEDGER {
        if entry.topic.len() < 12 || entry.default.len() < 8 {
            thin.push(format!("{}: topic or default is a bare label", entry.key));
        }
        if entry.note.len() < 60 {
            thin.push(format!("{}: states no divergence ({} bytes)", entry.key, entry.note.len()));
        }
        if entry.closure.len() < 25 {
            thin.push(format!("{}: states no closure condition", entry.key));
        }
        assert!(entry.closure.ends_with('.'), "{}: closure is not a sentence", entry.key);
        assert_ne!(entry.closure, entry.note, "{}: closure repeats the note", entry.key);

        // A closure condition either names the class of evidence that would settle the question
        // or states plainly that none is outstanding. Anything else is a note with an arrow
        // pointing nowhere.
        const EVIDENCE: &[&str] = &[
            "retail", "external", "asset", "documentation", "emulator", "comparison", "census",
            "renderer", "visual", "bytes", "disc", "capture", "recorded against", "listen",
            "against the original",
        ];
        let names_evidence = EVIDENCE.iter().any(|needle| entry.closure.contains(needle));
        let closes_explicitly = entry.closure.contains("No open question");
        if !(names_evidence || closes_explicitly) {
            uncloseable.push(format!("{}: {}", entry.key, entry.closure));
        }
    }

    assert!(thin.is_empty(), "records that no longer carry information:\n{}", thin.join("\n"));
    assert!(
        uncloseable.is_empty(),
        "records with no stated way to close them:\n{}",
        uncloseable.join("\n")
    );
}

/// An unresolved claim about the original platform is tracked in the register, not just noted.
///
/// This is the difference between the ledger and a comment. `Unresolved` or `Inferred` about the
/// platform with no register item behind it is a divergence that nobody will ever be in a
/// position to answer, and it reads in the ledger exactly like one that could be. The records
/// that legitimately have no register item are the ones whose confidence class already points at
/// how they were established — the four blend and transform rows whose equations came out of
/// running the reference, and the origin/latency rows that are reference conventions rather than
/// platform claims.
#[test]
fn an_unresolved_platform_claim_is_tracked_somewhere() {
    let untracked: Vec<String> = LEDGER
        .iter()
        .filter(|entry| entry.layer == Layer::Platform)
        .filter(|entry| {
            matches!(entry.confidence, Confidence::Inferred | Confidence::Unresolved)
        })
        .filter(|entry| entry.register.is_empty())
        .map(|entry| format!("{} ({:?})", entry.key, entry.confidence))
        .collect();
    assert!(untracked.is_empty(), "platform claims with nowhere to go: {untracked:?}");
}

// ---------------------------------------------------------------------------
// Citations have to resolve
// ---------------------------------------------------------------------------

/// Checklist rows, register items and document paths all point at something that exists.
///
/// Run against the real documents rather than a copy: the checklist is the authority for which
/// compatibility rows there are, and the register is the authority for which questions are open.
/// A ledger record that cites a row nobody wrote, or a question that closed and was renumbered,
/// is a record whose evidence has quietly gone.
#[test]
fn every_citation_resolves() {
    let root = workspace_root();
    let mut broken = Vec::new();

    for entry in LEDGER {
        for row in entry.rows {
            let needle = format!("| {row} ");
            if !CHECKLIST.lines().any(|line| line.starts_with(&needle)) {
                broken.push(format!("{}: row {row} is not a row of the checklist", entry.key));
            }
        }

        for token in entry.register.replace(',', " ").split_whitespace() {
            if !token.starts_with("Q-") {
                broken.push(format!("{}: {token:?} is not a register item", entry.key));
                continue;
            }
            if !REGISTER.contains(token) {
                broken.push(format!("{}: {token} is not in the register", entry.key));
            }
        }

        for path in document_paths(entry.source) {
            if !root.join(&path).is_file() {
                broken.push(format!("{}: {path} does not exist", entry.key));
            }
        }
    }

    assert!(broken.is_empty(), "citations that no longer point at anything:\n{}", broken.join("\n"));
}

/// Pull the repository-relative document paths out of a citation string.
///
/// Only `research/…` paths are taken. A citation may also name a file in the reference
/// implementation, and that repository is not part of this tree and must not become a build
/// dependency of it.
fn document_paths(citation: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut index = 0;
    while let Some(offset) = citation[index..].find("research/") {
        let start = index + offset;
        let end = citation[start..]
            .find(|c: char| !(c.is_ascii_alphanumeric() || matches!(c, '/' | '_' | '.' | '-')))
            .map_or(citation.len(), |length| start + length);
        let path = citation[start..end].trim_end_matches(['.', ' ']);
        if path.ends_with(".md") {
            out.push(path.to_string());
        }
        index = end.max(start + 1);
    }
    out
}

// ---------------------------------------------------------------------------
// Nothing may claim a validation that has not happened
// ---------------------------------------------------------------------------

/// No record claims retail-byte validation, and none carries a source-verified claim about the
/// original machine.
///
/// The project's evidence rules allow four classes above a bare inference, and one of them —
/// asset-validated — requires real disc bytes, which this workspace does not have and will not
/// download. The other side of the same rule is `SourceVerified`: used here it would mean the
/// original's behaviour had been read rather than reasoned about, which for every entry in this
/// ledger is not the case. Both are checked as text and as data, because a record can be written
/// into either.
#[test]
fn no_record_claims_a_validation_the_project_does_not_have() {
    const FORBIDDEN: &[&str] = &["asset-validated", "asset validated", "verified against retail"];

    let mut claims = Vec::new();
    for entry in LEDGER {
        if matches!(entry.confidence, Confidence::SourceVerified) {
            claims.push(format!("{}: claims a source-verified original", entry.key));
        }
        for field in [entry.topic, entry.source, entry.default, entry.note, entry.closure] {
            for needle in FORBIDDEN {
                let lower = field.to_lowercase();
                if lower.contains(needle) && !lower.contains(&format!("not {needle}")) {
                    claims.push(format!("{}: claims {needle:?}", entry.key));
                }
            }
        }
    }
    assert!(claims.is_empty(), "records that overstate their evidence:\n{}", claims.join("\n"));
}

// ---------------------------------------------------------------------------
// The ledger and the struct have to agree
// ---------------------------------------------------------------------------

/// The knobs named by the ledger are exactly the fields of [`CompatPolicy`], and no more.
///
/// Twelve of the sixteen records deliberately have no knob: their behaviour is fixed at the
/// default, and the record explains why there is nothing to choose. A thirteenth knob would have
/// to be a real choice to be worth its testing cost, so the count is pinned rather than
/// reviewed. If a field is added to the struct without a record, or a record names a knob the
/// struct does not have, this fails and the mismatch is the bug.
#[test]
fn the_knob_surface_is_exactly_the_fields_of_the_policy_struct() {
    let knobs: Vec<&str> = LEDGER.iter().map(|entry| entry.knob).filter(|k| !k.is_empty()).collect();
    let mut sorted = knobs.clone();
    sorted.sort_unstable();
    assert_eq!(
        sorted,
        vec!["gte_precision", "predictor_reset", "same_z_order", "sweep_envelope"],
        "the ledger's knobs and the policy struct's fields have come apart"
    );
    assert_eq!(knobs.len(), 4, "one knob per field of CompatPolicy");
}

/// The shipped defaults are the defaults the ledger says are shipped, and the reference-faithful
/// configuration differs from them in exactly one place.
///
/// The single difference is the sweep envelope, where the reference has an unfinished
/// implementation rather than a behaviour that could be faithfully reproduced. Pinning the count
/// is the point: four fields, one divergence, and a reader who can find it.
#[test]
fn the_shipped_defaults_are_the_ones_the_ledger_describes() {
    let shipped = CompatPolicy::default();
    let fidelity = CompatPolicy::reference_fidelity();

    // Each default is what its record says it is, in the record's own words.
    assert_eq!(shipped.same_z_order, SameZOrder::Lifo);
    assert!(record("G6").default.contains("Lifo"));
    assert_eq!(shipped.gte, GtePrecision::Float);
    assert!(record("T2").default.contains("Float"));
    assert_eq!(shipped.predictor_reset, PredictorReset::Never);
    assert!(record("X6").default.contains("Never"));
    assert_eq!(shipped.sweep, SweepEnvelope::Applied);
    assert!(record("S7").default.contains("Applied"));

    let mut divergent = Vec::new();
    if shipped.same_z_order != fidelity.same_z_order {
        divergent.push("same_z_order");
    }
    if shipped.gte != fidelity.gte {
        divergent.push("gte");
    }
    if shipped.predictor_reset != fidelity.predictor_reset {
        divergent.push("predictor_reset");
    }
    if shipped.sweep != fidelity.sweep {
        divergent.push("sweep");
    }
    assert_eq!(divergent, vec!["sweep"], "the crate doc names exactly one divergence");
    assert_eq!(fidelity.sweep, SweepEnvelope::FullVolume);
}

/// The ledger is one record per compatibility row, and its size is stated rather than assumed.
///
/// Rows are unique across records by design: a mechanism observed twice (blend equations and the
/// mask that reads their result, for instance) is recorded as two rows because the checklist
/// lists them as two, not merged into one record whose note would have to cover both.
#[test]
fn the_ledger_is_a_complete_and_unduplicated_census() {
    assert_eq!(LEDGER.len(), 16, "the ledger's size is quoted in the crate documentation");

    let mut keys: Vec<&str> = LEDGER.iter().map(|entry| entry.key).collect();
    keys.sort_unstable();
    let duplicates: Vec<&str> = keys
        .windows(2)
        .filter(|pair| pair[0] == pair[1])
        .map(|pair| pair[0])
        .collect();
    assert!(duplicates.is_empty(), "a row appears twice: {duplicates:?}");

    let rows: Vec<&str> = LEDGER.iter().flat_map(|entry| entry.rows).copied().collect();
    assert_eq!(rows.len(), rows.iter().collect::<std::collections::BTreeSet<_>>().len());

    // Every record answers to at least one row, and the row it names is its own key, so a
    // reader who has the key can find the evidence without a second index.
    for entry in LEDGER {
        assert_eq!(entry.rows, &[entry.key], "{} cites rows it is not", entry.key);
    }
}
