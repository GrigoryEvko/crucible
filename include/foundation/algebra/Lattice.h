#pragma once

// A lattice is a stateless tag class.  The Lattice and Semiring
// concepts below are the whole contract.  Nothing else is asked of one.
//
// Every operation a lattice publishes is constexpr, never consteval.
// A grade reaches leq through a contract predicate that runs at
// runtime, and a consteval operation is a hard error there.  The
// concepts cannot enforce this, because a requires-expression is
// unevaluated and accepts a consteval member happily.  A lattice whose
// grade is fixed at the type level looks consteval-eligible and still
// must not be.  Only name() and the verify_* helpers are consteval.
//
// Lattice<L> is not a signature check.  It evaluates the axioms at L's
// own bottom() and top(), so a lattice whose join is not idempotent,
// commutative, associative or absorbing fails the concept and cannot
// serve as a grade.  The axioms include the two that tie the order to
// the operations: leq(a, b) holds exactly when join(a, b) is b, and
// exactly when meet(a, b) is a.  Every other law compares elements
// through leq alone, so without these two a leq that runs against join
// passes all of them.  bottom() must also sit below top().
//
// That gate reaches only the extremes.  A break in the interior of an
// infinite carrier is invisible to it, so a lattice still owns a
// self-test that runs the verify_* helpers over its own representative
// witnesses.  The helpers and the concept evaluate the same law
// functions, so the two cannot disagree about what a law says.
//
// Semiring<S> is gated the same way, at S's own zero() and one().

#include <foundation/algebra/Modality.h>

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace foundation::algebra {

template <typename L>
using LatticeElement = typename L::element_type;

// The direction in which the order of a lattice runs, as a claim.  A
// lattice states its orientation with a static member claim_orientation
// of this type.  ClaimOrientation.h derives the orientation of a lattice
// that states none, and says which orientations Graded accepts.  This
// header declares the type, and a lattice states its orientation with no
// other include.
enum class ClaimOrientation : std::uint8_t {
    // The lattice states no orientation and its element has more than
    // one value, or it states one_claim over such an element.  Graded
    // refuses such a lattice as a stored grade.
    unstated = 0,
    // Up is the weaker claim.  Graded uses this orientation.
    weaker_is_higher = 1,
    // Up is the stronger claim.  Graded refuses it as a stored grade, and
    // the order dual has the orientation that Graded uses.
    stronger_is_higher = 2,
    // The element type is empty.  Each element names the same claim, and
    // no move changes it.  claim_orientation_of derives this orientation
    // from the element type.
    one_claim = 3,
};

// The signature-only probe, and internal scaffolding.  Downstream code
// uses Lattice.  The law-witness machinery below is constrained by this
// probe rather than by Lattice, because a helper naming Lattice would
// make the concept self-referential.
template <typename L>
concept LatticeShape = requires { typename L::element_type; } && requires(LatticeElement<L> a, LatticeElement<L> b) {
    { L::leq(a, b) } -> std::convertible_to<bool>;
    { L::join(a, b) } -> std::same_as<LatticeElement<L>>;
    { L::meet(a, b) } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept HasBottom = LatticeShape<L> && requires {
    { L::bottom() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept HasTop = LatticeShape<L> && requires {
    { L::top() } -> std::same_as<LatticeElement<L>>;
};

template <typename S>
concept SemiringShape = requires { typename S::element_type; } && requires(LatticeElement<S> a, LatticeElement<S> b) {
    { S::add(a, b) } -> std::same_as<LatticeElement<S>>;
    { S::mul(a, b) } -> std::same_as<LatticeElement<S>>;
    { a == b } -> std::convertible_to<bool>;
} && requires {
    { S::zero() } -> std::same_as<LatticeElement<S>>;
    { S::one() } -> std::same_as<LatticeElement<S>>;
};

// One function per law, over the signature probes.  The concepts below
// evaluate them at the extremes, and each public verify_* helper is one
// of them behind the concept, so a law is stated in one place.
namespace detail::lattice_laws {

template <LatticeShape L>
[[nodiscard]] consteval bool equivalent_(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && L::leq(b, a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool idempotent_join(LatticeElement<L> a) noexcept {
    return equivalent_<L>(L::join(a, a), a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool idempotent_meet(LatticeElement<L> a) noexcept {
    return equivalent_<L>(L::meet(a, a), a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool commutative_join(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent_<L>(L::join(a, b), L::join(b, a));
}

template <LatticeShape L>
[[nodiscard]] consteval bool commutative_meet(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent_<L>(L::meet(a, b), L::meet(b, a));
}

template <LatticeShape L>
[[nodiscard]] consteval bool associative_join(LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return equivalent_<L>(L::join(L::join(a, b), c), L::join(a, L::join(b, c)));
}

template <LatticeShape L>
[[nodiscard]] consteval bool associative_meet(LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return equivalent_<L>(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c)));
}

template <LatticeShape L>
[[nodiscard]] consteval bool absorption(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent_<L>(L::join(a, L::meet(a, b)), a) && equivalent_<L>(L::meet(a, L::join(a, b)), a);
}

// Reflexivity is checked on all three witnesses, not one, to catch a
// leq that reads the wrong operand.
template <LatticeShape L>
[[nodiscard]] consteval bool partial_order(LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    const bool reflexive = L::leq(a, a) && L::leq(b, b) && L::leq(c, c);
    const bool antisymmetric = !(L::leq(a, b) && L::leq(b, a)) || equivalent_<L>(a, b);
    const bool transitive = !(L::leq(a, b) && L::leq(b, c)) || L::leq(a, c);
    return reflexive && antisymmetric && transitive;
}

// leq is the order that join and meet induce, read in both directions of
// the pair.
template <LatticeShape L>
[[nodiscard]] consteval bool order_agrees(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    const bool by_join =
        L::leq(a, b) == equivalent_<L>(L::join(a, b), b) && L::leq(b, a) == equivalent_<L>(L::join(b, a), a);
    const bool by_meet =
        L::leq(a, b) == equivalent_<L>(L::meet(a, b), a) && L::leq(b, a) == equivalent_<L>(L::meet(b, a), b);
    return by_join && by_meet;
}

template <LatticeShape L>
[[nodiscard]] consteval bool axioms_at(LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    return idempotent_join<L>(a) && idempotent_meet<L>(a) && commutative_join<L>(a, b) && commutative_meet<L>(a, b)
        && associative_join<L>(a, b, c) && associative_meet<L>(a, b, c) && absorption<L>(a, b)
        && partial_order<L>(a, b, c) && order_agrees<L>(a, b) && order_agrees<L>(b, c) && order_agrees<L>(a, c);
}

// bottom() is below the element and is the identity of join with it.
template <HasBottom L>
[[nodiscard]] consteval bool bottom_identity(LatticeElement<L> a) noexcept {
    return L::leq(L::bottom(), a) && equivalent_<L>(L::join(L::bottom(), a), a);
}

// top() is above the element and is the identity of meet with it.
template <HasTop L>
[[nodiscard]] consteval bool top_identity(LatticeElement<L> a) noexcept {
    return L::leq(a, L::top()) && equivalent_<L>(L::meet(L::top(), a), a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool laws_hold() noexcept {
    if constexpr (HasBottom<L> && HasTop<L>) {
        const LatticeElement<L> lo = L::bottom();
        const LatticeElement<L> hi = L::top();
        return axioms_at<L>(lo, lo, lo) && axioms_at<L>(hi, hi, hi) && axioms_at<L>(lo, hi, lo)
            && axioms_at<L>(hi, lo, hi) && axioms_at<L>(lo, lo, hi) && axioms_at<L>(hi, hi, lo)
            && axioms_at<L>(lo, hi, hi) && axioms_at<L>(hi, lo, lo) && bottom_identity<L>(hi) && top_identity<L>(lo);
    } else if constexpr (HasBottom<L>) {
        const LatticeElement<L> lo = L::bottom();
        return axioms_at<L>(lo, lo, lo) && bottom_identity<L>(lo);
    } else if constexpr (HasTop<L>) {
        const LatticeElement<L> hi = L::top();
        return axioms_at<L>(hi, hi, hi) && top_identity<L>(hi);
    } else if constexpr (std::is_default_constructible_v<LatticeElement<L>>) {
        const LatticeElement<L> e{};
        return axioms_at<L>(e, e, e);
    } else {
        // Neither extreme exists and the carrier cannot be
        // default-constructed, so no witness can be built and L is not
        // a Lattice.
        return false;
    }
}

template <SemiringShape S>
[[nodiscard]] consteval bool additive_identity(LatticeElement<S> a) noexcept {
    return S::add(S::zero(), a) == a && S::add(a, S::zero()) == a;
}

template <SemiringShape S>
[[nodiscard]] consteval bool multiplicative_identity(LatticeElement<S> a) noexcept {
    return S::mul(S::one(), a) == a && S::mul(a, S::one()) == a;
}

template <SemiringShape S>
[[nodiscard]] consteval bool multiplicative_zero(LatticeElement<S> a) noexcept {
    return S::mul(S::zero(), a) == S::zero() && S::mul(a, S::zero()) == S::zero();
}

template <SemiringShape S>
[[nodiscard]] consteval bool additive_commutative(LatticeElement<S> a, LatticeElement<S> b) noexcept {
    return S::add(a, b) == S::add(b, a);
}

template <SemiringShape S>
[[nodiscard]] consteval bool additive_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                  LatticeElement<S> c) noexcept {
    return S::add(S::add(a, b), c) == S::add(a, S::add(b, c));
}

template <SemiringShape S>
[[nodiscard]] consteval bool multiplicative_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                        LatticeElement<S> c) noexcept {
    return S::mul(S::mul(a, b), c) == S::mul(a, S::mul(b, c));
}

template <SemiringShape S>
[[nodiscard]] consteval bool distributivity(LatticeElement<S> a, LatticeElement<S> b, LatticeElement<S> c) noexcept {
    return S::mul(a, S::add(b, c)) == S::add(S::mul(a, b), S::mul(a, c))
        && S::mul(S::add(a, b), c) == S::add(S::mul(a, c), S::mul(b, c));
}

template <SemiringShape S>
[[nodiscard]] consteval bool semiring_axioms_at(LatticeElement<S> a, LatticeElement<S> b,
                                                LatticeElement<S> c) noexcept {
    return additive_identity<S>(a) && multiplicative_identity<S>(a) && multiplicative_zero<S>(a)
        && additive_commutative<S>(a, b) && additive_associative<S>(a, b, c) && multiplicative_associative<S>(a, b, c)
        && distributivity<S>(a, b, c);
}

template <SemiringShape S>
[[nodiscard]] consteval bool semiring_laws_hold() noexcept {
    const LatticeElement<S> zero = S::zero();
    const LatticeElement<S> one = S::one();
    return semiring_axioms_at<S>(zero, zero, zero) && semiring_axioms_at<S>(one, one, one)
        && semiring_axioms_at<S>(zero, one, zero) && semiring_axioms_at<S>(one, zero, one)
        && semiring_axioms_at<S>(zero, zero, one) && semiring_axioms_at<S>(one, one, zero)
        && semiring_axioms_at<S>(zero, one, one) && semiring_axioms_at<S>(one, zero, zero);
}

}  // namespace detail::lattice_laws

template <typename L>
concept Lattice = LatticeShape<L> && detail::lattice_laws::laws_hold<L>();

template <typename L>
concept BoundedBelowLattice = Lattice<L> && requires {
    { L::bottom() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept BoundedAboveLattice = Lattice<L> && requires {
    { L::top() } -> std::same_as<LatticeElement<L>>;
};

template <typename L>
concept BoundedLattice = BoundedBelowLattice<L> && BoundedAboveLattice<L>;

template <typename L>
concept UnboundedLattice = Lattice<L> && !BoundedBelowLattice<L> && !BoundedAboveLattice<L>;

// Equality on element_type is part of the contract because the
// distributivity law has no other way to be stated.  The laws are
// evaluated at zero() and one(), so an add that is not commutative or a
// zero that does not annihilate fails the concept.
template <typename S>
concept Semiring = SemiringShape<S> && detail::lattice_laws::semiring_laws_hold<S>();

// A row is the powerset order over a finite set of named atoms: bottom
// is the empty row, top is every atom, join is union and meet is
// intersection.  What distinguishes it from a plain bounded lattice is
// that an element can be built from one atom and asked whether it holds
// one.  Effects, syscall surfaces and hardware atoms are rows, and a
// context admits an operation exactly when the operation's row is a
// subset of the context's.  The first Row is foundation/effects/Row.h.
template <typename R>
concept Row = BoundedLattice<R> && requires { typename R::atom_type; }
           && requires(LatticeElement<R> element, typename R::atom_type atom) {
                  { R::single(atom) } -> std::same_as<LatticeElement<R>>;
                  { R::contains(element, atom) } -> std::convertible_to<bool>;
              };

template <typename L>
concept HasLatticeName = requires {
    { L::name() } -> std::convertible_to<std::string_view>;
};

template <typename L>
[[nodiscard]] consteval std::string_view lattice_name() noexcept {
    if constexpr (HasLatticeName<L>)
        return L::name();
    else
        return std::string_view{"<unnamed lattice>"};
}

template <Lattice L>
[[nodiscard]] constexpr bool subsumes(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b);
}

template <Lattice L>
[[nodiscard]] constexpr bool equivalent(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && L::leq(b, a);
}

template <Lattice L>
[[nodiscard]] constexpr bool strictly_less(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && !L::leq(b, a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_idempotent_join(LatticeElement<L> a) noexcept {
    return detail::lattice_laws::idempotent_join<L>(a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_idempotent_meet(LatticeElement<L> a) noexcept {
    return detail::lattice_laws::idempotent_meet<L>(a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_join(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return detail::lattice_laws::commutative_join<L>(a, b);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_meet(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return detail::lattice_laws::commutative_meet<L>(a, b);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_join(LatticeElement<L> a, LatticeElement<L> b,
                                                     LatticeElement<L> c) noexcept {
    return detail::lattice_laws::associative_join<L>(a, b, c);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_meet(LatticeElement<L> a, LatticeElement<L> b,
                                                     LatticeElement<L> c) noexcept {
    return detail::lattice_laws::associative_meet<L>(a, b, c);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_absorption(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return detail::lattice_laws::absorption<L>(a, b);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_partial_order(LatticeElement<L> a, LatticeElement<L> b,
                                                  LatticeElement<L> c) noexcept {
    return detail::lattice_laws::partial_order<L>(a, b, c);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_order_agrees(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return detail::lattice_laws::order_agrees<L>(a, b);
}

template <BoundedBelowLattice L>
[[nodiscard]] consteval bool verify_bottom_identity(LatticeElement<L> a) noexcept {
    return detail::lattice_laws::bottom_identity<L>(a);
}

template <BoundedAboveLattice L>
[[nodiscard]] consteval bool verify_top_identity(LatticeElement<L> a) noexcept {
    return detail::lattice_laws::top_identity<L>(a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_lattice_axioms_at(LatticeElement<L> a, LatticeElement<L> b,
                                                      LatticeElement<L> c) noexcept {
    return detail::lattice_laws::axioms_at<L>(a, b, c);
}

template <BoundedLattice L>
[[nodiscard]] consteval bool verify_bounded_lattice_axioms_at(LatticeElement<L> a, LatticeElement<L> b,
                                                              LatticeElement<L> c) noexcept {
    return verify_lattice_axioms_at<L>(a, b, c) && verify_bottom_identity<L>(a) && verify_bottom_identity<L>(b)
        && verify_bottom_identity<L>(c) && verify_top_identity<L>(a) && verify_top_identity<L>(b)
        && verify_top_identity<L>(c);
}

// Distributivity over meet and join.  Not part of either rollup,
// because a non-distributive lattice is still a lattice.  Use it in a
// lattice that claims the property.
//
// The two laws are equivalent in any lattice, so checking both is
// redundant as mathematics and useful as a guard against an author who
// implemented only one of them.
//
// Do not confuse this with verify_distributivity, which is the
// multiplicative law for a Semiring over add and mul.
template <Lattice L>
[[nodiscard]] consteval bool verify_distributive_lattice(LatticeElement<L> a, LatticeElement<L> b,
                                                         LatticeElement<L> c) noexcept {
    return equivalent<L>(L::meet(a, L::join(b, c)), L::join(L::meet(a, b), L::meet(a, c)))
        && equivalent<L>(L::join(a, L::meet(b, c)), L::meet(L::join(a, b), L::join(a, c)));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_identity(LatticeElement<S> a) noexcept {
    return detail::lattice_laws::additive_identity<S>(a);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_identity(LatticeElement<S> a) noexcept {
    return detail::lattice_laws::multiplicative_identity<S>(a);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_zero(LatticeElement<S> a) noexcept {
    return detail::lattice_laws::multiplicative_zero<S>(a);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_commutative(LatticeElement<S> a, LatticeElement<S> b) noexcept {
    return detail::lattice_laws::additive_commutative<S>(a, b);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                         LatticeElement<S> c) noexcept {
    return detail::lattice_laws::additive_associative<S>(a, b, c);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                               LatticeElement<S> c) noexcept {
    return detail::lattice_laws::multiplicative_associative<S>(a, b, c);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_distributivity(LatticeElement<S> a, LatticeElement<S> b,
                                                   LatticeElement<S> c) noexcept {
    return detail::lattice_laws::distributivity<S>(a, b, c);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_semiring_axioms_at(LatticeElement<S> a, LatticeElement<S> b,
                                                       LatticeElement<S> c) noexcept {
    return detail::lattice_laws::semiring_axioms_at<S>(a, b, c);
}

namespace detail {

// The fixture lattices of the algebra checks.  The checks of this header,
// of Graded.h and of GradedTrait.h, test/foundation/test_algebra_core.cpp
// and the negative fixtures name them, so they live here and not in one
// check file.

// As a claim, false is the stronger element and true is the weaker.  The
// Graded self-tests can then store it as a grade.
struct TrivialBoolLattice {
    using element_type = bool;
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialBool"; }
};

struct TrivialBoolSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type zero() noexcept { return false; }
    [[nodiscard]] static constexpr element_type one() noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr element_type mul(bool a, bool b) noexcept { return a && b; }
};

// The smallest row: two atoms, carried as two bits.
enum class TrivialAtom : unsigned char {
    Read = 0,
    Write = 1
};

struct TrivialRow {
    using element_type = unsigned char;
    using atom_type = TrivialAtom;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return (a & ~b) == 0; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a | b);
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a & b);
    }
    [[nodiscard]] static constexpr element_type single(TrivialAtom atom) noexcept {
        return static_cast<element_type>(1u << static_cast<unsigned>(atom));
    }
    [[nodiscard]] static constexpr bool contains(element_type element, TrivialAtom atom) noexcept {
        return (element & single(atom)) != 0;
    }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialRow"; }
};

// The refused shapes.  Each one satisfies every law that compares
// elements through leq alone.

// leq runs against join and meet.
struct InvertedOrder {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a >= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
};

// The order is right, and bottom() and top() are exchanged.
struct ExchangedBounds {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 3; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 0; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
};

// Subtraction for add passes a signature check and no law.
struct SubtractionSemiring {
    using element_type = int;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type one() noexcept { return 1; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return a - b; }
    [[nodiscard]] static constexpr element_type mul(element_type a, element_type b) noexcept { return a + b; }
};

}  // namespace detail

}  // namespace foundation::algebra
