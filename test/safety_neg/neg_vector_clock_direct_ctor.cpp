// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of VectorClock is private.  mint_vector_clock is the
// only door, so a clock cannot come into being without the Init context.
#include <crucible/canopy/VectorClock.h>
int main() {
    namespace cc = crucible::canopy;
    constexpr auto node = ::fixy::mint_refined<cc::vector_clock_node_bound<4>>(std::uint16_t{1});
    cc::VectorClock<4> clock{node};
    (void)clock;
    return 0;
}
