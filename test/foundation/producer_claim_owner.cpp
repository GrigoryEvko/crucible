// The shared object that builds and destroys the brand.  See
// producer_claim_libraries.h.

#include "producer_claim_libraries.h"

#include <foundation/effects/Effect.h>

#include <utility>

namespace producer_claim_libraries {

// The Box builds the brand with value-initialization, not aggregate
// initialization: the implicit default constructor of the brand builds the
// claim, and only the brand has access.
::foundation::core::Box<SharedBrand> make_brand() noexcept {
    return ::foundation::core::mint_box<SharedBrand>(::foundation::effects::testing::test().alloc);
}

// The local Box ends at the end of this body, so the destructor of the claim
// runs in this shared object.
void destroy_brand(::foundation::core::Box<SharedBrand>&& brand) noexcept {
    ::foundation::core::Box<SharedBrand> const ended{std::move(brand)};
}

}  // namespace producer_claim_libraries
