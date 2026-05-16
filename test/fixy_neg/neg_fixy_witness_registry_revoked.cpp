// ── neg_fixy_witness_registry_revoked (Followup C HS14) ──────────────
//
// Pins the new Tier 1 witness-validity layer: a binding with
// `cg::*_e<Tested<revoked_id>>` carries a Tested-tier witness type
// AND a revoked TestRegistry entry, so is_valid_witness_v returns
// false even though WitnessAtLeast<W, Tested<>> is satisfied.
//
// The fixture defines a consumer concept `RequiresValidTestedFloor<W>`
// that demands BOTH Tested-or-stronger AND is_valid_witness_v.  A
// Tested<revoked_demo> witness passes the lattice gate but fails the
// validity gate.  The static_assert fires; build red is the EXPECTED
// outcome.  The matched diagnostic mentions "is_valid_witness" or
// "revoked" or "WitnessValidity".

#include <crucible/safety/diag/TestRegistry.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

namespace sw = crucible::safety::witness;
namespace sd = crucible::safety::diag;

namespace {

// Consumer-side gate: a binding demanding "Tested floor AND valid".
template <typename W>
concept RequiresValidTestedFloor =
    sw::WitnessAtLeast<W, sw::Tested<0>> && sw::is_valid_witness_v<W>;

// A Tested<revoked_demo> witness passes WitnessAtLeast against the
// Tested<> floor (tier 2 ≥ tier 2) but fails is_valid_witness_v
// because TestRegistry<revoked_demo>::status == Revoked.
using RevokedTestedWitness = sw::Tested<sd::id::fixy_revoked_demo>;

static_assert(sw::WitnessAtLeast<RevokedTestedWitness, sw::Tested<0>>,
    "Sanity: RevokedTestedWitness is at the Tested tier — passes the "
    "lattice gate.");

static_assert(!sw::is_valid_witness_v<RevokedTestedWitness>,
    "Sanity: RevokedTestedWitness has a Revoked TestRegistry entry — "
    "is_valid_witness_v reports false.");

// THE DISCIPLINE: a consumer demanding Tested-floor-AND-validity must
// reject a Revoked-tier witness.  The assertion INVERTS the predicate.
static_assert(RequiresValidTestedFloor<RevokedTestedWitness>,
    "Followup C fixture: a Tested-tier witness pointing at a Revoked "
    "TestRegistry entry must NOT satisfy RequiresValidTestedFloor.  "
    "is_valid_witness_v gates admission separately from the lattice "
    "tier check.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
