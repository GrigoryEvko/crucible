"""Global types whose transmissions carry explicit labels.

model.py spells a choice positionally: branch k of a choice has the label
k, and a message has no label.  The fixy session layer puts the label of a
branch on the wire, and it has three constructs that the positional model
cannot spell:

  a label key     a branch names its label, so the branches of a choice can
                  stand in any order and the labels need not be 0, 1, ...
  a crash branch  the branch Branch<CrashLabel, void, G> that a receiver
                  takes when it detects that the sender crashed (Barwell,
                  Hou, Yoshida and Zhou, LMCS 2025, section 4.1)
  an en-route     a message that its sender sent and its receiver did not
    message       receive yet (Pischke, Masters and Yoshida, Asynchronous
                  Global Protocols, Precisely, v4, section 2.1)

This module holds that richer syntax, the compact text form that the
golden file stores, and the printers for the C++ tree, for the
subject-reduction development of Tirore, Bengtson and Carbone (ECOOP 2025),
whose branches carry labels, and for the positional model.

A label is one of these:

  "v"      a plain value message, with payload sort "nat" or "bool"
  k        the label Label<k> of a branch of a choice, with payload "unit"
  "crash"  the crash label, with payload "void"

The compact form starts with "@", so the golden file can hold the two
kinds of global type in one column.  Every function here is pure and
deterministic.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Union

import model
from model import (GBranch, GEnd, GMsg, GRec, GVar, LBranch, LChoice, LEnd, LMsg, LRec, LVar,
                   Local, PRELUDE_NS, UntranslatableError, channel_of, cpp_role, cpp_sort)

Label = Union[str, int]
PREFIX = "@"

# ── Syntax ───────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class LGEnd:
    """The end of a global protocol."""


@dataclass(frozen=True, slots=True)
class LGVar:
    """A jump back to the recursion ``index`` binders out."""

    index: int = 0


@dataclass(frozen=True, slots=True)
class LGRec:
    """A recursion binder around ``body``."""

    body: "LGlobal"


@dataclass(frozen=True, slots=True)
class LGBranch:
    """One branch of a transmission: its label, its payload sort, its continuation."""

    label: Label
    sort: str
    cont: "LGlobal"


@dataclass(frozen=True, slots=True)
class LGComm:
    """A transmission from ``frm`` to ``to`` with one or more branches."""

    frm: int
    to: int
    branches: tuple[LGBranch, ...]


@dataclass(frozen=True, slots=True)
class LGEnRoute:
    """A transmission that ``frm`` sent with the label ``chosen`` and ``to`` has not received yet.

    Every branch stays, because the receiver still holds the whole choice
    (Barwell, Hou, Yoshida and Zhou, LMCS 2025, section 4.1).  A node with
    one branch is the en-route message of Pischke, Masters and Yoshida.
    """

    frm: int
    to: int
    chosen: Label
    branches: tuple[LGBranch, ...]

    def chosen_branch(self) -> LGBranch:
        """Return the branch that the sender took."""
        return next(b for b in self.branches if b.label == self.chosen)


LGlobal = Union[LGEnd, LGVar, LGRec, LGComm, LGEnRoute]


def is_labelled_text(text: str) -> bool:
    """Return true when ``text`` is the compact form of a labelled global type."""
    return text.startswith(PREFIX)


def roles_of(g: LGlobal) -> set[int]:
    """Return the roles that occur in ``g``.  O(size)."""
    if isinstance(g, (LGEnd, LGVar)):
        return set()
    if isinstance(g, LGRec):
        return roles_of(g.body)
    out = {g.frm, g.to}
    for b in g.branches:
        out |= roles_of(b.cont)
    return out


def size(g: LGlobal) -> int:
    """Return the number of nodes of ``g``.  O(size)."""
    if isinstance(g, (LGEnd, LGVar)):
        return 1
    if isinstance(g, LGRec):
        return 1 + size(g.body)
    return 1 + sum(size(b.cont) for b in g.branches)


def has_crash_branch(g: LGlobal) -> bool:
    """Return true when a transmission of ``g`` has a crash branch.  O(size)."""
    if isinstance(g, (LGEnd, LGVar)):
        return False
    if isinstance(g, LGRec):
        return has_crash_branch(g.body)
    return any(b.label == "crash" or has_crash_branch(b.cont) for b in g.branches)


# ── Compact text form ────────────────────────────────────────────────


def _show_label(label: Label) -> str:
    return str(label)


def _show(g: LGlobal) -> str:
    if isinstance(g, LGEnd):
        return "end"
    if isinstance(g, LGVar):
        return "var" if g.index == 0 else f"var{g.index}"
    if isinstance(g, LGRec):
        return f"rec({_show(g.body)})"
    inner = ",".join(f"{_show_label(b.label)}:{b.sort}:{_show(b.cont)}" for b in g.branches)
    if isinstance(g, LGEnRoute):
        return f"enroute({g.frm},{g.to},{_show_label(g.chosen)},[{inner}])"
    return f"comm({g.frm},{g.to},[{inner}])"


def show(g: LGlobal) -> str:
    """Print ``g`` in the compact form that the golden file stores."""
    return PREFIX + _show(g)


class _Reader:
    """A recursive-descent reader for the compact form."""

    def __init__(self, text: str) -> None:
        self.text = text
        self.pos = 0

    def fail(self, what: str) -> ValueError:
        return ValueError(f"labelled compact form: expected {what} at offset {self.pos} of {self.text!r}")

    def word(self) -> str:
        start = self.pos
        while self.pos < len(self.text) and self.text[self.pos].isalnum():
            self.pos += 1
        if start == self.pos:
            raise self.fail("a word")
        return self.text[start:self.pos]

    def eat(self, ch: str) -> None:
        if self.pos >= len(self.text) or self.text[self.pos] != ch:
            raise self.fail(repr(ch))
        self.pos += 1

    def peek(self) -> str:
        return self.text[self.pos] if self.pos < len(self.text) else ""

    def label(self) -> Label:
        w = self.word()
        return int(w) if w.isdigit() else w

    def branch(self) -> LGBranch:
        lab = self.label()
        self.eat(":")
        sort = self.word()
        self.eat(":")
        return LGBranch(lab, sort, self.global_())

    def branches(self) -> tuple[LGBranch, ...]:
        """Read a branch list.  An empty list is a choice with no branch, which the corpora hold."""
        self.eat("[")
        items: list[LGBranch] = []
        if self.peek() != "]":
            items.append(self.branch())
            while self.peek() == ",":
                self.eat(",")
                items.append(self.branch())
        self.eat("]")
        return tuple(items)

    def global_(self) -> LGlobal:
        w = self.word()
        if w == "end":
            return LGEnd()
        if w.startswith("var"):
            return LGVar(int(w[3:] or "0"))
        self.eat("(")
        if w == "rec":
            g: LGlobal = LGRec(self.global_())
        elif w in ("comm", "enroute"):
            frm = int(self.word())
            self.eat(",")
            to = int(self.word())
            self.eat(",")
            if w == "comm":
                g = LGComm(frm, to, self.branches())
            else:
                chosen = self.label()
                self.eat(",")
                g = LGEnRoute(frm, to, chosen, self.branches())
                if all(b.label != chosen for b in g.branches):
                    raise self.fail(f"a branch with the chosen label {chosen!r}")
        else:
            raise self.fail("a global constructor")
        self.eat(")")
        return g


def read(text: str) -> LGlobal:
    """Parse the compact form of a labelled global type."""
    if not is_labelled_text(text):
        raise ValueError(f"labelled compact form: {text!r} does not start with {PREFIX!r}")
    r = _Reader(text[len(PREFIX):])
    g = r.global_()
    if r.pos != len(r.text):
        raise r.fail("the end of input")
    return g


# ── Conversion from the positional model ─────────────────────────────

# A labelling scheme maps the path of a choice (the labels of the choices
# above it) and its number of branches to the new order: a list of pairs
# (the original index of a branch, its label).
Scheme = Callable[[tuple[int, ...], int], list[tuple[int, int]]]


def identity_scheme(path: tuple[int, ...], count: int) -> list[tuple[int, int]]:
    """Keep the order, and give branch k the label k."""
    return [(k, k) for k in range(count)]


def keyed_scheme(path: tuple[int, ...], count: int) -> list[tuple[int, int]]:
    """Give branch k the label 3k+1, and rotate the order by the path.

    The rotation depends on the labels of the choices above, so the same
    choice in two branches of an outer choice has its labels in two
    different orders.  A merge that compares branches in order then sees
    a difference, and a merge that pairs them by label does not.
    """
    shift = (sum(path) + len(path)) % count if count else 0
    order = list(range(shift, count)) + list(range(shift))
    return [(k, 3 * k + 1) for k in order]


def sparse_scheme(path: tuple[int, ...], count: int) -> list[tuple[int, int]]:
    """Keep the order, and give branch k the label 3k+1."""
    return [(k, 3 * k + 1) for k in range(count)]


def from_positional(g: model.Global, scheme: Scheme = identity_scheme,
                    path: tuple[int, ...] = ()) -> LGlobal:
    """Convert a positional global type.  ``scheme`` gives the labels and the order.  O(size)."""
    if isinstance(g, GEnd):
        return LGEnd()
    if isinstance(g, GVar):
        return LGVar(g.index)
    if isinstance(g, GRec):
        return LGRec(from_positional(g.body, scheme, path))
    if isinstance(g, GMsg):
        return LGComm(g.frm, g.to, (LGBranch("v", g.sort, from_positional(g.cont, scheme, path)),))
    return LGComm(g.frm, g.to, tuple(
        LGBranch(label, "unit", from_positional(g.branches[k], scheme, path + (label,)))
        for k, label in scheme(path, len(g.branches))))


def add_crash_branches(g: LGlobal, unreliable: frozenset[int], kind: str) -> LGlobal:
    """Give each transmission of an unreliable sender a crash branch.

    The crash branch is what the receiver does when it detects that the
    sender crashed (Barwell, Hou, Yoshida and Zhou, LMCS 2025, section 4.1).
    ``kind`` chooses its continuation:

      copy  the continuation of the first branch, with crash branches of its
            own, so every other role sees the same protocol after the crash
      end   End: the receiver stops

    The crash branch comes last.  O(size²) for copy, O(size) for end.
    """
    if isinstance(g, (LGEnd, LGVar)):
        return g
    if isinstance(g, LGRec):
        return LGRec(add_crash_branches(g.body, unreliable, kind))
    branches = tuple(LGBranch(b.label, b.sort, add_crash_branches(b.cont, unreliable, kind)) for b in g.branches)
    if g.frm in unreliable and branches:
        crash_cont = branches[0].cont if kind == "copy" else LGEnd()
        branches += (LGBranch("crash", "void", crash_cont),)
    if isinstance(g, LGEnRoute):
        return LGEnRoute(g.frm, g.to, g.chosen, branches)
    return LGComm(g.frm, g.to, branches)


def _subst_global(g: model.Global, value: model.Global, depth: int = 0) -> model.Global:
    """Replace the variable of a removed binder with the closed ``value``."""
    if isinstance(g, GEnd):
        return g
    if isinstance(g, GVar):
        if g.index == depth:
            return value
        return GVar(g.index - 1) if g.index > depth else g
    if isinstance(g, GRec):
        return GRec(_subst_global(g.body, value, depth + 1))
    if isinstance(g, GMsg):
        return GMsg(g.frm, g.to, g.sort, _subst_global(g.cont, value, depth))
    return GBranch(g.frm, g.to, tuple(_subst_global(b, value, depth) for b in g.branches))


def unfold_top(g: model.Global) -> model.Global:
    """Unfold the recursion binders at the top of ``g`` until an action or End.  O(size × binders)."""
    for _ in range(8):
        if not isinstance(g, GRec):
            return g
        g = _subst_global(g.body, g)
    return g


def enroute_variants(g: model.Global) -> list[tuple[str, LGlobal, int, int, Label, str]]:
    """Return the runtime global types that one send of the first transmission of ``g`` reaches.

    The first transmission of the unfolded ``g`` fires its send, and the
    message becomes en route.  Each result is (tag, global type, sender,
    receiver, label, payload sort).  The tags:

      e     a message: the one branch en route
      e<k>  branch k of a choice sent: every branch stays, with k chosen.
            This is the transition that sends first, to the en-route
            transmission of Barwell, Hou, Yoshida and Zhou (LMCS 2025,
            section 4.1)
      s<k>  branch k of a choice sent, and only that branch kept.  No
            transition reaches it.  Its projection onto the receiver is a
            keyed step, where the context that the send reaches still
            holds the whole Offer, so the two associate by subtyping

    k is the first and the last branch.  O(size).
    """
    head = unfold_top(g)
    if isinstance(head, GMsg):
        if head.frm == head.to:
            return []
        return [("e", LGEnRoute(head.frm, head.to, "v", (LGBranch("v", head.sort, from_positional(head.cont)),)),
                 head.frm, head.to, "v", head.sort)]
    if not (isinstance(head, GBranch) and head.branches and head.frm != head.to):
        return []
    whole = tuple(LGBranch(k, "unit", from_positional(b)) for k, b in enumerate(head.branches))
    out: list[tuple[str, LGlobal, int, int, Label, str]] = []
    for k in dict.fromkeys((0, len(head.branches) - 1)):
        out.append((f"e{k}", LGEnRoute(head.frm, head.to, k, whole), head.frm, head.to, k, "unit"))
        if len(whole) > 1:
            out.append((f"s{k}", LGEnRoute(head.frm, head.to, k, (whole[k],)), head.frm, head.to, k, "unit"))
    return out


def cpp_fixy_choice_local(e: Local, ns: str = "fs") -> str:
    """Spell a peer-annotated local type in the canonical form of fixy's projection.

    The channel of each action gives its peer (model.channel_of).  A
    choice with one branch is a plain Send or Recv, several branches are a
    Select of Sends or an Offer of Recvs with its Sender note.
    """
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_fixy_choice_local(e.body, ns)}>"
    if not isinstance(e, LChoice):
        raise UntranslatableError("a canonical fixy local type has labelled choices only")
    peer = e.channel % 8 if e.send else e.channel // 8
    head = "Send" if e.send else "Recv"
    arms = [f"{ns}::{head}<{ns}::PeerMsg<{cpp_role(peer)}, {cpp_label(lab, 'fg')}, {cpp_payload(sort)}>, "
            f"{cpp_fixy_choice_local(k, ns)}>" for lab, sort, k in e.branches]
    if len(arms) == 1:
        return arms[0]
    if e.send:
        return f"{ns}::Select<{', '.join(arms)}>"
    return f"{ns}::Offer<{ns}::Sender<{cpp_role(peer)}>, {', '.join(arms)}>"


def has_permutation(g: model.Global) -> bool:
    """Return true when ``g`` has a choice with two branches or more.  O(size)."""
    if isinstance(g, (GEnd, GVar)):
        return False
    if isinstance(g, GRec):
        return has_permutation(g.body)
    if isinstance(g, GMsg):
        return has_permutation(g.cont)
    return len(g.branches) >= 2 or any(has_permutation(b) for b in g.branches)


def to_positional(g: LGlobal) -> model.Global:
    """Convert back to the positional model.

    Valid when each choice has the labels 0, 1, ... in order and no crash
    branch and no en-route message occurs.  Raises UntranslatableError
    otherwise.  O(size).
    """
    if isinstance(g, LGEnd):
        return GEnd()
    if isinstance(g, LGVar):
        return GVar(g.index)
    if isinstance(g, LGRec):
        return GRec(to_positional(g.body))
    if isinstance(g, LGEnRoute):
        raise UntranslatableError("an en-route message has no positional form")
    if len(g.branches) == 1 and g.branches[0].label == "v":
        return GMsg(g.frm, g.to, g.branches[0].sort, to_positional(g.branches[0].cont))
    if [b.label for b in g.branches] != list(range(len(g.branches))):
        raise UntranslatableError("the labels of a choice are not 0, 1, ... in order")
    return GBranch(g.frm, g.to, tuple(to_positional(b.cont) for b in g.branches))


# ── C++ printers ─────────────────────────────────────────────────────


def cpp_label(label: Label, ns: str = "fg") -> str:
    """Return the C++ label type of ``label``."""
    if label == "v":
        return f"{PRELUDE_NS}::Val"
    if label == "crash":
        return f"{ns}::CrashLabel"
    if isinstance(label, int):
        return f"{PRELUDE_NS}::Label<{label}>"
    raise UntranslatableError(f"unknown label {label!r}")


def cpp_payload(sort: str) -> str:
    """Return the C++ payload type of ``sort``."""
    if sort == "unit":
        return f"{PRELUDE_NS}::Unit"
    if sort == "void":
        return "void"
    return cpp_sort(sort)


def cpp_fixy_global(g: LGlobal, ns: str = "fg") -> str:
    """Spell ``g`` in fixy::session::global.  A variable that skips a binder has no spelling."""
    if isinstance(g, LGEnd):
        return f"{ns}::End"
    if isinstance(g, LGVar):
        if g.index != 0:
            raise UntranslatableError(f"variable var{g.index} skips a binder")
        return f"{ns}::Var"
    if isinstance(g, LGRec):
        return f"{ns}::Rec<{cpp_fixy_global(g.body, ns)}>"
    inner = "".join(f", {ns}::Branch<{cpp_label(b.label, ns)}, {cpp_payload(b.sort)}, "
                    f"{cpp_fixy_global(b.cont, ns)}>" for b in g.branches)
    if isinstance(g, LGEnRoute):
        return f"{ns}::EnRouteChoice<{cpp_role(g.frm)}, {cpp_role(g.to)}, {cpp_label(g.chosen, ns)}{inner}>"
    return f"{ns}::Comm<{cpp_role(g.frm)}, {cpp_role(g.to)}{inner}>"


# ── The binary view of a two-party labelled global type ──────────────


def keyed_label(label: int) -> str:
    """Return the run label of the key Label<label>.

    A keyed choice puts the label word of its key on the wire, and a
    positional choice puts its position there.  The two never match, so a
    run gives the keys a label space of their own.
    """
    return f"k{label}"


def keyed_local_of(g: LGlobal, role: int) -> Local:
    """Read the local type of ``role`` off a two-party labelled global type.

    A value message becomes a Send or a Recv of its sort.  A choice becomes
    a keyed choice: each branch sends or receives Labelled<Label<k>, Unit>
    and then continues.  The channel of each action is the channel of its
    ordered pair.  O(size).
    """
    if isinstance(g, LGEnd):
        return LEnd()
    if isinstance(g, LGVar):
        return LVar(g.index)
    if isinstance(g, LGRec):
        return LRec(keyed_local_of(g.body, role))
    if isinstance(g, LGEnRoute):
        raise ValueError("keyed_local_of: an en-route message has no binary view")
    if role not in (g.frm, g.to) or g.frm == g.to:
        raise ValueError(f"keyed_local_of: {show(g)} is not a two-party action of role {role}")
    send = g.frm == role
    ch = channel_of(g.frm, g.to)
    if len(g.branches) == 1 and g.branches[0].label == "v":
        b = g.branches[0]
        return LMsg(send, ch, b.sort, keyed_local_of(b.cont, role))
    if any(not isinstance(b.label, int) for b in g.branches):
        raise ValueError(f"keyed_local_of: {show(g)} mixes a value message into a choice")
    return LChoice(send, ch, tuple((keyed_label(b.label), "unit", keyed_local_of(b.cont, role))  # type: ignore[arg-type]
                                   for b in g.branches))


def cpp_fixy_keyed_local(e: Local, ns: str = "fs") -> str:
    """Spell a binary local type whose choices may be keyed.

    A keyed choice (LChoice with labels k<n>) sends or receives
    Labelled<Label<n>, Unit> in each branch.  A positional choice (LBranch)
    stays positional.  A value message is a Send or a Recv of its sort.
    """
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_fixy_keyed_local(e.body, ns)}>"
    if isinstance(e, LMsg):
        head = "Send" if e.send else "Recv"
        return f"{ns}::{head}<{cpp_sort(e.sort)}, {cpp_fixy_keyed_local(e.cont, ns)}>"
    head = "Send" if e.send else "Recv"
    if isinstance(e, LBranch):
        inner = ", ".join(cpp_fixy_keyed_local(b, ns) for b in e.branches)
        return f"{ns}::{'Select' if e.send else 'Offer'}<{inner}>"
    arms = []
    for label, _, cont in e.branches:
        if not (isinstance(label, str) and label.startswith("k") and label[1:].isdigit()):
            raise UntranslatableError(f"a keyed choice has the label {label!r}")
        arms.append(f"{ns}::{head}<{ns}::Labelled<{PRELUDE_NS}::Label<{label[1:]}>, {PRELUDE_NS}::Unit>, "
                    f"{cpp_fixy_keyed_local(cont, ns)}>")
    if e.step:
        if len(arms) != 1:
            raise UntranslatableError("a step has exactly one branch")
        return arms[0]
    return f"{ns}::{'Select' if e.send else 'Offer'}<{', '.join(arms)}>"


def cpp_fixy_peer_keyed_local(e: Local, me: int, ns: str = "fs") -> str:
    """Spell a binary keyed local type of role ``me`` (0 or 1) in the form that projection writes.

    Each message names its peer, the other role: a keyed branch sends or
    receives PeerMsg<peer, Label<n>, Unit>, a value message
    PeerMsg<peer, Val, sort>, and an Offer carries Sender<peer>.  A
    positional choice has no such form.  O(size).
    """
    peer = cpp_role(1 - me)
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_fixy_peer_keyed_local(e.body, me, ns)}>"
    head = "Send" if e.send else "Recv"
    if isinstance(e, LMsg):
        return (f"{ns}::{head}<{ns}::PeerMsg<{peer}, {PRELUDE_NS}::Val, {cpp_sort(e.sort)}>, "
                f"{cpp_fixy_peer_keyed_local(e.cont, me, ns)}>")
    if isinstance(e, LBranch):
        raise UntranslatableError("a positional choice has no form with peer messages")
    arms = []
    for label, _, cont in e.branches:
        if not (isinstance(label, str) and label.startswith("k") and label[1:].isdigit()):
            raise UntranslatableError(f"a keyed choice has the label {label!r}")
        arms.append(f"{ns}::{head}<{ns}::PeerMsg<{peer}, {PRELUDE_NS}::Label<{label[1:]}>, {PRELUDE_NS}::Unit>, "
                    f"{cpp_fixy_peer_keyed_local(cont, me, ns)}>")
    if e.step:
        if len(arms) != 1:
            raise UntranslatableError("a step has exactly one branch")
        return arms[0]
    if e.send:
        return f"{ns}::Select<{', '.join(arms)}>"
    return f"{ns}::Offer<{ns}::Sender<{peer}>, {', '.join(arms)}>"


def keyed_mutations(u: Local) -> list[tuple[str, Local]]:
    """Return the variants T of the keyed binary local type ``u``.

    Each variant changes one keyed choice, the first in preorder that the
    change applies to, and its name says how:

      perm@k     the branches of choice k in reverse order
      permall    the branches of every keyed choice in reverse order
      drop@k     the middle branch of choice k removed (a subset of labels)
      add@k      a branch with a fresh label and the continuation End added
      relabel@k  the first branch of choice k under a fresh label
      pos@k      choice k written positionally, in the order of its branches
      step@k     the first branch of choice k alone, written as a plain Send
                 or Recv of its Labelled message

    Choice k is the k-th keyed choice in preorder.  A keyed choice matches
    its branches by label and a positional one by position, so perm and
    permall must refine in both directions, drop, add and step must refine
    in one direction, and relabel and pos in none.  A step is a choice of
    one branch whose label is on the wire (Gay and Hole 2005; Pischke,
    Masters and Yoshida, v4, the local types p&{m(B).T} and p+{m(B).T}),
    so a run reads it as one.  The list is a deterministic function of
    ``u``.  O(size²).
    """
    choices: list[LChoice] = []

    def collect(x: Local) -> None:
        if isinstance(x, LRec):
            collect(x.body)
        elif isinstance(x, LMsg):
            collect(x.cont)
        elif isinstance(x, LBranch):
            for b in x.branches:
                collect(b)
        elif isinstance(x, LChoice):
            if all(isinstance(lab, str) and lab.startswith("k") for lab, _, _ in x.branches):
                choices.append(x)
            for _, _, k in x.branches:
                collect(k)

    collect(u)

    def rebuild(x: Local, change: Callable[[int, LChoice], Local | None], counter: list[int]) -> Local:
        if isinstance(x, (LEnd, LVar)):
            return x
        if isinstance(x, LRec):
            return LRec(rebuild(x.body, change, counter))
        if isinstance(x, LMsg):
            return LMsg(x.send, x.channel, x.sort, rebuild(x.cont, change, counter))
        if isinstance(x, LBranch):
            return LBranch(x.send, x.channel, tuple(rebuild(b, change, counter) for b in x.branches))
        index = counter[0]
        counter[0] += 1
        inner = LChoice(x.send, x.channel, tuple((lab, s, rebuild(k, change, counter))
                                                  for lab, s, k in x.branches), x.step)
        replaced = change(index, inner)
        return inner if replaced is None else replaced

    def fresh(c: LChoice) -> str:
        used = [int(lab[1:]) for lab, _, _ in c.branches]  # type: ignore[index]
        return keyed_label(max(used, default=-2) + 3)

    kinds: list[tuple[str, Callable[[LChoice], LChoice | LBranch | None]]] = [
        ("perm", lambda c: LChoice(c.send, c.channel, tuple(reversed(c.branches)))
         if len(c.branches) >= 2 else None),
        ("drop", lambda c: LChoice(c.send, c.channel,
                                   c.branches[:len(c.branches) // 2] + c.branches[len(c.branches) // 2 + 1:])
         if len(c.branches) >= 2 else None),
        ("add", lambda c: LChoice(c.send, c.channel, c.branches + ((fresh(c), "unit", LEnd()),))),
        ("relabel", lambda c: LChoice(c.send, c.channel, ((fresh(c),) + c.branches[0][1:],) + c.branches[1:])
         if c.branches else None),
        ("pos", lambda c: LBranch(c.send, c.channel, tuple(k for _, _, k in c.branches))),
        ("step", lambda c: LChoice(c.send, c.channel, c.branches[:1], True) if len(c.branches) >= 2 else None),
    ]
    out: list[tuple[str, Local]] = []
    for name, apply in kinds:
        for target in range(len(choices)):
            v = rebuild(u, lambda i, c: apply(c) if i == target else None, [0])
            if v != u:
                out.append((f"{name}@{target}", v))
                break
    if sum(len(c.branches) >= 2 for c in choices) >= 2:
        v = rebuild(u, lambda i, c: LChoice(c.send, c.channel, tuple(reversed(c.branches))), [0])
        if v != u:
            out.append(("permall", v))
    return out


# ── The subject-reduction development (ECOOP 2025) ───────────────────


def _sr_sort(sort: str) -> str:
    # The development has the sorts SBool and SGType.  A nat payload takes
    # the sort of the global type End, so the two payload sorts of the
    # corpora stay two different sorts there.
    return {"bool": "(VSort SBool)", "nat": "(VSort (SGType GEnd))"}[sort]


def sr_coq_global(g: LGlobal, chan: str = "pair") -> str:
    """Print ``g`` as a gType of the ECOOP 2025 development.

    A transmission with one value branch is a GMsg.  A transmission whose
    branches all carry a Label<k> is a GBranch whose list pairs each label
    with its continuation.  A crash branch, an en-route message and a
    transmission that mixes a value message into a choice have no form
    there.  ``chan`` is a channel specification (model.channel_table).
    """
    table = model.channel_table(chan)

    def action(frm: int, to: int) -> str:
        if chan == "single":
            ch = 0
        elif table is None:
            ch = channel_of(frm, to)
        else:
            ch = table.get((frm, to), channel_of(frm, to))
        return f"(Action (Ptcp {frm}) (Ptcp {to}) (Ch {ch}))"

    def walk(x: LGlobal) -> str:
        if isinstance(x, LGEnd):
            return "GEnd"
        if isinstance(x, LGVar):
            return f"(GVar {x.index})"
        if isinstance(x, LGRec):
            return f"(GRec {walk(x.body)})"
        if isinstance(x, LGEnRoute):
            raise UntranslatableError("the development has no en-route message in a global type")
        if len(x.branches) == 1 and x.branches[0].label == "v":
            b = x.branches[0]
            return f"(GMsg {action(x.frm, x.to)} {_sr_sort(b.sort)} {walk(b.cont)})"
        if any(not isinstance(b.label, int) for b in x.branches):
            raise UntranslatableError("the development has no crash branch and no mixed choice")
        items = "".join(f"({b.label}, {walk(b.cont)}) :: " for b in x.branches)
        return f"(GBranch {action(x.frm, x.to)} ({items}nil))"

    return walk(g)


def decode_sr_local(codes: list[int]) -> Local | None:
    """Decode the nat-list encoding of an lType of the ECOOP 2025 development.

    ``[0]`` is a rejected projection.  ``[1, ...]`` is an accepted one,
    followed by the prefix encoding of the local type (see sr.py).  A
    message becomes an LMsg, and a labelled choice an LChoice whose
    branches carry the label k and the payload "unit".  O(length).
    """
    if not codes:
        raise ValueError("decode_sr_local: empty encoding")
    if codes[0] == 0:
        if len(codes) != 1:
            raise ValueError(f"decode_sr_local: trailing data after a rejection: {codes}")
        return None
    pos = 1
    sorts = {0: "nat", 1: "bool"}

    def take() -> int:
        nonlocal pos
        if pos >= len(codes):
            raise ValueError(f"decode_sr_local: truncated encoding {codes}")
        v = codes[pos]
        pos += 1
        return v

    def one() -> Local:
        tag = take()
        if tag == 0:
            return LVar(take())
        if tag == 1:
            return LEnd()
        if tag == 2:
            send = take() == 0
            ch = take()
            sort = sorts.get(take())
            if sort is None:
                raise ValueError(f"decode_sr_local: unknown sort code in {codes}")
            return LMsg(send, ch, sort, one())
        if tag == 3:
            send = take() == 0
            ch = take()
            n = take()
            items = []
            for _ in range(n):
                label = take()
                items.append((label, "unit", one()))
            return LChoice(send, ch, tuple(items))
        if tag == 4:
            return LRec(one())
        raise ValueError(f"decode_sr_local: unknown tag {tag} in {codes}")

    e = one()
    if pos != len(codes):
        raise ValueError(f"decode_sr_local: trailing data in {codes}")
    return e
