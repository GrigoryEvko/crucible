#pragma once

// ── crucible::fixy::sync::Wait — value-level wait-strategy surface ──
//
// FIXY-V-084 (file 1 of 2 in fixy/sync/ family — companion is
// MemOrderGrant.h).  Surfaces `safety::Wait<Strategy, T>` (the FOUND-G24
// chain wrapper) at `fixy::sync::Wait<Strategy, T>` for band-3
// consumers (cntp/, canopy/, cog/, topology/, forge/, mimic/, observe/,
// warden/) per the CRUCIBLE_FIXY_ONLY discipline (FIXY-V-070..072).
//
// THE LOAD-BEARING ROLE: type-fences CLAUDE.md §IX.5's latency
// hierarchy.  The substrate's `Wait<Strategy, T>` carries the pinned
// wait-mechanism at the value level; this header makes the wrapper
// reachable through the fixy:: umbrella so a band-3 caller writes
//
//   fixy::sync::Wait<fixy::sync::Strategy::SpinPause, int> waiter{42};
//
// rather than reaching directly into `safety::Wait<>`.  The discipline
// closure for the band-3 "no raw safety::*" rule (check-fixy-discipline
// .sh) is preserved.
//
// ── What this surface IS ──────────────────────────────────────────
//
//  1. `fixy::sync::Wait<Strategy, T>` — alias for the substrate's
//     `safety::Wait<Strategy, T>` Graded carrier.
//
//  2. `fixy::sync::Strategy` — alias for the substrate's `safety::
//     WaitStrategy_v` strong scoped enum.  Six enumerators
//     (SpinPause=5, BoundedSpin=4, UmwaitC01=3, AcquireWait=2,
//     Park=1, Block=0).  Subsumption-up direction: stronger wait-
//     discipline (higher ordinal) satisfies weaker requirement.
//
//  3. `fixy::sync::IsWait` concept + projection helpers
//     (`wait_value_t`, `wait_strategy_v`) — re-exports of the
//     FOUND-D26 detector surface from `safety::extract`.
//
//  4. `fixy::sync::wait::SpinPause<T>` / `BoundedSpin<T>` /
//     `UmwaitC01<T>` / `AcquireWait<T>` / `Park<T>` / `Block<T>` —
//     convenience aliases mirroring the substrate's
//     `safety::wait::*` namespace.
//
// ── What this surface IS NOT ──────────────────────────────────────
//
//  * NOT a new mint factory.  `Wait<Strategy, T>` has a public
//    explicit T-ctor at the substrate; no Ctx-bound gate is
//    meaningful (the pin is per-value, not per-binding).  HS14
//    fixtures pin the substrate's `relax<>()` strict-up-rejection
//    through the fixy surface — surface integrity, not mint gating.
//
//  * NOT a fixy::grant tag.  Synchronization is a wrapper-only axis
//    per `safety/Fn.h` DimensionAxis::Synchronization = 20 doc-block — there is NO Fn<...>
//    template-parameter slot for it.  The only fixy::grant tag for
//    DimensionAxis::Synchronization is `accept_default_strict_for<
//    DimensionAxis::Synchronization>` (already shipped in
//    `fixy/Grant.h`); this header does NOT specialize `which_dim`.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — fixy::sync::Strategy is the substrate enum verbatim;
//              cross-strategy mismatches reject at the substrate's
//              `relax<>()` requires-clause and `satisfies<>` static
//              predicate.
//   ThreadSafe — surfaces the substrate's hot-path-waiter admission
//                gate (CLAUDE.md §IX.5) for band-3 callers.
//   InitSafe / MemSafe / NullSafe / DetSafe — inherited from the
//   underlying `safety::Wait<Strategy, T>` Graded carrier.
//
// ── Runtime cost ──────────────────────────────────────────────────
//
// Zero.  Every name is a using-alias of the substrate; sentinels
// below witness identity via `std::is_same_v`.  `sizeof(fixy::sync::
// Wait<S, T>) == sizeof(T)`.
//
// ── References ────────────────────────────────────────────────────
//
// Substrate: safety/Wait.h (FOUND-G24), algebra/lattices/WaitLattice.h
// (FOUND-G23), safety/IsWait.h (FOUND-D26).
// CLAUDE.md §IX.5 — latency hierarchy + load-bearing discipline.
// fixy.md §24.1 — DimensionAxis::Synchronization = 20 axis (wrapper-only).
// 28_04_2026_effects.md §4.3.3 — production-call-site rationale.

#include <crucible/safety/Wait.h>
#include <crucible/safety/IsWait.h>

#include <type_traits>

namespace crucible::fixy::sync {

// ─── Strategy enum + wrapper class re-exports ──────────────────────
//
// The substrate ships these in `crucible::safety::` — re-export here
// so band-3 callers spell `fixy::sync::*` instead of reaching raw
// safety::*.  Naming preserved verbatim (Strategy, Wait) — the names
// already read as `fixy::sync::Wait<fixy::sync::Strategy::SpinPause,
// T>` at call sites.

using Strategy = ::crucible::safety::WaitStrategy_v;

template <Strategy S, typename T>
using Wait = ::crucible::safety::Wait<S, T>;

// ─── Lattice re-export ─────────────────────────────────────────────
//
// `WaitLattice` exposes `leq()` + `bottom()` + `top()` + `name()` for
// band-3 callers doing strategy-aware dispatch on the chain ordering.

using WaitLattice = ::crucible::safety::WaitLattice;

// ─── Detector concept + projections (FOUND-D26 surface) ────────────
//
// IsWait<T> is true iff T (with cv-ref stripped) is a Wait<S, U>
// instantiation for some S and U.  wait_value_t<T> projects to U;
// wait_strategy_v<T> projects to S.

template <typename T>
inline constexpr bool is_wait_v = ::crucible::safety::extract::is_wait_v<T>;

template <typename T>
concept IsWait = ::crucible::safety::extract::IsWait<T>;

template <typename T>
    requires is_wait_v<T>
using wait_value_t = ::crucible::safety::extract::wait_value_t<T>;

template <typename T>
    requires is_wait_v<T>
inline constexpr Strategy wait_strategy_v =
    ::crucible::safety::extract::wait_strategy_v<T>;

// ─── Convenience aliases (mirror safety::wait::*) ──────────────────
namespace wait {
    template <typename T> using SpinPause   = Wait<Strategy::SpinPause,   T>;
    template <typename T> using BoundedSpin = Wait<Strategy::BoundedSpin, T>;
    template <typename T> using UmwaitC01   = Wait<Strategy::UmwaitC01,   T>;
    template <typename T> using AcquireWait = Wait<Strategy::AcquireWait, T>;
    template <typename T> using Park        = Wait<Strategy::Park,        T>;
    template <typename T> using Block       = Wait<Strategy::Block,       T>;
}  // namespace wait

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Identity witnesses: the fixy surface MUST alias the substrate types
// verbatim.  A substrate refactor that drifts the underlying type
// reddens these cells at the fixy boundary, before band-3 consumers
// notice.

namespace detail::wait_surface_sentinel {

// Alias identity — Wait<S, T> at fixy::sync IS the substrate type.
static_assert(std::is_same_v<
    ::crucible::fixy::sync::Wait<Strategy::SpinPause, int>,
    ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::SpinPause, int>>,
    "FIXY-V-084: fixy::sync::Wait<S, T> must alias safety::Wait<S, T> "
    "verbatim.");

static_assert(std::is_same_v<
    ::crucible::fixy::sync::Wait<Strategy::Block, double>,
    ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::Block, double>>,
    "FIXY-V-084: fixy::sync::Wait<Block, double> identity drift.");

// Strategy enum identity — the type ID must be the substrate enum.
static_assert(std::is_same_v<Strategy, ::crucible::safety::WaitStrategy_v>,
    "FIXY-V-084: fixy::sync::Strategy must alias safety::WaitStrategy_v.");

// Enumerator value preservation — ordinals are load-bearing for
// `WaitLattice::leq()` chain ordering.
static_assert(static_cast<int>(Strategy::SpinPause)   == 5);
static_assert(static_cast<int>(Strategy::BoundedSpin) == 4);
static_assert(static_cast<int>(Strategy::UmwaitC01)   == 3);
static_assert(static_cast<int>(Strategy::AcquireWait) == 2);
static_assert(static_cast<int>(Strategy::Park)        == 1);
static_assert(static_cast<int>(Strategy::Block)       == 0);

// WaitLattice surface identity — `leq()` must be reachable through
// the fixy surface for band-3 callers doing chain-ordering checks.
static_assert(WaitLattice::leq(Strategy::Block, Strategy::SpinPause),
    "FIXY-V-084: Block ⊑ SpinPause must hold via fixy::sync::WaitLattice.");
static_assert(!WaitLattice::leq(Strategy::SpinPause, Strategy::Block),
    "FIXY-V-084: SpinPause ⊑ Block must FAIL — chain direction drift.");

// EBO collapse preservation — re-export does NOT inflate the size.
static_assert(sizeof(Wait<Strategy::SpinPause, int>) == sizeof(int),
    "FIXY-V-084: fixy::sync::Wait<SpinPause, int> must EBO-collapse "
    "to sizeof(int).");
static_assert(sizeof(Wait<Strategy::Block, double>) == sizeof(double),
    "FIXY-V-084: fixy::sync::Wait<Block, double> must EBO-collapse.");

// Detector concept reach — IsWait at fixy::sync ≡ safety::extract::IsWait.
static_assert(IsWait<Wait<Strategy::SpinPause, int>>);
static_assert(IsWait<Wait<Strategy::Park, double>>);
static_assert(!IsWait<int>);
static_assert(!IsWait<int*>);

// Projection helpers — wait_value_t / wait_strategy_v.
static_assert(std::is_same_v<wait_value_t<Wait<Strategy::SpinPause, int>>, int>);
static_assert(std::is_same_v<wait_value_t<Wait<Strategy::Park, double>>, double>);
static_assert(wait_strategy_v<Wait<Strategy::SpinPause, int>> == Strategy::SpinPause);
static_assert(wait_strategy_v<Wait<Strategy::Block,     int>> == Strategy::Block);

// Convenience alias identity — wait::SpinPause<T> ≡ Wait<SpinPause, T>.
static_assert(std::is_same_v<wait::SpinPause<int>,   Wait<Strategy::SpinPause,   int>>);
static_assert(std::is_same_v<wait::BoundedSpin<int>, Wait<Strategy::BoundedSpin, int>>);
static_assert(std::is_same_v<wait::UmwaitC01<int>,   Wait<Strategy::UmwaitC01,   int>>);
static_assert(std::is_same_v<wait::AcquireWait<int>, Wait<Strategy::AcquireWait, int>>);
static_assert(std::is_same_v<wait::Park<int>,        Wait<Strategy::Park,        int>>);
static_assert(std::is_same_v<wait::Block<int>,       Wait<Strategy::Block,       int>>);

// satisfies<> subsumption gate — reachable through the fixy surface.
// SpinPause satisfies every weaker consumer; Block satisfies only Block.
static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::SpinPause>);
static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::BoundedSpin>);
static_assert(Wait<Strategy::SpinPause, int>::template satisfies<Strategy::Block>);
static_assert(Wait<Strategy::Block, int>::template satisfies<Strategy::Block>);
static_assert(!Wait<Strategy::Block, int>::template satisfies<Strategy::SpinPause>,
    "FIXY-V-084: Block-tier value MUST NOT satisfy SpinPause — this "
    "is the load-bearing rejection the hot-path discipline depends "
    "on.  If this fires, futex-or-syscall-tier waits could silently "
    "flow through the SpinPause-required gate.");

// Cardinality witness — six WaitStrategy enumerators ship today.  If
// a new tier is added (e.g. WaitStrategy::UmwaitC02) the surface
// reddens here, forcing the conv-alias namespace to be updated in
// lockstep.
static_assert(std::meta::enumerators_of(^^Strategy).size() == 6,
    "FIXY-V-084: WaitStrategy enumerator count drift — fixy::sync::"
    "wait::* convenience-alias namespace must be updated to mirror.");

}  // namespace detail::wait_surface_sentinel

}  // namespace crucible::fixy::sync
