// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule W002 (Phase C, FIXY-V-082), BoundedSpin variant:
//
//     row_has_effect<F::effect_row_t, Effect::Bg> == true
//   ∧ F::type_t is safety::Wait<BoundedSpin, U>  (chain second-from-top)
//   ⇒ ill-formed
//
// This fixture probes the BoundedSpin tier of the WaitLattice.
// BoundedSpin ⊑ SpinPause in the chain (ordinal 4 vs 5), so
// `is_active_spin_v<BoundedSpin>` is TRUE — the rule fires.
// BoundedSpin is a deadline-bounded `_mm_pause` loop that falls back
// to `yield`/`Park` after N iterations.  While bounded, the spin
// portion still occupies 100% of the core; for a Bg row whose
// contract permits blocking, this is the same back-pressure trap as
// SpinPause — just with an upper time-bound on the trap duration.
//
// HS14 #2 of 2 for V-082 — pairs with
// neg_collision_W002_bg_wait_spinpause.cpp.  Both probe distinct
// lattice positions in the same rejected region:
//   1. Wait<SpinPause, T>   — the named chain-top "active-spin" tier.
//   2. Wait<BoundedSpin, T> — the second-from-top tier (this).
//
// Even though both fixtures fire the same W002 rule, they cover two
// structurally distinct lattice tiers; a refactor that accidentally
// narrowed `is_active_spin_v` to ONLY SpinPause (and excluded
// BoundedSpin) would silently let BoundedSpin slip through — this
// fixture catches that.
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

namespace neg_collision_w002_boundedspin {

using Bad = fn::Fn<
    sf::Wait<WS::BoundedSpin, int>,            // 1  Type — second-from-top
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

}  // namespace neg_collision_w002_boundedspin

[[maybe_unused]] neg_collision_w002_boundedspin::Bad the_fixture{};

int main() { return 0; }
