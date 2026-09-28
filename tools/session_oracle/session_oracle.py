#!/usr/bin/env python3
"""Differential tests of our session relations against published mechanisations.

We own no proofs.  Each oracle gives the expected answer, and our C++
relations must agree.  Where a relation and an oracle disagree, an
execution of the projected local types (execution.py) shows which side
lets something go wrong.  The oracles:

  rocq.py    the computable projection of Tirore, Bengtson and Carbone
             (ITP 2023, github.com/Tirore96/projection)
  sr.py      the labelled projection and the linearity check of Tirore,
             Bengtson and Carbone (ECOOP 2025)
  ekici.py   the coinductive subtyping relation of Ekici (ITP 2025), which
             coqc checks with a certificate for each pair
  mpstk.py   mpstk-crash-stop (CONCUR 2022), a model check of each
             crash-stop typing context
  keskin.py  the liveness theorem of Keskin, Yoshida and van Glabbeek
             (ITP 2026), which coqc applies with a certificate for each
             projected context
  sprout.py  Sprout(A) of Li and Wies (PLDI 2026), which decides the
             implementability of a global type on per-pair FIFO queues,
             on a mailbox for each receiver and on a bag for each receiver,
             built natively with its solver MuVal

Modes:

  regenerate  Generate the corpora, run the oracles, measure our
              relations, shrink every divergence to a minimal case, and
              write test/session_oracle/golden.csv and the emitted
              tests.  Needs the toolchains that scripts/session-oracle.sh
              names and the project compiler.
  derive      Compute the liveness and implementability rows again from
              the multiparty rows of the golden file, and write the golden
              file.  Needs the toolchains of keskin.py and sprout.py only.
              With the compiler, it also measures the semantics families
              again.
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
  keyed choices    the same relations on types whose choices carry label
                   keys in any order (the section "Keyed choices")
  crash-stop       project_crash_t and crash_live_by_construction_v,
                   against mpstk (the section "Crash-stop")
  runtime types    the projection, the liveness claim and association of
                   the runtime types that a send reaches (the section
                   "En-route global types")
  liveness         is_live_by_construction_v against a coqc-checked proof
                   of liveCtx for fixy's projected context (keskin.py)
  implementability fixy's projection verdict against implementability on
                   three kinds of network (sprout.py)
  semantics        the global and configuration transition systems and
                   crash association of Semantics.h and CrashAssociation.h,
                   along the run of each projected context (semantics.py)

The corpora are listed in emit.CORPORA, and the families in emit.FAMILIES.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import io
import logging
import subprocess
import sys
import tarfile
import tempfile
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

# The oracle writes no bytecode into the source tree, whichever runner starts it.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from emit import GoldenError, Row, emit_all, read_golden, write_golden  # noqa: E402
from execution import equal_up_to_unfolding, explore  # noqa: E402
from model import (GBranch, GEnd, GMsg, GRec, GVar, Global, Local,  # noqa: E402
                   UntranslatableError, action_ok, canonical_roles, contractive, has_empty_choice,
                   has_idle_loop,
                   cpp_fixy_global, cpp_fixy_local, cpp_fixy_peer_local,
                   generate, generate_adversarial, generate_outer, local_of, roles_of, show_global,
                   show_local, shrinks, size, size_ok)

LOG = logging.getLogger("session_oracle")

REPO = Path(__file__).resolve().parents[2]
TEST_DIR = REPO / "test" / "session_oracle"
GOLDEN = TEST_DIR / "golden.csv"

FIXY_SEED, FIXY_COUNT, FIXY_DEPTH = 20260923, 160, 6
FIXY_ADV_SEED, FIXY_ADV_COUNT, FIXY_ADV_DEPTH = 20260925, 120, 8
MULTI_SEED, MULTI_COUNT, MULTI_DEPTH = 20260924, 100, 6
MULTI_ADV_SEED, MULTI_ADV_COUNT, MULTI_ADV_DEPTH = 20260926, 120, 8
OUTER_SEED, OUTER_COUNT = 20260927, 40
MULTI_ROLES = 3
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
    Case("h5", GBranch(0, 1, (GBranch(1, 2, (GMsg(2, 0, "nat", GEnd()), GMsg(2, 0, "nat", GEnd()))),
                              GBranch(1, 2, (GMsg(2, 0, "nat", GEnd()), GMsg(2, 0, "nat", GEnd()))))),
         cite="a third role that sees one answering choice in each branch of an outer choice; the keyed form "
              "orders its labels differently in the two branches"),
    Case("h6", GBranch(0, 1, (GMsg(0, 2, "nat", GEnd()), GBranch(0, 2, (GEnd(),)))),
         cite="the counterexample to Theorem 4.20 of Barwell, Hou, Yoshida and Zhou (LMCS 2025, arXiv "
              "2311.11851v6), where the en-route sender acts before its message arrives.  The value label and "
              "the branch label 0 stand for its labels a and b"),
    Case("h7", GMsg(0, 1, "nat", GMsg(0, 1, "bool", GEnd())),
         cite="on a bag, one sender sends one label with two payload sorts to one receiver.  The wire word is "
              "the label alone, so the first receive can take the second message"),
    Case("h8", GMsg(0, 2, "nat", GMsg(1, 2, "nat", GEnd())),
         cite="on a bag, two senders send one label to one receiver, and the wire word does not carry the sender"),
    Case("h9", GMsg(0, 1, "nat", GMsg(1, 0, "bool", GMsg(0, 1, "bool", GEnd()))),
         cite="on a bag, one label twice to one receiver, where the second message is sent after the first is "
              "received"),
    Case("h10", GMsg(0, 1, "nat", GBranch(0, 1, (GEnd(),))),
         cite="on a bag, two labels to one receiver with no order between the two messages"),
)

PAPER_MULTI: tuple[Case, ...] = (
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
    Case("fh2", GBranch(0, 1, (GMsg(0, 1, "nat", GEnd()), GMsg(1, 0, "bool", GEnd()), GEnd())),
         cite="a choice of three branches with three different continuations"),
    Case("fh3", GRec(GBranch(0, 1, (GMsg(0, 1, "nat", GVar()), GBranch(1, 0, (GVar(), GEnd())), GEnd()))),
         cite="a loop whose choice holds an answering choice in one branch"),
    Case("fh4", GBranch(1, 0, (GBranch(0, 1, (GEnd(), GMsg(0, 1, "nat", GEnd()))),
                               GBranch(0, 1, (GMsg(0, 1, "nat", GEnd()), GEnd())))),
         cite="an offer whose two branches hold selections with the same labels and swapped continuations"),
)

PAPER_FIXY: tuple[Case, ...] = (
    Case("p_ekici25_ex18", GRec(GMsg(1, 0, "bool", GMsg(0, 1, "bool", GMsg(
        1, 0, "nat", GMsg(0, 1, "nat", GVar()))))),
         cite="Ekici, Kamegai, Yoshida, ITP 2025, Example 18, the local type T"),
)

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


def _fixy_spelling(e: Local | None) -> str | None:
    if e is None:
        return None
    try:
        return cpp_fixy_local(e)
    except UntranslatableError:
        return None


def projected_roles(g: Global) -> list[int]:
    """Return the roles onto which a multiparty case is projected."""
    from emit import multiparty_roles
    return multiparty_roles(roles_of(g))


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
    roles = projected_roles(c.g)
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


def _network_probe(g: Global) -> str:
    from emit import NETWORK_ENUMERATORS
    from probe import probe_header, show
    src = probe_header("multi", MULTI_ALIAS) + f"using G = {cpp_fixy_global(g)};\n"
    for network, enumerator in NETWORK_ENUMERATORS.items():
        src += show(f"net_{network}", "std::integral_constant<int, static_cast<int>("
                    f"fs::network_refusal_v<G, fs::Network::{enumerator}>)>")
    return src


def _network_refusal_of(spelling: str | None) -> str | None:
    """Name the NetworkRefusal that a spelling std::integral_constant<int,N> holds, or return None."""
    from emit import NETWORK_REFUSALS
    prefix = "std::integral_constant<int,"
    if spelling is None or not spelling.startswith(prefix) or not spelling.endswith(">"):
        return None
    index = int(spelling[len(prefix):-1])
    return NETWORK_REFUSALS[index] if 0 <= index < len(NETWORK_REFUSALS) else None


def evaluate_network(rows: list[Row], env: Env) -> list[Row]:
    """Measure network_refusal_v of fixy/session/Network.h for each well-formed multiparty case.

    A row pins fixy's verdict on one network, None where implementable_on_v
    admits the type and the reason of the refusal otherwise.  The
    implementability family (evaluate_sprout) compares that verdict with
    Sprout(A).  A probe that stops the build stops the run, because a gate
    must answer.  O(cases × networks).
    """
    from emit import NETWORK_ENUMERATORS
    from model import read_global
    from probe import run_many
    cases = [r for r in rows if r.family == "fixy.global_wf" and r.ours == "true"]
    sources = [_network_probe(read_global(r.global_text)) for r in cases]
    measured = run_many(env.cxx, env.include, sources, env.workers, env.heads)
    out: list[Row] = []
    for wf, m in zip(cases, measured, strict=True):
        if m.rejection is not None:
            raise RuntimeError(f"fixy.network case {wf.case}: the probe stops the build ({m.rejection})")
        for network in NETWORK_ENUMERATORS:
            verdict = _network_refusal_of(m.values.get(f"net_{network}"))
            if verdict is None:
                raise RuntimeError(f"fixy.network case {wf.case}: no measurement on {network}: {m}")
            out.append(Row("fixy.network", wf.case, network, wf.global_text, "-", verdict, "agree", "", ""))
    return out


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
                + show("invflag", "std::bool_constant<std::is_same_v<"
                       "fs::dual_of_t<fs::dual_of_t<T0>>, T0>>")
                + show("wf", "std::bool_constant<(fs::is_well_formed_v<T0> && "
                       "fs::is_well_formed_v<T1>)>"))
    if n0 is not None and n1 is not None:
        src += (f"using N0 = {n0};\nusing N1 = {n1};\n"
                + show("accepts", f"std::bool_constant<{fixy_accepts_expression()}>"))
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


def evaluate_multi(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Run the oracles and the probes for multiparty cases.  O(cases × roles).

    A case whose projection query gets no answer in time gets one
    oracle.safety gap row and no other row, because each other row
    compares with that answer.
    """
    import rocq
    import sr
    from probe import run_many
    queries: list[rocq.Query] = []
    where: dict[str, dict[int, int]] = {}
    for c in cases:
        slots = where.setdefault(c.ident, {})
        for r in projected_roles(c.g):
            slots[r] = len(queries)
            queries.append(rocq.Query(len(queries), c.g, r, c.chan))
    answers = rocq.run(queries) if queries else {}
    slow = {c.ident for c in cases
            if any(answers[q] is rocq.TIMEOUT for q in where[c.ident].values())}
    multi = [c for c in cases if c.ident not in slow and c.chan == "pair"]
    keyed = [(c, kg) for c in multi if (kg := keyed_global(c)) is not None]
    sr_queries = [(kg, r, "pair") for c, kg in keyed for r in projected_roles(c.g)]
    sr_flat = sr.project(sr_queries) if sr_queries else []
    sr_answers: dict[str, dict[int, object]] = {}
    position = 0
    for c, _ in keyed:
        for r in projected_roles(c.g):
            sr_answers.setdefault(c.ident, {})[r] = sr_flat[position]
            position += 1
    sources = ([_multi_probe(c.g, projected_roles(c.g)) if _translatable_fixy_global(c.g) else ""
                for c in multi]
               + [_keyed_multi_probe(kg, c.g, projected_roles(c.g)) for c, kg in keyed])
    measured = run_many(env.cxx, env.include, sources, env.workers, env.heads)
    by_multi = dict(zip((c.ident for c in multi), measured[:len(multi)], strict=True))
    by_keyed = {c.ident: (kg, m) for (c, kg), m in zip(keyed, measured[len(multi):], strict=True)}
    out: dict[str, list[Row]] = {}
    for c in cases:
        if c.ident in slow:
            out[c.ident] = [_timeout_row("oracle.safety", c)]
            continue
        pair = {r: answers[q] for r, q in where[c.ident].items()}
        safety = _oracle_safety_row(c, projected_roles(c.g), pair, "-")
        rows = [safety] if safety else []
        if c.ident in by_multi:
            rows += classify_fixy_multi(c, pair, by_multi[c.ident])
            if c.ident in by_keyed:
                kg, m = by_keyed[c.ident]
                rows += classify_keyed_multi(c, kg, sr_answers[c.ident], pair, m)  # type: ignore[arg-type]
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


def classify_subtype(c: Case, role: str, t: Local, u: Local, values: dict[str, str],
                     families: tuple[str, str] = ("fixy.subtype_sync", "fixy.subtype_async")) -> list[Row]:
    """Return the subtyping rows of one pair (T, U).

    The reference is a run.  T refines U when T runs against the dual of
    U at least as safely as U does, and when the run can still end from
    each state where the side of U can end.  When U itself fails against
    its dual, the pair decides nothing, and either answer agrees.  A run
    matches a message to a branch by its label: the position of a
    positional choice, and the key of a keyed choice (labelled.keyed_label).
    ``families`` names the synchronous and the asynchronous family.
    """
    from execution import explore_sync, keeps_exits_async, keeps_exits_sync
    from model import dual_local, has_empty_choice, has_idle_loop, has_step, local_contractive
    text = show_global(c.g)
    key = _subtype_key(role)
    rows: list[Row] = []
    sync_family, async_family = families
    runs = {
        sync_family: (explore_sync(t, dual_local(u)), explore_sync(u, dual_local(u))),
        async_family: (explore({0: t, 1: dual_local(u)}, liveness=False),
                       explore({0: u, 1: dual_local(u)}, liveness=False)),
    }
    for fam, prefix in ((sync_family, "s"), (async_family, "a")):
        spelled = values.get(f"{prefix}{key}")
        run, base = runs[fam]
        keeps_exits = keeps_exits_sync if fam == sync_family else keeps_exits_async
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
        elif ours == "false" and run.is_safe and _bool(values.get(f"x{key}")) == "true":
            if keeps_exits(t, dual_local(u)):
                rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "exit-stricter",
                                "ours too strict: the relation refuses the pair because T removes an exit "
                                "of U, and the run of T against the dual of U can still end from each "
                                "state where U can.  Exit preservation reads the product of the two types, "
                                "which is a sufficient condition"))
            else:
                rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "loses-exit",
                                "ours stricter by design: T runs safely against the dual of U, but from a "
                                "state of that run where U could still end, the run cannot end.  T removes "
                                "an exit that U offers, and refinement keeps each exit of the supertype "
                                "(fair subtyping, Padovani and Zavattaro, TOPLAS 2026)"))
        elif ours == "true" and run.is_safe and not keeps_exits(t, dual_local(u)):
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "exit-lost",
                            "ours wrong: the relation holds, but from a state of the run of T against "
                            "the dual of U where U could still end, the run cannot end.  T removes an "
                            "exit that U offers"))
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
        elif has_step(t) != has_step(u):
            rows.append(Row(fam, c.ident, role, text, run.text(), ours, "divergence", "keyed-step-shape",
                            "ours too strict: the relation compares a keyed step, a plain Send or Recv of a "
                            "Labelled message, with a keyed choice by shape, so the step never refines the "
                            "choice or is refined by it.  A step is a choice of one branch whose label is on "
                            "the wire (Pischke, Masters, Yoshida, v4, p&{m(B).T}), and the run pairs it with "
                            "the branch of its label"))
        elif fam == async_family:
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


def _subtype_source(pairs: list[tuple[str, Local, Local]], relations: tuple[str, ...],
                    keyed: bool = False) -> str:
    from emit import SUBTYPE_CHANNEL, subtype_channel_decl
    from labelled import cpp_fixy_keyed_local
    from probe import probe_header, show
    spell = cpp_fixy_keyed_local if keyed else cpp_fixy_local
    src = probe_header("keyed" if keyed else "subtype", "namespace fs = ::fixy::session;") + subtype_channel_decl()
    for role, t, u in pairs:
        key = _subtype_key(role)
        ns = f"p{key}"
        src += f"namespace {ns} {{\nusing T = {spell(t)};\nusing U = {spell(u)};\n}}\n"
        if "s" in relations:
            src += show(f"s{key}", f"std::bool_constant<fs::is_subtype_sync_v<{ns}::T, {ns}::U>>")
            src += show(f"x{key}", f"std::bool_constant<fs::subtype_mismatch_v<{ns}::T, {ns}::U> == "
                                   f"::foundation::algebra::transition::mismatch::loses_termination>")
        if "a" in relations:
            src += show(f"a{key}", f"std::bool_constant<fs::is_subtype_async_v<{ns}::T, {ns}::U, "
                                   f"::{SUBTYPE_CHANNEL}>>")
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
            reason = f"x{_subtype_key(role)}"
            if rel == "s" and m.rejection is None and reason in m.values:
                values[ident][reason] = m.values[reason]
    return {c.ident: [r for role, t, u in pairs
                      for r in classify_subtype(c, role, t, u, values[c.ident])]
            for c, pairs in work}


# ── Keyed choices ────────────────────────────────────────────────────
#
# A keyed choice puts the label word of each branch on the wire, and the
# relations match its branches by label in any order: a Select of the
# subtype sends a subset of the labels, and an Offer receives a superset
# (Subtype.h; Gay and Hole 2005).  The positional corpora above cannot
# exercise it, because every positional choice has the labels 0, 1, ...
# in order.  Two families do:
#
#   fixy.keyed_subtype_*   pairs (T, U) where U is the keyed binary view of
#                          a two-party global type (labelled.keyed_local_of,
#                          labels 3k+1) and T one keyed change of U
#                          (labelled.keyed_mutations).  The reference is the
#                          run, which matches branches by label.
#   fixy.keyed_projection  the projection of a multiparty global type whose
#   fixy.keyed_live        choices carry the labels 3k+1 in an order that
#                          depends on their path (labelled.keyed_scheme),
#                          against the projection of the subject-reduction
#                          development (ECOOP 2025), whose branches carry
#                          labels too, and against a run.

KEYED_SUBTYPE_FAMILIES = ("fixy.keyed_subtype_sync", "fixy.keyed_subtype_async")


def keyed_subtype_pairs(c: Case) -> list[tuple[str, Local, Local]]:
    """Return the (role, T, U) triples of the keyed pairs of one two-party case, or none."""
    from labelled import (cpp_fixy_keyed_local, from_positional, keyed_local_of, keyed_mutations,
                          sparse_scheme)
    try:
        u = keyed_local_of(from_positional(c.g, sparse_scheme), 0)
        cpp_fixy_keyed_local(u)
    except (UntranslatableError, ValueError):
        return []
    out: list[tuple[str, Local, Local]] = []
    for k, (name, t) in enumerate(keyed_mutations(u)):
        try:
            cpp_fixy_keyed_local(t)
        except UntranslatableError:
            continue
        out.append((f"k{k}+{name}", t, u))
        out.append((f"k{k}-{name}", u, t))
    return out


def evaluate_keyed_subtype(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Measure the two relations on the keyed pairs.  O(pairs).

    A probe that stops with a hard error is measured again pair by pair,
    as in evaluate_subtype, so the error is charged to its own pair.
    """
    from probe import run_many
    work = [(c, keyed_subtype_pairs(c)) for c in cases]
    work = [(c, pairs) for c, pairs in work if pairs]
    measured = run_many(env.cxx, env.include, [_subtype_source(p, ("s", "a"), keyed=True) for _, p in work],
                        env.workers, env.heads)
    values: dict[str, dict[str, str]] = {}
    single: list[tuple[str, str, str]] = []
    for (c, pairs), m in zip(work, measured, strict=True):
        values[c.ident] = dict(m.values)
        if m.rejection is not None:
            single += [(c.ident, role, rel) for role, _, _ in pairs for rel in ("s", "a")]
    if single:
        by_ident = {c.ident: pairs for c, pairs in work}
        sources = [_subtype_source([next(p for p in by_ident[ident] if p[0] == role)], (rel,), keyed=True)
                   for ident, role, rel in single]
        for (ident, role, rel), m in zip(single, run_many(env.cxx, env.include, sources,
                                                           env.workers, env.heads), strict=True):
            key = f"{rel}{_subtype_key(role)}"
            values[ident][key] = (m.values[key] if m.rejection is None and key in m.values
                                  else f"reject:{m.rejection}")
            reason = f"x{_subtype_key(role)}"
            if rel == "s" and m.rejection is None and reason in m.values:
                values[ident][reason] = m.values[reason]
    return {c.ident: [r for role, t, u in pairs
                      for r in classify_subtype(c, role, t, u, values[c.ident], KEYED_SUBTYPE_FAMILIES)]
            for c, pairs in work}


# ── Subtyping against the ITP 2025 relation ──────────────────────────
#
# ekici.subtype is a record family: for each positional and keyed pair of
# the two synchronous families, the verdict of subtypeC that coqc checked.
# subtypeC has no exit clause and no well-formedness condition, and every
# action in it is a choice, so three of its divergences are by design and
# carry their own class.


def evaluate_ekici(cases: list[Case], sync_rows: list[Row]) -> dict[str, list[Row]]:
    """Check each synchronous subtyping verdict against the relation of the ITP 2025 development.

    ``sync_rows`` are the fixy.subtype_sync and fixy.keyed_subtype_sync rows
    of ``cases``.  Each pair gets a certificate that coqc checks against the
    development's own subtypeC (ekici.py).  O(pairs).
    """
    import ekici
    from execution import keeps_exits_sync
    from model import dual_local, has_step, local_contractive
    by_key = {(r.case, r.role): r for r in sync_rows
              if r.family in ("fixy.subtype_sync", "fixy.keyed_subtype_sync")}
    fam = "ekici.subtype"
    out: dict[str, list[Row]] = {}
    work: list[tuple[Case, str, Local, Local, Row]] = []
    for c in cases:
        for role, t, u in _subtype_pairs(c) + keyed_subtype_pairs(c):
            row = by_key.get((c.ident, role))
            if row is None or row.ours.startswith("reject:"):
                continue
            if local_contractive(t) and local_contractive(u):
                work.append((c, role, t, u, row))
                continue
            out.setdefault(c.ident, []).append(Row(
                fam, c.ident, role, show_global(c.g), "no tree", row.ours, "gap", "no-tree",
                "an operand holds a loop that reaches its Continue before an action.  The development's "
                "local types are coinductive trees whose every node acts, so such a loop has no tree, and "
                "subtypeC gives no verdict.  fixy refuses the operand (is_well_formed_v)"))
    verdicts = ekici.decide([(t, u) for _, _, t, u, _ in work]) if work else []
    for (c, role, t, u, row), (proved, note) in zip(work, verdicts, strict=True):
        text = show_global(c.g)
        rows = out.setdefault(c.ident, [])
        if proved is None:
            rows.append(Row(fam, c.ident, role, text, "refused", row.ours, "divergence", "certificate-refused",
                            f"harness defect: coqc refused the certificate, so the pair has no verdict: {note}"))
            continue
        oracle = "proved" if proved else f"refuted: {note}"
        ours_true = row.ours == "true"
        if proved == ours_true:
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "agree", "", ""))
        elif not proved:
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "divergence", "unsound-vs-published",
                            "ours wrong: the relation holds, and coqc checks a proof that subtypeC of the ITP "
                            "2025 development does not hold for the pair"))
        elif any(has_empty_choice(x) or has_idle_loop(x) for x in (t, u)):
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "divergence", "ill-formed-operand",
                            "neither side is unsound: fixy refuses an operand that is not well-formed (an empty "
                            "choice, or a loop that never continues), and subtypeC has no well-formedness "
                            "condition"))
        elif not keeps_exits_sync(t, dual_local(u)):
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "divergence", "exit-preservation",
                            "ours stricter by design: coqc checks subtypeC, which has no exit clause, and from a "
                            "state of the run of T against the dual of U where U could end, the run cannot end.  "
                            "Refinement keeps each exit of the supertype (fair subtyping, Padovani and "
                            "Zavattaro, TOPLAS 2026)"))
        elif has_step(t) != has_step(u):
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "divergence", "keyed-step-shape",
                            "ours too strict: coqc checks subtypeC, where every action is a choice, and fixy "
                            "compares a keyed step with a keyed choice by shape"))
        else:
            rows.append(Row(fam, c.ident, role, text, oracle, row.ours, "divergence", "incomplete",
                            "ours too strict: coqc checks subtypeC of the ITP 2025 development for a pair that "
                            "fixy refuses"))
    return out


def keskin_systems(rows: list[Row]) -> list[tuple[Row, Global, dict[int, Local]]]:
    """Return fixy's projected context of each multiparty case that has a fixy.live row.

    The context comes from the fixy.projection rows of the case: the oracle's
    answer when fixy's spelling equals it, and the parsed spelling otherwise.
    """
    from model import read_global, read_local
    from probe import read_fixy_projection
    by_case: dict[str, list[Row]] = {}
    for r in rows:
        if r.family == "fixy.projection":
            by_case.setdefault(r.case, []).append(r)
    out = []
    for live in (r for r in rows if r.family == "fixy.live"):
        system: dict[int, Local] = {}
        for r in by_case.get(live.case, []):
            local = read_local(r.oracle) if r.ours == "=" else read_fixy_projection(r.ours, int(r.role))
            if local is None or isinstance(local, str):
                raise RuntimeError(f"keskin: case {live.case} has a fixy.live row, and role {r.role} has "
                                   f"no projected type ({r.ours})")
            system[int(r.role)] = local
        out.append((live, read_global(live.global_text), system))
    return out


def evaluate_keskin(rows: list[Row]) -> list[Row]:
    """Check fixy's projected contexts against the liveness theorem of the ITP 2026 development.

    ``rows`` are the rows of the multiparty families.  A context whose
    premises hold gets a certificate that coqc checks (keskin.py).
    O(cases × states²).
    """
    import keskin
    from keskin import Premise
    fam = "keskin.live"
    out: list[Row] = []
    work: list[tuple[Row, object]] = []
    for live, g, system in keskin_systems(rows):
        try:
            work.append((live, keskin.analyse(g, system)))
        except Premise as exc:
            claim = (".  fixy claims liveness here, and the fixy.live row decides with a run"
                     if live.ours == "true" else "")
            out.append(Row(fam, live.case, "-", live.global_text,f"no verdict: {exc}", live.ours, "gap",
                           exc.name, f"the premise {exc.name} of the liveness theorem of Keskin, Yoshida and "
                                     f"van Glabbeek (ITP 2026, STLive/lemma/liveness.v) does not hold, so the "
                                     f"theorem gives no verdict{claim}"))
    verdicts = keskin.decide([inst for _, inst in work]) if work else []  # type: ignore[misc]
    for (live, _), (proved, reason) in zip(work, verdicts, strict=True):
        if not proved:
            out.append(Row(fam, live.case, "-", live.global_text,"refused", live.ours, "divergence",
                           "certificate-refused",
                           f"harness defect: every premise holds by the generator's computation, and coqc "
                           f"refused the certificate: {reason}"))
        elif live.oracle not in ("safe", "safe-within-bound"):
            out.append(Row(fam, live.case, "-", live.global_text,"liveCtx proved", live.ours, "divergence",
                           "run-contradicts-proof",
                           f"harness defect: coqc checks liveCtx of fixy's context, and the run of the same "
                           f"context is {live.oracle}.  The run or the certificate generator is wrong"))
        elif live.ours == "true":
            out.append(Row(fam, live.case, "-", live.global_text,"liveCtx proved", live.ours, "agree", "", ""))
        else:
            out.append(Row(fam, live.case, "-", live.global_text,"liveCtx proved", live.ours, "divergence",
                           "incomplete",
                           "ours incomplete, not unsound: coqc checks liveCtx of fixy's projected context by the "
                           "liveness theorem of the ITP 2026 development (wfgC, projectableA, tctx_wf and assoc "
                           "hold), and is_live_by_construction_v is false"))
    return out


def crash_systems(rows: list[Row]) -> list[tuple[str, str, frozenset[int], str, dict[int, Local]]]:
    """Return each crash-stop variant whose projection fixy gives for every role.

    The rows are the fixy.crash_projection rows of the golden file: the role
    column is "<variant>/<role>", where the variant is "<kind>.u<unreliable
    roles>", and the ours column spells project_crash_t.  Each result is
    (case, variant, unreliable roles, labelled global text, context).
    O(rows).
    """
    from probe import SpellingError, read_fixy_projection
    groups: dict[tuple[str, str], list[Row]] = {}
    for r in rows:
        if r.family == "fixy.crash_projection":
            groups.setdefault((r.case, r.role.rpartition("/")[0]), []).append(r)
    out = []
    for (case, variant), group in groups.items():
        system: dict[int, Local] = {}
        for r in group:
            try:
                local = read_fixy_projection(r.ours, int(r.role.rpartition("/")[2]))
            except SpellingError:
                break
            if isinstance(local, str):
                break
            system[int(r.role.rpartition("/")[2])] = local
        else:
            digits = variant.rpartition(".u")[2]
            out.append((case, variant, frozenset(int(d) for d in digits), group[0].global_text, system))
    return out


def evaluate_semantics(rows: list[Row], env: Env) -> list[Row]:
    """Walk the transition systems of Semantics.h along the runs of each projected context.

    ``rows`` are the rows of the multiparty and crash-stop families.  A case
    enters when fixy projects every role, and a crash-stop variant enters
    when fixy's crash-stop projection gives every role (semantics.py).
    O((cases + variants) × nodes).
    """
    import semantics
    from labelled import cpp_fixy_global as labelled_global
    from labelled import read as read_labelled
    from probe import run_many
    work = []
    for live, g, system in keskin_systems(rows):
        nodes = semantics.walk(system)
        if nodes:
            work.append((live.case, "", frozenset(), show_global(g), cpp_fixy_global(g), sorted(system), nodes))
    for case, variant, unreliable, text, system in crash_systems(rows):
        nodes = semantics.walk(system, unreliable, semantics.MAX_CRASH_NODES)
        if nodes:
            work.append((case, variant, unreliable, text, labelled_global(read_labelled(text)), sorted(system),
                         nodes))
    sources = [semantics.probe_source(spelling, roles, nodes, unreliable)
               for _, _, unreliable, _, spelling, roles, nodes in work]
    measured = run_many(env.cxx, env.include, sources, env.workers, env.heads)
    out: list[Row] = []
    for (case, variant, _, text, _, _, nodes), m in zip(work, measured, strict=True):
        out += semantics.classify(case, text, nodes, m, variant)
    for note in semantics.check_ledger(out):
        LOG.warning("%s", note)
    return out


def sprout_verdicts(rows: list[Row]) -> list[tuple[Row, str]]:
    """Return the fixy.global_wf row and fixy's projection verdict of each well-formed multiparty case.

    The verdict is "projects" when fixy projects every role, and "refuses"
    when it refuses one.  A case with a gap or an unparsed row has none.
    """
    from probe import SpellingError, read_fixy_projection
    by_case: dict[str, list[Row]] = {}
    for r in rows:
        if r.family == "fixy.projection":
            by_case.setdefault(r.case, []).append(r)
    out = []
    for wf in (r for r in rows if r.family == "fixy.global_wf" and r.ours == "true"):
        projections = by_case.get(wf.case, [])
        if not projections or any(r.status == "gap" for r in projections):
            continue
        try:
            refused = any(r.ours != "=" and isinstance(read_fixy_projection(r.ours, int(r.role)), str)
                          for r in projections)
        except SpellingError:
            continue
        out.append((wf, "refuses" if refused else "projects"))
    return out


def evaluate_sprout(rows: list[Row]) -> list[Row]:
    """Check fixy's projection verdicts against implementability on three kinds of network.

    ``rows`` are the rows of the multiparty families.  Sprout(A) decides the
    implementability of each well-formed case on per-pair FIFO queues, one
    FIFO mailbox for each receiver and one unordered bag for each receiver
    (sprout.py).  Fixy's verdict on a network is its fixy.network row:
    network_refusal_v of fixy/session/Network.h.  Only the naive query
    generator of Sprout(A) counts, because its opt generator admits a type
    that deadlocks on a bag.  A type that fixy admits and the naive
    generator refuses is a soundness defect, and it stops the run.  A type
    that fixy refuses and Sprout(A) admits is an incompleteness gap on the
    shrink-only ledger of semantics.SHRINK_ONLY.  A bag refusal for a
    repeated wire word has a class of its own on that ledger: the message
    of Sprout(A) carries the sender and the payload sort, and fixy's wire
    word carries the label alone.  O(cases × networks).
    """
    import sprout
    from model import read_global
    fam = "sprout.implementable"
    cite = "Li and Wies, PLDI 2026, doi 10.1145/3808319"
    ours_on = {(r.case, r.role): r.ours for r in rows if r.family == "fixy.network"}
    cases = sprout_verdicts(rows)
    answers = sprout.decide([(read_global(wf.global_text),
                              sprout.NETWORKS if ours == "projects" else ("p2pbox",)) for wf, ours in cases])
    out: list[Row] = []
    defects: list[str] = []
    for (wf, _), by_network in zip(cases, answers, strict=True):
        for network, answer in by_network.items():
            measured = ours_on.get((wf.case, network))
            if measured is None:
                raise RuntimeError(f"sprout.implementable case {wf.case}: no fixy.network row on {network}")
            ours = "admits" if measured == "None" else "refuses"
            verdict = str(answer["verdict"])
            naive = str(answer["modes"]["naive"])  # type: ignore[index]
            valid = list(answer["valid"])  # type: ignore[call-overload]
            witness = f"  MuVal finds these queries valid: {' '.join(valid)}" if valid else ""
            row = (fam, wf.case, network, wf.global_text, verdict, ours)
            if ours == "admits" and naive == "non-implementable":
                defects.append(f"{wf.case} on {network}: {wf.global_text}")
            if verdict != "modes-disagree" and naive != verdict:
                out.append(Row(*row, "gap", "naive-silent",
                               f"only the opt query generator of Sprout(A) decides this type on the network "
                               f"{network} (naive {naive}), and only the naive one counts.{witness}"))
            elif verdict == "gclts-ineligible":
                out.append(Row(*row, "gap", verdict,
                               f"Sprout(A) decides implementability only for a sender-driven, sink-final, "
                               f"deterministic protocol with no global deadlock, and this protocol is outside "
                               f"that class, so it gives no verdict on the network {network}.{witness}"))
            elif verdict == "modes-disagree":
                modes = ", ".join(f"{mode} {v}" for mode, v in answer["modes"].items())  # type: ignore[attr-defined]
                out.append(Row(*row, "gap", verdict,
                               f"the two query generators of Sprout(A) give different verdicts on the network "
                               f"{network} ({modes}), so the pair does not count as a verdict.  The naive one "
                               f"still bounds fixy: fixy {ours} the type.{witness}"))
            elif verdict in ("inconclusive", "no-tree"):
                out.append(Row(*row, "gap", verdict,
                               f"Sprout(A) gives no verdict on the network {network} ({verdict})"))
            elif ours == "refuses" and verdict == "implementable" and measured == "RepeatedWordOnBag":
                out.append(Row(*row, "divergence", "bag-wire-word",
                               "our wire is narrower than the model of Sprout(A), not unsound: implementable_on "
                               "refuses two messages to one receiver with one label that no receive orders.  The "
                               "wire word of a message is its label alone, so a receive on a bag cannot tell the "
                               "two apart and can take the second first.  The message of Sprout(A) carries the "
                               f"sender and the payload sort, and its model has no values ({cite})"))
            elif ours == "refuses" and verdict == "implementable":
                out.append(Row(*row, "divergence", f"{network}-incomplete",
                               f"ours incomplete, not unsound: implementable_on refuses a type that an "
                               f"implementation on the network {network} carries ({cite}).  Projection and the "
                               f"reductions of fixy/session/Network.h are sufficient conditions only"))
            else:
                out.append(Row(*row, "agree", "", ""))
    if defects:
        raise RuntimeError("sprout.implementable: fixy admits a type that Sprout(A) refuses on the network, so a "
                           "binding that implementable_on admits can deadlock: " + "; ".join(defects))
    return out


# ── The wire, end to end ─────────────────────────────────────────────
#
# fixy.wire runs a keyed pair (T, U) over the session handle: the handle of
# T against the handle of the dual of U, over one queue of words
# (test/session_oracle/wire_driver.h).  A pair enters when its synchronous
# run is safe, so the reference outcome of every run is ok: each word that
# one endpoint writes names the branch that the other endpoint takes.  Two
# spellings run: the binary view (Labelled), and the form that projection
# writes (PeerMsg, whose label names the peer).


def _runtime_flags(cxx: str) -> list[str]:
    """Return the link flags that let a test binary find the runtime of ``cxx``."""
    lib = subprocess.run([cxx, "-print-file-name=libstdc++.so"], capture_output=True, text=True).stdout.strip()
    return ["-pthread", f"-Wl,-rpath,{Path(lib).parent}"] if lib else ["-pthread"]


def build_and_run(cxx: str, include: Path, source: str, args: list[str]) -> subprocess.CompletedProcess[str]:
    """Compile ``source`` beside wire_driver.h, link it, and run it with ``args``.

    A failed build returns the compiler's result.  The run has a time limit
    of ten minutes.
    """
    with tempfile.TemporaryDirectory(prefix="session_oracle_run_") as tmp:
        src = Path(tmp) / "run.cpp"
        src.write_text(source, encoding="utf-8")
        binary = Path(tmp) / "run"
        built = subprocess.run([cxx, "-std=c++26", "-freflection", "-fcontracts", "-fconstexpr-ops-limit=100000000",
                                f"-I{include}", f"-I{TEST_DIR}", "-fdiagnostics-color=never", str(src), "-o",
                                str(binary), *_runtime_flags(cxx)], capture_output=True, text=True)
        if built.returncode != 0:
            return built
        return subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=600)


def wire_pairs(c: Case, sync_rows: dict[tuple[str, str], Row]) -> list[str]:
    """Return the wire roles of one case: the keyed pairs whose synchronous run is safe, in each spelling and seed."""
    from emit import WIRE_SEEDS, keyed_subtype_locals
    from labelled import cpp_fixy_keyed_local, cpp_fixy_peer_keyed_local
    from model import dual_local, local_contractive
    out: list[str] = []
    for role, t, u in keyed_subtype_pairs(c):
        row = sync_rows.get((c.ident, role))
        if row is None or row.oracle != "safe" or row.ours.startswith("reject:"):
            continue
        if any(has_empty_choice(x) or has_idle_loop(x) or not local_contractive(x) for x in (t, u)):
            continue
        if keyed_subtype_locals(show_global(c.g), role) != (t, u):
            raise RuntimeError(f"wire case {c.ident} {role}: the golden text does not give back the pair")
        spellings = ["view"]
        try:
            cpp_fixy_peer_keyed_local(t, 0)
            cpp_fixy_peer_keyed_local(dual_local(u), 1)
            spellings.append("peer")
        except UntranslatableError:
            pass
        try:
            cpp_fixy_keyed_local(dual_local(u))
        except UntranslatableError:
            continue
        out += [f"{role}/{s}/s{seed}" for s in spellings for seed in WIRE_SEEDS]
    return out


def evaluate_wire(cases: list[Case], sync_rows: list[Row], env: Env) -> dict[str, list[Row]]:
    """Run each safe keyed pair over the handle and compare its outcome with ok.

    One program for each case holds its runs.  O(pairs) runs, each one
    child process.
    """
    from concurrent.futures import ThreadPoolExecutor
    from emit import wire_label, wire_pair, wire_source
    by_key = {(r.case, r.role): r for r in sync_rows if r.family == "fixy.keyed_subtype_sync"}
    work = [(c, wire_pairs(c, by_key)) for c in cases]
    work = [(c, roles) for c, roles in work if roles]

    def measure(item: tuple[Case, list[str]]) -> dict[str, str]:
        c, roles = item
        text = show_global(c.g)
        entries = [(wire_label(c.ident, role), *wire_pair(text, role), "-") for role in roles]
        proc = build_and_run(env.cxx, env.include, wire_source(entries), ["--measure"])
        if proc.returncode != 0:
            raise RuntimeError(f"wire case {c.ident}: the program failed:\n{(proc.stdout + proc.stderr)[-3000:]}")
        outcomes: dict[str, str] = {}
        for line in proc.stdout.splitlines():
            label, _, outcome = line.rpartition(" ")
            outcomes[label] = outcome
        return outcomes

    with ThreadPoolExecutor(max_workers=env.workers) as pool:
        measured = list(pool.map(measure, work))
    out: dict[str, list[Row]] = {}
    for (c, roles), outcomes in zip(work, measured, strict=True):
        text = show_global(c.g)
        rows = out.setdefault(c.ident, [])
        for role in roles:
            outcome = outcomes.get(wire_label(c.ident, role))
            if outcome is None:
                raise RuntimeError(f"wire case {c.ident} {role}: the program printed no outcome")
            twin = outcomes.get(wire_label(c.ident, role.replace("/peer/", "/view/")))
            if outcome == "ok":
                rows.append(Row("fixy.wire", c.ident, role, text, "ok", outcome, "agree", "", ""))
            elif "/peer/" in role and twin == "ok":
                rows.append(Row("fixy.wire", c.ident, role, text, "ok", outcome, "divergence", "peer-word-mismatch",
                                "ours wrong: the run of the binary view of the pair is ok, and the run of the form "
                                "that projection writes is not.  The label word of a PeerMsg names its peer, and "
                                "each endpoint names the other role, so the word that one endpoint writes names no "
                                "branch of the other (fixy/session/Protocol.h, peer_message::label_of)"))
            else:
                rows.append(Row("fixy.wire", c.ident, role, text, "ok", outcome, "divergence", "wire-failure",
                                f"ours wrong: the synchronous run of the pair is safe, and the run over the handle "
                                f"ends with {outcome}: a word that one endpoint writes is not what the other "
                                f"endpoint reads"))
    return out


_KEYED_MULTI_HELPER = (
    "template <class G, class R, bool = fg::is_global_well_formed_v<G>>\n"
    "struct session_oracle_kproj { using type = void; };\n"
    "template <class G, class R>\n"
    "struct session_oracle_kproj<G, R, true> { using type = fs::project_t<G, R>; };\n")


def _keyed_multi_probe(kg, g: Global, roles: list[int]) -> str:  # type: ignore[no-untyped-def]
    from labelled import cpp_fixy_global as labelled_global
    from model import cpp_role
    from probe import probe_header, show
    src = (probe_header("multi", MULTI_ALIAS) + _KEYED_MULTI_HELPER
           + f"using G = {labelled_global(kg)};\nusing P = {cpp_fixy_global(g)};\n")
    src += show("kwf", "std::bool_constant<fg::is_global_well_formed_v<G>>")
    src += show("kpwf", "std::bool_constant<fg::is_global_well_formed_v<P>>")
    src += show("klive", "std::bool_constant<fs::is_live_by_construction_v<G>>")
    src += show("kplive", "std::bool_constant<fs::is_live_by_construction_v<P>>")
    for r in roles:
        src += show(f"kproj{r}", f"typename session_oracle_kproj<G, {cpp_role(r)}>::type")
    return src


def keyed_global(c: Case):  # type: ignore[no-untyped-def]
    """Return the keyed form of a multiparty case, or None when it has no choice to reorder."""
    from labelled import from_positional, has_permutation, keyed_scheme
    if c.chan != "pair" or not has_permutation(c.g) or not _translatable_fixy_global(c.g):
        return None
    return from_positional(c.g, keyed_scheme)


def classify_keyed_multi(c: Case, kg, sr_answers: dict[int, object],  # type: ignore[no-untyped-def]
                         itp: dict[int, Local | None], measured) -> list[Row]:
    """Return the keyed projection rows and the keyed liveness row of one multiparty case.

    ``sr_answers`` holds the projection of the subject-reduction development
    onto each role (a local type, None, or rocq.TIMEOUT).  ``itp`` holds the
    answer of the projection oracle on the positional form of the case.
    """
    import rocq
    from labelled import show as show_labelled
    from probe import SpellingError, read_fixy_projection
    text = show_labelled(kg)
    roles = projected_roles(c.g)
    if measured.rejection is not None:
        return [Row("fixy.keyed_live", c.ident, "-", text, "-", f"reject:{measured.rejection}", "divergence",
                    "hard-error", "ours wrong: a fixy global relation stops the build with a hard error on a "
                    "keyed global type instead of answering; a predicate must fail closed with false")]
    kwf = _bool(measured.values.get("kwf"))
    pwf = _bool(measured.values.get("kpwf"))
    live = _bool(measured.values.get("klive"))
    plive = _bool(measured.values.get("kplive"))
    if kwf is None or pwf is None or live is None or plive is None:
        raise RuntimeError(f"keyed case {c.ident}: no fixy measurement: {measured}")
    if kwf != pwf:
        return [Row("fixy.keyed_live", c.ident, "-", text, f"positional well_formed {pwf}", f"well_formed {kwf}",
                    "divergence", "label-order-sensitive",
                    "ours wrong: is_global_well_formed_v answers differently on the keyed form and on the "
                    "positional form of one global type.  Labels 3k+1 in any order are pairwise distinct, so "
                    "well-formedness must not depend on them")]
    if kwf != "true":
        return [Row("fixy.keyed_live", c.ident, "-", text, "positional well_formed false", "well_formed false",
                    "agree", "", "neither form is well-formed")]
    rows: list[Row] = []
    system: dict[int, Local] = {}
    fam = "fixy.keyed_projection"
    for r in roles:
        spelling = measured.values.get(f"kproj{r}")
        if spelling is None:
            raise RuntimeError(f"keyed case {c.ident}: no projection onto role {r}")
        oracle = sr_answers[r]
        rs = str(r)
        if oracle is rocq.TIMEOUT:
            rows.append(Row(fam, c.ident, rs, text, "timeout", spelling, "gap", "oracle-timeout",
                            f"the subject-reduction development gave no answer in {rocq.QUERY_TIMEOUT} s"))
            continue
        oracle_text = show_local(oracle)  # type: ignore[arg-type]
        try:
            ours = read_fixy_projection(spelling, r)
        except SpellingError as exc:
            rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence", "unclassified",
                            f"unclassified: {exc}"))
            continue
        if isinstance(ours, str):
            if oracle is None:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "agree", "", ""))
            else:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence", "incomplete",
                                f"ours incomplete, not unsound: fixy's inductive merge refuses ({ours}) a keyed "
                                f"type that the development projects ({ECOOP25}, indProj.v, proj)"))
            continue
        system[r] = ours
        if oracle is not None:
            if equal_up_to_unfolding(ours, oracle):  # type: ignore[arg-type]
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "agree", "",
                                "equal to the development's answer when branches are paired by label"))
            else:
                rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence", "differs",
                                "unclassified: fixy's keyed projection differs from the development's; see the "
                                "fixy.keyed_live row for the run"))
            continue
        klass = "merge-by-label" if itp.get(r) is not None else "accepts-unprojectable"
        rows.append(Row(fam, c.ident, rs, text, oracle_text, spelling, "divergence", klass,
                        "ours accepts where the development's plain merge rejects; the fixy.keyed_live row decides "
                        "with a run"))
    if len(system) == len(roles):
        verdict = explore(system)
        lfam = "fixy.keyed_live"
        if live != plive:
            rows.append(Row(lfam, c.ident, "-", text, f"positional live {plive}; run {verdict.text()}", live,
                            "divergence", "label-order-sensitive",
                            "ours wrong: is_live_by_construction_v gives the keyed form of a global type another "
                            "verdict than its positional form.  Only the labels and their order differ, and a "
                            "receiver dispatches by label, so the verdict must not depend on them"))
        elif live == "true" and not verdict.is_safe:
            rows.append(Row(lfam, c.ident, "-", text, verdict.text(), live, "divergence", "live-claim-violated",
                            "ours wrong: is_live_by_construction_v holds on the keyed form, and the run of fixy's "
                            "own projection is not live (Pischke, Masters, Yoshida, v4, Theorem 13)"))
        elif not verdict.is_safe and verdict.kind != "starvation":
            rows.append(Row(lfam, c.ident, "-", text, verdict.text(), live, "divergence", "accepted-and-fails",
                            "ours wrong: fixy projects every role of the keyed form, and the run of that "
                            "projection fails"))
        else:
            rows.append(Row(lfam, c.ident, "-", text, verdict.text(), live, "agree", "",
                            "the run starves a role; the type is not balanced+, so fixy makes no liveness claim"
                            if verdict.kind == "starvation" else ""))
        if verdict.is_safe:
            rows = [dataclasses.replace(
                        r, klass="merge-by-label-safe" if r.klass == "merge-by-label" else "full-merge-safe",
                        note=("neither side is unsound: the development's plain merge compares the branch "
                              "lists of two continuations as written, in order, and fixy pairs them by label.  "
                              "The positional form, whose labels stand in the same order, is projected by the "
                              f"projection oracle ({ITP23}), and the run of fixy's projection is "
                              f"{verdict.text()} (see the fixy.keyed_live row)")
                        if r.klass == "merge-by-label" else
                        ("neither side is unsound: fixy's full merge joins external choices, which the "
                         f"plain merge of the development does not do ({ECOOP25}), and the run of fixy's "
                         f"projection is {verdict.text()} (see the fixy.keyed_live row)"))
                    if r.family == fam and r.klass in ("merge-by-label", "accepts-unprojectable") else r
                    for r in rows]
    return rows


# ── Subject reduction and the run semantics ─────────────────────────
#
# Every subtyping and liveness row above is decided by a run of
# execution.py.  The subject-reduction development proves, for a coherent
# global type (Congruence.v, coherentG: Linear, size_pred, action_pred,
# unique labels, projectable onto every role, goodG), that a process typed
# by its projection reduces to a typed process (SubjectRed4.v,
# subject_reduction_final) and is never in an error state (Safety.v,
# OFT_not_error_struct).  So a run of the development's own projection of a
# coherent type must never take a message of the wrong label or sort.  The
# family sr.safety checks that on three channel layouts: one queue for each
# ordered pair of roles, one queue for each receiver (a mailbox), and one
# queue for the whole session.  A coherent type whose run fails would be a
# defect of execution.py, the reference the other families trust.

SR_LAYOUTS = ("pair", "mailbox", "single")


def sr_channel_spec(layout: str, roles: list[int]) -> str:
    """Return the channel specification (model.channel_table) of a queue layout.

    A shared queue gets a channel number of 64 or more, which execution.py
    reads as a queue that names no receiver.
    """
    if layout == "pair":
        return "pair"
    pairs = [(f, t) for f in roles for t in roles if f != t]
    return "map:" + ",".join(f"{f}>{t}={100 + t if layout == 'mailbox' else 100}" for f, t in pairs)


def evaluate_sr_safety(cases: list[Case]) -> dict[str, list[Row]]:
    """Check the run of the development's projection of each coherent type.  O(cases × layouts × roles)."""
    import rocq
    import sr
    from labelled import from_positional, identity_scheme
    from model import contractive as g_contractive
    work: list[tuple[Case, str, str, list[int]]] = []
    for c in cases:
        if c.chan != "pair":
            continue
        roles = projected_roles(c.g)
        for layout in SR_LAYOUTS:
            work.append((c, layout, sr_channel_spec(layout, roles), roles))
    lg = {c.ident: from_positional(c.g, identity_scheme) for c in cases}
    linear = sr.linear([(lg[c.ident], spec) for c, _, spec, _ in work])
    proj_queries = [(lg[c.ident], r, spec) for c, _, spec, roles in work for r in roles]
    proj_flat = sr.project(proj_queries)
    out: dict[str, list[Row]] = {}
    position = 0
    for (c, layout, spec, roles), lin in zip(work, linear, strict=True):
        answers = proj_flat[position:position + len(roles)]
        position += len(roles)
        text = show_global(c.g)
        fam = "sr.safety"
        if lin is rocq.TIMEOUT or any(a is rocq.TIMEOUT for a in answers):
            out.setdefault(c.ident, []).append(Row(fam, c.ident, layout, text, "timeout", "-", "gap",
                                                   "oracle-timeout",
                                                   f"the subject-reduction development gave no answer in "
                                                   f"{rocq.QUERY_TIMEOUT} s"))
            continue
        rejected = [r for r, a in zip(roles, answers) if a is None]
        coherent = (lin and not rejected and action_ok(c.g) and size_ok(c.g) and g_contractive(c.g))
        oracle = ("coherent" if coherent else
                  "; ".join(filter(None, ["not linear" if not lin else "",
                                          f"rejects role {','.join(map(str, rejected))}" if rejected else "",
                                          "outside size_pred, action_pred or gcontractive"
                                          if not (action_ok(c.g) and size_ok(c.g) and g_contractive(c.g))
                                          else ""])))
        if rejected:
            out.setdefault(c.ident, []).append(Row(fam, c.ident, layout, text, oracle, "-", "agree", "",
                                                   "no run: the development projects no local type for a role"))
            continue
        verdict = explore(dict(zip(roles, answers)), liveness=False)  # type: ignore[arg-type]
        if coherent and verdict.kind == "wrong-type":
            out.setdefault(c.ident, []).append(Row(
                fam, c.ident, layout, text, oracle, verdict.text(), "divergence", "coherent-and-fails",
                "ours wrong: the type is coherent, so the development proves that no typed process takes a "
                "wrong message (Safety.v, OFT_not_error_struct), and the run of execution.py takes one.  The run "
                "semantics that decides the other families disagrees with the mechanised theorem"))
            continue
        note = ""
        if coherent and not verdict.is_safe:
            note = ("the theorem excludes a wrong message, not a deadlock or an orphan message, so this run "
                    "does not contradict it")
        elif not coherent and verdict.kind == "wrong-type":
            note = (f"the type is not coherent, so the theorem makes no claim; a shared queue without "
                    f"linearity is the counterexample class of {ECOOP25}, section 1")
        out.setdefault(c.ident, []).append(Row(fam, c.ident, layout, text, oracle, verdict.text(), "agree",
                                               "", note))
    return out


# ── Crash-stop ───────────────────────────────────────────────────────
#
# Each multiparty case gives crash-stop protocols (labelled.add_crash_branches):
# a set of unreliable roles, and a crash branch on each transmission that an
# unreliable role sends, whose continuation copies the first branch or ends.
# fixy measures the crash-stop projection onto each role (project_crash_t)
# and its liveness claim (crash_live_by_construction_v, Theorem 4.31 of
# Barwell, Hou, Yoshida and Zhou, LMCS 2025).  mpstk-crash-stop then
# model-checks the typing context made of fixy's own projections, with each
# unreliable entry able to crash before each of its actions.
#
#   fixy.crash_projection  fixy's projection onto each role, pinned
#   fixy.crash_live        fixy's liveness claim, pinned
#   mpstk.crash            mpstk's verdict on fixy's context, against the claim

CRASH_KINDS = ("copy", "end")


def crash_variants(c: Case) -> list[tuple[str, frozenset[int], object]]:
    """Return the crash-stop variants of one case: (row prefix, unreliable roles, labelled global type)."""
    from labelled import add_crash_branches, from_positional, identity_scheme
    if c.chan != "pair" or not _translatable_fixy_global(c.g) or not action_ok(c.g) or not size_ok(c.g):
        return []
    base = from_positional(c.g, identity_scheme)
    senders = _senders_in_order(c.g)
    if not senders:
        return []
    choices: list[frozenset[int]] = [frozenset({senders[0]})]
    if len(set(senders)) > 1:
        choices.append(frozenset(senders))
    out = []
    for unreliable in choices:
        for kind in CRASH_KINDS:
            prefix = f"{kind}.u{''.join(map(str, sorted(unreliable)))}"
            out.append((prefix, unreliable, add_crash_branches(base, unreliable, kind)))
    return out


def _senders_in_order(g: Global) -> list[int]:
    """Return the roles that send in ``g``, in preorder of first sending.  O(size)."""
    out: list[int] = []

    def walk(x: Global) -> None:
        if isinstance(x, GRec):
            walk(x.body)
        elif isinstance(x, GMsg):
            if x.frm not in out:
                out.append(x.frm)
            walk(x.cont)
        elif isinstance(x, GBranch):
            if x.frm not in out:
                out.append(x.frm)
            for b in x.branches:
                walk(b)

    walk(g)
    return out


_CRASH_HELPER = (
    "template <class G, class R, class RS, bool = fg::is_global_well_formed_v<G>>\n"
    "struct session_oracle_cproj { using type = void; };\n"
    "template <class G, class R, class RS>\n"
    "struct session_oracle_cproj<G, R, RS, true> { using type = fs::project_crash_t<G, R, RS>; };\n")


def reliable_set_spelling(roles: list[int], unreliable: frozenset[int]) -> str:
    """Return the C++ reliable set: every role of ``roles`` that is not unreliable."""
    from model import cpp_role
    return f"fs::ReliableSet<{', '.join(cpp_role(r) for r in roles if r not in unreliable)}>"


def _crash_probe(lg, g: Global, roles: list[int], unreliable: frozenset[int]) -> str:  # type: ignore[no-untyped-def]
    from labelled import cpp_fixy_global as labelled_global
    from model import cpp_role
    from probe import probe_header, show
    src = (probe_header("multi", MULTI_ALIAS) + _CRASH_HELPER
           + f"using G = {labelled_global(lg)};\nusing P = {cpp_fixy_global(g)};\n"
           + f"using RS = {reliable_set_spelling(roles, unreliable)};\n")
    src += show("cwf", "std::bool_constant<fg::is_global_well_formed_v<G>>")
    src += show("cpwf", "std::bool_constant<fg::is_global_well_formed_v<P>>")
    src += show("clive", "std::bool_constant<fs::crash_live_by_construction_v<G, RS>>")
    for r in roles:
        src += show(f"cproj{r}", f"typename session_oracle_cproj<G, {cpp_role(r)}, RS>::type")
    return src


def evaluate_crash(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Measure fixy's crash-stop projection and claim, and model-check its contexts with mpstk.

    O(variants) probes and O(variants) model checks.
    """
    import mpstk
    from labelled import show as show_labelled
    from probe import SpellingError, read_fixy_projection, run_many
    work = [(c, prefix, unreliable, lg) for c in cases for prefix, unreliable, lg in crash_variants(c)]
    measured = run_many(env.cxx, env.include,
                        [_crash_probe(lg, c.g, projected_roles(c.g), unreliable) for c, _, unreliable, lg in work],
                        env.workers, env.heads)
    out: dict[str, list[Row]] = {}
    pending: list[tuple[str, list[Row], str, str, str]] = []
    contexts: list[str] = []
    for (c, prefix, unreliable, lg), m in zip(work, measured, strict=True):
        text = show_labelled(lg)
        rows = out.setdefault(c.ident, [])
        roles = projected_roles(c.g)
        if m.rejection is not None:
            rows.append(Row("fixy.crash_live", c.ident, prefix, text, "-", f"reject:{m.rejection}", "divergence",
                            "hard-error", "ours wrong: a crash-stop relation stops the build with a hard error "
                            "instead of answering; a predicate must fail closed with false"))
            continue
        cwf, pwf, live = _bool(m.values.get("cwf")), _bool(m.values.get("cpwf")), _bool(m.values.get("clive"))
        if cwf is None or pwf is None or live is None:
            raise RuntimeError(f"crash case {c.ident} {prefix}: no measurement: {m}")
        if cwf != "true" and pwf != "true":
            rows.append(Row("fixy.crash_live", c.ident, prefix, text, "static well_formed false", live, "agree", "",
                            "the static type is not well-formed, and neither is its crash-stop variant"))
            continue
        if cwf != "true":
            rows.append(Row("fixy.crash_live", c.ident, prefix, text, "static well_formed true", live, "divergence",
                            "crash-ill-formed", "unclassified: is_global_well_formed_v refuses the crash-stop "
                            "variant of a global type that it accepts"))
            continue
        system: dict[int, Local] = {}
        refused: list[str] = []
        for r in roles:
            spelling = m.values.get(f"cproj{r}")
            if spelling is None:
                raise RuntimeError(f"crash case {c.ident} {prefix}: no projection onto role {r}")
            rows.append(Row("fixy.crash_projection", c.ident, f"{prefix}/{r}", text, "-", spelling, "agree", "",
                            ""))
            try:
                ours = read_fixy_projection(spelling, r)
            except SpellingError as exc:
                rows[-1] = dataclasses.replace(rows[-1], status="divergence", klass="unclassified",
                                               note=f"unclassified: {exc}")
                refused.append(f"{r} (unreadable)")
                continue
            if isinstance(ours, str):
                refused.append(f"{r} ({ours})")
            else:
                system[r] = ours
        rows.append(Row("fixy.crash_live", c.ident, prefix, text, "-", live, "agree", "", ""))
        claim = f"live {live}" + (f"; refuses role {', '.join(refused)}" if refused else "")
        if refused:
            rows.append(Row("mpstk.crash", c.ident, prefix, text, "no context", claim, "agree", "",
                            "fixy refuses a role, so no context of fixy's projections exists to check"))
            continue
        contexts.append(mpstk.context_text(system, unreliable))
        pending.append((c.ident, rows, prefix, text, claim))
    verdicts = mpstk.verify(contexts) if contexts else []
    for (ident, rows, prefix, text, claim), verdict in zip(pending, verdicts, strict=True):
        shown = " ".join(f"{p} {'true' if v else 'false'}" for p, v in verdict.items())
        all_hold = all(verdict.values())
        if claim.startswith("live true") and not all_hold:
            rows.append(Row("mpstk.crash", ident, prefix, text, shown, claim, "divergence",
                            "crash-live-claim-violated",
                            "ours wrong: crash_live_by_construction_v holds, and mpstk finds that the context of "
                            "fixy's own crash-stop projections is not safe, not deadlock-free or not live when the "
                            "unreliable roles crash (Barwell, Hou, Yoshida and Zhou, LMCS 2025, Theorem 4.31)"))
        elif not verdict["safety"]:
            rows.append(Row("mpstk.crash", ident, prefix, text, shown, claim, "divergence", "projected-and-unsafe",
                            "ours wrong: fixy projects every role under crash-stop, and mpstk finds that the "
                            "context of these projections is not safe.  Projection alone must give a safe context"))
        else:
            note = ("" if all_hold or claim.startswith("live true") else
                    "fixy makes no liveness claim here (the type is not balanced+), so a failed liveness "
                    "verdict does not contradict it")
            rows.append(Row("mpstk.crash", ident, prefix, text, shown, claim, "agree", "", note))
    return out


# ── En-route global types ────────────────────────────────────────────
#
# A runtime global type holds transmissions that their sender sent and
# their receiver did not receive (EnRouteChoice: Barwell, Hou, Yoshida and
# Zhou, LMCS 2025, section 4.1; the one-branch form is the en-route
# message of Pischke, Masters and Yoshida, v4, section 2.1).  No oracle has
# them: the ITP 2023 and ECOOP 2025 global types are static.  Each
# multiparty case gives the runtime types that the first send reaches
# (labelled.enroute_variants), and three properties decide them:
#
#   fixy.enroute_projection  the projection onto each role: the sender's
#                            queue holds the message, the other roles keep
#                            their projection
#   fixy.enroute_live        the claim on the runtime type that a
#                            transition reaches equals the claim on the
#                            static type (a transition keeps balanced+,
#                            Pischke, Masters, Yoshida, v4, Theorem 3 and
#                            Lemma 4), and a run that starts with the
#                            message in flight agrees with the claim
#   fixy.enroute_association the context that the send reaches from the
#                            static projection associates with the runtime
#                            type.  For a transition target (tag e, e<k>)
#                            association is preserved (v4, Definition 21
#                            and Theorem 13).  For the one-branch form of
#                            a choice (tag s<k>) the receiver's context
#                            entry is the whole Offer, and its projection
#                            is a keyed step, so the two associate when an
#                            Offer refines a keyed step of one of its labels


def _stepped_context(roles: list[int], base: dict[int, Local], sender: int, receiver: int,
                     label, sort: str) -> str | None:  # type: ignore[no-untyped-def]
    """Return the C++ typing context that the send reaches from the static projections, or None.

    The sender takes the branch of ``label`` and holds the message in its
    queue.  Every other role keeps its projection.
    """
    from execution import head
    from labelled import cpp_fixy_choice_local, cpp_label, cpp_payload
    from model import LChoice as Choice, cpp_role
    step = head(base[sender])
    if not isinstance(step, Choice) or not step.send:
        return None
    cont = next((k for lab, _, k in step.branches if lab == label), None)
    if cont is None:
        return None
    try:
        entries = []
        for r in roles:
            if r == sender:
                queue = (f"fs::OutQueue<fs::Queued<{cpp_role(receiver)}, {cpp_label(label)}, "
                         f"{cpp_payload(sort)}>>")
                local = cpp_fixy_choice_local(cont)
            else:
                queue, local = "fs::OutQueue<>", cpp_fixy_choice_local(base[r])
            entries.append(f"fs::RoleState<{cpp_role(r)}, {queue}, {local}>")
    except UntranslatableError:
        return None
    return f"fs::TypingContext<{', '.join(entries)}>"


def _enroute_probe(lg, roles: list[int], ctx: str | None) -> str:  # type: ignore[no-untyped-def]
    from labelled import cpp_fixy_global as labelled_global
    from model import cpp_role
    from probe import probe_header, show
    src = (probe_header("multi", MULTI_ALIAS) + _MULTI_HELPER + f"using G = {labelled_global(lg)};\n")
    src += show("ewf", "std::bool_constant<fg::is_global_well_formed_v<G>>")
    src += show("elive", "std::bool_constant<fs::is_live_by_construction_v<G>>")
    for r in roles:
        src += show(f"eproj{r}", f"typename session_oracle_proj<G, {cpp_role(r)}>::type")
    if ctx is not None:
        src += f"using C = {ctx};\n"
        src += show("eassoc", "std::bool_constant<fs::association_holds_v<C, G>>")
    return src


def evaluate_enroute(cases: list[Case], env: Env) -> dict[str, list[Row]]:
    """Measure the runtime global types that the first send of each multiparty case reaches.

    Two probe rounds: the static projection of each case, then the runtime
    type with the context that the send reaches.  O(cases × variants).
    """
    from labelled import enroute_variants, show as show_labelled
    from probe import SpellingError, read_fixy_projection, read_fixy_projection_with_queue, run_many
    base_cases = [c for c in cases if c.chan == "pair" and _translatable_fixy_global(c.g) and enroute_variants(c.g)]
    base_measured = run_many(env.cxx, env.include, [_multi_probe(c.g, projected_roles(c.g)) for c in base_cases],
                             env.workers, env.heads)
    work = []
    for c, m in zip(base_cases, base_measured, strict=True):
        roles = projected_roles(c.g)
        base: dict[int, Local] = {}
        base_live = _bool(m.values.get("flive")) if m.rejection is None else None
        base_wf = _bool(m.values.get("fwf")) if m.rejection is None else None
        if base_wf == "true":
            for r in roles:
                try:
                    local = read_fixy_projection(m.values.get(f"fproj{r}", ""), r)
                except SpellingError:
                    local = "unreadable"
                if not isinstance(local, str):
                    base[r] = local
        for tag, lg, sender, receiver, label, sort in enroute_variants(c.g):
            ctx = (_stepped_context(roles, base, sender, receiver, label, sort)
                   if len(base) == len(roles) else None)
            work.append((c, tag, lg, sender, receiver, label, sort, base_live, base_wf, ctx))
    measured = run_many(env.cxx, env.include, [_enroute_probe(w[2], projected_roles(w[0].g), w[9]) for w in work],
                        env.workers, env.heads)
    out: dict[str, list[Row]] = {}
    for (c, tag, lg, sender, receiver, label, sort, base_live, base_wf, ctx), m in zip(work, measured,
                                                                                    strict=True):
        rows = out.setdefault(c.ident, [])
        text = show_labelled(lg)
        roles = projected_roles(c.g)
        if m.rejection is not None:
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, "-", f"reject:{m.rejection}", "divergence",
                            "hard-error", "ours wrong: a relation stops the build with a hard error on a runtime "
                            "global type instead of answering; a predicate must fail closed with false"))
            continue
        ewf, live = _bool(m.values.get("ewf")), _bool(m.values.get("elive"))
        if ewf is None or live is None:
            raise RuntimeError(f"en-route case {c.ident} {tag}: no measurement: {m}")
        if ewf != "true" and base_wf != "true":
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, f"static well-formed {base_wf}", live, "agree",
                            "", "the static type is not well-formed, and neither is the runtime type that its "
                            "first send reaches"))
            continue
        if ewf != "true":
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, "static well-formed true", live, "divergence",
                            "enroute-ill-formed", "ours wrong: is_global_well_formed_v refuses a runtime type that "
                            "a well-formed static type reaches by one send"))
            continue
        system: dict[int, Local] = {}
        flight: list = []
        for r in roles:
            spelling = m.values.get(f"eproj{r}")
            if spelling is None:
                raise RuntimeError(f"en-route case {c.ident} {tag}: no projection onto role {r}")
            row = Row("fixy.enroute_projection", c.ident, f"{tag}/{r}", text, "-", spelling, "agree", "", "")
            try:
                local, queue = read_fixy_projection_with_queue(spelling, r)
            except SpellingError as exc:
                rows.append(dataclasses.replace(row, status="divergence", klass="unclassified",
                                                note=f"unclassified: {exc}"))
                continue
            expected = ((sender * 8 + receiver, sender, receiver, label, sort),) if r == sender else ()
            if not isinstance(local, str) and queue != expected:
                row = dataclasses.replace(row, oracle=repr(expected), status="divergence", klass="queue-mismatch",
                                          note="ours wrong: the queue of the projection is not the message that "
                                               "the global type holds en route from this role (Definition 4)")
            rows.append(row)
            if not isinstance(local, str):
                system[r] = local
                flight.extend(queue)
        if len(system) == len(roles):
            verdict = explore(system, flight=tuple(flight))
            run_text = verdict.text()
        else:
            verdict, run_text = None, "no run: fixy refuses a role of the runtime type"
        oracle = f"static live {base_live}; run {run_text}"
        narrowed = tag.startswith("s")
        if base_live == "true" and live != "true" and not narrowed:
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, oracle, live, "divergence",
                            "runtime-claim-lost",
                            "ours incomplete: the static type is live by construction, and the runtime type that "
                            "its first send reaches is not.  A transition keeps balanced+ (Pischke, Masters, "
                            "Yoshida, v4, Theorem 3 and Lemma 4), so the claim must hold on the runtime type"))
        elif live == "true" and verdict is not None and not verdict.is_safe:
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, oracle, live, "divergence",
                            "live-claim-violated",
                            "ours wrong: is_live_by_construction_v holds on the runtime type, and the run of "
                            "fixy's projection with the message in flight is not live"))
        else:
            rows.append(Row("fixy.enroute_live", c.ident, tag, text, oracle, live, "agree", "", ""))
        if ctx is None:
            continue
        assoc = _bool(m.values.get("eassoc"))
        if assoc is None:
            raise RuntimeError(f"en-route case {c.ident} {tag}: no association measurement: {m}")
        if base_live == "true" and assoc != "true" and narrowed:
            rows.append(Row("fixy.enroute_association", c.ident, tag, text, ctx, assoc, "divergence",
                            "offer-narrowing-refused",
                            "ours too strict: the receiver's context entry is the whole Offer, its projection of "
                            "the one-branch en-route type is a keyed step of the chosen label, and the other roles "
                            "keep a projection that refines their own.  An Offer refines an Offer of fewer labels "
                            "(Gay and Hole 2005, the external choice rule), and a keyed step is a choice of one "
                            "branch, so association holds.  The refusal is the keyed-step-shape defect"))
        elif base_live == "true" and assoc != "true":
            rows.append(Row("fixy.enroute_association", c.ident, tag, text, ctx, assoc, "divergence",
                            "association-not-preserved",
                            "ours too strict: the static type is live by construction, its projection "
                            "associates with it, and the context that the first send reaches does not associate "
                            "with the runtime type that the send reaches.  Association is preserved by a "
                            "transition (Pischke, Masters, Yoshida, v4, Definition 21 and Theorem 13)"))
        else:
            rows.append(Row("fixy.enroute_association", c.ident, tag, text, ctx, assoc, "agree", "",
                            "" if base_live == "true" else "the static type is not live by construction, so the "
                            "theorem makes no claim"))
    return out


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
    return any(r.family == t.family and r.klass == t.klass and r.status == "divergence" for r in rows)


class Evaluator:
    """Evaluate candidate types once each, for the multiparty and the two-party kind."""

    def __init__(self, env: Env) -> None:
        self.env = env
        self.memo: dict[tuple[str, Global], list[Row]] = {}

    def run(self, kind: str, gs: list[Global]) -> None:
        """Evaluate every type of ``gs`` that is not in the memo."""
        fresh = [g for g in dict.fromkeys(gs) if (kind, g) not in self.memo]
        if not fresh:
            return
        prefix = "m" if kind == "multi" else "fm"
        cases = [Case(f"{prefix}{i}", g) for i, g in enumerate(fresh)]
        rows = (evaluate_multi if kind == "multi" else evaluate_fixy)(cases, self.env)
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
        for kind in ("multi", "fixy"):
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
    return "fixy" if case.startswith("f") else "multi"


def minimal_corpus(rows: list[Row], cases: dict[tuple[str, str], Case], ev: Evaluator,
                   shrinkable: frozenset[str]) -> list[Row]:
    """Shrink every divergence of the generated and hand corpora.  Return the minimal rows.

    Each minimal type becomes one case of corpus m (multiparty) or fm
    (two-party).  Its divergence rows name the cases that shrank to it.
    ``shrinkable`` holds the families that the Evaluator measures.  The
    other families are not shrunk: a subtyping pair is one change of U
    already, and the crash, en-route and record families measure variants
    of a type that a shrink of the type does not keep.
    """
    groups: dict[tuple[str, str, str, str], list[Target]] = {}
    for row in rows:
        corpus = row.case.rstrip("0123456789")
        if row.status != "divergence" or corpus not in ("r", "a", "h", "fr", "fa"):
            continue
        if row.family not in shrinkable:
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
               for t, g in zip(targets, shrunk, strict=True) if t.kind == "multi"]
    ev.run("multi", [g2 for _, g2, _ in relabel])
    relabelled = {id(t): (g2, role2) for t, g2, role2 in relabel}
    for t, g in zip(targets, shrunk, strict=True):
        if id(t) in relabelled:
            g2, role2 = relabelled[id(t)]
            moved = dataclasses.replace(t, role=t.role if role2 is None else str(role2))
            if _keeps(ev.rows("multi", g2), moved):
                finals.append((moved, g2))
                continue
        finals.append((t, g))
    groups: dict[tuple[str, Global], list[Target]] = {}
    for t, g in finals:
        groups.setdefault((t.kind, g), []).append(t)
    # A target that never moved keeps its original type, which only the
    # full pass evaluated, so every final type is evaluated here.
    for kind in ("multi", "fixy"):
        ev.run(kind, [g for k, g in groups if k == kind])
    out: list[Row] = []
    for kind in ("multi", "fixy"):
        keys = sorted((k for k in groups if k[0] == kind),
                      key=lambda k: (size(k[1]), show_global(k[1])))
        for n, key in enumerate(keys):
            ident = f"{'m' if kind == 'multi' else 'fm'}{n}"
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
    """Return the multiparty cases and the two-party cases of every corpus but the minimal ones."""
    multi = [Case(f"r{i}", g) for i, g in enumerate(
        generate(MULTI_SEED, MULTI_COUNT, roles=MULTI_ROLES, max_depth=MULTI_DEPTH))]
    multi += [Case(f"a{i}", g) for i, g in enumerate(
        generate_adversarial(MULTI_ADV_SEED, MULTI_ADV_COUNT, MULTI_ROLES, MULTI_ADV_DEPTH))]
    multi += [Case(f"o{i}", g) for i, g in enumerate(generate_outer(OUTER_SEED, OUTER_COUNT, MULTI_ROLES))]
    multi += list(HAND_CASES) + list(PAPER_MULTI)
    fixy = [Case(f"fr{i}", g) for i, g in enumerate(
        generate(FIXY_SEED, FIXY_COUNT, roles=2, max_depth=FIXY_DEPTH))]
    fixy += [Case(f"fa{i}", g) for i, g in enumerate(
        generate_adversarial(FIXY_ADV_SEED, FIXY_ADV_COUNT, 2, FIXY_ADV_DEPTH))]
    fixy += list(FIXY_HAND) + list(PAPER_FIXY)
    return multi, fixy


@contextlib.contextmanager
def measured_tree(at: str | None) -> Iterator[tuple[Path, str]]:
    """Yield the include tree to measure and the text that names it in the golden file.

    With ``at``, the tree is the include directory of that commit, taken
    with git archive into a temporary directory that the context removes.
    The name is the full commit, so a run is pinned whatever HEAD does
    during it.  Without ``at``, the tree is the working tree, named by the
    HEAD at the start and by whether the session headers hold edits.
    """
    def git(*args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True)

    if at is None:
        head = git("rev-parse", "HEAD").stdout.strip() or "unknown"
        dirty = git("diff", "--quiet", "HEAD", "--", "include/fixy/session",
                    "include/foundation/algebra/Transition.h").returncode != 0
        yield REPO / "include", f"{head}{', the working tree with edits in the session headers' if dirty else ''}"
        return
    resolved = git("rev-parse", "--verify", f"{at}^{{commit}}")
    if resolved.returncode != 0:
        raise SystemExit(f"session_oracle: {at!r} names no commit: {resolved.stderr.strip()}")
    commit = resolved.stdout.strip()
    with tempfile.TemporaryDirectory(prefix="session_oracle_tree_") as tmp:
        archive = subprocess.run(["git", "-C", str(REPO), "archive", commit, "include"],
                                 capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(tmp, filter="data")
        yield Path(tmp) / "include", commit


def regenerate(cxx: str, workers: int, do_shrink: bool = True, at: str | None = None) -> None:
    """Run the full pipeline against the tree of ``at`` and write the golden file and the tests."""
    with measured_tree(at) as (include, measured):
        _regenerate(cxx, workers, do_shrink, include, measured)


def _regenerate(cxx: str, workers: int, do_shrink: bool, include: Path, measured: str) -> None:
    import rocq
    from probe import pch_heads

    multi, fixy = corpora()
    with pch_heads(cxx, include) as heads:
        env = Env(cxx, heads, workers, include)
        LOG.info("evaluating %d multiparty cases and %d two-party cases against %s", len(multi),
                 len(fixy), include)
        rows = [r for rs in evaluate_multi(multi, env).values() for r in rs]
        rows += [r for rs in evaluate_fixy(fixy, env).values() for r in rs]
        shrinkable = frozenset(r.family for r in rows)
        small = [c for c in fixy if size(c.g) <= SUBTYPE_MAX_SIZE]
        subtype_cases = ([c for c in small if c.ident.startswith("fr")][:SUBTYPE_CASES_PER_CORPUS]
                         + [c for c in small if c.ident.startswith("fa")][:SUBTYPE_CASES_PER_CORPUS]
                         + list(FIXY_HAND) + list(PAPER_FIXY))
        subtype_rows = [r for rs in evaluate_subtype(subtype_cases, env).values() for r in rs]
        subtype_rows += [r for rs in evaluate_keyed_subtype(subtype_cases, env).values() for r in rs]
        rows += subtype_rows
        rows += [r for rs in evaluate_ekici(subtype_cases, subtype_rows).values() for r in rs]
        rows += [r for rs in evaluate_wire(subtype_cases, subtype_rows, env).values() for r in rs]
        rows += [r for rs in evaluate_sr_safety(multi).values() for r in rs]
        rows += [r for rs in evaluate_crash(multi, env).values() for r in rs]
        rows += [r for rs in evaluate_enroute(multi, env).values() for r in rs]
        minimal: list[Row] = []
        if do_shrink:
            cases = {("multi", c.ident): c for c in multi} | {("fixy", c.ident): c for c in fixy}
            minimal = minimal_corpus(rows, cases, Evaluator(env), shrinkable)
        rows += minimal
        rows += evaluate_semantics(rows, env)
        rows += evaluate_network(rows, env)
    rows += _derived_rows(rows)
    import ekici
    import mpstk
    import sr
    meta = [
        "# Differential test of our session relations against published mechanisations.",
        f"# oracle: github.com/{rocq.PROJECTION.repo} at {rocq.PROJECTION.commit}",
        f"# oracle: github.com/{sr.SUBJECT_REDUCTION.repo} at {sr.SUBJECT_REDUCTION.commit}",
        f"# oracle: github.com/{ekici.REPO} at {ekici.COMMIT}",
        f"# oracle: github.com/{mpstk.REPO} at {mpstk.COMMIT}",
        *_derived_meta(),
        f"# toolchain: {rocq.coq_version()}; for {ekici.REPO}: {ekici.coq_version()}",
        f"# relations measured at commit {measured}",
        f"{NETWORK_MARKER}{measured}",
        f"# fixy corpora: fr seed {FIXY_SEED}, {FIXY_COUNT} types, depth {FIXY_DEPTH}; "
        f"fa seed {FIXY_ADV_SEED}, {FIXY_ADV_COUNT} types, depth {FIXY_ADV_DEPTH}",
        f"# multiparty corpora: r seed {MULTI_SEED}, {MULTI_COUNT} types over {MULTI_ROLES} roles, "
        f"depth {MULTI_DEPTH}; a seed {MULTI_ADV_SEED}, {MULTI_ADV_COUNT} types, depth {MULTI_ADV_DEPTH}; "
        f"o seed {OUTER_SEED}, {OUTER_COUNT} types",
        f"# minimal cases: {len({r.case for r in minimal})}",
    ]
    meta += [f"# {c.ident}: {c.cite}" for c in (*HAND_CASES, *PAPER_MULTI, *FIXY_HAND, *PAPER_FIXY)]
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


DERIVED_FAMILIES = ("keskin.live", "sprout.implementable")
# The metadata line that names the tree whose implementable_on the
# fixy.network rows measure.  --derive with a compiler measures them again
# at HEAD, so a change to fixy/session/Network.h needs no other oracle.
NETWORK_MARKER = "# network verdicts measured at commit "


def _derived_meta() -> list[str]:
    """Return the metadata lines of the derived families: the pinned oracles and their toolchains."""
    import keskin
    import sprout
    return [f"# oracle: github.com/{keskin.REPO} at {keskin.COMMIT}",
            f"# oracle: doi {sprout.ARTIFACT}, built natively: {sprout.build()[1]}",
            f"# toolchain for {keskin.REPO}: {keskin.coq_version()}"]


def _derived_rows(base: list[Row]) -> list[Row]:
    """Return the rows that read only the multiparty rows: liveness and implementability."""
    return evaluate_keskin(base) + evaluate_sprout(base)


def derive(cxx: str | None, workers: int) -> int:
    """Recompute the rows that the golden file derives from its own rows, and write it again.

    The liveness family (keskin.live) and the implementability family
    (sprout.implementable) read only the multiparty rows, so a change to
    their oracles needs no compiler and no other oracle.  With ``cxx``, the
    families of Semantics.h are measured again too, against the include tree
    of the commit that the golden file names, and the network family
    (fixy.network) against the include tree of HEAD.
    """
    import keskin
    import semantics
    import sprout
    meta, rows = read_golden(GOLDEN)
    base = [r for r in rows if r.family not in DERIVED_FAMILIES]
    if cxx is not None:
        from probe import pch_heads
        marker = "# relations measured at commit "
        commit = next(line[len(marker):].split(",")[0] for line in meta if line.startswith(marker))
        base = [r for r in base if r.family not in semantics.FAMILIES]
        with measured_tree(commit) as (include, _), pch_heads(cxx, include) as heads:
            base += evaluate_semantics(base, Env(cxx, heads, workers, include))
        base = [r for r in base if r.family != "fixy.network"]
        with measured_tree("HEAD") as (include, network_commit), pch_heads(cxx, include) as heads:
            base += evaluate_network(base, Env(cxx, heads, workers, include))
        meta = [line for line in meta if not line.startswith(NETWORK_MARKER)]
        relations = next(i for i, line in enumerate(meta) if line.startswith(marker))
        meta.insert(relations + 1, f"{NETWORK_MARKER}{network_commit}")
    cited = (*HAND_CASES, *PAPER_MULTI, *FIXY_HAND, *PAPER_FIXY)
    ours = (f"# oracle: github.com/{keskin.REPO} ", f"# oracle: doi {sprout.ARTIFACT}",
            f"# toolchain for {keskin.REPO}:", *(f"# {c.ident}: " for c in cited))
    kept = [line for line in meta if not line.startswith(ours)]
    last_oracle = max(i for i, line in enumerate(kept) if line.startswith("# oracle: "))
    meta = (kept[:last_oracle + 1] + _derived_meta() + kept[last_oracle + 1:]
            + [f"# {c.ident}: {c.cite}" for c in cited])
    write_golden(GOLDEN, meta, base + _derived_rows(base))
    _write_tests(read_golden(GOLDEN)[1])
    return check()


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
    import semantics
    try:
        for note in semantics.check_ledger(rows):
            print(f"session_oracle check: {note}", file=sys.stderr)
    except RuntimeError as exc:
        print(f"session_oracle check: {exc}", file=sys.stderr)
        return 1
    div = sum(r.status == "divergence" for r in rows)
    gap = sum(r.status == "gap" for r in rows)
    print(f"session_oracle check: {len(rows)} rows, {div} recorded divergences, {gap} gaps, "
          "tests in sync")
    return 0


def _compile(cxx: str, source: Path, include: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [cxx, "-std=c++26", "-freflection", "-fcontracts", "-fconstexpr-ops-limit=100000000",
         f"-I{include}", "-fsyntax-only", "-fdiagnostics-color=never", str(source)],
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
    plant("generated_fixy_keyed_subtype.cpp",
          lambda r: r.family == "fixy.keyed_subtype_sync" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_keyed_projection.cpp",
          lambda r: r.family == "fixy.keyed_live" and r.status == "agree" and r.ours in ("true", "false"),
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_crash.cpp",
          lambda r: r.family == "fixy.crash_live" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_enroute.cpp",
          lambda r: r.family == "fixy.enroute_live" and r.status == "agree",
          lambda r: "false" if r.ours == "true" else "true")
    plant("generated_fixy_wire.cpp",
          lambda r: r.family == "fixy.wire" and r.status == "agree",
          lambda r: "abort")
    missing = set(emit_all(rows)) - set(labels)
    if missing:
        raise RuntimeError(f"self-test: no planted row for {sorted(missing)}")
    return planted, labels


def self_test(cxx: str, at: str | None = None) -> int:
    """Prove the emitted tests pass and a planted disagreement fails.

    The clean tests compile in parallel against the tree of ``at``, the
    working tree by default (measured_tree).  Each planted row is compiled
    in a test that holds only the rows of its own case, so the check costs
    one small compile for each emitted file.
    """
    with measured_tree(at) as (include, _):
        return _self_test(cxx, include)


# The emitted tests whose assertions run: the test is a program that
# fails when a row differs.  Every other emitted test fails to compile.
RUNTIME_TESTS = frozenset({"generated_fixy_wire.cpp"})


def _self_test(cxx: str, include: Path) -> int:
    from concurrent.futures import ThreadPoolExecutor
    from emit import emit_self_check
    _, rows = read_golden(GOLDEN)
    failures: list[str] = emit_self_check()

    def verify(name: str, path: Path) -> subprocess.CompletedProcess[str]:
        if name in RUNTIME_TESTS:
            return build_and_run(cxx, include, path.read_text(encoding="utf-8"), [])
        return _compile(cxx, path, include)

    with tempfile.TemporaryDirectory(prefix="session_oracle_selftest_") as tmp:
        root = Path(tmp)
        clean = emit_all(rows)
        for name, text in clean.items():
            (root / name).write_text(text, encoding="utf-8")
        with ThreadPoolExecutor(max_workers=len(clean)) as pool:
            results = list(pool.map(lambda n: (n, verify(n, root / n)), clean))
        for name, proc in results:
            if proc.returncode != 0:
                failures.append(f"clean {name} fails:\n{(proc.stdout + proc.stderr)[-2000:]}")
        planted, labels = _plant(rows)
        for name, label in labels.items():
            case = label.split(" case ", 1)[1].split()[0].rstrip(":")
            subset = [r for r in planted if r.case == case]
            path = root / f"planted_{name}"
            path.write_text(emit_all(subset)[name], encoding="utf-8")
            proc = verify(name, path)
            if proc.returncode == 0:
                failures.append(f"planted {name} passes; the planted row is not caught")
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
    print("session_oracle self-test: the clean tests pass, each planted row is caught, "
          "and the drift check sees a one-line change")
    return 0


def main(argv: list[str]) -> int:
    """Parse the command line and run one mode."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("mode", choices=("regenerate", "derive", "emit", "check", "self-test", "install-toolchain"))
    parser.add_argument("--cxx", help="the project compiler (regenerate, self-test)")
    parser.add_argument("--jobs", type=int, default=32, help="parallel probe compiles")
    parser.add_argument("--no-shrink", action="store_true",
                        help="regenerate without the minimal corpus, for a quick look")
    parser.add_argument("--at", metavar="REV",
                        help="measure (regenerate) or compile the tests against (self-test) the include "
                             "tree of this commit; the default is the working tree")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")
    if args.mode in ("regenerate", "self-test") and not args.cxx:
        parser.error(f"{args.mode} needs --cxx")
    if args.mode == "regenerate":
        regenerate(args.cxx, args.jobs, do_shrink=not args.no_shrink, at=args.at)
        return check()
    if args.mode == "derive":
        return derive(args.cxx, args.jobs)
    if args.mode == "emit":
        _write_tests(read_golden(GOLDEN)[1])
        return check()
    if args.mode == "check":
        return check()
    if args.mode == "install-toolchain":
        import toolchain
        LOG.info("the crash-stop toolchain is in %s", toolchain.install())
        return 0
    return self_test(args.cxx, args.at)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
