#!/usr/bin/env bash
# check-lifetime-twin.sh — every site that claims a lifetime bound has
# the deleted overload that enforces it.
#
# CRUCIBLE_LIFETIMEBOUND expands to nothing.  It expanded to nothing on
# every build this project has ever produced, because it was guarded
# behind `#if defined(__clang__)` and GCC 16 is the only compiler that
# can build this tree.  Measured with __has_cpp_attribute: GCC 16.2.1
# recognises neither clang::lifetimebound nor gnu::lifetimebound nor the
# unqualified spelling, so 121 annotated sites across the two trees
# carried a claim no compiler read.  Two of them were live bugs, both
# proven by compiling the call: a borrow proof minted from a temporary
# permission, and a scoped view over a temporary carrier.
#
# So the annotation is the claim and this guard is what reads it.  The
# mechanism it requires is ordinary C++ that GCC does honour: an
# overload taking an rvalue reference in the same position, declared
# `= delete`, which is a better match for a prvalue argument and so
# refuses the temporary at the call site.
#
# The rule
# --------
# For every CRUCIBLE_LIFETIMEBOUND in the scanned roots, the declaration
# it sits in must have a name, and the same file must declare a deleted
# overload of that name whose parameter list contains an rvalue
# reference.  A constructor's name is its class.
#
# What this guard does NOT check, stated rather than implied
# ----------------------------------------------------------
#   - the POSITION of the rvalue reference in the twin's parameter list,
#     so a twin that refuses a temporary in a different parameter
#     satisfies this guard while leaving the annotated one open;
#   - the TYPE, so a twin over an unrelated type counts;
#   - anything across files, so a twin declared in another header does
#     not count and a site whose twin genuinely lives elsewhere must be
#     restructured or excluded;
#   - any site whose declaration's name is more than four lines above the
#     annotation, which the scan reports as unreadable rather than
#     passing silently.  A wrapped parameter list is read by walking back
#     to the line that opened it, so a mint with several parameters is
#     checked rather than skipped.
# Reflection would answer the first two, and cannot be used here: most
# annotated sites are inside function templates, and a template's
# parameters are not enumerable through std::meta in GCC 16.  Measured.
#
# Exit codes
#   0 — every claim has its twin
#   1 — a site claims a bound with no twin, or a site is unreadable
#   2 — bad invocation or a failed self-test
#
# Usage
#   check-lifetime-twin.sh [--quiet]   check the tree
#   check-lifetime-twin.sh --list      print every site and its verdict
#   check-lifetime-twin.sh --self-test plant a missing twin and prove the
#                                      guard reports it

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_roots="${LIFETIME_TWIN_ROOTS:-$root/include/foundation $root/include/fixy}"
quiet=0

usage() {
    printf 'usage: %s [--quiet | --list | --self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

run_py() {
    python3 - "$1" $scan_roots <<'PY'
import re
import sys
from pathlib import Path

mode = sys.argv[1]
roots = [Path(p) for p in sys.argv[2:]]

TOKEN = "CRUCIBLE_LIFETIMEBOUND"
# The declaration's name: a constructor or function name immediately
# before the parameter list the annotation sits in.
# The name may carry an explicit template argument list, as a friend
# declaration of a specialization does: `mint_read_view<Tag>(`.
NAME_BEFORE_PAREN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^()<>]*>)?\s*\(")
# A keyword followed by a parenthesis is not a declaration's name.  A
# constrained twin reads `requires(...) constexpr auto f(T&&) = delete`,
# and the first identifier before a parenthesis there is `requires`.
KEYWORDS_BEFORE_PAREN = frozenset({"requires", "decltype", "sizeof", "alignof", "noexcept", "delete", "if",
                                   "while", "for", "switch", "return", "static_assert", "typeid", "pre", "post"})
DELETED = re.compile(r"=\s*delete\b")
RVALUE_REF = re.compile(r"&&")

def first_declaration_name(text: str) -> str:
    """The first identifier before a parameter list that is not a keyword."""
    for name in NAME_BEFORE_PAREN.findall(text):
        if name not in KEYWORDS_BEFORE_PAREN:
            return name
    return ""

def declaration_name(line: str) -> str:
    """The declaration's own name, read off the line holding the token."""
    head = line.split(TOKEN, 1)[0]
    # The FIRST match is the declaration's own name.  A later one comes
    # from inside the parameter list, as in the array-of-reference
    # declarator `Borrowed(T (&array X)[N])`, where the last identifier
    # before a paren is the element type rather than the constructor.
    return first_declaration_name(head)

def declaration_name_near(lines: list[str], index: int) -> str:
    """The declaration's own name for the site on lines[index].

    A mint that takes more than one parameter wraps its list, and the
    annotated parameter then sits on a line carrying no paren at all, so
    reading that line alone yields nothing and the site is unreadable.
    An unreadable site is one the guard silently stops checking, which is
    how a claim loses its twin without anyone noticing.  So walk back
    until the line that opened the list is in view, at most four lines,
    the window the deleted-twin scan already joins.

    The walk cannot invent a pass: the name it finds is still matched
    against the deleted-twin set, so a wrong name is reported as a
    missing twin rather than accepted.
    """
    name = declaration_name(lines[index])
    if name:
        return name
    joined = lines[index]
    for back in range(1, 5):
        if index - back < 0:
            break
        joined = lines[index - back] + " " + joined
        name = declaration_name(joined)
        if name:
            return name
    return ""

def deleted_names(text: str) -> set[str]:
    """Names of deleted declarations whose parameter list takes an rvalue.

    A deleted declaration can span lines, so the file is joined and then
    split on semicolons, which is where a declaration ends."""
    found = set()
    flat = " ".join(text.splitlines())
    for chunk in flat.split(";"):
        if not DELETED.search(chunk):
            continue
        if not RVALUE_REF.search(chunk):
            continue
        name = first_declaration_name(chunk)
        if name:
            found.add(name)
    return found

sites, missing, unreadable = 0, [], []
for r in roots:
    for path in sorted(r.rglob("*.h")):
        text = path.read_text(errors="replace")
        if TOKEN not in text:
            continue
        twins = deleted_names(text)
        lines = text.splitlines()
        for lineno, line in enumerate(lines, 1):
            if TOKEN not in line:
                continue
            if line.lstrip().startswith(("//", "#define", "#")):
                continue
            sites += 1
            name = declaration_name_near(lines, lineno - 1)
            shown = path.relative_to(r.parent)
            if not name:
                unreadable.append((str(shown), lineno, line.strip()[:70]))
            elif name not in twins:
                missing.append((str(shown), lineno, name))
            elif mode == "list":
                print(f"OK        {shown}:{lineno}  {name}")

for shown, lineno, name in missing:
    print(f"NO TWIN   {shown}:{lineno}  {name}")
for shown, lineno, snippet in unreadable:
    print(f"UNREADABLE {shown}:{lineno}  {snippet}")

checked = sites - len(unreadable)
print(f"check-lifetime-twin: {sites} site(s), {checked} checkable, "
      f"{len(missing)} without a twin, {len(unreadable)} unreadable.",
      file=sys.stderr)
sys.exit(1 if (missing or unreadable) else 0)
PY
}

self_test() {
    local tmp rc out
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/planted/inner"

    # Two sites with their twins, one of them a constrained free
    # function whose twin opens with a requires-clause, and one site
    # without.  The guard must report exactly the last.
    cat >"$tmp/planted/inner/Planted.h" <<'EOF'
#pragma once
struct Carrier {};
struct Guarded {
    explicit Guarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;
    explicit Guarded(Carrier const&&) = delete("planted twin");
};
template <class T>
constexpr int planted_mint(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept;
template <class T>
    requires(!std::is_lvalue_reference_v<T>)
constexpr auto planted_mint(T&&) = delete("planted constrained twin");
template <class T>
constexpr int planted_wrapped(Carrier const& first,
                              T& second CRUCIBLE_LIFETIMEBOUND) noexcept;
template <class T>
    requires(!std::is_lvalue_reference_v<T>)
constexpr auto planted_wrapped(Carrier const&, T&&) = delete("planted wrapped twin");
struct Unguarded {
    explicit Unguarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;
};
EOF
    out="$tmp/out.txt"
    set +e
    LIFETIME_TWIN_ROOTS="$tmp/planted" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -eq 1 ]] && grep -q 'NO TWIN.*Unguarded' "$out" && ! grep -q 'NO TWIN.*Guarded(' "$out" \
        && ! grep -q 'NO TWIN.*planted_mint' "$out" \
        && ! grep -q 'UNREADABLE' "$out" && ! grep -q 'NO TWIN.*planted_wrapped' "$out"; then
        printf 'check-lifetime-twin --self-test: a claim without its twin is reported, and a claim with one is not, including a twin behind a requires-clause and a claim whose parameter list wraps, as expected.\n'
    else
        printf 'check-lifetime-twin --self-test: FAIL — expected exit 1 naming Unguarded (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi

    # A clean control: give the second one its twin and the guard passes.
    cat >>"$tmp/planted/inner/Planted.h" <<'EOF'
struct Repaired {
    explicit Repaired(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;
    explicit Repaired(Carrier const&&) = delete("planted twin");
};
EOF
    python3 - "$tmp/planted/inner/Planted.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read().replace(
    "struct Unguarded {\n    explicit Unguarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n};\n", "")
open(p, "w").write(s)
PY
    set +e
    LIFETIME_TWIN_ROOTS="$tmp/planted" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -eq 0 ]]; then
        printf 'check-lifetime-twin --self-test: the clean control passes, as expected.\n'
    else
        printf 'check-lifetime-twin --self-test: FAIL — a fully twinned set did not pass (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi
    printf 'check-lifetime-twin --self-test: PASS.\n'
}

case "${1:-}" in
    "")          run_py check ;;
    --quiet)     quiet=1; run_py check ;;
    --list)      run_py list ;;
    --self-test) self_test ;;
    *)           usage ;;
esac
