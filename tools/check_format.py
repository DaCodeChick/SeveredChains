#!/usr/bin/env python3
"""Formatting gate for project-owned C/C++ sources (AGENTS.md, docs/guidelines/cpp.md §6).

Two roles:

1. **Whole-style gate.** Runs `clang-format --dry-run --Werror` over every project file,
   so the root `.clang-format` is enforceable rather than advisory — including
   `RemoveBracesLLVM`, which is what encodes the project rule that single-statement
   `if`/`else`/`for`/`while` bodies carry no braces.
2. **Brace backstop.** A structural scan for that one rule, used when clang-format is
   absent *or older than 17* (older versions do not know `RemoveBracesLLVM`, so a silent
   pass there would be a hole rather than a check).

Skips are always loud: the tool prints what it could not verify and exits 0 only after
saying so, mirroring the exported-symbol audit (psx/tests/abi_symbols.cmake).

Usage:  python3 tools/check_format.py [path ...]      # paths override the scan roots
Exit:   0 = clean or skipped with a message, 1 = offenders found.
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

# --- brace backstop ---------------------------------------------------------------

# A control header whose closing paren is on the same line and whose body is a
# separate `{` line. Trailing comments are tolerated.
CONTROL_HEADER = re.compile(
    r"^(?:\s*)(?:if|for|while|else(?:\s+if)?)(?:\s*\(.*\)\s*(?://.*)?|\s*(?://.*)?)$"
)
OPEN_ONLY = re.compile(r"^\s*\{\s*(?://.*)?$")

# Removal is only *safe* (and only what clang-format does) when the braced body is one
# statement that neither declares anything nor is itself control flow: a nested `if`
# would create a dangling-else hazard, and a declaration would gain/lose a scope.
DECLARATION = re.compile(
    r"^(?:const |constexpr |static |auto |std::|[A-Z][A-Za-z0-9_]*\s+[a-z_]\w*\s*(?:=|;|\())"
)
NESTED_CONTROL = re.compile(r"^\s*(?:if|for|while|else|switch|do)\b")


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


def check_braces(path: Path) -> list[str]:
    """Return 'line N: <snippet>' for every single-statement braced body."""
    lines = path.read_text(encoding="utf-8").splitlines()
    findings: list[str] = []
    index = 0
    while index + 1 < len(lines):
        header = lines[index]
        openerIndex = index + 1
        opener = lines[openerIndex]
        index = openerIndex
        if not CONTROL_HEADER.match(header) or not OPEN_ONLY.match(opener):
            continue
        # Walk the braced body: `depth` counts open blocks *inside* the `{` (so it starts
        # at 1) and the body ends at the line that closes it. Everything collected is
        # flattened code text, so a *single* entry means "exactly one statement, one line".
        depth = 1
        body: list[str] = []
        cursor = openerIndex + 1
        while cursor < len(lines):
            text = lines[cursor]
            depth += text.count("{") - text.count("}")
            stripped = text.strip()
            if depth == 0:
                break
            if stripped and not stripped.startswith(("//", "/*", "*")):
                body.append(stripped)
            if len(body) > 1:
                break
            cursor += 1
        if len(body) != 1:
            continue
        statement = body[0]
        if NESTED_CONTROL.match(statement) or statement.endswith("{"):
            continue
        if DECLARATION.match(statement):
            continue
        if statement in ("break;", "continue;", "return;"):
            continue
        findings.append(f"line {index + 1}: {opener.strip()} {statement}")
    return findings


def clang_format_version() -> tuple[int, ...]:
    exe = shutil.which("clang-format")
    if not exe:
        return ()
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return ()
    match = re.search(r"version (\d+)\.(\d+)", out)
    return (int(match.group(1)), int(match.group(0).split(".")[1])) if match else ()


def main(argv: list[str]) -> int:
    paths = [Path(arg).resolve() for arg in argv] or [ROOT / name for name in SCAN_ROOTS]
    files = scan_files(paths)
    if not files:
        print("check_format: no project sources found — nothing checked")
        return 0

    offenders: list[str] = []
    version = clang_format_version()
    exe = shutil.which("clang-format")

    if exe and version >= MIN_CLANG_FORMAT:
        print(f"check_format: clang-format {version[0]}.{version[1]} over {len(files)} file(s) "
              f"(full .clang-format gate, incl. RemoveBracesLLVM)")
        for path in files:
            result = subprocess.run(
                [exe, "--style=file", "--dry-run", "--Werror", str(path)],
                capture_output=True,
                text=True,
                timeout=120,
            )
            if result.returncode != 0:
                offenders.append(str(path.relative_to(ROOT)))
        if offenders:
            print("  run: clang-format -i " + " ".join(offenders))
    else:
        why = "clang-format not found" if not exe else f"clang-format {version[0]}.{version[1]} < 17"
        print(f"check_format: WARNING — {why}; full style gate NOT verified, running the "
              f"brace backstop only over {len(files)} file(s)")
        for path in files:
            for finding in check_braces(path):
                offenders.append(f"{path.relative_to(ROOT)}: {finding}")

    if offenders:
        print(f"check_format: FAIL — {len(offenders)} offender(s)")
        for offender in offenders[:40]:
            print(f"  {offender}")
        if len(offenders) > 40:
            print(f"  ... and {len(offenders) - 40} more")
        return 1
    print("check_format: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))