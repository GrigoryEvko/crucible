#pragma once

// ── crucible::safety::Checked ───────────────────────────────────────
//
// Overflow-mode arithmetic primitives.  Explicit choice at the call
// site for every operation that could overflow, divide by zero, or
// shift out of range.
//
//   Axiom coverage: TypeSafe, DetSafe.
//   Runtime cost:   overflow detection is a single flag check on
//                   x86-64 via __builtin_*_overflow.  Division and
//                   shift validation is a single comparison.
//
// Four modes per op:
//   checked_*     — returns std::optional<T>; nullopt on overflow/invalid
//   wrapping_*    — two's-complement wrap on overflow
//   trapping_*    — std::abort() on overflow
//   saturating_*  — clamp to T's min/max on overflow
//
// Supported ops: add, sub, mul, div, mod, neg, shl, shr, abs.

#include <crucible/Platform.h>
#include <crucible/Saturate.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>

namespace crucible::safety {

// ── Checked (nullopt on overflow) ───────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] return std::nullopt;
    return r;
}

// Division: two overflow paths — divide by zero, and (for signed) MIN/-1.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            return std::nullopt;
    }
    return static_cast<T>(a / b);
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_mod(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]]
            return T{0};  // mathematically defined, despite INT_MIN/-1 overflow
    }
    return static_cast<T>(a % b);
}

// Negation: INT_MIN negated overflows for signed.
template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_neg(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]] return std::nullopt;
    return static_cast<T>(-a);
}

template <std::signed_integral T>
[[nodiscard]] constexpr std::optional<T> checked_abs(T a) noexcept {
    if (a == std::numeric_limits<T>::min()) [[unlikely]] return std::nullopt;
    return static_cast<T>(a < T{0} ? -a : a);
}

// Shift: invalid if shift count >= bit width or negative.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shl(T a, int shift) noexcept {
    if (shift < 0 || shift >= static_cast<int>(sizeof(T) * 8)) [[unlikely]]
        return std::nullopt;
    if constexpr (std::is_signed_v<T>) {
        // Left-shift of negative values is UB; reject.
        if (a < T{0}) [[unlikely]] return std::nullopt;
    }
    return static_cast<T>(a << shift);
}

template <std::integral T>
[[nodiscard]] constexpr std::optional<T> checked_shr(T a, int shift) noexcept {
    if (shift < 0 || shift >= static_cast<int>(sizeof(T) * 8)) [[unlikely]]
        return std::nullopt;
    return static_cast<T>(a >> shift);
}

// ── Wrapping (two's-complement wrap) ────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T wrapping_add(T a, T b) noexcept {
    T r{};
    (void)__builtin_add_overflow(a, b, &r);
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T wrapping_sub(T a, T b) noexcept {
    T r{};
    (void)__builtin_sub_overflow(a, b, &r);
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T wrapping_mul(T a, T b) noexcept {
    T r{};
    (void)__builtin_mul_overflow(a, b, &r);
    return r;
}

// ── Trapping (abort on overflow) ────────────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T trapping_add(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_sub(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_mul(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] std::abort();
    return r;
}

template <std::integral T>
[[nodiscard]] constexpr T trapping_div(T a, T b) noexcept {
    if (b == T{0}) [[unlikely]] std::abort();
    if constexpr (std::is_signed_v<T>) {
        if (a == std::numeric_limits<T>::min() && b == T{-1}) [[unlikely]] std::abort();
    }
    return static_cast<T>(a / b);
}

// ── Saturating (clamp to T's [min, max]) ────────────────────────────

template <std::integral T>
[[nodiscard]] constexpr T saturating_add(T a, T b) noexcept {
    return ::crucible::sat::add_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_sub(T a, T b) noexcept {
    return ::crucible::sat::sub_sat(a, b);
}

template <std::integral T>
[[nodiscard]] constexpr T saturating_mul(T a, T b) noexcept {
    return ::crucible::sat::mul_sat(a, b);
}

// ═════════════════════════════════════════════════════════════════════
// ── Compile-time capacity arithmetic (#408 SAFEINT-C19, §19) ────────
// ═════════════════════════════════════════════════════════════════════
//
// Session-queue capacities are often computed at compile time:
//
//     using MyRing = MpmcRing<Job, NumProducers * BurstPerProducer>;
//
// A bare `*` is silently wrong if the product overflows the result
// type — the corrupted capacity propagates into ring-buffer sizing,
// arena layout, and downstream invariants.  The variable templates
// below promote the existing runtime `checked_add` / `checked_sub` /
// `checked_mul` primitives into compile-time-failing arithmetic so
// the bug becomes a build error with a framework-controlled
// diagnostic.
//
// ─── Routing: helper struct, not consteval-lambda ──────────────────
//
// The natural-looking
//   inline constexpr auto safe_capacity = []() consteval {
//       auto r = checked_mul<std::size_t>(A, B);
//       if (!r) static_assert(false, "...");   // ← always fires!
//       return *r;
//   }();
// has a subtle bug: `static_assert(false, msg)` inside a function
// body fires unconditionally on instantiation regardless of any
// surrounding `if`.  The robust idiom routes through a class-template
// helper whose body has a `static constexpr auto _opt = ...` member:
// the `static_assert(_opt.has_value(), ...)` then evaluates the
// constexpr optional and fires ONLY on actual overflow.
//
// ─── Use ────────────────────────────────────────────────────────────
//
//     // Type-parameterised:
//     using MyChannel = MpmcRing<Job,
//         safe_mul<std::size_t, NumProducers, BurstPerProducer>>;
//
//     // size_t convenience (canonical session-queue capacity case):
//     using MyChannel = MpmcRing<Job,
//         safe_capacity<NumProducers, BurstPerProducer>>;
//
// ─── Diagnostics ────────────────────────────────────────────────────
//
// Every overflow site fires a static_assert beginning with
// `[Checked_Capacity_Overflow]` so audit greps can find every
// compile-time arithmetic-failure site mechanically.

namespace detail {

template <std::integral T, T A, T B>
struct safe_add_impl {
    static constexpr auto _opt = checked_add<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_add: A + B overflows the "
        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = *_opt;
};

template <std::integral T, T A, T B>
struct safe_sub_impl {
    static constexpr auto _opt = checked_sub<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_sub: A - B underflows the "
        "destination integer type.  For unsigned T, A must be >= B.");
    static constexpr T value = *_opt;
};

template <std::integral T, T A, T B>
struct safe_mul_impl {
    static constexpr auto _opt = checked_mul<T>(A, B);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_mul: A * B overflows the "
        "destination integer type.  Pick a wider T or smaller operands.");
    static constexpr T value = *_opt;
};

}  // namespace detail

// Type-parameterised compile-time arithmetic — fails to compile on
// overflow with the [Checked_Capacity_Overflow] diagnostic prefix.

template <std::integral T, T A, T B>
inline constexpr T safe_add = detail::safe_add_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_sub = detail::safe_sub_impl<T, A, B>::value;

template <std::integral T, T A, T B>
inline constexpr T safe_mul = detail::safe_mul_impl<T, A, B>::value;

// ─── std::size_t convenience aliases ────────────────────────────────
//
// The vast majority of capacity-arithmetic sites use std::size_t as
// the destination type (queue capacities, arena byte budgets, ring
// sizes).  These aliases let call sites omit the type parameter
// entirely, keeping the channel-declaration boilerplate to a minimum.

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_capacity = safe_mul<std::size_t, A, B>;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_byte_budget = safe_mul<std::size_t, A, B>;

// ─── Variadic byte-budget helpers (#134) ────────────────────────────
//
// Real-world memory/budget arithmetic is RARELY just `A * B`.  Arena
// sizing, struct-of-arrays layouts, protocol-framing headers, and
// permission-carrier buffers all need sums of terms — each term itself
// often a product of (count, per-item-size).  Pre-#134 a user doing:
//
//     constexpr std::size_t total = Hdr * 1                     // header
//                                 + Payload * EltSize * N       // body
//                                 + Tail * AlignPad;            // footer
//
// got no overflow protection — any intermediate product could wrap,
// silently producing a too-small total that then under-provisions an
// arena, over-provisions a recv buffer, or misaligns a struct.  The
// helpers below promote the compile-time discipline to variadic form
// and to common layout-specific cases.

namespace detail {

// Fold a variadic sum with overflow detection at EVERY step.  Each
// partial sum is checked; the first overflow halts with the named
// diagnostic.
template <std::integral T, T... Xs>
struct safe_add_all_impl;

template <std::integral T>
struct safe_add_all_impl<T> {
    static constexpr T value = T{0};
};

template <std::integral T, T X>
struct safe_add_all_impl<T, X> {
    static constexpr T value = X;
};

template <std::integral T, T X, T Y, T... Rest>
struct safe_add_all_impl<T, X, Y, Rest...> {
    static constexpr auto _opt = checked_add<T>(X, Y);
    static_assert(_opt.has_value(),
        "[Checked_Capacity_Overflow] safe_add_all: partial sum "
        "overflows the destination integer type.  One of the terms "
        "in the variadic sum exceeds the remaining budget; split the "
        "sum into smaller chunks, pick a wider T, or reduce an "
        "operand.");
    static constexpr T value = safe_add_all_impl<T, *_opt, Rest...>::value;
};

}  // namespace detail

// safe_add_all<T, X1, X2, ..., Xn> — variadic checked sum.  Fires
// `[Checked_Capacity_Overflow]` at the first partial-sum overflow.
template <std::integral T, T... Xs>
inline constexpr T safe_add_all = detail::safe_add_all_impl<T, Xs...>::value;

// ─── Layout-specific byte-budget helpers (#134) ─────────────────────

// safe_array_bytes<T, N> — total bytes for N elements of type T.
// Overflow-safe multiplication of sizeof(T) and N; fires the same
// [Checked_Capacity_Overflow] prefix on overflow.  The canonical "I
// want to allocate N objects of T" budget calculation.
template <typename T, std::size_t N>
inline constexpr std::size_t safe_array_bytes =
    safe_mul<std::size_t, sizeof(T), N>;

// safe_struct_bytes<T1, T2, ..., Tn> — sum of sizeof(Ti) over a type
// pack.  Overflow-safe variadic sum of the sizeof's.  Useful for
// struct-of-arrays layouts and protocol-framing headers where each
// field's contribution is `sizeof(field_type)` and the total is a
// running sum.  Note: this is a naive sum, NOT alignment-aware —
// callers that need padding-accurate layouts must also account for
// alignment(Ti) and pad bytes separately (or use reflection to compute
// the true sizeof-with-padding of a packed struct).
template <typename... Ts>
inline constexpr std::size_t safe_struct_bytes =
    safe_add_all<std::size_t, sizeof(Ts)...>;

// ─── Compile-time budget fit check (#134) ────────────────────────────
//
// `bytes_fit_v<Budget, Used>` is a boolean trait: true iff the USED
// byte count fits within the declared BUDGET.  `ensure_bytes_fit<
// Budget, Used>()` is the consteval one-line check that fires
// `[Byte_Budget_Exceeded]` when it doesn't — paralleling the
// `[Dual_Mismatch]` / `[Branch_Index_Out_Of_Range]` discipline from
// the session-types side.  Use at declaration sites that must not
// exceed a compile-time budget (arena page sizes, cache-line
// budgets, kernel-stack allocations, permission buffer footprints).
//
// Example:
//
//     struct MyMetadata {
//         uint64_t field_a;
//         uint64_t field_b;
//         uint32_t field_c;
//     };
//     // Must fit in a single 64-byte cache line:
//     ensure_bytes_fit<64, safe_struct_bytes<uint64_t, uint64_t, uint32_t>>();

template <std::size_t Budget, std::size_t Used>
inline constexpr bool bytes_fit_v = (Used <= Budget);

template <std::size_t Budget, std::size_t Used>
consteval void ensure_bytes_fit() noexcept {
    static_assert(bytes_fit_v<Budget, Used>,
        "[Byte_Budget_Exceeded] ensure_bytes_fit<Budget, Used>(): "
        "the computed byte usage exceeds the declared budget.  "
        "Inspect `safe_struct_bytes<...>` / `safe_array_bytes<T, N>` / "
        "`safe_add_all<size_t, ...>` for the individual contributors, "
        "OR widen the budget if the carrier can accommodate it.  "
        "Common causes: (a) added a new field to a cache-line-tight "
        "struct, (b) bumped N for an array that was sized to fit a "
        "single page, (c) composed a buffer layout whose sum crosses "
        "a hardware-alignment boundary (cache line, page, sector).");
}

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_size_sum = safe_add<std::size_t, A, B>;

template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_size_diff = safe_sub<std::size_t, A, B>;

// ── Self-tests ─────────────────────────────────────────────────────
//
// Compile-time witnesses that the helpers fire on the expected
// inputs.  Negative cases (overflow rejection) live in the test/
// safety_neg/ harness because `static_assert(false, ...)` is exactly
// what we want to TRIGGER, and that has to live in a TU that's not
// part of the regular build.

static_assert(safe_add<std::uint32_t, 10u, 20u>                         == 30u);
static_assert(safe_sub<std::uint32_t, 30u, 20u>                         == 10u);
static_assert(safe_mul<std::uint32_t, 6u, 7u>                           == 42u);
static_assert(safe_capacity<8u, 16u>                                    == 128u);
static_assert(safe_capacity<std::size_t{1} << 16, std::size_t{1} << 16> == (std::size_t{1} << 32));
static_assert(safe_byte_budget<256u, 64u>                               == 256u * 64u);
static_assert(safe_size_sum<10u, 20u>                                   == 30u);
static_assert(safe_size_diff<30u, 10u>                                  == 20u);

// Edge: zero is fine in both directions.
static_assert(safe_mul<std::size_t, std::size_t{0}, std::size_t{1} << 60> == 0u);
static_assert(safe_add<std::size_t, std::size_t{0}, std::size_t{0}>       == 0u);

// ─── Self-tests for the #134 variadic / layout helpers ─────────────

// safe_add_all: empty fold is 0, single-term is identity, multi-term
// equals the sum.
static_assert(safe_add_all<std::size_t>                               == 0u);
static_assert(safe_add_all<std::size_t, 42u>                          == 42u);
static_assert(safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u>           == 15u);
static_assert(safe_add_all<std::uint32_t, 10u, 20u, 30u>              == 60u);

// safe_array_bytes: sizeof(T) * N.
static_assert(safe_array_bytes<std::uint64_t, 8u>                     == 64u);
static_assert(safe_array_bytes<std::byte, 4096u>                      == 4096u);
static_assert(safe_array_bytes<std::uint32_t, 0u>                     == 0u);

// safe_struct_bytes: sum of sizeof(Ts...).  Naive sum — NOT alignment-
// aware; sizeof(struct{uint64;uint32;}) would be 16 not 12, but the
// helper returns 12 (raw sum).  The distinction is documented.
static_assert(safe_struct_bytes<>                                     == 0u);
static_assert(safe_struct_bytes<std::uint64_t>                        == 8u);
static_assert(safe_struct_bytes<std::uint64_t, std::uint32_t>         == 12u);
static_assert(safe_struct_bytes<std::uint64_t,
                                std::uint64_t,
                                std::uint32_t>                        == 20u);

// bytes_fit_v / ensure_bytes_fit.
static_assert(bytes_fit_v<64u, 20u>);
static_assert(bytes_fit_v<64u, 64u>);                          // exact fit
static_assert(!bytes_fit_v<64u, 65u>);                         // overflow by 1

// ensure_bytes_fit is consteval; the happy path compiles silently.
// (Neg-compile test covers the [Byte_Budget_Exceeded] path.)
[[maybe_unused]] constexpr auto _check_fits = []() {
    ensure_bytes_fit<64, safe_struct_bytes<std::uint64_t, std::uint64_t>>();
    return 0;
}();

} // namespace crucible::safety
