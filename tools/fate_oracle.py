#!/usr/bin/env python3
"""Fate oracle harness — drive the Java Script-Recompiler reference to generate
the goldens that the native `fate` toolchain is measured against.

Opt-in only. Nothing here runs in a default configure/build/test pass: the oracle
needs a JDK, Gradle, a network fetch of six Maven artefacts on a cold cache, and a
copy of a read-only AGPL reference repository. The goldens it produces are checked
into `fate/testdata/golden/`; this script exists so those goldens can be reproduced
from a clean machine, and so a future `fate compile` can be diffed against them.

Why a Java oracle at all: differential agreement with the reference is the acceptance
method for `fate compile`. Self-consistency tests would only prove the C port agrees
with itself.

Exit codes
  0   success (all fixtures matched / goldens written)
  1   drift, missing golden, or a reference run that failed unexpectedly
  77  skipped — prerequisites absent (CMake SKIP_RETURN_CODE convention)

Conventions that are part of the contract, not incidental:
  * `--ref` is READ-ONLY. It is copied into the scratch tree and built there.
  * The meta bundle is taken from a local directory. `snapshot` is deliberately not
    used: the reference always re-fetches `snapshot`, which makes goldens depend on
    whatever the server returned that day.
  * `.expected` is byte-exact reference output. Trailing spaces and the leading blank
    line are load-bearing; a normalising diff would hide real drift.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
REPO = Path(__file__).resolve().parent.parent
DEFAULT_TESTDATA = REPO / "fate" / "testdata"
DEFAULT_REF = Path.home() / "Documents" / "GitHub" / "Script-Recompiler"
DEFAULT_META = Path.home() / "Documents" / "GitHub" / "Severed-Chains" / "patches" / "meta"
DEFAULT_SCRATCH = Path.home() / ".cache" / "sc-fate-oracle"
META_VERSION = "m1"
META_FILES = (
    "descriptions.csv",
    "params.csv",
    "enums.csv",
    "legend.game.combat.bent.BattleEntityStat.csv",
    "legend.game.combat.types.BattleSoundUsage.csv",
)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
# `16:56:25.050 [main org.legendofdragoon.scripting.Shell:237] <ansi>ERROR<m>: msg`
LOG_PREFIX = re.compile(r"^\d\d:\d\d:\d\d\.\d+ \[[^\]]*\]\s*")
DIAGNOSTIC = re.compile(r"(ERROR|token recognition error|Exception in thread)")
# The reference prints a fixed banner before the collected diagnostics. It carries no
# information the list itself does not, so it is not part of a golden.
BANNER = "There were errors during compilation:"


def read_manifest(testdata: Path) -> dict:
    with open(testdata / "manifest.json", encoding="utf-8") as handle:
        return json.load(handle)


def tool_path(scratch: Path) -> Path:
    return scratch / "src" / "build" / "install" / "script-recompiler" / "bin" / "script-recompiler"


def which(name: str) -> str | None:
    return shutil.which(name)


def seed_run_dir(run_dir: Path, meta_dir: Path) -> None:
    """Recreate the layout the reference expects: `./cache/<version>/*.csv` relative to
    the process working directory, and no stale output from a previous fixture."""
    if run_dir.exists():
        shutil.rmtree(run_dir)
    cache = run_dir / "cache" / META_VERSION
    cache.mkdir(parents=True)
    for name in META_FILES:
        src = meta_dir / name
        if not src.is_file():
            raise FileNotFoundError(f"meta file missing: {src}")
        shutil.copy2(src, cache / name)


def reference_run(launcher: Path, run_dir: Path, source: Path, out_file: Path,
                  extra_args: list[str], fixtures_dir: Path, timeout: int = 120):
    """Run one `compile` invocation. Returns (exit_code, emitted_text_or_None, diagnostics)."""
    args = ["c", "-v", META_VERSION, "-i", str(source), "-o", str(out_file)]
    args += [a.replace("$FIXTURES", str(fixtures_dir)) for a in extra_args]
    proc = subprocess.run([str(launcher), *args], cwd=run_dir, capture_output=True,
                          text=True, timeout=timeout)
    text = out_file.read_text(encoding="utf-8") if out_file.is_file() else None
    raw = (proc.stdout or "") + (proc.stderr or "")
    lines = []
    for line in ANSI.sub("", raw).splitlines():
        stripped = LOG_PREFIX.sub("", line).strip()
        if not stripped:
            continue
        # Keep reported diagnostics and lexer complaints; drop stack-trace continuation
        # lines and the routine INFO chatter, which is noise that would age badly.
        if DIAGNOSTIC.search(stripped) and not stripped.startswith(("at ", "\t", "Caused by")):
            message = stripped.split("ERROR", 1)[-1].lstrip(": ").strip()
            if message and message != BANNER:
                lines.append(message)
        elif "Exception" in stripped or "recognition error" in stripped:
            lines.append(stripped)
    return proc.returncode, text, sorted(set(lines))


def missing_prereqs(scratch: Path, meta_dir: Path) -> list[str]:
    problems = []
    if not which("java"):
        problems.append("no `java` on PATH")
    if not which("gradle") and not (scratch / "src" / "gradlew").exists():
        problems.append("no `gradle` and no scratch copy to build in")
    if not tool_path(scratch).exists():
        problems.append(f"oracle not built (run `fate_oracle.py build`): {tool_path(scratch)}")
    if not meta_dir.is_dir():
        problems.append(f"no local meta bundle at {meta_dir}")
    return problems


def cmd_status(args) -> int:
    testdata, scratch, meta = Path(args.testdata), Path(args.scratch), Path(args.meta)
    print(f"testdata : {testdata}")
    print(f"scratch  : {scratch}")
    print(f"meta     : {meta}")
    print(f"java     : {which('java') or 'ABSENT'}")
    print(f"gradle   : {which('gradle') or 'ABSENT'}")
    print(f"oracle   : {'built' if tool_path(scratch).exists() else 'NOT BUILT'}")
    manifest = read_manifest(testdata)
    goldens = sum(1 for f in manifest["fixtures"] if (testdata / "golden" / f"{f['id']}.expected").exists())
    print(f"goldens  : {goldens}/{len(manifest['fixtures'])} present")
    problems = missing_prereqs(scratch, meta)
    if problems:
        print("\nSKIPPED — oracle cannot run:")
        for problem in problems:
            print(f"  - {problem}")
        return SKIP
    print("\noracle ready")
    return 0


def cmd_build(args) -> int:
    ref, scratch = Path(args.ref), Path(args.scratch)
    if not ref.is_dir():
        print(f"SKIPPED — reference repository not found at {ref}", file=sys.stderr)
        return SKIP
    src = scratch / "src"
    if src.exists():
        shutil.rmtree(src)
    src.mkdir(parents=True)
    # Copy, never build in place: the reference tree is read-only by project rule.
    shutil.copytree(ref, src, dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git", "build"))
    gradle = which("gradle")
    launcher_build = [gradle] if gradle else [str(src / "gradlew")]
    print(f"building oracle in {src} (this is the slow step, and it needs network on a cold cache)")
    proc = subprocess.run([*launcher_build, "--no-daemon", "--console=plain", "installDist"],
                          cwd=src, capture_output=True, text=True)
    tail = (proc.stdout + proc.stderr).strip().splitlines()[-8:]
    print("\n".join(tail))
    if proc.returncode != 0:
        print("SKIPPED — oracle build failed (see output above)", file=sys.stderr)
        return SKIP
    if not tool_path(scratch).exists():
        print(f"SKIPPED — build succeeded but no launcher at {tool_path(scratch)}", file=sys.stderr)
        return SKIP
    print(f"oracle built: {tool_path(scratch)}")
    return 0


def run_all(args, write: bool) -> int:
    testdata, scratch, meta = Path(args.testdata), Path(args.scratch), Path(args.meta)
    problems = missing_prereqs(scratch, meta)
    if problems:
        print("SKIPPED — oracle prerequisites missing:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        print("  Regenerate deliberately: `python3 tools/fate_oracle.py build && python3 tools/fate_oracle.py gen`",
              file=sys.stderr)
        return SKIP
    manifest = read_manifest(testdata)
    fixtures_dir = testdata / "fixtures"
    golden_dir = testdata / "golden"
    if write:
        golden_dir.mkdir(parents=True, exist_ok=True)
    if args.only:
        manifest["fixtures"] = [f for f in manifest["fixtures"] if f["id"] in args.only]
    failures = []
    with tempfile.TemporaryDirectory(prefix="fate-oracle-") as tmp:
        run_dir = Path(tmp)
        for fixture in manifest["fixtures"]:
            source = fixtures_dir / f"{fixture['id']}.fate"
            if not source.is_file():
                failures.append(f"{fixture['id']}: fixture missing ({source})")
                continue
            seed_run_dir(run_dir, meta)
            out_file = run_dir / "out" / f"{fixture['id']}.txt"
            out_file.parent.mkdir(parents=True, exist_ok=True)
            code, text, diagnostics = reference_run(tool_path(scratch), run_dir, source, out_file,
                                                    fixture.get("args", []), fixtures_dir)
            expected_text = "" if text is None else text
            diag_blob = f"exit={code}\nproduced_output={'yes' if text is not None else 'no'}\n" + "\n".join(diagnostics) + "\n"
            label = fixture["id"]
            if write:
                (golden_dir / f"{label}.expected").write_text(expected_text, encoding="utf-8")
                (golden_dir / f"{label}.diag").write_text(diag_blob, encoding="utf-8")
                print(f"  wrote {label}: {len(expected_text)} bytes, {len(diagnostics)} diagnostic(s)")
                continue
            for suffix, produced in ((".expected", expected_text), (".diag", diag_blob)):
                want = golden_dir / f"{label}{suffix}"
                if not want.is_file():
                    failures.append(f"{label}{suffix}: no golden committed")
                    continue
                if want.read_text(encoding="utf-8") != produced:
                    failures.append(f"{label}{suffix}: output drifted from the committed golden")
                    continue
            print(f"  ok {label}")
    if failures:
        print("\nFAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mode", choices=("status", "build", "gen", "check"))
    parser.add_argument("--testdata", default=str(DEFAULT_TESTDATA))
    parser.add_argument("--scratch", default=os.environ.get("FATE_ORACLE_SCRATCH", str(DEFAULT_SCRATCH)))
    parser.add_argument("--ref", default=os.environ.get("FATE_ORACLE_REF", str(DEFAULT_REF)))
    parser.add_argument("--meta", default=os.environ.get("FATE_ORACLE_META", str(DEFAULT_META)))
    parser.add_argument("--only", nargs="*", help="restrict to these fixture ids")
    args = parser.parse_args(argv)
    if args.mode == "status":
        return cmd_status(args)
    if args.mode == "build":
        return cmd_build(args)
    return run_all(args, write=args.mode == "gen")


if __name__ == "__main__":
    sys.exit(main())
