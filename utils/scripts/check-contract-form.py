#!/usr/bin/env python3
"""check-contract-form — no file of the tree has a P2900 contract specifier.

The tree writes each precondition as CRUCIBLE_PRE and each postcondition as
CRUCIBLE_POST, statements of the function body.  GCC 16 does not keep the
`pre` or `post` specifier of a template in a header unit or in a precompiled
header, and a constant evaluation can ignore the specifier.  The quarantine
plugin of utils/tools/quarantine/ rejects each specifier in each build
(CLAUDE.md section XII, "The contract rule").

This check is the second line of the rule.  The plugin sees only what a
compile sees.  The parser of this check does not preprocess, so it reads each
arm of each preprocessor conditional and each file that no build compiles,
for example a header of vessel/ when the build has no PyTorch tree.  It also
reads a member function of a local class in a template that the class
declares and does not define, which no hook of the plugin sees.  It does not
see a specifier that a macro spells, which the plugin sees.

WHAT THE CHECK READS
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py) of
    each C++ file that git tracks.  Each function_contract_specifier node is
    one specifier, on a function declarator or on a lambda declarator.

THE ALLOWLIST
    utils/scripts/contract-form-allowlist.txt admits a file whose subject is
    the specifier itself.  A row is `path | reason`.  A specifier in a listed
    file is no finding.  A listed file that holds no specifier is an error,
    so the list only shrinks.  A row with no reason, a row for a file that is
    not a tracked C++ file and a second row for one file are errors.

LEVELS
    Each finding is an error.  The tree has no specifier outside the
    allowlist, and the compiler rejects each one, so the check has no ledger
    and no warning.

Usage
    check-contract-form.py [--warnings-dir DIR]
    check-contract-form.py --self-test

Exit 0 with no finding, 1 on a finding or an input that the check cannot
read, 2 on a usage error or a failed self-test, 3 when the kit is missing.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402

CHECK = "contract-form"
ALLOWLIST = Path("utils/scripts/contract-form-allowlist.txt")
SPECIFIER = "function_contract_specifier"
REPLACEMENT = {
    "pre": "write CRUCIBLE_PRE(condition) of foundation/contracts/Pre.h as the first statement of the function body",
    "post": "write CRUCIBLE_POST(result, condition) of foundation/contracts/Post.h before each return statement",
}


@dataclass(frozen=True)
class Admission:
    """One row of the allowlist."""

    path: str
    reason: str
    line: int


def read_allowlist(root: Path) -> tuple[dict[str, Admission], list[check_report.Finding]]:
    """Read the allowlist of one tree.

    Args:
        root: The root of the tree

    Returns:
        The rows by path, and one error for each row that the check refuses or for a missing file
    """
    shown = ALLOWLIST.as_posix()
    path = root / ALLOWLIST
    rows: dict[str, Admission] = {}
    errors: list[check_report.Finding] = []
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        return rows, [check_report.Finding("error", shown, 0, CHECK, f"the allowlist cannot be read, so the check "
                                                                     f"cannot know which file it admits: {exc}")]
    for number, raw in enumerate(text.splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        listed, _bar, reason = (cell.strip() for cell in stripped.partition("|"))
        if not listed or not reason:
            errors.append(check_report.Finding("error", shown, number, CHECK,
                                               "a row is `path | reason`, and the reason is necessary"))
            continue
        if listed in rows:
            errors.append(check_report.Finding("error", shown, number, CHECK,
                                               f"{listed} has a second row.  Keep one row for each file."))
            continue
        rows[listed] = Admission(listed, reason, number)
    return rows, errors


def evaluate(root: Path) -> list[check_report.Finding]:
    """Read each tracked C++ file of one tree, and return each finding.

    Complexity: linear in the size of the parse trees of the files.

    Args:
        root: The root of the tree

    Returns:
        The findings
    """
    rows, findings = read_allowlist(root)
    files = [rel for rel in tsast.tracked_files(root) if tsast.is_in_cpp_scope(rel) and (root / rel).is_file()]
    relative_of = {(root / rel).as_posix(): rel for rel in files}
    holders: set[str] = set()
    for tree in tsast.parse([root / rel for rel in files], strict=False):
        rel = relative_of.get(Path(tree.path).as_posix(), Path(tree.path).as_posix())
        if tree.diagnostic is not None:
            findings.append(check_report.Finding("error", rel, 0, CHECK, f"the file does not parse, so its contract "
                                                                         f"specifiers are unknown.  "
                                                                         f"{tree.diagnostic.strip()}"))
            continue
        for node in tree.find(SPECIFIER):
            holders.add(rel)
            if rel in rows:
                continue
            kind = node.tokens()[0]
            findings.append(check_report.Finding(
                "error", rel, node.line, CHECK,
                f"a P2900 `{kind}` specifier is not permitted in this tree: "
                f"{REPLACEMENT.get(kind, REPLACEMENT['pre'])} (CLAUDE.md section XII, the contract rule)"))
    tracked = set(files)
    for admission in rows.values():
        if admission.path not in tracked:
            findings.append(check_report.Finding(
                "error", ALLOWLIST.as_posix(), admission.line, CHECK,
                f"{admission.path} is not a C++ file that git tracks.  Delete the row."))
        elif admission.path not in holders:
            findings.append(check_report.Finding(
                "error", ALLOWLIST.as_posix(), admission.line, CHECK,
                f"{admission.path} holds no contract specifier.  The list only shrinks: delete the row."))
    return findings


# Each form of a specifier, one on each line of FORM_LINES: a declaration, a
# definition with a postcondition, two specifiers on one declaration, a
# function template with no definition, a member of a class template, a member
# template, a member of a nested class of a class template, a hidden friend, a
# friend template, a member of a partial specialization, an abbreviated
# function template, a constrained template, a lambda, a generic lambda, a
# lambda in a function template, a local declaration, a member of a local
# class, a member of a local class in a template, an explicit specialization,
# and each arm of a preprocessor conditional.
FORMS = """#pragma once
int declared(int value) pre(value > 0);
int defined(int value) post(result : result > 0) { return value; }
int two(int value) pre(value > 0) post(result : result > 0);
template <class T> int template_declared(T value) pre(value > 0);
template <class T> struct Box {
    int member(int value) pre(value > 0);
    template <class U> int member_template(U value) pre(value > 0);
    struct Inner { int nested(int value) pre(value > 0); };
    friend int hidden(Box, int value) pre(value > 0) { return value; }
    template <class U> friend int befriended(Box, U value) pre(value > 0);
};
template <class T> struct Box<T*> { int partial(int value) pre(value > 0); };
int abbreviated(auto value) pre(value > 0);
template <class T> requires (sizeof(T) > 0) int constrained(T value) pre(value > 0);
inline auto lambda = [](int value) pre(value > 0) { return value; };
inline auto generic = [](auto value) pre(value > 0) { return value; };
template <class T> int with_lambda(T value) { return [](int other) pre(other > 0) { return other; }(value); }
void local_declarations() {
    int local(int value) pre(value > 0);
    struct Local { int member(int value) pre(value > 0); };
}
template <class T> void template_local() { struct Local { int member(int value) pre(value > 0); }; }
template <> int template_declared<long>(long value) pre(value > 0);
#if defined(__aarch64__)
int aarch64_only(int value) pre(value > 0);
#else
int other_arm(int value) post(result : result > 0);
#endif
#if 0
int never_compiled(int value) pre(value > 0);
#endif
"""
FORM_LINES = {(2, "pre"), (3, "post"), (4, "pre"), (4, "post"), (5, "pre"), (7, "pre"), (8, "pre"), (9, "pre"),
              (10, "pre"), (11, "pre"), (13, "pre"), (14, "pre"), (15, "pre"), (16, "pre"), (17, "pre"),
              (18, "pre"), (20, "pre"), (21, "pre"), (23, "pre"), (24, "pre"), (26, "pre"), (28, "post"),
              (31, "pre")}

# Shapes that are no specifier: a variable and a member with the names of the
# two specifiers, an assertion statement, a macro call, a comment and a text.
CLEAN = """#pragma once
int pre = 1;
struct Names { int post = 0; };
int asserted(int value) {
    contract_assert(value > 0);
    CRUCIBLE_PRE(value > 0);
    return value;
}
// int in_a_comment(int value) pre(value > 0);
inline const char* text = "int in_a_text(int value) pre(value > 0);";
"""

ADMITTED = "int admitted(int value) pre(value > 0);\n"


def self_test() -> int:
    """Plant each form of a specifier and each failure of an input in a scratch tree, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}" if detail != "" else name)

    tsast.kit_dir()
    with tempfile.TemporaryDirectory(prefix="contract-form-") as work:
        root = Path(work)
        (root / "include/planted").mkdir(parents=True)
        (root / "test/neg").mkdir(parents=True)
        (root / ALLOWLIST).parent.mkdir(parents=True)
        (root / "include/planted/Forms.h").write_text(FORMS, encoding="utf-8")
        (root / "include/planted/Clean.h").write_text(CLEAN, encoding="utf-8")
        (root / "test/neg/Admitted.cpp").write_text(ADMITTED, encoding="utf-8")
        allowlist = root / ALLOWLIST
        allowlist.write_text("# planted\ntest/neg/Admitted.cpp | the subject of the fixture is the specifier\n",
                             encoding="utf-8")

        found = evaluate(root)
        forms = {(item.line, item.message.split("`")[1]) for item in found if item.path == "include/planted/Forms.h"}
        expect("each form of a specifier is an error, also in each preprocessor arm", forms == FORM_LINES,
               f"missing {sorted(FORM_LINES - forms)}, unexpected {sorted(forms - FORM_LINES)}")
        expect("each finding is an error that names the replacement",
               all(item.level == "error" and "CRUCIBLE_P" in item.message for item in found))
        expect("a name, an assertion, a macro call, a comment and a text are no specifier",
               not [item for item in found if item.path == "include/planted/Clean.h"])
        expect("a specifier in a listed file is no finding",
               not [item for item in found if item.path == "test/neg/Admitted.cpp"])
        expect("the planted tree has no other finding",
               all(item.path == "include/planted/Forms.h" for item in found), found)

        (root / "include/planted/Forms.h").write_text(CLEAN, encoding="utf-8")
        expect("a tree with no specifier outside the allowlist gives no finding", evaluate(root) == [])

        for text, label, needle in (
                ("test/neg/Admitted.cpp\n", "a row with no reason", "the reason is necessary"),
                ("test/neg/Admitted.cpp | a reason\ntest/neg/Admitted.cpp | again\n", "a second row for one file",
                 "has a second row"),
                ("test/neg/Admitted.cpp | a reason\ntest/neg/Gone.cpp | a reason\n", "a row for a missing file",
                 "is not a C++ file that git tracks"),
                ("test/neg/Admitted.cpp | a reason\ninclude/planted/Clean.h | a reason\n",
                 "a row for a file that holds no specifier", "holds no contract specifier")):
            allowlist.write_text(text, encoding="utf-8")
            found = evaluate(root)
            expect(f"an error: {label}", any(item.level == "error" and item.path == ALLOWLIST.as_posix()
                                             and needle in item.message for item in found), found)
        allowlist.unlink()
        found = evaluate(root)
        expect("an error: a missing allowlist", any("cannot be read" in item.message for item in found), found)
        allowlist.write_text("test/neg/Admitted.cpp | the subject of the fixture is the specifier\n",
                             encoding="utf-8")

        (root / "include/planted/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        found = evaluate(root)
        expect("an error: a file that does not parse",
               any(item.path == "include/planted/Broken.h" and "does not parse" in item.message for item in found),
               found)
        (root / "include/planted/Broken.h").unlink()

        (root / "include/planted/Forms.h").write_text(FORMS, encoding="utf-8")
        warnings_dir = root / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(evaluate(root), CHECK, warnings_dir)
        lines = printed.getvalue().splitlines()
        expect("an error gives exit status 1, the line format, and no warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists() and bool(lines)
               and all(check_report.parse_line(line) is not None for line in lines), lines[:3])
    if failures:
        for failure in failures:
            print(f"check-contract-form --self-test: FAILED, {failure}", file=sys.stderr)
        return 2
    print("check-contract-form --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-contract-form.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    arguments = parser.parse_args(argv)
    try:
        if arguments.self_test:
            return self_test()
        findings = evaluate(tsast.REPO_ROOT)
    except tsast.KitMissing as missing:
        print(f"check-contract-form: SKIP, {missing}", file=sys.stderr)
        return 3
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    print(f"check-contract-form: {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
