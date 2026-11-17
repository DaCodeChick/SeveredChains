//! Where game knowledge is allowed to live, and which way the dependency edges may point.
//!
//! # Why a test enforces this and not a comment
//!
//! `psxrt` and `psxrt-import` are documented as reusable — a platform's arithmetic and a set of file
//! formats, neither of which knows that a game called Legend of Dragoon exists. That is only true
//! while nothing puts game facts back into them, and the pressure to do so is constant because the
//! game is the only thing that exercises either crate.
//!
//! Two rules, both cheap to check:
//!
//! 1. **A filename is not a format.** `psxrt-import` may parse an offset table at a caller-chosen
//!    scale; it may not decide the scale from a name. The seven sector-aligned containers on this
//!    disc are a fact about this disc, and the moment they are a constant inside a parser, adding a
//!    second game means editing a parser.
//! 2. **The edge points down.** `psxrt` may not depend on an import crate, and no import crate may
//!    depend on this one. A runtime crate that reads files, or a format crate that asks a game
//!    question, has started deciding at the wrong layer.
//!
//! # Why the name scan looks only at code
//!
//! Same reasoning as `psxrt-import/tests/architecture.rs`, and it matters more here: the reusable
//! crates talk about game names constantly and correctly. `psxrt-import/src/lib.rs` holds up
//! `DRGN0.BIN` as the example of what must not appear in a *parser*. A check that flagged that
//! sentence would be answered with an allow-attribute, and the next violation would be invisible.
//! The target is a literal in a position the compiler would evaluate.
//!
//! # Known limit, stated rather than hidden
//!
//! This reads files, so it needs to run inside the checkout, and it catches a name written as a
//! string literal rather than assembled at runtime. That is not a proof. It is a fence placed where
//! the last three such constants used to sit, so that putting them back is a decision somebody has
//! to make twice — once in the parser, and once here.

use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// The crates that are supposed to be reusable, and therefore blind to this game.
const REUSABLE: [&str; 2] = ["psxrt", "psxrt-import"];

/// A crate may not reach *up* into one of these.
const FORBIDDEN_EDGES: [(&str, &str); 2] = [
    ("psxrt", "psxrt-import"),
    ("psxrt-import", "severedchains-game"),
];

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("tests run from inside the workspace")
        .to_path_buf()
}

fn crate_dir(name: &str) -> PathBuf {
    let path = workspace_root().join("crates").join(name);
    assert!(path.is_dir(), "{name} is expected to be in the workspace");
    path
}

fn read(path: &Path) -> String {
    fs::read_to_string(path)
        .unwrap_or_else(|error| panic!("cannot read {}: {error}", path.display()))
}

/// Every `.rs` file under `directory`.
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

/// Lines the compiler would evaluate. Comments and doc comments are dropped, which is the whole
/// point: see the module header.
fn code_lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines()
        .map(str::trim)
        .filter(|line| !(line.starts_with("//") || line.starts_with("/*") || line.starts_with('*')))
}

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

/// Every crate name a manifest's dependency tables could be pulling in, and whether the table it
/// came from is dev-only. Duplicated from `psxrt-import/tests/architecture.rs` rather than shared,
/// because these are two independent test binaries and the shared home for this would be
/// `tests/support/`, which is reserved for format oracles.
///
/// Returns a superset of the real crate names on purpose: a false positive is a red test, a false
/// negative is a forbidden edge that ships. Names are gathered from a table header, from a key, and
/// from a quoted value, because a renamed dependency —
///
/// ```text
/// [dependencies.mod-tools]
/// package = "severedchains-game"
/// ```
///
/// — puts the crate in the value only, and renaming is precisely how an edge that is supposed to be
/// forbidden gets added without looking like one.
fn dependency_names(manifest: &str) -> Vec<(String, bool)> {
    let mut names: Vec<(String, bool)> = Vec::new();
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
/// Compares with `_` and `.` normalised to `-`, so a manifest cannot dodge the check by spelling
/// the crate the way the Rust module is.
fn production_dependency(manifest: &str, crate_name: &str) -> Vec<String> {
    dependency_names(manifest)
        .into_iter()
        .filter(|(name, dev_only)| !dev_only && name.replace(['_', '.'], "-") == crate_name)
        .map(|(name, dev_only)| format!("{name} (dev-only: {dev_only})"))
        .collect()
}

/// Rule one, first half: a name from this disc is not a fact about a file format.
#[test]
fn the_reusable_crates_do_not_name_this_games_files() {
    let needles = ["DRGN", ".BIN", "SUBMAP", "scripts.csv", "throw_item"];
    let mut violations = Vec::new();

    for name in REUSABLE {
        for file in rust_files(&crate_dir(name).join("src")) {
            let text = read(&file);
            for found in code_lines(&text).filter(|line| needles.iter().any(|n| line.contains(n))) {
                violations.push(format!("{}: {found}", file.display()));
            }
        }
    }

    assert!(
        violations.is_empty(),
        "these lines put a fact about one disc inside a crate that is supposed to be reusable:\n{}",
        violations.join("\n")
    );
}

/// Rule one, second half: and the place that fact belongs has it.
///
/// The pairing matters. Deleting the constant from the parser without putting it anywhere leaves the
/// scaling decision with no home, which is how it gets reinvented at three call sites.
#[test]
fn the_sector_scaled_names_have_exactly_one_home() {
    let game = crate_dir("severedchains-game").join("src/container/mrg.rs");
    let text = read(&game);

    for name in ["DRGN.BIN", "DRGN0.BIN", "DRGN1.BIN", "DRGN21.BIN"] {
        assert!(
            text.contains(name),
            "{name} is sector-scaled on this disc and is no longer listed in \
             crates/severedchains-game/src/container/mrg.rs"
        );
    }

    // And the crate that would need it for a *second* disc does not have it.
    assert!(
        !read(&crate_dir("psxrt-import").join("src/container/mrg.rs")).contains("DRGN.BIN"),
        "the name set has moved back into the parser"
    );
}

/// Rule two: no crate reaches up.
#[test]
fn no_crate_depends_on_a_crate_above_it() {
    let mut violations = Vec::new();

    for (lower, upper) in FORBIDDEN_EDGES {
        let manifest = read(&crate_dir(lower).join("Cargo.toml"));

        let reached = production_dependency(&manifest, upper);

        if !reached.is_empty() {
            violations.push(format!("{lower} -> {upper}: {reached:?}"));
        }
    }

    assert!(
        violations.is_empty(),
        "these dependency edges point the wrong way:\n{}",
        violations.join("\n")
    );
}

/// The oracle independence rule, applied to this crate's own support module.
///
/// `tests/support/index_writer.rs` is the second description of the cache index. The moment it
/// imports `severedchains_game`, `index_round_trip.rs` becomes a check that `Index::parse` agrees
/// with `Index::text`, which was never true and is the arrangement this rule exists to prevent.
#[test]
fn the_index_oracle_still_describes_the_format_independently() {
    let support = crate_dir("severedchains-game").join("tests/support");
    let mut violations = Vec::new();

    for file in rust_files(&support) {
        let text = read(&file);
        let reached: Vec<&str> = ["severedchains_game::", "psxrt_import::"]
            .into_iter()
            .filter(|needle| code_lines(&text).any(|line| line.contains(needle)))
            .collect();

        if !reached.is_empty() {
            violations.push(format!("{}: {reached:?}", file.display()));
        }
    }

    assert!(
        violations.is_empty(),
        "these oracles import the code they are supposed to arbitrate:\n{}",
        violations.join("\n")
    );
}

/// The detector, tested directly. `dependency_names` above is a second copy of the one in
/// `psxrt-import/tests/architecture.rs`, and a copy that is never exercised is a copy that drifts.
/// These pin the same behaviour against the crate name this file actually searches for.
mod detector {
    use super::*;

    #[test]
    fn a_forbidden_crate_is_found_in_whichever_form_a_manifest_writes_it() {
        let forms = [
            "[dependencies]\nseveredchains-game = { path = \"../severedchains-game\" }",
            "[dependencies]\nseveredchains-game.workspace = true",
            "[dependencies.tools]\npackage = \"severedchains-game\"\npath = \"../x\"",
            "[dependencies.severedchains-game.features]\nstd = []",
            "[target.'cfg(unix)'.dependencies]\nseveredchains-game = { path = \"x\" }",
            "[target.'cfg(windows)'.dev-dependencies]\nseveredchains-game = \"1\"",
        ];

        // The last form is dev-only and so is absent by design; the other five must be found.
        for manifest in &forms[..5] {
            assert!(
                !production_dependency(manifest, "severedchains-game").is_empty(),
                "missed a dependency in:\n{manifest}"
            );
        }

        let dev_only = forms[5];
        assert!(
            dependency_names(dev_only)
                .into_iter()
                .all(|(name, dev)| name != "severedchains-game" || dev),
            "a target-specific dev-dependency was not recognised as dev-only"
        );
    }

    #[test]
    fn what_is_not_a_forbidden_edge_is_not_reported() {
        let allowed = [
            "[dev-dependencies]\nproptest = \"1.12.0\"",
            "[features]\nstd = []\nseveredchains-game = [\"dep:severedchains-game\"]",
            "[package]\nname = \"severedchains-game\"",
            "[dependencies]\npsxrt-import.workspace = true",
        ];

        for manifest in allowed {
            assert!(
                production_dependency(manifest, "severedchains-game").is_empty(),
                "flagged a non-edge in:\n{manifest}"
            );
        }
    }
}
