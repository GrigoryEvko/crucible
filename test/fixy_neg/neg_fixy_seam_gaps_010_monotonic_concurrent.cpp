// ── neg_fixy_seam_gaps_010_monotonic_concurrent (FIXY-G14 HS14) ───────
//
// Pin gaps_010 seam pattern: Producer = Monotonic-mutation + Consumer
// = Reentrant + Channel = Identity composes to a monotonic-state race
// invisible per-component but visible at the seam.  mint_flow now
// rejects this composition via its `requires` clause.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

namespace cf = crucible::fixy;
namespace ct = crucible::fixy::theory;
namespace cp = crucible::fixy::theory::pattern;
namespace cch = crucible::fixy::channel;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace fx = crucible::effects;

namespace {

// Monotonic producer.
using Producer = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<fx::Effect::Bg>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cg::repr_atomic,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cg::monotonic_advance,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

// Reentrant consumer.
using Consumer = cf::fn<int,
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
    cg::reentrant,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

// Sanity: seam matcher flags the composition.
static_assert(ct::seam_matches_v<
    cp::gaps_010_monotonic_concurrent_no_atomic,
    typename Producer::underlying_fn_t,
    cch::Identity,
    typename Consumer::underlying_fn_t>);

// THE DISCIPLINE: any_seam_pattern_matches_v reports true; the
// `requires` clause on mint_flow rejects the composition.  Inverted
// static_assert pins the build-red trigger.
static_assert(!ct::any_seam_pattern_matches_v<
    typename Producer::underlying_fn_t,
    cch::Identity,
    typename Consumer::underlying_fn_t>,
    "FIXY-G14 fixture: Producer (Monotonic) + Consumer (Reentrant) "
    "over Identity channel must fire the gaps_010 seam.  Build red "
    "on this inverted predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
