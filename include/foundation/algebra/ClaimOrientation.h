#pragma once

// How a lattice reads as a claim: which way its order runs, and whether
// the claim is about the bytes of a value or about the slot that holds
// them.  The second question is answered near the end of this file.
//
// Graded reads its up direction as the weaker claim.  weaken() and
// compose() move a grade up and nowhere else, so a grade that moves up
// must promise less.  This is the approximation order of a graded modal
// type (Orchard, Liepelt and Eades, ICFP 2019): a value graded r may be
// used where grade s is asked for when r approximates s, and the order
// that Graded reads must be that approximation.
//
// A lattice whose order puts the stronger claim higher breaks the rule.
// Both operations then strengthen a claim with no proof.  A version
// counter is that case: a newer epoch is the stronger claim, so a Graded
// over the numeric epoch order lets weaken() mark a stale value as fresh.
// The order dual of that lattice reads the right way, and Graded accepts
// it.
//
// A lattice states its orientation with a static member
// claim_orientation.  Only a lattice whose axis has one reading states it.
// A generic lattice, such as a chain over an enumeration, has as many
// readings as its uses, so it states nothing.  Graded accepts an unstated
// lattice as before.  The dual of a lattice turns its orientation over,
// and a product has the orientation that its components share.  Each of
// the two is derived here from its components, never restated by hand.

#include <foundation/algebra/Lattice.h>

#include <cstdint>
#include <type_traits>

namespace foundation::algebra {

enum class ClaimOrientation : std::uint8_t {
    // The lattice does not say.  Its axis has more than one reading, and
    // the wrapper that grades by it chooses one.
    unstated = 0,
    // Up is the weaker claim.  This is the reading of Graded.
    weaker_is_higher = 1,
    // Up is the stronger claim.  Graded refuses it.  The dual reads the
    // way Graded does.
    stronger_is_higher = 2,
};

// The orientation that L states, or unstated when L states none.  A
// member with the right name and another type stops the build, so a
// misspelt declaration cannot fall back to unstated.
template <typename L>
[[nodiscard]] consteval ClaimOrientation claim_orientation_of() noexcept {
    if constexpr (requires { L::claim_orientation; }) {
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(L::claim_orientation)>, ClaimOrientation>,
                      "claim_orientation must be a foundation::algebra::ClaimOrientation.  State one of "
                      "weaker_is_higher or stronger_is_higher, or remove the member.");
        return L::claim_orientation;
    } else {
        return ClaimOrientation::unstated;
    }
}

template <typename L>
inline constexpr ClaimOrientation claim_orientation_v = claim_orientation_of<L>();

// The orientation of the order dual: the stronger and the weaker ends
// exchange places, and an unstated reading stays unstated.
[[nodiscard]] consteval ClaimOrientation turned_over(ClaimOrientation source) noexcept {
    if (source == ClaimOrientation::weaker_is_higher) return ClaimOrientation::stronger_is_higher;
    if (source == ClaimOrientation::stronger_is_higher) return ClaimOrientation::weaker_is_higher;
    return ClaimOrientation::unstated;
}

// The orientation of a product.  The product moves every component at
// once, so one component whose up is the stronger claim lets the product
// strengthen a claim.  The product reads the Graded way only when each
// component states that it does.
template <typename... Ls>
[[nodiscard]] consteval ClaimOrientation product_orientation() noexcept {
    if ((... || (claim_orientation_v<Ls> == ClaimOrientation::stronger_is_higher))) {
        return ClaimOrientation::stronger_is_higher;
    }
    if (sizeof...(Ls) > 0 && (... && (claim_orientation_v<Ls> == ClaimOrientation::weaker_is_higher))) {
        return ClaimOrientation::weaker_is_higher;
    }
    return ClaimOrientation::unstated;
}

// A lattice that Graded may read: its up direction is not stated to be
// the stronger claim.
template <typename L>
concept GradableLattice = Lattice<L> && (claim_orientation_v<L> != ClaimOrientation::stronger_is_higher);

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

namespace detail::claim_orientation_self_test {

struct Silent {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};
struct Weaker : Silent {
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
};
struct Stronger : Silent {
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;
};

static_assert(claim_orientation_v<Silent> == ClaimOrientation::unstated);
static_assert(claim_orientation_v<Weaker> == ClaimOrientation::weaker_is_higher);
static_assert(claim_orientation_v<Stronger> == ClaimOrientation::stronger_is_higher);

static_assert(turned_over(ClaimOrientation::weaker_is_higher) == ClaimOrientation::stronger_is_higher);
static_assert(turned_over(ClaimOrientation::stronger_is_higher) == ClaimOrientation::weaker_is_higher);
static_assert(turned_over(ClaimOrientation::unstated) == ClaimOrientation::unstated);
static_assert(turned_over(turned_over(ClaimOrientation::stronger_is_higher)) == ClaimOrientation::stronger_is_higher);

static_assert(product_orientation<Weaker, Weaker>() == ClaimOrientation::weaker_is_higher);
static_assert(product_orientation<Weaker, Stronger>() == ClaimOrientation::stronger_is_higher);
static_assert(product_orientation<Stronger, Silent>() == ClaimOrientation::stronger_is_higher);
static_assert(product_orientation<Weaker, Silent>() == ClaimOrientation::unstated);
static_assert(product_orientation<>() == ClaimOrientation::unstated);

static_assert(GradableLattice<Silent> && GradableLattice<Weaker> && !GradableLattice<Stronger>);
static_assert(Lattice<Stronger>, "the refusal is about orientation, not about the lattice laws");

struct AboutTheSlot : Silent {
    static constexpr ClaimSubject claim_subject = ClaimSubject::slot;
};
struct AboutTheBytes : Silent {
    static constexpr ClaimSubject claim_subject = ClaimSubject::bytes;
};

static_assert(claim_subject_v<Silent> == ClaimSubject::bytes, "a lattice that states nothing claims the bytes");
static_assert(claim_subject_v<AboutTheSlot> == ClaimSubject::slot);
static_assert(claim_subject_v<AboutTheBytes> == ClaimSubject::bytes);
static_assert(GradeIgnoresBytes<AboutTheSlot> && !GradeIgnoresBytes<AboutTheBytes> && !GradeIgnoresBytes<Silent>);

}  // namespace detail::claim_orientation_self_test

}  // namespace foundation::algebra
