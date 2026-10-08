"""use-after-move — no use of a local after std::move, on a path through the function body.

THE RULE
    After `std::move(x)`, `std::forward<T>(x)`, `std::move_if_noexcept(x)`,
    `std::forward_like<T>(x)` or `static_cast<T&&>(x)`, the name x is spent.
    The qualifier of the function does not change this: `move(x)` after a
    using-declaration and `alias::move(x)` after a namespace alias are moves
    too.  After the move, a read of x, a second move of x and a read of a
    member of x are uses after move.  An assignment to x before them makes x
    live again.  A copy capture of x in a lambda is a use too.  The rule
    refuses:
      use-after-move  the first use of a spent name after each move.  The
                      next uses of the same move are the same defect, and
                      they give no more findings
      macro-move      a #define with a replacement list that moves or
                      forwards.  Each use of such a macro hides a move from
                      the walk
      no-reason       a row of the ledger with no reason
      known-misses    more known misses in the plants of the rule than
                      KNOWN_MISS_BOUND (WHAT THE RULE DOES NOT SEE)

WHY
    A linear token of this tree is an empty class.  A moved-from token and a
    live token then look the same.  `Permission<Tag>` stays one byte, and it
    takes no byte as an empty base.  No byte records the move, and GCC 16 has
    no analysis of a use after move.  A second move of one token gives two
    owners of one region.  That is the violation of the exclusive points-to of
    Actris 2.0 (Hinrichsen, Bengtson and Krebbers, LMCS 2022), and a linear
    type must refuse it.  The rule is the affine discipline of a linear type
    system, and a flow check applies it to the names of C++.  It reads each
    type, because a use after move of each type is a defect.  For a linear
    type, the defect gives no sign at run time.

THE WALK
    The rule walks each function body, each lambda body and each
    member-initializer list as a tree of control flow.  The tree has the
    blocks, if and else, switch and case, the loops, try and catch, return,
    throw, break, continue and goto.  The arms of a preprocessor conditional
    are two paths.  The state at each point is the set of keys that a move
    can have spent.  Where two paths meet, a key that one path spent stays
    spent.
    A move in one branch of an `if` does not spend the name in the other
    branch.  A move before a return spends nothing after the return.  A move
    in a loop spends the name for the next iteration, unless the loop
    declares the name again or gives it a new value.  The walk does each loop
    body two times.  A move on one iteration then meets a use on the next.  A
    label that a subsequent goto of its block names is the head of a loop.
    The walk then does the statements after the label two times too.

THE KEYS
    A key is a name, a member path (a.b, a->b) or the object that a pointer
    holds (*p).  A move of a spends a and each member path below it.  A use
    of a.c after a move of a.b is not a use after move.  Parentheses do not
    change a key, and this->m and (*this).m are the key m.  A subscript by an
    integer literal is part of the key, and a[0] and a[1] are two keys.  A
    subscript by a different expression is the key a[*], which agrees with
    each element of a.
    An assignment through a[i] makes no key live, because the element that
    it fills can be a different one.  a.at(0) is the key a[0], a range for
    reads its range as a[*], and std::get<I>(t) is the key t[I].  A
    reference `T& r = x;` gives the key x to r, and a move of r spends x.
    The name b of `auto& [b] = obj;` gets the key obj.
    An assignment to a key makes it live again, and `reset`, `clear`,
    `emplace` and `assign` on it do the same.  A declaration makes its name
    live in its scope.  A name in an operand that the program does not
    evaluate is not a use: sizeof, alignof, decltype, noexcept, typeid,
    `requires`, static_assert and a reflection.

THE FIVE SHAPES THAT KEEP A NAME
    A call with a name that starts with try_ keeps its argument when it does
    not succeed, as std::map::try_emplace does.  In
    `while (!ring.try_push(std::move(v)))`, the loop body then sees v live,
    and the code after the loop sees v spent.  An if, a while and a do-while
    with one such call or its negation as the condition divide the paths in
    this way.
    swap(a, b), std::swap(a, b) and a.swap(b) interchange the spent state of
    a and b, as they interchange the values.  The swap itself is not a use.
    In a move constructor C(C&& other), an initializer with a move of other
    as its argument can only be a base.  C cannot hold a member of type C.
    The base moves only its own part, and the next initializers can read
    other.member.  The body sees other as spent.
    In a move assignment C& operator=(C&& other), a statement that is only
    Base::operator=(std::move(other)) gives a value to a base, for the same
    reason.  The next statements can read other.member.  A subsequent use of
    all of other is a use after move.
    static_cast<T&&>(x) for a scalar T, such as int, keeps x, because a
    scalar has no moved-from state.

THE LAMBDAS
    A lambda body is a new body.  It starts with the spent keys of the point
    of the lambda, minus its parameters and its init-captures.  A body that
    names a spent capture is then a use after move.  A move in a lambda body
    spends no name of the body around it, because the lambda can run
    subsequently or not at all.  An init-capture runs at the point of the
    lambda, and `[y = std::move(x)]` spends x there.

WHAT THE RULE DOES NOT SEE
    The directories control-known-miss-* of utils/plants/use-after-move/
    hold the shapes below.  Each one is a use after move, and the rule
    accepts it.  When the rule learns to refuse a shape, its control fails,
    and the control becomes a plant.  The list of known misses only becomes
    shorter: the rule refuses more than KNOWN_MISS_BOUND (5) directories
    control-known-miss-*, one for each shape below.
      * A move in a function that takes T&.  The call does not say
        std::move, and the name stays live at the call site.  A call that
        gives std::move(x) to a parameter T&& spends x, although the function
        can keep x.  A row of the ledger tells why when it does.
      * A use through a pointer to a spent object.  A use through a reference
        to an expression that is not a plain path, such as a reference that a
        call gives.
      * A lambda that captures a name by reference, runs after the move of
        the name, and reads it.  The lambda body is before the move in the
        source, and the rule reads it with the name live.
      * A try_ function that moves its argument when it does not succeed.  An
        example is a function that takes a parameter by value of a type that
        is not trivially copyable.  The rule of the try_ calls does not see
        that.
    The rule does not know types.  It also refuses a use after move of a type
    with a moved-from state that the standard specifies, such as a
    std::vector.  Give the name a new value first.  The ledger is only for a
    test that reads a moved-from object on purpose.

THE ENTITY
    The entity of a use after move is `FUNCTION: KEY`.  FUNCTION is the name
    of the function that holds the walked body, with its namespaces, its
    classes and its parameter types (tsast.function_signature).  A lambda
    gives the function that holds it.  A lambda that no function holds gives
    its namespaces, its classes and the name of the variable that holds it.
    KEY is the key of the use as the rule writes it, such as `source`,
    `*producer`, `t.size` or `parts.shards[0]`.  The entity of a macro is
    `#define NAME: MOVE`.  The entity of a row with no reason is
    `PATH: ENTITY` of the row.

THE LEDGER
    utils/ledgers/use-after-move.txt has the policy count, and each of its
    rows gives the reason that names its probe.  A row is only for a test
    that reads or uses a moved-from object on purpose.  Correct a use after
    move in the code.  Correct a finding that the rule reads incorrectly in
    the rule, with a plant.

THE SCOPE
    The C++ files of include/, src/, test/, vessel/ and utils/tools/.  A
    negative-compile fixture is out of the scope, because each one must fail
    to compile.  Its directory is neg or NAME_neg.  A file that does not
    parse gives the error of the kind parse (the runner of the engine).

Complexity: linear in the length of each body, times two for each loop or each label of a subsequent goto that holds the
code.  A nest of d loops walks its inner body 2**d times.
"""

from collections.abc import Callable
from dataclasses import dataclass, replace
from pathlib import PurePosixPath

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.registry import Rule
from assay.guard.engines.meta.runner import NotApplicable
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "use-after-move"
USE_KIND = "use-after-move"
MACRO_KIND = "macro-move"
REASON_KIND = "no-reason"
SECOND_MOVE = "a second move"
USE = "a use"
COPY_CAPTURE = "a copy capture"
ADVICE = (
    "Use the value before the move, or give it a new value first.  A test that reads a moved-from object on purpose "
    "gets a ledger row with its reason (CLAUDE.md section XVI)"
)
MACRO_ADVICE = (
    "Each use of the macro hides a move from the walk of use-after-move.  Move the value where the code uses the macro"
)
REASON_ADVICE = "Write the probe that the row makes as its reason"
# The plants of the rule, the prefix of the controls of its known misses, and the maximum number of known misses
# (WHAT THE RULE DOES NOT SEE).
PLANTS = "utils/plants/use-after-move/"
KNOWN_MISS_PREFIX = "control-known-miss-"
KNOWN_MISS_BOUND = 5
BOUND_KIND = "known-misses"
# The directories of the negative-compile fixtures, which must fail to compile (THE SCOPE).
NEGATIVE_DIRECTORY = "neg"
NEGATIVE_SUFFIX = "_neg"
# The last name of a function that gives an rvalue of its argument.  The qualifier does not change it.
MOVE_CALLEES = frozenset({"move", "forward", "move_if_noexcept", "forward_like"})
REFILL_METHODS = frozenset({"reset", "clear", "emplace", "assign"})
# The words of a scalar type.  static_cast<int&&>(x) keeps x, because a scalar has no moved-from state.
SCALAR_TYPE_WORDS = frozenset(
    {
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "wchar_t",
        "short",
        "int",
        "long",
        "signed",
        "unsigned",
        "float",
        "double",
        "const",
        "volatile",
        "std",
        "::",
        "byte",
        "size_t",
        "ptrdiff_t",
        "intptr_t",
        "uintptr_t",
        "int8_t",
        "int16_t",
        "int32_t",
        "int64_t",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
    }
)
# The nodes with operands that the program does not evaluate, and the nodes that name a type or a scope and not an
# object.  A name below one of them is not a use.
NOT_EVALUATED = frozenset(
    {
        "sizeof_expression",
        "alignof_expression",
        "decltype",
        "noexcept_expression",
        "typeid_expression",
        "requires_expression",
        "requires_clause",
        "static_assert_declaration",
        "reflect_expression",
        "splice_expression",
        "splice_type_specifier",
        "type_descriptor",
        "template_argument_list",
        "qualified_identifier",
        "namespace_identifier",
        "type_identifier",
        "primitive_type",
        "sized_type_specifier",
        "field_identifier",
        "statement_identifier",
        "this",
        "comment",
        "number_literal",
        "string_literal",
        "raw_string_literal",
        "char_literal",
        "concatenated_string",
        "user_defined_literal",
        "true",
        "false",
        "nullptr",
        "preproc_arg",
        "preproc_def",
        "preproc_function_def",
        "preproc_call",
        "preproc_include",
    }
)
# The statements that declare no local and evaluate nothing that the walk records.
INERT_STATEMENTS = frozenset(
    {
        "type_definition",
        "alias_declaration",
        "using_declaration",
        "static_assert_declaration",
        "namespace_alias_definition",
        "friend_declaration",
        "template_declaration",
        "asm_statement",
        "comment",
        "preproc_def",
        "preproc_function_def",
        "preproc_call",
        "preproc_include",
        "empty_statement",
        "concept_definition",
        "class_specifier",
        "struct_specifier",
        "union_specifier",
        "enum_specifier",
    }
)
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")
PREPROC_CONDITIONALS = frozenset({"preproc_if", "preproc_ifdef", "preproc_elif", "preproc_elifdef"})
LOOPS = frozenset({"while_statement", "for_statement", "for_range_loop", "expansion_statement"})
# The declarator nodes between a declaration and the name that it declares.
DECLARATOR_WRAPPERS = frozenset(
    {
        "init_declarator",
        "reference_declarator",
        "pointer_declarator",
        "array_declarator",
        "attributed_declarator",
        "parenthesized_declarator",
    }
)
# The nodes of a plain chain of names and member accesses, such as ring.try_push or this->q.try_push.
PLAIN_CALLEE = frozenset(
    {"identifier", "field_identifier", "namespace_identifier", "field_expression", "qualified_identifier", "this"}
)
NAME_LEAVES = frozenset({"identifier", "field_identifier", "namespace_identifier"})
TRY_PREFIX = "try_"
MEMBER = "."
ARROW = "->"
INDEX = "[]"
# The label of a subscript that is not an integer literal.  It agrees with each index.
ANY_INDEX = "*"
POINTEE_MARK = "*"
# The mark of the key that Base::operator=(std::move(p)) spends in a move assignment.  A use of all of p names the
# key p#base, and a member of p does not.  The base assignment takes only the base part.
BASE_MARK = "#base"
ANONYMOUS = "<anonymous>"


@dataclass(frozen=True, slots=True)
class Step:
    """One step of a key after its name: a member through `.` or `->` (MEMBER, ARROW), or a subscript (INDEX)."""

    access: str
    label: str

    def shown(self) -> str:
        """Return the step as the source writes it: `.m`, `->m`, `[0]` or `[*]`."""
        return f"[{self.label}]" if self.access == INDEX else f"{self.access}{self.label}"


@dataclass(frozen=True, slots=True)
class Place:
    """One key of the walk (THE KEYS): a name, the steps after it, and two marks.

    is_pointee is the key *path, the object that the path holds.  is_base is
    the key p#base of a base assignment (BASE_MARK).
    """

    name: str
    is_pointee: bool = False
    steps: tuple[Step, ...] = ()
    is_base: bool = False

    def shown(self) -> str:
        """Return the key as the rule writes it: `t`, `*o`, `p#base` or `parts.shards[0]`."""
        steps = "".join(step.shown() for step in self.steps)
        return f"{POINTEE_MARK if self.is_pointee else ''}{self.name}{steps}{BASE_MARK if self.is_base else ''}"

    def then(self, step: Step) -> Place:
        """Return the key with one more step."""
        return replace(self, steps=(*self.steps, step))

    def pointee(self) -> Place:
        """Return the key of the object that the path holds."""
        return replace(self, is_pointee=True)


# The keys that a move can have spent, each with the line of its move.  None is a point of no path.
type State = dict[Place, int] | None
# The names that a scope declares, each with the line of the move that spent the name out of the scope, or None.
type Scope = dict[str, int | None]
# One segment of a key, to compare two keys: its access, its label and the base mark.
type Segment = tuple[str, str, bool]


class Frame:
    """The states at the jumps out of one loop or one switch: at each break, and at each continue of a loop."""

    __slots__ = ("at_break", "at_continue", "is_loop")

    def __init__(self, is_loop: bool) -> None:
        """Start a frame with no jump."""
        self.is_loop = is_loop
        self.at_break: State = None
        self.at_continue: State = None


def join(first: State, second: State) -> State:
    """Return the state where two paths meet: a key that one path spent is spent."""
    if first is None:
        return None if second is None else dict(second)
    if second is None:
        return dict(first)
    out = dict(first)
    for key, line in second.items():
        out[key] = min(line, out.get(key, line))
    return out


def segments(place: Place) -> list[Segment]:
    """Return the segments of a key: its name with the pointee mark, then each step.  The last one has the base mark."""
    last = len(place.steps)
    found: list[Segment] = [(POINTEE_MARK if place.is_pointee else "", place.name, place.is_base and last == 0)]
    found += [(step.access, step.label, place.is_base and index == last - 1) for index, step in enumerate(place.steps)]
    return found


def reaches(spent: Place, key: Place) -> bool:
    """Return True when a key names a spent key or a part of it.

    Two subscripts agree when they are equal, or when one of them is
    ANY_INDEX.  An index that is not a literal can name each element.
    """
    spent_segments, key_segments = segments(spent), segments(key)
    if len(key_segments) < len(spent_segments):
        return False
    for mine, theirs in zip(spent_segments, key_segments, strict=False):
        is_any = mine[1] == ANY_INDEX or theirs[1] == ANY_INDEX
        is_wildcard = mine[0] == theirs[0] == INDEX and is_any and mine[2] == theirs[2]
        if mine != theirs and not is_wildcard:
            return False
    return True


def extends(spent: Place, key: Place) -> bool:
    """Return True when a spent key is the key, the key with the base mark, or a member or an element below the key."""
    if key.is_base:
        return spent == key
    return spent.name == key.name and spent.is_pointee == key.is_pointee and spent.steps[: len(key.steps)] == key.steps


def spent_prefix(state: dict[Place, int], key: Place) -> Place | None:
    """Return the spent key that a key names or goes into, or None.

    A move through a pointer, std::move(*p), spends the key *p.  Then *p, a
    member of *p and p->m go into it, and p does not.  A use of all of p
    names the key p#base of a base assignment.
    """
    for spent in state:
        if reaches(spent, key):
            return spent
        held = len(spent.steps)
        if (
            spent.is_pointee
            and not key.is_pointee
            and not spent.is_base
            and key.name == spent.name
            and len(key.steps) > held
            and key.steps[:held] == spent.steps
            and key.steps[held].access == ARROW
        ):
            return spent
        if spent.is_base and not key.is_base and spent == replace(key, is_base=True):
            return spent
    return None


def restore(state: dict[Place, int], key: Place) -> None:
    """Remove the spent keys that an assignment to a key makes live again.

    An assignment to p also makes *p live again, because p then holds a new
    value or points at one.
    """

    def is_live_again(spent: Place) -> bool:
        view = replace(spent, is_pointee=False) if spent.is_pointee and not key.is_pointee else spent
        return extends(view, key)

    for spent in [item for item in state if is_live_again(item)]:
        del state[spent]


def exchange(state: dict[Place, int], left: Place, right: Place) -> None:
    """Interchange the spent keys below left and the spent keys below right, as swap(left, right) does with the values."""
    moved: dict[Place, int] = {}
    for spent in list(state):
        for source, target in ((left, right), (right, left)):
            if extends(spent, source):
                steps = target.steps + spent.steps[len(source.steps) :]
                moved[Place(target.name, target.is_pointee, steps, spent.is_base)] = state.pop(spent)
                break
    state.update(moved)


def named(node: tsast.Node | None) -> list[tsast.Node]:
    """Return the named children of a node with no comment, or no child for no node."""
    return [] if node is None else tsast.non_comment_children(node)


def operator(node: tsast.Node) -> str:
    """Return the first token of a node that is in none of its named children.

    It is `.` or `->` of a member access, `*` or `&` of a unary operator, and
    `&` or `&&` of a reference declarator.
    """
    tokens = node.gap_tokens()
    return tokens[0] if tokens else ""


def leaf_name(node: tsast.Node | None) -> str | None:
    """Return the last name of a function name or a declarator: f of f, ns::f, obj.f, f<T> and ns::f<T>."""
    return None if node is None else tsast.leaf_name(node)


def name_of(node: tsast.Node) -> str:
    """Return the name of a name node, such as an identifier, with each line splice removed.

    tsast.leaf_name() reads the name, and tsast.lexeme() reads a node that it
    does not know.  The walk reads a name at each identifier, and leaf_name()
    does not lex the node.  On 2026-10-08, the change from lexeme() to
    leaf_name() decreased the time of the rule on the tree from 3.6 s to
    2.9 s.
    """
    name = tsast.leaf_name(node)
    return tsast.lexeme(node) if name is None else name


def is_plain_callee(node: tsast.Node) -> bool:
    """Return True when a function name is a chain of names and member accesses, with no call, subscript or template."""
    return node.type in PLAIN_CALLEE and all(is_plain_callee(child) for child in named(node))


def plain_leaf(node: tsast.Node) -> tsast.Node | None:
    """Return the last name node of a plain chain: f of f, obj.f, p->f and ns::f, or None for `this`."""
    if node.type in NAME_LEAVES:
        return node
    if node.type == "field_expression":
        field = node.child_by_field("field")
        return None if field is None else plain_leaf(field)
    if node.type == "qualified_identifier":
        name = node.child_by_field("name")
        return None if name is None else plain_leaf(name)
    return None


def template_index(template: tsast.Node) -> str | None:
    """Return the tokens of the template arguments of get<I>, with no space, or None when it has no arguments."""
    arguments = template.child_by_field("arguments")
    return None if arguments is None else "".join(arguments.tokens()[1:-1])


def get_template(call: tsast.Node) -> tsast.Node | None:
    """Return the template name of a call of get<I>( ... ), with or without a qualifier, or None for a different call."""
    function = call.child_by_field("function")
    if function is None or function.type == "field_expression":
        return None
    template = function.child_by_field("name") if function.type == "qualified_identifier" else function
    if template is None or template.type != "template_function" or leaf_name(template) != "get":
        return None
    return template


def function_declarator(function: tsast.Node) -> tsast.Node | None:
    """Return the function declarator of a definition, below the reference or pointer declarator of its return type."""
    node = function.child_by_field("declarator")
    while node is not None and node.type != "function_declarator":
        node = node.child_by_field("declarator")
    return node


def function_name(function: tsast.Node) -> str:
    """Return the name of a function definition: its last name, ~X for a destructor, operatorX for an operator."""
    return leaf_name(function.child_by_field("declarator")) or ANONYMOUS


def owner_class(function: tsast.Node) -> str | None:
    """Return the class of a member function: the scope of its name with qualifiers, or the class around its body."""
    declarator = function_declarator(function)
    name = None if declarator is None else declarator.child_by_field("declarator")
    if name is not None and name.type == "qualified_identifier":
        parts = tsast.qualified_parts(name)
        return parts[1][-2] if parts is not None and len(parts[1]) >= 2 else None
    enclosing = function.ancestor_of_type(*CLASS_SPECIFIERS)
    return None if enclosing is None else leaf_name(enclosing.child_by_field("name"))


def move_source(function: tsast.Node, owner: str | None) -> str | None:
    """Return the parameter p when the only parameter of a function is owner&& p, as in C(C&& p) and operator=(C&& p)."""
    declarator = function_declarator(function)
    items = named(None if declarator is None else declarator.child_by_field("parameters"))
    if owner is None or len(items) != 1 or items[0].type != "parameter_declaration":
        return None
    reference = items[0].child_by_field("declarator")
    if (
        leaf_name(items[0].child_by_field("type")) != owner
        or reference is None
        or reference.type != "reference_declarator"
        or operator(reference) != "&&"
    ):
        return None
    inner = reference.child_by_field("declarator") or next(iter(named(reference)), None)
    return name_of(inner) if inner is not None and inner.type == "identifier" else None


def site_scope(node: tsast.Node, name: str) -> str:
    """Return the namespaces and the classes that hold a node, then a name, joined with `::` (THE ENTITY)."""
    scopes = [tsast.ANONYMOUS_NAMESPACE if part == "" else part for part in tsast.namespace_path(node)]
    classes: list[str] = []
    holder = node.ancestor_of_type(*CLASS_SPECIFIERS)
    while holder is not None:
        classes.insert(0, leaf_name(holder.child_by_field("name")) or ANONYMOUS)
        holder = holder.ancestor_of_type(*CLASS_SPECIFIERS)
    return "::".join([*scopes, *classes, name])


def is_negative_fixture(path: str) -> bool:
    """Return True when a file is in a directory of negative-compile fixtures, neg or NAME_neg (THE SCOPE)."""
    return any(part == NEGATIVE_DIRECTORY or part.endswith(NEGATIVE_SUFFIX) for part in PurePosixPath(path).parts[:-1])


class Walk:
    """The walk of one file: each function body, each lambda body and each member-initializer list in it (THE WALK)."""

    __slots__ = (
        "aliases",
        "assigned_from",
        "found",
        "function",
        "goto_states",
        "hold_moves",
        "holder",
        "path",
        "seen",
        "tree",
    )

    def __init__(self, tree: tsast.Tree) -> None:
        """Start the walk of one parsed file."""
        self.tree = tree
        self.path = tree.path.as_posix()
        self.found: list[check_report.Finding] = []
        # The holder, the spent key and the line of each move that gave a finding.
        self.seen: set[tuple[str, Place, int]] = set()
        # The last name of the function that the walk is in, and its entity (THE ENTITY).
        self.function = ANONYMOUS
        self.holder = ANONYMOUS
        # True while the walk reads a condition on the path where its try_ call did not succeed.  A move in the call
        # then keeps the name.
        self.hold_moves = False
        # The state at the end of each goto of the body, at the end of each path, by label.
        self.goto_states: dict[str, State] = {}
        # A reference, or a name of `auto& [a, b] = obj;`, in scope: name -> (the key of the object that it names,
        # True for a name of such a declaration).
        self.aliases: dict[str, tuple[Place, bool]] = {}
        # The parameter p while the walk is in the body of a move assignment operator=(C&& p), and None elsewhere.
        self.assigned_from: str | None = None

    # ── The findings ─────────────────────────────────────────────────

    def report(self, node: tsast.Node, key: Place, what: str, spent: Place, moved_at: int) -> None:
        """Record the first use after one move.  A subsequent use of the same move is the same defect."""
        mark = (self.holder, spent, moved_at)
        if mark in self.seen:
            return
        self.seen.add(mark)
        self.found.append(
            check_report.Finding(
                "error",
                self.path,
                node.line,
                RULE,
                f"{what} of `{key.shown()}` after the move at line {moved_at}.  {ADVICE}",
                USE_KIND,
                f"{self.holder}: {key.shown()}",
            )
        )

    def spend(self, state: dict[Place, int], node: tsast.Node, path: Place) -> None:
        """Spend a key at the move at a node, and refuse the move of a key that an earlier move spent."""
        spent = spent_prefix(state, path)
        if spent is not None:
            self.report(node, path, SECOND_MOVE, spent, state[spent])
        if not self.hold_moves:
            state[path] = node.line

    def canon(self, path: Place) -> Place:
        """Return the key of a path.  A reference, or a name of `auto& [b] = obj;`, gives the key of its object."""
        alias = None if path.is_base and not path.steps else self.aliases.get(path.name)
        if alias is None:
            return path
        target = alias[0]
        return Place(target.name, path.is_pointee, target.steps + path.steps, path.is_base)

    def is_binding(self, path: Place) -> bool:
        """Return True when a path starts with a name of `auto& [b] = obj;`."""
        alias = None if path.is_base and not path.steps else self.aliases.get(path.name)
        return alias is not None and alias[1]

    def use(self, node: tsast.Node, state: dict[Place, int], raw: Place) -> None:
        """Refuse a read of a path at a node when a move spent it or a part of it."""
        is_binding = self.is_binding(raw)
        key = self.canon(raw)
        if is_binding:
            # A name of `auto& [b] = obj;` names a member of obj, and the walk does not know which one.  Each spent
            # key below the object then counts.
            under = next((spent for spent in state if reaches(key, spent)), None)
            if under is not None:
                self.report(node, key, USE, under, state[under])
                return
        spent = spent_prefix(state, key)
        if spent is not None:
            self.report(node, key, USE, spent, state[spent])

    # ── The paths ────────────────────────────────────────────────────

    def plain_path(self, node: tsast.Node) -> Place | None:
        """Return the key of a node that names a member path with literal indices, or None for a different node.

        this->m and (*this).m are the member m.  Parentheses do not change a
        path.  A subscript by an integer literal is part of the path.
        """
        kind = node.type
        if kind == "identifier":
            return Place(name_of(node))
        if kind == "parenthesized_expression":
            inner = named(node)
            return self.plain_path(inner[0]) if len(inner) == 1 else None
        if kind == "field_expression":
            argument = node.child_by_field("argument")
            field = node.child_by_field("field")
            if argument is None or field is None or field.type != "field_identifier":
                return None
            access = operator(node)
            if (argument.type == "this" and access == ARROW) or (
                access == MEMBER and self.is_dereferenced_this(argument)
            ):
                return Place(name_of(field))
            base = self.plain_path(argument)
            return None if base is None else base.then(Step(access, name_of(field)))
        if kind == "subscript_expression":
            argument = node.child_by_field("argument")
            index = self.literal_index(node)
            base = None if argument is None else self.plain_path(argument)
            return None if base is None or index is None else base.then(Step(INDEX, index))
        return None

    def is_dereferenced_this(self, node: tsast.Node) -> bool:
        """Return True when a node is (*this)."""
        inner = named(node)
        if node.type != "parenthesized_expression" or len(inner) != 1:
            return False
        pointer = inner[0]
        target = pointer.child_by_field("argument")
        return (
            pointer.type == "pointer_expression"
            and operator(pointer) == POINTEE_MARK
            and target is not None
            and target.type == "this"
        )

    def literal_index(self, subscript: tsast.Node) -> str | None:
        """Return the index of a subscript when it is one integer literal, or None."""
        items = named(subscript.child_by_field("indices"))
        return tsast.lexeme(items[0]) if len(items) == 1 and items[0].type == "number_literal" else None

    def index_step(self, arguments: tsast.Node | None) -> Step:
        """Return the step [N] of a call with one argument, the integer literal N, and [*] for a different call."""
        items = named(arguments)
        is_literal = len(items) == 1 and items[0].type == "number_literal"
        return Step(INDEX, tsast.lexeme(items[0]) if is_literal else ANY_INDEX)

    def deref_operand(self, node: tsast.Node) -> tsast.Node | None:
        """Return the operand of a unary * at a node, or None when the node is not a dereference."""
        if node.type == "pointer_expression" and operator(node) == POINTEE_MARK:
            return node.child_by_field("argument")
        return None

    def get_element(self, call: tsast.Node) -> Place | None:
        """Return the key t[I] of get<I>(t) with t a plain path, or None for a different call."""
        template = get_template(call)
        items = named(call.child_by_field("arguments"))
        if template is None or len(items) != 1:
            return None
        path = self.plain_path(items[0])
        index = template_index(template)
        return None if path is None or index is None else self.canon(path).then(Step(INDEX, index))

    def move_argument(self, arguments: tsast.Node | None) -> Place | None:
        """Return the key in std::move( ... ) when its one argument is a plain path or *path.

        The move std::move(*p) spends the object that p holds, and its key is
        *p.  A subscript by an expression that is not an integer literal gives
        the key of all elements of the array.
        """
        items = named(arguments)
        if len(items) != 1:
            return None
        node = items[0]
        while node.type == "parenthesized_expression" and len(named(node)) == 1:
            node = named(node)[0]
        operand = self.deref_operand(node)
        if operand is not None:
            path = self.plain_path(operand)
            return None if path is None else self.canon(path).pointee()
        if node.type == "call_expression":
            element = self.get_element(node)
            if element is not None:
                return element
            function = node.child_by_field("function")
            if function is not None and function.type == "field_expression" and leaf_name(function) == "at":
                receiver = function.child_by_field("argument")
                base = None if receiver is None else self.plain_path(receiver)
                if base is not None:
                    return self.canon(base).then(self.index_step(node.child_by_field("arguments")))
            return None
        if node.type == "subscript_expression" and self.literal_index(node) is None:
            argument = node.child_by_field("argument")
            base = None if argument is None else self.plain_path(argument)
            return None if base is None else self.canon(base).then(Step(INDEX, ANY_INDEX))
        path = self.plain_path(node)
        return None if path is None else self.canon(path)

    def move_callee(self, call: tsast.Node) -> bool:
        """Return True when a call is a move: the last name of its function is in MOVE_CALLEES, with no member call."""
        function = call.child_by_field("function")
        return function is not None and function.type != "field_expression" and leaf_name(function) in MOVE_CALLEES

    def element_of_get(self, call: tsast.Node) -> str | None:
        """Return the index I when a move is the first argument of get<I>( ... ), which moves the element I."""
        arguments = call.parent
        if arguments is None or arguments.type != "argument_list" or named(arguments)[0].index != call.index:
            return None
        outer = arguments.parent
        template = None if outer is None else get_template(outer)
        return None if template is None else template_index(template)

    def rvalue_cast(self, call: tsast.Node) -> bool:
        """Return True when a call is static_cast<T&&>(x) for a T that is not a scalar."""
        function = call.child_by_field("function")
        if function is None or function.type != "template_function" or leaf_name(function) != "static_cast":
            return False
        arguments = function.child_by_field("arguments")
        words = [] if arguments is None else arguments.tokens()
        return len(words) >= 3 and words[-2] == "&&" and not all(word in SCALAR_TYPE_WORDS for word in words[1:-2])

    # ── The file ─────────────────────────────────────────────────────

    def run(self) -> list[check_report.Finding]:
        """Walk each body of the file, and return the findings."""
        self.macro_moves()
        for function in self.tree.find("function_definition"):
            if function.ancestor_of_type("requires_expression", "concept_definition") is not None:
                continue
            body = function.child_by_field("body")
            if body is None:
                continue
            self.function = function_name(function)
            self.holder = tsast.function_signature(body) or site_scope(function, self.function)
            if self.function == "operator=":
                self.assigned_from = move_source(function, owner_class(function))
            state: State = {}
            initializers = function.children_of_type("field_initializer_list")
            if initializers:
                state = self.member_inits(function, initializers[0], state)
            self.walk_body(body, state)
            self.assigned_from = None
        for lam in self.tree.find("lambda_expression"):
            if lam.ancestor_of_type("function_definition", "lambda_expression", "requires_expression") is not None:
                continue
            owner = lam.ancestor_of_type("init_declarator")
            self.function = (None if owner is None else leaf_name(owner.child_by_field("declarator"))) or ANONYMOUS
            self.holder = site_scope(lam, self.function)
            self.lambda_expr(lam, {})
        return self.found

    def macro_moves(self) -> None:
        """Refuse each #define with a replacement list that moves or forwards.

        Each use of such a macro hides a move from the walk.  A macro must not
        move.  Complexity: linear in the length of the replacement lists.
        """
        for define in self.tree.find("preproc_def", "preproc_function_def"):
            values = [child for child in define.children if child.field == "value"]
            words = [token.text for value in values for token in tsast.pp_tokens(value.text, value.start[0])]
            for index, word in enumerate(words):
                after = words[index + 1] if index + 1 < len(words) else ""
                before = words[index - 1] if index > 0 else ""
                if word in MOVE_CALLEES and after in ("(", "<") and before not in (MEMBER, ARROW):
                    name = define.child_by_field("name")
                    macro = "<macro>" if name is None else tsast.lexeme(name)
                    self.found.append(
                        check_report.Finding(
                            "error",
                            self.path,
                            define.line,
                            RULE,
                            f"the macro {macro} calls {word}.  {MACRO_ADVICE}",
                            MACRO_KIND,
                            f"#define {macro}: {word}",
                        )
                    )
                    break

    def member_inits(self, function: tsast.Node, initializers: tsast.Node, state: State) -> State:
        """Walk the member-initializer list of a constructor.

        In a move constructor C(C&& other), an initializer with a move of other
        as its argument can only be a base.  A member of type C cannot be in
        C.  The base moves its own part.  The next initializers can then read
        other.member.  The body sees other as spent, as after each move.
        """
        source = move_source(function, function_name(function))
        base_moved_at: int | None = None
        for initializer in initializers.children_of_type("field_initializer"):
            arguments = [child for child in named(initializer) if child.type in ("argument_list", "initializer_list")]
            if not arguments:
                continue
            if source is not None and self.moves_whole(arguments[0], source):
                self.eval(arguments[0], state)
                base_moved_at = base_moved_at or initializer.line
            else:
                state = self.eval(arguments[0], state)
        if source is not None and base_moved_at is not None and state is not None:
            state = {**state, Place(source): base_moved_at}
        return state

    def base_assignment(self, node: tsast.Node) -> bool:
        """Return True when an expression is Base::operator=(std::move(p)) in the body of a move assignment.

        The qualifier names a base, because an operator= of the same class
        with no qualifier gives a value to all of the object.
        this->Base::operator=( ... ) is the same call.
        """
        if self.assigned_from is None or node.type != "call_expression":
            return False
        function = node.child_by_field("function")
        if function is not None and function.type == "field_expression":
            receiver = function.child_by_field("argument")
            function = function.child_by_field("field") if receiver is not None and receiver.type == "this" else None
        if function is None or function.type != "qualified_identifier" or leaf_name(function) != "operator=":
            return False
        arguments = node.child_by_field("arguments")
        return arguments is not None and self.moves_whole(arguments, self.assigned_from)

    def assign_base(self, call: tsast.Node, state: State) -> State:
        """Walk Base::operator=(std::move(p)), and spend the key p#base.

        The call takes only the base part of p.  The next statements can then
        read p.member, and a subsequent use of all of p is a use after move.  A
        second base assignment is not a use.  The walk reads its argument with
        no key p#base, as it reads a second base initializer of a move
        constructor.
        """
        if state is None or self.assigned_from is None:
            return None
        marker = Place(self.assigned_from, is_base=True)
        self.eval(call.child_by_field("arguments"), {key: line for key, line in state.items() if key != marker})
        return {**state, marker: state.get(marker, call.line)}

    def moves_whole(self, arguments: tsast.Node, source: str) -> bool:
        """Return True when the argument of an initializer is only a move of the plain name source."""
        items = named(arguments)
        if len(items) != 1 or items[0].type != "call_expression":
            return False
        call = items[0]
        if not (self.move_callee(call) or self.rvalue_cast(call)):
            return False
        inner = named(call.child_by_field("arguments"))
        return len(inner) == 1 and inner[0].type == "identifier" and name_of(inner[0]) == source

    # ── The statements ───────────────────────────────────────────────

    def walk_body(self, body: tsast.Node, state: State) -> State:
        """Walk one function body or lambda body.  Its labels and its gotos are its own."""
        frames: list[Frame] = []
        outer, self.goto_states = self.goto_states, {}
        outer_aliases = dict(self.aliases)
        try:
            if body.type == "compound_statement":
                return self.block(body, state, frames)
            return self.sub_stmt(body, state, frames)
        finally:
            self.goto_states, self.aliases = outer, outer_aliases

    def block(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk a block.  A label that a subsequent goto of the block names is the head of a loop.

        As for a loop, the walk does the statements from the first such label
        two times.  The second time enters the label with the join of the
        state of the statement before it and the state at each goto to it.
        """
        scope: Scope = {}
        statements = named(node)
        head: tuple[int, State, Scope] | None = None
        for index, statement in enumerate(statements):
            if head is None and statement.type == "labeled_statement" and self.goto_after(statement, node):
                head = (index, state, dict(scope))
            state = self.stmt(statement, state, frames, scope)
        if head is not None:
            first, state, scope = head
            for statement in statements[first:]:
                state = self.stmt(statement, state, frames, scope)
        for name in scope:
            self.aliases.pop(name, None)
        return self.close_scope(state, scope)

    def goto_after(self, labeled: tsast.Node, block: tsast.Node) -> bool:
        """Return True when a goto to the label of a labeled statement is in the block after it.

        Complexity: linear in the length of the block.
        """
        label = labeled.child_by_field("label")
        if label is None:
            return False
        name = name_of(label)
        return any(
            jump.start > labeled.start
            and (target := jump.child_by_field("label")) is not None
            and name_of(target) == name
            for jump in block.descendants("goto_statement")
        )

    def close_scope(self, state: State, scope: Scope) -> State:
        """Go out of a scope: its names are not spent, and a name that it hid gets its outer state again."""
        if state is None:
            return None
        out = dict(state)
        for name, outer in scope.items():
            restore(out, Place(name))
            if outer is not None:
                out[Place(name)] = outer
        return out

    def declare(self, state: State, scope: Scope, names: list[str]) -> State:
        """Declare names in a scope: each one starts live, and its outer state waits for the end of the scope."""
        if state is None:
            return None
        out = dict(state)
        for name in names:
            if name not in scope:
                scope[name] = out.get(Place(name))
            restore(out, Place(name))
        return out

    def stmt(self, node: tsast.Node, state: State, frames: list[Frame], scope: Scope) -> State:
        """Walk one statement from a state, and return the state after it."""
        kind = node.type
        if kind in INERT_STATEMENTS:
            return state
        if kind == "compound_statement":
            return self.block(node, state, frames)
        if kind == "if_statement":
            return self.if_stmt(node, state, frames)
        if kind in LOOPS:
            return self.loop_stmt(node, state, frames)
        if kind == "do_statement":
            return self.do_stmt(node, state, frames)
        if kind == "switch_statement":
            return self.switch_stmt(node, state, frames)
        if kind == "try_statement":
            return self.try_stmt(node, state, frames)
        if kind in ("return_statement", "co_return_statement", "throw_statement"):
            for child in named(node):
                state = self.eval(child, state)
            return None
        if kind == "expression_statement":
            return self.expression_stmt(node, state)
        if kind in ("break_statement", "continue_statement"):
            self.jump(kind == "continue_statement", state, frames)
            return None
        if kind == "goto_statement":
            label = node.child_by_field("label")
            if label is not None:
                name = name_of(label)
                self.goto_states[name] = join(self.goto_states.get(name), state)
            return None
        if kind == "labeled_statement":
            return self.labeled_stmt(node, state, frames, scope)
        if kind == "attributed_statement":
            for child in named(node):
                if child.type != "attribute_declaration":
                    state = self.stmt(child, state, frames, scope)
            return state
        if kind == "declaration":
            return self.declaration(node, state, scope)
        if kind in PREPROC_CONDITIONALS:
            return self.preproc_arms(node, state, lambda arm, entry: self.stmt(arm, entry, frames, scope))
        if kind == "case_statement":
            for child in self.case_body(node):
                state = self.stmt(child, state, frames, scope)
            return state
        return self.eval(node, state)

    def expression_stmt(self, node: tsast.Node, state: State) -> State:
        """Walk an expression statement: a throw ends the path, and a base assignment spends the key p#base."""
        items = named(node)
        if len(items) == 1 and items[0].type == "throw_expression":
            for child in named(items[0]):
                state = self.eval(child, state)
            return None
        if len(items) == 1 and self.base_assignment(items[0]):
            return self.assign_base(items[0], state)
        for child in items:
            state = self.eval(child, state)
        return state

    def jump(self, is_continue: bool, state: State, frames: list[Frame]) -> None:
        """Record the state of a break at the nearest frame, or of a continue at the nearest loop."""
        for frame in reversed(frames):
            if is_continue and not frame.is_loop:
                continue
            if is_continue:
                frame.at_continue = join(frame.at_continue, state)
            else:
                frame.at_break = join(frame.at_break, state)
            break

    def labeled_stmt(self, node: tsast.Node, state: State, frames: list[Frame], scope: Scope) -> State:
        """Walk a labeled statement: the label joins the state of each goto to it that the walk went through.

        A label with no walked goto to it keeps a live state, because a
        subsequent goto of the body can jump to it.
        """
        label = node.child_by_field("label")
        entered = join(state, None if label is None else self.goto_states.get(name_of(label)))
        state = entered if entered is not None else {}
        for child in named(node):
            if child.field != "label":
                state = self.stmt(child, state, frames, scope)
        return state

    def preproc_arms(self, node: tsast.Node, state: State, walk: Callable[[tsast.Node, State], State]) -> State:
        """Walk each arm of a preprocessor conditional from the same state, and join the arms.

        The build compiles one arm and not the others, and the arms are paths.
        A conditional with no #else also has the path where no arm compiles.
        """
        out: State = None
        has_else = False
        current: tsast.Node | None = node
        while current is not None:
            arm_state = state
            for child in named(current):
                if child.field in ("condition", "name", "alternative"):
                    continue
                arm_state = walk(child, arm_state)
            out = join(out, arm_state) if out is not None or arm_state is not None else None
            alternative = current.child_by_field("alternative")
            if alternative is None:
                break
            if alternative.type == "preproc_else":
                arm_state = state
                for child in named(alternative):
                    arm_state = walk(child, arm_state)
                out = join(out, arm_state)
                has_else = True
                break
            current = alternative
        return out if has_else else join(out, state)

    def case_body(self, case: tsast.Node) -> list[tsast.Node]:
        """Return the statements of a case, with no value."""
        return [child for child in named(case) if child.field != "value"]

    def sub_stmt(self, node: tsast.Node | None, state: State, frames: list[Frame]) -> State:
        """Walk a statement in a scope of its own."""
        if node is None:
            return state
        scope: Scope = {}
        return self.close_scope(self.stmt(node, state, frames, scope), scope)

    def declared_names(self, declarator: tsast.Node | None) -> list[str]:
        """Return the names that a declarator declares: its name, or each name of `auto [a, b] = obj;`."""
        node = declarator
        while node is not None and node.type in DECLARATOR_WRAPPERS:
            inner = node.child_by_field("declarator")
            if inner is None:
                items = [
                    child for child in named(node) if child.type not in ("type_qualifier", "attribute_declaration")
                ]
                inner = items[0] if items else None
            node = inner
        if node is None:
            return []
        if node.type == "identifier":
            return [name_of(node)]
        if node.type == "structured_binding_declarator":
            return [name_of(child) for child in named(node) if child.type == "identifier"]
        return []

    def declaration(self, node: tsast.Node, state: State, scope: Scope) -> State:
        """Walk a declaration: evaluate each initializer, then declare the names and give each reference its key.

        `T& r = x;` makes r name x.  `auto& [a, b] = obj;` makes a and b name a
        member of obj, and the walk gives each one the key obj.  A copy is a new
        object, and its name gets no different key.
        """
        declared: list[str] = []
        bindings: list[tuple[list[str], Place, bool]] = []
        for child in node.children:
            if child.field != "declarator":
                continue
            target: tsast.Node | None = child
            value: tsast.Node | None = None
            if child.type == "init_declarator":
                target = child.child_by_field("declarator")
                value = child.child_by_field("value")
                if value is not None:
                    state = self.eval(value, state)
            if target is not None and target.type == "function_declarator":
                continue
            names = self.declared_names(target)
            declared += names
            if (
                target is not None
                and target.type == "reference_declarator"
                and value is not None
                and (path := self.plain_path(value)) is not None
            ):
                inner = target.child_by_field("declarator") or next(iter(named(target)), None)
                is_binding = inner is not None and inner.type == "structured_binding_declarator"
                bindings.append((names, self.canon(path), is_binding))
        # A declaration in a condition keeps its value in the field value.
        if node.parent is not None and node.parent.type == "condition_clause":
            value = node.child_by_field("value")
            if value is not None:
                state = self.eval(value, state)
        state = self.declare(state, scope, declared)
        for name in declared:
            self.aliases.pop(name, None)
        for names, place, is_binding in bindings:
            for name in names:
                self.aliases[name] = (place, is_binding)
        return state

    def header(self, clause: tsast.Node | None, state: State, scope: Scope) -> State:
        """Evaluate a condition clause that can declare names, such as if (T x = f(); x)."""
        if clause is None:
            return state
        for child in named(clause):
            if child.type == "init_statement":
                for inner in named(child):
                    state = (
                        self.declaration(inner, state, scope)
                        if inner.type == "declaration"
                        else self.stmt(inner, state, [], scope)
                    )
            elif child.type == "declaration":
                state = self.declaration(child, state, scope)
            else:
                state = self.eval(child, state)
        return state

    def condition_value(self, clause: tsast.Node | None) -> tsast.Node | None:
        """Return the expression of a condition when it is all of the condition, with no declaration or initializer."""
        if clause is None:
            return None
        if clause.type != "condition_clause":
            node = clause
        else:
            items = named(clause)
            if len(items) != 1 or items[0].field != "value" or items[0].type == "declaration":
                return None
            node = items[0]
        while node.type == "parenthesized_expression" and len(named(node)) == 1:
            node = named(node)[0]
        return node

    def try_call(self, node: tsast.Node | None) -> bool | None:
        """Return the negation of a condition that is f(...) or !f(...), for one call f with a name that starts with try_.

        The result is True for !f(...), False for f(...), and None for each
        different condition.  The function can be a member, as in
        !ring->try_push(std::move(v)).
        """
        if node is None:
            return None
        is_negated = False
        if node.type == "unary_expression" and operator(node) in ("!", "not"):
            is_negated = True
            argument = node.child_by_field("argument")
            if argument is None:
                return None
            node = argument
        if node.type != "call_expression":
            return None
        function = node.child_by_field("function")
        if function is None or not is_plain_callee(function):
            return None
        leaf = plain_leaf(function)
        if leaf is None or not tsast.lexeme(leaf).startswith(TRY_PREFIX):
            return None
        return is_negated

    def condition(self, node: tsast.Node | None, state: State) -> tuple[State, State]:
        """Return the state where a condition holds and the state where it does not hold.

        A function with a name that starts with try_ keeps its argument when it
        does not succeed, as std::map::try_emplace does.  A move into such a
        call then spends the name only on the path where the call succeeded.
        """
        if node is None:
            return state, state
        is_negated = self.try_call(node)
        if is_negated is None:
            after = self.eval(node, state)
            return after, after
        self.hold_moves = True
        failed = self.eval(node, state)
        self.hold_moves = False
        succeeded = self.eval(node, state)
        return (failed, succeeded) if is_negated else (succeeded, failed)

    def if_stmt(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk an if: the two branches start where the condition holds and where it does not hold."""
        scope: Scope = {}
        clause = node.child_by_field("condition")
        value = self.condition_value(clause)
        if clause is None:
            when_true = when_false = state
        elif self.try_call(value) is not None:
            when_true, when_false = self.condition(value, state)
        else:
            when_true = when_false = self.header(clause, state, scope)
        then = self.sub_stmt(node.child_by_field("consequence"), when_true, frames)
        other = when_false
        alternative = node.child_by_field("alternative")
        if alternative is not None:
            branch = named(alternative)
            other = self.sub_stmt(branch[0] if branch else None, when_false, frames)
        return self.close_scope(join(then, other), scope)

    def loop_stmt(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk a loop two times.  A move on one iteration then meets a use on the next."""
        scope: Scope = {}
        kind = node.type
        body = node.child_by_field("body")
        loop_names: list[str] = []
        condition: tsast.Node | None = None
        update: tsast.Node | None = None
        if kind in ("for_range_loop", "expansion_statement"):
            initializer = node.child_by_field("initializer")
            if initializer is not None:
                state = self.header_part(initializer, state, scope)
            right = node.child_by_field("right")
            if right is not None and state is not None:
                # A range for reads each element of its range.  A plain range path then reads the key path[*],
                # which agrees with each spent element key.
                path = self.plain_path(right)
                if path is not None:
                    self.use(right, state, path.then(Step(INDEX, ANY_INDEX)))
                state = self.eval(right, state)
            loop_names = self.declared_names(node.child_by_field("declarator"))
        elif kind == "for_statement":
            initializer = node.child_by_field("initializer")
            if initializer is not None:
                state = self.header_part(initializer, state, scope)
            condition = node.child_by_field("condition")
            update = node.child_by_field("update")
        else:
            clause = node.child_by_field("condition")
            declarations = [] if clause is None else clause.children_of_type("declaration")
            loop_names = [
                name
                for declaration in declarations
                for child in declaration.children
                if child.field == "declarator"
                for name in self.declared_names(child.child_by_field("declarator") or child)
            ]
            condition = clause
        entry = state
        exit_state: State = None
        head = entry
        for _ in range(2):
            leave = head
            if condition is not None:
                value = self.condition_value(condition)
                if value is not None:
                    head, leave = self.condition(value, head)
                else:
                    head = leave = self.header(condition, head, {})
            exit_state = join(exit_state, leave)
            frame = Frame(is_loop=True)
            frames.append(frame)
            inner = self.declare(head, scope, loop_names) if loop_names else head
            tail = self.sub_stmt(body, inner, frames)
            frames.pop()
            tail = join(tail, frame.at_continue)
            if update is not None:
                tail = self.eval(update, tail)
            exit_state = join(exit_state, frame.at_break)
            head = join(entry, tail)
        return self.close_scope(exit_state, scope)

    def header_part(self, node: tsast.Node, state: State, scope: Scope) -> State:
        """Walk the initializer of a for: a declaration declares into the scope of the loop, and an expression runs."""
        if node.type == "declaration":
            return self.declaration(node, state, scope)
        if node.type == "init_statement":
            for inner in named(node):
                state = self.header_part(inner, state, scope)
            return state
        return self.eval(node, state)

    def do_stmt(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk a do-while two times: the body runs first, then the condition."""
        body = node.child_by_field("body")
        condition = node.child_by_field("condition")
        entry = state
        exit_state: State = None
        head = entry
        for _ in range(2):
            frame = Frame(is_loop=True)
            frames.append(frame)
            tail = self.sub_stmt(body, head, frames)
            frames.pop()
            tail = join(tail, frame.at_continue)
            tail, leave = self.condition(self.condition_value(condition), tail)
            exit_state = join(join(exit_state, leave), frame.at_break)
            head = join(entry, tail)
        return exit_state

    def switch_stmt(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk a switch: each case starts from the join of the state of the case before it and the entry state."""
        scope: Scope = {}
        state = self.header(node.child_by_field("condition"), state, scope)
        body = node.child_by_field("body")
        if body is None or body.type != "compound_statement":
            return self.close_scope(self.sub_stmt(body, state, frames), scope)
        frame = Frame(is_loop=False)
        has_default = False
        frames.append(frame)
        inner: State = None
        block_scope: Scope = {}
        for child in named(body):
            if child.type == "case_statement":
                if child.child_by_field("value") is None:
                    has_default = True
                inner = join(inner, state)
                for statement in self.case_body(child):
                    inner = self.stmt(statement, inner, frames, block_scope)
            else:
                inner = self.stmt(child, inner, frames, block_scope)
        frames.pop()
        inner = self.close_scope(inner, block_scope)
        out = join(inner, frame.at_break)
        if not has_default:
            out = join(out, state)
        return self.close_scope(out, scope)

    def try_stmt(self, node: tsast.Node, state: State, frames: list[Frame]) -> State:
        """Walk a try: each handler starts from the join of the entry state and the state after the try block."""
        body = node.child_by_field("body")
        tried = self.block(body, state, frames) if body is not None else state
        out = tried
        for handler in node.children_of_type("catch_clause"):
            scope: Scope = {}
            names = [
                name
                for parameter in named(handler.child_by_field("parameters"))
                for name in self.declared_names(parameter.child_by_field("declarator"))
            ]
            start = self.declare(join(state, tried), scope, names)
            handler_body = handler.child_by_field("body")
            caught = self.block(handler_body, start, frames) if handler_body is not None else start
            out = join(out, self.close_scope(caught, scope))
        return out

    # ── The expressions ──────────────────────────────────────────────

    def eval(self, node: tsast.Node | None, state: State) -> State:
        """Walk one expression from a state.  The assignments in it make their keys live at its end."""
        if state is None or node is None:
            return state
        out = dict(state)
        pending: list[Place] = []
        self.expr(node, out, pending)
        for path in pending:
            restore(out, path)
        return out

    def expr(self, node: tsast.Node, state: dict[Place, int], pending: list[Place]) -> None:
        """Walk the operands of an expression in the sequence of the source, and refuse each use after move."""
        kind = node.type
        if kind in NOT_EVALUATED:
            return
        if kind == "identifier":
            self.use(node, state, Place(name_of(node)))
            return
        if kind == "lambda_expression":
            state.update(self.lambda_expr(node, state))
            return
        if kind == "call_expression":
            self.call(node, state, pending)
            return
        if kind == "assignment_expression":
            self.assignment(node, state, pending)
            return
        if kind in ("field_expression", "subscript_expression", "pointer_expression"):
            self.access(node, state, pending)
            return
        if kind == "template_function":
            name = node.child_by_field("name")
            if name is not None:
                self.expr(name, state, pending)
            return
        if kind in PREPROC_CONDITIONALS:
            after = self.preproc_arms(node, state, lambda arm, entry: self.eval(arm, entry))
            state.clear()
            state.update(after or {})
            return
        if kind in ("cast_expression", "new_expression", "compound_literal_expression"):
            for child in named(node):
                if child.field != "type":
                    self.expr(child, state, pending)
            return
        if kind == "initializer_pair":
            value = node.child_by_field("value")
            if value is not None:
                self.expr(value, state, pending)
            return
        for child in named(node):
            self.expr(child, state, pending)

    def access(self, node: tsast.Node, state: dict[Place, int], pending: list[Place]) -> None:
        """Walk a member access, a subscript or a unary * or &, and read the path that it names."""
        if node.type == "pointer_expression":
            operand = node.child_by_field("argument")
            if operand is None:
                return
            path = self.plain_path(operand) if operator(node) == POINTEE_MARK else None
            if path is not None:
                # A unary * names the object that the path holds, which is the key *path.
                self.use(operand, state, path.pointee())
            else:
                self.expr(operand, state, pending)
            return
        if node.type == "subscript_expression":
            self.subscript(node, state, pending)
            return
        path = self.plain_path(node)
        if path is not None:
            self.use(node, state, path)
            return
        argument = node.child_by_field("argument")
        if argument is not None:
            self.expr(argument, state, pending)

    def subscript(self, node: tsast.Node, state: dict[Place, int], pending: list[Place]) -> None:
        """Walk a subscript.  An index that is not an integer literal can name each element, and it reads path[*]."""
        argument = node.child_by_field("argument")
        path = self.plain_path(node)
        if path is not None:
            self.use(node, state, path)
            return
        indices = node.child_by_field("indices")
        if indices is not None:
            self.expr(indices, state, pending)
        base = None if argument is None else self.plain_path(argument)
        if argument is not None and base is not None:
            self.use(argument, state, base.then(Step(INDEX, ANY_INDEX)))
        elif argument is not None:
            self.expr(argument, state, pending)

    def receiver(self, callee: tsast.Node) -> Place | None:
        """Return the path of the object of a member call: obj of obj.f() and *p of (*p).f(), when it is plain."""
        argument = callee.child_by_field("argument")
        if argument is None:
            return None
        operand = self.deref_operand(argument)
        if operand is not None:
            path = self.plain_path(operand)
            return None if path is None else path.pointee()
        return self.plain_path(argument)

    def call(self, node: tsast.Node, state: dict[Place, int], pending: list[Place]) -> None:
        """Walk a call: a move spends its argument, and the other shapes of THE KEYS change their keys."""
        function = node.child_by_field("function")
        arguments = node.child_by_field("arguments")
        if self.move_callee(node) or self.rvalue_cast(node):
            path = self.move_argument(arguments)
            if path is not None:
                element = self.element_of_get(node) if self.move_callee(node) else None
                spent = path if element is None else path.then(Step(INDEX, element))
                self.spend(state, function if function is not None else node, spent)
                return
            if arguments is not None:
                self.expr(arguments, state, pending)
            return
        if (
            function is not None
            and function.type != "field_expression"
            and self.free_call(node, function, state, pending)
        ):
            return
        if function is not None and function.type == "field_expression":
            receiver = self.receiver(function)
            if receiver is not None:
                if self.member_call(function, receiver, arguments, state, pending):
                    return
                self.use(function, state, receiver)
            else:
                argument = function.child_by_field("argument")
                if argument is not None:
                    self.expr(argument, state, pending)
        elif function is not None:
            self.expr(function, state, pending)
        if arguments is not None:
            self.expr(arguments, state, pending)

    def free_call(self, node: tsast.Node, function: tsast.Node, state: dict[Place, int], pending: list[Place]) -> bool:
        """Walk swap(a, b), get<I>(t), destroy_at(&x) and construct_at(&x, ...), and return True for one of them."""
        name = leaf_name(function)
        arguments = node.child_by_field("arguments")
        items = named(arguments)
        if name == "swap" and arguments is not None:
            paths = [self.plain_path(item) for item in items]
            if len(paths) == 2 and paths[0] is not None and paths[1] is not None:
                exchange(state, self.canon(paths[0]), self.canon(paths[1]))
                return True
        element = self.get_element(node)
        if element is not None:
            # std::get<I>(t) reads the element key t[I], with each qualifier of get.
            spent = spent_prefix(state, element)
            if spent is not None:
                self.report(node, element, USE, spent, state[spent])
            return True
        if name in ("destroy_at", "construct_at") and arguments is not None:
            target = items[0] if items else None
            operand = None if target is None else target.child_by_field("argument")
            path = None if operand is None else self.plain_path(operand)
            if (
                target is not None
                and target.type == "pointer_expression"
                and operator(target) == "&"
                and path is not None
            ):
                pending.append(self.canon(path))
                for item in items[1:]:
                    self.expr(item, state, pending)
                return True
        return False

    def member_call(
        self,
        function: tsast.Node,
        receiver: Place,
        arguments: tsast.Node | None,
        state: dict[Place, int],
        pending: list[Place],
    ) -> bool:
        """Walk REFILL_METHODS, a swap, at() and value() on a plain receiver, and return True for one of them."""
        name = leaf_name(function)
        if name in REFILL_METHODS:
            pending.append(self.canon(receiver))
            if arguments is not None:
                self.expr(arguments, state, pending)
            return True
        if name == "swap":
            other = self.move_argument(arguments)
            if other is not None:
                exchange(state, self.canon(receiver), other)
                return True
        if name == "at":
            self.use(function, state, receiver.then(self.index_step(arguments)))
            if arguments is not None:
                self.expr(arguments, state, pending)
            return True
        if name == "value" and arguments is not None and not named(arguments):
            self.use(function, state, receiver if receiver.is_pointee else receiver.pointee())
            return True
        return False

    def assignment(self, node: tsast.Node, state: dict[Place, int], pending: list[Place]) -> None:
        """Walk an assignment.  A plain = makes its left key live at the end of the expression.

        An assignment through a[i] fills one element, which can be a
        different element than the spent one.  It makes no key live.  A
        compound assignment reads its left side.
        """
        left = node.child_by_field("left")
        right = node.child_by_field("right")
        if left is not None and tsast.operator_of(node) == "=":
            operand = self.deref_operand(left)
            path = self.plain_path(operand) if operand is not None else self.plain_path(left)
            argument = left.child_by_field("argument")
            if path is not None:
                pending.append(self.canon(path.pointee() if operand is not None else path))
            elif left.type == "subscript_expression" and argument is not None and self.plain_path(argument) is not None:
                indices = left.child_by_field("indices")
                if indices is not None:
                    self.expr(indices, state, pending)
            else:
                self.expr(left, state, pending)
        elif left is not None:
            self.expr(left, state, pending)
        if right is not None:
            self.expr(right, state, pending)

    def capture_entries(self, captures: tsast.Node) -> list[tuple[tsast.Node, bool]]:
        """Return each capture of a lambda, with True when it captures by reference.

        The grammar gives no node to the `&` of a capture, and the walk finds
        the tokens of each capture in the tokens of the capture list.
        """
        words = captures.tokens()
        out: list[tuple[tsast.Node, bool]] = []
        at = 1
        for child in named(captures):
            own = child.tokens()
            found = next(
                (index for index in range(at, len(words) - len(own) + 1) if words[index : index + len(own)] == own),
                None,
            )
            if found is None:
                out.append((child, False))
                continue
            out.append((child, words[found - 1] == "&"))
            at = found + len(own)
        return out

    def lambda_expr(self, node: tsast.Node, state: dict[Place, int]) -> dict[Place, int]:
        """Walk the captures at the point of the lambda, and its body as a new body (THE LAMBDAS)."""
        state = dict(state)
        shadow: list[str] = []
        captures = node.child_by_field("captures")
        if captures is not None:
            for child, is_reference in self.capture_entries(captures):
                if child.type == "lambda_capture_initializer":
                    left = child.child_by_field("left")
                    right = child.child_by_field("right")
                    if left is not None and len(left.lexed()) == 1:
                        shadow.append(tsast.lexeme(left))
                    if right is not None:
                        state = self.eval(right, state) or {}
                elif child.type == "identifier" and not is_reference:
                    captured = Place(name_of(child))
                    spent = spent_prefix(state, self.canon(captured))
                    if spent is not None:
                        self.report(child, captured, COPY_CAPTURE, spent, state[spent])
        declarator = node.child_by_field("declarator")
        parameters = None if declarator is None else declarator.child_by_field("parameters")
        for parameter in named(parameters):
            shadow += self.declared_names(parameter.child_by_field("declarator"))
        inner: dict[Place, int] = dict(state)
        for name in shadow:
            restore(inner, Place(name))
        body = node.child_by_field("body")
        if body is not None:
            outer, self.assigned_from = self.assigned_from, None
            try:
                self.walk_body(body, inner)
            finally:
                self.assigned_from = outer
        return state


def reason_findings(rule: Rule, context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each row of the ledger with no reason (THE LEDGER).

    A ledger that does not obey its schema gives no finding here, because the
    run of the rule gives the error of the schema.
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError, OSError:
        return []
    shown = rule_ledger.shown_path(rule.id)
    return [
        check_report.Finding(
            "error",
            shown,
            entry.line,
            RULE,
            f"the row of {key.path} ({key.entity}) gives no reason.  {REASON_ADVICE}",
            REASON_KIND,
            f"{key.path}: {key.entity}",
        )
        for key, entry in sorted(ledger.base.items())
        if not entry.reason
    ]


def bound_findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error when the plants hold more known misses than KNOWN_MISS_BOUND (WHAT THE RULE DOES NOT SEE).

    A tree with no plants of the rule, such as the copy of one plant, holds
    no known miss.

    Raises:
        NotApplicable: If the root is not a git work tree
    """
    try:
        files = git.tracked_files(context.root, (PLANTS,), with_new=True)
    except git.GitError as problem:
        raise NotApplicable(f"the tree {context.root} is not a git work tree: {problem}") from None
    misses = sorted(
        {
            directory
            for directory in (PurePosixPath(path).relative_to(PLANTS).parts[0] for path in files)
            if directory.startswith(KNOWN_MISS_PREFIX)
        }
    )
    if len(misses) <= KNOWN_MISS_BOUND:
        return []
    return [
        check_report.Finding(
            "error",
            PLANTS,
            0,
            RULE,
            f"the plants hold {len(misses)} known misses ({', '.join(misses)}), and the maximum is "
            f"{KNOWN_MISS_BOUND}.  The list of known misses only becomes shorter.  Teach the rule the new shape, "
            f"and make its control a plant",
            BOUND_KIND,
        )
    ]


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give each use after move, each macro that moves, each row of the ledger with no reason, and a known miss too many."""
    paths = [path for path in context.scope_files() if not is_negative_fixture(path)]
    found = [finding for tree in context.trees(paths) for finding in Walk(tree).run()]
    if context.rule is not None:
        found += reason_findings(context.rule, context)
    found += bound_findings(context)
    return found
