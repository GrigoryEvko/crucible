// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-215 fixture #2: mint_vector_clock is an Init-only factory.
// Test contexts cannot mint production vector-clock state.

#include <crucible/canopy/VectorClock.h>

int main() {
    auto clock = crucible::canopy::mint_vector_clock<4>(
        crucible::effects::Test{},
        0);
    (void)clock;
    return 0;
}
