// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A vector clock with no participant has no causal content, so the
// MaxNodes concept gate refuses it at the mint.

#include <crucible/canopy/VectorClock.h>

int main() {
    auto clock = crucible::canopy::mint_vector_clock<0>(::foundation::effects::testing::init(), 0);
    (void)clock;
    return 0;
}
