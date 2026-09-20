#!/usr/bin/env bash
# check-brand-drain.sh — every spelling of a branded type that names no
# brand is listed, the list only shrinks, and it ends as a hard error.
#
# Region identity in this tree is a brand: a trailing template parameter
# on Permission, SharedPermission, SharedPermissionGuard,
# SharedPermissionPool, OwnedRegion, ScopedView, Borrowed, BorrowedRef
# and ReadView.  A mint gives each call site a fresh brand, so two
# regions minted for one tag are two types and a borrow of one cannot
# stand in for the other.  A spelling that names no brand, such as
# `Permission<Tag>`, is the erased identity `DefaultBrand`: every token
# under it is interchangeable with every other, which is the behaviour
# the tree had before brands.  The erasure is the compatibility door
# for code written before brands, not a second mode of operation.
#
# The rule
# --------
# Every spelling of one of the nine templates whose argument list has
# the old arity, or names DefaultBrand explicitly, is a site on the
# erased identity.  scripts/brand-drain.txt holds one count per file.
# A file with more sites than its entry fails: write the brand or a
# generic parameter, do not extend the ledger.  A file with fewer sites
# than its entry fails as stale: refresh the ledger in the same commit
# with --refresh.  When the ledger is empty, any site at all is a hard
# error, and that is the end state.
#
# What this guard does NOT do, stated rather than implied
# -------------------------------------------------------
#   - it reads text, so a spelling produced by a macro or an alias is
#     invisible to it, and `using P = Permission<Tag>;` counts once at
#     the alias rather than at each use;
#   - a `<` or `>` operator inside a template argument list, as in
#     `Borrowed<int, Owner<(N > 0)>>`, unbalances the scan, and the
#     site is reported as unreadable rather than counted; an argument
#     list that continues onto the next lines is joined with up to four
#     of them before the scan, and one longer than that is unreadable;
#   - string literals and line comments are stripped before the scan,
#     block comments are not;
#   - a site erased through a conversion, `Permission<Tag> p = mint...`,
#     is counted at the spelling and nowhere else, and a fresh token
#     handed to a function whose parameter is the erased spelling is
#     counted at that parameter.
# Reflection cannot replace this scan: the spellings sit inside function
# templates, whose parameters std::meta does not enumerate in GCC 16.
#
# Exit codes
#   0 — every file is at or below its ledger entry, and no file is below
#   1 — a file has more sites than the ledger allows
#   2 — bad invocation, a stale ledger entry, or a failed self-test
#
# Usage
#   check-brand-drain.sh [--quiet]     check the tree against the ledger
#   check-brand-drain.sh --list        print every site
#   check-brand-drain.sh --refresh     rewrite the ledger from the tree
#   check-brand-drain.sh --self-test   plant a site and prove it is reported

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_roots="${BRAND_DRAIN_ROOTS:-$root/include/foundation $root/include/fixy $root/test/foundation $root/test/fixy}"
ledger="${BRAND_DRAIN_LEDGER:-$root/scripts/brand-drain.txt}"

usage() {
    printf 'usage: %s [--quiet | --list | --refresh | --self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

run_py() {
    python3 - "$1" "$ledger" "$root" $scan_roots <<'PY'
import re
import sys
from pathlib import Path

mode, ledger_path, root = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
roots = [Path(p) for p in sys.argv[4:]]

# The nine templates and the arity a spelling has when it names no brand.
OLD_ARITY = {
    "Permission": 1,
    "SharedPermission": 1,
    "SharedPermissionGuard": 1,
    "SharedPermissionPool": 1,
    "OwnedRegion": 2,
    "ScopedView": 2,
    "Borrowed": 2,
    "BorrowedRef": 1,
    "ReadView": 1,
}
NAME = re.compile(r"\b(" + "|".join(OLD_ARITY) + r")\s*<")
STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
LINE_COMMENT = re.compile(r"//.*$")
ERASED = re.compile(r"\bDefaultBrand\b")


def strip(line: str) -> str:
    """Remove string literals and the line comment, keeping the length."""
    return LINE_COMMENT.sub("", STRING.sub('""', line))


def argument_count(text: str, start: int):
    """Top-level argument count of the list opening at text[start] == '<'.

    Returns (count, end_index) or (None, None) when the list does not
    close on this line.  `->` and `>>` are handled; a comparison
    operator inside the list is not."""
    depth, count, i, n = 0, 1, start, len(text)
    while i < n:
        c = text[i]
        if c == "<":
            depth += 1
        elif c == ">":
            if i > 0 and text[i - 1] == "-":
                i += 1
                continue
            depth -= 1
            if depth == 0:
                return count, i
        elif c == "," and depth == 1:
            count += 1
        elif c in "([{":
            # A parenthesised argument keeps its commas to itself.
            close = {"(": ")", "[": "]", "{": "}"}[c]
            inner = 1
            i += 1
            while i < n and inner:
                if text[i] == c:
                    inner += 1
                elif text[i] == close:
                    inner -= 1
                i += 1
            continue
        i += 1
    return None, None


sites, unreadable = [], []
for r in roots:
    if not r.exists():
        continue
    for path in sorted(p for p in r.rglob("*") if p.suffix in (".h", ".cpp")):
        # A planted root outside the project is shown relative to the
        # root itself, so the self-test's ledger keys are short.
        shown = str(path.relative_to(root) if path.is_relative_to(root) else path.relative_to(r))
        lines = [strip(raw) for raw in path.read_text(errors="replace").splitlines()]
        for index, line in enumerate(lines):
            lineno = index + 1
            for m in NAME.finditer(line):
                name = m.group(1)
                # An argument list that runs past the end of the line is
                # read with the lines that follow, up to four of them.
                joined = line
                count, end = argument_count(joined, m.end() - 1)
                extra = 0
                while count is None and extra < 4 and index + extra + 1 < len(lines):
                    extra += 1
                    joined = joined + " " + lines[index + extra]
                    count, end = argument_count(joined, m.end() - 1)
                if count is None:
                    unreadable.append((shown, lineno, line.strip()[:70]))
                    continue
                args = joined[m.end():end]
                if count == OLD_ARITY[name] or ERASED.search(args):
                    sites.append((shown, lineno, name))

per_file: dict[str, int] = {}
for shown, _, _ in sites:
    per_file[shown] = per_file.get(shown, 0) + 1

if mode == "list":
    for shown, lineno, name in sites:
        print(f"ERASED    {shown}:{lineno}  {name}")
    for shown, lineno, snippet in unreadable:
        print(f"UNREADABLE {shown}:{lineno}  {snippet}")
    print(f"check-brand-drain: {len(sites)} site(s) on the erased identity in "
          f"{len(per_file)} file(s), {len(unreadable)} unreadable.", file=sys.stderr)
    sys.exit(0)

if mode == "refresh":
    header = (
        "# scripts/brand-drain.txt — sites still on the erased brand, one count\n"
        "# per file.  Read by scripts/check-brand-drain.sh.  A count only goes\n"
        "# down: to add a site, write the brand instead.  Regenerate with\n"
        "# --refresh in the same commit that removes a site.\n"
    )
    body = "".join(f"{shown}\t{per_file[shown]}\n" for shown in sorted(per_file))
    ledger_path.write_text(header + body)
    print(f"check-brand-drain: ledger written with {len(per_file)} file(s), {len(sites)} site(s).",
          file=sys.stderr)
    sys.exit(0)

allowed: dict[str, int] = {}
if ledger_path.exists():
    for raw in ledger_path.read_text().splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        shown, _, count = entry.rpartition("\t")
        allowed[shown] = int(count)

over, stale = [], []
for shown in sorted(set(per_file) | set(allowed)):
    have, limit = per_file.get(shown, 0), allowed.get(shown, 0)
    if have > limit:
        over.append((shown, have, limit))
    elif have < limit:
        stale.append((shown, have, limit))

for shown, have, limit in over:
    print(f"NEW SITE  {shown}: {have} site(s) on the erased identity, ledger allows {limit}")
for shown, have, limit in stale:
    print(f"STALE     {shown}: {have} site(s), ledger still says {limit}; refresh the ledger in this commit")
for shown, lineno, snippet in unreadable:
    print(f"UNREADABLE {shown}:{lineno}  {snippet}")

total_allowed = sum(allowed.values())
print(f"check-brand-drain: {len(sites)} site(s) in {len(per_file)} file(s); ledger allows {total_allowed} "
      f"in {len(allowed)} file(s); {len(over)} over, {len(stale)} stale, {len(unreadable)} unreadable.",
      file=sys.stderr)
if over or unreadable:
    sys.exit(1)
if stale:
    sys.exit(2)
sys.exit(0)
PY
}

self_test() {
    local tmp rc out
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/planted/inner"
    out="$tmp/out.txt"

    # Three spellings that name no brand, one that names the erased
    # brand, two that name a brand of their own, and one branded
    # spelling whose argument list continues onto the next line.  The
    # count is four.
    cat >"$tmp/planted/inner/Planted.h" <<'EOF'
#pragma once
struct Tag {};
struct Fresh {};
template <class T, class B> struct Permission {};
template <class T, class B> struct ReadView {};
Permission<Tag> a;
ReadView<Tag> b;
Permission<Tag, DefaultBrand> c;
Permission<Tag, Fresh> d;
ReadView<Tag, Fresh> e;
// A comment spelling Permission<Tag> is not a site.
const char* text = "and neither is Permission<Tag> in a string";
OwnedRegion<int, Tag> f;
using Long = Permission<Tag,
                        Fresh>;
EOF
    printf 'inner/Planted.h\t4\n' >"$tmp/ledger.txt"
    set +e
    BRAND_DRAIN_ROOTS="$tmp/planted" BRAND_DRAIN_LEDGER="$tmp/ledger.txt" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -ne 0 ]]; then
        printf 'check-brand-drain --self-test: FAIL — four planted sites against a ledger of four did not pass (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi

    # One more site than the ledger allows is reported and fails.
    printf 'Permission<Tag> g;\n' >>"$tmp/planted/inner/Planted.h"
    set +e
    BRAND_DRAIN_ROOTS="$tmp/planted" BRAND_DRAIN_LEDGER="$tmp/ledger.txt" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -eq 1 ]] && grep -q 'NEW SITE.*inner/Planted.h: 5' "$out"; then
        printf 'check-brand-drain --self-test: a fifth site against a ledger of four is reported, as expected.\n'
    else
        printf 'check-brand-drain --self-test: FAIL — expected exit 1 naming the fifth site (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi

    # A ledger that allows more than the tree holds is stale and fails.
    printf 'inner/Planted.h\t9\n' >"$tmp/ledger.txt"
    set +e
    BRAND_DRAIN_ROOTS="$tmp/planted" BRAND_DRAIN_LEDGER="$tmp/ledger.txt" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -eq 2 ]] && grep -q 'STALE.*inner/Planted.h' "$out"; then
        printf 'check-brand-drain --self-test: a stale ledger entry is reported, as expected.\n'
    else
        printf 'check-brand-drain --self-test: FAIL — expected exit 2 naming the stale entry (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi

    # The end state: an empty ledger makes any site a hard error.
    : >"$tmp/ledger.txt"
    set +e
    BRAND_DRAIN_ROOTS="$tmp/planted" BRAND_DRAIN_LEDGER="$tmp/ledger.txt" bash "${BASH_SOURCE[0]}" --quiet >"$out" 2>&1
    rc=$?
    set -e
    if [[ $rc -eq 1 ]]; then
        printf 'check-brand-drain --self-test: with an empty ledger every site is an error, as expected.\n'
    else
        printf 'check-brand-drain --self-test: FAIL — an empty ledger did not refuse the sites (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 2
    fi
    printf 'check-brand-drain --self-test: PASS.\n'
}

case "${1:-}" in
    "")          run_py check ;;
    --quiet)     run_py check ;;
    --list)      run_py list ;;
    --refresh)   run_py refresh ;;
    --self-test) self_test ;;
    *)           usage ;;
esac
