// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule M012 — MARKER-tier trigger path (FIXY-FOUND-069
// sub-HS14 closure).
//
// Companion fixture to neg_collision_M012_monotonic_concurrent_no_atomic.cpp
// (which covers the Bg-ROW-tier trigger via `Row<Bg>`).  M012_OK gates on:
//
//   concept M012_OK = !(F::mutation_v == MutationMode::Monotonic &&
//                       concurrent_context_v<F> &&
//                       F::repr_v != ReprKind::Atomic);
//
// `concurrent_context_v<F>` is a 3-way OR-fold over:
//
//   * `has_async_v<F>`                          (ASYNC-tier — async marker)
//   * `row_has_effect_v<effect_row_t, Bg>`      (ROW-tier — Bg effect atom)
//   * `marks_concurrent_context<F>::value`      (MARKER-tier — author
//                                                specializes the trait on
//                                                the offending Fn)
//
// The shipped fixture exercises the ROW-tier arm (Row<Bg>); THIS fixture
// exercises the MARKER-tier arm — the effect row is empty (Row<>), no
// async marker is engaged, and `marks_concurrent_context` is specialized.
// The ASYNC-tier arm (has_async_v) remains a future sub-HS14 follow-up.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `row_has_effect_v<…, Bg>` term from the
//     OR-fold breaks the ROW-tier path → caught by the original fixture.
//   * A refactor that drops the `marks_concurrent_context<F>::value` term
//     breaks the MARKER-tier path → caught by THIS fixture.
//   * Without both, the OR-fold could silently degenerate to a single-arm
//     rule and ship; a Monotonic non-atomic binding that opts into the
//     concurrent-context marker (e.g. a custom executor adapter) would
//     then slip past M012 and race on the monotonic update.
//
// Mismatch class: marks_concurrent_context-engaged Monotonic × non-Atomic
// repr.  Distinct from the Bg-row class because here the concurrency
// signal comes from the trait specialization, not the EffectRow.
// Bg-row arm FALSE (Row<>), async arm FALSE (no marker).  L-series silent
// (Linear usage, no borrow/async/spawn).  H/B-series silent (no hot-path
// marker, cost bounded).  R-series silent (NonReentrant).
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg — the
// concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "M012:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_m012_marker {

// A Monotonic-mutation Fn with NON-atomic repr (Opaque) and an EMPTY
// effect row (Bg-row arm of concurrent_context_v FALSE), no async marker
// (async arm FALSE).  The M012 trigger fires ONLY via the marker arm,
// engaged by the specialization at file scope below.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Linear,                     // 3  Usage — Linear (no L-series)
    fx::Row<>,                                 // 4  EffectRow — EMPTY (Bg arm FALSE)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr — NON-Atomic (M012 repr term TRUE)
    fn::cost::Constant,                        // 11 Cost (bounded — B/H-series silent)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Monotonic,               // 15 Mutation — MONOTONIC (M012 trigger)
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy — non-Coroutine (R-series silent)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_m012_marker

// Opt in to the MARKER-tier trigger.  concurrent_context_v's Bg-row arm
// is FALSE (Row<>) and async arm is FALSE (no marker), so the marker
// alone supplies the concurrency signal.  Combined with Monotonic
// mutation and non-Atomic repr, M012 fires.
namespace crucible::safety::fn::collision {
    template <> struct marks_concurrent_context<::neg_collision_m012_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_m012_marker::Bad the_fixture{};

int main() { return 0; }
