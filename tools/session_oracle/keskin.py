"""Check the liveness of fixy's projected contexts with the Rocq theorem of Keskin et al. (ITP 2026).

The development is github.com/omerskeskin/mpstlive, which accompanies
"Formally Verified Liveness with Multiparty Session Types in Rocq" (Keskin,
Yoshida and van Glabbeek).  Its README states Coq 8.20.1, mathcomp-ssreflect
2.5, paco and mmaps, and the build also needs Equations.  It has no licence,
so nothing from it enters our tree, and only the verdicts do.  Its theorem
(STLive/lemma/liveness.v)

  liveness : forall gamma g, wfgC g -> projectableA g -> tctx_wf gamma ->
             assoc gamma g -> liveCtx gamma

gives the liveness of a typing context gamma from four premises: g is a
well-formed and balanced global type tree (wfgC, balancedG), the plain-merge
projection of g exists for every role (projectableA), every type of gamma
is well-formed (tctx_wf), and the type of each participant is a subtype of
its projection (assoc).

For a multiparty case that fixy projects onto every role, gamma is fixy's
projected context.  This module reads the global type as a finite set of
states, and computes the rank of each participant at each state (the
balance), the plain-merge projection and the subtyping pairs from fixy's
types to it.  When each premise holds by these computations, the module
writes a certificate: the states as data, one coinductive tree per state,
and an application of certificate_live (coq/LiveOracle.v), whose boolean
checks coqc runs by computation.  When a premise fails, the module names
the premise and writes no certificate, because the theorem then gives no
verdict.  A certificate that coqc refuses is a failure of this module,
never a verdict.

The encoding of a label as an index of the development's option lists:

  label "v" (a value message)       index 0
  label k (a positional branch)     index k + 1
  sort nat -> snat, bool and unit -> sbool
"""

from __future__ import annotations

import hashlib
import io
import logging
import os
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from execution import _SPIN, head
from model import GBranch, GEnd, GMsg, GRec, GVar, Global, LChoice, LEnd, Local
from rocq import OracleError, answer_cache_path, cache_root, load_answers, save_answers

LOG = logging.getLogger("session_oracle.keskin")

REPO = "omerskeskin/mpstlive"
COMMIT = "d96cf6842b9e8a0cc0b833cf5eb3f2e726e991dd"
LIBRARY = Path(__file__).resolve().parent / "coq" / "LiveOracle.v"
CHUNK = 8
CHUNK_TIMEOUT = 900
BUILD_JOBS = 8
# The axioms that a checked certificate may rest on: the extensionality of
# local type trees that the development assumes (src/balanced.v), and three
# axioms of the Coq standard library that its proofs use.
EXPECTED_AXIOMS = frozenset({
    "balanced.lltExt",
    "Eqdep.Eq_rect_eq.eq_rect_eq",
    "IndefiniteDescription.constructive_indefinite_description",
    "Classical_Prop.classic",
})

_SORT = {"nat": "snat", "bool": "sbool", "unit": "sbool"}


def _switch() -> list[str]:
    """Return the prefix that runs a command in the opam switch of the development.

    SESSION_ORACLE_KESKIN_SWITCH names the switch.  The default is
    sessoracle25 (scripts/session-oracle.sh says how to make it).
    """
    opam = shutil.which("opam") or str(Path.home() / ".local" / "bin" / "opam")
    if not Path(opam).is_file() and shutil.which(opam) is None:
        raise OracleError("opam is not on PATH.  The ITP 2026 development needs Coq 8.20.1 with "
                          "mathcomp-ssreflect 2.5, paco, mmaps and equations in an opam switch; "
                          "scripts/session-oracle.sh says how to install them.")
    return [opam, "exec", f"--switch={os.environ.get('SESSION_ORACLE_KESKIN_SWITCH', 'sessoracle25')}", "--"]


def coqc() -> list[str]:
    """Return the coqc command of the switch, or the one that SESSION_ORACLE_COQC25 names."""
    env = os.environ.get("SESSION_ORACLE_COQC25")
    return shlex.split(env) if env else [*_switch(), "coqc"]


def fetch() -> Path:
    """Download and unpack the pinned commit.  Return its root."""
    root = cache_root() / f"mpstlive-{COMMIT}"
    if (root / "_CoqProject").is_file():
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://github.com/{REPO}/archive/{COMMIT}.tar.gz"
    LOG.info("fetching %s", url)
    try:
        with urllib.request.urlopen(url, timeout=120) as resp:
            data = resp.read()
    except OSError as exc:
        raise OracleError(f"cannot download {url}: {exc}") from exc
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        prefix = f"mpstlive-{COMMIT}/"
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        tar.extractall(root.parent, members=members, filter="data")
    if not (root / "_CoqProject").is_file():
        raise OracleError(f"{root}: the archive has no _CoqProject")
    return root


def build(root: Path) -> None:
    """Compile the development with its own makefile.  A second call is a no-op."""
    if (root / "STLive" / "lemma" / "liveness.vo").is_file():
        return
    LOG.info("building the ITP 2026 development in %s (about ten minutes)", root)
    for cmd in ([*_switch(), "coq_makefile", "-f", "_CoqProject", "-o", "Makefile"],
                [*_switch(), "make", f"-j{BUILD_JOBS}"]):
        proc = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if proc.returncode != 0:
            raise OracleError(f"building the ITP 2026 development failed at {' '.join(cmd[-3:])}:\n"
                              f"{(proc.stdout + proc.stderr)[-3000:]}")


def library(root: Path) -> Path:
    """Compile coq/LiveOracle.v against the development.  Return the directory of the .vo file.

    The directory name carries a hash of the library text and the commit,
    so a changed library is compiled again and never mixed with an old one.
    """
    text = LIBRARY.read_text(encoding="utf-8")
    digest = hashlib.sha256((COMMIT + text).encode()).hexdigest()[:16]
    out = cache_root() / f"keskin-library-{digest}"
    if (out / "LiveOracle.vo").is_file():
        return out
    out.mkdir(parents=True, exist_ok=True)
    (out / "LiveOracle.v").write_text(text, encoding="utf-8")
    proc = subprocess.run([*coqc(), "-R", str(root), "live_mpst", "-R", str(out), "SessionOracle",
                           str(out / "LiveOracle.v")], cwd=out, capture_output=True, text=True)
    if proc.returncode != 0:
        raise OracleError(f"coq/LiveOracle.v does not compile against the ITP 2026 development:\n"
                          f"{(proc.stdout + proc.stderr)[-3000:]}")
    return out


# ── Global states ────────────────────────────────────────────────────
#
# A term of the development's inductive global syntax, as a tuple:
# ("var", n), ("end",), ("send", p, q, kids) with kids a tuple of None or
# (sort, term), and ("rec", body).


class Premise(Exception):
    """A premise of the theorem fails, so it gives no verdict.  ``name`` is the premise."""

    def __init__(self, name: str, detail: str) -> None:
        super().__init__(f"{name}: {detail}")
        self.name = name
        self.detail = detail


def _rename_inner(t: tuple, depth: int = 0) -> tuple:
    """Merge the two outer binders of a body: variables depth and depth + 1 become depth."""
    kind = t[0]
    if kind == "var":
        n = t[1]
        if n < depth:
            return t
        return ("var", depth) if n <= depth + 1 else ("var", n - 1)
    if kind == "end":
        return t
    if kind == "rec":
        return ("rec", _rename_inner(t[1], depth + 1))
    return ("send", t[1], t[2], tuple(None if k is None else (k[0], _rename_inner(k[1], depth)) for k in t[3]))


def _rec(body: tuple) -> tuple:
    """Return a recursion over ``body`` in the shape that the witness of wfgC needs.

    A recursion directly around a recursion becomes one binder.  A
    recursion around its own variable has no tree.  O(size).
    """
    while body[0] == "rec":
        body = _rename_inner(body[1])
    if body[0] == "var":
        if body[1] == 0:
            raise Premise("wfgC", "a recursion reaches its own variable before an action, so the type has "
                                  "no tree (guardG)")
        return ("var", body[1] - 1)
    return ("rec", body)


def term_of(g: Global) -> tuple:
    """Translate a global type of model.py into the development's syntax.  O(size)."""
    if isinstance(g, GEnd):
        return ("end",)
    if isinstance(g, GVar):
        return ("var", g.index)
    if isinstance(g, GRec):
        return _rec(term_of(g.body))
    if isinstance(g, GMsg):
        return ("send", g.frm, g.to, ((_SORT[g.sort], term_of(g.cont)),))
    assert isinstance(g, GBranch)
    return ("send", g.frm, g.to, (None,) + tuple(("sbool", term_of(b)) for b in g.branches))


def _subst(t: int, value: tuple, p: tuple) -> tuple:
    """The substitution of LiveOracle.gsubst for a closed ``value``."""
    kind = p[0]
    if kind == "var":
        n = p[1]
        if n == t:
            return value
        return ("var", n - 1) if n > t else p
    if kind == "end":
        return p
    if kind == "rec":
        return ("rec", _subst(t + 1, value, p[1]))
    return ("send", p[1], p[2], tuple(None if k is None else (k[0], _subst(t, value, k[1])) for k in p[3]))


def unfold(p: tuple) -> tuple:
    """The one-step unfolding LiveOracle.unf."""
    return _subst(0, p, p[1]) if p[0] == "rec" else p


@dataclass(slots=True)
class GlobalStates:
    """The states of a global type: a closed term, a head and the children of each."""

    terms: list[tuple]
    heads: list[tuple[int, int] | None]
    kids: list[tuple[tuple[str, int] | None, ...]]

    @classmethod
    def of(cls, g: Global) -> "GlobalStates":
        """Walk the terms that one-step unfolding reaches from the root.  O(states × size)."""
        root = term_of(g)
        if root[0] == "var":
            raise Premise("wfgC", "the type is a free variable")
        out = cls([], [], [])
        index: dict[tuple, int] = {}
        todo = deque([root])
        index[root] = 0
        out.terms.append(root)
        while todo:
            p = todo.popleft()
            h = unfold(p)
            if h[0] == "end":
                out.heads.append(None)
                out.kids.append(())
                continue
            if h[0] != "send":
                raise OracleError(f"keskin: a state unfolds to {h[0]}, not to an action or an end")
            kids = []
            for k in h[3]:
                if k is None:
                    kids.append(None)
                    continue
                if k[1] not in index:
                    index[k[1]] = len(out.terms)
                    out.terms.append(k[1])
                    todo.append(k[1])
                kids.append((k[0], index[k[1]]))
            out.heads.append((h[1], h[2]))
            out.kids.append(tuple(kids))
        return out

    def parts(self) -> list[int]:
        """Return the roles that act in some state, in order."""
        return sorted({r for h in self.heads if h is not None for r in h})

    def ranks(self, p: int) -> list[int | None]:
        """Return the rank of ``p`` at each state (LiveOracle.rank_ok).

        None when ``p`` never acts below the state.  Otherwise the largest
        number of steps before ``p`` acts on a path from the state.  Raise
        Premise when a path from a state where ``p`` can act never reaches
        an action of ``p`` (balancedG).  O(states²).
        """
        n = len(self.terms)
        acts = [h is not None and p in h for h in self.heads]
        reach = list(acts)
        changed = True
        while changed:
            changed = False
            for i in range(n):
                if not reach[i] and any(k is not None and reach[k[1]] for k in self.kids[i]):
                    reach[i] = changed = True
        rank: list[int | None] = [0 if acts[i] else None for i in range(n)]
        changed = True
        while changed:
            changed = False
            for i in range(n):
                if rank[i] is not None or not reach[i]:
                    continue
                below = [rank[k[1]] for k in self.kids[i] if k is not None]
                if below and all(r is not None for r in below):
                    rank[i] = 1 + max(r for r in below if r is not None)
                    changed = True
        for i in range(n):
            if reach[i] and rank[i] is None:
                raise Premise("balancedG", f"role {p} can act below state {i}, and a path from it ends or "
                                           f"loops without an action of role {p}")
        return rank


# ── Local states ─────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class LState:
    """A local state: kind 0 (end), 1 (send) or 2 (receive), the peer, and the children."""

    kind: int
    peer: int
    kids: tuple[tuple[str, int] | None, ...]


def _label_index(label: str | int) -> int:
    if label == "v":
        return 0
    if isinstance(label, int):
        return label + 1
    raise OracleError(f"keskin: no label index for {label!r}")


def plain_projection(gs: GlobalStates, p: int, rank: list[int | None],
                     states: list[LState]) -> list[int]:
    """Append the plain-merge projection onto ``p`` to ``states``.  Return the local state of each global state.

    The projection rules of the development (src/projection.v): a state
    where ``p`` never acts projects to end, a message of ``p`` to a send or
    a receive, and any other message to the plain merge of its branches,
    which must all project to the same tree (isMerge).  Trees are equal
    when they are bisimilar, because the development assumes lltExt.
    Raise Premise when a merge fails (projectableA).  Partition
    refinement, O(states² × labels).
    """
    n = len(gs.terms)
    acting = [i for i in range(n) if rank[i] is not None and gs.heads[i] is not None and p in gs.heads[i]]
    act_set = set(acting)

    def resolve(i: int, memo: dict[int, frozenset]) -> frozenset:
        """The acting states (and -1 for end) that the merge of state i ranges over."""
        if i in memo:
            return memo[i]
        if rank[i] is None:
            memo[i] = frozenset({-1})
        elif i in act_set:
            memo[i] = frozenset({i})
        else:
            memo[i] = frozenset().union(*(resolve(k[1], memo) for k in gs.kids[i] if k is not None))
        return memo[i]

    memo: dict[int, frozenset] = {}
    for i in range(n):
        resolve(i, memo)

    def signature(i: int) -> tuple:
        frm, to = gs.heads[i]  # type: ignore[misc]
        send = frm == p
        return (1 if send else 2, to if send else frm,
                tuple(None if k is None else k[0] for k in gs.kids[i]))

    def renumber(keys: dict[int, object]) -> dict[int, int]:
        ids: dict[object, int] = {}
        return {i: ids.setdefault(v, len(ids)) for i, v in keys.items()}

    klass = renumber({-1: ("end",)} | {i: signature(i) for i in acting})
    while True:
        def kid_class(k: tuple[str, int] | None) -> object:
            if k is None:
                return None
            return frozenset(klass[m] for m in memo[k[1]])
        refined = renumber({-1: (klass[-1],)} | {i: (klass[i], tuple(kid_class(k) for k in gs.kids[i]))
                                                 for i in acting})
        if len(set(refined.values())) == len(set(klass.values())):
            break
        klass = refined
    for i in range(n):
        if len({klass[m] for m in memo[i]}) > 1:
            raise Premise("projectableA", f"the branches of state {i} project onto role {p} to different "
                                          "trees, so the plain merge fails (isMerge)")
    end = _end_state(states)
    names: dict[int, int] = {}
    order: list[int] = []
    for i in acting:
        if klass[i] not in names:
            names[klass[i]] = len(states) + len(order)
            order.append(klass[i])
    rep = {klass[i]: i for i in reversed(acting)}

    def local_of(i: int) -> int:
        cls = klass[next(iter(memo[i]))]
        return end if cls == klass[-1] else names[cls]

    for cls in order:
        i = rep[cls]
        kind, peer, _ = signature(i)
        states.append(LState(kind, peer, tuple(None if k is None else (k[0], local_of(k[1]))
                                               for k in gs.kids[i])))
    return [local_of(i) for i in range(n)]


def _end_state(states: list[LState]) -> int:
    """Return the index of an end state, and add one when there is none."""
    for j, s in enumerate(states):
        if s.kind == 0:
            return j
    states.append(LState(0, 0, ()))
    return len(states) - 1


def context_states(role: int, e: Local, states: list[LState]) -> int:
    """Append the head forms of fixy's local type ``e`` of ``role`` to ``states``.  Return the root.

    A send names its channel role * 8 + peer, and a receive peer * 8 + role
    (model.channel_of).  O(states × size).
    """
    names: dict[Local, int] = {}
    pending: list[tuple[int, LChoice]] = []

    def name(x: Local) -> int:
        h = head(x)
        if h is _SPIN:
            raise Premise("wfgC", f"fixy's type of role {role} holds a loop that never acts, so it has no tree")
        if h in names:
            return names[h]
        if isinstance(h, LEnd):
            names[h] = len(states)
            states.append(LState(0, 0, ()))
            return names[h]
        assert isinstance(h, LChoice)
        names[h] = len(states)
        states.append(LState(0, 0, ()))
        pending.append((names[h], h))
        return names[h]

    root = name(e)
    while pending:
        j, h = pending.pop()
        slots: dict[int, tuple[str, int]] = {}
        for lab, sort, cont in h.branches:
            index = _label_index(lab)
            if index in slots:
                raise OracleError(f"keskin: role {role} has two branches with the label {lab!r}")
            slots[index] = (_SORT[sort], name(cont))
        width = max(slots) + 1
        peer = h.channel % 8 if h.send else h.channel // 8
        states[j] = LState(1 if h.send else 2, peer, tuple(slots.get(i) for i in range(width)))
    return root


def _subsort(sub: str, sup: str) -> bool:
    return sub == sup or (sub, sup) == ("snat", "sint")


def subtype_pairs(states: list[LState], a: int, b: int) -> list[tuple[int, int]]:
    """Return the pairs that the subtyping rules of the development reach from (a, b).

    The rules are subtypeC of src/local.v: a send of the subtype sends a
    subset of the labels of the supertype, a receive receives a superset,
    and the sorts are covariant and contravariant.  Raise Premise at the
    first failed pair (assoc).  O(states²).
    """
    seen = {(a, b)}
    order = [(a, b)]
    todo = deque(order)
    while todo:
        x, y = todo.popleft()
        s, t = states[x], states[y]
        if s.kind != t.kind or (s.kind != 0 and s.peer != t.peer):
            raise Premise("assoc", f"local state {x} against {y}: {_shape(s)} against {_shape(t)}")
        if s.kind == 0:
            continue
        sub_kids, sup_kids = (s.kids, t.kids)
        children = []
        if s.kind == 1:
            for i, k in enumerate(sub_kids):
                if k is None:
                    continue
                if i >= len(sup_kids) or sup_kids[i] is None:
                    raise Premise("assoc", f"local state {x} sends the label index {i}, which {y} does not send")
                if not _subsort(k[0], sup_kids[i][0]):  # type: ignore[index]
                    raise Premise("assoc", f"the sort of label index {i} of state {x} is not below that of {y}")
                children.append((k[1], sup_kids[i][1]))  # type: ignore[index]
        else:
            for i, k in enumerate(sup_kids):
                if k is None:
                    continue
                if i >= len(sub_kids) or sub_kids[i] is None:
                    raise Premise("assoc", f"local state {y} receives the label index {i}, which {x} does not")
                if not _subsort(k[0], sub_kids[i][0]):  # type: ignore[index]
                    raise Premise("assoc", f"the sort of label index {i} of state {y} is not below that of {x}")
                children.append((sub_kids[i][1], k[1]))  # type: ignore[index]
        for pair in children:
            if pair not in seen:
                seen.add(pair)
                order.append(pair)
                todo.append(pair)
    return order


def _shape(s: LState) -> str:
    return ("end", f"a send to role {s.peer}", f"a receive from role {s.peer}")[s.kind]


# ── Certificates ─────────────────────────────────────────────────────


@dataclass(slots=True)
class Instance:
    """Everything that one certificate states."""

    gs: GlobalStates
    parts: list[int]
    ranks: list[list[int | None]]
    local: list[LState]
    proj: list[list[int]]
    ctx: list[tuple[int, int]]
    sub: list[tuple[int, int]]


def analyse(g: Global, system: dict[int, Local]) -> Instance:
    """Compute the premises for global type ``g`` and fixy's context ``system``.

    Raise Premise when one fails.  O(states² × roles).
    """
    gs = GlobalStates.of(g)
    parts = gs.parts()
    ranks = [gs.ranks(p) for p in parts]
    local: list[LState] = [LState(0, 0, ())]
    proj = [plain_projection(gs, p, r, local) for p, r in zip(parts, ranks, strict=True)]
    ctx: list[tuple[int, int]] = []
    sub: list[tuple[int, int]] = []
    for role in sorted(system):
        root = context_states(role, system[role], local)
        ctx.append((role, root))
        if role in parts:
            for pair in subtype_pairs(local, root, proj[parts.index(role)][0]):
                if pair not in sub:
                    sub.append(pair)
        elif local[root].kind != 0:
            raise Premise("assoc", f"role {role} has no part in the global type, and fixy's type of it is not end")
    missing = [p for p in parts if p not in system]
    if missing:
        raise OracleError(f"keskin: the context has no type for the participants {missing}")
    return Instance(gs, parts, ranks, local, proj, ctx, sub)


def _coq_term(t: tuple) -> str:
    kind = t[0]
    if kind == "var":
        return f"(g_var {t[1]})"
    if kind == "end":
        return "g_end"
    if kind == "rec":
        return f"(g_rec {_coq_term(t[1])})"
    kids = "; ".join("None" if k is None else f"Some ({k[0]}, {_coq_term(k[1])})" for k in t[3])
    return f"(g_send {t[1]} {t[2]} [{kids}])"


def _coq_kids(kids: tuple[tuple[str, int] | None, ...], prefix: str | None) -> str:
    """Spell a list of children, as indices or, with ``prefix``, as tree constants."""
    return "[" + "; ".join("None" if k is None else f"Some ({k[0]}, {k[1] if prefix is None else prefix + str(k[1])})"
                           for k in kids) + "]"


def _unfolding(fn: str, eq: str, count: int, bound: str, shape: str) -> str:
    """Return the lemma that each state of ``fn`` unfolds to its shape, by one case per state."""
    cases = "".join(f"  destruct i as [|i]; [rewrite ({eq} ({fn} {i})); reflexivity|].\n" for i in range(count))
    return (f"Lemma {fn}_unf : forall i, i < {bound} dat -> {fn} i = {shape} dat {fn} i.\n"
            f"Proof.\n  intros i Hi.\n{cases}"
            f"  exfalso. assert (Hn : {bound} dat = {count}) by reflexivity. rewrite Hn in Hi. lia.\nQed.\n")


def certificate(key: str, inst: Instance) -> str:
    """Return the Rocq certificate that fixy's context of ``inst`` is live, under the module ``key``."""
    gs = inst.gs
    terms = "; ".join(_coq_term(t) for t in gs.terms)
    gstates = "; ".join("GS None []" if h is None else f"GS (Some ({h[0]}, {h[1]})) {_coq_kids(k, None)}"
                        for h, k in zip(gs.heads, gs.kids, strict=True))
    ranks = "; ".join("[" + "; ".join("None" if r is None else f"Some {r}" for r in rs) + "]" for rs in inst.ranks)
    lstates = "; ".join(f"LS {s.kind} {s.peer} {_coq_kids(s.kids, None)}" for s in inst.local)
    proj = "; ".join("[" + "; ".join(str(j) for j in js) + "]" for js in inst.proj)
    ctx = "; ".join(f"({p}, {j})" for p, j in inst.ctx)
    sub = "; ".join(f"({a}, {b})" for a, b in inst.sub)
    trees = "\nwith ".join(
        f"g{i} : gtt := " + ("gtt_end" if h is None else f"gtt_send {h[0]} {h[1]} {_coq_kids(k, 'g')}")
        for i, (h, k) in enumerate(zip(gs.heads, gs.kids, strict=True)))
    ltrees = "\nwith ".join(
        f"l{j} : ltt := " + ("ltt_end" if s.kind == 0 else
                             f"{'ltt_send' if s.kind == 1 else 'ltt_recv'} {s.peer} {_coq_kids(s.kids, 'l')}")
        for j, s in enumerate(inst.local))
    gmatch = " | ".join(f"{i} => g{i}" for i in range(len(gs.terms)))
    lmatch = " | ".join(f"{j} => l{j}" for j in range(len(inst.local)))
    return (f"Module {key}.\n"
            f"Definition dat : data := D\n  [{terms}]\n  [{gstates}]\n"
            f"  [{'; '.join(str(p) for p in inst.parts)}]\n  [{ranks}]\n  [{lstates}]\n  [{proj}]\n"
            f"  [{ctx}]\n  [{sub}].\n"
            f"CoFixpoint {trees}.\n"
            f"CoFixpoint {ltrees}.\n"
            f"Definition tree (i : nat) : gtt := match i with {gmatch} | _ => gtt_end end.\n"
            f"Definition ltree (j : nat) : ltt := match j with {lmatch} | _ => ltt_end end.\n"
            + _unfolding("tree", "gtt_eq", len(gs.terms), "n_g", "gshape")
            + _unfolding("ltree", "ltt_eq", len(inst.local), "n_l", "lshape")
            + "Theorem live : liveCtx (build ltree (d_ctx dat)).\n"
              "Proof.\n  apply (certificate_live dat tree ltree); [exact tree_unf | exact ltree_unf "
              "| vm_compute; reflexivity | vm_compute; reflexivity].\nQed.\n"
              f"End {key}.\n")


_HEAD = """\
Set Warnings "-non-full-mutual".
Require Import List Lia.
Import ListNotations.
From live_mpst.STBase Require Import src.expr src.global src.local.
From live_mpst.STLive Require Import src.lcontext src.path_props.
From SessionOracle Require Import LiveOracle.

"""

_AXIOM = re.compile(r"^([A-Za-z_][\w.']*) :", re.MULTILINE)


def _check(root: Path, lib: Path, blocks: list[tuple[str, str]]) -> tuple[bool, str]:
    """Compile the certificates ``blocks`` (key, text) in one file.  Return success and the reason."""
    text = _HEAD + "".join(t for _, t in blocks) + "".join(f"Print Assumptions {k}.live.\n" for k, _ in blocks)
    with tempfile.TemporaryDirectory(prefix="session_oracle_keskin_") as tmp:
        path = Path(tmp) / "KeskinCertificates.v"
        path.write_text(text, encoding="utf-8")
        try:
            proc = subprocess.run([*coqc(), "-R", str(root), "live_mpst", "-R", str(lib), "SessionOracle",
                                   str(path)], cwd=tmp, capture_output=True, text=True, timeout=CHUNK_TIMEOUT)
        except subprocess.TimeoutExpired:
            return False, f"timeout after {CHUNK_TIMEOUT} s"
    if proc.returncode != 0:
        return False, "coqc refused the certificate: " + " ".join((proc.stdout + proc.stderr).split())[-300:]
    extra = set(_AXIOM.findall(proc.stdout)) - EXPECTED_AXIOMS
    if extra:
        return False, f"the proof rests on axioms outside the expected set: {sorted(extra)}"
    return True, ""


def _planted(inst: Instance) -> Instance:
    """Return a copy of ``inst`` whose context type of the first participant has the wrong direction.

    Its certificate is wrong, so coqc must refuse it.  The check runs once
    per decision, so a checker that accepts everything cannot pass as one
    that accepts only the true certificates.
    """
    role, root = next((p, j) for p, j in inst.ctx if p in inst.parts)
    local = list(inst.local)
    s = local[root]
    if s.kind == 0:
        raise OracleError(f"keskin: the context type of participant {role} is end, so no control can be planted")
    local[root] = LState(3 - s.kind, s.peer, s.kids)
    return Instance(inst.gs, inst.parts, inst.ranks, local, inst.proj, inst.ctx, inst.sub)


def coq_version() -> str:
    """Return the version line of the coqc that checks the certificates."""
    out = subprocess.run([*coqc(), "--version"], check=True, capture_output=True, text=True).stdout
    return out.strip().splitlines()[0]


def decide(instances: list[Instance], workers: int = 8) -> list[tuple[bool, str]]:
    """Return, for each instance, whether coqc checks its certificate, and the reason when not.

    A checked verdict is kept in a cache whose file name carries the
    commit, keyed by a hash of the library and the certificate.  The other
    certificates run in chunks of CHUNK on ``workers`` coqc processes, and
    a chunk that fails runs again one certificate at a time.
    """
    root = fetch()
    build(root)
    lib = library(root)
    lib_text = LIBRARY.read_text(encoding="utf-8")
    if instances:
        accepted, _ = _check(root, lib, [("P", certificate("P", _planted(instances[0])))])
        if accepted:
            raise OracleError("keskin: coqc accepts a planted wrong certificate, so no verdict of the checker "
                              "counts")
    certs = [certificate(f"K{i}", inst) for i, inst in enumerate(instances)]
    keys = [hashlib.sha256((lib_text + _HEAD + certificate("K", inst)).encode()).hexdigest() for inst in instances]
    cache_path = answer_cache_path("keskin", COMMIT)
    cache = load_answers(cache_path)
    results: dict[int, tuple[bool, str]] = {i: (True, "") for i, k in enumerate(keys) if cache.get(k) is True}
    todo = [i for i in range(len(instances)) if i not in results]
    chunks = [todo[i:i + CHUNK] for i in range(0, len(todo), CHUNK)]
    LOG.info("%d certificates: %d from the cache, %d in %d chunks", len(instances), len(results), len(todo),
             len(chunks))
    try:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            outcomes = list(pool.map(lambda ch: _check(root, lib, [(f"K{i}", certs[i]) for i in ch]), chunks))
            retry: list[int] = []
            for chunk, (ok, _) in zip(chunks, outcomes, strict=True):
                if ok:
                    for i in chunk:
                        results[i] = (True, "")
                        cache[keys[i]] = True
                else:
                    retry += chunk
            singles = list(pool.map(lambda i: (i, _check(root, lib, [(f"K{i}", certs[i])])), retry))
        for i, (ok, reason) in singles:
            results[i] = (ok, reason)
            if ok:
                cache[keys[i]] = True
    finally:
        save_answers(cache_path, cache)
    return [results[i] for i in range(len(instances))]
