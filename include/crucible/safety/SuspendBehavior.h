#pragma once

// ── crucible::safety::SuspendBehavior<SuspendBehavior Behavior, T> ───
//
// FIXY-V-188 (Agent 6 §3.3 item 4): a phantom-typed clock-pause-on-suspend
// witness over the V-181 SuspendBehaviorLattice (the 3-element chain
// Unknown ⊏ PausesOnSuspend ⊏ KeepsTicking).  It pins, at the TYPE level,
// whether a clock value's underlying source advances through a
// suspend/resume cycle — distinguishing CLOCK_MONOTONIC (PausesOnSuspend)
// from CLOCK_BOOTTIME (KeepsTicking).
//
//   Substrate: Graded<ModalityKind::Absolute, SuspendBehaviorLattice::At<Behavior>, T>
//   Regime:    1 (zero-cost EBO collapse — At<Behavior>::element_type is
//              empty, sizeof(SuspendBehavior<Behavior, T>) == sizeof(T)).
//
// ── The load-bearing consumer gate (V-194 DeadlineWatchdog) ─────────
//
// A deadline watchdog must time against a clock that KEEPS TICKING through
// suspend; otherwise a 10-minute suspend looks like "no elapsed time" and
// the watchdog wrongly reports "no stall" right after resume — the
// Scenario-6 bug where a Keeper gates a Cipher commit on a stale verdict.
// SuspendBehavior exposes `satisfies<Required>` (the chain subsumption):
//
//   satisfies<Required> := SuspendBehaviorLattice::leq(Required, Behavior)
//        (this Behavior is AT-OR-ABOVE the required floor)
//
//   KeepsTicking::satisfies<KeepsTicking>     == TRUE   (admitted by V-194)
//   PausesOnSuspend::satisfies<KeepsTicking>  == FALSE  (rejected by V-194)
//   Unknown::satisfies<KeepsTicking>          == FALSE
//
// V-194's DeadlineWatchdog requires `satisfies<KeepsTicking>`; a
// PausesOnSuspend (CLOCK_MONOTONIC) witness is a compile error there, which
// forces the watchdog onto CLOCK_BOOTTIME.  A background worker doing
// spilled-state freshness checks across suspends legitimately uses a
// KeepsTicking witness (the sentinel pins that positive).
//
// ── Relationship to ClockSource (V-185) ─────────────────────────────
//
// SuspendBehavior is the AXIS-ISOLATED witness for ONE of the three axes
// ClockSourceLattice (V-184) multiplies (DetSafe × SuspendBehavior ×
// Pinning).  ClockSource<Source, T> (V-185) carries the whole projected
// triple; SuspendBehavior<Behavior, T> carries just the suspend axis, for
// call sites that care only about pause-on-suspend (e.g. the V-194 gate).
//
// ── §XVI / row_hash ─────────────────────────────────────────────────
//
// row_hash_contribution<SuspendBehavior<Behavior, Inner>> (salt 0x33) ships
// in safety/diag/RowHashFold.h — the row_hash key is the WRAPPER, never the
// lattice At<> (FIXY-V-181 deferred its row_hash here).  Sits in the
// Synchronization neighborhood alongside the other Agent-6 clock/sched
// witnesses.
//
//   Axiom coverage:
//     TypeSafe — SuspendBehavior is a strong scoped enum; two
//                different-behavior witnesses are DISTINCT types (so V-194
//                can statically require one and reject the other).
//     MemSafe — defaulted copy/move; T's semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//     DetSafe — the pause-on-suspend pin is the type-level WITNESS that a
//                deadline/freshness computation is suspend-correct.
//   Runtime cost: sizeof(SuspendBehavior<Behavior, T>) == sizeof(T).
//
// §XXI: `mint_suspend_behavior<Behavior, T>(args...)`.  HS14 fixtures:
// neg_suspend_behavior_pauses_at_watchdog / neg_suspend_behavior_cross_assign.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

#include <concepts>
#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

using ::crucible::algebra::lattices::SuspendBehaviorLattice;
using SuspendBehavior_v = ::crucible::algebra::lattices::SuspendBehavior;

template <SuspendBehavior_v Behavior, typename T>
class [[nodiscard]] SuspendBehavior {
public:
    using value_type   = T;
    using lattice_type = SuspendBehaviorLattice::template At<Behavior>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned suspend behavior — read by the V-194 gate without
    // instantiating the wrapper.
    static constexpr SuspendBehavior_v behavior = Behavior;

private:
    graded_type impl_;

public:
    // ── Construction ────────────────────────────────────────────────
    constexpr SuspendBehavior() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    constexpr explicit SuspendBehavior(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit SuspendBehavior(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    constexpr SuspendBehavior(const SuspendBehavior&)            = default;
    constexpr SuspendBehavior(SuspendBehavior&&)                 = default;
    constexpr SuspendBehavior& operator=(const SuspendBehavior&) = default;
    constexpr SuspendBehavior& operator=(SuspendBehavior&&)      = default;
    ~SuspendBehavior()                                           = default;

    // Equality: compares value bytes within the SAME suspend behavior.
    [[nodiscard]] friend constexpr bool operator==(
        SuspendBehavior const& a, SuspendBehavior const& b)
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
    constexpr void swap(SuspendBehavior& other) noexcept(std::is_nothrow_swappable_v<T>) {
        impl_.swap(other.impl_);
    }
    friend constexpr void swap(SuspendBehavior& a, SuspendBehavior& b)
        noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // ── satisfies<Required> — chain subsumption (the V-194 gate) ────
    //
    // TRUE iff this Behavior is at-or-above the Required floor in the
    // suspend chain.  V-194's DeadlineWatchdog gates on
    // `satisfies<KeepsTicking>` — only a KeepsTicking witness passes.
    template <SuspendBehavior_v Required>
    static constexpr bool satisfies =
        SuspendBehaviorLattice::leq(Required, Behavior);
};

// ── §XXI mint factory (token mint) ──────────────────────────────────
template <SuspendBehavior_v Behavior, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr SuspendBehavior<Behavior, T> mint_suspend_behavior(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return SuspendBehavior<Behavior, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases (named suspend_witness to avoid clashing with
//    the algebra::lattices::suspend_behavior At<> aliases) ───────────
namespace suspend_witness {
    template <typename T> using Unknown         = SuspendBehavior<SuspendBehavior_v::Unknown,         T>;
    template <typename T> using PausesOnSuspend  = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, T>;
    template <typename T> using KeepsTicking     = SuspendBehavior<SuspendBehavior_v::KeepsTicking,    T>;
}  // namespace suspend_witness

// ── Layout invariants — regime-1 EBO collapse ───────────────────────
namespace detail::suspend_behavior_layout {

template <typename T> using PausesSb = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, T>;
template <typename T> using KeepsSb  = SuspendBehavior<SuspendBehavior_v::KeepsTicking,    T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(PausesSb, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(KeepsSb,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(KeepsSb,  unsigned long long);

}  // namespace detail::suspend_behavior_layout

static_assert(sizeof(SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, int>) == sizeof(int));
static_assert(sizeof(SuspendBehavior<SuspendBehavior_v::KeepsTicking, unsigned long long>)
              == sizeof(unsigned long long));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::suspend_behavior_self_test {

using PausesU64 = SuspendBehavior<SuspendBehavior_v::PausesOnSuspend, unsigned long long>;
using KeepsU64  = SuspendBehavior<SuspendBehavior_v::KeepsTicking,    unsigned long long>;
using UnkU64    = SuspendBehavior<SuspendBehavior_v::Unknown,         unsigned long long>;

// ── Construction ────────────────────────────────────────────────────
inline constexpr KeepsU64 k_default{};
static_assert(k_default.peek() == 0);
static_assert(KeepsU64::behavior == SuspendBehavior_v::KeepsTicking);

inline constexpr KeepsU64 k_explicit{42};
static_assert(k_explicit.peek() == 42);

inline constexpr PausesU64 p_in_place{std::in_place, 7};
static_assert(p_in_place.peek() == 7);

static_assert(KeepsU64::modality == ::crucible::algebra::ModalityKind::Absolute);

// ── satisfies<KeepsTicking> — the V-194 DeadlineWatchdog gate ───────
static_assert( KeepsU64::satisfies<SuspendBehavior_v::KeepsTicking>,
    "FIXY-V-188: a KeepsTicking (CLOCK_BOOTTIME) witness MUST satisfy a "
    "KeepsTicking deadline-watchdog requirement.");
static_assert(!PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>,
    "FIXY-V-188: a PausesOnSuspend (CLOCK_MONOTONIC) witness MUST NOT satisfy "
    "a KeepsTicking requirement — V-194 forces CLOCK_BOOTTIME, closing the "
    "10-minute-suspend bug class.");
static_assert(!UnkU64::satisfies<SuspendBehavior_v::KeepsTicking>);
// Every behavior trivially satisfies the Unknown (⊥) floor.
static_assert(PausesU64::satisfies<SuspendBehavior_v::Unknown>);
static_assert(KeepsU64::satisfies<SuspendBehavior_v::PausesOnSuspend>,
    "KeepsTicking ⊒ PausesOnSuspend — a suspend-inclusive clock also meets a "
    "pause-tolerating requirement.");
static_assert(!PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>);

// ── Distinct types per behavior (the V-194 static-distinction basis) ─
static_assert(!std::is_same_v<PausesU64, KeepsU64>);
static_assert(!std::is_convertible_v<PausesU64, KeepsU64>);

// ── Diagnostic forwarders ──────────────────────────────────────────
static_assert(KeepsU64::lattice_name()  == "SuspendBehaviorLattice::At<KeepsTicking>");
static_assert(PausesU64::lattice_name() == "SuspendBehaviorLattice::At<PausesOnSuspend>");
static_assert(KeepsU64::value_type_name().find("long") != std::string_view::npos);

// ── swap / peek_mut / operator== ───────────────────────────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_behavior() noexcept {
    KeepsU64 a{10}; KeepsU64 b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_behavior());

[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    KeepsU64 a{42}; KeepsU64 b{42}; KeepsU64 c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// ── mint_suspend_behavior factory ──────────────────────────────────
inline constexpr auto minted =
    mint_suspend_behavior<SuspendBehavior_v::KeepsTicking, unsigned long long>(99);
static_assert(minted.peek() == 99 && minted.behavior == SuspendBehavior_v::KeepsTicking);

// ── Alias resolution ───────────────────────────────────────────────
static_assert(std::is_same_v<suspend_witness::KeepsTicking<unsigned long long>, KeepsU64>);
static_assert(std::is_same_v<suspend_witness::PausesOnSuspend<unsigned long long>, PausesU64>);

// ── DeadlineWatchdog gate + BgWorker positive (V-194 shape) ─────────
//
// A deadline watchdog admits only a clock that keeps ticking through
// suspend; a background-worker freshness check legitimately accepts the
// same KeepsTicking witness (the task's positive fixture).
template <typename Witness>
concept survives_suspend = Witness::template satisfies<SuspendBehavior_v::KeepsTicking>;

static_assert( survives_suspend<KeepsU64>,
    "A CLOCK_BOOTTIME witness MUST pass the deadline-watchdog gate.");
static_assert(!survives_suspend<PausesU64>,
    "A CLOCK_MONOTONIC witness MUST be rejected at the deadline-watchdog gate.");
static_assert( survives_suspend<suspend_witness::KeepsTicking<int>>,
    "BgWorker spilled-state freshness checks across suspends legitimately use "
    "a KeepsTicking witness — this MUST compile.");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    unsigned long long seed = 21;
    KeepsU64 k{seed * 2};
    if (k.peek() != 42) std::abort();
    k.peek_mut() = 9;
    if (k.peek() != 9) std::abort();

    auto m = mint_suspend_behavior<SuspendBehavior_v::PausesOnSuspend, unsigned long long>(seed);
    if (std::move(m).consume() != 21) std::abort();

    KeepsU64 a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();

    [[maybe_unused]] bool g1 = KeepsU64::satisfies<SuspendBehavior_v::KeepsTicking>;
    [[maybe_unused]] bool g2 = PausesU64::satisfies<SuspendBehavior_v::KeepsTicking>;
    if (!g1 || g2) std::abort();

    suspend_witness::Unknown<unsigned long long> u{123};
    if (u.peek() != 123) std::abort();
}

}  // namespace detail::suspend_behavior_self_test

}  // namespace crucible::safety
