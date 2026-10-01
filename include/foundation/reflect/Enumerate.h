// SPDX-License-Identifier: Apache-2.0
//
// bits_to_string renders a flag word as pipe-separated enumerator names.
//
// The ScopedEnum concept and the name helpers this builds on live in
// foundation/reflect/EnumName.h.  The precondition below names
// foundation::decide, and contracts/Decide.h reaches effects/Row.h and
// so effects/Effect.h.  A header inside that chain cannot include this
// one, and EnumName.h carries no such dependency.
//
// bits_to_string takes the raw std::underlying_type_t<E> mask rather
// than a Bits<E>, because Bits is a fixy wrapper header that foundation
// cannot name, and tests a flag with
// `(mask & static_cast<U>(flag)) != U{0}` as Bits::test does.

#pragma once

#include <foundation/contracts/Decide.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace foundation::reflect {

// The return value follows the snprintf convention: it counts the
// characters the full string would need, excluding the NUL, so a return
// of cap or more means the output was truncated.  A cap of 0 writes
// nothing and only reports that count, which is why `out` may be null
// in that one case.

template <ScopedEnum E>
[[nodiscard]] constexpr size_t bits_to_string(std::underlying_type_t<E> mask, char* out, size_t cap) noexcept
    pre(::foundation::decide::valid_span(cap, out)) {
    using U = std::underlying_type_t<E>;
    size_t needed = 0;
    bool first = true;

    auto emit_byte = [&](char c) noexcept {
        if (cap > 0 && needed + 1 < cap) out[needed] = c;
        ++needed;
    };
    auto emit_view = [&](std::string_view s) noexcept {
        for (char c : s)
            emit_byte(c);
    };

    for_each_single_bit_enumerator<E>([&](E flag, std::string_view name) noexcept {
        if ((mask & static_cast<U>(flag)) != U{0}) {
            if (!first) emit_byte('|');
            emit_view(name);
            first = false;
        }
    });

    if (cap > 0) {
        size_t nul_pos = needed < cap ? needed : (cap - 1);
        out[nul_pos] = '\0';
    }
    return needed;
}

}  // namespace foundation::reflect
