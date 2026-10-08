"""row-contains — a check that a context owns an effect uses a named lift.

THE LIFTS
    A check that a context owns an effect has a named lift:

        row_contains(^^row_type_of_t<Ctx>, Effect::X)          ->  CtxOwnsCapability<Ctx, Effect::X>
        row_contains(^^..., X) || row_contains(^^..., Y)       ->  CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>
        row_contains(^^..., X) && row_contains(^^..., Y)       ->  CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>

    A search for CtxOwns then finds each capability admission, and a change
    of the name of the row accessor gets to each check through the lift.

THE RULE
    A check is a call of row_contains, with no qualifier or with each
    qualifier, so an alias of the effects namespace and a using-directive do
    not hide it.  Its first argument reads the row of a context: a
    template-id named row_type_of_t, or a qualified name whose last part is
    row_type, such as `^^typename Ctx::row_type`.  The rule refuses:
      unlifted  a check outside the definitions of the lifts
    A membership test on a concrete row, such as a row that a static_assert
    names, is not a check of a context.  A name is compared as the lexer
    spells it, after the line splices of phase 2.

THE ENTITY
    The entity of a finding is `FUNCTION: ROW: EFFECT`: the holder of the
    check, and the spelling of its two arguments (tsast.spelled()).  The
    holder is the signature of the function whose body holds the check
    (tsast.function_signature), else the qualified name of the declaration
    that holds it, or `#define NAME` for a check in a macro body.

THE MACRO BODIES
    The rule reads the parse of each macro body (tsast.macro_bodies), so a
    block comment in a body does not divide the check.  It also reads the
    preprocessing tokens of each body: row_contains, `(`, and a first
    argument that holds row_type_of_t or `:: row_type`.  So a body that the
    kit cannot parse, such as one that pastes tokens with ##, and a call that
    a line splice cuts, are read.

THE SCOPE
    The scope of the registry, less the definitions of the lifts in
    include/foundation/effects/, and less each directory test, bench,
    examples, third_party, external and vendor.

Complexity: linear in the size of the files of the scope and of their macro bodies.
"""

from collections.abc import Iterator
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "row-contains"
KIND = "unlifted"
NAME = "row_contains"
ROW_OF = "row_type_of_t"
ROW_MEMBER = "row_type"
ROW_NODES = ("template_type", "template_function", "qualified_identifier")
EXCLUDED_PREFIXES = ("include/foundation/effects/",)
EXCLUDED_DIRECTORIES = frozenset({"test", "bench", "examples", "third_party", "external", "vendor"})
DECLARATIONS = ("declaration", "field_declaration", "template_declaration", "alias_declaration",
                "concept_definition", "function_definition")
OPENERS = {"(": 1, "[": 1, "{": 1, "<": 1, ")": -1, "]": -1, "}": -1, ">": -1, ">>": -2}
MESSAGE = ("`{entity}` checks the row of a context with row_contains.  Write CtxOwnsCapability<Ctx, Effect::X>, "
           "CtxOwnsAnyOf or CtxOwnsAllOf.  A check that must stay inline gets a ledger row with its reason")


def called_name(call: tsast.Node) -> str | None:
    """Return the last part of the name of the function of a call, or None for a call of a different shape."""
    function = call.child_by_field("function")
    while function is not None and function.type == "qualified_identifier":
        function = function.child_by_field("name")
    return tsast.spelled(function) if function is not None and function.type == "identifier" else None


def arguments_of(call: tsast.Node) -> list[tsast.Node]:
    """Return the arguments of a call, with no comment."""
    arguments = call.child_by_field("arguments")
    return tsast.non_comment_children(arguments) if arguments is not None else []


def reads_context_row(argument: tsast.Node) -> bool:
    """Return whether an argument reads the row of a context (THE RULE)."""
    for node in [argument, *argument.descendants(*ROW_NODES)]:
        named = node.child_by_field("name")
        if named is None:
            continue
        if node.type in ("template_type", "template_function") and tsast.spelled(named) == ROW_OF:
            return True
        if node.type == "qualified_identifier" and node.child_by_field("scope") is not None \
                and named.type in ("type_identifier", "identifier") and tsast.spelled(named) == ROW_MEMBER:
            return True
    return False


def context_checks(nodes: Iterator[tsast.Node]) -> Iterator[tuple[tsast.Node, str]]:
    """Yield each call among the nodes that checks the row of a context, with `ROW: EFFECT` of its arguments."""
    for node in nodes:
        arguments = arguments_of(node)
        if called_name(node) == NAME and arguments and reads_context_row(arguments[0]):
            yield node, ": ".join(tsast.spelled(argument) for argument in arguments[:2])


def holder_of(node: tsast.Node) -> str:
    """Return the holder of a check in a file (THE ENTITY)."""
    signature = tsast.function_signature(node)
    if signature is not None:
        return signature
    namespaces = tuple(tsast.ANONYMOUS_NAMESPACE if part == "" else part for part in tsast.namespace_path(node))
    declaration = node.ancestor_of_type(*DECLARATIONS)
    while declaration is not None and declaration.type == "template_declaration":
        inner = [child for child in tsast.non_comment_children(declaration) if child.type in DECLARATIONS]
        declaration = inner[-1] if inner else None
    declarator = None
    if declaration is not None:
        declarator = declaration.child_by_field("declarator") or declaration.child_by_field("name")
    name = tsast.leaf_name(declarator) if declarator is not None else None
    if name is not None:
        return "::".join((*namespaces, name))
    return "::".join(namespaces) or "-"


def token_checks(tokens: list[tsast.Token]) -> Iterator[tuple[int, str]]:
    """Yield the row and `ROW: EFFECT` of each check in the tokens of a macro body.

    The first argument runs from the `(` after row_contains to the first `,`
    or `)` at the same depth, and the second argument runs to the next `,` or
    `)` at that depth.  The check reads a context row when its first argument
    holds row_type_of_t, or `::` and then row_type.
    """
    for index, token in enumerate(tokens):
        if tsast.lexeme(token) != NAME or index + 1 >= len(tokens) or tokens[index + 1].text != "(":
            continue
        pieces: list[list[str]] = [[]]
        depth, cursor = 0, index + 2
        while cursor < len(tokens) and len(pieces) <= 2:
            text = tokens[cursor].text
            if text in (",", ")") and depth == 0:
                if text == ")":
                    break
                pieces.append([])
            else:
                depth += OPENERS.get(text, 0)
                pieces[-1].append(tsast.lexeme(tokens[cursor]))
            cursor += 1
        first = pieces[0]
        if ROW_OF in first or any(first[at] == "::" and first[at + 1] == ROW_MEMBER for at in range(len(first) - 1)):
            yield token.row, ": ".join(tsast.spelled_tokens(piece) for piece in pieces[:2])


def is_excluded(path: str) -> bool:
    """Return whether a file holds the definitions of the lifts, or is in a directory out of the scope."""
    return path.startswith(EXCLUDED_PREFIXES) or any(
        part in EXCLUDED_DIRECTORIES for part in PurePosixPath(path).parts[:-1])


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each check of the row of a context with no lift (THE RULE)."""
    sites: dict[tuple[str, int, str], None] = {}
    trees = list(context.trees([path for path in context.scope_files() if not is_excluded(path)]))
    for tree in trees:
        for node, arguments in context_checks(tree.find("call_expression")):
            sites[(tree.path.as_posix(), node.line, f"{holder_of(node)}: {arguments}")] = None
    for body in context.macro_bodies(trees):
        path = PurePosixPath(body.define.tree.path).as_posix()
        holder = f"#define {body.name}"
        if body.is_parsed:
            for node, arguments in context_checks(body.root.descendants("call_expression")):
                sites[(path, body.origin(node)[0] + 1, f"{holder}: {arguments}")] = None
        for row, arguments in token_checks(tsast.pp_tokens(body.text, body.first_row)):
            sites[(path, row + 1, f"{holder}: {arguments}")] = None
    return [check_report.Finding("error", path, line, RULE, MESSAGE.format(entity=entity), KIND, entity)
            for path, line, entity in sorted(sites)]
