// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::MatchPos with a value > K-1 = 4
// in a constexpr context.
//
// IterationDetector::match_pos_ is ::fixy::Refined<bounded_above<
// MATCH_POS_MAX>, uint8_t> with MATCH_POS_MAX = K - 1 = 4, and its one door
// is ::fixy::mint_refined<kMatchPosBound>.  The door checks the predicate,
// so 5 makes bounded_above<4>(5) false and the constant evaluation fails.
// A revision that loosens the predicate, or drops the Refined wrap and
// reverts to a raw uint8_t, admits 5 and this fixture reports it.
//
// Value choice: 5 is the smallest forbidden value (= K, the value the
// field never holds by control flow).  It catches off-by-one drift in
// MATCH_POS_MAX.

#include <crucible/IterationDetector.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::MatchPos bad =
        ::fixy::mint_refined<crucible::IterationDetector::kMatchPosBound>(uint8_t{5});
    (void)bad;
    return 0;
}
