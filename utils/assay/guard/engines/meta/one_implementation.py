"""one-implementation — the package has one body for each job.

THE RULE
    The rule reads each function and each method of the modules of the
    package and the masters (assay.guard.engines.meta.package).  The body of a
    function is its statements with no docstring.  A body of fewer than 20
    nodes does not count.  The canonical tokens of a body are its node types
    in the sequence of the tree.  In them, the number of its first use
    replaces each local name, the type of a constant replaces the constant,
    and no annotation stays.  The rule refuses:
      copy       two bodies or more with the same canonical tokens
      near-copy  two bodies with sets of node 5-grams that have a Jaccard
                 index of 0.8 or more, when they are not copies of each other.
                 A node 5-gram is five canonical tokens that come one after the
                 other.  The tokens keep the names of attributes and of global
                 names, and two bodies with the same node types and different
                 attributes do not agree
    A function and a function that it holds are not a pair.  The entity of a
    finding is the full name of the function.  The message names the other
    functions of its group.

THE SEARCH
    A similarity join with a prefix filter finds the near pairs.  Each set
    sorts its 5-grams from the rarest to the most frequent.  Two sets with an
    index of 0.8 or more then share one 5-gram of their two prefixes.  The
    prefix of a set of n items has n - ceil(0.8 * n) + 1 items.

Complexity: linear in the length of the files for the canonical tokens, and O(c * s) for the c candidate pairs of sets
of s items.
"""

import ast
import hashlib
import math
from collections import Counter, defaultdict
from dataclasses import dataclass

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "one-implementation"
MINIMUM_NODES = 20
GRAM = 5
NEAR_INDEX = 0.8
# The most functions that a message names.
SHOWN = 3


@dataclass(frozen=True, slots=True)
class Body:
    """One body: its file, its function, its line, the hash of its canonical tokens and its set of node 5-grams."""

    path: str
    function: str
    line: int
    digest: str
    grams: frozenset[str]

    @property
    def location(self) -> str:
        """The function, its file and its line, for a message."""
        return f"{self.function} ({self.path}:{self.line})"


def statements(node: ast.FunctionDef | ast.AsyncFunctionDef) -> list[ast.stmt]:
    """Return the statements of a function with no docstring."""
    first = node.body[0] if node.body else None
    has_docstring = (
        isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant) and isinstance(first.value.value, str)
    )
    return node.body[1:] if has_docstring else node.body


def local_names(node: ast.FunctionDef | ast.AsyncFunctionDef) -> set[str]:
    """Return each parameter of a function and each name that its body makes with an assignment, a def or a class."""
    arguments = node.args
    names = {parameter.arg for parameter in [*arguments.posonlyargs, *arguments.args, *arguments.kwonlyargs]}
    names.update(item.arg for item in (arguments.vararg, arguments.kwarg) if item is not None)
    for statement in statements(node):
        for part in ast.walk(statement):
            if isinstance(part, ast.Name) and isinstance(part.ctx, ast.Store):
                names.add(part.id)
            elif isinstance(part, ast.arg):
                names.add(part.arg)
            elif isinstance(part, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                names.add(part.name)
    return names


def is_annotation(node: ast.AST, parent: ast.AST) -> bool:
    """Return True when a node is the annotation of a parameter, of a return value or of an assignment."""
    return (
        (isinstance(parent, ast.arg) and node is parent.annotation)
        or (isinstance(parent, (ast.FunctionDef, ast.AsyncFunctionDef)) and node is parent.returns)
        or (isinstance(parent, ast.AnnAssign) and node is parent.annotation)
    )


def canonical_tokens(node: ast.FunctionDef | ast.AsyncFunctionDef) -> list[str]:
    """Return the canonical tokens of the body of a function (THE RULE), in the sequence of the tree.

    Complexity: linear in the number of the nodes of the body.
    """
    locals_of = local_names(node)
    numbers: dict[str, int] = {}
    tokens: list[str] = []
    pending: list[tuple[ast.AST, ast.AST]] = [(statement, node) for statement in reversed(statements(node))]
    while pending:
        current, parent = pending.pop()
        if is_annotation(current, parent):
            continue
        token = type(current).__name__
        if isinstance(current, ast.Name):
            token += f":{numbers.setdefault(current.id, len(numbers))}" if current.id in locals_of else f":{current.id}"
        elif isinstance(current, ast.Attribute):
            token += f":{current.attr}"
        elif isinstance(current, ast.Constant):
            token += f":{type(current.value).__name__}"
        tokens.append(token)
        pending.extend((child, current) for child in reversed(list(ast.iter_child_nodes(current))))
    return tokens


def bodies(item: package_scope.PythonFile) -> list[Body]:
    """Return each body of one file with MINIMUM_NODES nodes or more."""
    found: list[Body] = []
    for name, node in python_reader.scope_nodes(item.parsed.tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        tokens = canonical_tokens(node)
        if len(tokens) < MINIMUM_NODES:
            continue
        grams = frozenset(" ".join(tokens[index : index + GRAM]) for index in range(len(tokens) - GRAM + 1))
        digest = hashlib.sha256("\n".join(tokens).encode("utf-8")).hexdigest()
        found.append(Body(item.path, name, node.lineno, digest, grams))
    return found


def is_nested(first: Body, second: Body) -> bool:
    """Return True when one body holds the other: the same file, and one full name starts with the other and a point."""
    return first.path == second.path and (
        first.function.startswith(f"{second.function}.") or second.function.startswith(f"{first.function}.")
    )


def near_pairs(items: list[Body]) -> set[tuple[int, int]]:
    """Return each pair of indexes of two bodies with a Jaccard index of NEAR_INDEX or more (THE SEARCH).

    Complexity: O(n * p) for the index of n prefixes of p items, and O(c * s) for c candidate pairs.
    """
    frequency = Counter(gram for item in items for gram in item.grams)
    ordered = [sorted(item.grams, key=lambda gram: (frequency[gram], gram)) for item in items]
    index: dict[str, list[int]] = defaultdict(list)
    pairs: set[tuple[int, int]] = set()
    for position in sorted(range(len(items)), key=lambda number: (len(items[number].grams), number)):
        grams = ordered[position]
        count = len(grams)
        prefix = grams[: count - math.ceil(NEAR_INDEX * count) + 1]
        candidates = {other for gram in prefix for other in index[gram]}
        for other in candidates:
            # Each earlier set is not larger.  The length filter then reads the smaller set.
            if len(items[other].grams) < NEAR_INDEX * count:
                continue
            shared = len(items[position].grams & items[other].grams)
            if shared >= NEAR_INDEX * len(items[position].grams | items[other].grams):
                pairs.add((min(position, other), max(position, other)))
        for gram in prefix:
            index[gram].append(position)
    return pairs


def group_findings(items: list[Body], groups: dict[int, set[int]], kind: str, said: str) -> list[check_report.Finding]:
    """Return one finding for each body of a group, which names the other bodies of the group."""
    found: list[check_report.Finding] = []
    for position, others in sorted(groups.items()):
        item = items[position]
        shown = ", ".join(items[other].location for other in sorted(others)[:SHOWN])
        more = f" and {len(others) - SHOWN} more" if len(others) > SHOWN else ""
        found.append(
            check_report.Finding(
                "error",
                item.path,
                item.line,
                RULE,
                f"the body of {item.function} {said} {shown}{more}.  Keep one body, and call it from each caller",
                kind,
                item.function,
            )
        )
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each body of the package that is a copy or a near copy of a different body."""
    scope = package_scope.read_package(context)
    found = package_scope.parse_findings(RULE, scope)
    items = [body for item in scope.files for body in bodies(item)]
    by_digest: dict[str, list[int]] = defaultdict(list)
    for position, item in enumerate(items):
        by_digest[item.digest].append(position)
    copies: dict[int, set[int]] = defaultdict(set)
    for members in by_digest.values():
        for position in members:
            copies[position].update(
                other for other in members if other != position and not is_nested(items[position], items[other])
            )
    near: dict[int, set[int]] = defaultdict(set)
    for first, second in near_pairs(items):
        if items[first].digest != items[second].digest and not is_nested(items[first], items[second]):
            near[first].add(second)
            near[second].add(first)
    found += group_findings(
        items, {key: value for key, value in copies.items() if value}, "copy", "has the canonical tokens of"
    )
    found += group_findings(items, dict(near), "near-copy", "has the node 5-grams of")
    return found
