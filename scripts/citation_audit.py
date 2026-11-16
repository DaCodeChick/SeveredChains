#!/usr/bin/env python3
"""Citation audit for the research corpus (planning/05 §6, fifth open item).

Research and planning documents cite reference source as `File.java:<line>` or
`File.java:<from>-<to>`. A line number is a *position in a file at a revision*,
not a semantic anchor. When a reference repository moves, positions move with
it, and a citation that was exact when written can end up pointing at a blank
line or at an unrelated method. This tool makes that detectable.

Structural checks (against the target revision):
    PAST-EOF             the cited range runs past the end of the file
    BLANK-LINES          the cited lines are blank or whitespace
    IMPLAUSIBLE-RANGE    end < start, or a span wider than any real method
    NO-SUCH-FILE         the cited filename is in no reference repository
    AMBIGUOUS-REPO       the filename exists in more than one repository and
                         the surrounding prose does not say which

Relational checks (require --from, the revision the corpus was written against):
    MOVED                the cited block is still present, verbatim and
                         uniquely, elsewhere in the file: a pointer problem,
                         which --apply rewrites.
    CHANGED              the cited lines no longer match, and no unique copy
                         exists elsewhere: a *content* problem, the prose may
                         now be wrong. --apply never touches these.

A clean structural run means the pointers line up. It is not proof that the
prose is correct, and it never proves a behaviour claim — those are the
transcription and oracle checks.

Reference-repository policy (TD-25)
----------------------------------
Nothing runs by default: the repositories are resolved only when a root is
supplied explicitly (--repos-root or SEVERED_CHAINS_REPOS), so `make check`
stays dependency-free and reproducible on a machine with no reference
checkout. `make citations` is the opt-in entry point.

Usage:
    python3 scripts/citation_audit.py                                  # skipped
    python3 scripts/citation_audit.py --repos-root DIR --revision HEAD
    python3 scripts/citation_audit.py --repos-root DIR --revision HEAD \
        --from 0c08c679b                     # classify MOVED / CHANGED
    python3 scripts/citation_audit.py ... --apply        # rewrite the pointers

Exit status is 0 unless --strict is given and citations are unresolved.
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys

REPOS = ("Severed-Chains", "Script-Recompiler", "Mod-Loader", "Dig-Dabas")
SRC_EXT = r"(?:java|g4|vsh|fsh|gsh|gradle|json|png|txt)"
CIT = re.compile(
    r"([A-Za-z0-9_./-]+?\." + SRC_EXT + r"):(\d{2,4})"
    r"(?:\s*([-\u2013])\s*(\d{2,4}))?"
)
DECLARED_REV = re.compile(
    r"(Severed-Chains|Script-Recompiler|Mod-Loader|Dig-Dabas)"
    r"\s*(?:@|at)\s*`?([0-9a-f]{7,40})`?"
)


def sh(args, cwd):
    return subprocess.run(args, cwd=str(cwd), capture_output=True, text=True).stdout


class Repos:
    """File index and file contents of each reference repository, per revision."""

    def __init__(self, root, revisions):
        self.root = pathlib.Path(root)
        self.paths = {r: self.root / r for r in REPOS if (self.root / r / ".git").exists()}
        self.index = {}
        self._lines = {}
        for repo, path in self.paths.items():
            for rev in revisions:
                listing = sh(["git", "ls-tree", "-r", "--name-only", rev], path)
                for rel in listing.split("\n"):
                    if rel:
                        self.index.setdefault(rel.rsplit("/", 1)[-1], set()).add((repo, rel))

    def candidates(self, cited):
        base = cited.rsplit("/", 1)[-1]
        return [c for c in self.index.get(base, ()) if c[1].endswith(cited)]

    def file_lines(self, repo, rel, rev):
        key = (repo, rev, rel)
        if key not in self._lines:
            text = sh(["git", "show", f"{rev}:{rel}"], self.paths[repo])
            self._lines[key] = text.split("\n") if text else None
        return self._lines[key]


def locate(doc_text, match, repos, target_rev, declared):
    cited = match.group(1)
    cands = repos.candidates(cited)
    if not cands:
        return None, "NO-SUCH-FILE"
    if len(cands) > 1:
        # A document that only ever read one repository cannot be citing
        # another repository's same-named file. Use that, then the prose.
        named = [c for c in cands if c[0] in declared]
        if len(named) == 1:
            return named[0], None
        window = doc_text[max(0, match.start() - 240): match.end() + 60]
        near = [c for c in cands if c[1] in window]
        if len(near) == 1:
            return near[0], None
        return None, "AMBIGUOUS-REPO"
    return next(iter(cands)), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repos-root", default=None)
    ap.add_argument("--revision", default="HEAD", help="target revision")
    ap.add_argument("--from", dest="from_rev", default=None,
                    help="revision the corpus was written against")
    ap.add_argument("--from-repo", default="Severed-Chains",
                    help="which repository --from names")
    ap.add_argument("--corpus", default="research")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--max-report", type=int, default=40)
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    repos_root = args.repos_root or os.environ.get("SEVERED_CHAINS_REPOS")
    if not repos_root:
        print("citation audit skipped: no reference repositories configured (TD-25).")
        print("  opt in:  SEVERED_CHAINS_REPOS=/path/to/parent make citations")
        return 0
    repos = Repos(repos_root, [args.revision] + ([args.from_rev] if args.from_rev else []))
    if not repos.paths:
        print(f"citation audit skipped: no reference git repos under {repos_root}")
        return 0

    documents = sorted((root / args.corpus).rglob("*.md"))
    # `audits/` are transcripts of past verification passes; `citation-exceptions.md` is a register
    # that quotes citations in order to describe them. Neither makes a claim about reference code,
    # so counting their quoted pointers would inflate the census with the tool's own vocabulary.
    documents = [d for d in documents
                 if "audits" not in d.parts and d.name != "citation-exceptions.md"]

    counts = {}
    total = 0
    needs_human = []
    movable = []

    for doc in documents:
        text = doc.read_text()
        declared = dict(DECLARED_REV.findall(text))
        for match in CIT.finditer(text):
            total += 1
            cited, start = match.group(1), int(match.group(2))
            end = int(match.group(4)) if match.group(4) else start
            if end < start or end - start > 160:
                counts["IMPLAUSIBLE-RANGE"] = counts.get("IMPLAUSIBLE-RANGE", 0) + 1
                needs_human.append((doc, match, "IMPLAUSIBLE-RANGE"))
                continue
            target, why = locate(text, match, repos, args.revision, declared)
            if target is None:
                counts[why] = counts.get(why, 0) + 1
                needs_human.append((doc, match, why))
                continue
            repo, rel = target
            head_lines = repos.file_lines(repo, rel, args.revision)
            if head_lines is None:
                counts["NO-SUCH-FILE"] = counts.get("NO-SUCH-FILE", 0) + 1
                needs_human.append((doc, match, "NO-SUCH-FILE"))
                continue
            if end > len(head_lines):
                counts["PAST-EOF"] = counts.get("PAST-EOF", 0) + 1
                needs_human.append((doc, match, "PAST-EOF"))
                continue
            block = "\n".join(head_lines[start - 1:end])
            if not block.strip():
                counts["BLANK-LINES"] = counts.get("BLANK-LINES", 0) + 1
                needs_human.append((doc, match, "BLANK-LINES"))
                continue
            if not args.from_rev:
                counts["RESOLVED"] = counts.get("RESOLVED", 0) + 1
                continue
            # A baseline revision only exists for the repository it names.
            # `--from` describes --from-repo (Severed-Chains by default); a
            # document may also declare a per-repository revision explicitly.
            base_rev = declared.get(repo)
            if base_rev is None:
                base_rev = (
                    args.from_rev
                    if repo == args.from_repo
                    else None
                )
            if base_rev is None or base_rev == args.revision:
                # Nothing to compare against: the pointer is structurally
                # sound but movement in this file cannot be detected.
                counts["UNCHECKED-NO-BASELINE"] = counts.get("UNCHECKED-NO-BASELINE", 0) + 1
                continue
            base_lines = repos.file_lines(repo, rel, base_rev)
            if base_lines is None:
                counts["BAD-BASELINE-REV"] = counts.get("BAD-BASELINE-REV", 0) + 1
                needs_human.append((doc, match, "BAD-BASELINE-REV"))
                continue
            if start > len(base_lines):
                counts["PAST-EOF"] = counts.get("PAST-EOF", 0) + 1
                needs_human.append((doc, match, "PAST-EOF"))
                continue
            base_block = "\n".join(base_lines[start - 1:end])
            if base_block == block:
                counts["RESOLVED"] = counts.get("RESOLVED", 0) + 1
                continue
            hay = "\n".join(head_lines)
            hits = hay.count(base_block)
            if hits == 1:
                new_start = hay[:hay.find(base_block)].count("\n") + 1
                counts["MOVED"] = counts.get("MOVED", 0) + 1
                movable.append((doc, match, (new_start, new_start + (end - start),
                                             match.group(3))))
            else:
                counts["CHANGED"] = counts.get("CHANGED", 0) + 1
                needs_human.append((doc, match, "CHANGED"))

    resolved = sh(["git", "rev-parse", "--short", args.revision],
                  repos.paths.get("Severed-Chains", root)).strip() or args.revision
    print(f"citation audit against {resolved}: {total} citations in "
          f"{len(documents)} documents")
    for status, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {status:18s} {n}")
    print(f"  mechanically fixable: {len(movable)}  (--apply rewrites the pointers)")
    print(f"  needs a human: {len(needs_human)}")

    if args.apply:
        per_doc = {}
        for doc, match, (new_start, new_end, dash) in movable:
            if new_start == match.group(2) and (not dash or new_end == match.group(4)):
                continue
            replacement = f"{match.group(1)}:{new_start}"
            if dash:
                replacement += f"{dash}{new_end}"
            per_doc.setdefault(doc, []).append((match.start(), match.end(), replacement))
        for doc, edits in per_doc.items():
            text = doc.read_text()
            for start, end, replacement in sorted(edits, reverse=True):
                text = text[:start] + replacement + text[end:]
            doc.write_text(text)
        print(f"applied {sum(len(v) for v in per_doc.values())} rewrites "
              f"across {len(per_doc)} documents")
        return 0

    shown = 0
    for doc, match, status in needs_human:
        if shown >= args.max_report:
            print(f"  ... {len(needs_human) - shown} more (raise --max-report)")
            break
        print(f"  [{status}] {doc.relative_to(root)}: {match.group(0).strip()}")
        shown += 1
    if args.strict and needs_human:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
