#!/usr/bin/env bash
# check-escape-doors.sh — the tree-wide, coarse net for the escape guard: every
# public function that hands out a raw reference or pointer is a
# known-safe accessor, a mint, a marked and discouraged escape hatch, or
# a ledgered exception.  A new raw escape that is none of these fails the
# build.
#
# fixy is safe by default, and the
# ways a raw reference or pointer leaves a wrapper are explicit, still
# type-checked, and discouraged rather than silently available beside
# the safe call.
#
# This scan is the COARSE net, not the primary guard.  It reads source
# text, so a member written in a shape its regex does not match — a
# trailing return, a macro-hidden type, a wide multi-line declaration —
# is a raw escape it never sees.  The primary, unbreakable guard is
# foundation/reflect/RawEscape.h with test/fixy/test_escape_reflection.cpp,
# which walks each wrapper's members through std::meta and reads the
# return type the compiler resolved, so no spelling evades it.  This
# scan exists because reflection cannot walk FREE functions or function
# TEMPLATES: a free function is a member of no type, and a template has
# no single return type to read.  So the two divide the surface — the
# reflection probe owns member functions of the wrappers, this owns the
# free-function and template residue — and each states what it cannot
# see so the other's coverage is legible.
#
# What a raw escape is
# --------------------
# A public function whose return type, ignoring const, is a reference
# (`T&`, `const T&`, `T&&`) or a raw pointer (`T*`, `const T*`,
# `void*`).  A function returning a value — a span, a brand wrapper, an
# expected, an int — hands out no reference into anything and is not an
# escape.  Measured 2026-09-20 across include/fixy and include/foundation:
# 136 such functions, of which 129 are within-object accessors, 7 were
# bare, and none named a parameter brand.  The surface was already safe;
# this guard locks it so a NEW unsafe door cannot be added unseen.
#
# The four sanctioned shapes
# --------------------------
#   ACCESSOR   a getter whose name is in the accessor vocabulary below.
#              Its provenance is the object it is called on, so the
#              reference lives exactly as long as the object.  Admitted
#              silently.
#   MINT       a `mint_*` factory.  Admitted silently.  (Every mint in
#              the tree returns its wrapper by value, so none is a raw
#              escape today; the clause is here so a future
#              reference-returning mint is admitted rather than flagged.)
#   HATCH      a name in the discouraged-hatch vocabulary: from_raw,
#              from_raw_nonnull, into_raw, release, declassify.  These
#              are the type-checked, explicit, discouraged doors — the
#              C boundary and the ownership hand-off.  Admitted, but
#              COUNTED and printed every run, and the total is pinned in
#              the ledger, so a new hatch is a deliberate, reviewed act
#              rather than a quiet one.
#   LEDGERED   a bare door listed in scripts/escape-doors.txt with a
#              one-line reason.  The list only shrinks: a bare door is
#              reclassified into one of the three shapes above, never
#              added.
#
# Anything else is a NEW BARE DOOR and fails the build.
#
# What this guard does NOT do, stated rather than implied
# -------------------------------------------------------
#   - It classifies by the function's NAME, not by computing where the
#     returned reference points.  A getter named `data` that returned a
#     dangling reference would pass; the name is the claim and review is
#     what reads it.  Reflection cannot enumerate a function template's
#     return provenance in GCC 16, the same limit brand-drain and the
#     lifetime-twin guard document.
#   - It reads one declaration line plus up to three continuation lines,
#     so a return type spread wider than that is reported UNREADABLE
#     rather than passed.
#   - It strips string literals and line comments before the scan; block
#     comments are not stripped.
#   - It skips names ending in an underscore (the tree's internal-helper
#     convention) and anything inside a `detail` namespace segment of
#     the path is still scanned, because a detail header still ships.
#
# Exit codes
#   0 — every raw escape is accessor, mint, hatch, or ledgered, and the
#       hatch count matches the pin
#   1 — a new bare door, a stale ledger entry, an unreadable site, or a
#       hatch-count drift
#   2 — bad invocation or a failed self-test
#
# Usage
#   check-escape-doors.sh [--quiet]   check the tree against the ledger
#   check-escape-doors.sh --list      print every raw escape and its class
#   check-escape-doors.sh --refresh   rewrite the ledger and the hatch pin
#   check-escape-doors.sh --self-test plant a bare door and prove it fails

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_roots="${ESCAPE_DOORS_ROOTS:-$root/include/foundation $root/include/fixy}"
ledger="${ESCAPE_DOORS_LEDGER:-$root/scripts/escape-doors.txt}"

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

# The conventional getter names.  A member whose provenance is the
# object it is called on carries one of these; the survey that measured
# this surface enumerated them, and a new getter with a new name is
# added here in a reviewed edit rather than reaching for a bare door.
ACCESSORS = frozenset({
    "data", "data_mut", "begin", "end", "cbegin", "cend", "front", "back", "at", "get",
    "get_or_init", "get_assuming_set", "peek", "peek_mut", "value", "resource", "ctx",
    "in", "out", "input", "output", "stage", "machine", "state", "state_mut", "current",
    "carrier", "handle", "pin", "c_str", "sq_ring", "cq_ring", "sqes", "observe", "consume",
    "try_get", "raw_ptr", "graded", "cap", "span", "cspan",
})
# The discouraged, type-checked escape hatches.  Explicit by name, and
# printed every run so they stay in view.
HATCHES = frozenset({"from_raw", "from_raw_nonnull", "into_raw", "release", "declassify"})

# operator* / operator-> / operator[] hand out a reference or pointer to
# the object, so they are accessors under a name the identifier grammar
# does not spell.
OPERATOR_ACCESSORS = frozenset({"operator*", "operator->", "operator[]"})

STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
LINE_COMMENT = re.compile(r"//.*$")

# A declaration whose return type ends in a reference or a raw pointer,
# just before the function name and its open paren.  The return-type run
# is lazy so the `&`/`*` matched is the last one before the name.  A
# following `=`, `{`, `;` or `:` after the `)` is not required — a
# declaration and a definition both match at the paren.
DECL = re.compile(
    r"""^\s*
        (?:\[\[[^]]*\]\]\s*)?
        (?:(?:static|constexpr|consteval|inline|friend|explicit|virtual|CRUCIBLE_[A-Z_]+)\s+)*
        (?P<ret>[A-Za-z_][\w:\ ,<>]*?)
        \s*(?P<sigil>(?:\bconst\b\s*)?[*&]+)\s*
        (?P<name>operator\s*(?:\[\s*\]|->|\*)|~?[A-Za-z_]\w*)
        \s*\(""",
    re.VERBOSE,
)
# A deleted declaration hands out nothing.
DELETED = re.compile(r"=\s*delete\b")
# A return statement or a using/alias is not a declaration; the return
# type run would otherwise swallow `return *p;`.
NOT_DECL_HEAD = re.compile(r"^\s*(?:return|using|typedef|else|,)\b")


def strip(line: str) -> str:
    return LINE_COMMENT.sub("", STRING.sub('""', line))


def normalize_operator(name: str) -> str:
    return re.sub(r"\s+", "", name)


def classify(name: str) -> str:
    if name in OPERATOR_ACCESSORS:
        return "accessor"
    if name.startswith("mint_"):
        return "mint"
    if name in HATCHES:
        return "hatch"
    if name in ACCESSORS:
        return "accessor"
    return "bare"


escapes = []  # (shown, lineno, name, klass)
for r in roots:
    if not r.exists():
        continue
    for path in sorted(r.rglob("*.h")):
        shown = str(path.relative_to(root) if path.is_relative_to(root) else path.relative_to(r))
        raw_lines = path.read_text(errors="replace").splitlines()
        lines = [strip(x) for x in raw_lines]
        for index, line in enumerate(lines):
            if NOT_DECL_HEAD.match(line):
                continue
            m = DECL.match(line)
            if not m:
                continue
            # The whole declaration up to the first ')' may run onto the
            # next lines; join up to three to see a `= delete`.
            joined = line
            extra = 0
            while ")" not in joined and extra < 3 and index + extra + 1 < len(lines):
                extra += 1
                joined += " " + lines[index + extra]
            if DELETED.search(joined):
                continue
            # A return type that is itself a smart wrapper returned by
            # value is not an escape; those do not end in a bare * or &,
            # so the regex already excludes them.  A pointer-to-member or
            # a comparison inside a template argument would mis-match, so
            # the return run must not contain an unbalanced '<'.
            ret = m.group("ret")
            if ret.count("<") != ret.count(">"):
                continue
            name = normalize_operator(m.group("name"))
            if name.endswith("_") and name not in OPERATOR_ACCESSORS:
                # An internal helper by the tree's trailing-underscore
                # convention: not a public door, so it is not a public
                # escape.  The reflection probe, which walks only public
                # members, agrees by construction.
                continue
            escapes.append((shown, index + 1, name, classify(name)))

by_class: dict[str, int] = {}
for _, _, _, klass in escapes:
    by_class[klass] = by_class.get(klass, 0) + 1

hatches = [(s, ln, nm) for (s, ln, nm, k) in escapes if k == "hatch"]
bare = [(s, ln, nm) for (s, ln, nm, k) in escapes if k == "bare"]

if mode == "list":
    for shown, lineno, name, klass in escapes:
        print(f"{klass.upper():9} {shown}:{lineno}  {name}")
    print(f"check-escape-doors: {len(escapes)} raw escape(s) — "
          f"{by_class.get('accessor', 0)} accessor, {by_class.get('mint', 0)} mint, "
          f"{by_class.get('hatch', 0)} hatch, {by_class.get('bare', 0)} bare.", file=sys.stderr)
    sys.exit(0)

# The ledger holds two kinds of line: a `hatch-total <N>` pin, and one
# `path:name  reason` per admitted bare door.
LEDGER_HEADER = (
    "# scripts/escape-doors.txt — the escape surface the guard locks.\n"
    "# Read by scripts/check-escape-doors.sh.\n"
    "#\n"
    "# hatch-total pins the count of discouraged, type-checked escape\n"
    "# hatches (from_raw, from_raw_nonnull, into_raw, release,\n"
    "# declassify).  A new hatch raises the count and reds CI until the\n"
    "# pin is raised in the same commit, so a hatch is never added\n"
    "# quietly.  Removing a hatch lowers it and is refreshed here too.\n"
    "#\n"
    "# Each `path:name  reason` line admits one bare door: a raw escape\n"
    "# that is neither an accessor nor a mint nor a hatch, but is safe\n"
    "# for the stated reason.  The list only shrinks — a bare door is\n"
    "# reclassified, never added.  Regenerate with --refresh in the same\n"
    "# commit that removes one.\n"
)

if mode == "refresh":
    lines_out = [LEDGER_HEADER, f"hatch-total {len(hatches)}\n"]
    # Keep any reasons the current ledger already gives for bare doors
    # still present; a genuinely new bare door gets a placeholder reason
    # the author must replace.
    existing: dict[str, str] = {}
    if ledger_path.exists():
        for raw in ledger_path.read_text().splitlines():
            e = raw.strip()
            if not e or e.startswith("#") or e.startswith("hatch-total"):
                continue
            key, _, reason = e.partition("  ")
            existing[key.strip()] = reason.strip()
    seen: set[str] = set()
    for shown, lineno, name in sorted(bare):
        key = f"{shown}:{name}"
        if key in seen:
            continue  # a const/non-const overload pair is one door.
        seen.add(key)
        reason = existing.get(key, "REASON REQUIRED — why this bare door is safe")
        lines_out.append(f"{key}  {reason}\n")
    ledger_path.write_text("".join(lines_out))
    print(f"check-escape-doors: ledger written — hatch-total {len(hatches)}, {len(bare)} bare door(s).",
          file=sys.stderr)
    sys.exit(0)

# check mode.
pinned_hatch_total = None
allowed_bare: set[str] = set()
if ledger_path.exists():
    for raw in ledger_path.read_text().splitlines():
        e = raw.strip()
        if not e or e.startswith("#"):
            continue
        if e.startswith("hatch-total"):
            pinned_hatch_total = int(e.split()[1])
            continue
        key, _, _ = e.partition("  ")
        allowed_bare.add(key.strip())

failures = 0

new_bare = [(s, ln, nm) for (s, ln, nm) in bare if f"{s}:{nm}" not in allowed_bare]
for shown, lineno, name in new_bare:
    print(f"NEW BARE DOOR  {shown}:{lineno}  {name} — returns a raw reference or pointer that is not an "
          f"accessor, a mint, or a marked escape hatch.  Make it one of those, or add "
          f"'{shown}:{name}  <reason>' to {ledger_path.name} if it is a safe exception.")
    failures += 1

seen_bare = {f"{s}:{nm}" for (s, ln, nm) in bare}
for key in sorted(allowed_bare - seen_bare):
    print(f"STALE LEDGER   {key} — no longer a bare door; refresh {ledger_path.name} with --refresh.")
    failures += 1

if pinned_hatch_total is None:
    print(f"NO HATCH PIN   {ledger_path.name} has no 'hatch-total' line; refresh with --refresh.")
    failures += 1
elif pinned_hatch_total != len(hatches):
    verb = "rose" if len(hatches) > pinned_hatch_total else "fell"
    print(f"HATCH DRIFT    the discouraged-hatch count {verb} from {pinned_hatch_total} to {len(hatches)}.  "
          f"A hatch is an explicit, discouraged door; raise or lower the pin in {ledger_path.name} "
          f"with --refresh in the same commit.")
    for shown, lineno, name in hatches:
        print(f"    hatch  {shown}:{lineno}  {name}")
    failures += 1

print(f"check-escape-doors: {len(escapes)} raw escape(s) — {by_class.get('accessor', 0)} accessor, "
      f"{by_class.get('mint', 0)} mint, {len(hatches)} hatch (pinned {pinned_hatch_total}), "
      f"{len(bare)} bare ({len(new_bare)} new).", file=sys.stderr)
sys.exit(1 if failures else 0)
PY
}

self_test() {
    local tmp rc out
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN

    # Arm 1 — the real tree passes.
    rc=0
    run_py check >"$tmp/clean.out" 2>&1 || rc=$?
    if (( rc != 0 )); then
        printf 'check-escape-doors: SELF-TEST FAILED — the real tree must pass --check (got %d).\n' "$rc" >&2
        cat "$tmp/clean.out" >&2
        return 2
    fi

    # The remaining arms run the script as a subprocess so its
    # scan-root and ledger variables are recomputed from the planted
    # environment, the way every other guard's self-test invokes itself.

    # Arm 2 — a planted bare door with no ledger entry must fail.
    mkdir -p "$tmp/inc/planted"
    cat >"$tmp/inc/planted/Leak.h" <<'PLANTED'
#pragma once
namespace planted {
struct Thing {
    int* steal_the_pointer(int* p) noexcept { return p; }
};
}  // namespace planted
PLANTED
    cat >"$tmp/ledger.txt" <<'LEDGER'
hatch-total 0
LEDGER
    rc=0
    ESCAPE_DOORS_ROOTS="$tmp/inc" ESCAPE_DOORS_LEDGER="$tmp/ledger.txt" \
        bash "${BASH_SOURCE[0]}" --quiet >"$tmp/bare.out" 2>&1 || rc=$?
    if (( rc != 1 )) || ! grep -q 'NEW BARE DOOR.*steal_the_pointer' "$tmp/bare.out"; then
        printf 'check-escape-doors: SELF-TEST FAILED — a planted bare door must be reported (got %d).\n' "$rc" >&2
        cat "$tmp/bare.out" >&2
        return 2
    fi

    # Arm 3 — the same door, ledgered, passes; and an accessor-named
    # sibling beside it is admitted with no ledger line at all.
    cat >>"$tmp/inc/planted/Leak.h" <<'PLANTED'
namespace planted {
struct Getter {
    int* data() noexcept { return nullptr; }
};
}  // namespace planted
PLANTED
    cat >"$tmp/ledger.txt" <<'LEDGER'
hatch-total 0
planted/Leak.h:steal_the_pointer  planted control: safe for the self-test
LEDGER
    rc=0
    ESCAPE_DOORS_ROOTS="$tmp/inc" ESCAPE_DOORS_LEDGER="$tmp/ledger.txt" \
        bash "${BASH_SOURCE[0]}" --quiet >"$tmp/ledgered.out" 2>&1 || rc=$?
    if (( rc != 0 )); then
        printf 'check-escape-doors: SELF-TEST FAILED — a ledgered bare door and an accessor must pass (got %d).\n' "$rc" >&2
        cat "$tmp/ledgered.out" >&2
        return 2
    fi

    # Arm 4 — a planted hatch raises the count over the pin and fails.
    cat >>"$tmp/inc/planted/Leak.h" <<'PLANTED'
namespace planted {
struct Owner {
    int* release() noexcept { return nullptr; }
};
}  // namespace planted
PLANTED
    rc=0
    ESCAPE_DOORS_ROOTS="$tmp/inc" ESCAPE_DOORS_LEDGER="$tmp/ledger.txt" \
        bash "${BASH_SOURCE[0]}" --quiet >"$tmp/hatch.out" 2>&1 || rc=$?
    if (( rc != 1 )) || ! grep -q 'HATCH DRIFT' "$tmp/hatch.out"; then
        printf 'check-escape-doors: SELF-TEST FAILED — a new hatch over the pin must be reported (got %d).\n' "$rc" >&2
        cat "$tmp/hatch.out" >&2
        return 2
    fi

    printf 'check-escape-doors: self-test passed — the real tree passes, a planted bare door is reported, a ledgered door and an accessor pass, and a hatch over the pin is reported.\n'
}

case "${1:-}" in
    "")          run_py check ;;
    --quiet)     run_py check ;;
    --list)      run_py list ;;
    --refresh)   run_py refresh ;;
    --self-test) self_test ;;
    *)           usage ;;
esac
