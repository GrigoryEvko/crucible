#pragma once

// ── crucible::safety::extract::is_sched_class_v ─────────────────────
//
// FIXY-V-186 — wrapper-detection predicate for `SchedClass<Policy, T,
// ...>` (the V-183 SchedulerPolicy-axis carrier).  Mechanical sibling of
// IsClockSource / IsScopedFence — the partial spec captures the
// SchedulerPolicy NTTP (and the SCHED_DEADLINE budget NTTPs) alongside the
// wrapped type, so a thread pool can read the pinned policy off the type
// without instantiating the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice value:
// a `SchedClass<Fifo, T>` and a look-alike struct carrying a
// SchedulerPolicy field are NOT confused.

#include <crucible/safety/SchedClass.h>

#include <cstdint>
#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::SchedulerPolicy_v;

namespace detail {

template <typename T>
struct is_sched_class_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_policy = false;
};

template <SchedulerPolicy_v Policy, typename U,
          std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
struct is_sched_class_impl<
    ::crucible::safety::SchedClass<Policy, U, RuntimeNs, DeadlineNs, PeriodNs>>
    : std::true_type {
    using value_type = U;
    static constexpr SchedulerPolicy_v policy      = Policy;
    static constexpr std::uint64_t     runtime_ns  = RuntimeNs;
    static constexpr std::uint64_t     deadline_ns = DeadlineNs;
    static constexpr std::uint64_t     period_ns   = PeriodNs;
    static constexpr bool              has_policy   = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_sched_class_v =
    detail::is_sched_class_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSchedClass = is_sched_class_v<T>;

template <typename T>
    requires is_sched_class_v<T>
using sched_class_value_t =
    typename detail::is_sched_class_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_sched_class_v<T>
inline constexpr SchedulerPolicy_v sched_class_policy_v =
    detail::is_sched_class_impl<std::remove_cvref_t<T>>::policy;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_sched_class_self_test {

using F_int  = ::crucible::safety::SchedClass<SchedulerPolicy_v::Fifo,  int>;
using O_int  = ::crucible::safety::SchedClass<SchedulerPolicy_v::Other, int>;
using DL_int = ::crucible::safety::SchedClass<SchedulerPolicy_v::Deadline, int, 5'000, 10'000, 20'000>;
using F_dbl  = ::crucible::safety::SchedClass<SchedulerPolicy_v::Fifo,  double>;

static_assert(is_sched_class_v<F_int>);
static_assert(is_sched_class_v<O_int>);
static_assert(is_sched_class_v<DL_int>);
static_assert(is_sched_class_v<F_int&>);
static_assert(is_sched_class_v<F_int const&>);

static_assert(!is_sched_class_v<int>);
static_assert(!is_sched_class_v<void>);
static_assert(!is_sched_class_v<F_int*>);

struct LookalikeTask { int value; SchedulerPolicy_v policy; };
static_assert(!is_sched_class_v<LookalikeTask>);

static_assert(IsSchedClass<F_int>);
static_assert(!IsSchedClass<int>);

static_assert(std::is_same_v<sched_class_value_t<F_int>, int>);
static_assert(std::is_same_v<sched_class_value_t<F_dbl>, double>);

static_assert(sched_class_policy_v<F_int>  == SchedulerPolicy_v::Fifo);
static_assert(sched_class_policy_v<O_int>  == SchedulerPolicy_v::Other);
static_assert(sched_class_policy_v<DL_int> == SchedulerPolicy_v::Deadline);
static_assert(sched_class_policy_v<F_int>  != sched_class_policy_v<O_int>);

// The DEADLINE budget is recoverable from the type.
static_assert(detail::is_sched_class_impl<DL_int>::deadline_ns == 10'000);
static_assert(detail::is_sched_class_impl<F_int>::runtime_ns   == 0);

}  // namespace detail::is_sched_class_self_test

inline bool is_sched_class_smoke_test() noexcept {
    using namespace detail::is_sched_class_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_sched_class_v<F_int>;
        ok = ok && is_sched_class_v<DL_int>;
        ok = ok && !is_sched_class_v<int>;
        ok = ok && IsSchedClass<F_int&&>;
        ok = ok && (sched_class_policy_v<F_int> == SchedulerPolicy_v::Fifo);
        ok = ok && (sched_class_policy_v<O_int> == SchedulerPolicy_v::Other);
    }
    return ok;
}

}  // namespace crucible::safety::extract
