"""guard check --rule mint-pattern — hold every §XXI mint to the five enforcement axes, from the parse tree.

CLAUDE.md §XXI: every mint factory is `[[nodiscard]] constexpr ... noexcept`
with a `requires` clause carrying the fit check.  This gate reads each axis off
the AST.  The enclosing `template_declaration` tells whether a mint is a
template, at any distance from the signature.

ONE CLAUSE, TWO QUESTIONS.  §XXI's sentence carries two claims about the
constraint, so the gate reads two axes:

  * PRESENCE — is there a type-level constraint at all?  C++ spells one
    constraint two ways: `template <typename T> requires C<T>` and
    `template <C T>`, and the gate reads both.
  * CONTEXT FIT — for a ctx-bound mint, does the constraint gate the CONTEXT?
    A clause naming some other parameter does not.  A mint that constrains
    only its channel surface accepts every context equally, which is the
    opposite of what §XXI claims a ctx-bound mint verifies.

A single axis would let a surface constraint stand in for a context gate.

THE CONSTEXPR AXIS HAS TWO EXCEPTIONS.  §XXI drops `constexpr` from a factory
that genuinely allocates, and from a factory whose body can never be constant
evaluated because it reads runtime state: an atomic, a thread identity or a
system call.  The build refuses `constexpr` on the second kind through
-Winvalid-constexpr.  This gate cannot see which kind a body is, so each
exception is stated, and the statement is the exemption.

THREE EXEMPTION MECHANISMS, EACH FOR NAMED AXES:

  * a row `PATH | AXIS-ok | admit | MINT | REASON` in
    `utils/ledgers/mint-pattern.txt` (assay.core.ledger).  A row names the
    mint, never a line, and it exempts one axis for every overload of that
    name in that file.  A row can exempt any axis.
  * a `// §XXI carve-out: cx=alloc` or `rq=pre` comment in the comment run
    directly above the signature.  cx=alloc exempts constexpr, and rq=pre
    exempts requires.
  * a `// MINT-PATTERN-OK: <reason>` marker on the line that names the mint.
    It exempts constexpr and no other axis.  §XXI admits no exemption from
    noexcept, and a soundness axis (requires, ctxfit) takes a reviewed row.

AN EXEMPTION THAT EXEMPTS NOTHING FAILS, so a dead one cannot come to hide a
later regression:

  * a row whose mint does not fall short on its axis is stale
  * a row whose every short site carries its own marker is redundant
  * a marker on a site that a row already covers is redundant
  * a marker on a mint that meets its axis is dead
  * a marker that sits on no mint signature is dangling, because the model
    never reads it
  * a row that names no axis or an unknown axis, names a line, or names no
    mint is malformed
  * a ledger that does not obey the rows grammar, such as a ledger with a
    row that repeats another row or a row with no reason, is malformed

A FILE THE PARSER CANNOT READ FAILS.  Its mints are unknown, so a clean verdict
over it would be a guess.  The surface comes from `tsast.cpp_files`, which
leaves out the files that `tsast.UNPARSEABLE` names, because they are not C++.

EXIT CODES
    0  every mint meets the five axes, or one stated mechanism exempts it
    1  a mint falls short with nothing exempting it, or a file does not parse
    2  a row is stale, redundant or malformed, the ledger is malformed, or a
       marker is dead, redundant or dangling, or the self-test fails
    3  the pinned tree-sitter kit is not installed
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import rows as rows_grammar
from assay.loop.derive import mint_model as mintmodel
from assay.readers import tsast

RULE = "mint-pattern"
LEDGER = rule_ledger.shown_path(RULE)
# The suffix of the kind of a ledger row: the kind `constexpr-ok` exempts the axis constexpr.
AXIS_SUFFIX = "-ok"

# The axes, each with the kind of the ledger row that exempts it.  `requires` asks only
# whether a constraint is PRESENT; `ctxfit` asks the separate question of whether
# it gates the context, which is the one §XXI actually cares about for a ctx-bound
# mint.  Folding the two reported a constrained mint as unconstrained, and read a
# mint that gates nothing about its context as compliant the moment it constrained
# some other parameter.
AXES = ("nodiscard", "constexpr", "noexcept", "requires", "ctxfit")

# The in-source markers, each with the one axis it exempts.
MARKERS = {
    "inline": (mintmodel.INLINE_OK, "constexpr"),
    "cx": (mintmodel.CARVE_OUT_CX, "constexpr"),
    "rq": (mintmodel.CARVE_OUT_RQ, "requires"),
}
MINT_NAME = re.compile(r"mint_[A-Za-z0-9_]*[A-Za-z0-9]")

Row = tuple[str, str, str]


@dataclass(frozen=True)
class Marker:
    """One in-source exemption marker, at the line that holds its text."""

    path: str
    line: int
    kind: str


@dataclass(frozen=True)
class Finding:
    """One result of the gate.

    Attributes:
        kind: `violation` or `parse` exit 1, every other kind exits 2
        where: `path:line`, or the row text for a row finding
        name: The mint the finding names, or the marker kind for a dangling one
        message: The report line
    """

    kind: str
    where: str
    name: str
    message: str


FAILING_KINDS = frozenset({"violation", "parse"})


def row_text(path: str, axis_kind: str, name: str) -> str:
    """Return the start of a ledger row, as the file holds it, with no reason: the text to search for."""
    return rows_grammar.render([path or rows_grammar.EMPTY_CELL], [axis_kind, rule_ledger.ADMIT_VALUE, name])


def read_ledger(file: Path | None = None) -> tuple[set[Row], list[Finding]]:
    """Return the rows of the ledger and one finding for each malformed row.

    A row is `PATH | AXIS-ok | admit | MINT | REASON`.  The key is content,
    never a line number, so an edit above a site cannot drift it.

    Complexity: linear in the length of the file.

    Args:
        file: A planted ledger file, or None for utils/ledgers/mint-pattern.txt

    Returns:
        The rows as (path, mint name, axis) triples, and the malformed rows

    Raises:
        rows_grammar.RowsError: If the ledger does not obey the rows grammar, for example a key with a second row
            or a row with no reason
    """
    ledger = rule_ledger.read(rule_ledger.rule_of(RULE), file=file)
    shown = Path(LEDGER if file is None else str(file)).name
    axes = ", ".join(axis + AXIS_SUFFIX for axis in AXES)
    rows: set[Row] = set()
    problems: list[Finding] = []
    for key, entry in sorted(ledger.base.items(), key=lambda item: item[1].line):
        where = f"{shown}:{entry.line}"
        text = row_text(key.path, key.kind, key.entity)
        axis = key.kind.removesuffix(AXIS_SUFFIX)
        reason = ""
        if not key.path:
            reason = "a row names the file of the mint in its PATH"
        elif key.entity.isdigit():
            reason = "a row names the mint, never a line"
        elif MINT_NAME.fullmatch(key.entity) is None:
            reason = f"`{key.entity}` is not the name of a mint"
        elif not key.kind.endswith(AXIS_SUFFIX) or axis not in AXES:
            reason = f"`{key.kind or rows_grammar.EMPTY_CELL}` names no axis.  The kind of a row is one of {axes}"
        if reason:
            problems.append(Finding("malformed", where, text,
                                    f"MINT-PATTERN malformed row {where}: {text} — {reason}."))
            continue
        rows.add((key.path, key.entity, axis))
    return rows, problems


def shortfalls(mint: mintmodel.Mint) -> list[str]:
    """Return the axes this mint does not meet.

    The `requires` axis applies only to a templated mint, because a constraint
    constrains a template and a non-template cannot carry one.  It is satisfied by
    a clause OR by a concept on a template parameter, which are two spellings of
    one thing.

    The `ctxfit` axis applies only to a ctx-bound mint, and asks whether any
    constraint gates the context.  A token mint holds no context to gate.

    Args:
        mint: The mint to judge

    Returns:
        The axis names that fall short, in a stable order
    """
    missing: list[str] = []
    if not mint.nodiscard:
        missing.append("nodiscard")
    if not mint.constexpr and mintmodel.constexpr_applies(mint):
        missing.append("constexpr")
    if not mint.noexcept_:
        missing.append("noexcept")
    if mintmodel.requires_applies(mint) and not mintmodel.has_fit_constraint(mint):
        missing.append("requires")
    if mintmodel.ctxfit_applies(mint) and not mint.ctx_gated:
        missing.append("ctxfit")
    return missing


def marker_kinds(mint: mintmodel.Mint) -> list[str]:
    """Return the kinds of in-source marker that the model reads on this mint.

    Args:
        mint: The mint in question

    Returns:
        A subset of `inline`, `cx` and `rq`, in that order
    """
    flags = {"inline": mint.inline_ok, "cx": mint.carve_out_cx, "rq": mint.carve_out_rq}
    return [kind for kind, present in flags.items() if present]


def marker_axes(mint: mintmodel.Mint) -> set[str]:
    """Return the axes that an in-source marker exempts on this mint.

    Args:
        mint: The mint in question

    Returns:
        The exempted axes
    """
    return {MARKERS[kind][1] for kind in marker_kinds(mint)}


def exempted(mint: mintmodel.Mint, axis: str, rows: set[Row]) -> bool:
    """Report whether a stated mechanism exempts this mint on this axis.

    Args:
        mint: The mint in question
        axis: One of the five axis names
        rows: The ledger rows

    Returns:
        True when a row or a marker for that axis covers it
    """
    return (mint.path, mint.name, axis) in rows or axis in marker_axes(mint)


def find_markers(tree: tsast.Tree, sites: list[mintmodel.Mint]) -> list[Marker]:
    """Return every marker of one file that attaches to no mint signature.

    An inline marker attaches when its comment sits on the line that names a
    mint.  A carve-out attaches when it sits in the comment run directly above
    such a line.  Those are the two places the model reads, so a marker
    anywhere else exempts nothing.  A marker inside a string literal is no
    marker.

    Complexity: linear in the length of the file.

    Args:
        tree: The parsed file
        sites: The mint sites of the file, declarations included

    Returns:
        The dangling markers, in source order
    """
    named = {site.line for site in sites}
    above = {number for site in sites for number in site.carve_lines}
    dangling: list[Marker] = []
    for kind, (needle, _axis) in MARKERS.items():
        for row in sorted(mintmodel.marker_rows(tree, needle)):
            line = row + 1
            attached = line in named if kind == "inline" else line in above
            if not attached:
                dangling.append(Marker(str(tree.path), line, kind))
    return sorted(dangling, key=lambda marker: (marker.line, marker.kind))


def violation_text(mint: mintmodel.Mint, axis: str) -> str:
    """Return the report line for an unexempted shortfall.

    Args:
        mint: The mint that falls short
        axis: The axis it falls short on

    Returns:
        The report line, with each way to state an exemption for that axis
    """
    if axis == "ctxfit":
        lack = (
            f"takes a context ({mint.ctx_token}) that no constraint gates. "
            f"It accepts every context in the tree. Write a "
            f"`CtxFits...<..., {mint.ctx_token}>` conjunct into its "
            f"constraint, or state the reason"
        )
    else:
        lack = f"is missing {axis}. Add it to the signature, or state the reason"
    row = f"a row `{row_text(mint.path, axis + AXIS_SUFFIX, mint.name)} | REASON` in {LEDGER}"
    ways = f"an inline `// MINT-PATTERN-OK: <reason>` on the signature line, or {row}" \
        if axis == "constexpr" else row
    return f"MINT-PATTERN violation: {mint.path}:{mint.line} — {mint.name} {lack}: {ways}."


def evaluate(mints: list[mintmodel.Mint], rows: set[Row], dangling: list[Marker]) -> list[Finding]:
    """Judge every mint, every row and every marker.

    Complexity: O(m + r) in the mint count and the row count.

    Args:
        mints: The merged mints of the surface
        rows: The ledger rows
        dangling: The markers that attach to no mint signature

    Returns:
        Every finding, in a stable order
    """
    findings: list[Finding] = []
    covered: dict[Row, list[mintmodel.Mint]] = {}
    for mint in sorted(mints, key=lambda m: (m.path, m.line, m.name)):
        where = f"{mint.path}:{mint.line}"
        short = shortfalls(mint)
        for axis in short:
            row = (mint.path, mint.name, axis)
            if row in rows:
                covered.setdefault(row, []).append(mint)
            elif not exempted(mint, axis, rows):
                findings.append(Finding("violation", where, mint.name, violation_text(mint, axis)))
        kinds = marker_kinds(mint)
        for kind in kinds:
            axis = MARKERS[kind][1]
            if axis not in short:
                findings.append(Finding(
                    "dead-marker", where, mint.name,
                    f"MINT-PATTERN dead marker: {where} — the `{MARKERS[kind][0]}` marker on {mint.name} "
                    f"exempts {axis}, and the mint meets {axis}. Delete the marker."))
        if "inline" in kinds and "cx" in kinds and "constexpr" in short:
            findings.append(Finding(
                "redundant-marker", where, mint.name,
                f"MINT-PATTERN redundant marker: {where} — {mint.name} carries an inline marker and a cx=alloc "
                f"carve-out, and each exempts constexpr. Delete one of them."))

    for row in sorted(rows):
        path, name, axis = row
        sites = covered.get(row, [])
        text = row_text(path, axis + AXIS_SUFFIX, name)
        if not sites:
            findings.append(Finding(
                "stale-row", text, name,
                f"MINT-PATTERN stale row: {text} — no live {axis} shortfall for this mint. It was fixed, "
                f"renamed or moved. Delete the row."))
        elif all(axis in marker_axes(site) for site in sites):
            findings.append(Finding(
                "redundant-row", text, name,
                f"MINT-PATTERN redundant row: {text} — every site it covers carries its own marker for "
                f"{axis}, so the row exempts nothing. Delete the row, or delete the markers."))
        else:
            for site in sites:
                if axis in marker_axes(site):
                    findings.append(Finding(
                        "redundant-marker", f"{site.path}:{site.line}", name,
                        f"MINT-PATTERN redundant marker: {site.path}:{site.line} — the row {text} already "
                        f"exempts {name} on {axis}. Delete the marker."))

    for marker in dangling:
        needle = MARKERS[marker.kind][0]
        findings.append(Finding(
            "dangling-marker", f"{marker.path}:{marker.line}", marker.kind,
            f"MINT-PATTERN dangling marker: {marker.path}:{marker.line} — the `{needle}` marker sits on no "
            f"mint signature, so it exempts nothing. Delete it, or move it to the line that names the mint."))
    return findings


def run(files: list[Path], ledger_file: Path | None = None) -> int:
    """Scan the files, print every finding, and return the exit code.

    Args:
        files: The surface to scan
        ledger_file: A planted ledger file, or None for utils/ledgers/mint-pattern.txt

    Returns:
        0 when clean, 1 on a violation or a parse failure, 2 on a malformed ledger or on any other finding

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    try:
        rows, malformed = read_ledger(ledger_file)
    except rows_grammar.RowsError as exc:
        print(f"MINT-PATTERN malformed ledger: {exc}.", file=sys.stderr)
        return 2
    findings: list[Finding] = []
    dangling: list[Marker] = []

    def on_tree(tree: tsast.Tree, sites: list[mintmodel.Mint]) -> None:
        """Record a parse failure and the dangling markers of one file."""
        if tree.diagnostic is not None:
            findings.append(Finding(
                "parse", str(tree.path), "",
                f"MINT-PATTERN parse failure: {tree.path} — the parser cannot read this file, so its mints "
                f"are unknown.\n  {tree.diagnostic}"))
        dangling.extend(find_markers(tree, sites))

    mints = mintmodel.collect(files, on_tree=on_tree)
    findings += malformed + evaluate(mints, rows, dangling)
    for finding in findings:
        print(finding.message, file=sys.stderr)
    failing = sum(finding.kind in FAILING_KINDS for finding in findings)
    if findings:
        print(f"\ncheck-mint-pattern: {len(findings)} finding(s) across {len(mints)} mints: {failing} "
              f"violation(s) or parse failure(s), {len(findings) - failing} dead or malformed exemption(s).",
              file=sys.stderr)
        return 1 if failing else 2
    print(f"check-mint-pattern: clean — {len(mints)} mints meet the {len(AXES)} §XXI axes or carry one "
          f"stated exemption, and every exemption exempts something.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Exercise the axis, row and marker logic, positive and negative.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def check(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it.

        Args:
            name: What the case asserts
            ok: Whether it held
            negative: Whether the case plants something the gate must refuse
        """
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    def make(**kwargs: object) -> mintmodel.Mint:
        """Build a Mint for a case, compliant except where overridden.

        Args:
            kwargs: Fields to override

        Returns:
            The planted mint
        """
        fields: dict[str, object] = dict(
            name="mint_probe", path="p.h", line=1, owner=None, namespace="probe",
            inline_ok=False, nodiscard=True, constexpr=True, noexcept_=True,
            requires_=True, constraint_concepts=frozenset(), ctx_token="Ctx",
            ctx_gated=True, shape="ctx", templated=True,
            carve_out_cx=False, carve_out_rq=False, borrow_projection=False,
        )
        fields.update(kwargs)
        return mintmodel.Mint(**fields)  # type: ignore[arg-type]

    print("check-mint-pattern --self-test")

    check("a compliant mint has no shortfall", shortfalls(make()) == [])
    check("a missing nodiscard is a shortfall", shortfalls(make(nodiscard=False)) == ["nodiscard"], True)
    check("a missing noexcept is a shortfall", shortfalls(make(noexcept_=False)) == ["noexcept"], True)
    check(
        "all five can fall short at once",
        shortfalls(make(nodiscard=False, constexpr=False, noexcept_=False, requires_=False, ctx_gated=False))
        == ["nodiscard", "constexpr", "noexcept", "requires", "ctxfit"],
        True,
    )
    # The rule the bash window got wrong: requires applies only to a template.
    check("a templated mint with no constraint falls short",
          shortfalls(make(requires_=False, templated=True)) == ["requires"], True)
    check("a non-template with no clause does NOT fall short",
          shortfalls(make(requires_=False, templated=False)) == [])
    check("a constrained template parameter satisfies requires",
          shortfalls(make(requires_=False, constraint_concepts=frozenset({"Surface"}))) == [])
    # The presence axis is not the context axis.
    check("a parameter constraint does NOT satisfy the context axis",
          shortfalls(make(requires_=False, ctx_gated=False, constraint_concepts=frozenset({"Surface"})))
          == ["ctxfit"], True)
    check("a token mint is not asked to gate a context",
          shortfalls(make(shape="token", ctx_token=None, ctx_gated=False)) == [])
    check("a member mint is not asked to gate a context",
          shortfalls(make(shape="member", ctx_token=None, ctx_gated=False)) == [])
    check("a borrow projection is not asked for constexpr",
          shortfalls(make(constexpr=False, borrow_projection=True)) == [])
    check("a borrow projection is still held to its other axes",
          shortfalls(make(nodiscard=False, requires_=False, borrow_projection=True)) == ["nodiscard", "requires"],
          True)

    # Each mechanism exempts its own axes and no other.
    row = {("p.h", "mint_probe", "constexpr")}
    check("a row exempts its axis", exempted(make(constexpr=False), "constexpr", row))
    check("a row does not exempt another axis", not exempted(make(noexcept_=False), "noexcept", row), True)
    check("a row keyed to another file does not exempt",
          not exempted(make(path="q.h", constexpr=False), "constexpr", row), True)
    check("a cx carve-out exempts constexpr", exempted(make(constexpr=False, carve_out_cx=True), "constexpr", set()))
    check("a cx carve-out does not exempt requires",
          not exempted(make(requires_=False, carve_out_cx=True), "requires", set()), True)
    check("an rq carve-out exempts requires", exempted(make(requires_=False, carve_out_rq=True), "requires", set()))
    check("an rq carve-out does not exempt ctxfit",
          not exempted(make(ctx_gated=False, carve_out_rq=True), "ctxfit", set()), True)
    check("an inline marker exempts constexpr", exempted(make(constexpr=False, inline_ok=True), "constexpr", set()))
    for axis, field in (("noexcept", "noexcept_"), ("nodiscard", "nodiscard"), ("requires", "requires_"),
                        ("ctxfit", "ctx_gated")):
        check(f"an inline marker does not exempt {axis}",
              not exempted(make(inline_ok=True, **{field: False}), axis, set()), True)
    check("nothing exempts a bare shortfall", not exempted(make(constexpr=False), "constexpr", set()), True)

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        allow = root / "allow.txt"

        def plant(rows: list[tuple[Path, str, str]]) -> None:
            """Write the planted ledger: one row (file, kind, mint) for each item, each with a planted reason."""
            keys = [rule_ledger.Key(str(path), kind, name) for path, kind, name in rows]
            base = {key: rule_ledger.Entry(key, rule_ledger.ADMIT_VALUE, "planted") for key in keys}
            rule_ledger.store(rule_ledger.Ledger(RULE, rule_ledger.rule_of(RULE).policy, "# planted\n", base=base),
                              file=allow)

        # The grammar of a row.
        header = f"# a comment\n\nformat 1\nrule {RULE}\n"
        allow.write_text(
            header
            + "a.h | constexpr-ok | admit | mint_good | a reason\n"
            "a.h | - | admit | mint_bare | a reason\n"
            "a.h | constexpr-ok | admit | 120 | a reason\n"
            "a.h | constexr-ok | admit | mint_typo | a reason\n"
            "a.h | constexpr-ok | admit | helper | a reason\n"
            "a.h | constexpr-ok | admit | mint_space trailing | a reason\n",
            encoding="utf-8",
        )
        rows, malformed = read_ledger(allow)
        texts = " ".join(finding.message for finding in malformed)
        check("a well-formed row is read", rows == {("a.h", "mint_good", "constexpr")})
        check("a row with no axis is malformed", "a.h | - | admit | mint_bare — `-` names no axis" in texts, True)
        check("a row keyed by a line is malformed", "names the mint, never a line" in texts, True)
        check("a row with an unknown axis is malformed", "`constexr-ok` names no axis" in texts, True)
        check("a row that names no mint is malformed", "`helper` is not the name of a mint" in texts, True)
        check("a row with a space in its mint is malformed", "`mint_space trailing` is not the name" in texts, True)
        allow.write_text(header + "a.h | constexpr-ok | admit | mint_good | a reason\n"
                         "a.h | constexpr-ok | admit | mint_good | the same key\n", encoding="utf-8")
        try:
            read_ledger(allow)
            second = ""
        except rows_grammar.RowsError as exc:
            second = str(exc)
        check("a second copy of a row is malformed", "allow.txt:6: the key" in second and "second row" in second,
              True)

        # The whole scan, over a planted surface.
        live = root / "live.h"
        live.write_text(
            "#pragma once\n"
            "#include <atomic>\n"
            "namespace probe {\n"
            "inline std::atomic<int> seal{0};\n"
            "// The body reads an atomic, so no constant evaluation can run it.\n"
            "[[nodiscard]] inline int mint_runtime_read() noexcept { return seal.load(); }\n"
            "[[nodiscard]] inline int mint_forgot_constexpr() noexcept { return 42; }\n"
            "[[nodiscard]] constexpr int mint_compliant() noexcept { return 7; }\n"
            "// §XXI carve-out: cx=alloc — dead, because the mint is constexpr.\n"
            "[[nodiscard]] constexpr int mint_dead_carve_out() noexcept { return 1; }\n"
            "[[nodiscard]] constexpr int mint_dead_inline() noexcept { return 2; }  // MINT-PATTERN-OK: dead\n"
            "[[nodiscard]] inline int mint_inline_short(int value) { return value; }  // MINT-PATTERN-OK: runtime\n"
            "// §XXI carve-out: cx=alloc — this one allocates.\n"
            "[[nodiscard]] inline int* mint_marked_alloc() noexcept { return new int{0}; }\n"
            "[[nodiscard]] inline int* mint_marked_twice() noexcept { return new int{0}; }  // MINT-PATTERN-OK: x\n"
            "[[nodiscard]] inline int mint_overload(int value) noexcept { return value; }  // MINT-PATTERN-OK: x\n"
            "[[nodiscard]] inline int mint_overload(long value) noexcept { return static_cast<int>(value); }\n"
            "inline int not_a_mint = 0;  // MINT-PATTERN-OK: this marker sits on no mint\n"
            "/* §XXI carve-out: cx=alloc — a block comment, which the model does not read */\n"
            "[[nodiscard]] constexpr int mint_after_block() noexcept { return 3; }\n"
            'inline const char* text = "// MINT-PATTERN-OK: inside a string";\n'
            "// §XXI carve-out: cx=alloc — two markers for one axis.\n"
            "[[nodiscard]] inline int* mint_two_markers() noexcept { return new int{0}; }  // MINT-PATTERN-OK: x\n"
            "[[nodiscard]] inline int mint_string_marker() noexcept { return sizeof(\"MINT-PATTERN-OK: text\"); }\n"
            "inline constexpr char note[] = R\"(\n"
            "// §XXI carve-out: rq=pre )\";\n"
            "template <typename T> [[nodiscard]] constexpr int mint_raw_trap(T) noexcept { return 1; }\n"
            "}\n",
            encoding="utf-8",
        )
        plant([(live, "constexpr-ok", name)
               for name in ("mint_runtime_read", "mint_marked_twice", "mint_overload", "mint_compliant")])
        dangling: list[Marker] = []
        mints = mintmodel.collect([live], on_tree=lambda tree, sites: dangling.extend(find_markers(tree, sites)))
        rows, malformed = read_ledger(allow)
        found = evaluate(mints, rows, dangling)

        def has(kind: str, name: str) -> bool:
            """Report whether a finding of one kind names one mint."""
            return any(f.kind == kind and f.name == name for f in found)

        def mentions(kind: str, text: str) -> bool:
            """Report whether a finding of one kind holds a text."""
            return any(f.kind == kind and text in f.message for f in found)

        check("the planted rows parse", not malformed)
        check("a runtime-only mint is admitted by its row",
              not any(f.name == "mint_runtime_read" for f in found))
        check("a constexpr-able mint that lacks constexpr is refused", has("violation", "mint_forgot_constexpr"), True)
        check("a row on a mint that has constexpr is stale", has("stale-row", "mint_compliant"), True)
        check("a cx carve-out admits its mint", not any(f.name == "mint_marked_alloc" for f in found))
        check("an inline marker does not exempt a missing noexcept",
              mentions("violation", "mint_inline_short is missing noexcept"), True)
        check("an inline marker still exempts constexpr",
              not mentions("violation", "mint_inline_short is missing constexpr"))
        check("a cx carve-out on a constexpr mint is dead", has("dead-marker", "mint_dead_carve_out"), True)
        check("an inline marker on a constexpr mint is dead", has("dead-marker", "mint_dead_inline"), True)
        check("a row whose every site carries a marker is redundant", has("redundant-row", "mint_marked_twice"), True)
        check("a marker on a site that a needed row covers is redundant",
              has("redundant-marker", "mint_overload") and not has("redundant-row", "mint_overload"), True)
        check("two markers for one axis on one mint are redundant", has("redundant-marker", "mint_two_markers"), True)
        check("a marker on no signature is dangling", mentions("dangling-marker", f"{live}:18"), True)
        check("a carve-out in a block comment is dangling", mentions("dangling-marker", f"{live}:19"), True)
        check("a marker inside a string literal is no marker", not mentions("dangling-marker", f"{live}:21"))
        check("a marker text in a string literal on the signature line exempts nothing",
              mentions("violation", "mint_string_marker is missing constexpr"), True)
        check("a raw string line that starts with // is no rq carve-out",
              mentions("violation", "mint_raw_trap is missing requires"), True)
        check("a raw string line that starts with // is no dangling marker",
              not mentions("dangling-marker", f"{live}:26"))

        def captured(action) -> tuple[int, str]:
            """Run an action and return its code and its stderr."""
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = action()
            return code, buffer.getvalue()

        clean = root / "clean.h"
        clean.write_text(
            "#pragma once\n#include <atomic>\nnamespace probe_clean {\n"
            "inline std::atomic<int> seal{0};\n"
            "[[nodiscard]] inline int mint_runtime_read() noexcept { return seal.load(); }\n"
            "[[nodiscard]] constexpr int mint_compliant() noexcept { return 7; }\n}\n",
            encoding="utf-8",
        )
        runtime_row = (clean, "constexpr-ok", "mint_runtime_read")
        plant([runtime_row])
        check("a clean surface exits 0", captured(lambda: run([clean], allow))[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: run([clean], allow))
        finally:
            os.chdir(previous)
        check("the report from / equals the report from the repository", from_slash == captured(
            lambda: run([clean], allow)))
        plant([])
        check("a runtime-only mint with no row exits 1", captured(lambda: run([clean], allow))[0] == 1, True)
        plant([runtime_row, (clean, "constexpr-ok", "mint_compliant")])
        check("a stale row exits 2", captured(lambda: run([clean], allow))[0] == 2, True)
        plant([runtime_row, (clean, "", "mint_extra")])
        check("a malformed row exits 2", captured(lambda: run([clean], allow))[0] == 2, True)
        allow.write_text(f"format 1\nrule {RULE}\n{clean} | constexpr-ok | admit | mint_runtime_read | -\n",
                         encoding="utf-8")
        code, report = captured(lambda: run([clean], allow))
        check("a ledger with a row with no reason exits 2", code == 2 and "malformed ledger" in report, True)
        broken = root / "broken.h"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        plant([runtime_row])
        code, report = captured(lambda: run([clean, broken], allow))
        check("a file the parser cannot read exits 1", code == 1 and "parse failure" in report, True)

    if failures:
        print(f"check-mint-pattern --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print(f"check-mint-pattern --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The words after the rule ID

    Returns:
        The exit code
    """
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == []:
            return run(mintmodel.surface_files())
    except tsast.KitMissing as exc:
        print(f"check-mint-pattern: {exc}", file=sys.stderr)
        return 3
    print("usage: guard check --rule mint-pattern [--self-test]", file=sys.stderr)
    return 2
