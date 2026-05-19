// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-09 fixture: `refined_with<Pred>` requires
// `IsRefinementPredicate<Pred>` — a fundamental type (here `int`) is
// not a class and is rejected at template instantiation rather than
// silently producing an ill-typed projection into `Fn<T, int, ...>`'s
// Refinement slot.
//
// Pre-M-09 this compiled silently; the resolver would try to project
// `int` as if it were a predicate and surface as deep substitution
// failures inside Fn.h's pred:: machinery.  Post-M-09 the rejection
// fires HERE with the named concept in the diagnostic.
//
// Distinct from `PredicateInvocableOn` (Refined.h) which gates the
// CONSTRUCTOR-time check; IsRefinementPredicate is the broader
// fixy-layer structural gate that fires WITHOUT needing a payload
// type to probe.
//
// Expected diagnostic: constraints not satisfied /
// IsRefinementPredicate / is_class / no matching function.

#include <crucible/fixy/Grant.h>

namespace gr = crucible::fixy::grant;

int main() {
    // Should FAIL: `int` is a fundamental type, not a class →
    // IsRefinementPredicate<int> evaluates to false → the requires-
    // clause on `refined_with<Pred>` rejects.
    [[maybe_unused]] auto bad = gr::refined_with<int>{};
    return 0;
}
