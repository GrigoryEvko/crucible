#!/usr/bin/env bash
# check-ci-guard-labels.sh — ci_guard label-coverage guard.
#
# `ctest -L ci_guard` is the local pre-merge gate: the one command a
# developer runs to catch a discipline regression BEFORE pushing.  A
# guard registered in test/CMakeLists.txt without the `ci_guard` label
# is invisible to that command, so the gate reports a pass while never
# having run the guard at all.  That is worse than no gate — it is a
# gate that lies.
#
# This happened.  Of the 42 ctest entries that invoke a script under
# scripts/, only 30 carried the label; `ctest -L ci_guard` silently
# skipped fixy_discipline, mint_pattern, permission_storage,
# refined_pre_subsumption, trait_guard, both contract audits and the
# session-persistence include audit.  CI invoked those directly, so
# the divergence never showed up there — only locally, where it
# mattered most.
#
# ── DISCIPLINE ────────────────────────────────────────────────────────
#
# Every add_test() in test/CMakeLists.txt whose COMMAND references a
# path under scripts/ MUST carry the `ci_guard` label via
# set_tests_properties(... PROPERTIES LABELS "ci_guard").
#
# The label is what makes the count self-guarding: a new guard script
# registered without it fails THIS test, so the gap cannot reopen
# silently the way it did before.
#
# Non-script tests (compiled binaries) are ignored — they are covered
# by the normal `ctest` run, not by the discipline gate.
#
# Exit status:
#   0 — every script-invoking test carries the label
#   1 — at least one script-invoking test is unlabelled
#   2 — bad invocation / self-test failure / CMakeLists not found

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-ci-guard-labels.sh — ci_guard label-coverage guard.

Usage:
  check-ci-guard-labels.sh              # scan; exit 1 on an unlabelled guard
  check-ci-guard-labels.sh --self-test  # plant an unlabelled guard, verify catch
  check-ci-guard-labels.sh -h | --help  # usage

Rule:
  Every add_test() in test/CMakeLists.txt whose COMMAND names a path
  under scripts/ must appear in a set_tests_properties(...) call whose
  PROPERTIES carry the `ci_guard` label.  Otherwise
  `ctest -L ci_guard` — the local pre-merge gate — silently skips it.
USAGE
}

# ── Scanner ──────────────────────────────────────────────────────────
# Walks test/CMakeLists.txt, accumulating each add_test(...) and
# set_tests_properties(...) call by paren balance (calls span several
# lines, and CMake permits a trailing `)` anywhere).  Emits one line
# per unlabelled script-invoking test.
scan_cmakelists() {
    local cmakelists="$1"
    local line trimmed block name script
    local depth=0 kind=""
    local -a unlabelled=()
    local -A script_tests=()
    local -A labelled=()

    while IFS= read -r line; do
        # Strip a trailing comment before counting parens, so a `)`
        # inside a comment cannot close the block early.
        trimmed="${line%%#*}"

        if [[ "$depth" -eq 0 ]]; then
            case "$trimmed" in
                *add_test*\(*)              kind="add_test"; block="$trimmed" ;;
                *set_tests_properties*\(*)  kind="props";    block="$trimmed" ;;
                *) continue ;;
            esac
        else
            block+=$'\n'"$trimmed"
        fi

        # Paren balance over the accumulated fragment for THIS line.
        local opens closes
        opens="${trimmed//[^(]/}"
        closes="${trimmed//[^)]/}"
        depth=$(( depth + ${#opens} - ${#closes} ))
        (( depth > 0 )) && continue
        depth=0

        if [[ "$kind" == "add_test" ]]; then
            # NAME is the token right after `NAME`.
            name="${block#*NAME}"
            name="${name#"${name%%[![:space:]]*}"}"
            name="${name%%[[:space:]]*}"
            name="${name%%)*}"
            if [[ "$block" == *scripts/* && -n "$name" ]]; then
                script="${block##*scripts/}"
                script="${script%%[[:space:]]*}"
                script="${script%%)*}"
                script_tests["$name"]="$script"
            fi
        elif [[ "$kind" == "props" && "$block" == *ci_guard* ]]; then
            # Names sit between `set_tests_properties(` and `PROPERTIES`.
            local names="${block#*set_tests_properties(}"
            names="${names%%PROPERTIES*}"
            local n
            for n in $names; do
                labelled["$n"]=1
            done
        fi
        kind=""
    done < "$cmakelists"

    local t
    for t in "${!script_tests[@]}"; do
        if [[ -z "${labelled[$t]+set}" ]]; then
            unlabelled+=("$t -> ${script_tests[$t]}")
        fi
    done

    printf '%s\n' "TOTAL=${#script_tests[@]}"
    printf '%s\n' "LABELLED=${#labelled[@]}"
    local u
    for u in "${unlabelled[@]:-}"; do
        if [[ -n "$u" ]]; then
            printf 'UNLABELLED=%s\n' "$u"
        fi
    done
    # Explicit: the loop above ends on a false `[[ -n "" ]]` when the
    # array is empty, and `set -e` would kill the caller's command
    # substitution on that non-zero return.
    return 0
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant a synthetic test/CMakeLists.txt carrying one labelled
        # guard, one UNLABELLED guard, and one non-script test.  The
        # scanner must flag exactly the unlabelled one.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/test"
        cat >"$tmp_root/test/CMakeLists.txt" <<'PLANTED'
# Synthetic ci_guard label fixture for --self-test.
add_test(NAME planted_compiled_binary COMMAND planted_compiled_binary)
add_test(NAME planted_labelled
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-planted-labelled.sh)
set_tests_properties(planted_labelled PROPERTIES
  LABELS "ci_guard;PLANTED-1")
add_test(NAME planted_unlabelled
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-planted-unlabelled.sh)
# A properties call with a DIFFERENT label must not count as coverage.
set_tests_properties(planted_unlabelled PROPERTIES
  TIMEOUT 30
  LABELS "slow")
add_test(NAME planted_multi_a
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-planted-multi.sh --self-test)
add_test(NAME planted_multi_b
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-planted-multi.sh)
set_tests_properties(planted_multi_a planted_multi_b PROPERTIES
  LABELS "ci_guard;PLANTED-2")
PLANTED
        result_file="$(mktemp)"
        if CRUCIBLE_CI_GUARD_LABELS_TEST_FILE="$tmp_root/test/CMakeLists.txt" \
           bash "${BASH_SOURCE[0]}" >"$result_file" 2>&1; then
            printf 'check-ci-guard-labels: SELF-TEST FAILED — planted unlabelled guard not caught.\n' >&2
            printf '── scanner output ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        cgl_fail() {
            printf 'check-ci-guard-labels: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner output ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }
        grep -qF 'planted_unlabelled' "$result_file" || \
            cgl_fail "unlabelled guard missing from the diagnostic."
        if grep -qF 'planted_labelled' "$result_file"; then
            cgl_fail "labelled guard was flagged — the label lookup is broken."
        fi
        if grep -qF 'planted_compiled_binary' "$result_file"; then
            cgl_fail "a non-script test was flagged — the scripts/ filter is broken."
        fi
        for multi in planted_multi_a planted_multi_b; do
            if grep -qF "$multi" "$result_file"; then
                cgl_fail "${multi} was flagged — a multi-name set_tests_properties call is not being parsed."
            fi
        done
        rm -f "$result_file"
        printf 'check-ci-guard-labels: self-test passed — unlabelled guard caught; labelled, multi-name-labelled and non-script tests all clean.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-ci-guard-labels: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

cmakelists="${CRUCIBLE_CI_GUARD_LABELS_TEST_FILE:-$root/test/CMakeLists.txt}"

if [[ ! -f "$cmakelists" ]]; then
    printf 'check-ci-guard-labels: not found: %s\n' "$cmakelists" >&2
    exit 2
fi

report="$(scan_cmakelists "$cmakelists")"
total="${report#*TOTAL=}"; total="${total%%$'\n'*}"
labelled_count="${report#*LABELLED=}"; labelled_count="${labelled_count%%$'\n'*}"

if printf '%s\n' "$report" | grep -q '^UNLABELLED='; then
    printf 'check-ci-guard-labels: script-invoking ctest entries WITHOUT the ci_guard label:\n\n' >&2
    printf '%s\n' "$report" | while IFS= read -r l; do
        [[ "$l" == UNLABELLED=* ]] && printf '  %s\n' "${l#UNLABELLED=}" >&2
    done
    cat >&2 <<HINT

Each entry above is invisible to \`ctest -L ci_guard\` — the local
pre-merge gate reports a pass without ever running that guard.

Fix: add the test name to a set_tests_properties(...) call carrying
the label, next to its add_test():

  set_tests_properties(<name> [<name_self_test>] PROPERTIES
    LABELS "ci_guard")

Label the --self-test entry alongside the scan entry: a self-test that
never runs cannot witness a regex regression.
HINT
    exit 1
fi

printf 'check-ci-guard-labels: clean — all %s script-invoking ctest entries carry the ci_guard label (%s labelled entries total).\n' \
    "$total" "$labelled_count" >&2
exit 0
