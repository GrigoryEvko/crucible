#!/usr/bin/env bash
#
# The testing door hands out a context with no key.  Both trees have one:
# foundation::effects::testing in the new tree, and
# crucible::effects::testing in the old tree.  bg(), init() and test()
# each mint a context, and TestWitness is the friend that reaches the
# keys.  A context is what every ctx-bound mint checks for, so a use of the
# door in code that ships hands out authority that no mint gave.
#
# This guard scans the trees that ship and counts each use of the door in
# code.  A comment or a literal that names the door is not a use.  Each
# file with a use needs an entry in scripts/ctx-testing-boundary-allowlist.txt,
# and the entry states how many uses it admits, so a new use in a listed
# file is refused too.  An entry above the count of its file is stale and
# fails, so the list drains with the code.
#
# The trees that ship: include/foundation, include/fixy, include/crucible,
# src, vessel, tools and examples.  test/, bench/ and fuzz/ are not
# scanned, because taking the test path is what they are for.
#
# What this guard does not see, stated rather than implied: a name of the
# door that a macro builds by token pasting.  The scan reads source text,
# and review sees such a macro.
#
#   --self-test   plant uses in the new tree and the old tree, a clean
#                 file, a listed file with a counted use, and a stale
#                 entry, and prove each verdict.
#
# Exit 0 clean, 1 on a violation or a stale entry, 2 on a usage error or a
# failed self-test.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scripts_dir="$(dirname "${BASH_SOURCE[0]}")"

usage() {
    printf 'usage: %s [--self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

# Scans the trees that ship under the root, against the allowlist.
run_scan() {
    python3 - "$1" "$2" "$scripts_dir" <<'PY'
import re
import sys
from collections import defaultdict
from pathlib import Path

root = Path(sys.argv[1]).resolve()
allowlist = Path(sys.argv[2])
sys.path.insert(0, sys.argv[3])
from cxx_lex import blank, line_of, splice  # noqa: E402

SCAN_DIRS = ("include/foundation", "include/fixy", "include/crucible", "src", "vessel", "tools", "examples")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".c", ".cc", ".cpp", ".cxx", ".cppm"})
# One use of the door: a factory or the friend named through the testing
# namespace, however it is qualified, the friend by its own name, and a
# using-directive or a namespace alias that brings the testing namespace
# into scope, because the calls it enables name no testing::.
DOOR = re.compile(r"(?<!\w)testing\s*::\s*(?:bg|init|test|TestWitness)\b"
                  r"|\bTestWitness\b"
                  r"|\busing\s+namespace\s+(?:::\s*)?(?:\w+\s*::\s*)*testing\b"
                  r"|\bnamespace\s+\w+\s*=\s*(?:::\s*)?(?:\w+\s*::\s*)*testing\b")
ENTRY = re.compile(r"^(?P<path>\S+?)(?: x(?P<count>[1-9][0-9]*))?\s+—\s+\S")


def uses_in(path: Path) -> list[int]:
    """The line of each use of the door in the code of the file.

    Complexity: linear in the length of the file."""
    joined, joins = splice(path.read_text(errors="replace"))
    code, _ = blank(joined, blank_literals=True)
    return [line_of(joined, joins, match.start()) for match in DOOR.finditer(code)]


def read_allowlist() -> dict[str, tuple[int, int]]:
    """Each listed path, the number of uses it admits, and its line in the list."""
    entries: dict[str, tuple[int, int]] = {}
    for number, raw in enumerate(allowlist.read_text().splitlines() if allowlist.is_file() else [], 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if match is None:
            print(f"check-ctx-testing-boundary: {allowlist.name}:{number} has no path, count and reason.",
                  file=sys.stderr)
            entries[f"<malformed {number}>"] = (0, number)
            continue
        entries[match.group("path")] = (int(match.group("count") or 1), number)
    return entries


found: dict[str, list[int]] = defaultdict(list)
for directory in SCAN_DIRS:
    base = root / directory
    if not base.is_dir():
        continue
    for path in sorted(base.rglob("*")):
        if path.suffix in SUFFIXES and path.is_file():
            lines = uses_in(path)
            if lines:
                found[path.relative_to(root).as_posix()] = lines

entries = read_allowlist()
failed = False
for path, lines in sorted(found.items()):
    admitted = entries.get(path, (0, 0))[0]
    if len(lines) > admitted:
        failed = True
        print(f"check-ctx-testing-boundary: {path} uses the testing door {len(lines)} time(s) at lines "
              f"{', '.join(map(str, lines))}, and its entry admits {admitted}.", file=sys.stderr)
        print("  The door mints a context with no key, and a context is what every ctx-bound mint checks for.\n"
              "  Code that ships takes its context from a real mint.  A self-test in a header may use the door:\n"
              "  give its file an entry with the count and a sentence saying why.", file=sys.stderr)
for path, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
    if len(found.get(path, [])) < admitted:
        failed = True
        print(f"check-ctx-testing-boundary: {allowlist.name}:{number} admits {admitted} use(s) in {path}, and the "
              f"file has {len(found.get(path, []))}.  Lower or remove the entry.", file=sys.stderr)
total = sum(len(lines) for lines in found.values())
print(f"check-ctx-testing-boundary: {total} use(s) of the testing door in {len(found)} file(s), "
      f"{'refused' if failed else 'each one admitted'}.", file=sys.stderr)
sys.exit(1 if failed else 0)
PY
}

self_test() {
    local tmp rc
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/include/foundation/effects" "$tmp/include/crucible/effects" "$tmp/src" "$tmp/test" "$tmp/bench"

    # A new-tree header and an old-tree source that reach for the door.
    cat >"$tmp/include/foundation/effects/Planted.h" <<'EOF'
#pragma once
inline auto forged() noexcept { return ::foundation::effects::testing::init(); }
EOF
    cat >"$tmp/src/planted.cpp" <<'EOF'
namespace crucible::effects::testing { struct Door { static int bg() { return 0; } }; }
inline int old_tree() { using namespace crucible::effects; return testing::bg(); }
namespace eff = ::crucible::effects;
inline int through_alias() { return eff::testing::init(); }
namespace door = ::crucible::effects::testing;
inline int in_scope() { using namespace crucible::effects::testing; return bg(); }
EOF
    # A file that names the door only in a comment and a literal.
    cat >"$tmp/include/crucible/effects/Clean.h" <<'EOF'
#pragma once
// effects::testing::bg() hands out a context, so this header never calls it.
inline const char* note = "testing::bg() and TestWitness";
inline int clean() noexcept { return 0; }
EOF
    # A listed self-test header with two uses.
    cat >"$tmp/include/crucible/effects/Listed.h" <<'EOF'
#pragma once
inline void self_test() { (void)::crucible::effects::testing::bg(); (void)::crucible::effects::testing::test(); }
EOF
    # Test and bench code is not scanned.
    printf 'inline auto t() { return effects::testing::test(); }\n' >"$tmp/test/t.cpp"
    printf 'inline auto b() { return effects::testing::bg(); }\n' >"$tmp/bench/b.cpp"
    local list="$tmp/allowlist.txt"
    printf '%s\n' 'include/crucible/effects/Listed.h x2 — a planted self-test' >"$list"

    set +e
    run_scan "$tmp" "$list" >"$tmp/out" 2>&1
    rc=$?
    set -e
    if [[ $rc -ne 1 ]] || ! rg -q -F 'include/foundation/effects/Planted.h uses the testing door 1 time(s)' "$tmp/out" \
        || ! rg -q -F 'src/planted.cpp uses the testing door 4 time(s)' "$tmp/out"; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — a use in the new tree or the old tree was not reported (exit %s).\n' "$rc" >&2
        rg -N '' "$tmp/out" >&2 || true
        return 2
    fi
    if rg -q 'Clean\.h|Listed\.h|test/t\.cpp|bench/b\.cpp' "$tmp/out"; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — a comment, a literal, a listed file or test code was reported.\n' >&2
        rg -N '' "$tmp/out" >&2 || true
        return 2
    fi
    printf 'check-ctx-testing-boundary --self-test: uses in the new tree and the old tree are reported, through a namespace alias, a using-directive and a door alias too, and comments, literals, a listed file and test code are not.\n'

    # A third use in the listed file exceeds its count.
    rm -f "$tmp/include/foundation/effects/Planted.h" "$tmp/src/planted.cpp"
    printf 'inline void more() { (void)::crucible::effects::testing::init(); }\n' \
        >>"$tmp/include/crucible/effects/Listed.h"
    set +e
    run_scan "$tmp" "$list" >"$tmp/out" 2>&1
    rc=$?
    set -e
    if [[ $rc -ne 1 ]] || ! rg -q -F 'Listed.h uses the testing door 3 time(s)' "$tmp/out"; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — a new use in a listed file was not reported (exit %s).\n' "$rc" >&2
        rg -N '' "$tmp/out" >&2 || true
        return 2
    fi
    printf 'check-ctx-testing-boundary --self-test: a new use in a listed file is reported, as expected.\n'

    # An entry above the count of its file, and an entry for a file with
    # no use, are stale.
    printf '%s\n' 'include/crucible/effects/Listed.h x4 — a planted self-test' \
        'include/crucible/effects/Absent.h — never existed' >"$list"
    set +e
    run_scan "$tmp" "$list" >"$tmp/out" 2>&1
    rc=$?
    set -e
    if [[ $rc -ne 1 ]] || ! rg -q -F 'admits 4 use(s) in include/crucible/effects/Listed.h, and the file has 3' "$tmp/out" \
        || ! rg -q -F 'admits 1 use(s) in include/crucible/effects/Absent.h, and the file has 0' "$tmp/out"; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — a stale entry was not reported (exit %s).\n' "$rc" >&2
        rg -N '' "$tmp/out" >&2 || true
        return 2
    fi
    printf 'check-ctx-testing-boundary --self-test: stale entries are reported, as expected.\n'

    # A satisfied list reports clean.
    printf '%s\n' 'include/crucible/effects/Listed.h x3 — a planted self-test' >"$list"
    run_scan "$tmp" "$list" >"$tmp/out" 2>&1 || {
        printf 'check-ctx-testing-boundary --self-test: FAIL — a satisfied list was refused.\n' >&2
        rg -N '' "$tmp/out" >&2 || true
        return 2
    }
    printf 'check-ctx-testing-boundary --self-test: PASS.\n'
}

case "${1-}" in
    --self-test) self_test ;;
    "") run_scan "$root" "$root/scripts/ctx-testing-boundary-allowlist.txt" ;;
    *) usage ;;
esac
