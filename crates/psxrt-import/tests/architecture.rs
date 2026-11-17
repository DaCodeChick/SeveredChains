//! The dependency-direction and oracle-independence guard.
//!
//! # Why a test enforces this and not a comment
//!
//! Three rules, all cheap to check and all easy to break by accident:
//!
//! 1. Nothing in `crates/` is allowed to name `psxrt-compat`. It was retired on 2026-11-16; the
//!    code that justified it is still in the tree, as test-only oracles, and it moved to where it is
//!    used. The crate was the departure, not the arrangement.
//! 2. A runtime crate must not depend on an import crate.
//! 3. A module under any crate's `tests/support/` must not import the crate whose behaviour it is
//!    supposed to arbitrate.
//!
//! Rule 1 is a tripwire rather than a rule about the present. It costs nothing and it catches the
//! specific regression that would otherwise be invisible: somebody re-adds a writer crate because
//! that is where writers went, and the workspace quietly goes back to having a place where
//! "matches the reference" and "matches our own encoder" mean the same thing.
//!
//! Rule 3 is the one that still does real work, and it is the general form of what rule 1 used to
//! be. A round trip is only evidence when the two sides were written separately. Put the encoder
//! next to the decoder and let it import the decoder's types and the test measures one opinion
//! twice, which is why `mrg_round_trip.rs` and `bpe_round_trip.rs` existed in the first place — the
//! crate boundary was never the point, the independence was. An oracle that imports its own subject
//! has to be deleted rather than blessed, because it reads as coverage while providing none.
//!
//! # Why the source scan skips comment lines
//!
//! Because the honest target is a *code* edge. Module documentation names neighbouring crates
//! constantly and correctly — `container/bpe.rs` explains itself by reference to the decoder it is
//! tested against — and flagging prose would train everybody to add an allow-attribute within a
//! week. What must not appear is a path expression: `psxrt_import::` inside a `use`, a type, or a
//! macro. Stripping comment lines catches every one of those and nothing else.
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

/// Retired. Nothing in `crates/` may name it, in a manifest or in code.
const RETIRED_CRATE: &str = "psxrt-compat";

/// Keys that configure a dependency rather than name one.
const DEPENDENCY_OPTIONS: [&str; 8] = [
    "package",
    "version",
    "features",
    "default-features",
    "optional",
    "path",
    "git",
    "rev",
];

/// The same crate in its code form.
const RETIRED_MODULE: &str = "psxrt_compat";

/// The crate whose readers the oracles under `tests/support/` are supposed to arbitrate.
const SUBJECT: &str = "psxrt_import";

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

/// Every `.rs` file under `directory`, so a violation cannot hide in a submodule nobody thinks to
/// grep.
fn rust_files(directory: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    let mut pending = vec![directory.to_path_buf()];

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
/// commented-out `use psxrt_import::…` is not a dependency, and neither is one inside a `/* … */`
/// block. Anything subtler than that is out of scope for a fence, and would need the real
/// dependency graph — which cargo already computes for the manifest half of this file.
fn code_lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines()
        .map(str::trim)
        .filter(|line| !(line.starts_with("//") || line.starts_with("/*") || line.starts_with('*')))
}

/// Every crate name a manifest's dependency tables could be pulling in, and whether the table it
/// came from is dev-only.
///
/// Reading the *table* rather than grepping the file is the point: a `dev-dependencies` entry is a
/// legal arrangement in this workspace and a `[dependencies]` one is not, and the two look identical
/// to a substring search.
///
/// This is deliberately not a TOML parser. It returns a *superset* of the real crate names and lets
/// the callers filter for a specific forbidden crate, because the two failure modes are not equally
/// expensive: a false positive is a red test somebody reads, and a false negative is a forbidden
/// edge that ships.
///
/// Names are gathered from three places, because a dependency can be declared in three places and
/// renaming is why all three are needed. Each of these pulls in `psxrt-import`, and only the first
/// puts the name where a search of table keys would find it:
///
/// ```text
/// [dependencies]
/// psxrt-import = { path = "../psxrt-import" }
///
/// [dependencies.lod]
/// package = "psxrt-import"
/// path = "../psxrt-import"
///
/// [dependencies.psxrt-import.features]
/// std = []
/// ```
///
/// The middle form renames the crate to `lod`. A check that read only table keys waved it through,
/// which is exactly the smuggled-edge class this file exists to catch.
fn dependency_names(manifest: &str) -> Vec<(String, bool)> {
    let mut names: Vec<(String, bool)> = Vec::new();

    // `Some(true)` inside a dev-only table, `Some(false)` inside a table a build pulls in, `None`
    // anywhere else: a `[features]` section is not a dependency.
    let mut table: Option<bool> = None;

    for line in manifest.lines() {
        let trimmed = line.trim();

        if let Some(header) = trimmed
            .strip_prefix('[')
            .and_then(|inner| inner.strip_suffix(']'))
        {
            // A table path, split on `.`. `[dependencies.lod]`, `[dev-dependencies]` and
            // `[target.'cfg(unix)'.dependencies]` are all dependency tables, and the last one is
            // why the check looks at *every* segment rather than the first: here the first segment
            // is `target` and the crate-bearing word is the last.
            let segments: Vec<&str> = header.split('.').collect();

            let dependency_table = segments.iter().any(|s| s.ends_with("dependencies"));
            if !dependency_table {
                table = None;
                continue;
            }

            let dev_only = segments.contains(&"dev-dependencies");
            table = Some(dev_only);

            // In `[dependencies.lod]` and `[dependencies.psxrt-import.features]` the crate is
            // named in the header, in the segment after the table kind.
            let crate_name = segments
                .iter()
                .position(|s| s.ends_with("dependencies"))
                .and_then(|at| segments.get(at + 1))
                .filter(|name| !name.is_empty());

            if let Some(crate_name) = crate_name {
                names.push((crate_name.to_string(), dev_only));
            }
            continue;
        }

        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        let Some((key, value)) = trimmed.split_once('=') else {
            continue;
        };

        // Outside a dependency table, `features = [...]` is a package feature list and `name = "…"`
        // is the package's own name. Neither is a dependency.
        let Some(dev_only) = table else {
            continue;
        };

        let key = key.trim();

        // Inside a dependency table a dotted key is a dependency plus a sub-key, so the crate is
        // the first segment. `default-features = false` names no crate, and is skipped below.
        let name = key.split('.').next().unwrap_or(key);
        if !DEPENDENCY_OPTIONS.contains(&name) && !name.is_empty() {
            // `proptest + "macros"` names the crate before the ` + `.
            let name = name.split_once(" + ").map_or(name, |(name, _)| name);
            names.push((name.to_string(), dev_only));
        }

        // A rename puts the real crate in the value, and an inline table puts a path there too.
        for quoted in value.split('"').skip(1).step_by(2) {
            if !quoted.is_empty() {
                names.push((quoted.to_string(), dev_only));
            }
        }
    }

    names
}

/// Does this manifest pull in `crate_name` anywhere outside a dev-only table?
///
/// Compares with `_` and `.` normalised to `-` so a manifest cannot dodge the check by spelling
/// the crate the way the Rust module is spelled.
fn production_dependency(manifest: &str, crate_name: &str) -> Vec<String> {
    dependency_names(manifest)
        .into_iter()
        .filter(|(name, dev_only)| !dev_only && name.replace(['_', '.'], "-") == crate_name)
        .map(|(name, dev_only)| format!("{name} (dev-only: {dev_only})"))
        .collect()
}

/// Rule one: the retired crate stays retired.
#[test]
fn nothing_names_the_retired_compat_crate() {
    let mut violations = Vec::new();

    if workspace_root().join("crates").join(RETIRED_CRATE).exists() {
        violations.push(format!("crates/{RETIRED_CRATE} exists again"));
    }

    for crate_dir in crates() {
        let manifest = read(&crate_dir.join("Cargo.toml"));
        if manifest.contains(RETIRED_CRATE) {
            violations.push(format!("{}: Cargo.toml", crate_dir.display()));
        }

        for file in rust_files(&crate_dir.join("src")) {
            if read(&file).contains(RETIRED_MODULE) {
                violations.push(file.display().to_string());
            }
        }
    }

    assert!(
        violations.is_empty(),
        "{RETIRED_CRATE} was retired; these name it:\n{}",
        violations.join("\n")
    );
}

/// Rule two: no runtime crate depends on an import crate.
///
/// The direction is the whole reason `psxrt` can be reasoned about without knowing anything about
/// files. A draw loop that reaches for a container reader has started deciding format questions at
/// the wrong layer, and the divergence that follows is invisible from either side on its own.
#[test]
fn runtime_does_not_depend_on_import() {
    let runtime = workspace_root().join("crates/psxrt");
    let manifest = read(&runtime.join("Cargo.toml"));

    let edges = production_dependency(&manifest, "psxrt-import");

    assert!(
        edges.is_empty(),
        "`psxrt` must not depend on `psxrt-import`: {}\n{}",
        edges.join("\n"),
        "a runtime crate that reads files has stopped being a runtime crate"
    );
}

/// Rule three: an oracle may not import the crate it arbitrates.
///
/// This is checked across every crate, because the temptation is not specific to importing. The
/// check is deliberately about *path expressions* rather than about whether a crate is listed in
/// `[dev-dependencies]`: a support module that reaches its subject through a re-export is still
/// reading its subject's answer, and a support module that does not is still independent even when
/// the manifest looks suspicious.
#[test]
fn an_oracle_cannot_import_its_own_subject() {
    let mut violations = Vec::new();

    for crate_dir in crates() {
        let support = crate_dir.join("tests/support");
        if !support.is_dir() {
            continue;
        }

        for file in rust_files(&support) {
            let text = read(&file);
            let reached: Vec<&str> = ["psxrt_import::", "severedchains_game::"]
                .into_iter()
                .filter(|needle| code_lines(&text).any(|line| line.contains(needle)))
                .collect();

            if !reached.is_empty() {
                violations.push(format!("{}: {reached:?}", file.display()));
            }
        }
    }

    assert!(
        violations.is_empty(),
        "these oracles import the code they are supposed to arbitrate, and their round trips \
         prove nothing:\n{}",
        violations.join("\n")
    );
}

/// The same rule, from the other end: the oracle directory has to be *outside* `src/`.
///
/// A writer compiled into the crate under test is not an oracle at all — it ships, it can be called
/// from production, and a test that uses it is a tautology with extra steps. This catches the
/// version of the mistake where somebody moves a support module "somewhere tidier" and puts it in
/// `src/`.
#[test]
fn no_writer_lives_inside_a_crate_under_test() {
    let mut violations = Vec::new();

    for crate_dir in crates() {
        for file in rust_files(&crate_dir.join("src")) {
            let name = file
                .file_name()
                .expect("a file has a name")
                .to_string_lossy();
            if name.starts_with("write") || name.contains("_writer") || name.contains("_write") {
                violations.push(file.display().to_string());
            }
        }
    }

    assert!(
        violations.is_empty(),
        "these files look like writers and are inside a `src/` tree:\n{}",
        violations.join("\n")
    );
}

/// The `Subject` constant above is used by the doc comment's argument, and a stale name in this
/// file is worth catching: it means the rule was rewritten and the tests were not.
#[test]
fn the_subject_of_rule_three_is_a_crate_that_exists() {
    assert!(
        workspace_root()
            .join("crates")
            .join(SUBJECT.replace('_', "-"))
            .is_dir(),
        "{SUBJECT} is named as an oracle subject but no such crate is in the workspace"
    );
}

/// The detector, tested directly.
///
/// The live dependency graph makes the `psxrt-import -> severedchains-game` edge structurally
/// impossible rather than merely forbidden — it would be a package cycle, so cargo refuses to build
/// it — which means the second half of `no_crate_depends_on_a_crate_above_it` cannot be exercised
/// end to end. These cover the parser that both guards share, so a future edit that blinds it shows
/// up here instead of quietly succeeding.
mod detector {
    use super::*;

    /// Every way of naming a crate that must be caught, and the shape that must not be.
    #[test]
    fn a_forbidden_crate_is_found_in_whichever_form_a_manifest_writes_it() {
        let forms = [
            "[dependencies]\npsxrt-import = { path = \"../psxrt-import\" }",
            "[dependencies]\npsxrt-import.workspace = true",
            "[dependencies]\npsxrt-import = \"1\"",
            "[dependencies.reader]\npackage = \"psxrt-import\"\npath = \"../psxrt-import\"",
            "[dependencies.psxrt-import.features]\nstd = []",
            "[target.'cfg(unix)'.dependencies]\npsxrt-import = { path = \"x\" }",
        ];

        for manifest in forms {
            assert!(
                !production_dependency(manifest, "psxrt-import").is_empty(),
                "the detector missed a dependency in:\n{manifest}"
            );
        }
    }

    /// A dev-only edge is legal in this workspace, and a `[features]` list is not a dependency at
    /// all. A detector that flags these gets silenced within a week, which is how a fence becomes
    /// decoration.
    #[test]
    fn what_is_not_a_forbidden_edge_is_not_reported() {
        let allowed = [
            // Legal: a test-only oracle dependency, named and pinned in the manifest.
            "[dev-dependencies]\nproptest = \"1.12.0\"",
            // Not a dependency: a package feature list, then the package's own name.
            "[features]\nstd = []\npsxrt-import = [\"dep:psxrt-import\"]",
            "[package]\nname = \"psxrt-import\"",
            // A different crate entirely.
            "[dependencies]\nthiserror.workspace = true",
        ];

        for manifest in allowed {
            assert!(
                production_dependency(manifest, "psxrt-import").is_empty(),
                "the detector flagged a non-edge in:\n{manifest}"
            );
        }
    }

    /// The `[features]` case above only stays quiet because a non-dependency table resets the
    /// state. Assert the reset itself, so reordering a manifest cannot inherit a stale table.
    #[test]
    fn a_table_boundary_ends_the_previous_dependency_table() {
        let manifest =
            "[dependencies.reader]\npackage = \"psxrt-import\"\n\n[features]\nsomething = []";
        assert_eq!(production_dependency(manifest, "psxrt-import").len(), 1);

        let reordered = "[features]\nsomething = []\n\n[dev-dependencies]\nproptest = \"1\"";
        assert!(production_dependency(reordered, "psxrt-import").is_empty());
    }
}
