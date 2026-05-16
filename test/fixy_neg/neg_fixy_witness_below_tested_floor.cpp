// ── neg_fixy_witness_below_tested_floor (FIXY-G9 HS14) ───────────────
//
// Pins the WitnessAtLeast concept directly.  An Asserted-witness
// binding fails `FnWitnessAtLeast<F, dim::Reentrancy, Tested<id>>`.
// The assertion below INTENTIONALLY claims the concept admits the
// weaker witness; when the discipline is intact, the assertion fails
// with the embedded "WitnessAtLeast" / "FnWitnessAtLeast" string.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Witness.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace sw = crucible::safety::witness;

namespace {

using AllAssertedFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

// THE DISCIPLINE BEING PINNED: FnWitnessAtLeast rejects Asserted vs
// Tested floor.  The assertion is the INVERSE — claims the concept
// admits the weaker witness.  Build red is the expected outcome; the
// canonical phrase appears in the failure embedding.
static_assert(cf::FnWitnessAtLeast<AllAssertedFn, cd::Reentrancy,
                                    sw::Tested<42>>,
    "FnWitnessAtLeast fixture: Asserted witness must be REJECTED "
    "against a Tested floor.  WitnessAtLeast lattice: Asserted ⊑ "
    "Tested ⊑ CrossValidated ⊑ FormallyVerified.  Build red is the "
    "EXPECTED outcome.");

}  // namespace

int main() { return 0; }
