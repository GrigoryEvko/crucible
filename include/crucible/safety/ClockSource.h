#pragma once

// ── crucible::safety::ClockSource<ClockSource_v Source, typename T> ──
//
// FIXY-V-185 (Agent 6 §3.3 item 1): value-level Graded carrier for the
// V-184 ClockSource axis (ClockSourceLattice — a PRODUCT composite over
// DetSafe × SuspendBehavior × Pinning).  Pins, at the TYPE level, WHICH
// clock produced a time read — so a consumer that needs a suspend-
// inclusive monotonic clock can reject a wall-clock or a pause-on-suspend
// read at compile time.  The provenance sibling of ScopedFence (V-267).
//
//   Substrate: Graded<ModalityKind::Absolute, ClockSourceLattice::At<Source>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Source>::element_type is
//              empty, sizeof(ClockSource<Source, T>) == sizeof(T) at -O3).
//
// ── Provenance, NOT relaxation ──────────────────────────────────────
//
// Unlike ScopedFence — where a wider fence genuinely publishes at every
// narrower scope it dominates, so `relax<Narrower>()` is sound — a clock
// SOURCE is a physical provenance fact.  A CLOCK_BOOTTIME read cannot be
// soundly re-labelled as a CLOCK_MONOTONIC read (they tick differently
// across suspend), so ClockSource ships NO `relax`.  The source is fixed
// at construction and travels with the value.
//
// What it DOES expose is the projected (DetSafe, Suspend, Pinning) tuple
// the V-184 lattice assigns each source, so a consumer can gate on the
// exact axis it cares about:
//   - `det_safe_tier`        — DetSafeTier (WallClockRead vs MonotonicClockRead)
//   - `suspend_behavior`     — SuspendBehavior (PausesOnSuspend vs KeepsTicking)
//   - `pinning_requirement`  — PinningRequirement (NotRequired vs PerCore)
//   - `satisfies<Required>`  — whole-point PRODUCT subsumption: TRUE iff
//                              Required's projected point ⊑ this source's
//                              projected point on ALL THREE axes.
//
// ── The load-bearing consumer gate (V-194 DeadlineWatchdog) ─────────
//
//   BootClockBytes<uint64_t>::satisfies<ClockSource::Boot>      == TRUE
//   MonotonicClockBytes<uint64_t>::satisfies<ClockSource::Boot> == FALSE
//
// because Boot projects to KeepsTicking and Monotonic to PausesOnSuspend,
// and PausesOnSuspend ⋣ KeepsTicking on the suspend axis.  V-194's deadline
// watchdog requires a clock that keeps ticking through suspend; it admits
// BootClockBytes and rejects MonotonicClockBytes via exactly this gate.
//
// ── §XVI canonical wrapper-nesting position ─────────────────────────
//
// ClockSource is a Representation-neighborhood wrapper (Tier-L Lattice,
// peer to Vendor / NumaPlacement / SimdWidthPinned / ScopedFence), sitting
// between the Representation cluster and ResidencyHeat in the canonical
// outer→inner order.  The row_hash_contribution<ClockSource<Source, Inner>>
// federation-cache discriminator (salt 0x30) ships in
// safety/diag/RowHashFold.h — the row_hash key is the WRAPPER, never the
// lattice At<> (FIXY-V-184 deferred its row_hash here for exactly this
// reason).
//
//   Axiom coverage:
//     TypeSafe — ClockSource is a strong scoped enum; two different-source
//                wrappers are DISTINCT types (BootClockBytes ≠
//                MonotonicClockBytes), so a cross-source assignment is a
//                compile error (neg_clock_source_cross_source_assign.cpp).
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — the source pin is the type-level WITNESS of a time read's
//                provenance; NO ClockSource projects to DetSafeTier::Pure
//                (a clock read is never a pure function of declared
//                inputs), which a Pure-requiring context rejects.
//   Runtime cost: sizeof(ClockSource<Source, T>) == sizeof(T); verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.
//
// §XXI: `mint_clock_source<Source, T>(args...)`.  HS14 neg fixtures:
// neg_clock_source_cross_source_assign.cpp +
// neg_clock_source_suspend_gate_rejects_monotonic.cpp.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ClockSourceLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::ClockSourceLattice;
using ClockSource_v        = ::crucible::algebra::lattices::ClockSource;
using DetSafeTier_v        = ::crucible::algebra::lattices::DetSafeTier;
using SuspendBehavior_v    = ::crucible::algebra::lattices::SuspendBehavior;
using PinningRequirement_v = ::crucible::algebra::lattices::PinningRequirement;

template <ClockSource_v Source, typename T>
class [[nodiscard]] ClockSource {
public:
    // ── Public type aliases (GradedWrapper uniform surface) ─────────
    using value_type   = T;
    using lattice_type = ClockSourceLattice::At<Source>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned clock source — exposed for source-aware dispatch without
    // instantiating the wrapper.
    static constexpr ClockSource_v source = Source;

    // ── Projected (DetSafe, Suspend, Pinning) axes ──────────────────
    //
    // The V-184 lattice point this source pins, broken out per axis so a
    // consumer gates on the one it cares about (e.g. V-194 reads
    // suspend_behavior).  Computed via the constexpr projection — folds
    // away entirely at -O3.
    static constexpr DetSafeTier_v det_safe_tier =
        ClockSourceLattice::get<0>(
            ::crucible::algebra::lattices::clock_source_project(Source));
    static constexpr SuspendBehavior_v suspend_behavior =
        ClockSourceLattice::get<1>(
            ::crucible::algebra::lattices::clock_source_project(Source));
    static constexpr PinningRequirement_v pinning_requirement =
        ClockSourceLattice::get<2>(
            ::crucible::algebra::lattices::clock_source_project(Source));

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr ClockSource() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit ClockSource(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ClockSource(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr ClockSource(const ClockSource&)            = default;
    constexpr ClockSource(ClockSource&&)                 = default;
    constexpr ClockSource& operator=(const ClockSource&) = default;
    constexpr ClockSource& operator=(ClockSource&&)      = default;
    ~ClockSource()                                       = default;

    // Equality: compares value bytes within the SAME clock source.
    // Cross-source comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        ClockSource const& a, ClockSource const& b)
        noexcept(noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only / mutable access ──────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept { return impl_.peek(); }
    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    { return std::move(impl_).consume(); }
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(ClockSource& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(ClockSource& a, ClockSource& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Required> — whole-point PRODUCT subsumption ───────
    //
    // TRUE iff this source's projected point SUBSUMES the Required source's
    // projected point (Required ⊑ Source on ALL THREE axes in the
    // ClockSourceLattice partial order).  The DeadlineWatchdog gate is
    // `satisfies<Boot>` — admits Boot/TscRaw/TscSerialized/PmuCounter
    // (KeepsTicking), rejects Realtime/Monotonic/ThreadCpu (PausesOnSuspend).
    template <ClockSource_v Required>
    static constexpr bool satisfies = ClockSourceLattice::leq(
        ::crucible::algebra::lattices::clock_source_project(Required),
        ::crucible::algebra::lattices::clock_source_project(Source));
};

// ── §XXI mint factory ───────────────────────────────────────────────
//
// Token mint: wraps a value in a clock-source provenance carrier.
template <ClockSource_v Source, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr ClockSource<Source, T> mint_clock_source(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return ClockSource<Source, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases (Agent 6 §3.3 enumerated) ───────────────────
template <typename T> using RealtimeClockBytes      = ClockSource<ClockSource_v::Realtime,      T>;
template <typename T> using MonotonicClockBytes     = ClockSource<ClockSource_v::Monotonic,     T>;
template <typename T> using MonotonicRawClockBytes  = ClockSource<ClockSource_v::MonotonicRaw,  T>;
template <typename T> using BootClockBytes          = ClockSource<ClockSource_v::Boot,          T>;
template <typename T> using ThreadCpuBytes          = ClockSource<ClockSource_v::ThreadCpu,     T>;
template <typename T> using ProcessCpuBytes         = ClockSource<ClockSource_v::ProcessCpu,    T>;
template <typename T> using TscBytes                = ClockSource<ClockSource_v::TscRaw,        T>;
template <typename T> using TscSerializedBytes      = ClockSource<ClockSource_v::TscSerialized, T>;
template <typename T> using PmuBytes                = ClockSource<ClockSource_v::PmuCounter,    T>;

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::clock_source_layout {

CRUCIBLE_GRADED_LAYOUT_INVARIANT(RealtimeClockBytes,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(MonotonicClockBytes, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockBytes,      int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BootClockBytes,      unsigned long long);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(TscBytes,            unsigned long long);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PmuBytes,            int);

}  // namespace detail::clock_source_layout

static_assert(sizeof(RealtimeClockBytes<int>)                  == sizeof(int));
static_assert(sizeof(MonotonicClockBytes<int>)                 == sizeof(int));
static_assert(sizeof(BootClockBytes<unsigned long long>)       == sizeof(unsigned long long));
static_assert(sizeof(TscBytes<unsigned long long>)             == sizeof(unsigned long long));
static_assert(sizeof(PmuBytes<int>)                            == sizeof(int));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::clock_source_self_test {

using BootU64    = BootClockBytes<unsigned long long>;
using MonoU64    = MonotonicClockBytes<unsigned long long>;
using RealU64    = RealtimeClockBytes<unsigned long long>;
using TscU64     = TscBytes<unsigned long long>;
using ThreadU64  = ThreadCpuBytes<unsigned long long>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr BootU64 b_default{};
static_assert(b_default.peek() == 0);
static_assert(BootU64::source == ClockSource_v::Boot);

inline constexpr BootU64 b_explicit{42};
static_assert(b_explicit.peek() == 42);

inline constexpr BootU64 b_in_place{std::in_place, 7};
static_assert(b_in_place.peek() == 7);

static_assert(BootU64::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── Projected axes — the V-184 lattice points, per source ───────────
static_assert(BootU64::det_safe_tier       == DetSafeTier_v::MonotonicClockRead);
static_assert(BootU64::suspend_behavior    == SuspendBehavior_v::KeepsTicking);
static_assert(BootU64::pinning_requirement == PinningRequirement_v::NotRequired);

static_assert(MonoU64::suspend_behavior    == SuspendBehavior_v::PausesOnSuspend);
static_assert(RealU64::det_safe_tier       == DetSafeTier_v::WallClockRead);
static_assert(TscU64::pinning_requirement  == PinningRequirement_v::PerCore);
static_assert(TscU64::suspend_behavior     == SuspendBehavior_v::KeepsTicking);

// ── NO ClockSource is DetSafe::Pure — the Pure-context rejection ────
//
// A clock read is never a pure function of declared inputs; every source
// projects to WallClockRead or MonotonicClockRead, NEVER Pure.  A context
// requiring DetSafeTier::Pure rejects every clock-sourced value.
static_assert(BootU64::det_safe_tier   != DetSafeTier_v::Pure);
static_assert(MonoU64::det_safe_tier   != DetSafeTier_v::Pure);
static_assert(RealU64::det_safe_tier   != DetSafeTier_v::Pure);
static_assert(TscU64::det_safe_tier    != DetSafeTier_v::Pure);
static_assert(ThreadU64::det_safe_tier != DetSafeTier_v::Pure);

// ── satisfies<Required> — the V-194 DeadlineWatchdog gate ───────────
//
// Boot keeps ticking through suspend ⇒ subsumes a Boot requirement; the
// pause-on-suspend clocks do NOT.
static_assert( BootU64::satisfies<ClockSource_v::Boot>,
    "FIXY-V-185: BootClockBytes MUST satisfy a Boot requirement — it keeps "
    "ticking through suspend (the V-194 DeadlineWatchdog gate admits it).");
static_assert(!MonoU64::satisfies<ClockSource_v::Boot>,
    "FIXY-V-185: MonotonicClockBytes MUST NOT satisfy a Boot requirement — "
    "CLOCK_MONOTONIC pauses on suspend (PausesOnSuspend ⋣ KeepsTicking).  "
    "This is the V-194 rejection that the deadline watchdog depends on.");
static_assert( TscU64::satisfies<ClockSource_v::Boot>,
    "TscRaw keeps ticking through suspend ⇒ subsumes a Boot requirement on "
    "the suspend + det-safe axes (PerCore ⊒ NotRequired on pinning).");
static_assert(!RealU64::satisfies<ClockSource_v::Boot>);
// Boot subsumes Monotonic (Boot is weaker-or-equal on every axis below it):
// Monotonic ⊑ Boot ⇒ Boot satisfies a Monotonic requirement.
static_assert( BootU64::satisfies<ClockSource_v::Monotonic>);
static_assert(!MonoU64::satisfies<ClockSource_v::TscRaw>,
    "Monotonic does NOT subsume TscRaw — PerCore ⋣ NotRequired on pinning.");

// ── Distinct types per source — the V-194 static-distinction basis ──
static_assert(!std::is_same_v<BootU64, MonoU64>,
    "FIXY-V-185: BootClockBytes and MonotonicClockBytes MUST be DISTINCT "
    "types so V-194 can statically require one and reject the other.");
static_assert(!std::is_same_v<TscBytes<int>, PmuBytes<int>>);
static_assert(!std::is_convertible_v<MonoU64, BootU64>);

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(BootU64::lattice_name() == "ClockSourceLattice::At<Boot>");
static_assert(MonoU64::lattice_name() == "ClockSourceLattice::At<Monotonic>");
static_assert(TscU64::lattice_name()  == "ClockSourceLattice::At<TscRaw>");
// GCC reflection renders `unsigned long long` as `long long unsigned int`,
// so check containment, not a specific suffix; the int witness pins a
// concrete suffix.
static_assert(BootU64::value_type_name().find("long") != std::string_view::npos);
static_assert(BootClockBytes<int>::value_type_name().ends_with("int"));

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_source() noexcept {
    BootU64 a{10}; BootU64 b{20};
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
    BootU64 a{42}; BootU64 b{42}; BootU64 c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── mint_clock_source factory ──────────────────────────────────────
inline constexpr auto minted = mint_clock_source<ClockSource_v::Boot, unsigned long long>(99);
static_assert(minted.peek() == 99 && minted.source == ClockSource_v::Boot);

// ── Deadline-watchdog gate simulation (V-194 shape) ────────────────
//
// A deadline watchdog admits only a clock that keeps ticking through
// suspend — i.e. one that subsumes a Boot requirement.
template <typename Clock>
concept keeps_ticking_through_suspend = Clock::template satisfies<ClockSource_v::Boot>;

static_assert( keeps_ticking_through_suspend<BootU64>,
    "A CLOCK_BOOTTIME read MUST pass the deadline-watchdog gate.");
static_assert( keeps_ticking_through_suspend<TscU64>,
    "A TSC read MUST pass the deadline-watchdog gate (keeps ticking).");
static_assert(!keeps_ticking_through_suspend<MonoU64>,
    "A CLOCK_MONOTONIC read MUST be REJECTED at the deadline-watchdog gate "
    "— it pauses on suspend, so a deadline computed against it under-counts "
    "wall time across a suspend/resume cycle.");
static_assert(!keeps_ticking_through_suspend<RealU64>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: pure static_asserts
// can mask consteval/SFINAE/inline-body bugs; runtime ops with
// non-constant arguments catch them.
inline void runtime_smoke_test() {
    unsigned long long seed = 21;
    BootU64 n{seed * 2};
    if (n.peek() != 42) std::abort();
    n.peek_mut() = 9;
    if (n.peek() != 9) std::abort();

    auto m = mint_clock_source<ClockSource_v::TscRaw, unsigned long long>(seed);
    if (std::move(m).consume() != 21) std::abort();

    BootU64 a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool g1 = BootU64::satisfies<ClockSource_v::Boot>;
    [[maybe_unused]] bool g2 = MonoU64::satisfies<ClockSource_v::Boot>;
    if (!g1 || g2) std::abort();

    // Alias instantiation across sources.
    RealtimeClockBytes<unsigned long long> rt{123};
    PmuBytes<unsigned long long>           pmu{456};
    if (rt.peek() != 123 || pmu.peek() != 456) std::abort();
}

}  // namespace detail::clock_source_self_test

}  // namespace crucible::safety
