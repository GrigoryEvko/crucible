// A product lattice states the orientation of its components together.  A
// product with one component whose up is the stronger claim lets the product
// strengthen a claim.  This file tries to give such a product the weaker
// orientation: it writes an explicit specialization of the concept that the
// orientation of the product reads for each component, so that the stronger
// component does not count.  A concept cannot have a specialization, so the
// declaration does not compile, and the product keeps the stronger
// orientation.

#include <foundation/algebra/ClaimOrientation.h>

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
constexpr bool foundation::algebra::detail::OrientedStrongerIsHigher<Stronger> = false;

int main() { return 0; }
