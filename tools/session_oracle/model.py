"""Global and local session types for the differential tests.

The global types are the syntax of Tirore, Bengtson and Carbone (ITP
2023) with one payload sort per message: end, a message, a choice with
any number of branches, recursion and a recursion variable.  A variable
carries a de Bruijn index.  Index 0 names the nearest enclosing
recursion, which is the only variable that ``Var_G`` in the frozen tree
and ``Continue`` in fixy can spell.  A variable with a larger index, a
choice with no branch, a message from a role to itself and a recursion
whose body is a variable are legal here, because the adversarial corpus
needs them.

The local types are the decoded result of the oracle.  A local type
records the direction and the channel of each action.  The protocols of
the frozen tree and the binary fixy protocols record the direction
only, so their printers drop the channel.  The multiparty local types of
fixy name the peer of each action, and the channel gives that peer.

Every function here is pure and deterministic.  The generator takes a
seeded ``random.Random``, so one seed gives one corpus (DetSafe).
"""

from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Union

# ── Global types ─────────────────────────────────────────────────────

SORTS: tuple[str, ...] = ("nat", "bool")


@dataclass(frozen=True, slots=True)
class GEnd:
    """The end of a global protocol."""


@dataclass(frozen=True, slots=True)
class GVar:
    """A jump back to the recursion ``index`` binders out."""

    index: int = 0


@dataclass(frozen=True, slots=True)
class GRec:
    """A recursion binder around ``body``."""

    body: "Global"


@dataclass(frozen=True, slots=True)
class GMsg:
    """One message of sort ``sort`` from role ``frm`` to role ``to``."""

    frm: int
    to: int
    sort: str
    cont: "Global"


@dataclass(frozen=True, slots=True)
class GBranch:
    """A choice that role ``frm`` makes and sends to role ``to``."""

    frm: int
    to: int
    branches: tuple["Global", ...]


Global = Union[GEnd, GVar, GRec, GMsg, GBranch]

# ── Local types (oracle output) ──────────────────────────────────────


@dataclass(frozen=True, slots=True)
class LEnd:
    """The end of a local protocol."""


@dataclass(frozen=True, slots=True)
class LVar:
    """A jump back to the recursion ``index`` binders out."""

    index: int


@dataclass(frozen=True, slots=True)
class LRec:
    """A recursion binder around ``body``."""

    body: "Local"


@dataclass(frozen=True, slots=True)
class LMsg:
    """A send (``send`` true) or a receive of one value of ``sort``."""

    send: bool
    channel: int
    sort: str
    cont: "Local"


@dataclass(frozen=True, slots=True)
class LBranch:
    """A selection (``send`` true) or an offer over ``branches``."""

    send: bool
    channel: int
    branches: tuple["Local", ...]


@dataclass(frozen=True, slots=True)
class LChoice:
    """A choice whose branches carry a label and a payload sort each.

    This is the shape of a fixy multiparty local type: a Send, Recv,
    Select or Offer of PeerMsg<Peer, Label, Payload>.  A label is
    ``"v"`` for a plain value message, or the index k of Label<k>.  A
    full merge can put the two kinds of label into one Offer, which the
    oracle's local types cannot express.
    """

    send: bool
    channel: int
    branches: tuple[tuple[str | int, str, "Local"], ...]


Local = Union[LEnd, LVar, LRec, LMsg, LBranch, LChoice]


class UntranslatableError(ValueError):
    """A type has no spelling in one of our C++ protocol languages.

    The only such shape is a variable that skips a binder, because
    ``Var_G`` and ``Continue`` always name the nearest binder.
    """


# ── Generator ────────────────────────────────────────────────────────


def channel_of(frm: int, to: int) -> int:
    """Return the channel of the ordered pair (frm, to).

    Each ordered pair of roles gets its own channel.  That is the model
    in which the subject-reduction counterexample of Tirore, Bengtson and
    Carbone (ECOOP 2025) cannot occur.
    """
    return frm * 8 + to


def _pair(rng: random.Random, roles: int) -> tuple[int, int]:
    """Draw an ordered pair of two different roles."""
    frm = rng.randrange(roles)
    to = rng.randrange(roles - 1)
    if to >= frm:
        to += 1
    return frm, to


def _gen(rng: random.Random, roles: int, depth: int, in_rec: bool,
         guarded: bool, max_branches: int) -> Global:
    """Generate one global type.

    ``guarded`` is true when an action lies between the nearest
    recursion binder and this position, so a variable here is legal.
    The weights favour messages, so most types carry actions.  O(size).
    """
    if depth <= 0:
        return GVar() if (in_rec and guarded and rng.random() < 0.6) else GEnd()
    choices: list[tuple[str, int]] = [("end", 1), ("msg", 5), ("rec", 1)]
    if in_rec and guarded:
        choices.append(("var", 3))
    if depth >= 2:
        choices.append(("branch", 3))
    name = _weighted(rng, choices)
    if name == "end":
        return GEnd()
    if name == "var":
        return GVar()
    if name == "msg":
        frm, to = _pair(rng, roles)
        return GMsg(frm, to, rng.choice(SORTS),
                    _gen(rng, roles, depth - 1, in_rec, True, max_branches))
    if name == "branch":
        frm, to = _pair(rng, roles)
        count = rng.randrange(2, max_branches + 1)
        return GBranch(frm, to, tuple(
            _gen(rng, roles, depth - 1, in_rec, True, max_branches) for _ in range(count)))
    # A recursion body starts with an action, so every variable in it is
    # guarded and the type is contractive.
    frm, to = _pair(rng, roles)
    if depth >= 3 and rng.random() < 0.4:
        count = rng.randrange(2, max_branches + 1)
        body: Global = GBranch(frm, to, tuple(
            _gen(rng, roles, depth - 2, True, True, max_branches) for _ in range(count)))
    else:
        body = GMsg(frm, to, rng.choice(SORTS),
                    _gen(rng, roles, depth - 2, True, True, max_branches))
    return GRec(body)


def generate(seed: int, count: int, roles: int, max_depth: int,
             max_branches: int = 3) -> list[Global]:
    """Return ``count`` distinct global types over ``roles`` roles.

    The result is a deterministic function of the arguments.  Types
    with no action are dropped, because they test nothing.  O(count ×
    size), with a retry bound of 50 draws per kept type.
    """
    if roles < 2:
        raise ValueError(f"generate: a global type needs two roles, got {roles}")
    rng = random.Random(seed)
    seen: set[Global] = set()
    out: list[Global] = []
    draws = 0
    while len(out) < count:
        draws += 1
        if draws > 50 * count:
            raise RuntimeError(
                f"generate: only {len(out)} distinct types in {draws} draws; "
                f"raise max_depth ({max_depth}) or lower count ({count})")
        g = _gen(rng, roles, rng.randrange(2, max_depth + 1), False, False, max_branches)
        if isinstance(g, GEnd) or g in seen:
            continue
        seen.add(g)
        out.append(g)
    return out


def _weighted(rng: random.Random, choices: list[tuple[str, int]]) -> str:
    """Draw one name from ``choices`` with probability proportional to its weight."""
    total = sum(weight for _, weight in choices)
    pick = rng.randrange(total)
    for name, weight in choices:
        if pick < weight:
            return name
        pick -= weight
    raise AssertionError("unreachable: the weights sum to total")


def _gen_adversarial(rng: random.Random, roles: int, depth: int, binders: int,
                     guarded: bool) -> Global:
    """Generate one global type biased toward the corners of the syntax.

    ``binders`` counts the enclosing recursions.  ``guarded`` is true
    when an action lies between the nearest binder and this position.
    The corners are these: a choice with zero or one branch, a third
    role in one branch only, a variable directly under a choice, a
    variable that names an outer binder, nested binders with no action
    between them, a recursion whose body is a variable, and a message
    from a role to itself when ``roles`` is more than two.  O(size).
    """
    if depth <= 0:
        return GVar() if binders and rng.random() < 0.5 else GEnd()
    choices: list[tuple[str, int]] = [("end", 1), ("msg", 4), ("branch", 3), ("rec", 3)]
    if binders:
        choices.append(("var", 3 if guarded else 1))
    if roles > 2:
        choices += [("oneside", 2), ("self", 1)]
    name = _weighted(rng, choices)
    if name == "end":
        return GEnd()
    if name == "var":
        outer = binders > 1 and rng.random() < 0.35
        return GVar(rng.randrange(1, binders) if outer else 0)
    if name in ("msg", "self"):
        frm, to = _pair(rng, roles)
        if name == "self":
            to = frm
        return GMsg(frm, to, rng.choice(SORTS),
                    _gen_adversarial(rng, roles, depth - 1, binders, True))
    if name == "branch":
        frm, to = _pair(rng, roles)
        count = int(_weighted(rng, [("0", 1), ("1", 2), ("2", 4), ("3", 1)]))
        return GBranch(frm, to, tuple(
            _gen_adversarial(rng, roles, depth - 1, binders, True) for _ in range(count)))
    if name == "oneside":
        # The third role acts in the first branch and not in the second.
        frm, to = _pair(rng, roles)
        third = next(r for r in range(roles) if r not in (frm, to))
        first = GMsg(to, third, rng.choice(SORTS),
                     _gen_adversarial(rng, roles, depth - 2, binders, True))
        second = _gen_adversarial(rng, roles, depth - 2, binders, True)
        return GBranch(frm, to, (first, second))
    return GRec(_gen_adversarial(rng, roles, depth - 1, binders + 1, False))


def generate_adversarial(seed: int, count: int, roles: int, max_depth: int) -> list[Global]:
    """Return ``count`` distinct adversarial global types (see _gen_adversarial).

    The result is a deterministic function of the arguments.  Closed
    types only: a variable never names a binder that does not exist.
    O(count × size), with a retry bound of 50 draws per kept type.
    """
    if roles < 2:
        raise ValueError(f"generate_adversarial: a global type needs two roles, got {roles}")
    rng = random.Random(seed)
    seen: set[Global] = set()
    out: list[Global] = []
    draws = 0
    while len(out) < count:
        draws += 1
        if draws > 50 * count:
            raise RuntimeError(
                f"generate_adversarial: only {len(out)} distinct types in {draws} draws")
        g = _gen_adversarial(rng, roles, rng.randrange(1, max_depth + 1), 0, False)
        if g in seen:
            continue
        seen.add(g)
        out.append(g)
    return out


def size(g: Global) -> int:
    """Return the number of nodes of ``g``.  O(size)."""
    if isinstance(g, (GEnd, GVar)):
        return 1
    if isinstance(g, GRec):
        return 1 + size(g.body)
    if isinstance(g, GMsg):
        return 1 + size(g.cont)
    return 1 + sum(size(b) for b in g.branches)


def _mentions(g: Global, index: int) -> bool:
    """Return true when ``g`` refers to the binder ``index`` levels out."""
    if isinstance(g, GEnd):
        return False
    if isinstance(g, GVar):
        return g.index == index
    if isinstance(g, GRec):
        return _mentions(g.body, index + 1)
    if isinstance(g, GMsg):
        return _mentions(g.cont, index)
    return any(_mentions(b, index) for b in g.branches)


def _drop_binder(g: Global, cutoff: int = 0) -> Global:
    """Lower every variable above ``cutoff`` by one, for a removed binder.

    The caller makes sure that nothing refers to the removed binder.
    """
    if isinstance(g, GEnd):
        return g
    if isinstance(g, GVar):
        return GVar(g.index - 1) if g.index > cutoff else g
    if isinstance(g, GRec):
        return GRec(_drop_binder(g.body, cutoff + 1))
    if isinstance(g, GMsg):
        return GMsg(g.frm, g.to, g.sort, _drop_binder(g.cont, cutoff))
    return GBranch(g.frm, g.to, tuple(_drop_binder(b, cutoff) for b in g.branches))


def shrinks(g: Global) -> list[Global]:
    """Return the types one shrink step below ``g``, smallest first.

    A step replaces a node with End, removes a message, keeps one
    branch of a choice, removes one branch, removes a binder that
    nothing names, or changes a payload sort to nat.  Every result is
    closed when ``g`` is closed.  The order is deterministic.  O(size²).
    """
    out: set[Global] = set()
    if not isinstance(g, GEnd):
        out.add(GEnd())
    if isinstance(g, GRec):
        if not _mentions(g.body, 0):
            out.add(_drop_binder(g.body))
        out |= {GRec(b) for b in shrinks(g.body)}
    elif isinstance(g, GMsg):
        out.add(g.cont)
        if g.sort != "nat":
            out.add(GMsg(g.frm, g.to, "nat", g.cont))
        out |= {GMsg(g.frm, g.to, g.sort, c) for c in shrinks(g.cont)}
    elif isinstance(g, GBranch):
        out |= set(g.branches)
        for i in range(len(g.branches)):
            out.add(GBranch(g.frm, g.to, g.branches[:i] + g.branches[i + 1:]))
            for b in shrinks(g.branches[i]):
                out.add(GBranch(g.frm, g.to, g.branches[:i] + (b,) + g.branches[i + 1:]))
    out.discard(g)
    return sorted(out, key=lambda x: (size(x), show_global(x)))


def canonical_roles(g: Global, role: int | None) -> tuple[Global, int | None]:
    """Rename the roles of ``g`` to 0, 1, ... in the order of first use.

    ``role`` is renamed with them.  A role that ``g`` does not use gets
    the next free number.  The shrinker uses this to print one form of
    each minimal counterexample.  O(size).
    """
    order: dict[int, int] = {}

    def note(r: int) -> None:
        order.setdefault(r, len(order))

    def scan(x: Global) -> None:
        if isinstance(x, GRec):
            scan(x.body)
        elif isinstance(x, GMsg):
            note(x.frm)
            note(x.to)
            scan(x.cont)
        elif isinstance(x, GBranch):
            note(x.frm)
            note(x.to)
            for b in x.branches:
                scan(b)

    def rename(x: Global) -> Global:
        if isinstance(x, (GEnd, GVar)):
            return x
        if isinstance(x, GRec):
            return GRec(rename(x.body))
        if isinstance(x, GMsg):
            return GMsg(order[x.frm], order[x.to], x.sort, rename(x.cont))
        return GBranch(order[x.frm], order[x.to], tuple(rename(b) for b in x.branches))

    scan(g)
    if role is not None:
        note(role)
    return rename(g), (order[role] if role is not None else None)


def local_of(g: Global, role: int) -> Local:
    """Read the local type of ``role`` off a two-party global type.

    Every action of a two-party type involves ``role``, so the reading
    keeps every node.  It does not drop an unguarded recursion and it
    does not check anything: it is the naive projection whose verdict
    the adversarial fixy families compare with the oracle.  The channel
    of each action is the channel of its ordered pair.  O(size).
    """
    if isinstance(g, GEnd):
        return LEnd()
    if isinstance(g, GVar):
        return LVar(g.index)
    if isinstance(g, GRec):
        return LRec(local_of(g.body, role))
    if role not in (g.frm, g.to) or g.frm == g.to:
        raise ValueError(f"local_of: {show_global(g)} is not a two-party action of role {role}")
    if isinstance(g, GMsg):
        return LMsg(g.frm == role, channel_of(g.frm, g.to), g.sort, local_of(g.cont, role))
    return LBranch(g.frm == role, channel_of(g.frm, g.to),
                   tuple(local_of(b, role) for b in g.branches))


def action_ok(g: Global) -> bool:
    """Return true when no action of ``g`` goes from a role to itself.

    This is ``action_pred`` of the oracle's elimination.v.  The oracle's
    proj does not check it.  O(size).
    """
    if isinstance(g, (GEnd, GVar)):
        return True
    if isinstance(g, GRec):
        return action_ok(g.body)
    if g.frm == g.to:
        return False
    if isinstance(g, GMsg):
        return action_ok(g.cont)
    return all(action_ok(b) for b in g.branches)


def size_ok(g: Global) -> bool:
    """Return true when every choice of ``g`` has a branch.

    This is ``size_pred`` of the oracle's elimination.v.  The oracle's
    proj does not check it.  O(size).
    """
    if isinstance(g, (GEnd, GVar)):
        return True
    if isinstance(g, GRec):
        return size_ok(g.body)
    if isinstance(g, GMsg):
        return size_ok(g.cont)
    return bool(g.branches) and all(size_ok(b) for b in g.branches)


def contractive(g: Global) -> bool:
    """Return true when every recursion body of ``g`` acts before its variable.

    This is ``gcontractive`` of the oracle's elimination.v: the body of
    each binder is guarded for index 0, and a nested binder shifts the
    index.  O(size²).
    """
    def guarded(x: Global, n: int) -> bool:
        if isinstance(x, GVar):
            return x.index != n
        if isinstance(x, GRec):
            return guarded(x.body, n + 1)
        return True

    if isinstance(g, (GEnd, GVar)):
        return True
    if isinstance(g, GRec):
        return guarded(g.body, 0) and contractive(g.body)
    if isinstance(g, GMsg):
        return contractive(g.cont)
    return all(contractive(b) for b in g.branches)


def has_idle_loop(e: Local) -> bool:
    """Return true when ``e`` holds a Loop whose body never refers to it.

    Such a Loop is a binder around a finite body.  fixy's
    is_well_formed_v refuses it, and fixy's projection never writes one,
    because it returns the body of a recursion that never loops back.
    O(size²).
    """
    def mentions(x: Local, depth: int) -> bool:
        if isinstance(x, LVar):
            return x.index == depth
        if isinstance(x, LEnd):
            return False
        if isinstance(x, LRec):
            return mentions(x.body, depth + 1)
        if isinstance(x, LMsg):
            return mentions(x.cont, depth)
        if isinstance(x, LChoice):
            return any(mentions(k, depth) for _, _, k in x.branches)
        return any(mentions(b, depth) for b in x.branches)

    if isinstance(e, (LEnd, LVar)):
        return False
    if isinstance(e, LRec):
        return not mentions(e.body, 0) or has_idle_loop(e.body)
    if isinstance(e, LMsg):
        return has_idle_loop(e.cont)
    if isinstance(e, LChoice):
        return any(has_idle_loop(k) for _, _, k in e.branches)
    return any(has_idle_loop(b) for b in e.branches)


def has_empty_choice(e: Local) -> bool:
    """Return true when ``e`` holds a Select or an Offer with no branch.  O(size)."""
    if isinstance(e, (LEnd, LVar)):
        return False
    if isinstance(e, LRec):
        return has_empty_choice(e.body)
    if isinstance(e, LMsg):
        return has_empty_choice(e.cont)
    if isinstance(e, LChoice):
        return not e.branches or any(has_empty_choice(k) for _, _, k in e.branches)
    return not e.branches or any(has_empty_choice(b) for b in e.branches)


def local_contractive(e: Local) -> bool:
    """Return true when each Loop of ``e`` acts before it reaches its Continue.  O(size²)."""
    def guarded(x: Local, depth: int) -> bool:
        if isinstance(x, LVar):
            return x.index != depth
        if isinstance(x, LRec):
            return guarded(x.body, depth + 1)
        return True

    if isinstance(e, (LEnd, LVar)):
        return True
    if isinstance(e, LRec):
        return guarded(e.body, 0) and local_contractive(e.body)
    if isinstance(e, LMsg):
        return local_contractive(e.cont)
    if isinstance(e, LChoice):
        return all(local_contractive(k) for _, _, k in e.branches)
    return all(local_contractive(b) for b in e.branches)


def dual_local(e: Local) -> Local:
    """Return the dual of a two-party local type: every send becomes a receive.

    The channels stay, because the peer's send on a channel is this role's
    receive on it.  O(size).
    """
    if isinstance(e, (LEnd, LVar)):
        return e
    if isinstance(e, LRec):
        return LRec(dual_local(e.body))
    if isinstance(e, LMsg):
        return LMsg(not e.send, e.channel, e.sort, dual_local(e.cont))
    if isinstance(e, LChoice):
        return LChoice(not e.send, e.channel,
                       tuple((lab, sort, dual_local(k)) for lab, sort, k in e.branches))
    return LBranch(not e.send, e.channel, tuple(dual_local(b) for b in e.branches))


def _unfold_local(e: LRec) -> Local:
    def sub(x: Local, depth: int) -> Local:
        if isinstance(x, LVar):
            if x.index == depth:
                return e
            return LVar(x.index - 1) if x.index > depth else x
        if isinstance(x, LEnd):
            return x
        if isinstance(x, LRec):
            return LRec(sub(x.body, depth + 1))
        if isinstance(x, LMsg):
            return LMsg(x.send, x.channel, x.sort, sub(x.cont, depth))
        if isinstance(x, LBranch):
            return LBranch(x.send, x.channel, tuple(sub(b, depth) for b in x.branches))
        raise ValueError("unfold: a labelled choice has no place in a binary local type")
    return sub(e.body, 0)


def mutations(u: Local, limit: int = 6) -> list[tuple[str, Local]]:
    """Return up to ``limit`` variants T of the binary local type ``u``.

    Each variant changes one node, and its name says how, so that the
    subtyping families can compare T with U in the two directions:

      unfold      one unfolding of a top loop
      drop@k      the last branch of the choice at action k removed
      add@k       a branch that ends added to the choice at action k
      sort@k      the payload sort of the message at action k flipped
      end@k       the continuation after action k cut to End

    Action k is the k-th action node in preorder.  The list is a
    deterministic function of ``u``.  O(size²).
    """
    out: list[tuple[str, Local]] = []
    if isinstance(u, LRec):
        out.append(("unfold", _unfold_local(u)))
    count = 0

    def rebuild(x: Local, kind: str, target: int) -> Local:
        nonlocal count
        if isinstance(x, (LEnd, LVar)):
            return x
        if isinstance(x, LRec):
            return LRec(rebuild(x.body, kind, target))
        here = count
        count += 1
        if isinstance(x, LMsg):
            if here == target and kind == "sort":
                return LMsg(x.send, x.channel, "bool" if x.sort == "nat" else "nat", x.cont)
            if here == target and kind == "end":
                return LMsg(x.send, x.channel, x.sort, LEnd())
            return LMsg(x.send, x.channel, x.sort, rebuild(x.cont, kind, target))
        assert isinstance(x, LBranch)
        if here == target and kind == "drop" and len(x.branches) >= 2:
            return LBranch(x.send, x.channel, x.branches[:-1])
        if here == target and kind == "add":
            return LBranch(x.send, x.channel, x.branches + (LEnd(),))
        if here == target and kind == "end":
            return LBranch(x.send, x.channel, tuple(LEnd() for _ in x.branches))
        return LBranch(x.send, x.channel, tuple(rebuild(b, kind, target) for b in x.branches))

    def actions(x: Local) -> int:
        if isinstance(x, (LEnd, LVar)):
            return 0
        if isinstance(x, LRec):
            return actions(x.body)
        if isinstance(x, LMsg):
            return 1 + actions(x.cont)
        assert isinstance(x, LBranch)
        return 1 + sum(actions(b) for b in x.branches)

    total = actions(u)
    for kind in ("drop", "add", "sort", "end"):
        for target in range(total):
            count = 0
            v = rebuild(u, kind, target)
            if v != u:
                out.append((f"{kind}@{target}", v))
                break
    return out[:limit]


def roles_of(g: Global) -> set[int]:
    """Return the roles that occur in ``g``.  O(size)."""
    if isinstance(g, (GEnd, GVar)):
        return set()
    if isinstance(g, GRec):
        return roles_of(g.body)
    if isinstance(g, GMsg):
        return {g.frm, g.to} | roles_of(g.cont)
    out = {g.frm, g.to}
    for b in g.branches:
        out |= roles_of(b)
    return out


# ── Compact text form (golden file) ──────────────────────────────────


def show_global(g: Global) -> str:
    """Print ``g`` in the compact form that the golden file stores."""
    if isinstance(g, GEnd):
        return "end"
    if isinstance(g, GVar):
        return "var" if g.index == 0 else f"var{g.index}"
    if isinstance(g, GRec):
        return f"rec({show_global(g.body)})"
    if isinstance(g, GMsg):
        return f"msg({g.frm},{g.to},{g.sort},{show_global(g.cont)})"
    inner = ",".join(show_global(b) for b in g.branches)
    return f"branch({g.frm},{g.to},[{inner}])"


def show_local(e: Local | None) -> str:
    """Print a local type, or ``none`` for a rejected projection."""
    if e is None:
        return "none"
    if isinstance(e, LEnd):
        return "end"
    if isinstance(e, LVar):
        return f"var{e.index}"
    if isinstance(e, LRec):
        return f"rec({show_local(e.body)})"
    tag = "send" if e.send else "recv"
    if isinstance(e, LChoice):
        inner = ",".join(f"{'v' if lab == 'v' else f'l{lab}'}:{sort}:{show_local(k)}"
                         for lab, sort, k in e.branches)
        return f"{'sel' if e.send else 'off'}({e.channel},[{inner}])"
    if isinstance(e, LMsg):
        return f"{tag}({e.channel},{e.sort},{show_local(e.cont)})"
    inner = ",".join(show_local(b) for b in e.branches)
    return f"{'select' if e.send else 'offer'}({e.channel},[{inner}])"


class _Reader:
    """A recursive-descent reader for the compact form."""

    def __init__(self, text: str) -> None:
        self.text = text
        self.pos = 0

    def fail(self, what: str) -> ValueError:
        return ValueError(f"compact form: expected {what} at offset {self.pos} of {self.text!r}")

    def word(self) -> str:
        start = self.pos
        while self.pos < len(self.text) and (self.text[self.pos].isalnum()):
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

    def done(self) -> None:
        if self.pos != len(self.text):
            raise self.fail("the end of input")

    def list_of(self, item):  # type: ignore[no-untyped-def]
        self.eat("[")
        if self.peek() == "]":
            self.eat("]")
            return ()
        out = [item()]
        while self.peek() == ",":
            self.eat(",")
            out.append(item())
        self.eat("]")
        return tuple(out)

    def global_(self) -> Global:
        w = self.word()
        if w == "end":
            return GEnd()
        if w.startswith("var"):
            return GVar(int(w[3:] or "0"))
        self.eat("(")
        if w == "rec":
            g: Global = GRec(self.global_())
        elif w == "msg":
            frm = int(self.word()); self.eat(",")
            to = int(self.word()); self.eat(",")
            sort = self.word(); self.eat(",")
            g = GMsg(frm, to, sort, self.global_())
        elif w == "branch":
            frm = int(self.word()); self.eat(",")
            to = int(self.word()); self.eat(",")
            g = GBranch(frm, to, self.list_of(self.global_))
        else:
            raise self.fail("a global constructor")
        self.eat(")")
        return g

    def local(self) -> Local | None:
        w = self.word()
        if w == "none":
            return None
        if w == "end":
            return LEnd()
        if w.startswith("var"):
            return LVar(int(w[3:]))
        self.eat("(")
        if w == "rec":
            body = self.local()
            if body is None:
                raise self.fail("a local body")
            e: Local = LRec(body)
        elif w in ("send", "recv"):
            ch = int(self.word()); self.eat(",")
            sort = self.word(); self.eat(",")
            cont = self.local()
            if cont is None:
                raise self.fail("a local continuation")
            e = LMsg(w == "send", ch, sort, cont)
        elif w in ("select", "offer"):
            ch = int(self.word()); self.eat(",")
            items = self.list_of(self.local)
            if any(b is None for b in items):
                raise self.fail("a local branch")
            e = LBranch(w == "select", ch, items)  # type: ignore[arg-type]
        else:
            raise self.fail("a local constructor")
        self.eat(")")
        return e


def read_global(text: str) -> Global:
    """Parse the compact form of a global type."""
    r = _Reader(text)
    g = r.global_()
    r.done()
    return g


def read_local(text: str) -> Local | None:
    """Parse the compact form of a local type, or ``none``."""
    r = _Reader(text)
    e = r.local()
    r.done()
    return e


# ── Rocq term printer ────────────────────────────────────────────────


def _coq_sort(sort: str) -> str:
    # The oracle declares a postfix notation "s [e σ]" for substitution,
    # so a list after an application head is spelled with :: and nil.
    return {"nat": "(VSeqSort (SNat :: nil))", "bool": "(VSeqSort (SBool :: nil))"}[sort]


def channel_table(spec: str) -> dict[tuple[int, int], int] | None:
    """Parse a channel specification.

    ``pair`` gives each ordered pair of roles its own channel (see
    channel_of) and returns None.  ``single`` sends every action on
    channel 0.  ``map:F>T=C,...`` names the channel C of each pair
    (F, T); a pair that the map does not name gets its own channel.
    The paper corpus uses a map to reproduce the shared channels of an
    example.
    """
    if spec == "pair":
        return None
    if spec == "single":
        return {}
    if not spec.startswith("map:"):
        raise ValueError(f"channel spec: unknown form {spec!r}")
    table: dict[tuple[int, int], int] = {}
    for item in spec[4:].split(","):
        pair, _, ch = item.partition("=")
        frm, _, to = pair.partition(">")
        table[(int(frm), int(to))] = int(ch)
    return table


def _coq_action(frm: int, to: int, table: dict[tuple[int, int], int] | None,
                spec: str) -> str:
    if spec == "single":
        ch = 0
    elif table is None:
        ch = channel_of(frm, to)
    else:
        ch = table.get((frm, to), channel_of(frm, to))
    return f"(Action (Ptcp {frm}) (Ptcp {to}) (Ch {ch}))"


def coq_global(g: Global, chan: str = "pair") -> str:
    """Print ``g`` as a term of the oracle's ``gType``.

    ``chan`` is a channel specification (see channel_table).  With
    ``single`` the oracle sees the same channel-free view of a local
    type that our C++ protocols have, which the classifier uses to tell
    a channel-only rejection from a structural one.
    """
    table = channel_table(chan)

    def walk(x: Global) -> str:
        if isinstance(x, GEnd):
            return "GEnd"
        if isinstance(x, GVar):
            return f"(GVar {x.index})"
        if isinstance(x, GRec):
            return f"(GRec {walk(x.body)})"
        if isinstance(x, GMsg):
            return (f"(GMsg {_coq_action(x.frm, x.to, table, chan)} {_coq_sort(x.sort)} "
                    f"{walk(x.cont)})")
        items = "".join(f"{walk(b)} :: " for b in x.branches)
        return f"(GBranch {_coq_action(x.frm, x.to, table, chan)} ({items}nil))"

    return walk(g)


def decode_local(codes: list[int]) -> Local | None:
    """Decode the nat-list encoding that the oracle driver prints.

    ``[0]`` is a rejected projection.  ``[1, …]`` is an accepted one,
    followed by the prefix encoding of the local type (see the driver
    in rocq.py for the table).  O(length).
    """
    if not codes:
        raise ValueError("decode_local: empty encoding")
    if codes[0] == 0:
        if len(codes) != 1:
            raise ValueError(f"decode_local: trailing data after a rejection: {codes}")
        return None
    pos = 1
    sorts = {0: "nat", 1: "bool"}

    def take() -> int:
        nonlocal pos
        if pos >= len(codes):
            raise ValueError(f"decode_local: truncated encoding {codes}")
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
                raise ValueError(f"decode_local: unknown sort code in {codes}")
            return LMsg(send, ch, sort, one())
        if tag == 3:
            send = take() == 0
            ch = take()
            n = take()
            return LBranch(send, ch, tuple(one() for _ in range(n)))
        if tag == 4:
            return LRec(one())
        raise ValueError(f"decode_local: unknown tag {tag} in {codes}")

    e = one()
    if pos != len(codes):
        raise ValueError(f"decode_local: trailing data in {codes}")
    return e


# ── C++ printers ─────────────────────────────────────────────────────

PRELUDE_NS = "session_oracle"


def cpp_sort(sort: str) -> str:
    """Return the payload type for ``sort``."""
    return {"nat": f"{PRELUDE_NS}::Nat", "bool": f"{PRELUDE_NS}::Bool"}[sort]


def cpp_role(role: int) -> str:
    """Return the role tag for ``role``."""
    return f"{PRELUDE_NS}::R{role}"


def cpp_old_global(g: Global, ns: str = "pr") -> str:
    """Spell ``g`` in the global-type DSL of the frozen tree."""
    if isinstance(g, GEnd):
        return f"{ns}::End_G"
    if isinstance(g, GVar):
        if g.index != 0:
            raise UntranslatableError(f"variable var{g.index} skips a binder")
        return f"{ns}::Var_G"
    if isinstance(g, GRec):
        return f"{ns}::Rec_G<{cpp_old_global(g.body, ns)}>"
    if isinstance(g, GMsg):
        return (f"{ns}::Transmission<{cpp_role(g.frm)}, {cpp_role(g.to)}, "
                f"{cpp_sort(g.sort)}, {cpp_old_global(g.cont, ns)}>")
    inner = "".join(
        f", {ns}::BranchG<{PRELUDE_NS}::Label<{k}>, {cpp_old_global(b, ns)}>"
        for k, b in enumerate(g.branches))
    return f"{ns}::Choice<{cpp_role(g.frm)}, {cpp_role(g.to)}{inner}>"


def cpp_old_local(e: Local, ns: str = "pr") -> str:
    """Spell an oracle local type as a frozen-tree protocol.

    The frozen tree projects a choice to a Select or an Offer whose
    branch k first sends or receives the label ``Label<k>``, so a
    branch of the oracle maps to that shape.
    """
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_old_local(e.body, ns)}>"
    if isinstance(e, LMsg):
        head = "Send" if e.send else "Recv"
        return f"{ns}::{head}<{cpp_sort(e.sort)}, {cpp_old_local(e.cont, ns)}>"
    step = "Send" if e.send else "Recv"
    inner = ", ".join(
        f"{ns}::{step}<{PRELUDE_NS}::Label<{k}>, {cpp_old_local(b, ns)}>"
        for k, b in enumerate(e.branches))
    return f"{ns}::{'Select' if e.send else 'Offer'}<{inner}>"


def cpp_fixy_local(e: Local, ns: str = "fs") -> str:
    """Spell an oracle local type as a fixy protocol.

    A fixy Select or Offer carries its branches positionally and sends
    no label value, so branch k maps to the k-th type argument.
    """
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_fixy_local(e.body, ns)}>"
    if isinstance(e, LMsg):
        head = "Send" if e.send else "Recv"
        return f"{ns}::{head}<{cpp_sort(e.sort)}, {cpp_fixy_local(e.cont, ns)}>"
    inner = ", ".join(cpp_fixy_local(b, ns) for b in e.branches)
    return f"{ns}::{'Select' if e.send else 'Offer'}<{inner}>"


def cpp_fixy_global(g: Global, ns: str = "fg") -> str:
    """Spell ``g`` in fixy::session::global.

    A message is a transmission with one branch whose label is Val and
    whose payload is the sort.  Branch k of a choice has the label
    Label<k> and the payload Unit.  So a message and a choice with one
    branch stay different, as EMsg and EBranch are in the oracle.
    """
    if isinstance(g, GEnd):
        return f"{ns}::End"
    if isinstance(g, GVar):
        if g.index != 0:
            raise UntranslatableError(f"variable var{g.index} skips a binder")
        return f"{ns}::Var"
    if isinstance(g, GRec):
        return f"{ns}::Rec<{cpp_fixy_global(g.body, ns)}>"
    if isinstance(g, GMsg):
        return (f"{ns}::Comm<{cpp_role(g.frm)}, {cpp_role(g.to)}, {ns}::Branch<{PRELUDE_NS}::Val, "
                f"{cpp_sort(g.sort)}, {cpp_fixy_global(g.cont, ns)}>>")
    inner = "".join(f", {ns}::Branch<{PRELUDE_NS}::Label<{k}>, {PRELUDE_NS}::Unit, "
                    f"{cpp_fixy_global(b, ns)}>" for k, b in enumerate(g.branches))
    return f"{ns}::Comm<{cpp_role(g.frm)}, {cpp_role(g.to)}{inner}>"


def _peer_of(e: LMsg | LBranch | LChoice) -> int:
    if not 0 <= e.channel < 64:
        raise UntranslatableError(f"channel {e.channel} names no peer")
    return e.channel % 8 if e.send else e.channel // 8


def cpp_fixy_peer_local(e: Local, ns: str = "fs") -> str:
    """Spell an oracle local type in the peer-annotated form of fixy's projection.

    The channel of each action gives its peer.  A choice with one branch
    is a plain Send or Recv, as fixy::session::project_t writes it.
    """
    if isinstance(e, LEnd):
        return f"{ns}::End"
    if isinstance(e, LVar):
        if e.index != 0:
            raise UntranslatableError(f"variable var{e.index} skips a binder")
        return f"{ns}::Continue"
    if isinstance(e, LRec):
        return f"{ns}::Loop<{cpp_fixy_peer_local(e.body, ns)}>"
    if isinstance(e, LChoice):
        raise UntranslatableError("a labelled choice is not an oracle answer")
    peer = cpp_role(_peer_of(e))
    head = "Send" if e.send else "Recv"
    if isinstance(e, LMsg):
        return (f"{ns}::{head}<{ns}::PeerMsg<{peer}, {PRELUDE_NS}::Val, {cpp_sort(e.sort)}>, "
                f"{cpp_fixy_peer_local(e.cont, ns)}>")
    arms = [f"{ns}::{head}<{ns}::PeerMsg<{peer}, {PRELUDE_NS}::Label<{k}>, {PRELUDE_NS}::Unit>, "
            f"{cpp_fixy_peer_local(b, ns)}>" for k, b in enumerate(e.branches)]
    if len(arms) == 1:
        return arms[0]
    if e.send:
        return f"{ns}::Select<{', '.join(arms)}>"
    return f"{ns}::Offer<{', '.join([f'{ns}::Sender<{peer}>', *arms])}>"


def cpp_prelude() -> str:
    """Return the declarations that every generated translation unit shares."""
    return (
        f"namespace {PRELUDE_NS} {{\n"
        "struct Nat {};\n"
        "struct Bool {};\n"
        "struct Unit {};\n"
        "struct Val {};\n"
        "struct R0 {};\n"
        "struct R1 {};\n"
        "struct R2 {};\n"
        "struct R3 {};\n"
        "template <unsigned I>\n"
        "struct Label {};\n"
        f"}}  // namespace {PRELUDE_NS}\n")
