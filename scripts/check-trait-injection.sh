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
# ── Family C: fail-closed namespaces ─────────────────────────────────
#
# The new tree states a relation as a namespace of
# foundation::fail_closed::edge<From, To> variables
# (foundation/diag/FailClosed.h).  There is no primary template to
# specialize, so the forgery surface moves from "specialize the trait"
# to "reopen the namespace and declare an edge".  A namespace is open
# by definition; this guard closes it by scanning for the reopening:
#
#   fixy::tags::admitted_retags                 — the retag catalog of
#                                                 fixy/Tagged.h.  Same
#                                                 authority as
#                                                 retag_policy above.
#   fixy::tags::secret_policy::admitted_policies — the declassification
#                                                 exits of fixy/Secret.h.
#                                                 A forged edge lets a
#                                                 Secret leave through a
#                                                 policy nobody
#                                                 reviewed.
#   fixy::machine::admitted_transitions         — the shared edge set of
#                                                 fixy/Machine.h.  A forged
#                                                 edge admits a state
#                                                 transition the machine
#                                                 never declared.
#   fixy::refined::admitted_implications        — the implication relation
#                                                 of fixy/Refined.h.  A
#                                                 forged edge lets a proof
#                                                 of P stand in for a
#                                                 proof of Q.
#
# A namespace alias (`namespace x = fixy::tags::admitted_retags;`) does
# not match, and cannot add a member either.
#
# ── The pattern is the SHAPE, not the name ───────────────────────────
#
# Every row matches the construct its diagnostic names and nothing
# else: a specialization is `template <...> struct X<`, a reopening is
# `namespace ...X {`.  A bare `struct X<` or `namespace X` is a mention,
# and mentions are everywhere — doc blocks, and ledgers such as
# scripts/port-drops.txt, whose lines are neither comments nor
# Markdown.  The guard once matched `namespace admitted_retags` and
# read four ledger lines as forgeries; the fix was the regex, because a
# claim wider than its pattern is measured by the pattern.  The
# --self-test plants a prose ledger that must stay silent.
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

# The script's own absolute path, resolved once.
#
# Two places below re-invoke this script, and both used ${BASH_SOURCE[0]}
# verbatim.  Under `bash scripts/check-trait-injection.sh` that is a
# RELATIVE path, so a re-invocation from any other working directory
# fails to find the file — and a self-test axis whose inner run silently
# fails to start produces no output, which greps clean and passes
# vacuously.  That is how the build-tree axis passed under relative
# invocation while failing under the absolute one ctest uses.
script_path="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/$(basename -- "${BASH_SOURCE[0]}")"

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

  retag_policy              include/crucible/safety/_Tagged.h
                            include/crucible/safety/source/*.h      + test/**
  machine_transition        include/crucible/safety/_Machine.h
                            include/fixy/Machine.h                 + test/**
  predicate_implies         include/crucible/safety/_Refined{,Algebra}.h
                                                                    + test/**
  survivor_registry         include/crucible/permissions/PermissionInherit.h
                            include/crucible/fixy/Bridge.h          + test/**
  is_subsort                include/crucible/sessions/*.h           + test/**

  admitted_retags           include/fixy/Tagged.h                  + test/**
  admitted_policies         include/fixy/Secret.h                  + test/**
  admitted_transitions      include/fixy/Machine.h                 + test/**
  admitted_implications     include/fixy/Refined.h                 + test/**

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
substrate_paths='include/crucible/algebra/* include/foundation/algebra/* include/fixy/* include/crucible/safety/* include/crucible/permissions/* include/crucible/handles/* test/test_concept_cheat_probe.cpp test/fixy/test_cheat_probe.cpp test/fixy/neg/neg_cheat_graded_modality_injection.cpp'

# A specialization, explicit or partial, always carries a template
# header, and the header cannot contain a brace or a semicolon.  The
# scan is multiline, so the header may sit on the line above the
# struct.  A reopening is the namespace name followed by its brace.
spec='template\s*<[^{};]*>\s*(struct|class)\s+'

scan_table=(
    "substrate|${spec}(is_graded_specialization|value_type_decoupled|graded_modality|is_numerical_tier_impl)\s*<|${substrate_paths}"
    "retag_policy|${spec}retag_policy\s*<|include/crucible/safety/_Tagged.h include/crucible/safety/source/*.h test/*"
    "machine_transition|(${spec}machine_transition\s*<|CRUCIBLE_ALLOW_MACHINE_TRANSITION\s*\()|include/crucible/safety/_Machine.h include/fixy/Machine.h test/*"
    "predicate_implies|${spec}predicate_implies\s*<|include/crucible/safety/_Refined.h include/crucible/safety/_RefinedAlgebra.h test/*"
    "survivor_registry|${spec}survivor_registry\s*<|include/crucible/permissions/PermissionInherit.h include/crucible/fixy/Bridge.h test/*"
    "is_subsort|${spec}is_subsort\s*<|include/crucible/sessions/*.h test/*"
    "admitted_retags|namespace\s+(fixy::tags::)?admitted_retags\s*\{|include/fixy/Tagged.h test/*"
    "admitted_policies|namespace\s+(fixy::tags::secret_policy::|secret_policy::)?admitted_policies\s*\{|include/fixy/Secret.h test/*"
    "admitted_transitions|namespace\s+(fixy::machine::|machine::)?admitted_transitions\s*\{|include/fixy/Machine.h test/*"
    "admitted_implications|namespace\s+(fixy::refined::|refined::)?admitted_implications\s*\{|include/fixy/Refined.h test/*"
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

        # The two namespace-shaped relations: the qualified reopening on
        # line 3 and the nested reopening on line 7 must both be caught;
        # the alias on line 10 must not, because an alias adds nothing.
        planted_retags='src/planted/planted_admitted_retags.cpp'
        cat >"$tmp_root/$planted_retags" <<'PLANTED'
// Synthetic admitted_retags fixture for --self-test.
#include <fixy/Tagged.h>
namespace fixy::tags::admitted_retags {
inline constexpr ::foundation::fail_closed::edge<source::Sanitized, source::External> planted{};
}  // namespace fixy::tags::admitted_retags
namespace fixy::tags {
namespace admitted_retags {
}  // namespace admitted_retags
}  // namespace fixy::tags
namespace planted_alias = fixy::tags::admitted_retags;
PLANTED

        planted_policies='src/planted/planted_admitted_policies.cpp'
        cat >"$tmp_root/$planted_policies" <<'PLANTED'
// Synthetic admitted_policies fixture for --self-test.
#include <fixy/Secret.h>
namespace fixy::tags::secret_policy::admitted_policies {
inline constexpr ::foundation::fail_closed::edge<classified, PlantedPolicy> planted{};
}  // namespace fixy::tags::secret_policy::admitted_policies
PLANTED

        planted_transitions='src/planted/planted_admitted_transitions.cpp'
        cat >"$tmp_root/$planted_transitions" <<'PLANTED'
// Synthetic admitted_transitions fixture for --self-test.
#include <fixy/Machine.h>
namespace fixy::machine::admitted_transitions {
inline constexpr ::foundation::fail_closed::edge<PlantedFrom, PlantedTo> planted{};
}  // namespace fixy::machine::admitted_transitions
PLANTED

        planted_implications='src/planted/planted_admitted_implications.cpp'
        cat >"$tmp_root/$planted_implications" <<'PLANTED'
// Synthetic admitted_implications fixture for --self-test.
#include <fixy/Refined.h>
namespace fixy::refined::admitted_implications {
inline constexpr ::foundation::fail_closed::edge<non_negative, positive> planted{};
}  // namespace fixy::refined::admitted_implications
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

        exempt_machine='include/crucible/safety/_Machine.h'
        cat >"$tmp_root/$exempt_machine" <<'EXEMPT'
// Synthetic machine authoring-location fixture for --self-test.
CRUCIBLE_ALLOW_MACHINE_TRANSITION(PlantedFrom, PlantedTo)
namespace crucible::safety {
template <>
struct machine_transition<PlantedFrom, PlantedTo> : std::true_type {};
}  // namespace crucible::safety
EXEMPT

        exempt_implies='include/crucible/safety/_Refined.h'
        cat >"$tmp_root/$exempt_implies" <<'EXEMPT'
// Synthetic implies authoring-location fixture for --self-test.
namespace crucible::safety {
template <>
struct predicate_implies<PlantedWide, PlantedNarrow> : std::true_type {};
}  // namespace crucible::safety
EXEMPT

        mkdir -p "$tmp_root/include/fixy"
        exempt_retags='include/fixy/Tagged.h'
        cat >"$tmp_root/$exempt_retags" <<'EXEMPT'
// Synthetic admitted_retags authoring-location fixture for --self-test.
namespace fixy::tags::admitted_retags {
inline constexpr ::foundation::fail_closed::edge<source::PlantedRaw, source::Sanitized> planted{};
}  // namespace fixy::tags::admitted_retags
EXEMPT

        exempt_policies='include/fixy/Secret.h'
        cat >"$tmp_root/$exempt_policies" <<'EXEMPT'
// Synthetic admitted_policies authoring-location fixture for --self-test.
namespace fixy::tags::secret_policy::admitted_policies {
inline constexpr ::foundation::fail_closed::edge<classified, PlantedPolicy> planted{};
}  // namespace fixy::tags::secret_policy::admitted_policies
EXEMPT

        exempt_transitions='include/fixy/Machine.h'
        cat >"$tmp_root/$exempt_transitions" <<'EXEMPT'
// Synthetic admitted_transitions authoring-location fixture for --self-test.
namespace fixy::machine::admitted_transitions {
inline constexpr ::foundation::fail_closed::edge<PlantedFrom, PlantedTo> planted{};
}  // namespace fixy::machine::admitted_transitions
EXEMPT

        exempt_implications='include/fixy/Refined.h'
        cat >"$tmp_root/$exempt_implications" <<'EXEMPT'
// Synthetic admitted_implications authoring-location fixture for --self-test.
namespace fixy::refined::admitted_implications {
inline constexpr ::foundation::fail_closed::edge<positive, non_negative> planted{};
}  // namespace fixy::refined::admitted_implications
EXEMPT

        # ── the prose ledger: every name, no shape ───────────────────
        # Lines that are neither comments nor Markdown, in a file the
        # scan reaches, citing each relation the way scripts/port-drops.txt
        # does.  None of them is a specialization or a reopening, and
        # none may fire.
        mkdir -p "$tmp_root/scripts"
        planted_ledger='scripts/planted-ledger.txt'
        cat >"$tmp_root/$planted_ledger" <<'LEDGER'
crucible/safety/_Tagged.h:kCatalogRosterTuple  — The retag catalog tuple became the namespace admitted_retags of fail-closed edges; struct retag_policy<From, To> went with it.
crucible/safety/_Secret.h:All  — The policy roster tuple became the namespace admitted_policies of fail-closed edges in fixy/Secret.h.
crucible/safety/_Refined.h:predicate_implies  — fixy/Refined.h admits an edge only from the namespace admitted_implications, never from struct predicate_implies<P, Q>.
crucible/safety/_Machine.h:machine_transition  — The edge set is the namespace admitted_transitions; class machine_transition<From, To> and struct is_subsort<T, U> are gone, and struct is_graded_specialization<W> with them.
LEDGER

        scanner_stderr="$(mktemp)"

        self_test_fail() {
            printf 'check-trait-injection: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$scanner_stderr")" >&2
            rm -f "$scanner_stderr"
            exit 2
        }

        if CRUCIBLE_TRAIT_INJECTION_TEST_ROOT="$tmp_root" \
           bash "$script_path" 2>"$scanner_stderr"; then
            self_test_fail 'planted specializations not caught.'
        fi

        # Every planted violation must be named, at its exact line.  A
        # regex that drifts off one relation still greens the other
        # five, so each is asserted individually.  A specialization is
        # reported at its template header, the line the match starts on.
        for expect in \
            "$planted_substrate:7" \
            "$planted_retag:4" \
            "$planted_machine:4" \
            "$planted_machine:6" \
            "$planted_implies:3" \
            "$planted_survivor:3" \
            "$planted_subsort:3" \
            "$planted_retags:3" \
            "$planted_retags:7" \
            "$planted_policies:3" \
            "$planted_transitions:3" \
            "$planted_implications:3"
        do
            if ! grep -qF "at $expect" "$scanner_stderr"; then
                self_test_fail "expected diagnostic for $expect missing."
            fi
        done

        # The prose ledger names every relation and must not fire once:
        # a mention is not a specialization, and the ledger is the file
        # that turned the guard red when its pattern was the name.
        if grep -qF "at $planted_ledger:" "$scanner_stderr"; then
            self_test_fail 'a prose mention in a ledger was flagged as a specialization.'
        fi

        # The pure-comment citation on line 9 of the retag fixture must
        # be filtered out.
        if grep -qF "$planted_retag:9" "$scanner_stderr"; then
            self_test_fail 'pure-comment line leaked through the filter.'
        fi

        # The namespace alias on line 10 of the admitted_retags fixture
        # is not a reopening and must not fire.
        if grep -qF "$planted_retags:10" "$scanner_stderr"; then
            self_test_fail 'a namespace alias was flagged as a reopening.'
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
            "$exempt_implies" \
            "$exempt_retags" \
            "$exempt_policies" \
            "$exempt_transitions" \
            "$exempt_implications"
        do
            # Match the diagnostic form "…at <rel>:<line>", not a bare
            # path occurrence: the failure report echoes each trait's
            # authoring set, which names these very files, so a bare
            # grep would flag its own help text.
            if grep -qF "at $exempt:" "$scanner_stderr"; then
                self_test_fail "authoring-location exemption leaked for $exempt."
            fi
        done

        # ── The build-tree exclude holds from any working directory ──
        #
        # The scan root is absolute and ripgrep matches --glob against
        # paths relative to the CURRENT directory, so the `build/**`
        # excludes only name the build tree while the cwd is the repo
        # root.  ctest runs this script with the cwd inside the build
        # tree, where they named nothing, and every generated artifact
        # under it was scanned: a stale Testing/Temporary/LastTest.log
        # holding the text of a negative-compile fixture's expected
        # diagnostic read as a forbidden specialization, so the guard
        # failed on its own suite's output.
        #
        # The axis plants exactly that artifact and runs the scanner
        # from inside the build tree.  Without the anchoring subshell
        # around rg, this fires.
        planted_build_root="$tmp_root/build/Testing/Temporary"
        mkdir -p "$planted_build_root" "$tmp_root/build/test"
        {
            printf 'filler\n'
            cat "$tmp_root/$planted_policies"
        } >"$planted_build_root/LastTest.log"

        # The planted violation fixtures are still on disk, so the
        # scanner exits 1 either way here and the exit code says
        # nothing.  What distinguishes the two cases is whether the
        # build artifact is among the paths it reports.
        build_cwd_stderr="$(mktemp)"
        (cd "$tmp_root/build/test" \
         && CRUCIBLE_TRAIT_INJECTION_TEST_ROOT="$tmp_root" \
            bash "$script_path" 2>"$build_cwd_stderr" || true)
        if grep -qF 'build/Testing/Temporary/LastTest.log' "$build_cwd_stderr"; then
            rm -f "$build_cwd_stderr"
            self_test_fail 'the build-tree exclude does not hold when the scanner runs from inside the build tree, so a generated artifact was scanned as source.'
        fi
        rm -f "$build_cwd_stderr"

        # ── The self-test passes the way ctest invokes it ────────────
        #
        # ctest runs this script by an absolute path built from
        # CMAKE_SOURCE_DIR, with the working directory inside the build
        # tree.  Nothing covered that, and two defects hid in the gap at
        # once: the script re-invoked itself through ${BASH_SOURCE[0]},
        # which is relative under `bash scripts/...`, so a re-invocation
        # from another directory failed to start and the axis that
        # depended on it greped clean and passed on no evidence; and the
        # build-tree exclude was anchored at the repo root rather than
        # the scan root, which is the same path in production and a
        # different one under --self-test.
        #
        # So the axis is the whole self-test again, run the way ctest
        # runs it.  The env guard stops the recursion at one level.
        if [ -z "${CRUCIBLE_TRAIT_INJECTION_SELF_TEST_NESTED:-}" ]; then
            if ! (cd "$tmp_root/build/test" \
                  && CRUCIBLE_TRAIT_INJECTION_SELF_TEST_NESTED=1 \
                     bash "$script_path" --self-test >/dev/null 2>&1); then
                self_test_fail 'the self-test does not pass when invoked by absolute path from a working directory inside a build tree, which is the invocation ctest uses.'
            fi
        fi

        rm -f "$scanner_stderr"
        printf 'check-trait-injection: self-test passed — substrate injection + 5 fail-closed relations (retag_policy, machine_transition incl. macro form, predicate_implies, survivor_registry, is_subsort) + 4 fail-closed namespaces (admitted_retags, admitted_policies, admitted_transitions, admitted_implications) each caught, per-trait authoring-location exemptions + comment filter + namespace-alias filter honoured, a prose ledger naming every relation stays silent, the build-tree exclude holds with the cwd inside the build tree, and the whole self-test passes again under the absolute-path, deep-cwd invocation ctest uses.\n' >&2
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

    # --vimgrep prints one record per match, at the line the match
    # starts on, so a header-plus-struct specialization is one
    # diagnostic and not one per line it spans.
    while IFS=: read -r file line _column text; do
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
        # The scan root is absolute, and ripgrep matches --glob against
        # paths relative to the CURRENT DIRECTORY rather than to the root
        # it was handed.  Run from the repo root the `build/**` excludes
        # name the build tree; run from inside it, as ctest does, they
        # name a `build` under the build tree, which does not exist, and
        # every generated artifact under it is scanned instead.  That is
        # how a stale Testing/Temporary/LastTest.log holding the text of
        # a negative-compile fixture's expected diagnostic was read as a
        # forbidden specialization.
        #
        # The subshell anchors the globs where they read.  The anchor is
        # the SCAN ROOT and not the repo root: the two are the same in
        # production, and --self-test points the scan root at a fixture
        # tree, so anchoring at the repo root would leave the fixture
        # tree's own build directory unexcluded.  It does not change what
        # is scanned, because the scan root stays absolute.
        cd "$scan_root" && rg --vimgrep --no-heading --multiline --pcre2 \
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
