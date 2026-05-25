// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F101 (FIXY-FOUND-074):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t is FpReassociatePinned<BoundedTreeDepth, U>
//   ⇒ ill-formed
//
// Plain English: a function declared replay-required MUST NOT wrap its
// type in FpReassociatePinned<BoundedTreeDepth>.  BoundedTreeDepth pins
// only the tree DEPTH (log-N stages); the per-level LANE ASSIGNMENT is
// vendor-specific:
//
//   - NVIDIA: warp-shuffle (lane[i] ↔ lane[i XOR stride]).
//   - AMD:    wavefront permute (lane[i] ↔ lane[i + stride]).
//   - Intel:  SVE2 svadda/svaddv (LHS-vs-RHS contraction order).
//
// All three respect the BoundedTreeDepth contract (same tree depth,
// same number of additions) but produce DIFFERENT bit patterns under
// IEEE 754 — because (a + b) + c ≠ a + (b + c) in floating-point for
// non-trivial operands.  Cross-vendor numerics CI catches the bit
// divergence; F101 catches it at the type-system level, BEFORE the
// kernel reaches CI.
//
// Gap closed by FOUND-074: before this commit F101_OK only rejected
// UnrestrictedRewrite (the obviously-broken case where -fassociative-math
// reorders freely).  BoundedTreeDepth was admitted because its doc
// claimed "canonical topology" — but topology pins DEPTH, not the
// inter-lane operand-binding sequence.  The FOUND-074 fix is to read
// `fp_reassoc_non_strict` (a new detector OR-folding UnrestrictedRewrite
// AND BoundedTreeDepth) at the replay axis — leaving F103 (the CT-axis
// rule) on the narrower `fp_reassoc_unrestricted` because
// BoundedTreeDepth's topology IS data-independent (CT-safe) even though
// it's vendor-divergent (replay-unsafe).
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` whose Type
// triggers F101 — Fn's own `static_assert(ValidComposition<Fn>)` runs
// the validate() leg.  The concept layer ALONE does not gate Fn<>
// instantiation (feedback_collision_catalog_dual_wiring).
//
// Pairs with neg_collision_f101_replay_fp_reassoc.cpp (the
// UnrestrictedRewrite leg).  Two distinct mismatch classes covered:
//   1. Replay × FpReassociate<UnrestrictedRewrite>  (existing fixture)
//   2. Replay × FpReassociate<BoundedTreeDepth>     (this fixture)
//
// Expected diagnostic substring: "F101:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f101_bounded {

// Replay-required Fn whose Type is FpReassociatePinned<BoundedTreeDepth,
// float> — the FOUND-074 rejected combination.  Marker specialization
// (below) flips marks_replay_required to true.  Every other dimension
// avoids tripping any OTHER rule so F101 is the FIRST and only failure.
using Bad = fn::Fn<
    sf::FpReassociatePinned<sf::FpReassociate::BoundedTreeDepth, float>,
                                               // 1  Type — triggers F101
                                               //          (BoundedTreeDepth leg)
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(float)>,         // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(float)>,        // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f101_bounded

// Mark Bad as replay-required — required to fire F101 (rule guards
// marks_replay_required AND FpReassociate non-strict).  Specialization
// at file scope, mirrors the existing F101 UnrestrictedRewrite fixture.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<
        ::neg_collision_f101_bounded::Bad> : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F101.
[[maybe_unused]] neg_collision_f101_bounded::Bad the_fixture{};

int main() { return 0; }
