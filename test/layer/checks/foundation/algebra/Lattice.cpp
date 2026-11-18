// The compile-time checks of foundation/algebra/Lattice.h.

#include <foundation/algebra/Lattice.h>

namespace foundation::algebra {

namespace detail::lattice_self_test {

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

static_assert(Semiring<TrivialBoolSemiring>);
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, false, false));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(false, true, true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true, false, true));
static_assert(verify_semiring_axioms_at<TrivialBoolSemiring>(true, true, true));

static_assert(Row<TrivialRow>);
static_assert(BoundedLattice<TrivialRow>);
static_assert(!Row<TrivialBoolLattice>, "a bounded lattice without atoms is not a row");
static_assert(verify_bounded_lattice_axioms_at<TrivialRow>(0, 1, 2));
static_assert(verify_bounded_lattice_axioms_at<TrivialRow>(1, 2, 3));
static_assert(verify_bounded_lattice_axioms_at<TrivialRow>(3, 0, 1));
static_assert(verify_distributive_lattice<TrivialRow>(1, 2, 3));
static_assert(TrivialRow::contains(TrivialRow::single(TrivialAtom::Write), TrivialAtom::Write));
static_assert(!TrivialRow::contains(TrivialRow::single(TrivialAtom::Write), TrivialAtom::Read));
static_assert(subsumes<TrivialRow>(TrivialRow::single(TrivialAtom::Read), TrivialRow::top()));
static_assert(!subsumes<TrivialRow>(TrivialRow::top(), TrivialRow::single(TrivialAtom::Read)));

// The refused shapes.  Each one satisfies every law that compares
// elements through leq alone, so each is a witness that the laws tying
// the order to join, meet, bottom and top are evaluated.
template <typename L>
concept CanVerifyAxioms = requires { verify_lattice_axioms_at<L>(L::bottom(), L::bottom(), L::bottom()); };
template <typename L>
concept CanVerifyBoundedAxioms =
    requires { verify_bounded_lattice_axioms_at<L>(L::bottom(), L::bottom(), L::bottom()); };

// leq runs against join and meet.
static_assert(LatticeShape<InvertedOrder> && detail::lattice_laws::partial_order<InvertedOrder>(0, 1, 3)
                  && detail::lattice_laws::absorption<InvertedOrder>(0, 3),
              "the refused lattice must pass the laws that read leq alone, or it witnesses nothing");
static_assert(!Lattice<InvertedOrder>);
static_assert(!BoundedLattice<InvertedOrder>);
static_assert(!CanVerifyAxioms<InvertedOrder> && !CanVerifyBoundedAxioms<InvertedOrder>);

// The order is right, and bottom() and top() are exchanged.
static_assert(detail::lattice_laws::axioms_at<ExchangedBounds>(3, 0, 3),
              "the refused lattice must pass every law between elements, or it witnesses nothing");
static_assert(!Lattice<ExchangedBounds>);
static_assert(!BoundedLattice<ExchangedBounds>);
static_assert(!CanVerifyBoundedAxioms<ExchangedBounds>);

// Right at the extremes, wrong inside: leq(1, 2) is false while join(1,
// 2) is 2.  The concept cannot see the interior, and the helpers can.
struct InteriorDisagreement {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return a <= b && !(a == 1 && b == 2);
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
};
static_assert(BoundedLattice<InteriorDisagreement>);
static_assert(detail::lattice_laws::partial_order<InteriorDisagreement>(1, 2, 3));
static_assert(verify_bounded_lattice_axioms_at<InteriorDisagreement>(0, 3, 3));
static_assert(!verify_order_agrees<InteriorDisagreement>(1, 2));
static_assert(!verify_lattice_axioms_at<InteriorDisagreement>(1, 2, 3));
static_assert(!verify_bounded_lattice_axioms_at<InteriorDisagreement>(1, 2, 3));

// Subtraction for add passes a signature check and no law.
static_assert(SemiringShape<SubtractionSemiring>);
static_assert(!Semiring<SubtractionSemiring>);

// ── Partial commutative monoids ─────────────────────────────────────

// An element of a monoid of small counts, from a literal.  The witnesses of
// a claim have the element type exactly.
template <typename M>
[[nodiscard]] consteval LatticeElement<M> element_of(int value) noexcept {
    return static_cast<LatticeElement<M>>(value);
}

// The counts 0 to 15, with a sum only when it stays at most 15: a
// cancellative partial commutative monoid.
struct NibbleCount {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type a, element_type b) noexcept { return a + b <= 15; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a + b);
    }
};
static_assert(PartialMonoidShape<NibbleCount> && PartialCommutativeMonoid<NibbleCount>);
static_assert(verify_partial_commutative_monoid_over<NibbleCount>(
    element_of<NibbleCount>(0), element_of<NibbleCount>(1), element_of<NibbleCount>(7), element_of<NibbleCount>(8),
    element_of<NibbleCount>(15)));
static_assert(verify_cancellative_over<NibbleCount>(element_of<NibbleCount>(0), element_of<NibbleCount>(3),
                                                    element_of<NibbleCount>(12), element_of<NibbleCount>(15)));

// Or on a bool: a sum is always defined, and true absorbs, so the monoid is
// no cancellative one.
struct OrMonoid {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type zero() noexcept { return false; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return a || b; }
};
static_assert(PartialCommutativeMonoid<OrMonoid> && verify_partial_commutative_monoid_over<OrMonoid>(false, true));
static_assert(!verify_cancellative_over<OrMonoid>(false, true));
static_assert(!verify_partial_cancellative_at<OrMonoid>(true, false, true));

// The left choice: the first summand that is not zero.  zero is its unit,
// and it is associative, but not commutative.  The gate at zero cannot see
// it, and the claim at two witnesses refuses it.
struct LeftChoice {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept { return a != 0 ? a : b; }
};
static_assert(PartialCommutativeMonoid<LeftChoice>);
static_assert(!verify_partial_commutative_monoid_over<LeftChoice>(element_of<LeftChoice>(0), element_of<LeftChoice>(1),
                                                                  element_of<LeftChoice>(2)));
static_assert(detail::lattice_laws::partial_associative<LeftChoice>(1, 2, 3));
static_assert(!detail::lattice_laws::partial_commutative<LeftChoice>(1, 2));

// A domain that is not symmetric: a sum is defined only in one order.
struct OneWayDomain {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type a, element_type b) noexcept {
        return a == 0 || b == 0 || a < b;
    }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a + b);
    }
};
static_assert(PartialCommutativeMonoid<OneWayDomain>);
static_assert(!verify_partial_monoid_axioms_at<OneWayDomain>(1, 2, 0));

// A zero that is no unit fails the gate.
struct ZeroIsNoUnit {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 1; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a + b);
    }
};
static_assert(PartialMonoidShape<ZeroIsNoUnit> && !PartialCommutativeMonoid<ZeroIsNoUnit>);

// A domain that refuses a sum with zero fails the gate.
struct ZeroOutsideDomain {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type zero() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_sum_defined(element_type a, element_type b) noexcept { return a != b; }
    [[nodiscard]] static constexpr element_type add(element_type a, element_type b) noexcept {
        return static_cast<element_type>(a + b);
    }
};
static_assert(PartialMonoidShape<ZeroOutsideDomain> && !PartialCommutativeMonoid<ZeroOutsideDomain>);

// A total semiring has no domain predicate, so it is no partial monoid by
// signature, and a lattice has no add.
static_assert(!PartialMonoidShape<TrivialBoolSemiring> && !PartialMonoidShape<TrivialBoolLattice>);

}  // namespace detail::lattice_self_test

}  // namespace foundation::algebra
