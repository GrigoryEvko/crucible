#!/usr/bin/env python3
"""check-no-unchecked-access — access_context::unchecked() only in reviewed reflection walks.

A splice of a nonstatic data member has no access check in GCC 16: given the
reflection of a private member m, `obj.[:m:] = v` writes it.  The reflection
of a private member comes only from a walk under
std::meta::access_context::unchecked(), so that call is the one door to the
private state of every proof type: a count, a stamp, a grade, a Refined value
or a Secret payload.  scripts/unchecked-access-allowlist.txt names each file
that may open the door, with the reason, and the guard refuses the door
anywhere else.

WHAT COUNTS AS THE DOOR
    The guard reads the tokens of each file with its comments and literals
    blanked by scripts/cxx_lex.py, so white space, a line break or a comment
    between two tokens changes nothing, and a mention in a comment or a string
    is not a use.
      1. The name unchecked after ::, . or ->.  Every qualifier counts, so a
         namespace alias, a using-directive, a spliced class and a call
         through an object reach the same member.
      2. A using-declaration or an alias of access_context: a `using` or
         `typedef` statement that names access_context.
      3. A reflection of access_context, of std::meta or of std.  A walk of
         the members of one of them reaches unchecked by reflection, with no
         name to read.
      4. A splice that names a member of an object: `.[:`, `->[:` and the
         member pointer `&[:`.  This is the write itself, so it closes every
         route to the reflection, such as a walk of
         ^^decltype(access_context::current()) that finds unchecked by a
         string compare.

REVIEW RULE FOR THE ALLOWED FILES
    An allowed file may not return or publish a reflection of a nonstatic
    data member to its caller.  A public constexpr verdict whose field names
    a private member reopens the door, because `obj.[:verdict.field:]` needs
    no unchecked().  The guard does not check this rule.  The reviewer of each
    row does.

WHAT IT DOES NOT SEE, STATED RATHER THAN IMPLIED
    - A macro that builds the name from pieces with ##.
    - A walk that reaches the access_context class through a reflection it
      does not name, such as parent_of of a std::meta type.

A stale row, one whose file does not exist or does not open the door, fails
the guard, so the allowlist only shrinks.

Exit 0 clean, 1 on a door outside the allowlist or a stale row, 2 on a usage
error, a bad allowlist or a failed self-test.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = "scripts/unchecked-access-allowlist.txt"
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools", "bench", "fuzz", "examples")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp", ".tpp"})
PUNCTUATION = re.compile(r"\^\^|::|->|\S")
STATEMENT_END = frozenset({";", "{", "}"})
# Every door names unchecked or access_context, reflects std, or splices.  A
# backslash line splice can split a word, so a text with one is always read.
DOOR_WORDS = re.compile(r"unchecked|access_context|\^\^|\[\s*:|\\\r?\n")


class Refused(Exception):
    """The allowlist is missing or malformed (exit 2)."""


def read_allowlist(root: Path) -> list[tuple[int, str]]:
    """Return (line, path) for each row of the allowlist.

    Raises:
        Refused: If the allowlist is missing or a row has no reason
    """
    listing = root / ALLOWLIST
    if not listing.is_file():
        raise Refused(f"{ALLOWLIST} is missing, so no file may open the door.")
    rows = []
    for number, line in enumerate(listing.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        path, separator, reason = line.partition(" — ")
        if not separator or not path.strip() or not reason.strip():
            raise Refused(f"{ALLOWLIST}:{number} is not PATH — REASON.")
        rows.append((number, path.strip()))
    return rows


def scope_files(root: Path) -> list[str]:
    """Return the C++ files under the scan roots that git does not ignore."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, check=False)
    if listed.returncode == 0:
        paths = {p for p in listed.stdout.decode().split("\0") if p}
    else:
        paths = {str(p.relative_to(root)) for r in SCAN_ROOTS for p in (root / r).rglob("*") if p.is_file()}
    return sorted(p for p in paths if Path(p).suffix in SUFFIXES and (root / p).is_file())


def tokens(text: str) -> list[tuple[str, int]]:
    """Return the tokens of a text with its comments and literals blanked, each with its line.

    Complexity: linear in the length of the text.
    """
    joined, joins = cxx_lex.splice(text)
    blanked, _ = cxx_lex.blank(joined, blank_literals=True)
    return [(m.group(), cxx_lex.line_of(blanked, joins, m.start()))
            for m in re.finditer(r"[A-Za-z_]\w*|" + PUNCTUATION.pattern, blanked)]


def doors(text: str) -> list[tuple[int, str]]:
    """Return (line, shape) for each way the text opens the unchecked access context.

    A text that holds none of the words a door needs is not tokenized, which
    keeps the scan of the whole tree fast.
    """
    if not DOOR_WORDS.search(text):
        return []
    found: list[tuple[int, str]] = []
    toks = tokens(text)
    for i, (token, line) in enumerate(toks):
        before = toks[i - 1][0] if i > 0 else ""
        if token == "unchecked" and before in ("::", ".", "->"):
            found.append((line, "a use of unchecked"))
        after = [t for t, _ in toks[i + 1:i + 3]]
        if token in (".", "->", "&") and after == ["[", ":"]:
            found.append((line, "a splice that names a member of an object"))
        if token == "^^":
            name = []
            k = i + 1
            while k < len(toks) and (re.fullmatch(r"[A-Za-z_]\w*", toks[k][0]) or toks[k][0] == "::"):
                name.append(toks[k][0])
                k += 1
            spelled = "".join(name).lstrip(":")
            if spelled in ("std", "std::meta") or spelled.endswith("access_context"):
                found.append((line, f"a reflection of {spelled}"))
    start = 0
    for i, (token, _) in enumerate(toks + [(";", 0)]):
        if token not in STATEMENT_END:
            continue
        statement = [t for t, _ in toks[start:i]]
        if ("using" in statement or "typedef" in statement) and "access_context" in statement \
                and "namespace" not in statement:
            found.append((toks[start][1], "a using-declaration or an alias of access_context"))
        start = i + 1
    return found


def check(root: Path) -> int:
    """Compare the doors in the tree with the allowlist, and report to stderr.

    Returns:
        0 clean, 1 on a finding, 2 on a bad allowlist
    """
    try:
        rows = read_allowlist(root)
    except Refused as exc:
        print(f"check-no-unchecked-access: {exc}", file=sys.stderr)
        return 2
    admitted = {path for _, path in rows}
    opened: set[str] = set()
    problems: list[str] = []
    for rel in scope_files(root):
        found = doors((root / rel).read_text(errors="replace"))
        if found:
            opened.add(rel)
        if found and rel not in admitted:
            problems += [f"REFUSED   {rel}:{line} — {shape}.  A reflection of a private member lets a splice "
                         f"write a proof's private state.  Walk with access_context::current(), or add a "
                         f"reviewed row to {ALLOWLIST}." for line, shape in found]
    for number, path in rows:
        if path not in opened:
            problems.append(f"STALE     {ALLOWLIST}:{number}: {path} — does not open the door.  Remove the row.")
    for line in problems:
        print(f"check-no-unchecked-access: {line}", file=sys.stderr)
    if not problems:
        print(f"check-no-unchecked-access: {len(opened)} file(s) open the unchecked access context, "
              f"each on its reviewed row.")
    return 1 if problems else 0


def self_test() -> int:
    """Plant a repository, prove each verdict, and prove the verdict does not depend on the working directory.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def write(root: Path, rel: str, text: str) -> None:
        """Write one planted file."""
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")

    def captured(root: Path, cwd: Path | None = None) -> tuple[int, str]:
        """Run the check on the planted repository and keep its report."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = check(root)
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def expect(root: Path, code: int, needle: str, name: str, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        got, report = captured(root)
        ok = got == code and needle in report
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(f"{name}: expected exit {code} and '{needle}', got exit {got}:\n{report}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
        write(root, "include/foundation/Walk.h",
              "auto m = std::meta::members_of(^^T, std::meta::access_context::unchecked());\n")
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n")
        write(root, "src/Clean.cpp",
              "// std::meta::access_context::unchecked() is refused outside the allowlist.\n"
              'const char* s = "access_context::unchecked()";\n'
              "auto c = std::meta::access_context::current();\nbool unchecked = true;\nint n = unchecked;\n")
        expect(root, 0, "1 file(s) open", "a listed walk, a comment, a string and a plain name pass")

        forgeries = {
            "src/Qualified.cpp": "auto c = std::meta::access_context::unchecked();\n",
            "src/Alias.cpp": "namespace mm = std::meta;\nauto c = mm::access_context::unchecked();\n",
            "src/UsingDeclaration.cpp": "using std::meta::access_context;\nauto c = 1;\n",
            "src/TypeAlias.cpp": "using Door = std::meta::access_context;\n",
            "src/Typedef.cpp": "typedef std::meta::access_context Door;\n",
            "src/MultiLine.cpp": "auto c = std::meta::access_context\n    ::  /* door */\n    unchecked();\n",
            "src/Spliced.cpp": "auto c = [: ^^std::meta::access_context :]::unchecked();\n",
            "src/Member.cpp": "auto c = std::meta::access_context::current().unchecked();\n",
            "src/Address.cpp": "auto f = &std::meta::access_context::unchecked;\n",
            "src/ReflectClass.cpp": "constexpr auto r = ^^std::meta::access_context;\n",
            "src/ReflectNamespace.cpp": "constexpr auto r = ^^std::meta;\n",
            "src/Macro.cpp": "#define DOOR std::meta::access_context::unchecked()\n",
            "src/DecltypeWalk.cpp": "auto m = std::meta::members_of(^^decltype(std::meta::access_context::current()),"
                                    " std::meta::access_context::current());\nsealed.[:field:] = 42;\n",
            "src/TypeOfWalk.cpp": "constexpr auto ctx = std::meta::access_context::current();\n"
                                  "auto t = std::meta::type_of(^^ctx);\nsealed->[:field:] = 42;\n",
            "src/TemplateWalk.cpp": "template <class C> consteval auto f() { return std::meta::members_of(^^C, c); }\n"
                                    "auto g = f<decltype(std::meta::access_context::current())>();\nsealed.[:g:] = 1;\n",
            "src/ParameterWalk.cpp": "consteval auto f(auto c) { return std::meta::members_of(^^decltype(c), c); }\n"
                                     "void w(S& s) { s.[: f(1)[0] :] = 1; }\n",
            "src/MemberPointer.cpp": "constexpr auto m = &[:field:];\nvoid w(S& s) { s.*m = 42; }\n",
        }
        for rel, text in forgeries.items():
            write(root, rel, text)
            expect(root, 1, f"REFUSED   {rel}", f"a door outside the allowlist: {rel}", True)
            (root / rel).unlink()

        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n"
                               "src/Clean.cpp — opens nothing\n")
        expect(root, 1, "STALE     scripts/unchecked-access-allowlist.txt:3: src/Clean.cpp", "a stale row", True)
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\nsrc/Gone.cpp — gone\n")
        expect(root, 1, "STALE     scripts/unchecked-access-allowlist.txt:3: src/Gone.cpp", "a row for a missing file",
               True)
        write(root, ALLOWLIST, "include/foundation/Walk.h\n")
        expect(root, 2, "is not PATH — REASON", "a row with no reason", True)
        write(root, ALLOWLIST, "# planted\ninclude/foundation/Walk.h — a reviewed walk\n")
        same = captured(root, root) == captured(root, Path("/"))
        print(f"  {'ok  ' if same else 'FAIL'} the report from / equals the report from the repository")
        if not same:
            failures.append("the report depends on the working directory")
        (root / ALLOWLIST).unlink()
        expect(root, 2, "is missing", "a missing allowlist", True)
    for failure in failures:
        print(f"check-no-unchecked-access --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-no-unchecked-access --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, or run the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-no-unchecked-access.py [--self-test]", file=sys.stderr)
        return 2
    return self_test() if argv else check(REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
