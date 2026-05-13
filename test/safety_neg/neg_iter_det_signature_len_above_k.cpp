// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing IterationDetector::SignatureLen with K+1=6 in
// a constexpr context (boundary edge — smallest forbidden value).
//
// Per WRAP-IterDet-2 (#928), IterationDetector::SignatureLen is
// safety::BoundedMonotonic<uint32_t, K> where K=5.  The ctor's pre
// clause is `pre(!(T(Max) < initial))` i.e. `initial <= K`.  An
// initial of K+1 = 6 fires `!(5 < 6) == false` → contract violation
// → non-constant expression in constexpr context → ill-formed.
//
// Companion fixture to neg_iter_det_signature_len_uint32_max.cpp:
//   - This one is the boundary edge (= K+1, off-by-one).
//   - That one is the wide miss (= UINT32_MAX, full overflow).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.  A future regression
// that admits "any uint32_t" silently passes the wide-miss fixture
// but still fails this one if the predicate becomes
// `(initial != UINT32_MAX)`.  Vice versa for "drop the upper bound
// entirely" (e.g. switch to plain Monotonic): the wide-miss fixture
// fires but this one would silently pass.  Both fixtures together
// pin the upper bound exactly at K.

#include <crucible/IterationDetector.h>

#include <cstdint>

int main() {
    // Boundary edge: K+1 = 6 is the smallest forbidden initial value.
    // SignatureLen{6u} fires `!(5 < 6) == false` → contract failure
    // → not a constant expression → ill-formed.
    constexpr crucible::IterationDetector::SignatureLen bad{uint32_t{6}};
    (void)bad;
    return 0;
}
