#!/usr/bin/env python3
"""Naming-policy lint (AGENTS.md "Naming Conventions", docs/guidelines/cpp.md §2).

Two check families:

  * Acronym checks (code + docs). Every identifier is split into camel/Pascal
    humps and each hump is compared against the acronym dictionary. A leading
    lowercase acronym is legal only for variables (`timHeader`, `hasXaDir`);
    every other position must use the canonical spelling. Truncations (Ptr, Buf,
    Msg, Ctx, Tmp, Env, Args, Rel, Sub, Max, Min) are *not* acronyms and stay
    mixed-case. Docs check only backticked identifiers, because docs must cite
    reference-repository names verbatim (`Tim.java`, `Bpp.java`, `RenderApi`,
    `IsoReader`). Anything in the Java/third-party allowlist passes; a name that
    is *ours* must follow the policy — a doc that prescribes
    `Error::BpeSizeMismatch` will be implemented wrong or drift from the code.

  * Member-casing checks (`scan_members`). Data members of project-owned C++
    records. A PUBLIC data member is PascalCase (`RelPath`, `WindowWidth`,
    `HasXADir`); a PRIVATE/protected data member is camelCase with a trailing
    underscore (`sinkMutex_`, `pads_`, `rawXABlock_`). camelCase *without* the
    underscore is reserved for scoped variables and parameters, so a member is
    never "just a variable": `windowWidth` in a struct and `Pads` in a class are
    both violations. The underscore is load-bearing — it is what tells a reader
    at a use site that the name is member state, not a local.

Exit status: 0 = clean, 1 = violations (file:line, offending identifier, the
canonical form and the suggested rewrite).

Usage:  python3 tools/check_naming.py [--code-only] [--docs-only] [--no-members]
                                        [path ...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Canonical acronym spellings, keyed by their lowercase form. A hump whose
# lowercase form is a key here must be spelled exactly as the value. Truncations
# (Ptr, Buf, Msg, Ctx, Tmp, Env, Args, Rel, Sub) and directory names (Sect, Ovl)
# are deliberately absent: they are not acronyms.
ACRONYMS = {
    "abi": "ABI", "abgr": "ABGR", "anm": "ANM", "api": "API", "bpp": "BPP",
    "bpe": "BPE", "clut": "CLUT", "cpu": "CPU", "crc": "CRC", "cuda": "CUDA",
    "daba": "DABA", "deff": "DEFF", "dll": "DLL", "dpi": "DPI", "fps": "FPS",
    "fp": "FP", "glsl": "GLSL", "gte": "GTE", "hlsl": "HLSL", "hsv": "HSV",
    "id": "ID", "io": "IO", "iso": "ISO", "json": "JSON", "lba": "LBA",
    "midi": "MIDI", "mrg": "MRG", "ptx": "PTX", "rgb": "RGB", "rtti": "RTTI",
    "sdl": "SDL", "sdk": "SDK", "sfx": "SFX", "spu": "SPU", "spirv": "SPIRV",
    "sshd": "SSHD", "sssq": "SSSQ", "svg": "SVG", "tmd": "TMD", "tim": "TIM",
    "toml": "TOML", "url": "URL", "utf": "UTF", "uuid": "UUID", "uv": "UV",
    "vram": "VRAM", "wav": "WAV", "vram2": None, "xa": "XA", "xml": "XML",
    "yaml": "YAML", "yuv": "YUV",
}
ACRONYMS = {key: value for key, value in ACRONYMS.items() if value}

# Reference-repository (Java) and third-party identifiers cited verbatim in docs,
# compared case-insensitively against the whole token. Docs must quote the
# reference names exactly, so `Tim`, `Bpp`, `RenderApi`, `IsoReader` are correct
# *there* and are not our API.
ALLOWED_DOC_TOKENS = {
    "anm", "api", "bpp", "bpe", "clut", "daba", "deff", "gte", "iso", "mrg",
    "sdl", "spu", "tim", "tmd", "vram", "xa", "addclutanimation", "advancesTime",
    "audioTimerReloadValues_200840c", "battleTmd", "battletmd", "ccontainerclutanimations",
    "clutanimation", "copyvramtovram", "deffarchive", "entryid", "fromtmd", "glapi",
    "glesapi", "hasclut", "inclut", "inuv", "isobin", "isoreader", "loaddeffevent",
    "mrgarchive", "modid", "origclutx", "origcluty", "pathtomodid", "queuedmodelbattleTmd",
    "queuedmodelbattletmd", "queuedmodeltmd", "registerdeffsevent", "registryid",
    "renderapi", "replclutx", "replcluty", "timarchive", "tmdobjloader", "tmdwithid",
    "tim", "timers", "tmr", "deffs", "loaddeffs", "timewithid",
}

# Third-party / standard-library identifiers allowed in our own code.
ALLOWED_CODE_IDENTS = {"sdl", "npos", "uint", "lod", "psx", "midi"}

# --- C identifier grammar ---------------------------------------------------
#
# C code in this repository (the psx C core plus its ABI headers, and all of
# fate/) is prescribed snake_case with `_t` type names and UPPER_SNAKE
# macros/enumerators (psx-c-api.md §2, AGENTS.md naming table, and the 2026-10-27
# owner decision that fate follows the psx C style with no prefix). Two things had
# to be true before that could be checked rather than merely documented: `.c` had
# to be scanned at all (it was not), and the check had to exclude the C++ wrapper
# layer, where PascalCase is the correct grammar.
C_GRAMMAR_ALLOWED = {
    "_static_assert", "_thread_local", "_atomic", "_noreturn", "uint", "npos",
}

# Comments and literals, matched across newlines. The literal alternatives accept
# backslash escapes, so a help string containing a \n escape stays one token
# instead of becoming a place where code happens to live.
C_NON_CODE = re.compile(
    r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'""", re.S)

# Test code is not the C API surface, so the grammar rule does not speak to it.
# Excluding it is a scope choice, not an oversight; production C is checked fully.
TEST_SEGMENT = "tests"

# Doc lines citing a Java source path or symbol are exempt wholesale: the names
# there are quotations, not our API.
DOC_LINE_EXEMPT = re.compile(r"\.java|legend[./]|\.class|GlApi|GlesApi|RenderApi")

IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
# Documents that prescribe our API: every backticked identifier is checked there.
DOC_STRICT_ROOTS = re.compile(r"^(docs/(architecture|guidelines)/|README\.md$)")
HUMP = re.compile(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z][a-z0-9]*|[0-9]+")
COMMENT_LINE = re.compile(r"^\s*(//|/\*|\*)")

# `#define NAME` / `#  define NAME(x)` — the macro name only, not its body.
DEFINE_NAME = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)")

# A file-scope read-only table: no leading whitespace, so function-local `const`
# declarations are structurally out of scope here.
TABLE_DECL = re.compile(
    r"^(?:static\s+)?const\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\*)?\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[")
# `SomeName`, `ns::Name`, `Name(x)` — the identifier part before punctuation.
DOC_TOKEN = re.compile(r"`([^`]*)`")
DOC_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def humps(identifier: str) -> list[str]:
  return HUMP.findall(identifier)


def is_snake_or_macro(identifier: str) -> bool:
  """snake_case ids (files, test names, string-table ids) and ALL_CAPS macros are
  outside the acronym rule, which governs PascalCase positions."""
  return "_" in identifier and (identifier == identifier.lower() or identifier == identifier.upper())


def check_identifier(identifier: str, leading_lowercase_ok: bool) -> tuple[str, str] | None:
  """Return (offending hump, canonical spelling) if the identifier violates policy."""
  if is_snake_or_macro(identifier):
    return None
  parts = humps(identifier)
  for index, part in enumerate(parts):
    canonical = ACRONYMS.get(part.lower())
    if canonical is None:
      continue
    if part == canonical:
      continue
    # A lowercase acronym may lead a variable name (`timHeader`, `hasXaDir`).
    if index == 0 and part == canonical.lower() and identifier[0].islower() and leading_lowercase_ok:
      continue
    if part.lower() == canonical.lower():
      return part, canonical
  return None


def scan_code(path: Path) -> list[str]:
  findings = []
  # Strip // comments and string literals so prose and payloads are not scanned.
  text = path.read_text(encoding="utf-8", errors="replace")
  for number, line in enumerate(text.splitlines(), 1):
    if COMMENT_LINE.match(line):
      continue
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    code = re.sub(r"'(?:\\.|[^'\\])'", "''", code)
    for match in IDENT.finditer(code):
      identifier = match.group(0)
      if identifier.lower() in ALLOWED_CODE_IDENTS:
        continue
      found = check_identifier(identifier, leading_lowercase_ok=True)
      if found:
        hump, canonical = found
        findings.append(
            f"{path}:{number}: {identifier}: acronym '{hump}' must be '{canonical}' "
            f"(enum values and constants are PascalCase positions too)"
        )
  return findings


def scan_c_style(path: Path) -> list[str]:
  """Flag identifiers in C code that are neither lower_snake nor UPPER_SNAKE.

  Comments and string literals are blanked out first, with their newlines
  preserved so a finding points at the line it is really on. Without the
  escape-aware literal pattern in C_NON_CODE, help text such as "translate Fate
  Script source..." reads as two identifiers named Fate and Script; that was the
  first draft's bug, and the pattern - not an allowlist - is where it is fixed.
  """
  findings = []
  text = path.read_text(encoding="utf-8", errors="replace")

  def blank_out(match):
    return "".join(ch if ch == "\n" else " " for ch in match.group(0))

  code = C_NON_CODE.sub(blank_out, text)
  for number, line in enumerate(code.splitlines(), 1):
    for match in IDENT.finditer(line):
      identifier = match.group(0)
      if identifier == identifier.lower() or identifier == identifier.upper():
        continue  # lower_snake or UPPER_SNAKE: already the C grammar
      if identifier.lower() in C_GRAMMAR_ALLOWED:
        continue
      findings.append(
          f"{path}:{number}: {identifier}: C code uses snake_case identifiers "
          f"(types take a `_t` suffix); PascalCase and camelCase belong to the "
          f"C++ grammar (psx-c-api.md §2)"
      )
  return findings


def scan_macro_names(path: Path) -> list[str]:
  """Macro / named-constant spellings must be UPPER_SNAKE_CASE.

  This is the check that would have caught `op_name_count`, which scan_c_style
  cannot see: that check deliberately passes anything all-lowercase, because
  snake_case is the correct C grammar for ordinary identifiers. A macro name is not
  an ordinary identifier - its spelling is fixed by policy (AGENTS.md naming table
  and data convention, psx-c-api.md §2), so `#define op_name_count ...` is
  decidable from the text alone, without first deciding whether the thing is
  'really' a constant.

  Deliberately NOT attempted: the same rule over `const`-qualified objects. Most
  `const int x = ...` in this codebase are read-only locals whose value is computed
  at runtime (`const int len = text.len < 0 ? 0 : ...`), not named constants, and
  spelling cannot distinguish the two. A check that guessed would either flag
  correct code or stay quiet, so that distinction stays a review matter.
  """
  findings = []
  for number, line in enumerate(path.read_text(encoding="utf-8",
                                                errors="replace").splitlines(), 1):
    match = DEFINE_NAME.match(line)
    if match is None:
      continue
    name = match.group(1)
    if name != name.upper():
      findings.append(
          f"{path}:{number}: {name}: a macro or named constant is UPPER_SNAKE_CASE; "
          f"a lower-case spelling reads as a variable (AGENTS.md naming table, data "
          f"convention)"
      )
  return findings


def scan_table_constants(path: Path) -> list[str]:
  """File-scope `static const` tables must be UPPER_SNAKE_CASE.

  This is the half of the constants rule that IS decidable, and it is what catches
  `op_names` / `keywords` / `operators`. The discriminator is indentation, not
  intuition: a declaration at column 0 is file scope, and a file-scope read-only
  array is a named table, not a temporary. Function-local `const` sits at least two
  spaces in and is deliberately not touched — most of those are runtime values.

  The three tables this was written for were renamed on 2026-10-29; without this
  check the same drift had nowhere to be caught, which is how they stayed lower-case.
  """
  findings = []
  for number, line in enumerate(path.read_text(encoding="utf-8",
                                                errors="replace").splitlines(), 1):
    match = TABLE_DECL.match(line)
    if match is None:
      continue
    name = match.group(1)
    if name != name.upper():
      findings.append(
          f"{path}:{number}: {name}: a file-scope read-only table is a named "
          f"constant and is UPPER_SNAKE_CASE"
      )
  return findings


# --- member-casing check ----------------------------------------------------
#
# Member casing is a *scope* rule, so a flat regex over lines is not enough: this
# walks each file with a scope stack that remembers (a) whether the innermost
# brace block is a record body and (b) which access label is in effect there.
# `private:` inside a struct and the implicit-private default of a class both
# matter, and a member counts only at record scope — a local inside a member
# function is still a variable and stays camelCase.

RECORD_HEAD = re.compile(r"^(?:template\s*<.*>\s*)?(?P<kind>struct|class|union)\s+"
                         r"(?P<name>[A-Za-z_]\w*)"
                         # A record head is followed by `{`, by a base list `:`, or by
                         # nothing. Without this the elaborated-type member
                         # `class Inner* innerPtr = nullptr;` reads as a nested record
                         # and the member is never seen.
                         r"(?:\s*[{:]|\s*$)")
ACCESS_LABEL = re.compile(r"^(?P<acc>public|private|protected)\s*:\s*$")

# A data member declaration: optional attributes, a type, a name, then `;`, `=`,
# `{` or `[`. A `(` anywhere before that punctuation means a function or a call,
# not a member — that guard is what keeps accessors (`size()`, `Bytes() const`)
# out of the report, and it is the reason function bodies are skipped too.
#
# Known limits, stated so this is not mistaken for a parser: it is line-based. A
# record whose whole body is on one line (`struct Rect { int width; };`) reads as a
# record head, so its fields are unseen — no project code is written that way, and
# new code should not be. A line declaring several members at once
# (`float x, y, z;`) checks only the first name. The rule is verified with
# planted-violation probes (multi-line records, base lists, template heads,
# re-opened `public:`, multi-dimension arrays, elaborated-type members, `constexpr`
# members, function pointers, wrapped parameter lists), not by inspection.
MEMBER_DECL = re.compile(
    r"^(?:(?:mutable|static|constexpr|consteval|constinit|thread_local)\s+)*"
    # `class Inner* innerPtr` / `struct Foo bar` — the elaborated keyword is part
    # of the type, and skipping it used to hide the member.
    r"(?:class\s+|struct\s+|enum\s+)?"
    r"(?P<type>(?:const\s+|volatile\s+)*(?:unsigned\s+|signed\s+)?"
    r"[A-Za-z_][\w]*(?:::[\w:]+)*(?:<[^;{}]*>)?(?:\s*[&*]+\s*|\s+))"
    r"(?:const\s+|volatile\s+)?(?P<name>[A-Za-z_]\w*)"
    # One *or more* dimensions: `float values[2][2]` is one member too.
    r"(?:\s*\[[^\]]*\])*\s*(?:=|;|\{).*$"
)
# Function-pointer member: `void (*fn)(bool&);` hides the name inside parens.
# (removed: function-pointer members follow the function convention — see the
# note in scan_members where member declarations are matched)

# A `constexpr`/`consteval`/`constinit` (optionally `static`) member is a named
# constant, not member state: AGENTS.md and cpp.md §7 put constants in the
# PascalCase position ("function-scope named constants are constants, not
# variables"), so the `camelCase_` rule must not be applied to them — demanding
# `maxThing_` would contradict the constants rule.
CONSTANT_QUALIFIER = re.compile(r"^(?:static\s+)?(?:constexpr|consteval|constinit)\b")

# Lines that look like a member declaration but are not.
MEMBER_SKIP = re.compile(
    r"^(using|typedef|friend|static_assert|return|else\b|case\b|[A-Z_]{2,}\(|"
    r"SC_|PSX_|LOD_|SDL_|\}|#)|\boperator\b")


def scan_members(path: Path) -> list[str]:
  """Check data-member casing inside project-owned C++ records.

  Public data members are PascalCase; private/protected ones are camelCase with a
  trailing underscore. The underscore is required even when the name would
  otherwise read as a legal variable, because it is what distinguishes member
  state from a scoped variable at the use site.
  """
  findings: list[str] = []
  lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
  stack: list[list[str]] = []          # entries: [kind, access, owner]
  pending: tuple[str, str] | None = None  # record head awaiting its `{`
  depth = 0                            # parenthesis depth carried across lines

  for number, raw in enumerate(lines, 1):
    if COMMENT_LINE.match(raw):
      continue
    line = raw.split("//")[0].strip()
    if not line:
      continue

    # Parenthesis depth across lines. A parameter list may wrap, so a continuation
    # line carries only the *closing* paren (`ResourceKind kind = …);`) and would
    # otherwise read like a data member; anything seen while a parenthesis is
    # open, or opened on the line, is an argument rather than member state.
    open_parens = line.count("(")
    close_parens = line.count(")")
    in_parens = depth > 0 or open_parens > 0 or close_parens > 0
    depth = max(0, depth + open_parens - close_parens)

    label = ACCESS_LABEL.match(line)
    if label:
      if stack:
        stack[-1][1] = label.group("acc")
      continue

    head = RECORD_HEAD.match(line)
    if head and ";" not in line.split("{")[0]:
      kind = head.group("kind")
      pending = (kind, head.group("name"))
    elif line.endswith(";"):
      pending = None  # a statement (forward declaration, `struct Foo;`)

    record = stack[-1] if stack and stack[-1][0] in ("struct", "class", "union") else None
    if record and not head:
      hit = None
      # A parenthesis on the line — or a parenthesis still open from an earlier
      # line — means a function, a call or an argument (declaration, definition,
      # deleted `operator=`, wrapped parameter list), never a data member. That
      # guard is what keeps accessors (`size()`, `Bytes() const`) out of the
      # report, and it is also why function bodies are skipped.
      if (not MEMBER_SKIP.search(line) and not in_parens
              and not CONSTANT_QUALIFIER.match(line)):
        hit = MEMBER_DECL.match(line)
      # Function-pointer members are deliberately not data members for naming
      # purposes: they are invoked (`tc.fn(ok)`), so they follow the function
      # convention (camelCase, no trailing `_`) rather than the data-member one.
      # The parenthesis guard above already keeps such declarations
      # out of MEMBER_DECL; nothing matches them afterwards.
      if hit:
        name = hit.group("name")
        _kind, access, owner = record
        private = access in ("private", "protected")
        if private:
          ok = name.endswith("_") and name[:-1][:1].islower()
          why = "camelCase with a trailing '_' (member state, not a variable)"
        else:
          ok = name[:1].isupper() and not name.endswith("_")
          why = "PascalCase (a public data member is not a variable)"
        if not ok:
          if private:
            body = name[:-1] if name.endswith("_") else name
            fix = body[:1].lower() + body[1:] + "_"
          else:
            base = name.rstrip("_")
            fix = base[:1].upper() + base[1:]
          findings.append(f"{path}:{number}: {owner}.{name}: a {'private' if private else 'public'} "
                          f"data member wants {why} — write `{fix}` (AGENTS.md naming table)")
        else:
          acronym = check_identifier(name, leading_lowercase_ok=private)
          if acronym:
            hump, canonical = acronym
            slot = "private" if private else "public"
            findings.append(f"{path}:{number}: {owner}.{name}: acronym '{hump}' must be "
                            f"'{canonical}' in a {slot} data member")
            continue

    # Brace bookkeeping. The first `{` opens a record body when a record head is
    # pending and the brace is not part of a call or a braced-init tail.
    opened = None
    if pending and "{" in line and "(" not in line.split("{")[0]:
      opened = pending
      pending = None

    for ch in line:
      if ch == "{":
        if opened:
          kind, name = opened
          opened = None
          stack.append([kind, "private" if kind == "class" else "public", name])
        elif re.match(r"^(if|for|while|switch|else|do)\b", line):
          stack.append(["ctrl", "-", "-"])
        else:
          stack.append(["block", "-", "-"])
      elif ch == "}" and stack:
        stack.pop()

  return findings


def scan_doc(path: Path) -> list[str]:
  """Docs mode has two levels.

  Qualified names (`Status::DecodeUnsupportedBpp`) are unambiguously ours and are
  checked in every document — research docs are full of Java citations, but Java
  is dotted, not `::`-qualified. In the architecture/guideline documents, which
  *prescribe* our API, every backticked identifier is checked; a deliberate
  quotation or counter-example is exempted with a `naming-allow` comment on the
  line.
  """
  findings = []
  strict = DOC_STRICT_ROOTS.match(path.as_posix()) is not None
  text = path.read_text(encoding="utf-8", errors="replace")
  for number, line in enumerate(text.splitlines(), 1):
    if "naming-allow" in line:
      continue
    for token in DOC_TOKEN.findall(line):
      for match in DOC_IDENT.finditer(token):
        identifier = match.group(0)
        qualified = token[:match.start()].endswith("::")
        if not qualified:
          if not strict or DOC_LINE_EXEMPT.search(line) or identifier.lower() in ALLOWED_DOC_TOKENS:
            continue
        found = check_identifier(identifier, leading_lowercase_ok=True)
        if found:
          hump, canonical = found
          findings.append(f"{path}:{number}: `{identifier}`: acronym '{hump}' must be '{canonical}' "
                          f"(docs prescribe our names — a mis-cased name here becomes mis-cased code)")
  return findings


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--code-only", action="store_true")
  parser.add_argument("--docs-only", action="store_true")
  parser.add_argument("--no-members", action="store_true",
                      help="skip the data-member casing check")
  parser.add_argument("paths", nargs="*", default=["."])
  opts = parser.parse_args(argv)

  roots = [Path(p) for p in opts.paths]
  files: list[Path] = []
  for root in roots:
    if root.is_file():
      files.append(root)
      continue
    files.extend(sorted(root.rglob("*")))

  findings: list[str] = []
  for path in files:
    if "build" in path.parts or ".git" in path.parts or "third_party" in path.parts:
      continue
    # C roots carry two grammars: the C ABI surface (snake_case) and, under
    # psx/cpp, the C++ wrappers on top of it (PascalCase). Only the first is
    # checked by scan_c_style, and test code is out of scope by choice.
    c_root = "psx" in path.parts or "fate" in path.parts
    c_code = c_root and "tests" not in path.parts and "cpp" not in path.parts
    if path.suffix in {".c", ".h"} and c_code and not opts.docs_only:
      findings.extend(scan_c_style(path))
      findings.extend(scan_macro_names(path))
      findings.extend(scan_table_constants(path))
    elif path.suffix in {".h", ".hpp", ".cpp", ".cc"} and not opts.docs_only:
      findings.extend(scan_code(path))
      # The psx library keeps its own conventions: snake_case in the C17 core (a
      # C ABI surface) and camelCase in the C++ wrappers, whose structs mirror
      # those C shapes one-for-one. Member casing there is deliberate, not drift
      # (psx-c-api.md §2, psx-library.md §5), so the member check starts where the
      # engine's own types begin.
      # `fate/` is C23, not C++: it follows the same snake_case identifier style as the
      # psx C core (owner decision 2026-10-27 — "psx-style snake_case without a `fate_`
      # prefix"), so struct fields there are snake_case by design and the C++
      # data-member rule does not describe them.
      if not opts.no_members and not c_root:
        findings.extend(scan_members(path))
    elif path.suffix == ".md" and not opts.code_only:
      findings.extend(scan_doc(path))

  for finding in findings:
    print(finding)
  if findings:
    print(f"\n{len(findings)} naming violation(s): acronyms are fully capitalised in "
          f"every PascalCase position, public data members are PascalCase and private "
          f"ones are camelCase_ (AGENTS.md naming table); C code is snake_case with "
          f"`_t` types and UPPER_SNAKE macros (psx-c-api.md §2).")
    return 1
  print("naming: clean (acronym policy + data-member casing + C identifier grammar)")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
