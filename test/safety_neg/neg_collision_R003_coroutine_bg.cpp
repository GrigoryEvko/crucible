// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule R003 (FIXY-FOUND-071):
//
//     F::reentrancy_v == ReentrancyMode::Coroutine
//   ∧ row_has_effect_v<F::effect_row_t, Effect::Bg> == true
//   ⇒ ill-formed
//
// Plain English: C++ coroutines are NOT thread-safe by default; a
// coroutine suspended on thread A cannot resume on thread B without
// explicit executor handoff.  The coroutine frame's stack-state, TLS-
// tied state, and any captured borrows / atomics tied to the
// suspension thread become hazardous on resumption from a different
// thread.  Symmetric to R002 (Coroutine × Borrow, capture dangling)
// on the cross-thread half — R002 catches the borrow flavor, R003
// catches the executor-migration flavor where the Bg row exposes the
// resumption point to a different thread.
//
// Gap closed by R003 (FIXY-FOUND-071): R001 catches Coroutine ×
// hot_path (cost cliff); R002 catches Coroutine × Borrow (capture
// dangling); R003 closes the Coroutine × Bg-row (cross-thread resume)
// branch.  Together the three R-rules make the Reentrancy axis
// non-trivial — before FOUND-071 Coroutine was destructured but read
// by ZERO §6.8 rules.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg
// — the concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "R003:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_r003 {

// A Fn with Coroutine reentrancy + Row<Bg>, no Borrow, no
// marks_hot_path engaged.  R001 does not fire (no hot_path).  R002
// does not fire (Usage is Linear, not Borrow).  L007 does NOT fire
// (Usage is Linear, not Borrow).  R003 alone catches the
// Coroutine × Bg-row cross-thread-resume hazard.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage (NOT Borrow → R002/L007 silent)
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg engaged (R003 trigger
                                                //                paired with Coroutine)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::Coroutine,             // 16 Reentrancy — COROUTINE (R003 trigger
                                                //                paired with Row<Bg>)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_r003

[[maybe_unused]] neg_collision_r003::Bad the_fixture{};

int main() { return 0; }
