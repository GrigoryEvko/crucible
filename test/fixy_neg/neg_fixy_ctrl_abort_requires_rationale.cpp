// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-244 HS14 fixture 1/3 — the LOAD-BEARING abort discipline.
//
// `grant::ctrl::abort<Rationale>` carries NO default template argument:
// every abort grant MUST declare WHY the site is unrecoverable.  A bare
// `grant::ctrl::abort` (no rationale) is therefore ill-formed — "wrong
// number of template arguments".
//
// This is the central premise of V-244: the ~30 production std::abort /
// crucible_abort sites each map to a `grant::ctrl::abort<"reason">`
// declaration whose rationale is grep-discoverable and folded into the
// federation cache key.  Dropping the rationale-mandatory discipline
// (adding a default) would let an abort site declare the grant without a
// reason, defeating the auditability the grant exists to provide.
//
// Mismatch class for HS14 audit: template-argument ARITY (missing a
// non-defaulted parameter) — distinct from the conversion-failure
// (abort<42>, fixture 2) and the IsGrantTag cv-purity (fixture 3) paths.
//
// Expected diagnostic: a GCC template-argument-count error.

#include <crucible/fixy/grant/Ctrl.h>

namespace ctrl = crucible::fixy::grant::ctrl;

// Missing the mandatory rationale — abort has no default template arg.
using Bad = ctrl::abort<>;

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
