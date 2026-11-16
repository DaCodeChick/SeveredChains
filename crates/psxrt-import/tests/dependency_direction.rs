//! The dependency-direction guard.
//!
//! # Why a test enforces this and not a comment
//!
//! Two rules, both cheap to check and both easy to break by accident:
//!
//! 1. `psxrt-compat` never appears in any crate's `[dependencies]`. The only permitted edge into it
//!    is a `dev-dependencies` entry used by a round-trip test.
//! 2. `psxrt-compat`'s own sources never name `psxrt_import`, in code or in a manifest.
//!
//! A rule that only lives in `planning/01` gets violated the first time somebody wants a helper
//! that already exists over there, and the violation is expensive in a specific way:
//! `psxrt-compat` is the crate that records *deliberate* departures from reference behaviour. A
//! runtime crate that depends on it has started sourcing behaviour from the escape hatch rather
//! than from the model, and from then on "matches the reference" and "matches `psxrt-compat`" mean
//! different things — with nothing in the tree saying which one a given call site meant.
//!
//! # Why the source scan skips comment lines
//!
//! Because the honest target is a *code* edge. Module documentation names the neighbouring crate
//! constantly and correctly — `container/bpe.rs` in `psxrt-compat` explains itself by reference to
//! the decoder it is tested against — and flagging prose would train everybody to add an
//! allow-attribute within a week. What must not appear is a path expression: `psxrt_compat::` in a
//! `use`, a type, or a macro. Stripping comment lines catches every one of those and nothing else.
//!
//! # Known limit, stated rather than hidden
//!
//! This reads the workspace as files and so needs to run inside the checkout, which is the normal
//! case for `cargo test`. It does not see a dependency introduced by `[patch]`, by a build script,
//! or through a `--cfg`. It is a fence, not a proof. The fence exists so that crossing it is a
//! decision somebody has to notice rather than a line that scrolls past in review.

use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// The crate that must stay at the leaf: nothing in the runtime may depend on it.
const COMPAT_CRATE: &str = "psxrt-compat";

/// The same name in its code form, for the source scan.
const COMPAT_MODULE: &str = "psxrt_compat";

/// The import crate in its code form, for the reverse scan.
const IMPORT_MODULE: &str = "psxrt_import";

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("tests run from inside the workspace")
        .to_path_buf()
}

/// Every crate directory in the workspace, sorted so a failure lists them in a stable order.
fn crates() -> Vec<PathBuf> {
    let mut crates: Vec<PathBuf> = fs::read_dir(workspace_root().join("crates"))
        .expect("crates/ exists")
        .map(|entry| entry.expect("readable entry").path())
        .filter(|path| path.join("Cargo.toml").is_file())
        .collect();
    crates.sort();
    crates
}

fn read(path: &Path) -> String {
    fs::read_to_string(path)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", path.display()))
}

/// Every `.rs` file under this crate's `src/`, so a violation cannot hide in a submodule nobody
/// thinks to grep.
fn sources(crate_dir: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    let mut pending = vec![crate_dir.join("src")];

    while let Some(directory) = pending.pop() {
        if !directory.is_dir() {
            continue;
        }
        for entry in fs::read_dir(&directory).expect("readable directory") {
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

/// The lines that are code, for some value of "code".
///
/// Whole-line comments and doc comments are dropped. This is deliberately not a Rust parser: a
/// commented-out `use psxrt_compat::…` is not a dependency, and neither is one inside a `/* … */`
/// block. Anything subtler than that is out of scope for a fence, and would need the real
/// dependency graph, which cargo already computes for the manifest half of this file.
fn code_lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines()
        .map(str::trim)
        .filter(|line| !(line.starts_with("//") || line.starts_with("/*") || line.starts_with('*')))
}

/// The body of every TOML table whose header ends in `dependencies]`.
fn dependency_lines(manifest: &str) -> Vec<String> {
    let mut lines = Vec::new();
    let mut inside = false;

    for line in manifest.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') {
            inside = trimmed.ends_with("dependencies]") || trimmed.ends_with("dependencies]\"");
            if inside {
                lines.push(format!("section {trimmed}"));
            }
            continue;
        }
        if inside && !trimmed.is_empty() {
            lines.push(trimmed.to_string());
        }
    }

    lines
}

/// Rule one, first half: no crate lists `psxrt-compat` as a production dependency.
#[test]
fn no_crate_depends_on_compat_for_production() {
    let mut violations = Vec::new();

    for crate_dir in crates() {
        let manifest = read(&crate_dir.join("Cargo.toml"));

        // A production table is one that is not `dev` or `test`. Read this as "the table name
        // mentions dependencies and does not say it is only for tests".
        let mut in_production = false;
        for line in manifest.lines() {
            let trimmed = line.trim();
            if trimmed.starts_with('[') {
                in_production =
                    trimmed.ends_with("dependencies]") && !trimmed.contains("dev-dependencies");
                continue;
            }
            if in_production && trimmed.contains(COMPAT_CRATE) {
                violations.push(format!(
                    "{}: `{trimmed}` in a production dependencies table",
                    crate_dir.display()
                ));
            }
        }
    }

    assert!(
        violations.is_empty(),
        "{COMPAT_CRATE} must stay out of every production dependency list:\n{}",
        violations.join("\n")
    );
}

/// Rule one, second half: the one legal edge is a dev-dependency, and only `psxrt-import` takes
/// it. Anything else that grows one has started testing against the escape hatch instead of
/// against a model.
#[test]
fn the_only_dev_dependency_on_compat_is_psxrt_import() {
    let mut unexpected = Vec::new();
    let mut users = Vec::new();

    for crate_dir in crates() {
        let manifest = read(&crate_dir.join("Cargo.toml"));
        let name = crate_dir
            .file_name()
            .expect("a crate directory has a name")
            .to_string_lossy()
            .into_owned();

        let takes_the_edge = dependency_lines(&manifest)
            .iter()
            .any(|line| line.contains(COMPAT_CRATE));

        if takes_the_edge {
            users.push(name.clone());
            if name != "psxrt-import" {
                unexpected.push(name);
            }
        }
    }

    assert!(
        unexpected.is_empty(),
        "only psxrt-import may test against {COMPAT_CRATE}; these do too: {unexpected:?}"
    );
    assert!(
        users.contains(&"psxrt-import".to_string()),
        "the round-trip tests need the dev-dependency, and it has gone missing"
    );
}

/// Rule two: `psxrt-compat` has no project dependencies at all, in either table.
///
/// This is the other half of the same fence. The crate may be *read* by tests; it may not read the
/// models, because then a writer and its oracle share one definition of correct and the round trip
/// stops being evidence about anything.
#[test]
fn compat_has_no_project_dependencies() {
    let compat = workspace_root().join("crates").join(COMPAT_CRATE);
    assert!(compat.is_dir(), "{COMPAT_CRATE} must exist to be guarded");

    let manifest = read(&compat.join("Cargo.toml"));
    let lines = dependency_lines(&manifest);
    let offenders: Vec<&str> = lines
        .iter()
        .filter(|line| !line.starts_with("section "))
        .filter(|line| {
            // `thiserror` and anything like it is fine: the rule is about *project* crates.
            line.contains("psxrt")
        })
        .map(String::as_str)
        .collect();

    assert!(
        offenders.is_empty(),
        "{COMPAT_CRATE} must not depend on any workspace crate; found:\n{}",
        offenders.join("\n")
    );
}

/// Rule three: no crate's own sources contain a code path into `psxrt_compat`.
#[test]
fn no_source_reaches_into_compat() {
    let needle = format!("{COMPAT_MODULE}::");
    let mut violations = Vec::new();

    for crate_dir in crates() {
        for file in sources(&crate_dir) {
            let text = read(&file);
            if code_lines(&text).any(|line| line.contains(&needle)) {
                violations.push(file.display().to_string());
            }
        }
    }

    assert!(
        violations.is_empty(),
        "no crate may name `{COMPAT_MODULE}` in code, only in a dev-dependency and in prose:\n{}",
        violations.join("\n")
    );
}

/// Rule four: `psxrt-compat` cannot reach back into `psxrt-import`.
///
/// There is no allowed exception. A `dev-dependencies` cycle between packages is already rejected
/// by cargo, so the code-level check is the one that can actually fire — and if it ever does, the
/// round trip it enables proves nothing and should be deleted rather than blessed.
#[test]
fn compat_cannot_reach_back_into_import() {
    let compat = workspace_root().join("crates").join(COMPAT_CRATE);
    let mut violations = Vec::new();

    for file in sources(&compat) {
        let text = read(&file);
        if code_lines(&text).any(|line| line.contains(IMPORT_MODULE)) {
            violations.push(file.display().to_string());
        }
    }

    if read(&compat.join("Cargo.toml")).contains("psxrt-import") {
        violations.push(compat.join("Cargo.toml").display().to_string());
    }

    assert!(
        violations.is_empty(),
        "{COMPAT_CRATE} must not depend on psxrt-import in either direction:\n{}",
        violations.join("\n")
    );
}
