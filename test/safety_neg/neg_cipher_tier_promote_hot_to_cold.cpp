// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_promote refuses Hot to Cold.  That move goes down the tier chain,
// which is an eviction, and an eviction goes through mint_demote.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

int main() {
    using crucible::ContentHash;
    using crucible::cipher::mint_promote;
    using ::fixy::CipherTierTag_v;

    auto hot = ::fixy::mint_band<::fixy::cipher_tier::Hot<ContentHash>>(ContentHash{0x1234ULL});
    auto cold_claim = mint_promote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(std::move(hot));
    return static_cast<bool>(std::move(cold_claim).consume()) ? 0 : 1;
}
