// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-215 fixture #1: a zero-participant vector clock has no causal
// content, so the MaxNodes concept gate rejects it at the mint site.

#include <crucible/canopy/VectorClock.h>

int main() {
    auto clock = crucible::canopy::mint_vector_clock<0>(
        crucible::effects::testing::init(),
        0);
    (void)clock;
    return 0;
}
