#pragma once

// element_type is an in-house aggregate rather than a std::tuple because the
// libstdc++ tuple does not aggressively apply the empty base optimization to
// its members.  A tuple of two empty element types costs two bytes, one per
// empty component, where the aggregate below costs the one-byte language
// minimum for the whole product.
//
// The umbrella header that forward-declares the variadic primary template also
// includes this file, so this file must not include it back.  Doing so would
// have the specializations below parsed before the primary declaration they
// specialize is visible.  Each lattice header therefore declares the primary
// template it specializes inline.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

template <typename... Ls>
struct ProductLattice;

template <typename L1, typename L2>
struct ProductLattice<L1, L2> {
    static_assert(Lattice<L1>, "ProductLattice<L1, L2>: L1 must satisfy the Lattice concept.");
    static_assert(Lattice<L2>, "ProductLattice<L1, L2>: L2 must satisfy the Lattice concept.");

    // Defaulting operator== is sound because every lattice element type is
    // required to publish one of its own.
    struct element_type {
        [[no_unique_address]] typename L1::element_type first{};
        [[no_unique_address]] typename L2::element_type second{};

        [[nodiscard]] constexpr bool operator==(const element_type&) const noexcept = default;
    };

    using first_lattice = L1;
    using second_lattice = L2;

    static constexpr std::size_t arity = 2;

    template <std::size_t I>
        requires(I < 2)
    using nth_lattice = std::conditional_t<I == 0, L1, L2>;

    template <std::size_t I>
        requires(I < 2)
    [[nodiscard]] static constexpr auto& get(element_type& e) noexcept {
        if constexpr (I == 0)
            return e.first;
        else
            return e.second;
    }

    template <std::size_t I>
        requires(I < 2)
    [[nodiscard]] static constexpr auto const& get(element_type const& e) noexcept {
        if constexpr (I == 0)
            return e.first;
        else
            return e.second;
    }

    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires BoundedBelowLattice<L1> && BoundedBelowLattice<L2>
    {
        return element_type{L1::bottom(), L2::bottom()};
    }

    [[nodiscard]] static constexpr element_type top() noexcept
        requires BoundedAboveLattice<L1> && BoundedAboveLattice<L2>
    {
        return element_type{L1::top(), L2::top()};
    }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        return L1::leq(a.first, b.first) && L2::leq(a.second, b.second);
    }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{L1::join(a.first, b.first), L2::join(a.second, b.second)};
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{L1::meet(a.first, b.first), L2::meet(a.second, b.second)};
    }

    // The name is a fixed token rather than a composition of the component
    // names.  Building the composed string needs define_static_string glue and
    // pays compile time for a diagnostic the carrier's reflected display
    // already gives.  A formatter that wants the components reads the
    // first_lattice and second_lattice typedefs instead.
    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<L1xL2>"; }
};

template <>
struct ProductLattice<> {
    struct element_type {
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
    };

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<>"; }
};

// The N-ary product stores one base class per component rather than a
// recursive head-and-tail pair.  A cons-list nests element types as deep as
// the arity, so its layout has to be resolved one level at a time, and its
// get<I> recurses through I template instantiations.  Flat bases let the empty
// base optimization run over every slot in a single layout pass, and get<I>
// becomes a single cast to the I-th base.
//
// The binary case keeps a specialization of its own because it exposes the
// first and second member names that callers already use.  The N-ary primary
// would handle two components correctly, but under get<I> spelling only.

namespace detail {

// The index parameter is what makes each slot a distinct type.  Two components
// with the same lattice would otherwise name the same base class twice, which
// is ill-formed.
template <std::size_t I, typename L>
struct ProductSlot {
    [[no_unique_address]] typename L::element_type value{};

    [[nodiscard]] constexpr bool operator==(const ProductSlot&) const noexcept = default;
};

template <typename Indices, typename... Ls>
struct ProductElementImpl;

template <std::size_t... Is, typename... Ls>
struct ProductElementImpl<std::index_sequence<Is...>, Ls...> : ProductSlot<Is, Ls>... {
    [[nodiscard]] constexpr bool operator==(const ProductElementImpl&) const noexcept = default;
};

}  // namespace detail

template <typename... Ls>
    requires(sizeof...(Ls) != 2)
struct ProductLattice<Ls...> {
    static_assert((Lattice<Ls> && ...), "ProductLattice<Ls...>: every L_i must satisfy the Lattice concept.");

    static constexpr std::size_t arity = sizeof...(Ls);

    using element_type = detail::ProductElementImpl<std::make_index_sequence<sizeof...(Ls)>, Ls...>;

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    using nth_lattice = Ls...[I];

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    [[nodiscard]] static constexpr auto& get(element_type& e) noexcept {
        return static_cast<detail::ProductSlot<I, Ls...[I]>&>(e).value;
    }

    template <std::size_t I>
        requires(I < sizeof...(Ls))
    [[nodiscard]] static constexpr auto const& get(element_type const& e) noexcept {
        return static_cast<detail::ProductSlot<I, Ls...[I]> const&>(e).value;
    }

    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires(BoundedBelowLattice<Ls> && ...)
    {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value = Ls...[Is] ::bottom()), ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr element_type top() noexcept
        requires(BoundedAboveLattice<Ls> && ...)
    {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value = Ls...[Is] ::top()), ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr bool leq(element_type const& a, element_type const& b) noexcept {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (Ls...[Is] ::leq(get<Is>(a), get<Is>(b)) && ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
    }

    [[nodiscard]] static constexpr element_type join(element_type const& a, element_type const& b) noexcept {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value =
                  Ls...[Is] ::join(get<Is>(a), get<Is>(b))),
             ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static constexpr element_type meet(element_type const& a, element_type const& b) noexcept {
        element_type result;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((static_cast<detail::ProductSlot<Is, Ls...[Is]>&>(result).value =
                  Ls...[Is] ::meet(get<Is>(a), get<Is>(b))),
             ...);
        }(std::make_index_sequence<sizeof...(Ls)>{});
        return result;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "Product<L1x...xLn>"; }
};

namespace detail::product_lattice_self_test {

struct U8MinMax {
    using element_type = std::uint8_t;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 255; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a >= b ? a : b; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a <= b ? a : b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "U8MinMax"; }
};

static_assert(BoundedLattice<U8MinMax>);

using P_u8u8 = ProductLattice<U8MinMax, U8MinMax>;

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

template <typename T>
using BudgetU8U8 = Graded<ModalityKind::Absolute, P_u8u8, T>;

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

using P_u8u8u8 = ProductLattice<U8MinMax, U8MinMax, U8MinMax>;

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

template <typename T>
using Budgeted3U8 = Graded<ModalityKind::Absolute, P_u8u8u8, T>;
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

// Calling each operation on runtime operands catches the defects the
// compile-time assertions above cannot see, such as an inline body that only
// ever instantiates in a consteval context.
inline void runtime_smoke_test() {
    using L = P_u8u8;
    L::element_type lo{1, 2};
    L::element_type hi{5, 7};
    [[maybe_unused]] bool le = L::leq(lo, hi);
    [[maybe_unused]] L::element_type jn = L::join(lo, hi);
    [[maybe_unused]] L::element_type mt = L::meet(lo, hi);
    [[maybe_unused]] L::element_type bt = L::bottom();
    [[maybe_unused]] L::element_type tp = L::top();

    OneByteValue v{42};
    BudgetU8U8<OneByteValue> initial{v, lo};
    auto widened = initial.weaken(hi);
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(L::top());
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto _r = std::move(rv_widen).consume().c;

    using N = P_u8u8u8;
    N::element_type n_lo{};
    N::get<0>(n_lo) = 1;
    N::get<1>(n_lo) = 2;
    N::get<2>(n_lo) = 3;
    N::element_type n_hi{};
    N::get<0>(n_hi) = 4;
    N::get<1>(n_hi) = 5;
    N::get<2>(n_hi) = 6;

    [[maybe_unused]] bool n_le = N::leq(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_jn = N::join(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_mt = N::meet(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_bt = N::bottom();
    [[maybe_unused]] N::element_type n_tp = N::top();

    OneByteValue n_v{17};
    Budgeted3U8<OneByteValue> n_initial{n_v, n_lo};
    auto n_widened = n_initial.weaken(n_hi);
    auto n_composed = n_initial.compose(n_widened);
    [[maybe_unused]] auto n_g = n_composed.grade();
    [[maybe_unused]] auto n_vc = n_composed.peek().c;
}

}  // namespace detail::product_lattice_self_test

}  // namespace foundation::algebra::lattices
