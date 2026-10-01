// The compile-time checks of foundation/algebra/lattices/DualLattice.h.

#include <foundation/algebra/lattices/DualLattice.h>

namespace foundation::algebra::lattices {

namespace detail::dual_lattice_self_test {

// A three-point chain small enough to check by hand: 0 < 1 < 2.
struct ThreeChain {
    using element_type = unsigned;
    [[nodiscard]] static constexpr bool leq(unsigned a, unsigned b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr unsigned join(unsigned a, unsigned b) noexcept { return a >= b ? a : b; }
    [[nodiscard]] static constexpr unsigned meet(unsigned a, unsigned b) noexcept { return a <= b ? a : b; }
    [[nodiscard]] static constexpr unsigned bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr unsigned top() noexcept { return 2; }
};

// The same chain with no top, so the dual must have no bottom.
struct OpenChain {
    using element_type = unsigned;
    [[nodiscard]] static constexpr bool leq(unsigned a, unsigned b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr unsigned join(unsigned a, unsigned b) noexcept { return a >= b ? a : b; }
    [[nodiscard]] static constexpr unsigned meet(unsigned a, unsigned b) noexcept { return a <= b ? a : b; }
    [[nodiscard]] static constexpr unsigned bottom() noexcept { return 0; }
};

using D = DualLattice<ThreeChain>;

static_assert(BoundedLattice<D>);
static_assert(D::bottom() == 2 && D::top() == 0);
static_assert(D::leq(2, 1) && D::leq(1, 0) && !D::leq(0, 1));
static_assert(D::join(0, 2) == 0 && D::meet(0, 2) == 2);
static_assert(verify_bounded_lattice_axioms_at<D>(0, 1, 2));
static_assert(verify_bounded_lattice_axioms_at<D>(2, 0, 1));
static_assert(verify_distributive_lattice<D>(0, 1, 2));

// Each extreme of the dual comes from the opposite extreme of the source.
static_assert(BoundedAboveLattice<DualLattice<OpenChain>>);
static_assert(!BoundedBelowLattice<DualLattice<OpenChain>>);

// The dual of the dual has the source order, although it is a new type.
using DD = DualLattice<D>;
static_assert(DD::leq(0, 1) && !DD::leq(1, 0) && DD::bottom() == 0 && DD::top() == 2);

}  // namespace detail::dual_lattice_self_test

}  // namespace foundation::algebra::lattices
