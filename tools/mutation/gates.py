"""Find each gate of a C++ header and make the mutant that drops it.

A gate is a place where the header refuses a program: a requires clause, a
concept, a static_assert in a template, a deleted overload, a contract, a
seal check, or a fail-closed primary.  A mutant weakens exactly one gate.  If
every test still passes with the mutant in place, no test witnesses the gate.

The parse comes from the pinned tree-sitter kit through scripts/tsast.py, and
the contract specifiers come from scripts/cxx_lex.py over comment-free text.
Both modules are read, never changed.

Each mutant is a byte span of the header and the text that replaces it.  The
span is cut from the file bytes, so a mutant can be applied to any copy of
the header with the same bytes.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import cxx_lex  # noqa: E402  (a sibling module of the guards)
import tsast  # noqa: E402

# The call-shaped gates, by the name of the callee, and the index of the
# argument that holds the condition.
CALL_GATES: dict[str, int] = {
    "contract_assert": 0,
    "CRUCIBLE_PRE": 0,
    "CRUCIBLE_PRE_MSG": 0,
    "CRUCIBLE_PRE_FAST": 0,
    "CRUCIBLE_POST": 1,
}

# A seal check is a call whose only job is to stop the build.  The mutant
# removes the call.
SEAL_CALLS = ("require_seal_holds",)

# The declarations that carry a name for a gate report.
NAMED_DECLS = (
    "function_definition",
    "field_declaration",
    "declaration",
    "struct_specifier",
    "class_specifier",
    "concept_definition",
    "alias_declaration",
)

CONTRACT_SPECIFIER = re.compile(r"\b(pre|post)\s*\(")
SPECIFIER_LEAD = re.compile(r"(\)|\bnoexcept|\bconst|\boverride|\bfinal|&)\s*$")
IDENTIFIER_BYTE = re.compile(rb"[A-Za-z0-9_]")


@dataclass(frozen=True)
class Gate:
    """One gate of a header and the one mutant that weakens it."""

    header: str       # path relative to the repository root
    kind: str         # requires, concept, static_assert, delete, contract, seal, primary
    line: int         # 1-based line of the gate in the header
    start: int        # first byte of the span the mutant replaces
    end: int          # one past the last byte of that span
    replacement: str  # the text the mutant puts in the span
    entity: str       # the name of the declaration that holds the gate
    original: str     # the text of the span, for the report

    @property
    def key(self) -> str:
        """A name that identifies the mutant across runs of the same header bytes."""
        return f"{self.header}:{self.line}:{self.kind}:{self.start}"


def _line_starts(source: bytes) -> list[int]:
    """The byte offset of the first byte of each line."""
    starts = [0]
    starts.extend(i + 1 for i, byte in enumerate(source) if byte == 0x0A)
    return starts


def _span(node: tsast.Node, starts: list[int]) -> tuple[int, int]:
    """The byte span of a node in the file."""
    (row0, col0), (row1, col1) = node.start, node.end
    return starts[row0] + col0, starts[row1] + col1


def _entity(node: tsast.Node) -> str:
    """The name of the nearest declaration that holds the node, or an empty string."""
    decl = node.ancestor_of_type(*NAMED_DECLS, "template_declaration")
    if decl is not None and decl.type == "template_declaration":
        # A requires clause sits beside the declaration it constrains.
        inner = [child for child in decl.children if child.type in NAMED_DECLS]
        decl = inner[0] if inner else decl.ancestor_of_type(*NAMED_DECLS)
    while decl is not None:
        name = decl.child_by_field("name")
        if name is not None:
            return name.text.split("::")[-1]
        declarator = decl.child_by_field("declarator")
        while declarator is not None and declarator.type in (
            "function_declarator", "pointer_declarator", "reference_declarator", "init_declarator",
        ):
            inner = declarator.child_by_field("declarator")
            if inner is None:
                break
            declarator = inner
        if declarator is not None:
            text = re.split(r"[\s(<]", declarator.text.split("::")[-1].strip(), maxsplit=1)[0]
            if text:
                return text
        decl = decl.ancestor_of_type(*NAMED_DECLS)
    return ""


def _bounded(source: bytes, start: int, end: int, replacement: str) -> str:
    """The replacement, kept apart from the tokens on each side of the span.

    A requires-clause written `requires(X)` has the span `(X)`, and a bare
    `true` in it reads `requirestrue`, one identifier, so the mutant does not
    parse and its gate goes untested.  A span in parentheses keeps them, and
    a replacement that would join the identifier before or after the span
    gets a space on that side."""
    if not replacement:
        return replacement
    original = source[start:end]
    text = "(true)" if replacement == "true" and original[:1] == b"(" and original[-1:] == b")" else replacement
    if start > 0 and IDENTIFIER_BYTE.match(source[start - 1:start]) and IDENTIFIER_BYTE.match(text[:1].encode()):
        text = " " + text
    if end < len(source) and IDENTIFIER_BYTE.match(source[end:end + 1]) and IDENTIFIER_BYTE.match(
            text[-1:].encode()):
        text = text + " "
    return text


def _joined_by_and(node: tsast.Node, source: bytes, starts: list[int]) -> bool:
    """Say whether a two-operand node joins its operands with &&.

    The parse lists only named children, so the operator is read from the
    bytes between the two operands."""
    if len(node.children) != 2:
        return False
    _, left_end = _span(node.children[0], starts)
    right_start, _ = _span(node.children[1], starts)
    return source[left_end:right_start].strip() in (b"&&", b"and")


def _conjuncts(node: tsast.Node, source: bytes, starts: list[int]) -> list[tsast.Node]:
    """The leaves of a chain of && in a constraint, left to right."""
    if node.type in ("constraint_conjunction", "binary_expression") and _joined_by_and(node, source, starts):
        return _conjuncts(node.children[0], source, starts) + _conjuncts(node.children[1], source, starts)
    if node.type == "parenthesized_expression" and len(node.children) == 1:
        leaves = _conjuncts(node.children[0], source, starts)
        if len(leaves) > 1:
            return leaves
    return [node]


def _is_primary_template(struct: tsast.Node) -> bool:
    """Say whether a class specifier declares a primary template, not a specialization."""
    name = struct.child_by_field("name")
    return name is not None and name.type == "type_identifier" and struct.ancestor_of_type(
        "template_declaration") is not None


def _declaration_span(clause: tsast.Node, starts: list[int]) -> tuple[int, int, tsast.Node] | None:
    """The span of the whole declaration that a `= delete` clause ends."""
    decl = clause.ancestor_of_type("function_definition", "field_declaration", "declaration")
    if decl is None:
        return None
    outer = decl.parent
    if outer is not None and outer.type == "template_declaration":
        decl = outer
    start, end = _span(decl, starts)
    return start, end, decl


def _contract_specifiers(path: Path, source: bytes, rel: str) -> list[Gate]:
    """The pre and post specifiers of function declarations, found in comment-free text."""
    text = source.decode("utf-8", errors="replace")
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    if len(blanked.encode()) != len(source):
        # A multi-byte character shifts byte offsets.  Fall back to the
        # text offsets, which the check below keeps honest.
        pass
    gates: list[Gate] = []
    for match in CONTRACT_SPECIFIER.finditer(blanked):
        if not SPECIFIER_LEAD.search(blanked[max(0, match.start() - 40):match.start()]):
            continue
        depth = 0
        open_at = match.end() - 1
        close_at = None
        for index in range(open_at, len(blanked)):
            char = blanked[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    close_at = index
                    break
        if close_at is None:
            continue
        inner_start = open_at + 1
        if match.group(1) == "post":
            colon = re.search(r"(?<!:):(?!:)", blanked[inner_start:close_at])
            if colon is not None:
                inner_start += colon.end()
        start_b = len(text[:inner_start].encode())
        end_b = len(text[:close_at].encode())
        original = source[start_b:end_b].decode("utf-8", errors="replace")
        if not original.strip():
            continue
        line = text.count("\n", 0, match.start()) + 1
        name_match = re.search(r"([A-Za-z_][A-Za-z_0-9]*)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*[^;{]*$",
                               blanked[max(0, match.start() - 400):match.start()])
        gates.append(Gate(rel, "contract", line, start_b, end_b, " true ",
                          name_match.group(1) if name_match else "", original.strip()))
    return gates


def find_gates(path: Path, repo_root: Path = REPO_ROOT) -> list[Gate]:
    """Every gate of one header, with the mutant that weakens it.

    Complexity: linear in the size of the parse tree, plus one pass of the
    lexer over the text."""
    rel = str(path.resolve().relative_to(repo_root.resolve())) if path.is_absolute() else str(path)
    tree = next(tsast.parse([path], strict=False))
    source = tree.source
    starts = _line_starts(source)
    gates: list[Gate] = []

    def add(kind: str, node: tsast.Node, replacement: str, span: tuple[int, int] | None = None) -> None:
        start, end = span if span is not None else _span(node, starts)
        gates.append(Gate(rel, kind, node.line, start, end, _bounded(source, start, end, replacement), _entity(node),
                          source[start:end].decode("utf-8", errors="replace")))

    for clause in tree.find("requires_clause"):
        constraint = [child for child in clause.children]
        if not constraint:
            continue
        for leaf in _conjuncts(constraint[-1], source, starts):
            add("requires", leaf, "true")

    for concept in tree.find("concept_definition"):
        bodies = [child for child in concept.children if child.field != "name"]
        if bodies:
            for leaf in _conjuncts(bodies[-1], source, starts):
                add("concept", leaf, "true")

    for assertion in tree.find("static_assert_declaration"):
        if assertion.ancestor_of_type("template_declaration") is None:
            continue  # a self-test of the header, not a gate on a caller
        condition = assertion.child_by_field("condition") or (assertion.children[0] if assertion.children else None)
        if condition is not None:
            add("static_assert", condition, "true")

    for clause in tree.find("delete_method_clause"):
        found = _declaration_span(clause, starts)
        if found is not None:
            start, end, _ = found
            add("delete", clause, "", (start, end))

    for call in tree.find("call_expression"):
        function = call.child_by_field("function")
        if function is None:
            continue
        callee = function.text.split("::")[-1]
        if callee in CALL_GATES:
            arguments = call.child_by_field("arguments")
            if arguments is not None and len(arguments.children) > CALL_GATES[callee]:
                add("contract", arguments.children[CALL_GATES[callee]], "true")
        elif callee in SEAL_CALLS:
            add("seal", call, "static_cast<void>(0)")

    # The grammar parses contract_assert as a statement of its own, not as a call.
    for statement in tree.find("contract_assert_statement"):
        if statement.children:
            add("contract", statement.children[0], "true")

    for base in tree.find("base_class_clause"):
        struct = base.parent
        if struct is None or struct.type not in ("struct_specifier", "class_specifier"):
            continue
        if not _is_primary_template(struct):
            continue
        for child in base.children:
            if child.text.replace(" ", "") in ("std::false_type", "::std::false_type"):
                add("primary", child, "std::true_type")

    gates.extend(_contract_specifiers(path, source, rel))
    # A gate nested inside a deleted declaration or another span stays its
    # own mutant, because each mutant replaces one span of the original bytes.
    gates.sort(key=lambda gate: (gate.start, gate.kind))
    return gates


def entity_users(path: Path) -> dict[str, list[str]]:
    """For each name in a header, the declarations of the header that use it.

    A test that witnesses a gate often names a declaration that uses the
    gated one: a fixture calls the door, not the concept the door checks.
    Complexity: linear in the size of the parse tree."""
    tree = next(tsast.parse([path], strict=False))
    users: dict[str, list[str]] = {}
    for node_type in ("identifier", "type_identifier", "field_identifier"):
        for node in tree.find(node_type):
            user = _entity(node)
            # A structured binding has no name of its own, so it names no user.
            if re.fullmatch(r"[A-Za-z_]\w*", user) and user != node.text:
                named = users.setdefault(node.text, [])
                if user not in named:
                    named.append(user)
    return users


def apply(source: bytes, gate: Gate) -> bytes:
    """The header bytes with one gate weakened.

    The span must hold the text the gate was found with, or the mutant is
    refused, because a moved header would put the mutant in the wrong place."""
    if source[gate.start:gate.end].decode("utf-8", errors="replace") != gate.original:
        raise ValueError(f"{gate.key}: the header bytes moved since the gate was found")
    return source[:gate.start] + gate.replacement.encode() + source[gate.end:]


def main() -> int:
    """List the gates of the headers named on the command line."""
    total = 0
    for name in sys.argv[1:]:
        for gate in find_gates(Path(name)):
            total += 1
            print(f"{gate.header}:{gate.line}: {gate.kind:13} {gate.entity or '-':32} {gate.original[:70]!r}")
    print(f"{total} gates", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
