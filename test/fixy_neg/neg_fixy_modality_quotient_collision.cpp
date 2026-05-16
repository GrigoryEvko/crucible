// ── neg_fixy_modality_quotient_collision (FIXY-G10 HS14) ─────────────
//
// Pins the Quotient-modality equivalence-class discipline.  Two
// Quotient grants on the same dim with different representatives
// (e.g., Vendor=NV vs Vendor=AMD via stance compose) cannot coexist.
// The stance compose rejects via dims_all_distinct; the fn<>
// engagement gate only requires ≥1 engagement, so stance compose is
// where the discipline is enforced.
//
// The assertion below INTENTIONALLY composes two conflicting Quotient
// grants in a single stance::compose call.  Build red is expected
// (stance compose's static_assert fires, embedding "engage the same
// dim" — ModalityMismatch / Quotient).

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Stance.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

// Base: AllStrict (every dim strict).
using Base = cf::AllStrictAcceptPack;

// stance::compose<Base, NewGrants...> — adding TWO Quotient grants
// engaging dim::Representation (vendor_nv + vendor_am) is a same-dim
// conflict.  The static_assert in stance::compose fires.
//
// THE DISCIPLINE BEING PINNED.  This type alias instantiation IS the
// fixture's bug — instantiation triggers the static_assert.
using Conflict = cf::stance::compose_t<Base, cg::vendor_nv, cg::vendor_am>;

// Defensive: even if the above somehow compiled, this assertion
// hard-fails so the fixture cannot regress silently to a passing
// build.  The string carries the canonical "ModalityMismatch" /
// "Quotient" markers so the neg-compile driver regex matches.
static_assert(std::is_same_v<Conflict, void>,
    "ModalityMismatch / Quotient fixture: two Quotient-modality "
    "grants on dim::Representation with different representatives "
    "(vendor_nv vs vendor_am) MUST be rejected by stance::compose's "
    "dim-collision check.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
