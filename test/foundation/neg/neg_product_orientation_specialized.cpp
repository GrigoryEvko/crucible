// A product lattice states the orientation of its components together.  A
// product with one component whose up is the stronger claim lets the product
// strengthen a claim.  This file tries to give such a product the weaker
// orientation: it specializes the function template spelling
// product_orientation<Stronger, Stronger>.  The orientation of a product is
// an alias template over concepts, so the specialization does not compile,
// and the product keeps the stronger orientation.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/lattices/ProductLattice.h>

namespace {
struct Stronger {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[maybe_unused]] static constexpr foundation::algebra::ClaimOrientation claim_orientation =
        foundation::algebra::ClaimOrientation::stronger_is_higher;
};
}  // namespace

template <>
consteval foundation::algebra::ClaimOrientation
foundation::algebra::product_orientation<Stronger, Stronger>() noexcept {
    return foundation::algebra::ClaimOrientation::weaker_is_higher;
}

static_assert(foundation::algebra::lattices::ProductLattice<Stronger, Stronger>::claim_orientation
              == foundation::algebra::ClaimOrientation::weaker_is_higher);

int main() { return 0; }
