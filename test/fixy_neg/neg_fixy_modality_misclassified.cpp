// ── neg_fixy_modality_misclassified (FIXY-G10 HS14) ──────────────────
//
// Pins grant_traits's modality classification.  A consumer that
// claims `grant::mutable_in_place` has Frame modality (it actually has
// Declares modality — the binding PRODUCES the in-place mutation
// witness) must fail.
//
// The assertion below INTENTIONALLY claims a Declares-modality grant
// has Frame classification.  Build red is expected; the canonical
// ModalityMismatch / ModalityClass:: name appears.

#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Grant.h>

namespace cf = crucible::fixy;
namespace cg = crucible::fixy::grant;

namespace {

// THE DISCIPLINE BEING PINNED: grant_traits classifies
// `mutable_in_place` as ModalityClass::Declares (Comonad).  The
// assertion is the INVERSE.
static_assert(cf::grant_traits<cg::mutable_in_place>::modality_class_v
              == cf::ModalityClass::Frame,
    "ModalityMismatch fixture: grant::mutable_in_place has Declares "
    "modality (Comonad — the binding produces the in-place mutation "
    "witness), NOT Frame.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
