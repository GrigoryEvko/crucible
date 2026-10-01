#pragma once

// The order dual of a lattice: the same elements, with the order turned
// over.  leq(a, b) holds in the dual exactly when leq(b, a) holds in the
// source.  The join of the dual is the meet of the source, and the two
// extremes exchange places.
//
// Graded reads its up direction as the weaker claim: weaken() and
// compose() move a grade up and nowhere else.  A lattice whose natural
// order puts the stronger claim higher gives Graded the wrong direction.
// A version counter is such a lattice.  The newer version is the
// stronger claim, and a Graded over the numeric order can let weaken()
// mark an old value as new.  compose() can pair one value with the newer
// of two versions.  The dual of that order puts the older version higher,
// and the two operations become sound: weaken() moves to an older version,
// and compose() gives the older of two.  A vector clock, a fractional
// share and the numerical tiers of a recipe are graded the same way.
//
// The dual keeps the element type.  A value in the dual is the value in
// the source, so a caller reads it with the source's own accessors.
//
// The dual of the dual is the source order, but it is not the source
// type, and the row-hash identity keeps them apart.  Nothing here folds
// a double dual back, because no caller needs it.
//
// The dual turns the claim orientation of its source over.  Graded
// accepts the dual of a lattice whose up is the stronger claim, and it
// refuses the dual of a lattice whose up is the weaker claim.  The dual
// of a lattice that states no orientation states none, and Graded
// refuses it as a stored grade (ClaimOrientation.h).

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>

#include <string_view>

namespace foundation::algebra::lattices {

template <Lattice L>
struct DualLattice {
    using element_type = typename L::element_type;
    using source_lattice = L;

    static constexpr ClaimOrientation claim_orientation = turned_over(claim_orientation_v<L>);

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return L::leq(b, a); }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return L::meet(a, b); }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return L::join(a, b); }

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

}  // namespace foundation::algebra::lattices
