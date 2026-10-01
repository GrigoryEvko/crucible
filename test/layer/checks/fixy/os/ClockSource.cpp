// The compile-time checks of fixy/os/ClockSource.h.

#include <fixy/os/ClockSource.h>

namespace fixy {

namespace detail::clock_source_layout {

// The layout of the bare value, with one difference on purpose: a reading
// is not trivially copyable, so no byte pattern becomes a reading.  The
// shared layout invariant asserts triviality parity, so this header
// states the other three properties of it one by one.
template <typename Reading, typename T>
inline constexpr bool keeps_the_value_layout =
    sizeof(Reading) == sizeof(T) && alignof(Reading) == alignof(T)
    && std::is_trivially_destructible_v<Reading> == std::is_trivially_destructible_v<T>;

template <typename Reading>
inline constexpr bool refuses_every_byte_route =
    !std::is_trivially_copyable_v<Reading> && !::foundation::lifetime::ImplicitLifetimeThroughout<Reading>
    && !std::is_default_constructible_v<Reading> && std::is_trivially_copy_constructible_v<Reading>
    && std::is_trivially_move_constructible_v<Reading>;

static_assert(keeps_the_value_layout<RealtimeClockBytes<int>, int>);
static_assert(keeps_the_value_layout<MonotonicClockBytes<int>, int>);
static_assert(keeps_the_value_layout<BootClockBytes<unsigned long long>, unsigned long long>);
static_assert(keeps_the_value_layout<TscBytes<unsigned long long>, unsigned long long>);
static_assert(keeps_the_value_layout<PmuBytes<int>, int>);
static_assert(keeps_the_value_layout<PtpHwClockBytes<unsigned long long>, unsigned long long>);

static_assert(refuses_every_byte_route<BootClockBytes<unsigned long long>>
                  && refuses_every_byte_route<TscBytes<unsigned long long>>
                  && refuses_every_byte_route<MonotonicClockBytes<unsigned long long>>,
              "a clock reading must not be built from bytes, over a buffer, or from nothing, and its "
              "constructors must stay trivial so that it passes in a register");
static_assert(!std::is_constructible_v<BootClockBytes<unsigned long long>, unsigned long long>,
              "the value constructor must be private: a public one stamps any integer as a boot-clock reading");
static_assert(!std::is_constructible_v<BootClockBytes<unsigned long long>, std::in_place_t, unsigned long long>,
              "no in_place constructor may exist: it would be a second route to the same forgery");
static_assert(std::is_copy_constructible_v<BootClockBytes<unsigned long long>>,
              "a copy of a reading is a reading of the same clock, so copies stay");

}  // namespace detail::clock_source_layout

static_assert(sizeof(RealtimeClockBytes<int>) == sizeof(int));
static_assert(sizeof(MonotonicClockBytes<int>) == sizeof(int));
static_assert(sizeof(BootClockBytes<unsigned long long>) == sizeof(unsigned long long));
static_assert(sizeof(TscBytes<unsigned long long>) == sizeof(unsigned long long));
static_assert(sizeof(PmuBytes<int>) == sizeof(int));
static_assert(sizeof(PtpHwClockBytes<unsigned long long>) == sizeof(unsigned long long),
              "PtpHwClockBytes<u64> is the size of a bare u64.  The source grade is an "
              "empty singleton and carries nothing per instance.");

namespace detail::clock_source_invariants {

using BootU64 = BootClockBytes<unsigned long long>;
using MonoU64 = MonotonicClockBytes<unsigned long long>;
using RealU64 = RealtimeClockBytes<unsigned long long>;
using TscU64 = TscBytes<unsigned long long>;
using ThreadU64 = ThreadCpuBytes<unsigned long long>;

// These checks build no reading out of a literal, because that is the
// forgery the closed constructor refuses.  The behavior of a real
// reading (copy, swap, equality) is exercised in test/fixy/test_os_time.cpp
// over readings that a clock returned.
static_assert(BootU64::source == ClockSource_v::Boot);

static_assert(BootU64::modality == ::foundation::algebra::ModalityKind::Absolute);

static_assert(BootU64::det_safe_tier == DetSafeTier_v::MonotonicClockRead);
static_assert(BootU64::suspend_behavior == SuspendBehavior_v::KeepsTicking);
static_assert(BootU64::pinning_requirement == PinningRequirement_v::NotRequired);

static_assert(MonoU64::suspend_behavior == SuspendBehavior_v::PausesOnSuspend);
static_assert(RealU64::det_safe_tier == DetSafeTier_v::WallClockRead);
static_assert(TscU64::pinning_requirement == PinningRequirement_v::PerCore);
static_assert(TscU64::suspend_behavior == SuspendBehavior_v::KeepsTicking);

// A clock read is never a pure function of its declared inputs, so no
// source reaches the pure tier and a context demanding it turns away
// every clock-sourced value.
static_assert(BootU64::det_safe_tier != DetSafeTier_v::Pure);
static_assert(MonoU64::det_safe_tier != DetSafeTier_v::Pure);
static_assert(RealU64::det_safe_tier != DetSafeTier_v::Pure);
static_assert(TscU64::det_safe_tier != DetSafeTier_v::Pure);
static_assert(ThreadU64::det_safe_tier != DetSafeTier_v::Pure);

static_assert(BootU64::satisfies<ClockSource_v::Boot>,
              "BootClockBytes satisfies a Boot requirement.  A boot-time read keeps "
              "ticking through suspend.");
static_assert(!MonoU64::satisfies<ClockSource_v::Boot>,
              "MonotonicClockBytes does not satisfy a Boot requirement.  A monotonic "
              "read pauses on suspend, and a paused clock does not cover a "
              "keeps-ticking one.");
static_assert(TscU64::satisfies<ClockSource_v::Boot>,
              "A raw timestamp-counter read keeps ticking through suspend, and its "
              "per-core pinning requirement is above the no-pin requirement.");
static_assert(!RealU64::satisfies<ClockSource_v::Boot>);
static_assert(BootU64::satisfies<ClockSource_v::Monotonic>);
static_assert(!MonoU64::satisfies<ClockSource_v::TscRaw>,
              "Monotonic does NOT subsume TscRaw — PerCore ⋣ NotRequired on pinning.");

static_assert(!std::is_same_v<BootU64, MonoU64>, "BootClockBytes and MonotonicClockBytes are distinct types, which is "
                                                 "what lets a consumer require one and reject the other statically.");
static_assert(!std::is_same_v<TscBytes<int>, PmuBytes<int>>);
static_assert(!std::is_convertible_v<MonoU64, BootU64>);

// The adapter clock projects to the same three tiers as the boot clock,
// yet the two stay distinct types so a reading from one cannot fold into
// the other at a type check or a cache key.
using PtpHwU64 = PtpHwClockBytes<unsigned long long>;
static_assert(PtpHwU64::source == ClockSource_v::PtpHwClock);
static_assert(PtpHwU64::det_safe_tier == DetSafeTier_v::MonotonicClockRead);
static_assert(PtpHwU64::suspend_behavior == SuspendBehavior_v::KeepsTicking);
static_assert(PtpHwU64::pinning_requirement == PinningRequirement_v::NotRequired);
static_assert(PtpHwU64::det_safe_tier != DetSafeTier_v::Pure);
static_assert(PtpHwU64::satisfies<ClockSource_v::Boot>,
              "PtpHwClockBytes projects to the same three tiers as the boot clock, so "
              "it satisfies a Boot requirement even though the source identity "
              "differs.");
static_assert(PtpHwU64::satisfies<ClockSource_v::Monotonic>);
static_assert(!std::is_same_v<PtpHwU64, BootU64>,
              "PtpHwClockBytes and BootClockBytes are distinct types.  They project to "
              "the same tiers but name different sources, and the federation cache key "
              "keeps them apart.");
static_assert(!std::is_convertible_v<PtpHwU64, BootU64>);
static_assert(!std::is_convertible_v<BootU64, PtpHwU64>);

static_assert(BootU64::lattice_name() == "ClockSourceLattice::At<Boot>");
static_assert(MonoU64::lattice_name() == "ClockSourceLattice::At<Monotonic>");
static_assert(TscU64::lattice_name() == "ClockSourceLattice::At<TscRaw>");
// Reflection renders `unsigned long long` as `long long unsigned int`, so
// this matches on containment.  The int witness below pins an exact
// suffix.
static_assert(BootU64::value_type_name().find("long") != std::string_view::npos);
static_assert(BootClockBytes<int>::value_type_name().ends_with("int"));

template <typename Clock>
concept keeps_ticking_through_suspend = Clock::template satisfies<ClockSource_v::Boot>;

static_assert(keeps_ticking_through_suspend<BootU64>, "A boot-time read passes the deadline gate.");
static_assert(keeps_ticking_through_suspend<TscU64>,
              "A timestamp-counter read passes the deadline gate, because it keeps "
              "ticking.");
static_assert(!keeps_ticking_through_suspend<MonoU64>,
              "A monotonic read is rejected at the deadline gate.  It pauses on "
              "suspend, so a deadline measured against it under-counts the wall time "
              "spent across a suspend and resume.");
static_assert(!keeps_ticking_through_suspend<RealU64>);

}  // namespace detail::clock_source_invariants

}  // namespace fixy
