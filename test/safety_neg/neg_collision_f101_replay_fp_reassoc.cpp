// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F101 (FIXY-V-091):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t is FpReassociatePinned<UnrestrictedRewrite, U>
//   ⇒ ill-formed
//
// Plain English: a function declared replay-required MUST NOT wrap its
// type in FpReassociatePinned<UnrestrictedRewrite>.  Algebraic rewrite
// (-fassociative-math equivalent) reorders FP additions; under the same
// source the bit pattern diverges across NVIDIA SASS / AMD CDNA / Intel
// SPR backends — bit-exact replay across the Mimic CI matrix breaks.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-091's load-
// bearing claim is "Replay-required carriers REJECT toxic FP-mode
// values along the FpReassociate sub-axis".  Pairs with
// neg_collision_f104_ct_fp_denormal_honored.cpp for the 2-fixture HS14
// floor — one fixture per distinct mismatch class:
//   1. F101 — Replay axis × FpReassociate sub-axis (this).
//   2. F104 — CT axis × FpDenormalInput sub-axis.
//
// Concrete bug-class this catches: a contributor relaxes F101_OK in
// CollisionCatalog (drops the marks_replay_required term, switches
// the FpReassociate detector to a permissive predicate, or weakens
// the wraps_fp_axis_mode partial spec) — Replay-required carriers
// then silently accept reassociation-permitted FP wrappers, and the
// cross-vendor numerics CI starts failing bit-equality without an
// upstream type-system witness pointing at the offending Fn.  This
// fixture pins F101 at the source-code declaration boundary where
// the per-vendor bit-equality contract begins.
//
// Expected diagnostic substring: "F101:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f101 {

// Replay-required Fn whose Type is FpReassociatePinned<UnrestrictedRewrite,
// float> — the F101 rejected combination.  Marker trait specialization
// at file scope (below) flips marks_replay_required to true; the
// FpReassociate wrapper provides the inner FP-mode pinning the rule
// rejects.
using Bad = fn::Fn<
    sf::FpReassociatePinned<sf::FpReassociate::UnrestrictedRewrite, float>,
                                               // 1  Type — triggers F101
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

}  // namespace neg_collision_f101

// Mark Bad as replay-required — required to fire F101 (rule guards
// marks_replay_required AND FpReassociate<UnrestrictedRewrite>).
// Specialization at file scope, like H001/H002/H003/W001/W002.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::neg_collision_f101::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F101.
[[maybe_unused]] neg_collision_f101::Bad the_fixture{};

int main() { return 0; }
