// Negative-compile test.  This file must fail to compile.
//
// TraceRing::reset moves each counter of the ring back to zero, so no other
// thread may read a counter while it runs.  The reset takes the producer end
// and the consumer end of one ring, and only a frame of the two ends lends the
// two to one thread.  A producer that holds only its own end, while a second
// thread holds the consumer end and spins on the head, has no consumer end to
// give, so its reset does not compile.
//
// Expected diagnostic: the producer end does not convert to the consumer end.

#include <crucible/TraceRing.h>

#include "ring_ends.h"

int main() {
    crucible::test::with_ring_producer(
        [](crucible::TraceRing::Producer& ring) noexcept { crucible::TraceRing::reset(ring, ring); });
    return 0;
}
