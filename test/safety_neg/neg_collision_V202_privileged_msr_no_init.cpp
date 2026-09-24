// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V202 (FIXY-V-260):
//
//     F::type_t carries a Hw tier == PrivilegedMsr
//   ∧ F::effect_row_t does NOT contain Effect::Init
//   ⇒ ill-formed
//
// Plain English: rdmsr/wrmsr/IN/OUT require ring 0 and a Permission
// proof; the HwInstructionLattice doc pins them to one-shot privileged
// Init setup, never the steady state.  A PrivilegedMsr tier on a
// non-Init row is the unguarded-privilege shape.
//
// Mismatch class: PrivilegedMsr Hw tier × row WITHOUT Init.  Pure
// type + row read — fires with NO marker (the default Fn row is
// effects::Row<>, which lacks Init), distinct from the marker-driven
// and hot-path fixtures.
//
// Concrete bug this catches: dropping the row⊇Init term from V202_OK
// would let any function carry a PrivilegedMsr tier outside a privileged
// setup context.
//
// Expected diagnostic substring: "V202:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/Hw.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using HW = crucible::algebra::lattices::HwInstruction;

// Default Fn EffectRow is effects::Row<> — no Init — so V202 fires with
// no further annotation.
[[maybe_unused]] fn::Fn<sf::Hw<HW::PrivilegedMsr, int>> the_fixture{};

int main() { return 0; }
