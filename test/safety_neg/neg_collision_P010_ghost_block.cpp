// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule P010 — BLOCK-arm trigger path (FIXY-FOUND-069).
//
// Companion fixture to neg_collision_P010_ghost_runtime.cpp (which
// covers the ALLOC-arm trigger).  P010_OK gates on:
//
//   concept P010_OK = !(F::usage_v == UsageMode::Ghost &&
//                       (row_has_effect_v<effect_row_t, Effect::Alloc> ||
//                        row_has_effect_v<effect_row_t, Effect::IO>    ||
//                        row_has_effect_v<effect_row_t, Effect::Block>));
//
// The inner conjunct is a 3-way OR-fold over (Alloc, IO, Block).  The
// shipped fixture exercises the ALLOC arm.  THIS fixture exercises the
// BLOCK arm — the third structurally-distinct observable runtime
// effect that violates Ghost's erasure contract.  Block is the most
// orthogonal sibling: Alloc emits heap calls, IO emits syscalls, Block
// emits blocking primitives (mutex / condvar / futex).  Witnessing
// Block specifically guards against the case where someone refactors
// the inner OR to a 2-effect `(Alloc || IO)` check that "looks
// equivalent" but silently re-admits Ghost × Block.
//
// Why both/all-three fixtures matter per HS14:
//
//   * A refactor that drops `row_has_effect<Block>` from the inner OR
//     breaks the Block-arm path → caught by THIS fixture.
//   * Per the original Alloc fixture, the Alloc-arm has its own
//     witness.  The IO arm remains uncovered today; it's a future
//     sub-HS14 follow-up (see P010 IO-arm task in FOUND-069 sweep).
//
// Mismatch class: Ghost × Row<Block>.  Same outer Ghost shape as the
// Alloc fixture, third 3-way OR arm engaged.  Block-emitting code
// (mutex acquire / condvar wait / blocking syscall) is the most
// "obviously emitted" of the three — its erasure violation is also
// the most observable at runtime if P010 ever silently degenerates.
//
// Expected diagnostic substring: "P010:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_p010_block {

// A Ghost-usage Fn with Row<Block> — fires P010 via the BLOCK arm of
// the inner 3-way OR.  Outer shape identical to the Alloc fixture;
// only EffectRow flipped from Alloc to Block.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Ghost,                      // 3  Usage — Ghost
    fx::Row<fx::Effect::Block>,                // 4  EffectRow — Block atom (the 3rd-OR-arm
                                                //                trigger; observable runtime emit)
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

}  // namespace neg_collision_p010_block

[[maybe_unused]] neg_collision_p010_block::Bad the_fixture{};

int main() { return 0; }
