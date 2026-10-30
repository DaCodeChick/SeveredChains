#!/usr/bin/env python3
"""Formatting gate for project-owned C/C++ sources (AGENTS.md, docs/guidelines/cpp.md §6).

Two roles:

1. **Whole-style gate.** Runs `clang-format --dry-run --Werror` over every project file,
   so the root `.clang-format` is enforceable rather than advisory — including
   `RemoveBracesLLVM`, which encodes the project rule that single-statement
   `if`/`else`/`for`/`while` bodies carry no braces.
2. **Header inclusion style.** Every header must open with `#pragma once`. This is
   independent of role 1: clang-format has no opinion about inclusion guards, so there is
   no tool whose absence it is covering for, and it stays a hard gate on every host.

When no clang-format >= 17 exists on the host, role 1 reports that it could not run and
exits 77. It does not fall back to a hand-written approximation of the style rules. An
earlier version of this script did, and that backstop was wrong: it flagged 41 lines that
clang-format 23.1 reports clean, including braced bodies holding *two* statements where
braces are mandatory (`psx/src/bpe.c`, `src/engine/src/fs.cpp`). A gate that fails correct
code is worse than no gate — it teaches people to add `|| true`, and to delete braces that
were carrying a `dangling-else` guarantee. Reporting "unverified" is the honest shape, and
CTest renders it as `Skipped`, never as `Passed`.

Usage:  python3 tools/check_format.py [path ...]   # paths override the scan roots
Exit:   0 = clean, 1 = offenders found, 77 = could not verify (reported, not passed).
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Project-owned source roots. Third-party code is never formatted here (it is kept
# verbatim), and `psx/migration/legacy` holds the pre-migration originals used by the
# differential test — both stay outside the gate.
SCAN_ROOTS = ("src", "tests", "psx", "fate")
SKIP_DIRS = {"build", "third_party", "external", "legacy"}
SOURCE_SUFFIXES = (".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx")

MIN_CLANG_FORMAT = (17, 0)
SKIPPED = 77

# --- clang-format discovery ---------------------------------------------------------
#
# Probing only the bare name `clang-format` was a defect, not a style choice. The Ubuntu
# 24.04 CI image ships clang-format 16.0.6 / 17.0.6 / 18.1.3 under version-suffixed
# names (`clang-format-18`), so a fully-tooled runner looked like a host with no
# formatter and the gate degraded. Candidates are tried newest-first and the highest
# version wins.
#
# Anything below MIN_CLANG_FORMAT is still unusable, and *finding only a v16 is a skip,
# not a pass*: clang-format 16 does not implement RemoveBracesLLVM, so a clean report
# from it says nothing about the one rule this gate exists to enforce.

CLANG_FORMAT_CANDIDATES = ("clang-format",) + tuple(
    f"clang-format-{major}" for major in (23, 22, 21, 20, 19, 18, 17))


def find_clang_format() -> tuple:
    """Return ``(executable, version)`` for the newest usable clang-format on this host.

    Returns ``("", ())`` when none is installed or none is new enough. Callers must treat
    that as "cannot verify" and say so, never as "clean".
    """
    best_exe, best_version = "", ()
    for name in CLANG_FORMAT_CANDIDATES:
        exe = shutil.which(name)
        if not exe:
            continue
        try:
            out = subprocess.run([exe, "--version"], capture_output=True, text=True,
                                 timeout=30).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        match = re.search(r"version (\d+)\.(\d+)", out)
        if not match:
            continue
        version = (int(match.group(1)), int(match.group(2)))
        if best_version == () or version > best_version:
            best_exe, best_version = exe, version
    if best_version >= MIN_CLANG_FORMAT:
        return (best_exe, best_version)
    return ("", ())


def is_source(path: Path) -> bool:
    return path.suffix in SOURCE_SUFFIXES


def scan_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for base in paths:
        if base.is_file():
            files.append(base)
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or not is_source(path):
                continue
            if SKIP_DIRS & set(path.parts):
                continue
            files.append(path)
    return files


# --- header inclusion-style gate ----------------------------------------------------
#
# Every project header carries inclusion protection; the question is only which kind. The
# convention is `#pragma once` on line 1, and this check is deliberately narrow and needs
# no judgement call: either the file opens with it or it does not. It runs whether or not
# clang-format exists, because inclusion style is not something clang-format checks — so
# this is not a backstop for a missing tool, it is the only check for this property.

PRAGMA_ONCE = re.compile(r"^[ \t]*#[ \t]*pragma[ \t]+once\b")

# `#ifndef GUARD` where the name is an upper-case token: a guard, not a feature switch
# such as `#ifdef _WIN32` or `#ifndef NDEBUG`.
INCLUDE_GUARD = re.compile(r"^[ \t]*#[ \t]*ifndef[ \t]+[A-Z][A-Z0-9_]*[ \t]*$")

HEADER_SUFFIXES = (".h", ".hpp", ".hh", ".hxx")


def display(path: Path) -> str:
    """Repo-relative path for a finding, or the absolute path if it is outside the repo.

    A path passed on the command line may sit anywhere (a scratch tree used to prove the
    gate still bites), and slicing it against ROOT unconditionally made any such run
    crash instead of reporting.
    """
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def check_header_style(path: Path) -> list[str]:
    """Flag a header that is not on the `#pragma once` convention.

    Returns findings, not a pass/fail, so the caller can report them apart from
    clang-format offenders: the remedy for these is an edit, not `clang-format -i`.
    """
    if path.suffix not in HEADER_SUFFIXES:
        return []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not lines:
        return []
    if PRAGMA_ONCE.match(lines[0]):
        return []
    for number, line in enumerate(lines[:8], 1):
        if INCLUDE_GUARD.match(line):
            return [f"line {number}: include guard — the project convention is "
                    f"`#pragma once` on line 1"]
    return ["line 1: no `#pragma once` — a header must carry inclusion protection"]


def main(argv: list[str]) -> int:
    paths = [Path(arg).resolve() for arg in argv] or [ROOT / name for name in SCAN_ROOTS]
    files = scan_files(paths)
    if not files:
        print("check_format: no project sources found — nothing checked")
        return 0

    style_findings: list[str] = []
    for path in files:
        for finding in check_header_style(path):
            style_findings.append(f"{display(path)}: {finding}")

    exe, version = find_clang_format()
    offenders: list[str] = []
    unverified = ""

    if exe:
        print(f"check_format: clang-format {version[0]}.{version[1]} ({exe}) over "
              f"{len(files)} file(s) — full .clang-format gate, incl. RemoveBracesLLVM")
        for path in files:
            result = subprocess.run(
                [exe, "--style=file", "--dry-run", "--Werror", str(path)],
                capture_output=True,
                text=True,
                timeout=120,
            )
            if result.returncode != 0:
                offenders.append(display(path))
        if offenders:
            print("  run: clang-format -i " + " ".join(offenders))
    else:
        unverified = ("no clang-format >= 17 on this host (looked for: "
                      + ", ".join(CLANG_FORMAT_CANDIDATES) + ")")

    if offenders or style_findings:
        for finding in style_findings:
            print(f"  {finding}")
        for offender in offenders[:40]:
            print(f"  {offender}")
        if len(offenders) > 40:
            print(f"  ... and {len(offenders) - 40} more")
        total = len(offenders) + len(style_findings)
        print(f"check_format: FAIL — {total} offender(s) "
              f"({len(style_findings)} inclusion-style, {len(offenders)} formatting)")
        return 1

    if unverified:
        # Loud, and non-zero: `Skipped` in CTest terms. The style rules above were not
        # looked at, and the output says so instead of implying they passed.
        print(f"check_format: NOT VERIFIED — {unverified}")
        print("check_format: the .clang-format rules (whitespace, braces) were NOT "
              "checked; header inclusion style was, and was clean")
        print("check_format: install clang-format >= 17 to make this gate real "
              "(exit 77 = skipped, not passed)")
        return SKIPPED

    print("check_format: OK — formatting and header inclusion style")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))