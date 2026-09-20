// SPDX-License-Identifier: Apache-2.0
//
// The enumerator-name half of the reflection helpers, split out so that
// it names nothing outside <meta> and the standard library.
//
// Enumerate.h, which holds bits_to_string, states one precondition
// through foundation::decide, and that pulls contracts/Decide.h, which
// pulls effects/Row.h, which pulls effects/Effect.h.  A header inside
// that chain therefore could not read its own enumerator names: Effect.h
// and Modality.h each carried a hand-written name switch for exactly
// that reason.  This header has no such dependency, so both of them now
// read the identifier the enum already declares.
//
// Old spelling: include/crucible/safety/Reflected.h, whose name helpers
// arrived in include/foundation/reflect/Enumerate.h.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

namespace foundation::reflect {

// An unscoped enum converts to its underlying integer on its own, which
// would let a raw integer through the typed surface unannounced.

template <class E>
concept ScopedEnum = std::is_scoped_enum_v<E>;

// `f` takes the value and the name as ordinary runtime parameters, not
// as an NTTP `std::meta::info`.  `info` is consteval-only, so a lambda
// taking it as an NTTP is immediate-escalated and can no longer mutate
// runtime state such as an output buffer.

template <ScopedEnum E, typename F>
constexpr void for_each_enumerator(F&& f) {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E value = [:e:];
        constexpr std::string_view name = std::meta::identifier_of(e);
        f(value, name);
    }
#pragma GCC diagnostic pop
}

// The popcount filter sits here rather than in the lambda because once
// the value reaches the lambda as a runtime parameter it is no longer a
// constant expression, and `if constexpr` cannot see it.

template <ScopedEnum E, typename F>
constexpr void for_each_single_bit_enumerator(F&& f) {
    using U = std::underlying_type_t<E>;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E flag = [:e:];
        constexpr U raw = static_cast<U>(flag);
        if constexpr (std::popcount(static_cast<std::make_unsigned_t<U>>(raw)) == 1) {
            constexpr std::string_view name = std::meta::identifier_of(e);
            f(flag, name);
        }
    }
#pragma GCC diagnostic pop
}

// When two enumerators share a value, the first in declaration order
// wins.  The view points at compile-time-immortal storage.

template <ScopedEnum E>
[[nodiscard]] constexpr std::string_view enumerator_name(E value) noexcept {
    std::string_view result{};
    for_each_enumerator<E>([&](E candidate, std::string_view name) noexcept {
        if (result.empty() && candidate == value) {
            result = name;
        }
    });
    return result;
}

// The number of enumerators of E.  A count constant that a lattice
// header publishes derives from this, so the count and the enum cannot
// drift apart.

template <ScopedEnum E>
inline constexpr std::size_t enum_count = std::meta::enumerators_of(^^E).size();

namespace detail {

// The text is "<unknown E>" with the unqualified name of E.  It lives
// in static storage, so the view stays valid for the whole program.

template <ScopedEnum E>
[[nodiscard]] consteval std::string_view make_unknown_enum_sentinel() {
    std::string text{"<unknown "};
    text += std::meta::identifier_of(^^E);
    text += '>';
    return std::define_static_string(text);
}

}  // namespace detail

template <ScopedEnum E>
inline constexpr std::string_view unknown_enum_sentinel = detail::make_unknown_enum_sentinel<E>();

// The identifier of the enumerator that holds `value`, or the sentinel
// when no enumerator holds it.  This is enumerator_name with the empty
// result replaced, so a diagnostic never prints an empty name.  It is
// constexpr rather than consteval so that a runtime diagnostic can call
// it.

template <ScopedEnum E>
[[nodiscard]] constexpr std::string_view enum_name(E value) noexcept {
    const std::string_view found = enumerator_name(value);
    return found.empty() ? unknown_enum_sentinel<E> : found;
}

namespace detail::enum_name_self_test {

enum class TestFlags : std::uint8_t {
    Alpha = 0x01,
    Beta = 0x02,
    Gamma = 0x04,
    Delta = 0x08,
    AlphaBeta = 0x03,
    None = 0x00,
};

using TF = TestFlags;

inline constexpr auto name_alpha = enumerator_name(TF::Alpha);
static_assert(name_alpha == "Alpha");

inline constexpr auto name_gamma = enumerator_name(TF::Gamma);
static_assert(name_gamma == "Gamma");

inline constexpr auto name_alphabeta = enumerator_name(TF::AlphaBeta);
static_assert(name_alphabeta == "AlphaBeta");

[[nodiscard]] consteval bool composite_value_lookup_returns_empty() noexcept {
    constexpr auto raw = static_cast<TF>(static_cast<std::uint8_t>(TF::Alpha) | static_cast<std::uint8_t>(TF::Gamma));
    return enumerator_name(raw).empty();
}
static_assert(composite_value_lookup_returns_empty());

inline constexpr auto name_none = enumerator_name(TF::None);
static_assert(name_none == "None");

static_assert(enumerator_name(static_cast<TF>(0x03)) == "AlphaBeta");
static_assert(enumerator_name(static_cast<TF>(0xFF)).empty());

static_assert(enum_count<TF> == 6);

static_assert(unknown_enum_sentinel<TF> == "<unknown TestFlags>");
static_assert(enum_name(TF::Alpha) == "Alpha");
static_assert(enum_name(TF::AlphaBeta) == "AlphaBeta");
static_assert(enum_name(TF::None) == "None");
static_assert(enum_name(static_cast<TF>(0xFF)) == "<unknown TestFlags>");
static_assert(enum_name(static_cast<TF>(0xFF)) == unknown_enum_sentinel<TF>);

[[nodiscard]] consteval int count_enumerators() noexcept {
    int n = 0;
    for_each_enumerator<TF>([&](TF, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_enumerators() == 6);

[[nodiscard]] consteval int count_single_bit_enumerators() noexcept {
    int n = 0;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view) noexcept { ++n; });
    return n;
}
static_assert(count_single_bit_enumerators() == 4);

[[nodiscard]] consteval bool single_bit_iteration_yields_declaration_order() noexcept {
    char buf[64] = {};
    std::size_t pos = 0;
    bool first = true;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view name) noexcept {
        if (!first) buf[pos++] = ',';
        for (char c : name)
            buf[pos++] = c;
        first = false;
    });
    return std::string_view{buf, pos} == "Alpha,Beta,Gamma,Delta";
}
static_assert(single_bit_iteration_yields_declaration_order());

}  // namespace detail::enum_name_self_test

}  // namespace foundation::reflect
