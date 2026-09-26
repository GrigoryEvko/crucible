// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_demote refuses a payload that cannot move.  A demotion consumes the
// source band and moves its value into the band of the colder tier, so the
// payload must be move-constructible.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

struct ImmovablePayload {
    ImmovablePayload() = default;
    ImmovablePayload(const ImmovablePayload&) = delete;
    ImmovablePayload& operator=(const ImmovablePayload&) = delete;
    ImmovablePayload(ImmovablePayload&&) = delete;
    ImmovablePayload& operator=(ImmovablePayload&&) = delete;
};

using BadMint = decltype(::crucible::cipher::mint_demote<::fixy::CipherTierTag_v::Hot, ::fixy::CipherTierTag_v::Warm>(
    std::declval<::fixy::cipher_tier::Hot<ImmovablePayload>>()));

int main() { return sizeof(BadMint); }
