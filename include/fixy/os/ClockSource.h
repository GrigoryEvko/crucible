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
//
// Old spelling: include/crucible/safety/ClockSource.h.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

using ::foundation::algebra::lattices::ClockSourceLattice;
using ClockSource_v = ::foundation::algebra::lattices::ClockSource;
using DetSafeTier_v = ::foundation::algebra::lattices::DetSafeTier;
using SuspendBehavior_v = ::foundation::algebra::lattices::SuspendBehavior;
using PinningRequirement_v = ::foundation::algebra::lattices::PinningRequirement;

template <ClockSource_v Source, typename T>
class [[nodiscard]] ClockSource
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

public:
    constexpr ClockSource() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ClockSource(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ClockSource(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                             && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), typename lattice_type::element_type{}} {}

    constexpr ClockSource(const ClockSource&) = default;
    constexpr ClockSource(ClockSource&&) = default;
    constexpr ClockSource& operator=(const ClockSource&) = default;
    constexpr ClockSource& operator=(ClockSource&&) = default;
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
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(ClockSource& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }
    friend constexpr void swap(ClockSource& a, ClockSource& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // True when this source covers the required one on all three axes at
    // once, not on any single axis alone.
    template <ClockSource_v Required>
    static constexpr bool satisfies =
        ClockSourceLattice::leq(::foundation::algebra::lattices::clock_source_project(Required),
                                ::foundation::algebra::lattices::clock_source_project(Source));
};

template <ClockSource_v Source, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ClockSource<Source, T>
mint_clock_source(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return ClockSource<Source, T>{std::in_place, std::forward<Args>(args)...};
}

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

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RealtimeClockBytes, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MonotonicClockBytes, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockBytes, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockBytes, unsigned long long);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TscBytes, unsigned long long);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PmuBytes, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PtpHwClockBytes, unsigned long long);

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

inline constexpr BootU64 b_default{};
static_assert(b_default.peek() == 0);
static_assert(BootU64::source == ClockSource_v::Boot);

inline constexpr BootU64 b_explicit{42};
static_assert(b_explicit.peek() == 42);

inline constexpr BootU64 b_in_place{std::in_place, 7};
static_assert(b_in_place.peek() == 7);

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

[[nodiscard]] consteval bool swap_exchanges_within_same_source() noexcept {
    BootU64 a{10};
    BootU64 b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_source());

[[nodiscard]] consteval bool peek_mut_works() noexcept {
    BootU64 a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    BootU64 a{42};
    BootU64 b{42};
    BootU64 c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

inline constexpr auto minted = mint_clock_source<ClockSource_v::Boot, unsigned long long>(99);
static_assert(minted.peek() == 99 && minted.source == ClockSource_v::Boot);

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
