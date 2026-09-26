// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The count of a Lifeguard event batch cannot be advanced from outside.
// reserve_next() is the only function that moves it, and it refuses at
// the capacity, so the count cannot pass the event slots.

#include <crucible/canopy/Lifeguard.h>

int main() {
    crucible::canopy::LifeguardEventBatch<4> events{};
    ++events.count;
    return static_cast<int>(events.size().value());
}
