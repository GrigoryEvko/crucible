// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing Vigil::AlignmentPos with the
// uint8_t-max value (255) in a constexpr context.
//
// Companion fixture to neg_vigil_alignment_pos_above_max.cpp:
//   - That one tests the smallest forbidden value (6 = ALIGNMENT_K+1,
//     off-by-one).
//   - This one tests the wide miss (255 = uint8_t max — covers
//     "casting a wider counter to uint8_t and forgetting the bound").
//
// Per WRAP-Vigil-7 (#1077), Vigil::alignment_pos_ is wrapped in
// safety::Refined<safety::bounded_above<ALIGNMENT_POS_MAX=5>, uint8_t>.
// `AlignmentPos{255}` triggers `bounded_above<5>(255) == false` →
// contract violation → non-constant expression in constexpr context →
// ill-formed.
//
// Two fixtures rather than one because HS14 mandates ≥2 negative-
// compile fixtures per new soundness gate, each demonstrating a
// distinct mismatch class.  Different forbidden-value classes (boundary
// edge vs wide miss) cover different drift modes — a future
// regression that admits "any value ≤ ALIGNMENT_K" silently passes
// the wide-miss fixture, but fails the boundary one; vice versa for
// "drop the upper bound entirely".  Both fixtures together pin the
// gate.
//
// The wide-miss case is also the one that fires when a future migration
// from raw uint32_t loop-counter to uint8_t storage happens *without*
// the Refined wrap — UINT8_MAX is a normal-looking byte value that
// `static_cast<uint8_t>(some_uint32_t)` can produce silently from any
// value ≥ 255.  The Refined wrap rejects it; this fixture proves that
// rejection is load-bearing.

#include <crucible/Vigil.h>

int main() {
    // Wide miss: 255 fits in the underlying uint8_t but vastly
    // exceeds the bounded_above<5> predicate.  This catches the
    // "lost narrowing" bug class where a wider intermediate is
    // truncated to uint8_t before being assigned.
    constexpr crucible::Vigil::AlignmentPos bad{uint8_t{255}};
    (void)bad;
    return 0;
}
