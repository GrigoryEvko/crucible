// *_sat_det is pinned to DetSafe<Pure, ...>.  Cross-tier use must go
// through relax<WeakerTier>(); implicit assignment to another DetSafe
// tier would erase the production-site proof obligation.
//
// Old spelling: test/safety_neg/neg_saturate_det_cross_tier_assignment.cpp.

#include <fixy/Bands.h>
#include <fixy/Saturate.h>
#include <fixy/Saturated.h>

int main() {
    using Sat = fixy::Saturated<unsigned>;
    fixy::DetSafe<fixy::DetSafeTier_v::PhiloxRng, Sat> wrong = fixy::sat::mul_sat_det<unsigned>(2u, 3u);
    return static_cast<int>(wrong.peek().value());
}
