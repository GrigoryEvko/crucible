#!/usr/bin/env bash
# check-atom-roster-joined.sh — every atom roster declared under fixy/ is
# joined into fixy::collision::all_atom_roster, and no sample set is.
#
# Why this exists
# ---------------
# fixy/Collision.h joins the family rosters by hand, and the guard beside
# that join only notices a missing family when the family's AXIS ends up
# with no atoms at all.  A roster whose axis is already populated by some
# other family is invisible to it: the atoms exist, nothing reads them,
# and the check whose name promises completeness reports clean.  That is
# how fixy/os/Spawn.h's three atoms sat outside the population while the
# axis check said every axis was covered.  It is the same shape as an
# allowlist key naming a file that is gone — a guard that cannot fail the
# way its name implies.
#
# The relation is therefore stated over the DECLARATIONS, in the language,
# once: fixy::collision::roster_declared_but_not_joined<Site>() and
# fixy::collision::sample_set_wrongly_joined<Site>() in
# include/fixy/Collision.h.  This script does not restate it and does not
# grep for rosters.  It supplies the one thing the header cannot supply
# for itself — a vantage point that has seen every header.
#
# Why a sentinel is needed at all
# -------------------------------
# std::meta::members_of answers as of the point where the walk is
# INSTANTIATED.  A walk written in a header is frozen at that header's own
# line and can never see a roster declared by a header included after it,
# so Collision.h's own assertion covers only the families Collision.h
# includes.  The sentinel below includes every header under include/fixy/
# and instantiates the same two templates from its own tag, at the bottom,
# where every declaration is visible.  A specialization is instantiated
# once, which is why the tag parameter exists: a second site must pass a
# tag of its own or it silently reuses the first site's answer.
#
# The include list is built from the DIRECTORY, not from a list in this
# file.  A list would go stale the first time a header is added, and a
# stale list here is exactly the failure this guard exists to catch.
#
# Stage A's acceptance gate check 6 ("every atom has a consumer") consumes
# the same two templates rather than building a second set of atoms.
#
# Exit codes
#   0 — every declared roster is joined, and no sample set is joined
#   1 — a violation; the compiler's message names the roster and the edit
#   2 — the guard could not measure: no compiler, or the sentinel failed
#       to compile for a reason other than these two assertions.  A guard
#       that cannot measure must not report green.
#
# Environment
#   CXX / CRUCIBLE_CXX          compiler for the sentinel (a GCC 16 with
#                               -freflection); CMake passes CXX
#   ROSTER_EXTRA_INCLUDE_DIR    self-test only: an extra -I
#   ROSTER_EXTRA_HEADER         self-test only: an extra #include, emitted
#                               after the real headers and before the
#                               assertions
#
# Usage: check-atom-roster-joined.sh [--quiet | --self-test]

set -euo pipefail

# Resolved once, absolutely, from this file rather than from the caller's
# working directory.  The trait guard shipped a self-test that never ran
# because it re-invoked a relative $0 from a temporary directory and the
# failure looked like a pass; every path below is anchored here instead.
script_path="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/$(basename -- "${BASH_SOURCE[0]}")"
repo_root="$(dirname "$(dirname "$script_path")")"
include_root="$repo_root/include"
scan_dir="$include_root/fixy"

quiet=0
mode=run
case "${1:-}" in
    --quiet)     quiet=1 ;;
    --self-test) mode=self_test ;;
    "")          ;;
    *)           printf 'usage: %s [--quiet | --self-test]\n' "$script_path" >&2; exit 2 ;;
esac

find_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        printf '%s' "$CXX"
    elif [[ -n "${CRUCIBLE_CXX:-}" && -x "${CRUCIBLE_CXX}" ]]; then
        printf '%s' "${CRUCIBLE_CXX}"
    elif command -v g++ >/dev/null 2>&1; then
        printf '%s' g++
    else
        printf 'check-atom-roster-joined: no compiler.  Set CXX or CRUCIBLE_CXX to a GCC 16.\n' >&2
        exit 2
    fi
}

# No -Werror and no -W: this guard's finding is a static_assert, and the
# tree's warning policy is other guards' business.  Colour is off because
# escape sequences land inside identifiers and defeat matching on the
# message.
sentinel_flags=(-std=c++26 -freflection -fcontracts -fsyntax-only -w
                -fdiagnostics-color=never -fconstexpr-ops-limit=100000000
                -DCRUCIBLE_FIXY_STRICT=1 -DCRUCIBLE_FP_STRICT_FLOOR=1)

# The marker every violation message begins with.  Both diagnostics in
# Collision.h open with it, so one substring separates "the guard found
# what it looks for" from "the sentinel broke".
violation_marker='fixy/Collision.h: the atom-population relation: '

# Writes the sentinel TU to $1.  Collision.h comes first so the join is
# declared before anything else; the rest of the tree follows in sorted
# order so the file is reproducible; the assertions come last so the walk
# sees every declaration.
write_sentinel() {
    local out="$1" header
    {
        printf '// Generated by scripts/check-atom-roster-joined.sh.  Do not edit.\n'
        printf '#include <fixy/Collision.h>\n'
        while IFS= read -r header; do
            printf '#include <%s>\n' "${header#"$include_root/"}"
        done < <(find "$scan_dir" -type f -name '*.h' | sort)
        if [[ -n "${ROSTER_EXTRA_HEADER:-}" ]]; then
            printf '#include <%s>\n' "${ROSTER_EXTRA_HEADER}"
        fi
        cat <<'TU'

// The sentinel's own vantage point.  A tag of its own is required: a
// specialization is instantiated once, so reusing Collision.h's tag would
// return Collision.h's answer.
namespace { struct atom_roster_sentinel_site; }

static_assert(::fixy::collision::roster_join_diagnostic<atom_roster_sentinel_site>().empty(),
              ::fixy::collision::roster_join_diagnostic<atom_roster_sentinel_site>());
static_assert(::fixy::collision::sample_set_diagnostic<atom_roster_sentinel_site>().empty(),
              ::fixy::collision::sample_set_diagnostic<atom_roster_sentinel_site>());
TU
    } >"$out"
}

# Compiles the sentinel and classifies the outcome.  Echoes one of
# ok / violation / broken, and writes the compiler log to $2.
classify() {
    local tu="$1" log="$2" cxx
    cxx="$(find_cxx)"
    local includes=(-I"$include_root")
    if [[ -n "${ROSTER_EXTRA_INCLUDE_DIR:-}" ]]; then
        includes+=(-I"${ROSTER_EXTRA_INCLUDE_DIR}")
    fi
    if "$cxx" "${sentinel_flags[@]}" "${includes[@]}" "$tu" >"$log" 2>&1; then
        printf 'ok'
        return
    fi
    # A violation is reported only when EVERY diagnostic is one of the two
    # assertions.  A sentinel that also fails for some other reason is
    # broken, not a finding: the walk may have run over a namespace the
    # compiler was still recovering from, and a verdict from that is not a
    # verdict.  Counting rather than searching is what separates the two,
    # because a broken header and a real violation can appear together.
    local total marker
    total="$(grep -c 'error:' "$log" || true)"
    marker="$(grep -cF "$violation_marker" "$log" || true)"
    if [[ $total -gt 0 && $total -eq $marker ]]; then
        printf 'violation'
    else
        printf 'broken'
    fi
}

run_guard() {
    local tmp verdict
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    write_sentinel "$tmp/sentinel.cpp"

    if [[ $quiet -eq 0 ]]; then
        printf 'check-atom-roster-joined: sentinel over %s headers under include/fixy/\n' \
               "$(find "$scan_dir" -type f -name '*.h' | wc -l)"
    fi

    verdict="$(classify "$tmp/sentinel.cpp" "$tmp/sentinel.log")"
    case "$verdict" in
        ok)
            if [[ $quiet -eq 0 ]]; then
                printf 'check-atom-roster-joined: every declared atom roster is joined; no sample set is.\n'
            fi
            return 0 ;;
        violation)
            printf 'check-atom-roster-joined: FAIL\n\n' >&2
            grep -F "$violation_marker" "$tmp/sentinel.log" >&2
            return 1 ;;
        *)
            printf 'check-atom-roster-joined: the sentinel did not compile, so nothing was measured.\n' >&2
            printf 'This is not a pass.  The compiler said:\n\n' >&2
            head -60 "$tmp/sentinel.log" >&2
            return 2 ;;
    esac
}

# ── Self-test ───────────────────────────────────────────────────────────
#
# Six axes.  Two are POSITIVE controls that inject a violation and require
# the guard to find it and NAME it; two are NEGATIVE controls that inject
# something that looks similar and require the guard to say nothing about
# it, because a check that fires on anything new is not a check.
#
# The axes are judged against a BASELINE taken from the unmodified tree
# rather than against "the tree is clean".  The self-test's subject is the
# guard, not the tree: a real offender sitting in the tree is a finding
# for whoever owns it, and it must not be able to turn this self-test red
# or, worse, green for the wrong reason.  The baseline must be a definite
# verdict — 0 or 1, never 2 — and each axis is a delta from it.
#
# Every axis re-invokes this script by its absolute path, from a deep
# temporary working directory, which is the invocation ctest uses.

self_test_fail() {
    printf 'check-atom-roster-joined --self-test: FAIL: %s\n' "$1" >&2
    if [[ -n "${2:-}" && -s "${2:-}" ]]; then
        printf '\n--- guard output ---\n' >&2
        cat "$2" >&2
    fi
    exit 1
}

# Runs the guard with an injected probe header.  Echoes its exit status.
probe_run() {
    local probe_dir="$1" probe_header="$2" out="$3" deep status
    deep="$probe_dir/deep/build/test"
    mkdir -p "$deep"
    set +e
    ( cd "$deep" && ROSTER_EXTRA_INCLUDE_DIR="$probe_dir" ROSTER_EXTRA_HEADER="$probe_header" \
        bash "$script_path" --quiet ) >"$out" 2>&1
    status=$?
    set -e
    printf '%s' "$status"
}

self_test() {
    local tmp probe status baseline
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    probe="$tmp/probe"
    mkdir -p "$probe/roster_probe"

    # Axis 1 — the baseline, by absolute path from a deep directory.  A
    # definite verdict is required; exit 2 means the sentinel is broken
    # and nothing below would mean anything.
    mkdir -p "$tmp/deep/build/test"
    set +e
    ( cd "$tmp/deep/build/test" && bash "$script_path" --quiet ) >"$tmp/a1.log" 2>&1
    baseline=$?
    set -e
    [[ $baseline -eq 0 || $baseline -eq 1 ]] \
        || self_test_fail "axis 1: the baseline must be a definite verdict, not exit $baseline" "$tmp/a1.log"
    printf 'check-atom-roster-joined --self-test: baseline verdict is %s\n' "$baseline"

    # Axis 2 — POSITIVE: a roster declared and not joined.
    cat >"$probe/roster_probe/orphan.h" <<'PROBE'
#pragma once
#include <fixy/Atom.h>
#include <tuple>
namespace fixy::atom::detail {
using probe_orphan_atom_roster = std::tuple<int>;
}
PROBE
    status="$(probe_run "$probe" roster_probe/orphan.h "$tmp/a2.log")"
    [[ $status -eq 1 ]] || self_test_fail "axis 2: an unjoined roster must fail the guard (exit $status)" "$tmp/a2.log"
    grep -qF 'probe_orphan_atom_roster' "$tmp/a2.log" \
        || self_test_fail 'axis 2: the message must name the offending roster' "$tmp/a2.log"
    grep -qF 'all_atom_roster alias in include/fixy/Collision.h' "$tmp/a2.log" \
        || self_test_fail 'axis 2: the message must name the join site' "$tmp/a2.log"

    # Axis 3 — NEGATIVE: a roster declared AND joined, through an alias to
    # a family already in the join.  The guard must stay quiet, or it is
    # firing on the declaration rather than on the relation.
    cat >"$probe/roster_probe/joined.h" <<'PROBE'
#pragma once
#include <fixy/atoms/Sync.h>
namespace fixy::atom::detail {
using probe_joined_atom_roster = sync_atom_roster;
}
PROBE
    status="$(probe_run "$probe" roster_probe/joined.h "$tmp/a3.log")"
    [[ $status -eq $baseline ]] \
        || self_test_fail "axis 3: a joined roster must not change the verdict ($status, baseline $baseline)" "$tmp/a3.log"
    if grep -qF 'probe_joined_atom_roster' "$tmp/a3.log"; then
        self_test_fail 'axis 3: a joined roster must not be named as an offender' "$tmp/a3.log"
    fi

    # Axis 4 — POSITIVE: a sample set that IS joined.  This is the half
    # that keeps axis 2 from being dodged by a rename.
    cat >"$probe/roster_probe/wrong_samples.h" <<'PROBE'
#pragma once
#include <fixy/atoms/Sync.h>
namespace fixy::atom::detail {
using probe_wrong_atom_samples = sync_atom_roster;
}
PROBE
    status="$(probe_run "$probe" roster_probe/wrong_samples.h "$tmp/a4.log")"
    [[ $status -eq 1 ]] || self_test_fail "axis 4: a joined sample set must fail the guard (exit $status)" "$tmp/a4.log"
    grep -qF 'probe_wrong_atom_samples' "$tmp/a4.log" \
        || self_test_fail 'axis 4: the message must name the offending sample set' "$tmp/a4.log"

    # Axis 5 — NEGATIVE: a sample set outside the population, which is
    # what a sample set is supposed to be.
    cat >"$probe/roster_probe/ok_samples.h" <<'PROBE'
#pragma once
#include <fixy/Atom.h>
#include <tuple>
namespace fixy::atom::detail {
using probe_ok_atom_samples = std::tuple<int>;
}
PROBE
    status="$(probe_run "$probe" roster_probe/ok_samples.h "$tmp/a5.log")"
    [[ $status -eq $baseline ]] \
        || self_test_fail "axis 5: an unjoined sample set must not change the verdict ($status, baseline $baseline)" "$tmp/a5.log"
    if grep -qF 'probe_ok_atom_samples' "$tmp/a5.log"; then
        self_test_fail 'axis 5: an unjoined sample set must not be named as an offender' "$tmp/a5.log"
    fi

    # Axis 6 — a sentinel that will not compile must exit 2, not 0.  A
    # guard that cannot measure reporting green is the failure mode this
    # whole task is about.
    cat >"$probe/roster_probe/broken.h" <<'PROBE'
#pragma once
this is not c++;
PROBE
    status="$(probe_run "$probe" roster_probe/broken.h "$tmp/a6.log")"
    [[ $status -eq 2 ]] || self_test_fail "axis 6: an uncompilable sentinel must exit 2 (exit $status)" "$tmp/a6.log"

    printf 'check-atom-roster-joined --self-test: PASS (6 axes, 2 negative controls)\n'
}

if [[ $mode == self_test ]]; then
    self_test
else
    run_guard
fi
