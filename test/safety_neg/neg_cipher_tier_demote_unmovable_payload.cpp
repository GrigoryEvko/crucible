// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-041 mint_demote fixture #2: demotion consumes the source tier and
// materializes the colder tier, so DemotableTier requires a move-constructible
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

using BadMint = decltype(cipher::mint_demote<
    ::crucible::safety::CipherTierTag_v::Hot,
    ::crucible::safety::CipherTierTag_v::Warm>(
        std::declval<tier::Hot<ImmovablePayload>>()));

int main() { return sizeof(BadMint); }
