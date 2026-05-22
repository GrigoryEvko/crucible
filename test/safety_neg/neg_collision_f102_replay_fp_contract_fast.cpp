// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F102 (FIXY-V-091):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t is FpContractPinned<Fast, U>
//   ⇒ ill-formed
//
// Plain English: a function declared replay-required MUST NOT wrap its
// type in FpContractPinned<Fast>.  Cross-statement FMA folding picks
// different contraction boundaries per vendor (NVIDIA Hopper SASS picks
// fp32×fp32+fp32 boundaries one way, AMD CDNA3 picks them another); under
// the same C++ source the bit pattern of the final result diverges
// across the Mimic CI matrix and bit-exact replay breaks.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-091 hardening
// patch — companion to the original F101 + F104 fixture pair.  Per-rule
// neg-compile witnesses now cover all five new rules (F101..F105), so a
// refactor that surgically relaxes any ONE rule's gate (drops the
// `fp_contract_fast` detector, weakens its `wraps_fp_axis_mode` partial
// spec, or removes the `replay_required && fp_contract_fast` term from
// `validate()`) reddens CI with the precise rule's diagnostic.
//
// Pairs WITHIN the F-family with neg_collision_f101_replay_fp_reassoc.cpp
// (same axis, different sub-axis — Replay × Reassoc vs Replay × Contract).
// Together they pin both load-bearing Replay-axis sub-rules.
//
// Concrete bug-class this catches: a contributor decides FpContract<Fast>
// is "obviously safe" (because each individual FMA is bit-exact on the
// CHIP it runs on) and drops the F102 gate, missing that the CROSS-VENDOR
// numerics CI matrix executes the SAME kernel on NV+AMD+Intel and expects
// bit-for-bit equivalence per the recipe's tier.  V-091's F102 is the
// type-system witness that catches this at the Fn signature boundary,
// not at runtime CI when a contributor's PR has already been merged.
//
// Expected diagnostic substring: "F102:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f102 {

// Replay-required Fn whose Type is FpContractPinned<Fast, double> —
// the F102 rejected combination.  Marker trait specialization at file
// scope (below) flips marks_replay_required to true; the FpContract
// wrapper provides the inner FP-mode pinning the rule rejects.  Inner T
// = double to discriminate from F101's float carrier in case both
// fixture TUs land in the same translation-unit footprint during a
// project-wide rebuild — keeps trait specializations independent.
using Bad = fn::Fn<
    sf::FpContractPinned<sf::FpContract::Fast, double>,
                                               // 1  Type — triggers F102
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
    fn::space::Bounded<sizeof(double)>,        // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(double)>,       // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f102

// Mark Bad as replay-required — required to fire F102 (rule guards
// marks_replay_required AND FpContract<Fast>).  Specialization at
// file scope, paralleling F101/F104/F105 fixtures.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::neg_collision_f102::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F102.
[[maybe_unused]] neg_collision_f102::Bad the_fixture{};

int main() { return 0; }
