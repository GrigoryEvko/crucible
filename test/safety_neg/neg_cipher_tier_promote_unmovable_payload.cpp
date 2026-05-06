// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-041 mint_promote fixture #2: promotion consumes the source tier and
// materializes the hotter tier, so PromotableTier requires a move-constructible
// payload. An immovable payload cannot cross this mint boundary.

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

using BadMint = decltype(cipher::mint_promote<
    ::crucible::safety::CipherTierTag_v::Cold,
    ::crucible::safety::CipherTierTag_v::Warm>(
        std::declval<tier::Cold<ImmovablePayload>>()));

int main() { return sizeof(BadMint); }
