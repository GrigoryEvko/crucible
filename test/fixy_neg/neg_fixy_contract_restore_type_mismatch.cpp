// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Contract fixture: mint_restore via fixy::contract::cipher
// rejects when the cold handle's value type does not match the
// explicit template argument T.
//
// Violation: the cold handle's typed content identity is part of the
// mint boundary.  A Cold<int> handle cannot satisfy
// mint_restore<ContentHash>.  Routing through the fixy:: alias must
// reject identically to the substrate fixture
// neg_cipher_tier_restore_wrong_value_type.
//
// Expected diagnostic: no matching function / Cold<T> mismatch.

#include <crucible/fixy/Contract.h>

#include <utility>

namespace fcipher = ::crucible::fixy::contract::cipher;

using BadMint = decltype(fcipher::mint_restore<::crucible::ContentHash>(
    std::declval<fcipher::ColdTierHandle<int>>(),
    std::declval<::crucible::ContentHash>()));

int main() { return sizeof(BadMint); }
