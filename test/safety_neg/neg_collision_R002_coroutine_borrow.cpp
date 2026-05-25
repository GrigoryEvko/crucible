// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule R002 (FIXY-FOUND-071):
//
//     F::reentrancy_v == ReentrancyMode::Coroutine
//   ∧ F::usage_v == UsageMode::Borrow
//   ⇒ ill-formed
//
// Plain English: a Borrow-usage binding takes a borrowed reference
// whose lifetime is tied to the caller's stack; a coroutine suspends
// mid-execution and resumes after the caller's stack frame may have
// unwound (the suspension point hands control back to the coroutine's
// owner, who is free to destroy frames the borrow points into).  On
// resume the borrow dangles.  Symmetric to L002 (Borrow × marks_async)
// and L007 (Borrow × Row<Bg>) on the Reentrancy-axis side — L002
// catches marker-driven async, L007 catches Bg-row exposure, R002
// catches the Coroutine ReentrancyMode form (no async marker, no Bg
// row required, the Coroutine declaration alone says "this body will
// suspend").
//
// Gap closed by R002 (FIXY-FOUND-071): the Reentrancy axis was
// destructured by Fn but read by ZERO §6.8 collision rules before
// FOUND-071.  A binding could declare ReentrancyMode::Coroutine with
// UsageMode::Borrow with neither marks_async nor Row<Bg> engaged, and
// the existing L002/L007 rules would silently pass it.  R002 closes
// the Coroutine-axis half of the borrow-dangling family.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg
// — the concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "R002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_r002 {

// A Fn with Coroutine reentrancy + Borrow usage, no Bg row, no
// marks_async / marks_async / marks_unscoped_spawn engaged.  L002 / L003 /
// L007 do not fire (no async marker, no unscoped spawn, no Bg row).
// R001 does not fire (no marks_hot_path).  R003 does not fire (no Bg
// row).  R002 alone catches the Coroutine × Borrow dangling-on-resume
// hazard.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Borrow,                     // 3  Usage — BORROW (R002 trigger
                                                //                paired with Coroutine)
    fx::Row<>,                                 // 4  EffectRow — empty (L007/R003 silent)
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
    fn::ReentrancyMode::Coroutine,             // 16 Reentrancy — COROUTINE (R002 trigger
                                                //                paired with Borrow)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_r002

[[maybe_unused]] neg_collision_r002::Bad the_fixture{};

int main() { return 0; }
