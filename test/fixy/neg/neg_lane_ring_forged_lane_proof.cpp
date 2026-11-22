// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Only the owner of a ring of lanes mints the proof of a lane, one time for
// each holder of the lane (THE WRITERS of fixy/concurrent/LaneRing.h).  A
// stranger that builds a proof would push into a lane that another producer
// holds, and two producers would write one SPSC ring.
//
// Expected diagnostic: the constructor of the proof of a lane is private.

#include <fixy/concurrent/LaneRing.h>

namespace {

namespace cc = ::fixy::concurrent;

struct Owner final {};

using Lanes = cc::LaneRing<int, 8, cc::LaneCount{2}, Owner>;

[[maybe_unused]] bool push_without_the_lane(Lanes& ring) noexcept {
    return ring.try_push(Lanes::LaneProof{Lanes::lane_index{::foundation::core::index<1>()}}, 7);
}

}  // namespace

int main() { return 0; }
