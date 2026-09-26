// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::MatchPos with the uint8_t maximum
// (255) in a constexpr context.
//
// Companion fixture to neg_iter_det_match_pos_above_max.cpp:
//   - That one tests the smallest forbidden value (5 = K, off-by-one).
//   - This one tests the wide miss (255 = uint8_t max), the value a wider
//     counter reaches when it is cast to uint8_t without the bound.
//
// A regression that admits every value up to K passes the wide-miss
// fixture and fails the boundary one.  A regression that drops the upper
// bound entirely fails this one.  The two fixtures together pin the gate.

#include <crucible/IterationDetector.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::MatchPos bad =
        ::fixy::mint_refined<crucible::IterationDetector::kMatchPosBound>(uint8_t{255});
    (void)bad;
    return 0;
}
