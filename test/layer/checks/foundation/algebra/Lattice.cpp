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

}  // namespace detail::lattice_self_test

}  // namespace foundation::algebra
