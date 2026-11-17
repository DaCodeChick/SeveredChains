//! The round-trip inventory.
//!
//! # What this checks, in one sentence
//!
//! Every read entry point in `psxrt-import/src` either has a paired writer under `tests/support/`
//! being exercised against it, or declares in its own module header that no writer is planned — and
//! a module that does neither is a gap this test reports.
//!
//! # Why a fence is needed for something so small
//!
//! The natural failure is not that somebody deletes a writer. It is that somebody adds a decoder,
//! writes a test that builds its input by hand, and never asks the pairing question. Six months
//! later the hand-built fixture and the decoder agree with each other and with nothing else, which
//! is exactly the arrangement a round trip exists to rule out.
//!
//! This replaced an earlier check that worked by grepping the test tree for the writer crate's name.
//! That passed with the pairing deleted, because `parse` occurs in `index_round_trip.rs` and so does
//! `psxrt_compat`, which "proved" that `PatchList::parse` had an oracle. Pairings are therefore
//! stated as data below and each one is verified against the specific test file that has to exercise
//! both sides.
//!
//! # What the pairing is now, and what it costs
//!
//! Writers used to live in a separate crate, `psxrt-compat`, and that crate is gone: it had become a
//! home for anything whose placement nobody wanted to argue about, including two writers with no
//! caller. The writers that were used moved to `tests/support/`, which is still not `src/` and is
//! still not allowed to import this crate — `tests/architecture.rs` checks both.
//!
//! The honest cost: a support module inside the same package is one `use` away from being welded to
//! its subject, and no file-scanning test can make that physically impossible. What it can do is
//! fail the build when the weld happens. That is the whole claim, and it is worth making plainly
//! rather than describing as a guarantee.
//!
//! # The escape hatch, and why it is worded rather than attributed
//!
//! A module may declare `No paired writer` in its header instead of having an oracle. That is
//! deliberate: an `#[allow]`-style attribute gets added to make a warning go away, whereas a
//! sentence has to be true and is something a reviewer can argue with. The companion test below
//! requires the declaration to come with a reason.

use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// A reader module, the oracle that arbitrates it, the test file that pairs them, and the path
/// prefix that test uses to call the oracle.
///
/// Adding a decoder here without a writer means also adding a line to the module's header explaining
/// why; adding one with a writer means adding its quadruple here. Either way somebody has answered
/// the question before the code merges.
///
/// The oracle path is relative to `crates/psxrt-import/`, the test path to
/// `crates/psxrt-import/tests/`.
const PAIRINGS: &[(&str, &str, &str, &str)] = &[
    // The call prefix is in the table because naming a writer is not the same as using it, and a
    // `use` line on its own is exactly how a pairing gets faked.
    (
        "container/bpe.rs",
        "bpe_writer",
        "bpe_round_trip.rs",
        "bpe_writer::",
    ),
    (
        "container/mrg.rs",
        "mrg_writer",
        "mrg_round_trip.rs",
        "write::",
    ),
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
    let import = root.join("crates/psxrt-import");
    let src = import.join("src");
    let tests = collect_rust_files(&import.join("tests"));
    let found = readers(&src);

    // The scan must find the readers that are certainly there. Without this, a rename of the prefix
    // list or of the `pub fn` spelling switches the whole test off and reports success.
    assert!(
        found.len() >= 2,
        "expected at least the BPE and MRG readers; found {}: {:?}",
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

        let pairing = PAIRINGS.iter().find(|(module, ..)| *module == relative);

        match pairing {
            // Paired: the oracle has to exist, and one test file has to name both sides of it.
            Some((_, oracle, test_name, call_prefix)) => {
                let oracle_path = import
                    .join("tests/support")
                    .join(oracle)
                    .with_extension("rs");
                if !oracle_path.is_file() {
                    unpaired.push(format!(
                        "{relative}:{line} `{name}` is declared paired with `{oracle}`, which is \
                         not in the tree"
                    ));
                    continue;
                }

                // Naming the oracle is not enough; the test has to call into it. `write` is the
                // verb every oracle exposes, so a file that only mentions the path in prose fails.
                let driven = tests.iter().any(|test| {
                    let text = read(test);
                    let named = test.file_name().is_some_and(|name| name == *test_name);
                    named
                        && text.contains("mod support;")
                        && text.contains(oracle)
                        && text.contains(call_prefix)
                        && text.contains(&format!("::{name}"))
                });
                if !driven {
                    unpaired.push(format!(
                        "{relative}:{line} `{name}` names `{test_name}` as the file that drives \
                         `{oracle}`, and that file does not exercise both sides"
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

/// The pairing table has to describe the tree as it now is.
///
/// A stale entry — a writer that was renamed or deleted — would otherwise keep this file green while
/// testing nothing, which is the precise failure the table is meant to prevent.
#[test]
fn every_declared_oracle_still_exists() {
    let import = workspace_root().join("crates/psxrt-import");
    let mut missing = Vec::new();

    for (reader, oracle, test_name, _) in PAIRINGS {
        let oracle_path = import
            .join("tests/support")
            .join(oracle)
            .with_extension("rs");
        if !oracle_path.is_file() {
            missing.push(format!("{} (paired with {reader})", oracle_path.display()));
        }
        if !import.join("tests").join(test_name).is_file() {
            missing.push(format!("tests/{test_name} (drives {oracle})"));
        }
    }

    assert!(
        missing.is_empty(),
        "the pairing table names files that are not in the tree:\n{}",
        missing.join("\n")
    );
}

/// A declared absence has to explain itself.
///
/// The declaration exists so that a legitimate "no writer for this" is recorded rather than silently
/// omitted. Without a length floor it degenerates into a magic string pasted at the top of a file,
/// which restores the exact gap the check was written to close.
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
