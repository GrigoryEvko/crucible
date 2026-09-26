// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_restore refuses a payload that cannot move.  A restore checks the
// content hash and then moves the cold value into the warm tier, so the
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

using BadMint = decltype(::crucible::cipher::mint_restore<ImmovablePayload>(
    std::declval<::fixy::cipher_tier::Cold<ImmovablePayload>>(), std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
