// Negative-compile test.  This file must fail to compile.
//
// MetaLog::reset moves the counters of a log back to the start.  A frame of
// one log in a frame of another one gives a body the ends of two logs, and the
// reset would move the counters of a log whose consumer a second thread can
// hold.  The reset takes the pair of the two ends of one split, and only a
// pair door builds a pair, so two ends of two logs reach no reset.
//
// Expected diagnostic: the reset takes a pair, not two ends.

#include <crucible/MetaLog.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    auto first = ::fixy::concurrent::mint_spsc_split<crucible::MetaLog>(test.alloc, test.alloc);
    auto second = ::fixy::concurrent::mint_spsc_split<crucible::MetaLog>(test.alloc, test.alloc);
    ::fixy::concurrent::with_producer(
        first.split, first.producer_token, [&second](crucible::MetaLog::Producer& producer) noexcept {
            ::fixy::concurrent::with_consumer(second.split, second.consumer_token,
                                              [&producer](crucible::MetaLog::Consumer& consumer) noexcept {
                                                  crucible::MetaLog::reset(producer, consumer);
                                              });
        });
    return 0;
}
