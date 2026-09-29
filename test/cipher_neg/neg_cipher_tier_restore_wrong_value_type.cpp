// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_restore refuses a cold band over another value type.  The value
// type is part of the restore boundary: a Cold<int> handle cannot pass as
// the cold handle of mint_restore<ContentHash>, because that would restore
// bytes under the wrong typed content identity.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

using BadMint = decltype(::crucible::cipher::mint_restore<::crucible::ContentHash>(
    std::declval<::fixy::cipher_tier::Cold<int>>(), std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
