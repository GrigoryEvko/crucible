// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_vector_clock takes the init context only.  A test context must not
// mint production vector-clock state.

#include <crucible/canopy/VectorClock.h>

int main() {
    auto clock = crucible::canopy::mint_vector_clock<4>(::foundation::effects::testing::test(), 0);
    (void)clock;
    return 0;
}
