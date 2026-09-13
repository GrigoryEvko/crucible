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
// serve as a grade.  That gate reaches only the extremes.  A break in
// the interior of an infinite carrier is invisible to it, so a lattice
// still owns a self-test that runs the verify_* helpers over its own
// representative witnesses.

#include <crucible/algebra/Modality.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace crucible::algebra {

template <typename L>
using LatticeElement = typename L::element_type;

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

namespace detail::lattice_laws {

template <LatticeShape L>
[[nodiscard]] consteval bool raw_equivalent(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return L::leq(a, b) && L::leq(b, a);
}

template <LatticeShape L>
[[nodiscard]] consteval bool raw_axioms_at(LatticeElement<L> a, LatticeElement<L> b, LatticeElement<L> c) noexcept {
    const bool idem_join = raw_equivalent<L>(L::join(a, a), a);
    const bool idem_meet = raw_equivalent<L>(L::meet(a, a), a);
    const bool comm_join = raw_equivalent<L>(L::join(a, b), L::join(b, a));
    const bool comm_meet = raw_equivalent<L>(L::meet(a, b), L::meet(b, a));
    const bool assoc_join = raw_equivalent<L>(L::join(L::join(a, b), c), L::join(a, L::join(b, c)));
    const bool assoc_meet = raw_equivalent<L>(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c)));
    const bool absorb =
        raw_equivalent<L>(L::join(a, L::meet(a, b)), a) && raw_equivalent<L>(L::meet(a, L::join(a, b)), a);
    const bool reflexive = L::leq(a, a) && L::leq(b, b) && L::leq(c, c);
    const bool antisymmetric = !(L::leq(a, b) && L::leq(b, a)) || raw_equivalent<L>(a, b);
    const bool transitive = !(L::leq(a, b) && L::leq(b, c)) || L::leq(a, c);
    return idem_join && idem_meet && comm_join && comm_meet && assoc_join && assoc_meet && absorb && reflexive
        && antisymmetric && transitive;
}

template <LatticeShape L>
[[nodiscard]] consteval bool laws_hold() noexcept {
    if constexpr (HasBottom<L> && HasTop<L>) {
        const LatticeElement<L> lo = L::bottom();
        const LatticeElement<L> hi = L::top();
        return raw_axioms_at<L>(lo, lo, lo) && raw_axioms_at<L>(hi, hi, hi) && raw_axioms_at<L>(lo, hi, lo)
            && raw_axioms_at<L>(hi, lo, hi) && raw_axioms_at<L>(lo, lo, hi) && raw_axioms_at<L>(hi, hi, lo)
            && raw_axioms_at<L>(lo, hi, hi) && raw_axioms_at<L>(hi, lo, lo);
    } else if constexpr (HasBottom<L>) {
        const LatticeElement<L> lo = L::bottom();
        return raw_axioms_at<L>(lo, lo, lo);
    } else if constexpr (HasTop<L>) {
        const LatticeElement<L> hi = L::top();
        return raw_axioms_at<L>(hi, hi, hi);
    } else if constexpr (std::is_default_constructible_v<LatticeElement<L>>) {
        const LatticeElement<L> e{};
        return raw_axioms_at<L>(e, e, e);
    } else {
        // Neither extreme exists and the carrier cannot be
        // default-constructed, so no witness can be built and L is not
        // a Lattice.
        return false;
    }
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
// distributivity law has no other way to be stated.
template <typename S>
concept Semiring = requires { typename S::element_type; } && requires(LatticeElement<S> a, LatticeElement<S> b) {
    { S::add(a, b) } -> std::same_as<LatticeElement<S>>;
    { S::mul(a, b) } -> std::same_as<LatticeElement<S>>;
    { a == b } -> std::convertible_to<bool>;
} && requires {
    { S::zero() } -> std::same_as<LatticeElement<S>>;
    { S::one() } -> std::same_as<LatticeElement<S>>;
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
    return equivalent<L>(L::join(a, a), a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_idempotent_meet(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::meet(a, a), a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_join(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::join(a, b), L::join(b, a));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_commutative_meet(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::meet(a, b), L::meet(b, a));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_join(LatticeElement<L> a, LatticeElement<L> b,
                                                     LatticeElement<L> c) noexcept {
    return equivalent<L>(L::join(L::join(a, b), c), L::join(a, L::join(b, c)));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_associative_meet(LatticeElement<L> a, LatticeElement<L> b,
                                                     LatticeElement<L> c) noexcept {
    return equivalent<L>(L::meet(L::meet(a, b), c), L::meet(a, L::meet(b, c)));
}

template <Lattice L>
[[nodiscard]] consteval bool verify_absorption(LatticeElement<L> a, LatticeElement<L> b) noexcept {
    return equivalent<L>(L::join(a, L::meet(a, b)), a) && equivalent<L>(L::meet(a, L::join(a, b)), a);
}

// Reflexivity is checked on all three witnesses, not one, to catch a
// leq that reads the wrong operand.
template <Lattice L>
[[nodiscard]] consteval bool verify_partial_order(LatticeElement<L> a, LatticeElement<L> b,
                                                  LatticeElement<L> c) noexcept {
    const bool reflexive_a = L::leq(a, a);
    const bool reflexive_b = L::leq(b, b);
    const bool reflexive_c = L::leq(c, c);
    const bool antisymmetric = !(L::leq(a, b) && L::leq(b, a)) || equivalent<L>(a, b);
    const bool transitive = !(L::leq(a, b) && L::leq(b, c)) || L::leq(a, c);
    return reflexive_a && reflexive_b && reflexive_c && antisymmetric && transitive;
}

template <BoundedBelowLattice L>
[[nodiscard]] consteval bool verify_bottom_identity(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::join(L::bottom(), a), a);
}

template <BoundedAboveLattice L>
[[nodiscard]] consteval bool verify_top_identity(LatticeElement<L> a) noexcept {
    return equivalent<L>(L::meet(L::top(), a), a);
}

template <Lattice L>
[[nodiscard]] consteval bool verify_lattice_axioms_at(LatticeElement<L> a, LatticeElement<L> b,
                                                      LatticeElement<L> c) noexcept {
    return verify_idempotent_join<L>(a) && verify_idempotent_meet<L>(a) && verify_commutative_join<L>(a, b)
        && verify_commutative_meet<L>(a, b) && verify_associative_join<L>(a, b, c)
        && verify_associative_meet<L>(a, b, c) && verify_absorption<L>(a, b) && verify_partial_order<L>(a, b, c);
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
    return S::add(S::zero(), a) == a && S::add(a, S::zero()) == a;
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_identity(LatticeElement<S> a) noexcept {
    return S::mul(S::one(), a) == a && S::mul(a, S::one()) == a;
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_zero(LatticeElement<S> a) noexcept {
    return S::mul(S::zero(), a) == S::zero() && S::mul(a, S::zero()) == S::zero();
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_commutative(LatticeElement<S> a, LatticeElement<S> b) noexcept {
    return S::add(a, b) == S::add(b, a);
}

template <Semiring S>
[[nodiscard]] consteval bool verify_additive_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                         LatticeElement<S> c) noexcept {
    return S::add(S::add(a, b), c) == S::add(a, S::add(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_multiplicative_associative(LatticeElement<S> a, LatticeElement<S> b,
                                                               LatticeElement<S> c) noexcept {
    return S::mul(S::mul(a, b), c) == S::mul(a, S::mul(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_distributivity(LatticeElement<S> a, LatticeElement<S> b,
                                                   LatticeElement<S> c) noexcept {
    return S::mul(a, S::add(b, c)) == S::add(S::mul(a, b), S::mul(a, c))
        && S::mul(S::add(a, b), c) == S::add(S::mul(a, c), S::mul(b, c));
}

template <Semiring S>
[[nodiscard]] consteval bool verify_semiring_axioms_at(LatticeElement<S> a, LatticeElement<S> b,
                                                       LatticeElement<S> c) noexcept {
    return verify_additive_identity<S>(a) && verify_multiplicative_identity<S>(a) && verify_multiplicative_zero<S>(a)
        && verify_additive_commutative<S>(a, b) && verify_additive_associative<S>(a, b, c)
        && verify_multiplicative_associative<S>(a, b, c) && verify_distributivity<S>(a, b, c);
}

namespace detail::lattice_self_test {

struct TrivialBoolLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialBool"; }
};

static_assert(Lattice<TrivialBoolLattice>);
static_assert(BoundedBelowLattice<TrivialBoolLattice>);
static_assert(BoundedAboveLattice<TrivialBoolLattice>);
static_assert(BoundedLattice<TrivialBoolLattice>);
static_assert(!UnboundedLattice<TrivialBoolLattice>);

static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, false, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, false, true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, true, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(false, true, true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true, false, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true, false, true));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true, true, false));
static_assert(verify_bounded_lattice_axioms_at<TrivialBoolLattice>(true, true, true));

static_assert(subsumes<TrivialBoolLattice>(false, true));
static_assert(!subsumes<TrivialBoolLattice>(true, false));
static_assert(equivalent<TrivialBoolLattice>(true, true));
static_assert(!equivalent<TrivialBoolLattice>(false, true));
static_assert(strictly_less<TrivialBoolLattice>(false, true));
static_assert(!strictly_less<TrivialBoolLattice>(true, true));

static_assert(lattice_name<TrivialBoolLattice>() == "TrivialBool");

struct TrivialBoolSemiring {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type zero() noexcept { return false; }
    [[nodiscard]] static constexpr element_type one() noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr element_type mul(bool a, bool b) noexcept { return a && b; }
};

static_assert(Semiring<TrivialBoolSemiring>);
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, false, false));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, true, true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true, false, true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true, true, true));

// Every operation is driven once through a non-constant argument.  A
// static_assert reaches only the constant path, and the whole point of
// the constexpr-not-consteval rule is the other one.
inline void runtime_smoke_test() {
    bool x = true;  // deliberately not constexpr
    bool y = false;  // deliberately not constexpr
    [[maybe_unused]] bool bot = TrivialBoolLattice::bottom();
    [[maybe_unused]] bool top = TrivialBoolLattice::top();
    [[maybe_unused]] bool le = TrivialBoolLattice::leq(x, y);
    [[maybe_unused]] bool jo = TrivialBoolLattice::join(x, y);
    [[maybe_unused]] bool me = TrivialBoolLattice::meet(x, y);

    [[maybe_unused]] bool sub = subsumes<TrivialBoolLattice>(y, x);
    [[maybe_unused]] bool eq = equivalent<TrivialBoolLattice>(x, x);
    [[maybe_unused]] bool sl = strictly_less<TrivialBoolLattice>(y, x);

    [[maybe_unused]] bool zer = TrivialBoolSemiring::zero();
    [[maybe_unused]] bool one = TrivialBoolSemiring::one();
    [[maybe_unused]] bool add = TrivialBoolSemiring::add(x, y);
    [[maybe_unused]] bool mul = TrivialBoolSemiring::mul(x, y);
}

}  // namespace detail::lattice_self_test

}  // namespace crucible::algebra
