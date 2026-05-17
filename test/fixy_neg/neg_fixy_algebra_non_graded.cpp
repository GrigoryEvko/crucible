// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture #2: IsGraded probe rejects a bare type when
// routed through `fixy::algebra::IsGraded`.
//
// Violation: a bare `int` is NOT a `Graded<M, L, T>` specialization;
// the IsGraded concept must reject it identically through the fixy
// alias as through the algebra:: substrate.  Static-asserting the
// concept on `int` is therefore a hard compile error.
//
// Expected diagnostic: GCC's static_assert text pointing at the
// "fixy::algebra::IsGraded<int>" requires clause.

#include <crucible/fixy/Algebra.h>

namespace fa = crucible::fixy::algebra;

// Carrier type unique to this fixture.
struct AlgebraNegFixture2_BareCarrier {};

int main() {
    // The bare carrier IS NOT a Graded<...> specialization; the
    // concept must reject.
    static_assert(fa::IsGraded<AlgebraNegFixture2_BareCarrier>,
        "fa::IsGraded<BareCarrier> must reject — BareCarrier is not "
        "a Graded<M, L, T> specialization.  fixy::algebra alias "
        "preserves IsGraded concept gate identically.");
    return 0;
}
