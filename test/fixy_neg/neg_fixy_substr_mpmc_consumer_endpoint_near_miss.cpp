// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MPMC endpoint fixture 4 for
// safety::proto::mpmc_channel_session::mint_mpmc_consumer_endpoint
// (token mint, single-argument, sessions/MpmcChannelSession.h).  The
// template-parameter constraint `MpmcChannelSessionSurface Channel` requires
// `{ ch.consumer() } -> std::same_as<std::optional<ConsumerHandle>>`
// (a clause of that concept).  NearMissChannel satisfies EVERY other
// clause — all six nested types, producer()->optional<ProducerHandle>,
// try_push()->bool, try_pop()->optional<value_type> — but consumer()
// returns ConsumerHandle DIRECTLY instead of optional<ConsumerHandle>, so
// the concept fails on exactly that one method-signature clause (the very
// factory the consumer-endpoint mint forwards to).
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_consumer_endpoint_non_surface.cpp (#3): there a bare
// type failed the FIRST nested-type requirement; here a near-miss surface
// fails a single method-return-shape clause.  This catches the real-world
// bug "consumer() returns the handle directly instead of an optional".
//
// Expected diagnostic: MpmcChannelSessionSurface / constraints not
// satisfied / no matching function.

#include <optional>

#include <crucible/sessions/_MpmcChannelSession.h>

namespace fmpmc = ::crucible::safety::proto::mpmc_channel_session;

namespace neg_fixy_substr_mpmc_consumer_endpoint_near_miss {
struct UTag {};
struct PTag {};
struct CTag {};

struct FakeProducerHandle {
    bool try_push(int const&) { return true; }
};
struct FakeConsumerHandle {
    std::optional<int> try_pop() { return std::nullopt; }
};

// Satisfies every MpmcChannelSessionSurface clause EXCEPT consumer()'s
// return type: consumer() returns ConsumerHandle, not optional<ConsumerHandle>.
struct NearMissChannel {
    using value_type = int;
    using user_tag = UTag;
    using producer_tag = PTag;
    using consumer_tag = CTag;
    using ProducerHandle = FakeProducerHandle;
    using ConsumerHandle = FakeConsumerHandle;

    std::optional<FakeProducerHandle> producer() { return std::nullopt; }
    // BUG: must return std::optional<FakeConsumerHandle>.
    FakeConsumerHandle consumer() { return {}; }
};
}  // namespace neg_fixy_substr_mpmc_consumer_endpoint_near_miss

int main() {
    neg_fixy_substr_mpmc_consumer_endpoint_near_miss::NearMissChannel ch{};

    [[maybe_unused]] auto bad = fmpmc::mint_mpmc_consumer_endpoint(ch);
    return 0;
}
