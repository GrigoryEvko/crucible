#!/usr/bin/env python3
"""Differential tests of our session relations against a published mechanisation.

The oracle is the computable projection of Tirore, Bengtson and Carbone
(ITP 2023, github.com/Tirore96/projection).  We own no proofs: the
oracle gives the expected answer, and our C++ relations must agree.
Where a relation and the oracle disagree, an execution of the projected
local types (execution.py) shows which side lets something go wrong.

Modes:

  regenerate  Generate the corpora, run the oracle, measure our
              relations, shrink every divergence to a minimal case, and
              write test/session_oracle/golden.csv and the emitted
              tests.  Needs the Rocq toolchain (see rocq.py) and the
              project compiler.
  emit        Write the emitted tests from the golden file only.  Use it
              after a note in the golden file changes.
  check       Emit into memory and compare with the committed tests.  No
              oracle and no compiler.  CI runs this mode.
  self-test   Compile the emitted tests, which must pass, and a copy with
              one planted wrong row per test, which must fail and name
              the planted case.  Needs the project compiler.

The relations under test:

  fixy duality     For a two-party global type the oracle's projection
                   onto role 1 is the dual of its projection onto role 0.
                   dual_of_t, is_dual_v, the involution, the involutive
                   flag and is_well_formed_v are measured on that pair.
  fixy acceptance  fixy's verdict on the naive reading of the global type
                   (model.local_of), against the oracle's projection.
  frozen-tree      project_t and is_global_well_formed_v of
  projection       crucible/sessions/SessionGlobal.h, against the
                   oracle's projection onto each role, and an execution
                   of our projection for every type that we accept.
  fixy subtyping   is_subtype_sync_v and is_subtype_async_v of
                   fixy/session/Subtype.h on pairs (T, U), where U is the
                   naive reading of a two-party global type and T one
                   change of U.  The reference is a run of T against the
                   dual of U, synchronous or with bounded queues.
  fixy multiparty  is_global_well_formed_v, project_t and
  projection       is_live_by_construction_v of fixy/session/Global.h,
                   Projection.h and Liveness.h, against the oracle's
                   projection onto each role, and an execution of fixy's
                   own projection for every type that it calls live.

The corpora are listed in emit.CORPORA.
"""

from __future__ import annotations

import argparse
import dataclasses
import logging
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from emit import GoldenError, Row, emit_all, read_golden, write_golden  # noqa: E402
from execution import equal_up_to_unfolding, explore, explore_bindings  # noqa: E402
from model import (GBranch, GEnd, GMsg, GRec, GVar, Global, Local,  # noqa: E402
                   UntranslatableError, action_ok, canonical_roles, contractive, has_empty_choice,
                   has_idle_loop,
                   cpp_fixy_global, cpp_fixy_local, cpp_fixy_peer_local, cpp_old_global,
                   generate, generate_adversarial, local_of, roles_of, show_global,
                   show_local, shrinks, size, size_ok)

LOG = logging.getLogger("session_oracle")

REPO = Path(__file__).resolve().parents[2]
TEST_DIR = REPO / "test" / "session_oracle"
GOLDEN = TEST_DIR / "golden.csv"

FIXY_SEED, FIXY_COUNT, FIXY_DEPTH = 20260923, 160, 6
FIXY_ADV_SEED, FIXY_ADV_COUNT, FIXY_ADV_DEPTH = 20260925, 120, 8
OLD_SEED, OLD_COUNT, OLD_DEPTH = 20260924, 100, 6
OLD_ADV_SEED, OLD_ADV_COUNT, OLD_ADV_DEPTH = 20260926, 120, 8
OLD_ROLES = 3
SHRINK_ROUNDS = 400
SHRINK_BATCH = 10
SHRINK_PER_CLASS = 3

ITP23 = "Tirore, Bengtson, Carbone, ITP 2023"
ECOOP25 = "Tirore, Bengtson, Carbone, ECOOP 2025"
PMY25 = "Pischke, Masters, Yoshida, Asynchronous Global Protocols, Precisely, v4"
SHARED = "map:0>1=100,1>2=101,0>2=101"


@dataclass(frozen=True, slots=True)
class Case:
    """One global type of a corpus, with the channel specification of its queries."""

    ident: str
    g: Global
    chan: str = "pair"
    cite: str = ""


def _ecoop() -> Global:
    return GBranch(0, 1, (GMsg(1, 2, "bool", GEnd()), GMsg(0, 2, "bool", GEnd())))


def _wire() -> Global:
    return GBranch(0, 1, (GMsg(1, 2, "bool", GMsg(0, 2, "nat", GEnd())),
                          GMsg(0, 2, "bool", GMsg(1, 2, "nat", GEnd()))))


def _pmy_g1() -> Global:
    return GRec(GBranch(0, 1, (GVar(), GMsg(0, 2, "nat", GEnd()))))


def _pmy_g2() -> Global:
    return GRec(GBranch(0, 1, (GVar(), GMsg(3, 2, "nat", GEnd()))))


# Hand-written cases that name the defects the review found, so the
# corpus exercises them whatever the random draw gives.
HAND_CASES: tuple[Case, ...] = (
    Case("h0", GRec(GMsg(0, 1, "nat", GVar())),
         cite="a role that has no part in a loop"),
    Case("h1", GMsg(0, 2, "nat", GRec(GMsg(0, 1, "nat", GVar()))),
         cite="a role that leaves before a loop that it has no part in"),
    Case("h2", GRec(GBranch(0, 1, (GMsg(1, 2, "bool", GVar()), GMsg(1, 2, "bool", GVar())))),
         cite="a loop whose branches agree for the third role"),
    Case("h3", GBranch(0, 1, (GMsg(0, 2, "nat", GEnd()), GMsg(1, 2, "nat", GEnd()))),
         cite="a third role that receives from a different sender in each branch"),
    Case("h4", GBranch(0, 1, (GRec(GMsg(0, 1, "nat", GVar())), GEnd())),
         cite="a loop in one branch only"),
)

PAPER_OLD: tuple[Case, ...] = (
    Case("p_tirore23_eq2", GRec(GMsg(0, 1, "nat", GBranch(2, 3, (GVar(), GVar())))),
         cite=f"{ITP23}, equation (2)"),
    Case("p_tirore23_eq3", GMsg(0, 1, "nat", GRec(GBranch(2, 3, (GEnd(), GVar())))),
         cite=f"{ITP23}, equation (3): the desired projection onto p is !k<U>.end"),
    Case("p_tirore23_eq5", GRec(GMsg(0, 1, "nat", GRec(GMsg(2, 3, "nat", GVar(1))))),
         cite=f"{ITP23}, equation (5): the desired projection onto p is mu t.!k<U>.t"),
    Case("p_tirore23_eq7", GRec(GMsg(0, 1, "nat", GRec(GBranch(2, 3, (
        GVar(1), GMsg(0, 1, "nat", GVar(0))))))),
         cite=f"{ITP23}, equation (7), which is equation (1) with other names"),
    Case("p_ecoop25_eq1", _ecoop(),
         cite=f"{ECOOP25}, equation (1), with a queue for each ordered pair of roles"),
    Case("p_ecoop25_eq1_shared", _ecoop(), SHARED,
         cite=f"{ECOOP25}, equation (1), with the paper's shared channel 2"),
    Case("p_ecoop25_wire", _wire(),
         cite=f"built on {ECOOP25}, equation (1): the second message of each branch "
              "comes from the other sender"),
    Case("p_ecoop25_wire_shared", _wire(), SHARED,
         cite=f"built on {ECOOP25}, equation (1), with the paper's shared channel 2"),
    Case("p_pmy25_ex12_g1", _pmy_g1(),
         cite=f"{PMY25}, Example 12, G1, which is also Example 14, equation (50)"),
    Case("p_pmy25_ex12_g2", _pmy_g2(), cite=f"{PMY25}, Example 12, G2"),
    Case("p_pmy25_ex12_g1_prefixed", GMsg(0, 1, "nat", GMsg(3, 2, "nat", _pmy_g1())),
         cite=f"{PMY25}, Example 12, G1 prefixed as in equation (47)"),
    Case("p_pmy25_ex12_g2_prefixed", GMsg(0, 1, "nat", GMsg(3, 2, "nat", _pmy_g2())),
         cite=f"{PMY25}, Example 12, G2 prefixed as in equation (47)"),
)

# Hand-written two-party cases for the corners that the subtyping corpus
# must keep whatever the size bound selects.
FIXY_HAND: tuple[Case, ...] = (
    Case("fh0", GBranch(0, 1, ()), cite="a choice with no branch"),
    Case("fh1", GRec(GBranch(0, 1, (GMsg(0, 1, "nat", GVar()), GBranch(1, 0, ())))),
         cite="a loop with an empty choice in one branch"),
)

PAPER_FIXY: tuple[Case, ...] = (
    Case("p_ekici25_ex18", GRec(GMsg(1, 0, "bool", GMsg(0, 1, "bool", GMsg(
        1, 0, "nat", GMsg(0, 1, "nat", GVar()))))),
         cite="Ekici, Kamegai, Yoshida, ITP 2025, Example 18, the local type T"),
)

NOTE_UNGUARDED = (
    "ours wrong: our Rec_G rule keeps a Loop whose body reaches Continue before an action "
    f"of this role.  The oracle projects such a recursion to End ({ITP23}, indProj.v, "
    "trans, the GRec case with eguarded).  The role can then never act and never close.")
NOTE_PEERLESS = (
    "ours wrong: the oracle rejects because the merged branches use different channels, "
    "and with one channel for all pairs it accepts.  Our Send and Recv name no peer, so "
    "the plain merge sees equal branches and accepts.  A multiparty local type must name "
    f"the peer of each action ({ITP23}, plain merge over channel-annotated local types; "
    f"{ECOOP25}, section 1: one shared queue breaks subject reduction).")
NOTE_WF = (
    "ours wrong: is_global_well_formed checks variable scope, self-messages and empty "
    "choices only.  The oracle rejects the projection onto role {roles}, so no set of "
    f"local types implements this global type ({ITP23}, proj).")
NOTE_OUTER = (
    "our DSL cannot spell a variable that names an outer binder: Var_G and Continue name "
    f"the nearest binder.  The paper's projection needs one ({ITP23}).")
NOTE_IDLE = (
    "neither side is unsound: the oracle keeps a recursion binder whose body never loops back "
    f"({ITP23}, trans), and fixy's is_well_formed_v refuses such a Loop.  fixy's projection "
    "returns the body of such a recursion instead (Projection.h, proj_rec)")
NOTE_EMPTY = (
    "the oracle's domain excludes this type too: a choice with no branch fails size_pred "
    "(elimination.v), which the oracle's proj does not call.  fixy's is_well_formed_v refuses "
    "the empty choice, because a substitute of that type never sends (Gay and Hole 2005, the "
    "choice rules)")
NOTE_DOMAIN = (
    "oracle wrong for safety: its proj accepts a type that the development excludes with "
    "{pred} (elimination.v), which proj does not call.  The run of the oracle's own "
    "projection fails: {verdict}.")
NOTE_SHARED = (
    "oracle wrong for safety on shared channels: its proj accepts, and the run of its own "
    "projection fails: {verdict}.  Projection is not enough on an explicit shared queue "
    f"({ECOOP25}, section 1 and the unstuck predicate of section 5).")


# ── Classification ───────────────────────────────────────────────────


def _bool(spelling: str | None) -> str | None:
    table = {"std::integral_constant<bool,true>": "true",
             "std::integral_constant<bool,false>": "false"}
    return table.get(spelling or "")


def _expand(spelling: str, alias: str, target: str) -> str:
    from probe import canonical
    return canonical(spelling.replace(f"{alias}::", f"{target}::"))


def _translatable_old(g: Global) -> bool:
    try:
        cpp_old_global(g)
    except UntranslatableError:
        return False
    return True


def _fixy_spelling(e: Local | None) -> str | None:
    if e is None:
        return None
    try:
        return cpp_fixy_local(e)
    except UntranslatableError:
        return None


def old_roles(g: Global) -> list[int]:
    """Return the roles onto which a frozen-tree case is projected."""
    return list(range(max(OLD_ROLES, max(roles_of(g), default=-1) + 1)))


def _domain_pred(g: Global) -> str | None:
    missing = [name for name, ok in (("action_pred", action_ok(g)), ("size_pred", size_ok(g)))
               if not ok]
    return " and ".join(missing) or None


def _oracle_safety_row(c: Case, family_roles: list[int], answers: dict[int, Local | None],
                       ours: str) -> Row | None:
    """Run the oracle's own projection when it accepts every role."""
    if any(answers[r] is None for r in family_roles):
        return None
    verdict = explore({r: answers[r] for r in family_roles})  # type: ignore[misc]
    text = show_global(c.g)
    fam = "oracle.safety"
    if verdict.is_safe:
        return Row(fam, c.ident, "-", text, verdict.text(), ours, "agree", "", "")
    pred = _domain_pred(c.g)
    if pred:
        return Row(fam, c.ident, "-", text, verdict.text(), ours, "divergence",
                   "outside-oracle-domain", NOTE_DOMAIN.format(pred=pred, verdict=verdict.text()))
    if c.chan != "pair":
        return Row(fam, c.ident, "-", text, verdict.text(), ours, "divergence",
                   "shared-channel", NOTE_SHARED.format(verdict=verdict.text()))
    return Row(fam, c.ident, "-", text, verdict.text(), ours, "divergence",
               "oracle-accepts-and-fails",
               f"unclassified: the oracle's proj accepts and the run of its projection "
               f"fails: {verdict.text()}")


def _old_projection_row(c: Case, role: int, oracle: Local | None, single: Local | None,
                        measured) -> Row:  # type: ignore[no-untyped-def]
    from model import cpp_old_local
    from probe import (OLD_NS, SpellingError, drop_unguarded_loops, erase_channels,
                       read_protocol)
    text = show_global(c.g)
    oracle_text = show_local(oracle)
    # A hard error is a rejection even when the compiler still printed a
    # type: plain_merge_impl fires its static_assert and then defines its
    # type as the first branch, so the show line carries a spelling.
    value = None if measured.rejection is not None else measured.values.get("proj")
    fam, ident, rs = "old.projection", c.ident, str(role)
    pred = _domain_pred(c.g)
    if value is None:
        ours = f"reject:{measured.rejection}"
        if oracle is None:
            return Row(fam, ident, rs, text, oracle_text, ours, "agree", "", "")
        if pred:
            return Row(fam, ident, rs, text, oracle_text, ours, "divergence",
                       "outside-oracle-domain",
                       f"oracle wrong: its proj accepts a type that the development excludes "
                       f"with {pred} (elimination.v); our projection rejects it")
        return Row(fam, ident, rs, text, oracle_text, ours, "divergence", "ours-rejects",
                   f"ours too strict: our projection rejects ({measured.rejection}) where the "
                   "oracle projects.  Our plain merge compares branches as written and does "
                   f"not unfold a recursion ({ITP23}, section 2, equation (7))")
    if oracle is not None:
        try:
            expected = _expand(cpp_old_local(oracle), "pr", OLD_NS)
        except UntranslatableError as exc:
            return Row(fam, ident, rs, text, oracle_text, value, "gap", "inexpressible",
                       f"the oracle's answer names an outer binder ({exc}).  {NOTE_OUTER}")
        if value == expected:
            return Row(fam, ident, rs, text, oracle_text, "=", "agree", "", "")
    try:
        ours_ir = erase_channels(read_protocol(value, OLD_NS, labelled=True))
    except SpellingError as exc:
        return Row(fam, ident, rs, text, oracle_text, value, "divergence", "unclassified",
                   f"unclassified: {exc}")
    if oracle is not None:
        if drop_unguarded_loops(ours_ir) == erase_channels(oracle):
            return Row(fam, ident, rs, text, oracle_text, value, "divergence", "unguarded-loop",
                       NOTE_UNGUARDED)
        return Row(fam, ident, rs, text, oracle_text, value, "divergence", "unclassified",
                   "unclassified: our projection differs from the oracle's")
    if single is not None:
        base = erase_channels(single)
        if ours_ir == base:
            return Row(fam, ident, rs, text, oracle_text, value, "divergence", "peerless",
                       NOTE_PEERLESS)
        if drop_unguarded_loops(ours_ir) == base:
            return Row(fam, ident, rs, text, oracle_text, value, "divergence",
                       "peerless-and-unguarded", NOTE_PEERLESS + "  " + NOTE_UNGUARDED)
    if not contractive(c.g):
        return Row(fam, ident, rs, text, oracle_text, value, "divergence", "non-contractive",
                   "ours wrong: our projection accepts a recursion whose body loops back before "
                   "an action, and gives a Loop that can never act.  The oracle rejects a type "
                   f"that is not contractive ({ITP23}, gcontractive in elimination.v, and proj)")
    if pred:
        return Row(fam, ident, rs, text, oracle_text, value, "divergence", "outside-oracle-domain",
                   f"the type is outside the oracle's domain ({pred}, elimination.v); our "
                   "projection gives a local type for it")
    return Row(fam, ident, rs, text, oracle_text, value, "divergence", "accepts-unprojectable",
               "ours wrong: our projection accepts a role that the oracle rejects with and "
               f"without channels ({ITP23}, proj)")


def _old_execution_row(c: Case, roles: list[int], values: dict[int, str],
                       answers: dict[int, Local | None]) -> Row:
    """Run our projection of an accepted type, and compare with the oracle's run."""
    from probe import OLD_NS, erase_channels, read_protocol
    text = show_global(c.g)
    fixed: dict[int, Local] = {}
    peerless: dict[int, Local] = {}
    for r in roles:
        ours_ir = erase_channels(read_protocol(values[r], OLD_NS, labelled=True))
        oracle = answers[r]
        if oracle is not None and erase_channels(oracle) == ours_ir:
            fixed[r] = oracle
        else:
            peerless[r] = ours_ir
    participants = roles_of(c.g)
    candidates = {r: sorted(participants - {r}) for r in peerless}
    if not peerless:
        verdict = explore(fixed)
        ours_text, ours_safe = verdict.text(), verdict.is_safe
        pair_safe = ours_safe
    else:
        pair = explore_bindings(fixed, peerless, candidates, inbox=False)
        inbox = explore_bindings(fixed, peerless, candidates, inbox=True)
        ours_text = f"per-pair queues: {pair.text()} | one inbox per role: {inbox.text()}"
        pair_safe = bool(pair.safe) or pair.skipped
        ours_safe = pair_safe or bool(inbox.safe) or inbox.skipped
    rejected = [r for r in roles if answers[r] is None]
    oracle_safe: bool | None
    if rejected:
        oracle_text, oracle_safe = f"rejects role {','.join(map(str, rejected))}", None
    else:
        oracle_verdict = explore({r: answers[r] for r in roles})  # type: ignore[misc]
        oracle_text, oracle_safe = oracle_verdict.text(), oracle_verdict.is_safe
    fam = "old.execution"
    if oracle_safe and ours_safe:
        return Row(fam, c.ident, "-", text, oracle_text, ours_text, "agree", "", "")
    if oracle_safe is None and not ours_safe:
        return Row(fam, c.ident, "-", text, oracle_text, ours_text, "divergence",
                   "accepted-and-fails",
                   "ours wrong: our relations accept this global type, the oracle rejects it, "
                   "and every implementation of our projection that a local type can describe "
                   "fails when it runs")
    if oracle_safe is None and not pair_safe:
        return Row(fam, c.ident, "-", text, oracle_text, ours_text, "divergence",
                   "needs-shared-inbox",
                   "ours wrong for per-pair queues: every static peer binding of our projection "
                   "deadlocks or fails, and only one shared inbox per role runs it.  A shared "
                   f"queue is the model in which subject reduction fails ({ECOOP25}, section 1)")
    if oracle_safe is None:
        return Row(fam, c.ident, "-", text, oracle_text, ours_text, "divergence",
                   "accepted-no-failure-found",
                   "ours accepts and the oracle rejects, but a run of our projection with the "
                   "right peers finds no failure within the bounds of execution.py")
    if not oracle_safe:
        return Row(fam, c.ident, "-", text, oracle_text, ours_text, "divergence",
                   "oracle-run-fails",
                   "the oracle's own projection fails when it runs; see the oracle.safety row")
    return Row(fam, c.ident, "-", text, oracle_text, ours_text, "divergence", "ours-fails",
               "ours wrong: the oracle's projection runs safely and ours fails")


def classify_old(c: Case, answers: dict[tuple[int, str], Local | None],
                 measured: dict[int, object]) -> list[Row]:
    """Return every row of one frozen-tree case."""
    text = show_global(c.g)
    roles = old_roles(c.g)
    pair = {r: answers[(r, c.chan)] for r in roles}
    rows: list[Row] = []
    if not _translatable_old(c.g):
        summary = " || ".join(show_local(pair[r]) for r in roles)
        rows.append(Row("old.projection", c.ident, "-", text, summary, "untranslatable", "gap",
                        "inexpressible", NOTE_OUTER))
        safety = _oracle_safety_row(c, roles, pair, "untranslatable")
        return rows + ([safety] if safety else [])
    values: dict[int, str] = {}
    for r in roles:
        m = measured[r]
        rows.append(_old_projection_row(c, r, pair[r], answers[(r, "single")], m))
        if "proj" in m.values and m.rejection is None:  # type: ignore[attr-defined]
            values[r] = m.values["proj"]  # type: ignore[attr-defined]
    wf = _bool(measured[roles[0]].values.get("wf"))  # type: ignore[attr-defined]
    if wf is None:
        raise RuntimeError(f"old case {c.ident}: no well-formedness measurement: "
                           f"{measured[roles[0]]}")
    rejecting = [r for r in roles if pair[r] is None]
    oracle_wf = "false" if (rejecting or _domain_pred(c.g)) else "true"
    if wf == oracle_wf:
        rows.append(Row("old.well_formed", c.ident, "-", text, oracle_wf, wf, "agree", "", ""))
    elif wf == "true" and not contractive(c.g):
        rows.append(Row("old.well_formed", c.ident, "-", text, oracle_wf, wf, "divergence",
                        "non-contractive",
                        "ours wrong: is_global_well_formed accepts a recursion whose body loops "
                        "back before an action.  The oracle's gcontractive (elimination.v) and "
                        f"its proj reject it ({ITP23})"))
    elif wf == "true":
        rows.append(Row("old.well_formed", c.ident, "-", text, oracle_wf, wf, "divergence",
                        "accepts-unprojectable",
                        NOTE_WF.format(roles=", ".join(map(str, rejecting)))))
    else:
        rows.append(Row("old.well_formed", c.ident, "-", text, oracle_wf, wf, "divergence",
                        "rejects-projectable",
                        "ours too strict: is_global_well_formed rejects a global type that the "
                        f"oracle projects onto every role ({ITP23}, proj)"))
    if wf == "true" and len(values) == len(roles):
        rows.append(_old_execution_row(c, roles, values, pair))
    safety = _oracle_safety_row(c, roles, pair, f"well_formed {wf}")
    if safety:
        rows.append(safety)
    return rows


def classify_fixy(c: Case, e0: Local | None, e1: Local | None,
                  measured) -> list[Row]:  # type: ignore[no-untyped-def]
    """Return every row of one fixy case."""
    from probe import FIXY_NS
    text = show_global(c.g)
    oracle = f"{show_local(e0)} || {show_local(e1)}"
    rows: list[Row] = []
    if measured.rejection is not None:
        rows.append(Row("fixy.well_formed", c.ident, "-", text, oracle, f"reject:{measured.rejection}",
                        "divergence", "hard-error",
                        "ours wrong: a fixy relation stops the build with a hard error instead of "
                        "answering; a predicate must fail closed with false"))
        return rows
    t0, t1 = _fixy_spelling(e0), _fixy_spelling(e1)
    if e0 is not None and e1 is not None and (t0 is None or t1 is None):
        rows.append(Row("fixy.dual", c.ident, "-", text, oracle, "untranslatable", "gap",
                        "inexpressible", NOTE_OUTER))
    elif t0 is not None and t1 is not None:
        dual = measured.values.get("dual")
        if dual == _expand(t1, "fs", FIXY_NS):
            rows.append(Row("fixy.dual", c.ident, "-", text, oracle, "=", "agree", "", ""))
        else:
            rows.append(Row("fixy.dual", c.ident, "-", text, oracle,
                            dual or f"reject:{measured.rejection}", "divergence", "unclassified",
                            "unclassified: dual_of_t of the projection onto role 0 is not the "
                            "projection onto role 1"))
        for family, key in (("fixy.is_dual", "isdual"), ("fixy.involution", "invol"),
                            ("fixy.involutive_flag", "invflag"), ("fixy.well_formed", "wf")):
            value = _bool(measured.values.get(key))
            if value is None:
                raise RuntimeError(f"fixy case {c.ident}: no measurement for {key}: {measured}")
            if value == "true":
                rows.append(Row(family, c.ident, "-", text, oracle, value, "agree", "", ""))
            elif family == "fixy.well_formed" and (has_empty_choice(e0) or has_empty_choice(e1)):  # type: ignore[arg-type]
                rows.append(Row(family, c.ident, "-", text, oracle, value, "agree", "", NOTE_EMPTY))
            elif has_idle_loop(e0) or has_idle_loop(e1):  # type: ignore[arg-type]
                rows.append(Row(family, c.ident, "-", text, oracle, value, "divergence",
                                "idle-loop", NOTE_IDLE))
            else:
                rows.append(Row(family, c.ident, "-", text, oracle, value, "divergence",
                                "rejects-projected",
                                f"ours too strict: {family} is false on the oracle's pair "
                                f"({ITP23}, proj)"))
    try:
        n0, n1 = local_of(c.g, 0), local_of(c.g, 1)
        cpp_fixy_local(n0)
        cpp_fixy_local(n1)
    except UntranslatableError:
        rows.append(Row("fixy.accepts", c.ident, "-", text, oracle, "untranslatable", "gap",
                        "inexpressible", NOTE_OUTER))
    else:
        accepts = _bool(measured.values.get("accepts"))
        if accepts is None:
            raise RuntimeError(f"fixy case {c.ident}: no acceptance measurement: {measured}")
        equal = e0 == n0 and e1 == n1
        cell = "equal" if equal else oracle
        if (accepts == "true") == equal:
            rows.append(Row("fixy.accepts", c.ident, "-", text, cell, accepts, "agree", "", ""))
        elif accepts == "true":
            verdict = explore({0: n0, 1: n1})
            klass = "accepts-unprojected" if verdict.is_safe else "accepts-and-fails"
            rows.append(Row("fixy.accepts", c.ident, "-", text, cell, accepts, "divergence", klass,
                            "ours wrong: fixy accepts the naive reading, which the oracle does "
                            f"not produce ({ITP23}, proj); the run of the naive pair gives "
                            f"{verdict.text()}"))
        elif has_empty_choice(n0) or has_empty_choice(n1):
            rows.append(Row("fixy.accepts", c.ident, "-", text, cell, accepts, "agree", "", NOTE_EMPTY))
        elif has_idle_loop(n0) or has_idle_loop(n1):
            rows.append(Row("fixy.accepts", c.ident, "-", text, cell, accepts, "divergence",
                            "idle-loop", NOTE_IDLE))
        else:
            rows.append(Row("fixy.accepts", c.ident, "-", text, cell, accepts, "divergence",
                            "rejects-projected",
                            "ours too strict: fixy rejects the naive reading, which is the "
                            f"oracle's projection ({ITP23}, proj)"))
    safety = _oracle_safety_row(c, [0, 1], {0: e0, 1: e1}, "-")
    if safety:
        rows.append(safety)
    return rows


_MULTI_HELPER = (
    "template <class G, class R, bool = fg::is_global_well_formed_v<G>>\n"
    "struct session_oracle_proj { using type = void; };\n"
    "template <class G, class R>\n"
    "struct session_oracle_proj<G, R, true> { using type = fs::project_t<G, R>; };\n")
MULTI_ALIAS = "namespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;"


def _translatable_fixy_global(g: Global) -> bool:
    try:
        cpp_fixy_global(g)
    except UntranslatableError:
        return False
    return True


def classify_fixy_multi(c: Case, pair: dict[int, Local | None],
                        measured) -> list[Row]:  # type: ignore[no-untyped-def]
    """Return the fixy multiparty rows of one case of the multiparty corpora."""
    from probe import FIXY_NS, SpellingError, read_fixy_projection
    text = show_global(c.g)
    roles = old_roles(c.g)
    rows: list[Row] = []
    if not _translatable_fixy_global(c.g):
        return [Row("fixy.projection", c.ident, "-", text,
                    " || ".join(show_local(pair[r]) for r in roles), "untranslatable", "gap",
                    "inexpressible", NOTE_OUTER.replace("Var_G and Continue", "global::Var"))]
    if measured.rejection is not None:
        return [Row("fixy.global_wf", c.ident, "-", text, "-", f"reject:{measured.rejection}",
                    "divergence", "hard-error",
                    "ours wrong: a fixy global relation stops the build with a hard error instead "
                    "of answering; a predicate must fail closed with false")]
    fwf = _bool(measured.values.get("fwf"))
    live = _bool(measured.values.get("flive"))
    if fwf is None or live is None:
        raise RuntimeError(f"multiparty case {c.ident}: no fixy measurement: {measured}")
    oracle_wf = "true" if (action_ok(c.g) and size_ok(c.g) and contractive(c.g)) else "false"
    if fwf == oracle_wf:
        rows.append(Row("fixy.global_wf", c.ident, "-", text, oracle_wf, fwf, "agree", "", ""))
    else:
        rows.append(Row("fixy.global_wf", c.ident, "-", text, oracle_wf, fwf, "divergence",
                        "wf-differs",
                        "unclassified: is_global_well_formed_v differs from the conjunction of "
                        f"action_pred, size_pred and gcontractive ({ITP23}, elimination.v)"))
    if fwf != "true":
        return rows
    system: dict[int, Local] = {}
    for r in roles:
        spelling = measured.values.get(f"fproj{r}")
        if spelling is None:
            raise RuntimeError(f"multiparty case {c.ident}: no projection onto role {r}")
        try:
            ours = read_fixy_projection(spelling, r)
        except SpellingError as exc:
            rows.append(Row("fixy.projection", c.ident, str(r), text, show_local(pair[r]),
                            spelling, "divergence", "unclassified", f"unclassified: {exc}"))
            continue
        oracle = pair[r]
        fam, rs, oracle_text = "fixy.projection", str(r), show_local(oracle)
        if isinstance(ours, str):
            if oracle is None:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "agree", "", ""))
            else:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence",
                                "incomplete",
                                f"ours incomplete, not unsound: fixy's inductive merge refuses "
                                f"({ours}) a type that the oracle projects.  Projection.h states "
                                "that its projection is a subrelation of the coinductive one "
                                f"(Pischke, Masters, Yoshida, v4, page 9; {ITP23}, proj)"))
            continue
        system[r] = ours
        if oracle is not None:
            try:
                expected = _expand(cpp_fixy_peer_local(oracle), "fs", FIXY_NS)
            except UntranslatableError as exc:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "gap",
                                "inexpressible", f"the oracle's answer has no fixy spelling ({exc})"))
                continue
            if spelling == f"{FIXY_NS}::Projected<{FIXY_NS}::OutQueue<>,{expected}>":
                rows.append(Row(fam, c.ident, rs, text, oracle_text, "=", "agree", "", ""))
            elif equal_up_to_unfolding(ours, oracle):
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "agree", "",
                                "equal to the oracle's answer up to the unfolding of a loop"))
            else:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence",
                                "differs", "unclassified: fixy's projection differs from the "
                                "oracle's; see the fixy.live row for the run"))
            continue
        rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence",
                        "accepts-unprojectable",
                        "ours accepts where the plain-merge oracle rejects.  The full merge of "
                        "Projection.h joins external choices, which the oracle does not do "
                        f"({ITP23}, section 7), so the fixy.live row decides with a run"))
    if len(system) == len(roles):
        verdict = explore(system)
        fam = "fixy.live"
        if live == "true" and not verdict.is_safe:
            rows.append(Row(fam, c.ident, "-", text, verdict.text(), live, "divergence",
                            "live-claim-violated",
                            "ours wrong: is_live_by_construction_v holds, and the run of fixy's own "
                            "projection is not live (Pischke, Masters, Yoshida, v4, Theorem 13)"))
        elif not verdict.is_safe and verdict.kind != "starvation":
            rows.append(Row(fam, c.ident, "-", text, verdict.text(), live, "divergence",
                            "accepted-and-fails",
                            "ours wrong: fixy projects every role, and the run of that projection "
                            "fails.  is_live_by_construction_v is false here, but projection "
                            "alone must still give a safe context"))
        else:
            note = ("the run starves a role; the type is not balanced+, so fixy makes no "
                    "liveness claim" if verdict.kind == "starvation" else "")
            rows.append(Row(fam, c.ident, "-", text, verdict.text(), live, "agree", "", note))
        if verdict.is_safe:
            rows = [dataclasses.replace(
                        r, klass="full-merge-safe",
                        note="neither side is unsound: fixy's full merge joins external choices, "
                             "which the plain-merge oracle does not do "
                             f"({ITP23}, section 7), and the run of fixy's projection is "
                             f"{verdict.text()} (see the fixy.live row)")
                    if r.family == "fixy.projection" and r.klass == "accepts-unprojectable" else r
                    for r in rows]
    return rows


def _multi_probe(g: Global, roles: list[int]) -> str:
    from model import cpp_role
    from probe import probe_header, show
    src = probe_header("multi", MULTI_ALIAS) + _MULTI_HELPER + f"using G = {cpp_fixy_global(g)};\n"
    src += show("fwf", "std::bool_constant<fg::is_global_well_formed_v<G>>")
    src += show("flive", "std::bool_constant<fs::is_live_by_construction_v<G>>")
    for r in roles:
        src += show(f"fproj{r}", f"typename session_oracle_proj<G, {cpp_role(r)}>::type")
    return src


# ── Evaluation pipelines ─────────────────────────────────────────────


def _fixy_probe(t0: str | None, t1: str | None, n0: str | None, n1: str | None) -> str:
    from emit import fixy_accepts_expression
    from probe import probe_header, show
    src = probe_header("fixy", "namespace fs = ::fixy::session;")
    if t0 is not None and t1 is not None:
        src += (f"using T0 = {t0};\nusing T1 = {t1};\n"
                + show("dual", "fs::dual_of_t<T0>")
                + show("isdual", "std::bool_constant<fs::is_dual_v<T0, T1>>")
                + show("invol", "std::bool_constant<std::is_same_v<"
                       "fs::dual_of_t<fs::dual_of_t<T0>>, T0>>")
                + show("invflag", "std::bool_constant<fs::is_dual_involutive_v<T0>>")
                + show("wf", "std::bool_constant<(fs::is_well_formed_v<T0> && "
                       "fs::is_well_formed_v<T1>)>"))
    if n0 is not None and n1 is not None:
        src += (f"using N0 = {n0};\nusing N1 = {n1};\n"
                + show("accepts", f"std::bool_constant<{fixy_accepts_expression()}>"))
    return src


def _old_probe(g: Global, role: int, with_wf: bool) -> str:
    from model import cpp_role
    from probe import probe_header, show
    src = (probe_header("old", "namespace pr = ::crucible::safety::proto;")
           + f"using G = {cpp_old_global(g)};\n"
           + show("proj", f"pr::project_t<G, {cpp_role(role)}>"))
    if with_wf:
        src += show("wf", "std::bool_constant<pr::is_global_well_formed_v<G>>")
    return src


@dataclass(slots=True)
class Env:
    """What an evaluation needs: the compiler, the compiled heads, the pool size.

    ``include`` is the include tree that the probes read.  It is the
    working tree by default.  A snapshot of HEAD measures the committed
    relations when the working tree holds edits that are not committed.
    """

    cxx: str
    heads: Path
    workers: int
    include: Path


def _timeout_row(family: str, c: Case) -> Row:
    import rocq
    return Row(family, c.ident, "-", show_global(c.g), "timeout", "-", "gap", "oracle-timeout",
               f"the oracle gave no answer in {rocq.QUERY_TIMEOUT} s: its projectability "
               f"check unfolds recursion ({ITP23}, section 5)")


def evaluate_old(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Run the oracle and the probes for frozen-tree cases.  O(cases × roles)."""
    import rocq
    from probe import run_many
    queries: list[rocq.Query] = []
    where: dict[str, dict[tuple[int, str], int]] = {}
    for c in cases:
        slots = where.setdefault(c.ident, {})
        for r in old_roles(c.g):
            for chan in dict.fromkeys((c.chan, "single")):
                slots[(r, chan)] = len(queries)
                queries.append(rocq.Query(len(queries), c.g, r, chan))
    answers = rocq.run(queries) if queries else {}
    slow = {c.ident for c in cases
            if any(answers[q] is rocq.TIMEOUT for q in where[c.ident].values())}
    jobs = [(c, r) for c in cases if c.ident not in slow and _translatable_old(c.g)
            for r in old_roles(c.g)]
    multi = [c for c in cases if c.ident not in slow and c.chan == "pair"]
    sources = ([_old_probe(c.g, r, r == 0) for c, r in jobs]
               + [_multi_probe(c.g, old_roles(c.g)) if _translatable_fixy_global(c.g) else ""
                  for c in multi])
    measured = run_many(env.cxx, env.include, sources, env.workers, env.heads)
    by_case: dict[str, dict[int, object]] = {}
    for (c, r), m in zip(jobs, measured[:len(jobs)], strict=True):
        by_case.setdefault(c.ident, {})[r] = m
    by_multi = dict(zip((c.ident for c in multi), measured[len(jobs):], strict=True))
    out: dict[str, list[Row]] = {}
    for c in cases:
        if c.ident in slow:
            out[c.ident] = [_timeout_row("old.projection", c)]
            continue
        answers_c = {k: answers[q] for k, q in where[c.ident].items()}
        rows = classify_old(c, answers_c, by_case.get(c.ident, {}))
        if c.ident in by_multi:
            pair = {r: answers_c[(r, c.chan)] for r in old_roles(c.g)}
            rows += classify_fixy_multi(c, pair, by_multi[c.ident])
        out[c.ident] = rows
    return out


def evaluate_fixy(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Run the oracle and the probes for two-party cases.  O(cases)."""
    import rocq
    from probe import run_many
    queries = [rocq.Query(2 * i + r, c.g, r, c.chan) for i, c in enumerate(cases) for r in (0, 1)]
    answers = rocq.run(queries) if queries else {}
    live = [i for i in range(len(cases))
            if answers[2 * i] is not rocq.TIMEOUT and answers[2 * i + 1] is not rocq.TIMEOUT]
    sources: list[str] = []
    for i in live:
        c = cases[i]
        e0, e1 = answers[2 * i], answers[2 * i + 1]
        try:
            n0: str | None = cpp_fixy_local(local_of(c.g, 0))
            n1: str | None = cpp_fixy_local(local_of(c.g, 1))
        except UntranslatableError:
            n0 = n1 = None
        sources.append(_fixy_probe(_fixy_spelling(e0), _fixy_spelling(e1), n0, n1))  # type: ignore[arg-type]
    measured = dict(zip(live, run_many(env.cxx, env.include, sources, env.workers,
                                       env.heads), strict=True))
    return {c.ident: (classify_fixy(c, answers[2 * i], answers[2 * i + 1], measured[i])  # type: ignore[arg-type]
                      if i in measured else [_timeout_row("fixy.accepts", c)])
            for i, c in enumerate(cases)}


# The bounded asynchronous check costs seconds of compile time for each
# pair of a type with 20 nodes or more, so the subtyping corpus keeps the
# smaller types.  The paper cases are always in it.
SUBTYPE_CASES_PER_CORPUS = 40
SUBTYPE_MAX_SIZE = 16


def _subtype_pairs(c: Case) -> list[tuple[str, Local, Local]]:
    """Return the (role, T, U) triples of one two-party case, or none."""
    from emit import SUBTYPE_MUTATIONS
    from model import mutations
    try:
        u = local_of(c.g, 0)
        cpp_fixy_local(u)
    except (UntranslatableError, ValueError):
        return []
    out: list[tuple[str, Local, Local]] = []
    for k, (name, t) in enumerate(mutations(u, SUBTYPE_MUTATIONS)):
        try:
            cpp_fixy_local(t)
        except UntranslatableError:
            continue
        out.append((f"{k}+{name}", t, u))
        out.append((f"{k}-{name}", u, t))
    return out


def _subtype_key(role: str) -> str:
    return role.replace("+", "f").replace("-", "r").split("@")[0].replace("unfold", "u")


def classify_subtype(c: Case, role: str, t: Local, u: Local, values: dict[str, str]) -> list[Row]:
    """Return the subtyping rows of one pair (T, U).

    The reference is a run.  T refines U when T runs against the dual of
    U at least as safely as U does.  When U itself fails against its
    dual, the pair decides nothing, and either answer agrees.
    """
    from execution import explore_sync
    from model import dual_local, has_empty_choice, has_idle_loop, local_contractive
    text = show_global(c.g)
    key = _subtype_key(role)
    rows: list[Row] = []
    runs = {
        "fixy.subtype_sync": (explore_sync(t, dual_local(u)), explore_sync(u, dual_local(u))),
        "fixy.subtype_async": (explore({0: t, 1: dual_local(u)}, liveness=False),
                               explore({0: u, 1: dual_local(u)}, liveness=False)),
    }
    for fam, prefix in (("fixy.subtype_sync", "s"), ("fixy.subtype_async", "a")):
        spelled = values.get(f"{prefix}{key}")
        run, base = runs[fam]
        if spelled is not None and spelled.startswith("reject:"):
            rows.append(Row(fam, c.ident, role, text, run.text(), spelled, "divergence", "hard-error",
                            "ours wrong: the relation stops the build with a hard error (here the "
                            "constexpr operation budget of the project build) instead of answering"))
            continue
        ours = _bool(spelled)
        if ours is None:
            raise RuntimeError(f"subtype case {c.ident} {role}: no measurement for {fam}: {values}")
        if not base.is_safe:
            rows.append(Row(fam, c.ident, role, text, f"supertype run: {base.text()}", ours, "agree",
                            "", "the supertype fails against its own dual, so the pair decides nothing"))
        elif (ours == "true") == run.is_safe:
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "agree", "", ""))
        elif ours == "true" and has_empty_choice(t):
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "empty-choice",
                            "ours wrong: is_well_formed_v admits a Select with no branch, and the "
                            "relation lets it refine a Select that has branches.  The substitute can "
                            "never send, so the session deadlocks.  A choice needs one branch or more "
                            "(Gay and Hole 2005, the choice rules; the oracle's size_pred)"))
        elif ours == "true":
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "unsound",
                            "ours wrong: the relation holds, U runs safely against its dual, and T "
                            "fails against the dual of U"))
        elif not (local_contractive(t) and local_contractive(u)):
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence",
                            "ill-formed-operand",
                            "neither side is unsound: an operand holds a Loop that reaches its "
                            "Continue before an action, which is_well_formed_v refuses"))
        elif has_idle_loop(t) or has_idle_loop(u):
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "idle-loop",
                            "neither side is unsound: an operand holds a Loop whose body never "
                            "continues, which is_well_formed_v refuses.  Projection.h never writes "
                            "such a Loop"))
        elif fam == "fixy.subtype_async":
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence",
                            "async-not-proven",
                            "ours incomplete by design: the bounded asynchronous check does not prove "
                            "a pair whose run is safe (Subtype.h: precise asynchronous subtyping is "
                            "undecidable; Bravetti, Carbone, Zavattaro 2017)"))
        else:
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "incomplete",
                            "ours too strict: the relation refuses a pair whose synchronous run is "
                            "safe"))
    return rows


def _subtype_source(pairs: list[tuple[str, Local, Local]], relations: tuple[str, ...]) -> str:
    from emit import SUBTYPE_CAPACITY
    from probe import probe_header, show
    src = probe_header("subtype", "namespace fs = ::fixy::session;")
    for role, t, u in pairs:
        key = _subtype_key(role)
        ns = f"p{key}"
        src += f"namespace {ns} {{\nusing T = {cpp_fixy_local(t)};\nusing U = {cpp_fixy_local(u)};\n}}\n"
        if "s" in relations:
            src += show(f"s{key}", f"std::bool_constant<fs::is_subtype_sync_v<{ns}::T, {ns}::U>>")
        if "a" in relations:
            src += show(f"a{key}", f"std::bool_constant<fs::is_subtype_async_v<{ns}::T, {ns}::U, "
                                   f"{SUBTYPE_CAPACITY}>>")
    return src


def evaluate_subtype(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Measure is_subtype_sync_v and is_subtype_async_v on mutation pairs.

    One probe holds every pair of a case.  When that probe stops with a
    hard error, each pair and each relation is probed alone, so the
    error is charged to the one measurement that raised it.  O(pairs).
    """
    from probe import run_many
    work = [(c, _subtype_pairs(c)) for c in cases]
    work = [(c, pairs) for c, pairs in work if pairs]
    measured = run_many(env.cxx, env.include, [_subtype_source(p, ("s", "a")) for _, p in work],
                        env.workers, env.heads)
    values: dict[str, dict[str, str]] = {}
    single: list[tuple[str, str, str]] = []
    for (c, pairs), m in zip(work, measured, strict=True):
        values[c.ident] = dict(m.values)
        if m.rejection is not None:
            single += [(c.ident, role, rel) for role, _, _ in pairs for rel in ("s", "a")]
    if single:
        by_ident = {c.ident: pairs for c, pairs in work}
        sources = [_subtype_source([next(p for p in by_ident[ident] if p[0] == role)], (rel,))
                   for ident, role, rel in single]
        for (ident, role, rel), m in zip(single, run_many(env.cxx, env.include, sources,
                                                           env.workers, env.heads), strict=True):
            key = f"{rel}{_subtype_key(role)}"
            values[ident][key] = (m.values[key] if m.rejection is None and key in m.values
                                  else f"reject:{m.rejection}")
    return {c.ident: [r for role, t, u in pairs
                      for r in classify_subtype(c, role, t, u, values[c.ident])]
            for c, pairs in work}


# ── Shrinking ────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Target:
    """One divergence to shrink: the family, class and role that must survive."""

    kind: str
    family: str
    klass: str
    role: str
    source: str
    g: Global


def _keeps(rows: list[Row], t: Target) -> bool:
    return any(r.family == t.family and r.klass == t.klass and r.status == "divergence"
               and (t.family != "old.projection" or r.role == t.role) for r in rows)


class Evaluator:
    """Evaluate candidate types once each, for the two trees."""

    def __init__(self, env: Env) -> None:
        self.env = env
        self.memo: dict[tuple[str, Global], list[Row]] = {}

    def run(self, kind: str, gs: list[Global]) -> None:
        """Evaluate every type of ``gs`` that is not in the memo."""
        fresh = [g for g in dict.fromkeys(gs) if (kind, g) not in self.memo]
        if not fresh:
            return
        prefix = "m" if kind == "old" else "fm"
        cases = [Case(f"{prefix}{i}", g) for i, g in enumerate(fresh)]
        rows = (evaluate_old if kind == "old" else evaluate_fixy)(cases, self.env)
        for c in cases:
            self.memo[(kind, c.g)] = rows[c.ident]

    def rows(self, kind: str, g: Global) -> list[Row]:
        """Return the memoised rows of ``g``."""
        return self.memo[(kind, g)]


def shrink(targets: list[Target], ev: Evaluator) -> list[Global]:
    """Shrink every target to a local minimum that keeps its divergence.

    First improvement: each round evaluates the next SHRINK_BATCH
    one-step shrinks of every active target, smallest first, in one
    batch, and moves each target to the first of them that keeps the
    family, the class and the role.  A target whose candidates all fail
    stops.  O(rounds × targets × SHRINK_BATCH) evaluations.
    """
    current = [t.g for t in targets]
    cands = {i: shrinks(t.g) for i, t in enumerate(targets)}
    offset = {i: 0 for i in range(len(targets))}
    active = set(range(len(targets)))
    for round_no in range(SHRINK_ROUNDS):
        if not active:
            break
        batch = {i: cands[i][offset[i]:offset[i] + SHRINK_BATCH] for i in active}
        for kind in ("old", "fixy"):
            ev.run(kind, [g for i, gs in batch.items() if targets[i].kind == kind for g in gs])
        moved = 0
        for i in sorted(active):
            hit = next((g for g in batch[i] if _keeps(ev.rows(targets[i].kind, g), targets[i])),
                       None)
            if hit is not None:
                current[i], cands[i], offset[i] = hit, shrinks(hit), 0
                moved += 1
            else:
                offset[i] += SHRINK_BATCH
        active = {i for i in active if offset[i] < len(cands[i])}
        LOG.info("shrink round %d: %d moved, %d still active", round_no + 1, moved, len(active))
    return current


def _kind_of(case: str) -> str:
    return "fixy" if case.startswith("f") else "old"


def minimal_corpus(rows: list[Row], cases: dict[tuple[str, str], Case], ev: Evaluator) -> list[Row]:
    """Shrink every divergence of the generated and hand corpora.  Return the minimal rows.

    Each minimal type becomes one case of corpus m (frozen tree) or fm
    (fixy).  Its divergence rows name the cases that shrank to it.
    """
    groups: dict[tuple[str, str, str, str], list[Target]] = {}
    for row in rows:
        corpus = row.case.rstrip("0123456789")
        if row.status != "divergence" or corpus not in ("r", "a", "h", "fr", "fa"):
            continue
        if row.family.startswith("fixy.subtype"):
            # A subtyping pair is one change of U already, so it is its own
            # minimal form, and the two-party evaluator does not measure it.
            continue
        kind = _kind_of(row.case)
        role_key = row.role if row.family.endswith("projection") else "-"
        groups.setdefault((kind, row.family, row.klass, role_key), []).append(
            Target(kind, row.family, row.klass, row.role, row.case, cases[(kind, row.case)].g))
    # Each class shrinks from its smallest instances.  Larger instances of
    # the same class usually reach the same minimum, at a far higher cost.
    targets: list[Target] = []
    for members in groups.values():
        seen: set[Global] = set()
        for t in sorted(members, key=lambda t: (size(t.g), show_global(t.g))):
            if t.g in seen:
                continue
            seen.add(t.g)
            targets.append(t)
            if len(seen) == SHRINK_PER_CLASS:
                break
    LOG.info("shrinking %d divergences in %d classes", len(targets), len(groups))
    shrunk = shrink(targets, ev)
    finals: list[tuple[Target, Global]] = []
    relabel = [(t, *canonical_roles(g, int(t.role) if t.role != "-" else None))
               for t, g in zip(targets, shrunk, strict=True) if t.kind == "old"]
    ev.run("old", [g2 for _, g2, _ in relabel])
    relabelled = {id(t): (g2, role2) for t, g2, role2 in relabel}
    for t, g in zip(targets, shrunk, strict=True):
        if id(t) in relabelled:
            g2, role2 = relabelled[id(t)]
            moved = dataclasses.replace(t, role=t.role if role2 is None else str(role2))
            if _keeps(ev.rows("old", g2), moved):
                finals.append((moved, g2))
                continue
        finals.append((t, g))
    groups: dict[tuple[str, Global], list[Target]] = {}
    for t, g in finals:
        groups.setdefault((t.kind, g), []).append(t)
    # A target that never moved keeps its original type, which only the
    # full pass evaluated, so every final type is evaluated here.
    for kind in ("old", "fixy"):
        ev.run(kind, [g for k, g in groups if k == kind])
    out: list[Row] = []
    for kind in ("old", "fixy"):
        keys = sorted((k for k in groups if k[0] == kind),
                      key=lambda k: (size(k[1]), show_global(k[1])))
        for n, key in enumerate(keys):
            ident = f"{'m' if kind == 'old' else 'fm'}{n}"
            for row in ev.rows(kind, key[1]):
                row = dataclasses.replace(row, case=ident)
                names = sorted({t.source + (f" role {t.role}" if t.role != "-" else "")
                                for t in groups[key]
                                if t.family == row.family and t.klass == row.klass
                                and (t.role == "-" or t.role == row.role)})
                if row.status == "divergence" and names:
                    shown = ", ".join(names[:8])
                    if len(names) > 8:
                        shown += f" and {len(names) - 8} more"
                    row = dataclasses.replace(row, note=f"{row.note}  Minimal form of {shown}.")
                out.append(row)
    return out


# ── Regenerate ───────────────────────────────────────────────────────


def corpora() -> tuple[list[Case], list[Case]]:
    """Return the frozen-tree cases and the fixy cases of every corpus but the minimal ones."""
    old = [Case(f"r{i}", g) for i, g in enumerate(
        generate(OLD_SEED, OLD_COUNT, roles=OLD_ROLES, max_depth=OLD_DEPTH))]
    old += [Case(f"a{i}", g) for i, g in enumerate(
        generate_adversarial(OLD_ADV_SEED, OLD_ADV_COUNT, OLD_ROLES, OLD_ADV_DEPTH))]
    old += list(HAND_CASES) + list(PAPER_OLD)
    fixy = [Case(f"fr{i}", g) for i, g in enumerate(
        generate(FIXY_SEED, FIXY_COUNT, roles=2, max_depth=FIXY_DEPTH))]
    fixy += [Case(f"fa{i}", g) for i, g in enumerate(
        generate_adversarial(FIXY_ADV_SEED, FIXY_ADV_COUNT, 2, FIXY_ADV_DEPTH))]
    fixy += list(FIXY_HAND) + list(PAPER_FIXY)
    return old, fixy


def regenerate(cxx: str, workers: int, do_shrink: bool = True,
               include: Path | None = None) -> None:
    """Run the full pipeline and write the golden file and the tests."""
    import rocq
    from probe import pch_heads

    old, fixy = corpora()
    include = include or REPO / "include"
    with pch_heads(cxx, include) as heads:
        env = Env(cxx, heads, workers, include)
        LOG.info("evaluating %d frozen-tree cases and %d fixy cases against %s", len(old),
                 len(fixy), include)
        rows = [r for rs in evaluate_old(old, env).values() for r in rs]
        rows += [r for rs in evaluate_fixy(fixy, env).values() for r in rs]
        small = [c for c in fixy if size(c.g) <= SUBTYPE_MAX_SIZE]
        subtype_cases = ([c for c in small if c.ident.startswith("fr")][:SUBTYPE_CASES_PER_CORPUS]
                         + [c for c in small if c.ident.startswith("fa")][:SUBTYPE_CASES_PER_CORPUS]
                         + list(FIXY_HAND) + list(PAPER_FIXY))
        rows += [r for rs in evaluate_subtype(subtype_cases, env).values() for r in rs]
        minimal: list[Row] = []
        if do_shrink:
            cases = {("old", c.ident): c for c in old} | {("fixy", c.ident): c for c in fixy}
            minimal = minimal_corpus(rows, cases, Evaluator(env))
        rows += minimal
    meta = [
        "# Differential test of our session relations against the projection oracle.",
        f"# oracle: github.com/{rocq.PROJECTION_REPO} at {rocq.PROJECTION_COMMIT}",
        f"# toolchain: {rocq.coq_version()}",
        f"# fixy corpora: fr seed {FIXY_SEED}, {FIXY_COUNT} types, depth {FIXY_DEPTH}; "
        f"fa seed {FIXY_ADV_SEED}, {FIXY_ADV_COUNT} types, depth {FIXY_ADV_DEPTH}",
        f"# frozen-tree corpora: r seed {OLD_SEED}, {OLD_COUNT} types over {OLD_ROLES} roles, "
        f"depth {OLD_DEPTH}; a seed {OLD_ADV_SEED}, {OLD_ADV_COUNT} types, depth {OLD_ADV_DEPTH}",
        f"# minimal cases: {len({r.case for r in minimal})}",
    ]
    meta += [f"# {c.ident}: {c.cite}" for c in (*HAND_CASES, *PAPER_OLD, *FIXY_HAND, *PAPER_FIXY)]
    GOLDEN.parent.mkdir(parents=True, exist_ok=True)
    write_golden(GOLDEN, meta, rows)
    _write_tests(read_golden(GOLDEN)[1])
    counts: dict[str, int] = {}
    for r in rows:
        counts[r.status] = counts.get(r.status, 0) + 1
    LOG.info("wrote %d rows: %s", len(rows), counts)


def _write_tests(rows: list[Row]) -> None:
    for name, text in emit_all(rows).items():
        (TEST_DIR / name).write_text(text, encoding="utf-8")


# ── Check and self-test ──────────────────────────────────────────────


def drifted(rows: list[Row], directory: Path) -> list[str]:
    """Return the emitted tests whose committed text in ``directory`` differs."""
    return [name for name, text in emit_all(rows).items()
            if not (directory / name).is_file()
            or (directory / name).read_text(encoding="utf-8") != text]


def check() -> int:
    """Compare the committed tests with a fresh emission.  Return the exit code."""
    try:
        _, rows = read_golden(GOLDEN)
    except (GoldenError, ValueError) as exc:
        print(f"session_oracle check: {exc}", file=sys.stderr)
        return 1
    drift = drifted(rows, TEST_DIR)
    if drift:
        print("session_oracle check: these tests differ from what golden.csv emits: "
              + ", ".join(drift) + ".  Run scripts/session-oracle.sh --emit.", file=sys.stderr)
        return 1
    div = sum(r.status == "divergence" for r in rows)
    gap = sum(r.status == "gap" for r in rows)
    print(f"session_oracle check: {len(rows)} rows, {div} recorded divergences, {gap} gaps, "
          "tests in sync")
    return 0


def _compile(cxx: str, source: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [cxx, "-std=c++26", "-freflection", "-fcontracts", "-fconstexpr-ops-limit=100000000",
         f"-I{REPO / 'include'}", "-fsyntax-only", "-fdiagnostics-color=never", str(source)],
        capture_output=True, text=True)


def _plant(rows: list[Row]) -> tuple[list[Row], dict[str, str]]:
    """Return a copy of ``rows`` with one wrong agree row in each emitted test.

    The map gives, for each emitted file, the label that the planted row's
    assertion prints.
    """
    planted = list(rows)
    labels: dict[str, str] = {}

    def plant(file: str, pick, wrong) -> None:  # type: ignore[no-untyped-def]
        for i, row in enumerate(planted):
            if pick(row):
                planted[i] = dataclasses.replace(row, ours=wrong(row))
                role = f" role {row.role}" if row.role != "-" else ""
                labels[file] = f"session_oracle {row.family} case {row.case}{role}:"
                return
        raise RuntimeError(f"self-test: no agree row to plant in {file}")

    plant("generated_fixy_duality.cpp",
          lambda r: r.family == "fixy.is_dual" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_projection.cpp",
          lambda r: r.family == "fixy.global_wf" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_subtype.cpp",
          lambda r: r.family == "fixy.subtype_sync" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    old_end = "crucible::safety::proto::End"
    plant("generated_old_projection.cpp",
          lambda r: r.family == "old.projection" and r.status == "agree" and r.ours == "=",
          lambda r: old_end if r.oracle != "end"
          else "crucible::safety::proto::Loop<crucible::safety::proto::Continue>")
    missing = set(emit_all(rows)) - set(labels)
    if missing:
        raise RuntimeError(f"self-test: no planted row for {sorted(missing)}")
    return planted, labels


def self_test(cxx: str) -> int:
    """Prove the emitted tests pass and a planted disagreement fails.

    The clean tests compile in parallel.  Each planted row is compiled in
    a test that holds only the rows of its own case, so the check costs
    one small compile for each emitted file.
    """
    from concurrent.futures import ThreadPoolExecutor
    _, rows = read_golden(GOLDEN)
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="session_oracle_selftest_") as tmp:
        root = Path(tmp)
        clean = emit_all(rows)
        for name, text in clean.items():
            (root / name).write_text(text, encoding="utf-8")
        with ThreadPoolExecutor(max_workers=len(clean)) as pool:
            results = list(pool.map(lambda n: (n, _compile(cxx, root / n)), clean))
        for name, proc in results:
            if proc.returncode != 0:
                failures.append(f"clean {name} does not compile:\n{proc.stderr[-2000:]}")
        planted, labels = _plant(rows)
        for name, label in labels.items():
            case = label.split(" case ", 1)[1].split()[0].rstrip(":")
            subset = [r for r in planted if r.case == case]
            path = root / f"planted_{name}"
            path.write_text(emit_all(subset)[name], encoding="utf-8")
            proc = _compile(cxx, path)
            if proc.returncode == 0:
                failures.append(f"planted {name} compiles; the planted row is not caught")
            elif label not in proc.stderr:
                failures.append(f"planted {name} fails, but not on the planted row ({label})")
        # The drift comparison must see a one-line change in a committed test.
        copy = root / "drift"
        copy.mkdir()
        for name, text in clean.items():
            (copy / name).write_text(text, encoding="utf-8")
        victim = sorted(clean)[0]
        (copy / victim).write_text((copy / victim).read_text(encoding="utf-8") + "// drift\n",
                                   encoding="utf-8")
        if drifted(rows, copy) != [victim]:
            failures.append(f"the drift check does not report a changed {victim}")
    if failures:
        for failure in failures:
            print(f"session_oracle self-test: FAIL: {failure}", file=sys.stderr)
        return 1
    print("session_oracle self-test: the clean tests compile, each planted row is caught, "
          "and the drift check sees a one-line change")
    return 0


def main(argv: list[str]) -> int:
    """Parse the command line and run one mode."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("mode", choices=("regenerate", "emit", "check", "self-test"))
    parser.add_argument("--cxx", help="the project compiler (regenerate, self-test)")
    parser.add_argument("--jobs", type=int, default=32, help="parallel probe compiles")
    parser.add_argument("--no-shrink", action="store_true",
                        help="regenerate without the minimal corpus, for a quick look")
    parser.add_argument("--include", type=Path,
                        help="the include tree to measure (regenerate); the default is the "
                             "working tree")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")
    if args.mode in ("regenerate", "self-test") and not args.cxx:
        parser.error(f"{args.mode} needs --cxx")
    if args.mode == "regenerate":
        regenerate(args.cxx, args.jobs, do_shrink=not args.no_shrink, include=args.include)
        return check()
    if args.mode == "emit":
        _write_tests(read_golden(GOLDEN)[1])
        return check()
    if args.mode == "check":
        return check()
    return self_test(args.cxx)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
