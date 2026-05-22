// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W002 (Phase C, FIXY-V-082):
//
//     row_has_effect<F::effect_row_t, Effect::Bg> == true
//   ∧ F::type_t is safety::Wait<Strategy, U> where Strategy ⊒ BoundedSpin
//   ⇒ ill-formed
//
// Plain English: a Bg-context function MUST NOT wrap its return type
// (or any parameter type) in a Wait wrapper at active-spin strictness
// (BoundedSpin or SpinPause).  Active-spin strategies occupy 100% of
// the hosting core via `_mm_pause` loops.  Bg threads are by contract
// permitted to BLOCK — the kernel should be free to schedule other
// runnable threads onto the core while the Bg wait is pending.  An
// active-spin Bg wait is the back-pressure trap B001 catches one axis
// over: core stays busy, no useful work happens, scheduler is denied.
//
// This fixture uses Wait<SpinPause, int> — the chain-top tier directly
// named by the rule.  Pairs with neg_collision_W002_bg_wait_boundedspin.cpp
// which uses Wait<BoundedSpin, int>.  Both fixtures probe distinct
// lattice positions in the same rejected region; a refactor that
// accidentally narrowed `is_active_spin_v` to ONLY SpinPause (and
// excluded BoundedSpin) would let BoundedSpin slip through silently —
// the paired fixture catches that, this fixture pins the chain top.
//
// Concrete bug-class this catches: a refactor that loosened the W002
// gate — e.g. dropped the `wait_strategy_of` detector OR changed
// `is_active_spin_v` to a permissive predicate — would silently let a
// Bg-row function declare itself wrapped in Wait<SpinPause>, then
// monopolize a core forever on every call.  W002 is the structural
// dual of W001; together they pin the Synchronization-axis discipline
// at both endpoints of the WaitLattice chain.
//
// Expected diagnostic substring: "W002:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Wait.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace fn  = crucible::safety::fn;
namespace fx  = crucible::effects;
namespace sf  = crucible::safety;
using WS = crucible::algebra::lattices::WaitStrategy;

namespace neg_collision_w002_spinpause {

// Bg-row function whose return type is Wait<SpinPause, int> — the W002
// rejected combination.  The Bg effect in the row is what trips the
// rule; W002 fires the moment Fn instantiation reaches the static_assert
// inside CollisionRules::validate().
using Bad = fn::Fn<
    sf::Wait<WS::SpinPause, int>,              // 1  Type — triggers W002
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg ⇒ W002
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
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_w002_spinpause

// Instantiating Bad forces CollisionRules::validate() to fire W002.
[[maybe_unused]] neg_collision_w002_spinpause::Bad the_fixture{};

int main() { return 0; }
