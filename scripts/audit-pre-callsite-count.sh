#!/usr/bin/env bash
# audit-pre-callsite-count.sh — contracts-infra adoption metric tracker.
#
# Walks the production tree (include/ + src/) and counts:
#   * CRUCIBLE_PRE    — in-body precondition cite (consteval-bypass shim)
#   * CRUCIBLE_POST   — in-body postcondition cite
#   * pre()           — P2900 precondition clause (parser-position)
#   * post()          — P2900 postcondition clause
#   * contract_assert — mid-body invariant
#   * decide::*       — named-predicate cite (catalog discharge)
#
# Only a cite in use counts.  Four kinds of text are removed before the
# count, because none of them is a contract that production code checks:
#   * the body of a test namespace, a namespace whose name has the word
#     test, tests, testing or selftest in it (`self_test`,
#     `detail::fn_self_test`, `lattice_test`).  Test code is excluded
#     where it lives in test/, and a header's self-test is test code too.
#   * a using-declaration, using-directive or using-enum declaration.  It
#     names a predicate but checks nothing, so a header whose only job is
#     to re-export contributes no cites.  An alias declaration
#     (`using X = ...`) stays, because a refinement type can hold a
#     predicate that is in use.
#   * a namespace head or namespace alias (`namespace crucible::decide {`).
#   * a comment or a string literal.
# The filter is a small C++ lexer in python3.  It reads comments, string,
# character and raw-string literals and preprocessor lines as opaque, so
# a brace inside one of them does not move the namespace depth.
#
# Output:
#   * Aggregate counts across the tree
#   * Per-decide-procedure cite count (the cite-ratio audit reads it —
#     every Decide procedure should accumulate ≥ 2 cites within 6 months
#     of its introduction, and an unloved one is trimmed)
#   * Top-10 files by combined contract-cite density (signal of
#     where the boundary discipline is concentrated; surfaces files
#     under-served by the discipline)
#
# Modes:
#   default          — human-readable summary to stdout
#   --json           — single JSON object for machine ingestion (CI baselines)
#   --baseline FILE  — write JSON snapshot to FILE for diff tracking
#   --check FILE     — compare current counts to a saved baseline; non-zero
#                      exit if a counter regressed (decreased without
#                      explanation), zero otherwise
#
# Written in plain shell idioms: ripgrep for the counts, python3 for the
# scope filter, set -euo pipefail, and no awk or sed.
#
# Exit status:
#   0  — successful audit (or --check pass)
#   1  — --check detected regression
#   2  — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Decide catalog ────────────────────────────────────────────────────
# Catalog of named predicates in safety/Decide.h.  Declared ABOVE the
# argument dispatcher so --self-test can synthesize a fixture that cites
# EVERY procedure.  A fixture built from this same array cannot desync
# from the catalog when a procedure is trimmed or a migration adds
# one.  Counts are computed fresh each run; audit-decide-cite-ratio.sh
# audits the ratios (every procedure > 0 cites at 6mo), and the unloved
# ones are trimmed.
decide_procedures=(
    is_non_zero
    in_range
    all_in_range
    aligned_in_range
    no_overflow_mul
    no_overflow_sum
    no_overflow_pow2_shift
    is_power_of_two_le
    factorization_eq
    coprime
    intervals_pairwise_disjoint
    intervals_cover_unit
    tier_replaces
    row_subset
    fmix_preserves_non_zero
    strictly_increasing
    weakly_increasing
    conjunction
    disjunction
    implies
    positive
    non_negative
    valid_span
)

usage() {
    cat >&2 <<'USAGE'
audit-pre-callsite-count.sh — count contracts-infra adoption in include/ + src/.

Usage:
  audit-pre-callsite-count.sh                 # human summary
  audit-pre-callsite-count.sh --json          # JSON to stdout
  audit-pre-callsite-count.sh --baseline F    # write JSON snapshot to F
  audit-pre-callsite-count.sh --check F       # compare to F; nonzero on regress
  audit-pre-callsite-count.sh --self-test     # plant a regression, verify catch
  audit-pre-callsite-count.sh -h | --help     # usage
USAGE
}

mode="human"
baseline_path=""
case "${1:-}" in
    --self-test)
        # A regression gate that never plants a regression has never
        # demonstrated it fires.  Build a synthetic production tree, then
        # drive --check against it THREE times:
        #
        #   1. baseline counts ABOVE the tree's  -> every counter regresses
        #                                          -> MUST exit non-zero
        #   2. baseline counts EQUAL to the tree -> nothing moved
        #                                          -> MUST exit 0
        #   3. baseline counts BELOW the tree    -> adoption grew
        #                                          -> MUST exit 0 (silent)
        #
        # Directions 1 and 2 are the two halves of the gate; direction 3
        # proves growth stays silent rather than tripping the guard.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include" "$tmp_root/src"

        # Every counted pattern matches at least once.  Each regex then
        # shows that it finds its form, and the zero-count tree below
        # shows the opposite case.  The loop writes two decide:: cites for
        # each entry of decide_procedures.  The namespace name has no test
        # word in it, because the guard does not count the body of a test
        # namespace.
        fixture="$tmp_root/include/selftest_cites.h"
        cat >"$fixture" <<'FIXTURE'
#pragma once
// Synthetic contracts-infra fixture for --self-test.
namespace crucible::planted {
inline void planted_macro_forms(int n) {
    CRUCIBLE_PRE(n > 0);
    CRUCIBLE_PRE_FAST(n > 0);
    CRUCIBLE_PRE_MSG(n > 0, "synthetic");
    CRUCIBLE_POST(r, r > 0);
    CRUCIBLE_POST_FAST(r, r > 0);
    CRUCIBLE_POST_MSG(r, r > 0, "synthetic");
    contract_assert(n > 0);
}
inline int planted_p2900_forms(int const n)
    pre (n > 0)
    post (r: r > 0)
{ return n; }
inline void planted_decide_cites(int n) {
FIXTURE
        for proc in "${decide_procedures[@]}"; do
            printf '    decide::%s(n);\n' "$proc" >>"$fixture"
            printf '    decide::%s(n);\n' "$proc" >>"$fixture"
        done
        cat >>"$fixture" <<'FIXTURE_TAIL'
}
}  // namespace crucible::planted
FIXTURE_TAIL
        # src/ must exist and hold a cpp-typed file: the scan passes both
        # include/ and src/ to rg, and a missing path is an rg error.
        cat >"$tmp_root/src/selftest_anchor.cpp" <<'ANCHOR'
// Synthetic translation unit for --self-test.
int crucible_selftest_anchor = 0;
ANCHOR

        checked_fields=(crucible_pre crucible_pre_fast crucible_pre_msg
                        crucible_post crucible_post_fast crucible_post_msg
                        contract_assert decide_total
                        total_pre_cites total_post_cites total_contract_cites)

        # Direction 2's baseline IS the tree's own snapshot — an exact match
        # by construction, which is what "no regression" must accept.
        match_baseline="$tmp_root/baseline_match.json"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --json >"$match_baseline" 2>"$tmp_root/json.err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — --json aborted on the synthetic tree.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$tmp_root/json.err")" >&2
            exit 2
        fi
        # An equal-baseline pass proves nothing if the filter removed the
        # planted cites.  Each planted form must be counted exactly.
        planted_counts=(crucible_pre:1 crucible_pre_fast:1 crucible_pre_msg:1
                        crucible_post:1 crucible_post_fast:1 crucible_post_msg:1
                        p2900_pre:1 p2900_post:1 contract_assert:1)
        for proc in "${decide_procedures[@]}"; do
            planted_counts+=("${proc}:2")
        done
        for planted in "${planted_counts[@]}"; do
            if ! grep -qE "\"${planted%%:*}\":${planted##*:}[,}]" "$match_baseline"; then
                printf 'audit-pre-callsite-count: SELF-TEST FAILED — the planted tree does not report %s.\n' \
                    "$planted" >&2
                printf '── json ─────────────\n%s\n────────────────────\n' \
                    "$(cat "$match_baseline")" >&2
                exit 2
            fi
        done

        # Direction 1: a baseline claiming counts the tree cannot meet.
        high_baseline="$tmp_root/baseline_high.json"
        { printf '{'
          high_first=1
          for field in "${checked_fields[@]}"; do
              if [[ $high_first -eq 0 ]]; then printf ','; fi
              high_first=0
              printf '"%s":9999' "$field"
          done
          printf '}\n'
        } >"$high_baseline"

        # Direction 3: a baseline the tree has already grown past.
        low_baseline="$tmp_root/baseline_low.json"
        { printf '{'
          low_first=1
          for field in "${checked_fields[@]}"; do
              if [[ $low_first -eq 0 ]]; then printf ','; fi
              low_first=0
              printf '"%s":0' "$field"
          done
          printf '}\n'
        } >"$low_baseline"

        # ── Direction 1 — MUST fail ──────────────────────────────────
        regress_err="$tmp_root/regress.err"
        if CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" --check "$high_baseline" \
           >/dev/null 2>"$regress_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — planted regression not caught.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$regress_err")" >&2
            exit 2
        fi
        # Non-zero is not enough: prove it failed for the REGRESSION reason
        # and not because the scan blew up on the synthetic tree.
        for field in "${checked_fields[@]}"; do
            if ! grep -qF "REGRESSION ${field}: 9999 ->" "$regress_err"; then
                printf 'audit-pre-callsite-count: SELF-TEST FAILED — no REGRESSION diagnostic for %s.\n' \
                    "$field" >&2
                printf '── checker stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$regress_err")" >&2
                exit 2
            fi
        done

        # ── Direction 2 — equal counts MUST pass ─────────────────────
        match_err="$tmp_root/match.err"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --check "$match_baseline" \
             >/dev/null 2>"$match_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — exact-match baseline reported a regression.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$match_err")" >&2
            exit 2
        fi

        # ── Direction 3 — growth MUST stay silent ────────────────────
        grow_err="$tmp_root/grow.err"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --check "$low_baseline" \
             >/dev/null 2>"$grow_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — adoption growth tripped the gate.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$grow_err")" >&2
            exit 2
        fi

        # ── Alias cites — re-exports and self-tests MUST NOT count ───
        # The tree has one real cite of each form it uses, in a namespace
        # whose name holds the letters "test" inside a longer word
        # (attestation).  Every
        # other cite is a re-export, a namespace head or alias, or the body
        # of a test namespace.  The self-test namespace also holds braces
        # in a character literal, a string, a raw string and a digit
        # separator, so a lexer that miscounts braces ends it early or runs
        # it to the end of the file.  Either error moves a count.
        alias_root="$tmp_root/alias_tree"
        mkdir -p "$alias_root/include" "$alias_root/src"
        cp "$tmp_root/src/selftest_anchor.cpp" "$alias_root/src/selftest_anchor.cpp"
        alias_fixture="$alias_root/include/alias_cites.h"
        cat >"$alias_fixture" <<'ALIAS_FIXTURE'
#pragma once
// Synthetic re-export and self-test fixture for --self-test.
namespace crucible::fixy::decide {
using ::crucible::decide::coprime;
using ::crucible::decide::
    Interval;
namespace catalog_alias = ::crucible::decide;
}  // namespace crucible::fixy::decide
namespace crucible::decide::oracle {
}  // namespace crucible::decide::oracle
namespace crucible::fixy::decide::self_test {
static_assert(std::is_same_v<decltype(&::crucible::fixy::decide::coprime<int>),
                             decltype(&::crucible::decide::coprime<int>)>);
inline constexpr char close_brace = '}';
inline constexpr const char* closers = "}}";
inline constexpr const char* raw_closers = R"(}})";
inline constexpr int separated = 1'000;
inline void runtime_smoke_test(int n) {
    if (n > 0) { CRUCIBLE_PRE(decide::coprime(n, 3)); }
    contract_assert(decide::coprime(n, 3));
}
}  // namespace crucible::fixy::decide::self_test
namespace crucible::detail::coprime_self_test {
inline bool probe(int n) { return decide::coprime(n, 9); }
}  // namespace crucible::detail::coprime_self_test
namespace crucible::lattice_test {
inline bool probe(int n) { return decide::coprime(n, 11); }
}  // namespace crucible::lattice_test
namespace crucible::attestation {
inline bool real_use(int n) {
    CRUCIBLE_PRE(decide::coprime(n, 7));
    return true;
}
}  // namespace crucible::attestation
ALIAS_FIXTURE
        alias_json="$tmp_root/alias.json"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$alias_root" \
             bash "${BASH_SOURCE[0]}" --json >"$alias_json" 2>"$tmp_root/alias.err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — --json aborted on the alias tree.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$tmp_root/alias.err")" >&2
            exit 2
        fi
        # The fixture must carry the alias cites a plain text count sees.
        # Otherwise the exact counts below prove nothing.
        alias_raw_total=$( { rg -o 'decide::' "$alias_fixture" || true; } | wc -l)
        if [[ "$alias_raw_total" -lt 10 ]]; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — the alias fixture has %d decide:: tokens, expected 10 or more.\n' \
                "$alias_raw_total" >&2
            exit 2
        fi
        for planted in decide_total:1 coprime:1 crucible_pre:1 contract_assert:0 \
                       p2900_pre:0 total_contract_cites:1; do
            if ! grep -qE "\"${planted%%:*}\":${planted##*:}[,}]" "$alias_json"; then
                printf 'audit-pre-callsite-count: SELF-TEST FAILED — the alias tree does not report %s.\n' \
                    "$planted" >&2
                printf '── json ─────────────\n%s\n────────────────────\n' \
                    "$(cat "$alias_json")" >&2
                exit 2
            fi
        done

        # ── Zero counts — a tree with no cites MUST report zeros ─────
        # A tree with no cites gives a count of zero for each counter.  The
        # guard must report each zero and must not stop.
        zero_root="$tmp_root/zero_tree"
        mkdir -p "$zero_root/include" "$zero_root/src"
        cp "$tmp_root/src/selftest_anchor.cpp" "$zero_root/src/selftest_anchor.cpp"
        zero_json="$tmp_root/zero.json"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$zero_root" \
             bash "${BASH_SOURCE[0]}" --json >"$zero_json" 2>"$tmp_root/zero.err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — --json aborted on a tree with no cites.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$tmp_root/zero.err")" >&2
            exit 2
        fi
        for field in "${checked_fields[@]}" "${decide_procedures[@]}"; do
            if ! grep -qE "\"${field}\":0[,}]" "$zero_json"; then
                printf 'audit-pre-callsite-count: SELF-TEST FAILED — %s is not 0 on a tree with no cites.\n' \
                    "$field" >&2
                printf '── json ─────────────\n%s\n────────────────────\n' \
                    "$(cat "$zero_json")" >&2
                exit 2
            fi
        done
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$zero_root" \
             bash "${BASH_SOURCE[0]}" >/dev/null 2>"$tmp_root/zero_human.err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — the human summary aborted on a tree with no cites.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$tmp_root/zero_human.err")" >&2
            exit 2
        fi

        # ── Scan errors — a missing scan path MUST fail, not count 0 ─
        # The guard treats only the no-match status of `rg` as a zero
        # count.  Every other failure must stop the guard.
        broken_root="$tmp_root/broken_tree"
        mkdir -p "$broken_root/include"
        if CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$broken_root" \
           bash "${BASH_SOURCE[0]}" --json >/dev/null 2>&1; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — a tree with no src/ directory was counted as zero.\n' >&2
            exit 2
        fi

        printf 'audit-pre-callsite-count: self-test passed — regression caught (%d counters), exact-match and growth both accepted, alias cites not counted, zero counts reported, scan errors fatal.\n' \
            "${#checked_fields[@]}" >&2
        exit 0
        ;;
    --json)
        mode="json"
        ;;
    --baseline)
        if [[ $# -lt 2 ]]; then usage; exit 2; fi
        mode="baseline"
        baseline_path="$2"
        ;;
    --check)
        if [[ $# -lt 2 ]]; then usage; exit 2; fi
        mode="check"
        baseline_path="$2"
        ;;
    -h|--help)
        usage; exit 0
        ;;
    "")
        ;;
    *)
        printf 'audit-pre-callsite-count: unknown argument: %s\n' "$1" >&2
        usage; exit 2
        ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'audit-pre-callsite-count: ripgrep (rg) is required\n' >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'audit-pre-callsite-count: python3 is required for the scope filter\n' >&2
    exit 2
fi

# ── Scan-root override for --self-test recursion ─────────────────────
# The script's OWN location still resolves through BASH_SOURCE above;
# only the tree it counts moves.
scan_root="${CRUCIBLE_PRE_CALLSITE_TEST_ROOT:-$root}"

# ── Aggregate counters ────────────────────────────────────────────────
# rg --type=cpp picks up .h / .hpp / .cpp / .cc — all the production
# C++26 sources.  --glob excludes vendor + build trees.
common_globs=(--type=cpp \
              --glob '!build*/**' \
              --glob '!cmake-build-*/**' \
              --glob '!third_party/**' \
              --glob '!external/**' \
              --glob '!vendor/**' \
              --glob '!test/**' \
              --glob '!bench/**')

# rg_matches ARGS... — run rg, and treat "no match" as a result.
#
# `rg` exits 1 when it finds nothing.  Under `set -o pipefail`, a pipeline
# with such a stage fails, and a count of zero stops the script.  This
# wrapper gives an empty output for exit 1.  Exit 2 (a bad pattern or a
# path that cannot be read) stays a failure.  A broken scan does not read
# as a zero count.
rg_matches() {
    local rg_status=0
    rg "$@" || rg_status=$?
    if [[ $rg_status -gt 1 ]]; then
        printf 'audit-pre-callsite-count: rg failed with exit %d\n' "$rg_status" >&2
        return "$rg_status"
    fi
    return 0
}

# ── Scope filter: remove every text that is not a cite in use ─────────
# The program copies each scanned file into count_root and replaces with
# spaces, line breaks kept:
#   * the body of a test namespace (the word test, tests, testing or
#     selftest in its name, where words are split at `_`),
#   * a using-declaration, using-directive or using-enum (no `=` in it),
#   * a namespace head or namespace alias,
#   * a comment or a string literal.
# An alias declaration (`using X = ...`) stays, because a refinement type
# can hold a predicate that is in use.  Preprocessor lines are opaque and
# stay.  The filter reads each file in one pass.
cite_filter_program='
import os
import re
import sys

# A raw string before a plain string, a character literal before an
# identifier (so L"x" and L\x27x\x27 are literals), and a pp-number before an
# identifier (so the digit separator in 1\x27000 opens no character literal).
TOKEN = re.compile(
    r"(?P<pp>^[ \t]*#(?:\\\n|[^\n])*)"
    r"|(?P<lc>//[^\n]*)"
    r"|(?P<bc>/\*.*?(?:\*/|\Z))"
    r"|(?P<raw>(?:u8|[uUL])?R\x22(?P<delim>[^()\\\s\x22]{0,16})\(.*?\)(?P=delim)\x22)"
    r"|(?P<str>(?:u8|[uUL])?\x22(?:\\.|[^\x22\\\n])*\x22?)"
    r"|(?P<chr>(?:u8|[uUL])?\x27(?:\\.|[^\x27\\\n])*\x27?)"
    r"|(?P<num>\.?[0-9](?:[eEpP][+-]|[\x27\w.])*)"
    r"|(?P<id>[A-Za-z_]\w*)"
    r"|(?P<punct>[{};()=\[])",
    re.MULTILINE | re.DOTALL,
)
TEST_WORD = re.compile(r"(?:^|_)(?:self_?test|tests?|testing)(?:_|$)", re.IGNORECASE)


def blank_ranges(text):
    ranges = []
    stack = []          # one flag per open brace: true inside a test namespace
    names = None        # identifiers after the keyword namespace, else None
    names_at = 0
    using_at = None     # offset of an open using statement
    using_level = (0, 0)
    using_alias = False
    paren = 0
    test_open = 0
    last = ""
    for m in TOKEN.finditer(text):
        kind = m.lastgroup
        tok = m.group()
        if kind == "id":
            if tok == "namespace":
                names = []
                names_at = m.start()
            elif tok == "using" and using_at is None and last != "[":
                using_at = m.start()
                using_level = (len(stack), paren)
                using_alias = False
            elif names is not None:
                names.append(tok)
            last = tok
            continue
        if kind in ("lc", "bc", "str", "raw"):
            ranges.append((m.start(), m.end()))
            continue
        if kind != "punct":
            continue
        at_using_level = using_at is not None and (len(stack), paren) == using_level
        if tok == "{":
            is_test = names is not None and any(TEST_WORD.search(n) for n in names)
            if names is not None:
                ranges.append((names_at, m.start()))
            parent = stack[-1] if stack else False
            if is_test and not parent:
                test_open = m.start()
            if at_using_level:
                using_at = None
            stack.append(parent or is_test)
            names = None
        elif tok == "}":
            if stack:
                closing = stack.pop()
                if closing and not (stack[-1] if stack else False):
                    ranges.append((test_open, m.end()))
            if using_at is not None and len(stack) < using_level[0]:
                using_at = None
            names = None
        elif tok == ";":
            if names is not None:
                ranges.append((names_at, m.end()))
            names = None
            if at_using_level:
                if not using_alias:
                    ranges.append((using_at, m.end()))
                using_at = None
        elif tok == "=":
            if at_using_level:
                using_alias = True
        elif tok == "(":
            paren += 1
            names = None
        elif tok == ")":
            paren = max(paren - 1, 0)
            names = None
        last = tok
    if stack and stack[-1]:
        ranges.append((test_open, len(text)))
    return ranges


def blanked(text, ranges):
    pieces = []
    cursor = 0
    for start, end in sorted(ranges):
        if end <= cursor:
            continue
        start = max(start, cursor)
        pieces.append(text[cursor:start])
        pieces.append(re.sub(r"[^\n]", " ", text[start:end]))
        cursor = end
    pieces.append(text[cursor:])
    return "".join(pieces)


scan_root, count_root = sys.argv[1], sys.argv[2]
for line in sys.stdin:
    path = line.rstrip("\n")
    if not path:
        continue
    target = os.path.join(count_root, os.path.relpath(path, scan_root))
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(path, encoding="utf-8", errors="surrogateescape") as source:
        text = source.read()
    with open(target, "w", encoding="utf-8", errors="surrogateescape") as mirror:
        mirror.write(blanked(text, blank_ranges(text)))
'

# The file list comes from rg with the same globs as the counts, so the
# filtered tree holds exactly the files the counts would read.  A missing
# scan path fails here, with the rg exit status.
count_root="$(mktemp -d)"
trap 'rm -rf "$count_root"' EXIT
rg_matches --files "${common_globs[@]}" "$scan_root/include" "$scan_root/src" \
| python3 -c "$cite_filter_program" "$scan_root" "$count_root"
mkdir -p "$count_root/include" "$count_root/src"

count_pattern() {
    # Count OCCURRENCES, not lines.  `rg -c` reports one line per file with at
    # least one match, so two cites sharing a line count as one and the total
    # moves whenever a reformat joins or splits lines.  `rg -o` emits one line
    # per match, which is the quantity this audit actually claims to report.
    #
    # Count CODE, not prose.  A cite named in a comment is not adoption, and
    # counting it makes the metric track how much the tree talks about
    # contracts rather than how much it uses them.  Two guards do that: the
    # line must not open as a comment, and no `//` may precede the match on
    # the line.  A cite named inside a multi-line block comment still counts,
    # which is the residual this cheap form accepts.
    #
    # The two paragraphs above describe the intent.  A single `rg -oP` with
    # `^(?!...)(?:(?!//).)*?\K<pattern>` did NOT implement it: the `^` anchor
    # can match only once per line, so the lazy prefix plus `\K` yields the
    # FIRST cite on a line and no more.  That silently reinstated the
    # line-counting the doc-block disclaims, and it stayed invisible until a
    # clang-format pass joined two `pre(` clauses in RefreshDaemon.h onto one
    # line — the file kept both contracts, both still abort on violation, and
    # the audit reported a regression from 2 to 1.  A metric a reformat can
    # move is not measuring adoption.
    #
    # Three stages instead, so the line filter and the occurrence count are
    # separate concerns: select lines that do not open as a comment, cut each
    # line at its first `//`, then count occurrences in what is left.
    #
    # Each stage goes through rg_matches.  A count of zero then gives 0, and
    # a scan error makes the pipeline fail.  The caller assigns the result at
    # the top level, where `set -e` stops the script on that failure.
    #
    # The stages read the filtered tree in count_root, where comments,
    # strings, re-exports and test namespaces are already spaces.  The two
    # comment guards above are then a second line of defence.
    local pattern="$1"
    rg_matches -N --no-filename -P "^(?!\s*(?://|\*|/\*)).*${pattern}" "${common_globs[@]}" \
        "$count_root/include" "$count_root/src" \
    | rg_matches --passthru -P '//.*$' -r '' \
    | rg_matches -oP "${pattern}" \
    | wc -l
}

# count_decide_procedure PROC — occurrences of `decide::PROC` in code.
#
# The same three stages as every other counter, so two cites on one line
# count as two.  The catalogs define their procedures unqualified, and
# their self-tests live in test namespaces, so no catalog path needs to be
# excluded by name.
count_decide_procedure() {
    count_pattern "decide::${1}\\b"
}

# ── Counts ────────────────────────────────────────────────────────────
# Patterns are anchored to avoid false positives:
#   CRUCIBLE_PRE\b — word boundary so CRUCIBLE_PRE_FAST / CRUCIBLE_PRE_MSG
#                    are counted separately (they're variants of the same
#                    cite class but distinct mechanisms).
#   (?<![\w:])pre\s*\( — a pre() clause anywhere on the line.  Anchoring to
#                    line start was wrong: whether the clause shares a line
#                    with the signature is a formatting choice, not a change
#                    in contract adoption.  The lookbehind excludes both
#                    identifiers ending in "pre" (`prepare`) and qualified
#                    names (`ns::pre`).
#   contract_assert\b — same boundary discipline.
crucible_pre=$(count_pattern 'CRUCIBLE_PRE\b')
crucible_pre_fast=$(count_pattern 'CRUCIBLE_PRE_FAST\b')
crucible_pre_msg=$(count_pattern 'CRUCIBLE_PRE_MSG\b')
crucible_post=$(count_pattern 'CRUCIBLE_POST\b')
crucible_post_fast=$(count_pattern 'CRUCIBLE_POST_FAST\b')
crucible_post_msg=$(count_pattern 'CRUCIBLE_POST_MSG\b')

p2900_pre=$(count_pattern '(?<![\w:])pre\s*\(')
p2900_post=$(count_pattern '(?<![\w:])post\s*\(')
contract_assert=$(count_pattern 'contract_assert\b')

decide_total=$(count_pattern 'decide::')

total_pre_cites=$((crucible_pre + crucible_pre_fast + crucible_pre_msg + p2900_pre))
total_post_cites=$((crucible_post + crucible_post_fast + crucible_post_msg + p2900_post))
total_contract_cites=$((total_pre_cites + total_post_cites + contract_assert))

# ── Per-decide-procedure cite count ───────────────────────────────────
# The catalog itself (decide_procedures) is declared near the top of the
# script, above the argument dispatcher, so --self-test can build its
# fixture from the same array.  The script takes the counts here, one time
# and at the top level, where `set -e` stops the script on a scan error.
# Inside the print functions a failure does not stop it, because --check
# calls print_json in a command substitution, and bash clears `set -e`
# there.
declare -A decide_counts=()
for proc in "${decide_procedures[@]}"; do
    decide_counts[$proc]=$(count_decide_procedure "$proc")
done

# ── Top-N file density ────────────────────────────────────────────────
# Files with the most combined contract cites — the "boundary
# discipline frontier" — useful for spotting headers that have absorbed
# the migration sweep vs. ones still holding raw assertions.
top_n=10

print_human() {
    cat <<HEADER
=== Crucible contracts-infra adoption metrics ===
(production tree: include/ + src/, excludes test/ bench/ build/ third_party/)

── Aggregate ─────────────────────────────────────────
  CRUCIBLE_PRE        $crucible_pre
  CRUCIBLE_PRE_FAST   $crucible_pre_fast
  CRUCIBLE_PRE_MSG    $crucible_pre_msg
  CRUCIBLE_POST       $crucible_post
  CRUCIBLE_POST_FAST  $crucible_post_fast
  CRUCIBLE_POST_MSG   $crucible_post_msg
  pre()  (P2900)      $p2900_pre
  post() (P2900)      $p2900_post
  contract_assert     $contract_assert
  decide:: cites      $decide_total

  Total pre-cites     $total_pre_cites
  Total post-cites    $total_post_cites
  Total contract-cites $total_contract_cites

── Per-decide-procedure cite count ──────────────────
HEADER
    for proc in "${decide_procedures[@]}"; do
        printf '  %-30s %s\n' "decide::$proc" "${decide_counts[$proc]}"
    done

    cat <<MIDDLE

── Top-$top_n files by contract-cite density ─────────
MIDDLE

    # Build the per-file cite count: pre + post + contract_assert.
    # rg -c gives "file:count" lines; we sort by count desc, take top N.
    rg_matches -cP '(CRUCIBLE_PRE|CRUCIBLE_POST|^\s*pre\s*\(|^\s*post\s*\(|contract_assert)\b' \
       "${common_globs[@]}" \
       "$count_root/include" "$count_root/src" \
       | sort -t: -k2 -nr -s \
       | head -n "$top_n" \
       | while IFS=: read -r file count; do
           rel="${file#"$count_root"/}"
           printf '  %-60s %s\n' "$rel" "$count"
         done

    cat <<FOOTER

── Notes ─────────────────────────────────────────────
  Per CLAUDE.md §XII: prefer Refined<P, T> parameter types over pre()
  cites where the predicate is structurally provable (the subsumption
  discipline of check-refined-pre-subsumption.sh).  Prefer named
  decide::* cites over anonymous CRUCIBLE_PRE expressions where a
  catalog entry fits.

  Run with --json for machine-readable output.
  Run with --baseline FILE to snapshot for CI diff tracking.
  Run with --check FILE to flag regressions against a baseline.
FOOTER
}

print_json() {
    # Emit a flat JSON object.  Field ordering chosen so a diff between
    # two snapshots reads naturally: aggregates first, per-procedure
    # next, top-files last.  No external jq dependency — printf builds
    # the bytes directly.
    printf '{'
    printf '"crucible_pre":%s,' "$crucible_pre"
    printf '"crucible_pre_fast":%s,' "$crucible_pre_fast"
    printf '"crucible_pre_msg":%s,' "$crucible_pre_msg"
    printf '"crucible_post":%s,' "$crucible_post"
    printf '"crucible_post_fast":%s,' "$crucible_post_fast"
    printf '"crucible_post_msg":%s,' "$crucible_post_msg"
    printf '"p2900_pre":%s,' "$p2900_pre"
    printf '"p2900_post":%s,' "$p2900_post"
    printf '"contract_assert":%s,' "$contract_assert"
    printf '"decide_total":%s,' "$decide_total"
    printf '"total_pre_cites":%s,' "$total_pre_cites"
    printf '"total_post_cites":%s,' "$total_post_cites"
    printf '"total_contract_cites":%s,' "$total_contract_cites"

    printf '"decide_per_procedure":{'
    local first=1
    for proc in "${decide_procedures[@]}"; do
        if [[ $first -eq 0 ]]; then printf ','; fi
        first=0
        printf '"%s":%s' "$proc" "${decide_counts[$proc]}"
    done
    printf '}'

    printf '}\n'
}

case "$mode" in
    human)
        print_human
        ;;
    json)
        print_json
        ;;
    baseline)
        print_json > "$baseline_path"
        printf 'audit-pre-callsite-count: baseline written to %s\n' "$baseline_path" >&2
        ;;
    check)
        # Diff strategy: any checked counter that DECREASED is flagged;
        # increases are silent (adoption growth is good; regression is the
        # signal).
        #
        # The legacy P2900 forms (p2900_pre / p2900_post) are deliberately
        # EXCLUDED from the must-not-decrease set: the codebase is actively
        # migrating vanilla `pre()` / `post (r:...)` to the stronger
        # CRUCIBLE_PRE / CRUCIBLE_POST macros (consteval-firing, toolchain-
        # independent).  That migration
        # MOVES a cite from p2900_pre to crucible_pre, so p2900_pre shrinks by
        # design.  Genuine coverage loss (a pre() deleted, not migrated) still
        # trips the AGGREGATE guards (total_pre_cites / total_post_cites /
        # total_contract_cites), which net out the migration and only fall when
        # real coverage drops.  The preferred crucible_* forms stay in the set
        # (a drop there IS a real regression).
        if [[ ! -f "$baseline_path" ]]; then
            printf 'audit-pre-callsite-count: baseline file not found: %s\n' \
                   "$baseline_path" >&2
            exit 2
        fi
        current_json="$(print_json)"
        # Field-by-field comparison.  We use printf+rg rather than jq to
        # keep the script self-contained, with ripgrep as its one
        # dependency.
        regressed=0
        for field in crucible_pre crucible_pre_fast crucible_pre_msg \
                     crucible_post crucible_post_fast crucible_post_msg \
                     contract_assert \
                     decide_total total_pre_cites total_post_cites \
                     total_contract_cites; do
            # Tolerate either compact `"field":42` or pretty `"field": 42`
            # JSON formatting — third-party tools (jq, python json.dump
            # default) emit the latter; our own writer emits the former.
            old="$(rg -oP "\"${field}\":\s*\K[0-9]+" "$baseline_path" \
                    | head -n1 || printf '0')"
            new="$(printf '%s' "$current_json" \
                    | rg -oP "\"${field}\":\s*\K[0-9]+" \
                    | head -n1 || printf '0')"
            if [[ "$new" -lt "$old" ]]; then
                printf 'audit-pre-callsite-count: REGRESSION %s: %s -> %s\n' \
                       "$field" "$old" "$new" >&2
                regressed=1
            fi
        done
        if [[ "$regressed" -ne 0 ]]; then
            printf 'audit-pre-callsite-count: contracts-infra adoption regressed; investigate.\n' >&2
            exit 1
        fi
        printf 'audit-pre-callsite-count: no regression vs %s\n' \
               "$baseline_path" >&2
        ;;
esac
