// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The detector's constructor is private: mint_asymmetric_failure_detector,
// gated on an Init-row context, is the only way to build one.

#include <crucible/topology/AsymmetricFailure.h>

int main() {
    crucible::topology::AsymmetricFailureDetector<2, 8, 8> detector{crucible::topology::AsymmetricFailurePolicy{}};
    (void)detector;
    return 0;
}
