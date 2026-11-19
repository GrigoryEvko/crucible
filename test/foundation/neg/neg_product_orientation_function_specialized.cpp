// A product with one component whose up is the stronger claim lets the
// product strengthen a claim.  The orientation of a product was once the
// function template product_orientation.  This file tries to give such a
// product the weaker orientation: it specializes that function template.
// The orientation comes from an alias template over concepts, and no
// function template gives it, so the specialization does not compile.

#include <foundation/Quarantine.h>
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

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
consteval foundation::algebra::ClaimOrientation
foundation::algebra::product_orientation<Stronger, Stronger>() noexcept {
    return foundation::algebra::ClaimOrientation::weaker_is_higher;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
