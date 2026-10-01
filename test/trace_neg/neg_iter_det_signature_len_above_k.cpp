// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::SignatureLen with K+1 = 6 in a
// constexpr context (boundary edge, the smallest forbidden value).
//
// IterationDetector::SignatureLen is ::fixy::BoundedMonotonic<uint32_t, K>
// with K = 5.  Its constructor, reached only through
// ::fixy::mint_bounded_monotonic, has the precondition
// std::cmp_less_equal(initial, Max), that is initial <= K.  An initial
// value of 6 makes the predicate false, and the constant evaluation fails.
//
// Companion fixture to neg_iter_det_signature_len_uint32_max.cpp:
//   - This one is the boundary edge (= K+1, off-by-one).
//   - That one is the wide miss (= UINT32_MAX, full overflow).
//
// A regression to plain Monotonic drops the bound, and the wide-miss
// fixture reports it.  A predicate that only refuses UINT32_MAX passes the
// wide-miss fixture, and this one reports it.

#include <crucible/IterationDetector.h>
#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::SignatureLen bad =
        ::fixy::mint_bounded_monotonic<uint32_t, crucible::IterationDetector::K>(uint32_t{6});
    (void)bad;
    return 0;
}
