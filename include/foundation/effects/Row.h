#pragma once

// A row is the set of effect atoms a computation may exercise, spelled
// two ways that must always agree.
//
// Row<Es...> is the type-level spelling: the atoms are template
// arguments, membership and subset are folds over the pack, and the
// canonical form sorts by underlying value so that two spellings of one
// set are one type.  Every signature that names a row names this form.
//
// EffectRowLattice is the value-level spelling: the same set as a
// 64-bit mask, so that a grade can be compared at runtime inside an
// enforced precondition, where a consteval-only comparison would not
// instantiate.  Every operation is one bitwise primitive with the same
// meaning at compile time and at runtime.  It is the first Row in the
// sense of foundation/algebra/Lattice.h: a bounded lattice whose
// elements are built from atoms and asked whether they hold one.
//
// row_descriptor_v is the one-way projection from the first spelling to
// the second.  Code that must keep the atom pack itself keeps naming
// Row<Es...>.

#include <foundation/algebra/Lattice.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::effects {

template <Effect... Es>
struct Row {
    static constexpr std::size_t size = sizeof...(Es);
};

using EmptyRow = Row<>;

// Top-level cv and reference are stripped before matching, so that a
// concept fed a forwarding-reference deduction still recognizes the
// row.  Every recognition trait in the project behaves this way.
template <class T>
struct is_effect_row : std::false_type {};
template <Effect... Es>
struct is_effect_row<Row<Es...>> : std::true_type {};
template <class T>
inline constexpr bool is_effect_row_v = is_effect_row<std::remove_cvref_t<T>>::value;
template <class T>
concept IsEffectRow = is_effect_row_v<T>;

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

namespace detail {

// The complete atom list, substituted into whichever template asks for
// it.  Row and EffectRowLattice::At both take a pack of Effect
// non-type arguments, so one walk serves both.
//
// The atoms arrive in declaration order, which for this enum is also
// sorted underlying-value order, and that is the canonical row order
// this header defines.
[[nodiscard]] consteval std::meta::info every_effect_of_(std::meta::info tmpl) {
    std::vector<std::meta::info> atoms;
    for (const auto enumerator : std::meta::enumerators_of(^^Effect)) {
        atoms.push_back(std::meta::constant_of(enumerator));
    }
    return std::meta::substitute(tmpl, atoms);
}

}  // namespace detail

// The row of every atom the catalog declares.  Nine places used to
// spell the six atoms out, and each of those was a copy that a new atom
// would silently leave behind at five atoms.
using every_effect_row = [:detail::every_effect_of_(^^Row):];

static_assert(row_size_v<every_effect_row> == effect_count,
              "every_effect_row must hold one atom for each Effect enumerator.  It is substituted from "
              "enumerators_of, so a mismatch means a duplicate enumerator value collapsed two atoms into "
              "one row bit.");

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

using R_universe = every_effect_row;
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

static_assert(effect_count <= 64, "The EffectRowLattice carrier is uint64_t.  Extend it to a wider carrier "
                                  "(uint128 or std::bitset<effect_count>) before adding a 65th Effect atom.");

// The cardinality guard above is not enough.  Atom values are assigned
// by hand, so a pack of seven atoms could still spell one of them 100
// and shift past the carrier width.  This check reads the underlying
// value of every atom instead of counting them.
[[nodiscard]] consteval bool every_effect_underlying_lt_64() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
    // -Wshadow fires spuriously on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (static_cast<std::uint64_t>([:en:]) >= 64) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_effect_underlying_lt_64(),
              "At least one Effect atom has an underlying value >= 64, so the "
              "EffectRowLattice::At<...>::value bit-shift is undefined.  Give every atom the next free "
              "position below 64, or migrate the carrier first.");

struct EffectRowLattice {
    using element_type = std::uint64_t;
    using atom_type = Effect;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }

    [[nodiscard]] static constexpr element_type top() noexcept {
        return (effect_count == 0) ? element_type{0} : ((element_type{1} << effect_count) - element_type{1});
    }

    // Subset test.  Every bit of a is also in b exactly when a & ~b is
    // empty.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return (a & ~b) == 0; }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a | b; }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a & b; }

    // The row containing one atom, and the membership test.  These two
    // are what make the mask a Row and not just a bounded lattice.
    [[nodiscard]] static constexpr element_type single(Effect atom) noexcept {
        return element_type{1} << static_cast<std::uint8_t>(atom);
    }
    [[nodiscard]] static constexpr bool contains(element_type row, Effect atom) noexcept {
        return (row & single(atom)) != 0;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "EffectRow"; }

    // A singleton sub-lattice whose one element is the pinned atom set.
    // Every operation returns that element, so the lattice axioms are
    // satisfied without inspecting anything, and the grade can live in
    // the type instead of in a runtime field.
    template <Effect... Atoms>
    struct At {
        static constexpr std::uint64_t value =
            ((std::uint64_t{1} << static_cast<std::uint8_t>(Atoms)) | ... | std::uint64_t{0});

        struct element_type {
            constexpr element_type() noexcept = default;
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static_assert(std::is_empty_v<element_type>,
                      "At<Atoms...>::element_type must be empty so that the grade slot of the "
                      "grading substrate collapses to 0 bytes.");

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept { return "EffectRow::At"; }

        [[nodiscard]] static constexpr std::uint64_t bits() noexcept { return value; }
    };
};

// A type carrying no row maps to the empty mask, which is below every
// descriptor.  That answer is safe rather than meaningful: such a type
// has no business being queried.
template <typename R>
inline constexpr EffectRowLattice::element_type row_descriptor_v = 0;

template <Effect... Es>
inline constexpr EffectRowLattice::element_type row_descriptor_v<Row<Es...>> =
    ((EffectRowLattice::element_type{1} << static_cast<std::uint8_t>(Es)) | ... | EffectRowLattice::element_type{0});

static_assert(::foundation::algebra::Lattice<EffectRowLattice>);
static_assert(::foundation::algebra::Row<EffectRowLattice>, "EffectRowLattice is the first Row; if this fires, "
                                                            "atom_type, single or contains has drifted.");
static_assert(::foundation::algebra::BoundedBelowLattice<EffectRowLattice>);
static_assert(::foundation::algebra::BoundedAboveLattice<EffectRowLattice>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice>);

static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<>>);
static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);

static_assert(std::is_empty_v<EffectRowLattice::At<>::element_type>);
static_assert(std::is_empty_v<EffectRowLattice::At<Effect::Alloc>::element_type>);
static_assert(std::is_empty_v<EffectRowLattice::At<Effect::Alloc, Effect::IO>::element_type>);

static_assert(EffectRowLattice::At<>::bits() == 0);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits()
              == (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::Alloc)));
static_assert(EffectRowLattice::At<Effect::IO>::bits() == (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::IO)));
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == EffectRowLattice::join(EffectRowLattice::At<Effect::Alloc>::bits(),
                                        EffectRowLattice::At<Effect::IO>::bits()));

static_assert(EffectRowLattice::At<>::bits() == row_descriptor_v<Row<>>);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits() == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == EffectRowLattice::At<Effect::IO, Effect::Alloc>::bits());

namespace detail {
// The primary template is left undefined so that a non-Row argument
// fails as an incomplete type at the alias.  A defined fallback would
// hand the caller a grade that silently means nothing.
template <typename R>
struct effect_row_to_at;

template <Effect... Es>
struct effect_row_to_at<Row<Es...>> {
    using type = EffectRowLattice::template At<Es...>;
};
}  // namespace detail

template <typename R>
using effect_row_to_at_t = typename detail::effect_row_to_at<R>::type;

static_assert(std::is_same_v<effect_row_to_at_t<Row<>>, EffectRowLattice::At<>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc>>, EffectRowLattice::At<Effect::Alloc>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Bg>>, EffectRowLattice::At<Effect::Bg>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);
// Both sides come from the same walk over the enum, so what this pins
// is that the conversion carries every atom across rather than that two
// hand-written lists agree.
static_assert(
    std::is_same_v<effect_row_to_at_t<every_effect_row>, typename[:detail::every_effect_of_(^^EffectRowLattice::At):]>);

static_assert(effect_row_to_at_t<Row<>>::bits() == row_descriptor_v<Row<>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc>>::bits() == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);

namespace detail::effect_row_lattice_self_test {

using L = EffectRowLattice;
using EL = L::element_type;

inline constexpr EL e_bot = L::bottom();
inline constexpr EL e_alloc = EL{1} << static_cast<std::uint8_t>(Effect::Alloc);
inline constexpr EL e_io = EL{1} << static_cast<std::uint8_t>(Effect::IO);
inline constexpr EL e_block = EL{1} << static_cast<std::uint8_t>(Effect::Block);
inline constexpr EL e_bg = EL{1} << static_cast<std::uint8_t>(Effect::Bg);
inline constexpr EL e_init = EL{1} << static_cast<std::uint8_t>(Effect::Init);
inline constexpr EL e_test = EL{1} << static_cast<std::uint8_t>(Effect::Test);
inline constexpr EL e_top = L::top();

static_assert(L::leq(e_bot, e_top));
static_assert(!L::leq(e_top, e_bot));

static_assert(L::leq(e_bot, e_alloc));
static_assert(L::leq(e_alloc, e_top));
static_assert(!L::leq(e_alloc, e_io));
static_assert(!L::leq(e_io, e_alloc));

static_assert(L::join(e_alloc, e_io) == (e_alloc | e_io));
static_assert(L::join(e_alloc, e_bot) == e_alloc);
static_assert(L::join(e_alloc, e_top) == e_top);

static_assert(L::meet(e_alloc | e_io, e_io | e_block) == e_io);
static_assert(L::meet(e_alloc, e_bot) == e_bot);
static_assert(L::meet(e_alloc, e_top) == e_alloc);

static_assert((e_top & e_alloc) == e_alloc);
static_assert((e_top & e_io) == e_io);
static_assert((e_top & e_block) == e_block);
static_assert((e_top & e_bg) == e_bg);
static_assert((e_top & e_init) == e_init);
static_assert((e_top & e_test) == e_test);

static_assert(L::top() == ((EL{1} << effect_count) - EL{1}));

static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_bot, e_bot));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_alloc | e_io));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc, e_io, e_block));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc | e_io, e_io | e_block,
                                                                         e_block | e_bg));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_top));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_top, e_top, e_top));

static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_alloc, e_io, e_block));
static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_bot, e_alloc, e_top));

static_assert(row_descriptor_v<Row<>> == e_bot);
static_assert(row_descriptor_v<Row<Effect::Alloc>> == e_alloc);
static_assert(row_descriptor_v<Row<Effect::IO>> == e_io);
static_assert(row_descriptor_v<Row<Effect::Block>> == e_block);
static_assert(row_descriptor_v<Row<Effect::Bg>> == e_bg);
static_assert(row_descriptor_v<Row<Effect::Init>> == e_init);
static_assert(row_descriptor_v<Row<Effect::Test>> == e_test);

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>> == (e_alloc | e_io));
static_assert(row_descriptor_v<Row<Effect::Block, Effect::Alloc, Effect::IO>> == (e_block | e_alloc | e_io));

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>> == row_descriptor_v<Row<Effect::IO, Effect::Alloc>>);

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::Alloc>> == row_descriptor_v<Row<Effect::Alloc>>);

static_assert(row_descriptor_v<every_effect_row> == e_top);

static_assert(row_descriptor_v<int> == 0);
static_assert(row_descriptor_v<double> == 0);

static_assert(L::single(Effect::Alloc) == e_alloc);
static_assert(L::single(Effect::Test) == e_test);
static_assert(L::contains(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, Effect::IO));
static_assert(!L::contains(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, Effect::Bg));
static_assert(L::contains(e_top, Effect::Init));
static_assert(!L::contains(e_bot, Effect::Init));
static_assert(L::contains(row_descriptor_v<Row<Effect::Bg>>, Effect::Bg)
              == row_contains_v<Row<Effect::Bg>, Effect::Bg>);
static_assert(L::contains(row_descriptor_v<Row<Effect::Bg>>, Effect::IO)
              == row_contains_v<Row<Effect::Bg>, Effect::IO>);

// The membership-based subrow test and the bitmask-based order must
// give the same answer at every query site.  A disagreement would let
// the two surfaces classify the same pair of rows differently.
static_assert(L::leq(row_descriptor_v<Row<>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<>, Row<Effect::Alloc>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc>>, row_descriptor_v<Row<Effect::Alloc, Effect::IO>>)
              == is_subrow_v<Row<Effect::Alloc>, Row<Effect::Alloc, Effect::IO>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::Alloc, Effect::IO>, Row<Effect::Alloc>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::IO>, Row<Effect::Alloc>>);

}  // namespace detail::effect_row_lattice_self_test

// Every accessor is called here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path.
inline void runtime_smoke_test_lattice() noexcept {
    using L = EffectRowLattice;
    using EL = L::element_type;

    [[maybe_unused]] EL b = L::bottom();
    [[maybe_unused]] EL t = L::top();

    [[maybe_unused]] bool ok_le = L::leq(b, t);
    [[maybe_unused]] EL u = L::join(b, t);
    [[maybe_unused]] EL i = L::meet(b, t);

    [[maybe_unused]] auto nm = ::foundation::algebra::lattice_name<L>();

    [[maybe_unused]] EL d_pure = row_descriptor_v<Row<>>;
    [[maybe_unused]] EL d_alloc = row_descriptor_v<Row<Effect::Alloc>>;

    Effect atom = ok_le ? Effect::IO : Effect::Alloc;  // deliberately not constexpr
    [[maybe_unused]] EL one = L::single(atom);
    [[maybe_unused]] bool holds = L::contains(L::join(d_alloc, one), atom);

    using AtAlloc = L::template At<Effect::Alloc>;
    using AtAB = L::template At<Effect::Alloc, Effect::IO>;

    [[maybe_unused]] auto at_b = AtAlloc::bottom();
    [[maybe_unused]] auto at_t = AtAlloc::top();
    [[maybe_unused]] bool at_le = AtAlloc::leq(at_b, at_t);
    [[maybe_unused]] auto at_join = AtAlloc::join(at_b, at_t);
    [[maybe_unused]] auto at_meet = AtAlloc::meet(at_b, at_t);
    [[maybe_unused]] auto at_nm = AtAlloc::name();
    [[maybe_unused]] std::uint64_t at_bits = AtAlloc::bits();
    [[maybe_unused]] std::uint64_t ab_bits = AtAB::bits();
}

}  // namespace foundation::effects
