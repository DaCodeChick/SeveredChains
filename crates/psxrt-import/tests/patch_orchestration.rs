//! The patch phase's contract: which entries get touched, from what bytes, and how often.
//!
//! These tests are about *when a file is rewritten*, which is the part of the patch pipeline that is
//! finished here. The rewriting itself is `Toolchain`, and it ships as [`RecordingToolchain`]. That
//! split is the point: a toolchain that hands its input back is enough to prove that a second run
//! issues no calls at all, and it keeps the assertions about the gate rather than about script
//! assembly.
//!
//! # The two shapes a test can have
//!
//! The gate only measures an entry that already has a cached counterpart. With nothing cached every
//! entry is new and is applied without being hashed — which is what the reference does, and is the
//! right default. So a test about *re-running* needs a settled tree to start from, and gets it from
//! [`fixture`]; a test about the *first* run uses [`fixture_unpatched`].
//!
//! The fixture is small on purpose — one diff, one replacement, one dependency that both of them
//! `#include`. Three hundred and fifty-three entries exercise the same branches.

use psxrt_import::patch::{
    self, DiskWorkspace, MemWorkspace, PatchList, PatchType, RecordingToolchain, Root, Workspace,
};

/// A patch set with one shared dependency. The quoted descriptions carry a comma, which is what
/// every one of the real file's 353 rows does.
const LIST: &str = concat!(
    "diff,SECT/DRGN1.BIN/20,scripts/DRGN1/20.diff,\"Lizardman deallocate, leaked state\"\n",
    "replacement,throw_item,scripts/throw_item.txt,\"Handles throwing retail items\"\n",
);

/// The same set with the second entry removed.
const ONE: &str = "diff,SECT/DRGN1.BIN/20,scripts/DRGN1/20.diff,\"x\"\n";

fn fixture_into(ws: &mut impl Workspace) {
    ws.write(Root::Files, "SECT/DRGN1.BIN/20", b"original-twenty");
    ws.write(Root::Files, "throw_item", b"original-throw");
    ws.write(
        Root::Patches,
        "libs/char_utils.fate",
        b"dependency version 1",
    );
    ws.write(
        Root::Patches,
        "scripts/DRGN1/20.diff",
        b"+#include char_utils.fate\n-old line\n+new line\n",
    );
    ws.write(
        Root::Patches,
        "scripts/throw_item.txt",
        b"#include char_utils.fate\nthrow_item:\n",
    );
    ws.write(Root::Patches, "scripts.csv", LIST);
}

/// A settled tree: one run over the fixture, with the recorded calls cleared so that each test
/// measures only what its own change causes.
fn fixture() -> (MemWorkspace, RecordingToolchain) {
    let (mut ws, mut toolchain) = fixture_unpatched();
    patch::apply(&mut ws, &mut toolchain).expect("first run");
    toolchain.calls.clear();
    (ws, toolchain)
}

fn fixture_unpatched() -> (MemWorkspace, RecordingToolchain) {
    let mut ws = MemWorkspace::new();
    fixture_into(&mut ws);
    (ws, RecordingToolchain::new())
}

// ---------------------------------------------------------------------------------------------
// Idempotence: the property the whole design is arranged around.
// ---------------------------------------------------------------------------------------------

#[test]
fn a_fresh_tree_patches_everything_and_backs_everything_up_first() {
    let (mut ws, mut toolchain) = fixture_unpatched();

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("fresh run");

    assert_eq!(
        outcome.applied,
        ["SECT/DRGN1.BIN/20".to_string(), "throw_item".to_string()],
        "both entries are new, so both are done, in file order"
    );
    assert!(outcome.refreshed, "the cache is rebuilt after any work");
    assert!(
        outcome
            .warnings
            .iter()
            .any(|w| w.contains("Cache has no scripts.csv")),
        "a first run has nothing to compare against, and says so rather than guessing: {:?}",
        outcome.warnings
    );
    assert!(
        ws.exists(Root::Backups, "SECT/DRGN1.BIN/20") && ws.exists(Root::Backups, "throw_item"),
        "the pristine copy is taken before the rewrite, not after"
    );
    assert_eq!(
        ws.read(Root::Backups, "SECT/DRGN1.BIN/20").unwrap(),
        b"original-twenty"
    );
}

#[test]
fn an_unchanged_repatch_touches_nothing() {
    let (mut ws, mut toolchain) = fixture_unpatched();

    patch::apply(&mut ws, &mut toolchain).expect("first run");
    let after_first = toolchain.calls.len();

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second run");

    assert!(
        outcome.is_noop(),
        "the patch tree did not change, so nothing may: {outcome:?}"
    );
    assert_eq!(
        toolchain.calls.len(),
        after_first,
        "idempotence means the second run issues no work at all, not work that happens to land on \
         the same bytes"
    );
    assert!(
        outcome.warnings.is_empty(),
        "a populated cache should not warn: {:?}",
        outcome.warnings
    );
}

/// The second half of self-healing, and the half that is easy to get wrong.
///
/// A rebuild that read the *current* bytes would be patching an already-patched file, which is a
/// different operation from patching the original and would not land on the same answer. Starting
/// from the backup is what makes recovery converge instead of drift.
#[test]
fn a_tampered_source_is_rebuilt_from_the_backup_not_from_itself() {
    let (mut ws, mut toolchain) = fixture();

    // Someone edits a live file, and the cache copy of one patch body drifts.
    ws.write(Root::Files, "SECT/DRGN1.BIN/20", b"hand edited, wrong");
    ws.write(
        Root::Cache,
        "scripts/DRGN1/20.diff",
        b"+#include char_utils.fate\n",
    );

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("recovery run");

    assert_eq!(
        outcome.applied,
        ["SECT/DRGN1.BIN/20".to_string()],
        "only the entry whose closure moved is redone"
    );
    assert_eq!(
        ws.read(Root::Files, "SECT/DRGN1.BIN/20").unwrap(),
        b"original-twenty",
        "re-applied over the pristine backup, so the manual edit is gone rather than compounded"
    );
}

// ---------------------------------------------------------------------------------------------
// The dependency gate.
// ---------------------------------------------------------------------------------------------

/// Why the gate hashes a closure rather than a file: an entry is stale when anything it reads is
/// stale. `1.config.csv` in the real patch set declares a table length shared between several
/// scripts, and an entry whose shared dependency went stale while its own body sat untouched is
/// exactly the case a per-file check would skip.
#[test]
fn a_dependency_that_moves_re_triggers_every_entry_that_reads_it() {
    let (mut ws, mut toolchain) = fixture();

    ws.write(
        Root::Patches,
        "libs/char_utils.fate",
        b"dependency version 2",
    );

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second run");

    assert_eq!(
        outcome.applied,
        ["SECT/DRGN1.BIN/20".to_string(), "throw_item".to_string()],
        "both bodies include the shared file"
    );
}

#[test]
fn a_config_sibling_is_part_of_the_closure() {
    let (mut ws, mut toolchain) = fixture();

    // Added, not edited. The first run had no config at all, so this also shows that absent and
    // absent agreed: the entry was skipped before for the right reason, not because nothing was
    // measured.
    ws.write(
        Root::Patches,
        "scripts/DRGN1/20.config.csv",
        b"table_length, 17cc, 1\n",
    );

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second run");

    assert_eq!(outcome.applied, ["SECT/DRGN1.BIN/20".to_string()]);
    assert!(
        !outcome.warnings.iter().any(|w| w.contains("config")),
        "a config appearing is a dependency change, not a complaint: {:?}",
        outcome.warnings
    );
}

#[test]
fn an_include_resolves_against_libs_then_scripts_and_libs_wins() {
    let (mut ws, mut toolchain) = fixture();
    // Both trees answer to `char_utils.fate`; the shallower one is the dependency and the other is
    // scenery. This is the real arrangement — `scripts/char_utils.txt` shadows nothing.
    ws.write(Root::Patches, "scripts/char_utils.fate", b"shadowing copy");

    patch::apply(&mut ws, &mut toolchain).expect("first pass");
    toolchain.calls.clear();

    // Editing the loser of the lookup must not look like a change.
    ws.write(
        Root::Patches,
        "scripts/char_utils.fate",
        b"shadowing copy, changed",
    );
    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second pass");

    assert!(
        outcome.is_noop(),
        "scripts/char_utils.fate is shadowed by libs/char_utils.fate, so editing it moves nobody's \
         closure: {outcome:?}"
    );
}

#[test]
fn an_unresolvable_include_stops_the_run() {
    let (mut ws, mut toolchain) = fixture();
    ws.write(
        Root::Patches,
        "scripts/DRGN1/20.diff",
        b"+#include no_such_file.txt\n",
    );

    let error =
        patch::apply(&mut ws, &mut toolchain).expect_err("an include that resolves to nothing");

    assert!(
        error.to_string().contains("no_such_file.txt"),
        "the message has to name the thing that could not be found, got: {error}"
    );
    assert!(
        toolchain.calls.is_empty(),
        "a run that cannot measure one entry must not go on to half-apply the others"
    );
}

/// Cycles are possible, because the directive is not restricted to going downward: two files can
/// name each other. A walk without a visited set stops by running out of stack instead.
#[test]
fn mutual_includes_terminate() {
    let (mut ws, mut toolchain) = fixture();
    ws.write(
        Root::Patches,
        "scripts/throw_item.txt",
        b"#include char_utils.fate\n",
    );
    ws.write(
        Root::Patches,
        "libs/char_utils.fate",
        b"#include throw_item.txt\n",
    );

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("no infinite descent");
    assert_eq!(
        outcome.applied,
        ["SECT/DRGN1.BIN/20".to_string(), "throw_item".to_string()],
        "both bodies sit somewhere in the cycle, and both are redone — the point is that the walk          halts and reports, rather than recursing until the stack gives out"
    );
}

// ---------------------------------------------------------------------------------------------
// Absence, and what counts as agreeing with it.
// ---------------------------------------------------------------------------------------------

#[test]
fn a_tree_with_no_patch_list_does_nothing_and_is_not_an_error() {
    let mut ws = MemWorkspace::new();
    ws.put(Root::Files, "SECT/DRGN1.BIN/20", b"original-twenty");
    let mut toolchain = RecordingToolchain::new();

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("no patch set is a valid state");

    assert!(outcome.is_noop());
    assert_eq!(outcome.warnings.len(), 2, "one per tree that had no list");
}

#[test]
fn the_closure_is_hashed_and_records_absence() {
    let mut ws = MemWorkspace::new();
    ws.put(Root::Patches, "scripts/probe.txt", b"123456789");
    let entry = patch::ScriptPatch {
        kind: PatchType::Diff,
        source: "probe".to_string(),
        patch: "scripts/probe.txt".to_string(),
    };

    let closure = patch::closure_for(&ws, Root::Patches, &entry).expect("hashable");

    // The check value for the reflected CRC-32 that `java.util.zip.CRC32` implements, taken from
    // the same catalogue as the format itself rather than from this implementation — which is the
    // only reason the number means anything.
    assert_eq!(closure.get("scripts/probe.txt"), Some(&Some(0xCBF4_3926)));
    assert_eq!(
        closure.get("scripts/probe.config.csv"),
        Some(&None),
        "a missing config is recorded as absent, not omitted"
    );
    assert_eq!(
        closure.len(),
        2,
        "the body and its config, and nothing else"
    );
}

#[test]
fn an_include_reaches_the_closure_and_an_unresolvable_one_is_reported() {
    let mut ws = MemWorkspace::new();
    ws.put(Root::Patches, "scripts/a.txt", b"#include dep.txt\n");
    ws.put(Root::Patches, "libs/dep.txt", b"dependency");
    let entry = patch::ScriptPatch {
        kind: PatchType::Replacement,
        source: "a".to_string(),
        patch: "scripts/a.txt".to_string(),
    };

    let closure = patch::closure_for(&ws, Root::Patches, &entry).expect("resolvable");
    assert_eq!(
        closure.keys().collect::<Vec<_>>(),
        ["libs/dep.txt", "scripts/a.config.csv", "scripts/a.txt"],
        "the include is resolved into the tree, `libs/` answers first, and the config sibling is a \
         key even though nothing answers to it"
    );

    ws.write(Root::Patches, "scripts/a.txt", "#include gone.txt\n");
    let error =
        patch::closure_for(&ws, Root::Patches, &entry).expect_err("nothing answers to gone.txt");
    assert!(error.to_string().contains("gone.txt"), "{error}");
}

// ---------------------------------------------------------------------------------------------
// Removal.
// ---------------------------------------------------------------------------------------------

#[test]
fn a_removed_entry_is_reverted_from_the_backup() {
    let (mut ws, mut toolchain) = fixture();

    // Drop the second row from the live tree only. The cache still remembers it, because the cache
    // is a copy of the tree as it stood at the end of the previous run.
    ws.write(Root::Patches, "scripts.csv", ONE);
    ws.write(Root::Files, "throw_item", b"previously patched");

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second run");

    assert!(outcome.applied.is_empty(), "{:?}", outcome.applied);
    assert_eq!(outcome.restored, ["throw_item".to_string()]);
    assert!(
        !ws.exists(Root::Backups, "throw_item"),
        "the backup is consumed by the revert, so re-adding the entry re-backs-up rather than \
         reverting to a copy that has since gone stale"
    );
    assert_eq!(
        ws.read(Root::Files, "throw_item").unwrap(),
        b"original-throw",
        "the file goes back to what it was before any patch touched it"
    );
    assert!(outcome.refreshed);

    // And the third run is quiet, which is what the cache refresh is for.
    let outcome = patch::apply(&mut ws, &mut toolchain).expect("third run");
    assert!(outcome.is_noop(), "{outcome:?}");
}

#[test]
fn a_reverted_file_with_no_backup_is_an_error_not_a_deletion() {
    let (mut ws, mut toolchain) = fixture();

    ws.remove(Root::Backups, "throw_item");
    ws.write(Root::Patches, "scripts.csv", ONE);

    let error = patch::apply(&mut ws, &mut toolchain).expect_err("nothing to restore from");
    assert!(error.to_string().contains("throw_item"), "{error}");
    assert!(
        ws.exists(Root::Files, "throw_item"),
        "a failed revert must not remove the file it could not restore"
    );
}

// ---------------------------------------------------------------------------------------------
// Reading the list.
// ---------------------------------------------------------------------------------------------

#[test]
fn a_malformed_row_discards_the_whole_list() {
    let (mut ws, mut toolchain) = fixture_unpatched();
    // Row 1 is well formed, row 2 has one field and names nothing.
    ws.write(Root::Patches, "scripts.csv", "diff,a,b,\"ok\"\nbogus\n");

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("a bad list is handled");

    assert!(
        outcome.is_noop(),
        "nothing may be applied, because half a patch set is worse than none"
    );
    assert!(
        !ws.exists(Root::Backups, "SECT/DRGN1.BIN/20"),
        "the untouched files stay untouched, backups included"
    );
    assert!(
        outcome
            .warnings
            .iter()
            .any(|w| w.starts_with("bad patch list")),
        "{:?}",
        outcome.warnings
    );
}

#[test]
fn a_quoted_description_with_a_comma_does_not_shift_the_fields() {
    let list = PatchList::parse(LIST, |_| false).expect("two well-formed rows");

    assert_eq!(list.len(), 2);
    let second = list.get("throw_item").expect("keyed by source");
    assert_eq!(second.kind, PatchType::Replacement);
    assert_eq!(second.patch, "scripts/throw_item.txt");
}

#[test]
fn duplicate_sources_keep_the_last_row_at_the_first_position() {
    let text = "diff,x,a,\"first\"\ndiff,x,b,\"second\"\n";
    let list = PatchList::parse(text, |_| false).expect("two rows, one source");

    assert_eq!(
        list.len(),
        1,
        "the key is the source, and the later row wins"
    );
    assert_eq!(list.iter().next().unwrap().patch, "b");
}

/// The hazard, made visible.
///
/// Whether a row is legacy or modern is decided by whether field 0 names something in `files/`, and
/// `files/` is never empty. This is not an argument that the behaviour is good — it is a test, so
/// that if the Rust side ever *stops* replicating it, the difference shows up as a failing build
/// rather than as a quiet divergence from the tool being reimplemented.
#[test]
fn legacy_shape_is_chosen_by_what_is_on_disk() {
    let text = "player_combat_script,scripts/player_combat_script.txt\n";

    let rejected = PatchList::parse(text, |_| false).expect_err("one field is not a record");
    assert!(rejected.contains("does not name a file"), "{rejected}");

    // The same text, with the named file present: now it is a legacy entry.
    let list = PatchList::parse(text, |path| path == "player_combat_script").expect("legacy row");
    assert_eq!(list.len(), 1);
    let entry = list.iter().next().unwrap();
    assert_eq!(entry.source, "player_combat_script");
    assert_eq!(entry.patch, "scripts/player_combat_script.txt");
    assert_eq!(entry.kind, PatchType::Diff);
}

#[test]
fn the_shape_test_is_by_existence_not_by_field_count() {
    // Four fields, but the first names a real file, so the row is read as legacy: the source
    // becomes `diff` and the real source is demoted to a patch path.
    let text = "diff,SECT/DRGN1.BIN/20,scripts/DRGN1/20.diff,\"a comma\"\n";
    let list = PatchList::parse(text, |path| path == "diff").expect("legacy reading wins");

    let entry = list.get("diff").expect("keyed by the wrong thing");
    assert_eq!(entry.patch, "SECT/DRGN1.BIN/20");
}

#[test]
fn a_diff_entry_with_no_source_is_an_error() {
    let (mut ws, mut toolchain) = fixture_unpatched();
    ws.write(
        Root::Patches,
        "scripts.csv",
        "diff,GHOST,scripts/DRGN1/20.diff,\"x\"\n",
    );

    let error = patch::apply(&mut ws, &mut toolchain).expect_err("nothing to patch");
    assert!(error.to_string().contains("GHOST"), "{error}");
}

#[test]
fn a_replacement_with_no_original_creates_the_file_and_keeps_no_backup() {
    let (mut ws, mut toolchain) = fixture_unpatched();
    ws.remove(Root::Files, "throw_item");

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("new file");

    assert!(outcome.applied.contains(&"throw_item".to_string()));
    assert!(
        !ws.exists(Root::Backups, "throw_item"),
        "there was nothing pristine to preserve, and the reference agrees — which also means a \
         later revert of this entry has nothing to revert to"
    );
    assert_eq!(
        ws.read(Root::Files, "throw_item").unwrap(),
        b"#include char_utils.fate\nthrow_item:\n",
        "a replacement writes the body through the toolchain, and the stub passes it through"
    );
}

// ---------------------------------------------------------------------------------------------
// The disk-backed tree, so that the in-memory one is not the only thing ever exercised.
// ---------------------------------------------------------------------------------------------

#[test]
fn the_same_contract_holds_against_real_directories() {
    let root = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("patch_disk_tree");
    let _ = std::fs::remove_dir_all(&root);

    let files = root.join("files");
    let patches = root.join("patches");
    let cache = root.join("cache");
    let backups = root.join("backups");
    for dir in [&files, &patches, &cache, &backups] {
        std::fs::create_dir_all(dir).unwrap();
    }

    let mut ws = DiskWorkspace::new(&files, &patches, &cache, &backups);
    let mut toolchain = RecordingToolchain::new();
    fixture_into(&mut ws);

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("first run on disk");
    assert_eq!(outcome.applied.len(), 2);
    assert!(
        backups.join("SECT/DRGN1.BIN/20").is_file(),
        "nested backup paths stay nested, rather than being flattened into one name"
    );
    assert!(cache.join("scripts/DRGN1/20.diff").is_file());

    let outcome = patch::apply(&mut ws, &mut toolchain).expect("second run on disk");
    assert!(outcome.is_noop(), "{outcome:?}");

    // Remove one entry and let the disk tree revert it.
    std::fs::write(patches.join("scripts.csv"), ONE).unwrap();
    let outcome = patch::apply(&mut ws, &mut toolchain).expect("third run on disk");
    assert_eq!(outcome.restored, ["throw_item".to_string()]);
    assert_eq!(
        std::fs::read(files.join("throw_item")).unwrap(),
        b"original-throw",
        "a `rename` across trees, so the backup is consumed rather than copied back"
    );

    let _ = std::fs::remove_dir_all(&root);
}
