"""Walk the transition systems of fixy/session/Semantics.h against a run of the projected context.

Semantics.h gives two labelled transition systems (Barwell, Hou, Yoshida and
Zhou, LMCS 21:2, 2025, Definitions 4.12 and 4.18): one of global states and
one of configurations.  With every role reliable, the operational
correspondence of that paper and of Pischke, Masters and Yoshida says that a
global type and the context of its projections take the same labels, and that
association (Definition 4.19) holds after each step.

The reference here is the run of fixy's projected context by execution.py,
which knows nothing of Semantics.h: from a context it gives the labels that a
role can take, a send at an internal choice and a receive of the head message
of a queue.  This module walks the paths of that run up to DEPTH steps, and
asks Semantics.h along each path for:

  fixy.global_lts         the labels of the global state (state_enabled_t)
  fixy.config_lts         the labels of the configuration (config::enabled_t)
  fixy.crash_association  crash_association_holds_v of the configuration and
                          the global state that the path reaches

A case enters when fixy projects every role, so the run is the context of
fixy's own projection.  The full merge of Projection.h can project a type
that the plain merge of the oracles refuses, and such a type enters too.

A label is written as in execution.py: "p>q:x" for a send and "q<p:x" for a
receive, where x is the sort of a value or l<k> for the branch k.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

from emit import Row
from execution import _SPIN, _item, _match, _receiver, head
from model import Global, LChoice, Local, cpp_fixy_global, cpp_role, show_global
from probe import FIXY_NS, SpellingError, _label_of, _payload_of, _role_of, _split, probe_header, show

DEPTH = 3
MAX_NODES = 16
FAMILIES = ("fixy.global_lts", "fixy.config_lts", "fixy.crash_association")
BARWELL = "Barwell, Hou, Yoshida and Zhou, LMCS 21:2, 2025"
HEAD = "semantics"
ALIAS = "namespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n" \
        "namespace fc = ::fixy::session::config;"
_HELPER = (
    "using session_oracle_rel = fs::EveryRoleReliable;\n"
    "template <class S, class A> struct session_oracle_gstep {\n"
    "    using type = fg::state_step_t<S, A, session_oracle_rel>; };\n"
    "template <class A> struct session_oracle_gstep<fg::NoTransition, A> { using type = fg::NoTransition; };\n"
    "template <class S> struct session_oracle_gen { using type = fg::state_enabled_t<S, session_oracle_rel>; };\n"
    "template <> struct session_oracle_gen<fg::NoTransition> { using type = fg::NoTransition; };\n"
    "template <class C, class A> struct session_oracle_cstep {\n"
    "    using type = fc::step_t<C, A, session_oracle_rel>; };\n"
    "template <class A> struct session_oracle_cstep<fg::NoTransition, A> { using type = fg::NoTransition; };\n"
    "template <class C> struct session_oracle_cen { using type = fc::enabled_t<C, session_oracle_rel>; };\n"
    "template <> struct session_oracle_cen<fg::NoTransition> { using type = fg::NoTransition; };\n"
    "template <class C, class S> struct session_oracle_assoc {\n"
    "    using type = std::bool_constant<fc::crash_association_holds_v<C, S, session_oracle_rel>>; };\n"
    "template <class S> struct session_oracle_assoc<fg::NoTransition, S> { using type = fg::NoTransition; };\n"
    "template <class C> struct session_oracle_assoc<C, fg::NoTransition> { using type = fg::NoTransition; };\n"
    "template <> struct session_oracle_assoc<fg::NoTransition, fg::NoTransition> {\n"
    "    using type = fg::NoTransition; };\n")

Label = tuple[str, int, int, str | int, str]  # ("S", sender, receiver, label, sort) or ("R", receiver, sender, ...)


def label_text(a: Label) -> str:
    """Return the label in the form of execution.py."""
    kind, subject, peer, label, sort = a
    return f"{subject}{'>' if kind == 'S' else '<'}{peer}:{_item(label, sort)}"


def _cpp_label(a: Label) -> str:
    kind, subject, peer, label, sort = a
    from model import PRELUDE_NS, cpp_sort
    lab = f"{PRELUDE_NS}::Val" if label == "v" else f"{PRELUDE_NS}::Label<{label}>"
    payload = f"{PRELUDE_NS}::Unit" if label != "v" else cpp_sort(sort)
    action = "SendAction" if kind == "S" else "RecvAction"
    return f"fg::{action}<{cpp_role(subject)}, {cpp_role(peer)}, {lab}, {payload}>"


def read_actions(spelling: str) -> set[str] | None:
    """Parse an Actions<...> spelling into label texts.  None stands for NoTransition."""
    name, args = _split(spelling)
    if name == f"{FIXY_NS}::global::NoTransition":
        return None
    if name != f"{FIXY_NS}::global::Actions":
        raise SpellingError(f"not a set of labels: {spelling!r}")
    out = set()
    for arg in args:
        aname, aargs = _split(arg)
        short = aname.removeprefix(f"{FIXY_NS}::global::")
        if short not in ("SendAction", "RecvAction") or len(aargs) != 4:
            raise SpellingError(f"a label that the walk does not expect: {arg!r}")
        label, payload = _label_of(aargs[2]), _payload_of(aargs[3])
        out.add(label_text(("S" if short == "SendAction" else "R", _role_of(aargs[0]), _role_of(aargs[1]),
                            label, payload)))
    return out


State = tuple[tuple[Local, ...], tuple]


def moves(roles: list[int], state: State) -> list[tuple[Label, State]]:
    """Return the labels and successors of one context state, as execution.explore reads them."""
    locals_, flight = state
    out = []
    for i, e in enumerate(locals_):
        me = roles[i]
        if not isinstance(e, LChoice):
            continue
        if e.send:
            to = _receiver(e.channel, me)
            for label, sort, cont in e.branches:
                nxt = (locals_[:i] + (head(cont),) + locals_[i + 1:], flight + ((e.channel, me, to, label, sort),))
                out.append((("S", me, to, label, sort), nxt))
            continue
        pos = _match(e, me, flight)
        if pos is None:
            continue
        _, sender, _, label, sort = flight[pos]
        chosen = next((k for lab, _, k in e.branches if lab == label), None)
        if chosen is None:
            continue
        nxt = (locals_[:i] + (head(chosen),) + locals_[i + 1:], flight[:pos] + flight[pos + 1:])
        out.append((("R", me, sender, label, sort), nxt))
    return out


@dataclass(slots=True)
class Node:
    """One node of the walk: its path from the root and the labels of the reference there."""

    path: tuple[Label, ...]
    labels: set[str]


def walk(system: dict[int, Local]) -> list[Node]:
    """Return the nodes of the reference run up to DEPTH steps, breadth first, at most MAX_NODES."""
    roles = sorted(system)
    start: State = (tuple(head(system[r]) for r in roles), ())
    if any(e is _SPIN for e in start[0]):
        return []
    nodes: list[Node] = []
    todo: deque[tuple[tuple[Label, ...], State]] = deque([((), start)])
    while todo and len(nodes) < MAX_NODES:
        path, state = todo.popleft()
        succ = moves(roles, state)
        nodes.append(Node(path, {label_text(a) for a, _ in succ}))
        if len(path) < DEPTH:
            for a, nxt in succ:
                if not any(e is _SPIN for e in nxt[0]):
                    todo.append((path + (a,), nxt))
    return nodes


def probe_source(g: Global, roles: list[int], nodes: list[Node]) -> str:
    """Return the probe that asks Semantics.h along each path of ``nodes``."""
    src = probe_header(HEAD, ALIAS) + _HELPER + f"using G = {cpp_fixy_global(g)};\n"
    entries = ", ".join(f"fs::RoleState<{cpp_role(r)}, fs::OutQueue<>, typename fs::project_t<G, {cpp_role(r)}>::local>"
                        for r in roles)
    src += f"using S0 = fg::State<fg::Roles<>, G>;\nusing C0 = fs::TypingContext<{entries}>;\n"
    index = {(): 0}
    for k, node in enumerate(nodes):
        if k:
            parent = index[node.path[:-1]]
            a = _cpp_label(node.path[-1])
            src += (f"using S{k} = typename session_oracle_gstep<S{parent}, {a}>::type;\n"
                    f"using C{k} = typename session_oracle_cstep<C{parent}, {a}>::type;\n")
            index[node.path] = k
        src += (show(f"g{k}", f"typename session_oracle_gen<S{k}>::type")
                + show(f"c{k}", f"typename session_oracle_cen<C{k}>::type")
                + show(f"a{k}", f"typename session_oracle_assoc<C{k}, S{k}>::type"))
    return src


def _path_text(path: tuple[Label, ...]) -> str:
    return "/".join(label_text(a) for a in path) or "start"


def _set_text(labels: set[str] | None) -> str:
    return "none" if labels is None else "{" + " ".join(sorted(labels)) + "}"


def classify(case: str, g: Global, nodes: list[Node], measured) -> list[Row]:  # type: ignore[no-untyped-def]
    """Return the rows of one case, where ``nodes`` are the nodes of the run of fixy's projection."""
    text = show_global(g)
    if measured.rejection is not None:
        return [Row("fixy.global_lts", case, "start", text, "-", f"reject:{measured.rejection}", "divergence",
                    "hard-error", "ours wrong: a relation of Semantics.h stops the build with a hard error instead of "
                                  "answering NoTransition or a set of labels")]
    rows: list[Row] = []
    for k, node in enumerate(nodes):
        where = _path_text(node.path)
        try:
            glob = read_actions(measured.values[f"g{k}"])
            conf = read_actions(measured.values[f"c{k}"])
        except (KeyError, SpellingError) as exc:
            raise RuntimeError(f"semantics case {case} node {where}: no reading ({exc}): {measured}") from exc
        ref = _set_text(node.labels)
        for fam, ours, rule in (("fixy.config_lts", conf, "Definition 4.18 (Figure 8)"),
                                ("fixy.global_lts", glob, "Definition 4.12 (Figure 7) and Theorem 4.20")):
            if ours == node.labels:
                rows.append(Row(fam, case, where, text, ref, _set_text(ours), "agree", "", ""))
            elif ours is None:
                rows.append(Row(fam, case, where, text, ref, "none", "divergence", "no-step",
                                f"ours wrong: the run of the projected context takes this path, and a step of "
                                f"Semantics.h on it answers NoTransition ({BARWELL}, {rule})"))
            elif node.labels - ours:
                rows.append(Row(fam, case, where, text, ref, _set_text(ours), "divergence", "missing-label",
                                f"ours wrong: the run of the projected context takes a label that Semantics.h does "
                                f"not take ({BARWELL}, {rule}).  Figure 7 as printed has this gap for an en-route "
                                "sender (misc/session_types_literature.md, section 5, item 12)"))
            else:
                rows.append(Row(fam, case, where, text, ref, _set_text(ours), "divergence", "extra-label",
                                f"ours wrong: Semantics.h takes a label that the run of the projected context does "
                                f"not take ({BARWELL}, {rule})"))
        if conf is None or glob is None:
            continue
        ok = measured.values.get(f"a{k}", "") == "std::integral_constant<bool,true>"
        rows.append(Row("fixy.crash_association", case, where, text, "true", "true" if ok else "false",
                        "agree" if ok else "divergence", "" if ok else "association-lost",
                        "" if ok else f"ours wrong: the configuration and the global state that one path reaches are "
                                      f"not associated, and association holds at the start and is kept by each step "
                                      f"({BARWELL}, Definition 4.19 and Theorem 4.20)"))
    return rows
