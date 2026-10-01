// SPDX-License-Identifier: Apache-2.0
//
// The enumerator-name half of the reflection helpers, split out so that
// it names nothing outside <meta> and the standard library.
//
// Enumerate.h, which holds bits_to_string, states one precondition
// through foundation::decide, and that pulls contracts/Decide.h, which
// pulls effects/Row.h, which pulls effects/Effect.h.  A header inside
// that chain cannot include Enumerate.h.  This header has no such
// dependency, so Effect.h and Modality.h read their enumerator names
// through it.

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

namespace detail {

// The identifier in lower case, with the separator between words.  An
// upper-case letter that follows a lower-case letter or a digit starts a
// new word.  So CopyHostToDevice reads copy_host_to_device with '_', and
// SocketOracle reads socket-oracle with '-'.  The text lives in static
// storage.  Complexity: linear in the length of the identifier.
[[nodiscard]] consteval std::string_view lower_words_of(std::string_view identifier, char separator) {
    std::string words;
    for (std::size_t index = 0; index < identifier.size(); ++index) {
        const char letter = identifier[index];
        const bool is_upper = letter >= 'A' && letter <= 'Z';
        if (is_upper && index > 0) {
            const char previous = identifier[index - 1];
            const bool ends_word = (previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9');
            if (ends_word) words += separator;
        }
        words += is_upper ? static_cast<char>(letter - 'A' + 'a') : letter;
    }
    return std::define_static_string(words);
}

}  // namespace detail

// The identifier of the enumerator that holds `value`, in lower-case words
// joined by Separator, or the sentinel when no enumerator holds it.  When
// two enumerators share a value, the first in declaration order wins, as
// in enumerator_name.
template <ScopedEnum E, char Separator>
[[nodiscard]] constexpr std::string_view enum_words(E value) noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    std::string_view words = unknown_enum_sentinel<E>;
    bool is_found = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr std::string_view spelled = detail::lower_words_of(std::meta::identifier_of(e), Separator);
        if (!is_found && value == [:e:]) {
            words = spelled;
            is_found = true;
        }
    }
#pragma GCC diagnostic pop
    return words;
}

}  // namespace foundation::reflect
