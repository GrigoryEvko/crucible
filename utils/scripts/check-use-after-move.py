#!/usr/bin/env python3
"""check-use-after-move.py: no use of a local after std::move, on any path.

The rule
--------
After `std::move(x)`, `std::forward<T>(x)`, `std::move_if_noexcept(x)`,
`std::forward_like<T>(x)` or `static_cast<T&&>(x)`, the name `x` is spent.
The qualifier of the callee does not matter, so `move(x)` after a
using-declaration and `alias::move(x)` after a namespace alias are moves too.
A macro may not move or forward at all, because each use of such a macro
would hide a move.  A later read of `x`, a second move of `x`, or a member of
`x` is a use after move, unless an assignment to `x` comes first.  The guard
checks each path through a function body: a move in one branch of an `if`
does not spend the name in the other branch, a move that is followed by a
return spends nothing after it, and a move inside a loop spends the name on
the next iteration unless the loop declares or assigns the name again.  The
guard reports the first use after each move, because the next uses of the
same move are the same defect.

Why
---
A linear token in this tree is an empty class, so a moved-from token is
indistinguishable from a live one.  `Permission<Tag>` stays one byte and
collapses as an empty base, so no liveness byte records the move, and
GCC 16 has no use-after-move analysis.  Without this guard, a second move
of one token gives two owners of one region, which is the violation of
the exclusive points-to of Actris 2.0 (Hinrichsen, Bengtson and
Krebbers, LMCS 2022) that a linear type exists to refuse.  The rule is the
affine discipline of a linear type system, applied to C++ names by a
flow check.  It covers every type, because a use after move of any type is
a defect, and a linear type is the case where the defect is silent.

How it reads the source
-----------------------
The guard reads the parse tree of each file (utils/scripts/tsast.py, the pinned
tree-sitter kit).  It walks each function body, each lambda body and each
member-initializer list as a control-flow tree: blocks, if and else, switch
and case, the loops, try and catch, return, throw, break, continue and goto.
The arms of a preprocessor conditional are two paths, so their states join.
The state at each point is the set of names that may be spent.  Two paths
join by union, so a name that one path spends stays spent after the join.

A key is a name or a member path (a.b, a->b).  A move of a spends a and
each member path under it.  A use of a.c after a move of a.b is not a use
after move.  Parentheses around a name do not change its key, and this->m
and (*this).m key as m.  A subscript by an integer literal is part of the
key, so a[0] and a[1] are two keys.  A subscript by any other expression
keys as a[*], which matches every element key of a.  A label that a later
goto of its block targets is a loop head, so the walk passes the statements
from it twice, as it does for a loop.  An assignment through a[i] refills
nothing, because it need not refill the spent element.  a.at(0) keys as a[0],
a range for reads its range as a[*], and std::get<I>(t) keys as t[I].  A
reference `T& r = x;` keys as x, so a move of r spends x, and a structured
binding `auto& [b] = obj;` keys as obj.  An assignment to a key, or reset,
clear, emplace or assign on it, restores it.  A declaration restores its
name in the scope that declares it.  Uses in unevaluated operands (sizeof,
alignof, decltype, noexcept, typeid, requires, static_assert, a reflection)
are not uses.

Five shapes leave a name or its members whole, and the self-test holds each
one:
- A call whose name starts with try_ leaves its argument whole when it
  fails.  std::map::try_emplace states this rule.  So in
  `while (!ring.try_push(std::move(v)))` the loop body sees v live, and
  the code after the loop sees v spent.  An if, a while and a do-while
  whose whole condition is one such call, or its negation, split this way.
- swap(a, b), std::swap(a, b) and a.swap(b) exchange the spent state of a
  and b, as they exchange the values.  The swap itself is not a use.
- In a move constructor C(C&& other), an initializer whose whole argument
  is a move of other can only be a base, because C cannot hold a member of
  type C.  The base moves only its own subobject, so the next initializers
  can read other.member.  The body sees other as spent.
- In a move assignment C& operator=(C&& other), a statement that is only
  Base::operator=(std::move(other)) assigns a base, for the same reason.
  The next statements can read other.member, and a later use of other as
  a whole is a use after move.
- static_cast<T&&>(x) for a scalar T, such as int, leaves x whole, because
  a scalar has no moved-from state.

A lambda body is a new body.  It starts with the spent names of the point
where the lambda appears, minus its parameters and its init-captures, so
a body that names a spent capture is a use after move.  A move inside a
lambda body does not spend a name outside it, because the lambda may run
later or never.  An init-capture runs where the lambda appears, so
[y = std::move(x)] spends x there.

What it does not see, stated rather than implied
------------------------------------------------
test/guard_attacks/use_after_move_evasions.cpp holds a function for each
shape below that hides a real use after move, and its test fails when the
guard learns one of them.
- A move inside a function that takes T&.  The call does not say
  std::move, so the name stays live at the call site.  A call that passes
  std::move(x) to a T&& parameter spends x, although the callee can leave
  x whole.  An allowlist entry says why when it does.
- A use through a pointer to a spent object, or through a reference that
  is not bound to a plain name, such as a reference that a call returns.
- A lambda that captures a name by reference, runs after the name is
  moved, and reads it.  The lambda body appears before the move in the
  source, so the guard reads it with the name live.
- The guard does not know types, so it also reports a use after move of
  a type whose moved-from state is specified, such as a std::vector.
  Assign the name first.  The allowlist is only for a test that probes a
  moved-from object on purpose.
- A try_ function that moves its argument when it fails, for example one
  that takes a parameter by value of a type that is not trivially
  copyable, breaks the try_ rule, and the guard does not see that.

A file that does not parse is a finding, because the guard cannot read it.
A file that tsast.UNPARSEABLE lists is out of scope, because it is not C++.
Negative-compile fixtures (a test directory named neg or *_neg) are out of
scope, because each one must fail to compile.

The allowlist
-------------
utils/scripts/use-after-move-allowlist.txt holds reviewed findings.  An entry
is `path:function:key`, or `path:function:key xN` for N findings, with
a comment above it that names the probe.  The key survives a line shift.
Only a test that reads or uses a moved-from object on purpose belongs on
the list.  A real use after move is fixed in the code, and a finding that
the guard reads wrongly is fixed in the guard, with a self-test line.

Exit codes
  0  every finding has an entry, and every entry has its findings
  1  a finding with no entry
  2  an entry with more findings admitted than found, a bad invocation,
     or a failed self-test
  3  the pinned tree-sitter kit is not installed

Usage
  check-use-after-move.py                 check the tree
  check-use-after-move.py --list          print each finding
  check-use-after-move.py --self-test     prove the verdicts on planted
                                          sources
  check-use-after-move.py FILE...         check the named files only
"""

from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

SCAN_ROOTS = ("include", "src", "test", "vessel", "utils/tools")
ALLOWLIST = "utils/scripts/use-after-move-allowlist.txt"
# The files one worker parses in one kit run.  A run costs a fixed start-up,
# so a chunk amortises it, and the chunks spread over the workers.
CHUNK = 96

# The last component of a callee that gives an rvalue of its argument.  The
# qualifier does not matter: std::move, ::std::move, a namespace alias and a
# using-declaration all reach the same function.
MOVE_CALLEES = frozenset({"move", "forward", "move_if_noexcept", "forward_like"})
REINIT_METHODS = frozenset({"reset", "clear", "emplace", "assign"})
# The words of a scalar type.  static_cast<int&&>(x) leaves x whole,
# because a scalar has no moved-from state.
SCALAR_TYPE_WORDS = frozenset("""
    bool char char8_t char16_t char32_t wchar_t short int long signed unsigned float double const volatile
    std :: byte size_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t
    uint64_t
""".split())
# Nodes whose operands are not evaluated, and nodes that name a type or a
# scope rather than an object.  A name under one of them is never a use.
NOT_EVALUATED = frozenset({
    "sizeof_expression", "alignof_expression", "decltype", "noexcept_expression", "typeid_expression",
    "requires_expression", "requires_clause", "static_assert_declaration", "reflect_expression",
    "splice_expression", "splice_type_specifier", "type_descriptor", "template_argument_list",
    "qualified_identifier", "namespace_identifier", "type_identifier", "primitive_type", "sized_type_specifier",
    "field_identifier", "statement_identifier", "this", "comment", "number_literal", "string_literal",
    "raw_string_literal", "char_literal", "concatenated_string", "user_defined_literal", "true", "false",
    "nullptr", "preproc_arg", "preproc_def", "preproc_function_def", "preproc_call", "preproc_include",
})
# Statements that declare no local and evaluate nothing the walk tracks.
INERT_STATEMENTS = frozenset({
    "type_definition", "alias_declaration", "using_declaration", "static_assert_declaration",
    "namespace_alias_definition", "friend_declaration", "template_declaration", "asm_statement", "comment",
    "preproc_def", "preproc_function_def", "preproc_call", "preproc_include", "empty_statement",
    "concept_definition", "class_specifier", "struct_specifier", "union_specifier", "enum_specifier",
})
CLASS_SPECIFIERS = frozenset({"class_specifier", "struct_specifier", "union_specifier", "enum_specifier"})
PREPROC_CONDITIONALS = frozenset({"preproc_if", "preproc_ifdef", "preproc_elif", "preproc_elifdef"})
LOOPS = frozenset({"while_statement", "for_statement", "for_range_loop", "expansion_statement"})
# The suffix of the key that Base::operator=(std::move(p)) spends in a move
# assignment.  The key p#base is reached by a use of p as a whole and by no
# member of p, because the base assignment takes only the base subobject.
BASE_MARK = "#base"
# The declarator wrappers between a declaration and the name it declares.
DECLARATOR_WRAPPERS = frozenset({
    "init_declarator", "reference_declarator", "pointer_declarator", "array_declarator",
    "attributed_declarator", "parenthesized_declarator",
})


@dataclass
class Finding:
    path: str
    line: int
    function: str
    key: str
    what: str
    moved_at: int

    def allow_key(self) -> str:
        return f"{self.path}:{self.function}:{self.key}"


State = dict[str, int] | None  # spent key -> line of its move; None when no path reaches the point


def join(a: State, b: State) -> State:
    """Return the state after two paths meet: a key spent on either path is spent."""
    if a is None:
        return None if b is None else dict(b)
    if b is None:
        return dict(a)
    out = dict(a)
    for key, line in b.items():
        out[key] = min(line, out.get(key, line))
    return out


def segments(key: str) -> list[str]:
    """Split a key into its name, its members (.m, ->m) and its subscripts ([0], [*])."""
    parts: list[str] = []
    k = 0
    while k < len(key):
        start = k
        if key[k] == "[":
            k = key.index("]", k) + 1
        else:
            if key.startswith("->", k):
                k += 2
            elif key[k] in ".*":
                k += 1
            while k < len(key) and key[k] not in ".[" and not key.startswith("->", k):
                k += 1
        parts.append(key[start:k])
    return parts


def reaches(spent: str, key: str) -> bool:
    """Whether key names the spent key or a part of it.

    Two subscripts match when they are equal or when either one is [*], because
    an index that is not a literal can name any element.
    """
    spent_parts, key_parts = segments(spent), segments(key)
    if len(key_parts) < len(spent_parts):
        return False
    for mine, theirs in zip(spent_parts, key_parts):
        subscripts = mine.startswith("[") and theirs.startswith("[")
        if mine != theirs and not (subscripts and "[*]" in (mine, theirs)):
            return False
    return True


def spent_prefix(state: dict[str, int], key: str) -> str | None:
    """The spent key that key names or reaches into, if any.

    A move through a dereference, std::move(*p), spends the key *p.  Then *p,
    a member of *p and p->m reach into it, and p itself does not.  The key
    p#base of a base assignment is reached by p alone.
    """
    for spent in state:
        if reaches(spent, key):
            return spent
        if spent.startswith("*") and key.startswith(spent[1:] + "->"):
            return spent
        if spent == key + BASE_MARK:
            return spent
    return None


def restore(state: dict[str, int], key: str) -> None:
    """Drop the spent keys that an assignment to key refills.

    An assignment to p also refills *p, because p then holds or points at a new value.
    """
    def refilled(spent: str) -> bool:
        base = spent[1:] if spent.startswith("*") and not key.startswith("*") else spent
        return base == key or base.startswith((key + ".", key + "->", key + "[", key + BASE_MARK))

    for spent in [s for s in state if refilled(s)]:
        del state[spent]


def exchange(state: dict[str, int], left: str, right: str) -> None:
    """Swap the spent keys under left with the spent keys under right, as swap(left, right) swaps the values."""
    moved: dict[str, int] = {}
    for spent in list(state):
        for source, target in ((left, right), (right, left)):
            if spent == source or spent.startswith((source + ".", source + "->", source + "[", source + BASE_MARK)):
                moved[target + spent[len(source):]] = state.pop(spent)
                break
    state.update(moved)


def named(node: tsast.Node) -> list[tsast.Node]:
    """The children of a node, without its comments."""
    return [child for child in node.children if child.type != "comment"]


def leaf_name(node: tsast.Node | None) -> str | None:
    """The last name of a callee or a declarator: f in f, ns::f, obj.f, f<T> and ns::f<T>."""
    return None if node is None else tsast.leaf_name(node)


def operator(node: tsast.Node) -> str:
    """The first token that lies in a node outside its named children: . or -> of a member access, * or & of
    a unary operator, & or && of a reference declarator."""
    tokens = node.gap_tokens()
    return tokens[0] if tokens else ""


# The node types of a plain callee chain, such as ring.try_push, ring->try_push or this->q.try_push.
PLAIN_CALLEE = frozenset({"identifier", "field_identifier", "namespace_identifier", "field_expression",
                          "qualified_identifier", "this"})


def is_plain_callee(node: tsast.Node) -> bool:
    """Whether a callee is a chain of names and member accesses, with no call, subscript or template argument."""
    return node.type in PLAIN_CALLEE and all(is_plain_callee(child) for child in named(node))


class Walk:
    """The walk of one file: every function body, lambda body and initializer list in it."""

    def __init__(self, tree: tsast.Tree, path: str) -> None:
        self.tree = tree
        self.path = path
        self.findings: list[Finding] = []
        self.seen: set[tuple[str, str, int]] = set()
        self.function = "<file>"
        # True while a condition is walked for the path where its try_ call
        # failed: a move in the call then leaves the name whole.
        self.hold_moves = False
        # The join of the states at each goto of the current body, by label.
        self.goto_states: dict[str, State] = {}
        # A reference or a structured binding in scope: name -> (the key of the
        # object it names, True for a structured binding).
        self.aliases: dict[str, tuple[str, bool]] = {}
        # The parameter p while the body of a move assignment operator=(C&& p)
        # is walked, and None in every other body.
        self.assigned_from: str | None = None

    # ── helpers ──────────────────────────────────────────────────────

    def report(self, node: tsast.Node, key: str, what: str, spent: str, moved_at: int) -> None:
        """Record the first use after one move.  Later uses of the same move are the same defect."""
        mark = (self.function, spent, moved_at)
        if mark in self.seen:
            return
        self.seen.add(mark)
        self.findings.append(Finding(self.path, node.line, self.function, key, what, moved_at))

    def spend(self, state: dict[str, int], node: tsast.Node, path: str) -> None:
        """Spend path at the move at node, and report a move of a path that a move spent before."""
        spent = spent_prefix(state, path)
        if spent is not None:
            self.report(node, path, "second move", spent, state[spent])
        if not self.hold_moves:
            state[path] = node.line

    def canon(self, path: str) -> str:
        """Return the key of a path, with a reference or a structured binding replaced by the object it names."""
        parts = segments(path)
        if not parts or parts[0].lstrip("*") not in self.aliases:
            return path
        target = self.aliases[parts[0].lstrip("*")][0] + "".join(parts[1:])
        return "*" + target if parts[0].startswith("*") else target

    def is_binding(self, path: str) -> bool:
        """Whether a path starts with the name of a structured binding."""
        parts = segments(path)
        return bool(parts) and self.aliases.get(parts[0].lstrip("*"), ("", False))[1]

    def use(self, node: tsast.Node, state: dict[str, int], raw: str) -> None:
        """Report a read of the path raw at node when a move spent it or a part of it."""
        binding = self.is_binding(raw)
        key = self.canon(raw)
        if binding:
            # A structured binding names a member of its object, and the walk does
            # not know which one, so any spent key under the object counts.
            under = next((s for s in state if reaches(key, s)), None)
            if under is not None:
                self.report(node, key, "use after move", under, state[under])
                return
        spent = spent_prefix(state, key)
        if spent is not None:
            self.report(node, key, "use after move", spent, state[spent])

    # ── paths ────────────────────────────────────────────────────────

    def plain_path(self, node: tsast.Node) -> str | None:
        """The member path that a node names, or None when it is not a name with members and literal indices.

        this->m and (*this).m are the member m.  Parentheses around a path do not
        change it.  A subscript by an integer literal is part of the path.
        """
        kind = node.type
        if kind == "identifier":
            return node.text
        if kind == "parenthesized_expression":
            inner = named(node)
            return self.plain_path(inner[0]) if len(inner) == 1 else None
        if kind == "field_expression":
            argument = node.child_by_field("argument")
            field = node.child_by_field("field")
            if argument is None or field is None or field.type != "field_identifier":
                return None
            access = operator(node)
            if argument.type == "this" and access == "->":
                return field.text
            if access == "." and self.is_dereferenced_this(argument):
                return field.text
            base = self.plain_path(argument)
            return None if base is None else base + access + field.text
        if kind == "subscript_expression":
            argument = node.child_by_field("argument")
            index = self.literal_index(node)
            base = None if argument is None else self.plain_path(argument)
            return None if base is None or index is None else f"{base}[{index}]"
        return None

    def is_dereferenced_this(self, node: tsast.Node) -> bool:
        """Whether a node is (*this)."""
        inner = named(node)
        if node.type != "parenthesized_expression" or len(inner) != 1:
            return False
        pointer = inner[0]
        target = pointer.child_by_field("argument")
        return (pointer.type == "pointer_expression" and operator(pointer) == "*"
                and target is not None and target.type == "this")

    def literal_index(self, subscript: tsast.Node) -> str | None:
        """The index of a subscript when it is one integer literal, or None."""
        indices = subscript.child_by_field("indices")
        items = [] if indices is None else named(indices)
        if len(items) == 1 and items[0].type == "number_literal":
            return items[0].text
        return None

    def index_segment(self, arguments: tsast.Node | None) -> str:
        """Return [N] for a call whose one argument is an integer literal N, and [*] for any other."""
        items = [] if arguments is None else named(arguments)
        return f"[{items[0].text}]" if len(items) == 1 and items[0].type == "number_literal" else "[*]"

    def deref_operand(self, node: tsast.Node) -> tsast.Node | None:
        """The operand of a unary * at node, or None when node is not a dereference."""
        if node.type == "pointer_expression" and operator(node) == "*":
            return node.child_by_field("argument")
        return None

    def get_element(self, call: tsast.Node) -> str | None:
        """For get<I>(t) with t a plain path, return the key t[I]; None for any other call."""
        function = call.child_by_field("function")
        if function is None or function.type == "field_expression":
            return None
        template = function.child_by_field("name") if function.type == "qualified_identifier" else function
        if template is None or template.type != "template_function" or leaf_name(template) != "get":
            return None
        items = named(call.child_by_field("arguments"))
        if len(items) != 1:
            return None
        path = self.plain_path(items[0])
        if path is None:
            return None
        index = "".join(template.child_by_field("arguments").tokens()[1:-1])
        return f"{self.canon(path)}[{index}]"

    def move_argument(self, arguments: tsast.Node | None) -> str | None:
        """The key inside std::move( ... ) when its one argument is a plain path or *path.

        The move std::move(*p) spends the object that p reaches, so its key is *p.
        A subscript by anything but an integer literal keys the whole array.
        """
        items = [] if arguments is None else named(arguments)
        if len(items) != 1:
            return None
        node = items[0]
        while node.type == "parenthesized_expression" and len(named(node)) == 1:
            node = named(node)[0]
        operand = self.deref_operand(node)
        if operand is not None:
            path = self.plain_path(operand)
            return None if path is None else "*" + self.canon(path)
        if node.type == "call_expression":
            element = self.get_element(node)
            if element is not None:
                return element
            function = node.child_by_field("function")
            if function is not None and function.type == "field_expression" and leaf_name(function) == "at":
                base = self.plain_path(function.child_by_field("argument"))
                if base is not None:
                    return self.canon(base) + self.index_segment(node.child_by_field("arguments"))
            return None
        if node.type == "subscript_expression" and self.literal_index(node) is None:
            base = self.plain_path(node.child_by_field("argument"))
            return None if base is None else self.canon(base) + "[*]"
        path = self.plain_path(node)
        return None if path is None else self.canon(path)

    def move_callee(self, call: tsast.Node) -> bool:
        """Whether a call is a move: its callee's last name is in MOVE_CALLEES and it is not a member call."""
        function = call.child_by_field("function")
        return (function is not None and function.type != "field_expression"
                and leaf_name(function) in MOVE_CALLEES)

    def element_of_get(self, call: tsast.Node) -> str | None:
        """The index I when the move call is the first argument of get<I>( ... ), which moves element I."""
        arguments = call.parent
        if arguments is None or arguments.type != "argument_list" or named(arguments)[0].index != call.index:
            return None
        outer = arguments.parent
        function = None if outer is None else outer.child_by_field("function")
        if function is None or function.type == "field_expression":
            return None
        template = function.child_by_field("name") if function.type == "qualified_identifier" else function
        if template is None or template.type != "template_function" or leaf_name(template) != "get":
            return None
        return "".join(template.child_by_field("arguments").tokens()[1:-1])

    def rvalue_cast(self, call: tsast.Node) -> bool:
        """Whether a call is static_cast<T&&>(x) for a T that is not a scalar."""
        function = call.child_by_field("function")
        if function is None or function.type != "template_function" or leaf_name(function) != "static_cast":
            return False
        words = function.child_by_field("arguments").tokens()
        return len(words) >= 3 and words[-2] == "&&" and not all(word in SCALAR_TYPE_WORDS for word in words[1:-2])

    # ── the file ─────────────────────────────────────────────────────

    def run(self) -> list[Finding]:
        """Walk every body in the file and return the findings."""
        if self.tree.diagnostic is not None:
            self.findings.append(Finding(self.path, 1, "<file>", "<parse-error>", "a file the kit cannot parse", 0))
            return self.findings
        self.macro_moves()
        for function in self.tree.find("function_definition"):
            if function.ancestor_of_type("requires_expression", "concept_definition") is not None:
                continue
            body = function.child_by_field("body")
            if body is None:
                continue
            self.function = self.function_name(function)
            if self.function == "operator=":
                self.assigned_from = self.move_source(function, self.owner_class(function))
            state: State = {}
            initializers = function.children_of_type("field_initializer_list")
            if initializers:
                state = self.member_inits(function, initializers[0], state)
            self.walk_body(body, state)
            self.function = "<file>"
            self.assigned_from = None
        for lam in self.tree.find("lambda_expression"):
            if lam.ancestor_of_type("function_definition", "lambda_expression", "requires_expression") is not None:
                continue
            owner = lam.ancestor_of_type("init_declarator")
            self.function = leaf_name(owner.child_by_field("declarator")) if owner is not None else None
            self.function = self.function or "<anonymous>"
            self.lambda_expr(lam, {})
            self.function = "<file>"
        return self.findings

    def macro_moves(self) -> None:
        """Report each #define whose replacement list moves or forwards.

        Each use of such a macro would hide a move from the walk, so a macro may
        not move.  Complexity: linear in the length of the replacement lists.
        """
        for define in self.tree.find("preproc_def", "preproc_function_def"):
            body = " ".join(child.text for child in define.children if child.field == "value")
            words = [token.text for token in tsast.pp_tokens(body)]
            for i, word in enumerate(words):
                after = words[i + 1] if i + 1 < len(words) else ""
                before = words[i - 1] if i > 0 else ""
                if word in MOVE_CALLEES and after in ("(", "<") and before not in (".", "->"):
                    name = define.child_by_field("name")
                    self.findings.append(Finding(self.path, define.line, name.text if name else "<macro>", word,
                                                 "move inside a macro", 0))
                    break

    def function_name(self, function: tsast.Node) -> str:
        """The name of a function definition: its last name, ~X for a destructor, operatorX for an operator."""
        return leaf_name(function.child_by_field("declarator")) or "<anonymous>"

    def member_inits(self, function: tsast.Node, initializers: tsast.Node, state: State) -> State:
        """Walk the member-initializer list of a constructor.

        In a move constructor C(C&& other), an initializer whose whole argument is a move of
        other can only be a base: a member of type C cannot exist inside C.  The base moves its
        own subobject, so the next initializers can still read other.member.  The body sees
        other as spent, as after any other move.
        """
        source = self.move_source(function, self.function_name(function))
        base_moved_at = None
        for initializer in initializers.children_of_type("field_initializer"):
            arguments = [child for child in named(initializer) if child.type in ("argument_list", "initializer_list")]
            if not arguments:
                continue
            if source is not None and self.moves_whole(arguments[0], source):
                self.eval(arguments[0], state)
                base_moved_at = base_moved_at or initializer.line
            else:
                state = self.eval(arguments[0], state)
        if base_moved_at is not None and state is not None:
            state = {**state, source: base_moved_at}
        return state

    def function_declarator(self, function: tsast.Node) -> tsast.Node | None:
        """The function_declarator of a definition, under the reference or pointer declarator of its return type."""
        node = function.child_by_field("declarator")
        while node is not None and node.type != "function_declarator":
            node = node.child_by_field("declarator")
        return node

    def owner_class(self, function: tsast.Node) -> str | None:
        """The class of a member function: the scope of its qualified name, or the class around its definition."""
        declarator = self.function_declarator(function)
        name = None if declarator is None else declarator.child_by_field("declarator")
        if name is not None and name.type == "qualified_identifier":
            parts = tsast.qualified_parts(name)
            return parts[1][-2] if parts is not None and len(parts[1]) >= 2 else None
        enclosing = function.ancestor_of_type("class_specifier", "struct_specifier", "union_specifier")
        return None if enclosing is None else leaf_name(enclosing.child_by_field("name"))

    def move_source(self, function: tsast.Node, owner: str | None) -> str | None:
        """The parameter p when the only parameter of a function is owner&& p, as in C(C&& p) and operator=(C&& p)."""
        declarator = self.function_declarator(function)
        parameters = None if declarator is None else declarator.child_by_field("parameters")
        items = [] if parameters is None else named(parameters)
        if owner is None or len(items) != 1 or items[0].type != "parameter_declaration":
            return None
        declarator = items[0].child_by_field("declarator")
        if (leaf_name(items[0].child_by_field("type")) != owner or declarator is None
                or declarator.type != "reference_declarator" or operator(declarator) != "&&"):
            return None
        inner = declarator.child_by_field("declarator") or (named(declarator) or [None])[0]
        return inner.text if inner is not None and inner.type == "identifier" else None

    def base_assignment(self, node: tsast.Node) -> bool:
        """Whether an expression is Base::operator=(std::move(p)) in the body of a move assignment operator=(C&& p).

        The qualified name reaches a base, because an unqualified operator= of the same class would
        assign the whole object.  this->Base::operator=( ... ) is the same call.
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
        """Walk Base::operator=(std::move(p)) and spend p#base.

        The call takes only the base subobject of p, so the next statements can read p.member,
        and a later use of p as a whole is a use after move.  A second base assignment is not
        a use: the argument is checked against the state without p#base, as a second base
        initializer of a move constructor is.
        """
        if state is None:
            return None
        marker = self.assigned_from + BASE_MARK
        self.eval(call.child_by_field("arguments"), {key: line for key, line in state.items() if key != marker})
        return {**state, marker: state.get(marker, call.line)}

    def moves_whole(self, arguments: tsast.Node, source: str) -> bool:
        """Whether an initializer's argument is exactly a move of the plain name source."""
        items = named(arguments)
        if len(items) != 1 or items[0].type != "call_expression":
            return False
        call = items[0]
        if not (self.move_callee(call) or self.rvalue_cast(call)):
            return False
        inner = named(call.child_by_field("arguments"))
        return len(inner) == 1 and inner[0].type == "identifier" and inner[0].text == source

    # ── statements ───────────────────────────────────────────────────

    def walk_body(self, body: tsast.Node, state: State) -> State:
        """Walk one function or lambda body.  Its labels and gotos are its own."""
        ctx: list[dict] = []
        outer, self.goto_states = self.goto_states, {}
        outer_aliases = dict(self.aliases)
        try:
            if body.type == "compound_statement":
                return self.block(body, state, ctx)
            return self.sub_stmt(body, state, ctx)
        finally:
            self.goto_states, self.aliases = outer, outer_aliases

    def block(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk a block.  A label that a later goto of the block targets is a loop head.

        As for a loop, the statements from the first such label on are walked twice: the
        second walk enters the label with the join of the fallthrough state and the state
        at each goto to it.
        """
        scope: dict[str, int | None] = {}
        statements = named(node)
        head: tuple[int, State, dict] | None = None
        for i, statement in enumerate(statements):
            if head is None and statement.type == "labeled_statement" and self.goto_after(statement, node):
                head = (i, state, dict(scope))
            state = self.stmt(statement, state, ctx, scope)
        if head is not None:
            first, state, scope = head
            for statement in statements[first:]:
                state = self.stmt(statement, state, ctx, scope)
        for name in scope:
            self.aliases.pop(name, None)
        return self.close_scope(state, scope)

    def goto_after(self, labeled: tsast.Node, block: tsast.Node) -> bool:
        """Whether a goto to the label of labeled appears in block after it.  Complexity: O(size of block)."""
        label = labeled.child_by_field("label")
        if label is None:
            return False
        return any(jump.start > labeled.start and (target := jump.child_by_field("label")) is not None
                   and target.text == label.text for jump in block.descendants("goto_statement"))

    def close_scope(self, state: State, scope: dict[str, int | None]) -> State:
        """Leave a scope: its names are no longer spent, and a name it shadowed gets its outer state back."""
        if state is None:
            return None
        out = dict(state)
        for name, outer in scope.items():
            restore(out, name)
            if outer is not None:
                out[name] = outer
        return out

    def declare(self, state: State, scope: dict[str, int | None], names: list[str]) -> State:
        """Declare names in a scope: each one starts live, and its outer state waits for the scope's end."""
        if state is None:
            return None
        out = dict(state)
        for name in names:
            if name not in scope:
                scope[name] = out.get(name)
            restore(out, name)
        return out

    def stmt(self, node: tsast.Node, state: State, ctx: list[dict], scope: dict) -> State:
        """Walk one statement from a state and return the state after it."""
        kind = node.type
        if kind in INERT_STATEMENTS:
            return state
        if kind == "compound_statement":
            return self.block(node, state, ctx)
        if kind == "if_statement":
            return self.if_stmt(node, state, ctx)
        if kind in LOOPS:
            return self.loop_stmt(node, state, ctx)
        if kind == "do_statement":
            return self.do_stmt(node, state, ctx)
        if kind == "switch_statement":
            return self.switch_stmt(node, state, ctx)
        if kind == "try_statement":
            return self.try_stmt(node, state, ctx)
        if kind in ("return_statement", "co_return_statement", "throw_statement"):
            for child in named(node):
                state = self.eval(child, state)
            return None
        if kind == "expression_statement":
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
        if kind in ("break_statement", "continue_statement"):
            jump = "break" if kind == "break_statement" else "continue"
            for frame in reversed(ctx):
                if jump == "continue" and frame["kind"] != "loop":
                    continue
                frame[jump] = join(frame.get(jump), state)
                break
            return None
        if kind == "goto_statement":
            label = node.child_by_field("label")
            if label is not None:
                self.goto_states[label.text] = join(self.goto_states.get(label.text), state)
            return None
        if kind == "labeled_statement":
            # A label joins the state of each goto to it that the walk has passed.
            # A label that no walked goto reaches keeps a live state, since a goto
            # later in the body can still jump to it.
            label = node.child_by_field("label")
            entered = join(state, self.goto_states.get(label.text) if label is not None else None)
            state = entered if entered is not None else {}
            for child in named(node):
                if child.field != "label":
                    state = self.stmt(child, state, ctx, scope)
            return state
        if kind == "attributed_statement":
            for child in named(node):
                if child.type != "attribute_declaration":
                    state = self.stmt(child, state, ctx, scope)
            return state
        if kind == "declaration":
            return self.declaration(node, state, scope)
        if kind in PREPROC_CONDITIONALS:
            return self.preproc_arms(node, state, lambda arm, entry: self.stmt(arm, entry, ctx, scope))
        if kind == "case_statement":
            for child in self.case_body(node):
                state = self.stmt(child, state, ctx, scope)
            return state
        return self.eval(node, state)

    def preproc_arms(self, node: tsast.Node, state: State, walk) -> State:
        """Walk each arm of a preprocessor conditional from the same state, and join the arms.

        One arm is compiled and the others are not, so the arms are paths.  A
        conditional with no #else also has the path where no arm is compiled.
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
        """The statements of a case, without its value."""
        return [child for child in named(case) if child.field != "value"]

    def sub_stmt(self, node: tsast.Node | None, state: State, ctx: list[dict]) -> State:
        """Walk a statement in a scope of its own."""
        if node is None:
            return state
        scope: dict = {}
        return self.close_scope(self.stmt(node, state, ctx, scope), scope)

    def declared_names(self, declarator: tsast.Node | None) -> list[str]:
        """The names a declarator introduces: its name, or each name of a structured binding."""
        node = declarator
        while node is not None and node.type in DECLARATOR_WRAPPERS:
            inner = node.child_by_field("declarator")
            if inner is None:
                items = [child for child in named(node) if child.type not in ("type_qualifier", "attribute_declaration")]
                inner = items[0] if items else None
            node = inner
        if node is None:
            return []
        if node.type == "identifier":
            return [node.text]
        if node.type == "structured_binding_declarator":
            return [child.text for child in named(node) if child.type == "identifier"]
        return []

    def declaration(self, node: tsast.Node, state: State, scope: dict) -> State:
        """Walk a declaration: evaluate each initializer, then declare the names and bind the references.

        `T& r = x;` makes r name x.  `auto& [a, b] = obj;` makes a and b name a member
        of obj, and the walk keys each one as obj.  A copy is a new object and binds
        nothing.
        """
        declared: list[str] = []
        bindings: list[tuple[list[str], str, bool]] = []
        for child in node.children:
            if child.field != "declarator":
                continue
            target, value = child, None
            if child.type == "init_declarator":
                target = child.child_by_field("declarator")
                value = child.child_by_field("value")
                if value is not None:
                    state = self.eval(value, state)
            if target is not None and target.type == "function_declarator":
                continue
            names = self.declared_names(target)
            declared += names
            if (target is not None and target.type == "reference_declarator" and value is not None
                    and (path := self.plain_path(value)) is not None):
                inner = target.child_by_field("declarator") or (named(target) or [None])[0]
                binding = inner is not None and inner.type == "structured_binding_declarator"
                bindings.append((names, self.canon(path), binding))
        # A declaration in a condition keeps its value in the value field.
        if node.parent is not None and node.parent.type == "condition_clause":
            value = node.child_by_field("value")
            if value is not None:
                state = self.eval(value, state)
        state = self.declare(state, scope, declared)
        for name in declared:
            self.aliases.pop(name, None)
        for names, target, binding in bindings:
            for name in names:
                self.aliases[name] = (target, binding)
        return state

    def header(self, clause: tsast.Node | None, state: State, scope: dict) -> State:
        """Evaluate a condition clause that can declare names, such as if (T x = f(); x)."""
        if clause is None:
            return state
        for child in named(clause):
            if child.type == "init_statement":
                for inner in named(child):
                    state = (self.declaration(inner, state, scope) if inner.type == "declaration"
                             else self.stmt(inner, state, [], scope))
            elif child.type == "declaration":
                state = self.declaration(child, state, scope)
            else:
                state = self.eval(child, state)
        return state

    def condition_value(self, clause: tsast.Node | None) -> tsast.Node | None:
        """The expression of a condition when it is the whole condition, with no declaration or initializer."""
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
        """Whether a condition is !f(...), for one call f whose name starts with try_.

        The result is True for !f(...), False for f(...), and None for any other condition.  The
        callee can be a member, as in !ring->try_push(std::move(v)).
        """
        if node is None:
            return None
        negated = False
        if node.type == "unary_expression" and operator(node) in ("!", "not"):
            negated = True
            node = node.child_by_field("argument")
        if node is None or node.type != "call_expression":
            return None
        function = node.child_by_field("function")
        name = leaf_name(function)
        if function is None or name is None or not name.startswith("try_") or not is_plain_callee(function):
            return None
        return negated

    def condition(self, node: tsast.Node | None, state: State) -> tuple[State, State]:
        """The states where a condition is true and where it is false.

        A function whose name starts with try_ leaves its argument whole when it fails, the rule
        that std::map::try_emplace states.  So a move into such a call spends the name only on
        the path where the call succeeded.
        """
        if node is None:
            return state, state
        negated = self.try_call(node)
        if negated is None:
            after = self.eval(node, state)
            return after, after
        self.hold_moves = True
        failed = self.eval(node, state)
        self.hold_moves = False
        succeeded = self.eval(node, state)
        return (failed, succeeded) if negated else (succeeded, failed)

    def if_stmt(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk an if: the two branches start from the condition's true and false states."""
        scope: dict = {}
        clause = node.child_by_field("condition")
        value = self.condition_value(clause)
        if clause is None:
            when_true = when_false = state
        elif self.try_call(value) is not None:
            when_true, when_false = self.condition(value, state)
        else:
            when_true = when_false = self.header(clause, state, scope)
        then = self.sub_stmt(node.child_by_field("consequence"), when_true, ctx)
        other = when_false
        alternative = node.child_by_field("alternative")
        if alternative is not None:
            branch = [child for child in named(alternative)]
            other = self.sub_stmt(branch[0] if branch else None, when_false, ctx)
        return self.close_scope(join(then, other), scope)

    def loop_stmt(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk a loop twice, so a move on one iteration meets a use on the next."""
        scope: dict = {}
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
                # A range for reads every element of its range, so a plain range path
                # reads the key path[*], which matches any spent element key.
                path = self.plain_path(right)
                if path is not None:
                    self.use(right, state, path + "[*]")
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
            if declarations:
                loop_names = [name for d in declarations for c in d.children if c.field == "declarator"
                              for name in self.declared_names(c.child_by_field("declarator") or c)]
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
            frame = {"kind": "loop"}
            ctx.append(frame)
            inner = self.declare(head, scope, loop_names) if loop_names else head
            tail = self.sub_stmt(body, inner, ctx)
            ctx.pop()
            tail = join(tail, frame.get("continue"))
            if update is not None:
                tail = self.eval(update, tail)
            exit_state = join(exit_state, frame.get("break"))
            head = join(entry, tail)
        return self.close_scope(exit_state, scope)

    def header_part(self, node: tsast.Node, state: State, scope: dict) -> State:
        """Walk the initializer of a for: a declaration declares into the loop scope, anything else is evaluated."""
        if node.type == "declaration":
            return self.declaration(node, state, scope)
        if node.type == "init_statement":
            for inner in named(node):
                state = self.header_part(inner, state, scope)
            return state
        return self.eval(node, state)

    def do_stmt(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk a do-while twice: the body runs first, then the condition."""
        body = node.child_by_field("body")
        condition = node.child_by_field("condition")
        entry = state
        exit_state: State = None
        head = entry
        for _ in range(2):
            frame = {"kind": "loop"}
            ctx.append(frame)
            tail = self.sub_stmt(body, head, ctx)
            ctx.pop()
            tail = join(tail, frame.get("continue"))
            tail, leave = self.condition(self.condition_value(condition), tail)
            exit_state = join(join(exit_state, leave), frame.get("break"))
            head = join(entry, tail)
        return exit_state

    def switch_stmt(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk a switch: each case starts from the join of the fall-through state and the entry state."""
        scope: dict = {}
        state = self.header(node.child_by_field("condition"), state, scope)
        body = node.child_by_field("body")
        if body is None or body.type != "compound_statement":
            return self.close_scope(self.sub_stmt(body, state, ctx), scope)
        frame = {"kind": "switch", "entry": state, "has_default": False}
        ctx.append(frame)
        inner: State = None
        block_scope: dict = {}
        for child in named(body):
            if child.type == "case_statement":
                if child.child_by_field("value") is None:
                    frame["has_default"] = True
                inner = join(inner, state)
                for statement in self.case_body(child):
                    inner = self.stmt(statement, inner, ctx, block_scope)
            else:
                inner = self.stmt(child, inner, ctx, block_scope)
        ctx.pop()
        inner = self.close_scope(inner, block_scope)
        out = join(inner, frame.get("break"))
        if not frame["has_default"]:
            out = join(out, state)
        return self.close_scope(out, scope)

    def try_stmt(self, node: tsast.Node, state: State, ctx: list[dict]) -> State:
        """Walk a try: each handler starts from the join of the entry state and the state after the try block."""
        body = node.child_by_field("body")
        tried = self.block(body, state, ctx) if body is not None else state
        out = tried
        for handler in node.children_of_type("catch_clause"):
            scope: dict = {}
            parameters = handler.child_by_field("parameters")
            names = [name for p in ([] if parameters is None else named(parameters))
                     for name in self.declared_names(p.child_by_field("declarator"))]
            start = self.declare(join(state, tried), scope, names)
            handler_body = handler.child_by_field("body")
            caught = self.block(handler_body, start, ctx) if handler_body is not None else start
            out = join(out, self.close_scope(caught, scope))
        return out

    # ── expressions ──────────────────────────────────────────────────

    def eval(self, node: tsast.Node | None, state: State) -> State:
        """Walk one expression from a state.  The assignments in it refill their keys at its end."""
        if state is None or node is None:
            return None if state is None else state
        state = dict(state)
        pending: list[str] = []
        self.expr(node, state, pending)
        for path in pending:
            restore(state, path)
        return state

    def expr(self, node: tsast.Node, state: dict[str, int], pending: list[str]) -> None:
        """Walk the operands of an expression in source order, reporting each use after move."""
        kind = node.type
        if kind in NOT_EVALUATED:
            return
        if kind == "identifier":
            self.use(node, state, node.text)
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

    def access(self, node: tsast.Node, state: dict[str, int], pending: list[str]) -> None:
        """Walk a member access, a subscript or a unary * or &, and read the path it names."""
        if node.type == "pointer_expression":
            operand = node.child_by_field("argument")
            if operand is None:
                return
            path = self.plain_path(operand) if operator(node) == "*" else None
            if path is not None:
                # A unary * reaches the object that the path holds, which is the key *path.
                self.use(operand, state, "*" + path)
            else:
                self.expr(operand, state, pending)
            return
        if node.type == "subscript_expression":
            argument = node.child_by_field("argument")
            path = self.plain_path(node)
            if path is not None:
                self.use(node, state, path)
                return
            indices = node.child_by_field("indices")
            if indices is not None:
                self.expr(indices, state, pending)
            base = None if argument is None else self.plain_path(argument)
            if base is not None:
                # A subscript that is not an integer literal can name any element,
                # so it reads the key path[*].
                self.use(argument, state, base + "[*]")
            elif argument is not None:
                self.expr(argument, state, pending)
            return
        path = self.plain_path(node)
        if path is not None:
            self.use(node, state, path)
            return
        argument = node.child_by_field("argument")
        if argument is not None:
            self.expr(argument, state, pending)

    def receiver(self, callee: tsast.Node) -> str | None:
        """The path of the object a member call is called on, obj in obj.f() and *p in p->... when it is plain."""
        argument = callee.child_by_field("argument")
        if argument is None:
            return None
        operand = self.deref_operand(argument)
        if operand is not None:
            path = self.plain_path(operand)
            return None if path is None else "*" + path
        return self.plain_path(argument)

    def call(self, node: tsast.Node, state: dict[str, int], pending: list[str]) -> None:
        """Walk a call: a move spends its argument, and the other shapes the rule names act on their keys."""
        function = node.child_by_field("function")
        arguments = node.child_by_field("arguments")
        if self.move_callee(node) or self.rvalue_cast(node):
            path = self.move_argument(arguments)
            if path is not None:
                element = self.element_of_get(node) if self.move_callee(node) else None
                self.spend(state, function, f"{path}[{element}]" if element is not None else path)
                return
            if arguments is not None:
                self.expr(arguments, state, pending)
            return
        name = leaf_name(function)
        if function is not None and function.type != "field_expression":
            if name == "swap" and arguments is not None:
                items = named(arguments)
                paths = [self.plain_path(item) for item in items]
                if len(paths) == 2 and None not in paths:
                    exchange(state, self.canon(paths[0]), self.canon(paths[1]))
                    return
            element = self.get_element(node)
            if element is not None:
                # std::get<I>(t) reads the element key t[I], whatever qualifies get.
                spent = spent_prefix(state, element)
                if spent is not None:
                    self.report(node, element, "use after move", spent, state[spent])
                return
            if name in ("destroy_at", "construct_at") and arguments is not None:
                items = named(arguments)
                target = items[0] if items else None
                if (target is not None and target.type == "pointer_expression" and operator(target) == "&"
                        and (path := self.plain_path(target.child_by_field("argument"))) is not None):
                    pending.append(self.canon(path))
                    for item in items[1:]:
                        self.expr(item, state, pending)
                    return
        if function is not None and function.type == "field_expression":
            receiver = self.receiver(function)
            if receiver is not None:
                if name in REINIT_METHODS:
                    pending.append(self.canon(receiver))
                    if arguments is not None:
                        self.expr(arguments, state, pending)
                    return
                if name == "swap":
                    other = self.move_argument(arguments)
                    if other is not None:
                        exchange(state, self.canon(receiver), other)
                        return
                if name == "at":
                    self.use(function, state, receiver + self.index_segment(arguments))
                    if arguments is not None:
                        self.expr(arguments, state, pending)
                    return
                if name == "value" and arguments is not None and not named(arguments):
                    self.use(function, state, "*" + receiver if not receiver.startswith("*") else receiver)
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

    def assignment(self, node: tsast.Node, state: dict[str, int], pending: list[str]) -> None:
        """Walk an assignment.  A plain = refills its left key at the end of the expression.

        An assignment through a[i] refills one element, which need not be the
        spent one, so it refills nothing.  A compound assignment reads its left side.
        """
        left = node.child_by_field("left")
        right = node.child_by_field("right")
        if left is not None and tsast.operator_of(node) == "=":
            operand = self.deref_operand(left)
            path = self.plain_path(operand) if operand is not None else self.plain_path(left)
            if path is not None:
                pending.append(self.canon("*" + path if operand is not None else path))
            elif (left.type == "subscript_expression" and (argument := left.child_by_field("argument")) is not None
                  and self.plain_path(argument) is not None):
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
        """Each capture of a lambda, with True when it captures by reference."""
        words = captures.tokens()
        out: list[tuple[tsast.Node, bool]] = []
        at = 1
        for child in named(captures):
            own = child.tokens()
            found = next((i for i in range(at, len(words) - len(own) + 1) if words[i:i + len(own)] == own), None)
            if found is None:
                out.append((child, False))
                continue
            out.append((child, words[found - 1] == "&"))
            at = found + len(own)
        return out

    def lambda_expr(self, node: tsast.Node, state: dict[str, int]) -> dict[str, int]:
        """Walk the captures where the lambda appears, and its body as a new body."""
        state = dict(state)
        shadow: list[str] = []
        captures = node.child_by_field("captures")
        if captures is not None:
            for child, by_reference in self.capture_entries(captures):
                if child.type == "lambda_capture_initializer":
                    left = child.child_by_field("left")
                    right = child.child_by_field("right")
                    if left is not None:
                        shadow.append(left.text)
                    if right is not None:
                        state = self.eval(right, state) or {}
                elif child.type == "identifier" and not by_reference:
                    spent = spent_prefix(state, self.canon(child.text))
                    if spent is not None:
                        self.report(child, child.text, "copy capture after move", spent, state[spent])
        declarator = node.child_by_field("declarator")
        parameters = None if declarator is None else declarator.child_by_field("parameters")
        for parameter in ([] if parameters is None else named(parameters)):
            shadow += self.declared_names(parameter.child_by_field("declarator"))
        inner: State = dict(state)
        for name in shadow:
            restore(inner, name)
        body = node.child_by_field("body")
        if body is not None:
            outer, self.assigned_from = self.assigned_from, None
            try:
                self.walk_body(body, inner)
            finally:
                self.assigned_from = outer
        return state


def scan_trees(paths: list[str], display: dict[str, str] | None = None) -> list[Finding]:
    """Parse the files in one kit run and walk each one.  Complexity: linear in the size of the files."""
    findings: list[Finding] = []
    for tree in tsast.parse([Path(p) for p in paths], strict=False):
        key = str(tree.path)
        findings += Walk(tree, (display or {}).get(key, key)).run()
    return findings


def tracked_sources(root: Path) -> list[str] | None:
    """The C++ files under SCAN_ROOTS that git tracks or does not ignore, or None outside a checkout."""
    result = subprocess.run(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"check-use-after-move: git ls-files failed in {root}, so the guard cannot list the tree.  "
              f"Run it in a git checkout, or name the files.\n{result.stderr.strip()}", file=sys.stderr)
        return None
    out = []
    for rel in result.stdout.split("\n"):
        if not rel or not tsast.is_in_cpp_scope(rel):
            continue
        parts = Path(rel).parts
        if any(part == "neg" or part.endswith("_neg") for part in parts[:-1]):
            continue
        if (root / rel).is_file():
            out.append(rel)
    return sorted(out)


def read_allowlist(path: Path) -> tuple[Counter, list[str]]:
    admitted: Counter = Counter()
    errors: list[str] = []
    if not path.exists():
        return admitted, errors
    lines = path.read_text().split("\n")
    for number, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        entry, _, count = line.partition(" x")
        if entry.count(":") < 2:
            errors.append(f"{path.name}:{number}: an entry is path:function:key, got '{line}'")
            continue
        above = number - 2
        while above >= 0 and lines[above].strip() and not lines[above].strip().startswith("#"):
            above -= 1
        if above < 0 or not lines[above].strip().startswith("#"):
            errors.append(f"{path.name}:{number}: an entry needs a comment in its paragraph that gives the reason")
        admitted[entry.strip()] += int(count) if count.strip().isdigit() else 1
    return admitted, errors


def verdict(findings: list[Finding], admitted: Counter, errors: list[str], mode: str) -> int:
    found = Counter(f.allow_key() for f in findings)
    status = 2 if errors else 0
    for error in errors:
        print(f"check-use-after-move: {error}", file=sys.stderr)
    for finding in sorted(findings, key=lambda f: (f.path, f.line)):
        allowed = admitted[finding.allow_key()] >= found[finding.allow_key()]
        if mode == "list" or not allowed:
            where = f"{finding.path}:{finding.line}"
            print(f"{where}: {finding.what} of '{finding.key}' in {finding.function} "
                  f"(moved at line {finding.moved_at}){'' if not allowed else '  [allowlisted]'}")
        if not allowed:
            status = max(status, 1)
    for key, count in admitted.items():
        if found[key] < count:
            print(f"check-use-after-move: STALE entry {key} admits {count} and {found[key]} found; lower it",
                  file=sys.stderr)
            status = 2
    return status


def scan_tree(files: list[str]) -> list[Finding]:
    """Walk the files in chunks, one kit run for each chunk, spread over the workers."""
    chunks = [files[i:i + CHUNK] for i in range(0, len(files), CHUNK)]
    findings: list[Finding] = []
    with ProcessPoolExecutor(max_workers=16) as pool:
        for result in pool.map(scan_trees, chunks):
            findings += result
    return findings


# ── the self-test ─────────────────────────────────────────────────────

SELF_TEST_SOURCE = r'''
#include <utility>
struct Token { Token() = default; Token(Token&&) {} };
void take(Token&&);
void read(Token const&);
bool pick();

// Each must_catch function holds exactly the defect its name says.
void must_catch_second_move(Token t) { take(std::move(t)); take(std::move(t)); }
void must_catch_use(Token t) { take(std::move(t)); read(t); }
void must_catch_member(Token t) { take(std::move(t)); auto n = t.size; }
void must_catch_join(Token t) { if (pick()) { take(std::move(t)); } read(t); }
void must_catch_loop(Token t) { for (int i = 0; i < 2; ++i) take(std::move(t)); }
void must_catch_while(Token t) { while (pick()) { read(t); take(std::move(t)); } }
void must_catch_init_capture(Token t) { auto f = [u = std::move(t)] {}; read(t); }
void must_catch_lambda_body(Token t) { take(std::move(t)); auto f = [&] { read(t); }; }
void must_catch_copy_capture(Token t) { take(std::move(t)); auto f = [t] {}; }
void must_catch_forward(Token t) { take(std::forward<Token>(t)); read(t); }
void must_catch_door(Token t) { lend(std::move(t), [&](auto const&) { take(std::move(t)); }); }
void must_catch_switch(Token t, int k) { switch (k) { case 0: take(std::move(t)); case 1: read(t); break; } }
void must_catch_self_argument(Token t) { consume(std::move(t), t); }
struct Member { Token a; Token b; void must_catch_member_move() { take(std::move(a)); read(a); } };
struct must_catch_ctor {
    Token a; int n;
    must_catch_ctor(Token t) : a{std::move(t)}, n{t.size} {}
};
void must_catch_static_cast(Token t) { take(static_cast<Token&&>(t)); read(t); }
void must_catch_same_element(Tuple t) { take(std::get<1>(std::move(t))); take(std::get<1>(std::move(t))); }
void must_catch_token_moves_twice(Token& token) {
    auto first = Transferable{1, std::move(token)};
    auto second = Transferable{2, std::move(token)};
}
void must_catch_try_loop_exit(Ring& r, Token t) { while (!r.try_push(std::move(t))) {} read(t); }
void must_catch_try_if(Token t) { if (try_take(std::move(t))) { read(t); } }
void must_catch_plain_call_loop(Ring& r, Token t) { while (!r.push(std::move(t))) {} }
void must_catch_swap(Token t, Token u) { take(std::move(t)); std::swap(t, u); read(u); }
void must_catch_repeated_reads(Token t) { take(std::move(t)); read(t); read(t); read(t); }
struct must_catch_move_ctor_member_twice {
    Token a; Token b;
    must_catch_move_ctor_member_twice(must_catch_move_ctor_member_twice&& other)
        : a{std::move(other.a)}, b{std::move(other.a)} {}
};
struct must_catch_converting_ctor {
    Token a; int n;
    must_catch_converting_ctor(Pair&& p) : a{std::move(p)}, n{p.size} {}
};
void must_catch_deref_use(Opt o) { take(std::move(*o)); read(*o); }
void must_catch_deref_arrow(Opt o) { take(std::move(*o)); auto n = o->size; }
void must_catch_deref_value(Opt o) { take(std::move(*o)); read(o.value()); }
void must_catch_deref_paren(Opt o) { take(std::move(*o)); auto n = (*o).size; }
void must_catch_deref_second_move(Opt o) { take(std::move(*o)); take(std::move(*o)); }
#define must_catch_macro_move(x) \
    std::move(x)
void must_catch_using_declaration(Token t) { using std::move; take(move(t)); read(t); }
void must_catch_namespace_alias(Token t) { namespace standard = std; take(standard::move(t)); read(t); }
void must_catch_global_qualified(Token t) { take(::std::move(t)); read(t); }
void must_catch_move_if_noexcept(Token t) { take(std::move_if_noexcept(t)); read(t); }
void must_catch_forward_like(Token t) { take(std::forward_like<Token&&>(t)); read(t); }
void must_catch_parentheses(Token t) { take(std::move((t))); read(t); }
struct This {
    Token a; Token b;
    void must_catch_this_arrow() { take(std::move(a)); read(this->a); }
    void must_catch_this_dereference() { take(std::move(this->a)); read((*this).a); }
    void must_accept_this_other_member() { take(std::move(this->a)); read(this->b); }
    void must_accept_this_reassign() { take(std::move(a)); this->a = Token{}; read(a); }
};
void must_catch_backward_goto(Token t) {
again:
    take(std::move(t));
    if (pick()) goto again;
}
void must_catch_array_element(Token (&ts)[2]) { take(std::move(ts[0])); read(ts[0]); }
void must_catch_array_any_index(Token (&ts)[2], int i) { take(std::move(ts[i])); read(ts[1]); }
void must_catch_array_literal_then_any(Token (&ts)[2], int i) { take(std::move(ts[0])); read(ts[i]); }
void must_catch_refill_through_any_index(Token (&ts)[2], int i) { take(std::move(ts[0])); ts[i] = Token{}; read(ts[0]); }
void must_catch_read_through_at(Arr& ts) { take(std::move(ts[0])); read(ts.at(0)); }
void must_catch_move_through_at(Arr& ts, int i) { take(std::move(ts.at(i))); read(ts[1]); }
void must_catch_range_for(Arr& ts) { take(std::move(ts[0])); for (Token const& t : ts) read(t); }
void must_catch_get_element(Tuple& p) { take(std::move(std::get<0>(p))); read(std::get<0>(p)); }
void must_catch_through_reference(Token& token) { Token& alias = token; take(std::move(alias)); read(token); }
void must_catch_reference_after_move(Token& token) { Token const& alias = token; take(std::move(token)); read(alias); }
void must_catch_through_binding(Holder& h) { auto& [bound] = h; take(std::move(bound)); read(h.token); }
void must_catch_binding_after_move(Holder& h) { auto& [bound] = h; take(std::move(h.token)); read(bound); }
void must_catch_product_after_move(Token t, int n) { take(std::move(t)); auto z = n * t; }
void must_catch_compare_after_move(Token t, int n) { take(std::move(t)); bool less = n > t; }
template <class T> void must_catch_trailing_requires(T t) requires (sizeof(T) > 1) { take(std::move(t)); read(t); }
struct Into {
    Token t;
    Token must_catch_into() && noexcept(true) requires (sizeof(Token) > 0) { take(std::move(t)); return std::move(t); }
};
struct must_catch_member_twice_after_base_assignment : Base {
    Token r;
    must_catch_member_twice_after_base_assignment& operator=(must_catch_member_twice_after_base_assignment&& other) {
        Base::operator=(std::move(other));
        r = std::move(other.r);
        take(std::move(other.r));
        return *this;
    }
};
struct must_catch_whole_after_base_assignment : Base {
    must_catch_whole_after_base_assignment& operator=(must_catch_whole_after_base_assignment&& other) {
        Base::operator=(std::move(other));
        consume(std::move(other));
        return *this;
    }
};
struct must_catch_member_after_unqualified_assignment {
    Token r;
    must_catch_member_after_unqualified_assignment& operator=(must_catch_member_after_unqualified_assignment&& other) {
        operator=(std::move(other));
        read(other.r);
        return *this;
    }
};

// Each must_accept function is correct, and a finding in it is a false alarm.
void must_accept_reassign(Token t) { take(std::move(t)); t = Token{}; read(t); }
void must_accept_branch(Token t) { if (pick()) { take(std::move(t)); } else { read(t); } }
void must_accept_return(Token t) { if (pick()) { take(std::move(t)); return; } read(t); }
void must_accept_fresh_in_loop() { for (int i = 0; i < 2; ++i) { Token t; take(std::move(t)); } }
void must_accept_reassign_in_loop(Token t) { while (pick()) { take(std::move(t)); t = Token{}; } }
void must_accept_rebind(Token t) { t = consume(std::move(t)); read(t); }
void must_accept_door(Token t) { auto back = lend(std::move(t), [](auto const&) {}); read(back); }
void must_accept_shadow(Token t) { take(std::move(t)); { Token t; read(t); } }
void must_accept_sizeof(Token t) { take(std::move(t)); auto n = sizeof(t); }
void must_accept_decltype(Token t) { take(std::move(t)); using T = decltype(t); }
void must_accept_requires(Token t) { take(std::move(t)); if constexpr (requires { t.size; }) {} }
void must_accept_other_member(Member m) { take(std::move(m.a)); read(m.b); }
void must_accept_reset(Token t) { take(std::move(t)); t.reset(); read(t); }
void must_accept_lambda_param(Token t) { take(std::move(t)); auto f = [](Token t) { read(t); }; }
void must_accept_break(Token t) { for (;;) { take(std::move(t)); break; } }
void must_accept_switch(Token t, int k) { switch (k) { case 0: take(std::move(t)); break; case 1: read(t); break; } }
void must_accept_structured(Token t) { take(std::move(t)); auto [t2, u] = pair(); read(t2); }
void must_accept_throw(Token t) { if (pick()) { take(std::move(t)); throw 1; } read(t); }
void must_accept_try(Token t) { try { read(t); } catch (...) { read(t); } take(std::move(t)); }
void must_accept_rebuild(Token t) { take(std::move(t)); std::destroy_at(&t); std::construct_at(&t); read(t); }
void must_accept_other_element(Tuple t) { take(std::get<1>(std::move(t))); read(std::get<0>(t)); }
void must_accept_global_subscript(Table s) { s.cells[::ns::index] = 1; take(std::move(s)); }
void must_accept_try_loop(Ring& r, Token t) { while (!r->try_push(std::move(t))) { read(t); } }
void must_accept_try_do(Ring& r, Token t) { do { read(t); } while (!r.try_push(std::move(t))); }
void must_accept_try_if(Token t) { if (!try_take(std::move(t))) { read(t); } }
void must_accept_try_if_else(Token t) { if (try_take(std::move(t))) { return; } else { read(t); } }
void must_accept_swap(Token t, Token u) { take(std::move(t)); swap(t, u); read(t); }
void must_accept_member_swap(Token t, Token u) { take(std::move(t)); t.swap(u); read(t); }
void must_accept_scalar_cast(int x) { take_int(static_cast<int&&>(x)); read(x); }
void must_accept_deref_refill(Opt o) { take(std::move(*o)); *o = Token{}; read(*o); }
void must_accept_deref_rebind(Opt o) { take(std::move(*o)); o = make(); read(*o); }
void must_accept_deref_reset(Opt o) { take(std::move(*o)); o.reset(); if (o) { read(*o); } }
void must_accept_deref_engaged(Opt o) { take(std::move(*o)); if (o.has_value()) {} }
void must_accept_deref_product(Opt o, int n) { take(std::move(*o)); auto z = n * o; }
struct must_accept_base_move : Base {
    Token r;
    must_accept_base_move(must_accept_base_move&& other) : Base(std::move(other)), r{std::move(other.r)} {}
};
struct must_accept_two_bases : Left, Right {
    must_accept_two_bases(must_accept_two_bases&& other) : Left{std::move(other)}, Right{std::move(other)} {}
};
template <class R>
struct must_accept_base_assignment : Left, Right {
    Token r;
    must_accept_base_assignment& operator=(must_accept_base_assignment&& other) noexcept requires (sizeof(R) > 0) {
        if (this == &other) return *this;
        Left::operator=(std::move(other));
        this->Right::operator=(std::move(other));
        r = std::move(other.r);
        return *this;
    }
};
struct must_accept_out_of_class_assignment : Base { Token r; };
must_accept_out_of_class_assignment& must_accept_out_of_class_assignment::operator=(
        must_accept_out_of_class_assignment&& other) {
    Base::operator=(std::move(other));
    r = std::move(other.r);
    return *this;
}
template <class T> concept Moves = requires(T& t) { take(std::move(t)); take(std::move(t)); };
#define must_accept_macro_plain(x) (x)
#define must_accept_macro_member(x) (x).move(1)
void must_accept_goto_reassign(Token t) {
again:
    take(std::move(t));
    t = Token{};
    if (pick()) goto again;
}
void must_accept_goto_skips_move(Token t) { if (pick()) goto skip; take(std::move(t)); return; skip: read(t); }
void must_accept_array_other_element(Token (&ts)[2]) { take(std::move(ts[0])); read(ts[1]); }
void must_accept_array_refill(Token (&ts)[2]) { take(std::move(ts[0])); ts[0] = Token{}; read(ts[0]); }
void must_accept_at_other_element(Arr& ts) { take(std::move(ts[0])); read(ts.at(1)); }
void must_accept_get_other_element(Tuple& p) { take(std::move(std::get<0>(p))); read(std::get<1>(p)); }
void must_accept_copy_is_not_alias(Token& token) { Token copy = token; take(std::move(copy)); read(token); }
void must_accept_by_value_binding(Holder h) { auto [bound] = h; take(std::move(bound)); read(h.token); }
void must_accept_container_after_element(Vec& v, int i) { auto x = std::move(v[i]); v.erase(v.begin() + i); v.push_back(std::move(x)); }
void must_accept_member_move_call(Mover m, Token t) { m.move(t); read(t); }
void must_accept_algorithm_move(Token* first, Token* last, Token* out) { std::move(first, last, out); read(*first); }
void must_accept_preprocessor_arms(Token t) {
#if defined(ARM_A)
    take(std::move(t));
#else
    take(std::move(t));
#endif
}
void must_accept_reference_capture(Token t) { auto f = [&t] { read(t); }; take(std::move(t)); }
'''


FIXTURE_PREFIXES = ("must_catch_", "must_accept_")


def fixtures(tree: tsast.Tree) -> list[tuple[str, int, int]]:
    """Each fixture of the self-test tree, as (name, first line, last line).

    A fixture is a function, a class or a macro whose name starts with a
    fixture prefix.  A member function defined outside its class takes the
    name of its class, so an out-of-class operator= is a fixture of the class.
    """
    walk = Walk(tree, "")
    out: list[tuple[str, int, int]] = []
    for node in tree.find("function_definition", "struct_specifier", "class_specifier", "preproc_def",
                          "preproc_function_def"):
        if node.type == "function_definition":
            name = walk.function_name(node)
            if not name.startswith(FIXTURE_PREFIXES):
                name = walk.owner_class(node) or ""
        else:
            name_node = node.child_by_field("name")
            name = "" if name_node is None else name_node.text
        if name.startswith(FIXTURE_PREFIXES):
            out.append((name, node.line, node.end[0] + 1))
    return out


def fixture_of(ranges: list[tuple[str, int, int]], line: int) -> str | None:
    """The name of the innermost fixture whose lines hold line, or None."""
    holders = [(last - first, name) for name, first, last in ranges if first <= line <= last]
    return min(holders)[1] if holders else None


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "self_test.cpp"
        source.write_text(SELF_TEST_SOURCE, encoding="utf-8")
        tree = next(tsast.parse([source], strict=False))
        findings = Walk(tree, "self_test.cpp").run()
        ranges = fixtures(tree)
    caught = {name for name, _, _ in ranges if name.startswith("must_catch_")}
    accepted = {name for name, _, _ in ranges if name.startswith("must_accept_")}
    by_fixture = Counter(fixture_of(ranges, f.line) or f.function for f in findings)
    failures = []
    if tree.diagnostic is not None:
        failures.append(f"the self-test source does not parse: {tree.diagnostic}")
    for name in sorted(caught):
        if by_fixture[name] == 0:
            failures.append(f"missed a real use after move in {name}")
    for name in sorted(accepted):
        if by_fixture[name] != 0:
            failures.append(f"false alarm in {name}: "
                            f"{[f.what + ' ' + f.key for f in findings if fixture_of(ranges, f.line) == name]}")
    if by_fixture["Moves"] or by_fixture["<file>"]:
        failures.append("a requires-expression or a declaration outside a body was read as a use")
    if by_fixture["must_catch_repeated_reads"] != 1:
        failures.append("three reads after one move gave "
                        f"{by_fixture['must_catch_repeated_reads']} findings, and one move is one finding")
    if len(caught) < 50 or len(accepted) < 50:
        failures.append(f"the fixture names were not read from the tree ({len(caught)} caught, {len(accepted)} accepted)")
    # The allowlist admits a finding, and an entry above its count is stale.
    with tempfile.TemporaryDirectory() as tmp:
        allow = Path(tmp) / "allow.txt"
        allow.write_text("# a reason\nself_test.cpp:must_catch_use:t\n# a reason\nself_test.cpp:nowhere:x\n")
        admitted, errors = read_allowlist(allow)
        sub = [f for f in findings if f.function == "must_catch_use"]
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            stale_status = verdict(sub, admitted, errors, "check")
        if stale_status != 2 or "STALE entry self_test.cpp:nowhere:x" not in report.getvalue():
            failures.append("a stale allowlist entry did not exit 2 with its name")
        allow.write_text("# a reason\nself_test.cpp:must_catch_use:t\n")
        admitted, errors = read_allowlist(allow)
        if verdict(sub, admitted, errors, "check") != 0:
            failures.append("an allowlisted finding did not pass")
        allow.write_text("self_test.cpp:must_catch_use:t\n")
        admitted, errors = read_allowlist(allow)
        if not errors:
            failures.append("an entry with no reason above it was accepted")
        # A file the kit cannot parse is a finding, not a silent pass.
        broken = Path(tmp) / "broken.cpp"
        broken.write_text("void f( { take(std::move(t)); read(t); \n", encoding="utf-8")
        broken_tree = next(tsast.parse([broken], strict=False))
        if not any(f.key == "<parse-error>" for f in Walk(broken_tree, "broken.cpp").run()):
            failures.append("a file the kit cannot parse passed with no finding")
    for failure in failures:
        print(f"check-use-after-move: SELF-TEST FAILED: {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-use-after-move: self-test passed ({len(caught)} caught, {len(accepted)} accepted)")
    return 0


def main(argv: list[str]) -> int:
    root = REPO_ROOT
    try:
        if argv[:1] == ["--self-test"]:
            return self_test()
        mode = "list" if argv[:1] == ["--list"] else "check"
        rest = argv[1:] if mode == "list" else argv
        if rest and rest[0].startswith("-"):
            print(__doc__, file=sys.stderr)
            return 2
        files = rest or tracked_sources(root)
        if files is None:
            return 2
        findings = scan_tree(files)
    except tsast.KitMissing as exc:
        print(f"check-use-after-move: {exc}", file=sys.stderr)
        return 3
    admitted, errors = read_allowlist(root / ALLOWLIST)
    if rest:
        admitted = Counter({k: v for k, v in admitted.items() if k.split(":", 1)[0] in set(rest)})
    status = verdict(findings, admitted, errors, mode)
    if status == 0:
        print(f"check-use-after-move: {len(files)} files, {len(findings)} findings, each one allowlisted")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
