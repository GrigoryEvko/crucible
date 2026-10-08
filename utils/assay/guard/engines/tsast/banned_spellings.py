"""banned-spellings — no code of the scope spells the keyword reinterpret_cast.

THE RULE
    CLAUDE.md section III bans `reinterpret_cast`.  Use `std::bit_cast` with
    the limits of section IV for a copy of the bits of a value.  Use
    `std::start_lifetime_as` for the objects of an arena.  The tree of a cast
    does not keep the keyword as a node.  GCC gives a warning only for a cast
    from one pointer type to a different pointer type.  For these two
    reasons, the rule reads the keyword:
      reinterpret-cast  an identifier token spelled `reinterpret_cast`
    The word is a keyword and has no other use.  A comment, a string literal
    and a raw string are nodes of their own, and they give no incorrect
    finding.  The rule finds a cast on more than one line, and a cast with a
    space before its `<`.

THE MACRO BODIES
    The rule reads the parse of each macro body (`tsast.macro_bodies`) with
    the same node rule.  When the kit cannot parse a body, for example a body
    with `#` or `##`, the rule reads its preprocessing tokens.

THE ENTITY
    The entity of a finding is `HOLDER: reinterpret_cast<TYPE>`, with the
    spelling of the target type (`tsast.spelled()`).  HOLDER is the signature
    of the function that holds the cast (`tsast.function_signature`).  For a
    cast in no function, HOLDER is the scope of the cast: the names of its
    namespaces and its classes.  For a cast in a macro body, HOLDER is
    `#define NAME`.  When the tree shows no target type, as in a body that
    does not parse, the entity has `reinterpret_cast` alone.

THE SCOPE
    The scope of the registry, less each directory test, examples, bench,
    third_party, external and vendor.

Complexity: linear in the number of bytes of the files of the scope and of their macro bodies.
"""

from collections.abc import Iterator
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext, has_directory
from assay.readers import tsast

RULE = "banned-spellings"
KIND = "reinterpret-cast"
KEYWORD = "reinterpret_cast"
EXCLUDED_DIRECTORIES = frozenset({"test", "examples", "bench", "third_party", "external", "vendor"})
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
MESSAGE = (
    "`{entity}` spells the keyword reinterpret_cast, which CLAUDE.md section III bans.  Use std::bit_cast with "
    "the limits of section IV, or std::start_lifetime_as for the objects of an arena"
)


def scope_holder(node: tsast.Node) -> str:
    """Return the scope of a node in no function: the names of its namespaces and its classes, or `-`."""
    parts = [tsast.ANONYMOUS_NAMESPACE if part == "" else part for part in tsast.namespace_path(node)]
    classes: list[str] = []
    holder = node.ancestor_of_type(*CLASSES)
    while holder is not None:
        name = holder.child_by_field("name")
        classes.insert(0, (tsast.leaf_name(name) or "?") if name is not None else "?")
        holder = holder.ancestor_of_type(*CLASSES)
    return "::".join([*parts, *classes]) or "-"


def cast_of(keyword: tsast.Node) -> str:
    """Return `reinterpret_cast<TYPE>` of the cast that holds a keyword node, or the keyword alone."""
    owner = keyword.parent
    arguments = owner.child_by_field("arguments") if owner is not None and owner.type == "template_function" else None
    return f"{KEYWORD}{tsast.spelled(arguments)}" if arguments is not None else KEYWORD


def keyword_nodes(root: tsast.Node) -> Iterator[tsast.Node]:
    """Yield each identifier token spelled `reinterpret_cast` in a root, in the sequence of the source."""
    yield from root.descendants_named(frozenset({KEYWORD}), "identifier")


def finding(path: str, line: int, entity: str) -> check_report.Finding:
    """Return the error of one cast (THE ENTITY)."""
    return check_report.Finding("error", path, line, RULE, MESSAGE.format(entity=entity), KIND, entity)


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each `reinterpret_cast` of the scope (THE RULE)."""
    found: list[check_report.Finding] = []
    trees = list(
        context.trees([path for path in context.scope_files() if not has_directory(path, EXCLUDED_DIRECTORIES)])
    )
    for tree in trees:
        path = tree.path.as_posix()
        for node in keyword_nodes(tree.root):
            holder = tsast.function_signature(node) or scope_holder(node)
            found.append(finding(path, node.line, f"{holder}: {cast_of(node)}"))
    for body in context.macro_bodies(trees):
        path = PurePosixPath(body.define.tree.path).as_posix()
        holder = f"#define {body.name}"
        if body.is_parsed:
            found += [
                finding(path, body.origin(node)[0] + 1, f"{holder}: {cast_of(node)}")
                for node in keyword_nodes(body.root)
            ]
        else:
            found += [
                finding(path, token.row + 1, f"{holder}: {KEYWORD}")
                for token in tsast.pp_tokens(body.text, body.first_row)
                if token.kind == "identifier" and tsast.lexeme(token) == KEYWORD
            ]
    return found
