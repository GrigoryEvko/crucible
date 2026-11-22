// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A push into a lane takes the proof of the lane, not its index (THE WRITERS
// of fixy/concurrent/LaneRing.h).  An index names a lane, but each thread can
// name it, so a push with an index would let two threads write the SPSC ring
// of one lane.
//
// Expected diagnostic: the index of a lane does not convert to the proof of a
// lane.

#include <fixy/concurrent/LaneRing.h>

namespace {

namespace cc = ::fixy::concurrent;

struct Owner final {};

using Lanes = cc::LaneRing<int, 8, cc::LaneCount{2}, Owner>;

[[maybe_unused]] bool push_by_index(Lanes& ring) noexcept {
    return ring.try_push(Lanes::lane_index{::foundation::core::index<1>()}, 7);
}

}  // namespace

int main() { return 0; }
