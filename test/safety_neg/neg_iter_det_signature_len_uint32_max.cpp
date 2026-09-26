// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::SignatureLen with UINT32_MAX in a
// constexpr context (wide miss, the whole upper half-line).
//
// IterationDetector::SignatureLen is ::fixy::BoundedMonotonic<uint32_t, K>
// with K = 5, and its constructor carries pre(!(T(Max) < initial)).
// UINT32_MAX makes the predicate false, and the constant evaluation fails.
//
// Companion fixture to neg_iter_det_signature_len_above_k.cpp:
//   - That one is the boundary edge (= K+1 = 6, off-by-one).
//   - This one is the wide miss (= UINT32_MAX).  It catches an unsigned
//     counter cast to uint32_t without the bound, a bit-flipped counter,
//     or a regression that replaces BoundedMonotonic with plain
//     Monotonic and loses the upper bound.

#include <crucible/IterationDetector.h>
#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::SignatureLen bad =
        ::fixy::mint_bounded_monotonic<uint32_t, crucible::IterationDetector::K>(uint32_t{UINT32_MAX});
    (void)bad;
    return 0;
}
