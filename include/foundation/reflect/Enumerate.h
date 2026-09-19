// SPDX-License-Identifier: Apache-2.0
//
// This header defines the ScopedEnum concept, because its old home,
// crucible/safety/Bits.h, is a fixy wrapper header that foundation cannot
// name.  For the same reason bits_to_string takes the raw
// std::underlying_type_t<E> mask rather than a Bits<E>, and tests a flag
// with `(mask & static_cast<U>(flag)) != U{0}` as Bits::test does.
//
// Old spelling: include/crucible/safety/Reflected.h.

#pragma once

#include <foundation/contracts/Decide.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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

namespace detail::reflected_self_test {

enum class TestFlags : std::uint8_t {
    Alpha = 0x01,
    Beta = 0x02,
    Gamma = 0x04,
    Delta = 0x08,
    AlphaBeta = 0x03,
    None = 0x00,
};

using TF = TestFlags;
using U = std::underlying_type_t<TF>;

// The word that a Bits<TF>{flags...} held: the enumerators' values ORed.
[[nodiscard]] constexpr U mask_of(std::initializer_list<TF> flags) noexcept {
    U acc = 0;
    for (TF f : flags) {
        acc = static_cast<U>(acc | static_cast<U>(f));
    }
    return acc;
}

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

[[nodiscard]] consteval bool empty_bits_writes_empty_string() noexcept {
    char buf[16] = {};
    U b = 0;
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 0 && buf[0] == '\0';
}
static_assert(empty_bits_writes_empty_string());

[[nodiscard]] consteval bool single_flag_writes_name() noexcept {
    char buf[16] = {};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return n == 5 && std::string_view{buf} == "Alpha";
}
static_assert(single_flag_writes_name());

[[nodiscard]] consteval bool multi_flag_writes_pipe_separated() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Gamma, TF::Delta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Gamma|Delta" && n == std::string_view{buf}.size();
}
static_assert(multi_flag_writes_pipe_separated());

[[nodiscard]] consteval bool composite_enumerator_is_skipped() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta" && n == std::string_view{buf}.size();
}
static_assert(composite_enumerator_is_skipped());

[[nodiscard]] consteval bool zero_enumerator_is_skipped() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha" && n == 5;
}
static_assert(zero_enumerator_is_skipped());

[[nodiscard]] consteval bool truncation_returns_needed_length() noexcept {
    char buf[8] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|B") return false;
    return buf[7] == '\0';
}
static_assert(truncation_returns_needed_length());

[[nodiscard]] consteval bool zero_capacity_probes_size() noexcept {
    U b = mask_of({TF::Alpha, TF::Beta, TF::Gamma});
    auto n = bits_to_string<TF>(b, nullptr, 0);
    return n == std::string_view{"Alpha|Beta|Gamma"}.size();
}
static_assert(zero_capacity_probes_size());

[[nodiscard]] consteval bool unit_capacity_writes_nul_only() noexcept {
    char buf[1] = {'X'};
    U b = mask_of({TF::Alpha});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return buf[0] == '\0' && n == 5;
}
static_assert(unit_capacity_writes_nul_only());

[[nodiscard]] consteval bool exact_fit_capacity_no_truncation() noexcept {
    char buf[11] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|Beta") return false;
    return buf[10] == '\0';
}
static_assert(exact_fit_capacity_no_truncation());

[[nodiscard]] consteval bool one_byte_short_truncates_one_char() noexcept {
    char buf[10] = {};
    U b = mask_of({TF::Alpha, TF::Beta});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    if (n != 10) return false;
    if (std::string_view{buf} != "Alpha|Bet") return false;
    return buf[9] == '\0';
}
static_assert(one_byte_short_truncates_one_char());

[[nodiscard]] consteval bool all_flags_set_emits_every_single_bit() noexcept {
    char buf[64] = {};
    U b = mask_of({TF::Alpha, TF::Beta, TF::Gamma, TF::Delta, TF::AlphaBeta, TF::None});
    auto n = bits_to_string<TF>(b, buf, sizeof(buf));
    return std::string_view{buf} == "Alpha|Beta|Gamma|Delta" && n == std::string_view{buf}.size();
}
static_assert(all_flags_set_emits_every_single_bit());

[[nodiscard]] consteval bool deterministic_output() noexcept {
    char buf1[64] = {};
    char buf2[64] = {};
    U b = mask_of({TF::Beta, TF::Delta});
    auto n1 = bits_to_string<TF>(b, buf1, sizeof(buf1));
    auto n2 = bits_to_string<TF>(b, buf2, sizeof(buf2));
    return n1 == n2 && std::string_view{buf1} == std::string_view{buf2};
}
static_assert(deterministic_output());

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
    size_t pos = 0;
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

template <class T>
concept can_bits_to_string = requires(T t, char* p, size_t s) {
    { bits_to_string<T>(std::underlying_type_t<T>{}, p, s) } -> std::same_as<size_t>;
};
static_assert(can_bits_to_string<TF>);
static_assert(!can_bits_to_string<int>, "bits_to_string requires a scoped enum.  int does not satisfy "
                                        "ScopedEnum, so std::underlying_type_t<int> is itself ill-formed.");

inline void runtime_smoke_test() {
    char buf[64] = {};

    U empty = 0;
    if (bits_to_string<TF>(empty, buf, sizeof(buf)) != 0) std::abort();
    if (buf[0] != '\0') std::abort();

    U a = mask_of({TF::Alpha});
    auto na = bits_to_string<TF>(a, buf, sizeof(buf));
    if (na != 5) std::abort();
    if (std::string_view{buf} != "Alpha") std::abort();

    U abc = mask_of({TF::Alpha, TF::Beta, TF::Gamma});
    auto nabc = bits_to_string<TF>(abc, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta|Gamma") std::abort();
    if (nabc != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    U ab = mask_of({TF::Alpha, TF::Beta});
    auto nab = bits_to_string<TF>(ab, buf, sizeof(buf));
    if (std::string_view{buf} != "Alpha|Beta") std::abort();
    if (nab != 10) std::abort();

    char small[8] = {};
    auto nt = bits_to_string<TF>(ab, small, sizeof(small));
    if (nt != 10) std::abort();
    if (std::string_view{small} != "Alpha|B") std::abort();
    if (small[7] != '\0') std::abort();

    auto np = bits_to_string<TF>(abc, nullptr, 0);
    if (np != std::string_view{"Alpha|Beta|Gamma"}.size()) std::abort();

    {
        char tight[11] = {};
        auto nfit = bits_to_string<TF>(mask_of({TF::Alpha, TF::Beta}), tight, sizeof(tight));
        if (nfit != 10) std::abort();
        if (std::string_view{tight} != "Alpha|Beta") std::abort();
        if (tight[10] != '\0') std::abort();
    }
    {
        char short_buf[10] = {};
        auto nshort = bits_to_string<TF>(mask_of({TF::Alpha, TF::Beta}), short_buf, sizeof(short_buf));
        if (nshort != 10) std::abort();
        if (std::string_view{short_buf} != "Alpha|Bet") std::abort();
        if (short_buf[9] != '\0') std::abort();
    }
    if (enumerator_name(TF::None) != "None") std::abort();

    if (enumerator_name(TF::Alpha) != "Alpha") std::abort();
    if (enumerator_name(TF::Delta) != "Delta") std::abort();

    if (enumerator_name(TF::AlphaBeta) != "AlphaBeta") std::abort();

    auto fancy = static_cast<TF>(static_cast<std::uint8_t>(TF::Alpha) | static_cast<std::uint8_t>(TF::Delta));
    if (!enumerator_name(fancy).empty()) std::abort();

    // enum_name runs under runtime semantics here, with the sentinel
    // read from static storage.
    if (enum_name(fancy) != "<unknown TestFlags>") std::abort();
    if (enum_name(TF::Gamma) != "Gamma") std::abort();
    if (enum_count<TF> != 6) std::abort();

    int counter = 0;
    for_each_enumerator<TF>([&](TF, std::string_view) noexcept { ++counter; });
    if (counter != 6) std::abort();

    int single_bit_counter = 0;
    for_each_single_bit_enumerator<TF>([&](TF, std::string_view) noexcept { ++single_bit_counter; });
    if (single_bit_counter != 4) std::abort();
}

}  // namespace detail::reflected_self_test

}  // namespace foundation::reflect
