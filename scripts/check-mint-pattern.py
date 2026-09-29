#!/usr/bin/env python3
"""Hold every §XXI mint to the five enforcement axes, from the parse tree.

CLAUDE.md §XXI: every mint factory is `[[nodiscard]] constexpr ... noexcept`
with a `requires` clause carrying the fit check.  This gate reads each axis off
the AST rather than off a line window, which is what `check-mint-pattern.sh`
does in 1,173 lines of bash.

ONE CLAUSE, TWO QUESTIONS.  §XXI's sentence carries two claims about the
constraint, and reading it as one axis got both wrong in opposite directions.

  * PRESENCE — is there a type-level constraint at all?  C++ spells one
    constraint two ways: `template <typename T> requires C<T>` and
    `template <C T>`.  Reading only the keyword called 49 of the tree's 332
    mints unconstrained when every one carried a concept on a parameter, and
    50 allowlist entries existed to excuse that misreading.
  * CONTEXT FIT — for a ctx-bound mint, does the constraint gate the CONTEXT?
    A clause naming some other parameter does not.  Twenty-six session mints
    constrain their channel surface and accept `HotFgCtx`, `ColdInitCtx` and
    every other context equally, which is the opposite of what §XXI claims a
    ctx-bound mint verifies.

So the two are separate axes.  Folding them let a surface constraint stand in
for a context gate, and fixing the presence blind spot alone would have retired
all 50 entries and left those 26 sites recorded nowhere.

WHAT THE WINDOW COST.  The bash guard decides whether the `requires` axis
applies by walking fifteen lines up from the signature looking for the word
`template`.  In `permissions/FederationPermission.h` the template parameter list
sits at line 288 and the signature at line 305 — seventeen lines.  The guard
finds no template, skips the axis, and `mint_federation_admittance` passes with
no `requires` clause at all.  The window fails open.  The enclosing
`template_declaration` answers the same question exactly, at any distance.

THE CONSTEXPR AXIS HAS TWO EXCEPTIONS.  §XXI drops `constexpr` from a factory
that genuinely allocates, and from a factory whose body can never be constant
evaluated because it reads runtime state: an atomic, a thread identity or a
system call.  The build refuses `constexpr` on the second kind through
-Winvalid-constexpr.  This gate cannot see which kind a body is, so each
exception is stated, and the statement is the exemption.

THREE EXEMPTION MECHANISMS, EACH FOR NAMED AXES:

  * a row `path:mint_name:axis-ok` in `scripts/mint-pattern-allowlist.txt`.
    A row names the mint, never a line, and it exempts one axis for every
    overload of that name in that file.  A row can exempt any axis.
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
  * a row that does not parse, names an unknown axis, names a line, or
    repeats another row is malformed

A marker in a file under a prefix of `scripts/frozen-paths.txt` is not held to
the last three rules.  A frozen file cannot change, so its mints cannot lose a
flag, and its dead marker leaves with the old tree.  A redundant row that names
a frozen file still fails, because the row is the half that can be removed.

SUPERSEDED HEADERS ARE OUT OF SCOPE.  The bash guard scanned
`include/crucible/**/_*.h`, the ported old-substrate headers, and 24 allowlist
entries existed only to exempt them.  Those files are frozen, so a shortfall
there cannot be repaired, and they go with the old tree.  The inventory
generator excludes them too.

A FILE THE PARSER CANNOT READ FAILS.  Its mints are unknown, so a clean verdict
over it would be a guess.  The surface comes from `tsast.cpp_files`, which
leaves out the files that `tsast.UNPARSEABLE` names, because they are not C++.

EXIT CODES
    0  every mint meets the five axes, or one stated mechanism exempts it
    1  a mint falls short with nothing exempting it, or a file does not parse
    2  a row is stale, redundant or malformed, or a marker is dead, redundant
       or dangling, or the self-test fails
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

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mintmodel  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

ALLOWLIST = tsast.REPO_ROOT / "scripts" / "mint-pattern-allowlist.txt"
FROZEN_PATHS = tsast.REPO_ROOT / "scripts" / "frozen-paths.txt"

# The axes, each with the allowlist suffix that exempts it.  `requires` asks only
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


def read_allowlist(path: Path = ALLOWLIST) -> tuple[set[Row], list[Finding]]:
    """Return the rows of the allowlist and one finding for each malformed row.

    A row is `path:mint_name:axis-ok`.  The key is content, never a line
    number, so an edit above a site cannot drift it.

    Complexity: linear in the length of the file.

    Args:
        path: The allowlist file

    Returns:
        The rows as (path, mint name, axis) triples, and the malformed rows
    """
    rows: set[Row] = set()
    problems: list[Finding] = []
    if not path.is_file():
        return rows, problems
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        where = f"{path.name}:{number}"
        parts = line.split(":")
        reason = ""
        if len(parts) != 3 or not parts[0] or any(ch.isspace() for ch in line):
            reason = "a row is `path:mint_name:axis-ok`, with no space"
        elif parts[1].isdigit():
            reason = "a row names the mint, never a line"
        elif MINT_NAME.fullmatch(parts[1]) is None:
            reason = f"`{parts[1]}` is not the name of a mint"
        elif not parts[2].endswith("-ok") or parts[2].removesuffix("-ok") not in AXES:
            reason = f"`{parts[2]}` names no axis; the axes are {', '.join(a + '-ok' for a in AXES)}"
        if reason:
            problems.append(Finding("malformed", where, line, f"MINT-PATTERN malformed row {where}: {line} — {reason}."))
            continue
        row = (parts[0], parts[1], parts[2].removesuffix("-ok"))
        if row in rows:
            problems.append(Finding("malformed", where, line,
                                    f"MINT-PATTERN malformed row {where}: {line} — a row before this one says the same."))
            continue
        rows.add(row)
    return rows, problems


def read_frozen(path: Path = FROZEN_PATHS) -> tuple[str, ...]:
    """Return the frozen path prefixes.

    Args:
        path: The list of the frozen prefixes, scripts/frozen-paths.txt

    Returns:
        One prefix for each live line
    """
    if not path.is_file():
        return ()
    return tuple(line.strip() for line in path.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.strip().startswith("#"))


def is_frozen(path: str, prefixes: tuple[str, ...]) -> bool:
    """Report whether a path lies under a frozen prefix.

    Args:
        path: The path of a scanned file
        prefixes: The frozen prefixes

    Returns:
        True when one prefix starts the path
    """
    return any(path.startswith(prefix) for prefix in prefixes)


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
        rows: The allowlist rows

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
    row = f"a row `{mint.path}:{mint.name}:{axis}-ok` in scripts/mint-pattern-allowlist.txt"
    ways = f"an inline `// MINT-PATTERN-OK: <reason>` on the signature line, or {row}" \
        if axis == "constexpr" else row
    return f"MINT-PATTERN violation: {mint.path}:{mint.line} — {mint.name} {lack}: {ways}."


def evaluate(
    mints: list[mintmodel.Mint],
    rows: set[Row],
    frozen: tuple[str, ...],
    dangling: list[Marker],
) -> list[Finding]:
    """Judge every mint, every row and every marker.

    Complexity: O(m + r) in the mint count and the row count.

    Args:
        mints: The merged mints of the surface
        rows: The allowlist rows
        frozen: The frozen path prefixes
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
        if is_frozen(mint.path, frozen):
            continue
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
        text = f"{path}:{name}:{axis}-ok"
        if not sites:
            findings.append(Finding(
                "stale-row", text, name,
                f"MINT-PATTERN stale row: {text} — no live {axis} shortfall for this mint. It was fixed, "
                f"renamed, moved, or its file is a superseded `_*.h` header this gate does not scan. "
                f"Delete the row."))
        elif all(axis in marker_axes(site) for site in sites):
            findings.append(Finding(
                "redundant-row", text, name,
                f"MINT-PATTERN redundant row: {text} — every site it covers carries its own marker for "
                f"{axis}, so the row exempts nothing. Delete the row, or delete the markers."))
        else:
            for site in sites:
                if axis in marker_axes(site) and not is_frozen(site.path, frozen):
                    findings.append(Finding(
                        "redundant-marker", f"{site.path}:{site.line}", name,
                        f"MINT-PATTERN redundant marker: {site.path}:{site.line} — the row {text} already "
                        f"exempts {name} on {axis}. Delete the marker."))

    for marker in dangling:
        if is_frozen(marker.path, frozen):
            continue
        needle = MARKERS[marker.kind][0]
        findings.append(Finding(
            "dangling-marker", f"{marker.path}:{marker.line}", marker.kind,
            f"MINT-PATTERN dangling marker: {marker.path}:{marker.line} — the `{needle}` marker sits on no "
            f"mint signature, so it exempts nothing. Delete it, or move it to the line that names the mint."))
    return findings


def run(files: list[Path], allowlist: Path, frozen: tuple[str, ...]) -> int:
    """Scan the files, print every finding, and return the exit code.

    Args:
        files: The surface to scan
        allowlist: The allowlist file
        frozen: The frozen path prefixes

    Returns:
        0 when clean, 1 on a violation or a parse failure, 2 on any other finding

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
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
    rows, malformed = read_allowlist(allowlist)
    findings += malformed + evaluate(mints, rows, frozen, dangling)
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

        # The grammar of a row.
        allow.write_text(
            "# a comment\n\n"
            "a.h:mint_good:constexpr-ok\n"
            "a.h:mint_bare\n"
            "a.h:120:constexpr-ok\n"
            "a.h:mint_typo:constexr-ok\n"
            "a.h:mint_good:constexpr-ok\n"
            "a.h:helper:constexpr-ok\n"
            "a.h:mint_space:constexpr-ok  trailing\n",
            encoding="utf-8",
        )
        rows, malformed = read_allowlist(allow)
        texts = " ".join(finding.message for finding in malformed)
        check("a well-formed row is read", rows == {("a.h", "mint_good", "constexpr")})
        check("a bare key with no axis is malformed", "a.h:mint_bare —" in texts, True)
        check("a row keyed by a line is malformed", "names the mint, never a line" in texts, True)
        check("a row with an unknown axis is malformed", "`constexr-ok` names no axis" in texts, True)
        check("a second copy of a row is malformed", "a row before this one says the same" in texts, True)
        check("a row that names no mint is malformed", "`helper` is not the name of a mint" in texts, True)
        check("a row with a space is malformed", "mint_space" in texts, True)

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
        frozen_dir = root / "frozen"
        frozen_dir.mkdir()
        old = frozen_dir / "old.h"
        old.write_text(
            "#pragma once\n"
            "namespace probe_old {\n"
            "// §XXI carve-out: cx=alloc — dead, but the file is frozen.\n"
            "[[nodiscard]] constexpr int mint_frozen_dead() noexcept { return 1; }\n"
            "// §XXI carve-out: cx=alloc — this one allocates.\n"
            "[[nodiscard]] inline int* mint_frozen_twice() noexcept { return new int{0}; }\n"
            "inline int frozen_stray = 0;  // MINT-PATTERN-OK: dangling, but frozen\n"
            "}\n",
            encoding="utf-8",
        )
        frozen = (str(frozen_dir) + "/",)
        allow.write_text(
            f"{live}:mint_runtime_read:constexpr-ok\n"
            f"{live}:mint_marked_twice:constexpr-ok\n"
            f"{live}:mint_overload:constexpr-ok\n"
            f"{live}:mint_compliant:constexpr-ok\n"
            f"{old}:mint_frozen_twice:constexpr-ok\n",
            encoding="utf-8",
        )
        dangling: list[Marker] = []
        mints = mintmodel.collect([live, old], on_tree=lambda tree, sites: dangling.extend(find_markers(tree, sites)))
        rows, malformed = read_allowlist(allow)
        found = evaluate(mints, rows, frozen, dangling)

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
        check("a dead marker in a frozen file is not reported", not has("dead-marker", "mint_frozen_dead"))
        check("a dangling marker in a frozen file is not reported", not mentions("dangling-marker", str(old)))
        check("a redundant row that names a frozen file still fails", has("redundant-row", "mint_frozen_twice"), True)

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
        allow.write_text(f"{clean}:mint_runtime_read:constexpr-ok\n", encoding="utf-8")
        check("a clean surface exits 0", captured(lambda: run([clean], allow, ()))[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: run([clean], allow, ()))
        finally:
            os.chdir(previous)
        check("the report from / equals the report from the repository", from_slash == captured(
            lambda: run([clean], allow, ())))
        allow.write_text("", encoding="utf-8")
        check("a runtime-only mint with no row exits 1", captured(lambda: run([clean], allow, ()))[0] == 1, True)
        allow.write_text(f"{clean}:mint_runtime_read:constexpr-ok\n{clean}:mint_compliant:constexpr-ok\n",
                         encoding="utf-8")
        check("a stale row exits 2", captured(lambda: run([clean], allow, ()))[0] == 2, True)
        allow.write_text(f"{clean}:mint_runtime_read:constexpr-ok\n{clean}:mint_extra\n", encoding="utf-8")
        check("a malformed row exits 2", captured(lambda: run([clean], allow, ()))[0] == 2, True)
        broken = root / "broken.h"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        allow.write_text(f"{clean}:mint_runtime_read:constexpr-ok\n", encoding="utf-8")
        code, report = captured(lambda: run([clean, broken], allow, ()))
        check("a file the parser cannot read exits 1", code == 1 and "parse failure" in report, True)

    if failures:
        print(f"check-mint-pattern --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print(f"check-mint-pattern --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    try:
        if argv == ["--self-test"]:
            return self_test()
        if argv == []:
            return run(mintmodel.surface_files(), ALLOWLIST, read_frozen())
    except tsast.KitMissing as exc:
        print(f"check-mint-pattern: {exc}", file=sys.stderr)
        return 3
    print("usage: check-mint-pattern.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
