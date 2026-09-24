"""The subject-reduction development of Tirore, Bengtson and Carbone (ECOOP 2025).

Branch ECOOP2025 of github.com/Tirore96/subject_reduction, MIT licence.
Its global and local types carry labels: a choice is a list of pairs of a
label and a continuation, so a branch is found by its label and the labels
need not be 0, 1, ...  Two of its decision procedures are computable:

  proj g p      theories/Projection/indProj.v: the projection of g onto p
                with the plain merge, or None
  linear g      theories/linearityDecide.v: the linearity of g, which the
                subject-reduction theorem of the paper assumes (Linear_decidable
                states that it decides Linear for a closed contractive g)

The encoding of a local type extends the one of rocq.py with the label of
each branch:

    rejected projection      [0]
    accepted projection      [1] ++ enc e
    enc (EVar n)             [0; n]
    enc EEnd                 [1]
    enc (EMsg d c u e)       [2; dir d; c; sort u] ++ enc e
    enc (EBranch d c es)     [3; dir d; c; size es] ++ (l_1 :: enc e_1) ++ ...
    enc (ERec e)             [4] ++ enc e
    dir Sd = 0, dir Rd = 1;  sort SGType GEnd = 0 (our nat), SBool = 1

A linearity answer is [1] for linear and [0] for not linear.
"""

from __future__ import annotations

from pathlib import Path

import rocq
from labelled import LGlobal, decode_sr_local, sr_coq_global
from model import Local


def _sr_load_path(root: Path) -> list[str]:
    return ["-R", str(root / "theories"), "MPSTSR"]


def _build_sr(dev: rocq.Development, root: Path) -> None:
    rocq._build_sources(dev, root, "MPSTSR", False, "theories/linearityDecide.vo",
                        ("theories/Projection/indProj.v", "theories/linearityDecide.v"))


_SR_HEAD = """\
From mathcomp Require Import all_ssreflect.
From MPSTSR Require Import IndTypes.syntax Projection.indProj linearityDecide.

Definition sr_dir (d : dir) : nat := if d is Sd then 0 else 1.

Definition sr_sort (u : value) : nat :=
  match u with
  | VSort (SGType GEnd) => 0
  | VSort SBool => 1
  | _ => 9
  end.

Fixpoint sr_enc (e : lType) : seq nat :=
  match e with
  | EVar n => [:: 0; n]
  | EEnd => [:: 1]
  | EMsg d c u e0 => [:: 2; sr_dir d; nat_of_ch c; sr_sort u] ++ sr_enc e0
  | EBranch d c es => [:: 3; sr_dir d; nat_of_ch c; size es]
                        ++ flatten (map (fun p => p.1 :: sr_enc p.2) es)
  | ERec e0 => 4 :: sr_enc e0
  end.

Definition sr_opt (o : option lType) : seq nat :=
  if o is Some e then 1 :: sr_enc e else [:: 0].

"""

SUBJECT_REDUCTION = rocq.Development(
    name="subject_reduction",
    repo="Tirore96/subject_reduction",
    commit="6d5362df12b049b79b3a28b4519c28f7d0ee4831",
    load_path=_sr_load_path,
    build=_build_sr,
    driver_head=_SR_HEAD,
)


def project(queries: list[tuple[LGlobal, int, str]]) -> list[Local | None | rocq.Timeout]:
    """Project each (global type, role, channel specification) with the development's proj.

    Returns the decoded local type, None when proj rejects, or TIMEOUT.
    The global types must have a form in the development (sr_coq_global).
    """
    terms = [sr_coq_global(g, chan) for g, _, chan in queries]
    exprs = [f"sr_opt (proj {t} (Ptcp {role}))" for t, (_, role, _) in zip(terms, queries, strict=True)]
    keys = [f"proj|{t}|{role}" for t, (_, role, _) in zip(terms, queries, strict=True)]
    return [codes if codes is rocq.TIMEOUT else decode_sr_local(codes)  # type: ignore[arg-type]
            for codes in rocq.evaluate(SUBJECT_REDUCTION, exprs, keys)]


def linear(queries: list[tuple[LGlobal, str]]) -> list[bool | rocq.Timeout]:
    """Decide the linearity of each (global type, channel specification)."""
    terms = [sr_coq_global(g, chan) for g, chan in queries]
    exprs = [f"[:: nat_of_bool (linear {t})]" for t in terms]
    keys = [f"linear|{t}" for t in terms]
    out: list[bool | rocq.Timeout] = []
    for codes in rocq.evaluate(SUBJECT_REDUCTION, exprs, keys):
        if codes is rocq.TIMEOUT:
            out.append(rocq.TIMEOUT)
        elif codes in ([0], [1]):
            out.append(codes == [1])
        else:
            raise rocq.OracleError(f"linear: unexpected answer {codes}")
    return out
