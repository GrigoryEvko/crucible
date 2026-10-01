#!/usr/bin/env python3
"""Refuse task, stage and tracker references in the prose of the tree.

THE RULE
    A comment, a docstring, a diagnostic, a script or an allowlist states what
    the code does.  It does not name the task, the stage or the tracker entry
    that produced the code.  Such a name means nothing to a reader who has no
    access to the tracker, and it becomes false when the plan changes.

    Three families of text fail:
      1. A task reference: `#` and two to four digits (#147, task #193,
         #1519).  One digit is a list item (#1), and five or more digits is a
         colour (#374151), so neither matches.
      2. A tracker tag: the FIXY-U-, FIXY-V-, FIXY-FOUND-, FOUND-, GAPS-,
         SEPLOG-, CONTRACT-, METX-, WRAP-, BC- and PERF- families, the
         FOUND_I16 and GAPS_096 underscore forms, the short WRAP-5 form, the
         lowercase fixy-A5-016 and fix-18 forms, the short U-002 and V-073
         forms, the CR-05 audit form, the AUDIT-B and audit-D group forms and
         the F09-AUDIT form.
      3. A stage label: a dotted stage such as A13.2 or A10.x, "Stage D",
         "Stage B4", "at A9", "Phase F6" and "Agent 11".
    Four kinds of text look similar and pass by construction: a preprocessor
    directive (a letter follows `#`), a collision rule code (S004, I002: no
    hyphen), a suppression marker (ROW-CONTAINS-OK: no digit after the
    prefix) and the pull request of an upstream project (PR #1367: `PR ` in
    front of the `#`).  The guard has no allowlist.  A legitimate construct
    that it reports is a reason to narrow a pattern, not to exempt a line.

THE SCOPE
    The scan reads every file that git tracks, except this guard and the notes
    under misc/.  A file that git does not track is not read, because the guard
    reads what a commit holds.  For a symbolic link, a commit holds the path of
    the target, so the scan reads that path and not the content behind the
    link.  The target, when git tracks it, is read under its own name, and each
    reference in it is reported one time.

THE ENGINE
    The rule is about prose, so the scan reads prose only, from a parser where
    the language has one:
      * C++: the parse tree gives the comment, string and raw string nodes and
        the argument of each directive such as #error or #pragma.  The string
        literals inside a macro body come from the preprocessing tokens of the
        body.  Code is never read, so a member access such as `A1.x` is not a
        stage label.  A C++ file that the kit cannot parse (tsast.UNPARSEABLE,
        for example generated BPF C) is read line by line.
      * Python: the tokenize module gives the comments and the strings,
        docstrings and f-string text included.
      * Every other file (shell, CMake, YAML, allowlists, JSON, Markdown) has
        no parser here, and its text is read line by line.
    The store of utils/scripts/preprocessed.py keeps the references of each
    file under a hash of its bytes, so a warm run parses no file.

EXIT STATUS
    0  clean
    1  at least one reference
    2  bad invocation, a file that does not parse, or a self-test failure
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)
"""

from __future__ import annotations

import hashlib
import io
import re
import subprocess
import sys
import tempfile
import tokenize
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cache_dir  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402
from preprocessed import text_results  # noqa: E402

# The scan reads every tracked file, except the paths below.  This file plants
# references in its self-test.  The notes under misc/ are out of scope, and the
# scan does not read them.
SKIPPED_FILES = frozenset({"utils/scripts/check-no-coordination-refs.py"})
SKIPPED_DIRS = ("misc/",)

# One branch for each family.  The first lookbehind on the task branch refuses
# an HTML entity (&#123;), the shell argument count ($#), a printf flag (%#08x),
# a regex bracket ([#0-9]), an escape (\#) and token pasting (##).  The second
# refuses the pull request of an upstream project (PR #1367).
BRANCHES = (
    r"(?<![&\w$%{\[\\#])(?<!\bPR )#[0-9]{2,4}\b",
    r"\bFIXY-(?:U|V|FOUND)-[0-9]",
    r"\bFIXY-[0-9]",
    r"\bFOUND[-_](?:[A-Z][0-9]*|[0-9]+)\b",
    r"\bGAPS[-_][0-9]",
    r"\bSEPLOG-[A-Z]?[0-9]",
    r"\bCONTRACT-[0-9]",
    r"\bMETX-[A-Z0-9]",
    r"\bWRAP-(?:\*|[0-9]+\b|[A-Z][A-Za-z]*(?:-[A-Za-z]+)*-[0-9]+)",
    r"\b(?:AUDIT|audit)-[A-Z]\b",
    r"\b[A-Z][0-9]{2}-AUDIT\b",
    r"\bBC-[0-9]+\b",
    r"\bPERF-[0-9]",
    r"\bCR-[0-9]{2}\b",
    r"\bfixy-[A-Z]+[0-9]*-(?:[0-9]+\b|\*|X{3}\b)",
    r"\bfix-[0-9]+\b",
    r"(?<![\w-])[UV]-[0-9]{3}[a-z]?\b",
    r"(?<![\w.\-/])A[0-9]{1,2}(?:\.(?:[0-9]+|x))+(?![\w\-])",
    r"\bStage\s+[A-D](?:[0-9]{1,2}(?:\.[0-9]+)*)?\b",
    r"\b(?:at|task|tasks|since|until)\s+A[0-9]{1,2}\b",
    r"\bPhase\s+[A-Z][0-9]+\b",
    r"\bAgent\s+[0-9]+\b",
)
REFERENCE = re.compile("|".join(BRANCHES))

# The Python token kinds that hold prose.  FSTRING_MIDDLE and TSTRING_MIDDLE
# exist only in newer interpreters, so they are looked up by name.
PYTHON_PROSE = frozenset(
    kind for kind in (
        tokenize.COMMENT, tokenize.STRING,
        getattr(tokenize, "FSTRING_MIDDLE", None), getattr(tokenize, "TSTRING_MIDDLE", None),
    ) if kind is not None
)


class Unreadable(RuntimeError):
    """A file in scope cannot be read by its parser, so the scan cannot vouch for it."""


def scoped_files(root: Path) -> list[Path]:
    """Return every file in scope under root, repo-relative, sorted.

    The list comes from the index of git, through tsast.tracked_files, so a
    build product or an untracked scratch file is never read.  The self-test
    makes its scratch tree a repository for the same reason.  A symbolic link
    is in the list even when its target is missing, because the link holds its
    own prose, the path of the target.

    Complexity: linear in the number of files under root.

    Args:
        root: The repository root

    Returns:
        The files in scope, relative to root
    """
    return sorted(
        Path(name) for name in tsast.tracked_files(root)
        if name not in SKIPPED_FILES and not name.startswith(SKIPPED_DIRS)
        and ((root / name).is_symlink() or (root / name).is_file())
    )


def cpp_prose(tree: tsast.Tree) -> Iterator[tuple[int, str]]:
    """Yield every prose line of one C++ file, from its parse tree.

    Args:
        tree: The parse of the file

    Yields:
        (one-based line, text) for each line of each prose span
    """
    spans: list[tuple[int, str]] = [
        (node.line, node.text) for node in tree.find("comment", "string_literal", "raw_string_literal")
    ]
    for call in tree.find("preproc_call"):
        argument = call.child_by_field("argument")
        if argument is not None:
            spans.append((argument.line, argument.text))
    for body in tree.find("preproc_arg"):
        parent = body.parent
        if parent is None or parent.type not in ("preproc_def", "preproc_function_def"):
            continue
        for token in tsast.pp_tokens(body.text, first_row=body.start[0]):
            if token.kind == "string":
                spans.append((token.row + 1, token.text))
    for line, text in spans:
        for offset, part in enumerate(text.split("\n")):
            yield line + offset, part


def python_prose(relative: Path, data: bytes) -> Iterator[tuple[int, str]]:
    """Yield every prose line of one Python file, from its tokens.

    Args:
        relative: The repo-relative Python file, for a report
        data: The bytes of the file

    Yields:
        (one-based line, text) for each line of each comment or string

    Raises:
        Unreadable: If the file does not tokenize
    """
    try:
        # The universal newlines of a file read as text: each \r\n and each \r is \n.
        source = data.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
        tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
    except (tokenize.TokenError, SyntaxError, UnicodeDecodeError) as exc:
        raise Unreadable(f"{relative}: {exc}") from exc
    for token in tokens:
        if token.type in PYTHON_PROSE:
            for offset, part in enumerate(token.string.split("\n")):
                yield token.start[0] + offset, part


def text_lines(data: bytes) -> Iterator[tuple[int, str]]:
    """Yield every line of a file that has no parser here.

    A file that is not UTF-8 text is binary and holds no prose.

    Args:
        data: The bytes of the file

    Yields:
        (one-based line, text) for each line
    """
    if b"\0" in data:
        return
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return
    yield from enumerate(text.split("\n"), start=1)


def references(lines: Iterator[tuple[int, str]]) -> list[list]:
    """Return [line, text] for each prose line that names a reference."""
    return [[line, text] for line, text in lines if REFERENCE.search(text)]


def result_key(kind: str, data: bytes) -> str:
    """Return the name of the stored result of one file: a hash of how the scan reads it and of its bytes."""
    return hashlib.sha256(kind.encode() + b"\0" + data).hexdigest()


def scan(root: Path) -> tuple[int, list[str]]:
    """Report every coordination reference in the prose of the files in scope.

    The store of utils/scripts/preprocessed.py keeps the references of each
    file under a hash of its bytes and of the way the scan reads it, and
    under a name that hashes this guard and the parser.  So a warm run reads
    and hashes each file, and parses none.  A file that the scan cannot read
    gets no stored result, so each run reports it again.

    Complexity: O(total bytes in scope) plus one parse of each C++ file that
    the store does not hold.

    Args:
        root: The repository root

    Returns:
        (exit status, report lines), with the status as the module docstring states
    """
    files = scoped_files(root)
    # A symbolic link is read as the path of its target, the text that git
    # stores for it.  The content behind the link is not read under the name of
    # the link, so a reference in a linked document is not reported twice.
    links = {f for f in files if (root / f).is_symlink()}
    targets = [(link, 1, str((root / link).readlink())) for link in sorted(links)]
    hits: list[tuple[Path, int, str]] = [hit for hit in targets if REFERENCE.search(hit[2])]
    results = text_results("no-coordination-refs", Path(__file__), tsast.parser_identity())
    missed_cpp: list[Path] = []
    try:
        for relative in files:
            if relative in links:
                continue
            data = (root / relative).read_bytes()
            # A file that the kit cannot parse, such as generated BPF C, is
            # read line by line, so no file in scope goes unread.
            if tsast.is_in_cpp_scope(relative):
                kind = "cpp"
            else:
                kind = "python" if relative.suffix == ".py" else "text"
            stored = None if results is None else results.get(result_key(kind, data))
            if isinstance(stored, list):
                found = stored
            elif kind == "cpp":
                missed_cpp.append(relative)
                continue
            else:
                found = references(python_prose(relative, data) if kind == "python" else text_lines(data))
                if results is not None:
                    results.put(result_key(kind, data), found)
            hits.extend((relative, line, text) for line, text in found)
        for relative, tree in zip(missed_cpp, tsast.parse([root / f for f in missed_cpp]), strict=True):
            found = references(cpp_prose(tree))
            if results is not None:
                results.put(result_key("cpp", tree.source), found)
            hits.extend((relative, line, text) for line, text in found)
    except tsast.ParseError as exc:
        return 2, [f"check-no-coordination-refs: cannot read a file in scope: {exc}"]
    except Unreadable as exc:
        return 2, [f"check-no-coordination-refs: cannot read a file in scope: {exc}"]
    report = [f"{relative.as_posix()}:{line}: {text.strip()}" for relative, line, text in sorted(set(hits))]
    if report:
        report += [
            "",
            f"check-no-coordination-refs: {len(report)} line(s) name a task, a stage or a tracker entry.",
            "State what the code does, and remove the reference:",
            '  "Folded into one concept at #147"  ->  "Folded into one concept"',
            '  "arrives with #190"                ->  "arrives with the endpoint bridge"',
            "If the sentence says nothing without the reference, remove the sentence.",
            "A collision rule code (S004, W001) is not a reference.",
        ]
        return 1, report
    return 0, report


PLANTED_REFERENCES = (
    "Folded with its two siblings at #147.",
    "See task #193 for the fold.",
    "The forgery (#172, Door 2) is closed.",
    "Wired by Agent 11 in its second tier.",
    "Ported at A13.2.",
    "The wrappers arrive in tasks A10.x.",
    "The grant tier retired at A9.",
    "Stage D deletes the shim.",
    "Tracked as FIXY-V-264.",
    "The cache row fence of FOUND-I02.",
    "The cluster WRAP-Cipher-2.",
    "Per fixy-A5-016.",
    "Content-keyed since fix-18.",
    "Migrated at V-073.",
    "The attack pattern of CR-05.",
    "Part of Phase F6.",
    "Filed as #1519.",
    "The gate of GAPS-096.",
    "The fast path of PERF-2.",
    "Pinned by PROD-WRAP-5.",
    "The row matrix of FOUND_I16.",
    "The underscore gate of GAPS_096.",
    "Checked in group AUDIT-B.",
    "Checked in group audit-D.",
    "Per F09-AUDIT.",
)

# One planted reference in each area that the scan once skipped: the runtime
# headers and sources, the tests and their build file, the fuzz and tool
# trees, the build and toolchain files, the papers, the root documents and a
# generated C++ file that the kit cannot parse.
NEWLY_COVERED = (
    ("include/crucible/PlantedRuntime.h", "// Folded at #147.\n"),
    ("src/cntp/Planted.cpp", "// Tracked as GAPS-096.\n"),
    ("test/CMakeLists.txt", "# Filed as #1519.\n"),
    ("test/test_planted.cpp", "// Lifted at FOUND-I05.\n"),
    ("test/planted_neg/neg_planted.cpp", 'static_assert(false, "WRAP-Types-3 fixture");\n'),
    ("test/fuzz/property/prop_planted.cpp", "// The production cite of CONTRACT-112.\n"),
    ("utils/tools/planted_tool.cpp", "// The refresh thread is #67.\n"),
    ("cmake/Planted.cmake", "# FIXY-V-094.  The floor.\n"),
    ("CMakePresets.json", '{"description": "Stage D preset"}\n'),
    ("examples/fn/planted.cpp", "// Ported at A13.2.\n"),
    ("utils/toolchain/gcc/planted.sh", "# Part of Phase F6.\n"),
    ("papers/whitepaper/planted.tex", "% Per fixy-A5-016.\n"),
    ("CLAUDE.md", "Migrated at V-073.\n"),
    ("README.md", "Wired by Agent 11.\n"),
    (".gitattributes", "# Language stats (fix-22).\n"),
    ("include/crucible/perf/bpf/vmlinux.h", "struct planted { int operator; };  /* CR-05 */\n"),
)

# The two paths that stay out of scope, each with a planted reference.
OUT_OF_SCOPE = (
    ("misc/notes.md", "Stage D deletes the shim.\n"),
    ("utils/scripts/check-no-coordination-refs.py", "# Filed as #1519.\n"),
)


def self_test() -> int:
    """Plant each reference family, the look-alikes and the scope edges, and check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def track(root: Path) -> None:
        """Add every file of the scratch tree to its index, as a commit would hold it.

        Args:
            root: The scratch repository root
        """
        subprocess.run(["git", "-C", str(root), "add", "-A", "--", "."], check=True, capture_output=True)

    def expect(name: str, root: Path, want_status: int, named: list[str], unnamed: list[str]) -> None:
        """Run the scan and compare its status and the sites it names.

        Args:
            name: What the case proves
            root: The scratch repository root
            want_status: The exit status the scan must give
            named: Site prefixes the report must contain
            unnamed: Site prefixes the report must not contain
        """
        status, report = scan(root)
        text = "\n".join(report)
        held = status == want_status and all(n in text for n in named) and not any(n in text for n in unnamed)
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            print(f"       status {status}, want {want_status}\n       " + text.replace("\n", "\n       "))

    def write(root: Path, relative: str, text: str) -> None:
        """Write one file into the scratch tree.

        Args:
            root: The scratch repository root
            relative: The repo-relative path
            text: The file content
        """
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    with cache_dir.scratch_root(), tempfile.TemporaryDirectory() as work:
        root = Path(work)
        throwaway_repo.init(root)
        write(root, "include/fixy/PlantedBreadcrumbs.h", "".join(f"// {line}\n" for line in PLANTED_REFERENCES))
        write(root, "include/foundation/PlantedClean.h",
              "#pragma once\n#include <cstdint>\n#define PLANTED_FLAG 1\n#if PLANTED_FLAG\n#endif\n"
              "// S004 refuses an async launch that carries no completion witness.\n"
              "// I002, F101, W001, P010 and V101 are collision rule codes.\n"
              "// UTF-8, SHA-256, AVX-512, FNV-1a, x86-64 and C++26 are not tags.\n"
              "// template <class B1, class B2> and A1 are template parameters.\n"
              "// ROW-CONTAINS-OK: a marker.  REFINED-PRE-OK: a marker too.\n"
              "// Headline #1 and item #2 number the items of a list.\n"
              "// &#123; is an entity, and #374151 is a color.\n"
              "// TVM Analyzer (PR #1367) has the same narrow scope.\n"
              "// An audit-trail entry, FOUND_ENTRY_COUNT and GAPS_TOTAL are not tags.\n")
        write(root, "utils/scripts/planted_clean.sh",
              'echo "$#" "${#arr[@]}"\nprintf \'%#08x\\n\' 255\n[[ "x" =~ [#0-9] ]]\n')
        write(root, "bench/planted_bench.cpp", "// Lifted to harness scope at GAPS-004y.\n// AVX-512 and x86-64 are not tags.\n")
        write(root, "CMakeLists.txt", "# Tracked as FIXY-V-264.\n")
        write(root, ".github/workflows/ci.yml", "  # The sibling guards (Stage A1).\n")
        track(root)
        planted = [f"include/fixy/PlantedBreadcrumbs.h:{n}:" for n in range(1, len(PLANTED_REFERENCES) + 1)]
        expect("each reference family is reported on its own line", root, 1,
               planted + ["CMakeLists.txt:1:", ".github/workflows/ci.yml:1:", "bench/planted_bench.cpp:1:"],
               ["PlantedClean.h", "planted_clean.sh", "bench/planted_bench.cpp:2:"])

        for relative in ("include/fixy/PlantedBreadcrumbs.h", "CMakeLists.txt", ".github/workflows/ci.yml",
                         "bench/planted_bench.cpp"):
            (root / relative).unlink()
        track(root)
        expect("rule codes, directives and look-alikes scan clean", root, 0, [], [])

        # Positive controls: the line grep of the old shell guard got each of
        # these wrong, one way or the other.
        write(root, "include/fixy/Code.h",
              "#pragma once\nstruct P { int x; };\ninline P A1{};\n"
              "inline int member_read = A1.x;\ninline int token = 12;\n")
        write(root, "utils/scripts/code.py", "A1 = type('A', (), {'x': 1})\nvalue = A1.x  # plain access\n")
        track(root)
        expect("a member access such as A1.x in C++ or Python code is not a stage label", root, 0, [], [])
        write(root, "include/fixy/Macro.h", '#pragma once\n#define NOTE "Folded at A10.x"\n#error Stage D is not here\n')
        write(root, "utils/scripts/doc.py", '"""Ported at A13.2."""\nMESSAGE = f"see {1} and #147"\n')
        write(root, "src/fixy/Moved.cpp", "// Tracked as GAPS-096.\n")
        track(root)
        expect("macro strings, directives, docstrings, f-strings and src/fixy are prose in scope", root, 1,
               ["include/fixy/Macro.h:2:", "include/fixy/Macro.h:3:", "utils/scripts/doc.py:1:",
                "utils/scripts/doc.py:2:", "src/fixy/Moved.cpp:1:"], ["include/fixy/Code.h", "utils/scripts/code.py"])

        # The scope is every tracked file: each area that the scan once
        # skipped reports its plant, and the two paths out of scope do not.
        # A file that git does not track is not read.
        for relative, text in NEWLY_COVERED + OUT_OF_SCOPE:
            write(root, relative, text)
        track(root)
        write(root, "include/crucible/Untracked.h", "// Folded at #147.\n")
        expect("every tracked file except misc/ and this guard is in scope", root, 1,
               [f"{relative}:1:" for relative, _ in NEWLY_COVERED],
               [relative for relative, _ in OUT_OF_SCOPE] + ["include/crucible/Untracked.h"])

        # A symbolic link holds the path of its target.  The link to CLAUDE.md
        # does not report the plant of CLAUDE.md a second time, and a link whose
        # target path holds a reference reports it, although the target is
        # missing.
        (root / "AGENTS.md").symlink_to("CLAUDE.md")
        (root / "planted-link.md").symlink_to("plans/FIXY-V-264.md")
        track(root)
        expect("a symbolic link is read as the path of its target, and a linked file under its own name only",
               root, 1, ["CLAUDE.md:1:", "planted-link.md:1:"], ["AGENTS.md"])

    if failures:
        print(f"check-no-coordination-refs --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-coordination-refs --self-test: every case holds")
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test or the scan, as the arguments ask.

    Args:
        argv: The command-line arguments after the program name

    Returns:
        The process exit status
    """
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv:
            print(__doc__, file=sys.stderr)
            return 2
        status, report = scan(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-no-coordination-refs: {exc}", file=sys.stderr)
        return 3
    for line in report:
        print(line, file=sys.stderr)
    if status == 0:
        print("check-no-coordination-refs: clean, no task, stage or tracker references in scope")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
