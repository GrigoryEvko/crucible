// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing IterationDetector::MatchPos with the
// uint8_t-max value (255) in a constexpr context.
//
// Companion fixture to neg_iter_det_match_pos_above_max.cpp:
//   - That one tests the smallest forbidden value (5 = K, off-by-one).
//   - This one tests the wide miss (255 = uint8_t max — covers
//     "casting a wider counter to uint8_t and forgetting the bound").
//
// Per WRAP-IterDet-1 (#927), IterationDetector::match_pos_ is wrapped
// in safety::Refined<safety::bounded_above<MATCH_POS_MAX=4>, uint8_t>.
// `MatchPos{255}` triggers `bounded_above<4>(255) == false` → contract
// violation → non-constant expression in constexpr context → ill-formed.
//
// Two fixtures rather than one because HS14 mandates ≥2 negative-
// compile fixtures per new soundness gate, each demonstrating a
// distinct mismatch class.  Different forbidden-value classes (boundary
// edge vs wide miss) cover different drift modes — a future
// regression that admits "any value <= K" silently passes the wide-
// miss fixture, but fails the boundary one; vice versa for "drop the
// upper bound entirely".  Both fixtures together pin the gate.

#include <crucible/ir001/IterationDetector.h>

int main() {
    // Wide miss: 255 fits in the underlying uint8_t but vastly
    // exceeds the bounded_above<4> predicate.  This catches the
    // "lost narrowing" bug class where a wider intermediate is
    // truncated to uint8_t before being assigned.
    constexpr crucible::IterationDetector::MatchPos bad{uint8_t{255}};
    (void)bad;
    return 0;
}
