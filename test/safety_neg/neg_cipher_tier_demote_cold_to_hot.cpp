// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: mint_demote<Cold, Hot> attempts to use the demotion mint
// site in the lattice's upward direction.  Cold -> Hot requires a
// restore/promotion boundary, not an eviction path.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

int main() {
    using crucible::ContentHash;
    using crucible::cipher::mint_demote;
    using crucible::safety::CipherTierTag_v;
    using crucible::safety::cipher_tier::Cold;

    Cold<ContentHash> cold{ContentHash{0x1234ULL}};
    auto hot_claim = mint_demote<
        CipherTierTag_v::Cold,
        CipherTierTag_v::Hot>(std::move(cold));
    return static_cast<bool>(std::move(hot_claim).consume()) ? 0 : 1;
}
