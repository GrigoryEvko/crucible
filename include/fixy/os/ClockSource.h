#pragma once

// Pins which clock produced a time reading, so a consumer that needs a
// particular kind of clock can turn away the others at compile time.
//
// There is no widening or relabelling operation.  A source is a physical
// fact about where the reading came from, and two clocks that tick
// differently across a suspend cannot stand in for one another.  What
// the wrapper does expose is the tier each source projects to on the
// determinism, suspend and pinning axes, so a consumer can gate on the
// one property it actually depends on.

#include <fixy/GradedFacade.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy::time::detail {
// The door that stamps a value with the clock it came from.  It is
// defined in fixy/os/Time.h beside the readers, and only they reach it.
struct clock_stamp_access;
}  // namespace fixy::time::detail

namespace fixy {

using ::foundation::algebra::lattices::ClockSourceLattice;
using ClockSource_v = ::foundation::algebra::lattices::ClockSource;
using DetSafeTier_v = ::foundation::algebra::lattices::DetSafeTier;
using SuspendBehavior_v = ::foundation::algebra::lattices::SuspendBehavior;
using PinningRequirement_v = ::foundation::algebra::lattices::PinningRequirement;

template <ClockSource_v Source, typename T>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] ClockSource
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute, ClockSourceLattice::At<Source>, T> {
public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the names this class
    // body uses unqualified are re-declared here rather than found by
    // lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute, ClockSourceLattice::At<Source>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

    static constexpr ClockSource_v source = Source;

    static constexpr DetSafeTier_v det_safe_tier =
        ClockSourceLattice::get<0>(::foundation::algebra::lattices::clock_source_project(Source));
    static constexpr SuspendBehavior_v suspend_behavior =
        ClockSourceLattice::get<1>(::foundation::algebra::lattices::clock_source_project(Source));
    static constexpr PinningRequirement_v pinning_requirement =
        ClockSourceLattice::get<2>(::foundation::algebra::lattices::clock_source_project(Source));

private:
    graded_type impl_;

    // The one constructor that stamps a value with a source, and it is
    // private.  Its sole friend is fixy::time::detail::clock_stamp_access,
    // whose stamp is private too and reached only by the readers in
    // fixy/os/Time.h, right after the read of this same clock.  So a
    // ClockSource<Boot, T> holds a value that CLOCK_BOOTTIME returned.
    constexpr explicit ClockSource(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{::foundation::algebra::grade_key<ClockSource>{}, std::move(value),
                typename lattice_type::element_type{}} {}

    friend struct ::fixy::time::detail::clock_stamp_access;

public:
    // There is no default constructor, no in_place constructor and no
    // free mint.  Each of them builds a reading that no clock returned.
    ClockSource() = delete("a default-constructed ClockSource would claim a reading that no clock returned.  Read "
                           "the clock through a reader from fixy::time::mint_clock_reader, mint_tsc_reader or "
                           "mint_ptp_clock_reader.");

    // A copy of a reading is still a reading of the same clock, so copies
    // stay.  The constructors are trivial, so a reading passes in a
    // register.  The assignments are user-provided, so the class is not
    // trivially copyable: std::bit_cast refuses to stamp a raw integer
    // with a source, and -Wclass-memaccess refuses a memcpy into one.
    // The annotation on the class refuses the checked lifetime start over
    // bytes, and utils/scripts/check-start-lifetime.py refuses the raw one.
    constexpr ClockSource(const ClockSource&) = default;
    constexpr ClockSource(ClockSource&&) = default;
    constexpr ClockSource& operator=(const ClockSource& other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        impl_ = other.impl_;
        return *this;
    }
    constexpr ClockSource& operator=(ClockSource&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        impl_ = std::move(other.impl_);
        return *this;
    }
    ~ClockSource() = default;

    [[nodiscard]] friend constexpr bool operator==(ClockSource const& a,
                                                   ClockSource const& b) noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) {
            { x == y } -> std::convertible_to<bool>;
        }
    {
        return a.peek() == b.peek();
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    // There is no peek_mut().  A mutable reference would let a holder
    // write any value under the source of a real reading.

    constexpr void swap(ClockSource& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ClockSource& a, ClockSource& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // True when this source covers the required one on all three axes at
    // once, not on any single axis alone.
    template <ClockSource_v Required>
    static constexpr bool satisfies =
        ClockSourceLattice::leq(::foundation::algebra::lattices::clock_source_project(Required),
                                ::foundation::algebra::lattices::clock_source_project(Source));
};

template <typename T>
using RealtimeClockBytes = ClockSource<ClockSource_v::Realtime, T>;
template <typename T>
using MonotonicClockBytes = ClockSource<ClockSource_v::Monotonic, T>;
template <typename T>
using MonotonicRawClockBytes = ClockSource<ClockSource_v::MonotonicRaw, T>;
template <typename T>
using BootClockBytes = ClockSource<ClockSource_v::Boot, T>;
template <typename T>
using ThreadCpuBytes = ClockSource<ClockSource_v::ThreadCpu, T>;
template <typename T>
using ProcessCpuBytes = ClockSource<ClockSource_v::ProcessCpu, T>;
template <typename T>
using TscBytes = ClockSource<ClockSource_v::TscRaw, T>;
template <typename T>
using TscSerializedBytes = ClockSource<ClockSource_v::TscSerialized, T>;
template <typename T>
using PmuBytes = ClockSource<ClockSource_v::PmuCounter, T>;
// A clock on the network adapter itself.  Its oscillator runs
// independently of the host, so it keeps ticking through a host suspend,
// and reading it needs no CPU pin.
template <typename T>
using PtpHwClockBytes = ClockSource<ClockSource_v::PtpHwClock, T>;

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
