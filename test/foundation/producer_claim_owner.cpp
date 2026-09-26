// The shared object that builds and destroys the brand.  See
// producer_claim_libraries.h.

#include "producer_claim_libraries.h"

namespace producer_claim_libraries {

// Value-initialization, not aggregate initialization: the implicit default
// constructor of the brand builds the claim, and only the brand has access.
SharedBrand* make_brand() { return new SharedBrand(); }

void destroy_brand(SharedBrand* brand) { delete brand; }

}  // namespace producer_claim_libraries
