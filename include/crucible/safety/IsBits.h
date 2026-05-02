#pragma once

// ── crucible::safety::extract::is_bits_v ────────────────────────────
//
// Wrapper-detection predicate for `safety::Bits<EnumType>` (#1079).
// Mechanical extension of the established 24-header `safety/Is*.h`
// family; same template shape as `IsHotPath.h:1-90`.
//
// Surface:
//
//   is_bits_v<W>            constexpr bool — true iff W is a
//                           Bits<E> instantiation (cvref-stripped).
//   IsBits<W>               concept gate.
//   bits_enum_t<W>          extracts E from Bits<E> when is_bits_v<W>.
//   bits_underlying_t<W>    extracts std::underlying_type_t<E>.
//
// LookalikeBits witness asserts the pattern-match is template-spec-
// based (NOT duck-typing on member names).
//
// Why this lives in safety/.  Per the existing Is*.h convention —
// trait helpers accompanying their wrapper sit alongside it (see
// IsHotPath.h, IsConsumerHandle.h, etc.).
//
// References: WRAP-Bits-Integration-1 (#1085).

#include <crucible/safety/Bits.h>

#include <cstdlib>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_bits_impl : std::false_type {
    using value_type      = void;
    using underlying_type = void;
};

template <ScopedEnum E>
struct is_bits_impl<::crucible::safety::Bits<E>> : std::true_type {
    using value_type      = E;
    using underlying_type = std::underlying_type_t<E>;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_bits_v =
    detail::is_bits_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBits = is_bits_v<T>;

template <typename T>
    requires is_bits_v<T>
using bits_enum_t =
    typename detail::is_bits_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_bits_v<T>
using bits_underlying_t =
    typename detail::is_bits_impl<std::remove_cvref_t<T>>::underlying_type;

// ── Self-test ───────────────────────────────────────────────────────

namespace detail::is_bits_self_test {

enum class TestEnum8  : std::uint8_t  { A = 1, B = 2 };
enum class TestEnum16 : std::uint16_t { X = 1, Y = 2 };
enum class TestEnum32 : std::uint32_t { K = 1, L = 2 };

using B8  = ::crucible::safety::Bits<TestEnum8>;
using B16 = ::crucible::safety::Bits<TestEnum16>;
using B32 = ::crucible::safety::Bits<TestEnum32>;

// Positive cases — every Bits<E> instantiation is detected.
static_assert( is_bits_v<B8>);
static_assert( is_bits_v<B16>);
static_assert( is_bits_v<B32>);

// cvref strip — references and const-qualifications also detect.
static_assert( is_bits_v<B8&>);
static_assert( is_bits_v<B8 const&>);
static_assert( is_bits_v<B8&&>);

// Negative cases — non-Bits types rejected.
static_assert(!is_bits_v<int>);
static_assert(!is_bits_v<TestEnum8>);              // bare enum
static_assert(!is_bits_v<std::uint8_t>);           // bare underlying
static_assert(!is_bits_v<void>);
static_assert(!is_bits_v<B8*>);                    // pointer-to-Bits
static_assert(!is_bits_v<B8[3]>);                  // array-of-Bits

// Lookalike rejection — pattern-match is template-spec-based, NOT
// duck-typing on member names.  A struct that happens to have a
// `bits_` member must NOT satisfy is_bits_v.
struct LookalikeBits { std::uint8_t bits_; TestEnum8 enum_value; };
static_assert(!is_bits_v<LookalikeBits>,
    "is_bits_v MUST reject lookalikes that share the Bits<E> "
    "internal-member shape but aren't actually Bits<E> instantiations. "
    "If this fires, the partial spec has been weakened to duck-typing "
    "and downstream concept overloads will misfire on user types.");

// Concept gate parity with the variable.
static_assert( IsBits<B8>);
static_assert(!IsBits<int>);

// Extractor types resolve correctly.
static_assert(std::is_same_v<bits_enum_t<B8>,        TestEnum8>);
static_assert(std::is_same_v<bits_enum_t<B16>,       TestEnum16>);
static_assert(std::is_same_v<bits_underlying_t<B8>,  std::uint8_t>);
static_assert(std::is_same_v<bits_underlying_t<B16>, std::uint16_t>);
static_assert(std::is_same_v<bits_underlying_t<B32>, std::uint32_t>);

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    if (!is_bits_v<B8>)        std::abort();
    if ( is_bits_v<int>)       std::abort();
    if (!IsBits<B16>)          std::abort();
    if ( IsBits<TestEnum8>)    std::abort();
    if (!std::is_same_v<bits_enum_t<B32>, TestEnum32>) std::abort();
}

}  // namespace detail::is_bits_self_test

}  // namespace crucible::safety::extract
