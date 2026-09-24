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

The crash-stop variants of a case (session_oracle.crash_variants) enter the
same walk with their unreliable roles, when fixy's crash-stop projection
(project_crash_t) projects every role.  The run then also takes the two
crash rules of the configuration (Definition 4.18, Figure 8), and every
step and the association use the reliable set of the variant:

  [Γ-↯]  an unreliable role that is not at End or Stop crashes: it goes to
         Stop, and each message to it leaves its queue
  [Γ-⊙]  a role at a receive from p that has a crash branch detects the
         crash of p, when p is at Stop and no message from p to it waits

A send to a role at Stop is lost, and the messages that a crashed role sent
stay in flight.

A label is written as in execution.py: "p>q:x" for a send and "q<p:x" for a
receive, where x is the sort of a value or l<k> for the branch k.  "p:crash"
is the crash of p, and "q<p:crash" is the detection by q of the crash of p.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

from emit import Row
from execution import _SPIN, _item, _match, _receiver, head
from model import LChoice, LEnd, Local, LVar, cpp_role
from probe import FIXY_NS, SpellingError, _label_of, _payload_of, _role_of, _split, probe_header, show

DEPTH = 3
MAX_NODES = 16
MAX_CRASH_NODES = 16
FAMILIES = ("fixy.global_lts", "fixy.config_lts", "fixy.crash_association")
BARWELL = "Barwell, Hou, Yoshida and Zhou, LMCS 21:2, 2025"
HEAD = "semantics"
ALIAS = "namespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n" \
        "namespace fc = ::fixy::session::config;"
EVERY_ROLE_RELIABLE = "fs::EveryRoleReliable"
# The local type of a crashed role.  Like execution._SPIN, it is a
# variable that no type binds, so it is equal to no local type.
STOP = LVar(-2)
_HELPER = (
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
    "    using type = fg::NoTransition; };\n"
    "template <class C, class S> struct session_oracle_fault {\n"
    "    using type = std::integral_constant<int,\n"
    "        static_cast<int>(fc::crash_association_fault_v<C, S, session_oracle_rel>)>; };\n"
    "template <class S> struct session_oracle_fault<fg::NoTransition, S> { using type = fg::NoTransition; };\n"
    "template <class C> struct session_oracle_fault<C, fg::NoTransition> { using type = fg::NoTransition; };\n"
    "template <> struct session_oracle_fault<fg::NoTransition, fg::NoTransition> {\n"
    "    using type = fg::NoTransition; };\n")
# The first crash-stop projection failure among the live roles of a state,
# or void.  Only a crash walk asks it, because project_crash_t takes a
# ReliableSet.
_CRASH_HELPER = (
    "template <class G, class... Rs> struct session_oracle_first_fail { using type = void; };\n"
    "template <class G, class R, class... Rs> struct session_oracle_first_fail<G, R, Rs...> {\n"
    "    using projected = typename std::conditional_t<fg::role_in_v<R, fg::active_roles_t<G>>,\n"
    "        std::type_identity<fs::project_crash_t<G, R, session_oracle_rel>>, std::type_identity<void>>::type;\n"
    "    using type = std::conditional_t<fs::is_projection_failure_v<projected>, projected,\n"
    "        typename session_oracle_first_fail<G, Rs...>::type>; };\n"
    "template <class S, class... Rs> struct session_oracle_projfail {\n"
    "    using type = typename session_oracle_first_fail<typename S::type, Rs...>::type; };\n"
    "template <class... Rs> struct session_oracle_projfail<fg::NoTransition, Rs...> { using type = fg::NoTransition; };\n")
# Divergence classes on a shrink-only ledger: the most rows a run may give.
# A run that gives more fails (check_ledger).  When a repair lowers the
# count, lower the bound in the same commit.  At 0, delete the class when
# no code gives it any more.  The three network classes stay at 0, because
# evaluate_sprout of session_oracle.py gives a class for each network: a
# global type that fixy refuses on that network and Sprout(A) admits.
SHRINK_ONLY = {"p2pbox-incomplete": 44, "mailbox-incomplete": 0, "bag-incomplete": 13}
# The enumerators of fixy::session::config::CrashAssociationFault, in order.
FAULTS = ("None", "NotWellAnnotated", "NotBalancedPlus", "CrashedRoleMismatch", "LiveRoleMismatch", "UnfinishedRole",
          "QueueMismatch")

# ("S", sender, receiver, label, sort), ("R", receiver, sender, label, sort),
# ("C", crashed role, -1, "crash", "void") or ("D", detecting role, crashed role, "crash", "void").
Label = tuple[str, int, int, str | int, str]


def label_text(a: Label) -> str:
    """Return the label in the form of execution.py, or the crash forms of this module."""
    kind, subject, peer, label, sort = a
    if kind == "C":
        return f"{subject}:crash"
    if kind == "D":
        return f"{subject}<{peer}:crash"
    return f"{subject}{'>' if kind == 'S' else '<'}{peer}:{_item(label, sort)}"


def _cpp_label(a: Label) -> str:
    kind, subject, peer, label, sort = a
    if kind == "C":
        return f"fg::CrashAction<{cpp_role(subject)}>"
    if kind == "D":
        return f"fg::DetectAction<{cpp_role(subject)}, {cpp_role(peer)}>"
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
        if short == "CrashAction" and len(aargs) == 1:
            out.add(label_text(("C", _role_of(aargs[0]), -1, "crash", "void")))
            continue
        if short == "DetectAction" and len(aargs) == 2:
            out.add(label_text(("D", _role_of(aargs[0]), _role_of(aargs[1]), "crash", "void")))
            continue
        if short not in ("SendAction", "RecvAction") or len(aargs) != 4:
            raise SpellingError(f"a label that the walk does not expect: {arg!r}")
        label, payload = _label_of(aargs[2]), _payload_of(aargs[3])
        out.add(label_text(("S" if short == "SendAction" else "R", _role_of(aargs[0]), _role_of(aargs[1]),
                            label, payload)))
    return out


State = tuple[tuple[Local, ...], tuple]


def _with(locals_: tuple[Local, ...], i: int, e: Local) -> tuple[Local, ...]:
    return locals_[:i] + (e,) + locals_[i + 1:]


def moves(roles: list[int], state: State, unreliable: frozenset[int] = frozenset()) -> list[tuple[Label, State]]:
    """Return the labels and successors of one context state.

    The communication moves are those of execution.explore.  The crash moves
    of the unreliable roles come first, then the detections, so a walk that
    stops at MAX_NODES keeps the crash paths.  O(roles × messages).
    """
    locals_, flight = state
    at = {r: i for i, r in enumerate(roles)}
    crashes: list[tuple[Label, State]] = []
    detections: list[tuple[Label, State]] = []
    out: list[tuple[Label, State]] = []
    for i, e in enumerate(locals_):
        me = roles[i]
        if me in unreliable and e is not STOP and not isinstance(e, LEnd):
            kept = tuple(m for m in flight if m[2] != me)
            crashes.append((("C", me, -1, "crash", "void"), (_with(locals_, i, STOP), kept)))
        if not isinstance(e, LChoice):
            continue
        if e.send:
            to = _receiver(e.channel, me)
            lost = to in at and locals_[at[to]] is STOP
            for label, sort, cont in e.branches:
                sent = flight if lost else flight + ((e.channel, me, to, label, sort),)
                out.append((("S", me, to, label, sort), (_with(locals_, i, head(cont)), sent)))
            continue
        peer = e.channel // 8
        crash_branch = next((k for lab, _, k in e.branches if lab == "crash"), None)
        if crash_branch is not None and peer in at and locals_[at[peer]] is STOP \
                and not any(m[0] == e.channel for m in flight):
            detections.append((("D", me, peer, "crash", "void"), (_with(locals_, i, head(crash_branch)), flight)))
        pos = _match(e, me, flight)
        if pos is None:
            continue
        _, sender, _, label, sort = flight[pos]
        chosen = next((k for lab, _, k in e.branches if lab == label), None)
        if chosen is None:
            continue
        out.append((("R", me, sender, label, sort), (_with(locals_, i, head(chosen)), flight[:pos] + flight[pos + 1:])))
    return crashes + detections + out


@dataclass(slots=True)
class Node:
    """One node of the walk: its path from the root and the labels of the reference there."""

    path: tuple[Label, ...]
    labels: set[str]


def walk(system: dict[int, Local], unreliable: frozenset[int] = frozenset(), max_nodes: int = MAX_NODES) -> list[Node]:
    """Return the nodes of the reference run up to DEPTH steps, breadth first, at most ``max_nodes``."""
    roles = sorted(system)
    start: State = (tuple(head(system[r]) for r in roles), ())
    if any(e is _SPIN for e in start[0]):
        return []
    nodes: list[Node] = []
    todo: deque[tuple[tuple[Label, ...], State]] = deque([((), start)])
    while todo and len(nodes) < max_nodes:
        path, state = todo.popleft()
        succ = moves(roles, state, unreliable)
        nodes.append(Node(path, {label_text(a) for a, _ in succ}))
        if len(path) < DEPTH:
            for a, nxt in succ:
                if not any(e is _SPIN for e in nxt[0]):
                    todo.append((path + (a,), nxt))
    return nodes


def reliable_set(roles: list[int], unreliable: frozenset[int]) -> str:
    """Return the C++ reliability of a walk: every role that is not unreliable, or every role."""
    if not unreliable:
        return EVERY_ROLE_RELIABLE
    return f"fs::ReliableSet<{', '.join(cpp_role(r) for r in roles if r not in unreliable)}>"


def probe_source(g_spelling: str, roles: list[int], nodes: list[Node],
                 unreliable: frozenset[int] = frozenset()) -> str:
    """Return the probe that asks Semantics.h along each path of ``nodes``.

    ``g_spelling`` is the global type in fixy::session::global.  With
    unreliable roles, the context is made of the crash-stop projections, and
    each step and the association read the reliable set of the walk.
    """
    src = (probe_header(HEAD, ALIAS) + f"using session_oracle_rel = {reliable_set(roles, unreliable)};\n" + _HELPER
           + (_CRASH_HELPER if unreliable else "") + f"using G = {g_spelling};\n")
    role_list = ", ".join(cpp_role(r) for r in roles)
    project = ("fs::project_t<G, {r}>" if not unreliable
               else "fs::project_crash_t<G, {r}, session_oracle_rel>")
    entries = ", ".join(f"fs::RoleState<{cpp_role(r)}, fs::OutQueue<>, "
                        f"typename {project.format(r=cpp_role(r))}::local>" for r in roles)
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
                + show(f"a{k}", f"typename session_oracle_assoc<C{k}, S{k}>::type")
                + show(f"f{k}", f"typename session_oracle_fault<C{k}, S{k}>::type"))
        if unreliable:
            src += show(f"p{k}", f"typename session_oracle_projfail<S{k}, {role_list}>::type")
    return src


def _path_text(path: tuple[Label, ...]) -> str:
    return "/".join(label_text(a) for a in path) or "start"


def _fault_of(spelling: str) -> str:
    """Name the CrashAssociationFault that a spelling std::integral_constant<int,N> holds."""
    prefix = "std::integral_constant<int,"
    if not spelling.startswith(prefix) or not spelling.endswith(">"):
        raise SpellingError(f"not an association fault: {spelling!r}")
    index = int(spelling[len(prefix):-1])
    if not 0 <= index < len(FAULTS):
        raise SpellingError(f"no association fault number {index}")
    return FAULTS[index]


def _set_text(labels: set[str] | None) -> str:
    return "none" if labels is None else "{" + " ".join(sorted(labels)) + "}"


def classify(case: str, text: str, nodes: list[Node], measured, variant: str = "") -> list[Row]:  # type: ignore[no-untyped-def]
    """Return the rows of one case, where ``nodes`` are the nodes of the run of fixy's projection.

    ``text`` is the global type as the golden file shows it.  A crash-stop
    variant names itself in ``variant`` (for example copy.u0), and the role
    column of its rows is "<variant>@<path>".
    """
    at = f"{variant}@" if variant else ""
    if measured.rejection is not None:
        return [Row("fixy.global_lts", case, f"{at}start", text, "-", f"reject:{measured.rejection}", "divergence",
                    "hard-error", "ours wrong: a relation of Semantics.h stops the build with a hard error instead of "
                                  "answering NoTransition or a set of labels")]
    rows: list[Row] = []
    for k, node in enumerate(nodes):
        where = at + _path_text(node.path)
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
        if ok:
            rows.append(Row("fixy.crash_association", case, where, text, "true", "true", "agree", "", ""))
            continue
        fault = _fault_of(measured.values.get(f"f{k}", ""))
        failure = _projection_failure_of(measured.values.get(f"p{k}", "void"))
        note = (f"ours wrong: the configuration and the global state that one path reaches are not associated, and "
                f"association holds at the start and is kept by each step ({BARWELL}, Definition 4.19 and "
                f"Theorem 4.20).  The fault is {fault}")
        klass = "association-lost"
        if failure:
            note += f", and a live role has no crash-stop projection ({failure})"
        shown = f"false ({fault}{', ' + failure if failure else ''})"
        rows.append(Row("fixy.crash_association", case, where, text, "true", shown, "divergence", klass, note))
    return rows


def check_ledger(rows: list[Row]) -> list[str]:
    """Refuse rows whose shrink-only class grew.  Return a note for each class that shrank.

    Raise RuntimeError, with the rows of the class, when a class of
    SHRINK_ONLY has more rows than its bound.  O(rows).
    """
    counts = {klass: 0 for klass in SHRINK_ONLY}
    for r in rows:
        if r.klass in counts:
            counts[r.klass] += 1
    grown = [klass for klass, n in counts.items() if n > SHRINK_ONLY[klass]]
    if grown:
        shown = [f"{r.case} {r.role}" for r in rows if r.klass in grown][:20]
        raise RuntimeError(f"semantics: the shrink-only classes {grown} grew past their bounds "
                           f"({ {k: counts[k] for k in grown} } > { {k: SHRINK_ONLY[k] for k in grown} }): "
                           + "; ".join(shown))
    return [f"the class {klass} has {n} rows, less than its bound {SHRINK_ONLY[klass]}: lower the bound in "
            "semantics.SHRINK_ONLY" for klass, n in counts.items() if n < SHRINK_ONLY[klass]]


def _projection_failure_of(spelling: str) -> str:
    """Name the reason of a NotProjectable spelling, or return "" for void."""
    if spelling == "void":
        return ""
    name, args = _split(spelling)
    if name != f"{FIXY_NS}::NotProjectable" or len(args) != 1:
        raise SpellingError(f"not a projection failure: {spelling!r}")
    return args[0].rsplit("::", 1)[-1]
