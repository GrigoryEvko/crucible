#pragma once

// The order dual of a lattice: the same elements, with the order turned
// over.  leq(a, b) holds in the dual exactly when leq(b, a) holds in the
// source.  The join of the dual is the meet of the source, and the two
// extremes exchange places.
//
// Graded reads its up direction as the weaker claim: weaken() and
// compose() move a grade up and nowhere else.  A lattice whose natural
// order puts the stronger claim higher gives Graded the wrong direction.
// A version counter is the case in this tree: the newer version is the
// stronger claim, so a Graded over the numeric order would let weaken()
// mark an old value as new, and compose() would pair one value with the
// newer of two versions.  The dual of that order puts the older version
// higher, so both operations become sound: weaken() moves to an older
// version, and compose() reports the older of two.
//
// The dual keeps the element type.  A value in the dual is the value in
// the source, so a caller reads it with the source's own accessors.
//
// The dual of the dual is the source order, but it is not the source
// type, and the row-hash identity keeps them apart.  Nothing here folds
// a double dual back, because no caller needs it.

#include <foundation/algebra/Lattice.h>

#include <string_view>

namespace foundation::algebra::lattices {

template <Lattice L>
struct DualLattice {
    using element_type = typename L::element_type;
    using source_lattice = L;

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return L::leq(b, a); }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return L::meet(a, b);
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return L::join(a, b);
    }

    // Each extreme exists in the dual exactly when the opposite extreme
    // exists in the source.
    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires BoundedAboveLattice<L>
    {
        return L::top();
    }
    [[nodiscard]] static constexpr element_type top() noexcept
        requires BoundedBelowLattice<L>
    {
        return L::bottom();
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "DualLattice"; }
};

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
