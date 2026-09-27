#pragma once

// Two ops scheduled together on one device add their demands, so
// combining their rows is arithmetic over magnitudes rather than the
// set union that combines rows of effect atoms.  Without a row that
// adds, the compiler sees each op's demand alone and never the
// combined load that actually contends on the silicon.
//
// "Concurrent" separates this from the sequential case, where the
// second op starts only once the first has released its budgets.  That
// case takes the maximum per axis rather than the sum, and has no
// operation here.
//
// Budgets are uint64_t, and unsigned addition wraps.  A wrapped sum
// reads as a smaller demand than either operand, which would let an
// oversubscribed schedule pass a fitting check.  Three gates refuse a
// wrap.  ConcurrentRow refuses a pack whose sum on one axis wraps, so
// the demand of a row on an axis is always the true sum.  The sum of
// two rows requires ConcurrentlySchedulable, and the sum of any number
// of rows requires that the rows together fit, so neither sum wraps.

#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Decide.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Resources.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <vector>

// The claim a concurrent row makes that no lattice grades.  The row hash
// below folds this identity, and the summed tags as its payloads.
namespace foundation::effects::row_discipline {
struct concurrent_row;
}  // namespace foundation::effects::row_discipline

namespace foundation::effects {

namespace detail {

// True when no axis of the pack sums past the top of uint64_t.  Every
// partial sum is checked, so a wrap that a later value would bring back
// down is still caught.  Complexity: quadratic in the pack length, at
// compile time only.
template <ResourceTag... Tags>
[[nodiscard]] consteval bool pack_sums_fit_() noexcept {
    constexpr std::array<ResourceKind, sizeof...(Tags)> kinds{Tags::kind...};
    constexpr std::array<std::uint64_t, sizeof...(Tags)> values{Tags::value...};
    for (std::size_t axis = 0; axis < kinds.size(); ++axis) {
        std::uint64_t axis_sum = 0;
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            if (kinds[index] != kinds[axis]) continue;
            if (!::foundation::decide::no_overflow_sum(axis_sum, values[index])) return false;
            axis_sum += values[index];
        }
    }
    return true;
}

}  // namespace detail

// A row may name one axis more than once, and its demand on that axis
// is the sum of the budgets that it names there.  A pack whose sum on
// one axis passes the top of uint64_t is refused, because its demand
// would read as the wrapped value.
template <ResourceTag... Tags>
    requires(detail::pack_sums_fit_<Tags...>())
struct ConcurrentRow {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyConcurrentRow = ConcurrentRow<>;

namespace detail {

template <typename T>
struct is_concurrent_row : std::false_type {};

template <ResourceTag... Ts>
struct is_concurrent_row<ConcurrentRow<Ts...>> : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool is_concurrent_row_v = detail::is_concurrent_row<T>::value;

template <typename T>
concept IsConcurrentRow = is_concurrent_row_v<T>;

// The demand of a row on one axis.  The constraint of the row makes the
// fold exact.
template <ResourceKind K, typename R>
struct concurrent_row_value;

template <ResourceKind K, ResourceTag... Ts>
struct concurrent_row_value<K, ConcurrentRow<Ts...>> {
    static constexpr std::uint64_t value = ((Ts::kind == K ? Ts::value : std::uint64_t{0}) + ... + std::uint64_t{0});
};

template <ResourceKind K, typename R>
inline constexpr std::uint64_t concurrent_row_value_v = concurrent_row_value<K, R>::value;

// Two rows are schedulable together when their sum on each axis fits in
// uint64_t.  The demand of each row on an axis is exact, so this one
// check is enough.  The walk reads the axis set through reflection, so a
// new axis extends it with no edit here.
//
// This is necessary and not sufficient.  A separate check compares the
// combined demand against what the hardware offers.
namespace detail {

template <typename R1, typename R2>
[[nodiscard]] consteval bool sums_fit_pairwise_() noexcept {
    // The local must be static.  An expansion statement needs its
    // operand to be a constant expression, and a non-static constexpr
    // local has a per-invocation address, which is not one.
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis : axes) {
        constexpr ResourceKind kind = [:axis:];
        if (!::foundation::decide::no_overflow_sum(concurrent_row_value_v<kind, R1>, concurrent_row_value_v<kind, R2>)) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

template <typename R1, typename R2>
concept ConcurrentlySchedulable =
    IsConcurrentRow<R1> && IsConcurrentRow<R2> && detail::sums_fit_pairwise_<R1, R2>();

// The sum of two rows holds one tag per axis with a non-zero sum, in
// catalog order.  It is canonical, and its order does not depend on the
// order of the two inputs.  The tag template of each axis comes from
// Resources.h by reflection, so a new axis joins the sum with no edit
// here.
namespace detail {

template <typename R1, typename R2>
[[nodiscard]] consteval std::meta::info canonical_sum_() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
    std::vector<std::meta::info> tags;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis : axes) {
        constexpr ResourceKind kind = [:axis:];
        constexpr std::uint64_t sum = concurrent_row_value_v<kind, R1> + concurrent_row_value_v<kind, R2>;
        if constexpr (sum > 0) {
            tags.push_back(std::meta::substitute(tag_template_of_<kind>(), {std::meta::reflect_constant(sum)}));
        }
    }
#pragma GCC diagnostic pop
    return std::meta::substitute(^^ConcurrentRow, tags);
}

}  // namespace detail

template <typename R1, typename R2>
    requires ConcurrentlySchedulable<R1, R2>
using concurrent_row_sum_t = [:detail::canonical_sum_<R1, R2>():];

// The sum of any number of rows.  Its constraint admits only concurrent
// rows whose demands together fit in uint64_t on each axis, so the fold
// below never names a partial sum that wraps, and a caller can ask
// whether a sum exists with a requires expression.  The fold direction is
// fixed even though the sum is commutative and associative, so that the
// same inputs always give the same type.
namespace detail {

// True when every argument is a concurrent row and no axis of the
// arguments together sums past the top of uint64_t.  Every partial sum is
// checked.  Complexity: linear in the number of rows for each axis, at
// compile time only.
template <typename... Rs>
[[nodiscard]] consteval bool rows_sum_fit_() noexcept {
    if constexpr (!(IsConcurrentRow<Rs> && ...)) {
        return false;
    } else {
        static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto axis : axes) {
            constexpr ResourceKind kind = [:axis:];
            constexpr std::array<std::uint64_t, sizeof...(Rs)> demands{concurrent_row_value_v<kind, Rs>...};
            std::uint64_t axis_sum = 0;
            for (const std::uint64_t demand : demands) {
                if (!::foundation::decide::no_overflow_sum(axis_sum, demand)) return false;
                axis_sum += demand;
            }
        }
#pragma GCC diagnostic pop
        return true;
    }
}

template <typename... Rs>
struct concurrent_row_n;

template <>
struct concurrent_row_n<> {
    using type = ConcurrentRow<>;
};

template <typename R>
struct concurrent_row_n<R> {
    using type = R;
};

template <typename R1, typename R2, typename... Rest>
struct concurrent_row_n<R1, R2, Rest...> {
    using type = typename concurrent_row_n<concurrent_row_sum_t<R1, R2>, Rest...>::type;
};

}  // namespace detail

template <typename... Rs>
    requires(detail::rows_sum_fit_<Rs...>())
using concurrent_row_n_t = typename detail::concurrent_row_n<Rs...>::type;

// A row is canonical when it names each axis at most once, and then
// its demand on an axis is exactly the value written in its own pack.
// The sum of two rows is canonical.  The gates that add rows do not
// demand canonical input, since summing non-canonical rows is a
// supported operation.  A caller that wants the stronger property asks
// for it through this concept.
namespace detail {

template <ResourceKind K, typename R>
struct kind_occurrence_count;

template <ResourceKind K, ResourceTag... Ts>
struct kind_occurrence_count<K, ConcurrentRow<Ts...>> {
    static constexpr std::size_t value = ((Ts::kind == K ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
};

template <typename R>
[[nodiscard]] consteval bool names_each_axis_once_() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis : axes) {
        constexpr ResourceKind kind = [:axis:];
        if (kind_occurrence_count<kind, R>::value > 1) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

template <typename R>
concept IsCanonicalConcurrentRow = IsConcurrentRow<R> && detail::names_each_axis_once_<R>();

// One descriptor per tag, for code that walks a row at runtime without
// knowing the tag types.  The array holds the tags in the order the
// row names them and does not re-sort, so a hand-written row keeps its
// own order while a summed row is already in catalog order.
template <typename R>
struct concurrent_row_descriptors;

template <ResourceTag... Ts>
struct concurrent_row_descriptors<ConcurrentRow<Ts...>> {
    static constexpr std::array<ResourceTagDescriptor, sizeof...(Ts)> value{
        ResourceTagDescriptor{Ts::kind, Ts::value, Ts::name}...};
};

template <typename R>
inline constexpr auto concurrent_row_descriptors_v = concurrent_row_descriptors<R>::value;

namespace detail::concurrent_row_self_test {

using resource::CarbonGramsPerKwh;
using resource::HbmBytes;
using resource::NicQp;
using resource::SmBudget;

// A pack that the row refuses, and a pair that the sum refuses, answer
// false here instead of stopping the build.
template <typename... Tags>
concept row_is_formable = requires { typename ConcurrentRow<Tags...>; };

template <typename R1, typename R2>
concept sum_is_formable = requires { typename concurrent_row_sum_t<R1, R2>; };

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<>>, ConcurrentRow<>>);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<>>, ConcurrentRow<SmBudget<32>>>);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<SmBudget<32>>>, ConcurrentRow<SmBudget<32>>>);
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<SmBudget<64>>>,
                             ConcurrentRow<SmBudget<96>>>);

// The two axes come out in catalog order, which puts the compute axis
// before the network one, whichever side names which.
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<NicQp<4>>>,
                             ConcurrentRow<SmBudget<32>, NicQp<4>>>);
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<NicQp<4>>, ConcurrentRow<SmBudget<32>>>,
                             ConcurrentRow<SmBudget<32>, NicQp<4>>>);
static_assert(std::is_same_v<
              concurrent_row_sum_t<ConcurrentRow<SmBudget<32>, NicQp<4>>, ConcurrentRow<SmBudget<64>, NicQp<2>>>,
              ConcurrentRow<SmBudget<96>, NicQp<6>>>);

// The last axis of the catalog joins the sum, so the walk reaches the
// end of the catalog.
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<CarbonGramsPerKwh<3>>, ConcurrentRow<SmBudget<1>>>,
                             ConcurrentRow<SmBudget<1>, CarbonGramsPerKwh<3>>>);

static_assert(std::is_same_v<concurrent_row_n_t<ConcurrentRow<SmBudget<10>>, ConcurrentRow<SmBudget<20>>,
                                                ConcurrentRow<SmBudget<30>>, ConcurrentRow<SmBudget<40>>>,
                             ConcurrentRow<SmBudget<100>>>);
static_assert(std::is_same_v<concurrent_row_n_t<>, ConcurrentRow<>>);
static_assert(std::is_same_v<concurrent_row_n_t<ConcurrentRow<SmBudget<32>>>, ConcurrentRow<SmBudget<32>>>);

// A sum that wraps on one axis, at any position of the fold, names no
// type, and neither does an argument that is not a row.  A sum that
// reaches the top of uint64_t exactly still exists.
template <typename... Rs>
concept HasConcurrentSum = requires { typename concurrent_row_n_t<Rs...>; };

static_assert(HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX - 1>>, ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(!HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(!HasConcurrentSum<ConcurrentRow<>, ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<SmBudget<1>>>);
static_assert(!HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX / 2 + 1>>, ConcurrentRow<HbmBytes<1>>,
                                ConcurrentRow<SmBudget<UINT64_MAX / 2 + 1>>>);
static_assert(!HasConcurrentSum<int>);
static_assert(!HasConcurrentSum<ConcurrentRow<>, SmBudget<1>>);

static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<SmBudget<32>>> == 32);
static_assert(concurrent_row_value_v<ResourceKind::NicQp, ConcurrentRow<SmBudget<32>>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>> == 60);
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<10>, SmBudget<20>>, ConcurrentRow<SmBudget<30>>>,
                             ConcurrentRow<SmBudget<60>>>);

// A row naming an axis once cannot wrap, whatever the value, because
// the fold starts from zero.
static_assert(row_is_formable<SmBudget<UINT64_MAX>>);
static_assert(row_is_formable<SmBudget<UINT64_MAX - 1>, SmBudget<1>>);

// This row would otherwise report a demand of zero on the SM axis.
static_assert(!row_is_formable<SmBudget<UINT64_MAX>, SmBudget<1>>);

// One wrapping axis is enough to refuse the row, the last axis of the
// catalog is walked, and a wrap that a third value would bring back down
// is still refused.
static_assert(!row_is_formable<SmBudget<32>, HbmBytes<UINT64_MAX>, HbmBytes<1>>);
static_assert(!row_is_formable<CarbonGramsPerKwh<UINT64_MAX>, CarbonGramsPerKwh<1>>);
static_assert(!row_is_formable<SmBudget<UINT64_MAX - 10>, SmBudget<20>, SmBudget<1>>);
static_assert(!row_is_formable<int>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<32>>, ConcurrentRow<SmBudget<64>>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<HbmBytes<40000000000ULL>>, ConcurrentRow<HbmBytes<40000000000ULL>>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<>, ConcurrentRow<>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<>>);

static_assert(!ConcurrentlySchedulable<ConcurrentRow<HbmBytes<UINT64_MAX>>, ConcurrentRow<HbmBytes<1>>>);
static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<SmBudget<32>, HbmBytes<UINT64_MAX>>, ConcurrentRow<SmBudget<64>, HbmBytes<1>>>);
static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<CarbonGramsPerKwh<UINT64_MAX>>, ConcurrentRow<CarbonGramsPerKwh<1>>>);
static_assert(!ConcurrentlySchedulable<int, ConcurrentRow<>>);
static_assert(!ConcurrentlySchedulable<ConcurrentRow<>, SmBudget<1>>);

// A pair that wraps has no sum, so the wrapped value never becomes a
// type.
static_assert(sum_is_formable<ConcurrentRow<HbmBytes<UINT64_MAX - 1>>, ConcurrentRow<HbmBytes<1>>>);
static_assert(!sum_is_formable<ConcurrentRow<HbmBytes<UINT64_MAX>>, ConcurrentRow<HbmBytes<2>>>);
static_assert(!sum_is_formable<ConcurrentRow<>, int>);

static_assert(IsCanonicalConcurrentRow<ConcurrentRow<>>);
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>>>);
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>, NicQp<4>>>);

// Order does not bear on it.  Naming an axis twice does.
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<NicQp<4>, SmBudget<32>, HbmBytes<1024>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<10>, SmBudget<20>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>, NicQp<4>, SmBudget<64>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<CarbonGramsPerKwh<10>, CarbonGramsPerKwh<20>>>);
static_assert(!IsCanonicalConcurrentRow<int>);

static_assert(
    IsCanonicalConcurrentRow<concurrent_row_sum_t<ConcurrentRow<SmBudget<10>, SmBudget<20>>, ConcurrentRow<SmBudget<30>>>>);
static_assert(IsCanonicalConcurrentRow<
              concurrent_row_sum_t<ConcurrentRow<SmBudget<32>, NicQp<4>>, ConcurrentRow<SmBudget<64>, NicQp<2>>>>);

static_assert(IsConcurrentRow<ConcurrentRow<>>);
static_assert(IsConcurrentRow<ConcurrentRow<SmBudget<32>>>);
static_assert(!IsConcurrentRow<int>);
static_assert(!IsConcurrentRow<SmBudget<32>>);

static_assert(concurrent_row_descriptors_v<ConcurrentRow<>>.size() == 0);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>.size() == 1);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>[0].kind == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>[0].value == 32);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>.size() == 2);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[0].kind == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[1].kind == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[1].value == 4);

// The row below names its axes out of catalog order, and the
// descriptors keep that order.
static_assert(concurrent_row_descriptors_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>[0].kind == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>[1].kind == ResourceKind::Sm);

static_assert(std::is_empty_v<ConcurrentRow<>>);
static_assert(std::is_empty_v<ConcurrentRow<SmBudget<32>>>);
static_assert(std::is_empty_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>);

// The run-time half of these checks lives in
// test/foundation/test_effects_resources.cpp, which builds the rows and
// walks their descriptors with values the optimizer cannot fold.

}  // namespace detail::concurrent_row_self_test

// The payloads the row hash folds: the tags of a row, as a payload list.
namespace detail {

template <typename R>
struct concurrent_row_payloads;

template <ResourceTag... Ts>
struct concurrent_row_payloads<ConcurrentRow<Ts...>> {
    using type = ::foundation::diag::row_payloads<Ts...>;
};

}  // namespace detail

}  // namespace foundation::effects

// Without this specialization every row takes the zero slot that a bare
// payload has, so two wrapper stacks that differ only in their declared
// budgets would share a federation cache key.
//
// Two rows are equivalent when their per-axis sums agree, whether or
// not either row names an axis twice.  The hash has to agree with
// that, so it folds the canonical form of the row: the sum of the row
// with the empty row, which holds one tag per non-zero axis in catalog
// order.  Anything else would split the cache between two spellings of
// the same demand.  A row whose demand wraps does not exist, so each
// hash reads a true demand.
//
// The canonical form depends on the sum above, so the fold cannot be a
// member alias of the row.  The row is incomplete while its own body is
// read, and the sum needs a complete row.
namespace foundation::diag {

template <::foundation::effects::ResourceTag... Tags>
struct row_hash_contribution<::foundation::effects::ConcurrentRow<Tags...>> {
    static constexpr std::uint64_t value = discipline_row_hash_v<
        ::foundation::effects::row_discipline::concurrent_row,
        typename ::foundation::effects::detail::concurrent_row_payloads<::foundation::effects::concurrent_row_sum_t<
            ::foundation::effects::ConcurrentRow<Tags...>, ::foundation::effects::ConcurrentRow<>>>::type>;
};

}  // namespace foundation::diag

namespace foundation::effects::detail::row_hash_concurrent_row_self_test {

using ::foundation::diag::row_hash_contribution_v;
using ::foundation::effects::ConcurrentRow;
using ::foundation::effects::EmptyConcurrentRow;
using ::foundation::effects::resource::HbmBytes;
using ::foundation::effects::resource::NicQp;
using ::foundation::effects::resource::SmBudget;

static_assert(row_hash_contribution_v<EmptyConcurrentRow> != 0);
static_assert(row_hash_contribution_v<EmptyConcurrentRow> != row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<ConcurrentRow<NicQp<4>>>);
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<HbmBytes<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<SmBudget<64>>>);

// The largest demand on one axis separates from the empty row.  A row
// that wraps to zero on that axis cannot be spelled.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<UINT64_MAX>>>
              != row_hash_contribution_v<EmptyConcurrentRow>);

// The two rows below declare the same demand in two spellings, so
// they must land in the same cache slot.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<30>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<60>>>);

// Naming the axes in the other order is a third spelling of one
// demand.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>
              == row_hash_contribution_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>);

// A row holding one tag and that tag on its own are different things,
// and the row identity keeps them apart whatever the payload fold
// produces.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<SmBudget<32>>);

// A row of zero budgets declares no demand on any axis, which is the
// demand the empty row declares.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<0>>> == row_hash_contribution_v<EmptyConcurrentRow>);

}  // namespace foundation::effects::detail::row_hash_concurrent_row_self_test

// A concurrent row is a ConcurrentRow over resource tags, empty or not.  A
// resource tag alone is no row.
template <>
struct foundation::contracts::armed_cell<::foundation::effects::detail::is_concurrent_row> {
    using accepts = witnesses<::foundation::effects::ConcurrentRow<>,
                              ::foundation::effects::ConcurrentRow<::foundation::effects::resource::SmBudget<32>>>;
    using refuses = witnesses<int, ::foundation::effects::resource::SmBudget<32>>;
};
