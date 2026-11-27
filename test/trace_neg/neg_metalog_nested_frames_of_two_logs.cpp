// Negative-compile test.  This file must fail to compile.
//
// A frame of one metadata log in a frame of another one holds the ends of two
// logs.  The inner body holds a reference to the end of the outer log, where
// it could keep a borrow, so the store gate of the frame door refuses it.
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
                                                  static_cast<void>(producer);
                                                  static_cast<void>(consumer);
                                              });
        });
    return 0;
}
