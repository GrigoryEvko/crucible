// ── neg_fixy_rule_R018_stance_compose_frame_declares (Followup B HS14)
//
// Pins R018 enforcement at the stance::compose call site (Followup B
// adds teeth).  A NewGrants pack containing both a Frame-modality grant
// AND a Declares-modality grant engaging the SAME dim axis is now
// rejected at compose time — BEFORE the final fn<> aggregator runs.
//
// Reentrancy has no shipped Declares-modality grant (cg::reentrant /
// cg::coroutine are both Frame).  This fixture defines a synthetic
// Declares-modality grant on dim::Reentrancy and composes it with
// cg::reentrant via stance::compose.  R018 detects the Frame×Declares
// collision and rejects.
//
// Build red is the EXPECTED outcome.  The matched diagnostic mentions
// "R018" or "Frame.*Declares" or "modality-class collision".

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Stance.h>
#include <crucible/algebra/Modality.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

// Synthetic Declares-modality grant on dim::Reentrancy.  Inherits
// grant_base for IsGrantTag conformance; overrides modality to
// Comonad_t (which classifies as ModalityClass::Declares).
struct synthetic_reentrancy_declares final : cf::grant_base {
    static constexpr cd::DimAxis relaxes = cd::Reentrancy;
    using modality = ::crucible::algebra::modality::Comonad_t;
};

// Trigger the compose-time check.  R018's static_assert on
// frame_declares_consistency_v<NewGrants...> in stance::compose fires
// because cg::reentrant is Frame on dim::Reentrancy AND
// synthetic_reentrancy_declares is Declares on dim::Reentrancy.
using ConflictedStance =
    cf::stance::compose_t<cf::stance::BgWorker,
                          cg::reentrant,
                          synthetic_reentrancy_declares>;

// The static_assert in stance::compose has already failed by this
// point — the alias instantiation IS the trigger.  This main is
// unreachable in a green build path but anchors the fixture to a
// real translation unit.
[[maybe_unused]] ConflictedStance probe{};

}  // namespace

int main() { return 0; }
