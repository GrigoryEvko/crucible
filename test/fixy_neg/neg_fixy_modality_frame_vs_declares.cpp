// ── neg_fixy_modality_frame_vs_declares (FIXY-G10 HS14) ──────────────
//
// Pins R018 framing.  A consumer that claims grant::reentrant (Frame
// modality) and grant::mutable_in_place (Declares modality) classify
// to the SAME ModalityClass must fail.  These two grants engage
// DIFFERENT dims (Reentrancy vs Mutation), so the fn<> dim-engagement
// gate does NOT catch them — only the modality classifier does.
//
// The assertion below INTENTIONALLY claims both grants share the
// Frame class.  Build red is expected — Declares is the correct
// classification for mutable_in_place; the canonical
// "ModalityMismatch" / "Declares" / "Frame" markers appear.

#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Grant.h>

namespace cf = crucible::fixy;
namespace cg = crucible::fixy::grant;

namespace {

// THE DISCIPLINE BEING PINNED.  mutable_in_place is Declares, NOT
// Frame.  The inverse claim fires the static_assert.
static_assert(
    cf::grant_traits<cg::reentrant>::modality_class_v
        == cf::grant_traits<cg::mutable_in_place>::modality_class_v,
    "ModalityMismatch (R018-shape) fixture: grant::reentrant is Frame "
    "modality; grant::mutable_in_place is Declares modality.  They "
    "are NOT in the same class.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
