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

#include <foundation/Platform.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::effects {

template <Effect... Es>
struct Row {
    static constexpr std::size_t size = sizeof...(Es);
};

using EmptyRow = Row<>;

// Each answer below about a row is a function at namespace scope that is
// not a template, or a concept that reads one, so no translation unit can
// specialize it.  A variable template or a class template in its place is
// a door: a specialization for a row that no header instantiates first
// gives that row an effect, and every context gate then admits a context
// that owns no such effect.
namespace detail {

// True when type reflects a specialization of Row, read through its
// aliases, with no qualifier.
[[nodiscard]] consteval bool is_row_specialization(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    return std::meta::has_template_arguments(dealiased) && std::meta::template_of(dealiased) == ^^Row;
}

// Declared and not defined, and not constexpr.  A constant evaluation
// that calls it fails, and the diagnostic gives its name as the reason.
void type_is_not_an_effect_row() noexcept;

}  // namespace detail

// True when the type is a row.  Top-level cv and reference are stripped,
// so that a concept fed a forwarding-reference deduction still recognizes
// the row.
[[nodiscard]] consteval bool is_effect_row(std::meta::info type) {
    return detail::is_row_specialization(std::meta::remove_cvref(std::meta::dealias(type)));
}

template <class T>
concept IsEffectRow = is_effect_row(^^T);

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

// The number of atoms that the row names, duplicates counted, as
// row_pack_size_v counts them.  The count comes from the template
// arguments of the row, so no member of the class plays a part.  A type
// that is not a row has no size, and a call for one is not a constant
// expression.
[[nodiscard]] consteval std::size_t row_size(std::meta::info row) {
    if (!detail::is_row_specialization(row)) detail::type_is_not_an_effect_row();
    return std::meta::template_arguments_of(std::meta::dealias(row)).size();
}

// True when the row names the effect.  A type that is not a row names no
// effect.  Complexity: linear in the size of the row.
[[nodiscard]] consteval bool row_contains(std::meta::info row, Effect effect) {
    if (!detail::is_row_specialization(row)) return false;
    for (const std::meta::info argument : std::meta::template_arguments_of(std::meta::dealias(row))) {
        if (std::meta::extract<Effect>(argument) == effect) return true;
    }
    return false;
}

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
    using keep_or_drop = std::conditional_t<row_contains(^^R2, E), Row<>, Row<E>>;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

template <typename R1, typename R2>
struct row_intersection_impl;

template <Effect... E1s, typename R2>
struct row_intersection_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<row_contains(^^R2, E), Row<E>, Row<>>;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

}  // namespace detail

template <typename R1, typename R2>
using row_union_t = canonical_row_t<typename detail::row_union_recursive<R1, R2>::type>;

template <typename R1, typename R2>
using row_difference_t = canonical_row_t<typename detail::row_difference_impl<R1, R2>::type>;

template <typename R1, typename R2>
using row_intersection_t = canonical_row_t<typename detail::row_intersection_impl<R1, R2>::type>;

// True when each atom of the narrow row is in the wide row.  A type that
// is not a row is no subrow and has no subrow.  Complexity: the product
// of the two sizes.
[[nodiscard]] consteval bool is_subrow(std::meta::info narrow, std::meta::info wide) {
    if (!detail::is_row_specialization(narrow) || !detail::is_row_specialization(wide)) return false;
    for (const std::meta::info argument : std::meta::template_arguments_of(std::meta::dealias(narrow))) {
        if (!row_contains(wide, std::meta::extract<Effect>(argument))) return false;
    }
    return true;
}

template <typename R1, typename R2>
concept Subrow = is_subrow(^^R1, ^^R2);

// The row of every atom the catalog declares, in declaration order, which
// is the canonical row order.  The list is written out, so that no
// includer walks the enum.  The check file of this header derives the row
// from the enum and pins this list to it, so a new atom cannot stay out.
using every_effect_row = Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>;

struct EffectRowLattice {
    using element_type = std::uint64_t;
    using atom_type = Effect;

    // A row bounds the effects that a computation can do.  A larger row
    // promises less and is the weaker claim.
    static constexpr ::foundation::algebra::ClaimOrientation claim_orientation =
        ::foundation::algebra::ClaimOrientation::weaker_is_higher;

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

// EffectMask is the runtime dual of the type-level `Row<Es...>`: a
// value-level set of Effect atoms that can be recorded, transmitted and
// compared against a declared row.
//
// The carrier is the lattice element, so a mask and the descriptor of a
// row are one encoding: bit n is the atom whose underlying value is n.
//
// Effect is position-encoded, so this newtype does the position-to-mask
// shift itself: `set(Effect::Bg)` sets bit 3.
//
// IsEffectRow strips cv-qualifiers and references, so the projections
// below take a qualified Row type as well as a bare one.
class [[nodiscard]] EffectMask {
public:
    using underlying_type = EffectRowLattice::element_type;

private:
    underlying_type bits_{0};

    struct from_raw_tag_t {};
    constexpr EffectMask(from_raw_tag_t, underlying_type b) noexcept : bits_{b} {}

    [[nodiscard]] static constexpr underlying_type bit_for(Effect e) noexcept { return EffectRowLattice::single(e); }

    // Effect occupies positions 0 through effect_count - 1, so the top of
    // the lattice is the full valid-bit mask.  A bit above that names no
    // atom.
    static constexpr underlying_type valid_mask = EffectRowLattice::top();

public:
    constexpr EffectMask() noexcept = default;

    constexpr EffectMask(EffectMask const&) = default;
    constexpr EffectMask(EffectMask&&) = default;
    constexpr EffectMask& operator=(EffectMask const&) = default;
    constexpr EffectMask& operator=(EffectMask&&) = default;
    ~EffectMask() = default;

    // A bit above the valid mask names no atom and is poison from a
    // buggy or hostile peer.  Without the mask it would round-trip back
    // out through `raw()`.
    //
    // The sanitizing AND must not be paired with a claim that it is a
    // no-op.  A precondition macro lowers to `[[assume(sanitized == b)]]`
    // under the ignore semantic, which lets the optimizer substitute `b`
    // for `sanitized`, leaving the AND dead and deleting it.  The debug
    // assertion carries no such assumption, so the AND survives every
    // build mode, and the `if consteval` branch keeps the compile-time
    // rejection.
    [[nodiscard]] static constexpr EffectMask from_raw(underlying_type b) noexcept {
        underlying_type const sanitized = b & valid_mask;
        // `__builtin_trap` is not a constant expression, so a
        // compile-time call with a poisoned input is a hard error.
        if consteval {
            if (sanitized != b) {
                __builtin_trap();
            }
        }
        CRUCIBLE_DEBUG_ASSERT(sanitized == b);
        return EffectMask{from_raw_tag_t{}, sanitized};
    }

    constexpr void set(Effect e) noexcept { bits_ = bits_ | bit_for(e); }
    constexpr void unset(Effect e) noexcept { bits_ = bits_ & ~bit_for(e); }
    constexpr void clear() noexcept { bits_ = 0; }

    [[nodiscard]] constexpr bool test(Effect e) const noexcept { return (bits_ & bit_for(e)) != underlying_type{0}; }
    [[nodiscard]] constexpr bool none() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr bool any() const noexcept { return bits_ != 0; }
    [[nodiscard]] constexpr int popcount() const noexcept { return std::popcount(bits_); }
    [[nodiscard]] constexpr underlying_type raw() const noexcept { return bits_; }

    [[nodiscard]] friend constexpr bool operator==(EffectMask, EffectMask) noexcept = default;

    [[nodiscard]] friend constexpr EffectMask operator|(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, a.bits_ | b.bits_};
    }
    [[nodiscard]] friend constexpr EffectMask operator&(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, a.bits_ & b.bits_};
    }
    [[nodiscard]] friend constexpr EffectMask operator^(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, a.bits_ ^ b.bits_};
    }
    // The complement stays inside the valid mask.  A plain bitwise NOT
    // would set every bit above the last atom, which is the poison that
    // from_raw refuses.
    [[nodiscard]] friend constexpr EffectMask operator~(EffectMask a) noexcept {
        return EffectMask{from_raw_tag_t{}, ~a.bits_ & valid_mask};
    }
};

template <Effect... Es>
[[nodiscard]] constexpr EffectMask bits_for() noexcept {
    EffectMask result{};
    (result.set(Es), ...);
    return result;
}

template <Effect... Es>
[[nodiscard]] constexpr EffectMask bits_from_row_pack(Row<Es...>) noexcept {
    return bits_for<Es...>();
}

template <IsEffectRow R>
[[nodiscard]] constexpr EffectMask bits_from_row() noexcept {
    return bits_from_row_pack(std::remove_cvref_t<R>{});
}

// A sample bit outside the declared row is effect drift.
template <IsEffectRow R>
[[nodiscard]] constexpr bool row_subsumes_bits(EffectMask sample) noexcept {
    auto row_bits = bits_from_row<R>();
    auto outside = sample & ~row_bits;
    return outside.none();
}

template <IsEffectRow R>
[[nodiscard]] constexpr bool bits_subsumes_row(EffectMask sample) noexcept {
    auto row_bits = bits_from_row<R>();
    auto missing = row_bits & ~sample;
    return missing.none();
}

}  // namespace foundation::effects
