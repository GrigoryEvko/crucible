// The compile-time checks of foundation/algebra/lattices/ProductLattice.h.

#include <foundation/algebra/lattices/ProductLattice.h>

namespace foundation::algebra::lattices {

namespace detail::product_lattice_self_test {

static_assert(BoundedLattice<U8MinMax>);

static_assert(Lattice<P_u8u8>);
static_assert(BoundedBelowLattice<P_u8u8>);
static_assert(BoundedAboveLattice<P_u8u8>);
static_assert(BoundedLattice<P_u8u8>);

static_assert(P_u8u8::bottom().first == 0);
static_assert(P_u8u8::bottom().second == 0);
static_assert(P_u8u8::top().first == 255);
static_assert(P_u8u8::top().second == 255);

static_assert(P_u8u8::leq({1, 2}, {3, 4}));
static_assert(!P_u8u8::leq({3, 2}, {1, 4}));
static_assert(!P_u8u8::leq({1, 4}, {3, 2}));
static_assert(P_u8u8::leq({0, 0}, {255, 255}));
static_assert(!P_u8u8::leq({255, 255}, {0, 0}));

static_assert(P_u8u8::join({1, 4}, {3, 2}).first == 3);
static_assert(P_u8u8::join({1, 4}, {3, 2}).second == 4);

static_assert(P_u8u8::meet({1, 4}, {3, 2}).first == 1);
static_assert(P_u8u8::meet({1, 4}, {3, 2}).second == 2);

static_assert(verify_bounded_lattice_axioms_at<P_u8u8>({0, 0}, {0, 0}, {0, 0}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>({0, 0}, {127, 64}, {255, 255}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>({1, 4}, {3, 2}, {5, 7}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>({255, 0}, {0, 255}, {127, 127}));

static_assert(subsumes<P_u8u8>({1, 2}, {3, 4}));
static_assert(!subsumes<P_u8u8>({3, 4}, {1, 2}));
static_assert(equivalent<P_u8u8>({5, 7}, {5, 7}));
static_assert(!equivalent<P_u8u8>({5, 7}, {7, 5}));
static_assert(strictly_less<P_u8u8>({1, 2}, {3, 4}));
static_assert(!strictly_less<P_u8u8>({3, 4}, {1, 2}));

static_assert(P_u8u8::name() == "Product<L1xL2>");

static_assert(std::is_same_v<P_u8u8::first_lattice, U8MinMax>);
static_assert(std::is_same_v<P_u8u8::second_lattice, U8MinMax>);

static_assert(P_u8u8::arity == 2);
static_assert(std::is_same_v<P_u8u8::nth_lattice<0>, U8MinMax>);
static_assert(std::is_same_v<P_u8u8::nth_lattice<1>, U8MinMax>);

static_assert(P_u8u8::get<0>(P_u8u8::bottom()) == 0);
static_assert(P_u8u8::get<1>(P_u8u8::bottom()) == 0);
static_assert(P_u8u8::get<0>(P_u8u8::top()) == 255);
static_assert(P_u8u8::get<1>(P_u8u8::top()) == 255);

[[nodiscard]] consteval bool binary_get_matches_first_second() noexcept {
    P_u8u8::element_type e{17, 42};
    return P_u8u8::get<0>(e) == e.first && P_u8u8::get<1>(e) == e.second;
}
static_assert(binary_get_matches_first_second());

using P_empty = ProductLattice<>;

static_assert(BoundedLattice<P_empty>);
static_assert(verify_bounded_lattice_axioms_at<P_empty>(P_empty::bottom(), P_empty::bottom(), P_empty::bottom()));
static_assert(P_empty::name() == "Product<>");
static_assert(std::is_empty_v<P_empty::element_type>);

using P_qtt_u8 = ProductLattice<QttSemiring::At<QttGrade::One>, U8MinMax>;

static_assert(Lattice<P_qtt_u8>);
static_assert(P_qtt_u8::bottom().second == 0);
static_assert(P_qtt_u8::top().second == 255);
static_assert(P_qtt_u8::leq({{}, 1}, {{}, 5}));
static_assert(!P_qtt_u8::leq({{}, 5}, {{}, 1}));

// The bound is an inequality, not an equality, because the claim under test is
// that the empty component costs no second slot.  One trailing byte is still
// admissible if a compiler declines the collapse.
static_assert(sizeof(P_qtt_u8::element_type) <= sizeof(std::uint8_t) + 1,
              "ProductLattice<Empty, NonEmpty>::element_type must EBO-collapse "
              "the empty component down to ≤ 1 trailing byte; if this fires, "
              "the [[no_unique_address]] discipline drifted.");

// The layout-invariant macro asserts that the carrier costs exactly the value
// type, so it applies only where every component element type is empty.  Where
// a component carries runtime grade data the carrier legitimately grows, and
// the assertions below bound that growth by hand instead.
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

namespace empty_empty_witness {
struct PredA {};
struct PredB {};
}  // namespace empty_empty_witness

using P_empty_empty = ProductLattice<BoolLattice<empty_empty_witness::PredA>, BoolLattice<empty_empty_witness::PredB>>;

template <typename T>
using BudgetEmptyEmpty = Graded<ModalityKind::Absolute, P_empty_empty, T>;

static_assert(std::is_empty_v<P_empty_empty::element_type>,
              "ProductLattice<EmptyL1, EmptyL2>::element_type must be empty for "
              "the EBO collapse contract to hold; if this fires the [[no_unique_"
              "address]] discipline drifted.");

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, double);

static_assert(sizeof(BudgetU8U8<int>) <= sizeof(int) + 4,
              "BudgetU8U8<int> exceeded sizeof(int) + 4 — the U8×U8 grade "
              "(2 bytes) plus alignment padding (≤ 2 bytes) should fit in 4 "
              "trailing bytes; if this fires investigate Graded's grade "
              "field placement.");
static_assert(sizeof(BudgetU8U8<double>) <= sizeof(double) + 8,
              "BudgetU8U8<double> exceeded sizeof(double) + 8 — the U8×U8 "
              "grade (2 bytes) plus alignment padding (≤ 6 bytes) should fit "
              "in 8 trailing bytes; if this fires investigate Graded's grade "
              "field placement.");

// A single-component product is the boundary case that catches any accidental
// assumption of two or more slots in the index-sequence folds.
using P_u8 = ProductLattice<U8MinMax>;

static_assert(Lattice<P_u8>);
static_assert(BoundedLattice<P_u8>);
static_assert(P_u8::arity == 1);
static_assert(std::is_same_v<P_u8::nth_lattice<0>, U8MinMax>);

static_assert(P_u8::get<0>(P_u8::bottom()) == 0);
static_assert(P_u8::get<0>(P_u8::top()) == 255);

[[nodiscard]] consteval bool n1_construction_works() noexcept {
    P_u8::element_type e{};
    P_u8::get<0>(e) = 42;
    return P_u8::get<0>(e) == 42;
}
static_assert(n1_construction_works());

static_assert(Lattice<P_u8u8u8>);
static_assert(BoundedLattice<P_u8u8u8>);
static_assert(BoundedBelowLattice<P_u8u8u8>);
static_assert(BoundedAboveLattice<P_u8u8u8>);
static_assert(P_u8u8u8::arity == 3);

static_assert(std::is_same_v<P_u8u8u8::nth_lattice<0>, U8MinMax>);
static_assert(std::is_same_v<P_u8u8u8::nth_lattice<1>, U8MinMax>);
static_assert(std::is_same_v<P_u8u8u8::nth_lattice<2>, U8MinMax>);

static_assert(P_u8u8u8::get<0>(P_u8u8u8::bottom()) == 0);
static_assert(P_u8u8u8::get<1>(P_u8u8u8::bottom()) == 0);
static_assert(P_u8u8u8::get<2>(P_u8u8u8::bottom()) == 0);
static_assert(P_u8u8u8::get<0>(P_u8u8u8::top()) == 255);
static_assert(P_u8u8u8::get<1>(P_u8u8u8::top()) == 255);
static_assert(P_u8u8u8::get<2>(P_u8u8u8::top()) == 255);

[[nodiscard]] consteval P_u8u8u8::element_type make_u8u8u8(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
    P_u8u8u8::element_type e{};
    P_u8u8u8::get<0>(e) = a;
    P_u8u8u8::get<1>(e) = b;
    P_u8u8u8::get<2>(e) = c;
    return e;
}

static_assert(P_u8u8u8::leq(make_u8u8u8(1, 2, 3), make_u8u8u8(5, 6, 7)));
static_assert(!P_u8u8u8::leq(make_u8u8u8(5, 2, 3), make_u8u8u8(1, 6, 7)));
static_assert(!P_u8u8u8::leq(make_u8u8u8(1, 6, 3), make_u8u8u8(5, 2, 7)));
// The failing slot is the last one, so a fold that stopped before the end
// would report this pair as ordered.
static_assert(!P_u8u8u8::leq(make_u8u8u8(1, 2, 7), make_u8u8u8(5, 6, 3)));

static_assert(P_u8u8u8::get<0>(P_u8u8u8::join(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 4);
static_assert(P_u8u8u8::get<1>(P_u8u8u8::join(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 5);
static_assert(P_u8u8u8::get<2>(P_u8u8u8::join(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 6);

static_assert(P_u8u8u8::get<0>(P_u8u8u8::meet(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 1);
static_assert(P_u8u8u8::get<1>(P_u8u8u8::meet(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 2);
static_assert(P_u8u8u8::get<2>(P_u8u8u8::meet(make_u8u8u8(1, 5, 3), make_u8u8u8(4, 2, 6))) == 3);

static_assert(verify_bounded_lattice_axioms_at<P_u8u8u8>(make_u8u8u8(0, 0, 0), make_u8u8u8(127, 64, 200),
                                                         make_u8u8u8(255, 255, 255)));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8u8>(make_u8u8u8(1, 4, 9), make_u8u8u8(3, 2, 5),
                                                         make_u8u8u8(5, 7, 1)));

// Each slot carries a chain order, which is distributive, and a product of
// distributive lattices is distributive.
static_assert(verify_distributive_lattice<P_u8u8u8>(make_u8u8u8(1, 4, 9), make_u8u8u8(3, 2, 5), make_u8u8u8(5, 7, 1)));

using P_u8x4 = ProductLattice<U8MinMax, U8MinMax, U8MinMax, U8MinMax>;
static_assert(Lattice<P_u8x4>);
static_assert(BoundedLattice<P_u8x4>);
static_assert(P_u8x4::arity == 4);
static_assert(P_u8x4::get<0>(P_u8x4::bottom()) == 0);
static_assert(P_u8x4::get<3>(P_u8x4::top()) == 255);

namespace n_ary_witness {
struct PredA {};
struct PredB {};
struct PredC {};
struct PredD {};
struct PredE {};
}  // namespace n_ary_witness

using P_empty_3way = ProductLattice<BoolLattice<n_ary_witness::PredA>, BoolLattice<n_ary_witness::PredB>,
                                    BoolLattice<n_ary_witness::PredC>>;
using P_empty_4way = ProductLattice<BoolLattice<n_ary_witness::PredA>, BoolLattice<n_ary_witness::PredB>,
                                    BoolLattice<n_ary_witness::PredC>, BoolLattice<n_ary_witness::PredD>>;
using P_empty_5way = ProductLattice<BoolLattice<n_ary_witness::PredA>, BoolLattice<n_ary_witness::PredB>,
                                    BoolLattice<n_ary_witness::PredC>, BoolLattice<n_ary_witness::PredD>,
                                    BoolLattice<n_ary_witness::PredE>>;

static_assert(std::is_empty_v<P_empty_3way::element_type>,
              "ProductLattice<EmptyL,EmptyL,EmptyL>::element_type must be empty "
              "for the inheritance-EBO contract to hold; if this fires the "
              "ProductSlot<I, L> base inheritance discipline drifted.");
static_assert(std::is_empty_v<P_empty_4way::element_type>);
static_assert(std::is_empty_v<P_empty_5way::element_type>);

static_assert(sizeof(P_empty_3way::element_type) == 1);
static_assert(sizeof(P_empty_4way::element_type) == 1);
static_assert(sizeof(P_empty_5way::element_type) == 1);

using P_mixed_one_nonempty =
    ProductLattice<U8MinMax, BoolLattice<n_ary_witness::PredA>, BoolLattice<n_ary_witness::PredB>>;
static_assert(sizeof(P_mixed_one_nonempty::element_type) == 1,
              "ProductLattice<NonEmpty, Empty, Empty>::element_type must be 1 "
              "byte — the two empty slots EBO-collapse to zero.  If this fires "
              "the inheritance discipline failed to share addresses.");

using P_mixed_two_nonempty =
    ProductLattice<BoolLattice<n_ary_witness::PredA>, U8MinMax, BoolLattice<n_ary_witness::PredB>, U8MinMax>;
static_assert(sizeof(P_mixed_two_nonempty::element_type) == 2,
              "ProductLattice<Empty, NonEmpty, Empty, NonEmpty>::element_type "
              "must be 2 bytes — the two empty slots EBO-collapse, leaving two "
              "1-byte non-empty slots adjacent.  If this fires the per-slot EBO "
              "failed to share addresses across non-adjacent empty bases.");

template <typename T>
using Budgeted3Empty = Graded<ModalityKind::Absolute, P_empty_3way, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(Budgeted3Empty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Budgeted3Empty, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Budgeted3Empty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Budgeted3Empty, double);

static_assert(sizeof(Budgeted3U8<int>) <= sizeof(int) + 4,
              "Budgeted3U8<int> exceeded sizeof(int) + 4 — the U8×U8×U8 grade "
              "(3 bytes) plus alignment padding (≤ 1 byte) should fit in 4 "
              "trailing bytes; if this fires investigate Graded's grade field "
              "placement or the inheritance-EBO discipline.");
static_assert(sizeof(Budgeted3U8<double>) <= sizeof(double) + 8,
              "Budgeted3U8<double> exceeded sizeof(double) + 8 — the U8×U8×U8 "
              "grade (3 bytes) plus alignment padding (≤ 5 bytes) should fit "
              "in 8 trailing bytes.");

static_assert(P_u8u8u8::name() == "Product<L1x...xLn>");
static_assert(P_u8::name() == "Product<L1x...xLn>");
static_assert(P_u8x4::name() == "Product<L1x...xLn>");

}  // namespace detail::product_lattice_self_test

}  // namespace foundation::algebra::lattices
