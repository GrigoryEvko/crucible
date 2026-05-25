// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L007 (FIXY-FOUND-065):
//
//     has_borrow_capture_v<F> (Usage == Borrow, or borrowed carrier, or
//                              marks_borrow_capture engaged)
//   ∧ row_has_effect_v<F::effect_row_t, effects::Effect::Bg>
//   ⇒ ill-formed
//
// Plain English: a borrow-capture function takes a borrowed reference
// whose lifetime is tied to the caller's stack.  A Bg-row function
// executes in background-thread context — and when the background
// thread runs the body, the caller's stack may have unwound, leaving
// the borrow dangling.  Same cross-thread lifetime hazard L003 targets,
// but readable from the EffectRow alone with no marker specialization
// required.
//
// Gap closed by L007 (FIXY-FOUND-065): L002 catches `borrow_capture x
// marks_async` (coroutine/await suspension); L003 catches `borrow_capture
// x marks_unscoped_spawn` (spawn marker).  Both require a marker
// specialization that downstream code must hand-roll on the offending
// Fn (FIXY-FOUND-067 dormant-marker family).  But a borrow_capture
// binding with Row<Bg> in its effect row and NO marker specialization
// slips L002 AND L003 — yet has the same lifetime hazard.  L007 catches
// this directly from the EffectRow.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg — the
// concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "L007:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l007 {

// A Borrow-usage Fn with Row<Bg> — fires L007 cleanly.  No
// marks_async / marks_unscoped_spawn specialization is needed; L007
// reads the effect row directly.  pred::True would normally also
// trip H002 (HotPath × pred::True), but this Fn is NOT marked hot-path,
// so H002 doesn't fire.  H010 (HotPath × Bg) also doesn't fire (no
// hot-path marker).  L007 alone catches the borrow × Bg shape.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Borrow,                     // 3  Usage — Borrow (triggers
                                                //                has_borrow_capture_v)
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg atom (Bg-row)
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
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l007

[[maybe_unused]] neg_collision_l007::Bad the_fixture{};

int main() { return 0; }
