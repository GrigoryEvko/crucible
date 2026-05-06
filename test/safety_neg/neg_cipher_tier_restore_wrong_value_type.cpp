// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-041 mint_restore fixture #2: the cold handle value type is part of the
// mint boundary. A Cold<int> handle cannot satisfy mint_restore<ContentHash>,
// because that would restore bytes under the wrong typed content identity.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

namespace cipher = ::crucible::cipher;
namespace tier = ::crucible::safety::cipher_tier;

using BadMint = decltype(cipher::mint_restore<::crucible::ContentHash>(
    std::declval<tier::Cold<int>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
