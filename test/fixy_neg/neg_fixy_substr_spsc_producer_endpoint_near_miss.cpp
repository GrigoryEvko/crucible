// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-045-audit fixture #2 for fixy::substr::spsc::mint_spsc_producer_endpoint
// (token mint, two-argument, fixy/Substr.h v045::).  The template-parameter
// constraint `SpscChannelSessionSurface Channel` requires
// `{ ch.producer(std::move(prod_perm)) } -> std::same_as<ProducerHandle>`
// (fixy/Substr.h v045:: SpscChannelSessionSurface clause).  NearMissChannel
// satisfies EVERY other clause — all six nested types, consumer()->ConsumerHandle,
// try_push()->bool, try_pop()->optional<value_type> — but producer()
// returns std::optional<ProducerHandle> instead of bare ProducerHandle, so
// the concept fails on exactly that one method-signature clause (the very
// factory the producer-endpoint mint forwards to).
//
// Distinct mismatch class from
// neg_fixy_substr_spsc_producer_endpoint_non_surface.cpp (#1): there a bare
// type failed the FIRST nested-type requirement; here a near-miss surface
// fails a single method-return-shape clause.  This catches the real-world
// bug "producer() returns optional<handle> when SPSC's exact-one-producer
// linearity requires the bare-handle form" (the mistake an MPMC author
// would make on a copy-paste port).
//
// Expected diagnostic: SpscChannelSessionSurface / constraints not
// satisfied / no matching function / same_as.

#include <optional>
#include <utility>

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fsubstr = ::crucible::fixy::substr;
namespace saf     = ::crucible::safety;

namespace neg_fixy_substr_spsc_producer_endpoint_near_miss {
struct UTag {};
struct PTag {};
struct CTag {};

struct FakeProducerHandle {
    bool try_push(int const&) { return true; }
};
struct FakeConsumerHandle {
    std::optional<int> try_pop() { return std::nullopt; }
};

// Satisfies every SpscChannelSessionSurface clause EXCEPT producer()'s
// return type: producer() returns optional<ProducerHandle>, not bare
// ProducerHandle.  SPSC's exact-one-producer linearity contract requires
// bare-handle form (the optional-form is for MPMC where producer() may
// fail when no slot is free).
struct NearMissChannel {
    using value_type     = int;
    using user_tag       = UTag;
    using whole_tag      = void;  // placeholder — surface concept doesn't probe whole_tag
    using producer_tag   = PTag;
    using consumer_tag   = CTag;
    using ProducerHandle = FakeProducerHandle;
    using ConsumerHandle = FakeConsumerHandle;

    // BUG: must return FakeProducerHandle (bare), not optional<FakeProducerHandle>.
    std::optional<FakeProducerHandle> producer(saf::Permission<PTag>&&) {
        return std::nullopt;
    }
    FakeConsumerHandle consumer(saf::Permission<CTag>&&) {
        return FakeConsumerHandle{};
    }
};
}  // namespace neg_fixy_substr_spsc_producer_endpoint_near_miss

int main() {
    neg_fixy_substr_spsc_producer_endpoint_near_miss::NearMissChannel ch{};
    auto perm = saf::mint_permission_root<
        neg_fixy_substr_spsc_producer_endpoint_near_miss::PTag>();

    [[maybe_unused]] auto bad =
        fsubstr::spsc::mint_spsc_producer_endpoint(ch, std::move(perm));
    return 0;
}
