// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-041 mint_restore fixture #1: restore promotes a cold handle into the
// warm tier after validating the content hash, so RestorableTier requires a
// move-constructible payload.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

namespace cipher = ::crucible::cipher;
namespace tier = ::crucible::safety::cipher_tier;

struct ImmovablePayload {
    ImmovablePayload() = default;
    ImmovablePayload(const ImmovablePayload&) = delete;
    ImmovablePayload& operator=(const ImmovablePayload&) = delete;
    ImmovablePayload(ImmovablePayload&&) = delete;
    ImmovablePayload& operator=(ImmovablePayload&&) = delete;
};

using BadMint = decltype(cipher::mint_restore<ImmovablePayload>(
    std::declval<tier::Cold<ImmovablePayload>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
