// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V401 (FIXY-V-268):
//
//     scope_at_or_above_v<Gpu, F::type_t> == true   (device / system scope)
//   ∧ barrier_at_or_above_v<AcqRel, F::type_t> == false (barrier ⊏ AcqRel)
//   ⇒ ill-formed
//
// Plain English: a publication declared at device-or-wider visibility (a
// PTX `.gpu`/`.sys`-scope ScopedFence) guarded by a barrier WEAKER than
// acquire-release widens VISIBILITY but never establishes the two-sided
// ordering cross-CTA / cross-device readers require — a silent weak-memory
// race.  The MemoryScope axis and the BarrierStrength axis must agree.
//
// Mismatch class: SCOPE × STRENGTH sufficiency, read from ONE composed
// F::type_t (the scope detector pierces the outer BarrierGuarded; the
// barrier detector reads it directly).  Distinct from V402, which rejects a
// scope-trunk × host-arch contradiction.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg — the
// concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).  Concrete bug this catches:
// dropping the V401 term from validate()/AllRulesOK would let a device-scope
// value ship under a None/CompilerBarrier fence.
//
// Expected diagnostic substring: "V401:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/ScopedFence.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using MS = crucible::algebra::lattices::MemoryScope;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v401 {
// A Gpu-scope publication guarded only by a None barrier — V401 fires.
using Bad = fn::Fn<sf::BarrierGuarded<BS::None, sf::ScopedFence<MS::Gpu, int>>>;
}  // namespace neg_collision_v401

[[maybe_unused]] neg_collision_v401::Bad the_fixture{};

int main() { return 0; }
