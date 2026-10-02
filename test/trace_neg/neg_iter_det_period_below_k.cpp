// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting IterationDetector::Period with a length of K - 1 ops in
// a constant evaluation.
//
// IterationDetector::Period is ::fixy::Refined<in_range<K, MAX_PERIOD>,
// uint32_t>, and ::fixy::mint_refined<kPeriodBound> is its one door.  A
// period shorter than the window of K ops gives no window that occurs once
// per period, so the search takes the smallest multiple of a short period
// that is K or more.  A revision that loosens the lower bound admits the
// length and this fixture reports it.

#include <crucible/IterationDetector.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::IterationDetector::Period bad =
        ::fixy::mint_refined<crucible::IterationDetector::kPeriodBound>(uint32_t{crucible::IterationDetector::K - 1});
    (void)bad;
    return 0;
}
