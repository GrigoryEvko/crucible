// ── neg_fixy_seam_caires_pfenning_2010_delegate_drift_reshard (G14)
//
// Pin caires_pfenning_2010 seam: Producer and Consumer carry DIFFERENT
// protocol_t types (session-typed continuation drift) + Channel =
// Reshard composes to epoch-mismatch at the atomic-region boundary.
// Reshard's atomicity doesn't align with session continuation
// boundary — the delegator's epoch invalidates at reshard, but the
// delegatee still holds the (now stale) continuation.
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

// Distinct protocol tags representing different session continuations.
struct ProtocolA {};
struct ProtocolB {};

using Producer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ProtocolA, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using Consumer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ProtocolB, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

// Sanity: seam matcher flags the composition.
static_assert(ct::seam_matches_v<
    cp::caires_pfenning_2010_implicit_flow,
    Producer, cch::Reshard, Consumer>);

// THE DISCIPLINE: inverted any_seam_pattern_matches_v pins build-red.
static_assert(!ct::any_seam_pattern_matches_v<
    Producer, cch::Reshard, Consumer>,
    "FIXY-G14 fixture: Producer (protocol_t = ProtocolA) + Consumer "
    "(protocol_t = ProtocolB) over Reshard channel must fire the "
    "caires_pfenning_2010 delegate-drift seam.  Build red on this "
    "inverted predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
