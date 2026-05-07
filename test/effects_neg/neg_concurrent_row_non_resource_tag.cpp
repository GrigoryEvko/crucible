// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for effects::ConcurrentRow / effects::
// ConcurrentlySchedulable (GAPS-190 #1215).
//
// Premise: ConcurrentRow<Tags...> requires every Tag satisfies the
// ResourceTag concept (`template <ResourceTag... Tags>`).  Passing a
// non-resource type (bare scalar, Effect atom, plain class) into the
// pack fails template substitution at the parameter constraint —
// before any algebra runs, before any sum is attempted.
//
// Why this is the load-bearing soundness gate:
//
// Without the carrier concept gate, ConcurrentRow<int, void*> would
// be a well-formed type that the algebra would later fail to operate
// on with a confusing deep-template substitution error.  The
// concept-gated pack rejects up front with a localized diagnostic
// pointing at the offending parameter.
//
// This also enforces the wrapper-author discipline: a downstream
// component (FitsCog, AdaptiveOptimizer) that constrains its inputs
// on `IsConcurrentRow<R>` is guaranteed that R's tag pack contains
// ONLY ResourceTag instantiations — no need for runtime tag-kind
// dispatch or per-tag SFINAE in the consumer.
//
// Companion fixture: neg_concurrent_row_overflow_rejected.cpp
//   * That one tests rejection at the SCHEDULING concept gate —
//     the ResourceTag pack is well-formed, but the pairwise sum
//     overflows uint64_t.  Distinct mismatch class (overflow
//     detection inside the concept body).
//   * This one tests rejection at the CARRIER concept gate — a
//     non-ResourceTag in the pack fails the parameter constraint.
//     Distinct mismatch class (template substitution failure on
//     the parameter constraint, not on the algebra).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" /
// "constraints not satisfied" / "ResourceTag" /
// "ConcurrentRow" pointing at the alias instantiation below.

#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

namespace eff = crucible::effects;

// `int` is not a ResourceTag (no kind/value/name members).  The
// ConcurrentRow<int> instantiation fails the parameter constraint,
// emitting the diagnostic at the alias-instantiation site.
//
// Note: a bare `using` of an ill-formed alias may or may not trigger
// substitution (depends on compiler eagerness).  We force evaluation
// by referring to the alias's `size` member in a static_assert,
// which is always-evaluated.
using R_bad = eff::ConcurrentRow<int>;

static_assert(R_bad::size == 1,
    "ConcurrentRow<int> instantiation should have failed at the "
    "ResourceTag concept gate before reaching this assertion — "
    "GAPS-190 carrier-shape defense compromised.");

int main() { return 0; }
