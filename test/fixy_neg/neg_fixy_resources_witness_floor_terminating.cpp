// ── neg_fixy_resources_witness_floor_terminating (FIXY-G11/G12 FUtb-B) ─
//
// Followup B — `cg::terminating_e<W>`, `cg::bounded_io_e<W, N>`, and
// `cg::loop_bound_e<W, N>` join the evidenced-grant family (G9 witness
// sweep companions to G12).  Production hot-path callers can demand a
// Tested-witness floor on termination claims via FnWitnessAtLeast.
//
// This fixture pins the floor: a binding declaring cg::terminating_e
// with Asserted-witness (tier 1) under a Tested-floor admission gate
// (tier 2) is rejected at compile time.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Witness.h>
#include <crucible/fixy/dim/Termination.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace sw = crucible::safety::witness;

namespace {

// Binding carrying terminating_e with Asserted-witness (the floor).
using TerminatingAsserted = cf::fn<int,
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
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating_e<sw::Asserted<>>,
    cg::bounded_alloc<2048>,
    cg::bounded_io_e<sw::Asserted<>, 0>>;

// Sanity: terminating claim engaged; resources axis extracted; alloc OK.
static_assert(cf::is_terminating_v<TerminatingAsserted>);
static_assert(cf::bounded_io_v<TerminatingAsserted> == 0);
static_assert(cf::bounded_alloc_v<TerminatingAsserted> == 2048);

// THE DISCIPLINE: a hot-path consumer demanding Tested-witness floor
// on the Resources axis rejects an Asserted-witness terminating_e.
// INVERTED static_assert pins the build-red outcome.
static_assert(
    cf::FnWitnessAtLeast<TerminatingAsserted, cd::Resources, sw::Tested<0>>,
    "Followup B fixture: terminating_e<Asserted<>> + bounded_io_e<"
    "Asserted<>, 0> cannot satisfy a Tested-witness floor on Resources.  "
    "Hot-path bindings demand Tested calibration evidence; Asserted-only "
    "termination claims fail the witness-floor gate.  Build red is the "
    "EXPECTED outcome.");

}  // namespace

int main() { return 0; }
