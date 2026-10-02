// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::Period with a length one op above
// MAX_PERIOD in a constant evaluation.
//
// IterationDetector::Period is ::fixy::Refined<in_range<K, MAX_PERIOD>,
// uint32_t>, and ::fixy::mint_refined<kPeriodBound> is its one door.  A
// square of more than MAX_PERIOD ops does not fit in the largest ring of the
// history, so no accepted period is longer.  A revision that loosens the
// bound, or drops the Refined wrap, admits the length and this fixture
// reports it.

#include <crucible/IterationDetector.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::Period bad = ::fixy::mint_refined<crucible::IterationDetector::kPeriodBound>(
        uint32_t{crucible::IterationDetector::MAX_PERIOD + 1});
    (void)bad;
    return 0;
}
