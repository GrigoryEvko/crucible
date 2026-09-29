// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_promote refuses a payload that cannot move.  A promotion consumes
// the source band and moves its value into the band of the hotter tier,
// so the payload must be move-constructible.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

struct ImmovablePayload {
    ImmovablePayload() = default;
    ImmovablePayload(const ImmovablePayload&) = delete;
    ImmovablePayload& operator=(const ImmovablePayload&) = delete;
    ImmovablePayload(ImmovablePayload&&) = delete;
    ImmovablePayload& operator=(ImmovablePayload&&) = delete;
};

using BadMint = decltype(::crucible::cipher::mint_promote<::fixy::CipherTierTag_v::Cold, ::fixy::CipherTierTag_v::Warm>(
    std::declval<::fixy::cipher_tier::Cold<ImmovablePayload>>()));

int main() { return sizeof(BadMint); }
