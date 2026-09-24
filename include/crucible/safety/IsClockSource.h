#pragma once

#include <crucible/safety/_ClockSource.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::ClockSource_v;

namespace detail {

template <typename T>
struct is_clock_source_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_source = false;
};

template <ClockSource_v Source, typename U>
struct is_clock_source_impl<::crucible::safety::ClockSource<Source, U>> : std::true_type {
    using value_type = U;
    static constexpr ClockSource_v source = Source;
    static constexpr bool has_source = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_clock_source_v = detail::is_clock_source_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsClockSource = is_clock_source_v<T>;

template <typename T>
    requires is_clock_source_v<T>
using clock_source_value_t = typename detail::is_clock_source_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_clock_source_v<T>
inline constexpr ClockSource_v clock_source_source_v = detail::is_clock_source_impl<std::remove_cvref_t<T>>::source;

namespace detail::is_clock_source_self_test {

using B_u64 = ::crucible::safety::BootClockBytes<unsigned long long>;
using M_u64 = ::crucible::safety::MonotonicClockBytes<unsigned long long>;
using R_u64 = ::crucible::safety::RealtimeClockBytes<unsigned long long>;
using Tsc_u64 = ::crucible::safety::TscBytes<unsigned long long>;
using Pmu_int = ::crucible::safety::PmuBytes<int>;
using Phc_u64 = ::crucible::safety::PtpHwClockBytes<unsigned long long>;

static_assert(is_clock_source_v<B_u64>);
static_assert(is_clock_source_v<M_u64>);
static_assert(is_clock_source_v<R_u64>);
static_assert(is_clock_source_v<Tsc_u64>);
static_assert(is_clock_source_v<Pmu_int>);
static_assert(is_clock_source_v<Phc_u64>);

static_assert(is_clock_source_v<B_u64&>);
static_assert(is_clock_source_v<B_u64 const&>);

static_assert(!is_clock_source_v<int>);
static_assert(!is_clock_source_v<unsigned long long>);
static_assert(!is_clock_source_v<void>);
static_assert(!is_clock_source_v<B_u64*>);

struct LookalikeClock {
    unsigned long long value;
    ClockSource_v source;
};
static_assert(!is_clock_source_v<LookalikeClock>);

static_assert(IsClockSource<B_u64>);
static_assert(!IsClockSource<int>);

static_assert(std::is_same_v<clock_source_value_t<B_u64>, unsigned long long>);
static_assert(std::is_same_v<clock_source_value_t<Pmu_int>, int>);

static_assert(clock_source_source_v<B_u64> == ClockSource_v::Boot);
static_assert(clock_source_source_v<M_u64> == ClockSource_v::Monotonic);
static_assert(clock_source_source_v<R_u64> == ClockSource_v::Realtime);
static_assert(clock_source_source_v<Tsc_u64> == ClockSource_v::TscRaw);
static_assert(clock_source_source_v<Pmu_int> == ClockSource_v::PmuCounter);
static_assert(clock_source_source_v<Phc_u64> == ClockSource_v::PtpHwClock);

static_assert(clock_source_source_v<B_u64> != clock_source_source_v<M_u64>);
static_assert(clock_source_source_v<B_u64> != clock_source_source_v<Phc_u64>,
              "The hardware clock and the boot clock project to the same tuple but keep distinct "
              "source identities. The detector must read different ClockSource_v values rather "
              "than collapse them.");

}  // namespace detail::is_clock_source_self_test

inline bool is_clock_source_smoke_test() noexcept {
    using namespace detail::is_clock_source_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_clock_source_v<B_u64>;
        ok = ok && is_clock_source_v<Tsc_u64>;
        ok = ok && !is_clock_source_v<int>;
        ok = ok && IsClockSource<B_u64&&>;
        ok = ok && (clock_source_source_v<B_u64> == ClockSource_v::Boot);
        ok = ok && (clock_source_source_v<M_u64> == ClockSource_v::Monotonic);
    }
    return ok;
}

}  // namespace crucible::safety::extract
