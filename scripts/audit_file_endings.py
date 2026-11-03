#!/usr/bin/env python3
"""Audit project text files for line-ending hygiene.

Checks (text files only):
  M  missing final newline (non-empty file does not end with \\n)
  T  trailing blank line(s) (file ends with two or more \\n)
  C  CRLF line endings present

Binary detection uses git's heuristic (NUL byte in the first 8000 bytes).
Binary files are reported as skipped, never modified.

Scope:
  - files tracked by git in the current repository
  - untracked, non-ignored files (new files not yet committed)
  - everything is resolved relative to the repository root; nothing outside
    it is ever read or written

Fixes applied by --fix (minimal, line-ending only):
  C  CRLF -> LF
  M  append a single final newline
  T  collapse trailing blank lines to a single final newline

Usage:
  scripts/audit_file_endings.py          # audit only
  scripts/audit_file_endings.py --fix    # audit and correct violations
  scripts/audit_file_endings.py --json   # machine-readable output

Exit codes: 0 = clean (or all violations fixed), 1 = violations remain or
read errors occurred, 2 = usage/environment error.
"""

import json
import subprocess
import sys
from pathlib import Path

HEAD_LIMIT = 8000  # git's binary-detection window


def repo_files(root: Path):
    """Yield (path, tracked: bool) for tracked and untracked-non-ignored files."""
    tracked = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True, capture_output=True,
    ).stdout.decode("utf-8", "surrogateescape").split("\0")
    untracked = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--others", "--exclude-standard", "-z"],
        check=True, capture_output=True,
    ).stdout.decode("utf-8", "surrogateescape").split("\0")
    seen = set()
    for name in tracked:
        if not name:
            continue
        p = root / name
        seen.add(p)
        yield p, True
    for name in untracked:
        if not name:
            continue
        p = root / name
        if p not in seen:
            seen.add(p)
            yield p, False


def is_binary(data_head: bytes) -> bool:
    return b"\x00" in data_head


def audit_file(path: Path):
    """Return (issues, skipped_binary, read_error). issues: list of 'M'/'T'/'C'."""
    try:
        data = path.read_bytes()
    except OSError as e:
        return [], False, str(e)
    if is_binary(data[:HEAD_LIMIT]):
        return [], True, None
    issues = []
    if b"\r\n" in data:
        issues.append("C")
    if data:
        if not data.endswith(b"\n"):
            issues.append("M")
        if data.endswith(b"\n\n"):
            issues.append("T")
    return issues, False, None


def fix_file(path: Path):
    """Correct violations in place (line-ending only). Return applied fix codes."""
    original = path.read_bytes()
    out = original
    fixed = []
    if b"\r\n" in out:
        out = out.replace(b"\r\n", b"\n")
        fixed.append("C")
    if out:
        candidate = out.rstrip(b"\n") + b"\n"
        if out != candidate:
            if not out.endswith(b"\n"):
                fixed.append("M")
            if out.endswith(b"\n\n"):
                fixed.append("T")
            out = candidate
    if out != original:
        path.write_bytes(out)
    return fixed


def main(argv):
    as_json = "--json" in argv
    do_fix = "--fix" in argv

    try:
        root = Path(subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            check=True, capture_output=True,
        ).stdout.decode().strip())
    except (subprocess.CalledProcessError, OSError) as e:
        print(f"error: not inside a git repository ({e})", file=sys.stderr)
        return 2

    results = {"files": [], "skipped_binary": [], "errors": [], "counts": {}}
    total_checked = 0
    total_violations = 0
    by_issue = {"M": 0, "T": 0, "C": 0}

    for path, tracked in repo_files(root):
        rel = str(path.relative_to(root))
        issues, binary, err = audit_file(path)
        if err:
            results["errors"].append({"path": rel, "error": err})
            continue
        if binary:
            results["skipped_binary"].append(rel)
            continue
        total_checked += 1
        if issues:
            total_violations += len(issues)
            for code in issues:
                by_issue[code] += 1
            entry = {"path": rel, "tracked": tracked, "issues": issues}
            if do_fix:
                entry["fixed"] = fix_file(path)
            results["files"].append(entry)

    results["counts"] = {
        "checked": total_checked,
        "skipped_binary": len(results["skipped_binary"]),
        "violating_files": len(results["files"]),
        "violations": total_violations,
        "by_issue": by_issue,
        "fixed": do_fix,
        "errors": len(results["errors"]),
    }

    if as_json:
        print(json.dumps(results, indent=2))
    else:
        labels = {"M": "missing final newline", "T": "trailing blank line(s)", "C": "CRLF endings"}
        for entry in results["files"]:
            codes = ",".join(entry["issues"])
            tag = " [fixed: %s]" % ",".join(entry["fixed"]) if entry.get("fixed") else ""
            print(f"  {entry['path']}  {codes} ({'/'.join(labels[c] for c in entry['issues'])}){tag}")
        for e in results["errors"]:
            print(f"  ERROR {e['path']}: {e['error']}")
        c = results["counts"]
        print(f"\nChecked {c['checked']} text file(s); skipped {c['skipped_binary']} binary file(s).")
        if c["errors"]:
            print(f"Errors: {c['errors']} file(s) could not be read.")
        if c["violations"]:
            if do_fix and not results["errors"]:
                print(f"Violations: {c['violations']} found and fixed across {c['violating_files']} file(s) "
                      f"(M={by_issue['M']}, T={by_issue['T']}, C={by_issue['C']}).")
            else:
                print(f"Violations: {c['violations']} across {c['violating_files']} file(s) "
                      f"(M={by_issue['M']}, T={by_issue['T']}, C={by_issue['C']}).")
        else:
            print("Violations: none.")

    return 1 if (results["errors"] or (results["files"] and not do_fix)) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
