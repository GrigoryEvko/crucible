#pragma once

// ── crucible::safety::extract::is_suspend_behavior_v ────────────────
//
// FIXY-V-188 — wrapper-detection predicate for `SuspendBehavior<Behavior,
// T>` (the V-181 pause-on-suspend witness).  Mechanical sibling of
// IsClockSource / IsSchedClass — the partial spec captures the
// SuspendBehavior NTTP alongside the wrapped type, so the V-194
// DeadlineWatchdog can read the pinned behavior off the type without
// instantiating the wrapper.

#include <crucible/safety/SuspendBehavior.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::SuspendBehavior_v;

namespace detail {

template <typename T>
struct is_suspend_behavior_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_behavior = false;
};

template <SuspendBehavior_v Behavior, typename U>
struct is_suspend_behavior_impl<::crucible::safety::SuspendBehavior<Behavior, U>>
    : std::true_type {
    using value_type = U;
    static constexpr SuspendBehavior_v behavior = Behavior;
    static constexpr bool has_behavior = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_suspend_behavior_v =
    detail::is_suspend_behavior_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSuspendBehavior = is_suspend_behavior_v<T>;

template <typename T>
    requires is_suspend_behavior_v<T>
using suspend_behavior_value_t =
    typename detail::is_suspend_behavior_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_suspend_behavior_v<T>
inline constexpr SuspendBehavior_v suspend_behavior_v =
    detail::is_suspend_behavior_impl<std::remove_cvref_t<T>>::behavior;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_suspend_behavior_self_test {

using K_u64 = ::crucible::safety::SuspendBehavior<SuspendBehavior_v::KeepsTicking,    unsigned long long>;
using P_u64 = ::crucible::safety::SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, unsigned long long>;
using U_int = ::crucible::safety::SuspendBehavior<SuspendBehavior_v::Unknown,         int>;

static_assert(is_suspend_behavior_v<K_u64>);
static_assert(is_suspend_behavior_v<P_u64>);
static_assert(is_suspend_behavior_v<U_int>);
static_assert(is_suspend_behavior_v<K_u64&>);
static_assert(is_suspend_behavior_v<K_u64 const&>);

static_assert(!is_suspend_behavior_v<int>);
static_assert(!is_suspend_behavior_v<void>);
static_assert(!is_suspend_behavior_v<K_u64*>);

struct LookalikeWitness { unsigned long long value; SuspendBehavior_v behavior; };
static_assert(!is_suspend_behavior_v<LookalikeWitness>);

static_assert(IsSuspendBehavior<K_u64>);
static_assert(!IsSuspendBehavior<int>);

static_assert(std::is_same_v<suspend_behavior_value_t<K_u64>, unsigned long long>);
static_assert(std::is_same_v<suspend_behavior_value_t<U_int>, int>);

static_assert(suspend_behavior_v<K_u64> == SuspendBehavior_v::KeepsTicking);
static_assert(suspend_behavior_v<P_u64> == SuspendBehavior_v::PausesOnSuspend);
static_assert(suspend_behavior_v<K_u64> != suspend_behavior_v<P_u64>);

}  // namespace detail::is_suspend_behavior_self_test

inline bool is_suspend_behavior_smoke_test() noexcept {
    using namespace detail::is_suspend_behavior_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_suspend_behavior_v<K_u64>;
        ok = ok && is_suspend_behavior_v<P_u64>;
        ok = ok && !is_suspend_behavior_v<int>;
        ok = ok && IsSuspendBehavior<K_u64&&>;
        ok = ok && (suspend_behavior_v<K_u64> == SuspendBehavior_v::KeepsTicking);
        ok = ok && (suspend_behavior_v<P_u64> == SuspendBehavior_v::PausesOnSuspend);
    }
    return ok;
}

}  // namespace crucible::safety::extract
