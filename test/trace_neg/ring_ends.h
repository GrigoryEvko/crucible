#pragma once

// The ends of a new trace ring and of a new metadata log for a fixture.  Each
// helper builds a keyed split (fixy/concurrent/SpscEnds.h) and calls the body
// with one end, so a fixture calls a door of a side with the end of that
// side, and the gate of the door gives the diagnostic of the fixture.  The
// parts of the split are read by name, with no structured binding.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/concurrent/SpscEnds.h>
#include <foundation/effects/Effect.h>

namespace crucible::test {

template <class Body>
void with_ring_producer(Body&& body) noexcept {
    auto minted = ::fixy::concurrent::mint_spsc_split<TraceRing>(::foundation::effects::testing::test().alloc);
    ::fixy::concurrent::with_producer(minted.split, minted.producer_token, static_cast<Body&&>(body));
}

template <class Body>
void with_ring_consumer(Body&& body) noexcept {
    auto minted = ::fixy::concurrent::mint_spsc_split<TraceRing>(::foundation::effects::testing::test().alloc);
    ::fixy::concurrent::with_consumer(minted.split, minted.consumer_token, static_cast<Body&&>(body));
}

template <class Body>
void with_log_producer(Body&& body) noexcept {
    auto minted = ::fixy::concurrent::mint_spsc_split<MetaLog>(::foundation::effects::testing::test().alloc,
                                                               ::foundation::effects::testing::test().alloc);
    ::fixy::concurrent::with_producer(minted.split, minted.producer_token, static_cast<Body&&>(body));
}

}  // namespace crucible::test
