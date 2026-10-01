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
# The acceptance check that every atom has a consumer reads the same two
# templates rather than building a second set of atoms.
#
# The verdict cache
# -----------------
# A verdict depends only on the compiler, its flags, the text of the
# sentinel, this script and the contents of each header that the sentinel
# reads.  The cache keeps the verdict and the compiler log under a name that
# hashes the first four, with one variant for each set of header contents.
# The -MD dependency file of the compile lists those headers, and the
# variant records the SHA-256 of each one.  A run whose headers all have
# their recorded contents reads the variant and starts no compile.  A
# compile that a signal stopped, or that failed with no diagnostic, is not
# kept, because its verdict says nothing about the inputs.  The cache is
# the "atom-roster" directory of the root that utils/scripts/cache_dir.py
# describes, and CRUCIBLE_CACHE_DIR=off turns it off.  A variant that no run
# used for 14 days goes.  The limit of the cache is the limit of the direct
# mode of ccache: a new header that hides a listed header earlier on the
# include path is not seen.
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
#   CRUCIBLE_CACHE_DIR          the root of the caches, or "off"
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
. "$(dirname -- "$script_path")/repo_root.sh"
include_root="$REPO_ROOT/include"
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
                -DCRUCIBLE_FP_STRICT_FLOOR=1)

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
        printf '// Generated by utils/scripts/check-atom-roster-joined.sh.  Do not edit.\n'
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

# Prints the directory of the verdict cache, or fails when the caches are off.
verdict_cache_dir() {
    local chosen="${CRUCIBLE_CACHE_DIR:-}"
    if [[ "$chosen" == off ]]; then
        return 1
    elif [[ -n "$chosen" ]]; then
        printf '%s/atom-roster' "$chosen"
    else
        printf '%s/crucible/atom-roster' "${XDG_CACHE_HOME:-$HOME/.cache}"
    fi
}

# Prints the path, the size and the mtime of the compiler driver $1 and of its cc1plus.
compiler_identity() {
    local program
    for program in "$(command -v -- "$1" || printf '%s' "$1")" "$("$1" -print-prog-name=cc1plus 2>/dev/null || true)"; do
        printf '%s ' "$(realpath -- "$program" 2>/dev/null || printf '%s' "$program")"
        stat -L -c '%s %Y' -- "$program" 2>/dev/null || printf 'missing\n'
    done
}

# Prints the name of the verdicts of the sentinel $2 under compiler $1 and the include flags after them.
verdict_key() {
    local cxx="$1" tu="$2"
    shift 2
    {
        printf 'atom-roster-verdict-1\n'
        sha256sum -- "$script_path" "$tu" | cut -d' ' -f1
        compiler_identity "$cxx"
        printf '%s\n' "${sentinel_flags[@]}" "$@"
        printf 'CPATH=%s\nCPLUS_INCLUDE_PATH=%s\nC_INCLUDE_PATH=%s\n' \
               "${CPATH:-}" "${CPLUS_INCLUDE_PATH:-}" "${C_INCLUDE_PATH:-}"
    } | sha256sum | cut -d' ' -f1
}

# Prints the stem of the variant in directory $1 whose headers all have
# their recorded contents, and touches it.  Fails when no variant matches.
cached_variant() {
    local deps stem
    [[ -d "$1" ]] || return 1
    for deps in "$1"/*.deps; do
        stem="${deps%.deps}"
        [[ -f "$deps" && -f "$stem.verdict" && -f "$stem.log" ]] || continue
        if sha256sum --check --status -- "$deps" 2>/dev/null; then
            touch -- "$deps" "$stem.verdict" "$stem.log"
            printf '%s' "$stem"
            return 0
        fi
    done
    return 1
}

# Keeps a variant in directory $1: the SHA-256 of each header that the
# dependency file $3 lists, except the sentinel $2, the log $4 and the
# verdict $5.  Each file goes in under a temporary name and then a rename.
keep_variant() {
    local dir="$1" tu="$2" depfile="$3" log="$4" verdict="$5" text word staging name
    local -a words=() paths=()
    [[ -s "$depfile" ]] || return 0
    text="$(<"$depfile")"
    # A path with an escaped space cannot be split from the list safely.
    [[ "$text" != *'\ '* ]] || return 0
    text="${text//\\$'\n'/ }"
    read -r -d '' -a words <<<"$text" || true
    for word in "${words[@]:1}"; do
        [[ "$word" == "$tu" ]] || paths+=("$word")
    done
    [[ ${#paths[@]} -gt 0 ]] || return 0
    mkdir -p -- "$dir"
    staging="$(mktemp -- "$dir/.variant.XXXXXX")"
    if ! sha256sum -- "${paths[@]}" >"$staging" 2>/dev/null; then
        rm -f -- "$staging"
        return 0
    fi
    name="$dir/$(sha256sum <"$staging" | cut -d' ' -f1)"
    cp -- "$log" "$staging.log" && mv -f -- "$staging.log" "$name.log"
    printf '%s' "$verdict" >"$staging.verdict" && mv -f -- "$staging.verdict" "$name.verdict"
    mv -f -- "$staging" "$name.deps"
}

# Removes each variant of the cache $1 that no run used for 14 days, and each empty name.
evict_old_variants() {
    find "$1" -mindepth 2 -maxdepth 2 -type f -mtime +14 -delete 2>/dev/null || true
    find "$1" -mindepth 1 -maxdepth 1 -type d -empty -delete 2>/dev/null || true
}

# Compiles the sentinel and classifies the outcome, or reads the verdict
# from the cache.  Echoes one of ok / violation / broken, writes the
# compiler log to $2, and sets cache_outcome to hit, miss or off.
classify() {
    local tu="$1" log="$2" cxx dir="" key="" stem verdict status=0
    cxx="$(find_cxx)"
    local includes=(-I"$include_root")
    if [[ -n "${ROSTER_EXTRA_INCLUDE_DIR:-}" ]]; then
        includes+=(-I"${ROSTER_EXTRA_INCLUDE_DIR}")
    fi
    cache_outcome=off
    if dir="$(verdict_cache_dir)"; then
        key="$(verdict_key "$cxx" "$tu" "${includes[@]}")"
        if stem="$(cached_variant "$dir/$key")"; then
            cp -- "$stem.log" "$log"
            cache_outcome=hit
            cat -- "$stem.verdict"
            return
        fi
        cache_outcome=miss
    fi
    "$cxx" "${sentinel_flags[@]}" "${includes[@]}" -MD -MF "$log.d" "$tu" >"$log" 2>&1 || status=$?
    # A violation is reported only when EVERY diagnostic is one of the two
    # assertions.  A sentinel that also fails for some other reason is
    # broken, not a finding: the walk may have run over a namespace the
    # compiler was still recovering from, and a verdict from that is not a
    # verdict.  Counting rather than searching is what separates the two,
    # because a broken header and a real violation can appear together.
    local total marker
    total="$(grep -c 'error:' "$log" || true)"
    marker="$(grep -cF "$violation_marker" "$log" || true)"
    if [[ $status -eq 0 ]]; then
        verdict=ok
    elif [[ $total -gt 0 && $total -eq $marker ]]; then
        verdict=violation
    else
        verdict=broken
    fi
    if [[ $cache_outcome == miss && ( $status -eq 0 || ( $status -eq 1 && $total -gt 0 ) ) ]]; then
        keep_variant "$dir/$key" "$tu" "$log.d" "$log" "$verdict"
        evict_old_variants "$dir"
    fi
    printf '%s' "$verdict"
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
# Axes 1 to 6 re-invoke this script by its absolute path, from a deep
# temporary working directory, which is the invocation ctest uses.
#
# The six runs are independent, so they run at the same time, and the
# self-test takes the time of one sentinel compile.  Each verdict is then
# read in the order of the axes.  Axes 7 to 9 prove the verdict cache on a
# small sentinel: a second run reads it, a changed header misses it, and
# the caches can be turned off.

self_test_fail() {
    printf 'check-atom-roster-joined --self-test: FAIL: %s\n' "$1" >&2
    if [[ -n "${2:-}" && -s "${2:-}" ]]; then
        printf '\n--- guard output ---\n' >&2
        cat "$2" >&2
    fi
    exit 1
}

# Starts the guard in the background, from a deep directory, with an
# injected probe header when $2 is not empty.  The output goes to $3 and
# the exit status to $3.status.
probe_start() {
    local probe_dir="$1" probe_header="$2" out="$3" deep
    deep="$probe_dir/deep/build/test"
    mkdir -p "$deep"
    (
        set +e
        if [[ -n "$probe_header" ]]; then
            ( cd "$deep" && ROSTER_EXTRA_INCLUDE_DIR="$probe_dir" ROSTER_EXTRA_HEADER="$probe_header" \
                bash "$script_path" --quiet ) >"$out" 2>&1
        else
            ( cd "$deep" && bash "$script_path" --quiet ) >"$out" 2>&1
        fi
        printf '%s' "$?" >"$out.status"
    ) &
}

# Echoes the exit status that a background run of probe_start recorded.
probe_status() {
    cat "$1.status"
}

# Writes stdin to the probe header $1 under a temporary name and then a
# rename, so a self-test that runs at the same time reads the whole file.
put_probe() {
    local target="$probe/roster_probe/$1"
    cat >"$target.$$" && mv -f -- "$target.$$" "$target"
}

self_test() {
    local tmp probe status baseline root
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    # The probe headers are the same for each run of one version of this
    # script, so they live at a path that the script names, and the verdict
    # cache serves a second run.  With the caches off, a scratch path is enough.
    if root="$(verdict_cache_dir)"; then
        probe="$root/probe-$(sha256sum -- "$script_path" | cut -c1-16)"
    else
        probe="$tmp/probe"
    fi
    mkdir -p "$probe/roster_probe"

    # Axis 2 — POSITIVE: a roster declared and not joined.
    put_probe orphan.h <<'PROBE'
#pragma once
#include <fixy/Atom.h>
#include <tuple>
namespace fixy::atom::detail {
using probe_orphan_atom_roster = std::tuple<int>;
}
PROBE

    # Axis 3 — NEGATIVE: a roster declared AND joined, through an alias to
    # a family already in the join.  The guard must stay quiet, or it is
    # firing on the declaration rather than on the relation.
    put_probe joined.h <<'PROBE'
#pragma once
#include <fixy/atoms/Sync.h>
namespace fixy::atom::detail {
using probe_joined_atom_roster = sync_atom_roster;
}
PROBE

    # Axis 4 — POSITIVE: a sample set that IS joined.  This is the half
    # that keeps axis 2 from being dodged by a rename.
    put_probe wrong_samples.h <<'PROBE'
#pragma once
#include <fixy/atoms/Sync.h>
namespace fixy::atom::detail {
using probe_wrong_atom_samples = sync_atom_roster;
}
PROBE

    # Axis 5 — NEGATIVE: a sample set outside the population, which is
    # what a sample set is supposed to be.
    put_probe ok_samples.h <<'PROBE'
#pragma once
#include <fixy/Atom.h>
#include <tuple>
namespace fixy::atom::detail {
using probe_ok_atom_samples = std::tuple<int>;
}
PROBE

    # Axis 6 — a sentinel that will not compile must exit 2, not 0.  A
    # guard that cannot measure reporting green is the failure mode this
    # whole task is about.
    put_probe broken.h <<'PROBE'
#pragma once
this is not c++;
PROBE

    # Axis 1 is the baseline, by absolute path from a deep directory, with
    # no probe.  The six runs go at the same time.
    probe_start "$probe" "" "$tmp/a1.log"
    probe_start "$probe" roster_probe/orphan.h "$tmp/a2.log"
    probe_start "$probe" roster_probe/joined.h "$tmp/a3.log"
    probe_start "$probe" roster_probe/wrong_samples.h "$tmp/a4.log"
    probe_start "$probe" roster_probe/ok_samples.h "$tmp/a5.log"
    probe_start "$probe" roster_probe/broken.h "$tmp/a6.log"
    wait

    # Axis 1 — a definite verdict is required; exit 2 means the sentinel is
    # broken and nothing below would mean anything.
    baseline="$(probe_status "$tmp/a1.log")"
    [[ $baseline -eq 0 || $baseline -eq 1 ]] \
        || self_test_fail "axis 1: the baseline must be a definite verdict, not exit $baseline" "$tmp/a1.log"
    printf 'check-atom-roster-joined --self-test: baseline verdict is %s\n' "$baseline"

    status="$(probe_status "$tmp/a2.log")"
    [[ $status -eq 1 ]] || self_test_fail "axis 2: an unjoined roster must fail the guard (exit $status)" "$tmp/a2.log"
    grep -qF 'probe_orphan_atom_roster' "$tmp/a2.log" \
        || self_test_fail 'axis 2: the message must name the offending roster' "$tmp/a2.log"
    grep -qF 'all_atom_roster alias in include/fixy/Collision.h' "$tmp/a2.log" \
        || self_test_fail 'axis 2: the message must name the join site' "$tmp/a2.log"

    status="$(probe_status "$tmp/a3.log")"
    [[ $status -eq $baseline ]] \
        || self_test_fail "axis 3: a joined roster must not change the verdict ($status, baseline $baseline)" "$tmp/a3.log"
    if grep -qF 'probe_joined_atom_roster' "$tmp/a3.log"; then
        self_test_fail 'axis 3: a joined roster must not be named as an offender' "$tmp/a3.log"
    fi

    status="$(probe_status "$tmp/a4.log")"
    [[ $status -eq 1 ]] || self_test_fail "axis 4: a joined sample set must fail the guard (exit $status)" "$tmp/a4.log"
    grep -qF 'probe_wrong_atom_samples' "$tmp/a4.log" \
        || self_test_fail 'axis 4: the message must name the offending sample set' "$tmp/a4.log"

    status="$(probe_status "$tmp/a5.log")"
    [[ $status -eq $baseline ]] \
        || self_test_fail "axis 5: an unjoined sample set must not change the verdict ($status, baseline $baseline)" "$tmp/a5.log"
    if grep -qF 'probe_ok_atom_samples' "$tmp/a5.log"; then
        self_test_fail 'axis 5: an unjoined sample set must not be named as an offender' "$tmp/a5.log"
    fi

    status="$(probe_status "$tmp/a6.log")"
    [[ $status -eq 2 ]] || self_test_fail "axis 6: an uncompilable sentinel must exit 2 (exit $status)" "$tmp/a6.log"

    # Axes 7 to 9 — the verdict cache, with a small sentinel and a scratch
    # cache, so each run is one fast compile or none.
    local tiny="$tmp/tiny" seen
    mkdir -p "$tiny/roster_tiny"
    printf '#include <roster_tiny/tiny.h>\n' >"$tiny/tiny.cpp"
    printf 'int roster_tiny_value;\n' >"$tiny/roster_tiny/tiny.h"

    # Axis 7 — a second run reads the verdict and the log of the first and
    # starts no compile.
    CRUCIBLE_CACHE_DIR="$tmp/cache" ROSTER_EXTRA_INCLUDE_DIR="$tiny" \
        classify "$tiny/tiny.cpp" "$tiny/first.log" >"$tiny/first.out"
    seen="$cache_outcome $(<"$tiny/first.out")"
    [[ $seen == 'miss ok' ]] || self_test_fail "axis 7: a first run must compile and pass ($seen)" "$tiny/first.log"
    CRUCIBLE_CACHE_DIR="$tmp/cache" ROSTER_EXTRA_INCLUDE_DIR="$tiny" \
        classify "$tiny/tiny.cpp" "$tiny/second.log" >"$tiny/second.out"
    seen="$cache_outcome $(<"$tiny/second.out")"
    { [[ $seen == 'hit ok' ]] && cmp -s "$tiny/first.log" "$tiny/second.log"; } \
        || self_test_fail "axis 7: a second run must read the verdict and the log of the first ($seen)" "$tiny/second.log"

    # Axis 8 — POSITIVE: a header whose contents change makes the cache miss
    # and gives the new verdict, so the cache cannot give a stale one.  The
    # old contents read their own variant again.
    # The message is two literals, so the source line that the compiler
    # quotes under the error does not hold the marker a second time.
    printf 'static_assert(false, "fixy/Collision.h: the atom-population " "relation: tiny probe");\n' \
        >"$tiny/roster_tiny/tiny.h"
    CRUCIBLE_CACHE_DIR="$tmp/cache" ROSTER_EXTRA_INCLUDE_DIR="$tiny" \
        classify "$tiny/tiny.cpp" "$tiny/changed.log" >"$tiny/changed.out"
    seen="$cache_outcome $(<"$tiny/changed.out")"
    [[ $seen == 'miss violation' ]] \
        || self_test_fail "axis 8: a changed header must miss and give its own verdict ($seen)" "$tiny/changed.log"
    printf 'int roster_tiny_value;\n' >"$tiny/roster_tiny/tiny.h"
    CRUCIBLE_CACHE_DIR="$tmp/cache" ROSTER_EXTRA_INCLUDE_DIR="$tiny" \
        classify "$tiny/tiny.cpp" "$tiny/back.log" >"$tiny/back.out"
    seen="$cache_outcome $(<"$tiny/back.out")"
    [[ $seen == 'hit ok' ]] || self_test_fail "axis 8: the old contents must read their own variant ($seen)" "$tiny/back.log"

    # Axis 9 — with the caches off, each run compiles.
    CRUCIBLE_CACHE_DIR=off ROSTER_EXTRA_INCLUDE_DIR="$tiny" \
        classify "$tiny/tiny.cpp" "$tiny/off.log" >"$tiny/off.out"
    seen="$cache_outcome $(<"$tiny/off.out")"
    [[ $seen == 'off ok' ]] || self_test_fail "axis 9: with the caches off a run must compile ($seen)" "$tiny/off.log"

    printf 'check-atom-roster-joined --self-test: PASS (9 axes, 2 negative controls)\n'
}

if [[ $mode == self_test ]]; then
    self_test
else
    run_guard
fi
