#pragma once

// How the order of a lattice relates to a claim: which way the order
// runs, and whether the claim is about the bytes of a value or about the
// slot that holds them.  The end of this file answers the second
// question.
//
// In Graded, up is the weaker claim.  weaken() and compose() move a
// stored grade up and nowhere else, and a grade that moves up must
// promise less.  This is the approximation order of a graded modal type
// (Orchard, Liepelt and Eades, ICFP 2019): a value graded r can be used
// where grade s is necessary when r approximates s.  The order of Graded
// must be that approximation.
//
// A lattice whose order puts the stronger claim higher breaks the rule,
// because both operations then strengthen a claim with no proof.  A
// version counter is that case.  A newer epoch is the stronger claim, and
// a Graded over the numeric epoch order lets weaken() mark a stale value
// as fresh.  The order dual of that lattice has the correct orientation,
// and Graded accepts it.
//
// A lattice states its orientation with the static member
// claim_orientation (the type is in Lattice.h).  The orientation of a
// lattice that states none comes from its element type.  An empty element
// type has one value, and each move leaves the claim where it was: the
// orientation is one_claim.  Any other element type makes the orientation
// unstated.  Graded refuses an unstated lattice as a stored grade, because
// an unknown orientation is refused and never admitted.
//
// A generic lattice, such as a chain over any enumeration, has as many
// orientations as its uses.  It states none, and the wrapper that grades
// by it gives a lattice that states one.  The dual of a lattice turns its
// orientation over, and a product has the orientation that its components
// share.  This file derives each of the two from the components, and no
// lattice states them again by hand.

#include <foundation/algebra/Lattice.h>

#include <cstdint>
#include <type_traits>

namespace foundation::algebra {

// The orientation that L states.  If L states none, the orientation is
// one_claim for an empty element type, and unstated for any other.  A
// member with the correct name and another type stops the build, because
// a misspelt declaration must not fall back to a derived orientation.
// One claim is a property of the element type.  A lattice that states it
// over an element with more than one value is unstated.
template <typename L>
[[nodiscard]] consteval ClaimOrientation claim_orientation_of() noexcept {
    if constexpr (requires { L::claim_orientation; }) {
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(L::claim_orientation)>, ClaimOrientation>,
                      "claim_orientation must be a foundation::algebra::ClaimOrientation.  State one of "
                      "weaker_is_higher or stronger_is_higher, or remove the member.");
        if (L::claim_orientation == ClaimOrientation::one_claim && !std::is_empty_v<LatticeElement<L>>) {
            return ClaimOrientation::unstated;
        }
        return L::claim_orientation;
    } else if constexpr (std::is_empty_v<LatticeElement<L>>) {
        return ClaimOrientation::one_claim;
    } else {
        return ClaimOrientation::unstated;
    }
}

template <typename L>
inline constexpr ClaimOrientation claim_orientation_v = claim_orientation_of<L>();

// The orientation of the order dual: the stronger and the weaker ends
// exchange places.  One claim stays one claim, and an unstated
// orientation stays unstated.
[[nodiscard]] consteval ClaimOrientation turned_over(ClaimOrientation source) noexcept {
    if (source == ClaimOrientation::weaker_is_higher) return ClaimOrientation::stronger_is_higher;
    if (source == ClaimOrientation::stronger_is_higher) return ClaimOrientation::weaker_is_higher;
    return source;
}

// The orientation of a product.  The product moves each component at
// the same time.  One component whose up is the stronger claim lets the
// product strengthen a claim, and one component whose orientation is
// unknown makes the orientation of the product unknown.  A one-claim
// component does not move, and it leaves the orientation of the others
// as it is.
template <typename... Ls>
[[nodiscard]] consteval ClaimOrientation product_orientation() noexcept {
    if ((... || (claim_orientation_v<Ls> == ClaimOrientation::stronger_is_higher))) {
        return ClaimOrientation::stronger_is_higher;
    }
    if ((... || (claim_orientation_v<Ls> == ClaimOrientation::unstated))) {
        return ClaimOrientation::unstated;
    }
    if ((... || (claim_orientation_v<Ls> == ClaimOrientation::weaker_is_higher))) {
        return ClaimOrientation::weaker_is_higher;
    }
    return ClaimOrientation::one_claim;
}

// A lattice that Graded can store beside a value: up is the weaker claim,
// or the lattice has one element and no up at all.
template <typename L>
concept GradableLattice = Lattice<L>
                       && (claim_orientation_v<L> == ClaimOrientation::weaker_is_higher
                           || claim_orientation_v<L> == ClaimOrientation::one_claim);

// What a grade is a claim about.
//
// Most grades say something about the bytes a carrier holds: that they
// satisfy a predicate, that a deterministic source produced them, that a
// named backend built them.  Replacing the bytes can make such a grade
// false, so Graded hands out a mutable reference, and builds a value at a
// grade it cannot derive, only to a holder of a key (Graded.h).
//
// A few grades say something about the slot and nothing about what it
// holds: how many times the slot may be used, for instance.  Any bytes
// may sit under such a grade, so Graded opens those doors without a key.
// A lattice states this with a static member claim_subject.  A lattice
// that states nothing is read as a claim about the bytes, because an
// unknown claim is refused rather than admitted.
enum class ClaimSubject : std::uint8_t {
    // The grade constrains the bytes.  This is the reading of a lattice
    // that states nothing.
    bytes = 0,
    // The grade constrains the slot, and any bytes satisfy it.
    slot = 1,
};

// The subject that L states, or bytes when L states none.  A member with
// the right name and another type stops the build, so a misspelt
// declaration cannot fall back to the default.
template <typename L>
[[nodiscard]] consteval ClaimSubject claim_subject_of() noexcept {
    if constexpr (requires { L::claim_subject; }) {
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(L::claim_subject)>, ClaimSubject>,
                      "claim_subject must be a foundation::algebra::ClaimSubject.  State bytes or slot, or "
                      "remove the member.");
        return L::claim_subject;
    } else {
        return ClaimSubject::bytes;
    }
}

template <typename L>
inline constexpr ClaimSubject claim_subject_v = claim_subject_of<L>();

// A grade that any bytes satisfy.  This is the one condition under which
// Graded writes or default-builds a value without a key.
template <typename L>
concept GradeIgnoresBytes = claim_subject_v<L> == ClaimSubject::slot;

}  // namespace foundation::algebra
