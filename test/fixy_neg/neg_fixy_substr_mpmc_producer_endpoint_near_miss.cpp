// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074k fixture #2 for fixy::substr::mpmc::mint_mpmc_producer_endpoint
// (token mint, single-argument, MpmcChannelSession.h:239).  The
// template-parameter constraint `MpmcChannelSessionSurface Channel` requires
// `{ ch.producer() } -> std::same_as<std::optional<ProducerHandle>>`
// (MpmcChannelSession.h:216-217).  NearMissChannel satisfies EVERY other
// clause — all six nested types, consumer()->optional<ConsumerHandle>,
// try_push()->bool, try_pop()->optional<value_type> — but producer()
// returns ProducerHandle DIRECTLY instead of optional<ProducerHandle>, so
// the concept fails on exactly that one method-signature clause (the very
// factory the producer-endpoint mint forwards to).
//
// Distinct mismatch class from
// neg_fixy_substr_mpmc_producer_endpoint_non_surface.cpp (#1): there a bare
// type failed the FIRST nested-type requirement; here a near-miss surface
// fails a single method-return-shape clause.  This catches the real-world
// bug "producer() returns the handle directly instead of an optional".
//
// Expected diagnostic: MpmcChannelSessionSurface / constraints not
// satisfied / no matching function.

#include <optional>

#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;

namespace neg_fixy_substr_mpmc_producer_endpoint_near_miss {
struct UTag {};
struct PTag {};
struct CTag {};

struct FakeProducerHandle {
    bool try_push(int const&) { return true; }
};
struct FakeConsumerHandle {
    std::optional<int> try_pop() { return std::nullopt; }
};

// Satisfies every MpmcChannelSessionSurface clause EXCEPT producer()'s
// return type: producer() returns ProducerHandle, not optional<ProducerHandle>.
struct NearMissChannel {
    using value_type    = int;
    using user_tag      = UTag;
    using producer_tag  = PTag;
    using consumer_tag  = CTag;
    using ProducerHandle = FakeProducerHandle;
    using ConsumerHandle = FakeConsumerHandle;

    // BUG: must return std::optional<FakeProducerHandle>.
    FakeProducerHandle producer() { return {}; }
    std::optional<FakeConsumerHandle> consumer() { return std::nullopt; }
};
}  // namespace neg_fixy_substr_mpmc_producer_endpoint_near_miss

int main() {
    neg_fixy_substr_mpmc_producer_endpoint_near_miss::NearMissChannel ch{};

    [[maybe_unused]] auto bad =
        fsubstr::mpmc::mint_mpmc_producer_endpoint(ch);
    return 0;
}
