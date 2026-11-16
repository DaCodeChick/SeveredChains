//! Patch orchestration: the `patches/scripts.csv` walk.
//!
//! This is the *orchestration* half of the patch pipeline and nothing more. It decides which of
//! the 353 entries need work, keeps the pristine backups, and tells a toolchain to act. It does
//! not decompile, patch or reassemble a script — that is `ScriptPatchToolchain`, and what ships
//! here is a recording stub. The real toolchain is I4.
//!
//! Keeping the two apart is not decoration. The orchestrator's contract is about *when a file is
//! touched*, and that contract is testable today against a fake toolchain; the toolchain's
//! contract is about byte-for-byte reassembly of PSX script code, and it is not. Compiling them
//! together would make the first untestable in order to wait for the second.
//!
//! # No paired writer
//!
//! [`PatchList::parse`] has no counterpart in `psxrt-compat`, and none is planned. That is a fact
//! about the artifact rather than a shortfall in effort. `patches/scripts.csv` is authored by hand
//! and checked into the reference repository; it is the *input* to the patch phase, not an
//! intermediate format the toolchain decodes and re-encodes on its way somewhere else, and no
//! part of this project generates one. A writer here would have no caller, which is the standard
//! the I0 policy scaffold was deleted for.
//!
//! The reader is not therefore untested, and it is not tested against fixtures derived from
//! itself: `tests/patch_orchestration.rs` runs 21 cases against hand-written patch lists, two of
//! which exist specifically to be wrong — a quoted field holding an embedded comma, and a
//! two-column row that must be read in the legacy shape. What is verified is the read contract,
//! including the reference's choice to discard an entire list on one malformed row.
//!
//! # The mechanism, in one paragraph
//!
//! An entry is skipped when the CRC32 of its source file, its `.config.csv` sibling, and every
//! file they `#include` — all measured in the cache copy — matches the same set measured in the
//! live patch tree. When anything differs, the pristine bytes come out of `backups/`, the
//! toolchain rewrites the file from scratch, and at the end the whole cache tree is replaced with
//! a fresh copy of the patch tree. Entries present in the cache but absent from the live tree are
//! reverted from the backup.
//!
//! That is what makes it idempotent and self-healing, and both words have a concrete meaning
//! here: re-running with an unchanged patch tree does nothing, because the cache agrees with the
//! tree; tampering with a cache file re-applies everything whose closure reads it, and
//! re-application starts from the backup rather than from the current, already-patched bytes.
//!
//! # Two ways this is not a transcription of the reference
//!
//! **Order.** The reference keys its patch map with a `HashMap` and iterates `.values()`, so its
//! application order is whatever that hash table yields. This iterates in file order. Order only
//! matters when two entries name the same source file, and a patch set that does that is already
//! broken — but "whatever the hash table felt like" is not a thing a Rust reader should
//! reproduce for fidelity's sake. Duplicate sources collapse to the last row, which is what
//! `Map.put` does.
//!
//! **The filesystem.** The reference calls `java.nio.file` directly. Here the four trees sit
//! behind [`Workspace`] so the whole orchestrator can be tested against
//! [`MemWorkspace`](Workspace::list), which is the answer to the hazard recorded at
//! `planning/04` §A: an orchestrator that can only be exercised with a real unpacked tree never
//! gets exercised.

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};

use crate::error::ImportError;

/// Which of the four trees a path is relative to.
///
/// Four, because the reference keeps four and they are not interchangeable: the same relative
/// path means a different file in each, and the existence test that picks a patch list's *shape*
/// looks in `Files` while the bytes are read from `Cache`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Root {
    /// The unpacked game tree — what gets patched.
    Files,
    /// The live patch set — what the operator edits.
    Patches,
    /// The previous run's copy of `Patches` — what the CRC gate compares against.
    Cache,
    /// The pristine copies — what a re-apply and a restore both read.
    Backups,
}

/// The minimum a patch tree has to be able to do.
///
/// Seven operations, named after what the orchestrator needs rather than after filesystem calls,
/// and none of them fallible: a missing file is a *question the orchestrator asks*, not an error
/// it survives. Everything that can genuinely fail is reported by
/// [`apply`](struct@apply)'s `Result`.
pub trait Workspace {
    /// Is `rel` present in `root`?
    fn exists(&self, root: Root, rel: &str) -> bool;

    /// The bytes at `rel`, or `None` if it is not there.
    fn read(&self, root: Root, rel: &str) -> Option<Vec<u8>>;

    /// Replace the bytes at `rel`, creating whatever parents are needed.
    fn write(&mut self, root: Root, rel: &str, bytes: impl AsRef<[u8]>);

    /// Copy one file to another location, leaving the source alone.
    fn copy(&mut self, from: (Root, &str), to: (Root, &str));

    /// Move `from` onto `to`, replacing `to`, then discard any directory on the `from` side that
    /// the move left with nothing in it.
    ///
    /// The pruning is not tidiness. The reference walks up from the emptied directory to the tree
    /// root and deletes what it finds, and the reason it bothers is that a later full copy of the
    /// patch tree would otherwise refill directories that are supposed to be gone.
    fn restore(&mut self, from: (Root, &str), to: (Root, &str));

    /// Every file in `root`, as relative paths. Only used by the in-memory test tree.
    fn list(&self, root: Root) -> Vec<String>;

    /// Discard the cache tree and rebuild it as a copy of the patch tree.
    fn refresh_cache(&mut self);
}

/// A patch set backed by four real directories.
#[derive(Debug, Clone)]
pub struct DiskWorkspace {
    roots: [PathBuf; 4],
}

impl DiskWorkspace {
    /// Build from the four directories. No check is made that they exist — an absent cache
    /// directory is an ordinary first run, not a mistake.
    #[must_use]
    pub fn new(
        files: impl Into<PathBuf>,
        patches: impl Into<PathBuf>,
        cache: impl Into<PathBuf>,
        backups: impl Into<PathBuf>,
    ) -> Self {
        Self {
            roots: [files.into(), patches.into(), cache.into(), backups.into()],
        }
    }

    fn path(&self, root: Root, rel: &str) -> PathBuf {
        self.roots[usize::from(root)].join(rel)
    }
}

/// The ordinal is the array index into [`DiskWorkspace::roots`].
impl From<Root> for usize {
    fn from(root: Root) -> Self {
        match root {
            Root::Files => 0,
            Root::Patches => 1,
            Root::Cache => 2,
            Root::Backups => 3,
        }
    }
}

impl Workspace for DiskWorkspace {
    fn exists(&self, root: Root, rel: &str) -> bool {
        self.path(root, rel).is_file()
    }

    fn read(&self, root: Root, rel: &str) -> Option<Vec<u8>> {
        std::fs::read(self.path(root, rel)).ok()
    }

    fn write(&mut self, root: Root, rel: &str, bytes: impl AsRef<[u8]>) {
        let path = self.path(root, rel);
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::write(path, bytes.as_ref());
    }

    fn copy(&mut self, from: (Root, &str), to: (Root, &str)) {
        if let Some(bytes) = self.read(from.0, from.1) {
            self.write(to.0, to.1, &bytes);
        }
    }

    fn restore(&mut self, from: (Root, &str), to: (Root, &str)) {
        let source = self.path(from.0, from.1);
        let target = self.path(to.0, to.1);
        if let Some(parent) = target.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::rename(&source, &target);
        prune_ancestors(&source, &self.roots[usize::from(from.0)]);
    }

    fn list(&self, root: Root) -> Vec<String> {
        let mut out = Vec::new();
        walk(
            self.roots[usize::from(root)].as_path(),
            Path::new(""),
            &mut out,
        );
        out.sort();
        out
    }

    fn refresh_cache(&mut self) {
        let cache = self.roots[usize::from(Root::Cache)].clone();
        let patches = self.roots[usize::from(Root::Patches)].clone();
        let _ = std::fs::remove_dir_all(&cache);
        let _ = copy_tree(patches.as_path(), cache.as_path());
    }
}

/// A patch tree held in memory, for tests.
///
/// Paths are keys, not nodes, so there is no directory object to prune and
/// [`restore`](Workspace::restore) prunes nothing. That is the honest shape rather than a shortcut:
/// the in-memory tree has no directories to leak, and the one behaviour that *would* matter — a
/// later cache refresh resurrecting a directory the restore deleted — cannot be expressed here at
/// all, because a cache refresh is a whole-tree replacement in both models.
#[derive(Debug, Default, Clone)]
pub struct MemWorkspace {
    trees: [BTreeMap<String, Vec<u8>>; 4],
}

impl MemWorkspace {
    /// An empty tree.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Insert a file. Test scaffolding, so it overwrites without asking.
    pub fn put(&mut self, root: Root, rel: &str, bytes: impl AsRef<[u8]>) {
        self.trees[usize::from(root)].insert(rel.to_string(), bytes.as_ref().to_vec());
    }

    /// Delete a file.
    ///
    /// Also scaffolding, and here for a reason: several of the interesting cases in the patch phase
    /// are *absence* cases — a source that was never there, a backup already spent on a revert — and
    /// a fixture that can only add things cannot build them.
    pub fn remove(&mut self, root: Root, rel: &str) {
        self.trees[usize::from(root)].remove(rel);
    }
}

impl Workspace for MemWorkspace {
    fn exists(&self, root: Root, rel: &str) -> bool {
        self.trees[usize::from(root)].contains_key(rel)
    }

    fn read(&self, root: Root, rel: &str) -> Option<Vec<u8>> {
        self.trees[usize::from(root)].get(rel).cloned()
    }

    fn write(&mut self, root: Root, rel: &str, bytes: impl AsRef<[u8]>) {
        self.trees[usize::from(root)].insert(rel.to_string(), bytes.as_ref().to_vec());
    }

    fn copy(&mut self, from: (Root, &str), to: (Root, &str)) {
        if let Some(bytes) = self.read(from.0, from.1).clone() {
            self.write(to.0, to.1, &bytes);
        }
    }

    fn restore(&mut self, from: (Root, &str), to: (Root, &str)) {
        if let Some(bytes) = self.read(from.0, from.1).clone() {
            self.trees[usize::from(from.0)].remove(from.1);
            self.write(to.0, to.1, &bytes);
        }
    }

    fn list(&self, root: Root) -> Vec<String> {
        self.trees[usize::from(root)].keys().cloned().collect()
    }

    fn refresh_cache(&mut self) {
        let fresh = self.trees[usize::from(Root::Patches)].clone();
        self.trees[usize::from(Root::Cache)] = fresh;
    }
}

/// Delete `path` and each of its parents up to — but not including — `stop`, while empty.
fn prune_ancestors(path: &Path, stop: &Path) {
    let mut current = path.parent().map(Path::to_path_buf);
    while let Some(dir) = current {
        if dir == stop {
            break;
        }
        let empty = std::fs::read_dir(&dir).is_ok_and(|mut it| it.next().is_none());
        if empty {
            let _ = std::fs::remove_dir(&dir);
        }
        current = dir.parent().map(Path::to_path_buf);
    }
}

/// Recursively copy a directory tree, replacing whatever is at the destination.
fn copy_tree(from: &Path, to: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(to)?;
    for entry in std::fs::read_dir(from)? {
        let entry = entry?;
        let target = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &target)?;
        } else {
            std::fs::copy(entry.path(), &target)?;
        }
    }
    Ok(())
}

/// How a source file is rewritten.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PatchType {
    /// A diff applied over the decompiled original.
    Diff,
    /// A whole-file replacement.
    Replacement,
}

/// One row of `scripts.csv`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScriptPatch {
    /// How the source is rewritten.
    pub kind: PatchType,
    /// The file under `files/` that gets rewritten — the key, in both patch lists.
    pub source: String,
    /// The patch body, relative to the patch tree.
    pub patch: String,
}

/// A patch list, in file order.
///
/// Duplicate sources keep the *last* row, at the position of the first, which is the closest
/// thing to the reference's `Map.put` that is also deterministic.
#[derive(Debug, Clone, Default)]
pub struct PatchList {
    entries: Vec<ScriptPatch>,
    index: HashMap<String, usize>,
}

impl PatchList {
    /// An empty list — which is also what a *broken* list becomes. See
    /// [`PatchList::parse`].
    #[must_use]
    pub fn empty() -> Self {
        Self::default()
    }

    /// The number of entries.
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether there is nothing to do.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// The entries, in file order.
    // No `#[must_use]`: clippy already treats an iterator-returning method that way, and
    // restating it here only adds a line that says nothing.
    pub fn iter(&self) -> std::slice::Iter<'_, ScriptPatch> {
        self.entries.iter()
    }

    /// The entry for `source`, if one exists.
    #[must_use]
    pub fn get(&self, source: &str) -> Option<&ScriptPatch> {
        self.index.get(source).map(|&i| &self.entries[i])
    }

    /// Read a `scripts.csv`.
    ///
    /// Two shapes are accepted, and which one a row is depends on the *filesystem*, not on how many
    /// fields it has: if field 0 names a file that exists under `files/`, the row is read as a
    /// legacy two-column entry — `DIFF`, with field 0 as the source and field 1 as the patch body.
    /// Only when that test fails is the row read as `type,source,patch,description`.
    ///
    /// That is a hazard, not a feature, and it is replicated verbatim for two reasons. It is what
    /// the reference does, so a patch set written for the reference parses the same way here. And it
    /// is reachable: a four-column row whose first field happens to name something in `files/` is
    /// read as a patch *of that thing*, with the real source promoted to a patch path. `files/`
    /// holds a script called `throw_item` and directories called `SECT` and `S` — a patch set that
    /// grows a file named `diff` would silently misread all 351 diff rows. The test
    /// `legacy_shape_is_chosen_by_what_is_on_disk` demonstrates the switch, and
    /// `the_shape_test_is_by_existence_not_by_field_count` shows that a quoted four-column row
    /// takes it.
    ///
    /// A malformed row discards the *whole* list, not just the row, because the reference wraps the
    /// entire loop in one `catch (Throwable)`. Half a patch set is worse than none: the entries that
    /// did load would be applied on top of the ones that silently did not.
    pub fn parse(text: &str, file_exists: impl Fn(&str) -> bool) -> Result<Self, String> {
        let mut list = Self::default();

        for (number, record) in read_csv(text).into_iter().enumerate() {
            let line = number + 1;
            let entry = match record.as_slice() {
                // Shape is decided by the filesystem, not by the field count, so this arm can
                // swallow a four-column row. Kept exactly because it is exactly what the
                // reference does, and `files/` is not empty.
                [source, patch, ..] if file_exists(source) => ScriptPatch {
                    kind: PatchType::Diff,
                    source: source.clone(),
                    patch: patch.clone(),
                },
                [kind, source, patch, ..] => {
                    // `toUpperCase(Locale.US)`, which is ASCII-only: a Turkish-locale dotless `i`
                    // cannot change the result. Same reason, same effect.
                    let kind = match kind.to_ascii_uppercase().as_str() {
                        "DIFF" => PatchType::Diff,
                        "REPLACEMENT" => PatchType::Replacement,
                        other => return Err(format!("line {line}: no patch type {other:?}")),
                    };
                    ScriptPatch {
                        kind,
                        source: source.clone(),
                        patch: patch.clone(),
                    }
                }
                other => {
                    return Err(format!(
                        "line {line}: {} field(s), and the first does not name a file",
                        other.len()
                    ));
                }
            };

            match list.index.get(&entry.source) {
                Some(&i) => list.entries[i] = entry,
                None => {
                    list.index.insert(entry.source.clone(), list.entries.len());
                    list.entries.push(entry);
                }
            }
        }

        Ok(list)
    }
}

/// The work handed to a toolchain.
#[derive(Debug, Clone)]
pub enum Job<'a> {
    /// Rebuild `source` by applying a diff to its pristine bytes.
    Diff {
        /// The file being rewritten, relative to `files/`.
        source: &'a str,
        /// The pristine bytes, read from `backups/`. Never the current contents.
        original: &'a [u8],
        /// The patch body, read from the *live* patch tree.
        patch: &'a [u8],
        /// The `.config.csv` sibling, empty if there is none.
        config: &'a [u8],
    },
    /// Replace `source` outright. There is nothing to patch against, which is why this arm carries
    /// no `original`.
    Replacement {
        /// The file being replaced, relative to `files/`.
        source: &'a str,
        /// The replacement text.
        replacement: &'a [u8],
        /// The `.config.csv` sibling, empty if there is none.
        config: &'a [u8],
    },
}

/// The thing that actually turns bytes into bytes. Absent until I4.
pub trait Toolchain {
    /// Produce the new contents of `source`.
    ///
    /// The [`Job`] deliberately does not borrow a [`Workspace`]. A toolchain that could reach the
    /// tree could also *change* it, and then nothing about the orchestrator's guarantees would
    /// hold: the backup is supposed to be written once, by the orchestrator, from pristine bytes.
    fn apply(&mut self, job: Job<'_>) -> Result<Vec<u8>, ImportError>;
}

/// A toolchain that changes nothing and remembers being asked.
///
/// This is what I1a ships, and it exists so that the orchestration — *which* entries are visited,
/// in what order, from which bytes — is under test now rather than at the end of I4, when a real
/// toolchain would make every one of those assertions expensive to write.
///
/// It hands back the bytes it was given. That keeps the idempotence test honest: a second run with
/// an unchanged patch tree issues no calls at all, and that fact is about the gate, not about a
/// toolchain happening to reproduce a match.
#[derive(Debug, Default)]
pub struct RecordingToolchain {
    /// One entry per call, in call order.
    pub calls: Vec<String>,
}

impl RecordingToolchain {
    /// A fresh recorder.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }
}

impl Toolchain for RecordingToolchain {
    fn apply(&mut self, job: Job<'_>) -> Result<Vec<u8>, ImportError> {
        match job {
            Job::Diff {
                source, original, ..
            } => {
                self.calls.push(source.to_string());
                Ok(original.to_vec())
            }
            Job::Replacement {
                source,
                replacement,
                ..
            } => {
                self.calls.push(source.to_string());
                Ok(replacement.to_vec())
            }
        }
    }
}

/// What one run of [`apply`] did.
#[derive(Debug, Default, Clone)]
pub struct Outcome {
    /// Sources rewritten, in the order they were rewritten.
    pub applied: Vec<String>,
    /// Sources reverted from backup, in the order they were reverted.
    pub restored: Vec<String>,
    /// Whether the cache tree was replaced afterwards.
    pub refreshed: bool,
    /// Anything the reference would have logged as a warning. Kept rather than printed, so the
    /// pipeline can decide what to do with it and so a test can assert on it.
    pub warnings: Vec<String>,
}

impl Outcome {
    /// Whether the run touched nothing at all.
    #[must_use]
    pub fn is_noop(&self) -> bool {
        self.applied.is_empty() && self.restored.is_empty()
    }
}

/// Every file that has to agree between the cache and the patch tree for one entry to be skipped.
///
/// Keys are paths relative to whichever tree is being measured; values are CRC32s, with `None`
/// meaning "this file is not there". The `None` is load-bearing: the reference stores a null for a
/// missing include and compares with `Objects.equals`, so *absent and absent* agree while *absent
/// and present* do not. Modelling a missing file as an absent key instead would reverse that, and a
/// patch that later gains an include would fail to re-trigger.
type CrcMap = BTreeMap<String, Option<u32>>;

/// The `.config.csv` sibling of a patch body.
///
/// The extension is *replaced*, not appended: `20.diff` pairs with `20.config.csv` and
/// `player_combat_script.txt` with `player_combat_script.config.csv`, which is how the files are
/// actually laid out in the patch set. Getting this wrong is silent — the config just never turns
/// up, and every entry whose behaviour depends on a table length gets applied blind forever.
///
/// A name with no dot has no sibling — the reference throws there too. Reported rather than
/// swallowed, for the same reason.
fn config_name(patch: &str) -> Result<String, ImportError> {
    let stem = patch
        .rfind('.')
        .ok_or_else(|| ImportError::PatchConfigName {
            text: patch.to_string(),
        })?;
    Ok(format!("{}.config.csv", &patch[..stem]))
}

/// Measure one entry's dependency closure.
///
/// Public because the gate is a *measurement*, and a measurement observable only through a whole
/// run is a measurement that cannot be pinned. `tests/patch_orchestration.rs` checks the hash and
/// the absent-file encoding directly, against a CRC-32 check value taken from outside this crate.
///
/// `root` is the tree being measured: the live tree for the new side, the cache copy for the cached
/// side. Includes are looked up in `libs/` then `scripts/` *within that tree* — not relative to the
/// including file, which is what makes an include written as a bare filename resolve at all.
pub fn closure_for(
    ws: &impl Workspace,
    root: Root,
    entry: &ScriptPatch,
) -> Result<CrcMap, ImportError> {
    let mut map = CrcMap::new();
    // Named before the first visit: `visit` wants `&mut map`, so the second name has to be
    // computed before the borrow that the first one opens.
    let config = config_name(&entry.patch)?;
    visit(ws, root, &entry.patch, &mut map)?;
    visit(ws, root, &config, &mut map)?;
    Ok(map)
}

fn visit(
    ws: &impl Workspace,
    root: Root,
    path: &str,
    seen: &mut CrcMap,
) -> Result<(), ImportError> {
    if seen.contains_key(path) {
        return Ok(());
    }

    let Some(bytes) = ws.read(root, path) else {
        seen.insert(path.to_string(), None);
        return Ok(());
    };

    seen.insert(path.to_string(), Some(crc32(&bytes)));

    // The guard is checked before the hash and before the descent, so a pair of files that include
    // each other terminates. This is the one place a recursive reader has to prove it stops.
    let text = String::from_utf8_lossy(&bytes);
    for include in includes(&text) {
        let Some(resolved) = resolve_include(ws, root, include) else {
            return Err(ImportError::IncludeFailed {
                from: path.to_string(),
                text: include.to_string(),
            });
        };
        visit(ws, root, &resolved, seen)?;
    }

    Ok(())
}

/// Find the `#include` targets in one file's text.
///
/// The pattern is `^\s*\+?#?include\s+(.+?)\s*;?\s*$`, matched against every line — which is
/// why a `.config.csv` *can* pull in a dependency, and why the config is hashed too. Written out as
/// a matcher rather than a regex because the shape is five characters wide, and a regex dependency
/// to match it is not free.
fn includes(text: &str) -> Vec<&str> {
    let mut out = Vec::new();

    for line in text.split(['\n', '\r']) {
        // `\s*`, then an optional `+` and an optional `#`. Both optionals sit behind the `\s*`, so
        // `+ # include x` matches while `# include x` does not — there is no room in the pattern
        // for whitespace between the `#` and the word.
        let rest = trim_start_ws(line);
        let rest = rest.strip_prefix('+').unwrap_or(rest);
        let rest = rest.strip_prefix('#').unwrap_or(rest);
        let Some(rest) = rest.strip_prefix("include") else {
            continue;
        };

        // One whitespace character is required between the keyword and the capture, and the capture
        // needs a character of its own. Both are shape checks, not style preferences: without the
        // first, a line reading exactly `include` would ask for a dependency on the empty path.
        if !rest.starts_with(is_pattern_whitespace) {
            continue;
        }
        let rest = trim_start_ws(rest);
        if rest.is_empty() {
            continue;
        }

        // The capture is lazy and is followed by `\s*;?\s*`, so the target is the tail with those
        // peeled off from the right.
        let target = trim_end_ws(rest);
        let target = trim_end_ws(target.strip_suffix(';').unwrap_or(target));

        if !target.is_empty() {
            out.push(target);
        }
    }

    out
}

/// The pattern's leading `\s*` / `\s+`.
fn trim_start_ws(text: &str) -> &str {
    let end = text
        .char_indices()
        .find(|(_, c)| !is_pattern_whitespace(*c))
        .map_or(text.len(), |(i, _)| i);
    &text[end..]
}

/// The pattern's trailing `\s*;?\s*`.
///
/// Keeps everything up to and including the last non-whitespace character. The mirror of
/// [`trim_start_ws`], and easy to write one character wrong — slicing from the found index would
/// keep the *tail* instead of the head, which silently turns every non-blank line into an empty
/// target.
fn trim_end_ws(text: &str) -> &str {
    match text
        .char_indices()
        .rfind(|(_, c)| !is_pattern_whitespace(*c))
    {
        Some((i, c)) => &text[..i + c.len_utf8()],
        None => "",
    }
}

/// What this matcher counts as whitespace: the ASCII set.
///
/// `char::is_whitespace` would also take U+00A0 and friends, which `\s` does not. Narrow on
/// purpose — widening it here would change which lines count as dependencies.
const fn is_pattern_whitespace(c: char) -> bool {
    matches!(c, ' ' | '\t' | '\n' | '\r' | '\u{b}' | '\u{c}')
}

/// Resolve one include target to a path inside the tree, or `None` if nothing answers to it.
///
/// `libs/` first, then `scripts/`, first hit wins — so a file in `scripts/` can be shadowed by one
/// in `libs/`, which is what the real tree does with `char_utils.fate`. An absolute target is
/// unresolvable by definition: the reference tests it outside the tree, where nothing lives.
fn resolve_include(ws: &impl Workspace, root: Root, include: &str) -> Option<String> {
    if include.starts_with('/') {
        return None;
    }

    ["libs", "scripts"]
        .into_iter()
        .map(|base| format!("{base}/{include}"))
        .find(|candidate| ws.exists(root, candidate))
}

/// CRC32 as `java.util.zip.CRC32` computes it: reflected, order 32, both polynomials inverted,
/// seeded with `0xFFFF_FFFF` and finalized by complement.
fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for byte in data {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let odd = crc & 1 != 0;
            crc >>= 1;
            if odd {
                crc ^= 0xEDB8_8320;
            }
        }
    }
    !crc
}

/// Whether one entry has to be redone.
///
/// Only the keys on the *new* side are consulted, and that asymmetry is the reference's, kept
/// deliberately: a dependency that has since disappeared from the patch tree does not by itself
/// force a redo, because there is nothing left to read it. In practice no input isolates this —
/// dropping a dependency also changes the body that used to name it — so it is documented rather
/// than tested, and the documented behaviour is the one that would surprise a reader.
fn needs_update(cached: Option<&CrcMap>, fresh: &CrcMap) -> bool {
    let Some(cached) = cached else {
        return true;
    };

    fresh
        .iter()
        .any(|(path, fresh_crc)| cached.get(path).copied().unwrap_or(None) != *fresh_crc)
}

/// The name the patch list is keyed under inside a tree.
const LIST: &str = "scripts.csv";

/// Run the patch phase once.
///
/// Reads the live list from `Patches` and the previous one from `Cache`, applies everything whose
/// dependency closure disagrees, reverts everything that has gone away, and replaces the cache if
/// either happened.
///
/// One failure mode worth naming before it bites someone: an unresolvable `#include` stops the whole
/// run rather than skipping one entry. That matches the reference, and it is the right call — a
/// patch applied against a half-read dependency closure is worse than no patch, because it looks
/// like it succeeded.
pub fn apply(
    ws: &mut impl Workspace,
    toolchain: &mut impl Toolchain,
) -> Result<Outcome, ImportError> {
    let mut outcome = Outcome::default();

    let (fresh, warning) = load_list(ws, Root::Patches);
    outcome.warnings.extend(warning);
    let (cached, warning) = load_list(ws, Root::Cache);
    outcome.warnings.extend(warning);

    let mut changed = false;

    for patch in fresh.iter() {
        // Both closures are measured before either tree is touched, and the entry is skipped or
        // redone on the comparison. Measuring lazily, per read, would let an earlier patch in this
        // same loop change what a later one is compared against.
        let stale = match cached.get(&patch.source) {
            None => true,
            Some(entry) => {
                let before = closure_for(ws, Root::Cache, entry)?;
                let after = closure_for(ws, Root::Patches, patch)?;
                needs_update(Some(&before), &after)
            }
        };

        if !stale {
            continue;
        }

        apply_one(ws, toolchain, patch)?;
        outcome.applied.push(patch.source.clone());
        changed = true;
    }

    for entry in cached.iter() {
        if fresh.get(&entry.source).is_none() {
            restore_one(ws, entry)?;
            outcome.restored.push(entry.source.clone());
            changed = true;
        }
    }

    if changed {
        ws.refresh_cache();
        outcome.refreshed = true;
    }

    Ok(outcome)
}

/// Load one patch list, degrading to empty.
///
/// Returns the list and, if it did not load, the reason. The reference logs a warning and continues
/// with nothing; returning the reason keeps that observable to a test instead of printing it into a
/// log nobody reads while a build fails.
fn load_list(ws: &impl Workspace, root: Root) -> (PatchList, Option<String>) {
    let Some(bytes) = ws.read(root, LIST) else {
        return (PatchList::empty(), Some(format!("{root:?} has no {LIST}")));
    };

    // The two-shape test reads `files/`, whichever tree the list itself came from. Straight from
    // the reference, and the reason this takes the tree as a parameter instead of closing over one.
    match PatchList::parse(&String::from_utf8_lossy(&bytes), |rel| {
        ws.exists(Root::Files, rel)
    }) {
        Ok(list) => (list, None),
        Err(reason) => (
            PatchList::empty(),
            Some(format!("bad patch list: {reason}")),
        ),
    }
}

/// Rewrite one source file.
fn apply_one(
    ws: &mut impl Workspace,
    toolchain: &mut impl Toolchain,
    patch: &ScriptPatch,
) -> Result<(), ImportError> {
    let source = patch.source.as_str();

    // The backup is written once, ever. This is what makes a re-run a *rebuild* rather than a
    // patch-on-a-patch, and it is the only reason self-healing can be trusted to land on the same
    // bytes as a first run. A replacement whose original was absent gets no backup, because there
    // was nothing pristine to keep — which also means a later revert of that entry has nothing to
    // revert to, and says so.
    let backup_needed = !ws.exists(Root::Backups, source)
        && (patch.kind != PatchType::Replacement || ws.exists(Root::Files, source));
    if backup_needed {
        if !ws.exists(Root::Files, source) {
            return Err(ImportError::MissingSource {
                text: source.to_string(),
            });
        }
        ws.copy((Root::Files, source), (Root::Backups, source));
    }

    let config = patch_config(ws, patch);

    match patch.kind {
        PatchType::Diff => {
            // Read from the backup, never from the live file. There is one path where the backup is
            // absent: a diff whose source was never there, which the reference answers with
            // `NoSuchFileException` and which is reported, not papered over.
            let Some(original) = ws.read(Root::Backups, source) else {
                return Err(ImportError::MissingSource {
                    text: source.to_string(),
                });
            };
            let Some(body) = ws.read(Root::Patches, &patch.patch) else {
                return Err(ImportError::MissingSource {
                    text: patch.patch.clone(),
                });
            };
            let rebuilt = toolchain.apply(Job::Diff {
                source,
                original: &original,
                patch: &body,
                config: &config,
            })?;
            ws.write(Root::Files, source, &rebuilt);
        }
        PatchType::Replacement => {
            let Some(body) = ws.read(Root::Patches, &patch.patch) else {
                return Err(ImportError::MissingSource {
                    text: patch.patch.clone(),
                });
            };
            let rebuilt = toolchain.apply(Job::Replacement {
                source,
                replacement: &body,
                config: &config,
            })?;
            ws.write(Root::Files, source, &rebuilt);
        }
    }

    Ok(())
}

/// The `.config.csv` beside a patch body, read from the live tree.
fn patch_config(ws: &impl Workspace, patch: &ScriptPatch) -> Vec<u8> {
    match config_name(&patch.patch) {
        Ok(name) => ws.read(Root::Patches, &name).unwrap_or_default(),
        // A body with no extension has no config in the reference either. Not a failure *here*:
        // the closure gate is what reports that, and this side hands the toolchain nothing.
        Err(_) => Vec::new(),
    }
}

/// Revert one source file to its backup.
fn restore_one(ws: &mut impl Workspace, entry: &ScriptPatch) -> Result<(), ImportError> {
    let source = entry.source.as_str();

    if !ws.exists(Root::Backups, source) {
        return Err(ImportError::MissingSource {
            text: format!("backup of {source}"),
        });
    }

    ws.restore((Root::Backups, source), (Root::Files, source));
    Ok(())
}

/// A minimal RFC-4180 reader, sized to what `scripts.csv` actually needs.
///
/// Quoted fields, `""` for a literal quote, LF or CRLF terminated, and a record is one CSV *row*
/// rather than one text line, so a quoted field may hold a newline. Blank lines are skipped.
/// Spaces inside a field are preserved: the reference's reader does not trim them, and a config
/// row written `branch, 0x1a4` has to keep its space rather than silently become a different key.
fn read_csv(text: &str) -> Vec<Vec<String>> {
    let chars: Vec<char> = text.chars().collect();
    let mut rows: Vec<Vec<String>> = Vec::new();
    let mut row: Vec<String> = Vec::new();
    let mut field = String::new();
    let mut quoted = false;
    let mut in_row = false;
    let mut i = 0;

    while i < chars.len() {
        in_row = true;
        let c = chars[i];

        match c {
            '"' if !quoted && field.is_empty() => quoted = true,
            '"' if quoted => {
                if chars.get(i + 1) == Some(&'"') {
                    field.push('"');
                    i += 2;
                    continue;
                }
                quoted = false;
            }
            ',' if !quoted => row.push(std::mem::take(&mut field)),
            '\n' | '\r' if !quoted => {
                if c == '\r' && chars.get(i + 1) == Some(&'\n') {
                    i += 1;
                }
                // A blank line carries no record. The test has to be "nothing collected yet", not
                // "one empty field": a row that kept a stray empty field from here would prepend it
                // to the next real record.
                if !row.is_empty() || !field.is_empty() {
                    row.push(std::mem::take(&mut field));
                    rows.push(std::mem::take(&mut row));
                }
                field.clear();
                row.clear();
                in_row = false;
            }
            other => field.push(other),
        }

        i += 1;
    }

    if in_row && (!field.is_empty() || !row.is_empty()) {
        row.push(field);
        rows.push(row);
    }

    rows
}

/// Collect every file under `dir` as `/`-separated paths relative to `base`.
fn walk(dir: &Path, base: &Path, out: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let relative = base.join(entry.file_name());
        if path.is_dir() {
            walk(&path, &relative, out);
        } else {
            out.push(relative.to_string_lossy().into_owned());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_config_name_replaces_the_extension() {
        assert_eq!(
            config_name("scripts/DRGN1/20.diff").unwrap(),
            "scripts/DRGN1/20.config.csv"
        );
        assert_eq!(
            config_name("scripts/player_combat_script.txt").unwrap(),
            "scripts/player_combat_script.config.csv"
        );
        assert!(config_name("nodot").is_err());
    }

    #[test]
    fn the_include_matcher_matches_the_shapes_that_occur() {
        // The two real shapes: a bare directive, and a `+`-prefixed one inside a diff hunk.
        assert_eq!(includes("#include dep.txt"), ["dep.txt"]);
        assert_eq!(includes("+#include char_utils.fate"), ["char_utils.fate"]);
        // Trailing semicolon and trailing spaces are part of the pattern.
        assert_eq!(includes("#include dep.txt;"), ["dep.txt"]);
        assert_eq!(
            includes("   # include-ish"),
            Vec::<&str>::new(),
            "no whitespace between `#` and the keyword, and no whitespace after it"
        );
        assert_eq!(
            includes("#include"),
            Vec::<&str>::new(),
            "a keyword with no target"
        );
        assert_eq!(
            includes("#include    "),
            Vec::<&str>::new(),
            "whitespace is not a target"
        );
        // Two directives on separate lines, and the CRLF that a Windows-authored file carries.
        assert_eq!(includes("#include a\r\n#include b\r\n"), ["a", "b"]);
        // A line that merely mentions it, mid-text, is not a directive: the pattern is anchored.
        assert_eq!(includes("call include thing"), Vec::<&str>::new());
    }

    #[test]
    fn a_quoted_field_keeps_its_comma_and_its_newline() {
        // Rows are compared as `Vec<&str>` so that records of different widths can appear in one
        // assertion; `read_csv` hands back `Vec<Vec<String>>` and `String` compares with `&str`.
        assert_eq!(
            read_csv("a,b,\"x, y\",d"),
            vec![vec!["a", "b", "x, y", "d"]]
        );
        // A newline inside a quoted field belongs to the field: a row is a CSV record, not a line.
        assert_eq!(
            read_csv("a,\"m\nl\",z\nnext,2"),
            vec![vec!["a", "m\nl", "z"], vec!["next", "2"]]
        );
        assert_eq!(
            read_csv("a,b\r\nc,d\r\n"),
            vec![vec!["a", "b"], vec!["c", "d"]]
        );
        assert_eq!(
            read_csv("table_length, 17cc, 1"),
            vec![vec!["table_length", " 17cc", " 1"]],
            "spaces are not trimmed"
        );
        assert_eq!(
            read_csv("a,b\n\nc,d"),
            vec![vec!["a", "b"], vec!["c", "d"]],
            "a blank line is not a record"
        );
    }

    #[test]
    fn crc32_matches_the_known_answer() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
    }
}
