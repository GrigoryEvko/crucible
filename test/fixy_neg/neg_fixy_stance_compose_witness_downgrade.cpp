// ── neg_fixy_stance_compose_witness_downgrade (FIXY-G11/G12 FUtb-C) ───
//
// Followup C — stance::compose<Base, NewGrants...> rejects when a
// NewGrant carries an EVIDENCED witness_t weaker than its modality
// class's floor.
//
// Canonical case: cost_polynomial_e<W, ...> is Coeffect modality whose
// class default witness is Tested<0> (tier 2).  Passing W = Asserted<>
// (tier 1) silently downgrades the binding's posture below the class
// expectation.  Pre-Followup the compose silently accepted; post-
// Followup it rejects at the compose call site.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Stance.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/safety/witness/Witness.h>

namespace cf = crucible::fixy;
namespace cs = crucible::fixy::stance;
namespace cg = crucible::fixy::grant;
namespace sw = crucible::safety::witness;

namespace {

// Named rationale — distinguishes evidenced Asserted<NamedRationale>
// from the bare-grant default Asserted<UnnamedRationale>.  Followup C
// exempts the bare default (so existing call sites stay valid) but
// fires on EVIDENCED downgrades.
struct ExplicitlyAssertedRationale {};

// Sanity: Coeffect class floor is Tested<0> (tier 2).
static_assert(
    sw::witness_tier_v<cf::default_witness_for_class<cf::ModalityClass::Coeffect>>
    == 2);

// Sanity: cost_polynomial_e<Asserted<NamedRationale>, ...> carries
// Asserted (tier 1) with a non-default rationale — NOT equal to
// DefaultWitness, so the consistency check fires.
using DowngradedCost = cg::cost_polynomial_e<
    sw::Asserted<ExplicitlyAssertedRationale>, 100, 50>;
static_assert(sw::witness_tier_v<typename DowngradedCost::witness_t> == 1);

// THE DISCIPLINE: stance::compose now folds the witness-consistency
// check over NewGrants.  Composing BgWorker with DowngradedCost fires
// the static_assert at the compose call site.  sizeof on the composed
// type forces instantiation.
using BadCompose = cs::compose_t<cs::BgWorker, DowngradedCost>;
[[maybe_unused]] constexpr std::size_t kBadComposeSize = sizeof(BadCompose);

}  // namespace

int main() { return 0; }
