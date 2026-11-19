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
#include <meta>
#include <type_traits>

namespace foundation::algebra {

// The answers below about a lattice are concepts, alias templates and
// functions that are not templates, so no translation unit can specialize
// an answer.  A lattice states its orientation and its subject in its own
// members.  A concept reads a member as a name lookup finds it, so a member
// of a base class counts.  GCC keeps the answer of each concept for each
// lattice, and no answer calls a function over a reflection.
namespace detail {

template <typename L>
concept StatesClaimOrientation = requires { L::claim_orientation; };

template <typename L>
concept StatesClaimOrientationOfItsType =
    std::is_same_v<std::remove_cvref_t<decltype(L::claim_orientation)>, ClaimOrientation>;

// Each function below has no constant definition.  A call to one stops the
// constant evaluation, and the diagnostic names it.  The name is the fix.
void claim_orientation_must_be_a_ClaimOrientation_so_state_weaker_or_stronger_or_remove_the_member() noexcept;
void claim_subject_must_be_a_ClaimSubject_so_state_bytes_or_slot_or_remove_the_member() noexcept;

// Stops the build for a lattice that declares claim_orientation with
// another type, because a misspelt declaration must not fall back to a
// derived orientation.  The lattice is a parameter, so that each call
// depends on the lattice of its concept.
consteval bool refuse_mistyped_orientation(std::meta::info lattice) {
    if (lattice != std::meta::info{}) {
        claim_orientation_must_be_a_ClaimOrientation_so_state_weaker_or_stronger_or_remove_the_member();
    }
    return false;
}

template <typename L>
concept OrientationIsWellTyped =
    !StatesClaimOrientation<L> || StatesClaimOrientationOfItsType<L> || refuse_mistyped_orientation(^^L);

template <typename L, ClaimOrientation Stated>
concept StatesOrientation = StatesClaimOrientation<L> && OrientationIsWellTyped<L> && (L::claim_orientation == Stated);

template <typename L>
concept HasEmptyElement = std::is_empty_v<LatticeElement<L>>;

template <typename L>
concept OrientedWeakerIsHigher = StatesOrientation<L, ClaimOrientation::weaker_is_higher>;

template <typename L>
concept OrientedStrongerIsHigher = StatesOrientation<L, ClaimOrientation::stronger_is_higher>;

// One claim is a property of the element type.  A lattice that states it
// over an element with more than one value is unstated.
template <typename L>
concept OrientedOneClaim = OrientationIsWellTyped<L> && HasEmptyElement<L>
                        && (!StatesClaimOrientation<L> || StatesOrientation<L, ClaimOrientation::one_claim>);

template <typename L>
concept OrientedUnstated = !OrientedWeakerIsHigher<L> && !OrientedStrongerIsHigher<L> && !OrientedOneClaim<L>;

// Gives its argument.  A gate passes its answer through this call, so the
// gate is one atomic constraint.  The overloads of Graded are ordered by
// their constraints, and an ordering expands each concept of a constraint
// that is not atomic.
[[nodiscard]] consteval bool as_one_constraint(bool answer) noexcept { return answer; }

}  // namespace detail

// The orientation that the lattice states, as the value of the type.  If
// the lattice states none, the orientation is one_claim for an empty
// element type, and unstated for any other.  A member with the correct
// name and another type stops the build.
template <typename L>
using claim_orientation_t =
    std::integral_constant<ClaimOrientation, detail::OrientedWeakerIsHigher<L> ? ClaimOrientation::weaker_is_higher
                                             : detail::OrientedStrongerIsHigher<L>
                                                 ? ClaimOrientation::stronger_is_higher
                                             : detail::OrientedOneClaim<L> ? ClaimOrientation::one_claim
                                                                           : ClaimOrientation::unstated>;

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
// as it is.  The product of no component is one claim.
template <typename... Ls>
using product_orientation_t =
    std::integral_constant<ClaimOrientation,
                           (... || detail::OrientedStrongerIsHigher<Ls>) ? ClaimOrientation::stronger_is_higher
                           : (... || detail::OrientedUnstated<Ls>)       ? ClaimOrientation::unstated
                           : (... || detail::OrientedWeakerIsHigher<Ls>) ? ClaimOrientation::weaker_is_higher
                                                                         : ClaimOrientation::one_claim>;

// A lattice that Graded can store beside a value: up is the weaker claim,
// or the lattice has one element and no up at all.
template <typename L>
concept GradableLattice =
    Lattice<L> && detail::as_one_constraint(detail::OrientedWeakerIsHigher<L> || detail::OrientedOneClaim<L>);

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

namespace detail {

template <typename L>
concept StatesClaimSubject = requires { L::claim_subject; };

template <typename L>
concept StatesClaimSubjectOfItsType = std::is_same_v<std::remove_cvref_t<decltype(L::claim_subject)>, ClaimSubject>;

// Stops the build for a lattice that declares claim_subject with another
// type, so a misspelt declaration cannot fall back to the default.
consteval bool refuse_mistyped_subject(std::meta::info lattice) {
    if (lattice != std::meta::info{})
        claim_subject_must_be_a_ClaimSubject_so_state_bytes_or_slot_or_remove_the_member();
    return false;
}

template <typename L>
concept SubjectIsWellTyped = !StatesClaimSubject<L> || StatesClaimSubjectOfItsType<L> || refuse_mistyped_subject(^^L);

template <typename L>
concept StatesSlot = StatesClaimSubject<L> && SubjectIsWellTyped<L> && (L::claim_subject == ClaimSubject::slot);

}  // namespace detail

// The subject that the lattice states, or bytes when it states none, as the
// value of the type.  A member with the right name and another type stops
// the build.
template <typename L>
using claim_subject_t =
    std::integral_constant<ClaimSubject, detail::StatesSlot<L> ? ClaimSubject::slot : ClaimSubject::bytes>;

// A grade that any bytes satisfy.  This is the one condition under which
// Graded writes or default-builds a value without a key.
template <typename L>
concept GradeIgnoresBytes = detail::as_one_constraint(detail::StatesSlot<L>);

}  // namespace foundation::algebra
