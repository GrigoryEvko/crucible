#pragma once

// ── crucible::safety::extract::is_det_safe_v ────────────────────────
//
// FOUND-D24 — wrapper-detection predicate for `DetSafe<Tier, T>`.
// Mechanical extension of D21/D22/D23 — partial-spec captures the
// DetSafeTier_v NTTP enum value alongside the wrapped type.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_det_safe_v<T>      Variable template; cv-ref-stripped.
//   IsDetSafe<T>           Concept form.
//   det_safe_value_t<T>    Wrapped element type; constrained.
//   det_safe_tier_v<T>     Pinned DetSafeTier_v tier; constrained.

#include <crucible/safety/DetSafe.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::DetSafeTier_v;

namespace detail {

template <typename T>
struct is_det_safe_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tier = false;
};

template <DetSafeTier_v Tier, typename U>
struct is_det_safe_impl<::crucible::safety::DetSafe<Tier, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr DetSafeTier_v tier = Tier;
    static constexpr bool has_tier = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_det_safe_v =
    detail::is_det_safe_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsDetSafe = is_det_safe_v<T>;

template <typename T>
    requires is_det_safe_v<T>
using det_safe_value_t =
    typename detail::is_det_safe_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_det_safe_v<T>
inline constexpr DetSafeTier_v det_safe_tier_v =
    detail::is_det_safe_impl<std::remove_cvref_t<T>>::tier;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_det_safe_self_test {

using DS_int_pure =
    ::crucible::safety::DetSafe<DetSafeTier_v::Pure, int>;
using DS_double_philox =
    ::crucible::safety::DetSafe<DetSafeTier_v::PhiloxRng, double>;
using DS_int_nds =
    ::crucible::safety::DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>;

static_assert(is_det_safe_v<DS_int_pure>);
static_assert(is_det_safe_v<DS_double_philox>);
static_assert(is_det_safe_v<DS_int_nds>);

static_assert(is_det_safe_v<DS_int_pure&>);
static_assert(is_det_safe_v<DS_int_pure const&>);

static_assert(!is_det_safe_v<int>);
static_assert(!is_det_safe_v<int*>);
static_assert(!is_det_safe_v<void>);

struct LookalikeDetSafe { int value; DetSafeTier_v tier; };
static_assert(!is_det_safe_v<LookalikeDetSafe>);

static_assert(!is_det_safe_v<DS_int_pure*>);

static_assert(IsDetSafe<DS_int_pure>);
static_assert(!IsDetSafe<int>);

static_assert(std::is_same_v<det_safe_value_t<DS_int_pure>, int>);
static_assert(std::is_same_v<det_safe_value_t<DS_double_philox>, double>);

static_assert(det_safe_tier_v<DS_int_pure> == DetSafeTier_v::Pure);
static_assert(det_safe_tier_v<DS_double_philox>
              == DetSafeTier_v::PhiloxRng);
static_assert(det_safe_tier_v<DS_int_nds>
              == DetSafeTier_v::NonDeterministicSyscall);

}  // namespace detail::is_det_safe_self_test

inline bool is_det_safe_smoke_test() noexcept {
    using namespace detail::is_det_safe_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_det_safe_v<DS_int_pure>;
        ok = ok && !is_det_safe_v<int>;
        ok = ok && IsDetSafe<DS_int_pure&&>;
        ok = ok && (det_safe_tier_v<DS_int_pure> == DetSafeTier_v::Pure);
    }
    return ok;
}

}  // namespace crucible::safety::extract
