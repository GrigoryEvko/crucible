// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-045-audit fixture #4 for fixy::substr::spsc::mint_spsc_consumer_endpoint
// (token mint, two-argument, fixy/Substr.h v045::).  The template-parameter
// constraint `SpscChannelSessionSurface Channel` requires
// `{ ch.consumer(std::move(cons_perm)) } -> std::same_as<ConsumerHandle>`
// (fixy/Substr.h v045:: SpscChannelSessionSurface clause).  NearMissChannel
// satisfies EVERY other clause — all six nested types, producer()->ProducerHandle,
// try_push()->bool, try_pop()->optional<value_type> — but consumer()
// returns std::optional<ConsumerHandle> instead of bare ConsumerHandle, so
// the concept fails on exactly that one method-signature clause (the very
// factory the consumer-endpoint mint forwards to).
//
// Distinct mismatch class from
// neg_fixy_substr_spsc_consumer_endpoint_non_surface.cpp (#3): there a bare
// type failed the FIRST nested-type requirement; here a near-miss surface
// fails a single method-return-shape clause.  This catches the real-world
// bug "consumer() returns optional<handle> when SPSC's exact-one-consumer
// linearity requires the bare-handle form".
//
// Expected diagnostic: SpscChannelSessionSurface / constraints not
// satisfied / no matching function / same_as.

#include <optional>
#include <utility>

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsubstr = ::crucible::fixy::substr;
namespace saf     = ::crucible::safety;

namespace neg_fixy_substr_spsc_consumer_endpoint_near_miss {
struct UTag {};
struct PTag {};
struct CTag {};

struct FakeProducerHandle {
    bool try_push(int const&) { return true; }
};
struct FakeConsumerHandle {
    std::optional<int> try_pop() { return std::nullopt; }
};

// Satisfies every SpscChannelSessionSurface clause EXCEPT consumer()'s
// return type: consumer() returns optional<ConsumerHandle>, not bare
// ConsumerHandle.  SPSC's exact-one-consumer linearity contract requires
// bare-handle form.
struct NearMissChannel {
    using value_type     = int;
    using user_tag       = UTag;
    using whole_tag      = void;  // placeholder — surface concept doesn't probe whole_tag
    using producer_tag   = PTag;
    using consumer_tag   = CTag;
    using ProducerHandle = FakeProducerHandle;
    using ConsumerHandle = FakeConsumerHandle;

    FakeProducerHandle producer(saf::Permission<PTag>&&) {
        return FakeProducerHandle{};
    }
    // BUG: must return FakeConsumerHandle (bare), not optional<FakeConsumerHandle>.
    std::optional<FakeConsumerHandle> consumer(saf::Permission<CTag>&&) {
        return std::nullopt;
    }
};
}  // namespace neg_fixy_substr_spsc_consumer_endpoint_near_miss

int main() {
    neg_fixy_substr_spsc_consumer_endpoint_near_miss::NearMissChannel ch{};
    auto perm = saf::mint_permission_root<
        neg_fixy_substr_spsc_consumer_endpoint_near_miss::CTag>();

    [[maybe_unused]] auto bad =
        fsubstr::spsc::mint_spsc_consumer_endpoint(ch, std::move(perm));
    return 0;
}
