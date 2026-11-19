// Graded stores a grade only over a lattice whose up is the weaker claim.
// The orientation was once the variable template claim_orientation_v, and
// the gate read it.  This file tries to pass a lattice whose up is the
// stronger claim through the gate: it specializes that variable.  The
// orientation comes from concepts and alias templates, and no variable
// gives it, so the specialization does not compile.

#include <foundation/Quarantine.h>
#include <foundation/algebra/ClaimOrientation.h>

namespace forged_claim {
struct Stronger {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[maybe_unused]] static constexpr foundation::algebra::ClaimOrientation claim_orientation =
        foundation::algebra::ClaimOrientation::stronger_is_higher;
};
}  // namespace forged_claim

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr foundation::algebra::ClaimOrientation
    foundation::algebra::claim_orientation_v<forged_claim::Stronger> =
        foundation::algebra::ClaimOrientation::weaker_is_higher;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
