// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for effects::ResourceTag (GAPS-189 #1214).
//
// Premise: ResourceTag<T> demands T expose the canonical triple
//
//     T::kind  -> convertible to ResourceKind
//     T::value -> convertible to uint64_t
//     T::name  -> convertible to string_view
//
// AND that T::kind names a valid ResourceKind atom (IsResourceKind).
// A bare scalar type like `int` exposes none of the three members,
// so the `requires` clause's first sub-expression — checking the
// existence of `T::kind` — fails substitution and the concept does
// NOT match.  The static_assert below then trips at compile time.
//
// Why this is the load-bearing soundness gate:
//
// Without rejection, generic algorithms in GAPS-190 (concurrent-row
// summation) and GAPS-191 (FitsCog gate) could be instantiated with
// an arbitrary scalar type — silently treating it as if it consumed
// zero of every resource axis.  Compute kernels that omitted their
// declared budgets would look free, comm kernels would oversubscribe
// SMs, and the unified compute+comm budget invariant would hold
// vacuously.  A vacuous safety property is no safety property at
// all.  This fixture witnesses that the concept-gate rejects.
//
// Companion fixture: neg_resources_concept_rejects_bogus_kind.cpp
//   * That one tests rejection by INVALID ResourceKind atom — a type
//     that exposes the canonical triple but with `kind` outside the
//     0..22 range.  Distinct mismatch class.
//   * This one tests rejection by ABSENT triple — a type that has no
//     `kind` / `value` / `name` members at all.  Distinct mismatch
//     class (substitution failure on member lookup, not failure on
//     IsResourceKind sub-expression).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" /
// "constraints not satisfied" / "no member named 'kind'" /
// "ResourceTag" pointing at the static_assert call site.

#include <crucible/effects/Resources.h>

namespace eff = crucible::effects;

// ResourceTag<int> must NOT be satisfied — int has no `kind`, no
// `value`, no `name`.  The static_assert fires.
static_assert(eff::ResourceTag<int>,
    "ResourceTag concept must NOT accept bare scalar types — "
    "GAPS-189 soundness gate compromised.");

int main() { return 0; }
