// ── neg_fixy_seam_krishnaswami_2014_linear_dup_federate_fanout (G14)
//
// Pin krishnaswami_2014 seam: Producer with UsageMode::Capability +
// Channel = Federate composes to linear-resource duplication at the
// fan-out boundary.  Capability requires replay-discharge, which
// federation's multi-cast inherently breaks.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>

namespace ct = crucible::fixy::theory;
namespace cp = crucible::fixy::theory::pattern;
namespace cch = crucible::fixy::channel;
namespace ssf = crucible::safety::fn;
namespace ssrc = crucible::safety::source;
namespace strust = crucible::safety::trust;
namespace fx = crucible::effects;

namespace {

using Producer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Capability, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using Consumer = ssf::Fn<int>;

// Sanity: seam matcher flags the composition.
static_assert(ct::seam_matches_v<
    cp::krishnaswami_2014_capability_replay,
    Producer, cch::Federate, Consumer>);

// THE DISCIPLINE: inverted any_seam_pattern_matches_v pins build-red.
static_assert(!ct::any_seam_pattern_matches_v<
    Producer, cch::Federate, Consumer>,
    "FIXY-G14 fixture: Producer (UsageMode::Capability) over Federate "
    "channel must fire the krishnaswami_2014 linear-fan-out seam.  "
    "Build red on this inverted predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
