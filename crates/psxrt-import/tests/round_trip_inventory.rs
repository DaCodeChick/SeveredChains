//! The round-trip inventory.
//!
//! # What this checks, in one sentence
//!
//! Every read entry point in `psxrt-import` either has a paired writer in `psxrt-compat` being
//! exercised against it, or declares in its own module header that no writer is planned — and a
//! module that does neither is a gap this test reports.
//!
//! # Why a fence is needed for something so small
//!
//! "`psxrt-compat` is for exporters" is a true statement about this workspace and a quietly
//! degradable one. The natural failure is not that someone deletes a writer; it is that someone
//! adds a decoder, writes a test that builds its input by hand, and never asks the pairing
//! question. Six months later the hand-built fixture and the decoder agree with each other and
//! with nothing else, which is exactly the arrangement a round trip exists to rule out.
//!
//! # Why the pairing is a table and not a name match
//!
//! The first version of this file searched the test tree for any file mentioning both the reader's
//! function name and the writer crate. It passed with the declaration deleted, because `parse`
//! occurs in `index_round_trip.rs` and so does `psxrt_compat`, which "proved" that
//! `PatchList::parse` had an oracle. A check that cannot fail is not a check, so the pairing is
//! now stated as data below and each entry is verified against the one test file that has to
//! reference both sides of it.
//!
//! That is still a weak assertion — a file can `use` a writer without exercising it — and it is
//! presented as such. What it buys is narrow and real: deleting a writer, or deleting the test that
//! drives one, breaks the build.
//!
//! # The escape hatch, and why it is worded rather than attributed
//!
//! A module may declare `No paired writer` in its header instead of having an oracle. That is
//! deliberate: an `#[allow]`-style attribute gets added to make a warning go away, whereas a
//! sentence has to be true and is something a reviewer can argue with. The companion test below
//! requires the declaration to come with a reason.

use std::fs;
use std::path::{Path, PathBuf};

/// The pairings, as data. Left: a module under `psxrt-import/src`. Right: the writer path that a
/// test in `psxrt-import/tests` must reference *in the same file as* a call into the left module.
///
/// Adding a decoder here without a writer means also adding a line to the module's header
/// explaining why; adding one with a writer means adding its pair here. Either way somebody has
/// answered the question before the code merges.
const PAIRINGS: &[(&str, &str)] = &[
    ("container/bpe.rs", "psxrt_compat::container::bpe"),
    ("container/mrg.rs", "psxrt_compat::container::mrg"),
    ("cache.rs", "psxrt_compat::container::index"),
];

/// The phrase a module header uses to declare that no writer is planned.
const DECLARATION: &str = "No paired writer";

/// The function-name prefixes that mean "this reads something written in a format someone else
/// chose".
const READ_PREFIXES: [&str; 3] = ["decode", "parse", "read"];

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("tests run from inside the workspace")
        .to_path_buf()
}

fn collect_rust_files(directory: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    let mut pending = vec![directory.to_path_buf()];

    while let Some(dir) = pending.pop() {
        if !dir.is_dir() {
            continue;
        }
        for entry in fs::read_dir(&dir).expect("readable directory") {
            let path = entry.expect("readable entry").path();
            if path.is_dir() {
                pending.push(path);
            } else if path.extension().is_some_and(|ext| ext == "rs") {
                found.push(path);
            }
        }
    }

    found.sort();
    found
}

fn read(path: &Path) -> String {
    fs::read_to_string(path)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", path.display()))
}

/// Every `pub fn` in `psxrt-import/src` that reads a format, with the file that defines it.
fn readers(src: &Path) -> Vec<(String, PathBuf, usize)> {
    let mut found = Vec::new();

    for file in collect_rust_files(src) {
        for (index, line) in read(&file).lines().enumerate() {
            let Some(rest) = line.trim().strip_prefix("pub fn ") else {
                continue;
            };
            let name: String = rest
                .chars()
                .take_while(|c| c.is_alphanumeric() || *c == '_')
                .collect();
            if READ_PREFIXES.iter().any(|prefix| name.starts_with(prefix)) {
                found.push((name, file.clone(), index + 1));
            }
        }
    }

    found
}

/// The rule, run.
#[test]
fn every_decoder_either_has_an_oracle_or_declares_that_it_has_none() {
    let root = workspace_root();
    let src = root.join("crates/psxrt-import/src");
    let tests = collect_rust_files(&root.join("crates/psxrt-import/tests"));
    let found = readers(&src);

    // The scan must find the readers that are certainly there. Without this, a rename of the
    // prefix list or of the `pub fn` spelling switches the whole test off and reports success.
    assert!(
        found.len() >= 4,
        "expected at least the BPE, MRG, cache-index and patch-list readers; found {}: {:?}",
        found.len(),
        found
            .iter()
            .map(|(name, _, _)| name.as_str())
            .collect::<Vec<_>>()
    );

    let mut unpaired = Vec::new();

    for (name, file, line) in &found {
        let relative = file
            .strip_prefix(&src)
            .expect("a reader is found under src")
            .to_string_lossy()
            .replace('\\', "/");

        // Path separator normalisation: the pairing table is written with `/` and this file runs on
        // machines that use either.
        let writer = PAIRINGS
            .iter()
            .find(|(module, _)| *module == relative)
            .map(|(_, writer)| *writer);

        match writer {
            // Paired: one test file has to name both the reader's module and the writer's path.
            Some(writer) => {
                let driven = tests.iter().any(|test| {
                    let text = read(test);
                    text.contains(writer) && text.contains(&format!("::{}", name))
                });
                if !driven {
                    unpaired.push(format!(
                        "{relative}:{line} `{name}` is declared paired with `{writer}`, and no \
                         test file names both"
                    ));
                }
            }
            // Unpaired: the module has to say so, with a reason.
            None => {
                let text = read(file);
                if !text.contains(DECLARATION) {
                    unpaired.push(format!(
                        "{relative}:{line} `{name}` has no entry in the pairing table and no \
                         `{DECLARATION}` line"
                    ));
                }
            }
        }
    }

    assert!(
        unpaired.is_empty(),
        "these readers are tested only against fixtures built by the same hand that wrote them:\n{}",
        unpaired.join("\n")
    );
}

/// The pairing table has to describe the crate as it now is.
///
/// A stale entry — a writer that was renamed or deleted — would otherwise keep this file green
/// while testing nothing, which is the precise failure the table is meant to prevent.
#[test]
fn every_declared_writer_still_exists() {
    let compat = workspace_root().join("crates/psxrt-compat/src");
    let mut missing = Vec::new();

    for (reader, writer) in PAIRINGS {
        let _ = reader;
        // `psxrt_compat::container::bpe` → `crates/psxrt-compat/src/container/bpe.rs`.
        let path = writer
            .trim_start_matches("psxrt_compat::")
            .replace("::", "/");
        if !compat.join(format!("{path}.rs")).is_file() {
            missing.push(writer.to_string());
        }
    }

    assert!(
        missing.is_empty(),
        "the pairing table names writers that are not in the tree: {missing:?}"
    );
}

/// A declared absence has to explain itself.
///
/// The declaration exists so that a legitimate "no writer for this" is recorded rather than
/// silently omitted. Without a length floor it degenerates into a magic string pasted at the top of
/// a file, which restores the exact gap the check was written to close.
#[test]
fn a_declared_absence_explains_itself() {
    let src = workspace_root().join("crates/psxrt-import/src");
    let mut unexplained = Vec::new();

    for file in collect_rust_files(&src) {
        let text = read(&file);
        let Some(at) = text.find(DECLARATION) else {
            continue;
        };

        let window = &text[at..text.len().min(at + 700)];
        if window.split_whitespace().count() < 40 {
            unexplained.push(file.display().to_string());
        }
    }

    assert!(
        unexplained.is_empty(),
        "these files say a writer is not planned without saying why:\n{}",
        unexplained.join("\n")
    );
}
