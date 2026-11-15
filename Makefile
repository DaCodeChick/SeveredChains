# Severed Chains Rust workspace — developer entry points.
#
# Everything here is a wrapper around a script or a cargo command, so a target can be run by
# hand if the Makefile is ever inconvenient. `test` deliberately prepares the golden test data
# before running the suite: the fixtures are embedded at compile time, but the target-directory
# copies are what a run-time reader will use, and the two must not drift apart.

CARGO ?= cargo
FIXTURES := scripts/prepare_test_fixtures.sh
ENDINGS := scripts/audit_file_endings.py

.PHONY: all check test test-prepare check-fixtures audit-endings unit clippy clean

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
