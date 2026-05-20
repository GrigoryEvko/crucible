#pragma once

// ── Saturation arithmetic polyfill ──────────────────────────────────
//
// P0543 (C++26) adds std::add_sat / sub_sat / mul_sat.  GCC 16.0.1
// rawhide does not yet ship these in libstdc++ (__cpp_lib_saturation_
// arithmetic is undefined).  This header provides drop-in replacements
// in crucible::sat::* using __builtin_*_overflow.
//
// When the standard library catches up, #ifdef __cpp_lib_saturation_
// arithmetic can forward to std::*; until then we implement it.
//
// Semantics match P0543:
//   add_sat(a, b):  min(max(a + b, T_MIN), T_MAX)
//   sub_sat(a, b):  min(max(a - b, T_MIN), T_MAX)
//   mul_sat(a, b):  min(max(a * b, T_MIN), T_MAX)
//
// Runtime cost: one __builtin_*_overflow (single CMP + CMOV on x86-64,
// ~1 cycle) plus a branchless clamp.  Constexpr-capable.

#include <crucible/Platform.h>
// FIXY-U-096b production migration: safety wrappers (Saturated /
// DetSafe / DetSafeTier_v) and the {add,sub,mul}_sat_checked free
// functions referenced through fixy::wrap:: instead of safety::*.
//
// NOTE: Saturate.h is included transitively by Arena.h (an upstream
// substrate header reached through safety/OwnedRegion.h:72), so it
// CANNOT pull in the full <crucible/fixy/Wrap.h> umbrella — that path
// cycles through Arena.h.  Instead we (a) include the narrow substrate
// headers Saturate.h actually needs, and (b) re-open
// `crucible::fixy::wrap` below to install the 6 using-decls Saturate.h
// references.  fixy/Wrap.h's own using-decls (re-declared independently
// in its TU) are idempotent — multiple using-decls naming the same
// entity in the same namespace are not a redeclaration error.  The
// dual-export sentinels in fixy/Wrap.h continue to witness identity.
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Saturated.h>
#include <version>

namespace crucible::fixy::wrap {
using ::crucible::safety::DetSafe;
using ::crucible::safety::DetSafeTier_v;
using ::crucible::safety::Saturated;
using ::crucible::safety::add_sat_checked;
using ::crucible::safety::sub_sat_checked;
using ::crucible::safety::mul_sat_checked;
}  // namespace crucible::fixy::wrap

#include <concepts>
#include <limits>
#include <type_traits>

namespace crucible::sat {

template <std::integral T>
using DetSatPure = ::crucible::fixy::wrap::DetSafe<
    ::crucible::fixy::wrap::DetSafeTier_v::Pure,
    ::crucible::fixy::wrap::Saturated<T>>;

// gnu::const: takes two values, no memory access, no side effects.
// Optimizer may CSE freely across statements (no aliasing concerns).
// CRUCIBLE_CONST bundles [[nodiscard]] — a saturated arith result that
// is thrown away is almost certainly a bug.

template <std::integral T>
CRUCIBLE_CONST constexpr T add_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // Signed overflow direction: if a >= 0 → wrapped low → clamp MAX;
            // if a < 0 → wrapped high → clamp MIN.
            return (a < T{0}) ? std::numeric_limits<T>::min()
                              : std::numeric_limits<T>::max();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr T sub_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // a - b wraps low iff a < 0 and result overshot MIN; wraps high
            // iff a >= 0 and result overshot MAX.
            return (a < T{0}) ? std::numeric_limits<T>::min()
                              : std::numeric_limits<T>::max();
        } else {
            // Unsigned sub can only wrap below zero → clamp MIN (= 0).
            return std::numeric_limits<T>::min();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr T mul_sat(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // Sign of the mathematical result: negative iff exactly one of
            // a, b is negative.  XOR of sign bits suffices.
            const bool neg = (a < T{0}) != (b < T{0});
            return neg ? std::numeric_limits<T>::min()
                       : std::numeric_limits<T>::max();
        } else {
            return std::numeric_limits<T>::max();
        }
    }
    return r;
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> add_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::add_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> sub_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::sub_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_CONST constexpr DetSatPure<T> mul_sat_det(T a, T b) noexcept {
    return DetSatPure<T>{::crucible::fixy::wrap::mul_sat_checked(a, b)};
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T>
add_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::add_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T>
sub_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::sub_sat_checked(counter, value);
}

template <std::integral T>
CRUCIBLE_PURE constexpr ::crucible::fixy::wrap::Saturated<T>
mul_sat_from(T const& counter, T value) noexcept {
    return ::crucible::fixy::wrap::mul_sat_checked(counter, value);
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T>
add_sat_into(T& dest, T value) noexcept {
    auto result = add_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T>
sub_sat_into(T& dest, T value) noexcept {
    auto result = sub_sat_from(dest, value);
    dest = result.value();
    return result;
}

template <std::integral T>
[[nodiscard]] constexpr ::crucible::fixy::wrap::Saturated<T>
mul_sat_into(T& dest, T value) noexcept {
    auto result = mul_sat_from(dest, value);
    dest = result.value();
    return result;
}

} // namespace crucible::sat
