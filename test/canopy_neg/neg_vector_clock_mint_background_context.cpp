// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_vector_clock takes the Init context.  A background context is a
// different context, and it does not convert to Init.
#include <crucible/canopy/VectorClock.h>
int main() {
    auto clock = crucible::canopy::mint_vector_clock<4>(::foundation::effects::testing::bg(), 0);
    (void)clock;
    return 0;
}
