"""The golden file and the C++ tests that are emitted from it.

The golden file is a CSV table with one row per measured property.
Lines that start with ``#`` are metadata.  The columns:

    family   the relation and property, for example ``old.projection``
    case     the case identifier: a corpus letter and a number, or
             ``p_`` and the name of a paper example (see CORPORA)
    role     the role of a projection, or ``-``
    global   the global type in the compact form of model.py
    oracle   the oracle's answer: a local type, ``none``, ``true``,
             ``false`` or an execution verdict; for the fixy families,
             ``e0 || e1``
    ours     what our relation computed: ``=`` when it equals the
             oracle's answer, a canonical C++ spelling, ``true``,
             ``false``, ``reject:<tag>`` or an execution verdict
    status   ``agree``, ``divergence``, or ``gap`` for a type that our
             protocol language cannot spell
    class    for a divergence or a gap, the kind of disagreement; the
             shrinker keeps it while it shrinks a case
    note     for a divergence, which side is wrong and the definition
             that decides it

The tests assert what our relation computed.  For an agree row that is
the oracle's answer, so a change to our relation that breaks agreement
fails the build.  For a divergence row the assertion pins our current
answer, so a repair also fails the build, and the golden file must be
regenerated.  The divergence list can therefore only shrink by an
explicit step.  A row whose relation rejects with a hard error cannot
be compiled in the shared test, so it stays in the golden file only.
So do the gap rows and the rows of RECORD_FAMILIES.

Emission reads the golden file and nothing else, so the drift check in
CI needs no oracle and no compiler.
"""

from __future__ import annotations

import csv
import io
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Union

import labelled
from model import (Local, cpp_fixy_global, cpp_fixy_local, cpp_fixy_peer_local, cpp_old_global,
                   cpp_old_local, cpp_prelude, cpp_role, dual_local, local_of, mutations, read_global,
                   read_local, show_global)

COLUMNS = ("family", "case", "role", "global", "oracle", "ours", "status", "class", "note")

# The corpora, in the order of the golden file.  For the frozen tree: r
# random, a adversarial, o types whose inner loop jumps to an outer
# binder, h hand-written review cases, m minimal forms of the divergences
# that the shrinker found.  p holds the paper examples of the two trees.
# fr, fa, fh and fm are the random, adversarial, hand-written and minimal
# corpora of the fixy tree.
CORPORA = ("r", "a", "o", "h", "p", "m", "fr", "fa", "fh", "fm")

FIXY_FAMILIES = ("fixy.dual", "fixy.is_dual", "fixy.involution", "fixy.involutive_flag",
                 "fixy.well_formed", "fixy.accepts")
MULTI_FAMILIES = ("fixy.global_wf", "fixy.projection", "fixy.live")
SUBTYPE_FAMILIES = ("fixy.subtype_sync", "fixy.subtype_async")
# The capacity that the asynchronous subtyping rows use, in the probe, in
# the run that decides them, and in the emitted assertion.  The relation
# reads its capacity from a channel type, never from a number, so each
# source that queries it declares SUBTYPE_CHANNEL.
SUBTYPE_CAPACITY = 3
SUBTYPE_CHANNEL = "oracle_channel"
SUBTYPE_MUTATIONS = 4


def subtype_channel_decl() -> str:
    """Return the channel type whose capacity the asynchronous queries read."""
    return (f"// The end of a channel that holds {SUBTYPE_CAPACITY} messages in each direction.\n"
            f"struct {SUBTYPE_CHANNEL} {{\n"
            f"    static constexpr unsigned channel_capacity = {SUBTYPE_CAPACITY};\n"
            f"}};\n")


OLD_FAMILIES = ("old.projection", "old.well_formed")
# Keyed choices: pairs of keyed binary protocols, and multiparty global
# types whose choices carry labels in an order that depends on the path.
# The golden file stores the global type of a keyed multiparty row in the
# labelled compact form of labelled.py.
KEYED_SUBTYPE_FAMILIES = ("fixy.keyed_subtype_sync", "fixy.keyed_subtype_async")
KEYED_MULTI_FAMILIES = ("fixy.keyed_projection", "fixy.keyed_live")
# Crash-stop variants of the multiparty cases.  The role of a row is
# "<kind>.u<unreliable roles>", and "/<role>" for a projection.
CRASH_FAMILIES = ("fixy.crash_projection", "fixy.crash_live")
# Runtime global types that the first send of a multiparty case reaches.
# The role of a row is the variant tag ("e", "e<k>"), and "/<role>" for a
# projection.  The oracle column of an association row holds the C++
# context that the send reaches.
ENROUTE_FAMILIES = ("fixy.enroute_projection", "fixy.enroute_live", "fixy.enroute_association")
# A keyed pair (T, U) whose synchronous run is safe, run end to end: the
# handle of T against the handle of the dual of U, over one queue of
# words (test/session_oracle/wire_driver.h).  The role of a row is
# "<pair role>/<spelling>/s<seed>", and the ours column is the outcome.
WIRE_FAMILIES = ("fixy.wire",)
WIRE_SEEDS = (0, 1)
# Families that no C++ test can assert: the execution verdict of the
# frozen tree's projection, the run of the oracle's own projection, the
# run of the subject-reduction development's projection, mpstk's model
# check of fixy's crash-stop context, the coqc-checked verdict of the
# ITP 2025 subtyping relation on each synchronous subtyping pair, and the
# coqc-checked liveness of fixy's projected context by the ITP 2026
# liveness theorem.
RECORD_FAMILIES = ("old.execution", "oracle.safety", "sr.safety", "mpstk.crash", "ekici.subtype",
                   "keskin.live")
FAMILIES = (FIXY_FAMILIES + MULTI_FAMILIES + SUBTYPE_FAMILIES + KEYED_SUBTYPE_FAMILIES + KEYED_MULTI_FAMILIES
            + CRASH_FAMILIES + ENROUTE_FAMILIES + WIRE_FAMILIES + OLD_FAMILIES + RECORD_FAMILIES)
# The roles that a multiparty case is projected onto: every role it names,
# and at least three.
MIN_ROLES = 3


def multiparty_roles(named: set[int]) -> list[int]:
    """Return the roles that a multiparty case is projected onto."""
    return list(range(max(MIN_ROLES, max(named, default=-1) + 1)))


STATUSES = ("agree", "divergence", "gap")

GENERATED_NOTICE = (
    "// Generated by tools/session_oracle from test/session_oracle/golden.csv.\n"
    "// Do not edit.  Regenerate with scripts/session-oracle.sh --regenerate, or\n"
    "// re-emit after a note change with scripts/session-oracle.sh --emit.\n")


class GoldenError(ValueError):
    """The golden file is malformed."""


@dataclass(frozen=True, slots=True)
class Row:
    """One measured property."""

    family: str
    case: str
    role: str
    global_text: str
    oracle: str
    ours: str
    status: str
    klass: str
    note: str

    def as_list(self) -> list[str]:
        """Return the row as CSV fields in column order."""
        return [self.family, self.case, self.role, self.global_text, self.oracle,
                self.ours, self.status, self.klass, self.note]


def case_key(case: str) -> tuple[int, int, str]:
    """Return the sort key of a case identifier: corpus, number, name."""
    if case.startswith("p_") and case[2:].replace("_", "").isalnum():
        return (CORPORA.index("p"), 0, case)
    corpus = case.rstrip("0123456789")
    digits = case[len(corpus):]
    if corpus not in CORPORA or corpus == "p" or not digits:
        raise GoldenError(f"malformed case identifier {case!r}")
    return (CORPORA.index(corpus), int(digits), "")


def read_golden(path: Path) -> tuple[list[str], list[Row]]:
    """Read and validate the golden file.  Return metadata and rows.

    Every row must name a known family and status, a divergence must
    carry a note, and the global type must parse.  O(rows).
    """
    meta: list[str] = []
    body: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        (meta if line.startswith("#") else body).append(line)
    reader = csv.reader(body)
    header = next(reader, None)
    if header is None or tuple(header) != COLUMNS:
        raise GoldenError(f"{path}: the header must be {','.join(COLUMNS)}")
    rows: list[Row] = []
    for lineno, fields in enumerate(reader, start=len(meta) + 2):
        if len(fields) != len(COLUMNS):
            raise GoldenError(f"{path}:{lineno}: {len(fields)} fields, expected {len(COLUMNS)}")
        row = Row(*fields)
        case_key(row.case)
        if row.family not in FAMILIES:
            raise GoldenError(f"{path}:{lineno}: unknown family {row.family!r}")
        if row.status not in STATUSES:
            raise GoldenError(f"{path}:{lineno}: unknown status {row.status!r}")
        if row.status != "agree" and not (row.note and row.klass):
            raise GoldenError(f"{path}:{lineno}: a {row.status} must carry a class and a note")
        if row.status == "agree" and row.ours.startswith("reject:") and row.oracle != "none":
            raise GoldenError(f"{path}:{lineno}: a rejection agrees only with oracle 'none'")
        if labelled.is_labelled_text(row.global_text):
            if labelled.show(labelled.read(row.global_text)) != row.global_text:
                raise GoldenError(f"{path}:{lineno}: the labelled global type is not in canonical form")
        else:
            show_global(read_global(row.global_text))
        rows.append(row)
    return meta, rows


def write_golden(path: Path, meta: list[str], rows: list[Row]) -> None:
    """Write the golden file in a deterministic order."""
    ordered = sorted(rows, key=lambda r: (FAMILIES.index(r.family), case_key(r.case), r.role))
    buf = io.StringIO()
    for line in meta:
        buf.write(line + "\n")
    writer = csv.writer(buf, lineterminator="\n")
    writer.writerow(COLUMNS)
    for row in ordered:
        writer.writerow(row.as_list())
    path.write_text(buf.getvalue(), encoding="utf-8")


def _pin(spelling: str) -> str:
    """Qualify a canonical spelling from the global namespace."""
    return "::" + spelling


def _message(row: Row) -> str:
    role = f" role {row.role}" if row.role != "-" else ""
    return f"session_oracle {row.family} case {row.case}{role}: {row.status}"


def _fixy_pair(row: Row) -> tuple[str, str]:
    left, _, right = row.oracle.partition(" || ")
    e0, e1 = read_local(left), read_local(right)
    if e0 is None or e1 is None:
        raise GoldenError(f"fixy case {row.case}: the oracle rejected a two-party projection")
    return cpp_fixy_local(e0), cpp_fixy_local(e1)


_FIXY_BOOL = {
    "fixy.is_dual": "fs::is_dual_v<T0, T1>",
    "fixy.involution": "std::is_same_v<fs::dual_of_t<fs::dual_of_t<T0>>, T0>",
    "fixy.involutive_flag": "std::is_same_v<fs::dual_of_t<fs::dual_of_t<T0>>, T0>",
    "fixy.well_formed": "(fs::is_well_formed_v<T0> && fs::is_well_formed_v<T1>)",
    "fixy.accepts": ("(fs::is_well_formed_v<N0> && fs::is_well_formed_v<N1> && "
                     "fs::is_dual_v<N0, N1>)"),
}


def fixy_accepts_expression() -> str:
    """Return the C++ expression that fixy.accepts measures on N0 and N1."""
    return _FIXY_BOOL["fixy.accepts"]


def _hard_error(row: Row) -> bool:
    """Return true for a row whose relation stopped the build, which no test can assert."""
    return row.ours.startswith("reject:") and row.family not in ("old.projection",)


def _fixy_assert(row: Row) -> str:
    msg = _message(row)
    if _hard_error(row):
        return f"// {row.family}: the relation stops the build ({row.ours[7:]})\n"
    if row.family == "fixy.dual":
        target = "T1" if row.ours == "=" else _pin(row.ours)
        return f"static_assert(std::is_same_v<fs::dual_of_t<T0>, {target}>, \"{msg}\");\n"
    if row.ours not in ("true", "false"):
        raise GoldenError(f"{row.family} case {row.case}: ours must be true or false")
    return f"static_assert({_FIXY_BOOL[row.family]} == {row.ours}, \"{msg}\");\n"


def _namespace(case: str) -> str:
    return "c_" + case


def emit_fixy(rows: list[Row]) -> str:
    """Return the fixy test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in FIXY_FAMILIES and row.status != "gap":
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Duality against the projection oracle.  For a two-party global type the\n",
           "// oracle's projection onto role 1 is the dual of its projection onto role 0,\n",
           "// so dual_of_t must map T0 onto T1.  N0 and N1 are the naive reading of the\n",
           "// global type, which fixy.accepts compares with the oracle.\n\n",
           "#include <fixy/session/Protocol.h>\n\n#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\n\n",
           "namespace session_oracle::fixy_duality {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: FIXY_FAMILIES.index(r.family))
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        pair_rows = [r for r in group if r.family != "fixy.accepts"]
        if pair_rows:
            t0, t1 = _fixy_pair(pair_rows[0])
            out.append(f"using T0 = {t0};\nusing T1 = {t1};\n")
        if any(r.family == "fixy.accepts" for r in group):
            g = read_global(group[0].global_text)
            out.append(f"using N0 = {cpp_fixy_local(local_of(g, 0))};\n"
                       f"using N1 = {cpp_fixy_local(local_of(g, 1))};\n")
        for row in group:
            out.append(_fixy_assert(row))
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::fixy_duality\n\nint main() { return 0; }\n")
    return "".join(out)


def emit_old(rows: list[Row]) -> str:
    """Return the frozen-tree test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in OLD_FAMILIES and row.status != "gap":
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Projection and well-formedness of the frozen tree against the projection\n",
           "// oracle.  A row whose projection rejects with a hard error is not asserted;\n",
           "// the golden file records it.\n\n",
           "#include <crucible/sessions/SessionGlobal.h>\n\n#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace pr = ::crucible::safety::proto;\n\n",
           "namespace session_oracle::old_projection {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (OLD_FAMILIES.index(r.family), r.role))
        g = read_global(group[0].global_text)
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        out.append(f"using G = {cpp_old_global(g)};\n")
        for row in group:
            msg = _message(row)
            if row.family == "old.well_formed":
                out.append(f"static_assert(pr::is_global_well_formed_v<G> == {row.ours}, \"{msg}\");\n")
                continue
            if row.ours.startswith("reject:"):
                out.append(f"// role {row.role}: our projection rejects ({row.ours[7:]}); "
                           f"oracle {row.oracle}\n")
                continue
            if row.ours == "=":
                expected = read_local(row.oracle)
                if expected is None:
                    raise GoldenError(f"old.projection case {row.case}: '=' needs an oracle type")
                target = cpp_old_local(expected)
            else:
                target = _pin(row.ours)
            out.append(f"static_assert(std::is_same_v<pr::project_t<G, {cpp_role(int(row.role))}>, "
                       f"{target}>, \"{msg}\");\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::old_projection\n\nint main() { return 0; }\n")
    return "".join(out)


def emit_multi(rows: list[Row]) -> str:
    """Return the fixy multiparty test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in MULTI_FAMILIES and row.status != "gap":
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Global types, projection and liveness of fixy/session against the\n",
           "// projection oracle.  A projection row asserts the oracle's answer when\n",
           "// fixy gives it as written, and pins fixy's answer otherwise.\n\n",
           "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n",
           "#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
           "namespace session_oracle::fixy_projection {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (MULTI_FAMILIES.index(r.family), r.role))
        g = read_global(group[0].global_text)
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        out.append(f"using G = {cpp_fixy_global(g)};\n")
        for row in group:
            msg = _message(row)
            if _hard_error(row):
                out.append(f"// {row.family}: the relation stops the build ({row.ours[7:]})\n")
                continue
            if row.family == "fixy.global_wf":
                out.append(f"static_assert(fg::is_global_well_formed_v<G> == {row.ours}, \"{msg}\");\n")
            elif row.family == "fixy.live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            else:
                if row.ours == "=":
                    expected = read_local(row.oracle)
                    if expected is None:
                        raise GoldenError(f"fixy.projection case {row.case}: '=' needs an oracle type")
                    target = f"fs::Projected<fs::OutQueue<>, {cpp_fixy_peer_local(expected)}>"
                else:
                    target = _pin(row.ours)
                out.append(f"static_assert(std::is_same_v<fs::project_t<G, {cpp_role(int(row.role))}>, "
                           f"{target}>, \"{msg}\");\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::fixy_projection\n\nint main() { return 0; }\n")
    return "".join(out)


def subtype_pair(g_text: str, role: str) -> tuple[str, str]:
    """Return the C++ spellings of T and U for a subtyping row.

    ``role`` is ``k+name`` for the pair (mutation k of U, U) and
    ``k-name`` for the pair (U, mutation k of U), where U is the naive
    reading of the global type onto role 0 (model.local_of).
    """
    u = local_of(read_global(g_text), 0)
    sep = "+" if "+" in role else "-"
    index, _, name = role.partition(sep)
    found = mutations(u, SUBTYPE_MUTATIONS)
    k = int(index)
    if k >= len(found) or found[k][0] != name:
        raise GoldenError(f"subtype row {g_text} {role}: no mutation {name} at index {k}")
    t = found[k][1]
    first, second = (t, u) if sep == "+" else (u, t)
    return cpp_fixy_local(first), cpp_fixy_local(second)


def emit_subtype(rows: list[Row]) -> str:
    """Return the fixy subtyping test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in SUBTYPE_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Subtyping of fixy/session against runs of each pair: T refines U when T runs\n",
           "// against the dual of U without a wrong message, a deadlock or a loop that never\n",
           "// acts.  Each U is the naive reading of a global type, and each T one change of U.\n\n",
           "#include <fixy/session/Subtype.h>\n\n#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\n\n", subtype_channel_decl(), "\n",
           "namespace session_oracle::fixy_subtype {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (r.role, SUBTYPE_FAMILIES.index(r.family)))
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        seen: set[str] = set()
        for row in group:
            ns = "m" + row.role.split("+")[0].split("-")[0] + ("f" if "+" in row.role else "r")
            if ns not in seen:
                if seen:
                    out.append(f"}}  // namespace {sorted(seen)[-1]}\n")
                seen = {ns}
                t, u = subtype_pair(row.global_text, row.role)
                out.append(f"namespace {ns} {{\n// {row.role}\nusing T = {t};\nusing U = {u};\n")
            expr = ("fs::is_subtype_sync_v<T, U>" if row.family == "fixy.subtype_sync"
                    else f"fs::is_subtype_async_v<T, U, ::{SUBTYPE_CHANNEL}>")
            out.append(f"static_assert({expr} == {row.ours}, \"{_message(row)}\");\n")
        if seen:
            out.append(f"}}  // namespace {sorted(seen)[-1]}\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::fixy_subtype\n\nint main() { return 0; }\n")
    return "".join(out)


def keyed_subtype_locals(g_text: str, role: str) -> tuple[Local, Local]:
    """Return T and U of a keyed subtyping row.

    ``role`` is ``kK+name`` for the pair (keyed change K of U, U) and
    ``kK-name`` for the pair (U, keyed change K of U), where U is the keyed
    binary view of the global type onto role 0 with the labels 3k+1
    (labelled.keyed_local_of and labelled.sparse_scheme).
    """
    u = labelled.keyed_local_of(labelled.from_positional(read_global(g_text), labelled.sparse_scheme), 0)
    if not role.startswith("k"):
        raise GoldenError(f"keyed subtype row {g_text} {role}: the role must start with k")
    sep = "+" if "+" in role else "-"
    index, _, name = role[1:].partition(sep)
    found = labelled.keyed_mutations(u)
    k = int(index)
    if k >= len(found) or found[k][0] != name:
        raise GoldenError(f"keyed subtype row {g_text} {role}: no keyed change {name} at index {k}")
    t = found[k][1]
    return (t, u) if sep == "+" else (u, t)


def keyed_subtype_pair(g_text: str, role: str) -> tuple[str, str]:
    """Return the C++ spellings of T and U for a keyed subtyping row."""
    t, u = keyed_subtype_locals(g_text, role)
    return labelled.cpp_fixy_keyed_local(t), labelled.cpp_fixy_keyed_local(u)


def wire_pair(g_text: str, role: str) -> tuple[str, str, int]:
    """Return the two endpoint protocols and the seed of a wire row.

    ``role`` is ``<keyed pair role>/<spelling>/s<seed>``.  The endpoints
    are T and the dual of U of the keyed pair.  The spelling ``view`` is
    the binary view (Labelled), and ``peer`` is the form that projection
    writes (PeerMsg, with the peer of T role 1 and of the dual role 0).
    """
    pair_role, spelling, seed = role.split("/")
    t, u = keyed_subtype_locals(g_text, pair_role)
    d = dual_local(u)
    if spelling == "view":
        return labelled.cpp_fixy_keyed_local(t), labelled.cpp_fixy_keyed_local(d), int(seed[1:])
    if spelling == "peer":
        return (labelled.cpp_fixy_peer_keyed_local(t, 0), labelled.cpp_fixy_peer_keyed_local(d, 1),
                int(seed[1:]))
    raise GoldenError(f"wire row {g_text} {role}: no spelling {spelling!r}")


def emit_keyed_subtype(rows: list[Row]) -> str:
    """Return the keyed subtyping test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in KEYED_SUBTYPE_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Subtyping of keyed choices against runs of each pair.  A keyed branch sends\n",
           "// Labelled<Label<n>, Unit>, the handle puts the label word of Label<n> on the wire,\n",
           "// and a run matches each message to the branch of its label.  Each U is the keyed\n",
           "// reading of a global type, and each T one keyed change of U: its branches in\n",
           "// another order, one branch fewer, one branch more, one branch under a new label,\n",
           "// or one choice written positionally.\n\n",
           "#include <fixy/session/Projection.h>\n#include <fixy/session/Subtype.h>\n\n#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\n\n", subtype_channel_decl(), "\n",
           "namespace session_oracle::fixy_keyed_subtype {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (r.role, KEYED_SUBTYPE_FAMILIES.index(r.family)))
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        current = ""
        for row in group:
            index = row.role[1:].split("+")[0].split("-")[0]
            ns = "m" + index + ("f" if "+" in row.role else "r")
            if ns != current:
                if current:
                    out.append(f"}}  // namespace {current}\n")
                current = ns
                t, u = keyed_subtype_pair(row.global_text, row.role)
                out.append(f"namespace {ns} {{\n// {row.role}\nusing T = {t};\nusing U = {u};\n")
            expr = ("fs::is_subtype_sync_v<T, U>" if row.family == "fixy.keyed_subtype_sync"
                    else f"fs::is_subtype_async_v<T, U, ::{SUBTYPE_CHANNEL}>")
            out.append(f"static_assert({expr} == {row.ours}, \"{_message(row)}\");\n")
        if current:
            out.append(f"}}  // namespace {current}\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::fixy_keyed_subtype\n\nint main() { return 0; }\n")
    return "".join(out)


def emit_keyed_multi(rows: list[Row]) -> str:
    """Return the keyed multiparty test translation unit.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in KEYED_MULTI_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Projection and liveness of global types whose choices carry the labels 3k+1\n",
           "// in an order that depends on the path.  Each row pins fixy's answer.  The\n",
           "// golden file records how it compares with the projection of the\n",
           "// subject-reduction development, whose branches carry labels too.\n\n",
           "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n",
           "#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
           "namespace session_oracle::fixy_keyed_projection {\n\n"]
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (KEYED_MULTI_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        out.append(f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n")
        out.append(f"using G = {labelled.cpp_fixy_global(g)};\n")
        for row in group:
            msg = _message(row)
            if row.family == "fixy.keyed_live" and row.ours.startswith("well_formed "):
                out.append(f"static_assert(fg::is_global_well_formed_v<G> == {row.ours.split()[1]}, \"{msg}\");\n")
            elif row.family == "fixy.keyed_live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            else:
                out.append(f"static_assert(std::is_same_v<fs::project_t<G, {cpp_role(int(row.role))}>, "
                           f"{_pin(row.ours)}>, \"{msg}\");\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
    out.append("}  // namespace session_oracle::fixy_keyed_projection\n\nint main() { return 0; }\n")
    return "".join(out)


def crash_reliable_set(roles: list[int], prefix: str) -> str:
    """Return the C++ reliable set of a crash row whose role starts with ``prefix``."""
    unreliable = {int(ch) for ch in prefix.split(".u", 1)[1]}
    return f"fs::ReliableSet<{', '.join(cpp_role(r) for r in roles if r not in unreliable)}>"


def emit_crash(rows: list[Row]) -> str:
    """Return the crash-stop test translation unit.  O(rows)."""
    groups: dict[tuple[str, str], list[Row]] = {}
    for row in rows:
        if row.family in CRASH_FAMILIES and row.status != "gap" and not _hard_error(row):
            groups.setdefault((row.case, row.role.split("/")[0]), []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Crash-stop projection and liveness of fixy/session.  Each global type is a\n",
           "// multiparty case with a crash branch on each transmission of an unreliable\n",
           "// sender.  Each row pins fixy's answer.  The golden file records how mpstk\n",
           "// judges the context of these projections when the unreliable roles crash.\n\n",
           "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n",
           "#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
           "namespace session_oracle::fixy_crash {\n\n"]
    for (case, prefix) in sorted(groups, key=lambda k: (case_key(k[0]), k[1])):
        group = sorted(groups[(case, prefix)], key=lambda r: (CRASH_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        roles = multiparty_roles(labelled.roles_of(g))
        ns = f"{_namespace(case)}_{prefix.replace('.', '_')}"
        out.append(f"// {group[0].global_text}\nnamespace {ns} {{\n")
        out.append(f"using G = {labelled.cpp_fixy_global(g)};\nusing RS = {crash_reliable_set(roles, prefix)};\n")
        for row in group:
            msg = _message(row)
            if row.family == "fixy.crash_live":
                out.append(f"static_assert(fs::crash_live_by_construction_v<G, RS> == {row.ours}, \"{msg}\");\n")
            else:
                role = int(row.role.split("/")[1])
                out.append(f"static_assert(std::is_same_v<fs::project_crash_t<G, {cpp_role(role)}, RS>, "
                           f"{_pin(row.ours)}>, \"{msg}\");\n")
        out.append(f"}}  // namespace {ns}\n\n")
    out.append("}  // namespace session_oracle::fixy_crash\n\nint main() { return 0; }\n")
    return "".join(out)


def emit_enroute(rows: list[Row]) -> str:
    """Return the runtime global type test translation unit.  O(rows)."""
    groups: dict[tuple[str, str], list[Row]] = {}
    for row in rows:
        if row.family in ENROUTE_FAMILIES and row.status != "gap" and not _hard_error(row):
            groups.setdefault((row.case, row.role.split("/")[0]), []).append(row)
    out = [GENERATED_NOTICE, "//\n",
           "// Runtime global types: the first send of a multiparty case puts its\n",
           "// transmission en route, with every branch or with the chosen branch only.\n",
           "// Each row pins fixy's projection, its liveness claim, and whether the context\n",
           "// that the send reaches from the static projection associates with the runtime\n",
           "// type.\n\n",
           "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n",
           "#include <type_traits>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
           "namespace session_oracle::fixy_enroute {\n\n"]
    for (case, tag) in sorted(groups, key=lambda k: (case_key(k[0]), k[1])):
        group = sorted(groups[(case, tag)], key=lambda r: (ENROUTE_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        ns = f"{_namespace(case)}_{tag}"
        out.append(f"// {group[0].global_text}\nnamespace {ns} {{\nusing G = {labelled.cpp_fixy_global(g)};\n")
        for row in group:
            msg = _message(row)
            if row.family == "fixy.enroute_live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            elif row.family == "fixy.enroute_association":
                out.append(f"static_assert(fs::association_holds_v<{row.oracle}, G> == {row.ours}, \"{msg}\");\n")
            else:
                role = int(row.role.split("/")[1])
                out.append(f"static_assert(std::is_same_v<fs::project_t<G, {cpp_role(role)}>, {_pin(row.ours)}>, "
                           f"\"{msg}\");\n")
        out.append(f"}}  // namespace {ns}\n\n")
    out.append("}  // namespace session_oracle::fixy_enroute\n\nint main() { return 0; }\n")
    return "".join(out)


def wire_label(case: str, role: str) -> str:
    """Return the text that the wire test prints before the outcome of a row."""
    return f"session_oracle fixy.wire case {case} role {role}:"


def wire_source(entries: list[tuple[str, str, str, int, str]]) -> str:
    """Return a wire test for rows (label, endpoint A, endpoint B, seed, pinned outcome).

    Run with --measure, the program prints the outcome of each row.
    Otherwise it compares each outcome with the pinned one.  O(entries).
    """
    out = [GENERATED_NOTICE, "//\n",
           "// End-to-end runs of keyed pairs over the session handle: the handle of T\n",
           "// against the handle of the dual of U, over one queue of words, for pairs\n",
           "// whose synchronous run is safe.  Each row pins the outcome of one run.\n\n",
           "#include \"wire_driver.h\"\n\n#include <fixy/session/Projection.h>\n\n",
           cpp_prelude(), "\nnamespace fs = ::fixy::session;\n\n",
           "namespace session_oracle::fixy_wire {\n\n"]
    rows = []
    for n, (label, a, b, seed, pinned) in enumerate(entries):
        out.append(f"// {label}\nnamespace r{n} {{\nusing A = {a};\nusing B = {b};\n}}  // namespace r{n}\n")
        rows.append(f"    {{\"{label}\", &::session_oracle::wire::run_pair<r{n}::A, r{n}::B, {seed}>, \"{pinned}\"}},\n")
    out.append("\ninline constexpr ::session_oracle::wire::Row rows[] = {\n")
    out.extend(rows or ["    {\"\", nullptr, \"\"},\n"])
    out.append("};\n\n}  // namespace session_oracle::fixy_wire\n\n"
               "int main(int argc, char** argv) {\n")
    if rows:
        out.append("    return ::session_oracle::wire::check_all(::session_oracle::fixy_wire::rows, argc, argv);\n")
    else:
        out.append("    (void)argc;\n    (void)argv;\n    return 0;\n")
    out.append("}\n")
    return "".join(out)


def emit_wire(rows: list[Row]) -> str:
    """Return the wire test translation unit.  O(rows)."""
    entries = []
    for row in sorted((r for r in rows if r.family in WIRE_FAMILIES and r.status != "gap"),
                      key=lambda r: (case_key(r.case), r.role)):
        a, b, seed = wire_pair(row.global_text, row.role)
        entries.append((wire_label(row.case, row.role), a, b, seed, row.ours))
    return wire_source(entries)


def _split_closers(text: str) -> str:
    """Write each run of closing angle brackets as separate tokens.

    The pinned tree-sitter grammar that the AST gates use reads a long
    run such as >>>> inside a static_assert argument as shift operators.
    The emitted tests hold no shift operator, so every >> becomes > >.
    O(length).
    """
    while ">>" in text:
        text = text.replace(">>", "> >")
    return text


# The deepest template nest that one emitted line may hold.  The pinned
# tree-sitter grammar of the AST gates cannot tell a template from a
# less-than inside a nest of about twenty levels, and a file with one such
# line fails parse_clean as a whole.
NEST_LIMIT = 6
_QUALIFIED = re.compile(r"\s*(::)?[A-Za-z_][A-Za-z_0-9]*(::[A-Za-z_][A-Za-z_0-9]*)*\s*")


class _Nest:
    """The template argument list of one template-id: each argument is a list of pieces."""

    __slots__ = ("args",)

    def __init__(self, args: list[list["_Piece"]]) -> None:
        self.args = args


_Piece = Union[str, _Nest]


def _tokens(line: str) -> list[str]:
    """Split one line into string literals, the three bracket tokens and runs of other text."""
    out: list[str] = []
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == '"':
            j = i + 1
            while j < len(line) and line[j] != '"':
                j += 2 if line[j] == "\\" else 1
            out.append(line[i:j + 1])
            i = j + 1
        elif ch in "<>,":
            out.append(ch)
            i += 1
        else:
            j = i
            while j < len(line) and line[j] not in '"<>,':
                j += 1
            out.append(line[i:j])
            i = j
    return out


def _parse_nest(tokens: list[str], pos: int, inside: bool) -> tuple[list[_Piece], int]:
    """Read pieces up to the end of one template argument.  Returns the pieces and the next position."""
    pieces: list[_Piece] = []
    while pos < len(tokens):
        tok = tokens[pos]
        if tok == "<":
            args: list[list[_Piece]] = []
            pos += 1
            while True:
                arg, pos = _parse_nest(tokens, pos, True)
                args.append(arg)
                if pos >= len(tokens):
                    raise GoldenError("an emitted line opens a template argument list that it never closes")
                closer = tokens[pos]
                pos += 1
                if closer == ">":
                    break
            pieces.append(_Nest(args))
        elif inside and tok in ",>":
            return pieces, pos
        else:
            pieces.append(tok)
            pos += 1
    return pieces, pos


def _depth(pieces: list[_Piece]) -> int:
    return max((1 + max((_depth(a) for a in p.args), default=0) for p in pieces if isinstance(p, _Nest)),
               default=0)


def _render(pieces: list[_Piece]) -> str:
    return "".join(p if isinstance(p, str) else "<" + ",".join(_render(a) for a in p.args) + ">"
                   for p in pieces)


def _hoist(pieces: list[_Piece], aliases: list[str], counter: list[int]) -> None:
    """Replace each deep template argument that is one template-id by an alias, bottom up."""
    for piece in pieces:
        if not isinstance(piece, _Nest):
            continue
        for k, arg in enumerate(piece.args):
            _hoist(arg, aliases, counter)
            core = list(arg)
            trail = ""
            while core and isinstance(core[-1], str) and not core[-1].strip():
                trail = core.pop() + trail
            is_template_id = (len(core) == 2 and isinstance(core[0], str) and isinstance(core[1], _Nest)
                              and _QUALIFIED.fullmatch(core[0]) is not None)
            if is_template_id and _depth(core) > NEST_LIMIT - 1:
                name = f"session_oracle_nest{counter[0]}"
                counter[0] += 1
                lead = core[0][:len(core[0]) - len(core[0].lstrip())]
                aliases.append(f"using {name} = {_render(core).strip()};\n")
                piece.args[k] = [lead + name + trail]


def _hoist_nests(text: str) -> str:
    """Give each template nest deeper than NEST_LIMIT an alias on the lines before its statement.

    Every emitted statement that nests deeper gets one using-declaration for
    each deep template argument, built bottom up, so no line nests deeper
    than NEST_LIMIT.  The aliases are named session_oracle_nest<k>, unique in
    the file, and each one is in scope where its statement is, because it
    sits in the same scope just before it.  O(length of the text).
    """
    out: list[str] = []
    counter = [0]
    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        if stripped.startswith(("#", "//")):
            out.append(line)
            continue
        tokens = _tokens(line)
        pieces, _ = _parse_nest(tokens, 0, False)
        if _depth(pieces) <= NEST_LIMIT:
            out.append(line)
            continue
        if not stripped.rstrip().endswith(";"):
            raise GoldenError(f"an emitted line nests deeper than {NEST_LIMIT} and is not one statement: "
                              f"{stripped[:120]}")
        aliases: list[str] = []
        _hoist(pieces, aliases, counter)
        indent = line[:len(line) - len(stripped)]
        out.extend(indent + a for a in aliases)
        rendered = _render(pieces)
        if _depth(_parse_nest(_tokens(rendered), 0, False)[0]) > NEST_LIMIT:
            raise GoldenError(f"an emitted line still nests deeper than {NEST_LIMIT}: {stripped[:120]}")
        out.append(rendered)
    return "".join(out)


def emit_all(rows: list[Row]) -> dict[str, str]:
    """Return the file name and text of every emitted test."""
    return {name: _hoist_nests(_split_closers(text)) for name, text in (
        ("generated_fixy_duality.cpp", emit_fixy(rows)),
        ("generated_fixy_projection.cpp", emit_multi(rows)),
        ("generated_fixy_subtype.cpp", emit_subtype(rows)),
        ("generated_fixy_keyed_subtype.cpp", emit_keyed_subtype(rows)),
        ("generated_fixy_keyed_projection.cpp", emit_keyed_multi(rows)),
        ("generated_fixy_crash.cpp", emit_crash(rows)),
        ("generated_fixy_enroute.cpp", emit_enroute(rows)),
        ("generated_fixy_wire.cpp", emit_wire(rows)),
        ("generated_old_projection.cpp", emit_old(rows)))}
