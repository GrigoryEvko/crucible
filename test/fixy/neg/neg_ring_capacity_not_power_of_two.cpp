// A ring maps a monotonic position to a slot with `position & (Capacity
// - 1)`.  That is the position modulo Capacity only when Capacity is a
// power of two; for any other Capacity the mask drops bits and two live
// positions collide on one slot, which the ring has no way to detect.
//
// The value type is fine and the capacity is non-zero, so only the
// power-of-two clause can reject this.

#include <fixy/concurrent/SpscRing.h>

namespace c = fixy::concurrent;

// 12 is not a power of two: 12 - 1 is 0b1011, so positions 4 and 12
// both map to slot 0 while both are in flight.
using Broken = c::SpscRing<int, 12>;

int main() {
    Broken ring{};
    return ring.empty_approx() ? 0 : 1;
}
