#pragma once

// The flat lattice over the values of a scoped enum: one bottom, one top,
// and each other value a sibling of each other value.
//
// leq(a, b) holds when a is b, when a is the bottom, or when b is the top.
// Two different values between the bounds are siblings.  Neither covers the
// other, their join is the top and their meet is the bottom.  With three
// siblings or more the lattice holds M3, which is not distributive, so the
// template claims a bounded lattice and no more.
//
// The order reads a value as a claim: a value that covers more values is the
// stronger claim, and the top covers each value.  An instance names its
// enum and its two bounds, and it derives from the template to give its own
// name:
//
//   NumaNodeLattice      NumaNodeId::None     NumaNodeId::Any
//   VendorLattice        VendorBackend::None  VendorBackend::Portable
//   RecipeFamilyLattice  RecipeFamily::None   RecipeFamily::Any
//
// The values are storage, not order: the template compares only for
// equality, so two siblings stay unordered however their numbers compare.

#include <foundation/algebra/Lattice.h>

#include <type_traits>

namespace foundation::algebra::lattices {

template <class Element>
concept FlatLatticeElement = std::is_scoped_enum_v<Element>;

template <FlatLatticeElement Element, Element Bottom, Element Top>
    requires(Bottom != Top)
struct FlatLattice {
    using element_type = Element;

    // A value that covers more values is the stronger claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return Bottom; }
    [[nodiscard]] static constexpr element_type top() noexcept { return Top; }

    [[nodiscard]] static constexpr bool leq(element_type lower, element_type upper) noexcept {
        return lower == upper || lower == Bottom || upper == Top;
    }

    // Two siblings have nothing between them and the bounds, so their least
    // upper bound is the top and their greatest lower bound is the bottom.
    // The arguments swap with no change of the result.
    [[nodiscard]] static constexpr element_type join(element_type left, element_type right) noexcept {
        if (leq(left, right)) return right;
        if (leq(right, left)) return left;
        return Top;
    }

    [[nodiscard]] static constexpr element_type meet(element_type left, element_type right) noexcept {
        if (leq(left, right)) return left;
        if (leq(right, left)) return right;
        return Bottom;
    }
};

}  // namespace foundation::algebra::lattices
