// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-10 fixture: mint_restore<T> requires T to opt in to the
// `content_hash_projection<T>` customization point.  A move-
// constructible production payload that ships no projection
// specialization must fail at the `RestorableHashed` concept gate
// — pre-CR-10 it compiled silently and the verification check
// fired only for T = ContentHash, accepting tampered cold blobs
// for every other T.
//
// Expected diagnostic: RestorableTier / RestorableHashed /
// content_hash_projection / constraints not satisfied — must name
// the missing customization point, NOT the cold-handle type
// shape (which is the safety_neg sibling fixture's failure mode).

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

namespace cipher = ::crucible::cipher;
namespace tier   = ::crucible::safety::cipher_tier;

// Movable production-shape payload — passes std::move_constructible
// but does NOT specialize content_hash_projection.  After CR-10 this
// must red at the RestorableHashed gate.
struct ProductionPayload {
    int  data    = 0;
    long version = 0;

    constexpr ProductionPayload() noexcept = default;
    constexpr ProductionPayload(ProductionPayload&&) noexcept = default;
    constexpr ProductionPayload& operator=(ProductionPayload&&) noexcept = default;
    ProductionPayload(const ProductionPayload&) = delete;
    ProductionPayload& operator=(const ProductionPayload&) = delete;
};

using BadMint = decltype(cipher::mint_restore<ProductionPayload>(
    std::declval<tier::Cold<ProductionPayload>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
