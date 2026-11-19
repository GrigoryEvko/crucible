// Graded stores a grade only over a lattice whose up is the weaker claim.
// Over a lattice whose up is the stronger claim, weaken() can strengthen a
// claim with no proof.  This file tries to pass such a lattice through the
// gate: it writes an explicit specialization of the concept that the gate
// reads.  A concept cannot have a specialization, so the declaration does
// not compile, and the gate refuses the lattice.

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
constexpr bool foundation::algebra::detail::OrientedWeakerIsHigher<Stronger> = true;

int main() { return 0; }
