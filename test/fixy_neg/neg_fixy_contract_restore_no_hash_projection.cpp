// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture: mint_restore via fixy::contract::cipher
// rejects when the payload T does not opt in to the
// `content_hash_projection<T>` customization point.  Routing through
// the fixy:: alias must reject identically to the substrate fixture
// neg_cipher_tier_restore_no_hash_projection — the gap was that pre-
// CR-10, mint_restore<T> would compile and run for ANY move-
// constructible T but only verify the supplied content_hash against
// the cold handle when T == ContentHash (production payloads bypassed
// the check entirely).
//
// Expected diagnostic: RestorableTier / RestorableHashed /
// content_hash_projection / constraints not satisfied — must name
// the missing customization point.

#include <crucible/fixy/Contract.h>

#include <utility>

namespace fcipher = ::crucible::fixy::contract::cipher;

// Movable production-shape payload — passes std::move_constructible
// but does NOT specialize content_hash_projection.  After CR-10 the
// fixy:: alias must red at the same RestorableHashed gate as the
// substrate path.
struct ProductionPayload {
    int  data    = 0;
    long version = 0;

    constexpr ProductionPayload() noexcept = default;
    constexpr ProductionPayload(ProductionPayload&&) noexcept = default;
    constexpr ProductionPayload& operator=(ProductionPayload&&) noexcept = default;
    ProductionPayload(const ProductionPayload&) = delete;
    ProductionPayload& operator=(const ProductionPayload&) = delete;
};

using BadMint = decltype(fcipher::mint_restore<ProductionPayload>(
    std::declval<fcipher::ColdTierHandle<ProductionPayload>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
