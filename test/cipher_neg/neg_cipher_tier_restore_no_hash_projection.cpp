// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_restore refuses a payload that does not specialize
// content_hash_projection.  The payload below can move, so it passes the
// move check, and the refusal names the missing projection.  Without the
// projection gate, the hash check would run for ContentHash alone, and a
// tampered cold value of every other type would restore unchecked.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

struct ProductionPayload {
    int data = 0;
    long version = 0;

    constexpr ProductionPayload() noexcept = default;
    constexpr ProductionPayload(ProductionPayload&&) noexcept = default;
    constexpr ProductionPayload& operator=(ProductionPayload&&) noexcept = default;
    ProductionPayload(const ProductionPayload&) = delete;
    ProductionPayload& operator=(const ProductionPayload&) = delete;
};

using BadMint = decltype(::crucible::cipher::mint_restore<ProductionPayload>(
    std::declval<::fixy::cipher_tier::Cold<ProductionPayload>>(), std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
