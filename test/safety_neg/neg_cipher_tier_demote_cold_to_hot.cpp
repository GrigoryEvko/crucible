// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_demote refuses Cold to Hot.  That move goes up the tier chain, and
// a move up needs a restore or a promotion, not an eviction path.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

int main() {
    using crucible::ContentHash;
    using crucible::cipher::mint_demote;
    using ::fixy::CipherTierTag_v;

    auto cold = ::fixy::mint_band<::fixy::cipher_tier::Cold<ContentHash>>(ContentHash{0x1234ULL});
    auto hot_claim = mint_demote<CipherTierTag_v::Cold, CipherTierTag_v::Hot>(std::move(cold));
    return static_cast<bool>(std::move(hot_claim).consume()) ? 0 : 1;
}
