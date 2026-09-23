"""Run a system of local types and find a bad execution.

A relation that accepts a global type claims that the projected local
types, run together, cannot go wrong.  This module checks the claim by
exploring every interleaving of an asynchronous system, breadth first,
so the first bad state that it finds has a shortest trace.

The semantics is the one of Tirore, Bengtson and Carbone (ECOOP 2025):
a send never blocks and puts its message on the FIFO queue of its
channel; a receive takes the oldest message of the channel that its
type names.  When each ordered pair of roles has its own channel
(model.channel_of), the queues are the per-pair queues of implicit
channels.  When two pairs share a channel, as in an explicit-channel
global type, they share one queue.  A receive that names no channel
(channel -1) takes the oldest message sent to its role, whoever sent
it.  That is the only way to run a local type of the frozen tree, whose
Recv names no peer.

The bad states:

  wrong-type   a receive takes a value of the wrong sort, a label where
               a value is expected, a value where a label is expected,
               or a label that the offer does not have
  deadlock     no role can move, and a role is not at End
  orphan       every role is at End, and a message stays in a queue
  spin         a role reaches a recursion whose body loops back before
               an action: it can never act again and never reach End
  starvation   no state is bad, but a role waits for ever on a fair
               cycle of the other roles (see _starvation)

Each channel queue is bounded by QUEUE_BOUND.  A state where the only
moves are sends to a full queue is cut, not reported, so the bound
cannot invent a deadlock.  An exploration that visits STATE_BOUND
states stops with the verdict ``unknown``.
"""

from __future__ import annotations

import itertools
from collections import deque
from dataclasses import dataclass

from model import LBranch, LChoice, LEnd, LMsg, LRec, LVar, Local

QUEUE_BOUND = 3
STATE_BOUND = 60_000
UNFOLD_BOUND = 32

_SPIN = LVar(-1)


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of one exploration."""

    kind: str
    trace: tuple[str, ...]
    detail: str = ""

    @property
    def is_safe(self) -> bool:
        """Return true when no bad state was found."""
        return self.kind in ("safe", "safe-within-bound")

    def text(self) -> str:
        """Return a one-line form for the golden file."""
        if self.is_safe or self.kind == "unknown":
            return self.kind
        steps = " ".join(self.trace) if self.trace else "(start)"
        detail = f" ({self.detail})" if self.detail else ""
        return f"{self.kind}{detail} after {steps}"


def _subst(e: Local, value: Local, depth: int = 0) -> Local:
    """Replace the variable of the removed binder with the closed ``value``."""
    if isinstance(e, LEnd):
        return e
    if isinstance(e, LVar):
        if e.index == depth:
            return value
        return LVar(e.index - 1) if e.index > depth else e
    if isinstance(e, LRec):
        return LRec(_subst(e.body, value, depth + 1))
    if isinstance(e, LMsg):
        return LMsg(e.send, e.channel, e.sort, _subst(e.cont, value, depth))
    if isinstance(e, LChoice):
        return LChoice(e.send, e.channel, tuple((lab, sort, _subst(k, value, depth))
                                                 for lab, sort, k in e.branches))
    return LBranch(e.send, e.channel, tuple(_subst(b, value, depth) for b in e.branches))


def _as_choice(e: LMsg | LBranch | LChoice) -> LChoice:
    """Read a message or a branch node as a choice with labelled branches."""
    if isinstance(e, LChoice):
        return e
    if isinstance(e, LMsg):
        return LChoice(e.send, e.channel, (("v", e.sort, e.cont),))
    return LChoice(e.send, e.channel, tuple((k, "unit", b) for k, b in enumerate(e.branches)))


def head(e: Local) -> Local:
    """Unfold ``e`` until its head is an action or End.

    Return _SPIN when the unfolding reaches a variable before an action,
    or does not reach an action in UNFOLD_BOUND steps.  An action comes
    back as an LChoice.  O(UNFOLD_BOUND × size).
    """
    for _ in range(UNFOLD_BOUND):
        if isinstance(e, LRec):
            e = _subst(e.body, e)
            continue
        if isinstance(e, LVar):
            return _SPIN
        if isinstance(e, LEnd):
            return e
        return _as_choice(e)
    return _SPIN


def equal_up_to_unfolding(a: Local, b: Local) -> bool:
    """Return true when ``a`` and ``b`` unfold to the same infinite tree.

    A coinductive check over pairs of head forms.  Channels and labels
    must agree.  O(pairs × size), and the pairs are finite because both
    types are regular.
    """
    seen: set[tuple[Local, Local]] = set()
    todo = [(a, b)]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen:
            continue
        seen.add((x, y))
        hx, hy = head(x), head(y)
        if hx is _SPIN or hy is _SPIN:
            if not (hx is _SPIN and hy is _SPIN):
                return False
            continue
        if isinstance(hx, LEnd) or isinstance(hy, LEnd):
            if not (isinstance(hx, LEnd) and isinstance(hy, LEnd)):
                return False
            continue
        assert isinstance(hx, LChoice) and isinstance(hy, LChoice)
        if (hx.send, hx.channel) != (hy.send, hy.channel):
            return False
        bx = {lab: (sort, k) for lab, sort, k in hx.branches}
        by = {lab: (sort, k) for lab, sort, k in hy.branches}
        if bx.keys() != by.keys():
            return False
        for lab, (sort, k) in bx.items():
            if by[lab][0] != sort:
                return False
            todo.append((k, by[lab][1]))
    return True


def _receiver(ch: int, me: int) -> int:
    """Return the receiver of a send by ``me`` on ``ch``, or -1 if unknown.

    Only a per-pair channel (model.channel_of) names its receiver.  A
    shared channel of the paper corpus uses a number of 64 or more.
    """
    if 0 <= ch < 64 and ch // 8 == me:
        return ch % 8
    return -1


def _item(label: str | int, sort: str) -> str:
    return sort if label == "v" else f"l{label}"


def _match(e: LChoice, me: int, flight: tuple) -> int | None:
    """Return the position of the message that the receive ``e`` takes, or None."""
    if e.channel < 0:
        return next((p for p, m in enumerate(flight) if m[2] == me), None)
    return next((p for p, m in enumerate(flight) if m[0] == e.channel), None)


def _enabled(e: Local, me: int, flight: tuple) -> bool:
    """Return true when role ``me`` can act in the unbounded semantics."""
    if not isinstance(e, LChoice):
        return False
    return e.send or _match(e, me, flight) is not None


def explore(system: dict[int, Local], liveness: bool = True) -> Verdict:
    """Explore every interleaving of ``system``.  Return the first bad state.

    ``system`` maps each role to its local type.  A state is the local
    type of each role and the ordered list of messages in flight, each a
    tuple (channel, sender, receiver, label, sort).  Breadth first, so a
    returned trace is a shortest one.  When no state is bad and
    ``liveness`` is true, the state graph is searched for starvation
    (see _starvation).  O(states × roles × messages).
    """
    roles = sorted(system)
    start = (tuple(head(system[r]) for r in roles), ())
    parent: dict[tuple, tuple[tuple | None, str]] = {start: (None, "")}
    edges: list[tuple[tuple, tuple, int]] = []
    frontier: deque[tuple] = deque([start])
    cut = False

    def trace_of(state: tuple) -> tuple[str, ...]:
        steps: list[str] = []
        while True:
            prev, step = parent[state]
            if prev is None:
                return tuple(reversed(steps))
            steps.append(step)
            state = prev

    while frontier:
        state = frontier.popleft()
        locals_, flight = state
        for i, e in enumerate(locals_):
            if e is _SPIN:
                return Verdict("spin", trace_of(state), f"role {roles[i]} loops without an action")
        moves: list[tuple[tuple, str, int]] = []
        blocked = False
        for i, e in enumerate(locals_):
            me = roles[i]
            if not isinstance(e, LChoice):
                continue
            if e.send:
                to = _receiver(e.channel, me)
                if e.channel < 0:
                    return Verdict("wrong-type", trace_of(state), f"role {me} sends on no channel")
                if 0 <= e.channel < 64 and to not in system:
                    return Verdict("wrong-type", trace_of(state),
                                   f"role {me} sends to role {to}, which is not in the session")
                if sum(1 for m in flight if m[0] == e.channel) >= QUEUE_BOUND:
                    blocked = True
                    continue
                shown = to if to >= 0 else f"ch{e.channel}"
                for label, sort, cont in e.branches:
                    new_locals = locals_[:i] + (head(cont),) + locals_[i + 1:]
                    moves.append(((new_locals, flight + ((e.channel, me, to, label, sort),)),
                                  f"{me}>{shown}:{_item(label, sort)}", me))
                continue
            pos = _match(e, me, flight)
            if pos is None:
                continue
            _, sender, _, label, sort = flight[pos]
            step = f"{me}<{sender}:{_item(label, sort)}"
            chosen = next((k for lab, srt, k in e.branches if lab == label), None)
            if chosen is None:
                offered = ",".join(_item(lab, srt) for lab, srt, _ in e.branches)
                return Verdict("wrong-type", trace_of(state) + (step,),
                               f"role {me} offers {{{offered}}} and takes {_item(label, sort)}")
            expected = next(srt for lab, srt, _ in e.branches if lab == label)
            if expected != sort:
                return Verdict("wrong-type", trace_of(state) + (step,),
                               f"role {me} expects {expected} and takes {sort}")
            new_locals = locals_[:i] + (head(chosen),) + locals_[i + 1:]
            moves.append(((new_locals, flight[:pos] + flight[pos + 1:]), step, me))
        if not moves:
            if blocked:
                cut = True
                continue
            waiting = [roles[i] for i, e in enumerate(locals_) if not isinstance(e, LEnd)]
            if waiting:
                return Verdict("deadlock", trace_of(state),
                               "waiting: " + ",".join(map(str, waiting)))
            if flight:
                return Verdict("orphan", trace_of(state),
                               f"{len(flight)} message(s) never received")
            continue
        for nxt, step, actor in moves:
            edges.append((state, nxt, actor))
            if nxt in parent:
                continue
            if len(parent) >= STATE_BOUND:
                return Verdict("unknown", trace_of(state), f"more than {STATE_BOUND} states")
            parent[nxt] = (state, step)
            frontier.append(nxt)
    if liveness:
        starving = _starvation(roles, list(parent), edges)
        if starving is not None:
            role, entry = starving
            return Verdict("starvation", trace_of(entry),
                           f"role {role} waits for ever on a fair cycle of the other roles")
    return Verdict("safe-within-bound" if cut else "safe", ())


def explore_sync(left: Local, right: Local) -> Verdict:
    """Run two local types with synchronous communication.

    A send fires only together with the receive that takes it, so no
    message waits in a queue.  This is the semantics of the synchronous
    subtyping relation: T refines U exactly when T runs against the dual
    of U without a wrong message, a deadlock or a loop that never acts.
    Breadth first.  O(pairs of states).
    """
    start = (head(left), head(right))
    parent: dict[tuple, tuple[tuple | None, str]] = {start: (None, "")}
    frontier: deque[tuple] = deque([start])

    def trace_of(state: tuple) -> tuple[str, ...]:
        steps: list[str] = []
        while True:
            prev, step = parent[state]
            if prev is None:
                return tuple(reversed(steps))
            steps.append(step)
            state = prev

    while frontier:
        state = frontier.popleft()
        a, b = state
        for who, e in enumerate(state):
            if e is _SPIN:
                return Verdict("spin", trace_of(state), f"side {who} loops without an action")
        if isinstance(a, LEnd) and isinstance(b, LEnd):
            continue
        if not (isinstance(a, LChoice) and isinstance(b, LChoice)) or a.send == b.send:
            return Verdict("deadlock", trace_of(state), "no send meets a receive")
        sender, receiver = (a, b) if a.send else (b, a)
        moves: list[tuple[tuple, str]] = []
        for label, sort, cont in sender.branches:
            match = next(((srt, k) for lab, srt, k in receiver.branches if lab == label), None)
            step = f"{'0>1' if a.send else '1>0'}:{_item(label, sort)}"
            if match is None or match[0] != sort:
                return Verdict("wrong-type", trace_of(state) + (step,),
                               f"the receiver has no branch for {_item(label, sort)}")
            nxt = (head(cont), head(match[1])) if a.send else (head(match[1]), head(cont))
            moves.append((nxt, step))
        if not moves:
            return Verdict("deadlock", trace_of(state), "a choice with no branch cannot send")
        for nxt, step in moves:
            if nxt not in parent:
                if len(parent) >= STATE_BOUND:
                    return Verdict("unknown", trace_of(state), f"more than {STATE_BOUND} states")
                parent[nxt] = (state, step)
                frontier.append(nxt)
    return Verdict("safe", ())


def _sccs(nodes: list[tuple], succ: dict[tuple, list[tuple]]) -> list[list[tuple]]:
    """Return the strongly connected components of a graph (iterative Tarjan)."""
    index: dict[tuple, int] = {}
    low: dict[tuple, int] = {}
    on_stack: set[tuple] = set()
    stack: list[tuple] = []
    out: list[list[tuple]] = []
    counter = 0
    for root in nodes:
        if root in index:
            continue
        work = [(root, 0)]
        while work:
            node, child = work.pop()
            if child == 0:
                index[node] = low[node] = counter
                counter += 1
                stack.append(node)
                on_stack.add(node)
            recurse = False
            nexts = succ.get(node, [])
            for j in range(child, len(nexts)):
                nxt = nexts[j]
                if nxt not in index:
                    work.append((node, j + 1))
                    work.append((nxt, 0))
                    recurse = True
                    break
                if nxt in on_stack:
                    low[node] = min(low[node], index[nxt])
            if recurse:
                continue
            if low[node] == index[node]:
                comp: list[tuple] = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    comp.append(w)
                    if w == node:
                        break
                out.append(comp)
            if work:
                parent_node = work[-1][0]
                low[parent_node] = min(low[parent_node], low[node])
    return out


def _starvation(roles: list[int], states: list[tuple],
                edges: list[tuple[tuple, tuple, int]]) -> tuple[int, tuple] | None:
    """Find a role that waits for ever while the others keep going.

    Role r starves when a reachable cycle exists on which r takes no
    step, r waits on a receive that is disabled at some state of the
    cycle, and every other role either acts on the cycle or is disabled
    at some state of it.  The last condition is weak fairness in the
    unbounded semantics, so a sender that only the queue bound stops
    cannot hide a cycle.  Balanced global types rule starvation out
    (Pischke, Masters, Yoshida, Definition 14 and Example 12).
    O(roles × (states + edges)).
    """
    for idx, r in enumerate(roles):
        succ: dict[tuple, list[tuple]] = {}
        acts: dict[tuple[tuple, tuple], set[int]] = {}
        for a, b, actor in edges:
            if actor == r:
                continue
            succ.setdefault(a, []).append(b)
            acts.setdefault((a, b), set()).add(actor)
        for comp in _sccs(states, succ):
            members = set(comp)
            inner = [(a, b) for a in comp for b in succ.get(a, []) if b in members]
            if not inner:
                continue
            e = comp[0][0][idx]
            if not isinstance(e, LChoice) or e.send:
                continue
            if all(_enabled(st[0][idx], r, st[1]) for st in comp):
                continue
            actors = set().union(*(acts[pair] for pair in inner))
            fair = True
            for jdx, s in enumerate(roles):
                if s == r or s in actors:
                    continue
                if all(_enabled(st[0][jdx], s, st[1]) for st in comp):
                    fair = False
                    break
            if fair:
                return r, min(comp, key=lambda st: (len(st[1]), repr(st)))
    return None


# ── Peer bindings for local types that name no peer ──────────────────


def _positions(e: Local, prefix: tuple[int, ...] = ()) -> list[tuple[tuple[int, ...], bool]]:
    """Return the path and the direction of every action in ``e``, preorder."""
    if isinstance(e, (LEnd, LVar)):
        return []
    if isinstance(e, LRec):
        return _positions(e.body, prefix + (0,))
    out = [(prefix, e.send)]
    if isinstance(e, LMsg):
        return out + _positions(e.cont, prefix + (0,))
    for k, b in enumerate(e.branches):
        out += _positions(b, prefix + (k,))
    return out


def _bind(e: Local, me: int, peers: dict[tuple[int, ...], int],
          prefix: tuple[int, ...] = ()) -> Local:
    """Give each action of ``e`` the peer that ``peers`` names for its path."""
    if isinstance(e, (LEnd, LVar)):
        return e
    if isinstance(e, LRec):
        return LRec(_bind(e.body, me, peers, prefix + (0,)))
    peer = peers.get(prefix)
    if peer is None:
        ch = -1
    else:
        ch = me * 8 + peer if e.send else peer * 8 + me
    if isinstance(e, LMsg):
        return LMsg(e.send, ch, e.sort, _bind(e.cont, me, peers, prefix + (0,)))
    return LBranch(e.send, ch, tuple(_bind(b, me, peers, prefix + (k,))
                                     for k, b in enumerate(e.branches)))


MAX_BINDINGS = 729


@dataclass(frozen=True, slots=True)
class BindingReport:
    """The verdicts of every peer binding of a system with peerless roles."""

    total: int
    safe: tuple[str, ...]
    failures: tuple[tuple[str, Verdict], ...]
    skipped: bool

    def text(self) -> str:
        """Return a one-line form for the golden file."""
        if self.skipped:
            return f"not explored: more than {MAX_BINDINGS} peer bindings"
        if self.safe:
            return f"{len(self.safe)} of {self.total} peer bindings safe, e.g. {self.safe[0]}"
        binding, verdict = self.failures[0]
        return f"all {self.total} peer bindings fail, e.g. {binding}: {verdict.text()}"


def explore_bindings(fixed: dict[int, Local], peerless: dict[int, Local],
                     candidates: dict[int, list[int]], inbox: bool) -> BindingReport:
    """Explore every static peer binding of the ``peerless`` roles.

    A static binding gives each action position one peer, the same on
    every loop iteration, because a local type is all that an
    implementation of the role knows.  With ``inbox`` true the receives
    stay unbound and read the inbox head, and only the sends are bound.
    ``fixed`` roles keep their channels.  O(bindings × explore).
    """
    slots: list[tuple[int, tuple[int, ...]]] = []
    for r, e in sorted(peerless.items()):
        for path, send in _positions(e):
            if send or not inbox:
                slots.append((r, path))
    options = [candidates[r] for r, _ in slots]
    total = 1
    for o in options:
        total *= max(len(o), 1)
    if total > MAX_BINDINGS or any(not o for o in options):
        return BindingReport(total, (), (), True)
    safe: list[str] = []
    failures: list[tuple[str, Verdict]] = []
    for choice in itertools.product(*options):
        peers: dict[int, dict[tuple[int, ...], int]] = {r: {} for r in peerless}
        for (r, path), peer in zip(slots, choice, strict=True):
            peers[r][path] = peer
        system = dict(fixed)
        for r, e in peerless.items():
            system[r] = _bind(e, r, peers[r])
        name = ";".join(f"{r}@{'.'.join(map(str, p)) or 'top'}->{q}"
                        for (r, p), q in zip(slots, choice, strict=True)) or "no action to bind"
        verdict = explore(system)
        if verdict.is_safe:
            safe.append(name)
        else:
            failures.append((name, verdict))
    return BindingReport(total, tuple(safe), tuple(failures), False)
