// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Saturate-3 (#1001): *_sat_det is pinned to DetSafe<Pure, ...>.
// Cross-tier use must go through relax<WeakerTier>(); implicit
// assignment to another DetSafe tier would erase the production-site
// proof obligation.

#include <crucible/Saturate.h>

int main() {
  using Sat = crucible::safety::Saturated<unsigned>;
  crucible::safety::DetSafe<
      crucible::safety::DetSafeTier_v::PhiloxRng, Sat> wrong =
          crucible::sat::mul_sat_det<unsigned>(2u, 3u);
  return static_cast<int>(wrong.peek().value());
}
