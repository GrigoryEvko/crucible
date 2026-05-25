// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `fixy::substr::mpsc::mint_mpsc_producer_endpoint<Channel>(Channel&)`.
//
// Violation: pass an "almost-channel" struct that provides ALL six
// nested type aliases the `MpscChannelSessionSurface` concept
// requires (`value_type`, `user_tag`, `producer_tag`,
// `consumer_tag`, `ProducerHandle`, `ConsumerHandle`) BUT lacks the
// `ch.producer()` method shape.  The concept rejects on the method-
// requirement clause — distinct mismatch class from the non-channel
// fixture (which rejects on the FIRST typedef requirement).
//
// Pairs with neg_fixy_substr_mpsc_producer_endpoint_non_channel.cpp.
//
// Expected diagnostic: "constraints not satisfied" /
// "MpscChannelSessionSurface" / "no matching function" / "producer"
// / "no member named" / "mint_mpsc_producer_endpoint".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

#include <optional>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_almost_mpsc_producer {

struct AlmostMpscChannel {
    using value_type = int;
    struct UserTag {};
    struct ProducerTag {};
    struct ConsumerTag {};
    using user_tag = UserTag;
    using producer_tag = ProducerTag;
    using consumer_tag = ConsumerTag;

    struct ProducerHandle {
        bool try_push(int const&) { return true; }
    };
    struct ConsumerHandle {
        std::optional<int> try_pop() { return std::nullopt; }
    };

    // NOTE: deliberately MISSING `.producer()` — concept fails here.
    // The hand-rolled "almost channel" must mirror the real
    // PermissionedMpscChannel consumer() signature so the
    // MpscChannelSessionSurface concept passes the six typedef checks and
    // rejects ONLY on the missing producer() method — the substrate
    // spelling IS the shape under test, not production code.
    ConsumerHandle consumer(
        ::crucible::safety::Permission<ConsumerTag>&&) {  // FIXY-DISCIPLINE-OK: neg-fixture mirrors real channel consumer() signature
        return ConsumerHandle{};
    }
};

}  // namespace neg_fixy_almost_mpsc_producer

int main() {
    neg_fixy_almost_mpsc_producer::AlmostMpscChannel ch{};
    auto bad = fmpsc::mint_mpsc_producer_endpoint(ch);
    (void)bad;
    return 0;
}
