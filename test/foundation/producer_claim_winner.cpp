// The shared object that wins the claim of the brand.  See
// producer_claim_libraries.h.

#include "producer_claim_libraries.h"

namespace producer_claim_libraries {

void win_claim(SharedBrand& brand) { (void)brand.claim.mint_producer_context(); }

}  // namespace producer_claim_libraries
