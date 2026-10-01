"""The golden file and the C++ tests that are emitted from it.

The golden file is a CSV table with one row per measured property.
Lines that start with ``#`` are metadata.  The columns:

    family   the relation and property, for example ``fixy.projection``
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

import labelled
from model import (Local, cpp_fixy_global, cpp_fixy_local, cpp_fixy_peer_local, cpp_prelude, cpp_role,
                   dual_local, local_of, mutations, read_global, read_local, show_global)

COLUMNS = ("family", "case", "role", "global", "oracle", "ours", "status", "class", "note")

# The corpora, in the order of the golden file.  For the multiparty
# relations: r random, a adversarial, o types whose inner loop jumps to
# an outer binder, h hand-written review cases, m minimal forms of the
# divergences that the shrinker found.  p holds the paper examples of the
# two kinds.  fr, fa, fh and fm are the random, adversarial, hand-written
# and minimal corpora of the two-party relations.
CORPORA = ("r", "a", "o", "h", "p", "m", "fr", "fa", "fh", "fm")

FIXY_FAMILIES = ("fixy.dual", "fixy.is_dual", "fixy.involution", "fixy.involutive_flag",
                 "fixy.well_formed", "fixy.accepts")
MULTI_FAMILIES = ("fixy.global_wf", "fixy.projection", "fixy.live", "fixy.network")
# The networks of the network family and of the implementability family
# (sprout.py), and the enumerator of fixy::session::Network for each.  The
# role of a fixy.network row is the network, and its ours column is
# network_refusal_v of fixy/session/Network.h: None where implementable_on_v
# admits the type, and otherwise the reason of the refusal.
NETWORK_ENUMERATORS = {"p2pbox": "PerPairFifo", "mailbox": "Mailbox", "bag": "Bag"}
# The enumerators of fixy::session::NetworkRefusal, in order.  The probe
# reads a refusal as its number, and the emitted assertion names it, so a
# change of the order in the header fails the emitted test.
NETWORK_REFUSALS = ("None", "NotAReliableSet", "NotWellFormed", "RuntimeConstruct", "NotBalancedPlus",
                    "RoleNotProjectable", "CrashStopOffPerPairFifo", "ReceiverHasTwoSenders", "ChoiceOnBag",
                    "RepeatedWordOnBag")
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
# Families that no C++ test can assert: the run of the oracle's own
# projection, the run of the subject-reduction development's projection,
# mpstk's model check of fixy's crash-stop context, the coqc-checked
# verdict of the ITP 2025 subtyping relation on each synchronous
# subtyping pair, the coqc-checked liveness of fixy's projected context
# by the ITP 2026 liveness theorem, the implementability verdict of
# Sprout(A) on each kind of network, and the walks of the transition
# systems of Semantics.h along the run of each projected context.
RECORD_FAMILIES = ("oracle.safety", "sr.safety", "mpstk.crash", "ekici.subtype",
                   "keskin.live", "sprout.implementable", "fixy.global_lts", "fixy.config_lts",
                   "fixy.crash_association")
FAMILIES = (FIXY_FAMILIES + MULTI_FAMILIES + SUBTYPE_FAMILIES + KEYED_SUBTYPE_FAMILIES + KEYED_MULTI_FAMILIES
            + CRASH_FAMILIES + ENROUTE_FAMILIES + WIRE_FAMILIES + RECORD_FAMILIES)
# The roles that a multiparty case is projected onto: every role it names,
# and at least three.
MIN_ROLES = 3


def multiparty_roles(named: set[int]) -> list[int]:
    """Return the roles that a multiparty case is projected onto."""
    return list(range(max(MIN_ROLES, max(named, default=-1) + 1)))


STATUSES = ("agree", "divergence", "gap")

_NOTICE_LINES = ("Generated by utils/tools/session_oracle from test/session_oracle/golden.csv.",
                 "Do not edit.  Regenerate with utils/scripts/session-oracle.sh --regenerate, or",
                 "re-emit after a note change with utils/scripts/session-oracle.sh --emit.")
GENERATED_NOTICE = ("".join(f"// {line}\n" for line in _NOTICE_LINES)
                    + "// clang-format off: the emitter owns this layout, and session_oracle_check compares it "
                      "byte for byte.\n")

# ── Shards ───────────────────────────────────────────────────────────
#
# Each emitted test is one executable, test_session_oracle_<stem>.  Its
# rows go into several source files of that executable, the shards
# generated_<stem>_<k>.cpp, so that no translation unit holds all the
# rows of a family.  A shard ends at the boundary of a unit whose rows
# share their types: a case, or one keyed pair in the wire test.  Two
# shards that both named one type would instantiate it two times.  Shard
# 0 holds main.  The hoister of a test numbers its aliases across all its
# shards, so the shards of a test, joined in order, hold the cases of
# the test in the order of the golden file.
#
# SHARD_BUDGET gives the size of one shard of each test, in characters of
# emitted case text.  Each size keeps the compile of one shard in the
# Debug build under 20 s, with a margin for a loaded host: about 8 s for
# the cases and up to 9 s for the headers of the shard.  The longest
# compile sets the wall time of a build at a high job count, so the
# budget is small, although each shard parses its headers one time.  The
# cost of a character differs between the tests: a wire row instantiates
# a run of two handles, and a static assertion instantiates one relation.
# It also differs inside one test: a large global type costs more for
# each character than a small one, so each budget fits the heaviest
# shard of its test.
# GENERATED_SOURCES lists the shards of each test for CMake.
SHARD_BUDGET = {
    "fixy_duality": 340_000,
    "fixy_projection": 500_000,
    "fixy_subtype": 230_000,
    "fixy_keyed_subtype": 280_000,
    "fixy_keyed_projection": 450_000,
    "fixy_crash": 165_000,
    "fixy_enroute": 375_000,
    "fixy_wire": 24_000,
}
GENERATED_SOURCES = "generated_sources.cmake"


def shard_file(stem: str, index: int) -> str:
    """Return the file name of shard ``index`` of the test of ``stem``."""
    return f"generated_{stem}_{index:02d}.cpp"


def _partition(weights: list[int], budget: int) -> list[range]:
    """Cut items of the given weights into contiguous shards.  Return the index range of each shard.

    The count of shards is the least count whose mean weight stays within
    ``budget``.  Each item goes to the shard that holds the middle of its
    weight on the cumulative scale, so each cut falls at the item boundary
    nearest to an equal share.  An item heavier than ``budget`` stays whole.
    The result holds at least one range, which is empty for no items.  O(items).
    """
    total = sum(weights)
    if total == 0:
        return [range(0, len(weights))]
    count = -(-total // budget)
    owners: list[int] = []
    before = 0
    for weight in weights:
        # Twice the middle of the item, so the arithmetic stays in integers.
        owners.append(min(count - 1, (2 * before + weight) * count // (2 * total)))
        before += weight
    ranges: list[range] = []
    start = 0
    for end in range(1, len(owners) + 1):
        if end == len(owners) or owners[end] != owners[start]:
            ranges.append(range(start, end))
            start = end
    return ranges or [range(0, 0)]


def _shard_line(stem: str, index: int, count: int) -> str:
    """Return the comment line that names the place of one shard in its test."""
    holder = "  It holds main()." if index == 0 else ""
    return f"// Shard {index} of the {count} shards of test_session_oracle_{stem}.{holder}\n"


def _static_shards(stem: str, description: str, preamble: str, namespace: str, blocks: list[str]) -> list[str]:
    """Return the shards of a test whose rows are static assertions.

    ``blocks`` holds the text of each case, in order.  Each shard is the
    notice, the description, the preamble (includes and declarations) and
    the namespace that holds its cases.  O(size).
    """
    ranges = _partition([len(block) for block in blocks], SHARD_BUDGET[stem])
    shards: list[str] = []
    for index, part in enumerate(ranges):
        text = [GENERATED_NOTICE, _shard_line(stem, index, len(ranges)), description, preamble,
                f"namespace {namespace} {{\n\n", *(blocks[i] for i in part), f"}}  // namespace {namespace}\n"]
        if index == 0:
            text.append("\nint main() { return 0; }\n")
        shards.append("".join(text))
    return shards


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


# ── Type terms and the nest budget ───────────────────────────────────
#
# Every type that an emitted test names comes from one of two sources: a
# printer of model.py or labelled.py, or a canonical spelling that the
# golden file stores (the compiler's print of our relation's answer).  Both
# are type-ids of one small grammar:
#
#     type  := integer | name [ '<' [ type { ',' type } ] '>' ]
#     name  := [ '::' ] identifier { '::' identifier }
#
# cpp_type reads a spelling of that grammar into a CppType, and refuses any
# other text.  The emitters build each statement from literal text and
# CppType terms, and the renderer writes every term in one canonical form.
# So no emitted line is ever read back as text.

# The deepest template nest that one emitted line may hold.  The pinned
# tree-sitter grammar of the AST gates cannot tell a template from a
# less-than inside a nest of about twenty levels, and a file with one such
# line fails parse_clean as a whole.
NEST_LIMIT = 6

_TYPE_TOKEN = re.compile(r"\s*(?:(?P<scope>::)|(?P<open><)|(?P<close>>)|(?P<comma>,)"
                         r"|(?P<ident>[A-Za-z_][A-Za-z_0-9]*)|(?P<integer>[0-9]+[uUlL]*))")


@dataclass(frozen=True, slots=True)
class CppType:
    """One type-id or template argument of an emitted test.

    ``name`` is a qualified name, which can start with ``::``, or an integer
    literal.  ``args`` is None for a name with no template argument list,
    and a tuple (empty for ``X<>``) for a template-id.
    """

    name: str
    args: tuple["CppType", ...] | None = None

    @property
    def depth(self) -> int:
        """Return the template nest depth: 0 for a plain name, 1 + the deepest argument otherwise.  O(size)."""
        if self.args is None:
            return 0
        return 1 + max((arg.depth for arg in self.args), default=0)

    def render(self) -> str:
        """Return the canonical spelling.

        Arguments are divided by ", ".  A closing bracket that follows
        another closing bracket gets a space before it, so that the pinned
        grammar never reads a run of closers as a shift operator.  O(size).
        """
        if self.args is None:
            return self.name
        inner = ", ".join(arg.render() for arg in self.args)
        closer = " >" if inner.endswith(">") else ">"
        return f"{self.name}<{inner}{closer}"


def cpp_type(spelling: str) -> CppType:
    """Read one type-id of the grammar above.  Refuse any other text with GoldenError.  O(length)."""
    tokens: list[tuple[str, str]] = []
    pos = 0
    while pos < len(spelling):
        match = _TYPE_TOKEN.match(spelling, pos)
        if match is None or match.end() == pos:
            if spelling[pos:].strip() == "":
                break
            raise GoldenError(f"a C++ spelling holds text outside the type grammar at column {pos}: "
                              f"{spelling[:120]!r}")
        kind = match.lastgroup
        assert kind is not None
        tokens.append((kind, match.group(kind)))
        pos = match.end()

    def parse(at: int) -> tuple[CppType, int]:
        if at < len(tokens) and tokens[at][0] == "integer":
            return CppType(tokens[at][1]), at + 1
        parts: list[str] = []
        if at < len(tokens) and tokens[at][0] == "scope":
            parts.append("::")
            at += 1
        while True:
            if at >= len(tokens) or tokens[at][0] != "ident":
                raise GoldenError(f"a C++ spelling has no name where the grammar needs one: {spelling[:120]!r}")
            parts.append(tokens[at][1])
            at += 1
            if at < len(tokens) and tokens[at][0] == "scope":
                parts.append("::")
                at += 1
                continue
            break
        name = "".join(parts)
        if at >= len(tokens) or tokens[at][0] != "open":
            return CppType(name), at
        at += 1
        args: list[CppType] = []
        if at < len(tokens) and tokens[at][0] == "close":
            return CppType(name, ()), at + 1
        while True:
            arg, at = parse(at)
            args.append(arg)
            if at >= len(tokens):
                raise GoldenError(f"a C++ spelling opens a template argument list that it never closes: "
                                  f"{spelling[:120]!r}")
            if tokens[at][0] == "comma":
                at += 1
                continue
            if tokens[at][0] == "close":
                return CppType(name, tuple(args)), at + 1
            raise GoldenError(f"a C++ spelling has {tokens[at][1]!r} inside a template argument list: "
                              f"{spelling[:120]!r}")

    term, end = parse(0)
    if end != len(tokens):
        raise GoldenError(f"a C++ spelling holds text after its type-id: {spelling[:120]!r}")
    return term


class _Hoister:
    """Give each deep template argument of one emitted file an alias before its statement.

    A statement is literal text and CppType terms.  A term is written at the
    top level of its statement, so every template that holds a term is part
    of the term.  Each template argument that nests NEST_LIMIT deep or more
    gets a using-declaration on the lines before the statement, built bottom
    up, so no line nests deeper than NEST_LIMIT.  The aliases are named
    session_oracle_nest<k>, unique in the file, and each one is in scope
    where its statement is, because it sits in the same scope just before it.
    """

    __slots__ = ("_count", "deepest")

    def __init__(self) -> None:
        self._count = 0
        # The deepest nest of any term that this hoister wrote, for the self-check.
        self.deepest = 0

    def line(self, *parts: str | CppType) -> str:
        """Return the aliases that the terms of one statement need, then the statement.  O(size)."""
        aliases: list[str] = []
        rendered: list[str] = []
        for part in parts:
            if isinstance(part, str):
                rendered.append(part)
                continue
            lifted = self._lift(part, aliases, top=True)
            self.deepest = max(self.deepest, lifted.depth)
            rendered.append(lifted.render())
        return "".join(aliases) + "".join(rendered)

    def _lift(self, term: CppType, aliases: list[str], top: bool) -> CppType:
        if term.args is None:
            return term
        lifted = CppType(term.name, tuple(self._lift(arg, aliases, top=False) for arg in term.args))
        if top or lifted.depth < NEST_LIMIT:
            return lifted
        name = f"session_oracle_nest{self._count}"
        self._count += 1
        self.deepest = max(self.deepest, lifted.depth)
        aliases.append(f"using {name} = {lifted.render()};\n")
        return CppType(name)


def emit_self_check() -> list[str]:
    """Return the failures of the type grammar and of the nest budget on planted input.

    Each case holds a spelling that the grammar must refuse, or a nest that
    the hoister must cut to NEST_LIMIT.  The text tokenizer that the hoister
    replaced read every '<' and '>' as a bracket, so it accepted the refused
    spellings below and miscounted their nest.
    """
    failures: list[str] = []
    canonical = cpp_type("a::B<c::D<e::F<1>>, ::g::H<>>").render()
    if canonical != "a::B<c::D<e::F<1> >, ::g::H<> >":
        failures.append(f"the renderer writes {canonical!r}")
    for refused in ("a::X<1> == 2", "a::X<(1 > 0)>", "a::X<b::Y", "a::X<b>::type", "a::X<b,>", "a::X<b> c"):
        try:
            cpp_type(refused)
        except GoldenError:
            continue
        failures.append(f"the type grammar admits {refused!r}")
    hoister = _Hoister()
    deep = "fs::Loop<" * 30 + "fs::End" + ">" * 30
    text = hoister.line("using T = ", cpp_type(deep), ";\n")
    # The aliases cut the chain at depth 6, 12, 18 and 24, and the statement keeps the top 6 levels.
    if hoister.deepest > NEST_LIMIT or text.count("using session_oracle_nest") != 4:
        failures.append(f"a nest of 30 comes out {hoister.deepest} deep with "
                        f"{text.count('using session_oracle_nest')} aliases")
    seven = _Hoister().line("using T = ", cpp_type("a::A<" * 7 + "x" + ">" * 7), ";\n")
    expected = ("using session_oracle_nest0 = a::A<a::A<a::A<a::A<a::A<a::A<x> > > > > >;\n"
                "using T = a::A<session_oracle_nest0>;\n")
    if seven != expected:
        failures.append(f"a nest of 7 comes out as {seven!r}")
    return failures


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
    "fixy.involution": "std::is_same_v<fs::dual_of_t<fs::dual_of_t<T0> >, T0>",
    "fixy.involutive_flag": "std::is_same_v<fs::dual_of_t<fs::dual_of_t<T0> >, T0>",
    "fixy.well_formed": "(fs::is_well_formed_v<T0> && fs::is_well_formed_v<T1>)",
    "fixy.accepts": ("(fs::is_well_formed_v<N0> && fs::is_well_formed_v<N1> && "
                     "fs::is_dual_v<N0, N1>)"),
}


def fixy_accepts_expression() -> str:
    """Return the C++ expression that fixy.accepts measures on N0 and N1."""
    return _FIXY_BOOL["fixy.accepts"]


def _hard_error(row: Row) -> bool:
    """Return true for a row whose relation stopped the build, which no test can assert."""
    return row.ours.startswith("reject:")


def _fixy_assert(row: Row, hoister: _Hoister) -> str:
    msg = _message(row)
    if _hard_error(row):
        return f"// {row.family}: the relation stops the build ({row.ours[7:]})\n"
    if row.family == "fixy.dual":
        target = "T1" if row.ours == "=" else _pin(row.ours)
        return hoister.line("static_assert(", cpp_type(f"std::is_same_v<fs::dual_of_t<T0>, {target}>"),
                            f", \"{msg}\");\n")
    if row.ours not in ("true", "false"):
        raise GoldenError(f"{row.family} case {row.case}: ours must be true or false")
    return f"static_assert({_FIXY_BOOL[row.family]} == {row.ours}, \"{msg}\");\n"


def _namespace(case: str) -> str:
    return "c_" + case


def emit_fixy(rows: list[Row]) -> list[str]:
    """Return the shards of the duality test.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in FIXY_FAMILIES and row.status != "gap":
            cases.setdefault(row.case, []).append(row)
    hoister = _Hoister()
    blocks: list[str] = []
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: FIXY_FAMILIES.index(r.family))
        out = [f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n"]
        pair_rows = [r for r in group if r.family != "fixy.accepts"]
        if pair_rows:
            t0, t1 = _fixy_pair(pair_rows[0])
            out.append(hoister.line("using T0 = ", cpp_type(t0), ";\n"))
            out.append(hoister.line("using T1 = ", cpp_type(t1), ";\n"))
        if any(r.family == "fixy.accepts" for r in group):
            g = read_global(group[0].global_text)
            out.append(hoister.line("using N0 = ", cpp_type(cpp_fixy_local(local_of(g, 0))), ";\n"))
            out.append(hoister.line("using N1 = ", cpp_type(cpp_fixy_local(local_of(g, 1))), ";\n"))
        for row in group:
            out.append(_fixy_assert(row, hoister))
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
        blocks.append("".join(out))
    return _static_shards(
        "fixy_duality",
        "//\n"
        "// Duality against the projection oracle.  For a two-party global type the\n"
        "// oracle's projection onto role 1 is the dual of its projection onto role 0,\n"
        "// so dual_of_t must map T0 onto T1.  N0 and N1 are the naive reading of the\n"
        "// global type, which fixy.accepts compares with the oracle.\n\n",
        "#include <fixy/session/Protocol.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\n\n",
        "session_oracle::fixy_duality", blocks)


def emit_multi(rows: list[Row]) -> list[str]:
    """Return the shards of the fixy multiparty test.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in MULTI_FAMILIES and row.status != "gap":
            cases.setdefault(row.case, []).append(row)
    hoister = _Hoister()
    blocks: list[str] = []
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (MULTI_FAMILIES.index(r.family), r.role))
        g = read_global(group[0].global_text)
        out = [f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n"]
        out.append(hoister.line("using G = ", cpp_type(cpp_fixy_global(g)), ";\n"))
        for row in group:
            msg = _message(row)
            if _hard_error(row):
                out.append(f"// {row.family}: the relation stops the build ({row.ours[7:]})\n")
                continue
            if row.family == "fixy.global_wf":
                out.append(f"static_assert(fg::is_global_well_formed_v<G> == {row.ours}, \"{msg}\");\n")
            elif row.family == "fixy.live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            elif row.family == "fixy.network":
                network = NETWORK_ENUMERATORS.get(row.role)
                if network is None:
                    raise GoldenError(f"fixy.network case {row.case}: {row.role!r} names no network")
                if row.ours not in NETWORK_REFUSALS:
                    raise GoldenError(f"fixy.network case {row.case}: {row.ours!r} names no network refusal")
                out.append(f"static_assert(fs::network_refusal_v<G, fs::Network::{network}> == "
                           f"fs::NetworkRefusal::{row.ours}, \"{msg}\");\n")
            else:
                if row.ours == "=":
                    expected = read_local(row.oracle)
                    if expected is None:
                        raise GoldenError(f"fixy.projection case {row.case}: '=' needs an oracle type")
                    target = f"fs::Projected<fs::OutQueue<>, {cpp_fixy_peer_local(expected)}>"
                else:
                    target = _pin(row.ours)
                out.append(hoister.line(
                    "static_assert(",
                    cpp_type(f"std::is_same_v<fs::project_t<G, {cpp_role(int(row.role))}>, {target}>"),
                    f", \"{msg}\");\n"))
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
        blocks.append("".join(out))
    return _static_shards(
        "fixy_projection",
        "//\n"
        "// Global types, projection and liveness of fixy/session against the\n"
        "// projection oracle.  A projection row asserts the oracle's answer when\n"
        "// fixy gives it as written, and pins fixy's answer otherwise.\n\n",
        "#include <fixy/session/Liveness.h>\n#include <fixy/session/Network.h>\n"
        "#include <fixy/session/Projection.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
        "session_oracle::fixy_projection", blocks)


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


def emit_subtype(rows: list[Row]) -> list[str]:
    """Return the shards of the fixy subtyping test.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in SUBTYPE_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    hoister = _Hoister()
    blocks: list[str] = []
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (r.role, SUBTYPE_FAMILIES.index(r.family)))
        out = [f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n"]
        seen: set[str] = set()
        for row in group:
            ns = "m" + row.role.split("+")[0].split("-")[0] + ("f" if "+" in row.role else "r")
            if ns not in seen:
                if seen:
                    out.append(f"}}  // namespace {sorted(seen)[-1]}\n")
                seen = {ns}
                t, u = subtype_pair(row.global_text, row.role)
                out.append(f"namespace {ns} {{\n// {row.role}\n")
                out.append(hoister.line("using T = ", cpp_type(t), ";\n"))
                out.append(hoister.line("using U = ", cpp_type(u), ";\n"))
            expr = ("fs::is_subtype_sync_v<T, U>" if row.family == "fixy.subtype_sync"
                    else f"fs::is_subtype_async_v<T, U, ::{SUBTYPE_CHANNEL}>")
            out.append(f"static_assert({expr} == {row.ours}, \"{_message(row)}\");\n")
        if seen:
            out.append(f"}}  // namespace {sorted(seen)[-1]}\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
        blocks.append("".join(out))
    return _static_shards(
        "fixy_subtype",
        "//\n"
        "// Subtyping of fixy/session against runs of each pair: T refines U when T runs\n"
        "// against the dual of U without a wrong message, a deadlock or a loop that never\n"
        "// acts.  Each U is the naive reading of a global type, and each T one change of U.\n\n",
        "#include <fixy/session/Subtype.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\n\n" + subtype_channel_decl() + "\n",
        "session_oracle::fixy_subtype", blocks)


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


def emit_keyed_subtype(rows: list[Row]) -> list[str]:
    """Return the shards of the keyed subtyping test.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in KEYED_SUBTYPE_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    hoister = _Hoister()
    blocks: list[str] = []
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (r.role, KEYED_SUBTYPE_FAMILIES.index(r.family)))
        out = [f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n"]
        current = ""
        for row in group:
            index = row.role[1:].split("+")[0].split("-")[0]
            ns = "m" + index + ("f" if "+" in row.role else "r")
            if ns != current:
                if current:
                    out.append(f"}}  // namespace {current}\n")
                current = ns
                t, u = keyed_subtype_pair(row.global_text, row.role)
                out.append(f"namespace {ns} {{\n// {row.role}\n")
                out.append(hoister.line("using T = ", cpp_type(t), ";\n"))
                out.append(hoister.line("using U = ", cpp_type(u), ";\n"))
            expr = ("fs::is_subtype_sync_v<T, U>" if row.family == "fixy.keyed_subtype_sync"
                    else f"fs::is_subtype_async_v<T, U, ::{SUBTYPE_CHANNEL}>")
            out.append(f"static_assert({expr} == {row.ours}, \"{_message(row)}\");\n")
        if current:
            out.append(f"}}  // namespace {current}\n")
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
        blocks.append("".join(out))
    return _static_shards(
        "fixy_keyed_subtype",
        "//\n"
        "// Subtyping of keyed choices against runs of each pair.  A keyed branch sends\n"
        "// Labelled<Label<n>, Unit>, the handle puts the label word of Label<n> on the wire,\n"
        "// and a run matches each message to the branch of its label.  Each U is the keyed\n"
        "// reading of a global type, and each T one keyed change of U: its branches in\n"
        "// another order, one branch fewer, one branch more, one branch under a new label,\n"
        "// or one choice written positionally.\n\n",
        "#include <fixy/session/Projection.h>\n#include <fixy/session/Subtype.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\n\n" + subtype_channel_decl() + "\n",
        "session_oracle::fixy_keyed_subtype", blocks)


def emit_keyed_multi(rows: list[Row]) -> list[str]:
    """Return the shards of the keyed multiparty test.  O(rows)."""
    cases: dict[str, list[Row]] = {}
    for row in rows:
        if row.family in KEYED_MULTI_FAMILIES and row.status != "gap" and not _hard_error(row):
            cases.setdefault(row.case, []).append(row)
    hoister = _Hoister()
    blocks: list[str] = []
    for case in sorted(cases, key=case_key):
        group = sorted(cases[case], key=lambda r: (KEYED_MULTI_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        out = [f"// {group[0].global_text}\nnamespace {_namespace(case)} {{\n"]
        out.append(hoister.line("using G = ", cpp_type(labelled.cpp_fixy_global(g)), ";\n"))
        for row in group:
            msg = _message(row)
            if row.family == "fixy.keyed_live" and row.ours.startswith("well_formed "):
                out.append(f"static_assert(fg::is_global_well_formed_v<G> == {row.ours.split()[1]}, \"{msg}\");\n")
            elif row.family == "fixy.keyed_live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            else:
                out.append(hoister.line(
                    "static_assert(",
                    cpp_type(f"std::is_same_v<fs::project_t<G, {cpp_role(int(row.role))}>, {_pin(row.ours)}>"),
                    f", \"{msg}\");\n"))
        out.append(f"}}  // namespace {_namespace(case)}\n\n")
        blocks.append("".join(out))
    return _static_shards(
        "fixy_keyed_projection",
        "//\n"
        "// Projection and liveness of global types whose choices carry the labels 3k+1\n"
        "// in an order that depends on the path.  Each row pins fixy's answer.  The\n"
        "// golden file records how it compares with the projection of the\n"
        "// subject-reduction development, whose branches carry labels too.\n\n",
        "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
        "session_oracle::fixy_keyed_projection", blocks)


def crash_reliable_set(roles: list[int], prefix: str) -> str:
    """Return the C++ reliable set of a crash row whose role starts with ``prefix``."""
    unreliable = {int(ch) for ch in prefix.split(".u", 1)[1]}
    return f"fs::ReliableSet<{', '.join(cpp_role(r) for r in roles if r not in unreliable)}>"


def emit_crash(rows: list[Row]) -> list[str]:
    """Return the shards of the crash-stop test.  O(rows).

    The unit of a shard is one case, with every crash-stop variant of it,
    because the variants of a case share its global type.
    """
    groups: dict[tuple[str, str], list[Row]] = {}
    for row in rows:
        if row.family in CRASH_FAMILIES and row.status != "gap" and not _hard_error(row):
            groups.setdefault((row.case, row.role.split("/")[0]), []).append(row)
    hoister = _Hoister()
    blocks: dict[str, list[str]] = {}
    for (case, prefix) in sorted(groups, key=lambda k: (case_key(k[0]), k[1])):
        group = sorted(groups[(case, prefix)], key=lambda r: (CRASH_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        roles = multiparty_roles(labelled.roles_of(g))
        ns = f"{_namespace(case)}_{prefix.replace('.', '_')}"
        out = blocks.setdefault(case, [])
        out.append(f"// {group[0].global_text}\nnamespace {ns} {{\n")
        out.append(hoister.line("using G = ", cpp_type(labelled.cpp_fixy_global(g)), ";\n"))
        out.append(hoister.line("using RS = ", cpp_type(crash_reliable_set(roles, prefix)), ";\n"))
        for row in group:
            msg = _message(row)
            if row.family == "fixy.crash_live":
                out.append(f"static_assert(fs::crash_live_by_construction_v<G, RS> == {row.ours}, \"{msg}\");\n")
            else:
                role = int(row.role.split("/")[1])
                out.append(hoister.line(
                    "static_assert(",
                    cpp_type(f"std::is_same_v<fs::project_crash_t<G, {cpp_role(role)}, RS>, {_pin(row.ours)}>"),
                    f", \"{msg}\");\n"))
        out.append(f"}}  // namespace {ns}\n\n")
    return _static_shards(
        "fixy_crash",
        "//\n"
        "// Crash-stop projection and liveness of fixy/session.  Each global type is a\n"
        "// multiparty case with a crash branch on each transmission of an unreliable\n"
        "// sender.  Each row pins fixy's answer.  The golden file records how mpstk\n"
        "// judges the context of these projections when the unreliable roles crash.\n\n",
        "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
        "session_oracle::fixy_crash", ["".join(parts) for parts in blocks.values()])


def emit_enroute(rows: list[Row]) -> list[str]:
    """Return the shards of the runtime global type test.  O(rows).

    The unit of a shard is one case, with every runtime variant of it.
    """
    groups: dict[tuple[str, str], list[Row]] = {}
    for row in rows:
        if row.family in ENROUTE_FAMILIES and row.status != "gap" and not _hard_error(row):
            groups.setdefault((row.case, row.role.split("/")[0]), []).append(row)
    hoister = _Hoister()
    blocks: dict[str, list[str]] = {}
    for (case, tag) in sorted(groups, key=lambda k: (case_key(k[0]), k[1])):
        group = sorted(groups[(case, tag)], key=lambda r: (ENROUTE_FAMILIES.index(r.family), r.role))
        g = labelled.read(group[0].global_text)
        ns = f"{_namespace(case)}_{tag}"
        out = blocks.setdefault(case, [])
        out.append(f"// {group[0].global_text}\nnamespace {ns} {{\n")
        out.append(hoister.line("using G = ", cpp_type(labelled.cpp_fixy_global(g)), ";\n"))
        for row in group:
            msg = _message(row)
            if row.family == "fixy.enroute_live":
                out.append(f"static_assert(fs::is_live_by_construction_v<G> == {row.ours}, \"{msg}\");\n")
            elif row.family == "fixy.enroute_association":
                out.append(hoister.line("static_assert(", cpp_type(f"fs::association_holds_v<{row.oracle}, G>"),
                                        f" == {row.ours}, \"{msg}\");\n"))
            else:
                role = int(row.role.split("/")[1])
                out.append(hoister.line(
                    "static_assert(",
                    cpp_type(f"std::is_same_v<fs::project_t<G, {cpp_role(role)}>, {_pin(row.ours)}>"),
                    f", \"{msg}\");\n"))
        out.append(f"}}  // namespace {ns}\n\n")
    return _static_shards(
        "fixy_enroute",
        "//\n"
        "// Runtime global types: the first send of a multiparty case puts its\n"
        "// transmission en route, with every branch or with the chosen branch only.\n"
        "// Each row pins fixy's projection, its liveness claim, and whether the context\n"
        "// that the send reaches from the static projection associates with the runtime\n"
        "// type.\n\n",
        "#include <fixy/session/Liveness.h>\n#include <fixy/session/Projection.h>\n\n#include <type_traits>\n\n"
        + cpp_prelude() + "\nnamespace fs = ::fixy::session;\nnamespace fg = ::fixy::session::global;\n\n",
        "session_oracle::fixy_enroute", ["".join(parts) for parts in blocks.values()])


def wire_label(case: str, role: str) -> str:
    """Return the text that the wire test prints before the outcome of a row."""
    return f"session_oracle fixy.wire case {case} role {role}:"


WireEntry = tuple[str, str, str, int, str]


def wire_shards(blocks: list[list[WireEntry]], budget: int | None = None) -> list[str]:
    """Return the shards of a wire test for blocks of rows (label, endpoint A, endpoint B, seed, pinned outcome).

    A block stays in one shard.  ``budget`` is the size of one shard, and
    SHARD_BUDGET gives it when the argument is None.  Each shard holds the
    rows of its blocks and the function shard_rows_<k>, which gives them
    to main in shard 0.  Run with --measure, the program prints the
    outcome of each row.  Otherwise it compares each outcome with the
    pinned one.  O(entries).
    """
    hoister = _Hoister()
    texts: list[tuple[str, str]] = []
    row_number = 0
    for block in blocks:
        types: list[str] = []
        entries: list[str] = []
        for label, a, b, seed, pinned in block:
            ns = f"r{row_number}"
            types.append(f"// {label}\nnamespace {ns} {{\n")
            types.append(hoister.line("using A = ", cpp_type(a), ";\n"))
            types.append(hoister.line("using B = ", cpp_type(b), ";\n"))
            types.append(f"}}  // namespace {ns}\n")
            entries.append(f"    {{\"{label}\", &::session_oracle::wire::run_pair<{ns}::A, {ns}::B, {seed}>, "
                           f"\"{pinned}\"}},\n")
            row_number += 1
        texts.append(("".join(types), "".join(entries)))
    ranges = _partition([len(types) + len(entries) for types, entries in texts],
                        SHARD_BUDGET["fixy_wire"] if budget is None else budget)
    span = "std::span<const ::session_oracle::wire::Row>"
    declarations = "".join(f"{span} shard_rows_{k}();\n" for k in range(len(ranges)))
    shards: list[str] = []
    for index, part in enumerate(ranges):
        entries = "".join(texts[i][1] for i in part)
        out = [GENERATED_NOTICE, _shard_line("fixy_wire", index, len(ranges)), "//\n",
               "// End-to-end runs of keyed pairs over the session handle: the handle of T\n",
               "// against the handle of the dual of U, over one queue of words, for pairs\n",
               "// whose synchronous run is safe.  Each row pins the outcome of one run.\n\n",
               "#include \"wire_driver.h\"\n\n#include <fixy/session/Projection.h>\n\n#include <span>\n\n",
               cpp_prelude(), "\nnamespace fs = ::fixy::session;\n\n",
               "namespace session_oracle::fixy_wire {\n\n", *(texts[i][0] for i in part)]
        if entries:
            out.append(f"\nnamespace {{\n\nconstexpr ::session_oracle::wire::Row rows[] = {{\n{entries}}};\n\n"
                       "}  // namespace\n")
        body = "rows" if entries else "{}"
        out.append(f"\n// The rows of each shard of this test.\n{declarations}\n"
                   f"{span} shard_rows_{index}() {{ return {body}; }}\n\n"
                   "}  // namespace session_oracle::fixy_wire\n")
        if index == 0:
            calls = "".join(f"            ::session_oracle::fixy_wire::shard_rows_{k}(),\n" for k in range(len(ranges)))
            out.append("\nint main(int argc, char** argv) {\n    return ::session_oracle::wire::check_all(\n"
                       f"        {{\n{calls}        }},\n        argc, argv);\n}}\n")
        shards.append("".join(out))
    return shards


def emit_wire(rows: list[Row], budget: int | None = None) -> list[str]:
    """Return the shards of the wire test, with ``budget`` as in wire_shards.  O(rows).

    The unit of a shard is one keyed pair of a case: its two spellings and
    its two seeds.  The two seeds of a spelling run the same two handles.
    A case can hold more rows than one shard, so the unit is smaller than
    a case.  Two shards that hold pairs of one case both instantiate the
    endpoint that the pairs share.
    """
    blocks: dict[tuple[str, str], list[WireEntry]] = {}
    for row in sorted((r for r in rows if r.family in WIRE_FAMILIES and r.status != "gap"),
                      key=lambda r: (case_key(r.case), r.role)):
        a, b, seed = wire_pair(row.global_text, row.role)
        unit = (row.case, row.role.split("/")[0])
        blocks.setdefault(unit, []).append((wire_label(row.case, row.role), a, b, seed, row.ours))
    return wire_shards(list(blocks.values()), budget)


_STATIC_EMITTERS = {
    "fixy_duality": emit_fixy,
    "fixy_projection": emit_multi,
    "fixy_subtype": emit_subtype,
    "fixy_keyed_subtype": emit_keyed_subtype,
    "fixy_keyed_projection": emit_keyed_multi,
    "fixy_crash": emit_crash,
    "fixy_enroute": emit_enroute,
}


def emit_family(rows: list[Row], stem: str, wire_budget: int | None = None) -> list[str]:
    """Return the shards of the emitted test of ``stem``.

    ``wire_budget`` replaces the shard size of the wire test when it is not
    None.  Only the layout of the wire rows changes: the rows, their order
    and each case text stay the same.
    """
    if stem == "fixy_wire":
        return emit_wire(rows, wire_budget)
    return _STATIC_EMITTERS[stem](rows)


def emit_families(rows: list[Row], wire_budget: int | None = None) -> dict[str, list[str]]:
    """Return the shards of every emitted test, by the stem of the test, in the order of SHARD_BUDGET."""
    if tuple(SHARD_BUDGET) != (*_STATIC_EMITTERS, "fixy_wire"):
        raise RuntimeError("the emitters and SHARD_BUDGET must name the same tests in the same order")
    return {stem: emit_family(rows, stem, wire_budget) for stem in SHARD_BUDGET}


def _cmake_sources(families: dict[str, list[str]]) -> str:
    """Return the CMake file that lists the shards of each test."""
    out = [*(f"# {line}\n" for line in _NOTICE_LINES),
           "# The source files of each session oracle test.  Each file holds one shard of the rows of its test.\n"]
    for stem, shards in families.items():
        out.append(f"set(SESSION_ORACLE_{stem.upper()}_SOURCES\n")
        out += [f"    {shard_file(stem, index)}\n" for index in range(len(shards))]
        out.append(")\n")
    return "".join(out)


def emit_files(families: dict[str, list[str]]) -> dict[str, str]:
    """Return the file name and text of every emitted file: the shards of each test and the list of them."""
    files = {shard_file(stem, index): text for stem, shards in families.items() for index, text in enumerate(shards)}
    files[GENERATED_SOURCES] = _cmake_sources(families)
    return files


def emit_all(rows: list[Row]) -> dict[str, str]:
    """Return the file name and text of every file that the golden file emits."""
    return emit_files(emit_families(rows))
