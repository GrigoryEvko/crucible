"""fullness-guard — a fullness test compares with `>=`, and not with `==`.

THE DEFECT
    A container guards its push with a fullness test.  When the test is
    `count == Capacity`, a count that went past the bound reads as not full,
    and the next push writes outside the array:

        if (count == Capacity) { return false; }   // 300 == 64 is false
        entries[count] = incoming;                 // writes entries[300]

    `>=` costs the same instruction, and it is correct for each count that
    the type can hold.  A Release build has no _GLIBCXX_ASSERTIONS, the
    operator[] of fixy::FixedArray has no precondition, and some units
    compile their contracts out, so the comparison is the only check that
    gets to production.

THE RULE
    A candidate is an `==` between a counter and a bound:
      * A counter is a name, a chain of member accesses (`state.count`,
        `hdr->n`, `this->count`) or a subscript of one, through parentheses
        and a static_cast.  A call is not a counter, because a container
        that compares its own size() reads its own invariant.
      * A bound is a name whose last part is Capacity, capacity, max_WORD,
        MAX_WORD or MaxWord, with or without a qualifier, on each side.
      * A bound is also a call of size(), capacity() or max_size() with no
        arguments on a named object, on the right of the counter.  On the
        left, `v.size() == n` is a length check of the tree, and not a
        fullness test.
    The rule refuses:
      equal-bound  a candidate outside a constant context, on a counter
                   whose last name does not end with `_`
    The rule does not read `!=`: each `!= BOUND` of the tree is a sentinel of
    a failed search.  A comparison that runs only in a constant evaluation is
    not read: one in a static_assert, a template argument, a requires clause
    or a consteval function, where a write out of bounds is a compile error.
    The convention of the tree gives `_` at the end of the name of a member
    that a class invariant holds, and the rule skips such a counter.  This
    is a heuristic: a private counter that two methods write can still go
    past its bound.

THE ENTITY
    The entity of a finding is `FUNCTION: COUNTER == BOUND`, with the two
    operands in the order of the source (tsast.spelled()).  FUNCTION is the
    signature of the function that holds the test (tsast.function_signature),
    the qualified scope of a test outside a function, or `#define NAME` for
    a test in a macro body.  The key has no line, so a change that only
    moves code changes no row.

THE MACRO BODIES
    The rule reads the same shapes in the parse of each macro body
    (tsast.macro_bodies), and a static_assert there is a constant context.
    A body that the kit cannot parse, such as one that pastes tokens with
    ##, is read from its preprocessing tokens: a counter chain before `==`
    and a bound name or a bound call after it, or a bound name before `==`
    and a counter chain after it.  The token reader does not know a
    constant context.

THE SCOPE
    The scope of the registry, less each directory third_party, external
    and vendor.

Complexity: linear in the size of the files of the scope and of their macro bodies.
"""

from collections.abc import Iterator
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "fullness-guard"
KIND = "equal-bound"
EXCLUDED_DIRECTORIES = frozenset({"third_party", "external", "vendor"})
BOUND_WORDS = frozenset({"Capacity", "capacity"})
BOUND_PREFIXES = ("max_", "MAX_")
BOUND_CALLS = frozenset({"size", "capacity", "max_size"})
MEMBER_ACCESS = (".", "->")
MESSAGE = ("`{test}` in {holder} is an `==` fullness test against a capacity bound.  Write `>=`, so that a count past "
           "the bound still reads as full.  A sentinel of a failed search gets a ledger row with its reason")


def is_bound_word(word: str) -> bool:
    """Return whether a name has the form of a capacity: Capacity, capacity, max_WORD, MAX_WORD or MaxWord."""
    if word in BOUND_WORDS:
        return True
    if word.startswith(BOUND_PREFIXES):
        return len(word) > len(BOUND_PREFIXES[0])
    return word.startswith("Max") and len(word) > 3 and "A" <= word[3] <= "Z"


def final_segment(node: tsast.Node) -> tsast.Node | None:
    """Return the last name of an identifier, a qualified name or a member access, or None."""
    current: tsast.Node | None = node
    while current is not None:
        if current.type in ("identifier", "field_identifier", "type_identifier", "namespace_identifier"):
            return current
        if current.type == "qualified_identifier":
            current = current.child_by_field("name")
        elif current.type == "field_expression":
            current = current.child_by_field("field")
        else:
            return None
    return None


def unwrap(node: tsast.Node | None) -> tsast.Node | None:
    """Remove the parentheses and a static_cast around an operand."""
    while node is not None:
        if node.type == "parenthesized_expression":
            inner = tsast.non_comment_children(node)
            node = inner[0] if len(inner) == 1 else None
            continue
        callee = node.child_by_field("function") if node.type == "call_expression" else None
        named = callee.child_by_field("name") if callee is not None and callee.type == "template_function" else None
        if named is None or tsast.spelled(named) != "static_cast":
            return node
        arguments = node.child_by_field("arguments")
        inner = tsast.non_comment_children(arguments) if arguments is not None else []
        node = inner[0] if len(inner) == 1 else None
    return None


def is_counter(node: tsast.Node | None) -> bool:
    """Return whether an operand is a name, a chain of member accesses or a subscript of one, with no call in it."""
    node = unwrap(node)
    if node is None:
        return False
    if node.type == "subscript_expression":
        return is_counter(node.child_by_field("argument"))
    if node.type in ("identifier", "qualified_identifier"):
        return final_segment(node) is not None
    if node.type == "field_expression":
        base = node.child_by_field("argument")
        return base is not None and (base.type == "this" or is_counter(base)) and final_segment(node) is not None
    return False


def is_protected(node: tsast.Node) -> bool:
    """Return whether the last name of a counter ends with `_`, the mark of a member that an invariant holds."""
    current = unwrap(node)
    while current is not None and current.type == "subscript_expression":
        current = unwrap(current.child_by_field("argument"))
    last = final_segment(current) if current is not None else None
    return last is not None and tsast.lexeme(last).endswith("_")


def is_bound_name(node: tsast.Node | None) -> bool:
    """Return whether an operand is a name whose last part has the form of a capacity."""
    node = unwrap(node)
    if node is None or node.type not in ("identifier", "qualified_identifier", "field_expression"):
        return False
    last = final_segment(node)
    return last is not None and is_bound_word(tsast.lexeme(last))


def is_bound_call(node: tsast.Node | None) -> bool:
    """Return whether an operand is a call of size(), capacity() or max_size() on a named object, with no argument."""
    node = unwrap(node)
    if node is None or node.type != "call_expression":
        return False
    callee = node.child_by_field("function")
    arguments = node.child_by_field("arguments")
    if callee is None or callee.type != "field_expression" or arguments is None \
            or tsast.non_comment_children(arguments):
        return False
    last = final_segment(callee)
    return last is not None and tsast.spelled(last) in BOUND_CALLS and is_counter(callee.child_by_field("argument"))


def is_constant_context(node: tsast.Node) -> bool:
    """Return whether a comparison runs only in a constant evaluation.

    That is a static_assert, a template argument, a requires clause or a consteval function, where a write out of
    bounds is a compile error.
    """
    if node.ancestor_of_type("static_assert_declaration", "template_argument_list", "requires_clause",
                             "requires_expression") is not None:
        return True
    function = node.ancestor_of_type("function_definition")
    while function is not None:
        if any(child.type == "type_qualifier" and child.text == "consteval" for child in function.children):
            return True
        function = function.ancestor_of_type("function_definition")
    return False


def fullness_nodes(root: tsast.Node) -> Iterator[tsast.Node]:
    """Yield each `==` fullness test under the root of a file or of a macro body.

    Complexity: linear in the number of nodes under the root.
    """
    for node in root.descendants("binary_expression"):
        left, right = node.child_by_field("left"), node.child_by_field("right")
        if left is None or right is None or tsast.operator_of(node) != "==":
            continue
        is_forward = is_counter(left) and not is_protected(left) and (is_bound_name(right) or is_bound_call(right))
        is_reversed = is_counter(right) and not is_protected(right) and is_bound_name(left)
        if (is_forward or is_reversed) and not is_constant_context(node):
            yield node


def holder_of(node: tsast.Node) -> str:
    """Return the FUNCTION of the entity of a test in a file (THE ENTITY)."""
    signature = tsast.function_signature(node)
    if signature is not None:
        return signature
    namespaces = tuple(tsast.ANONYMOUS_NAMESPACE if part == "" else part for part in tsast.namespace_path(node))
    classes: list[str] = []
    holder = node.ancestor_of_type("class_specifier", "struct_specifier", "union_specifier")
    while holder is not None and holder.ancestor_of_type("namespace_definition") == node.ancestor_of_type(
            "namespace_definition"):
        name = holder.child_by_field("name")
        classes.insert(0, (tsast.leaf_name(name) or "") if name is not None else "")
        holder = holder.ancestor_of_type("class_specifier", "struct_specifier", "union_specifier")
    return "::".join(namespaces + tuple(classes)) or "-"


def counter_before(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the first token of a counter chain that ends before a token, or None.

    A chain is `name` or `(name)`, and then `.name` or `->name` each number of
    times.  A chain after a name, `::`, `.` or `->` is part of a larger
    expression and does not count.
    """
    cursor = index - 1
    if cursor < 0 or tokens[cursor].kind != "identifier":
        return None
    while cursor >= 2 and tokens[cursor - 1].text in MEMBER_ACCESS and tokens[cursor - 2].kind == "identifier":
        cursor -= 2
    if cursor >= 4 and tokens[cursor - 1].text in MEMBER_ACCESS and tokens[cursor - 2].text == ")" \
            and tokens[cursor - 3].kind == "identifier" and tokens[cursor - 4].text == "(":
        cursor -= 4
    before = tokens[cursor - 1] if cursor >= 1 else None
    if before is not None and (before.kind == "identifier" or before.text in ("::", *MEMBER_ACCESS)):
        return None
    return cursor


def counter_after(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the last token of a counter chain that starts at a token, or None.

    A chain before a call, `.`, `->` or a name is part of a larger expression and does not count.
    """
    cursor = index
    if cursor + 2 < len(tokens) and tokens[cursor].text == "(" and tokens[cursor + 1].kind == "identifier" \
            and tokens[cursor + 2].text == ")":
        cursor += 2
    elif cursor >= len(tokens) or tokens[cursor].kind != "identifier":
        return None
    while cursor + 2 < len(tokens) and tokens[cursor + 1].text in MEMBER_ACCESS \
            and tokens[cursor + 2].kind == "identifier":
        cursor += 2
    after = tokens[cursor + 1] if cursor + 1 < len(tokens) else None
    if after is not None and (after.kind == "identifier" or after.text in ("(", *MEMBER_ACCESS)):
        return None
    return cursor


def bound_after(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the last token of a bound name or a bound call that starts at a token, or None."""
    cursor = index
    while cursor + 2 < len(tokens) and tokens[cursor].kind == "identifier" and tokens[cursor + 1].text == "::":
        cursor += 2
    if cursor < len(tokens) and tokens[cursor].kind == "identifier" and is_bound_word(tsast.lexeme(tokens[cursor])) \
            and (cursor + 1 >= len(tokens) or tokens[cursor + 1].text != "("):
        return cursor
    cursor = index
    while cursor + 2 < len(tokens) and tokens[cursor].kind == "identifier" \
            and tokens[cursor + 1].text in MEMBER_ACCESS:
        cursor += 2
    if cursor >= index + 2 and cursor + 2 < len(tokens) and tokens[cursor].text in BOUND_CALLS \
            and tokens[cursor + 1].text == "(" and tokens[cursor + 2].text == ")":
        return cursor + 2
    return None


def bound_before(tokens: list[tsast.Token], index: int) -> int | None:
    """Return the index of the first token of a bound name that ends before a token, with no member access before it."""
    cursor = index - 1
    if cursor < 0 or tokens[cursor].kind != "identifier" or not is_bound_word(tsast.lexeme(tokens[cursor])):
        return None
    while cursor >= 2 and tokens[cursor - 1].text == "::" and tokens[cursor - 2].kind == "identifier":
        cursor -= 2
    return cursor if cursor == 0 or tokens[cursor - 1].text not in MEMBER_ACCESS else None


def token_sites(tokens: list[tsast.Token]) -> Iterator[tuple[int, str]]:
    """Yield the row and the test of each `==` fullness test in the tokens of a macro body that does not parse.

    The row is the row of the counter.  A counter whose last name ends with `_` is protected, as in a tree.
    """
    def joined(first: int, last: int) -> str:
        """Return the tokens from first to last, joined as tsast.spelled() joins them."""
        return tsast.spelled_tokens(tsast.lexeme(token) for token in tokens[first:last + 1])

    for index, token in enumerate(tokens):
        if token.text != "==":
            continue
        start = counter_before(tokens, index)
        end = bound_after(tokens, index + 1)
        if start is not None and end is not None and not tsast.lexeme(tokens[index - 1]).endswith("_"):
            yield tokens[start].row, f"{joined(start, index - 1)} == {joined(index + 1, end)}"
            continue
        last = counter_after(tokens, index + 1)
        first = bound_before(tokens, index)
        if last is not None and first is not None and not tsast.lexeme(tokens[last]).endswith("_"):
            yield tokens[index + 1].row, f"{joined(first, index - 1)} == {joined(index + 1, last)}"


def is_excluded(path: str) -> bool:
    """Return whether a file is in a directory of vendored code."""
    return any(part in EXCLUDED_DIRECTORIES for part in PurePosixPath(path).parts[:-1])


def test_of(node: tsast.Node) -> str:
    """Return `COUNTER == BOUND` of a fullness test, with the operands in the order of the source."""
    return " == ".join(tsast.spelled(operand) for operand in (node.child_by_field("left"), node.child_by_field("right"))
                       if operand is not None)


def finding(path: str, line: int, holder: str, test: str) -> check_report.Finding:
    """Return the error of one fullness test, with its entity (THE ENTITY)."""
    return check_report.Finding("error", path, line, RULE, MESSAGE.format(test=test, holder=holder), KIND,
                                f"{holder}: {test}")


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each `==` fullness test of the scope (THE RULE)."""
    found: list[check_report.Finding] = []
    trees = list(context.trees([path for path in context.scope_files() if not is_excluded(path)]))
    for tree in trees:
        path = tree.path.as_posix()
        found += [finding(path, node.line, holder_of(node), test_of(node)) for node in fullness_nodes(tree.root)]
    for body in context.macro_bodies(trees):
        path = PurePosixPath(body.define.tree.path).as_posix()
        holder = f"#define {body.name}"
        if body.is_parsed:
            found += [finding(path, body.origin(node)[0] + 1, holder, test_of(node))
                      for node in fullness_nodes(body.root)]
        else:
            found += [finding(path, row + 1, holder, test)
                      for row, test in token_sites(tsast.pp_tokens(body.text, body.first_row))]
    return found
