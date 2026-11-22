// SPDX-License-Identifier: Apache-2.0
//
// The enumerator-name half of the reflection helpers, split out so that
// it names nothing outside <meta>, the standard library and
// foundation/reflect/Anchor.h.
//
// Enumerate.h, which holds bits_to_string, states one precondition
// through foundation::decide, and that pulls contracts/Decide.h, which
// pulls effects/Row.h, which pulls effects/Effect.h.  A header inside
// that chain cannot include Enumerate.h.  This header has no such
// dependency, so Effect.h and Modality.h read their enumerator names
// through it.
//
// Names
//   This header is on a lower layer than foundation/core/Text.h, so it
//   cannot name a TextView.  So it gives no name in a public form.  Its
//   detail functions give the characters of each name as a NameChars, and
//   foundation/core/Text.h gives each name as a TextView: enum_name_text,
//   enum_words_text, enumerator_name_text, unknown_enum_text and the walk
//   for_each_enumerator_text.  The two walks of this header,
//   for_each_enumerator_value and for_each_single_bit_enumerator_value,
//   give only the value of each enumerator.

#pragma once

#include <foundation/reflect/Anchor.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace foundation::reflect {

// An unscoped enum converts to its underlying integer on its own, which
// would let a raw integer through the typed surface unannounced.

template <class E>
concept ScopedEnum = std::is_scoped_enum_v<E>;

// `f` takes the value as an ordinary runtime parameter, not as an NTTP
// `std::meta::info`.  `info` is consteval-only, so a lambda taking it as an
// NTTP is immediate-escalated and can no longer mutate runtime state such as
// an output buffer.
//
// Each walk below holds its enumerators in a std::array, and no walk
// calls std::define_static_array.  A function that is not a template
// instantiates a walk where the function stands, for example a name
// function of one enum.  The first instantiation of define_static_array
// costs about 68 M instructions in a unit, and a std::array of a size
// that the enum gives costs almost nothing.  The size depends on E, so a
// unit that instantiates no walk evaluates no list.
//
// The two copies below read a std::vector for the same reason.  A list
// for each enum, with the reflection as a template parameter, adds about
// 88 M instructions to a unit that names one enum from such a function.

namespace detail {

// Copies the enumerators of the enum that `enumeration` reflects into the
// `count` slots at `target`, one slot for each of them.  The function is
// not a template, so no translation unit can specialize the list that a
// walk reads.  It takes a pointer and a count, and not a std::span: a
// std::span<std::meta::info> in a function that is not a template costs
// each includer about 32 M instructions.  Complexity: linear in the number
// of enumerators.
consteval void copy_enumerators(std::meta::info enumeration, std::meta::info* target, std::size_t count) {
    const std::vector<std::meta::info> found = std::meta::enumerators_of(enumeration);
    const std::meta::info* const source = found.data();
    for (std::size_t index = 0; index < count; ++index)
        target[index] = source[index];
}

// The position of the enumerator of the enum that `enumeration` reflects
// whose identifier is the `size` characters at `first`, or the count of the
// enumerators when the enum declares no such enumerator.  The function is
// not a template, so each search of each enum shares one body.  The text
// comes as a pointer and a count, so this header names no type of the core.
// foundation/core/Text.h gives the search a TextView (enumerator_named).
// Complexity: linear in the enumerators and the characters of the name.
consteval std::size_t enumerator_position(std::meta::info enumeration, char const* first, std::size_t size) {
    const std::vector<std::meta::info> found = std::meta::enumerators_of(enumeration);
    const std::string_view name{first, size};
    for (std::size_t index = 0; index < found.size(); ++index) {
        if (std::meta::identifier_of(found[index]) == name) return index;
    }
    return found.size();
}

// The enumerator of E at a position that enumerator_position gave.
template <ScopedEnum E>
[[nodiscard]] consteval E enumerator_at(std::size_t position) {
    return std::meta::extract<E>(std::meta::enumerators_of(^^E)[position]);
}

// The parts of a name of this header: its first character and its count of
// characters.  A zero follows the last character.  foundation/core/Text.h
// makes a TextView of the two parts, because a file of the language names
// no member of std::string_view.
struct NameChars final {
    char const* first = "";
    std::size_t size = 0;
};

// The name of the enumerator that a search found, or no name.
struct FoundName final {
    NameChars name{};
    bool is_found = false;
};

// The characters of the identifier of the entity that `entity` reflects.  The
// identifier of a reflection has a zero after its last character.  The
// function is not a template, so each walk and each sentinel of each enum
// share one body.  Complexity: constant.
consteval NameChars read_identifier_chars(std::meta::info entity) {
    const std::string_view name = std::meta::identifier_of(entity);
    return NameChars{name.data(), name.size()};
}

// Calls f(value, name) for each enumerator of E in declaration order, with
// the characters of the identifier of the enumerator.  The identifier of a
// reflection has a zero after its last character.  foundation/core/Text.h
// gives the name to the caller as a TextView (for_each_enumerator_text).
template <ScopedEnum E, typename F>
constexpr void walk_enumerator_names(F&& f) {
    static constexpr auto enumerators = [] consteval {
        std::array<std::meta::info, std::meta::enumerators_of(^^E).size()> items{};
        copy_enumerators(^^E, items.data(), items.size());
        return items;
    }();
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E value = [:e:];
        constexpr NameChars name = read_identifier_chars(e);
        f(value, name);
    }
#pragma GCC diagnostic pop
}

// The same walk over the enumerators whose value has one bit set, for
// bits_to_string of foundation/reflect/Enumerate.h.  The popcount filter
// sits here rather than in the callback because once the value reaches the
// callback as a runtime parameter it is no longer a constant expression, and
// `if constexpr` cannot see it.
template <ScopedEnum E, typename F>
constexpr void walk_single_bit_enumerator_names(F&& f) {
    using U = std::underlying_type_t<E>;
    static constexpr auto enumerators = [] consteval {
        std::array<std::meta::info, std::meta::enumerators_of(^^E).size()> items{};
        copy_enumerators(^^E, items.data(), items.size());
        return items;
    }();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr E flag = [:e:];
        constexpr U raw = static_cast<U>(flag);
        if constexpr (std::popcount(static_cast<std::make_unsigned_t<U>>(raw)) == 1) {
            constexpr NameChars name = read_identifier_chars(e);
            f(flag, name);
        }
    }
#pragma GCC diagnostic pop
}

}  // namespace detail

// Calls f(value) for each enumerator of E, in declaration order.  The walk
// gives no name: for_each_enumerator_text of foundation/core/Text.h gives
// each value with its name.
template <ScopedEnum E, typename F>
constexpr void for_each_enumerator_value(F&& f) {
    detail::walk_enumerator_names<E>([&f](E const value, detail::NameChars) { f(value); });
}

// Calls f(flag) for each enumerator of E whose value has one bit set, in
// declaration order.
template <ScopedEnum E, typename F>
constexpr void for_each_single_bit_enumerator_value(F&& f) {
    detail::walk_single_bit_enumerator_names<E>([&f](E flag, detail::NameChars) { f(flag); });
}

// The number of enumerators of E.  A count constant that a lattice
// header publishes derives from this, so the count and the enum cannot
// drift apart.

template <ScopedEnum E>
inline constexpr std::size_t enum_count = std::meta::enumerators_of(^^E).size();

namespace detail {

// The text is "<unknown E>" with the unqualified name of E.  It lives
// in a static array of its own length and one zero, so the characters stay
// valid for the whole program.  A zero follows each name of this header,
// as it follows the text of a reflection, so a TextView of the base can
// borrow each one (foundation/core/Text.h).  A name function of one enum
// instantiates the sentinel where the function stands, and the array costs
// almost nothing there.  A call of std::define_static_string would
// instantiate that function.

template <ScopedEnum E>
[[nodiscard]] consteval NameChars make_unknown_enum_chars() {
    static constexpr char prefix[] = "<unknown ";
    static constexpr std::size_t prefix_size = sizeof(prefix) - 1;
    static constexpr NameChars name = read_identifier_chars(^^E);
    static constexpr std::size_t name_size = name.size;
    struct SentinelChars {
        char bytes[prefix_size + name_size + 2]{};
    };
    static constexpr SentinelChars text = [] consteval {
        SentinelChars chars{};
        char const* const tail = name.first;
        for (std::size_t index = 0; index < prefix_size; ++index)
            chars.bytes[index] = prefix[index];
        for (std::size_t index = 0; index < name_size; ++index)
            chars.bytes[prefix_size + index] = tail[index];
        chars.bytes[prefix_size + name_size] = '>';
        return chars;
    }();
    return NameChars{text.bytes, prefix_size + name_size + 1};
}

// The characters of the sentinel of E.
template <ScopedEnum E>
inline constexpr NameChars unknown_enum_chars = make_unknown_enum_chars<E>();

// The characters of the identifier of the first enumerator, in declaration
// order, that holds `value`, or no characters when no enumerator holds it.
// An identifier is never empty, so the count zero marks the value of no
// enumerator, and the search keeps two words and no flag.  Complexity: one
// compare for each enumerator.
template <ScopedEnum E>
[[nodiscard]] constexpr NameChars first_name_chars_(E value) noexcept {
    NameChars found{};
    walk_enumerator_names<E>([&found, value](E candidate, NameChars name) noexcept {
        if (found.size == 0 && candidate == value) {
            found = name;
        }
    });
    return found;
}

// The identifier of the enumerator that holds `value`, or no name when no
// enumerator holds it.  When two enumerators share a value, the first in
// declaration order wins.  It is constexpr rather than consteval so that a
// runtime diagnostic can call it.
template <ScopedEnum E>
[[nodiscard]] constexpr FoundName enumerator_chars(E value) noexcept {
    NameChars const found = first_name_chars_(value);
    return FoundName{found, found.size != 0};
}

// The identifier of the enumerator that holds `value`, or the sentinel when
// no enumerator holds it, so a diagnostic never prints an empty name.
template <ScopedEnum E>
[[nodiscard]] constexpr NameChars enum_name_chars(E value) noexcept {
    NameChars const found = first_name_chars_(value);
    return found.size != 0 ? found : unknown_enum_chars<E>;
}

// True when the two names hold the same characters in the same order.  The
// two names can swap with no change of the answer.  Complexity: linear in
// the count of the characters.
[[nodiscard]] consteval bool same_chars_(NameChars left, NameChars right) noexcept {
    if (left.size != right.size) return false;
    for (std::size_t index = 0; index < left.size; ++index) {
        if (left.first[index] != right.first[index]) return false;
    }
    return true;
}

// True when the identifier of the entity that `entity` reflects spells the
// name of the enumerator that holds value, for a reader of reflection that
// compares an identifier, such as the bare tag of an effect in
// foundation/effects/Effect.h.  A value that no enumerator holds matches no
// identifier.  Complexity: linear in the enumerators and the characters of
// the identifier.
template <ScopedEnum E>
[[nodiscard]] consteval bool is_enumerator_identifier(std::meta::info entity, E value) {
    FoundName const found = enumerator_chars(value);
    return found.is_found && same_chars_(read_identifier_chars(entity), found.name);
}

// The characters of the identifier in lower case, with Separator between
// words.  An upper-case letter that follows a lower-case letter or a digit
// starts a new word.  So CopyHostToDevice reads copy_host_to_device with '_',
// and SocketOracle reads socket-oracle with '-'.  The text lives in static
// storage, with a zero after its last character.  Complexity: linear in the
// length of the identifier.  The function is a template only so that its
// text can depend on Anchor (foundation/reflect/Anchor.h).
template <char Separator, class Anchor = void>
[[nodiscard]] consteval NameChars lower_words_of(NameChars identifier) {
    anchored_t<^^Anchor, std::string> words;
    for (std::size_t index = 0; index < identifier.size; ++index) {
        const char letter = identifier.first[index];
        const bool is_upper = letter >= 'A' && letter <= 'Z';
        if (is_upper && index > 0) {
            const char previous = identifier.first[index - 1];
            const bool ends_word = (previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9');
            if (ends_word) words += Separator;
        }
        words += is_upper ? static_cast<char>(letter - 'A' + 'a') : letter;
    }
    return NameChars{std::define_static_string(words), words.size()};
}

// The identifier of the enumerator that holds `value`, in lower-case words
// joined by Separator, or the sentinel when no enumerator holds it.  When
// two enumerators share a value, the first in declaration order wins, as in
// enumerator_chars.
template <ScopedEnum E, char Separator>
[[nodiscard]] constexpr NameChars enum_words_chars(E value) noexcept {
    static constexpr auto enumerators = [] consteval {
        std::array<std::meta::info, std::meta::enumerators_of(^^E).size()> items{};
        copy_enumerators(^^E, items.data(), items.size());
        return items;
    }();
    NameChars words = unknown_enum_chars<E>;
    bool is_found = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto e : enumerators) {
        constexpr NameChars spelled = lower_words_of<Separator>(read_identifier_chars(e));
        if (!is_found && value == [:e:]) {
            words = spelled;
            is_found = true;
        }
    }
#pragma GCC diagnostic pop
    return words;
}

// The identifier of Entity.  Its return type is deduced, so a call of it
// has a type that depends on Entity (foundation/reflect/Anchor.h).
template <std::meta::info Entity>
[[nodiscard]] consteval auto identifier_text_() {
    return std::meta::identifier_of(Entity);
}

// The parts of the identifier of a declared entity, such as a data member.
// The characters have static storage, so a run-time reader can keep them.
// The text has a type that depends on Entity, so only a unit that names the
// identifier instantiates the static string.
template <std::meta::info Entity>
[[nodiscard]] consteval NameChars identifier_chars() {
    char const* const first = std::define_static_string(detail::identifier_text_<Entity>());
    std::size_t size = 0;
    while (first[size] != '\0') {
        ++size;
    }
    return NameChars{first, size};
}

}  // namespace detail

}  // namespace foundation::reflect
