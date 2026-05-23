// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L006 (FIXY-V-243):
//
//     Usage::Linear
//   ∧ F::type_t carries a ControlFlowPinned tier >= MayLongjmp
//   ⇒ ill-formed
//
// Plain English: a Linear resource MUST NOT be held in a scope that may
// longjmp.  longjmp performs a non-local JUMP that SKIPS destructors
// (RAII-unsafe per the ControlFlowLattice doc), so the Linear's release
// never runs — a leak / dangle.  This is the TYPE-LEVEL companion to the
// C++ rule that already rejects `goto` across a destructor scope; L006
// makes the same guarantee at the Fn binding surface.
//
// Mismatch class: Linear × ControlFlow tier >= MayLongjmp.  This is the
// WRAPPER-TIER trigger path (the shipped V-242 ControlFlowPinned carrier
// pins MayLongjmp directly; no marker needed) — distinct from the
// marker-driven C001/D001/D002/G001/S004 fixtures.
//
// Concrete bug-class this catches: a refactor that drops the
// `cf_at_or_above_v<MayLongjmp, type_t>` term from L006_OK would let a
// Linear resource coexist with a longjmp-capable control-flow tier,
// silently re-opening the destructor-skipping leak.
//
// Expected diagnostic substring: "L006:"

#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;
using CF = crucible::algebra::lattices::ControlFlow;

namespace neg_collision_l006 {

// Linear-usage Fn whose return type is ControlFlowPinned<MayLongjmp, int>
// — the L006 rejected combination.  No marker needed: the wrapper tier
// (MayLongjmp) is the toxic value, and Usage::Linear is the second axis.
using Bad = fn::Fn<
    sf::ControlFlowPinned<CF::MayLongjmp, int>,  // 1  Type — CF tier >= MayLongjmp
    fn::pred::True,                              // 2  Refinement
    fn::UsageMode::Linear,                       // 3  Usage — the Linear axis
    fx::Row<>,                                   // 4  EffectRow
    fn::SecLevel::Public,                        // 5  Security
    fn::proto::None,                             // 6  Protocol
    fn::lifetime::Static,                        // 7  Lifetime
    fn::source::FromInternal,                    // 8  Source
    fn::trust::Verified,                         // 9  Trust
    fn::ReprKind::Opaque,                        // 10 Repr
    fn::cost::Constant,                          // 11 Cost
    fn::precision::Exact,                        // 12 Precision
    fn::space::Bounded<sizeof(int)>,             // 13 Space
    fn::OverflowMode::Trap,                      // 14 Overflow
    fn::MutationMode::Immutable,                 // 15 Mutation
    fn::ReentrancyMode::NonReentrant,            // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,            // 17 Size
    /*Version=*/1,                               // 18 Version
    fn::stale::Fresh                             // 19 Staleness
>;

}  // namespace neg_collision_l006

[[maybe_unused]] neg_collision_l006::Bad the_fixture{};

int main() { return 0; }
