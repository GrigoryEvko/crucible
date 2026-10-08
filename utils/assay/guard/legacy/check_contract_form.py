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

The check also holds the assumption rule.  A function body that holds a
contract macro of foundation/contracts/Pre.h or Post.h holds no [[assume]]
about the values that the macro checks.  Under the ignore semantic the macro
gives the optimizer that assumption itself.  Under the observe semantic a
violation handler can return, and an assumption of a false condition after
the check is undefined behaviour.  Before the check, the optimizer can use
the assumption to delete the check.

WHAT THE CHECK READS
    The parse tree of the pinned tree-sitter kit (utils/assay/readers/tsast.py) of
    each C++ file that git tracks.  Each function_contract_specifier node is
    one specifier, on a function declarator or on a lambda declarator.

    For the assumption rule, the check reads each [[assume(X)]] and each
    [[gnu::assume(X)]], and each call of CRUCIBLE_PRE, CRUCIBLE_PRE_FAST,
    CRUCIBLE_PRE_MSG, CRUCIBLE_POST, CRUCIBLE_POST_FAST and CRUCIBLE_POST_MSG.
    Each one belongs to its nearest function body or lambda body.  The
    subjects of a condition are the access paths that it reads: a variable,
    a member through `.` or `->` (`this->` dropped), and the receiver of a
    member function call.  The name of a called function, a qualified name,
    a type and an identifier in capital letters are no subject.  An
    assumption is an error when its subjects are not empty and one contract
    condition of the same body has each of them.  So `[[assume(n > 0)]]`
    after `CRUCIBLE_PRE(decide::positive(n))` is an error, and
    `[[assume(p->size > 0)]]` after `CRUCIBLE_PRE(p != nullptr)` is not.

THE LEDGER
    utils/ledgers/contract-form.txt (the ledger module assay.core.ledger,
    policy admit) admits a file whose subject is the specifier itself.  A row
    is `PATH | - | admit | - | REASON`.  A specifier in an admitted file is
    no finding.  An admitted file that holds no specifier is an error, so the
    ledger only shrinks.  A file that does not obey the schema, a row with a
    kind or an entity, and a row for a file that is not a tracked C++ file
    are errors.  A missing file admits no file.  The ledger does not apply to
    the assumption rule.

LEVELS
    Each finding is an error.  The tree has no specifier outside the ledger
    and no assumption about the values of a contract check of its body, and
    the compiler rejects each specifier, so the check gives no warning.

Usage
    guard check --rule contract-form [--warnings-dir DIR]
    guard check --rule contract-form --self-test

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

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.readers import tsast

CHECK = "contract-form"
LEDGER = Path(rule_ledger.shown_path(CHECK))
SPECIFIER = "function_contract_specifier"
REPLACEMENT = {
    "pre": "write CRUCIBLE_PRE(condition) of foundation/contracts/Pre.h as the first statement of the function body",
    "post": "write CRUCIBLE_POST(result, condition) of foundation/contracts/Post.h before each return statement",
}
# The contract macros, with the position of the condition among their arguments.
CONTRACT_CONDITION = {
    "CRUCIBLE_PRE": 0, "CRUCIBLE_PRE_FAST": 0, "CRUCIBLE_PRE_MSG": 0,
    "CRUCIBLE_POST": 1, "CRUCIBLE_POST_FAST": 1, "CRUCIBLE_POST_MSG": 1,
}
FUNCTION_NODES = frozenset({"function_definition", "lambda_expression"})
# The parts of an expression that name no value of the function: a qualified
# name, a type and a literal.
NO_SUBJECT_NODES = frozenset({
    "qualified_identifier", "template_function", "type_descriptor", "type_identifier", "primitive_type",
    "number_literal", "string_literal", "char_literal", "raw_string_literal", "concatenated_string", "true", "false",
    "null", "sizeof_expression", "alignof_expression",
})
ASSUMPTION_WHY = ("Under the ignore semantic the macro gives the optimizer that assumption itself.  Under the observe "
                  "semantic a violation handler can return, and an assumption of a false condition after the check is "
                  "undefined behaviour.  Before the check, the optimizer can use the assumption to delete the check.  "
                  "Remove the [[assume]] (include/foundation/contracts/Pre.h).")


@dataclass(frozen=True)
class Condition:
    """One condition of a function body: an assumption or the condition of a contract macro."""

    macro: str
    line: int
    subjects: frozenset[str]


def access_path(node: tsast.Node) -> str | None:
    """Return the access path of a variable or a member access, or None for another expression.

    `x` gives "x", `p->a.b` gives "p.a.b" and `this->size_` gives "size_".

    Args:
        node: An expression node

    Returns:
        The path, with each member operator written as a dot
    """
    if node.type == "identifier":
        return tsast.lexeme(node)
    if node.type == "this":
        return "this"
    if node.type == "parenthesized_expression":
        inner = tsast.non_comment_children(node)
        return access_path(inner[0]) if len(inner) == 1 else None
    if node.type == "field_expression":
        receiver = node.child_by_field("argument")
        field = node.child_by_field("field")
        base = None if receiver is None else access_path(receiver)
        if base is None or field is None or field.type != "field_identifier":
            return None
        name = tsast.lexeme(field)
        return name if base == "this" else f"{base}.{name}"
    return None


def is_constant_name(path: str) -> bool:
    """Say whether a path is one identifier in capital letters, such as a macro or a named limit."""
    return "." not in path and path.isupper()


def subjects_of(node: tsast.Node, out: set[str]) -> None:
    """Add to out each access path that an expression reads.

    The receiver of a member function call is a subject, and the name of a
    called function is not.  Complexity: linear in the size of the expression.

    Args:
        node: An expression node
        out: The set to add the paths to
    """
    if node.type in NO_SUBJECT_NODES:
        return
    path = access_path(node)
    if path is not None:
        if path != "this" and not is_constant_name(path):
            out.add(path)
        return
    if node.type == "call_expression":
        function = node.child_by_field("function")
        if function is not None and function.type == "field_expression":
            receiver = function.child_by_field("argument")
            if receiver is not None:
                subjects_of(receiver, out)
        arguments = node.child_by_field("arguments")
        for argument in [] if arguments is None else tsast.non_comment_children(arguments):
            subjects_of(argument, out)
        return
    for child in tsast.non_comment_children(node):
        subjects_of(child, out)


def body_of(node: tsast.Node) -> tsast.Node | None:
    """Return the nearest function body or lambda body that holds a node, or None at namespace scope."""
    owner = node.ancestor_of_type("compound_statement")
    while owner is not None:
        parent = owner.parent
        if parent is not None and parent.type in FUNCTION_NODES:
            return owner
        owner = owner.ancestor_of_type("compound_statement")
    return None


def condition_of(node: tsast.Node, macro: str, index: int) -> Condition | None:
    """Return the condition of an assumption or a contract macro: the argument at index of its argument list."""
    lists = node.children_of_type("argument_list") if node.type == "attribute" else [
        child for child in [node.child_by_field("arguments")] if child is not None]
    arguments = tsast.non_comment_children(lists[0]) if lists else []
    if len(arguments) <= index:
        return None
    found: set[str] = set()
    subjects_of(arguments[index], found)
    return Condition(macro, node.line, frozenset(found))


def assumption_findings(rel: str, tree: tsast.Tree) -> list[check_report.Finding]:
    """Return one error for each assumption about the values of a contract condition of the same function body.

    Complexity: linear in the size of the parse tree, plus the product of the
    assumptions and the contracts of each body.

    Args:
        rel: The file, relative to the root of the tree
        tree: Its parse tree

    Returns:
        The findings in file order
    """
    assumptions: dict[tsast.Node, list[Condition]] = {}
    contracts: dict[tsast.Node, list[Condition]] = {}
    for attribute in tree.find("attribute"):
        name = attribute.child_by_field("name")
        prefix = attribute.child_by_field("prefix")
        if name is None or name.type != "identifier" or tsast.lexeme(name) != "assume":
            continue
        if prefix is not None and tsast.lexeme(prefix) != "gnu":
            continue
        body = body_of(attribute)
        condition = None if body is None else condition_of(attribute, "[[assume]]", 0)
        if body is not None and condition is not None and condition.subjects:
            assumptions.setdefault(body, []).append(condition)
    if not assumptions:
        return []
    for call in tree.find("call_expression"):
        function = call.child_by_field("function")
        if function is None or function.type != "identifier":
            continue
        macro = tsast.lexeme(function)
        if macro not in CONTRACT_CONDITION:
            continue
        body = body_of(call)
        condition = None if body is None else condition_of(call, macro, CONTRACT_CONDITION[macro])
        if body is not None and condition is not None:
            contracts.setdefault(body, []).append(condition)
    findings: list[check_report.Finding] = []
    for body, held in assumptions.items():
        for assumption in held:
            restated = next((contract for contract in contracts.get(body, [])
                             if assumption.subjects <= contract.subjects), None)
            if restated is None:
                continue
            findings.append(check_report.Finding(
                "error", rel, assumption.line, CHECK,
                f"an [[assume]] about {', '.join(sorted(assumption.subjects))} in a function body that does a check of "
                f"the same values with {restated.macro} at line {restated.line}.  {ASSUMPTION_WHY}"))
    return sorted(findings, key=lambda finding: finding.line)


@dataclass(frozen=True)
class Admission:
    """One row of the ledger."""

    path: str
    reason: str
    line: int


def read_ledger(root: Path) -> tuple[dict[str, Admission], list[check_report.Finding]]:
    """Read the ledger of one tree.

    Args:
        root: The root of the tree

    Returns:
        The rows by path, and one error for a file that the check cannot read or for each row that it refuses
    """
    shown = LEDGER.as_posix()
    try:
        ledger = rule_ledger.read(rule_ledger.rule_of(CHECK), root)
    except rows_grammar.RowsError as problem:
        return {}, [check_report.Finding("error", problem.path, problem.line, CHECK,
                                         f"the ledger does not obey its schema, so the check cannot know which file "
                                         f"it admits: {str(problem).removeprefix(f'{problem.path}:{problem.line}: ')}")]
    except (OSError, UnicodeDecodeError) as exc:
        return {}, [check_report.Finding("error", shown, 0, CHECK, f"the ledger cannot be read, so the check cannot "
                                                                   f"know which file it admits: {exc}")]
    rows: dict[str, Admission] = {}
    errors: list[check_report.Finding] = []
    for key, entry in sorted(ledger.base.items()):
        if key.kind or key.entity:
            errors.append(check_report.Finding("error", shown, entry.line, CHECK,
                                               "a row is `PATH | - | admit | - | REASON`, with no kind and no entity"))
            continue
        rows[key.path] = Admission(key.path, entry.reason, entry.line)
    return rows, errors


def evaluate(root: Path) -> list[check_report.Finding]:
    """Read each tracked C++ file of one tree, and return each finding.

    Complexity: linear in the size of the parse trees of the files.

    Args:
        root: The root of the tree

    Returns:
        The findings
    """
    rows, findings = read_ledger(root)
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
        findings.extend(assumption_findings(rel, tree))
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
                "error", LEDGER.as_posix(), admission.line, CHECK,
                f"{admission.path} is not a C++ file that git tracks.  Delete the row."))
        elif admission.path not in holders:
            findings.append(check_report.Finding(
                "error", LEDGER.as_posix(), admission.line, CHECK,
                f"{admission.path} holds no contract specifier.  The ledger only shrinks: delete the row."))
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
ADMITTED_ROW = "test/neg/Admitted.cpp | - | admit | - | the subject of the fixture is the specifier"


def planted_ledger(*rows: str) -> str:
    """Return the text of a planted ledger of the rule with the rows."""
    return f"# planted\nformat {rule_ledger.FORMAT}\nrule {CHECK}\n" + "".join(f"{row}\n" for row in rows)

# Each shape of an assumption about the values of a contract of its body, one on
# each line of RESTATED_LINES: after the check, the same values in another
# spelling, before the check, before a postcondition, a member through this->,
# the attribute of GNU, and the message form.
RESTATED = """#pragma once
struct Arena {
    int* alloc(int n) {
        CRUCIBLE_PRE(n > 0);
        [[assume(n > 0)]];
        return nullptr;
    }
    int* alloc_nonzero(unsigned n) {
        CRUCIBLE_PRE(::foundation::decide::positive(n));
        [[assume(n > 0)]];
        return nullptr;
    }
    int lookup(Hash hash) {
        [[assume(hash.raw() != 0)]];
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(hash));
        return 0;
    }
    int* make(int* body) {
        int* node = body;
        [[assume(node != nullptr)]];
        CRUCIBLE_POST(node, node != nullptr);
        return node;
    }
    int size() {
        CRUCIBLE_PRE(this->size_ > 0);
        [[assume(size_ > 0)]];
        return size_;
    }
    int gnu(int n) {
        CRUCIBLE_PRE_FAST(n < 8);
        [[gnu::assume(n < 8)]];
        return n;
    }
    int message(const Recipe& recipe) {
        CRUCIBLE_PRE_MSG(!recipe.hash.is_sentinel(), "a recipe hash is never the sentinel");
        [[assume(recipe.hash.raw() != ~0ull)]];
        return 0;
    }
    int size_ = 1;
};
"""
RESTATED_LINES = {5, 10, 14, 20, 26, 31, 36}

# Shapes that make no assumption about the values of a contract of their body:
# another value, a member of a checked pointer, a lambda body, a constant, an
# assumption with no contract, another attribute namespace, and a contract in
# another function.
INDEPENDENT = """#pragma once
int other(int x, int y) {
    CRUCIBLE_PRE(x > 0);
    [[assume(y < 4)]];
    return x + y;
}
int member(const Box* p) {
    CRUCIBLE_PRE(p != nullptr);
    [[assume(p->size > 0)]];
    return p->size;
}
int nested(int n) {
    CRUCIBLE_PRE(n > 0);
    auto inner = [](int n) { [[assume(n > 0)]]; return n; };
    return inner(n);
}
int constant(int n) {
    CRUCIBLE_PRE(CAPACITY > 0 && n > 0);
    [[assume(CAPACITY > 0)]];
    return n;
}
int alone(unsigned slot) {
    [[assume(slot < CAPACITY)]];
    return static_cast<int>(slot);
}
int vendor(int n) {
    CRUCIBLE_PRE(n > 0);
    [[msvc::assume(n > 0)]];
    return n;
}
int checked(int n) {
    CRUCIBLE_PRE(n > 0);
    return n;
}
int assumed(int n) {
    [[assume(n > 0)]];
    return n;
}
"""


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
        (root / LEDGER).parent.mkdir(parents=True)
        (root / "include/planted/Forms.h").write_text(FORMS, encoding="utf-8")
        (root / "include/planted/Clean.h").write_text(CLEAN, encoding="utf-8")
        (root / "test/neg/Admitted.cpp").write_text(ADMITTED, encoding="utf-8")
        ledger = root / LEDGER
        ledger.write_text(planted_ledger(ADMITTED_ROW), encoding="utf-8")

        found = evaluate(root)
        forms = {(item.line, item.message.split("`")[1]) for item in found if item.path == "include/planted/Forms.h"}
        expect("each form of a specifier is an error, also in each preprocessor arm", forms == FORM_LINES,
               f"missing {sorted(FORM_LINES - forms)}, unexpected {sorted(forms - FORM_LINES)}")
        expect("each finding is an error that names the replacement",
               all(item.level == "error" and "CRUCIBLE_P" in item.message for item in found))
        expect("a name, an assertion, a macro call, a comment and a text are no specifier",
               not [item for item in found if item.path == "include/planted/Clean.h"])
        expect("a specifier in an admitted file is no finding",
               not [item for item in found if item.path == "test/neg/Admitted.cpp"])
        expect("the planted tree has no other finding",
               all(item.path == "include/planted/Forms.h" for item in found), found)

        (root / "include/planted/Forms.h").write_text(CLEAN, encoding="utf-8")
        expect("a tree with no specifier outside the ledger gives no finding", evaluate(root) == [])

        (root / "include/planted/Restated.h").write_text(RESTATED, encoding="utf-8")
        (root / "include/planted/Independent.h").write_text(INDEPENDENT, encoding="utf-8")
        found = evaluate(root)
        restated = {item.line for item in found if item.path == "include/planted/Restated.h"}
        expect("each assumption about the values of a contract of its body is an error, before or after the check",
               restated == RESTATED_LINES,
               f"missing {sorted(RESTATED_LINES - restated)}, unexpected {sorted(restated - RESTATED_LINES)}")
        expect("each such finding is an error that names the contract and its line",
               all(item.level == "error" and "CRUCIBLE_P" in item.message and "at line" in item.message
                   for item in found if item.path == "include/planted/Restated.h"), found)
        expect("another value, a member of a checked pointer, a lambda body, a constant, a lone assumption, another "
               "attribute namespace and another function make no assumption about a contract",
               not [item for item in found if item.path == "include/planted/Independent.h"], found)
        (root / "test/neg/Admitted.cpp").write_text(ADMITTED + "int f(int n) {\n    CRUCIBLE_PRE(n > 0);\n"
                                                    "    [[assume(n > 0)]];\n    return n;\n}\n", encoding="utf-8")
        found = evaluate(root)
        expect("the ledger does not admit an assumption about the values of a contract",
               any(item.path == "test/neg/Admitted.cpp" and item.line == 4 for item in found), found)
        (root / "test/neg/Admitted.cpp").write_text(ADMITTED, encoding="utf-8")
        (root / "include/planted/Restated.h").unlink()
        (root / "include/planted/Independent.h").unlink()

        for text, label, needle in (
                (planted_ledger("test/neg/Admitted.cpp | - | admit | - | -"), "a row with no reason",
                 "gives a reason"),
                (planted_ledger(ADMITTED_ROW, "test/neg/Admitted.cpp | - | admit | - | again"),
                 "a second row for one file", "has a second row"),
                (planted_ledger(ADMITTED_ROW, "test/neg/Gone.cpp | - | admit | - | a reason"),
                 "a row for a missing file", "is not a C++ file that git tracks"),
                (planted_ledger(ADMITTED_ROW, "include/planted/Clean.h | - | admit | - | a reason"),
                 "a row for a file that holds no specifier", "holds no contract specifier"),
                (planted_ledger(ADMITTED_ROW, "include/planted/Clean.h | kind | admit | - | a reason"),
                 "a row with a kind", "no kind and no entity")):
            ledger.write_text(text, encoding="utf-8")
            found = evaluate(root)
            expect(f"an error: {label}", any(item.level == "error" and item.path == LEDGER.as_posix()
                                             and needle in item.message for item in found), found)
        ledger.unlink()
        found = evaluate(root)
        expect("a missing ledger admits no file",
               any(item.path == "test/neg/Admitted.cpp" and "specifier" in item.message for item in found), found)
        ledger.write_text(planted_ledger(ADMITTED_ROW), encoding="utf-8")

        (root / "include/planted/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        found = evaluate(root)
        expect("an error: a file that does not parse",
               any(item.path == "include/planted/Broken.h" and "does not parse" in item.message for item in found),
               found)
        (root / "include/planted/Broken.h").unlink()

        (root / "include/planted/Forms.h").write_text(FORMS, encoding="utf-8")
        warnings_dir = root / "warnings"
        with check_report.github_actions(False), contextlib.redirect_stdout(io.StringIO()) as printed:
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
    parser = argparse.ArgumentParser(prog="guard check --rule contract-form", description=__doc__.split("\n", 1)[0])
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
