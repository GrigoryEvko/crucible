// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing IterationDetector::SignatureLen with
// UINT32_MAX in a constexpr context (wide miss — full overflow).
//
// Per WRAP-IterDet-2 (#928), IterationDetector::SignatureLen is
// safety::BoundedMonotonic<uint32_t, K> where K=5.  The ctor's pre
// clause is `pre(!(T(Max) < initial))` i.e. `initial <= K`.  An
// initial of UINT32_MAX = 4294967295 fires `!(5 < UINT32_MAX) ==
// false` → contract violation → non-constant expression in constexpr
// context → ill-formed.
//
// Companion fixture to neg_iter_det_signature_len_above_k.cpp:
//   - That one is the boundary edge (= K+1=6, off-by-one).
//   - This one is the wide miss (= UINT32_MAX, full domain) —
//     catches "casting an unsigned counter to uint32_t and forgetting
//     the bound", or a bit-flipped counter from corrupted memory, or
//     a regression that drops the upper bound entirely (e.g. switches
//     from BoundedMonotonic to plain Monotonic).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.  Different forbidden-
// value classes (boundary edge vs wide miss) cover different drift
// modes — see the companion fixture's docstring for the symmetric
// rationale.

#include <crucible/ir001/IterationDetector.h>

#include <climits>
#include <cstdint>

int main() {
    // Wide miss: UINT32_MAX = 4294967295 exercises the full upper
    // half of the uint32_t range.  Catches any predicate that only
    // filters specific magic values rather than the whole upper-bound
    // half-line.
    constexpr crucible::IterationDetector::SignatureLen bad{
        uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
