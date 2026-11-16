# Severed Chains Rust workspace — developer entry points.
#
# Everything here is a wrapper around a script or a cargo command, so a target can be run by
# hand if the Makefile is ever inconvenient. `test` deliberately prepares the golden test data
# before running the suite: the fixtures are embedded at compile time, but the target-directory
# copies are what a run-time reader will use, and the two must not drift apart.

CARGO ?= cargo
FIXTURES := scripts/prepare_test_fixtures.sh
ENDINGS := scripts/audit_file_endings.py
CITATIONS := scripts/citation_audit.py

.PHONY: all check test test-prepare check-fixtures audit-endings citations unit clippy clean

all: check

## test: prepare the golden test data, then run the whole suite
test: test-prepare
	$(CARGO) test --workspace

## test-prepare: copy canonical fixtures into the target tree and verify them
test-prepare:
	@bash $(FIXTURES)

## check-fixtures: verify the canonical and copied fixtures without touching anything
check-fixtures:
	@bash $(FIXTURES) --check

## audit-endings: file-ending audit (LF, one final newline, no CRLF)
audit-endings:
	@python3 $(ENDINGS)

## citations: re-check the corpus's reference-code citations (opt-in, TD-25).
## Needs the four reference repositories in one directory. Either point SEVERED_CHAINS_REPOS at
## their parent or pass --repos-root. With neither set, the script reports and exits 0, which is
## what keeps `make check` free of any reference-repository dependency.
##   SEVERED_CHAINS_REPOS=/path/to/parent make citations
## Add --apply to rewrite pointers that moved but whose quoted code is unchanged; add --strict to
## fail on unresolved citations. Unresolved items belong in research/citation-exceptions.md.
## a relocated pointer from a correct one: without it the script reports 1 198 of 1 330 as "resolved",
## with it 925 of 1 330. `--revision HEAD` means "whatever is checked out now" — pass an explicit sha
## when you want a reproducible number (a full 40-char sha for --revision currently mis-resolves
## files; use the short form, e.g. make citations CITATION_TARGET=d47ac8637). Override both knobs with make citations VAR=x.
CITATION_TARGET ?= HEAD
CITATION_BASELINE ?= 0c08c679b6b54e167b00851f1f1458d4dea3eade
citations:
	@python3 $(CITATIONS) --revision $(CITATION_TARGET) --from $(CITATION_BASELINE) --from-repo Severed-Chains

## unit: library tests only
unit:
	$(CARGO) test --workspace --lib

## clippy: lints over library and test code
clippy:
	$(CARGO) clippy --workspace --all-targets

## check: the milestone gate — fixtures, tests, and the ending audit
check: test
	python3 $(ENDINGS)

clean:
	rm -rf target
