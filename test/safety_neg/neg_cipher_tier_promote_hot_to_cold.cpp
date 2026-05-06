// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: mint_promote<Hot, Cold> attempts to use the promotion
// mint site in the lattice's downward direction.  Hot -> Cold is an
// eviction/demotion, not a promotion.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

int main() {
    using crucible::ContentHash;
    using crucible::cipher::mint_promote;
    using crucible::safety::CipherTierTag_v;
    using crucible::safety::cipher_tier::Hot;

    Hot<ContentHash> hot{ContentHash{0x1234ULL}};
    auto cold_claim = mint_promote<
        CipherTierTag_v::Hot,
        CipherTierTag_v::Cold>(std::move(hot));
    return static_cast<bool>(std::move(cold_claim).consume()) ? 0 : 1;
}
