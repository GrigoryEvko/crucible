// Graded stores a grade only over a lattice whose up is the weaker claim.
// Over a lattice whose up is the stronger claim, weaken() would strengthen
// a claim with no proof.  This file tries to pass such a lattice through
// the gate: it specializes the trait spelling claim_orientation_v.  The
// orientation is a function at namespace scope that is not a template, so
// the specialization does not compile, and the gate refuses the lattice.

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
inline constexpr foundation::algebra::ClaimOrientation foundation::algebra::claim_orientation_v<Stronger> =
    foundation::algebra::ClaimOrientation::weaker_is_higher;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(foundation::algebra::GradableLattice<Stronger>);

int main() { return 0; }
