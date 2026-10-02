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
#include <foundation/reflect/Anchor.h>

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

// True when type reflects a specialization of ConcurrentRow, read through
// its aliases, with no qualifier.  The answer is a function at namespace
// scope that is not a template, so no translation unit can specialize it
// to make a class a row.
[[nodiscard]] consteval bool is_concurrent_row(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == ^^ConcurrentRow;
}

template <typename T>
concept IsConcurrentRow = is_concurrent_row(^^T);

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
    static constexpr auto axes = std::define_static_array(
        static_cast<::foundation::reflect::anchored_t<^^R1, std::vector<std::meta::info>>>(
            std::meta::enumerators_of(^^ResourceKind)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis : axes) {
        constexpr ResourceKind kind = [:axis:];
        if (!::foundation::decide::no_overflow_sum(concurrent_row_value_v<kind, R1>,
                                                   concurrent_row_value_v<kind, R2>)) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

}  // namespace detail

template <typename R1, typename R2>
concept ConcurrentlySchedulable = IsConcurrentRow<R1> && IsConcurrentRow<R2> && detail::sums_fit_pairwise_<R1, R2>();

// The sum of two rows holds one tag per axis with a non-zero sum, in
// catalog order.  It is canonical, and its order does not depend on the
// order of the two inputs.  The tag template of each axis comes from
// Resources.h by reflection, so a new axis joins the sum with no edit
// here.
namespace detail {

template <typename R1, typename R2>
[[nodiscard]] consteval std::meta::info canonical_sum_() noexcept {
    static constexpr auto axes = std::define_static_array(
        static_cast<::foundation::reflect::anchored_t<^^R1, std::vector<std::meta::info>>>(
            std::meta::enumerators_of(^^ResourceKind)));
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
        using axis_list = ::foundation::reflect::anchored_t<std::meta::reflect_constant(sizeof...(Rs)),
                                                            std::vector<std::meta::info>>;
        static constexpr auto axes =
            std::define_static_array(static_cast<axis_list>(std::meta::enumerators_of(^^ResourceKind)));
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
    static constexpr auto axes = std::define_static_array(
        static_cast<::foundation::reflect::anchored_t<^^R, std::vector<std::meta::info>>>(
            std::meta::enumerators_of(^^ResourceKind)));
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
