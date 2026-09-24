#!/usr/bin/env python3
"""check-use-after-move.py: no use of a local after std::move, on any path.

The rule
--------
After `std::move(x)`, `std::forward<T>(x)`, `std::move_if_noexcept(x)`,
`std::forward_like<T>(x)` or `static_cast<T&&>(x)`, the name `x` is spent.
The qualifier of the callee does not matter, so `move(x)` after a
using-declaration and `alias::move(x)` after a namespace alias are moves too.
A macro may not move or forward at all: the walk reads the source with the
preprocessor lines removed, so each use of such a macro would hide a move.  A later read of `x`, a second move of `x`, or a member
of `x` is a use after move, unless an assignment to `x` comes first.  The
guard checks each path through a function body: a move in one branch of
an `if` does not spend the name in the other branch, a move that is
followed by a return spends nothing after it, and a move inside a loop
spends the name on the next iteration unless the loop declares or assigns
the name again.  The guard reports the first use after each move, because
the next uses of the same move are the same defect.

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
The guard reads the tokens of each file after scripts/cxx_lex.py blanks
the comments and the literals.  It finds each function body, each lambda
body and each member-initializer list, and it walks the statements of a
body as a control-flow tree: blocks, if and else, switch and case, the
loops, try and catch, return, throw, break, continue and goto.  The
state at each point is the set of names that may be spent.  Two paths
join by union, so a name that one path spends stays spent after the join.

A key is a name or a member path (a.b, a->b).  A move of a spends a and
each member path under it.  A use of a.c after a move of a.b is not a use
after move.  Parentheses around a name do not change its key, and this->m
and (*this).m key as m.  A subscript by an integer literal is part of the
key, so a[0] and a[1] are two keys.  A subscript by any other expression
keys as a[*], which matches every element key of a.  A label that a later
goto of its block targets is a loop head, so the walk passes the statements
from it twice, as it does for a loop.  An assignment to a key, or reset, clear, emplace or assign
on it, restores it.  A declaration restores its name in the scope that
declares it.  Uses in unevaluated operands (sizeof, alignof, decltype,
noexcept, typeid, requires, static_assert) are not uses.

Four shapes leave a name whole, and the self-test holds each one:
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
- A move inside a function that takes T&.  The call does not say
  std::move, so the name stays live at the call site.  A call that passes
  std::move(x) to a T&& parameter spends x, although the callee can leave
  x whole.  An allowlist entry says why when it does.
- A use through a pointer or a reference to a spent object.
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

Negative-compile fixtures (a test directory named neg or *_neg) are out of
scope, because each one must fail to compile.

The allowlist
-------------
scripts/use-after-move-allowlist.txt holds reviewed findings.  An entry
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
import re
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cxx_lex import blank, line_of, splice  # noqa: E402

SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl", ".ipp", ".tpp"}
SCAN_ROOTS = ("include", "src", "test", "vessel", "tools")
ALLOWLIST = "scripts/use-after-move-allowlist.txt"

TOKEN = re.compile(r"""
    (?P<id>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<num>\.?[0-9](?:[eEpP][+-]|['\w.])*)
  | (?P<op>>>=|<<=|<=>|->\*|\.\.\.|::|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^=|\^\^|\[:(?!:)|:\]|\.\*|[{}()\[\];,.<>=!~?:&|^+\-*/%#@$\\])
""", re.X)

OPEN = {"(": ")", "[": "]", "{": "}", "[:": ":]"}
CLOSE = {v: k for k, v in OPEN.items()}

KEYWORDS = frozenset("""
    alignas alignof and asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class
    co_await co_return co_yield compl concept const consteval constexpr constinit const_cast continue decltype
    default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline
    int long mutable namespace new noexcept not nullptr operator or private protected public register
    reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template
    this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t
    while xor pre post contract_assert final override
""".split())
UNEVALUATED = frozenset({"sizeof", "alignof", "decltype", "noexcept", "typeid", "static_assert", "alignas"})
NOT_A_TYPE = frozenset({"return", "delete", "throw", "co_return", "co_await", "co_yield", "new", "case",
                        "goto", "else", "do", "sizeof", "not", "and", "or"})
REINIT_METHODS = frozenset({"reset", "clear", "emplace", "assign"})
# The last component of a callee that gives an rvalue of its argument.  The
# qualifier does not matter: std::move, ::std::move, a namespace alias and a
# using-declaration all reach the same function.
MOVE_CALLEES = frozenset({"move", "forward", "move_if_noexcept", "forward_like"})
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)")
CLASS_KEYS = frozenset({"class", "struct", "union"})
# The words of a scalar type.  static_cast<int&&>(x) leaves x whole,
# because a scalar has no moved-from state.
SCALAR_TYPE_WORDS = frozenset("""
    bool char char8_t char16_t char32_t wchar_t short int long signed unsigned float double const volatile
    std :: byte size_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t
    uint64_t
""".split())


@dataclass
class Tok:
    kind: str
    text: str
    off: int


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


def tokenize(text: str) -> list[Tok]:
    """The tokens of blanked text, with preprocessor lines removed."""
    lines = text.split("\n")
    pos = 0
    kept: list[str] = []
    for line in lines:
        kept.append(" " * len(line) if line.lstrip().startswith("#") else line)
        pos += len(line) + 1
    clean = "\n".join(kept)
    toks: list[Tok] = []
    for match in TOKEN.finditer(clean):
        kind = match.lastgroup or "op"
        toks.append(Tok(kind, match.group(0), match.start()))
    return toks


def match_brackets(toks: list[Tok]) -> list[int] | None:
    """The index of the partner of each bracket, or None when the file does not balance."""
    partner = [-1] * len(toks)
    stack: list[int] = []
    for i, tok in enumerate(toks):
        if tok.text in OPEN:
            stack.append(i)
        elif tok.text in CLOSE:
            if not stack or toks[stack[-1]].text != CLOSE[tok.text]:
                return None
            j = stack.pop()
            partner[i] = j
            partner[j] = i
    return partner if not stack else None


State = dict[str, int] | None  # spent key -> line of its move; None when no path reaches the point


def join(a: State, b: State) -> State:
    if a is None:
        return None if b is None else dict(b)
    if b is None:
        return dict(a)
    out = dict(a)
    for key, line in b.items():
        out[key] = min(line, out.get(key, line))
    return out


SEGMENT = re.compile(r"\*?[A-Za-z_]\w*|\.\w+|->\w+|\[[^\]]*\]")


def reaches(spent: str, key: str) -> bool:
    """Whether key names the spent key or a part of it.

    A key is a list of segments: a name, then members (.m, ->m) and subscripts
    ([0] for an integer literal, [*] for any other index).  Two subscripts match
    when they are equal or when either one is [*], because an index that is not
    a literal can name any element.
    """
    spent_parts, key_parts = SEGMENT.findall(spent), SEGMENT.findall(key)
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
    a member of *p and p->m reach into it, and p itself does not.
    """
    for spent in state:
        if reaches(spent, key):
            return spent
        if spent.startswith("*") and key.startswith(spent[1:] + "->"):
            return spent
    return None


def restore(state: dict[str, int], key: str) -> None:
    """Drop the spent keys that an assignment to key refills.

    An assignment to p also refills *p, because p then holds or points at a new value.
    """
    def refilled(spent: str) -> bool:
        base = spent[1:] if spent.startswith("*") and not key.startswith("*") else spent
        return base == key or base.startswith((key + ".", key + "->", key + "["))

    for spent in [s for s in state if refilled(s)]:
        del state[spent]


def exchange(state: dict[str, int], left: str, right: str) -> None:
    """Swap the spent keys under left with the spent keys under right, as swap(left, right) swaps the values."""
    moved: dict[str, int] = {}
    for spent in list(state):
        for source, target in ((left, right), (right, left)):
            if spent == source or spent.startswith((source + ".", source + "->", source + "[")):
                moved[target + spent[len(source):]] = state.pop(spent)
                break
    state.update(moved)


class Body:
    """The walk of one file: every function body, lambda body and initializer list in it."""

    def __init__(self, path: str, text: str) -> None:
        self.path = path
        joined, self.joins = splice(text)
        self.text, _ = blank(joined, blank_literals=True)
        self.toks = tokenize(self.text)
        partner = match_brackets(self.toks)
        self.balanced = partner is not None
        self.partner = partner or []
        self.findings: list[Finding] = []
        self.seen: set[tuple[str, str, int]] = set()
        self.function = "<file>"
        # True while a condition is walked for the path where its try_ call
        # failed: a move in the call then leaves the name whole.
        self.hold_moves = False
        # The join of the states at each goto of the current body, by label.
        self.goto_states: dict[str, State] = {}

    # ── helpers ──────────────────────────────────────────────────────

    def line(self, i: int) -> int:
        return line_of(self.text, self.joins, self.toks[i].off)

    def t(self, i: int) -> str:
        return self.toks[i].text if 0 <= i < len(self.toks) else ""

    def report(self, i: int, key: str, what: str, spent: str, moved_at: int) -> None:
        """Record the first use after one move.  Later uses of the same move are the same defect."""
        mark = (self.function, spent, moved_at)
        if mark in self.seen:
            return
        self.seen.add(mark)
        self.findings.append(Finding(self.path, self.line(i), self.function, key, what, moved_at))

    def spend(self, state: dict[str, int], k: int, path: str) -> None:
        """Spend path at the move at token k, and report a move of a path that a move spent before."""
        spent = spent_prefix(state, path)
        if spent is not None:
            self.report(k, path, "second move", spent, state[spent])
        if not self.hold_moves:
            state[path] = self.line(k)

    def skip_angle(self, i: int, hi: int) -> int:
        """The index after a template argument list that opens at i."""
        depth = 0
        k = i
        while k < hi:
            text = self.t(k)
            if text in OPEN:
                k = self.partner[k] + 1
                continue
            if text == "<":
                depth += 1
            elif text == ">":
                depth -= 1
            elif text == ">>":
                depth -= 2
            elif text in (";", "{", "}"):
                return k
            k += 1
            if depth <= 0:
                return k
        return k

    # ── the declaration scope ────────────────────────────────────────

    def scan_scope(self, lo: int, hi: int) -> None:
        """Find the bodies in a namespace or a class body."""
        start = lo
        init_colon = -1
        k = lo
        while k < hi:
            text = self.t(k)
            if text == ";":
                start, init_colon = k + 1, -1
                k += 1
                continue
            if text in ("public", "private", "protected") and self.t(k + 1) == ":":
                start, init_colon = k + 2, -1
                k += 2
                continue
            if text == ":" and init_colon < 0 and self.decl_has_params(start, k):
                init_colon = k
            if text in ("(", "["):
                k = self.partner[k] + 1
                continue
            if text != "{":
                k += 1
                continue
            end = self.partner[k]
            kind = self.classify(start, k, init_colon)
            if kind == "namespace" or kind == "class":
                self.scan_scope(k + 1, end)
                start, init_colon = end + 1, -1
            elif kind == "skip":
                start, init_colon = end + 1, -1
            elif kind == "meminit":
                pass
            elif kind == "function":
                self.function = self.function_name(start, k)
                state: State = {}
                if init_colon >= 0:
                    state = self.member_inits(start, init_colon, k, state)
                self.walk_body(k, end, state)
                self.function = "<file>"
                start, init_colon = end + 1, -1
            else:
                self.function = self.function_name(start, k)
                self.eval(start, end + 1, {}, [])
                self.function = "<file>"
                start, init_colon = end + 1, -1
            k = end + 1

    def member_inits(self, start: int, colon: int, brace: int, state: State) -> State:
        """Walk the member-initializer list of the constructor declared from start to brace.

        In a move constructor C(C&& other), an initializer whose whole argument is a move of
        other can only be a base: a member of type C cannot exist inside C.  The base moves its
        own subobject, so the next initializers can still read other.member.  The body sees
        other as spent, as after any other move.
        """
        source = self.move_source(start, colon)
        if source is None:
            return self.eval(colon + 1, brace, state, [])
        base_moved_at = None
        for lo, hi in self.initializers(colon + 1, brace):
            if self.moves_whole(lo, hi, source):
                self.eval(lo, hi, state, [])
                base_moved_at = base_moved_at or self.line(lo)
            else:
                state = self.eval(lo, hi, state, [])
        if base_moved_at is not None and state is not None:
            state = {**state, source: base_moved_at}
        return state

    def move_source(self, start: int, colon: int) -> str | None:
        """The parameter name when the declaration from start to colon is a move constructor C(C&& p)."""
        name = self.function_name(start, colon)
        for k in range(start, colon):
            if self.t(k) != name or self.t(k + 1) != "(":
                continue
            close = self.partner[k + 1]
            i = k + 2
            if self.t(i) != name:
                return None
            i += 1
            if self.t(i) == "<":
                i = self.skip_angle(i, close)
            if self.t(i) != "&&" or self.toks[i + 1].kind != "id" or i + 2 != close:
                return None
            return self.t(i + 1)
        return None

    def initializers(self, lo: int, hi: int) -> list[tuple[int, int]]:
        """The extent of each initializer in a member-initializer list."""
        out: list[tuple[int, int]] = []
        k = lo
        while k < hi:
            first = k
            while k < hi and self.t(k) not in ("(", "{"):
                k = self.skip_angle(k, hi) if self.t(k) == "<" else k + 1
            if k >= hi:
                break
            end = self.partner[k] + 1
            if self.t(end) == "...":
                end += 1
            out.append((first, end))
            k = end + 1 if self.t(end) == "," else end
        return out

    def moves_whole(self, lo: int, hi: int, source: str) -> bool:
        """Whether the argument of the initializer from lo to hi is exactly a move of source."""
        close = hi - 2 if self.t(hi - 1) == "..." else hi - 1
        m = self.partner[close] + 1
        callee = m
        while self.t(callee + 1) == "::" or self.t(callee) == "::":
            callee += 1
        if self.move_callee(callee, close) >= 0:
            m = self.move_callee(callee, close)
        elif self.t(m) == "static_cast" and self.t(m + 1) == "<":
            m = self.skip_angle(m + 1, close)
        else:
            return False
        return self.t(m) == "(" and self.t(m + 1) == source and m + 2 == close - 1 and self.t(m + 2) == ")"

    def decl_has_params(self, lo: int, hi: int) -> bool:
        return any(self.t(i) == "(" for i in range(lo, hi))

    def classify(self, lo: int, brace: int, init_colon: int) -> str:
        words = [self.t(i) for i in range(lo, brace)]
        before = self.t(brace - 1)
        if "namespace" in words or (words[:1] == ["extern"] and len(words) <= 2):
            return "namespace"
        if "concept" in words or before == "requires" or self.preceded_by_requires(lo, brace):
            return "skip"
        if "enum" in words and "(" not in words:
            return "skip"
        head = self.strip_template_head(lo, brace)
        head_top = self.top_level(head, brace)
        if before != ")" and any(word in CLASS_KEYS for word in head_top) and "=" not in head_top:
            return "class"
        if init_colon >= 0 and (self.toks[brace - 1].kind == "id" or before == ">") and before not in KEYWORDS:
            return "meminit"
        if "=" in head_top:
            return "initializer"
        if "(" in words:
            return "function"
        return "initializer"

    def preceded_by_requires(self, lo: int, brace: int) -> bool:
        k = brace - 1
        if self.t(k) == ")":
            k = self.partner[k] - 1
            return self.t(k) == "requires"
        return False

    def strip_template_head(self, lo: int, hi: int) -> int:
        k = lo
        while k < hi and self.t(k) == "template" and self.t(k + 1) == "<":
            k = self.skip_angle(k + 1, hi)
        return k

    def top_level(self, lo: int, hi: int) -> list[str]:
        out: list[str] = []
        k = lo
        while k < hi:
            text = self.t(k)
            if text in OPEN:
                k = self.partner[k] + 1
                continue
            out.append(text)
            k += 1
        return out

    def function_name(self, lo: int, hi: int) -> str:
        top = []
        k = lo
        while k < hi:
            if self.t(k) in OPEN:
                k = self.partner[k] + 1
                continue
            top.append(k)
            k += 1
        for i in top:
            if self.t(i) == "=" and i > lo and self.toks[i - 1].kind == "id":
                return self.t(i - 1)
        k = lo
        while k < hi:
            if self.t(k) == "operator":
                return "operator" + self.t(k + 1)
            if self.t(k) == "(":
                j = k - 1
                name = self.t(j)
                if self.toks[j].kind == "id" and name not in KEYWORDS:
                    return ("~" + name) if self.t(j - 1) == "~" else name
                if name == ">":
                    depth, m = 0, j
                    while m > lo:
                        if self.t(m) == ">":
                            depth += 1
                        elif self.t(m) == "<":
                            depth -= 1
                            if depth == 0:
                                break
                        m -= 1
                    if self.toks[m - 1].kind == "id":
                        return self.t(m - 1)
                k = self.partner[k] + 1
                continue
            if self.t(k) in OPEN:
                k = self.partner[k] + 1
                continue
            k += 1
        return "<anonymous>"

    # ── statements ───────────────────────────────────────────────────

    def walk_body(self, open_brace: int, close_brace: int, state: State) -> State:
        """Walk one function or lambda body.  Its labels and gotos are its own."""
        ctx: list[dict] = []
        outer, self.goto_states = self.goto_states, {}
        try:
            return self.block(open_brace, close_brace, state, ctx)
        finally:
            self.goto_states = outer

    def label_at(self, k: int) -> str | None:
        """The name of the label that the statement at k declares, or None."""
        if self.toks[k].kind == "id" and self.t(k) not in KEYWORDS and self.t(k + 1) == ":" and self.t(k + 2) != ":":
            return self.t(k)
        return None

    def goto_after(self, label: str, lo: int, hi: int) -> bool:
        """Whether a goto to label appears between lo and hi.  Complexity: O(hi - lo)."""
        return any(self.t(i) == "goto" and self.t(i + 1) == label for i in range(lo, hi))

    def block(self, open_brace: int, close_brace: int, state: State, ctx: list[dict]) -> State:
        """Walk a block.  A label that a later goto of the block targets is a loop head.

        As for a loop, the statements from the first such label on are walked twice: the
        second walk enters the label with the join of the fallthrough state and the state
        at each goto to it.
        """
        scope: dict[str, int | None] = {}
        head: tuple[int, State, dict] | None = None
        k = open_brace + 1
        while k < close_brace:
            label = self.label_at(k)
            if head is None and label is not None and self.goto_after(label, k, close_brace):
                head = (k, state, dict(scope))
            k, state = self.stmt(k, close_brace, state, ctx, scope)
        if head is not None:
            k, state, scope = head
            while k < close_brace:
                k, state = self.stmt(k, close_brace, state, ctx, scope)
        return self.close_scope(state, scope)

    def close_scope(self, state: State, scope: dict[str, int | None]) -> State:
        if state is None:
            return None
        out = dict(state)
        for name, outer in scope.items():
            restore(out, name)
            if outer is not None:
                out[name] = outer
        return out

    def declare(self, state: State, scope: dict[str, int | None], names: list[str]) -> State:
        if state is None:
            return None
        out = dict(state)
        for name in names:
            if name not in scope:
                scope[name] = out.get(name)
            restore(out, name)
        return out

    def stmt_end(self, k: int, hi: int) -> int:
        """The index of the ; that ends the statement at k, or hi."""
        while k < hi:
            text = self.t(k)
            if text == ";":
                return k
            if text in OPEN:
                k = self.partner[k] + 1
                continue
            if text == "}":
                return k
            k += 1
        return hi

    def stmt(self, k: int, hi: int, state: State, ctx: list[dict], scope: dict) -> tuple[int, State]:
        text = self.t(k)
        if text == ";":
            return k + 1, state
        if text == "[" and self.t(k + 1) == "[":
            return self.partner[k] + 1, state
        if text == "{":
            end = self.partner[k]
            return end + 1, self.block(k, end, state, ctx)
        if text == "if":
            return self.if_stmt(k, hi, state, ctx, scope)
        if text in ("while", "for") or (text == "template" and self.t(k + 1) == "for"):
            return self.loop_stmt(k, hi, state, ctx, scope)
        if text == "do":
            return self.do_stmt(k, hi, state, ctx, scope)
        if text == "switch":
            return self.switch_stmt(k, hi, state, ctx, scope)
        if text == "try":
            return self.try_stmt(k, hi, state, ctx, scope)
        if text in ("return", "co_return", "throw"):
            end = self.stmt_end(k, hi)
            self.eval(k + 1, end, state, [])
            return end + 1, None
        if text in ("break", "continue"):
            end = self.stmt_end(k, hi)
            for frame in reversed(ctx):
                if text == "continue" and frame["kind"] != "loop":
                    continue
                frame["break" if text == "break" else "continue"] = join(frame.get(
                    "break" if text == "break" else "continue"), state)
                break
            return end + 1, None
        if text == "goto":
            label = self.t(k + 1)
            self.goto_states[label] = join(self.goto_states.get(label), state)
            return self.stmt_end(k, hi) + 1, None
        if text in ("case", "default") and ctx and any(f["kind"] == "switch" for f in ctx):
            m = k + 1
            while m < hi and not (self.t(m) == ":" and self.t(m + 1) != ":"):
                m = self.partner[m] + 1 if self.t(m) in OPEN else m + 1
            frame = next(f for f in reversed(ctx) if f["kind"] == "switch")
            if text == "default":
                frame["has_default"] = True
            return m + 1, join(state, frame["entry"])
        if self.label_at(k) is not None:
            # A label joins the state of each goto to it that the walk has passed.
            # A label that no walked goto reaches keeps a live state, since a goto
            # later in the body can still jump to it.
            entered = join(state, self.goto_states.get(text))
            return k + 2, entered if entered is not None else {}
        if text in ("using", "typedef", "static_assert", "namespace", "asm", "friend"):
            return self.stmt_end(k, hi) + 1, state
        if text in CLASS_KEYS or text == "enum":
            end = self.stmt_end(k, hi)
            m = k
            while m < end:
                if self.t(m) == "{":
                    if text != "enum":
                        saved = self.function
                        self.scan_scope(m + 1, self.partner[m])
                        self.function = saved
                    m = self.partner[m] + 1
                    continue
                m = self.partner[m] + 1 if self.t(m) in OPEN else m + 1
            return end + 1, state
        end = self.stmt_end(k, hi)
        names = self.declared_names(k, end)
        state = self.eval(k, end, state, names)
        state = self.declare(state, scope, [n for n, _ in names])
        return end + 1, state

    def declared_names(self, lo: int, hi: int) -> list[tuple[str, int]]:
        """The names a declaration between lo and hi introduces, with the index of each."""
        out: list[tuple[str, int]] = []
        k = lo
        while k < hi:
            text = self.t(k)
            if text == "[" and k > lo and (self.t(k - 1) in ("auto", "&", "&&")) and self.t(k + 1) != "[":
                end = self.partner[k]
                out += [(self.t(m), m) for m in range(k + 1, end) if self.toks[m].kind == "id"]
                k = end + 1
                continue
            if text in OPEN:
                k = self.partner[k] + 1
                continue
            if (self.toks[k].kind == "id" and text not in KEYWORDS and k > lo
                    and self.t(k + 1) in ("=", "{", "(", ";", ",", "[", ":") and self.t(k + 2) != ":"
                    and self.t(k + 1) != "::"):
                prev = self.toks[k - 1]
                if ((prev.kind == "id" and prev.text not in NOT_A_TYPE) or prev.text in (">", ">>", "*", "&",
                                                                                       "&&")):
                    if not (prev.kind == "id" and self.t(k - 2) in (".", "->")):
                        out.append((text, k))
            k += 1
        return out

    def header(self, open_paren: int, state: State, scope: dict) -> State:
        """Evaluate a parenthesised header that can declare names, such as if (T x = f(); x)."""
        close = self.partner[open_paren]
        k = open_paren + 1
        while k < close:
            end = k
            while end < close and self.t(end) != ";":
                end = self.partner[end] + 1 if self.t(end) in OPEN else end + 1
            names = self.declared_names(k, end)
            state = self.eval(k, end, state, names)
            state = self.declare(state, scope, [n for n, _ in names])
            k = end + 1
        return state

    def sub_stmt(self, k: int, hi: int, state: State, ctx: list[dict]) -> tuple[int, State]:
        scope: dict = {}
        k, state = self.stmt(k, hi, state, ctx, scope)
        return k, self.close_scope(state, scope)

    def if_stmt(self, k: int, hi: int, state: State, ctx: list[dict], outer: dict) -> tuple[int, State]:
        scope: dict = {}
        k += 1
        if self.t(k) == "constexpr":
            k += 1
        if self.t(k) == "!":
            k += 1
        if self.t(k) == "consteval":
            k += 1
            when_true = when_false = state
        elif self.try_call(k + 1, self.partner[k]) is not None:
            when_true, when_false = self.condition(k + 1, self.partner[k], state)
            k = self.partner[k] + 1
        else:
            when_true = when_false = self.header(k, state, scope)
            k = self.partner[k] + 1
        k, then = self.sub_stmt(k, hi, when_true, ctx)
        other = when_false
        if self.t(k) == "else":
            k, other = self.sub_stmt(k + 1, hi, when_false, ctx)
        return k, self.close_scope(join(then, other), scope)

    def loop_stmt(self, k: int, hi: int, state: State, ctx: list[dict], outer: dict) -> tuple[int, State]:
        scope: dict = {}
        if self.t(k) == "template":
            k += 1
        keyword = self.t(k)
        paren = k + 1
        close = self.partner[paren]
        parts: list[tuple[int, int]] = []
        m = paren + 1
        while m <= close:
            end = m
            while end < close and self.t(end) != ";":
                end = self.partner[end] + 1 if self.t(end) in OPEN else end + 1
            parts.append((m, end))
            m = end + 1
        colon = -1
        if keyword == "for" and len(parts) == 1:
            m = paren + 1
            while m < close:
                if self.t(m) == ":" and self.t(m + 1) != ":":
                    colon = m
                    break
                m = self.partner[m] + 1 if self.t(m) in OPEN else m + 1
        body = close + 1
        body_end, _ = self.sub_stmt_extent(body, hi)
        loop_names: list[str] = []
        if colon >= 0:
            state = self.eval(colon + 1, close, state, [])
            loop_names = [n for n, _ in self.declared_names(paren + 1, colon)] or \
                [self.t(i) for i in range(paren + 1, colon) if self.toks[i].kind == "id"][-1:]
            cond_lo = cond_hi = incr_lo = incr_hi = -1
        elif keyword == "for" and len(parts) == 3:
            names = self.declared_names(*parts[0])
            state = self.eval(parts[0][0], parts[0][1], state, names)
            state = self.declare(state, scope, [n for n, _ in names])
            cond_lo, cond_hi = parts[1]
            incr_lo, incr_hi = parts[2]
        else:
            names = self.declared_names(paren + 1, close)
            cond_lo, cond_hi = paren + 1, close
            incr_lo = incr_hi = -1
            loop_names = [n for n, _ in names]
        entry = state
        exit_state: State = None
        head = entry
        for _ in range(2):
            leave = head
            if cond_lo >= 0:
                head, leave = self.condition(cond_lo, cond_hi, head)
            exit_state = join(exit_state, leave)
            frame = {"kind": "loop"}
            ctx.append(frame)
            inner = self.declare(head, scope, loop_names) if loop_names else head
            _, tail = self.sub_stmt(body, hi, inner, ctx)
            ctx.pop()
            tail = join(tail, frame.get("continue"))
            if incr_lo >= 0:
                tail = self.eval(incr_lo, incr_hi, tail, [])
            exit_state = join(exit_state, frame.get("break"))
            head = join(entry, tail)
        return body_end, self.close_scope(exit_state, scope)

    def sub_stmt_extent(self, k: int, hi: int) -> tuple[int, None]:
        """The index after the statement that starts at k, found without walking it."""
        text = self.t(k)
        if text == "{":
            return self.partner[k] + 1, None
        if text == "[" and self.t(k + 1) == "[":
            return self.sub_stmt_extent(self.partner[k] + 1, hi)
        if text == "if":
            m = k + 1
            while self.t(m) in ("constexpr", "!", "consteval"):
                m += 1
            if self.t(m) == "(":
                m = self.partner[m] + 1
            m, _ = self.sub_stmt_extent(m, hi)
            if self.t(m) == "else":
                m, _ = self.sub_stmt_extent(m + 1, hi)
            return m, None
        if text in ("while", "for", "switch") or (text == "template" and self.t(k + 1) == "for"):
            m = k + 1 if text != "template" else k + 2
            return self.sub_stmt_extent(self.partner[m] + 1, hi)
        if text == "do":
            m, _ = self.sub_stmt_extent(k + 1, hi)
            return self.stmt_end(m, hi) + 1, None
        if text == "try":
            m = self.partner[k + 1] + 1
            while self.t(m) == "catch":
                m = self.partner[self.partner[m + 1] + 1] + 1
            return m, None
        return self.stmt_end(k, hi) + 1, None

    def do_stmt(self, k: int, hi: int, state: State, ctx: list[dict], outer: dict) -> tuple[int, State]:
        body = k + 1
        body_end, _ = self.sub_stmt_extent(body, hi)
        cond = body_end + 1
        entry = state
        exit_state: State = None
        head = entry
        for _ in range(2):
            frame = {"kind": "loop"}
            ctx.append(frame)
            _, tail = self.sub_stmt(body, hi, head, ctx)
            ctx.pop()
            tail = join(tail, frame.get("continue"))
            tail, leave = self.condition(cond + 1, self.partner[cond], tail)
            exit_state = join(join(exit_state, leave), frame.get("break"))
            head = join(entry, tail)
        return self.stmt_end(body_end, hi) + 1, exit_state

    def switch_stmt(self, k: int, hi: int, state: State, ctx: list[dict], outer: dict) -> tuple[int, State]:
        scope: dict = {}
        state = self.header(k + 1, state, scope)
        body = self.partner[k + 1] + 1
        if self.t(body) != "{":
            return self.sub_stmt(body, hi, state, ctx)[0], self.close_scope(state, scope)
        end = self.partner[body]
        frame = {"kind": "switch", "entry": state, "has_default": False}
        ctx.append(frame)
        inner: State = None
        block_scope: dict = {}
        m = body + 1
        while m < end:
            m, inner = self.stmt(m, end, inner, ctx, block_scope)
        ctx.pop()
        inner = self.close_scope(inner, block_scope)
        out = join(inner, frame.get("break"))
        if not frame["has_default"]:
            out = join(out, state)
        return end + 1, self.close_scope(out, scope)

    def try_stmt(self, k: int, hi: int, state: State, ctx: list[dict], outer: dict) -> tuple[int, State]:
        body = k + 1
        end = self.partner[body]
        tried = self.block(body, end, state, ctx)
        out = tried
        m = end + 1
        while self.t(m) == "catch":
            paren = m + 1
            handler = self.partner[paren] + 1
            scope: dict = {}
            names = [n for n, _ in self.declared_names(paren + 1, self.partner[paren])]
            start = self.declare(join(state, tried), scope, names)
            caught = self.block(handler, self.partner[handler], start, ctx)
            out = join(out, self.close_scope(caught, scope))
            m = self.partner[handler] + 1
        return m, out

    # ── expressions ──────────────────────────────────────────────────

    def path_at(self, k: int, hi: int) -> tuple[str, int]:
        """The member path that starts at k, and the index after it.

        A subscript by an integer literal is part of the path, so a[0] and a[1] are two
        keys.  A subscript by any other expression ends the path at the array, so a[i]
        keys as the whole array a.
        """
        parts = [self.t(k)]
        m = k + 1
        while m + 1 < hi:
            if self.t(m) in (".", "->") and self.toks[m + 1].kind == "id" and self.t(m + 2) != "(":
                parts.append(self.t(m) + self.t(m + 1))
                m += 2
            elif self.t(m) == "[" and self.partner[m] == m + 2 and self.toks[m + 1].kind == "num":
                parts.append(f"[{self.t(m + 1)}]")
                m += 3
            else:
                break
        return "".join(parts), m

    def member_of_this(self, k: int) -> int:
        """The index of the member name after this-> or (*this). at k, or -1."""
        if self.t(k) == "this" and self.t(k + 1) == "->" and self.toks[k + 2].kind == "id":
            return k + 2
        if (self.t(k) == "(" and self.t(k + 1) == "*" and self.t(k + 2) == "this" and self.t(k + 3) == ")"
                and self.t(k + 4) == "." and self.toks[k + 5].kind == "id"):
            return k + 5
        return -1

    def move_argument(self, open_paren: int) -> str | None:
        """The path inside std::move( ... ) when the argument is a plain path or *path.

        The move std::move(*p) spends the object that p reaches, so its key is *p.
        Parentheses around the argument do not change its key, and this->m and
        (*this).m key as m.  A subscript by anything but an integer literal keys
        the whole array.
        """
        close = self.partner[open_paren]
        k = open_paren + 1
        while self.t(k) == "(" and self.partner[k] == close - 1:
            k, close = k + 1, close - 1
        deref = self.t(k) == "*"
        if deref:
            k += 1
        if self.member_of_this(k) >= 0:
            k = self.member_of_this(k)
        if self.toks[k].kind != "id" or self.t(k) in KEYWORDS:
            return None
        path, end = self.path_at(k, close)
        if end < close and self.t(end) == "[" and self.partner[end] == close - 1:
            path, end = path + "[*]", close
        if end != close:
            return None
        return "*" + path if deref else path

    def move_callee(self, k: int, hi: int) -> int:
        """The index of the ( of a move call whose callee name is at k, or -1.

        The callee is a move when its last component is in MOVE_CALLEES, whatever
        qualifies it.  A member call such as obj.move(x) is not a move of x.
        """
        if self.t(k) not in MOVE_CALLEES or self.t(k - 1) in (".", "->"):
            return -1
        m = k + 1
        if self.t(m) == "<":
            m = self.skip_angle(m, hi)
        return m if self.t(m) == "(" else -1

    def qualified_start(self, k: int) -> int:
        """The index of the first token of the qualified name whose last component is at k."""
        while self.t(k - 1) == "::":
            if k < 2 or self.toks[k - 2].kind != "id" or self.t(k - 2) in KEYWORDS:
                return k - 1
            k -= 2
        return k

    def swap_arguments(self, open_paren: int) -> tuple[str, str] | None:
        """The two paths of swap(a, b) when each argument is a plain path."""
        close = self.partner[open_paren]
        paths: list[str] = []
        k = open_paren + 1
        while k < close:
            if self.toks[k].kind != "id" or self.t(k) in KEYWORDS:
                return None
            path, end = self.path_at(k, close)
            paths.append(path)
            if end < close and self.t(end) != ",":
                return None
            k = end + 1
        return (paths[0], paths[1]) if len(paths) == 2 else None

    def try_call(self, lo: int, hi: int) -> bool | None:
        """Whether the condition from lo to hi is !f(...), for one call f whose name starts with try_.

        The result is True for !f(...), False for f(...), and None for any other condition.  The
        callee can be a member, as in !ring->try_push(std::move(v)).
        """
        negated = self.t(lo) == "!"
        start = lo + 1 if negated else lo
        close = hi - 1
        if close <= start or self.t(close) != ")":
            return None
        open_paren = self.partner[close]
        name = open_paren - 1
        if self.toks[name].kind != "id" or not self.t(name).startswith("try_"):
            return None
        chain_is_plain = all(self.toks[i].kind == "id" or self.t(i) in (".", "->", "::")
                             for i in range(start, name))
        return negated if chain_is_plain else None

    def condition(self, lo: int, hi: int, state: State) -> tuple[State, State]:
        """The states where the condition from lo to hi is true and where it is false.

        A function whose name starts with try_ leaves its argument whole when it fails, the rule
        that std::map::try_emplace states.  So a move into such a call spends the name only on
        the path where the call succeeded.
        """
        negated = self.try_call(lo, hi)
        if negated is None:
            after = self.eval(lo, hi, state, [])
            return after, after
        self.hold_moves = True
        failed = self.eval(lo, hi, state, [])
        self.hold_moves = False
        succeeded = self.eval(lo, hi, state, [])
        return (failed, succeeded) if negated else (succeeded, failed)

    def element_of_get(self, k: int) -> str | None:
        """The index of std::get<I>( std::move(t) ), which moves element I of t and not t."""
        if self.t(k - 1) != "(":
            return None
        close = self.partner[k - 1] if self.partner[k - 1] >= 0 else -1
        j = k - 2
        if self.t(j) != ">":
            return None
        depth, m = 0, j
        while m > 0:
            if self.t(m) == ">":
                depth += 1
            elif self.t(m) == "<":
                depth -= 1
                if depth == 0:
                    break
            m -= 1
        if self.t(m - 1) != "get" or close < 0:
            return None
        return "".join(self.t(i) for i in range(m + 1, j))

    def is_operand_end(self, i: int) -> bool:
        tok = self.toks[i] if i >= 0 else None
        if tok is None:
            return False
        if tok.kind == "id":
            return tok.text not in KEYWORDS or tok.text in ("this", "true", "false", "nullptr")
        return tok.kind == "num" or tok.text in (")", "]", ">")

    def lambda_body(self, k: int, hi: int) -> int:
        """The index of the { of the lambda that opens at the [ at k, or -1."""
        m = self.partner[k] + 1
        if self.t(m) == "<":
            m = self.skip_angle(m, hi)
        if self.t(m) == "(":
            m = self.partner[m] + 1
        while m < hi:
            text = self.t(m)
            if text == "{":
                return m
            if text in ("(", "["):
                m = self.partner[m] + 1
                continue
            if text in (";", ",", ")", "]", "}", "="):
                return -1
            m += 1
        return -1

    def eval(self, lo: int, hi: int, state: State, decls: list[tuple[str, int]]) -> State:
        """Walk the tokens of an expression or a declaration from lo to hi."""
        if state is None:
            return None
        state = dict(state)
        decl_at = {i for _, i in decls}
        pending: list[str] = []
        k = lo
        while k < hi:
            tok = self.toks[k]
            text = tok.text
            if text in UNEVALUATED and self.t(k + 1) == "(":
                k = self.partner[k + 1] + 1
                continue
            if text == "sizeof" and self.t(k + 1) == "...":
                k = self.partner[k + 2] + 1 if self.t(k + 2) == "(" else k + 2
                continue
            if text == "requires":
                m = k + 1
                if self.t(m) == "(":
                    m = self.partner[m] + 1
                if self.t(m) == "{":
                    k = self.partner[m] + 1
                    continue
                k += 1
                continue
            if text == "[" and self.t(k + 1) == "[":
                k = self.partner[k] + 1
                continue
            if text == "[" and not self.is_operand_end(k - 1):
                body = self.lambda_body(k, hi)
                if body >= 0:
                    state = self.lambda_expr(k, body, state)
                    k = self.partner[body] + 1
                    continue
            if tok.kind == "id" and (m := self.move_callee(k, hi)) >= 0:
                path = self.move_argument(m)
                element = self.element_of_get(self.qualified_start(k))
                if path is not None and element is not None:
                    path = f"{path}[{element}]"
                if path is not None:
                    self.spend(state, k, path)
                    k = self.partner[m] + 1
                    continue
                k = m + 1
                continue
            if (member := self.member_of_this(k)) >= 0 and self.t(k - 1) not in (".", "->", "::"):
                # this->m and (*this).m are the member m.  The walk resumes at m with no
                # qualifier in front of it, so m is read as a plain name.
                k = member
                tok, text = self.toks[k], self.t(k)
                anchored = True
            else:
                anchored = False
            if text == "static_cast" and self.t(k + 1) == "<":
                m = self.skip_angle(k + 1, hi)
                scalar = all(self.t(i) in SCALAR_TYPE_WORDS for i in range(k + 2, m - 2))
                if self.t(m - 2) == "&&" and self.t(m) == "(" and not scalar:
                    path = self.move_argument(m)
                    if path is not None:
                        self.spend(state, k, path)
                        k = self.partner[m] + 1
                        continue
                k = m
                continue
            if text == "swap" and self.t(k + 1) == "(" and self.t(k - 1) not in (".", "->"):
                pair = self.swap_arguments(k + 1)
                if pair is not None:
                    exchange(state, *pair)
                    k = self.partner[k + 1] + 1
                    continue
            if tok.kind == "id" and text not in KEYWORDS and k not in decl_at:
                prev = "" if anchored else self.t(k - 1)
                if prev in (".", "->", "::") or self.t(k + 1) == "::":
                    k += 1
                    continue
                if text == "this" or prev == "~":
                    k += 1
                    continue
                path, end = self.path_at(k, hi)
                nxt = self.t(end)
                # A unary * before the path, or a call of value() on it, reaches the object
                # that the path holds, which is the key *path.
                unary_deref = prev == "*" and not self.is_operand_end(k - 2)
                value_call = nxt == "." and self.t(end + 1) == "value" and self.t(end + 2) == "("
                if unary_deref or value_call:
                    path = "*" + path
                if prev == "&" and self.t(k - 2) == "(" and self.t(k - 3) in ("destroy_at", "construct_at"):
                    pending.append(path)
                    k = end
                    continue
                if nxt == "=":
                    pending.append(path)
                    k = end + 1
                    continue
                if nxt == "[" and self.t(end + 1) != "[":
                    # A subscript that is not an integer literal can name any element,
                    # so it reads the key path[*].  An assignment through it refills
                    # every element key of the array.
                    if self.t(self.partner[end] + 1) == "=":
                        pending.append(path)
                        k = end
                        continue
                    path += "[*]"
                if nxt in (".", "->") and self.t(end + 1) in REINIT_METHODS and self.t(end + 2) == "(":
                    pending.append(path)
                    k = end + 2
                    continue
                if nxt in (".", "->") and self.t(end + 1) == "swap" and self.t(end + 2) == "(":
                    other = self.move_argument(end + 2)
                    if other is not None:
                        exchange(state, path, other)
                        k = self.partner[end + 2] + 1
                        continue
                spent = spent_prefix(state, path)
                if spent is not None:
                    self.report(k, path, "use after move", spent, state[spent])
                k = end
                continue
            k += 1
        for path in pending:
            restore(state, path)
        return state

    def lambda_expr(self, open_bracket: int, body: int, state: dict[str, int]) -> dict[str, int]:
        """Walk the captures where the lambda appears, and its body as a new body."""
        close = self.partner[open_bracket]
        shadow: list[str] = []
        k = open_bracket + 1
        while k < close:
            end = k
            while end < close and self.t(end) != ",":
                end = self.partner[end] + 1 if self.t(end) in OPEN else end + 1
            part = [self.t(i) for i in range(k, end)]
            if "=" in part and part[0] != "=":
                eq = k + part.index("=")
                name = self.t(eq - 1)
                shadow.append(name)
                state = self.eval(eq + 1, end, state, []) or {}
            elif len(part) == 1 and self.toks[k].kind == "id" and part[0] not in KEYWORDS:
                spent = spent_prefix(state, part[0])
                if spent is not None:
                    self.report(k, part[0], "copy capture after move", spent, state[spent])
            k = end + 1
        m = close + 1
        if self.t(m) == "<":
            m = self.skip_angle(m, body)
        if self.t(m) == "(":
            params_close = self.partner[m]
            for i in range(m + 1, params_close):
                if self.toks[i].kind == "id" and self.t(i + 1) in (",", ")", "=", "["):
                    shadow.append(self.t(i))
        inner: State = dict(state)
        for name in shadow:
            restore(inner, name)
        self.walk_body(body, self.partner[body], inner)
        return state

    # ── the file ────────────────────────────────────────────────────

    def macro_moves(self) -> None:
        """Report each #define whose replacement list moves or forwards.

        The walk reads the source with the preprocessor lines removed, so a move
        inside a macro is invisible at each use of the macro.  A macro therefore
        may not move.  Complexity: O(length of the text).
        """
        offset = 0
        for line in self.text.split("\n"):
            define = DEFINE.match(line)
            if define is not None:
                tokens = [m.group(0) for m in TOKEN.finditer(line, define.end())]
                for i, token in enumerate(tokens):
                    after = tokens[i + 1] if i + 1 < len(tokens) else ""
                    before = tokens[i - 1] if i > 0 else ""
                    if token in MOVE_CALLEES and after in ("(", "<") and before not in (".", "->"):
                        self.findings.append(Finding(self.path, line_of(self.text, self.joins, offset),
                                                     define.group(1), token, "move inside a macro", 0))
                        break
            offset += len(line) + 1

    def run(self) -> list[Finding]:
        if not self.balanced:
            self.findings.append(Finding(self.path, 1, "<file>", "<unbalanced>", "unbalanced brackets", 0))
            return self.findings
        self.macro_moves()
        self.scan_scope(0, len(self.toks))
        return self.findings


def scan_file(path: str, root: str) -> list[Finding]:
    text = (Path(root) / path).read_text(encoding="utf-8", errors="replace")
    return Body(path, text).run()


def tracked_sources(root: Path) -> list[str] | None:
    """The source files under SCAN_ROOTS that git tracks or does not ignore, or None outside a checkout."""
    result = subprocess.run(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard",
                             "--", *SCAN_ROOTS], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"check-use-after-move: git ls-files failed in {root}, so the guard cannot list the tree.  "
              f"Run it in a git checkout, or name the files.\n{result.stderr.strip()}", file=sys.stderr)
        return None
    listed = result.stdout.split("\n")
    out = []
    for rel in listed:
        if not rel or Path(rel).suffix not in SOURCE_SUFFIXES:
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


def scan_tree(root: Path, files: list[str]) -> list[Finding]:
    findings: list[Finding] = []
    with ProcessPoolExecutor(max_workers=16) as pool:
        for result in pool.map(scan_file, files, [str(root)] * len(files), chunksize=16):
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
struct Ctor {
    Token a; int n;
    Ctor(Token t) : a{std::move(t)}, n{t.size} {}
    void must_catch_ctor() {}
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
void must_accept_array_refill(Token (&ts)[2], int i) { take(std::move(ts[i])); ts[i] = Token{}; read(ts[i]); }
void must_accept_container_after_element(Vec& v, int i) { auto x = std::move(v[i]); v.erase(v.begin() + i); v.push_back(std::move(x)); }
void must_accept_member_move_call(Mover m, Token t) { m.move(t); read(t); }
void must_accept_algorithm_move(Token* first, Token* last, Token* out) { std::move(first, last, out); read(*first); }
'''


def self_test() -> int:
    body = Body("self_test.cpp", SELF_TEST_SOURCE)
    findings = body.run()
    by_function = Counter(f.function for f in findings)
    caught = {name for name in re.findall(r"\b(must_catch_\w+)", SELF_TEST_SOURCE)}
    accepted = {name for name in re.findall(r"\b(must_accept_\w+)", SELF_TEST_SOURCE)}
    failures = []
    # The constructor fixture reports under the constructor's name.
    expected_catch = (caught - {"must_catch_ctor"}) | {"Ctor"}
    for name in sorted(expected_catch):
        if by_function[name] == 0:
            failures.append(f"missed a real use after move in {name}")
    for name in sorted(accepted):
        if by_function[name] != 0:
            failures.append(f"false alarm in {name}: {[f.what + ' ' + f.key for f in findings if f.function == name]}")
    if by_function["Moves"] or by_function["<file>"]:
        failures.append("a requires-expression or a declaration outside a body was read as a use")
    if by_function["must_catch_repeated_reads"] != 1:
        failures.append("three reads after one move gave "
                        f"{by_function['must_catch_repeated_reads']} findings, and one move is one finding")
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
    for failure in failures:
        print(f"check-use-after-move: SELF-TEST FAILED: {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-use-after-move: self-test passed ({len(expected_catch)} caught, {len(accepted)} accepted)")
    return 0


def main(argv: list[str]) -> int:
    sys.setrecursionlimit(50000)
    root = Path(__file__).resolve().parent.parent
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
    findings = scan_tree(root, files)
    admitted, errors = read_allowlist(root / ALLOWLIST)
    if rest:
        admitted = Counter({k: v for k, v in admitted.items() if k.split(":", 1)[0] in set(rest)})
    status = verdict(findings, admitted, errors, mode)
    if status == 0:
        print(f"check-use-after-move: {len(files)} files, {len(findings)} findings, each one allowlisted")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
