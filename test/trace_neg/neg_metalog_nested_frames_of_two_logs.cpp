// Negative-compile test.  This file must fail to compile.
//
// MetaLog::reset takes the two ends of one log.  A frame of one log in a frame
// of another one would give it the ends of two logs, and the reset would move
// the counters of a log whose consumer a second thread can hold.  The inner
// body holds a reference to the end of the outer log, where it could keep a
// borrow, so the store gate of the frame door refuses it.
//
// Expected diagnostic: the store gate of the frame door.

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
