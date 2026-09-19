#!/usr/bin/env bash
# check-trait-injection.sh — substrate trait + fail-closed relation
# orphan-specialization guard.
#
# C++ has no orphan rule.  Any translation unit may specialize any
# template for any type, including types it does not own.  Every
# fail-closed relation in the tree is therefore only as strong as the
# discipline that keeps its specializations beside the declarations
# they name, and that discipline needs a scanner or it is only a
# comment.
#
# Two families are covered.
#
# ── Family A: substrate traits ───────────────────────────────────────
#
# `is_graded_specialization`, `value_type_decoupled`, `graded_modality`
# and `is_numerical_tier_impl` assert an algebraic property of a Graded
# wrapper.  A specialization from a foreign TU forges a property the
# wrapper never proved.
#
# ── Family B: fail-closed relations ──────────────────────────────────
#
# Each of these is a relation defaulting to false, where every legal
# edge is one explicit specialization.  Forging an edge is forging
# authority:
#
#   retag_policy<From, To>          — admits a provenance/trust
#                                     transition.  A forged edge
#                                     launders an untrusted value into
#                                     a Sanitized / Verified tag, which
#                                     is exactly the bug the phantom
#                                     axis exists to catch.
#   machine_transition<From, To>    — admits a state-machine edge.  A
#                                     forged edge rolls a machine
#                                     backwards past a step it must not
#                                     skip.  Reached through the
#                                     CRUCIBLE_ALLOW_MACHINE_TRANSITION
#                                     macro, so the macro invocation is
#                                     scanned alongside the raw
#                                     specialization.
#   predicate_implies<P, Q>         — admits a refinement subsumption.
#                                     A forged edge lets a Refined<P, T>
#                                     stand where Refined<Q, T> is
#                                     required without the implication
#                                     holding.
#   survivor_registry<DeadTag>      — names who inherits a dead peer's
#                                     permissions on crash-stop
#                                     recovery.  A forged registry mints
#                                     authority for a tag nobody granted.
#   is_subsort<T, U>                — admits a payload subtype, which
#                                     propagates into session-protocol
#                                     subtyping.  A forged edge widens
#                                     what a channel accepts.
#
# `splits_into` / `splits_into_pack` are the sixth relation of this
# shape; they have their own guard (check-splits-into-orphan.sh) plus a
# companion authoring-witness trait, and are not duplicated here.
#
# ── Authoring sets are PER TRAIT ─────────────────────────────────────
#
# A single shared whitelist would be a downgrade: admitting sessions/
# so `is_subsort` can be declared there would also admit a forged
# `is_graded_specialization` from sessions/.  Each trait therefore
# carries its own set, listed in the scan table below.  Widening a set
# is a deliberate one-line edit that a reviewer sees.
#
# test/** is exempt for the Family B relations, matching the
# check-splits-into-orphan.sh precedent: negative-compile fixtures and
# sentinel TUs legitimately declare local tag trees and their relations,
# and are themselves the witnesses that the relation stays fail-closed.
#
# This script excludes itself from the scan: its --self-test fixtures
# plant the forbidden specializations as literal heredoc text.
#
# Exit status:
#   0 — clean (no forbidden specialization outside the authoring set)
#   1 — at least one violation
#   2 — bad invocation / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-trait-injection.sh — substrate trait + fail-closed relation guard.

Usage:
  check-trait-injection.sh              # scan; exit 1 on violation
  check-trait-injection.sh --self-test  # plant a violation per trait, verify catch
  check-trait-injection.sh -h | --help  # usage

Scanned traits and their authoring sets:

  is_graded_specialization  include/crucible/{algebra,safety,permissions,handles}/
  value_type_decoupled      + include/foundation/algebra/ + include/fixy/
  graded_modality           + test/test_concept_cheat_probe.cpp
  is_numerical_tier_impl

  retag_policy              include/crucible/safety/Tagged.h
                            include/crucible/safety/source/*.h      + test/**
  machine_transition        include/crucible/safety/Machine.h       + test/**
  predicate_implies         include/crucible/safety/Refined{,Algebra}.h
                                                                    + test/**
  survivor_registry         include/crucible/permissions/PermissionInherit.h
                            include/crucible/fixy/Bridge.h          + test/**
  is_subsort                include/crucible/sessions/*.h           + test/**

CLAUDE.md §XVI — a substrate trait is specialized only beside the
wrapper whose algebraic property it asserts.  CLAUDE.md §IX — a
fail-closed relation is specialized only in the TU that declares the
types it names; otherwise any foreign TU forges authority.
USAGE
}

# ── Scan table ───────────────────────────────────────────────────────
#
# One row per scan pass: "<label>|<pcre2 pattern>|<space-separated
# path globs>".  Globs are matched with bash [[ == ]], where * spans
# directory separators.
#
# Family A keeps its original single authoring set, widened for the
# sibling substrate: include/foundation/algebra/ carries the same Graded
# trait specializations as include/crucible/algebra/, and include/fixy/
# holds the wrappers that specialize beside themselves the way
# include/crucible/safety/ does, until Stage D of the canonical-fixy
# refactor deletes the old tree.
substrate_paths='include/crucible/algebra/* include/foundation/algebra/* include/fixy/* include/crucible/safety/* include/crucible/permissions/* include/crucible/handles/* test/test_concept_cheat_probe.cpp'

scan_table=(
    "substrate|(struct|class)\s+(is_graded_specialization|value_type_decoupled|graded_modality|is_numerical_tier_impl)\s*<|${substrate_paths}"
    "retag_policy|(struct|class)\s+retag_policy\s*<|include/crucible/safety/Tagged.h include/crucible/safety/source/*.h test/*"
    "machine_transition|((struct|class)\s+machine_transition\s*<|CRUCIBLE_ALLOW_MACHINE_TRANSITION\s*\()|include/crucible/safety/Machine.h test/*"
    "predicate_implies|(struct|class)\s+predicate_implies\s*<|include/crucible/safety/Refined.h include/crucible/safety/RefinedAlgebra.h test/*"
    "survivor_registry|(struct|class)\s+survivor_registry\s*<|include/crucible/permissions/PermissionInherit.h include/crucible/fixy/Bridge.h test/*"
    "is_subsort|(struct|class)\s+is_subsort\s*<|include/crucible/sessions/*.h test/*"
)

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant every scanned relation TWICE: once under a non-exempt
        # path (must be flagged) and once under that trait's own
        # blessed authoring path (must NOT be flagged).  A guard that
        # only ever sees a clean tree has never demonstrated it fires,
        # and a guard whose exemption is never exercised can silently
        # widen to everything.
        #
        # A third fixture plants a pure-comment citation, which the
        # line filter must drop: these trait names appear in prose all
        # over the tree and a doc edit must not red CI.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT

        mkdir -p "$tmp_root/src/planted" \
                 "$tmp_root/include/crucible/algebra" \
                 "$tmp_root/include/crucible/safety/source" \
                 "$tmp_root/include/crucible/sessions" \
                 "$tmp_root/include/crucible/fixy"

        # ── violation fixtures: <rel-path>:<expected line> ───────────
        planted_substrate='src/planted/planted_trait.cpp'
        cat >"$tmp_root/$planted_substrate" <<'PLANTED'
// Synthetic trait-injection fixture for --self-test.
namespace crucible::planted {
template <typename T>
struct PlantedProbe {};
}  // namespace crucible::planted
namespace crucible {
template <>
struct is_graded_specialization<planted::PlantedProbe<int>> {
    static constexpr bool is_specialized = true;
};
}  // namespace crucible
PLANTED

        # One file per Family B relation, each planting the forged edge
        # on a known line so the diagnostic can be asserted precisely.
        planted_retag='src/planted/planted_retag.cpp'
        cat >"$tmp_root/$planted_retag" <<'PLANTED'
// Synthetic retag_policy fixture for --self-test.  Line 5 forges the
// laundering edge; line 9 is a pure-comment citation that must not fire.
namespace crucible::safety {
template <>
struct retag_policy<source::FromDb, trust::Verified> {
    static constexpr bool allowed = true;
};
}  // namespace crucible::safety
// struct retag_policy<source::FromUser, trust::Verified> — doc-comment only.
PLANTED

        planted_machine='src/planted/planted_machine.cpp'
        cat >"$tmp_root/$planted_machine" <<'PLANTED'
// Synthetic machine_transition fixture for --self-test.  The macro
// form on line 4 is the reachable one; the raw specialization on
// line 7 is the form the macro expands to.
CRUCIBLE_ALLOW_MACHINE_TRANSITION(Authenticated, Disconnected)
namespace crucible::safety {
template <>
struct machine_transition<Authenticated, Disconnected> : std::true_type {};
}  // namespace crucible::safety
PLANTED

        planted_implies='src/planted/planted_implies.cpp'
        cat >"$tmp_root/$planted_implies" <<'PLANTED'
// Synthetic predicate_implies fixture for --self-test.
namespace crucible::safety {
template <>
struct predicate_implies<PlantedWide, PlantedNarrow> : std::true_type {};
}  // namespace crucible::safety
PLANTED

        planted_survivor='src/planted/planted_survivor.cpp'
        cat >"$tmp_root/$planted_survivor" <<'PLANTED'
// Synthetic survivor_registry fixture for --self-test.
namespace crucible::safety {
template <>
struct survivor_registry<PlantedDeadTag> {
    using type = inheritance_list<PlantedForeignTag>;
};
}  // namespace crucible::safety
PLANTED

        planted_subsort='src/planted/planted_subsort.cpp'
        cat >"$tmp_root/$planted_subsort" <<'PLANTED'
// Synthetic is_subsort fixture for --self-test.
namespace crucible::safety::proto {
template <>
struct is_subsort<PlantedNarrow, PlantedWide> : std::true_type {};
}  // namespace crucible::safety::proto
PLANTED

        # ── exemption fixtures: same edge, blessed authoring path ────
        exempt_substrate='include/crucible/algebra/planted.h'
        cat >"$tmp_root/$exempt_substrate" <<'EXEMPT'
// Synthetic authoring-location fixture for --self-test.
namespace crucible {
template <>
struct is_graded_specialization<planted::ExemptProbe<int>> {
    static constexpr bool is_specialized = true;
};
}  // namespace crucible
EXEMPT

        exempt_retag='include/crucible/safety/source/Planted.h'
        cat >"$tmp_root/$exempt_retag" <<'EXEMPT'
// Synthetic retag authoring-location fixture for --self-test.
namespace crucible::safety {
template <>
struct retag_policy<source::PlantedRaw, source::Sanitized> {
    static constexpr bool allowed = true;
};
}  // namespace crucible::safety
EXEMPT

        exempt_subsort='include/crucible/sessions/Planted.h'
        cat >"$tmp_root/$exempt_subsort" <<'EXEMPT'
// Synthetic subsort authoring-location fixture for --self-test.
namespace crucible::safety::proto {
template <>
struct is_subsort<PlantedNarrow, PlantedWide> : std::true_type {};
}  // namespace crucible::safety::proto
EXEMPT

        # The last three authoring sets name exact files rather than a
        # directory glob, so the fixtures must carry those exact names
        # or the exemption witness proves nothing about the real row.
        exempt_survivor='include/crucible/fixy/Bridge.h'
        cat >"$tmp_root/$exempt_survivor" <<'EXEMPT'
// Synthetic survivor authoring-location fixture for --self-test.
namespace crucible::safety {
template <>
struct survivor_registry<PlantedDeadTag> {
    using type = inheritance_list<PlantedForeignTag>;
};
}  // namespace crucible::safety
EXEMPT

        exempt_machine='include/crucible/safety/Machine.h'
        cat >"$tmp_root/$exempt_machine" <<'EXEMPT'
// Synthetic machine authoring-location fixture for --self-test.
CRUCIBLE_ALLOW_MACHINE_TRANSITION(PlantedFrom, PlantedTo)
namespace crucible::safety {
template <>
struct machine_transition<PlantedFrom, PlantedTo> : std::true_type {};
}  // namespace crucible::safety
EXEMPT

        exempt_implies='include/crucible/safety/Refined.h'
        cat >"$tmp_root/$exempt_implies" <<'EXEMPT'
// Synthetic implies authoring-location fixture for --self-test.
namespace crucible::safety {
template <>
struct predicate_implies<PlantedWide, PlantedNarrow> : std::true_type {};
}  // namespace crucible::safety
EXEMPT

        scanner_stderr="$(mktemp)"

        self_test_fail() {
            printf 'check-trait-injection: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$scanner_stderr")" >&2
            rm -f "$scanner_stderr"
            exit 2
        }

        if CRUCIBLE_TRAIT_INJECTION_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$scanner_stderr"; then
            self_test_fail 'planted specializations not caught.'
        fi

        # Every planted violation must be named, at its exact line.  A
        # regex that drifts off one relation still greens the other
        # five, so each is asserted individually.
        for expect in \
            "$planted_substrate:8" \
            "$planted_retag:5" \
            "$planted_machine:4" \
            "$planted_machine:7" \
            "$planted_implies:4" \
            "$planted_survivor:4" \
            "$planted_subsort:4"
        do
            if ! grep -qF "$expect" "$scanner_stderr"; then
                self_test_fail "expected diagnostic for $expect missing."
            fi
        done

        # The pure-comment citation on line 9 of the retag fixture must
        # be filtered out.
        if grep -qF "$planted_retag:9" "$scanner_stderr"; then
            self_test_fail 'pure-comment line leaked through the filter.'
        fi

        # Every blessed authoring location must be exempt.  A whitelist
        # that stopped matching would turn the guard into noise and get
        # switched off, so each exemption gets its own witness too.
        for exempt in \
            "$exempt_substrate" \
            "$exempt_retag" \
            "$exempt_subsort" \
            "$exempt_survivor" \
            "$exempt_machine" \
            "$exempt_implies"
        do
            # Match the diagnostic form "…at <rel>:<line>", not a bare
            # path occurrence: the failure report echoes each trait's
            # authoring set, which names these very files, so a bare
            # grep would flag its own help text.
            if grep -qF "at $exempt:" "$scanner_stderr"; then
                self_test_fail "authoring-location exemption leaked for $exempt."
            fi
        done

        rm -f "$scanner_stderr"
        printf 'check-trait-injection: self-test passed — substrate injection + 5 fail-closed relations (retag_policy, machine_transition incl. macro form, predicate_implies, survivor_registry, is_subsort) each caught, per-trait authoring-location exemptions + comment filter honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-trait-injection: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_TRAIT_INJECTION_TEST_ROOT:-$root}"
status=0

# Glob match against a trait's authoring set.  Unquoted "$glob" on the
# right of [[ == ]] is deliberate: it is a pattern, and * spans '/'.
path_is_authored() {
    local rel=$1
    shift
    local glob
    for glob in "$@"; do
        # shellcheck disable=SC2053
        if [[ $rel == $glob ]]; then
            return 0
        fi
    done
    return 1
}

for row in "${scan_table[@]}"; do
    label="${row%%|*}"
    rest="${row#*|}"
    pattern="${rest%|*}"
    read -r -a allowed_globs <<<"${rest##*|}"

    while IFS=: read -r file line text; do
        rel="${file#"$scan_root"/}"

        # Skip pure-comment lines.  These trait names are cited in
        # doc-blocks throughout the tree; a prose edit must not red CI.
        stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'*'*) continue ;;
        esac

        if path_is_authored "$rel" "${allowed_globs[@]}"; then
            continue
        fi

        printf 'trait_guard[%s]: forbidden specialization at %s:%s\n' "$label" "$rel" "$line" >&2
        printf 'trait_guard[%s]: %s\n' "$label" "$text" >&2
        printf 'trait_guard[%s]: authoring set is: %s\n' "$label" "${allowed_globs[*]}" >&2
        status=1
    done < <(
        rg -n --no-heading --multiline --pcre2 \
            --glob '!build/**' \
            --glob '!build-*/**' \
            --glob '!cmake-build-*/**' \
            --glob '!third_party/**' \
            --glob '!external/**' \
            --glob '!vendor/**' \
            --glob '!**/*.md' \
            --glob '!**/check-trait-injection.sh' \
            "$pattern" "$scan_root" || true
    )
done

if [[ "$status" -ne 0 ]]; then
    printf 'trait_guard: each scanned trait is specialized only inside its own authoring set (run --help for the table).\n' >&2
    printf 'trait_guard: C++ has no orphan rule, so a specialization outside the TU that declares the named types forges a property or an authority that was never granted.\n' >&2
    printf 'trait_guard: if the new location is legitimate, widen that trait row in scripts/check-trait-injection.sh so the widening is reviewed.\n' >&2
fi

exit "$status"
