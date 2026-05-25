// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule P010 — IO-arm trigger path (FIXY-FOUND-069).
//
// Companion fixtures already shipped:
//   * neg_collision_P010_ghost_runtime.cpp — Alloc-arm
//   * neg_collision_P010_ghost_block.cpp   — Block-arm
//
// This fixture closes the final third arm of P010's 3-way OR-fold:
//
//   concept P010_OK = !(F::usage_v == UsageMode::Ghost &&
//                       (row_has_effect_v<effect_row_t, Effect::Alloc> ||
//                        row_has_effect_v<effect_row_t, Effect::IO>    ||
//                        row_has_effect_v<effect_row_t, Effect::Block>));
//
// The inner conjunct is a 3-way OR-fold over (Alloc, IO, Block).  All
// three arms ship a Ghost-usage Fn whose effect_row engages exactly
// one observable runtime effect.  The arms are structurally distinct:
//
//   * Alloc emits heap-touching code (malloc/new/delete) — Alloc-arm
//   * IO emits syscall / kernel-mediated traffic                — THIS arm
//   * Block emits blocking primitives (mutex/condvar/futex)     — Block-arm
//
// Why all three fixtures matter per HS14:
//
//   * Each arm guards against a refactor dropping THAT specific atom
//     from the inner OR-fold.  Without per-arm witnesses, the OR-fold
//     could silently degenerate to a 2-effect or 1-effect rule and
//     ship.
//   * IO is the most common observable runtime emit (every syscall in
//     production code engages it); a Ghost-usage binding that emits a
//     syscall is the most pernicious erasure violation because it
//     looks "lightweight" at the source but contractually forbidden.
//   * After this fixture lands, P010 graduates from sub-HS14 (partial
//     arm witnessing) to full-HS14 (all-arms witnessed) — bulletproof
//     against any single-arm refactor regression.
//
// Mismatch class: Ghost × Row<IO>.  Same outer Ghost shape as the
// Alloc and Block fixtures, middle 3-way OR arm engaged.  No marker
// specialization needed (P010 is the structural-OR rule; P002 catches
// the marker-driven variant).
//
// Expected diagnostic substring: "P010:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_p010_io {

// A Ghost-usage Fn with Row<IO> — fires P010 via the IO arm of the
// inner 3-way OR.  Outer shape identical to the Alloc and Block
// fixtures; only EffectRow flipped from Alloc/Block to IO.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Ghost,                      // 3  Usage — Ghost
    fx::Row<fx::Effect::IO>,                   // 4  EffectRow — IO atom (the 2nd-OR-arm
                                                //                trigger; observable syscall emit)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_p010_io

[[maybe_unused]] neg_collision_p010_io::Bad the_fixture{};

int main() { return 0; }
