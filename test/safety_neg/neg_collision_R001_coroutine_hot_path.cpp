// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule R001 (FIXY-FOUND-071):
//
//     F::reentrancy_v == ReentrancyMode::Coroutine
//   ∧ marks_hot_path<F>::value == true
//   ⇒ ill-formed
//
// Plain English: a coroutine carries a state-machine frame whose
// suspend/resume sequence costs an indirect call through the coroutine
// handle plus a state-spill at every suspension point.  The hot-path
// budget per CLAUDE.md §IX is ≤40 ns intra-socket; the resume indirect
// call alone is ~15-25 ns and the spill adds I-cache cold-line cost.
// Coroutines belong in Bg / Init contexts where the per-suspend
// overhead is amortized against millisecond-scale latency budgets.
//
// Gap closed by R001 (FIXY-FOUND-071): the Reentrancy axis was
// destructured by Fn but read by ZERO §6.8 collision rules before
// FOUND-071.  Every binding could declare ReentrancyMode::Coroutine
// regardless of execution context, including HotPath-marked bindings
// where the per-suspend cost cliff is fundamentally incompatible.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg
// — the concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "R001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_r001 {

// A Fn with Coroutine reentrancy, no Borrow, no Bg row, but with
// marks_hot_path set externally.  R002 / R003 do not fire (no Borrow,
// no Bg).  R001 alone catches the Coroutine × hot-path contradiction.
// Refinement is left pred::True; H002 would fire on hot-path × trivial
// refinement, so to isolate R001 the binding stays NON-hot-path until
// the file-scope marker specialization below.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage (NOT Borrow → R002 silent)
    fx::Row<>,                                 // 4  EffectRow — empty (R003 silent)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001 won't fire)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::Coroutine,             // 16 Reentrancy — COROUTINE (R001 trigger
                                                //                 once marks_hot_path is on)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_r001

// Mark Bad as hot-path AT FILE SCOPE — composed with Coroutine
// already in the type.  R001 fires on the (Coroutine, marks_hot_path)
// pair.  H002 also fires (hot_path × pred::True is the H002 shape) —
// that is fine for this fixture; the diagnostic still hits a HotPath/
// Reentrancy-family rule, AND the static_assert chain runs ALL
// asserts so R001 appears in the diagnostic regardless of
// first_failure ordering.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_r001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_r001::Bad the_fixture{};

int main() { return 0; }
