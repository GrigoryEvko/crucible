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

That cost runs the other way too.  A comment at `FederationPermission.h:301`
states that its `[[nodiscard]]` sits on its own line "because the source scanner
that audits mint factories reads a fixed qualifier window ahead of the
signature".  The scanner's limit reached into how the source is formatted.

THREE EXEMPTION MECHANISMS.  The tree carries all three at once, and this gate
reads every one so its verdict matches the old guard:

  * `scripts/mint-pattern-allowlist.txt`, keyed `path:name:axis-ok`
  * a `// §XXI carve-out: cx=alloc` or `rq=pre` comment above the signature
  * a `// MINT-PATTERN-OK: <reason>` marker on the signature line

Measured 2026-09-21: the allowlist opened the day at 138 entries and closed it at
75, against 39 carve-out comments and 9 inline markers.  Fifty of the entries it
shed were not exemptions — they recorded a constraint the scanner could not read.

SUPERSEDED HEADERS ARE OUT OF SCOPE, and that is a change.  The bash guard scans
`include/crucible/**/_*.h`, the ported old-substrate headers, and 24 allowlist
entries exist only to exempt them.  Those files are frozen, so a shortfall there
cannot be repaired, and they go with the old tree.  Enforcing §XXI on them buys
nothing and keeps 24 ledger entries alive.  The inventory generator already
excludes them.

EXIT CODES
    0  every mint meets the four axes, or a stated mechanism exempts it
    2  a mint falls short with nothing exempting it, or an entry went stale
    3  the pinned tree-sitter kit is not installed
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mintmodel  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

ALLOWLIST = tsast.REPO_ROOT / "scripts" / "mint-pattern-allowlist.txt"

# The axes, each with the allowlist suffix that exempts it.  `requires` asks only
# whether a constraint is PRESENT; `ctxfit` asks the separate question of whether
# it gates the context, which is the one §XXI actually cares about for a ctx-bound
# mint.  Folding the two reported a constrained mint as unconstrained, and read a
# mint that gates nothing about its context as compliant the moment it constrained
# some other parameter.
AXES = ("nodiscard", "constexpr", "noexcept", "requires", "ctxfit")


def read_allowlist(path: Path = ALLOWLIST) -> set[tuple[str, str, str]]:
    """Return the allowlist as a set of (path, mint name, axis) triples.

    The key is content, never a line number, so an edit above a site cannot
    drift it.

    Args:
        path: The allowlist file

    Returns:
        One triple for each live entry
    """
    if not path.is_file():
        return set()
    found: set[tuple[str, str, str]] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(":")
        if len(parts) < 3:
            continue
        axis = parts[2].removesuffix("-ok")
        if axis in AXES:
            found.add((parts[0], parts[1], axis))
    return found


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


def exempted(mint: mintmodel.Mint, axis: str, allowlist: set[tuple[str, str, str]]) -> bool:
    """Report whether a stated mechanism exempts this mint on this axis.

    Args:
        mint: The mint in question
        axis: One of the four axis names
        allowlist: The allowlist triples

    Returns:
        True when the allowlist, a carve-out comment or an inline marker covers it
    """
    if (mint.path, mint.name, axis) in allowlist:
        return True
    if mint.inline_ok:
        return True
    if axis == "constexpr" and mint.carve_out_cx:
        return True
    if axis == "requires" and mint.carve_out_rq:
        return True
    return False


def scan() -> int:
    """Report every unexempted shortfall and every stale allowlist entry.

    Returns:
        0 when clean, 2 on a finding, 3 when the pinned kit is absent
    """
    try:
        mints = mintmodel.collect(mintmodel.guard_files())
    except tsast.KitMissing as exc:
        print(f"check-mint-pattern: {exc}", file=sys.stderr)
        return 3

    allowlist = read_allowlist()
    findings: list[str] = []
    live: set[tuple[str, str, str]] = set()

    for mint in sorted(mints, key=lambda m: (m.path, m.line)):
        for axis in shortfalls(mint):
            live.add((mint.path, mint.name, axis))
            if exempted(mint, axis, allowlist):
                continue
            if axis == "ctxfit":
                lack = (
                    f"takes a context ({mint.ctx_token}) that no constraint gates. "
                    f"It accepts every context in the tree. Write a "
                    f"`CtxFits...<..., {mint.ctx_token}>` conjunct into its "
                    f"constraint, or state the reason"
                )
            else:
                lack = f"is missing {axis}. Add it to the signature, or state the reason"
            findings.append(
                f"MINT-PATTERN violation: {mint.path}:{mint.line} — {mint.name} "
                f"{lack}: an inline `// MINT-PATTERN-OK: <reason>` on the signature "
                f"line, or an entry `{mint.path}:{mint.name}:{axis}-ok` in "
                f"scripts/mint-pattern-allowlist.txt."
            )

    for entry in sorted(allowlist - live):
        path, name, axis = entry
        findings.append(
            f"MINT-PATTERN stale: {path}:{name}:{axis}-ok — no live {axis} "
            f"shortfall for this mint. It was fixed, renamed, moved, or its file "
            f"is a superseded `_*.h` header this gate no longer scans. Remove the "
            f"entry so the gate keeps its drift coverage."
        )

    for message in findings:
        print(message, file=sys.stderr)
    if findings:
        print(
            f"\ncheck-mint-pattern: {len(findings)} finding(s) across {len(mints)} "
            f"mint sites on the {len(AXES)} §XXI axes.",
            file=sys.stderr,
        )
        return 2
    print(
        f"check-mint-pattern: clean — {len(mints)} mint sites meet the {len(AXES)} "
        f"§XXI axes or carry a stated exemption, and no entry is stale.",
        file=sys.stderr,
    )
    return 0


def self_test() -> int:
    """Exercise the axis and exemption logic, positive and negative.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        """Record one case result and print it.

        Args:
            name: What the case asserts
            ok: Whether it held
        """
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
    check("a missing nodiscard is a shortfall", shortfalls(make(nodiscard=False)) == ["nodiscard"])
    check("a missing noexcept is a shortfall", shortfalls(make(noexcept_=False)) == ["noexcept"])
    check(
        "all five can fall short at once",
        shortfalls(make(nodiscard=False, constexpr=False, noexcept_=False,
                        requires_=False, ctx_gated=False))
        == ["nodiscard", "constexpr", "noexcept", "requires", "ctxfit"],
    )
    # The rule the bash window got wrong: requires applies only to a template.
    check(
        "a templated mint with no constraint falls short",
        shortfalls(make(requires_=False, templated=True)) == ["requires"],
    )
    check(
        "a non-template with no clause does NOT fall short",
        shortfalls(make(requires_=False, templated=False)) == [],
    )
    # A constraint on a template parameter satisfies the presence axis.
    check(
        "a constrained template parameter satisfies requires",
        shortfalls(make(requires_=False, constraint_concepts=frozenset({"Surface"})))
        == [],
    )
    # Negative control: the presence axis is not the context axis.  A mint
    # constrained on another parameter still has to gate its context.
    check(
        "a parameter constraint does NOT satisfy the context axis",
        shortfalls(make(requires_=False, ctx_gated=False,
                        constraint_concepts=frozenset({"Surface"})))
        == ["ctxfit"],
    )
    check(
        "an ungated context is a shortfall",
        shortfalls(make(ctx_gated=False)) == ["ctxfit"],
    )
    # Negative control: a token mint holds no context, so the axis is silent.
    check(
        "a token mint is not asked to gate a context",
        shortfalls(make(shape="token", ctx_token=None, ctx_gated=False)) == [],
    )
    check(
        "a member mint is not asked to gate a context",
        shortfalls(make(shape="member", ctx_token=None, ctx_gated=False)) == [],
    )

    # Each of the three exemption mechanisms covers, and covers only its own axis.
    allow = {("p.h", "mint_probe", "constexpr")}
    check(
        "an allowlist entry exempts its axis",
        exempted(make(constexpr=False), "constexpr", allow),
    )
    check(
        "an allowlist entry does not exempt another axis",
        not exempted(make(noexcept_=False), "noexcept", allow),
    )
    check(
        "a cx carve-out comment exempts constexpr",
        exempted(make(constexpr=False, carve_out_cx=True), "constexpr", set()),
    )
    check(
        "a cx carve-out does not exempt requires",
        not exempted(make(requires_=False, carve_out_cx=True), "requires", set()),
    )
    check(
        "an rq carve-out comment exempts requires",
        exempted(make(requires_=False, carve_out_rq=True), "requires", set()),
    )
    # Negative control: an rq carve-out documents an absent CLAUSE.  It says
    # nothing about the context, so it must not reach the context axis.
    check(
        "an rq carve-out does not exempt ctxfit",
        not exempted(make(ctx_gated=False, carve_out_rq=True), "ctxfit", set()),
    )
    check(
        "a ctxfit allowlist entry exempts the context axis",
        exempted(make(ctx_gated=False), "ctxfit", {("p.h", "mint_probe", "ctxfit")}),
    )
    check(
        "an inline MINT-PATTERN-OK marker exempts the site",
        exempted(make(constexpr=False, inline_ok=True), "constexpr", set()),
    )
    check(
        "nothing exempts a bare shortfall",
        not exempted(make(constexpr=False), "constexpr", set()),
    )

    # The cosmetic axis: a borrow projection hands out a view over its own
    # carrier, so constexpr could never carry meaning there.
    check(
        "a borrow projection is not asked for constexpr",
        shortfalls(make(constexpr=False, borrow_projection=True)) == [],
    )
    # Negative control: the same absence IS a shortfall for a real factory.
    check(
        "a real factory with no constexpr still falls short",
        shortfalls(make(constexpr=False, borrow_projection=False)) == ["constexpr"],
    )
    # Negative control: the exemption is cosmetic only and must not reach a
    # soundness axis.
    check(
        "a borrow projection is still held to its other axes",
        shortfalls(make(nodiscard=False, requires_=False, borrow_projection=True))
        == ["nodiscard", "requires"],
    )

    if failures:
        print(f"check-mint-pattern --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print("check-mint-pattern --self-test: 23 cases pass, 10 of them negative controls.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        sys.exit(self_test())
    sys.exit(scan())
