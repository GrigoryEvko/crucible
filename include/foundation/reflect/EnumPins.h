// SPDX-License-Identifier: Apache-2.0
//
// A pin table fixes the underlying value of each enumerator of one enum,
// and pin_enum compares the table with the enum by reflection.
//
// An enum is pinned when its values are part of a format: a cache key
// folds them, or a stored or transmitted record carries them.  A new
// enumerator then takes the next free value and extends its table in the
// same change, and a renumber fails the build until the table moves with
// it, which makes the renumber a visible format migration.
//
// The table is written by hand and never derived from the enum, because a
// derived table agrees with every renumber and pins nothing.

#pragma once

#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::reflect {

// One expected enumerator: the identifier as it is written, and the value
// the format pins it to.  The value has the underlying type of E, so a
// value that E cannot hold is a narrowing error in the table itself.
template <ScopedEnum E>
struct enum_pin {
    std::string_view name;
    std::underlying_type_t<E> value;
};

// True when the enumerators of E are exactly the entries of `expected`.
// The table deduces E, so a table cannot be checked against another enum.
//
// The walk runs over the enum and looks each enumerator up by identifier,
// so a renamed enumerator and a moved value are both caught.  The size
// comparison catches the other direction, an entry that no enumerator
// answers to, which the walk alone cannot see.  The distinctness pass
// catches a table that names one enumerator twice, which would leave a
// second entry unread while the sizes still agreed.
//
// The table and the enum hold at most a few dozen entries, so the
// quadratic passes cost nothing at compile time.
template <ScopedEnum E, std::size_t N>
[[nodiscard]] consteval bool pin_enum(std::array<enum_pin<E>, N> const& expected) noexcept {
    using Underlying = std::underlying_type_t<E>;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
    if (enumerators.size() != N) return false;

    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (expected[i].name == expected[j].name) return false;
        }
    }

    bool pinned = true;
// An expansion statement unrolls into successive scopes that each
// declare the same induction variable, so -Wshadow fires once per
// iteration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr std::string_view name = std::meta::identifier_of(en);
        constexpr auto value = static_cast<Underlying>([:en:]);
        bool matched = false;
        for (const enum_pin<E>& pin : expected) {
            if (pin.name == name) {
                matched = pin.value == value;
                break;
            }
        }
        pinned = pinned && matched;
    }
#pragma GCC diagnostic pop
    return pinned;
}

}  // namespace foundation::reflect
