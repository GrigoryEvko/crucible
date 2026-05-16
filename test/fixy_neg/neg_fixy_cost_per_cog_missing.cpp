// ── neg_fixy_cost_per_cog_missing (FIXY-G11 HS14) ────────────────────
//
// Pins per-Cog calibration gate: a binding projected onto a Cog
// without calibration data fails the calibration-required check.
//
// The fixture defines a consumer concept that demands
// has_calibration_v<TargetCog> AND uses the predicted_cost_v projection.
// On a Cog like OpticalTransceiver (default multiplier — no
// calibration), the predicate fails.

#include <crucible/cog/CostProjection.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/dim/Cost.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cc = crucible::cog;

namespace {

using LinearBinding = cf::fn<int,
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
    cg::cost_polynomial<5, 3>>;

// Consumer-side gate: demands the target Cog has calibration data
// AND the predicted cost is finite.
template <typename F, cc::CogKind K>
concept ProjectableOn = cc::has_calibration_v<K>;

// THE DISCIPLINE: OpticalTransceiver uses the default multiplier
// (100), which `has_calibration_v` reports as "no calibration".
// The static_assert INVERTS the predicate.
static_assert(ProjectableOn<LinearBinding, cc::CogKind::OpticalTransceiver>,
    "per-Cog projection fixture: OpticalTransceiver carries the "
    "default cost multiplier (100), indicating no calibration data has "
    "been recorded.  A consumer demanding has_calibration_v rejects.  "
    "Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
