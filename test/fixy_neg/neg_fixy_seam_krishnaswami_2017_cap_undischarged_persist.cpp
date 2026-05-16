// ── neg_fixy_seam_krishnaswami_2017_cap_undischarged_persist (G14 HS14)
//
// Pin krishnaswami_2017 seam: Producer = Effect::IO + Consumer =
// Effect::Block + Channel = Persist composes to capability evaporation
// at the serialization boundary.  Persist doesn't carry runtime caps,
// so the consumer's observed value lacks the cap the producer
// assumed.
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
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::IO>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using Consumer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<fx::Effect::Block>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

// Sanity: seam matcher flags the composition.
static_assert(ct::seam_matches_v<
    cp::krishnaswami_2017_staleness_ct_channel,
    Producer, cch::Persist, Consumer>);

// THE DISCIPLINE: inverted any_seam_pattern_matches_v pins build-red.
static_assert(!ct::any_seam_pattern_matches_v<
    Producer, cch::Persist, Consumer>,
    "FIXY-G14 fixture: Producer (IO cap) + Consumer (Block cap) over "
    "Persist channel must fire the krishnaswami_2017 cap-evaporation "
    "seam.  Build red on this inverted predicate is the EXPECTED "
    "outcome.");

}  // namespace

int main() { return 0; }
