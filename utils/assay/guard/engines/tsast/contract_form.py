"""contract-form — no file of the tree has a P2900 contract specifier, and no [[assume]] restates a contract.

THE SPECIFIER
    The tree writes each precondition as `CRUCIBLE_PRE` and each
    postcondition as `CRUCIBLE_POST`, statements of the function body.
    GCC 16 does not keep the `pre` or `post` specifier of a template in a
    header unit or in a precompiled header.  A constant evaluation can ignore
    the specifier.  The quarantine plugin of utils/tools/quarantine/ refuses
    each specifier in each build (CLAUDE.md section XII, "The contract rule").

    This rule is the second line of that rule.  The plugin sees only what a
    compile sees.  The kit does not preprocess.  This rule then reads each arm
    of each preprocessor conditional.  It also reads each file that no build
    compiles, for example a header of vessel/ when the build has no PyTorch
    tree.  And it reads a member function of a local class in a template when
    the class declares the function and does not give its body.  No hook of the
    plugin sees that function.  The rule does not see a specifier that a
    macro spells, and the plugin sees it.  The rule refuses:
      specifier   a `function_contract_specifier` node, on a function
                  declarator or on a lambda declarator
    The entity of the kind is empty.  A ledger row then admits each specifier
    of its file.  Such a file is a probe, a fixture or a plugin test with the
    specifier as its subject.

THE ASSUMPTION
    A function body that holds a contract macro of foundation/contracts/Pre.h
    or Post.h holds no `[[assume]]` about the values that the macro checks.
    With the ignore semantic, the macro itself gives that assumption to the
    optimizer.  With the semantic that reports a violation and continues, a
    violation handler can return.  After the check, an assumption of an
    incorrect condition is undefined behavior.  Before the check, the
    optimizer can use the assumption to delete the check.  The rule refuses:
      assumption  an `[[assume(X)]]` or a `[[gnu::assume(X)]]` with subjects,
                  when one contract condition of the same body has each subject
    The contract conditions are the conditions of `CRUCIBLE_PRE`,
    `CRUCIBLE_PRE_FAST`, `CRUCIBLE_PRE_MSG`, `CRUCIBLE_POST`,
    `CRUCIBLE_POST_FAST` and `CRUCIBLE_POST_MSG`.  Each condition belongs to
    its nearest function body or lambda body.  The subjects of a condition are
    the access paths that it reads.  A subject is a variable, a member through
    `.` or `->` (the rule removes `this->`), or the receiver of a call of a
    member function.  These are no subject: a called function, a name after a
    namespace or a class, a type and a name in capital letters.
    For example, `[[assume(n > 0)]]` after `CRUCIBLE_PRE(decide::positive(n))`
    is an error, and `[[assume(p->size > 0)]]` after
    `CRUCIBLE_PRE(p != nullptr)` is not.  The entity of the kind is
    `FUNCTION: SUBJECTS`.

THE SCOPE
    Each C++ file of the tree.

Complexity: linear in the number of bytes of the files, plus the product of the assumptions and the contracts of each body.
"""

from dataclasses import dataclass

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "contract-form"
SPECIFIER_KIND = "specifier"
ASSUMPTION_KIND = "assumption"
SPECIFIER = "function_contract_specifier"
REPLACEMENT = {
    "pre": "write CRUCIBLE_PRE(condition) of foundation/contracts/Pre.h as the first statement of the function body",
    "post": "write CRUCIBLE_POST(result, condition) of foundation/contracts/Post.h before each return statement",
}
# The contract macros, with the position of the condition in their arguments.
CONTRACT_CONDITION = {
    "CRUCIBLE_PRE": 0,
    "CRUCIBLE_PRE_FAST": 0,
    "CRUCIBLE_PRE_MSG": 0,
    "CRUCIBLE_POST": 1,
    "CRUCIBLE_POST_FAST": 1,
    "CRUCIBLE_POST_MSG": 1,
}
FUNCTION_NODES = frozenset({"function_definition", "lambda_expression"})
# The parts of an expression that name no value of the function: a name with a namespace or a class, a type and a
# literal.
NO_SUBJECT_NODES = frozenset(
    {
        "qualified_identifier",
        "template_function",
        "type_descriptor",
        "type_identifier",
        "primitive_type",
        "number_literal",
        "string_literal",
        "char_literal",
        "raw_string_literal",
        "concatenated_string",
        "true",
        "false",
        "null",
        "sizeof_expression",
        "alignof_expression",
    }
)
ASSUMPTION_WHY = (
    "Under the ignore semantic the macro gives the optimizer that assumption itself.  Under the observe "
    "semantic a violation handler can return, and an assumption of a false condition after the check is "
    "undefined behavior.  Before the check, the optimizer can use the assumption to delete the check.  "
    "Remove the [[assume]] (include/foundation/contracts/Pre.h)"
)


@dataclass(frozen=True, slots=True)
class Condition:
    """One condition of a function body: an assumption or the condition of a contract macro."""

    macro: str
    line: int
    subjects: frozenset[str]


def access_path(node: tsast.Node) -> str | None:
    """Return the access path of a variable or a member access, or None for a different expression.

    `x` gives "x", `p->a.b` gives "p.a.b" and `this->size_` gives "size_".
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
    """Return True when a path is one identifier in capital letters, such as a macro or a named limit."""
    return "." not in path and path.isupper()


def subjects_of(node: tsast.Node, out: set[str]) -> None:
    """Add to out each access path that an expression reads.

    The receiver of a call of a member function is a subject, and the name of a called function is not.
    Complexity: linear in the number of nodes of the expression.
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
    lists = (
        node.children_of_type("argument_list")
        if node.type == "attribute"
        else [child for child in [node.child_by_field("arguments")] if child is not None]
    )
    arguments = tsast.non_comment_children(lists[0]) if lists else []
    if len(arguments) <= index:
        return None
    found: set[str] = set()
    subjects_of(arguments[index], found)
    return Condition(macro, node.line, frozenset(found))


def assumption_findings(path: str, tree: tsast.Tree) -> list[check_report.Finding]:
    """Return one error for each assumption about the values of a contract condition of the same body (THE ASSUMPTION).

    Complexity: linear in the number of nodes of the tree, plus the product of the assumptions and the contracts of
    each body.
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
    found: list[check_report.Finding] = []
    for body, held in assumptions.items():
        holder = tsast.function_signature(body) or "-"
        for assumption in held:
            restated = next(
                (contract for contract in contracts.get(body, []) if assumption.subjects <= contract.subjects), None
            )
            if restated is None:
                continue
            subjects = ", ".join(sorted(assumption.subjects))
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    assumption.line,
                    RULE,
                    f"an [[assume]] about {subjects} in a function body that does a "
                    f"check of the same values with {restated.macro} at line {restated.line}.  {ASSUMPTION_WHY}",
                    ASSUMPTION_KIND,
                    f"{holder}: {subjects}",
                )
            )
    return sorted(found, key=lambda item: item.line)


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each contract specifier, and for each assumption that says again what a contract checks."""
    found: list[check_report.Finding] = []
    for tree in context.trees(context.scope_files()):
        path = tree.path.as_posix()
        found += assumption_findings(path, tree)
        for node in tree.find(SPECIFIER):
            kind = node.tokens()[0]
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    node.line,
                    RULE,
                    f"a P2900 `{kind}` specifier is not permitted in this tree: "
                    f"{REPLACEMENT.get(kind, REPLACEMENT['pre'])} (CLAUDE.md section XII, the contract rule)",
                    SPECIFIER_KIND,
                )
            )
    return found
