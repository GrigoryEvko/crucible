// ── neg_fixy_seam_gaps_013_trust_laundering_federate (FIXY-G14 HS14) ──
//
// Pin gaps_013 seam pattern: Producer = source::External + Consumer =
// source::FromInternal + Channel = Federate composes to trust
// laundering at the seam — federate doesn't sanitize, and the
// consumer's sanitized-only contract is broken silently.
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
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::External, strust::Unverified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

using Consumer = ssf::Fn<int,
    ssf::pred::True, ssf::UsageMode::Linear, fx::Row<>,
    ssf::SecLevel::Classified, ssf::proto::None, ssf::lifetime::Static,
    ssrc::FromInternal, strust::Verified, ssf::ReprKind::Opaque,
    ssf::cost::Unstated, ssf::precision::Exact, ssf::space::Zero,
    ssf::OverflowMode::Trap, ssf::MutationMode::Immutable,
    ssf::ReentrancyMode::NonReentrant, ssf::size_pol::Unstated, 1,
    ssf::stale::Fresh>;

// Sanity: seam matcher flags the composition.
static_assert(ct::seam_matches_v<
    cp::gaps_013_decimal_overflow_wrap,
    Producer, cch::Federate, Consumer>);

// THE DISCIPLINE: any_seam_pattern_matches_v reports true; inverted
// static_assert pins the build-red.
static_assert(!ct::any_seam_pattern_matches_v<
    Producer, cch::Federate, Consumer>,
    "FIXY-G14 fixture: Producer (source::External) + Consumer "
    "(source::FromInternal) over Federate channel must fire the "
    "gaps_013 trust-laundering seam.  Build red on this inverted "
    "predicate is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
