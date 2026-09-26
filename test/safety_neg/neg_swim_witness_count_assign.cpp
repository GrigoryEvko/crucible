// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The count of a SWIM witness set cannot be set from outside.  A count
// past the capacity would make size() mint a false bound and make each
// loop over the live witnesses read past the array.

#include <crucible/canopy/Swim.h>

#include <cstdint>

int main() {
    crucible::canopy::SwimWitnessSet<4> witnesses{};
    witnesses.count = std::uint16_t{41};
    return static_cast<int>(witnesses.size().value());
}
