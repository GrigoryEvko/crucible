#pragma once

// ── crucible::fixy::sync::MemOrder — value-level mem-order surface ──
//
// FIXY-V-084 (file 2 of 2 in fixy/sync/ family — companion is
// WaitGrant.h).  Surfaces `safety::MemOrder<Tag, T>` (the FOUND-G29
// chain wrapper) at `fixy::sync::MemOrder<Tag, T>` for band-3
// consumers (cntp/, canopy/, cog/, topology/, forge/, mimic/, observe/,
// warden/) per the CRUCIBLE_FIXY_ONLY discipline (FIXY-V-070..072).
//
// THE LOAD-BEARING ROLE: type-fences CLAUDE.md §VI's "never use
// `memory_order_seq_cst` outside narrow exceptions" discipline.  The
// substrate's `MemOrder<Tag, T>` carries the pinned C++ memory-order
// at the value level; this header makes the wrapper reachable through
// the fixy:: umbrella so a band-3 caller writes
//
//   fixy::sync::MemOrder<fixy::sync::Order::AcqRel, int> slot{42};
//
// rather than reaching directly into `safety::MemOrder<>`.  The
// discipline closure for the band-3 "no raw safety::*" rule
// (check-fixy-discipline.sh) is preserved.
//
// ── What this surface IS ──────────────────────────────────────────
//
//  1. `fixy::sync::MemOrder<Tag, T>` — alias for the substrate's
//     `safety::MemOrder<Tag, T>` Graded carrier.
//
//  2. `fixy::sync::Order` — alias for the substrate's `safety::
//     MemOrderTag_v` strong scoped enum.  Five enumerators
//     (Relaxed=4, Acquire=3, Release=2, AcqRel=1, SeqCst=0).
//     Subsumption-up direction: stronger (higher-ordinal) "claims
//     no fence needed" satisfies weaker requirement.
//
//  3. `fixy::sync::IsMemOrder` concept + projection helpers
//     (`mem_order_value_t`, `mem_order_tag_v`) — re-exports of the
//     FOUND-D27 detector surface from `safety::extract`.
//
//  4. `fixy::sync::mem_order::SeqCst<T>` / `AcqRel<T>` /
//     `Release<T>` / `Acquire<T>` / `Relaxed<T>` — convenience
//     aliases mirroring the substrate's `safety::mem_order::*`
//     namespace.
//
// ── What this surface IS NOT ──────────────────────────────────────
//
//  * NOT a new mint factory.  `MemOrder<Tag, T>` has a public
//    explicit T-ctor at the substrate; no Ctx-bound gate is
//    meaningful (the pin is per-value, not per-binding).  HS14
//    fixtures pin the substrate's `relax<>()` strict-up-rejection
//    through the fixy surface — surface integrity, not mint gating.
//
//  * NOT a fixy::grant tag.  Synchronization is a wrapper-only axis
//    per `safety/Fn.h` DimensionAxis::Synchronization = 20 doc-block — there is NO Fn<...>
//    template-parameter slot for it.  This header does NOT
//    specialize `which_dim`.
//
// ── Memory-order ban discipline ───────────────────────────────────
//
// CLAUDE.md §VI bans seq_cst on hot paths; this surface makes the
// ban type-fenceable.  A band-3 hot-path consumer writes
//
//   void publish(fixy::sync::mem_order::AcqRel<uint64_t> slot);
//
// and a refactor that adds a SeqCst-tier value at the call site
// fails statically through the substrate's `relax<>()` requires-
// clause: relax from SeqCst-pinned UP to a weaker tag is fine, but
// a value pinned at e.g. AcqRel CANNOT be implicitly converted into
// the seq_cst slot, because the wrapper's strategy IS the type.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — fixy::sync::Order is the substrate enum verbatim;
//              cross-tag mismatches reject at the substrate's
//              `relax<>()` requires-clause and `satisfies<>` static
//              predicate.
//   ThreadSafe — THE LOAD-BEARING AXIS.  CLAUDE.md §VI's seq_cst ban
//                becomes reachable through the fixy umbrella.
//   InitSafe / MemSafe / NullSafe / DetSafe — inherited from the
//   underlying `safety::MemOrder<Tag, T>` Graded carrier.
//
// ── Runtime cost ──────────────────────────────────────────────────
//
// Zero.  Every name is a using-alias of the substrate; sentinels
// below witness identity via `std::is_same_v`.  `sizeof(fixy::sync::
// MemOrder<Tag, T>) == sizeof(T)`.
//
// ── References ────────────────────────────────────────────────────
//
// Substrate: safety/MemOrder.h (FOUND-G29), algebra/lattices/
// MemOrderLattice.h (FOUND-G28), safety/IsMemOrder.h (FOUND-D27).
// CLAUDE.md §VI — memory-order ordering discipline + seq_cst ban.
// fixy.md §24.1 — DimensionAxis::Synchronization = 20 axis (wrapper-only).
// 28_04_2026_effects.md §4.3.4 — production-call-site rationale.

#include <crucible/safety/MemOrder.h>
#include <crucible/safety/IsMemOrder.h>

#include <type_traits>

namespace crucible::fixy::sync {

// ─── Order enum + wrapper class re-exports ─────────────────────────
//
// The substrate ships these in `crucible::safety::` — re-export here
// so band-3 callers spell `fixy::sync::*` instead of reaching raw
// safety::*.  Note: WaitGrant.h ships `fixy::sync::Strategy`; this
// header ships the peer `fixy::sync::Order` enum.  No name collision —
// both live in the same `fixy::sync::` namespace and represent the
// distinct value-axes (wait-mechanism vs. memory-order).

using Order = ::crucible::safety::MemOrderTag_v;

template <Order O, typename T>
using MemOrder = ::crucible::safety::MemOrder<O, T>;

// ─── Lattice re-export ─────────────────────────────────────────────
//
// `MemOrderLattice` exposes `leq()` + `bottom()` + `top()` + `name()`
// for band-3 callers doing tag-aware dispatch on the chain ordering.

using MemOrderLattice = ::crucible::safety::MemOrderLattice;

// ─── Detector concept + projections (FOUND-D27 surface) ────────────
//
// IsMemOrder<T> is true iff T (with cv-ref stripped) is a MemOrder<O,
// U> instantiation for some O and U.  mem_order_value_t<T> projects
// to U; mem_order_tag_v<T> projects to O.

template <typename T>
inline constexpr bool is_mem_order_v =
    ::crucible::safety::extract::is_mem_order_v<T>;

template <typename T>
concept IsMemOrder = ::crucible::safety::extract::IsMemOrder<T>;

template <typename T>
    requires is_mem_order_v<T>
using mem_order_value_t =
    ::crucible::safety::extract::mem_order_value_t<T>;

template <typename T>
    requires is_mem_order_v<T>
inline constexpr Order mem_order_tag_v =
    ::crucible::safety::extract::mem_order_tag_v<T>;

// ─── Convenience aliases (mirror safety::mem_order::*) ─────────────
namespace mem_order {
    template <typename T> using Relaxed = MemOrder<Order::Relaxed, T>;
    template <typename T> using Acquire = MemOrder<Order::Acquire, T>;
    template <typename T> using Release = MemOrder<Order::Release, T>;
    template <typename T> using AcqRel  = MemOrder<Order::AcqRel,  T>;
    template <typename T> using SeqCst  = MemOrder<Order::SeqCst,  T>;
}  // namespace mem_order

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Identity witnesses: the fixy surface MUST alias the substrate types
// verbatim.  A substrate refactor that drifts the underlying type
// reddens these cells at the fixy boundary, before band-3 consumers
// notice.

namespace detail::mem_order_surface_sentinel {

// Alias identity — MemOrder<O, T> at fixy::sync IS the substrate type.
static_assert(std::is_same_v<
    ::crucible::fixy::sync::MemOrder<Order::Relaxed, int>,
    ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::Relaxed, int>>,
    "FIXY-V-084: fixy::sync::MemOrder<O, T> must alias "
    "safety::MemOrder<O, T> verbatim.");

static_assert(std::is_same_v<
    ::crucible::fixy::sync::MemOrder<Order::SeqCst, double>,
    ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::SeqCst, double>>,
    "FIXY-V-084: fixy::sync::MemOrder<SeqCst, double> identity drift.");

// Order enum identity — the type ID must be the substrate enum.
static_assert(std::is_same_v<Order, ::crucible::safety::MemOrderTag_v>,
    "FIXY-V-084: fixy::sync::Order must alias safety::MemOrderTag_v.");

// Enumerator value preservation — ordinals are load-bearing for
// `MemOrderLattice::leq()` chain ordering.  Order is reversed from
// Wait: bottom=SeqCst (weakest claim, heaviest fence), top=Relaxed.
static_assert(static_cast<int>(Order::SeqCst)  == 0);
static_assert(static_cast<int>(Order::AcqRel)  == 1);
static_assert(static_cast<int>(Order::Release) == 2);
static_assert(static_cast<int>(Order::Acquire) == 3);
static_assert(static_cast<int>(Order::Relaxed) == 4);

// MemOrderLattice surface identity — `leq()` reachable.
static_assert(MemOrderLattice::leq(Order::SeqCst, Order::Relaxed),
    "FIXY-V-084: SeqCst ⊑ Relaxed must hold via "
    "fixy::sync::MemOrderLattice.");
static_assert(!MemOrderLattice::leq(Order::Relaxed, Order::SeqCst),
    "FIXY-V-084: Relaxed ⊑ SeqCst must FAIL — chain direction drift.");

// EBO collapse preservation — re-export does NOT inflate the size.
static_assert(sizeof(MemOrder<Order::Relaxed, int>) == sizeof(int),
    "FIXY-V-084: fixy::sync::MemOrder<Relaxed, int> must EBO-collapse "
    "to sizeof(int).");
static_assert(sizeof(MemOrder<Order::SeqCst, double>) == sizeof(double),
    "FIXY-V-084: fixy::sync::MemOrder<SeqCst, double> must EBO-collapse.");

// Detector concept reach — IsMemOrder at fixy::sync ≡ safety::extract.
static_assert(IsMemOrder<MemOrder<Order::Relaxed, int>>);
static_assert(IsMemOrder<MemOrder<Order::SeqCst,  double>>);
static_assert(!IsMemOrder<int>);
static_assert(!IsMemOrder<int*>);

// Projection helpers — mem_order_value_t / mem_order_tag_v.
static_assert(std::is_same_v<mem_order_value_t<MemOrder<Order::Relaxed, int>>, int>);
static_assert(std::is_same_v<mem_order_value_t<MemOrder<Order::SeqCst,  double>>, double>);
static_assert(mem_order_tag_v<MemOrder<Order::Relaxed, int>> == Order::Relaxed);
static_assert(mem_order_tag_v<MemOrder<Order::SeqCst,  int>> == Order::SeqCst);

// Convenience alias identity — mem_order::Relaxed<T> ≡ MemOrder<Relaxed, T>.
static_assert(std::is_same_v<mem_order::Relaxed<int>, MemOrder<Order::Relaxed, int>>);
static_assert(std::is_same_v<mem_order::Acquire<int>, MemOrder<Order::Acquire, int>>);
static_assert(std::is_same_v<mem_order::Release<int>, MemOrder<Order::Release, int>>);
static_assert(std::is_same_v<mem_order::AcqRel<int>,  MemOrder<Order::AcqRel,  int>>);
static_assert(std::is_same_v<mem_order::SeqCst<int>,  MemOrder<Order::SeqCst,  int>>);

// satisfies<> subsumption gate — reachable through the fixy surface.
// Relaxed (top) satisfies every weaker consumer; SeqCst satisfies only
// SeqCst (the seq_cst-fenced value carries a total-order dependency
// the relaxed discipline forbids).
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::Relaxed>);
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::Acquire>);
static_assert(MemOrder<Order::Relaxed, int>::template satisfies<Order::SeqCst>);
static_assert(MemOrder<Order::SeqCst, int>::template satisfies<Order::SeqCst>);
static_assert(!MemOrder<Order::SeqCst, int>::template satisfies<Order::Relaxed>,
    "FIXY-V-084: SeqCst-tier value MUST NOT satisfy Relaxed — this "
    "is the load-bearing rejection the hot-path discipline depends "
    "on.  If this fires, seq_cst-fenced values could silently flow "
    "through the Relaxed-required gate and violate the CLAUDE.md §VI "
    "seq_cst ban.");

// Cardinality witness — five MemOrderTag enumerators ship today
// (no Consume — explicit P3475R2 omission per substrate doc-block).
// If a new tier is added the surface reddens here, forcing the
// conv-alias namespace to be updated in lockstep.
static_assert(std::meta::enumerators_of(^^Order).size() == 5,
    "FIXY-V-084: MemOrderTag enumerator count drift — fixy::sync::"
    "mem_order::* convenience-alias namespace must be updated to "
    "mirror.");

}  // namespace detail::mem_order_surface_sentinel

}  // namespace crucible::fixy::sync
