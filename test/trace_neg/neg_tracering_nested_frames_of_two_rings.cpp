// Negative-compile test.  This file must fail to compile.
//
// TraceRing::reset moves the counters of a ring back to the start.  A frame of
// one ring can hold a frame of another one, so a body holds the producer end
// of one ring and the consumer end of the other.  The two ends would give a
// reset of two rings, whose consumer a second thread can hold.  The reset
// takes the pair of the two ends of one split, and only a pair door builds a
// pair, so two ends of two rings reach no reset.
//
// Expected diagnostic: the reset takes a pair, not two ends.

#include <crucible/TraceRing.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    auto first = ::fixy::concurrent::mint_spsc_split<crucible::TraceRing>(test.alloc);
    auto second = ::fixy::concurrent::mint_spsc_split<crucible::TraceRing>(test.alloc);
    ::fixy::concurrent::with_producer(
        first.split, first.producer_token, [&second](crucible::TraceRing::Producer& producer) noexcept {
            ::fixy::concurrent::with_consumer(second.split, second.consumer_token,
                                              [&producer](crucible::TraceRing::Consumer& consumer) noexcept {
                                                  crucible::TraceRing::reset(producer, consumer);
                                              });
        });
    return 0;
}
