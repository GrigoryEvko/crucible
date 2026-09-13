#pragma once

#include <crucible/effects/Capabilities.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::effects {

template <Effect... Es>
struct Row {
    static constexpr std::size_t size = sizeof...(Es);
};

using EmptyRow = Row<>;

// The sort key is the Effect underlying value.  The row hash that keys
// the federation cache is permutation-invariant and set-semantic, so
// sorting on anything else would let two rows share a hash while
// differing as types.
template <typename R>
struct canonical_row;

namespace detail {

// The sort is O(N²).  N is one row's atom count, which the effect
// catalog caps at 64, so a faster sort would only add compile-time
// template machinery.
template <Effect... Es>
[[nodiscard]] consteval auto compute_canonical_effect_pack() noexcept {
    struct Result {
        std::array<Effect, sizeof...(Es) == 0 ? 1 : sizeof...(Es)> data{};
        std::size_t count = 0;
    };
    Result r{};
    if constexpr (sizeof...(Es) == 0) {
        return r;
    } else {
        std::array<Effect, sizeof...(Es)> raw{Es...};
        using U = std::underlying_type_t<Effect>;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            for (std::size_t j = i + 1; j < raw.size(); ++j) {
                if (static_cast<U>(raw[j]) < static_cast<U>(raw[i])) {
                    Effect const tmp = raw[i];
                    raw[i] = raw[j];
                    raw[j] = tmp;
                }
            }
        }
        std::size_t out = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (i == 0 || raw[i] != raw[i - 1]) {
                r.data[out++] = raw[i];
            }
        }
        r.count = out;
        return r;
    }
}

template <Effect... Es>
inline constexpr auto canonical_effect_pack_v = compute_canonical_effect_pack<Es...>();

}  // namespace detail

template <>
struct canonical_row<Row<>> {
    using type = Row<>;
};

template <Effect E0, Effect... Es>
struct canonical_row<Row<E0, Es...>> {
private:
    template <std::size_t... Is>
    static auto build(std::index_sequence<Is...>) -> Row<detail::canonical_effect_pack_v<E0, Es...>.data[Is]...>;

public:
    using type = decltype(build(std::make_index_sequence<detail::canonical_effect_pack_v<E0, Es...>.count>{}));
};

template <typename R>
using canonical_row_t = typename canonical_row<R>::type;

template <typename R>
inline constexpr std::size_t row_pack_size_v = R::size;

template <typename R>
inline constexpr std::size_t row_unique_size_v = row_pack_size_v<canonical_row_t<R>>;

// The unqualified spelling of `row_pack_size_v`.  Both names exist so
// that a call site can say which cardinality it means.
template <typename R>
inline constexpr std::size_t row_size_v = R::size;

template <typename R, Effect E>
inline constexpr bool row_contains_v = false;

template <Effect E, Effect... Es>
inline constexpr bool row_contains_v<Row<Es...>, E> = ((Es == E) || ...);

namespace detail {

template <typename R, Effect E>
struct row_insert_unique;

template <Effect... Es, Effect E>
struct row_insert_unique<Row<Es...>, E> {
    using type = std::conditional_t<((Es == E) || ...), Row<Es...>, Row<Es..., E>>;
};

template <typename R, Effect E>
using row_insert_unique_t = typename row_insert_unique<R, E>::type;

template <typename R1, typename R2>
struct row_union_recursive;

template <typename R1>
struct row_union_recursive<R1, Row<>> {
    using type = R1;
};

template <typename R1, Effect Head, Effect... Tail>
struct row_union_recursive<R1, Row<Head, Tail...>> {
    using type = typename row_union_recursive<row_insert_unique_t<R1, Head>, Row<Tail...>>::type;
};

template <typename...>
struct row_concat;

template <>
struct row_concat<> {
    using type = Row<>;
};

template <Effect... Xs>
struct row_concat<Row<Xs...>> {
    using type = Row<Xs...>;
};

template <Effect... Xs, Effect... Ys, typename... Rest>
struct row_concat<Row<Xs...>, Row<Ys...>, Rest...> {
    using type = typename row_concat<Row<Xs..., Ys...>, Rest...>::type;
};

template <typename R1, typename R2>
struct row_difference_impl;

template <Effect... E1s, typename R2>
struct row_difference_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<row_contains_v<R2, E>, Row<>, Row<E>>;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

template <typename R1, typename R2>
struct row_intersection_impl;

template <Effect... E1s, typename R2>
struct row_intersection_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<row_contains_v<R2, E>, Row<E>, Row<>>;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

}  // namespace detail

template <typename R1, typename R2>
using row_union_t = canonical_row_t<typename detail::row_union_recursive<R1, R2>::type>;

template <typename R1, typename R2>
using row_difference_t = canonical_row_t<typename detail::row_difference_impl<R1, R2>::type>;

template <typename R1, typename R2>
using row_intersection_t = canonical_row_t<typename detail::row_intersection_impl<R1, R2>::type>;

template <typename R1, typename R2>
struct is_subrow : std::false_type {};

template <Effect... E1s, Effect... E2s>
struct is_subrow<Row<E1s...>, Row<E2s...>> : std::bool_constant<(row_contains_v<Row<E2s...>, E1s> && ...)> {};

template <typename R1, typename R2>
inline constexpr bool is_subrow_v = is_subrow<R1, R2>::value;

template <typename R1, typename R2>
concept Subrow = is_subrow_v<R1, R2>;

namespace detail::effect_row_self_test {

using R_empty = Row<>;
using R_alloc = Row<Effect::Alloc>;
using R_io = Row<Effect::IO>;
using R_alloc_io = Row<Effect::Alloc, Effect::IO>;
using R_alloc_io_bg = Row<Effect::Alloc, Effect::IO, Effect::Bg>;

static_assert(row_size_v<R_empty> == 0);
static_assert(row_size_v<R_alloc> == 1);
static_assert(row_size_v<R_alloc_io> == 2);
static_assert(row_size_v<R_alloc_io_bg> == 3);

static_assert(!row_contains_v<R_empty, Effect::Alloc>);
static_assert(row_contains_v<R_alloc, Effect::Alloc>);
static_assert(!row_contains_v<R_alloc, Effect::IO>);
static_assert(row_contains_v<R_alloc_io, Effect::Alloc>);
static_assert(row_contains_v<R_alloc_io, Effect::IO>);
static_assert(!row_contains_v<R_alloc_io, Effect::Bg>);

static_assert(is_subrow_v<R_empty, R_empty>);
static_assert(is_subrow_v<R_empty, R_alloc>);
static_assert(is_subrow_v<R_alloc, R_alloc_io>);
static_assert(!is_subrow_v<R_alloc_io, R_alloc>);
static_assert(is_subrow_v<R_alloc_io, R_alloc_io_bg>);
static_assert(!is_subrow_v<R_io, R_alloc>);

static_assert(Subrow<R_empty, R_alloc_io>);
static_assert(Subrow<R_alloc, R_alloc_io_bg>);
static_assert(!Subrow<R_alloc_io, R_alloc>);

static_assert(std::is_same_v<row_union_t<R_empty, R_empty>, R_empty>);
static_assert(std::is_same_v<row_union_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, row_union_t<R_empty, R_alloc_io>>);
static_assert(is_subrow_v<row_union_t<R_empty, R_alloc_io>, R_alloc_io>);

using R_union_a_io = row_union_t<R_alloc, R_io>;
static_assert(is_subrow_v<R_alloc, R_union_a_io>);
static_assert(is_subrow_v<R_io, R_union_a_io>);
static_assert(row_size_v<R_union_a_io> == 2);

using R_union_dup = row_union_t<R_alloc_io, R_alloc>;
static_assert(row_size_v<R_union_dup> == 2);
static_assert(is_subrow_v<R_alloc_io, R_union_dup>);
static_assert(is_subrow_v<R_alloc, R_union_dup>);
static_assert(!row_contains_v<R_union_dup, Effect::Bg>);

using R_left = row_union_t<R_alloc, R_io>;
using R_right = row_union_t<R_io, R_alloc>;
static_assert(is_subrow_v<R_left, R_right>);
static_assert(is_subrow_v<R_right, R_left>);

using R_lr_then_bg = row_union_t<row_union_t<R_alloc, R_io>, Row<Effect::Bg>>;
using R_lr_then_bg_alt = row_union_t<R_alloc, row_union_t<R_io, Row<Effect::Bg>>>;
static_assert(is_subrow_v<R_lr_then_bg, R_lr_then_bg_alt>);
static_assert(is_subrow_v<R_lr_then_bg_alt, R_lr_then_bg>);
static_assert(row_size_v<R_lr_then_bg> == 3);

static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(row_size_v<row_difference_t<R_alloc_io, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_alloc_io>, R_empty>);

using R_diff = row_difference_t<R_alloc_io_bg, R_alloc>;
static_assert(row_size_v<R_diff> == 2);
static_assert(!row_contains_v<R_diff, Effect::Alloc>);
static_assert(row_contains_v<R_diff, Effect::IO>);
static_assert(row_contains_v<R_diff, Effect::Bg>);

static_assert(row_size_v<row_intersection_t<R_empty, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_intersection_t<R_alloc_io, R_alloc_io>, R_alloc_io>);

using R_inter = row_intersection_t<R_alloc_io, R_alloc_io_bg>;
static_assert(is_subrow_v<R_inter, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, R_inter>);
static_assert(row_size_v<R_inter> == 2);

using R_inter_disjoint = row_intersection_t<R_alloc, R_io>;
static_assert(row_size_v<R_inter_disjoint> == 0);

using R_universe = Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>;
static_assert(row_size_v<R_universe> == effect_count);
static_assert(is_subrow_v<R_alloc_io, R_universe>);
static_assert(is_subrow_v<R_alloc_io_bg, R_universe>);

static_assert(row_size_v<row_difference_t<R_universe, R_universe>> == 0);
static_assert(row_size_v<row_intersection_t<R_universe, R_empty>> == 0);

// The expected packs below are in sorted underlying-value order:
// Alloc, IO, Block, Bg, Init, Test.
static_assert(std::is_same_v<canonical_row_t<Row<>>, Row<>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg>>, Row<Effect::Bg>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Alloc, Effect::IO>>, Row<Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::IO, Effect::Alloc>>, Row<Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>, Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>, Row<Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::Block>>,
                             Row<Effect::IO, Effect::Block, Effect::Bg>>);

using R_canon_once = canonical_row_t<Row<Effect::Bg, Effect::Alloc, Effect::Bg>>;
using R_canon_twice = canonical_row_t<R_canon_once>;
static_assert(std::is_same_v<R_canon_once, R_canon_twice>);
static_assert(std::is_same_v<R_canon_once, Row<Effect::Alloc, Effect::Bg>>);

static_assert(row_size_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>> == 2);
static_assert(row_size_v<canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>> == 1);
static_assert(row_size_v<canonical_row_t<Row<>>> == 0);

namespace cardinality_lens_witness {

using R_dup = Row<Effect::Bg, Effect::IO, Effect::Bg>;

static_assert(row_pack_size_v<R_dup> == 3);
static_assert(row_size_v<R_dup> == 3);

static_assert(row_unique_size_v<R_dup> == 2);

// The row hash is set-semantic, so it counts what
// `row_unique_size_v` counts and needs no trait of its own.
static_assert(row_pack_size_v<canonical_row_t<R_dup>> == row_unique_size_v<R_dup>);

using R_canon = canonical_row_t<R_dup>;
static_assert(row_pack_size_v<R_canon> == 2);
static_assert(row_unique_size_v<R_canon> == 2);

static_assert(row_pack_size_v<Row<>> == 0);
static_assert(row_unique_size_v<Row<>> == 0);
static_assert(row_size_v<Row<>> == 0);

}  // namespace cardinality_lens_witness

static_assert(std::is_same_v<row_union_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
                             Row<Effect::IO, Effect::Block, Effect::Bg>>);

static_assert(std::is_same_v<row_union_t<R_alloc, R_io>, row_union_t<R_io, R_alloc>>);

static_assert(std::is_same_v<R_lr_then_bg, R_lr_then_bg_alt>);

static_assert(std::is_same_v<row_difference_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
                             Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<row_intersection_t<Row<Effect::Bg, Effect::IO>, Row<Effect::IO, Effect::Bg>>,
                             Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<row_union_t<Row<Effect::Test, Effect::Alloc>, Row<Effect::Bg>>,
                             row_union_t<Row<Effect::Bg>, Row<Effect::Alloc, Effect::Test>>>);

// The rest of this header is consteval-only.  This body is the one
// place the row types are instantiated as runtime objects, so a
// canonicalization change that dragged in a non-trivial default
// constructor fails here.
inline void runtime_smoke_test() {
    [[maybe_unused]] Row<Effect::Bg> r_bg{};
    [[maybe_unused]] Row<Effect::Bg, Effect::IO> r_bg_io{};
    [[maybe_unused]] EmptyRow r_empty{};

    [[maybe_unused]] auto sz1 = sizeof(r_bg);
    [[maybe_unused]] auto sz2 = sizeof(r_empty);

    [[maybe_unused]] std::size_t n_bg = decltype(r_bg)::size;
    [[maybe_unused]] std::size_t n_bg_io = decltype(r_bg_io)::size;
    [[maybe_unused]] std::size_t n_empty = EmptyRow::size;
}

}  // namespace detail::effect_row_self_test

}  // namespace crucible::effects
